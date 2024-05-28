// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Intel Corporation */

#include <linux/bitfield.h>
#include <linux/debugfs.h>
#include <linux/kernel.h>

#include "adf_accel_devices.h"
#include "adf_admin.h"
#include "adf_common_drv.h"
#include "adf_cnv_dbgfs.h"
#include "qat_compression.h"

#define CNV_DEBUGFS_FILENAME		"cnv_errors"
#define CNV_MIN_PADDING			16

#define CNV_ERR_INFO_MASK		GENMASK(11, 0)
#define CNV_ERR_TYPE_MASK		GENMASK(15, 12)
#define CNV_SLICE_ERR_SIGN_BIT_INDEX	7
#define CNV_DELTA_ERR_SIGN_BIT_INDEX	11

enum cnv_error_type {
	CNV_ERR_TYPE_NONE,
	CNV_ERR_TYPE_CHECKSUM,
	CNV_ERR_TYPE_DECOMP_PRODUCED_LENGTH,
	CNV_ERR_TYPE_DECOMPRESSION,
	CNV_ERR_TYPE_TRANSLATION,
	CNV_ERR_TYPE_DECOMP_CONSUMED_LENGTH,
	CNV_ERR_TYPE_UNKNOWN,
	CNV_ERR_TYPES_COUNT
};

#define CNV_ERROR_TYPE_GET(latest_err)	\
	min_t(u16, u16_get_bits(latest_err, CNV_ERR_TYPE_MASK), CNV_ERR_TYPE_UNKNOWN)

#define CNV_GET_DELTA_ERR_INFO(latest_error)	\
	sign_extend32(latest_error, CNV_DELTA_ERR_SIGN_BIT_INDEX)

#define CNV_GET_SLICE_ERR_INFO(latest_error)	\
	sign_extend32(latest_error, CNV_SLICE_ERR_SIGN_BIT_INDEX)

#define CNV_GET_DEFAULT_ERR_INFO(latest_error)	\
	u16_get_bits(latest_error, CNV_ERR_INFO_MASK)

enum cnv_fields {
	CNV_ERR_COUNT,
	CNV_LATEST_ERR,
	CNV_FIELDS_COUNT
};

static const char * const cnv_field_names[CNV_FIELDS_COUNT] = {
	[CNV_ERR_COUNT] = "Total Errors",
	[CNV_LATEST_ERR] = "Last Error",
};

static const char * const cnv_error_names[CNV_ERR_TYPES_COUNT] = {
	[CNV_ERR_TYPE_NONE] = "No Error",
	[CNV_ERR_TYPE_CHECKSUM] = "Checksum Error",
	[CNV_ERR_TYPE_DECOMP_PRODUCED_LENGTH] = "Length Error-P",
	[CNV_ERR_TYPE_DECOMPRESSION] = "Decomp Error",
	[CNV_ERR_TYPE_TRANSLATION] = "Xlat Error",
	[CNV_ERR_TYPE_DECOMP_CONSUMED_LENGTH] = "Length Error-C",
	[CNV_ERR_TYPE_UNKNOWN] = "Unknown Error",
};

struct ae_cnv_errors {
	u16 ae;
	u16 err_cnt;
	u16 latest_err;
	bool is_comp_ae;
};

struct cnv_err_stats {
	u16 ae_count;
	struct ae_cnv_errors ae_cnv_errors[];
};

static s16 get_err_info(u8 error_type, u16 latest)
{
	switch (error_type) {
	case CNV_ERR_TYPE_DECOMP_PRODUCED_LENGTH:
	case CNV_ERR_TYPE_DECOMP_CONSUMED_LENGTH:
		return CNV_GET_DELTA_ERR_INFO(latest);
	case CNV_ERR_TYPE_DECOMPRESSION:
	case CNV_ERR_TYPE_TRANSLATION:
		return CNV_GET_SLICE_ERR_INFO(latest);
	default:
		return CNV_GET_DEFAULT_ERR_INFO(latest);
	}
}

static void *qat_cnv_errors_seq_start(struct seq_file *sfile, loff_t *pos)
{
	struct cnv_err_stats *err_stats = sfile->private;

	if (*pos == 0)
		return SEQ_START_TOKEN;

	if (*pos > err_stats->ae_count)
		return NULL;

	return &err_stats->ae_cnv_errors[*pos - 1];
}

static void *qat_cnv_errors_seq_next(struct seq_file *sfile, void *v,
				     loff_t *pos)
{
	struct cnv_err_stats *err_stats = sfile->private;

	(*pos)++;

	if (*pos > err_stats->ae_count)
		return NULL;

	return &err_stats->ae_cnv_errors[*pos - 1];
}

static void qat_cnv_errors_seq_stop(struct seq_file *sfile, void *v)
{
}

static int qat_cnv_errors_seq_show(struct seq_file *sfile, void *v)
{
	struct ae_cnv_errors *ae_errors;
	unsigned int i;
	s16 err_info;
	u8 err_type;

	if (v == SEQ_START_TOKEN) {
		seq_puts(sfile, "AE ");
		for (i = 0; i < CNV_FIELDS_COUNT; ++i)
			seq_printf(sfile, " %*s", CNV_MIN_PADDING,
				   cnv_field_names[i]);
	} else {
		ae_errors = v;

		if (!ae_errors->is_comp_ae)
			return 0;

		err_type = CNV_ERROR_TYPE_GET(ae_errors->latest_err);
		err_info = get_err_info(err_type, ae_errors->latest_err);

		seq_printf(sfile, "%d:", ae_errors->ae);
		seq_printf(sfile, " %*d", CNV_MIN_PADDING, ae_errors->err_cnt);
		seq_printf(sfile, "%*s [%d]", CNV_MIN_PADDING,
			   cnv_error_names[err_type], err_info);
	}
	seq_putc(sfile, '\n');

	return 0;
}

static const struct seq_operations qat_cnv_errors_sops = {
	.start = qat_cnv_errors_seq_start,
	.next = qat_cnv_errors_seq_next,
	.stop = qat_cnv_errors_seq_stop,
	.show = qat_cnv_errors_seq_show,
};

/**
 * cnv_err_stats_alloc() - Get CNV stats for the provided device.
 * @accel_dev: Pointer to a QAT acceleration device
 *
 * Allocates and populates table of CNV errors statistics for each non-admin AE
 * available through the supplied acceleration device. The caller becomes the
 * owner of such memory and is responsible for the deallocation through a call
 * to kfree().
 *
 * Returns: a pointer to a dynamically allocated struct cnv_err_stats on success
 * or a negative value on error.
 */
static struct cnv_err_stats *cnv_err_stats_alloc(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = GET_HW_DATA(accel_dev);
	struct cnv_err_stats *err_stats;
	unsigned long ae_count;
	unsigned long ae_mask;
	size_t err_stats_size;
	unsigned long ae;
	unsigned int i;
	u16 latest_err;
	u16 err_cnt;
	int ret;

	if (!adf_dev_started(accel_dev)) {
		dev_err(&GET_DEV(accel_dev), "QAT Device not started\n");
		return ERR_PTR(-EBUSY);
	}

	/* Ignore the admin AEs */
	ae_mask = hw_data->ae_mask & ~hw_data->admin_ae_mask;
	ae_count = hweight_long(ae_mask);
	if (unlikely(!ae_count))
		return ERR_PTR(-EINVAL);

	err_stats_size = struct_size(err_stats, ae_cnv_errors, ae_count);
	err_stats = kmalloc(err_stats_size, GFP_KERNEL);
	if (!err_stats)
		return ERR_PTR(-ENOMEM);

	err_stats->ae_count = ae_count;

	i = 0;
	for_each_set_bit(ae, &ae_mask, GET_MAX_ACCELENGINES(accel_dev)) {
		ret = adf_get_cnv_stats(accel_dev, ae, &err_cnt, &latest_err);
		if (ret) {
			dev_dbg(&GET_DEV(accel_dev),
				"Failed to get CNV stats for ae %ld, [%d].\n",
				ae, ret);
			err_stats->ae_cnv_errors[i++].is_comp_ae = false;
			continue;
		}
		err_stats->ae_cnv_errors[i].is_comp_ae = true;
		err_stats->ae_cnv_errors[i].latest_err = latest_err;
		err_stats->ae_cnv_errors[i].err_cnt = err_cnt;
		err_stats->ae_cnv_errors[i].ae = ae;
		i++;
	}

	return err_stats;
}

static int qat_cnv_errors_file_open(struct inode *inode, struct file *file)
{
	struct adf_accel_dev *accel_dev = inode->i_private;
	struct seq_file *cnv_errors_seq_file;
	struct cnv_err_stats *cnv_err_stats;
	int ret;

	cnv_err_stats = cnv_err_stats_alloc(accel_dev);
	if (IS_ERR(cnv_err_stats))
		return PTR_ERR(cnv_err_stats);

	ret = seq_open(file, &qat_cnv_errors_sops);
	if (unlikely(ret)) {
		kfree(cnv_err_stats);
		return ret;
	}

	cnv_errors_seq_file = file->private_data;
	cnv_errors_seq_file->private = cnv_err_stats;
	return ret;
}

static int qat_cnv_errors_file_release(struct inode *inode, struct file *file)
{
	struct seq_file *cnv_errors_seq_file = file->private_data;

	kfree(cnv_errors_seq_file->private);
	cnv_errors_seq_file->private = NULL;

	return seq_release(inode, file);
}

static const struct file_operations qat_cnv_fops = {
	.owner = THIS_MODULE,
	.open = qat_cnv_errors_file_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = qat_cnv_errors_file_release,
};

static ssize_t no_comp_file_read(struct file *f, char __user *buf, size_t count,
				 loff_t *pos)
{
	char *file_msg = "No engine configured for comp\n";

	return simple_read_from_buffer(buf, count, pos, file_msg,
				       strlen(file_msg));
}

static const struct file_operations qat_cnv_no_comp_fops = {
	.owner = THIS_MODULE,
	.read = no_comp_file_read,
};

void adf_cnv_dbgfs_add(struct adf_accel_dev *accel_dev)
{
	const struct file_operations *fops;
	void *data;

	if (adf_hw_dev_has_compression(accel_dev)) {
		fops = &qat_cnv_fops;
		data = accel_dev;
	} else {
		fops = &qat_cnv_no_comp_fops;
		data = NULL;
	}

	accel_dev->cnv_dbgfile = debugfs_create_file(CNV_DEBUGFS_FILENAME, 0400,
						     accel_dev->debugfs_dir,
						     data, fops);
}

void adf_cnv_dbgfs_rm(struct adf_accel_dev *accel_dev)
{
	debugfs_remove(accel_dev->cnv_dbgfile);
	accel_dev->cnv_dbgfile = NULL;
}
