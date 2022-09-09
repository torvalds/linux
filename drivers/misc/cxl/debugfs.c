// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2014 IBM Corp.
 */

#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "cxl.h"

static struct dentry *cxl_debugfs;

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

static void debugfs_create_io_x64(const char *name, umode_t mode,
				  struct dentry *parent, u64 __iomem *value)
{
	debugfs_create_file_unsafe(name, mode, parent, (void __force *)value,
				   &fops_io_x64);
}

void cxl_debugfs_add_adapter_regs_psl9(struct cxl *adapter, struct dentry *dir)
{
	debugfs_create_io_x64("fir1", S_IRUSR, dir, _cxl_p1_addr(adapter, CXL_PSL9_FIR1));
	debugfs_create_io_x64("fir_mask", 0400, dir,
			      _cxl_p1_addr(adapter, CXL_PSL9_FIR_MASK));
	debugfs_create_io_x64("fir_cntl", S_IRUSR, dir, _cxl_p1_addr(adapter, CXL_PSL9_FIR_CNTL));
	debugfs_create_io_x64("trace", S_IRUSR | S_IWUSR, dir, _cxl_p1_addr(adapter, CXL_PSL9_TRACECFG));
	debugfs_create_io_x64("debug", 0600, dir,
			      _cxl_p1_addr(adapter, CXL_PSL9_DEBUG));
	debugfs_create_io_x64("xsl-debug", 0600, dir,
			      _cxl_p1_addr(adapter, CXL_XSL9_DBG));
}

void cxl_debugfs_add_adapter_regs_psl8(struct cxl *adapter, struct dentry *dir)
{
	debugfs_create_io_x64("fir1", S_IRUSR, dir, _cxl_p1_addr(adapter, CXL_PSL_FIR1));
	debugfs_create_io_x64("fir2", S_IRUSR, dir, _cxl_p1_addr(adapter, CXL_PSL_FIR2));
	debugfs_create_io_x64("fir_cntl", S_IRUSR, dir, _cxl_p1_addr(adapter, CXL_PSL_FIR_CNTL));
	debugfs_create_io_x64("trace", S_IRUSR | S_IWUSR, dir, _cxl_p1_addr(adapter, CXL_PSL_TRACE));
}

void cxl_debugfs_adapter_add(struct cxl *adapter)
{
	struct dentry *dir;
	char buf[32];

	if (!cxl_debugfs)
		return;

	snprintf(buf, 32, "card%i", adapter->adapter_num);
	dir = debugfs_create_dir(buf, cxl_debugfs);
	adapter->debugfs = dir;

	debugfs_create_io_x64("err_ivte", S_IRUSR, dir, _cxl_p1_addr(adapter, CXL_PSL_ErrIVTE));

	if (adapter->native->sl_ops->debugfs_add_adapter_regs)
		adapter->native->sl_ops->debugfs_add_adapter_regs(adapter, dir);
}

void cxl_debugfs_adapter_remove(struct cxl *adapter)
{
	debugfs_remove_recursive(adapter->debugfs);
}

void cxl_debugfs_add_afu_regs_psl9(struct cxl_afu *afu, struct dentry *dir)
{
	debugfs_create_io_x64("serr", S_IRUSR, dir, _cxl_p1n_addr(afu, CXL_PSL_SERR_An));
}

void cxl_debugfs_add_afu_regs_psl8(struct cxl_afu *afu, struct dentry *dir)
{
	debugfs_create_io_x64("sstp0", S_IRUSR, dir, _cxl_p2n_addr(afu, CXL_SSTP0_An));
	debugfs_create_io_x64("sstp1", S_IRUSR, dir, _cxl_p2n_addr(afu, CXL_SSTP1_An));

	debugfs_create_io_x64("fir", S_IRUSR, dir, _cxl_p1n_addr(afu, CXL_PSL_FIR_SLICE_An));
	debugfs_create_io_x64("serr", S_IRUSR, dir, _cxl_p1n_addr(afu, CXL_PSL_SERR_An));
	debugfs_create_io_x64("afu_debug", S_IRUSR, dir, _cxl_p1n_addr(afu, CXL_AFU_DEBUG_An));
	debugfs_create_io_x64("trace", S_IRUSR | S_IWUSR, dir, _cxl_p1n_addr(afu, CXL_PSL_SLICE_TRACE));
}

void cxl_debugfs_afu_add(struct cxl_afu *afu)
{
	struct dentry *dir;
	char buf[32];

	if (!afu->adapter->debugfs)
		return;

	snprintf(buf, 32, "psl%i.%i", afu->adapter->adapter_num, afu->slice);
	dir = debugfs_create_dir(buf, afu->adapter->debugfs);
	afu->debugfs = dir;

	debugfs_create_io_x64("sr",         S_IRUSR, dir, _cxl_p1n_addr(afu, CXL_PSL_SR_An));
	debugfs_create_io_x64("dsisr",      S_IRUSR, dir, _cxl_p2n_addr(afu, CXL_PSL_DSISR_An));
	debugfs_create_io_x64("dar",        S_IRUSR, dir, _cxl_p2n_addr(afu, CXL_PSL_DAR_An));

	debugfs_create_io_x64("err_status", S_IRUSR, dir, _cxl_p2n_addr(afu, CXL_PSL_ErrStat_An));

	if (afu->adapter->native->sl_ops->debugfs_add_afu_regs)
		afu->adapter->native->sl_ops->debugfs_add_afu_regs(afu, dir);
}

void cxl_debugfs_afu_remove(struct cxl_afu *afu)
{
	debugfs_remove_recursive(afu->debugfs);
}

void __init cxl_debugfs_init(void)
{
	if (!cpu_has_feature(CPU_FTR_HVMODE))
		return;

	cxl_debugfs = debugfs_create_dir("cxl", NULL);
}

void cxl_debugfs_exit(void)
{
	debugfs_remove_recursive(cxl_debugfs);
}
