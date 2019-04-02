/*
 * Copyright 2014 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/defs.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "cxl.h"

static struct dentry *cxl_defs;

/* Helpers to export CXL mmaped IO registers via defs */
static int defs_io_u64_get(void *data, u64 *val)
{
	*val = in_be64((u64 __iomem *)data);
	return 0;
}

static int defs_io_u64_set(void *data, u64 val)
{
	out_be64((u64 __iomem *)data, val);
	return 0;
}
DEFINE_DEFS_ATTRIBUTE(fops_io_x64, defs_io_u64_get, defs_io_u64_set,
			 "0x%016llx\n");

static struct dentry *defs_create_io_x64(const char *name, umode_t mode,
					    struct dentry *parent, u64 __iomem *value)
{
	return defs_create_file_unsafe(name, mode, parent,
					  (void __force *)value, &fops_io_x64);
}

void cxl_defs_add_adapter_regs_psl9(struct cxl *adapter, struct dentry *dir)
{
	defs_create_io_x64("fir1", S_IRUSR, dir, _cxl_p1_addr(adapter, CXL_PSL9_FIR1));
	defs_create_io_x64("fir_mask", 0400, dir,
			      _cxl_p1_addr(adapter, CXL_PSL9_FIR_MASK));
	defs_create_io_x64("fir_cntl", S_IRUSR, dir, _cxl_p1_addr(adapter, CXL_PSL9_FIR_CNTL));
	defs_create_io_x64("trace", S_IRUSR | S_IWUSR, dir, _cxl_p1_addr(adapter, CXL_PSL9_TRACECFG));
	defs_create_io_x64("de", 0600, dir,
			      _cxl_p1_addr(adapter, CXL_PSL9_DE));
	defs_create_io_x64("xsl-de", 0600, dir,
			      _cxl_p1_addr(adapter, CXL_XSL9_DBG));
}

void cxl_defs_add_adapter_regs_psl8(struct cxl *adapter, struct dentry *dir)
{
	defs_create_io_x64("fir1", S_IRUSR, dir, _cxl_p1_addr(adapter, CXL_PSL_FIR1));
	defs_create_io_x64("fir2", S_IRUSR, dir, _cxl_p1_addr(adapter, CXL_PSL_FIR2));
	defs_create_io_x64("fir_cntl", S_IRUSR, dir, _cxl_p1_addr(adapter, CXL_PSL_FIR_CNTL));
	defs_create_io_x64("trace", S_IRUSR | S_IWUSR, dir, _cxl_p1_addr(adapter, CXL_PSL_TRACE));
}

int cxl_defs_adapter_add(struct cxl *adapter)
{
	struct dentry *dir;
	char buf[32];

	if (!cxl_defs)
		return -ENODEV;

	snprintf(buf, 32, "card%i", adapter->adapter_num);
	dir = defs_create_dir(buf, cxl_defs);
	if (IS_ERR(dir))
		return PTR_ERR(dir);
	adapter->defs = dir;

	defs_create_io_x64("err_ivte", S_IRUSR, dir, _cxl_p1_addr(adapter, CXL_PSL_ErrIVTE));

	if (adapter->native->sl_ops->defs_add_adapter_regs)
		adapter->native->sl_ops->defs_add_adapter_regs(adapter, dir);
	return 0;
}

void cxl_defs_adapter_remove(struct cxl *adapter)
{
	defs_remove_recursive(adapter->defs);
}

void cxl_defs_add_afu_regs_psl9(struct cxl_afu *afu, struct dentry *dir)
{
	defs_create_io_x64("serr", S_IRUSR, dir, _cxl_p1n_addr(afu, CXL_PSL_SERR_An));
}

void cxl_defs_add_afu_regs_psl8(struct cxl_afu *afu, struct dentry *dir)
{
	defs_create_io_x64("sstp0", S_IRUSR, dir, _cxl_p2n_addr(afu, CXL_SSTP0_An));
	defs_create_io_x64("sstp1", S_IRUSR, dir, _cxl_p2n_addr(afu, CXL_SSTP1_An));

	defs_create_io_x64("fir", S_IRUSR, dir, _cxl_p1n_addr(afu, CXL_PSL_FIR_SLICE_An));
	defs_create_io_x64("serr", S_IRUSR, dir, _cxl_p1n_addr(afu, CXL_PSL_SERR_An));
	defs_create_io_x64("afu_de", S_IRUSR, dir, _cxl_p1n_addr(afu, CXL_AFU_DE_An));
	defs_create_io_x64("trace", S_IRUSR | S_IWUSR, dir, _cxl_p1n_addr(afu, CXL_PSL_SLICE_TRACE));
}

int cxl_defs_afu_add(struct cxl_afu *afu)
{
	struct dentry *dir;
	char buf[32];

	if (!afu->adapter->defs)
		return -ENODEV;

	snprintf(buf, 32, "psl%i.%i", afu->adapter->adapter_num, afu->slice);
	dir = defs_create_dir(buf, afu->adapter->defs);
	if (IS_ERR(dir))
		return PTR_ERR(dir);
	afu->defs = dir;

	defs_create_io_x64("sr",         S_IRUSR, dir, _cxl_p1n_addr(afu, CXL_PSL_SR_An));
	defs_create_io_x64("dsisr",      S_IRUSR, dir, _cxl_p2n_addr(afu, CXL_PSL_DSISR_An));
	defs_create_io_x64("dar",        S_IRUSR, dir, _cxl_p2n_addr(afu, CXL_PSL_DAR_An));

	defs_create_io_x64("err_status", S_IRUSR, dir, _cxl_p2n_addr(afu, CXL_PSL_ErrStat_An));

	if (afu->adapter->native->sl_ops->defs_add_afu_regs)
		afu->adapter->native->sl_ops->defs_add_afu_regs(afu, dir);

	return 0;
}

void cxl_defs_afu_remove(struct cxl_afu *afu)
{
	defs_remove_recursive(afu->defs);
}

int __init cxl_defs_init(void)
{
	struct dentry *ent;

	if (!cpu_has_feature(CPU_FTR_HVMODE))
		return 0;

	ent = defs_create_dir("cxl", NULL);
	if (IS_ERR(ent))
		return PTR_ERR(ent);
	cxl_defs = ent;

	return 0;
}

void cxl_defs_exit(void)
{
	defs_remove_recursive(cxl_defs);
}
