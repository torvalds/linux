/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2020 Intel Corporation */
#ifndef ADF_4XXX_HW_DATA_H_
#define ADF_4XXX_HW_DATA_H_

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

/* qat_4xxx fuse bits are different from old GENs, redefine them */
enum icp_qat_4xxx_slice_mask {
	ICP_ACCEL_4XXX_MASK_CIPHER_SLICE = BIT(0),
	ICP_ACCEL_4XXX_MASK_AUTH_SLICE = BIT(1),
	ICP_ACCEL_4XXX_MASK_PKE_SLICE = BIT(2),
	ICP_ACCEL_4XXX_MASK_COMPRESS_SLICE = BIT(3),
	ICP_ACCEL_4XXX_MASK_UCS_SLICE = BIT(4),
	ICP_ACCEL_4XXX_MASK_EIA3_SLICE = BIT(5),
	ICP_ACCEL_4XXX_MASK_SMX_SLICE = BIT(6),
};

void adf_init_hw_data_4xxx(struct adf_hw_device_data *hw_data);
void adf_clean_hw_data_4xxx(struct adf_hw_device_data *hw_data);
int adf_crypto_dev_config(struct adf_accel_dev *accel_dev);

#endif
