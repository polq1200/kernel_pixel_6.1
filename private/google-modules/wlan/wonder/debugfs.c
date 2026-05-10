// SPDX-License-Identifier: GPL-2.0
/*
 * Google Wonder WiFi Virtual Soft-MAC Driver
 *
 * Debugfs implementation for the Wonder driver.
 */
#define LOG_MODULE_NAME "debugfs"

#include <linux/debugfs.h>
#include <linux/netdevice.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>

#include "core.h"
#include "wonder_log.h"
#include "mac80211.h"
#include "wondertap_internal.h"

static struct dentry *wonder_debugfs_root;

static ssize_t wonder_force_stop_tx_write(struct file *file,
					  const char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct wonder_data *wonder = file->private_data;
	bool stop;
	int ret;

	ret = kstrtobool_from_user(user_buf, count, &stop);
	if (ret)
		return ret;

	if (!wonder || !wonder->vdev) {
		wonder_error("vdev not available\n");
		return -ENODEV;
	}

	if (stop) {
		wonder_info("Forcing TX queue stop on %s\n", wonder->vdev->name);
		if (0)
			netif_stop_queue(wonder->vdev);
		else
			wonder->tx_stop = true;
	} else {
		wonder_info("Forcing TX queue wake on %s\n", wonder->vdev->name);
		if (0)
			netif_wake_queue(wonder->vdev);
		else
			wonder->tx_stop = false;
	}

	return count;
}

static const struct file_operations fops_force_stop_tx = {
	.write = wonder_force_stop_tx_write,
	.open = simple_open,
	.owner = THIS_MODULE,
};

static int wonder_channel_status_report_show(struct seq_file *m, void *v)
{
	struct wonder_data *wonder = m->private;
	struct wondertap_data *wondertap = &wonder->wondertap_data;
	struct wondertap_channel_status_report *report;
	u32 num_channels;
	size_t size;
	int ret, i;

	mutex_lock(&wondertap->lock);
	num_channels = wondertap->cached_channel_schedule.channel_list_len;
	mutex_unlock(&wondertap->lock);

	if (num_channels == 0) {
		seq_puts(m, "Channel hopping list is empty.\n");
		return 0;
	}

	size = sizeof(*report) + num_channels * sizeof(struct wondertap_channel_status);
	report = kzalloc(size, GFP_KERNEL);
	if (!report)
		return -ENOMEM;

	report->channel_status_len = num_channels;
	ret = wondertap_get_channel_status_report(wondertap, report);
	if (ret) {
		seq_printf(m, "Failed to get channel status report: %d\n", ret);
		kfree(report);
		return 0;
	}

	seq_printf(m, "Current Hopping Request TSF: 0x%08x\n",
		report->current_channel_hopping_request_tsf);
	seq_printf(m, "Current Channel Index: %u\n", report->current_channel_index);
	seq_printf(m, "Channel Status Length: %u\n", report->channel_status_len);

	for (i = 0; i < report->channel_status_len; i++) {
		struct wondertap_channel_status *status = &report->status[i];
		seq_printf(m, "\n  [%d] Freq: %u MHz\n", i, status->freq);
		seq_printf(m, "      Switch TSF: 0x%08x\n", status->channel_switch_tsf);
		seq_printf(m, "      Start TSF:  0x%08x\n", status->channel_start_tsf);
		seq_printf(m, "      End TSF:    0x%08x\n", status->channel_end_tsf);
		seq_printf(m, "      TX: %u frames, %llu bytes\n", status->tx_frames,
			status->tx_bytes);
		seq_printf(m, "      RX: %u frames, %llu bytes\n", status->rx_frames,
			status->rx_bytes);
	}

	kfree(report);
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(wonder_channel_status_report);

int wonder_debugfs_init(void *wonder)
{
	wonder_debugfs_root = debugfs_create_dir("wonder", NULL);
	if (!wonder_debugfs_root)
		return -ENODEV;

	debugfs_create_file("force_stop_tx", 0200, wonder_debugfs_root,
			    wonder, &fops_force_stop_tx);
	debugfs_create_file("channel_status_report", 0400, wonder_debugfs_root,
			    wonder, &wonder_channel_status_report_fops);
	return 0;
}

void wonder_debugfs_exit(void)
{
	debugfs_remove_recursive(wonder_debugfs_root);
}