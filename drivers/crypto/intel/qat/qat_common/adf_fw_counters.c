// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Intel Corporation */
#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/seq_file.h>
#include <linux/types.h>

#include "adf_accel_devices.h"
#include "adf_admin.h"
#include "adf_common_drv.h"
#include "adf_fw_counters.h"

#define ADF_FW_COUNTERS_MAX_PADDING 16

enum adf_fw_counters_types {
	ADF_FW_REQUESTS,
	ADF_FW_RESPONSES,
	ADF_FW_COUNTERS_COUNT
};

static const char * const adf_fw_counter_names[] = {
	[ADF_FW_REQUESTS] = "Requests",
	[ADF_FW_RESPONSES] = "Responses",
};

static_assert(ARRAY_SIZE(adf_fw_counter_names) == ADF_FW_COUNTERS_COUNT);

struct adf_ae_counters {
	u16 ae;
	u64 values[ADF_FW_COUNTERS_COUNT];
};

struct adf_fw_counters {
	u16 ae_count;
	struct adf_ae_counters ae_counters[] __counted_by(ae_count);
};

static void adf_fw_counters_parse_ae_values(struct adf_ae_counters *ae_counters, u32 ae,
					    u64 req_count, u64 resp_count)
{
	ae_counters->ae = ae;
	ae_counters->values[ADF_FW_REQUESTS] = req_count;
	ae_counters->values[ADF_FW_RESPONSES] = resp_count;
}

static int adf_fw_counters_load_from_device(struct adf_accel_dev *accel_dev,
					    struct adf_fw_counters *fw_counters)
{
	struct adf_hw_device_data *hw_data = GET_HW_DATA(accel_dev);
	unsigned long ae_mask;
	unsigned int i;
	unsigned long ae;

	/* Ignore the admin AEs */
	ae_mask = hw_data->ae_mask & ~hw_data->admin_ae_mask;

	if (hweight_long(ae_mask) > fw_counters->ae_count)
		return -EINVAL;

	i = 0;
	for_each_set_bit(ae, &ae_mask, GET_MAX_ACCELENGINES(accel_dev)) {
		u64 req_count, resp_count;
		int ret;

		ret = adf_get_ae_fw_counters(accel_dev, ae, &req_count, &resp_count);
		if (ret)
			return ret;

		adf_fw_counters_parse_ae_values(&fw_counters->ae_counters[i++], ae,
						req_count, resp_count);
	}

	return 0;
}

static struct adf_fw_counters *adf_fw_counters_allocate(unsigned long ae_count)
{
	struct adf_fw_counters *fw_counters;

	if (unlikely(!ae_count))
		return ERR_PTR(-EINVAL);

	fw_counters = kmalloc(struct_size(fw_counters, ae_counters, ae_count), GFP_KERNEL);
	if (!fw_counters)
		return ERR_PTR(-ENOMEM);

	fw_counters->ae_count = ae_count;

	return fw_counters;
}

/**
 * adf_fw_counters_get() - Return FW counters for the provided device.
 * @accel_dev: Pointer to a QAT acceleration device
 *
 * Allocates and returns a table of counters containing execution statistics
 * for each non-admin AE available through the supplied acceleration device.
 * The caller becomes the owner of such memory and is responsible for
 * the deallocation through a call to kfree().
 *
 * Returns: a pointer to a dynamically allocated struct adf_fw_counters
 *          on success, or a negative value on error.
 */
static struct adf_fw_counters *adf_fw_counters_get(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = GET_HW_DATA(accel_dev);
	struct adf_fw_counters *fw_counters;
	unsigned long ae_count;
	int ret;

	if (!adf_dev_started(accel_dev)) {
		dev_err(&GET_DEV(accel_dev), "QAT Device not started\n");
		return ERR_PTR(-EFAULT);
	}

	/* Ignore the admin AEs */
	ae_count = hweight_long(hw_data->ae_mask & ~hw_data->admin_ae_mask);

	fw_counters = adf_fw_counters_allocate(ae_count);
	if (IS_ERR(fw_counters))
		return fw_counters;

	ret = adf_fw_counters_load_from_device(accel_dev, fw_counters);
	if (ret) {
		kfree(fw_counters);
		dev_err(&GET_DEV(accel_dev),
			"Failed to create QAT fw_counters file table [%d].\n", ret);
		return ERR_PTR(ret);
	}

	return fw_counters;
}

static void *qat_fw_counters_seq_start(struct seq_file *sfile, loff_t *pos)
{
	struct adf_fw_counters *fw_counters = sfile->private;

	if (*pos == 0)
		return SEQ_START_TOKEN;

	if (*pos > fw_counters->ae_count)
		return NULL;

	return &fw_counters->ae_counters[*pos - 1];
}

static void *qat_fw_counters_seq_next(struct seq_file *sfile, void *v, loff_t *pos)
{
	struct adf_fw_counters *fw_counters = sfile->private;

	(*pos)++;

	if (*pos > fw_counters->ae_count)
		return NULL;

	return &fw_counters->ae_counters[*pos - 1];
}

static void qat_fw_counters_seq_stop(struct seq_file *sfile, void *v) {}

static int qat_fw_counters_seq_show(struct seq_file *sfile, void *v)
{
	int i;

	if (v == SEQ_START_TOKEN) {
		seq_puts(sfile, "AE ");
		for (i = 0; i < ADF_FW_COUNTERS_COUNT; ++i)
			seq_printf(sfile, " %*s", ADF_FW_COUNTERS_MAX_PADDING,
				   adf_fw_counter_names[i]);
	} else {
		struct adf_ae_counters *ae_counters = (struct adf_ae_counters *)v;

		seq_printf(sfile, "%2d:", ae_counters->ae);
		for (i = 0; i < ADF_FW_COUNTERS_COUNT; ++i)
			seq_printf(sfile, " %*llu", ADF_FW_COUNTERS_MAX_PADDING,
				   ae_counters->values[i]);
	}
	seq_putc(sfile, '\n');

	return 0;
}

static const struct seq_operations qat_fw_counters_sops = {
	.start = qat_fw_counters_seq_start,
	.next = qat_fw_counters_seq_next,
	.stop = qat_fw_counters_seq_stop,
	.show = qat_fw_counters_seq_show,
};

static int qat_fw_counters_file_open(struct inode *inode, struct file *file)
{
	struct adf_accel_dev *accel_dev = inode->i_private;
	struct seq_file *fw_counters_seq_file;
	struct adf_fw_counters *fw_counters;
	int ret;

	fw_counters = adf_fw_counters_get(accel_dev);
	if (IS_ERR(fw_counters))
		return PTR_ERR(fw_counters);

	ret = seq_open(file, &qat_fw_counters_sops);
	if (unlikely(ret)) {
		kfree(fw_counters);
		return ret;
	}

	fw_counters_seq_file = file->private_data;
	fw_counters_seq_file->private = fw_counters;
	return ret;
}

static int qat_fw_counters_file_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq = file->private_data;

	kfree(seq->private);
	seq->private = NULL;

	return seq_release(inode, file); }

static const struct file_operations qat_fw_counters_fops = {
	.owner = THIS_MODULE,
	.open = qat_fw_counters_file_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = qat_fw_counters_file_release,
};

/**
 * adf_fw_counters_dbgfs_add() - Create a debugfs file containing FW
 * execution counters.
 * @accel_dev:  Pointer to a QAT acceleration device
 *
 * Function creates a file to display a table with statistics for the given
 * QAT acceleration device. The table stores device specific execution values
 * for each AE, such as the number of requests sent to the FW and responses
 * received from the FW.
 *
 * Return: void
 */
void adf_fw_counters_dbgfs_add(struct adf_accel_dev *accel_dev)
{
	accel_dev->fw_cntr_dbgfile = debugfs_create_file("fw_counters", 0400,
							 accel_dev->debugfs_dir,
							 accel_dev,
							 &qat_fw_counters_fops);
}

/**
 * adf_fw_counters_dbgfs_rm() - Remove the debugfs file containing FW counters.
 * @accel_dev:  Pointer to a QAT acceleration device.
 *
 * Function removes the file providing the table of statistics for the given
 * QAT acceleration device.
 *
 * Return: void
 */
void adf_fw_counters_dbgfs_rm(struct adf_accel_dev *accel_dev)
{
	debugfs_remove(accel_dev->fw_cntr_dbgfile);
	accel_dev->fw_cntr_dbgfile = NULL;
}
