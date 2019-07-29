// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Core SoC Power Management Controller Driver
 *
 * Copyright (c) 2016, Intel Corporation.
 * All Rights Reserved.
 *
 * Authors: Rajneesh Bhardwaj <rajneesh.bhardwaj@intel.com>
 *          Vishwanath Somayaji <vishwanath.somayaji@intel.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/uaccess.h>

#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>

#include "intel_pmc_core.h"

#define ICPU(model, data) \
	{ X86_VENDOR_INTEL, 6, model, X86_FEATURE_MWAIT, (kernel_ulong_t)data }

static struct pmc_dev pmc;

static const struct pmc_bit_map spt_pll_map[] = {
	{"MIPI PLL",			SPT_PMC_BIT_MPHY_CMN_LANE0},
	{"GEN2 USB2PCIE2 PLL",		SPT_PMC_BIT_MPHY_CMN_LANE1},
	{"DMIPCIE3 PLL",		SPT_PMC_BIT_MPHY_CMN_LANE2},
	{"SATA PLL",			SPT_PMC_BIT_MPHY_CMN_LANE3},
	{},
};

static const struct pmc_bit_map spt_mphy_map[] = {
	{"MPHY CORE LANE 0",           SPT_PMC_BIT_MPHY_LANE0},
	{"MPHY CORE LANE 1",           SPT_PMC_BIT_MPHY_LANE1},
	{"MPHY CORE LANE 2",           SPT_PMC_BIT_MPHY_LANE2},
	{"MPHY CORE LANE 3",           SPT_PMC_BIT_MPHY_LANE3},
	{"MPHY CORE LANE 4",           SPT_PMC_BIT_MPHY_LANE4},
	{"MPHY CORE LANE 5",           SPT_PMC_BIT_MPHY_LANE5},
	{"MPHY CORE LANE 6",           SPT_PMC_BIT_MPHY_LANE6},
	{"MPHY CORE LANE 7",           SPT_PMC_BIT_MPHY_LANE7},
	{"MPHY CORE LANE 8",           SPT_PMC_BIT_MPHY_LANE8},
	{"MPHY CORE LANE 9",           SPT_PMC_BIT_MPHY_LANE9},
	{"MPHY CORE LANE 10",          SPT_PMC_BIT_MPHY_LANE10},
	{"MPHY CORE LANE 11",          SPT_PMC_BIT_MPHY_LANE11},
	{"MPHY CORE LANE 12",          SPT_PMC_BIT_MPHY_LANE12},
	{"MPHY CORE LANE 13",          SPT_PMC_BIT_MPHY_LANE13},
	{"MPHY CORE LANE 14",          SPT_PMC_BIT_MPHY_LANE14},
	{"MPHY CORE LANE 15",          SPT_PMC_BIT_MPHY_LANE15},
	{},
};

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
	.mphy_sts = spt_mphy_map,
	.pll_sts = spt_pll_map,
	.slp_s0_offset = SPT_PMC_SLP_S0_RES_COUNTER_OFFSET,
	.ltr_ignore_offset = SPT_PMC_LTR_IGNORE_OFFSET,
	.regmap_length = SPT_PMC_MMIO_REG_LEN,
	.ppfear0_offset = SPT_PMC_XRAM_PPFEAR0A,
	.ppfear_buckets = SPT_PPFEAR_NUM_ENTRIES,
	.pm_cfg_offset = SPT_PMC_PM_CFG_OFFSET,
	.pm_read_disable_bit = SPT_PMC_READ_DISABLE_BIT,
};

/* Cannonlake: PGD PFET Enable Ack Status Register(s) bitmap */
static const struct pmc_bit_map cnp_pfear_map[] = {
	{"PMC",                 BIT(0)},
	{"OPI-DMI",             BIT(1)},
	{"SPI/eSPI",            BIT(2)},
	{"XHCI",                BIT(3)},
	{"SPA",                 BIT(4)},
	{"SPB",                 BIT(5)},
	{"SPC",                 BIT(6)},
	{"GBE",                 BIT(7)},

	{"SATA",                BIT(0)},
	{"HDA_PGD0",            BIT(1)},
	{"HDA_PGD1",            BIT(2)},
	{"HDA_PGD2",            BIT(3)},
	{"HDA_PGD3",            BIT(4)},
	{"SPD",                 BIT(5)},
	{"LPSS",                BIT(6)},
	{"LPC",                 BIT(7)},

	{"SMB",                 BIT(0)},
	{"ISH",                 BIT(1)},
	{"P2SB",                BIT(2)},
	{"NPK_VNN",             BIT(3)},
	{"SDX",                 BIT(4)},
	{"SPE",                 BIT(5)},
	{"Fuse",                BIT(6)},
	{"Res_23",              BIT(7)},

	{"CSME_FSC",            BIT(0)},
	{"USB3_OTG",            BIT(1)},
	{"EXI",                 BIT(2)},
	{"CSE",                 BIT(3)},
	{"csme_kvm",            BIT(4)},
	{"csme_pmt",            BIT(5)},
	{"csme_clink",          BIT(6)},
	{"csme_ptio",           BIT(7)},

	{"csme_usbr",           BIT(0)},
	{"csme_susram",         BIT(1)},
	{"csme_smt1",           BIT(2)},
	{"CSME_SMT4",           BIT(3)},
	{"csme_sms2",           BIT(4)},
	{"csme_sms1",           BIT(5)},
	{"csme_rtc",            BIT(6)},
	{"csme_psf",            BIT(7)},

	{"SBR0",                BIT(0)},
	{"SBR1",                BIT(1)},
	{"SBR2",                BIT(2)},
	{"SBR3",                BIT(3)},
	{"SBR4",                BIT(4)},
	{"SBR5",                BIT(5)},
	{"CSME_PECI",           BIT(6)},
	{"PSF1",                BIT(7)},

	{"PSF2",                BIT(0)},
	{"PSF3",                BIT(1)},
	{"PSF4",                BIT(2)},
	{"CNVI",                BIT(3)},
	{"UFS0",                BIT(4)},
	{"EMMC",                BIT(5)},
	{"Res_6",               BIT(6)},
	{"SBR6",                BIT(7)},

	{"SBR7",                BIT(0)},
	{"NPK_AON",             BIT(1)},
	{"HDA_PGD4",            BIT(2)},
	{"HDA_PGD5",            BIT(3)},
	{"HDA_PGD6",            BIT(4)},
	{}
};

static const struct pmc_bit_map cnp_slps0_dbg0_map[] = {
	{"AUDIO_D3",		BIT(0)},
	{"OTG_D3",		BIT(1)},
	{"XHCI_D3",		BIT(2)},
	{"LPIO_D3",		BIT(3)},
	{"SDX_D3",		BIT(4)},
	{"SATA_D3",		BIT(5)},
	{"UFS0_D3",		BIT(6)},
	{"UFS1_D3",		BIT(7)},
	{"EMMC_D3",		BIT(8)},
	{}
};

static const struct pmc_bit_map cnp_slps0_dbg1_map[] = {
	{"SDIO_PLL_OFF",	BIT(0)},
	{"USB2_PLL_OFF",	BIT(1)},
	{"AUDIO_PLL_OFF",	BIT(2)},
	{"OC_PLL_OFF",		BIT(3)},
	{"MAIN_PLL_OFF",	BIT(4)},
	{"XOSC_OFF",		BIT(5)},
	{"LPC_CLKS_GATED",	BIT(6)},
	{"PCIE_CLKREQS_IDLE",	BIT(7)},
	{"AUDIO_ROSC_OFF",	BIT(8)},
	{"HPET_XOSC_CLK_REQ",	BIT(9)},
	{"PMC_ROSC_SLOW_CLK",	BIT(10)},
	{"AON2_ROSC_GATED",	BIT(11)},
	{"CLKACKS_DEASSERTED",	BIT(12)},
	{}
};

static const struct pmc_bit_map cnp_slps0_dbg2_map[] = {
	{"MPHY_CORE_GATED",	BIT(0)},
	{"CSME_GATED",		BIT(1)},
	{"USB2_SUS_GATED",	BIT(2)},
	{"DYN_FLEX_IO_IDLE",	BIT(3)},
	{"GBE_NO_LINK",		BIT(4)},
	{"THERM_SEN_DISABLED",	BIT(5)},
	{"PCIE_LOW_POWER",	BIT(6)},
	{"ISH_VNNAON_REQ_ACT",	BIT(7)},
	{"ISH_VNN_REQ_ACT",	BIT(8)},
	{"CNV_VNNAON_REQ_ACT",	BIT(9)},
	{"CNV_VNN_REQ_ACT",	BIT(10)},
	{"NPK_VNNON_REQ_ACT",	BIT(11)},
	{"PMSYNC_STATE_IDLE",	BIT(12)},
	{"ALST_GT_THRES",	BIT(13)},
	{"PMC_ARC_PG_READY",	BIT(14)},
	{}
};

static const struct pmc_bit_map *cnp_slps0_dbg_maps[] = {
	cnp_slps0_dbg0_map,
	cnp_slps0_dbg1_map,
	cnp_slps0_dbg2_map,
	NULL,
};

static const struct pmc_reg_map cnp_reg_map = {
	.pfear_sts = cnp_pfear_map,
	.slp_s0_offset = CNP_PMC_SLP_S0_RES_COUNTER_OFFSET,
	.slps0_dbg_maps = cnp_slps0_dbg_maps,
	.slps0_dbg_offset = CNP_PMC_SLPS0_DBG_OFFSET,
	.ltr_ignore_offset = CNP_PMC_LTR_IGNORE_OFFSET,
	.regmap_length = CNP_PMC_MMIO_REG_LEN,
	.ppfear0_offset = CNP_PMC_HOST_PPFEAR0A,
	.ppfear_buckets = CNP_PPFEAR_NUM_ENTRIES,
	.pm_cfg_offset = CNP_PMC_PM_CFG_OFFSET,
	.pm_read_disable_bit = CNP_PMC_READ_DISABLE_BIT,
};

static inline u8 pmc_core_reg_read_byte(struct pmc_dev *pmcdev, int offset)
{
	return readb(pmcdev->regbase + offset);
}

static inline u32 pmc_core_reg_read(struct pmc_dev *pmcdev, int reg_offset)
{
	return readl(pmcdev->regbase + reg_offset);
}

static inline void pmc_core_reg_write(struct pmc_dev *pmcdev, int
							reg_offset, u32 val)
{
	writel(val, pmcdev->regbase + reg_offset);
}

static inline u32 pmc_core_adjust_slp_s0_step(u32 value)
{
	return value * SPT_PMC_SLP_S0_RES_COUNTER_STEP;
}

static int pmc_core_dev_state_get(void *data, u64 *val)
{
	struct pmc_dev *pmcdev = data;
	const struct pmc_reg_map *map = pmcdev->map;
	u32 value;

	value = pmc_core_reg_read(pmcdev, map->slp_s0_offset);
	*val = pmc_core_adjust_slp_s0_step(value);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(pmc_core_dev_state, pmc_core_dev_state_get, NULL, "%llu\n");

static int pmc_core_check_read_lock_bit(void)
{
	struct pmc_dev *pmcdev = &pmc;
	u32 value;

	value = pmc_core_reg_read(pmcdev, pmcdev->map->pm_cfg_offset);
	return value & BIT(pmcdev->map->pm_read_disable_bit);
}

#if IS_ENABLED(CONFIG_DEBUG_FS)
static bool slps0_dbg_latch;

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
	u8 pf_regs[PPFEAR_MAX_NUM_ENTRIES];
	int index, iter;

	iter = pmcdev->map->ppfear0_offset;

	for (index = 0; index < pmcdev->map->ppfear_buckets &&
	     index < PPFEAR_MAX_NUM_ENTRIES; index++, iter++)
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

/* This function should return link status, 0 means ready */
static int pmc_core_mtpmc_link_status(void)
{
	struct pmc_dev *pmcdev = &pmc;
	u32 value;

	value = pmc_core_reg_read(pmcdev, SPT_PMC_PM_STS_OFFSET);
	return value & BIT(SPT_PMC_MSG_FULL_STS_BIT);
}

static int pmc_core_send_msg(u32 *addr_xram)
{
	struct pmc_dev *pmcdev = &pmc;
	u32 dest;
	int timeout;

	for (timeout = NUM_RETRIES; timeout > 0; timeout--) {
		if (pmc_core_mtpmc_link_status() == 0)
			break;
		msleep(5);
	}

	if (timeout <= 0 && pmc_core_mtpmc_link_status())
		return -EBUSY;

	dest = (*addr_xram & MTPMC_MASK) | (1U << 1);
	pmc_core_reg_write(pmcdev, SPT_PMC_MTPMC_OFFSET, dest);
	return 0;
}

static int pmc_core_mphy_pg_sts_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	const struct pmc_bit_map *map = pmcdev->map->mphy_sts;
	u32 mphy_core_reg_low, mphy_core_reg_high;
	u32 val_low, val_high;
	int index, err = 0;

	if (pmcdev->pmc_xram_read_bit) {
		seq_puts(s, "Access denied: please disable PMC_READ_DISABLE setting in BIOS.");
		return 0;
	}

	mphy_core_reg_low  = (SPT_PMC_MPHY_CORE_STS_0 << 16);
	mphy_core_reg_high = (SPT_PMC_MPHY_CORE_STS_1 << 16);

	mutex_lock(&pmcdev->lock);

	if (pmc_core_send_msg(&mphy_core_reg_low) != 0) {
		err = -EBUSY;
		goto out_unlock;
	}

	msleep(10);
	val_low = pmc_core_reg_read(pmcdev, SPT_PMC_MFPMC_OFFSET);

	if (pmc_core_send_msg(&mphy_core_reg_high) != 0) {
		err = -EBUSY;
		goto out_unlock;
	}

	msleep(10);
	val_high = pmc_core_reg_read(pmcdev, SPT_PMC_MFPMC_OFFSET);

	for (index = 0; map[index].name && index < 8; index++) {
		seq_printf(s, "%-32s\tState: %s\n",
			   map[index].name,
			   map[index].bit_mask & val_low ? "Not power gated" :
			   "Power gated");
	}

	for (index = 8; map[index].name; index++) {
		seq_printf(s, "%-32s\tState: %s\n",
			   map[index].name,
			   map[index].bit_mask & val_high ? "Not power gated" :
			   "Power gated");
	}

out_unlock:
	mutex_unlock(&pmcdev->lock);
	return err;
}

static int pmc_core_mphy_pg_sts_open(struct inode *inode, struct file *file)
{
	return single_open(file, pmc_core_mphy_pg_sts_show, inode->i_private);
}

static const struct file_operations pmc_core_mphy_pg_ops = {
	.open           = pmc_core_mphy_pg_sts_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static int pmc_core_pll_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	const struct pmc_bit_map *map = pmcdev->map->pll_sts;
	u32 mphy_common_reg, val;
	int index, err = 0;

	if (pmcdev->pmc_xram_read_bit) {
		seq_puts(s, "Access denied: please disable PMC_READ_DISABLE setting in BIOS.");
		return 0;
	}

	mphy_common_reg  = (SPT_PMC_MPHY_COM_STS_0 << 16);
	mutex_lock(&pmcdev->lock);

	if (pmc_core_send_msg(&mphy_common_reg) != 0) {
		err = -EBUSY;
		goto out_unlock;
	}

	/* Observed PMC HW response latency for MTPMC-MFPMC is ~10 ms */
	msleep(10);
	val = pmc_core_reg_read(pmcdev, SPT_PMC_MFPMC_OFFSET);

	for (index = 0; map[index].name ; index++) {
		seq_printf(s, "%-32s\tState: %s\n",
			   map[index].name,
			   map[index].bit_mask & val ? "Active" : "Idle");
	}

out_unlock:
	mutex_unlock(&pmcdev->lock);
	return err;
}

static int pmc_core_pll_open(struct inode *inode, struct file *file)
{
	return single_open(file, pmc_core_pll_show, inode->i_private);
}

static const struct file_operations pmc_core_pll_ops = {
	.open           = pmc_core_pll_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static ssize_t pmc_core_ltr_ignore_write(struct file *file, const char __user
*userbuf, size_t count, loff_t *ppos)
{
	struct pmc_dev *pmcdev = &pmc;
	const struct pmc_reg_map *map = pmcdev->map;
	u32 val, buf_size, fd;
	int err = 0;

	buf_size = count < 64 ? count : 64;
	mutex_lock(&pmcdev->lock);

	if (kstrtou32_from_user(userbuf, buf_size, 10, &val)) {
		err = -EFAULT;
		goto out_unlock;
	}

	if (val > NUM_IP_IGN_ALLOWED) {
		err = -EINVAL;
		goto out_unlock;
	}

	fd = pmc_core_reg_read(pmcdev, map->ltr_ignore_offset);
	fd |= (1U << val);
	pmc_core_reg_write(pmcdev, map->ltr_ignore_offset, fd);

out_unlock:
	mutex_unlock(&pmcdev->lock);
	return err == 0 ? count : err;
}

static int pmc_core_ltr_ignore_show(struct seq_file *s, void *unused)
{
	return 0;
}

static int pmc_core_ltr_ignore_open(struct inode *inode, struct file *file)
{
	return single_open(file, pmc_core_ltr_ignore_show, inode->i_private);
}

static const struct file_operations pmc_core_ltr_ignore_ops = {
	.open           = pmc_core_ltr_ignore_open,
	.read           = seq_read,
	.write          = pmc_core_ltr_ignore_write,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static void pmc_core_slps0_dbg_latch(struct pmc_dev *pmcdev, bool reset)
{
	const struct pmc_reg_map *map = pmcdev->map;
	u32 fd;

	mutex_lock(&pmcdev->lock);

	if (!reset && !slps0_dbg_latch)
		goto out_unlock;

	fd = pmc_core_reg_read(pmcdev, map->slps0_dbg_offset);
	if (reset)
		fd &= ~CNP_PMC_LATCH_SLPS0_EVENTS;
	else
		fd |= CNP_PMC_LATCH_SLPS0_EVENTS;
	pmc_core_reg_write(pmcdev, map->slps0_dbg_offset, fd);

	slps0_dbg_latch = 0;

out_unlock:
	mutex_unlock(&pmcdev->lock);
}

static int pmc_core_slps0_dbg_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	const struct pmc_bit_map **maps = pmcdev->map->slps0_dbg_maps;
	const struct pmc_bit_map *map;
	int offset;
	u32 data;

	pmc_core_slps0_dbg_latch(pmcdev, false);
	offset = pmcdev->map->slps0_dbg_offset;
	while (*maps) {
		map = *maps;
		data = pmc_core_reg_read(pmcdev, offset);
		offset += 4;
		while (map->name) {
			seq_printf(s, "SLP_S0_DBG: %-32s\tState: %s\n",
				   map->name,
				   data & map->bit_mask ?
				   "Yes" : "No");
			++map;
		}
		++maps;
	}
	pmc_core_slps0_dbg_latch(pmcdev, true);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_slps0_dbg);

static void pmc_core_dbgfs_unregister(struct pmc_dev *pmcdev)
{
	debugfs_remove_recursive(pmcdev->dbgfs_dir);
}

static int pmc_core_dbgfs_register(struct pmc_dev *pmcdev)
{
	struct dentry *dir;

	dir = debugfs_create_dir("pmc_core", NULL);
	if (!dir)
		return -ENOMEM;

	pmcdev->dbgfs_dir = dir;

	debugfs_create_file("slp_s0_residency_usec", 0444, dir, pmcdev,
			    &pmc_core_dev_state);

	debugfs_create_file("pch_ip_power_gating_status", 0444, dir, pmcdev,
			    &pmc_core_ppfear_ops);

	debugfs_create_file("ltr_ignore", 0644, dir, pmcdev,
			    &pmc_core_ltr_ignore_ops);

	if (pmcdev->map->pll_sts)
		debugfs_create_file("pll_status", 0444, dir, pmcdev,
				    &pmc_core_pll_ops);

	if (pmcdev->map->mphy_sts)
		debugfs_create_file("mphy_core_lanes_power_gating_status",
				    0444, dir, pmcdev,
				    &pmc_core_mphy_pg_ops);

	if (pmcdev->map->slps0_dbg_maps) {
		debugfs_create_file("slp_s0_debug_status", 0444,
				    dir, pmcdev,
				    &pmc_core_slps0_dbg_fops);

		debugfs_create_bool("slp_s0_dbg_latch", 0644,
				    dir, &slps0_dbg_latch);
	}

	return 0;
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
	ICPU(INTEL_FAM6_SKYLAKE_MOBILE, &spt_reg_map),
	ICPU(INTEL_FAM6_SKYLAKE_DESKTOP, &spt_reg_map),
	ICPU(INTEL_FAM6_KABYLAKE_MOBILE, &spt_reg_map),
	ICPU(INTEL_FAM6_KABYLAKE_DESKTOP, &spt_reg_map),
	ICPU(INTEL_FAM6_CANNONLAKE_MOBILE, &cnp_reg_map),
	{}
};

MODULE_DEVICE_TABLE(x86cpu, intel_pmc_core_ids);

static const struct pci_device_id pmc_pci_ids[] = {
	{ PCI_VDEVICE(INTEL, SPT_PMC_PCI_DEVICE_ID), 0},
	{ 0, },
};

static int __init pmc_core_probe(void)
{
	struct pmc_dev *pmcdev = &pmc;
	const struct x86_cpu_id *cpu_id;
	u64 slp_s0_addr;
	int err;

	cpu_id = x86_match_cpu(intel_pmc_core_ids);
	if (!cpu_id)
		return -ENODEV;

	pmcdev->map = (struct pmc_reg_map *)cpu_id->driver_data;

	/*
	 * Coffeelake has CPU ID of Kabylake and Cannonlake PCH. So here
	 * Sunrisepoint PCH regmap can't be used. Use Cannonlake PCH regmap
	 * in this case.
	 */
	if (!pci_dev_present(pmc_pci_ids))
		pmcdev->map = &cnp_reg_map;

	if (lpit_read_residency_count_address(&slp_s0_addr))
		pmcdev->base_addr = PMC_BASE_ADDR_DEFAULT;
	else
		pmcdev->base_addr = slp_s0_addr - pmcdev->map->slp_s0_offset;

	pmcdev->regbase = ioremap(pmcdev->base_addr,
				  pmcdev->map->regmap_length);
	if (!pmcdev->regbase)
		return -ENOMEM;

	mutex_init(&pmcdev->lock);
	pmcdev->pmc_xram_read_bit = pmc_core_check_read_lock_bit();

	err = pmc_core_dbgfs_register(pmcdev);
	if (err < 0) {
		pr_warn(" debugfs register failed.\n");
		iounmap(pmcdev->regbase);
		return err;
	}

	pr_info(" initialized\n");
	return 0;
}
module_init(pmc_core_probe)

static void __exit pmc_core_remove(void)
{
	struct pmc_dev *pmcdev = &pmc;

	pmc_core_dbgfs_unregister(pmcdev);
	mutex_destroy(&pmcdev->lock);
	iounmap(pmcdev->regbase);
}
module_exit(pmc_core_remove)

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel PMC Core Driver");
