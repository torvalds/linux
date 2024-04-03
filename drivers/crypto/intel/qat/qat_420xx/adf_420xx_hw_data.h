/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2023 Intel Corporation */
#ifndef ADF_420XX_HW_DATA_H_
#define ADF_420XX_HW_DATA_H_

#include <adf_accel_devices.h>

#define ADF_420XX_MAX_ACCELENGINES		17

#define ADF_420XX_ACCELENGINES_MASK		0x1FFFF
#define ADF_420XX_ADMIN_AE_MASK			0x10000

#define ADF_420XX_HICPPAGENTCMDPARERRLOG_MASK	(0xFF)
#define ADF_420XX_PARITYERRORMASK_ATH_CPH_MASK	(0xFF00FF)
#define ADF_420XX_PARITYERRORMASK_CPR_XLT_MASK	(0x10001)
#define ADF_420XX_PARITYERRORMASK_DCPR_UCS_MASK	(0xF0007)
#define ADF_420XX_PARITYERRORMASK_PKE_MASK	(0xFFF)
#define ADF_420XX_PARITYERRORMASK_WAT_WCP_MASK	(0x3FF03FF)

/*
 * SSMFEATREN bit mask
 * BIT(4) - enables parity detection on CPP
 * BIT(12) - enables the logging of push/pull data errors
 *	     in pperr register
 * BIT(16) - BIT(27) - enable parity detection on SPPs
 */
#define ADF_420XX_SSMFEATREN_MASK \
	(BIT(4) | BIT(12) | BIT(16) | BIT(17) | BIT(18) | BIT(19) | BIT(20) | \
	 BIT(21) | BIT(22) | BIT(23) | BIT(24) | BIT(25) | BIT(26) | BIT(27))

/* Firmware Binaries */
#define ADF_420XX_FW		"qat_420xx.bin"
#define ADF_420XX_MMP		"qat_420xx_mmp.bin"
#define ADF_420XX_SYM_OBJ	"qat_420xx_sym.bin"
#define ADF_420XX_DC_OBJ	"qat_420xx_dc.bin"
#define ADF_420XX_ASYM_OBJ	"qat_420xx_asym.bin"
#define ADF_420XX_ADMIN_OBJ	"qat_420xx_admin.bin"

/* RL constants */
#define ADF_420XX_RL_PCIE_SCALE_FACTOR_DIV	100
#define ADF_420XX_RL_PCIE_SCALE_FACTOR_MUL	102
#define ADF_420XX_RL_DCPR_CORRECTION		1
#define ADF_420XX_RL_SCANS_PER_SEC		954
#define ADF_420XX_RL_MAX_TP_ASYM		173750UL
#define ADF_420XX_RL_MAX_TP_SYM			95000UL
#define ADF_420XX_RL_MAX_TP_DC			40000UL
#define ADF_420XX_RL_SLICE_REF			1000UL

/* Clocks frequency */
#define ADF_420XX_AE_FREQ		(1000 * HZ_PER_MHZ)

void adf_init_hw_data_420xx(struct adf_hw_device_data *hw_data, u32 dev_id);
void adf_clean_hw_data_420xx(struct adf_hw_device_data *hw_data);

#endif
