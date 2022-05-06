/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) 2021, MediaTek Inc.
 * Copyright (c) 2021-2022, Intel Corporation.
 *
 * Authors:
 *  Haijun Liu <haijun.liu@mediatek.com>
 *  Chiranjeevi Rapolu <chiranjeevi.rapolu@intel.com>
 *
 * Contributors:
 *  Amir Hanania <amir.hanania@intel.com>
 *  Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *  Eliot Lee <eliot.lee@intel.com>
 *  Moises Veleta <moises.veleta@intel.com>
 *  Ricardo Martinez <ricardo.martinez@linux.intel.com>
 *  Sreehari Kancharla <sreehari.kancharla@intel.com>
 */

#ifndef __T7XX_REG_H__
#define __T7XX_REG_H__

#include <linux/bits.h>

/* Device base address offset */
#define MHCCIF_RC_DEV_BASE			0x10024000

#define REG_RC2EP_SW_BSY			0x04
#define REG_RC2EP_SW_INT_START			0x08

#define REG_RC2EP_SW_TCHNUM			0x0c
#define H2D_CH_EXCEPTION_ACK			1
#define H2D_CH_EXCEPTION_CLEARQ_ACK		2
#define H2D_CH_DS_LOCK				3
/* Channels 4-8 are reserved */
#define H2D_CH_SUSPEND_REQ			9
#define H2D_CH_RESUME_REQ			10
#define H2D_CH_SUSPEND_REQ_AP			11
#define H2D_CH_RESUME_REQ_AP			12
#define H2D_CH_DEVICE_RESET			13
#define H2D_CH_DRM_DISABLE_AP			14

#define REG_EP2RC_SW_INT_STS			0x10
#define REG_EP2RC_SW_INT_ACK			0x14
#define REG_EP2RC_SW_INT_EAP_MASK		0x20
#define REG_EP2RC_SW_INT_EAP_MASK_SET		0x30
#define REG_EP2RC_SW_INT_EAP_MASK_CLR		0x40

#define D2H_INT_DS_LOCK_ACK			BIT(0)
#define D2H_INT_EXCEPTION_INIT			BIT(1)
#define D2H_INT_EXCEPTION_INIT_DONE		BIT(2)
#define D2H_INT_EXCEPTION_CLEARQ_DONE		BIT(3)
#define D2H_INT_EXCEPTION_ALLQ_RESET		BIT(4)
#define D2H_INT_PORT_ENUM			BIT(5)
/* Bits 6-10 are reserved */
#define D2H_INT_SUSPEND_ACK			BIT(11)
#define D2H_INT_RESUME_ACK			BIT(12)
#define D2H_INT_SUSPEND_ACK_AP			BIT(13)
#define D2H_INT_RESUME_ACK_AP			BIT(14)
#define D2H_INT_ASYNC_SAP_HK			BIT(15)
#define D2H_INT_ASYNC_MD_HK			BIT(16)

/* Register base */
#define INFRACFG_AO_DEV_CHIP			0x10001000

/* ATR setting */
#define T7XX_PCIE_REG_TRSL_ADDR_CHIP		0x10000000
#define T7XX_PCIE_REG_SIZE_CHIP			0x00400000

/* Reset Generic Unit (RGU) */
#define TOPRGU_CH_PCIE_IRQ_STA			0x1000790c

#define ATR_PORT_OFFSET				0x100
#define ATR_TABLE_OFFSET			0x20
#define ATR_TABLE_NUM_PER_ATR			8
#define ATR_TRANSPARENT_SIZE			0x3f

/* PCIE_MAC_IREG Register Definition */

#define ISTAT_HST_CTRL				0x01ac
#define ISTAT_HST_CTRL_DIS			BIT(0)

#define T7XX_PCIE_MISC_CTRL			0x0348
#define T7XX_PCIE_MISC_MAC_SLEEP_DIS		BIT(7)

#define T7XX_PCIE_CFG_MSIX			0x03ec
#define ATR_PCIE_WIN0_T0_ATR_PARAM_SRC_ADDR	0x0600
#define ATR_PCIE_WIN0_T0_TRSL_ADDR		0x0608
#define ATR_PCIE_WIN0_T0_TRSL_PARAM		0x0610
#define ATR_PCIE_WIN0_ADDR_ALGMT		GENMASK_ULL(63, 12)

#define ATR_SRC_ADDR_INVALID			0x007f

#define T7XX_PCIE_PM_RESUME_STATE		0x0d0c

enum t7xx_pm_resume_state {
	PM_RESUME_REG_STATE_L3,
	PM_RESUME_REG_STATE_L1,
	PM_RESUME_REG_STATE_INIT,
	PM_RESUME_REG_STATE_EXP,
	PM_RESUME_REG_STATE_L2,
	PM_RESUME_REG_STATE_L2_EXP,
};

#define T7XX_PCIE_MISC_DEV_STATUS		0x0d1c
#define MISC_STAGE_MASK				GENMASK(2, 0)
#define MISC_RESET_TYPE_PLDR			BIT(26)
#define MISC_RESET_TYPE_FLDR			BIT(27)
#define LINUX_STAGE				4

#define T7XX_PCIE_RESOURCE_STATUS		0x0d28
#define T7XX_PCIE_RESOURCE_STS_MSK		GENMASK(4, 0)

#define DISABLE_ASPM_LOWPWR			0x0e50
#define ENABLE_ASPM_LOWPWR			0x0e54
#define T7XX_L1_BIT(i)				BIT((i) * 4 + 1)
#define T7XX_L1_1_BIT(i)			BIT((i) * 4 + 2)
#define T7XX_L1_2_BIT(i)			BIT((i) * 4 + 3)

#define MSIX_ISTAT_HST_GRP0_0			0x0f00
#define IMASK_HOST_MSIX_SET_GRP0_0		0x3000
#define IMASK_HOST_MSIX_CLR_GRP0_0		0x3080
#define EXT_INT_START				24
#define EXT_INT_NUM				8
#define MSIX_MSK_SET_ALL			GENMASK(31, 24)

enum t7xx_int {
	DPMAIF_INT,
	CLDMA0_INT,
	CLDMA1_INT,
	CLDMA2_INT,
	MHCCIF_INT,
	DPMAIF2_INT,
	SAP_RGU_INT,
	CLDMA3_INT,
};

#endif /* __T7XX_REG_H__ */
