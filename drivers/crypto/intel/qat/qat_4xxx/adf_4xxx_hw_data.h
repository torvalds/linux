/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2020 Intel Corporation */
#ifndef ADF_4XXX_HW_DATA_H_
#define ADF_4XXX_HW_DATA_H_

#include <linux/units.h>
#include <adf_accel_devices.h>

#define ADF_4XXX_MAX_ACCELENGINES	9

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
#define ADF_4XXX_AE_FREQ		(1000 * HZ_PER_MHZ)

void adf_init_hw_data_4xxx(struct adf_hw_device_data *hw_data, u32 dev_id);
void adf_clean_hw_data_4xxx(struct adf_hw_device_data *hw_data);

#endif
