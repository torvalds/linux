// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2023 Intel Corporation. */
#define dev_fmt(fmt) "Telemetry debugfs: " fmt

#include <linux/atomic.h>
#include <linux/debugfs.h>
#include <linux/dev_printk.h>
#include <linux/dcache.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/units.h>

#include "adf_accel_devices.h"
#include "adf_telemetry.h"
#include "adf_tl_debugfs.h"

#define TL_VALUE_MIN_PADDING	20
#define TL_KEY_MIN_PADDING	23

static int tl_collect_values_u32(struct adf_telemetry *telemetry,
				 size_t counter_offset, u64 *arr)
{
	unsigned int samples, hb_idx, i;
	u32 *regs_hist_buff;
	u32 counter_val;

	samples = min(telemetry->msg_cnt, telemetry->hbuffs);
	hb_idx = telemetry->hb_num + telemetry->hbuffs - samples;

	mutex_lock(&telemetry->regs_hist_lock);

	for (i = 0; i < samples; i++) {
		regs_hist_buff = telemetry->regs_hist_buff[hb_idx % telemetry->hbuffs];
		counter_val = regs_hist_buff[counter_offset / sizeof(counter_val)];
		arr[i] = counter_val;
		hb_idx++;
	}

	mutex_unlock(&telemetry->regs_hist_lock);

	return samples;
}

static int tl_collect_values_u64(struct adf_telemetry *telemetry,
				 size_t counter_offset, u64 *arr)
{
	unsigned int samples, hb_idx, i;
	u64 *regs_hist_buff;
	u64 counter_val;

	samples = min(telemetry->msg_cnt, telemetry->hbuffs);
	hb_idx = telemetry->hb_num + telemetry->hbuffs - samples;

	mutex_lock(&telemetry->regs_hist_lock);

	for (i = 0; i < samples; i++) {
		regs_hist_buff = telemetry->regs_hist_buff[hb_idx % telemetry->hbuffs];
		counter_val = regs_hist_buff[counter_offset / sizeof(counter_val)];
		arr[i] = counter_val;
		hb_idx++;
	}

	mutex_unlock(&telemetry->regs_hist_lock);

	return samples;
}

/**
 * avg_array() - Return average of values within an array.
 * @array: Array of values.
 * @len: Number of elements.
 *
 * This algorithm computes average of an array without running into overflow.
 *
 * Return: average of values.
 */
#define avg_array(array, len) (				\
{							\
	typeof(&(array)[0]) _array = (array);		\
	__unqual_scalar_typeof(_array[0]) _x = 0;	\
	__unqual_scalar_typeof(_array[0]) _y = 0;	\
	__unqual_scalar_typeof(_array[0]) _a, _b;	\
	typeof(len) _len = (len);			\
	size_t _i;					\
							\
	for (_i = 0; _i < _len; _i++) {			\
		_a = _array[_i];			\
		_b = do_div(_a, _len);			\
		_x += _a;				\
		if (_y >= _len - _b) {			\
			_x++;				\
			_y -= _len - _b;		\
		} else {				\
			_y += _b;			\
		}					\
	}						\
	do_div(_y, _len);				\
	(_x + _y);					\
})

/* Calculation function for simple counter. */
static int tl_calc_count(struct adf_telemetry *telemetry,
			 const struct adf_tl_dbg_counter *ctr,
			 struct adf_tl_dbg_aggr_values *vals)
{
	struct adf_tl_hw_data *tl_data = &GET_TL_DATA(telemetry->accel_dev);
	u64 *hist_vals;
	int sample_cnt;
	int ret = 0;

	hist_vals = kmalloc_array(tl_data->num_hbuff, sizeof(*hist_vals),
				  GFP_KERNEL);
	if (!hist_vals)
		return -ENOMEM;

	memset(vals, 0, sizeof(*vals));
	sample_cnt = tl_collect_values_u32(telemetry, ctr->offset1, hist_vals);
	if (!sample_cnt)
		goto out_free_hist_vals;

	vals->curr = hist_vals[sample_cnt - 1];
	vals->min = min_array(hist_vals, sample_cnt);
	vals->max = max_array(hist_vals, sample_cnt);
	vals->avg = avg_array(hist_vals, sample_cnt);

out_free_hist_vals:
	kfree(hist_vals);
	return ret;
}

/* Convert CPP bus cycles to ns. */
static int tl_cycles_to_ns(struct adf_telemetry *telemetry,
			   const struct adf_tl_dbg_counter *ctr,
			   struct adf_tl_dbg_aggr_values *vals)
{
	struct adf_tl_hw_data *tl_data = &GET_TL_DATA(telemetry->accel_dev);
	u8 cpp_ns_per_cycle = tl_data->cpp_ns_per_cycle;
	int ret;

	ret = tl_calc_count(telemetry, ctr, vals);
	if (ret)
		return ret;

	vals->curr *= cpp_ns_per_cycle;
	vals->min *= cpp_ns_per_cycle;
	vals->max *= cpp_ns_per_cycle;
	vals->avg *= cpp_ns_per_cycle;

	return 0;
}

/*
 * Compute latency cumulative average with division of accumulated value
 * by sample count. Returned value is in ns.
 */
static int tl_lat_acc_avg(struct adf_telemetry *telemetry,
			  const struct adf_tl_dbg_counter *ctr,
			  struct adf_tl_dbg_aggr_values *vals)
{
	struct adf_tl_hw_data *tl_data = &GET_TL_DATA(telemetry->accel_dev);
	u8 cpp_ns_per_cycle = tl_data->cpp_ns_per_cycle;
	u8 num_hbuff = tl_data->num_hbuff;
	int sample_cnt, i;
	u64 *hist_vals;
	u64 *hist_cnt;
	int ret = 0;

	hist_vals = kmalloc_array(num_hbuff, sizeof(*hist_vals), GFP_KERNEL);
	if (!hist_vals)
		return -ENOMEM;

	hist_cnt = kmalloc_array(num_hbuff, sizeof(*hist_cnt), GFP_KERNEL);
	if (!hist_cnt) {
		ret = -ENOMEM;
		goto out_free_hist_vals;
	}

	memset(vals, 0, sizeof(*vals));
	sample_cnt = tl_collect_values_u64(telemetry, ctr->offset1, hist_vals);
	if (!sample_cnt)
		goto out_free_hist_cnt;

	tl_collect_values_u32(telemetry, ctr->offset2, hist_cnt);

	for (i = 0; i < sample_cnt; i++) {
		/* Avoid division by 0 if count is 0. */
		if (hist_cnt[i])
			hist_vals[i] = div_u64(hist_vals[i] * cpp_ns_per_cycle,
					       hist_cnt[i]);
		else
			hist_vals[i] = 0;
	}

	vals->curr = hist_vals[sample_cnt - 1];
	vals->min = min_array(hist_vals, sample_cnt);
	vals->max = max_array(hist_vals, sample_cnt);
	vals->avg = avg_array(hist_vals, sample_cnt);

out_free_hist_cnt:
	kfree(hist_cnt);
out_free_hist_vals:
	kfree(hist_vals);
	return ret;
}

/* Convert HW raw bandwidth units to Mbps. */
static int tl_bw_hw_units_to_mbps(struct adf_telemetry *telemetry,
				  const struct adf_tl_dbg_counter *ctr,
				  struct adf_tl_dbg_aggr_values *vals)
{
	struct adf_tl_hw_data *tl_data = &GET_TL_DATA(telemetry->accel_dev);
	u16 bw_hw_2_bits = tl_data->bw_units_to_bytes * BITS_PER_BYTE;
	u64 *hist_vals;
	int sample_cnt;
	int ret = 0;

	hist_vals = kmalloc_array(tl_data->num_hbuff, sizeof(*hist_vals),
				  GFP_KERNEL);
	if (!hist_vals)
		return -ENOMEM;

	memset(vals, 0, sizeof(*vals));
	sample_cnt = tl_collect_values_u32(telemetry, ctr->offset1, hist_vals);
	if (!sample_cnt)
		goto out_free_hist_vals;

	vals->curr = div_u64(hist_vals[sample_cnt - 1] * bw_hw_2_bits, MEGA);
	vals->min = div_u64(min_array(hist_vals, sample_cnt) * bw_hw_2_bits, MEGA);
	vals->max = div_u64(max_array(hist_vals, sample_cnt) * bw_hw_2_bits, MEGA);
	vals->avg = div_u64(avg_array(hist_vals, sample_cnt) * bw_hw_2_bits, MEGA);

out_free_hist_vals:
	kfree(hist_vals);
	return ret;
}

static void tl_seq_printf_counter(struct adf_telemetry *telemetry,
				  struct seq_file *s, const char *name,
				  struct adf_tl_dbg_aggr_values *vals)
{
	seq_printf(s, "%-*s", TL_KEY_MIN_PADDING, name);
	seq_printf(s, "%*llu", TL_VALUE_MIN_PADDING, vals->curr);
	if (atomic_read(&telemetry->state) > 1) {
		seq_printf(s, "%*llu", TL_VALUE_MIN_PADDING, vals->min);
		seq_printf(s, "%*llu", TL_VALUE_MIN_PADDING, vals->max);
		seq_printf(s, "%*llu", TL_VALUE_MIN_PADDING, vals->avg);
	}
	seq_puts(s, "\n");
}

static int tl_calc_and_print_counter(struct adf_telemetry *telemetry,
				     struct seq_file *s,
				     const struct adf_tl_dbg_counter *ctr,
				     const char *name)
{
	const char *counter_name = name ? name : ctr->name;
	enum adf_tl_counter_type type = ctr->type;
	struct adf_tl_dbg_aggr_values vals;
	int ret;

	switch (type) {
	case ADF_TL_SIMPLE_COUNT:
		ret = tl_calc_count(telemetry, ctr, &vals);
		break;
	case ADF_TL_COUNTER_NS:
		ret = tl_cycles_to_ns(telemetry, ctr, &vals);
		break;
	case ADF_TL_COUNTER_NS_AVG:
		ret = tl_lat_acc_avg(telemetry, ctr, &vals);
		break;
	case ADF_TL_COUNTER_MBPS:
		ret = tl_bw_hw_units_to_mbps(telemetry, ctr, &vals);
		break;
	default:
		return -EINVAL;
	}

	if (ret)
		return ret;

	tl_seq_printf_counter(telemetry, s, counter_name, &vals);

	return 0;
}

static int tl_print_sl_counter(struct adf_telemetry *telemetry,
			       const struct adf_tl_dbg_counter *ctr,
			       struct seq_file *s, u8 cnt_id)
{
	size_t sl_regs_sz = GET_TL_DATA(telemetry->accel_dev).slice_reg_sz;
	struct adf_tl_dbg_counter slice_ctr;
	size_t offset_inc = cnt_id * sl_regs_sz;
	char cnt_name[MAX_COUNT_NAME_SIZE];

	snprintf(cnt_name, MAX_COUNT_NAME_SIZE, "%s%d", ctr->name, cnt_id);
	slice_ctr = *ctr;
	slice_ctr.offset1 += offset_inc;

	return tl_calc_and_print_counter(telemetry, s, &slice_ctr, cnt_name);
}

static int tl_calc_and_print_sl_counters(struct adf_accel_dev *accel_dev,
					 struct seq_file *s, u8 cnt_type, u8 cnt_id)
{
	struct adf_tl_hw_data *tl_data = &GET_TL_DATA(accel_dev);
	struct adf_telemetry *telemetry = accel_dev->telemetry;
	const struct adf_tl_dbg_counter *sl_tl_util_counters;
	const struct adf_tl_dbg_counter *sl_tl_exec_counters;
	const struct adf_tl_dbg_counter *ctr;
	int ret;

	sl_tl_util_counters = tl_data->sl_util_counters;
	sl_tl_exec_counters = tl_data->sl_exec_counters;

	ctr = &sl_tl_util_counters[cnt_type];

	ret = tl_print_sl_counter(telemetry, ctr, s, cnt_id);
	if (ret) {
		dev_notice(&GET_DEV(accel_dev),
			   "invalid slice utilization counter type\n");
		return ret;
	}

	ctr = &sl_tl_exec_counters[cnt_type];

	ret = tl_print_sl_counter(telemetry, ctr, s, cnt_id);
	if (ret) {
		dev_notice(&GET_DEV(accel_dev),
			   "invalid slice execution counter type\n");
		return ret;
	}

	return 0;
}

static void tl_print_msg_cnt(struct seq_file *s, u32 msg_cnt)
{
	seq_printf(s, "%-*s", TL_KEY_MIN_PADDING, SNAPSHOT_CNT_MSG);
	seq_printf(s, "%*u\n", TL_VALUE_MIN_PADDING, msg_cnt);
}

static int tl_print_dev_data(struct adf_accel_dev *accel_dev,
			     struct seq_file *s)
{
	struct adf_tl_hw_data *tl_data = &GET_TL_DATA(accel_dev);
	struct adf_telemetry *telemetry = accel_dev->telemetry;
	const struct adf_tl_dbg_counter *dev_tl_counters;
	u8 num_dev_counters = tl_data->num_dev_counters;
	u8 *sl_cnt = (u8 *)&telemetry->slice_cnt;
	const struct adf_tl_dbg_counter *ctr;
	unsigned int i;
	int ret;
	u8 j;

	if (!atomic_read(&telemetry->state)) {
		dev_info(&GET_DEV(accel_dev), "not enabled\n");
		return -EPERM;
	}

	dev_tl_counters = tl_data->dev_counters;

	tl_print_msg_cnt(s, telemetry->msg_cnt);

	/* Print device level telemetry. */
	for (i = 0; i < num_dev_counters; i++) {
		ctr = &dev_tl_counters[i];
		ret = tl_calc_and_print_counter(telemetry, s, ctr, NULL);
		if (ret) {
			dev_notice(&GET_DEV(accel_dev),
				   "invalid counter type\n");
			return ret;
		}
	}

	/* Print per slice telemetry. */
	for (i = 0; i < ADF_TL_SL_CNT_COUNT; i++) {
		for (j = 0; j < sl_cnt[i]; j++) {
			ret = tl_calc_and_print_sl_counters(accel_dev, s, i, j);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int tl_dev_data_show(struct seq_file *s, void *unused)
{
	struct adf_accel_dev *accel_dev = s->private;

	if (!accel_dev)
		return -EINVAL;

	return tl_print_dev_data(accel_dev, s);
}
DEFINE_SHOW_ATTRIBUTE(tl_dev_data);

static int tl_control_show(struct seq_file *s, void *unused)
{
	struct adf_accel_dev *accel_dev = s->private;

	if (!accel_dev)
		return -EINVAL;

	seq_printf(s, "%d\n", atomic_read(&accel_dev->telemetry->state));

	return 0;
}

static ssize_t tl_control_write(struct file *file, const char __user *userbuf,
				size_t count, loff_t *ppos)
{
	struct seq_file *seq_f = file->private_data;
	struct adf_accel_dev *accel_dev;
	struct adf_telemetry *telemetry;
	struct adf_tl_hw_data *tl_data;
	struct device *dev;
	u32 input;
	int ret;

	accel_dev = seq_f->private;
	if (!accel_dev)
		return -EINVAL;

	tl_data = &GET_TL_DATA(accel_dev);
	telemetry = accel_dev->telemetry;
	dev = &GET_DEV(accel_dev);

	mutex_lock(&telemetry->wr_lock);

	ret = kstrtou32_from_user(userbuf, count, 10, &input);
	if (ret)
		goto unlock_and_exit;

	if (input > tl_data->num_hbuff) {
		dev_info(dev, "invalid control input\n");
		ret = -EINVAL;
		goto unlock_and_exit;
	}

	/* If input is 0, just stop telemetry. */
	if (!input) {
		ret = adf_tl_halt(accel_dev);
		if (!ret)
			ret = count;

		goto unlock_and_exit;
	}

	/* If TL is already enabled, stop it. */
	if (atomic_read(&telemetry->state)) {
		dev_info(dev, "already enabled, restarting.\n");
		ret = adf_tl_halt(accel_dev);
		if (ret)
			goto unlock_and_exit;
	}

	ret = adf_tl_run(accel_dev, input);
	if (ret)
		goto unlock_and_exit;

	ret = count;

unlock_and_exit:
	mutex_unlock(&telemetry->wr_lock);
	return ret;
}
DEFINE_SHOW_STORE_ATTRIBUTE(tl_control);

void adf_tl_dbgfs_add(struct adf_accel_dev *accel_dev)
{
	struct adf_telemetry *telemetry = accel_dev->telemetry;
	struct dentry *parent = accel_dev->debugfs_dir;
	struct dentry *dir;

	if (!telemetry)
		return;

	dir = debugfs_create_dir("telemetry", parent);
	accel_dev->telemetry->dbg_dir = dir;
	debugfs_create_file("device_data", 0444, dir, accel_dev, &tl_dev_data_fops);
	debugfs_create_file("control", 0644, dir, accel_dev, &tl_control_fops);
}

void adf_tl_dbgfs_rm(struct adf_accel_dev *accel_dev)
{
	struct adf_telemetry *telemetry = accel_dev->telemetry;
	struct dentry *dbg_dir;

	if (!telemetry)
		return;

	dbg_dir = telemetry->dbg_dir;

	debugfs_remove_recursive(dbg_dir);

	if (atomic_read(&telemetry->state))
		adf_tl_halt(accel_dev);
}
