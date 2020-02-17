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
#include <linux/bitfield.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/suspend.h>
#include <linux/uaccess.h>

#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/msr.h>
#include <asm/tsc.h>

#include "intel_pmc_core.h"

static struct pmc_dev pmc;

/* PKGC MSRs are common across Intel Core SoCs */
static const struct pmc_bit_map msr_map[] = {
	{"Package C2",                  MSR_PKG_C2_RESIDENCY},
	{"Package C3",                  MSR_PKG_C3_RESIDENCY},
	{"Package C6",                  MSR_PKG_C6_RESIDENCY},
	{"Package C7",                  MSR_PKG_C7_RESIDENCY},
	{"Package C8",                  MSR_PKG_C8_RESIDENCY},
	{"Package C9",                  MSR_PKG_C9_RESIDENCY},
	{"Package C10",                 MSR_PKG_C10_RESIDENCY},
	{}
};

static const struct pmc_bit_map spt_pll_map[] = {
	{"MIPI PLL",			SPT_PMC_BIT_MPHY_CMN_LANE0},
	{"GEN2 USB2PCIE2 PLL",		SPT_PMC_BIT_MPHY_CMN_LANE1},
	{"DMIPCIE3 PLL",		SPT_PMC_BIT_MPHY_CMN_LANE2},
	{"SATA PLL",			SPT_PMC_BIT_MPHY_CMN_LANE3},
	{}
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
	{}
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
	{}
};

static const struct pmc_bit_map *ext_spt_pfear_map[] = {
	spt_pfear_map,
	NULL
};

static const struct pmc_bit_map spt_ltr_show_map[] = {
	{"SOUTHPORT_A",		SPT_PMC_LTR_SPA},
	{"SOUTHPORT_B",		SPT_PMC_LTR_SPB},
	{"SATA",		SPT_PMC_LTR_SATA},
	{"GIGABIT_ETHERNET",	SPT_PMC_LTR_GBE},
	{"XHCI",		SPT_PMC_LTR_XHCI},
	{"Reserved",		SPT_PMC_LTR_RESERVED},
	{"ME",			SPT_PMC_LTR_ME},
	/* EVA is Enterprise Value Add, doesn't really exist on PCH */
	{"EVA",			SPT_PMC_LTR_EVA},
	{"SOUTHPORT_C",		SPT_PMC_LTR_SPC},
	{"HD_AUDIO",		SPT_PMC_LTR_AZ},
	{"LPSS",		SPT_PMC_LTR_LPSS},
	{"SOUTHPORT_D",		SPT_PMC_LTR_SPD},
	{"SOUTHPORT_E",		SPT_PMC_LTR_SPE},
	{"CAMERA",		SPT_PMC_LTR_CAM},
	{"ESPI",		SPT_PMC_LTR_ESPI},
	{"SCC",			SPT_PMC_LTR_SCC},
	{"ISH",			SPT_PMC_LTR_ISH},
	/* Below two cannot be used for LTR_IGNORE */
	{"CURRENT_PLATFORM",	SPT_PMC_LTR_CUR_PLT},
	{"AGGREGATED_SYSTEM",	SPT_PMC_LTR_CUR_ASLT},
	{}
};

static const struct pmc_reg_map spt_reg_map = {
	.pfear_sts = ext_spt_pfear_map,
	.mphy_sts = spt_mphy_map,
	.pll_sts = spt_pll_map,
	.ltr_show_sts = spt_ltr_show_map,
	.msr_sts = msr_map,
	.slp_s0_offset = SPT_PMC_SLP_S0_RES_COUNTER_OFFSET,
	.ltr_ignore_offset = SPT_PMC_LTR_IGNORE_OFFSET,
	.regmap_length = SPT_PMC_MMIO_REG_LEN,
	.ppfear0_offset = SPT_PMC_XRAM_PPFEAR0A,
	.ppfear_buckets = SPT_PPFEAR_NUM_ENTRIES,
	.pm_cfg_offset = SPT_PMC_PM_CFG_OFFSET,
	.pm_read_disable_bit = SPT_PMC_READ_DISABLE_BIT,
	.ltr_ignore_max = SPT_NUM_IP_IGN_ALLOWED,
	.pm_vric1_offset = SPT_PMC_VRIC1_OFFSET,
};

/* Cannon Lake: PGD PFET Enable Ack Status Register(s) bitmap */
static const struct pmc_bit_map cnp_pfear_map[] = {
	/* Reserved for Cannon Lake but valid for Comet Lake */
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
	/*
	 * Reserved for Cannon Lake but valid for Ice Lake, Comet Lake,
	 * Tiger Lake and Elkhart Lake.
	 */
	{"SBR8",		BIT(7)},

	{"CSME_FSC",            BIT(0)},
	{"USB3_OTG",            BIT(1)},
	{"EXI",                 BIT(2)},
	{"CSE",                 BIT(3)},
	{"CSME_KVM",            BIT(4)},
	{"CSME_PMT",            BIT(5)},
	{"CSME_CLINK",          BIT(6)},
	{"CSME_PTIO",           BIT(7)},

	{"CSME_USBR",           BIT(0)},
	{"CSME_SUSRAM",         BIT(1)},
	{"CSME_SMT1",           BIT(2)},
	{"CSME_SMT4",           BIT(3)},
	{"CSME_SMS2",           BIT(4)},
	{"CSME_SMS1",           BIT(5)},
	{"CSME_RTC",            BIT(6)},
	{"CSME_PSF",            BIT(7)},

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
	{"SPF",			BIT(6)},
	{"SBR6",                BIT(7)},

	{"SBR7",                BIT(0)},
	{"NPK_AON",             BIT(1)},
	{"HDA_PGD4",            BIT(2)},
	{"HDA_PGD5",            BIT(3)},
	{"HDA_PGD6",            BIT(4)},
	/*
	 * Reserved for Cannon Lake but valid for Ice Lake, Comet Lake,
	 * Tiger Lake and ELkhart Lake.
	 */
	{"PSF6",		BIT(5)},
	{"PSF7",		BIT(6)},
	{"PSF8",		BIT(7)},
	{}
};

static const struct pmc_bit_map *ext_cnp_pfear_map[] = {
	cnp_pfear_map,
	NULL
};

static const struct pmc_bit_map icl_pfear_map[] = {
	/* Ice Lake generation onwards only */
	{"RES_65",		BIT(0)},
	{"RES_66",		BIT(1)},
	{"RES_67",		BIT(2)},
	{"TAM",			BIT(3)},
	{"GBETSN",		BIT(4)},
	{"TBTLSX",		BIT(5)},
	{"RES_71",		BIT(6)},
	{"RES_72",		BIT(7)},
	{}
};

static const struct pmc_bit_map *ext_icl_pfear_map[] = {
	cnp_pfear_map,
	icl_pfear_map,
	NULL
};

static const struct pmc_bit_map tgl_pfear_map[] = {
	/* Tiger Lake and Elkhart Lake generation onwards only */
	{"PSF9",		BIT(0)},
	{"RES_66",		BIT(1)},
	{"RES_67",		BIT(2)},
	{"RES_68",		BIT(3)},
	{"RES_69",		BIT(4)},
	{"RES_70",		BIT(5)},
	{"TBTLSX",		BIT(6)},
	{}
};

static const struct pmc_bit_map *ext_tgl_pfear_map[] = {
	cnp_pfear_map,
	tgl_pfear_map,
	NULL
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
	NULL
};

static const struct pmc_bit_map cnp_ltr_show_map[] = {
	{"SOUTHPORT_A",		CNP_PMC_LTR_SPA},
	{"SOUTHPORT_B",		CNP_PMC_LTR_SPB},
	{"SATA",		CNP_PMC_LTR_SATA},
	{"GIGABIT_ETHERNET",	CNP_PMC_LTR_GBE},
	{"XHCI",		CNP_PMC_LTR_XHCI},
	{"Reserved",		CNP_PMC_LTR_RESERVED},
	{"ME",			CNP_PMC_LTR_ME},
	/* EVA is Enterprise Value Add, doesn't really exist on PCH */
	{"EVA",			CNP_PMC_LTR_EVA},
	{"SOUTHPORT_C",		CNP_PMC_LTR_SPC},
	{"HD_AUDIO",		CNP_PMC_LTR_AZ},
	{"CNV",			CNP_PMC_LTR_CNV},
	{"LPSS",		CNP_PMC_LTR_LPSS},
	{"SOUTHPORT_D",		CNP_PMC_LTR_SPD},
	{"SOUTHPORT_E",		CNP_PMC_LTR_SPE},
	{"CAMERA",		CNP_PMC_LTR_CAM},
	{"ESPI",		CNP_PMC_LTR_ESPI},
	{"SCC",			CNP_PMC_LTR_SCC},
	{"ISH",			CNP_PMC_LTR_ISH},
	{"UFSX2",		CNP_PMC_LTR_UFSX2},
	{"EMMC",		CNP_PMC_LTR_EMMC},
	/* Reserved for Cannon Lake but valid for Ice Lake */
	{"WIGIG",		ICL_PMC_LTR_WIGIG},
	/* Below two cannot be used for LTR_IGNORE */
	{"CURRENT_PLATFORM",	CNP_PMC_LTR_CUR_PLT},
	{"AGGREGATED_SYSTEM",	CNP_PMC_LTR_CUR_ASLT},
	{}
};

static const struct pmc_reg_map cnp_reg_map = {
	.pfear_sts = ext_cnp_pfear_map,
	.slp_s0_offset = CNP_PMC_SLP_S0_RES_COUNTER_OFFSET,
	.slps0_dbg_maps = cnp_slps0_dbg_maps,
	.ltr_show_sts = cnp_ltr_show_map,
	.msr_sts = msr_map,
	.slps0_dbg_offset = CNP_PMC_SLPS0_DBG_OFFSET,
	.ltr_ignore_offset = CNP_PMC_LTR_IGNORE_OFFSET,
	.regmap_length = CNP_PMC_MMIO_REG_LEN,
	.ppfear0_offset = CNP_PMC_HOST_PPFEAR0A,
	.ppfear_buckets = CNP_PPFEAR_NUM_ENTRIES,
	.pm_cfg_offset = CNP_PMC_PM_CFG_OFFSET,
	.pm_read_disable_bit = CNP_PMC_READ_DISABLE_BIT,
	.ltr_ignore_max = CNP_NUM_IP_IGN_ALLOWED,
};

static const struct pmc_reg_map icl_reg_map = {
	.pfear_sts = ext_icl_pfear_map,
	.slp_s0_offset = CNP_PMC_SLP_S0_RES_COUNTER_OFFSET,
	.slps0_dbg_maps = cnp_slps0_dbg_maps,
	.ltr_show_sts = cnp_ltr_show_map,
	.msr_sts = msr_map,
	.slps0_dbg_offset = CNP_PMC_SLPS0_DBG_OFFSET,
	.ltr_ignore_offset = CNP_PMC_LTR_IGNORE_OFFSET,
	.regmap_length = CNP_PMC_MMIO_REG_LEN,
	.ppfear0_offset = CNP_PMC_HOST_PPFEAR0A,
	.ppfear_buckets = ICL_PPFEAR_NUM_ENTRIES,
	.pm_cfg_offset = CNP_PMC_PM_CFG_OFFSET,
	.pm_read_disable_bit = CNP_PMC_READ_DISABLE_BIT,
	.ltr_ignore_max = ICL_NUM_IP_IGN_ALLOWED,
};

static const struct pmc_reg_map tgl_reg_map = {
	.pfear_sts = ext_tgl_pfear_map,
	.slp_s0_offset = CNP_PMC_SLP_S0_RES_COUNTER_OFFSET,
	.slps0_dbg_maps = cnp_slps0_dbg_maps,
	.ltr_show_sts = cnp_ltr_show_map,
	.msr_sts = msr_map,
	.slps0_dbg_offset = CNP_PMC_SLPS0_DBG_OFFSET,
	.ltr_ignore_offset = CNP_PMC_LTR_IGNORE_OFFSET,
	.regmap_length = CNP_PMC_MMIO_REG_LEN,
	.ppfear0_offset = CNP_PMC_HOST_PPFEAR0A,
	.ppfear_buckets = ICL_PPFEAR_NUM_ENTRIES,
	.pm_cfg_offset = CNP_PMC_PM_CFG_OFFSET,
	.pm_read_disable_bit = CNP_PMC_READ_DISABLE_BIT,
	.ltr_ignore_max = TGL_NUM_IP_IGN_ALLOWED,
};

static inline u32 pmc_core_reg_read(struct pmc_dev *pmcdev, int reg_offset)
{
	return readl(pmcdev->regbase + reg_offset);
}

static inline void pmc_core_reg_write(struct pmc_dev *pmcdev, int reg_offset,
				      u32 val)
{
	writel(val, pmcdev->regbase + reg_offset);
}

static inline u64 pmc_core_adjust_slp_s0_step(u32 value)
{
	return (u64)value * SPT_PMC_SLP_S0_RES_COUNTER_STEP;
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

static inline u8 pmc_core_reg_read_byte(struct pmc_dev *pmcdev, int offset)
{
	return readb(pmcdev->regbase + offset);
}

static void pmc_core_display_map(struct seq_file *s, int index, int idx, int ip,
				 u8 pf_reg, const struct pmc_bit_map **pf_map)
{
	seq_printf(s, "PCH IP: %-2d - %-32s\tState: %s\n",
		   ip, pf_map[idx][index].name,
		   pf_map[idx][index].bit_mask & pf_reg ? "Off" : "On");
}

static int pmc_core_ppfear_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	const struct pmc_bit_map **maps = pmcdev->map->pfear_sts;
	u8 pf_regs[PPFEAR_MAX_NUM_ENTRIES];
	int index, iter, idx, ip = 0;

	iter = pmcdev->map->ppfear0_offset;

	for (index = 0; index < pmcdev->map->ppfear_buckets &&
	     index < PPFEAR_MAX_NUM_ENTRIES; index++, iter++)
		pf_regs[index] = pmc_core_reg_read_byte(pmcdev, iter);

	for (idx = 0; maps[idx]; idx++) {
		for (index = 0; maps[idx][index].name &&
		     index < pmcdev->map->ppfear_buckets * 8; ip++, index++)
			pmc_core_display_map(s, index, idx, ip,
					     pf_regs[index / 8], maps);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_ppfear);

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

static int pmc_core_mphy_pg_show(struct seq_file *s, void *unused)
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
DEFINE_SHOW_ATTRIBUTE(pmc_core_mphy_pg);

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
DEFINE_SHOW_ATTRIBUTE(pmc_core_pll);

static ssize_t pmc_core_ltr_ignore_write(struct file *file,
					 const char __user *userbuf,
					 size_t count, loff_t *ppos)
{
	struct pmc_dev *pmcdev = &pmc;
	const struct pmc_reg_map *map = pmcdev->map;
	u32 val, buf_size, fd;
	int err;

	buf_size = count < 64 ? count : 64;

	err = kstrtou32_from_user(userbuf, buf_size, 10, &val);
	if (err)
		return err;

	mutex_lock(&pmcdev->lock);

	if (val > map->ltr_ignore_max) {
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

static u32 convert_ltr_scale(u32 val)
{
	/*
	 * As per PCIE specification supporting document
	 * ECN_LatencyTolnReporting_14Aug08.pdf the Latency
	 * Tolerance Reporting data payload is encoded in a
	 * 3 bit scale and 10 bit value fields. Values are
	 * multiplied by the indicated scale to yield an absolute time
	 * value, expressible in a range from 1 nanosecond to
	 * 2^25*(2^10-1) = 34,326,183,936 nanoseconds.
	 *
	 * scale encoding is as follows:
	 *
	 * ----------------------------------------------
	 * |scale factor	|	Multiplier (ns)	|
	 * ----------------------------------------------
	 * |	0		|	1		|
	 * |	1		|	32		|
	 * |	2		|	1024		|
	 * |	3		|	32768		|
	 * |	4		|	1048576		|
	 * |	5		|	33554432	|
	 * |	6		|	Invalid		|
	 * |	7		|	Invalid		|
	 * ----------------------------------------------
	 */
	if (val > 5) {
		pr_warn("Invalid LTR scale factor.\n");
		return 0;
	}

	return 1U << (5 * val);
}

static int pmc_core_ltr_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	const struct pmc_bit_map *map = pmcdev->map->ltr_show_sts;
	u64 decoded_snoop_ltr, decoded_non_snoop_ltr;
	u32 ltr_raw_data, scale, val;
	u16 snoop_ltr, nonsnoop_ltr;
	int index;

	for (index = 0; map[index].name ; index++) {
		decoded_snoop_ltr = decoded_non_snoop_ltr = 0;
		ltr_raw_data = pmc_core_reg_read(pmcdev,
						 map[index].bit_mask);
		snoop_ltr = ltr_raw_data & ~MTPMC_MASK;
		nonsnoop_ltr = (ltr_raw_data >> 0x10) & ~MTPMC_MASK;

		if (FIELD_GET(LTR_REQ_NONSNOOP, ltr_raw_data)) {
			scale = FIELD_GET(LTR_DECODED_SCALE, nonsnoop_ltr);
			val = FIELD_GET(LTR_DECODED_VAL, nonsnoop_ltr);
			decoded_non_snoop_ltr = val * convert_ltr_scale(scale);
		}

		if (FIELD_GET(LTR_REQ_SNOOP, ltr_raw_data)) {
			scale = FIELD_GET(LTR_DECODED_SCALE, snoop_ltr);
			val = FIELD_GET(LTR_DECODED_VAL, snoop_ltr);
			decoded_snoop_ltr = val * convert_ltr_scale(scale);
		}

		seq_printf(s, "%-32s\tLTR: RAW: 0x%-16x\tNon-Snoop(ns): %-16llu\tSnoop(ns): %-16llu\n",
			   map[index].name, ltr_raw_data,
			   decoded_non_snoop_ltr,
			   decoded_snoop_ltr);
	}
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_ltr);

static int pmc_core_pkgc_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	const struct pmc_bit_map *map = pmcdev->map->msr_sts;
	u64 pcstate_count;
	int index;

	for (index = 0; map[index].name ; index++) {
		if (rdmsrl_safe(map[index].bit_mask, &pcstate_count))
			continue;

		pcstate_count *= 1000;
		do_div(pcstate_count, tsc_khz);
		seq_printf(s, "%-8s : %llu\n", map[index].name,
			   pcstate_count);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_pkgc);

static void pmc_core_dbgfs_unregister(struct pmc_dev *pmcdev)
{
	debugfs_remove_recursive(pmcdev->dbgfs_dir);
}

static void pmc_core_dbgfs_register(struct pmc_dev *pmcdev)
{
	struct dentry *dir;

	dir = debugfs_create_dir("pmc_core", NULL);
	pmcdev->dbgfs_dir = dir;

	debugfs_create_file("slp_s0_residency_usec", 0444, dir, pmcdev,
			    &pmc_core_dev_state);

	if (pmcdev->map->pfear_sts)
		debugfs_create_file("pch_ip_power_gating_status", 0444, dir,
				    pmcdev, &pmc_core_ppfear_fops);

	debugfs_create_file("ltr_ignore", 0644, dir, pmcdev,
			    &pmc_core_ltr_ignore_ops);

	debugfs_create_file("ltr_show", 0444, dir, pmcdev, &pmc_core_ltr_fops);

	debugfs_create_file("package_cstate_show", 0444, dir, pmcdev,
			    &pmc_core_pkgc_fops);

	if (pmcdev->map->pll_sts)
		debugfs_create_file("pll_status", 0444, dir, pmcdev,
				    &pmc_core_pll_fops);

	if (pmcdev->map->mphy_sts)
		debugfs_create_file("mphy_core_lanes_power_gating_status",
				    0444, dir, pmcdev,
				    &pmc_core_mphy_pg_fops);

	if (pmcdev->map->slps0_dbg_maps) {
		debugfs_create_file("slp_s0_debug_status", 0444,
				    dir, pmcdev,
				    &pmc_core_slps0_dbg_fops);

		debugfs_create_bool("slp_s0_dbg_latch", 0644,
				    dir, &slps0_dbg_latch);
	}
}
#else
static inline void pmc_core_dbgfs_register(struct pmc_dev *pmcdev)
{
}

static inline void pmc_core_dbgfs_unregister(struct pmc_dev *pmcdev)
{
}
#endif /* CONFIG_DEBUG_FS */

static const struct x86_cpu_id intel_pmc_core_ids[] = {
	INTEL_CPU_FAM6(SKYLAKE_L, spt_reg_map),
	INTEL_CPU_FAM6(SKYLAKE, spt_reg_map),
	INTEL_CPU_FAM6(KABYLAKE_L, spt_reg_map),
	INTEL_CPU_FAM6(KABYLAKE, spt_reg_map),
	INTEL_CPU_FAM6(CANNONLAKE_L, cnp_reg_map),
	INTEL_CPU_FAM6(ICELAKE_L, icl_reg_map),
	INTEL_CPU_FAM6(ICELAKE_NNPI, icl_reg_map),
	INTEL_CPU_FAM6(COMETLAKE, cnp_reg_map),
	INTEL_CPU_FAM6(COMETLAKE_L, cnp_reg_map),
	INTEL_CPU_FAM6(TIGERLAKE_L, tgl_reg_map),
	INTEL_CPU_FAM6(TIGERLAKE, tgl_reg_map),
	INTEL_CPU_FAM6(ATOM_TREMONT, tgl_reg_map),
	{}
};

MODULE_DEVICE_TABLE(x86cpu, intel_pmc_core_ids);

static const struct pci_device_id pmc_pci_ids[] = {
	{ PCI_VDEVICE(INTEL, SPT_PMC_PCI_DEVICE_ID) },
	{ }
};

/*
 * This quirk can be used on those platforms where
 * the platform BIOS enforces 24Mhz crystal to shutdown
 * before PMC can assert SLP_S0#.
 */
static int quirk_xtal_ignore(const struct dmi_system_id *id)
{
	struct pmc_dev *pmcdev = &pmc;
	u32 value;

	value = pmc_core_reg_read(pmcdev, pmcdev->map->pm_vric1_offset);
	/* 24MHz Crystal Shutdown Qualification Disable */
	value |= SPT_PMC_VRIC1_XTALSDQDIS;
	/* Low Voltage Mode Enable */
	value &= ~SPT_PMC_VRIC1_SLPS0LVEN;
	pmc_core_reg_write(pmcdev, pmcdev->map->pm_vric1_offset, value);
	return 0;
}

static const struct dmi_system_id pmc_core_dmi_table[]  = {
	{
	.callback = quirk_xtal_ignore,
	.ident = "HP Elite x2 1013 G3",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "HP"),
		DMI_MATCH(DMI_PRODUCT_NAME, "HP Elite x2 1013 G3"),
		},
	},
	{}
};

static int pmc_core_probe(struct platform_device *pdev)
{
	static bool device_initialized;
	struct pmc_dev *pmcdev = &pmc;
	const struct x86_cpu_id *cpu_id;
	u64 slp_s0_addr;

	if (device_initialized)
		return -ENODEV;

	cpu_id = x86_match_cpu(intel_pmc_core_ids);
	if (!cpu_id)
		return -ENODEV;

	pmcdev->map = (struct pmc_reg_map *)cpu_id->driver_data;

	/*
	 * Coffee Lake has CPU ID of Kaby Lake and Cannon Lake PCH. So here
	 * Sunrisepoint PCH regmap can't be used. Use Cannon Lake PCH regmap
	 * in this case.
	 */
	if (pmcdev->map == &spt_reg_map && !pci_dev_present(pmc_pci_ids))
		pmcdev->map = &cnp_reg_map;

	if (lpit_read_residency_count_address(&slp_s0_addr)) {
		pmcdev->base_addr = PMC_BASE_ADDR_DEFAULT;

		if (page_is_ram(PHYS_PFN(pmcdev->base_addr)))
			return -ENODEV;
	} else {
		pmcdev->base_addr = slp_s0_addr - pmcdev->map->slp_s0_offset;
	}

	pmcdev->regbase = ioremap(pmcdev->base_addr,
				  pmcdev->map->regmap_length);
	if (!pmcdev->regbase)
		return -ENOMEM;

	mutex_init(&pmcdev->lock);
	platform_set_drvdata(pdev, pmcdev);
	pmcdev->pmc_xram_read_bit = pmc_core_check_read_lock_bit();
	dmi_check_system(pmc_core_dmi_table);

	pmc_core_dbgfs_register(pmcdev);

	device_initialized = true;
	dev_info(&pdev->dev, " initialized\n");

	return 0;
}

static int pmc_core_remove(struct platform_device *pdev)
{
	struct pmc_dev *pmcdev = platform_get_drvdata(pdev);

	pmc_core_dbgfs_unregister(pmcdev);
	platform_set_drvdata(pdev, NULL);
	mutex_destroy(&pmcdev->lock);
	iounmap(pmcdev->regbase);
	return 0;
}

#ifdef CONFIG_PM_SLEEP

static bool warn_on_s0ix_failures;
module_param(warn_on_s0ix_failures, bool, 0644);
MODULE_PARM_DESC(warn_on_s0ix_failures, "Check and warn for S0ix failures");

static int pmc_core_suspend(struct device *dev)
{
	struct pmc_dev *pmcdev = dev_get_drvdata(dev);

	pmcdev->check_counters = false;

	/* No warnings on S0ix failures */
	if (!warn_on_s0ix_failures)
		return 0;

	/* Check if the syspend will actually use S0ix */
	if (pm_suspend_via_firmware())
		return 0;

	/* Save PC10 residency for checking later */
	if (rdmsrl_safe(MSR_PKG_C10_RESIDENCY, &pmcdev->pc10_counter))
		return -EIO;

	/* Save S0ix residency for checking later */
	if (pmc_core_dev_state_get(pmcdev, &pmcdev->s0ix_counter))
		return -EIO;

	pmcdev->check_counters = true;
	return 0;
}

static inline bool pmc_core_is_pc10_failed(struct pmc_dev *pmcdev)
{
	u64 pc10_counter;

	if (rdmsrl_safe(MSR_PKG_C10_RESIDENCY, &pc10_counter))
		return false;

	if (pc10_counter == pmcdev->pc10_counter)
		return true;

	return false;
}

static inline bool pmc_core_is_s0ix_failed(struct pmc_dev *pmcdev)
{
	u64 s0ix_counter;

	if (pmc_core_dev_state_get(pmcdev, &s0ix_counter))
		return false;

	if (s0ix_counter == pmcdev->s0ix_counter)
		return true;

	return false;
}

static int pmc_core_resume(struct device *dev)
{
	struct pmc_dev *pmcdev = dev_get_drvdata(dev);
	const struct pmc_bit_map **maps = pmcdev->map->slps0_dbg_maps;
	int offset = pmcdev->map->slps0_dbg_offset;
	const struct pmc_bit_map *map;
	u32 data;

	if (!pmcdev->check_counters)
		return 0;

	if (!pmc_core_is_s0ix_failed(pmcdev))
		return 0;

	if (pmc_core_is_pc10_failed(pmcdev)) {
		/* S0ix failed because of PC10 entry failure */
		dev_info(dev, "CPU did not enter PC10!!! (PC10 cnt=0x%llx)\n",
			 pmcdev->pc10_counter);
		return 0;
	}

	/* The real interesting case - S0ix failed - lets ask PMC why. */
	dev_warn(dev, "CPU did not enter SLP_S0!!! (S0ix cnt=%llu)\n",
		 pmcdev->s0ix_counter);
	while (*maps) {
		map = *maps;
		data = pmc_core_reg_read(pmcdev, offset);
		offset += 4;
		while (map->name) {
			dev_dbg(dev, "SLP_S0_DBG: %-32s\tState: %s\n",
				map->name,
				data & map->bit_mask ? "Yes" : "No");
			map++;
		}
		maps++;
	}
	return 0;
}

#endif

static const struct dev_pm_ops pmc_core_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(pmc_core_suspend, pmc_core_resume)
};

static const struct acpi_device_id pmc_core_acpi_ids[] = {
	{"INT33A1", 0}, /* _HID for Intel Power Engine, _CID PNP0D80*/
	{ }
};
MODULE_DEVICE_TABLE(acpi, pmc_core_acpi_ids);

static struct platform_driver pmc_core_driver = {
	.driver = {
		.name = "intel_pmc_core",
		.acpi_match_table = ACPI_PTR(pmc_core_acpi_ids),
		.pm = &pmc_core_pm_ops,
	},
	.probe = pmc_core_probe,
	.remove = pmc_core_remove,
};

module_platform_driver(pmc_core_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel PMC Core Driver");
