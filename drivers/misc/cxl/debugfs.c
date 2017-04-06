/*
 * Copyright 2014 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "cxl.h"

static struct dentry *cxl_debugfs;

void cxl_stop_trace(struct cxl *adapter)
{
	int slice;

	/* Stop the trace */
	cxl_p1_write(adapter, CXL_PSL_TRACE, 0x8000000000000017LL);

	/* Stop the slice traces */
	spin_lock(&adapter->afu_list_lock);
	for (slice = 0; slice < adapter->slices; slice++) {
		if (adapter->afu[slice])
			cxl_p1n_write(adapter->afu[slice], CXL_PSL_SLICE_TRACE, 0x8000000000000000LL);
	}
	spin_unlock(&adapter->afu_list_lock);
}

/* Helpers to export CXL mmaped IO registers via debugfs */
static int debugfs_io_u64_get(void *data, u64 *val)
{
	*val = in_be64((u64 __iomem *)data);
	return 0;
}

static int debugfs_io_u64_set(void *data, u64 val)
{
	out_be64((u64 __iomem *)data, val);
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_io_x64, debugfs_io_u64_get, debugfs_io_u64_set,
			 "0x%016llx\n");

static struct dentry *debugfs_create_io_x64(const char *name, umode_t mode,
					    struct dentry *parent, u64 __iomem *value)
{
	return debugfs_create_file_unsafe(name, mode, parent,
					  (void __force *)value, &fops_io_x64);
}

void cxl_debugfs_add_adapter_psl_regs(struct cxl *adapter, struct dentry *dir)
{
	debugfs_create_io_x64("fir1", S_IRUSR, dir, _cxl_p1_addr(adapter, CXL_PSL_FIR1));
	debugfs_create_io_x64("fir2", S_IRUSR, dir, _cxl_p1_addr(adapter, CXL_PSL_FIR2));
	debugfs_create_io_x64("fir_cntl", S_IRUSR, dir, _cxl_p1_addr(adapter, CXL_PSL_FIR_CNTL));
	debugfs_create_io_x64("trace", S_IRUSR | S_IWUSR, dir, _cxl_p1_addr(adapter, CXL_PSL_TRACE));
}

void cxl_debugfs_add_adapter_xsl_regs(struct cxl *adapter, struct dentry *dir)
{
	debugfs_create_io_x64("fec", S_IRUSR, dir, _cxl_p1_addr(adapter, CXL_XSL_FEC));
}

int cxl_debugfs_adapter_add(struct cxl *adapter)
{
	struct dentry *dir;
	char buf[32];

	if (!cxl_debugfs)
		return -ENODEV;

	snprintf(buf, 32, "card%i", adapter->adapter_num);
	dir = debugfs_create_dir(buf, cxl_debugfs);
	if (IS_ERR(dir))
		return PTR_ERR(dir);
	adapter->debugfs = dir;

	debugfs_create_io_x64("err_ivte", S_IRUSR, dir, _cxl_p1_addr(adapter, CXL_PSL_ErrIVTE));

	if (adapter->native->sl_ops->debugfs_add_adapter_sl_regs)
		adapter->native->sl_ops->debugfs_add_adapter_sl_regs(adapter, dir);
	return 0;
}

void cxl_debugfs_adapter_remove(struct cxl *adapter)
{
	debugfs_remove_recursive(adapter->debugfs);
}

void cxl_debugfs_add_afu_psl_regs(struct cxl_afu *afu, struct dentry *dir)
{
	debugfs_create_io_x64("fir", S_IRUSR, dir, _cxl_p1n_addr(afu, CXL_PSL_FIR_SLICE_An));
	debugfs_create_io_x64("serr", S_IRUSR, dir, _cxl_p1n_addr(afu, CXL_PSL_SERR_An));
	debugfs_create_io_x64("afu_debug", S_IRUSR, dir, _cxl_p1n_addr(afu, CXL_AFU_DEBUG_An));
	debugfs_create_io_x64("trace", S_IRUSR | S_IWUSR, dir, _cxl_p1n_addr(afu, CXL_PSL_SLICE_TRACE));
}

int cxl_debugfs_afu_add(struct cxl_afu *afu)
{
	struct dentry *dir;
	char buf[32];

	if (!afu->adapter->debugfs)
		return -ENODEV;

	snprintf(buf, 32, "psl%i.%i", afu->adapter->adapter_num, afu->slice);
	dir = debugfs_create_dir(buf, afu->adapter->debugfs);
	if (IS_ERR(dir))
		return PTR_ERR(dir);
	afu->debugfs = dir;

	debugfs_create_io_x64("sr",         S_IRUSR, dir, _cxl_p1n_addr(afu, CXL_PSL_SR_An));
	debugfs_create_io_x64("dsisr",      S_IRUSR, dir, _cxl_p2n_addr(afu, CXL_PSL_DSISR_An));
	debugfs_create_io_x64("dar",        S_IRUSR, dir, _cxl_p2n_addr(afu, CXL_PSL_DAR_An));
	debugfs_create_io_x64("sstp0",      S_IRUSR, dir, _cxl_p2n_addr(afu, CXL_SSTP0_An));
	debugfs_create_io_x64("sstp1",      S_IRUSR, dir, _cxl_p2n_addr(afu, CXL_SSTP1_An));
	debugfs_create_io_x64("err_status", S_IRUSR, dir, _cxl_p2n_addr(afu, CXL_PSL_ErrStat_An));

	if (afu->adapter->native->sl_ops->debugfs_add_afu_sl_regs)
		afu->adapter->native->sl_ops->debugfs_add_afu_sl_regs(afu, dir);

	return 0;
}

void cxl_debugfs_afu_remove(struct cxl_afu *afu)
{
	debugfs_remove_recursive(afu->debugfs);
}

int __init cxl_debugfs_init(void)
{
	struct dentry *ent;

	if (!cpu_has_feature(CPU_FTR_HVMODE))
		return 0;

	ent = debugfs_create_dir("cxl", NULL);
	if (IS_ERR(ent))
		return PTR_ERR(ent);
	cxl_debugfs = ent;

	return 0;
}

void cxl_debugfs_exit(void)
{
	debugfs_remove_recursive(cxl_debugfs);
}
