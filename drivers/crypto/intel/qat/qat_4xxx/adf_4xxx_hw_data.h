/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2020 Intel Corporation */
#ifndef ADF_4XXX_HW_DATA_H_
#define ADF_4XXX_HW_DATA_H_

#include <linux/units.h>
#include <adf_accel_devices.h>

/* PCIe configuration space */
#define ADF_4XXX_SRAM_BAR		0
#define ADF_4XXX_PMISC_BAR		1
#define ADF_4XXX_ETR_BAR		2
#define ADF_4XXX_RX_RINGS_OFFSET	1
#define ADF_4XXX_TX_RINGS_MASK		0x1
#define ADF_4XXX_MAX_ACCELERATORS	1
#define ADF_4XXX_MAX_ACCELENGINES	9
#define ADF_4XXX_BAR_MASK		(BIT(0) | BIT(2) | BIT(4))

/* Physical function fuses */
#define ADF_4XXX_FUSECTL0_OFFSET	(0x2C8)
#define ADF_4XXX_FUSECTL1_OFFSET	(0x2CC)
#define ADF_4XXX_FUSECTL2_OFFSET	(0x2D0)
#define ADF_4XXX_FUSECTL3_OFFSET	(0x2D4)
#define ADF_4XXX_FUSECTL4_OFFSET	(0x2D8)
#define ADF_4XXX_FUSECTL5_OFFSET	(0x2DC)

#define ADF_4XXX_ACCELERATORS_MASK	(0x1)
#define ADF_4XXX_ACCELENGINES_MASK	(0x1FF)
#define ADF_4XXX_ADMIN_AE_MASK		(0x100)

#define ADF_4XXX_HICPPAGENTCMDPARERRLOG_MASK	0x1F
#define ADF_4XXX_PARITYERRORMASK_ATH_CPH_MASK	0xF000F
#define ADF_4XXX_PARITYERRORMASK_CPR_XLT_MASK	0x10001
#define ADF_4XXX_PARITYERRORMASK_DCPR_UCS_MASK	0x30007
#define ADF_4XXX_PARITYERRORMASK_PKE_MASK	0x3F

/*
 * SSMFEATREN bit mask
 * BIT(4) - enables parity detection on CPP
 * BIT(12) - enables the logging of push/pull data errors
 *	     in pperr register
 * BIT(16) - BIT(23) - enable parity detection on SPPs
 */
#define ADF_4XXX_SSMFEATREN_MASK \
	(BIT(4) | BIT(12) | BIT(16) | BIT(17) | BIT(18) | \
	 BIT(19) | BIT(20) | BIT(21) | BIT(22) | BIT(23))

#define ADF_4XXX_ETR_MAX_BANKS		64

/* MSIX interrupt */
#define ADF_4XXX_SMIAPF_RP_X0_MASK_OFFSET	(0x41A040)
#define ADF_4XXX_SMIAPF_RP_X1_MASK_OFFSET	(0x41A044)
#define ADF_4XXX_SMIAPF_MASK_OFFSET		(0x41A084)
#define ADF_4XXX_MSIX_RTTABLE_OFFSET(i)		(0x409000 + ((i) * 0x04))

/* Bank and ring configuration */
#define ADF_4XXX_NUM_RINGS_PER_BANK	2
#define ADF_4XXX_NUM_BANKS_PER_VF	4

/* Arbiter configuration */
#define ADF_4XXX_ARB_CONFIG			(BIT(31) | BIT(6) | BIT(0))
#define ADF_4XXX_ARB_OFFSET			(0x0)
#define ADF_4XXX_ARB_WRK_2_SER_MAP_OFFSET	(0x400)

/* Admin Interface Reg Offset */
#define ADF_4XXX_ADMINMSGUR_OFFSET	(0x500574)
#define ADF_4XXX_ADMINMSGLR_OFFSET	(0x500578)
#define ADF_4XXX_MAILBOX_BASE_OFFSET	(0x600970)

/* Firmware Binaries */
#define ADF_4XXX_FW		"qat_4xxx.bin"
#define ADF_4XXX_MMP		"qat_4xxx_mmp.bin"
#define ADF_4XXX_SYM_OBJ	"qat_4xxx_sym.bin"
#define ADF_4XXX_DC_OBJ		"qat_4xxx_dc.bin"
#define ADF_4XXX_ASYM_OBJ	"qat_4xxx_asym.bin"
#define ADF_4XXX_ADMIN_OBJ	"qat_4xxx_admin.bin"
/* Firmware for 402XXX */
#define ADF_402XX_FW		"qat_402xx.bin"
#define ADF_402XX_MMP		"qat_402xx_mmp.bin"
#define ADF_402XX_SYM_OBJ	"qat_402xx_sym.bin"
#define ADF_402XX_DC_OBJ	"qat_402xx_dc.bin"
#define ADF_402XX_ASYM_OBJ	"qat_402xx_asym.bin"
#define ADF_402XX_ADMIN_OBJ	"qat_402xx_admin.bin"

/* RL constants */
#define ADF_4XXX_RL_PCIE_SCALE_FACTOR_DIV	100
#define ADF_4XXX_RL_PCIE_SCALE_FACTOR_MUL	102
#define ADF_4XXX_RL_DCPR_CORRECTION		1
#define ADF_4XXX_RL_SCANS_PER_SEC		954
#define ADF_4XXX_RL_MAX_TP_ASYM			173750UL
#define ADF_4XXX_RL_MAX_TP_SYM			95000UL
#define ADF_4XXX_RL_MAX_TP_DC			45000UL
#define ADF_4XXX_RL_SLICE_REF			1000UL

/* Clocks frequency */
#define ADF_4XXX_KPT_COUNTER_FREQ	(100 * HZ_PER_MHZ)
#define ADF_4XXX_AE_FREQ		(1000 * HZ_PER_MHZ)

/* qat_4xxx fuse bits are different from old GENs, redefine them */
enum icp_qat_4xxx_slice_mask {
	ICP_ACCEL_4XXX_MASK_CIPHER_SLICE = BIT(0),
	ICP_ACCEL_4XXX_MASK_AUTH_SLICE = BIT(1),
	ICP_ACCEL_4XXX_MASK_PKE_SLICE = BIT(2),
	ICP_ACCEL_4XXX_MASK_COMPRESS_SLICE = BIT(3),
	ICP_ACCEL_4XXX_MASK_UCS_SLICE = BIT(4),
	ICP_ACCEL_4XXX_MASK_EIA3_SLICE = BIT(5),
	ICP_ACCEL_4XXX_MASK_SMX_SLICE = BIT(7),
};

void adf_init_hw_data_4xxx(struct adf_hw_device_data *hw_data, u32 dev_id);
void adf_clean_hw_data_4xxx(struct adf_hw_device_data *hw_data);
int adf_gen4_dev_config(struct adf_accel_dev *accel_dev);

#endif
