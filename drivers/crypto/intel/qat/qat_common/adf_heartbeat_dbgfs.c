// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Intel Corporation */

#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/kstrtox.h>
#include <linux/types.h>
#include "adf_admin.h"
#include "adf_cfg.h"
#include "adf_common_drv.h"
#include "adf_heartbeat.h"
#include "adf_heartbeat_dbgfs.h"

#define HB_OK 0
#define HB_ERROR -1
#define HB_STATUS_MAX_STRLEN 4
#define HB_STATS_MAX_STRLEN 16

static ssize_t adf_hb_stats_read(struct file *file, char __user *user_buffer,
				 size_t count, loff_t *ppos)
{
	char buf[HB_STATS_MAX_STRLEN];
	unsigned int *value;
	int len;

	if (*ppos > 0)
		return 0;

	value = file->private_data;
	len = scnprintf(buf, sizeof(buf), "%u\n", *value);

	return simple_read_from_buffer(user_buffer, count, ppos, buf, len + 1);
}

static const struct file_operations adf_hb_stats_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = adf_hb_stats_read,
};

static ssize_t adf_hb_status_read(struct file *file, char __user *user_buf,
				  size_t count, loff_t *ppos)
{
	enum adf_device_heartbeat_status hb_status;
	char ret_str[HB_STATUS_MAX_STRLEN];
	struct adf_accel_dev *accel_dev;
	int ret_code;
	size_t len;

	if (*ppos > 0)
		return 0;

	accel_dev = file->private_data;
	ret_code = HB_OK;

	adf_heartbeat_status(accel_dev, &hb_status);

	if (hb_status != HB_DEV_ALIVE)
		ret_code = HB_ERROR;

	len = scnprintf(ret_str, sizeof(ret_str), "%d\n", ret_code);

	return simple_read_from_buffer(user_buf, count, ppos, ret_str, len + 1);
}

static const struct file_operations adf_hb_status_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = adf_hb_status_read,
};

static ssize_t adf_hb_cfg_read(struct file *file, char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	char timer_str[ADF_CFG_MAX_VAL_LEN_IN_BYTES];
	struct adf_accel_dev *accel_dev;
	unsigned int timer_ms;
	int len;

	if (*ppos > 0)
		return 0;

	accel_dev = file->private_data;
	timer_ms = accel_dev->heartbeat->hb_timer;
	len = scnprintf(timer_str, sizeof(timer_str), "%u\n", timer_ms);

	return simple_read_from_buffer(user_buf, count, ppos, timer_str,
				       len + 1);
}

static ssize_t adf_hb_cfg_write(struct file *file, const char __user *user_buf,
				size_t count, loff_t *ppos)
{
	char input_str[ADF_CFG_MAX_VAL_LEN_IN_BYTES] = { };
	struct adf_accel_dev *accel_dev;
	int ret, written_chars;
	unsigned int timer_ms;
	u32 ticks;

	accel_dev = file->private_data;
	timer_ms = ADF_CFG_HB_TIMER_DEFAULT_MS;

	/* last byte left as string termination */
	if (count > sizeof(input_str) - 1)
		return -EINVAL;

	written_chars = simple_write_to_buffer(input_str, sizeof(input_str) - 1,
					       ppos, user_buf, count);
	if (written_chars > 0) {
		ret = kstrtouint(input_str, 10, &timer_ms);
		if (ret) {
			dev_err(&GET_DEV(accel_dev),
				"heartbeat_cfg: Invalid value\n");
			return ret;
		}

		if (timer_ms < ADF_CFG_HB_TIMER_MIN_MS) {
			dev_err(&GET_DEV(accel_dev),
				"heartbeat_cfg: Invalid value\n");
			return -EINVAL;
		}

		/*
		 * On 4xxx devices adf_timer is responsible for HB updates and
		 * its period is fixed to 200ms
		 */
		if (accel_dev->timer)
			timer_ms = ADF_CFG_HB_TIMER_MIN_MS;

		ret = adf_heartbeat_save_cfg_param(accel_dev, timer_ms);
		if (ret)
			return ret;

		ret = adf_heartbeat_ms_to_ticks(accel_dev, timer_ms, &ticks);
		if (ret)
			return ret;

		ret = adf_send_admin_hb_timer(accel_dev, ticks);
		if (ret)
			return ret;

		accel_dev->heartbeat->hb_timer = timer_ms;
	}

	return written_chars;
}

static const struct file_operations adf_hb_cfg_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = adf_hb_cfg_read,
	.write = adf_hb_cfg_write,
};

static ssize_t adf_hb_error_inject_write(struct file *file,
					 const char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct adf_accel_dev *accel_dev = file->private_data;
	char buf[3];
	int ret;

	/* last byte left as string termination */
	if (*ppos != 0 || count != 2)
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;
	buf[count] = '\0';

	if (buf[0] != '1')
		return -EINVAL;

	ret = adf_heartbeat_inject_error(accel_dev);
	if (ret) {
		dev_err(&GET_DEV(accel_dev),
			"Heartbeat error injection failed with status %d\n",
			ret);
		return ret;
	}

	dev_info(&GET_DEV(accel_dev), "Heartbeat error injection enabled\n");

	return count;
}

static const struct file_operations adf_hb_error_inject_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = adf_hb_error_inject_write,
};

void adf_heartbeat_dbgfs_add(struct adf_accel_dev *accel_dev)
{
	struct adf_heartbeat *hb = accel_dev->heartbeat;

	if (!hb)
		return;

	hb->dbgfs.base_dir = debugfs_create_dir("heartbeat", accel_dev->debugfs_dir);
	hb->dbgfs.status = debugfs_create_file("status", 0400, hb->dbgfs.base_dir,
					       accel_dev, &adf_hb_status_fops);
	hb->dbgfs.sent = debugfs_create_file("queries_sent", 0400, hb->dbgfs.base_dir,
					     &hb->hb_sent_counter, &adf_hb_stats_fops);
	hb->dbgfs.failed = debugfs_create_file("queries_failed", 0400, hb->dbgfs.base_dir,
					       &hb->hb_failed_counter, &adf_hb_stats_fops);
	hb->dbgfs.cfg = debugfs_create_file("config", 0600, hb->dbgfs.base_dir,
					    accel_dev, &adf_hb_cfg_fops);

	if (IS_ENABLED(CONFIG_CRYPTO_DEV_QAT_ERROR_INJECTION)) {
		struct dentry *inject_error __maybe_unused;

		inject_error = debugfs_create_file("inject_error", 0200,
						   hb->dbgfs.base_dir, accel_dev,
						   &adf_hb_error_inject_fops);
#ifdef CONFIG_CRYPTO_DEV_QAT_ERROR_INJECTION
		hb->dbgfs.inject_error = inject_error;
#endif
	}
}
EXPORT_SYMBOL_GPL(adf_heartbeat_dbgfs_add);

void adf_heartbeat_dbgfs_rm(struct adf_accel_dev *accel_dev)
{
	struct adf_heartbeat *hb = accel_dev->heartbeat;

	if (!hb)
		return;

	debugfs_remove(hb->dbgfs.status);
	hb->dbgfs.status = NULL;
	debugfs_remove(hb->dbgfs.sent);
	hb->dbgfs.sent = NULL;
	debugfs_remove(hb->dbgfs.failed);
	hb->dbgfs.failed = NULL;
	debugfs_remove(hb->dbgfs.cfg);
	hb->dbgfs.cfg = NULL;
#ifdef CONFIG_CRYPTO_DEV_QAT_ERROR_INJECTION
	debugfs_remove(hb->dbgfs.inject_error);
	hb->dbgfs.inject_error = NULL;
#endif
	debugfs_remove(hb->dbgfs.base_dir);
	hb->dbgfs.base_dir = NULL;
}
EXPORT_SYMBOL_GPL(adf_heartbeat_dbgfs_rm);
