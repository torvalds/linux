// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2021 Intel Corporation. All rights rsvd. */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/smp.h>
#include <uapi/linux/idxd.h>
#include <linux/idxd.h>
#include <linux/dmaengine.h>
#include "../../dma/idxd/idxd.h"
#include <linux/debugfs.h>
#include <crypto/internal/acompress.h>
#include "iaa_crypto.h"
#include "iaa_crypto_stats.h"

static atomic64_t total_comp_calls;
static atomic64_t total_decomp_calls;
static atomic64_t total_sw_decomp_calls;
static atomic64_t total_comp_bytes_out;
static atomic64_t total_decomp_bytes_in;
static atomic64_t total_completion_einval_errors;
static atomic64_t total_completion_timeout_errors;
static atomic64_t total_completion_comp_buf_overflow_errors;

static struct dentry *iaa_crypto_debugfs_root;

void update_total_comp_calls(void)
{
	atomic64_inc(&total_comp_calls);
}

void update_total_comp_bytes_out(int n)
{
	atomic64_add(n, &total_comp_bytes_out);
}

void update_total_decomp_calls(void)
{
	atomic64_inc(&total_decomp_calls);
}

void update_total_sw_decomp_calls(void)
{
	atomic64_inc(&total_sw_decomp_calls);
}

void update_total_decomp_bytes_in(int n)
{
	atomic64_add(n, &total_decomp_bytes_in);
}

void update_completion_einval_errs(void)
{
	atomic64_inc(&total_completion_einval_errors);
}

void update_completion_timeout_errs(void)
{
	atomic64_inc(&total_completion_timeout_errors);
}

void update_completion_comp_buf_overflow_errs(void)
{
	atomic64_inc(&total_completion_comp_buf_overflow_errors);
}

void update_wq_comp_calls(struct idxd_wq *idxd_wq)
{
	struct iaa_wq *wq = idxd_wq_get_private(idxd_wq);

	atomic64_inc(&wq->comp_calls);
	atomic64_inc(&wq->iaa_device->comp_calls);
}

void update_wq_comp_bytes(struct idxd_wq *idxd_wq, int n)
{
	struct iaa_wq *wq = idxd_wq_get_private(idxd_wq);

	atomic64_add(n, &wq->comp_bytes);
	atomic64_add(n, &wq->iaa_device->comp_bytes);
}

void update_wq_decomp_calls(struct idxd_wq *idxd_wq)
{
	struct iaa_wq *wq = idxd_wq_get_private(idxd_wq);

	atomic64_inc(&wq->decomp_calls);
	atomic64_inc(&wq->iaa_device->decomp_calls);
}

void update_wq_decomp_bytes(struct idxd_wq *idxd_wq, int n)
{
	struct iaa_wq *wq = idxd_wq_get_private(idxd_wq);

	atomic64_add(n, &wq->decomp_bytes);
	atomic64_add(n, &wq->iaa_device->decomp_bytes);
}

static void reset_iaa_crypto_stats(void)
{
	atomic64_set(&total_comp_calls, 0);
	atomic64_set(&total_decomp_calls, 0);
	atomic64_set(&total_sw_decomp_calls, 0);
	atomic64_set(&total_comp_bytes_out, 0);
	atomic64_set(&total_decomp_bytes_in, 0);
	atomic64_set(&total_completion_einval_errors, 0);
	atomic64_set(&total_completion_timeout_errors, 0);
	atomic64_set(&total_completion_comp_buf_overflow_errors, 0);
}

static void reset_wq_stats(struct iaa_wq *wq)
{
	atomic64_set(&wq->comp_calls, 0);
	atomic64_set(&wq->comp_bytes, 0);
	atomic64_set(&wq->decomp_calls, 0);
	atomic64_set(&wq->decomp_bytes, 0);
}

static void reset_device_stats(struct iaa_device *iaa_device)
{
	struct iaa_wq *iaa_wq;

	atomic64_set(&iaa_device->comp_calls, 0);
	atomic64_set(&iaa_device->comp_bytes, 0);
	atomic64_set(&iaa_device->decomp_calls, 0);
	atomic64_set(&iaa_device->decomp_bytes, 0);

	list_for_each_entry(iaa_wq, &iaa_device->wqs, list)
		reset_wq_stats(iaa_wq);
}

static void wq_show(struct seq_file *m, struct iaa_wq *iaa_wq)
{
	seq_printf(m, "    name: %s\n", iaa_wq->wq->name);
	seq_printf(m, "    comp_calls: %llu\n",
		   atomic64_read(&iaa_wq->comp_calls));
	seq_printf(m, "    comp_bytes: %llu\n",
		   atomic64_read(&iaa_wq->comp_bytes));
	seq_printf(m, "    decomp_calls: %llu\n",
		   atomic64_read(&iaa_wq->decomp_calls));
	seq_printf(m, "    decomp_bytes: %llu\n\n",
		   atomic64_read(&iaa_wq->decomp_bytes));
}

static void device_stats_show(struct seq_file *m, struct iaa_device *iaa_device)
{
	struct iaa_wq *iaa_wq;

	seq_puts(m, "iaa device:\n");
	seq_printf(m, "  id: %d\n", iaa_device->idxd->id);
	seq_printf(m, "  n_wqs: %d\n", iaa_device->n_wq);
	seq_printf(m, "  comp_calls: %llu\n",
		   atomic64_read(&iaa_device->comp_calls));
	seq_printf(m, "  comp_bytes: %llu\n",
		   atomic64_read(&iaa_device->comp_bytes));
	seq_printf(m, "  decomp_calls: %llu\n",
		   atomic64_read(&iaa_device->decomp_calls));
	seq_printf(m, "  decomp_bytes: %llu\n",
		   atomic64_read(&iaa_device->decomp_bytes));
	seq_puts(m, "  wqs:\n");

	list_for_each_entry(iaa_wq, &iaa_device->wqs, list)
		wq_show(m, iaa_wq);
}

static int global_stats_show(struct seq_file *m, void *v)
{
	seq_puts(m, "global stats:\n");
	seq_printf(m, "  total_comp_calls: %llu\n",
		   atomic64_read(&total_comp_calls));
	seq_printf(m, "  total_decomp_calls: %llu\n",
		   atomic64_read(&total_decomp_calls));
	seq_printf(m, "  total_sw_decomp_calls: %llu\n",
		   atomic64_read(&total_sw_decomp_calls));
	seq_printf(m, "  total_comp_bytes_out: %llu\n",
		   atomic64_read(&total_comp_bytes_out));
	seq_printf(m, "  total_decomp_bytes_in: %llu\n",
		   atomic64_read(&total_decomp_bytes_in));
	seq_printf(m, "  total_completion_einval_errors: %llu\n",
		   atomic64_read(&total_completion_einval_errors));
	seq_printf(m, "  total_completion_timeout_errors: %llu\n",
		   atomic64_read(&total_completion_timeout_errors));
	seq_printf(m, "  total_completion_comp_buf_overflow_errors: %llu\n\n",
		   atomic64_read(&total_completion_comp_buf_overflow_errors));

	return 0;
}

static int wq_stats_show(struct seq_file *m, void *v)
{
	struct iaa_device *iaa_device;

	mutex_lock(&iaa_devices_lock);

	list_for_each_entry(iaa_device, &iaa_devices, list)
		device_stats_show(m, iaa_device);

	mutex_unlock(&iaa_devices_lock);

	return 0;
}

static int iaa_crypto_stats_reset(void *data, u64 value)
{
	struct iaa_device *iaa_device;

	reset_iaa_crypto_stats();

	mutex_lock(&iaa_devices_lock);

	list_for_each_entry(iaa_device, &iaa_devices, list)
		reset_device_stats(iaa_device);

	mutex_unlock(&iaa_devices_lock);

	return 0;
}

static int wq_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, wq_stats_show, file);
}

static const struct file_operations wq_stats_fops = {
	.open = wq_stats_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int global_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, global_stats_show, file);
}

static const struct file_operations global_stats_fops = {
	.open = global_stats_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

DEFINE_DEBUGFS_ATTRIBUTE(wq_stats_reset_fops, NULL, iaa_crypto_stats_reset, "%llu\n");

int __init iaa_crypto_debugfs_init(void)
{
	if (!debugfs_initialized())
		return -ENODEV;

	iaa_crypto_debugfs_root = debugfs_create_dir("iaa_crypto", NULL);

	debugfs_create_file("global_stats", 0644, iaa_crypto_debugfs_root, NULL,
			    &global_stats_fops);
	debugfs_create_file("wq_stats", 0644, iaa_crypto_debugfs_root, NULL,
			    &wq_stats_fops);
	debugfs_create_file("stats_reset", 0644, iaa_crypto_debugfs_root, NULL,
			    &wq_stats_reset_fops);

	return 0;
}

void __exit iaa_crypto_debugfs_cleanup(void)
{
	debugfs_remove_recursive(iaa_crypto_debugfs_root);
}

MODULE_LICENSE("GPL");
