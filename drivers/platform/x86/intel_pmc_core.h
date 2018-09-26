/*
 * Intel Core SoC Power Management Controller Header File
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

#ifndef PMC_CORE_H
#define PMC_CORE_H

#define PMC_BASE_ADDR_DEFAULT			0xFE000000

/* Sunrise Point Power Management Controller PCI Device ID */
#define SPT_PMC_PCI_DEVICE_ID			0x9d21
#define SPT_PMC_BASE_ADDR_OFFSET		0x48
#define SPT_PMC_SLP_S0_RES_COUNTER_OFFSET	0x13c
#define SPT_PMC_PM_CFG_OFFSET			0x18
#define SPT_PMC_PM_STS_OFFSET			0x1c
#define SPT_PMC_MTPMC_OFFSET			0x20
#define SPT_PMC_MFPMC_OFFSET			0x38
#define SPT_PMC_LTR_IGNORE_OFFSET		0x30C
#define SPT_PMC_MPHY_CORE_STS_0			0x1143
#define SPT_PMC_MPHY_CORE_STS_1			0x1142
#define SPT_PMC_MPHY_COM_STS_0			0x1155
#define SPT_PMC_MMIO_REG_LEN			0x1000
#define SPT_PMC_SLP_S0_RES_COUNTER_STEP		0x64
#define PMC_BASE_ADDR_MASK			~(SPT_PMC_MMIO_REG_LEN - 1)
#define MTPMC_MASK				0xffff0000
#define PPFEAR_MAX_NUM_ENTRIES			5
#define SPT_PPFEAR_NUM_ENTRIES			5
#define SPT_PMC_READ_DISABLE_BIT		0x16
#define SPT_PMC_MSG_FULL_STS_BIT		0x18
#define NUM_RETRIES				100
#define NUM_IP_IGN_ALLOWED			17

/* Sunrise Point: PGD PFET Enable Ack Status Registers */
enum ppfear_regs {
	SPT_PMC_XRAM_PPFEAR0A = 0x590,
	SPT_PMC_XRAM_PPFEAR0B,
	SPT_PMC_XRAM_PPFEAR0C,
	SPT_PMC_XRAM_PPFEAR0D,
	SPT_PMC_XRAM_PPFEAR1A,
};

#define SPT_PMC_BIT_PMC				BIT(0)
#define SPT_PMC_BIT_OPI				BIT(1)
#define SPT_PMC_BIT_SPI				BIT(2)
#define SPT_PMC_BIT_XHCI			BIT(3)
#define SPT_PMC_BIT_SPA				BIT(4)
#define SPT_PMC_BIT_SPB				BIT(5)
#define SPT_PMC_BIT_SPC				BIT(6)
#define SPT_PMC_BIT_GBE				BIT(7)

#define SPT_PMC_BIT_SATA			BIT(0)
#define SPT_PMC_BIT_HDA_PGD0			BIT(1)
#define SPT_PMC_BIT_HDA_PGD1			BIT(2)
#define SPT_PMC_BIT_HDA_PGD2			BIT(3)
#define SPT_PMC_BIT_HDA_PGD3			BIT(4)
#define SPT_PMC_BIT_RSVD_0B			BIT(5)
#define SPT_PMC_BIT_LPSS			BIT(6)
#define SPT_PMC_BIT_LPC				BIT(7)

#define SPT_PMC_BIT_SMB				BIT(0)
#define SPT_PMC_BIT_ISH				BIT(1)
#define SPT_PMC_BIT_P2SB			BIT(2)
#define SPT_PMC_BIT_DFX				BIT(3)
#define SPT_PMC_BIT_SCC				BIT(4)
#define SPT_PMC_BIT_RSVD_0C			BIT(5)
#define SPT_PMC_BIT_FUSE			BIT(6)
#define SPT_PMC_BIT_CAMREA			BIT(7)

#define SPT_PMC_BIT_RSVD_0D			BIT(0)
#define SPT_PMC_BIT_USB3_OTG			BIT(1)
#define SPT_PMC_BIT_EXI				BIT(2)
#define SPT_PMC_BIT_CSE				BIT(3)
#define SPT_PMC_BIT_CSME_KVM			BIT(4)
#define SPT_PMC_BIT_CSME_PMT			BIT(5)
#define SPT_PMC_BIT_CSME_CLINK			BIT(6)
#define SPT_PMC_BIT_CSME_PTIO			BIT(7)

#define SPT_PMC_BIT_CSME_USBR			BIT(0)
#define SPT_PMC_BIT_CSME_SUSRAM			BIT(1)
#define SPT_PMC_BIT_CSME_SMT			BIT(2)
#define SPT_PMC_BIT_RSVD_1A			BIT(3)
#define SPT_PMC_BIT_CSME_SMS2			BIT(4)
#define SPT_PMC_BIT_CSME_SMS1			BIT(5)
#define SPT_PMC_BIT_CSME_RTC			BIT(6)
#define SPT_PMC_BIT_CSME_PSF			BIT(7)

#define SPT_PMC_BIT_MPHY_LANE0			BIT(0)
#define SPT_PMC_BIT_MPHY_LANE1			BIT(1)
#define SPT_PMC_BIT_MPHY_LANE2			BIT(2)
#define SPT_PMC_BIT_MPHY_LANE3			BIT(3)
#define SPT_PMC_BIT_MPHY_LANE4			BIT(4)
#define SPT_PMC_BIT_MPHY_LANE5			BIT(5)
#define SPT_PMC_BIT_MPHY_LANE6			BIT(6)
#define SPT_PMC_BIT_MPHY_LANE7			BIT(7)

#define SPT_PMC_BIT_MPHY_LANE8			BIT(0)
#define SPT_PMC_BIT_MPHY_LANE9			BIT(1)
#define SPT_PMC_BIT_MPHY_LANE10			BIT(2)
#define SPT_PMC_BIT_MPHY_LANE11			BIT(3)
#define SPT_PMC_BIT_MPHY_LANE12			BIT(4)
#define SPT_PMC_BIT_MPHY_LANE13			BIT(5)
#define SPT_PMC_BIT_MPHY_LANE14			BIT(6)
#define SPT_PMC_BIT_MPHY_LANE15			BIT(7)

#define SPT_PMC_BIT_MPHY_CMN_LANE0		BIT(0)
#define SPT_PMC_BIT_MPHY_CMN_LANE1		BIT(1)
#define SPT_PMC_BIT_MPHY_CMN_LANE2		BIT(2)
#define SPT_PMC_BIT_MPHY_CMN_LANE3		BIT(3)

/* Cannonlake Power Management Controller register offsets */
#define CNP_PMC_SLP_S0_RES_COUNTER_OFFSET      0x193C
#define CNP_PMC_LTR_IGNORE_OFFSET              0x1B0C
#define CNP_PMC_PM_CFG_OFFSET                  0x1818
#define CNP_PMC_SLPS0_DBG_OFFSET		0x10B4
/* Cannonlake: PGD PFET Enable Ack Status Register(s) start */
#define CNP_PMC_HOST_PPFEAR0A                  0x1D90

#define CNP_PMC_MMIO_REG_LEN                   0x2000
#define CNP_PPFEAR_NUM_ENTRIES                 8
#define CNP_PMC_READ_DISABLE_BIT               22
#define CNP_PMC_LATCH_SLPS0_EVENTS		BIT(31)

struct pmc_bit_map {
	const char *name;
	u32 bit_mask;
};

/**
 * struct pmc_reg_map - Structure used to define parameter unique to a
			PCH family
 * @pfear_sts:		Maps name of IP block to PPFEAR* bit
 * @mphy_sts:		Maps name of MPHY lane to MPHY status lane status bit
 * @pll_sts:		Maps name of PLL to corresponding bit status
 * @slps0_dbg_maps:	Array of SLP_S0_DBG* registers containing debug info
 * @slp_s0_offset:	PWRMBASE offset to read SLP_S0 residency
 * @ltr_ignore_offset:	PWRMBASE offset to read/write LTR ignore bit
 * @regmap_length:	Length of memory to map from PWRMBASE address to access
 * @ppfear0_offset:	PWRMBASE offset to to read PPFEAR*
 * @ppfear_buckets:	Number of 8 bits blocks to read all IP blocks from
 *			PPFEAR
 * @pm_cfg_offset:	PWRMBASE offset to PM_CFG register
 * @pm_read_disable_bit: Bit index to read PMC_READ_DISABLE
 * @slps0_dbg_offset:	PWRMBASE offset to SLP_S0_DEBUG_REG*
 *
 * Each PCH has unique set of register offsets and bit indexes. This structure
 * captures them to have a common implementation.
 */
struct pmc_reg_map {
	const struct pmc_bit_map *pfear_sts;
	const struct pmc_bit_map *mphy_sts;
	const struct pmc_bit_map *pll_sts;
	const struct pmc_bit_map **slps0_dbg_maps;
	const u32 slp_s0_offset;
	const u32 ltr_ignore_offset;
	const int regmap_length;
	const u32 ppfear0_offset;
	const int ppfear_buckets;
	const u32 pm_cfg_offset;
	const int pm_read_disable_bit;
	const u32 slps0_dbg_offset;
};

/**
 * struct pmc_dev - pmc device structure
 * @base_addr:		contains pmc base address
 * @regbase:		pointer to io-remapped memory location
 * @map:		pointer to pmc_reg_map struct that contains platform
 *			specific attributes
 * @dbgfs_dir:		path to debugfs interface
 * @pmc_xram_read_bit:	flag to indicate whether PMC XRAM shadow registers
 *			used to read MPHY PG and PLL status are available
 * @mutex_lock:		mutex to complete one transcation
 *
 * pmc_dev contains info about power management controller device.
 */
struct pmc_dev {
	u32 base_addr;
	void __iomem *regbase;
	const struct pmc_reg_map *map;
#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct dentry *dbgfs_dir;
#endif /* CONFIG_DEBUG_FS */
	int pmc_xram_read_bit;
	struct mutex lock; /* generic mutex lock for PMC Core */
};

#endif /* PMC_CORE_H */
