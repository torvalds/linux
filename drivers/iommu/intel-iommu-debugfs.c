// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright © 2018 Intel Corporation.
 *
 * Authors: Gayatri Kammela <gayatri.kammela@intel.com>
 *	    Sohil Mehta <sohil.mehta@intel.com>
 *	    Jacob Pan <jacob.jun.pan@linux.intel.com>
 */

#include <linux/debugfs.h>
#include <linux/dmar.h>
#include <linux/intel-iommu.h>
#include <linux/pci.h>

#include <asm/irq_remapping.h>

struct iommu_regset {
	int offset;
	const char *regs;
};

#define IOMMU_REGSET_ENTRY(_reg_)					\
	{ DMAR_##_reg_##_REG, __stringify(_reg_) }
static const struct iommu_regset iommu_regs[] = {
	IOMMU_REGSET_ENTRY(VER),
	IOMMU_REGSET_ENTRY(CAP),
	IOMMU_REGSET_ENTRY(ECAP),
	IOMMU_REGSET_ENTRY(GCMD),
	IOMMU_REGSET_ENTRY(GSTS),
	IOMMU_REGSET_ENTRY(RTADDR),
	IOMMU_REGSET_ENTRY(CCMD),
	IOMMU_REGSET_ENTRY(FSTS),
	IOMMU_REGSET_ENTRY(FECTL),
	IOMMU_REGSET_ENTRY(FEDATA),
	IOMMU_REGSET_ENTRY(FEADDR),
	IOMMU_REGSET_ENTRY(FEUADDR),
	IOMMU_REGSET_ENTRY(AFLOG),
	IOMMU_REGSET_ENTRY(PMEN),
	IOMMU_REGSET_ENTRY(PLMBASE),
	IOMMU_REGSET_ENTRY(PLMLIMIT),
	IOMMU_REGSET_ENTRY(PHMBASE),
	IOMMU_REGSET_ENTRY(PHMLIMIT),
	IOMMU_REGSET_ENTRY(IQH),
	IOMMU_REGSET_ENTRY(IQT),
	IOMMU_REGSET_ENTRY(IQA),
	IOMMU_REGSET_ENTRY(ICS),
	IOMMU_REGSET_ENTRY(IRTA),
	IOMMU_REGSET_ENTRY(PQH),
	IOMMU_REGSET_ENTRY(PQT),
	IOMMU_REGSET_ENTRY(PQA),
	IOMMU_REGSET_ENTRY(PRS),
	IOMMU_REGSET_ENTRY(PECTL),
	IOMMU_REGSET_ENTRY(PEDATA),
	IOMMU_REGSET_ENTRY(PEADDR),
	IOMMU_REGSET_ENTRY(PEUADDR),
	IOMMU_REGSET_ENTRY(MTRRCAP),
	IOMMU_REGSET_ENTRY(MTRRDEF),
	IOMMU_REGSET_ENTRY(MTRR_FIX64K_00000),
	IOMMU_REGSET_ENTRY(MTRR_FIX16K_80000),
	IOMMU_REGSET_ENTRY(MTRR_FIX16K_A0000),
	IOMMU_REGSET_ENTRY(MTRR_FIX4K_C0000),
	IOMMU_REGSET_ENTRY(MTRR_FIX4K_C8000),
	IOMMU_REGSET_ENTRY(MTRR_FIX4K_D0000),
	IOMMU_REGSET_ENTRY(MTRR_FIX4K_D8000),
	IOMMU_REGSET_ENTRY(MTRR_FIX4K_E0000),
	IOMMU_REGSET_ENTRY(MTRR_FIX4K_E8000),
	IOMMU_REGSET_ENTRY(MTRR_FIX4K_F0000),
	IOMMU_REGSET_ENTRY(MTRR_FIX4K_F8000),
	IOMMU_REGSET_ENTRY(MTRR_PHYSBASE0),
	IOMMU_REGSET_ENTRY(MTRR_PHYSMASK0),
	IOMMU_REGSET_ENTRY(MTRR_PHYSBASE1),
	IOMMU_REGSET_ENTRY(MTRR_PHYSMASK1),
	IOMMU_REGSET_ENTRY(MTRR_PHYSBASE2),
	IOMMU_REGSET_ENTRY(MTRR_PHYSMASK2),
	IOMMU_REGSET_ENTRY(MTRR_PHYSBASE3),
	IOMMU_REGSET_ENTRY(MTRR_PHYSMASK3),
	IOMMU_REGSET_ENTRY(MTRR_PHYSBASE4),
	IOMMU_REGSET_ENTRY(MTRR_PHYSMASK4),
	IOMMU_REGSET_ENTRY(MTRR_PHYSBASE5),
	IOMMU_REGSET_ENTRY(MTRR_PHYSMASK5),
	IOMMU_REGSET_ENTRY(MTRR_PHYSBASE6),
	IOMMU_REGSET_ENTRY(MTRR_PHYSMASK6),
	IOMMU_REGSET_ENTRY(MTRR_PHYSBASE7),
	IOMMU_REGSET_ENTRY(MTRR_PHYSMASK7),
	IOMMU_REGSET_ENTRY(MTRR_PHYSBASE8),
	IOMMU_REGSET_ENTRY(MTRR_PHYSMASK8),
	IOMMU_REGSET_ENTRY(MTRR_PHYSBASE9),
	IOMMU_REGSET_ENTRY(MTRR_PHYSMASK9),
	IOMMU_REGSET_ENTRY(VCCAP),
	IOMMU_REGSET_ENTRY(VCMD),
	IOMMU_REGSET_ENTRY(VCRSP),
};

static int iommu_regset_show(struct seq_file *m, void *unused)
{
	struct dmar_drhd_unit *drhd;
	struct intel_iommu *iommu;
	unsigned long flag;
	int i, ret = 0;
	u64 value;

	rcu_read_lock();
	for_each_active_iommu(iommu, drhd) {
		if (!drhd->reg_base_addr) {
			seq_puts(m, "IOMMU: Invalid base address\n");
			ret = -EINVAL;
			goto out;
		}

		seq_printf(m, "IOMMU: %s Register Base Address: %llx\n",
			   iommu->name, drhd->reg_base_addr);
		seq_puts(m, "Name\t\t\tOffset\t\tContents\n");
		/*
		 * Publish the contents of the 64-bit hardware registers
		 * by adding the offset to the pointer (virtual address).
		 */
		raw_spin_lock_irqsave(&iommu->register_lock, flag);
		for (i = 0 ; i < ARRAY_SIZE(iommu_regs); i++) {
			value = dmar_readq(iommu->reg + iommu_regs[i].offset);
			seq_printf(m, "%-16s\t0x%02x\t\t0x%016llx\n",
				   iommu_regs[i].regs, iommu_regs[i].offset,
				   value);
		}
		raw_spin_unlock_irqrestore(&iommu->register_lock, flag);
		seq_putc(m, '\n');
	}
out:
	rcu_read_unlock();

	return ret;
}
DEFINE_SHOW_ATTRIBUTE(iommu_regset);

void __init intel_iommu_debugfs_init(void)
{
	struct dentry *intel_iommu_debug = debugfs_create_dir("intel",
						iommu_debugfs_dir);

	debugfs_create_file("iommu_regset", 0444, intel_iommu_debug, NULL,
			    &iommu_regset_fops);
}
