/*
 * Intel Core SoC Power Management Controller Driver
 *
 * Copyright (c) 2016, Intel Corporation.
 * All Rights Reserved.
 *
 * Authors: Rajneesh Bhardwaj <rajneesh.bhardwaj@intel.com>
 *          Vishwanath Somayaji <vishwanath.somayaji@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/pci.h>

#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/pmc_core.h>

#include "intel_pmc_core.h"

static struct pmc_dev pmc;

static const struct pmc_bit_map spt_pfear_map[] = {
	{"PMC",				SPT_PMC_BIT_PMC},
	{"OPI-DMI",			SPT_PMC_BIT_OPI},
	{"SPI / eSPI",			SPT_PMC_BIT_SPI},
	{"XHCI",			SPT_PMC_BIT_XHCI},
	{"SPA",				SPT_PMC_BIT_SPA},
	{"SPB",				SPT_PMC_BIT_SPB},
	{"SPC",				SPT_PMC_BIT_SPC},
	{"GBE",				SPT_PMC_BIT_GBE},
	{"SATA",			SPT_PMC_BIT_SATA},
	{"HDA-PGD0",			SPT_PMC_BIT_HDA_PGD0},
	{"HDA-PGD1",			SPT_PMC_BIT_HDA_PGD1},
	{"HDA-PGD2",			SPT_PMC_BIT_HDA_PGD2},
	{"HDA-PGD3",			SPT_PMC_BIT_HDA_PGD3},
	{"RSVD",			SPT_PMC_BIT_RSVD_0B},
	{"LPSS",			SPT_PMC_BIT_LPSS},
	{"LPC",				SPT_PMC_BIT_LPC},
	{"SMB",				SPT_PMC_BIT_SMB},
	{"ISH",				SPT_PMC_BIT_ISH},
	{"P2SB",			SPT_PMC_BIT_P2SB},
	{"DFX",				SPT_PMC_BIT_DFX},
	{"SCC",				SPT_PMC_BIT_SCC},
	{"RSVD",			SPT_PMC_BIT_RSVD_0C},
	{"FUSE",			SPT_PMC_BIT_FUSE},
	{"CAMERA",			SPT_PMC_BIT_CAMREA},
	{"RSVD",			SPT_PMC_BIT_RSVD_0D},
	{"USB3-OTG",			SPT_PMC_BIT_USB3_OTG},
	{"EXI",				SPT_PMC_BIT_EXI},
	{"CSE",				SPT_PMC_BIT_CSE},
	{"CSME_KVM",			SPT_PMC_BIT_CSME_KVM},
	{"CSME_PMT",			SPT_PMC_BIT_CSME_PMT},
	{"CSME_CLINK",			SPT_PMC_BIT_CSME_CLINK},
	{"CSME_PTIO",			SPT_PMC_BIT_CSME_PTIO},
	{"CSME_USBR",			SPT_PMC_BIT_CSME_USBR},
	{"CSME_SUSRAM",			SPT_PMC_BIT_CSME_SUSRAM},
	{"CSME_SMT",			SPT_PMC_BIT_CSME_SMT},
	{"RSVD",			SPT_PMC_BIT_RSVD_1A},
	{"CSME_SMS2",			SPT_PMC_BIT_CSME_SMS2},
	{"CSME_SMS1",			SPT_PMC_BIT_CSME_SMS1},
	{"CSME_RTC",			SPT_PMC_BIT_CSME_RTC},
	{"CSME_PSF",			SPT_PMC_BIT_CSME_PSF},
	{},
};

static const struct pmc_reg_map spt_reg_map = {
	.pfear_sts = spt_pfear_map,
};

static const struct pci_device_id pmc_pci_ids[] = {
	{ PCI_VDEVICE(INTEL, SPT_PMC_PCI_DEVICE_ID),
					(kernel_ulong_t)&spt_reg_map },
	{ 0, },
};

static inline u8 pmc_core_reg_read_byte(struct pmc_dev *pmcdev, int offset)
{
	return readb(pmcdev->regbase + offset);
}

static inline u32 pmc_core_reg_read(struct pmc_dev *pmcdev, int reg_offset)
{
	return readl(pmcdev->regbase + reg_offset);
}

static inline u32 pmc_core_adjust_slp_s0_step(u32 value)
{
	return value * SPT_PMC_SLP_S0_RES_COUNTER_STEP;
}

/**
 * intel_pmc_slp_s0_counter_read() - Read SLP_S0 residency.
 * @data: Out param that contains current SLP_S0 count.
 *
 * This API currently supports Intel Skylake SoC and Sunrise
 * Point Platform Controller Hub. Future platform support
 * should be added for platforms that support low power modes
 * beyond Package C10 state.
 *
 * SLP_S0_RESIDENCY counter counts in 100 us granularity per
 * step hence function populates the multiplied value in out
 * parameter @data.
 *
 * Return: an error code or 0 on success.
 */
int intel_pmc_slp_s0_counter_read(u32 *data)
{
	struct pmc_dev *pmcdev = &pmc;
	u32 value;

	if (!pmcdev->has_slp_s0_res)
		return -EACCES;

	value = pmc_core_reg_read(pmcdev, SPT_PMC_SLP_S0_RES_COUNTER_OFFSET);
	*data = pmc_core_adjust_slp_s0_step(value);

	return 0;
}
EXPORT_SYMBOL_GPL(intel_pmc_slp_s0_counter_read);

static int pmc_core_dev_state_get(void *data, u64 *val)
{
	struct pmc_dev *pmcdev = data;
	u32 value;

	value = pmc_core_reg_read(pmcdev, SPT_PMC_SLP_S0_RES_COUNTER_OFFSET);
	*val = pmc_core_adjust_slp_s0_step(value);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(pmc_core_dev_state, pmc_core_dev_state_get, NULL, "%llu\n");

#if IS_ENABLED(CONFIG_DEBUG_FS)
static void pmc_core_display_map(struct seq_file *s, int index,
				 u8 pf_reg, const struct pmc_bit_map *pf_map)
{
	seq_printf(s, "PCH IP: %-2d - %-32s\tState: %s\n",
		   index, pf_map[index].name,
		   pf_map[index].bit_mask & pf_reg ? "Off" : "On");
}

static int pmc_core_ppfear_sts_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	const struct pmc_bit_map *map = pmcdev->map->pfear_sts;
	u8 pf_regs[NUM_ENTRIES];
	int index, iter;

	iter = SPT_PMC_XRAM_PPFEAR0A;

	for (index = 0; index < NUM_ENTRIES; index++, iter++)
		pf_regs[index] = pmc_core_reg_read_byte(pmcdev, iter);

	for (index = 0; map[index].name; index++)
		pmc_core_display_map(s, index, pf_regs[index / 8], map);

	return 0;
}

static int pmc_core_ppfear_sts_open(struct inode *inode, struct file *file)
{
	return single_open(file, pmc_core_ppfear_sts_show, inode->i_private);
}

static const struct file_operations pmc_core_ppfear_ops = {
	.open           = pmc_core_ppfear_sts_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static void pmc_core_dbgfs_unregister(struct pmc_dev *pmcdev)
{
	debugfs_remove_recursive(pmcdev->dbgfs_dir);
}

static int pmc_core_dbgfs_register(struct pmc_dev *pmcdev)
{
	struct dentry *dir, *file;

	dir = debugfs_create_dir("pmc_core", NULL);
	if (!dir)
		return -ENOMEM;

	pmcdev->dbgfs_dir = dir;
	file = debugfs_create_file("slp_s0_residency_usec", S_IFREG | S_IRUGO,
				   dir, pmcdev, &pmc_core_dev_state);
	if (!file)
		goto err;

	file = debugfs_create_file("pch_ip_power_gating_status",
				   S_IFREG | S_IRUGO, dir, pmcdev,
				   &pmc_core_ppfear_ops);
	if (!file)
		goto err;

	return 0;

err:
	pmc_core_dbgfs_unregister(pmcdev);
	return -ENODEV;
}
#else
static inline int pmc_core_dbgfs_register(struct pmc_dev *pmcdev)
{
	return 0;
}

static inline void pmc_core_dbgfs_unregister(struct pmc_dev *pmcdev)
{
}
#endif /* CONFIG_DEBUG_FS */

static const struct x86_cpu_id intel_pmc_core_ids[] = {
	{ X86_VENDOR_INTEL, 6, INTEL_FAM6_SKYLAKE_MOBILE, X86_FEATURE_MWAIT,
		(kernel_ulong_t)NULL},
	{ X86_VENDOR_INTEL, 6, INTEL_FAM6_SKYLAKE_DESKTOP, X86_FEATURE_MWAIT,
		(kernel_ulong_t)NULL},
	{}
};

static int pmc_core_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct device *ptr_dev = &dev->dev;
	struct pmc_dev *pmcdev = &pmc;
	const struct x86_cpu_id *cpu_id;
	const struct pmc_reg_map *map = (struct pmc_reg_map *)id->driver_data;
	int err;

	cpu_id = x86_match_cpu(intel_pmc_core_ids);
	if (!cpu_id) {
		dev_dbg(&dev->dev, "PMC Core: cpuid mismatch.\n");
		return -EINVAL;
	}

	err = pcim_enable_device(dev);
	if (err < 0) {
		dev_dbg(&dev->dev, "PMC Core: failed to enable Power Management Controller.\n");
		return err;
	}

	err = pci_read_config_dword(dev,
				    SPT_PMC_BASE_ADDR_OFFSET,
				    &pmcdev->base_addr);
	if (err < 0) {
		dev_dbg(&dev->dev, "PMC Core: failed to read PCI config space.\n");
		return err;
	}
	pmcdev->base_addr &= PMC_BASE_ADDR_MASK;
	dev_dbg(&dev->dev, "PMC Core: PWRMBASE is %#x\n", pmcdev->base_addr);

	pmcdev->regbase = devm_ioremap_nocache(ptr_dev,
					      pmcdev->base_addr,
					      SPT_PMC_MMIO_REG_LEN);
	if (!pmcdev->regbase) {
		dev_dbg(&dev->dev, "PMC Core: ioremap failed.\n");
		return -ENOMEM;
	}

	err = pmc_core_dbgfs_register(pmcdev);
	if (err < 0)
		dev_warn(&dev->dev, "PMC Core: debugfs register failed.\n");

	pmcdev->map = map;
	pmc.has_slp_s0_res = true;
	return 0;
}

static struct pci_driver intel_pmc_core_driver = {
	.name = "intel_pmc_core",
	.id_table = pmc_pci_ids,
	.probe = pmc_core_probe,
};

builtin_pci_driver(intel_pmc_core_driver);
