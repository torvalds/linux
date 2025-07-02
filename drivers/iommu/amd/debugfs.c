// SPDX-License-Identifier: GPL-2.0
/*
 * AMD IOMMU driver
 *
 * Copyright (C) 2018 Advanced Micro Devices, Inc.
 *
 * Author: Gary R Hook <gary.hook@amd.com>
 */

#include <linux/debugfs.h>
#include <linux/pci.h>

#include "amd_iommu.h"

static struct dentry *amd_iommu_debugfs;

#define	MAX_NAME_LEN	20
#define	OFS_IN_SZ	8

static ssize_t iommu_mmio_write(struct file *filp, const char __user *ubuf,
				size_t cnt, loff_t *ppos)
{
	struct seq_file *m = filp->private_data;
	struct amd_iommu *iommu = m->private;
	int ret;

	iommu->dbg_mmio_offset = -1;

	if (cnt > OFS_IN_SZ)
		return -EINVAL;

	ret = kstrtou32_from_user(ubuf, cnt, 0, &iommu->dbg_mmio_offset);
	if (ret)
		return ret;

	if (iommu->dbg_mmio_offset > iommu->mmio_phys_end - 4) {
		iommu->dbg_mmio_offset = -1;
		return  -EINVAL;
	}

	return cnt;
}

static int iommu_mmio_show(struct seq_file *m, void *unused)
{
	struct amd_iommu *iommu = m->private;
	u64 value;

	if (iommu->dbg_mmio_offset < 0) {
		seq_puts(m, "Please provide mmio register's offset\n");
		return 0;
	}

	value = readq(iommu->mmio_base + iommu->dbg_mmio_offset);
	seq_printf(m, "Offset:0x%x Value:0x%016llx\n", iommu->dbg_mmio_offset, value);

	return 0;
}
DEFINE_SHOW_STORE_ATTRIBUTE(iommu_mmio);

static ssize_t iommu_capability_write(struct file *filp, const char __user *ubuf,
				      size_t cnt, loff_t *ppos)
{
	struct seq_file *m = filp->private_data;
	struct amd_iommu *iommu = m->private;
	int ret;

	iommu->dbg_cap_offset = -1;

	if (cnt > OFS_IN_SZ)
		return -EINVAL;

	ret = kstrtou32_from_user(ubuf, cnt, 0, &iommu->dbg_cap_offset);
	if (ret)
		return ret;

	/* Capability register at offset 0x14 is the last IOMMU capability register. */
	if (iommu->dbg_cap_offset > 0x14) {
		iommu->dbg_cap_offset = -1;
		return -EINVAL;
	}

	return cnt;
}

static int iommu_capability_show(struct seq_file *m, void *unused)
{
	struct amd_iommu *iommu = m->private;
	u32 value;
	int err;

	if (iommu->dbg_cap_offset < 0) {
		seq_puts(m, "Please provide capability register's offset in the range [0x00 - 0x14]\n");
		return 0;
	}

	err = pci_read_config_dword(iommu->dev, iommu->cap_ptr + iommu->dbg_cap_offset, &value);
	if (err) {
		seq_printf(m, "Not able to read capability register at 0x%x\n",
			   iommu->dbg_cap_offset);
		return 0;
	}

	seq_printf(m, "Offset:0x%x Value:0x%08x\n", iommu->dbg_cap_offset, value);

	return 0;
}
DEFINE_SHOW_STORE_ATTRIBUTE(iommu_capability);

void amd_iommu_debugfs_setup(void)
{
	struct amd_iommu *iommu;
	char name[MAX_NAME_LEN + 1];

	amd_iommu_debugfs = debugfs_create_dir("amd", iommu_debugfs_dir);

	for_each_iommu(iommu) {
		iommu->dbg_mmio_offset = -1;
		iommu->dbg_cap_offset = -1;

		snprintf(name, MAX_NAME_LEN, "iommu%02d", iommu->index);
		iommu->debugfs = debugfs_create_dir(name, amd_iommu_debugfs);

		debugfs_create_file("mmio", 0644, iommu->debugfs, iommu,
				    &iommu_mmio_fops);
		debugfs_create_file("capability", 0644, iommu->debugfs, iommu,
				    &iommu_capability_fops);
	}
}
