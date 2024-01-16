// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2020, NVIDIA CORPORATION.  All rights reserved.
 * Copyright (C) 2015 Google, Inc.
 */

#include <linux/clk.h>
#include <linux/clk/tegra.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include <soc/tegra/fuse.h>

#include "xusb.h"

#define FUSE_SKU_CALIB_HS_CURR_LEVEL_PADX_SHIFT(x) \
					((x) ? (11 + ((x) - 1) * 6) : 0)
#define FUSE_SKU_CALIB_HS_CURR_LEVEL_PAD_MASK 0x3f
#define FUSE_SKU_CALIB_HS_TERM_RANGE_ADJ_SHIFT 7
#define FUSE_SKU_CALIB_HS_TERM_RANGE_ADJ_MASK 0xf

#define FUSE_USB_CALIB_EXT_RPD_CTRL_SHIFT 0
#define FUSE_USB_CALIB_EXT_RPD_CTRL_MASK 0x1f

#define XUSB_PADCTL_USB2_PAD_MUX 0x004
#define XUSB_PADCTL_USB2_PAD_MUX_HSIC_PAD_TRK_SHIFT 16
#define XUSB_PADCTL_USB2_PAD_MUX_HSIC_PAD_TRK_MASK 0x3
#define XUSB_PADCTL_USB2_PAD_MUX_HSIC_PAD_TRK_XUSB 0x1
#define XUSB_PADCTL_USB2_PAD_MUX_USB2_BIAS_PAD_SHIFT 18
#define XUSB_PADCTL_USB2_PAD_MUX_USB2_BIAS_PAD_MASK 0x3
#define XUSB_PADCTL_USB2_PAD_MUX_USB2_BIAS_PAD_XUSB 0x1

#define XUSB_PADCTL_USB2_PORT_CAP 0x008
#define XUSB_PADCTL_USB2_PORT_CAP_PORTX_CAP_DISABLED(x) (0x0 << ((x) * 4))
#define XUSB_PADCTL_USB2_PORT_CAP_PORTX_CAP_HOST(x) (0x1 << ((x) * 4))
#define XUSB_PADCTL_USB2_PORT_CAP_PORTX_CAP_DEVICE(x) (0x2 << ((x) * 4))
#define XUSB_PADCTL_USB2_PORT_CAP_PORTX_CAP_OTG(x) (0x3 << ((x) * 4))
#define XUSB_PADCTL_USB2_PORT_CAP_PORTX_CAP_MASK(x) (0x3 << ((x) * 4))

#define XUSB_PADCTL_SS_PORT_MAP 0x014
#define XUSB_PADCTL_SS_PORT_MAP_PORTX_INTERNAL(x) (1 << (((x) * 5) + 4))
#define XUSB_PADCTL_SS_PORT_MAP_PORTX_MAP_SHIFT(x) ((x) * 5)
#define XUSB_PADCTL_SS_PORT_MAP_PORTX_MAP_MASK(x) (0x7 << ((x) * 5))
#define XUSB_PADCTL_SS_PORT_MAP_PORTX_MAP(x, v) (((v) & 0x7) << ((x) * 5))
#define XUSB_PADCTL_SS_PORT_MAP_PORT_DISABLED 0x7

#define XUSB_PADCTL_ELPG_PROGRAM_0 0x20
#define   USB2_PORT_WAKE_INTERRUPT_ENABLE(x)      BIT((x))
#define   USB2_PORT_WAKEUP_EVENT(x)               BIT((x) + 7)
#define   SS_PORT_WAKE_INTERRUPT_ENABLE(x)        BIT((x) + 14)
#define   SS_PORT_WAKEUP_EVENT(x)                 BIT((x) + 21)
#define   USB2_HSIC_PORT_WAKE_INTERRUPT_ENABLE(x) BIT((x) + 28)
#define   USB2_HSIC_PORT_WAKEUP_EVENT(x)          BIT((x) + 30)
#define   ALL_WAKE_EVENTS ( \
		USB2_PORT_WAKEUP_EVENT(0) | USB2_PORT_WAKEUP_EVENT(1) | \
		USB2_PORT_WAKEUP_EVENT(2) | USB2_PORT_WAKEUP_EVENT(3) | \
		SS_PORT_WAKEUP_EVENT(0) | SS_PORT_WAKEUP_EVENT(1) | \
		SS_PORT_WAKEUP_EVENT(2) | SS_PORT_WAKEUP_EVENT(3) | \
		USB2_HSIC_PORT_WAKEUP_EVENT(0))

#define XUSB_PADCTL_ELPG_PROGRAM1 0x024
#define XUSB_PADCTL_ELPG_PROGRAM1_AUX_MUX_LP0_VCORE_DOWN (1 << 31)
#define XUSB_PADCTL_ELPG_PROGRAM1_AUX_MUX_LP0_CLAMP_EN_EARLY (1 << 30)
#define XUSB_PADCTL_ELPG_PROGRAM1_AUX_MUX_LP0_CLAMP_EN (1 << 29)
#define XUSB_PADCTL_ELPG_PROGRAM1_SSPX_ELPG_VCORE_DOWN(x) (1 << (2 + (x) * 3))
#define XUSB_PADCTL_ELPG_PROGRAM1_SSPX_ELPG_CLAMP_EN_EARLY(x) \
							(1 << (1 + (x) * 3))
#define XUSB_PADCTL_ELPG_PROGRAM1_SSPX_ELPG_CLAMP_EN(x) (1 << ((x) * 3))

#define XUSB_PADCTL_USB3_PAD_MUX 0x028
#define XUSB_PADCTL_USB3_PAD_MUX_PCIE_IDDQ_DISABLE(x) (1 << (1 + (x)))
#define XUSB_PADCTL_USB3_PAD_MUX_SATA_IDDQ_DISABLE(x) (1 << (8 + (x)))

#define XUSB_PADCTL_USB2_BATTERY_CHRG_OTGPADX_CTL0(x) (0x080 + (x) * 0x40)
#define XUSB_PADCTL_USB2_BATTERY_CHRG_OTGPAD_CTL0_ZIP (1 << 18)
#define XUSB_PADCTL_USB2_BATTERY_CHRG_OTGPAD_CTL0_ZIN (1 << 22)

#define XUSB_PADCTL_USB2_BATTERY_CHRG_OTGPADX_CTL1(x) (0x084 + (x) * 0x40)
#define XUSB_PADCTL_USB2_BATTERY_CHRG_OTGPAD_CTL1_VREG_LEV_SHIFT 7
#define XUSB_PADCTL_USB2_BATTERY_CHRG_OTGPAD_CTL1_VREG_LEV_MASK 0x3
#define XUSB_PADCTL_USB2_BATTERY_CHRG_OTGPAD_CTL1_VREG_LEV_VAL 0x1
#define XUSB_PADCTL_USB2_BATTERY_CHRG_OTGPAD_CTL1_VREG_FIX18 (1 << 6)

#define XUSB_PADCTL_USB2_OTG_PADX_CTL0(x) (0x088 + (x) * 0x40)
#define XUSB_PADCTL_USB2_OTG_PAD_CTL0_PD_ZI (1 << 29)
#define XUSB_PADCTL_USB2_OTG_PAD_CTL0_PD2 (1 << 27)
#define XUSB_PADCTL_USB2_OTG_PAD_CTL0_PD (1 << 26)
#define XUSB_PADCTL_USB2_OTG_PAD_CTL0_HS_CURR_LEVEL_SHIFT 0
#define XUSB_PADCTL_USB2_OTG_PAD_CTL0_HS_CURR_LEVEL_MASK 0x3f

#define XUSB_PADCTL_USB2_OTG_PADX_CTL1(x) (0x08c + (x) * 0x40)
#define XUSB_PADCTL_USB2_OTG_PAD_CTL1_RPD_CTRL_SHIFT 26
#define XUSB_PADCTL_USB2_OTG_PAD_CTL1_RPD_CTRL_MASK 0x1f
#define XUSB_PADCTL_USB2_OTG_PAD_CTL1_TERM_RANGE_ADJ_SHIFT 3
#define XUSB_PADCTL_USB2_OTG_PAD_CTL1_TERM_RANGE_ADJ_MASK 0xf
#define XUSB_PADCTL_USB2_OTG_PAD_CTL1_PD_DR (1 << 2)
#define XUSB_PADCTL_USB2_OTG_PAD_CTL1_PD_DISC_OVRD (1 << 1)
#define XUSB_PADCTL_USB2_OTG_PAD_CTL1_PD_CHRP_OVRD (1 << 0)
#define   RPD_CTRL(x)                      (((x) & 0x1f) << 26)
#define   RPD_CTRL_VALUE(x)                (((x) >> 26) & 0x1f)

#define XUSB_PADCTL_USB2_BIAS_PAD_CTL0 0x284
#define XUSB_PADCTL_USB2_BIAS_PAD_CTL0_PD (1 << 11)
#define XUSB_PADCTL_USB2_BIAS_PAD_CTL0_HS_DISCON_LEVEL_SHIFT 3
#define XUSB_PADCTL_USB2_BIAS_PAD_CTL0_HS_DISCON_LEVEL_MASK 0x7
#define XUSB_PADCTL_USB2_BIAS_PAD_CTL0_HS_DISCON_LEVEL_VAL 0x7
#define XUSB_PADCTL_USB2_BIAS_PAD_CTL0_HS_SQUELCH_LEVEL_SHIFT 0
#define XUSB_PADCTL_USB2_BIAS_PAD_CTL0_HS_SQUELCH_LEVEL_MASK 0x7
#define XUSB_PADCTL_USB2_BIAS_PAD_CTL0_HS_SQUELCH_LEVEL_VAL 0x2

#define XUSB_PADCTL_USB2_BIAS_PAD_CTL1 0x288
#define XUSB_PADCTL_USB2_BIAS_PAD_CTL1_PD_TRK (1 << 26)
#define XUSB_PADCTL_USB2_BIAS_PAD_CTL1_TRK_DONE_RESET_TIMER_SHIFT 19
#define XUSB_PADCTL_USB2_BIAS_PAD_CTL1_TRK_DONE_RESET_TIMER_MASK 0x7f
#define XUSB_PADCTL_USB2_BIAS_PAD_CTL1_TRK_DONE_RESET_TIMER_VAL 0x0a
#define XUSB_PADCTL_USB2_BIAS_PAD_CTL1_TRK_START_TIMER_SHIFT 12
#define XUSB_PADCTL_USB2_BIAS_PAD_CTL1_TRK_START_TIMER_MASK 0x7f
#define XUSB_PADCTL_USB2_BIAS_PAD_CTL1_TRK_START_TIMER_VAL 0x1e
#define   TCTRL_VALUE(x)                (((x) & 0x3f) >> 0)
#define   PCTRL_VALUE(x)                (((x) >> 6) & 0x3f)

#define XUSB_PADCTL_HSIC_PADX_CTL0(x) (0x300 + (x) * 0x20)
#define XUSB_PADCTL_HSIC_PAD_CTL0_RPU_STROBE (1 << 18)
#define XUSB_PADCTL_HSIC_PAD_CTL0_RPU_DATA1 (1 << 17)
#define XUSB_PADCTL_HSIC_PAD_CTL0_RPU_DATA0 (1 << 16)
#define XUSB_PADCTL_HSIC_PAD_CTL0_RPD_STROBE (1 << 15)
#define XUSB_PADCTL_HSIC_PAD_CTL0_RPD_DATA1 (1 << 14)
#define XUSB_PADCTL_HSIC_PAD_CTL0_RPD_DATA0 (1 << 13)
#define XUSB_PADCTL_HSIC_PAD_CTL0_PD_ZI_STROBE (1 << 9)
#define XUSB_PADCTL_HSIC_PAD_CTL0_PD_ZI_DATA1 (1 << 8)
#define XUSB_PADCTL_HSIC_PAD_CTL0_PD_ZI_DATA0 (1 << 7)
#define XUSB_PADCTL_HSIC_PAD_CTL0_PD_RX_STROBE (1 << 6)
#define XUSB_PADCTL_HSIC_PAD_CTL0_PD_RX_DATA1 (1 << 5)
#define XUSB_PADCTL_HSIC_PAD_CTL0_PD_RX_DATA0 (1 << 4)
#define XUSB_PADCTL_HSIC_PAD_CTL0_PD_TX_STROBE (1 << 3)
#define XUSB_PADCTL_HSIC_PAD_CTL0_PD_TX_DATA1 (1 << 2)
#define XUSB_PADCTL_HSIC_PAD_CTL0_PD_TX_DATA0 (1 << 1)

#define XUSB_PADCTL_HSIC_PADX_CTL1(x) (0x304 + (x) * 0x20)
#define XUSB_PADCTL_HSIC_PAD_CTL1_TX_RTUNEP_SHIFT 0
#define XUSB_PADCTL_HSIC_PAD_CTL1_TX_RTUNEP_MASK 0xf

#define XUSB_PADCTL_HSIC_PADX_CTL2(x) (0x308 + (x) * 0x20)
#define XUSB_PADCTL_HSIC_PAD_CTL2_RX_STROBE_TRIM_SHIFT 8
#define XUSB_PADCTL_HSIC_PAD_CTL2_RX_STROBE_TRIM_MASK 0xf
#define XUSB_PADCTL_HSIC_PAD_CTL2_RX_DATA_TRIM_SHIFT 0
#define XUSB_PADCTL_HSIC_PAD_CTL2_RX_DATA_TRIM_MASK 0xff

#define XUSB_PADCTL_HSIC_PAD_TRK_CTL 0x340
#define XUSB_PADCTL_HSIC_PAD_TRK_CTL_PD_TRK (1 << 19)
#define XUSB_PADCTL_HSIC_PAD_TRK_CTL_TRK_DONE_RESET_TIMER_SHIFT 12
#define XUSB_PADCTL_HSIC_PAD_TRK_CTL_TRK_DONE_RESET_TIMER_MASK 0x7f
#define XUSB_PADCTL_HSIC_PAD_TRK_CTL_TRK_DONE_RESET_TIMER_VAL 0x0a
#define XUSB_PADCTL_HSIC_PAD_TRK_CTL_TRK_START_TIMER_SHIFT 5
#define XUSB_PADCTL_HSIC_PAD_TRK_CTL_TRK_START_TIMER_MASK 0x7f
#define XUSB_PADCTL_HSIC_PAD_TRK_CTL_TRK_START_TIMER_VAL 0x1e

#define XUSB_PADCTL_HSIC_STRB_TRIM_CONTROL 0x344

#define XUSB_PADCTL_UPHY_PLL_P0_CTL1 0x360
#define XUSB_PADCTL_UPHY_PLL_CTL1_FREQ_NDIV_SHIFT 20
#define XUSB_PADCTL_UPHY_PLL_CTL1_FREQ_NDIV_MASK 0xff
#define XUSB_PADCTL_UPHY_PLL_CTL1_FREQ_NDIV_USB_VAL 0x19
#define XUSB_PADCTL_UPHY_PLL_CTL1_FREQ_NDIV_SATA_VAL 0x1e
#define XUSB_PADCTL_UPHY_PLL_CTL1_FREQ_MDIV_SHIFT 16
#define XUSB_PADCTL_UPHY_PLL_CTL1_FREQ_MDIV_MASK 0x3
#define XUSB_PADCTL_UPHY_PLL_CTL1_LOCKDET_STATUS (1 << 15)
#define XUSB_PADCTL_UPHY_PLL_CTL1_PWR_OVRD (1 << 4)
#define XUSB_PADCTL_UPHY_PLL_CTL1_ENABLE (1 << 3)
#define XUSB_PADCTL_UPHY_PLL_CTL1_SLEEP_SHIFT 1
#define XUSB_PADCTL_UPHY_PLL_CTL1_SLEEP_MASK 0x3
#define XUSB_PADCTL_UPHY_PLL_CTL1_IDDQ (1 << 0)

#define XUSB_PADCTL_UPHY_PLL_P0_CTL2 0x364
#define XUSB_PADCTL_UPHY_PLL_CTL2_CAL_CTRL_SHIFT 4
#define XUSB_PADCTL_UPHY_PLL_CTL2_CAL_CTRL_MASK 0xffffff
#define XUSB_PADCTL_UPHY_PLL_CTL2_CAL_CTRL_VAL 0x136
#define XUSB_PADCTL_UPHY_PLL_CTL2_CAL_OVRD (1 << 2)
#define XUSB_PADCTL_UPHY_PLL_CTL2_CAL_DONE (1 << 1)
#define XUSB_PADCTL_UPHY_PLL_CTL2_CAL_EN (1 << 0)

#define XUSB_PADCTL_UPHY_PLL_P0_CTL4 0x36c
#define XUSB_PADCTL_UPHY_PLL_CTL4_XDIGCLK_EN (1 << 19)
#define XUSB_PADCTL_UPHY_PLL_CTL4_TXCLKREF_EN (1 << 15)
#define XUSB_PADCTL_UPHY_PLL_CTL4_TXCLKREF_SEL_SHIFT 12
#define XUSB_PADCTL_UPHY_PLL_CTL4_TXCLKREF_SEL_MASK 0x3
#define XUSB_PADCTL_UPHY_PLL_CTL4_TXCLKREF_SEL_USB_VAL 0x2
#define XUSB_PADCTL_UPHY_PLL_CTL4_TXCLKREF_SEL_SATA_VAL 0x0
#define XUSB_PADCTL_UPHY_PLL_CTL4_REFCLKBUF_EN (1 << 8)
#define XUSB_PADCTL_UPHY_PLL_CTL4_REFCLK_SEL_SHIFT 4
#define XUSB_PADCTL_UPHY_PLL_CTL4_REFCLK_SEL_MASK 0xf

#define XUSB_PADCTL_UPHY_PLL_P0_CTL5 0x370
#define XUSB_PADCTL_UPHY_PLL_CTL5_DCO_CTRL_SHIFT 16
#define XUSB_PADCTL_UPHY_PLL_CTL5_DCO_CTRL_MASK 0xff
#define XUSB_PADCTL_UPHY_PLL_CTL5_DCO_CTRL_VAL 0x2a

#define XUSB_PADCTL_UPHY_PLL_P0_CTL8 0x37c
#define XUSB_PADCTL_UPHY_PLL_CTL8_RCAL_DONE (1 << 31)
#define XUSB_PADCTL_UPHY_PLL_CTL8_RCAL_OVRD (1 << 15)
#define XUSB_PADCTL_UPHY_PLL_CTL8_RCAL_CLK_EN (1 << 13)
#define XUSB_PADCTL_UPHY_PLL_CTL8_RCAL_EN (1 << 12)

#define XUSB_PADCTL_UPHY_MISC_PAD_PX_CTL1(x) (0x460 + (x) * 0x40)
#define XUSB_PADCTL_UPHY_MISC_PAD_CTL1_AUX_RX_IDLE_MODE_SHIFT 20
#define XUSB_PADCTL_UPHY_MISC_PAD_CTL1_AUX_RX_IDLE_MODE_MASK 0x3
#define XUSB_PADCTL_UPHY_MISC_PAD_CTL1_AUX_RX_IDLE_MODE_VAL 0x1
#define XUSB_PADCTL_UPHY_MISC_PAD_CTL1_AUX_RX_TERM_EN BIT(18)
#define XUSB_PADCTL_UPHY_MISC_PAD_CTL1_AUX_RX_MODE_OVRD BIT(13)

#define XUSB_PADCTL_UPHY_MISC_PAD_PX_CTL2(x) (0x464 + (x) * 0x40)
#define XUSB_PADCTL_UPHY_MISC_PAD_CTL2_TX_IDDQ BIT(0)
#define XUSB_PADCTL_UPHY_MISC_PAD_CTL2_TX_IDDQ_OVRD BIT(1)
#define XUSB_PADCTL_UPHY_MISC_PAD_CTL2_TX_SLEEP_MASK GENMASK(5, 4)
#define XUSB_PADCTL_UPHY_MISC_PAD_CTL2_TX_SLEEP_VAL GENMASK(5, 4)
#define XUSB_PADCTL_UPHY_MISC_PAD_CTL2_TX_PWR_OVRD BIT(24)
#define XUSB_PADCTL_UPHY_MISC_PAD_CTL2_RX_IDDQ BIT(8)
#define XUSB_PADCTL_UPHY_MISC_PAD_CTL2_RX_IDDQ_OVRD BIT(9)
#define XUSB_PADCTL_UPHY_MISC_PAD_CTL2_RX_SLEEP_MASK GENMASK(13, 12)
#define XUSB_PADCTL_UPHY_MISC_PAD_CTL2_RX_SLEEP_VAL GENMASK(13, 12)
#define XUSB_PADCTL_UPHY_MISC_PAD_CTL2_RX_PWR_OVRD BIT(25)

#define XUSB_PADCTL_UPHY_PLL_S0_CTL1 0x860

#define XUSB_PADCTL_UPHY_PLL_S0_CTL2 0x864

#define XUSB_PADCTL_UPHY_PLL_S0_CTL4 0x86c

#define XUSB_PADCTL_UPHY_PLL_S0_CTL5 0x870

#define XUSB_PADCTL_UPHY_PLL_S0_CTL8 0x87c

#define XUSB_PADCTL_UPHY_MISC_PAD_S0_CTL1 0x960
#define XUSB_PADCTL_UPHY_MISC_PAD_S0_CTL2 0x964

#define XUSB_PADCTL_UPHY_USB3_PADX_ECTL1(x) (0xa60 + (x) * 0x40)
#define XUSB_PADCTL_UPHY_USB3_PAD_ECTL1_TX_TERM_CTRL_SHIFT 16
#define XUSB_PADCTL_UPHY_USB3_PAD_ECTL1_TX_TERM_CTRL_MASK 0x3
#define XUSB_PADCTL_UPHY_USB3_PAD_ECTL1_TX_TERM_CTRL_VAL 0x2

#define XUSB_PADCTL_UPHY_USB3_PADX_ECTL2(x) (0xa64 + (x) * 0x40)
#define XUSB_PADCTL_UPHY_USB3_PAD_ECTL2_RX_CTLE_SHIFT 0
#define XUSB_PADCTL_UPHY_USB3_PAD_ECTL2_RX_CTLE_MASK 0xffff
#define XUSB_PADCTL_UPHY_USB3_PAD_ECTL2_RX_CTLE_VAL 0x00fc

#define XUSB_PADCTL_UPHY_USB3_PADX_ECTL3(x) (0xa68 + (x) * 0x40)
#define XUSB_PADCTL_UPHY_USB3_PAD_ECTL3_RX_DFE_VAL 0xc0077f1f

#define XUSB_PADCTL_UPHY_USB3_PADX_ECTL4(x) (0xa6c + (x) * 0x40)
#define XUSB_PADCTL_UPHY_USB3_PAD_ECTL4_RX_CDR_CTRL_SHIFT 16
#define XUSB_PADCTL_UPHY_USB3_PAD_ECTL4_RX_CDR_CTRL_MASK 0xffff
#define XUSB_PADCTL_UPHY_USB3_PAD_ECTL4_RX_CDR_CTRL_VAL 0x01c7

#define XUSB_PADCTL_UPHY_USB3_PADX_ECTL6(x) (0xa74 + (x) * 0x40)
#define XUSB_PADCTL_UPHY_USB3_PAD_ECTL6_RX_EQ_CTRL_H_VAL 0xfcf01368

#define XUSB_PADCTL_USB2_VBUS_ID 0xc60
#define XUSB_PADCTL_USB2_VBUS_ID_OVERRIDE_VBUS_ON (1 << 14)
#define XUSB_PADCTL_USB2_VBUS_ID_OVERRIDE_SHIFT 18
#define XUSB_PADCTL_USB2_VBUS_ID_OVERRIDE_MASK 0xf
#define XUSB_PADCTL_USB2_VBUS_ID_OVERRIDE_FLOATING 8
#define XUSB_PADCTL_USB2_VBUS_ID_OVERRIDE_GROUNDED 0

/* USB2 SLEEPWALK registers */
#define UTMIP(_port, _offset1, _offset2) \
		(((_port) <= 2) ? (_offset1) : (_offset2))

#define PMC_UTMIP_UHSIC_SLEEP_CFG(x)	UTMIP(x, 0x1fc, 0x4d0)
#define   UTMIP_MASTER_ENABLE(x)		UTMIP(x, BIT(8 * (x)), BIT(0))
#define   UTMIP_FSLS_USE_PMC(x)			UTMIP(x, BIT(8 * (x) + 1), \
							BIT(1))
#define   UTMIP_PCTRL_USE_PMC(x)		UTMIP(x, BIT(8 * (x) + 2), \
							BIT(2))
#define   UTMIP_TCTRL_USE_PMC(x)		UTMIP(x, BIT(8 * (x) + 3), \
							BIT(3))
#define   UTMIP_WAKE_VAL(_port, _value)		(((_value) & 0xf) << \
					(UTMIP(_port, 8 * (_port) + 4, 4)))
#define   UTMIP_WAKE_VAL_NONE(_port)		UTMIP_WAKE_VAL(_port, 12)
#define   UTMIP_WAKE_VAL_ANY(_port)		UTMIP_WAKE_VAL(_port, 15)

#define PMC_UTMIP_UHSIC_SLEEP_CFG1	(0x4d0)
#define   UTMIP_RPU_SWITC_LOW_USE_PMC_PX(x)	BIT((x) + 8)
#define   UTMIP_RPD_CTRL_USE_PMC_PX(x)		BIT((x) + 16)

#define PMC_UTMIP_MASTER_CONFIG		(0x274)
#define   UTMIP_PWR(x)				UTMIP(x, BIT(x), BIT(4))
#define   UHSIC_PWR				BIT(3)

#define PMC_USB_DEBOUNCE_DEL		(0xec)
#define   DEBOUNCE_VAL(x)			(((x) & 0xffff) << 0)
#define   UTMIP_LINE_DEB_CNT(x)			(((x) & 0xf) << 16)
#define   UHSIC_LINE_DEB_CNT(x)			(((x) & 0xf) << 20)

#define PMC_UTMIP_UHSIC_FAKE(x)		UTMIP(x, 0x218, 0x294)
#define   UTMIP_FAKE_USBOP_VAL(x)		UTMIP(x, BIT(4 * (x)), BIT(8))
#define   UTMIP_FAKE_USBON_VAL(x)		UTMIP(x, BIT(4 * (x) + 1), \
							BIT(9))
#define   UTMIP_FAKE_USBOP_EN(x)		UTMIP(x, BIT(4 * (x) + 2), \
							BIT(10))
#define   UTMIP_FAKE_USBON_EN(x)		UTMIP(x, BIT(4 * (x) + 3), \
							BIT(11))

#define PMC_UTMIP_UHSIC_SLEEPWALK_CFG(x)	UTMIP(x, 0x200, 0x288)
#define   UTMIP_LINEVAL_WALK_EN(x)		UTMIP(x, BIT(8 * (x) + 7), \
							BIT(15))

#define PMC_USB_AO			(0xf0)
#define   USBOP_VAL_PD(x)			UTMIP(x, BIT(4 * (x)), BIT(20))
#define   USBON_VAL_PD(x)			UTMIP(x, BIT(4 * (x) + 1), \
							BIT(21))
#define   STROBE_VAL_PD				BIT(12)
#define   DATA0_VAL_PD				BIT(13)
#define   DATA1_VAL_PD				BIT(24)

#define PMC_UTMIP_UHSIC_SAVED_STATE(x)	UTMIP(x, 0x1f0, 0x280)
#define   SPEED(_port, _value)			(((_value) & 0x3) << \
						(UTMIP(_port, 8 * (_port), 8)))
#define   UTMI_HS(_port)			SPEED(_port, 0)
#define   UTMI_FS(_port)			SPEED(_port, 1)
#define   UTMI_LS(_port)			SPEED(_port, 2)
#define   UTMI_RST(_port)			SPEED(_port, 3)

#define PMC_UTMIP_UHSIC_TRIGGERS		(0x1ec)
#define   UTMIP_CLR_WALK_PTR(x)			UTMIP(x, BIT(x), BIT(16))
#define   UTMIP_CAP_CFG(x)			UTMIP(x, BIT((x) + 4), BIT(17))
#define   UTMIP_CLR_WAKE_ALARM(x)		UTMIP(x, BIT((x) + 12), \
							BIT(19))
#define   UHSIC_CLR_WALK_PTR			BIT(3)
#define   UHSIC_CLR_WAKE_ALARM			BIT(15)

#define PMC_UTMIP_SLEEPWALK_PX(x)	UTMIP(x, 0x204 + (4 * (x)), \
							0x4e0)
/* phase A */
#define   UTMIP_USBOP_RPD_A			BIT(0)
#define   UTMIP_USBON_RPD_A			BIT(1)
#define   UTMIP_AP_A				BIT(4)
#define   UTMIP_AN_A				BIT(5)
#define   UTMIP_HIGHZ_A				BIT(6)
/* phase B */
#define   UTMIP_USBOP_RPD_B			BIT(8)
#define   UTMIP_USBON_RPD_B			BIT(9)
#define   UTMIP_AP_B				BIT(12)
#define   UTMIP_AN_B				BIT(13)
#define   UTMIP_HIGHZ_B				BIT(14)
/* phase C */
#define   UTMIP_USBOP_RPD_C			BIT(16)
#define   UTMIP_USBON_RPD_C			BIT(17)
#define   UTMIP_AP_C				BIT(20)
#define   UTMIP_AN_C				BIT(21)
#define   UTMIP_HIGHZ_C				BIT(22)
/* phase D */
#define   UTMIP_USBOP_RPD_D			BIT(24)
#define   UTMIP_USBON_RPD_D			BIT(25)
#define   UTMIP_AP_D				BIT(28)
#define   UTMIP_AN_D				BIT(29)
#define   UTMIP_HIGHZ_D				BIT(30)

#define PMC_UTMIP_UHSIC_LINE_WAKEUP	(0x26c)
#define   UTMIP_LINE_WAKEUP_EN(x)		UTMIP(x, BIT(x), BIT(4))
#define   UHSIC_LINE_WAKEUP_EN			BIT(3)

#define PMC_UTMIP_TERM_PAD_CFG		(0x1f8)
#define   PCTRL_VAL(x)				(((x) & 0x3f) << 1)
#define   TCTRL_VAL(x)				(((x) & 0x3f) << 7)

#define PMC_UTMIP_PAD_CFGX(x)		(0x4c0 + (4 * (x)))
#define   RPD_CTRL_PX(x)			(((x) & 0x1f) << 22)

#define PMC_UHSIC_SLEEP_CFG	PMC_UTMIP_UHSIC_SLEEP_CFG(0)
#define   UHSIC_MASTER_ENABLE			BIT(24)
#define   UHSIC_WAKE_VAL(_value)		(((_value) & 0xf) << 28)
#define   UHSIC_WAKE_VAL_SD10			UHSIC_WAKE_VAL(2)
#define   UHSIC_WAKE_VAL_NONE			UHSIC_WAKE_VAL(12)

#define PMC_UHSIC_FAKE			PMC_UTMIP_UHSIC_FAKE(0)
#define   UHSIC_FAKE_STROBE_VAL			BIT(12)
#define   UHSIC_FAKE_DATA_VAL			BIT(13)
#define   UHSIC_FAKE_STROBE_EN			BIT(14)
#define   UHSIC_FAKE_DATA_EN			BIT(15)

#define PMC_UHSIC_SAVED_STATE		PMC_UTMIP_UHSIC_SAVED_STATE(0)
#define   UHSIC_MODE(_value)			(((_value) & 0x1) << 24)
#define   UHSIC_HS				UHSIC_MODE(0)
#define   UHSIC_RST				UHSIC_MODE(1)

#define PMC_UHSIC_SLEEPWALK_CFG		PMC_UTMIP_UHSIC_SLEEPWALK_CFG(0)
#define   UHSIC_WAKE_WALK_EN			BIT(30)
#define   UHSIC_LINEVAL_WALK_EN			BIT(31)

#define PMC_UHSIC_SLEEPWALK_P0		(0x210)
#define   UHSIC_DATA0_RPD_A			BIT(1)
#define   UHSIC_DATA0_RPU_B			BIT(11)
#define   UHSIC_DATA0_RPU_C			BIT(19)
#define   UHSIC_DATA0_RPU_D			BIT(27)
#define   UHSIC_STROBE_RPU_A			BIT(2)
#define   UHSIC_STROBE_RPD_B			BIT(8)
#define   UHSIC_STROBE_RPD_C			BIT(16)
#define   UHSIC_STROBE_RPD_D			BIT(24)

struct tegra210_xusb_fuse_calibration {
	u32 hs_curr_level[4];
	u32 hs_term_range_adj;
	u32 rpd_ctrl;
};

struct tegra210_xusb_padctl_context {
	u32 usb2_pad_mux;
	u32 usb2_port_cap;
	u32 ss_port_map;
	u32 usb3_pad_mux;
};

struct tegra210_xusb_padctl {
	struct tegra_xusb_padctl base;
	struct regmap *regmap;

	struct tegra210_xusb_fuse_calibration fuse;
	struct tegra210_xusb_padctl_context context;
};

static inline struct tegra210_xusb_padctl *
to_tegra210_xusb_padctl(struct tegra_xusb_padctl *padctl)
{
	return container_of(padctl, struct tegra210_xusb_padctl, base);
}

static const struct tegra_xusb_lane_map tegra210_usb3_map[] = {
	{ 0, "pcie", 6 },
	{ 1, "pcie", 5 },
	{ 2, "pcie", 0 },
	{ 2, "pcie", 3 },
	{ 3, "pcie", 4 },
	{ 3, "sata", 0 },
	{ 0, NULL,   0 }
};

static int tegra210_usb3_lane_map(struct tegra_xusb_lane *lane)
{
	const struct tegra_xusb_lane_map *map;

	for (map = tegra210_usb3_map; map->type; map++) {
		if (map->index == lane->index &&
		    strcmp(map->type, lane->pad->soc->name) == 0) {
			dev_dbg(lane->pad->padctl->dev, "lane = %s map to port = usb3-%d\n",
				lane->pad->soc->lanes[lane->index].name, map->port);
			return map->port;
		}
	}

	return -EINVAL;
}

/* must be called under padctl->lock */
static int tegra210_pex_uphy_enable(struct tegra_xusb_padctl *padctl)
{
	struct tegra_xusb_pcie_pad *pcie = to_pcie_pad(padctl->pcie);
	unsigned long timeout;
	u32 value;
	unsigned int i;
	int err;

	if (pcie->enable)
		return 0;

	err = clk_prepare_enable(pcie->pll);
	if (err < 0)
		return err;

	if (tegra210_plle_hw_sequence_is_enabled())
		goto skip_pll_init;

	err = reset_control_deassert(pcie->rst);
	if (err < 0)
		goto disable;

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_P0_CTL2);
	value &= ~(XUSB_PADCTL_UPHY_PLL_CTL2_CAL_CTRL_MASK <<
		   XUSB_PADCTL_UPHY_PLL_CTL2_CAL_CTRL_SHIFT);
	value |= XUSB_PADCTL_UPHY_PLL_CTL2_CAL_CTRL_VAL <<
		 XUSB_PADCTL_UPHY_PLL_CTL2_CAL_CTRL_SHIFT;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_P0_CTL2);

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_P0_CTL5);
	value &= ~(XUSB_PADCTL_UPHY_PLL_CTL5_DCO_CTRL_MASK <<
		   XUSB_PADCTL_UPHY_PLL_CTL5_DCO_CTRL_SHIFT);
	value |= XUSB_PADCTL_UPHY_PLL_CTL5_DCO_CTRL_VAL <<
		 XUSB_PADCTL_UPHY_PLL_CTL5_DCO_CTRL_SHIFT;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_P0_CTL5);

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_P0_CTL1);
	value |= XUSB_PADCTL_UPHY_PLL_CTL1_PWR_OVRD;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_P0_CTL1);

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_P0_CTL2);
	value |= XUSB_PADCTL_UPHY_PLL_CTL2_CAL_OVRD;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_P0_CTL2);

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_P0_CTL8);
	value |= XUSB_PADCTL_UPHY_PLL_CTL8_RCAL_OVRD;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_P0_CTL8);

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_P0_CTL4);
	value &= ~((XUSB_PADCTL_UPHY_PLL_CTL4_TXCLKREF_SEL_MASK <<
		    XUSB_PADCTL_UPHY_PLL_CTL4_TXCLKREF_SEL_SHIFT) |
		   (XUSB_PADCTL_UPHY_PLL_CTL4_REFCLK_SEL_MASK <<
		    XUSB_PADCTL_UPHY_PLL_CTL4_REFCLK_SEL_SHIFT));
	value |= (XUSB_PADCTL_UPHY_PLL_CTL4_TXCLKREF_SEL_USB_VAL <<
		  XUSB_PADCTL_UPHY_PLL_CTL4_TXCLKREF_SEL_SHIFT) |
		 XUSB_PADCTL_UPHY_PLL_CTL4_TXCLKREF_EN;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_P0_CTL4);

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_P0_CTL1);
	value &= ~((XUSB_PADCTL_UPHY_PLL_CTL1_FREQ_MDIV_MASK <<
		    XUSB_PADCTL_UPHY_PLL_CTL1_FREQ_MDIV_SHIFT) |
		   (XUSB_PADCTL_UPHY_PLL_CTL1_FREQ_NDIV_MASK <<
		    XUSB_PADCTL_UPHY_PLL_CTL1_FREQ_NDIV_SHIFT));
	value |= XUSB_PADCTL_UPHY_PLL_CTL1_FREQ_NDIV_USB_VAL <<
		 XUSB_PADCTL_UPHY_PLL_CTL1_FREQ_NDIV_SHIFT;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_P0_CTL1);

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_P0_CTL1);
	value &= ~XUSB_PADCTL_UPHY_PLL_CTL1_IDDQ;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_P0_CTL1);

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_P0_CTL1);
	value &= ~(XUSB_PADCTL_UPHY_PLL_CTL1_SLEEP_MASK <<
		   XUSB_PADCTL_UPHY_PLL_CTL1_SLEEP_SHIFT);
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_P0_CTL1);

	usleep_range(10, 20);

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_P0_CTL4);
	value |= XUSB_PADCTL_UPHY_PLL_CTL4_REFCLKBUF_EN;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_P0_CTL4);

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_P0_CTL2);
	value |= XUSB_PADCTL_UPHY_PLL_CTL2_CAL_EN;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_P0_CTL2);

	timeout = jiffies + msecs_to_jiffies(100);

	while (time_before(jiffies, timeout)) {
		value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_P0_CTL2);
		if (value & XUSB_PADCTL_UPHY_PLL_CTL2_CAL_DONE)
			break;

		usleep_range(10, 20);
	}

	if (time_after_eq(jiffies, timeout)) {
		err = -ETIMEDOUT;
		goto reset;
	}

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_P0_CTL2);
	value &= ~XUSB_PADCTL_UPHY_PLL_CTL2_CAL_EN;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_P0_CTL2);

	timeout = jiffies + msecs_to_jiffies(100);

	while (time_before(jiffies, timeout)) {
		value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_P0_CTL2);
		if (!(value & XUSB_PADCTL_UPHY_PLL_CTL2_CAL_DONE))
			break;

		usleep_range(10, 20);
	}

	if (time_after_eq(jiffies, timeout)) {
		err = -ETIMEDOUT;
		goto reset;
	}

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_P0_CTL1);
	value |= XUSB_PADCTL_UPHY_PLL_CTL1_ENABLE;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_P0_CTL1);

	timeout = jiffies + msecs_to_jiffies(100);

	while (time_before(jiffies, timeout)) {
		value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_P0_CTL1);
		if (value & XUSB_PADCTL_UPHY_PLL_CTL1_LOCKDET_STATUS)
			break;

		usleep_range(10, 20);
	}

	if (time_after_eq(jiffies, timeout)) {
		err = -ETIMEDOUT;
		goto reset;
	}

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_P0_CTL8);
	value |= XUSB_PADCTL_UPHY_PLL_CTL8_RCAL_EN |
		 XUSB_PADCTL_UPHY_PLL_CTL8_RCAL_CLK_EN;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_P0_CTL8);

	timeout = jiffies + msecs_to_jiffies(100);

	while (time_before(jiffies, timeout)) {
		value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_P0_CTL8);
		if (value & XUSB_PADCTL_UPHY_PLL_CTL8_RCAL_DONE)
			break;

		usleep_range(10, 20);
	}

	if (time_after_eq(jiffies, timeout)) {
		err = -ETIMEDOUT;
		goto reset;
	}

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_P0_CTL8);
	value &= ~XUSB_PADCTL_UPHY_PLL_CTL8_RCAL_EN;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_P0_CTL8);

	timeout = jiffies + msecs_to_jiffies(100);

	while (time_before(jiffies, timeout)) {
		value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_P0_CTL8);
		if (!(value & XUSB_PADCTL_UPHY_PLL_CTL8_RCAL_DONE))
			break;

		usleep_range(10, 20);
	}

	if (time_after_eq(jiffies, timeout)) {
		err = -ETIMEDOUT;
		goto reset;
	}

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_P0_CTL8);
	value &= ~XUSB_PADCTL_UPHY_PLL_CTL8_RCAL_CLK_EN;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_P0_CTL8);

	tegra210_xusb_pll_hw_control_enable();

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_P0_CTL1);
	value &= ~XUSB_PADCTL_UPHY_PLL_CTL1_PWR_OVRD;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_P0_CTL1);

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_P0_CTL2);
	value &= ~XUSB_PADCTL_UPHY_PLL_CTL2_CAL_OVRD;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_P0_CTL2);

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_P0_CTL8);
	value &= ~XUSB_PADCTL_UPHY_PLL_CTL8_RCAL_OVRD;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_P0_CTL8);

	usleep_range(10, 20);

	tegra210_xusb_pll_hw_sequence_start();

skip_pll_init:
	pcie->enable = true;

	for (i = 0; i < padctl->pcie->soc->num_lanes; i++) {
		value = padctl_readl(padctl, XUSB_PADCTL_USB3_PAD_MUX);
		value |= XUSB_PADCTL_USB3_PAD_MUX_PCIE_IDDQ_DISABLE(i);
		padctl_writel(padctl, value, XUSB_PADCTL_USB3_PAD_MUX);
	}

	return 0;

reset:
	reset_control_assert(pcie->rst);
disable:
	clk_disable_unprepare(pcie->pll);
	return err;
}

static void tegra210_pex_uphy_disable(struct tegra_xusb_padctl *padctl)
{
	struct tegra_xusb_pcie_pad *pcie = to_pcie_pad(padctl->pcie);
	u32 value;
	unsigned int i;

	if (WARN_ON(!pcie->enable))
		return;

	pcie->enable = false;

	for (i = 0; i < padctl->pcie->soc->num_lanes; i++) {
		value = padctl_readl(padctl, XUSB_PADCTL_USB3_PAD_MUX);
		value &= ~XUSB_PADCTL_USB3_PAD_MUX_PCIE_IDDQ_DISABLE(i);
		padctl_writel(padctl, value, XUSB_PADCTL_USB3_PAD_MUX);
	}

	clk_disable_unprepare(pcie->pll);
}

/* must be called under padctl->lock */
static int tegra210_sata_uphy_enable(struct tegra_xusb_padctl *padctl)
{
	struct tegra_xusb_sata_pad *sata = to_sata_pad(padctl->sata);
	struct tegra_xusb_lane *lane = tegra_xusb_find_lane(padctl, "sata", 0);
	unsigned long timeout;
	u32 value;
	unsigned int i;
	int err;
	bool usb;

	if (sata->enable)
		return 0;

	if (IS_ERR(lane))
		return 0;

	if (tegra210_plle_hw_sequence_is_enabled())
		goto skip_pll_init;

	usb = tegra_xusb_lane_check(lane, "usb3-ss");

	err = clk_prepare_enable(sata->pll);
	if (err < 0)
		return err;

	err = reset_control_deassert(sata->rst);
	if (err < 0)
		goto disable;

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_S0_CTL2);
	value &= ~(XUSB_PADCTL_UPHY_PLL_CTL2_CAL_CTRL_MASK <<
		   XUSB_PADCTL_UPHY_PLL_CTL2_CAL_CTRL_SHIFT);
	value |= XUSB_PADCTL_UPHY_PLL_CTL2_CAL_CTRL_VAL <<
		 XUSB_PADCTL_UPHY_PLL_CTL2_CAL_CTRL_SHIFT;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_S0_CTL2);

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_S0_CTL5);
	value &= ~(XUSB_PADCTL_UPHY_PLL_CTL5_DCO_CTRL_MASK <<
		   XUSB_PADCTL_UPHY_PLL_CTL5_DCO_CTRL_SHIFT);
	value |= XUSB_PADCTL_UPHY_PLL_CTL5_DCO_CTRL_VAL <<
		 XUSB_PADCTL_UPHY_PLL_CTL5_DCO_CTRL_SHIFT;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_S0_CTL5);

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_S0_CTL1);
	value |= XUSB_PADCTL_UPHY_PLL_CTL1_PWR_OVRD;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_S0_CTL1);

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_S0_CTL2);
	value |= XUSB_PADCTL_UPHY_PLL_CTL2_CAL_OVRD;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_S0_CTL2);

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_S0_CTL8);
	value |= XUSB_PADCTL_UPHY_PLL_CTL8_RCAL_OVRD;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_S0_CTL8);

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_S0_CTL4);
	value &= ~((XUSB_PADCTL_UPHY_PLL_CTL4_TXCLKREF_SEL_MASK <<
		    XUSB_PADCTL_UPHY_PLL_CTL4_TXCLKREF_SEL_SHIFT) |
		   (XUSB_PADCTL_UPHY_PLL_CTL4_REFCLK_SEL_MASK <<
		    XUSB_PADCTL_UPHY_PLL_CTL4_REFCLK_SEL_SHIFT));
	value |= XUSB_PADCTL_UPHY_PLL_CTL4_TXCLKREF_EN;

	if (usb)
		value |= (XUSB_PADCTL_UPHY_PLL_CTL4_TXCLKREF_SEL_USB_VAL <<
			  XUSB_PADCTL_UPHY_PLL_CTL4_TXCLKREF_SEL_SHIFT);
	else
		value |= (XUSB_PADCTL_UPHY_PLL_CTL4_TXCLKREF_SEL_SATA_VAL <<
			  XUSB_PADCTL_UPHY_PLL_CTL4_TXCLKREF_SEL_SHIFT);

	value &= ~XUSB_PADCTL_UPHY_PLL_CTL4_XDIGCLK_EN;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_S0_CTL4);

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_S0_CTL1);
	value &= ~((XUSB_PADCTL_UPHY_PLL_CTL1_FREQ_MDIV_MASK <<
		    XUSB_PADCTL_UPHY_PLL_CTL1_FREQ_MDIV_SHIFT) |
		   (XUSB_PADCTL_UPHY_PLL_CTL1_FREQ_NDIV_MASK <<
		    XUSB_PADCTL_UPHY_PLL_CTL1_FREQ_NDIV_SHIFT));

	if (usb)
		value |= XUSB_PADCTL_UPHY_PLL_CTL1_FREQ_NDIV_USB_VAL <<
			 XUSB_PADCTL_UPHY_PLL_CTL1_FREQ_NDIV_SHIFT;
	else
		value |= XUSB_PADCTL_UPHY_PLL_CTL1_FREQ_NDIV_SATA_VAL <<
			 XUSB_PADCTL_UPHY_PLL_CTL1_FREQ_NDIV_SHIFT;

	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_S0_CTL1);

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_S0_CTL1);
	value &= ~XUSB_PADCTL_UPHY_PLL_CTL1_IDDQ;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_S0_CTL1);

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_S0_CTL1);
	value &= ~(XUSB_PADCTL_UPHY_PLL_CTL1_SLEEP_MASK <<
		   XUSB_PADCTL_UPHY_PLL_CTL1_SLEEP_SHIFT);
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_S0_CTL1);

	usleep_range(10, 20);

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_S0_CTL4);
	value |= XUSB_PADCTL_UPHY_PLL_CTL4_REFCLKBUF_EN;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_S0_CTL4);

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_S0_CTL2);
	value |= XUSB_PADCTL_UPHY_PLL_CTL2_CAL_EN;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_S0_CTL2);

	timeout = jiffies + msecs_to_jiffies(100);

	while (time_before(jiffies, timeout)) {
		value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_S0_CTL2);
		if (value & XUSB_PADCTL_UPHY_PLL_CTL2_CAL_DONE)
			break;

		usleep_range(10, 20);
	}

	if (time_after_eq(jiffies, timeout)) {
		err = -ETIMEDOUT;
		goto reset;
	}

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_S0_CTL2);
	value &= ~XUSB_PADCTL_UPHY_PLL_CTL2_CAL_EN;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_S0_CTL2);

	timeout = jiffies + msecs_to_jiffies(100);

	while (time_before(jiffies, timeout)) {
		value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_S0_CTL2);
		if (!(value & XUSB_PADCTL_UPHY_PLL_CTL2_CAL_DONE))
			break;

		usleep_range(10, 20);
	}

	if (time_after_eq(jiffies, timeout)) {
		err = -ETIMEDOUT;
		goto reset;
	}

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_S0_CTL1);
	value |= XUSB_PADCTL_UPHY_PLL_CTL1_ENABLE;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_S0_CTL1);

	timeout = jiffies + msecs_to_jiffies(100);

	while (time_before(jiffies, timeout)) {
		value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_S0_CTL1);
		if (value & XUSB_PADCTL_UPHY_PLL_CTL1_LOCKDET_STATUS)
			break;

		usleep_range(10, 20);
	}

	if (time_after_eq(jiffies, timeout)) {
		err = -ETIMEDOUT;
		goto reset;
	}

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_S0_CTL8);
	value |= XUSB_PADCTL_UPHY_PLL_CTL8_RCAL_EN |
		 XUSB_PADCTL_UPHY_PLL_CTL8_RCAL_CLK_EN;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_S0_CTL8);

	timeout = jiffies + msecs_to_jiffies(100);

	while (time_before(jiffies, timeout)) {
		value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_S0_CTL8);
		if (value & XUSB_PADCTL_UPHY_PLL_CTL8_RCAL_DONE)
			break;

		usleep_range(10, 20);
	}

	if (time_after_eq(jiffies, timeout)) {
		err = -ETIMEDOUT;
		goto reset;
	}

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_S0_CTL8);
	value &= ~XUSB_PADCTL_UPHY_PLL_CTL8_RCAL_EN;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_S0_CTL8);

	timeout = jiffies + msecs_to_jiffies(100);

	while (time_before(jiffies, timeout)) {
		value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_S0_CTL8);
		if (!(value & XUSB_PADCTL_UPHY_PLL_CTL8_RCAL_DONE))
			break;

		usleep_range(10, 20);
	}

	if (time_after_eq(jiffies, timeout)) {
		err = -ETIMEDOUT;
		goto reset;
	}

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_S0_CTL8);
	value &= ~XUSB_PADCTL_UPHY_PLL_CTL8_RCAL_CLK_EN;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_S0_CTL8);

	tegra210_sata_pll_hw_control_enable();

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_S0_CTL1);
	value &= ~XUSB_PADCTL_UPHY_PLL_CTL1_PWR_OVRD;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_S0_CTL1);

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_S0_CTL2);
	value &= ~XUSB_PADCTL_UPHY_PLL_CTL2_CAL_OVRD;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_S0_CTL2);

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_PLL_S0_CTL8);
	value &= ~XUSB_PADCTL_UPHY_PLL_CTL8_RCAL_OVRD;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_PLL_S0_CTL8);

	usleep_range(10, 20);

	tegra210_sata_pll_hw_sequence_start();

skip_pll_init:
	sata->enable = true;

	for (i = 0; i < padctl->sata->soc->num_lanes; i++) {
		value = padctl_readl(padctl, XUSB_PADCTL_USB3_PAD_MUX);
		value |= XUSB_PADCTL_USB3_PAD_MUX_SATA_IDDQ_DISABLE(i);
		padctl_writel(padctl, value, XUSB_PADCTL_USB3_PAD_MUX);
	}

	return 0;

reset:
	reset_control_assert(sata->rst);
disable:
	clk_disable_unprepare(sata->pll);
	return err;
}

static void tegra210_sata_uphy_disable(struct tegra_xusb_padctl *padctl)
{
	struct tegra_xusb_sata_pad *sata = to_sata_pad(padctl->sata);
	u32 value;
	unsigned int i;

	if (WARN_ON(!sata->enable))
		return;

	sata->enable = false;

	for (i = 0; i < padctl->sata->soc->num_lanes; i++) {
		value = padctl_readl(padctl, XUSB_PADCTL_USB3_PAD_MUX);
		value &= ~XUSB_PADCTL_USB3_PAD_MUX_SATA_IDDQ_DISABLE(i);
		padctl_writel(padctl, value, XUSB_PADCTL_USB3_PAD_MUX);
	}

	clk_disable_unprepare(sata->pll);
}

static void tegra210_aux_mux_lp0_clamp_disable(struct tegra_xusb_padctl *padctl)
{
	u32 value;

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM1);
	value &= ~XUSB_PADCTL_ELPG_PROGRAM1_AUX_MUX_LP0_CLAMP_EN;
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM1);

	usleep_range(100, 200);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM1);
	value &= ~XUSB_PADCTL_ELPG_PROGRAM1_AUX_MUX_LP0_CLAMP_EN_EARLY;
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM1);

	usleep_range(100, 200);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM1);
	value &= ~XUSB_PADCTL_ELPG_PROGRAM1_AUX_MUX_LP0_VCORE_DOWN;
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM1);
}

static void tegra210_aux_mux_lp0_clamp_enable(struct tegra_xusb_padctl *padctl)
{
	u32 value;

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM1);
	value |= XUSB_PADCTL_ELPG_PROGRAM1_AUX_MUX_LP0_VCORE_DOWN;
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM1);

	usleep_range(100, 200);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM1);
	value |= XUSB_PADCTL_ELPG_PROGRAM1_AUX_MUX_LP0_CLAMP_EN_EARLY;
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM1);

	usleep_range(100, 200);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM1);
	value |= XUSB_PADCTL_ELPG_PROGRAM1_AUX_MUX_LP0_CLAMP_EN;
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM1);
}

static int tegra210_uphy_init(struct tegra_xusb_padctl *padctl)
{
	if (padctl->pcie)
		tegra210_pex_uphy_enable(padctl);

	if (padctl->sata)
		tegra210_sata_uphy_enable(padctl);

	if (!tegra210_plle_hw_sequence_is_enabled())
		tegra210_plle_hw_sequence_start();
	else
		dev_dbg(padctl->dev, "PLLE is already in HW control\n");

	tegra210_aux_mux_lp0_clamp_disable(padctl);

	return 0;
}

static void __maybe_unused
tegra210_uphy_deinit(struct tegra_xusb_padctl *padctl)
{
	tegra210_aux_mux_lp0_clamp_enable(padctl);

	if (padctl->sata)
		tegra210_sata_uphy_disable(padctl);

	if (padctl->pcie)
		tegra210_pex_uphy_disable(padctl);
}

static int tegra210_hsic_set_idle(struct tegra_xusb_padctl *padctl,
				  unsigned int index, bool idle)
{
	u32 value;

	value = padctl_readl(padctl, XUSB_PADCTL_HSIC_PADX_CTL0(index));

	value &= ~(XUSB_PADCTL_HSIC_PAD_CTL0_RPU_DATA0 |
		   XUSB_PADCTL_HSIC_PAD_CTL0_RPU_DATA1 |
		   XUSB_PADCTL_HSIC_PAD_CTL0_RPD_STROBE);

	if (idle)
		value |= XUSB_PADCTL_HSIC_PAD_CTL0_RPD_DATA0 |
			 XUSB_PADCTL_HSIC_PAD_CTL0_RPD_DATA1 |
			 XUSB_PADCTL_HSIC_PAD_CTL0_RPU_STROBE;
	else
		value &= ~(XUSB_PADCTL_HSIC_PAD_CTL0_RPD_DATA0 |
			   XUSB_PADCTL_HSIC_PAD_CTL0_RPD_DATA1 |
			   XUSB_PADCTL_HSIC_PAD_CTL0_RPU_STROBE);

	padctl_writel(padctl, value, XUSB_PADCTL_HSIC_PADX_CTL0(index));

	return 0;
}

static int tegra210_usb3_enable_phy_sleepwalk(struct tegra_xusb_lane *lane,
					      enum usb_device_speed speed)
{
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	int port = tegra210_usb3_lane_map(lane);
	struct device *dev = padctl->dev;
	u32 value;

	if (port < 0) {
		dev_err(dev, "invalid usb3 port number\n");
		return -EINVAL;
	}

	mutex_lock(&padctl->lock);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM1);
	value |= XUSB_PADCTL_ELPG_PROGRAM1_SSPX_ELPG_CLAMP_EN_EARLY(port);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM1);

	usleep_range(100, 200);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM1);
	value |= XUSB_PADCTL_ELPG_PROGRAM1_SSPX_ELPG_CLAMP_EN(port);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM1);

	usleep_range(250, 350);

	mutex_unlock(&padctl->lock);

	return 0;
}

static int tegra210_usb3_disable_phy_sleepwalk(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	int port = tegra210_usb3_lane_map(lane);
	struct device *dev = padctl->dev;
	u32 value;

	if (port < 0) {
		dev_err(dev, "invalid usb3 port number\n");
		return -EINVAL;
	}

	mutex_lock(&padctl->lock);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM1);
	value &= ~XUSB_PADCTL_ELPG_PROGRAM1_SSPX_ELPG_CLAMP_EN_EARLY(port);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM1);

	usleep_range(100, 200);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM1);
	value &= ~XUSB_PADCTL_ELPG_PROGRAM1_SSPX_ELPG_CLAMP_EN(port);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM1);

	mutex_unlock(&padctl->lock);

	return 0;
}

static int tegra210_usb3_enable_phy_wake(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	int port = tegra210_usb3_lane_map(lane);
	struct device *dev = padctl->dev;
	u32 value;

	if (port < 0) {
		dev_err(dev, "invalid usb3 port number\n");
		return -EINVAL;
	}

	mutex_lock(&padctl->lock);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM_0);
	value &= ~ALL_WAKE_EVENTS;
	value |= SS_PORT_WAKEUP_EVENT(port);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM_0);

	usleep_range(10, 20);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM_0);
	value &= ~ALL_WAKE_EVENTS;
	value |= SS_PORT_WAKE_INTERRUPT_ENABLE(port);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM_0);

	mutex_unlock(&padctl->lock);

	return 0;
}

static int tegra210_usb3_disable_phy_wake(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	int port = tegra210_usb3_lane_map(lane);
	struct device *dev = padctl->dev;
	u32 value;

	if (port < 0) {
		dev_err(dev, "invalid usb3 port number\n");
		return -EINVAL;
	}

	mutex_lock(&padctl->lock);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM_0);
	value &= ~ALL_WAKE_EVENTS;
	value &= ~SS_PORT_WAKE_INTERRUPT_ENABLE(port);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM_0);

	usleep_range(10, 20);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM_0);
	value &= ~ALL_WAKE_EVENTS;
	value |= SS_PORT_WAKEUP_EVENT(port);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM_0);

	mutex_unlock(&padctl->lock);

	return 0;
}

static bool tegra210_usb3_phy_remote_wake_detected(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	int index = tegra210_usb3_lane_map(lane);
	u32 value;

	if (index < 0)
		return false;

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM_0);
	if ((value & SS_PORT_WAKE_INTERRUPT_ENABLE(index)) && (value & SS_PORT_WAKEUP_EVENT(index)))
		return true;

	return false;
}

static int tegra210_utmi_enable_phy_wake(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	unsigned int index = lane->index;
	u32 value;

	mutex_lock(&padctl->lock);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM_0);
	value &= ~ALL_WAKE_EVENTS;
	value |= USB2_PORT_WAKEUP_EVENT(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM_0);

	usleep_range(10, 20);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM_0);
	value &= ~ALL_WAKE_EVENTS;
	value |= USB2_PORT_WAKE_INTERRUPT_ENABLE(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM_0);

	mutex_unlock(&padctl->lock);

	return 0;
}

static int tegra210_utmi_disable_phy_wake(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	unsigned int index = lane->index;
	u32 value;

	mutex_lock(&padctl->lock);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM_0);
	value &= ~ALL_WAKE_EVENTS;
	value &= ~USB2_PORT_WAKE_INTERRUPT_ENABLE(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM_0);

	usleep_range(10, 20);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM_0);
	value &= ~ALL_WAKE_EVENTS;
	value |= USB2_PORT_WAKEUP_EVENT(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM_0);

	mutex_unlock(&padctl->lock);

	return 0;
}

static bool tegra210_utmi_phy_remote_wake_detected(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	unsigned int index = lane->index;
	u32 value;

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM_0);
	if ((value & USB2_PORT_WAKE_INTERRUPT_ENABLE(index)) &&
	    (value & USB2_PORT_WAKEUP_EVENT(index)))
		return true;

	return false;
}

static int tegra210_hsic_enable_phy_wake(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	unsigned int index = lane->index;
	u32 value;

	mutex_lock(&padctl->lock);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM_0);
	value &= ~ALL_WAKE_EVENTS;
	value |= USB2_HSIC_PORT_WAKEUP_EVENT(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM_0);

	usleep_range(10, 20);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM_0);
	value &= ~ALL_WAKE_EVENTS;
	value |= USB2_HSIC_PORT_WAKE_INTERRUPT_ENABLE(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM_0);

	mutex_unlock(&padctl->lock);

	return 0;
}

static int tegra210_hsic_disable_phy_wake(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	unsigned int index = lane->index;
	u32 value;

	mutex_lock(&padctl->lock);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM_0);
	value &= ~ALL_WAKE_EVENTS;
	value &= ~USB2_HSIC_PORT_WAKE_INTERRUPT_ENABLE(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM_0);

	usleep_range(10, 20);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM_0);
	value &= ~ALL_WAKE_EVENTS;
	value |= USB2_HSIC_PORT_WAKEUP_EVENT(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM_0);

	mutex_unlock(&padctl->lock);

	return 0;
}

static bool tegra210_hsic_phy_remote_wake_detected(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	unsigned int index = lane->index;
	u32 value;

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM_0);
	if ((value & USB2_HSIC_PORT_WAKE_INTERRUPT_ENABLE(index)) &&
	    (value & USB2_HSIC_PORT_WAKEUP_EVENT(index)))
		return true;

	return false;
}

#define padctl_pmc_readl(_priv, _offset)						\
({											\
	u32 value;									\
	WARN(regmap_read(_priv->regmap, _offset, &value), "read %s failed\n", #_offset);\
	value;										\
})

#define padctl_pmc_writel(_priv, _value, _offset)					\
	WARN(regmap_write(_priv->regmap, _offset, _value), "write %s failed\n", #_offset)

static int tegra210_pmc_utmi_enable_phy_sleepwalk(struct tegra_xusb_lane *lane,
						  enum usb_device_speed speed)
{
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	struct tegra210_xusb_padctl *priv = to_tegra210_xusb_padctl(padctl);
	unsigned int port = lane->index;
	u32 value, tctrl, pctrl, rpd_ctrl;

	if (!priv->regmap)
		return -EOPNOTSUPP;

	if (speed > USB_SPEED_HIGH)
		return -EINVAL;

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_BIAS_PAD_CTL1);
	tctrl = TCTRL_VALUE(value);
	pctrl = PCTRL_VALUE(value);

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_OTG_PADX_CTL1(port));
	rpd_ctrl = RPD_CTRL_VALUE(value);

	/* ensure sleepwalk logic is disabled */
	value = padctl_pmc_readl(priv, PMC_UTMIP_UHSIC_SLEEP_CFG(port));
	value &= ~UTMIP_MASTER_ENABLE(port);
	padctl_pmc_writel(priv, value, PMC_UTMIP_UHSIC_SLEEP_CFG(port));

	/* ensure sleepwalk logics are in low power mode */
	value = padctl_pmc_readl(priv, PMC_UTMIP_MASTER_CONFIG);
	value |= UTMIP_PWR(port);
	padctl_pmc_writel(priv, value, PMC_UTMIP_MASTER_CONFIG);

	/* set debounce time */
	value = padctl_pmc_readl(priv, PMC_USB_DEBOUNCE_DEL);
	value &= ~UTMIP_LINE_DEB_CNT(~0);
	value |= UTMIP_LINE_DEB_CNT(0x1);
	padctl_pmc_writel(priv, value, PMC_USB_DEBOUNCE_DEL);

	/* ensure fake events of sleepwalk logic are desiabled */
	value = padctl_pmc_readl(priv, PMC_UTMIP_UHSIC_FAKE(port));
	value &= ~(UTMIP_FAKE_USBOP_VAL(port) | UTMIP_FAKE_USBON_VAL(port) |
		   UTMIP_FAKE_USBOP_EN(port) | UTMIP_FAKE_USBON_EN(port));
	padctl_pmc_writel(priv, value, PMC_UTMIP_UHSIC_FAKE(port));

	/* ensure wake events of sleepwalk logic are not latched */
	value = padctl_pmc_readl(priv, PMC_UTMIP_UHSIC_LINE_WAKEUP);
	value &= ~UTMIP_LINE_WAKEUP_EN(port);
	padctl_pmc_writel(priv, value, PMC_UTMIP_UHSIC_LINE_WAKEUP);

	/* disable wake event triggers of sleepwalk logic */
	value = padctl_pmc_readl(priv, PMC_UTMIP_UHSIC_SLEEP_CFG(port));
	value &= ~UTMIP_WAKE_VAL(port, ~0);
	value |= UTMIP_WAKE_VAL_NONE(port);
	padctl_pmc_writel(priv, value, PMC_UTMIP_UHSIC_SLEEP_CFG(port));

	/* power down the line state detectors of the pad */
	value = padctl_pmc_readl(priv, PMC_USB_AO);
	value |= (USBOP_VAL_PD(port) | USBON_VAL_PD(port));
	padctl_pmc_writel(priv, value, PMC_USB_AO);

	/* save state per speed */
	value = padctl_pmc_readl(priv, PMC_UTMIP_UHSIC_SAVED_STATE(port));
	value &= ~SPEED(port, ~0);

	switch (speed) {
	case USB_SPEED_HIGH:
		value |= UTMI_HS(port);
		break;

	case USB_SPEED_FULL:
		value |= UTMI_FS(port);
		break;

	case USB_SPEED_LOW:
		value |= UTMI_LS(port);
		break;

	default:
		value |= UTMI_RST(port);
		break;
	}

	padctl_pmc_writel(priv, value, PMC_UTMIP_UHSIC_SAVED_STATE(port));

	/* enable the trigger of the sleepwalk logic */
	value = padctl_pmc_readl(priv, PMC_UTMIP_UHSIC_SLEEPWALK_CFG(port));
	value |= UTMIP_LINEVAL_WALK_EN(port);
	padctl_pmc_writel(priv, value, PMC_UTMIP_UHSIC_SLEEPWALK_CFG(port));

	/*
	 * Reset the walk pointer and clear the alarm of the sleepwalk logic,
	 * as well as capture the configuration of the USB2.0 pad.
	 */
	value = padctl_pmc_readl(priv, PMC_UTMIP_UHSIC_TRIGGERS);
	value |= UTMIP_CLR_WALK_PTR(port) | UTMIP_CLR_WAKE_ALARM(port) | UTMIP_CAP_CFG(port);
	padctl_pmc_writel(priv, value, PMC_UTMIP_UHSIC_TRIGGERS);

	/* program electrical parameters read from XUSB PADCTL */
	value = padctl_pmc_readl(priv, PMC_UTMIP_TERM_PAD_CFG);
	value &= ~(TCTRL_VAL(~0) | PCTRL_VAL(~0));
	value |= (TCTRL_VAL(tctrl) | PCTRL_VAL(pctrl));
	padctl_pmc_writel(priv, value, PMC_UTMIP_TERM_PAD_CFG);

	value = padctl_pmc_readl(priv, PMC_UTMIP_PAD_CFGX(port));
	value &= ~RPD_CTRL_PX(~0);
	value |= RPD_CTRL_PX(rpd_ctrl);
	padctl_pmc_writel(priv, value, PMC_UTMIP_PAD_CFGX(port));

	/*
	 * Set up the pull-ups and pull-downs of the signals during the four
	 * stages of sleepwalk. If a device is connected, program sleepwalk
	 * logic to maintain a J and keep driving K upon seeing remote wake.
	 */
	value = padctl_pmc_readl(priv, PMC_UTMIP_SLEEPWALK_PX(port));
	value = UTMIP_USBOP_RPD_A | UTMIP_USBOP_RPD_B | UTMIP_USBOP_RPD_C | UTMIP_USBOP_RPD_D;
	value |= UTMIP_USBON_RPD_A | UTMIP_USBON_RPD_B | UTMIP_USBON_RPD_C | UTMIP_USBON_RPD_D;

	switch (speed) {
	case USB_SPEED_HIGH:
	case USB_SPEED_FULL:
		/* J state: D+/D- = high/low, K state: D+/D- = low/high */
		value |= UTMIP_HIGHZ_A;
		value |= UTMIP_AP_A;
		value |= UTMIP_AN_B | UTMIP_AN_C | UTMIP_AN_D;
		break;

	case USB_SPEED_LOW:
		/* J state: D+/D- = low/high, K state: D+/D- = high/low */
		value |= UTMIP_HIGHZ_A;
		value |= UTMIP_AN_A;
		value |= UTMIP_AP_B | UTMIP_AP_C | UTMIP_AP_D;
		break;

	default:
		value |= UTMIP_HIGHZ_A | UTMIP_HIGHZ_B | UTMIP_HIGHZ_C | UTMIP_HIGHZ_D;
		break;
	}

	padctl_pmc_writel(priv, value, PMC_UTMIP_SLEEPWALK_PX(port));

	/* power up the line state detectors of the pad */
	value = padctl_pmc_readl(priv, PMC_USB_AO);
	value &= ~(USBOP_VAL_PD(port) | USBON_VAL_PD(port));
	padctl_pmc_writel(priv, value, PMC_USB_AO);

	usleep_range(50, 100);

	/* switch the electric control of the USB2.0 pad to PMC */
	value = padctl_pmc_readl(priv, PMC_UTMIP_UHSIC_SLEEP_CFG(port));
	value |= UTMIP_FSLS_USE_PMC(port) | UTMIP_PCTRL_USE_PMC(port) | UTMIP_TCTRL_USE_PMC(port);
	padctl_pmc_writel(priv, value, PMC_UTMIP_UHSIC_SLEEP_CFG(port));

	value = padctl_pmc_readl(priv, PMC_UTMIP_UHSIC_SLEEP_CFG1);
	value |= UTMIP_RPD_CTRL_USE_PMC_PX(port) | UTMIP_RPU_SWITC_LOW_USE_PMC_PX(port);
	padctl_pmc_writel(priv, value, PMC_UTMIP_UHSIC_SLEEP_CFG1);

	/* set the wake signaling trigger events */
	value = padctl_pmc_readl(priv, PMC_UTMIP_UHSIC_SLEEP_CFG(port));
	value &= ~UTMIP_WAKE_VAL(port, ~0);
	value |= UTMIP_WAKE_VAL_ANY(port);
	padctl_pmc_writel(priv, value, PMC_UTMIP_UHSIC_SLEEP_CFG(port));

	/* enable the wake detection */
	value = padctl_pmc_readl(priv, PMC_UTMIP_UHSIC_SLEEP_CFG(port));
	value |= UTMIP_MASTER_ENABLE(port);
	padctl_pmc_writel(priv, value, PMC_UTMIP_UHSIC_SLEEP_CFG(port));

	value = padctl_pmc_readl(priv, PMC_UTMIP_UHSIC_LINE_WAKEUP);
	value |= UTMIP_LINE_WAKEUP_EN(port);
	padctl_pmc_writel(priv, value, PMC_UTMIP_UHSIC_LINE_WAKEUP);

	return 0;
}

static int tegra210_pmc_utmi_disable_phy_sleepwalk(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	struct tegra210_xusb_padctl *priv = to_tegra210_xusb_padctl(padctl);
	unsigned int port = lane->index;
	u32 value;

	if (!priv->regmap)
		return -EOPNOTSUPP;

	/* disable the wake detection */
	value = padctl_pmc_readl(priv, PMC_UTMIP_UHSIC_SLEEP_CFG(port));
	value &= ~UTMIP_MASTER_ENABLE(port);
	padctl_pmc_writel(priv, value, PMC_UTMIP_UHSIC_SLEEP_CFG(port));

	value = padctl_pmc_readl(priv, PMC_UTMIP_UHSIC_LINE_WAKEUP);
	value &= ~UTMIP_LINE_WAKEUP_EN(port);
	padctl_pmc_writel(priv, value, PMC_UTMIP_UHSIC_LINE_WAKEUP);

	/* switch the electric control of the USB2.0 pad to XUSB or USB2 */
	value = padctl_pmc_readl(priv, PMC_UTMIP_UHSIC_SLEEP_CFG(port));
	value &= ~(UTMIP_FSLS_USE_PMC(port) | UTMIP_PCTRL_USE_PMC(port) |
		   UTMIP_TCTRL_USE_PMC(port));
	padctl_pmc_writel(priv, value, PMC_UTMIP_UHSIC_SLEEP_CFG(port));

	value = padctl_pmc_readl(priv, PMC_UTMIP_UHSIC_SLEEP_CFG1);
	value &= ~(UTMIP_RPD_CTRL_USE_PMC_PX(port) | UTMIP_RPU_SWITC_LOW_USE_PMC_PX(port));
	padctl_pmc_writel(priv, value, PMC_UTMIP_UHSIC_SLEEP_CFG1);

	/* disable wake event triggers of sleepwalk logic */
	value = padctl_pmc_readl(priv, PMC_UTMIP_UHSIC_SLEEP_CFG(port));
	value &= ~UTMIP_WAKE_VAL(port, ~0);
	value |= UTMIP_WAKE_VAL_NONE(port);
	padctl_pmc_writel(priv, value, PMC_UTMIP_UHSIC_SLEEP_CFG(port));

	/* power down the line state detectors of the port */
	value = padctl_pmc_readl(priv, PMC_USB_AO);
	value |= (USBOP_VAL_PD(port) | USBON_VAL_PD(port));
	padctl_pmc_writel(priv, value, PMC_USB_AO);

	/* clear alarm of the sleepwalk logic */
	value = padctl_pmc_readl(priv, PMC_UTMIP_UHSIC_TRIGGERS);
	value |= UTMIP_CLR_WAKE_ALARM(port);
	padctl_pmc_writel(priv, value, PMC_UTMIP_UHSIC_TRIGGERS);

	return 0;
}

static int tegra210_pmc_hsic_enable_phy_sleepwalk(struct tegra_xusb_lane *lane,
						  enum usb_device_speed speed)
{
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	struct tegra210_xusb_padctl *priv = to_tegra210_xusb_padctl(padctl);
	u32 value;

	if (!priv->regmap)
		return -EOPNOTSUPP;

	/* ensure sleepwalk logic is disabled */
	value = padctl_pmc_readl(priv, PMC_UHSIC_SLEEP_CFG);
	value &= ~UHSIC_MASTER_ENABLE;
	padctl_pmc_writel(priv, value, PMC_UHSIC_SLEEP_CFG);

	/* ensure sleepwalk logics are in low power mode */
	value = padctl_pmc_readl(priv, PMC_UTMIP_MASTER_CONFIG);
	value |= UHSIC_PWR;
	padctl_pmc_writel(priv, value, PMC_UTMIP_MASTER_CONFIG);

	/* set debounce time */
	value = padctl_pmc_readl(priv, PMC_USB_DEBOUNCE_DEL);
	value &= ~UHSIC_LINE_DEB_CNT(~0);
	value |= UHSIC_LINE_DEB_CNT(0x1);
	padctl_pmc_writel(priv, value, PMC_USB_DEBOUNCE_DEL);

	/* ensure fake events of sleepwalk logic are desiabled */
	value = padctl_pmc_readl(priv, PMC_UHSIC_FAKE);
	value &= ~(UHSIC_FAKE_STROBE_VAL | UHSIC_FAKE_DATA_VAL |
		   UHSIC_FAKE_STROBE_EN | UHSIC_FAKE_DATA_EN);
	padctl_pmc_writel(priv, value, PMC_UHSIC_FAKE);

	/* ensure wake events of sleepwalk logic are not latched */
	value = padctl_pmc_readl(priv, PMC_UTMIP_UHSIC_LINE_WAKEUP);
	value &= ~UHSIC_LINE_WAKEUP_EN;
	padctl_pmc_writel(priv, value, PMC_UTMIP_UHSIC_LINE_WAKEUP);

	/* disable wake event triggers of sleepwalk logic */
	value = padctl_pmc_readl(priv, PMC_UHSIC_SLEEP_CFG);
	value &= ~UHSIC_WAKE_VAL(~0);
	value |= UHSIC_WAKE_VAL_NONE;
	padctl_pmc_writel(priv, value, PMC_UHSIC_SLEEP_CFG);

	/* power down the line state detectors of the port */
	value = padctl_pmc_readl(priv, PMC_USB_AO);
	value |= STROBE_VAL_PD | DATA0_VAL_PD | DATA1_VAL_PD;
	padctl_pmc_writel(priv, value, PMC_USB_AO);

	/* save state, HSIC always comes up as HS */
	value = padctl_pmc_readl(priv, PMC_UHSIC_SAVED_STATE);
	value &= ~UHSIC_MODE(~0);
	value |= UHSIC_HS;
	padctl_pmc_writel(priv, value, PMC_UHSIC_SAVED_STATE);

	/* enable the trigger of the sleepwalk logic */
	value = padctl_pmc_readl(priv, PMC_UHSIC_SLEEPWALK_CFG);
	value |= UHSIC_WAKE_WALK_EN | UHSIC_LINEVAL_WALK_EN;
	padctl_pmc_writel(priv, value, PMC_UHSIC_SLEEPWALK_CFG);

	/*
	 * Reset the walk pointer and clear the alarm of the sleepwalk logic,
	 * as well as capture the configuration of the USB2.0 port.
	 */
	value = padctl_pmc_readl(priv, PMC_UTMIP_UHSIC_TRIGGERS);
	value |= UHSIC_CLR_WALK_PTR | UHSIC_CLR_WAKE_ALARM;
	padctl_pmc_writel(priv, value, PMC_UTMIP_UHSIC_TRIGGERS);

	/*
	 * Set up the pull-ups and pull-downs of the signals during the four
	 * stages of sleepwalk. Maintain a HSIC IDLE and keep driving HSIC
	 * RESUME upon remote wake.
	 */
	value = padctl_pmc_readl(priv, PMC_UHSIC_SLEEPWALK_P0);
	value = UHSIC_DATA0_RPD_A | UHSIC_DATA0_RPU_B | UHSIC_DATA0_RPU_C | UHSIC_DATA0_RPU_D |
		UHSIC_STROBE_RPU_A | UHSIC_STROBE_RPD_B | UHSIC_STROBE_RPD_C | UHSIC_STROBE_RPD_D;
	padctl_pmc_writel(priv, value, PMC_UHSIC_SLEEPWALK_P0);

	/* power up the line state detectors of the port */
	value = padctl_pmc_readl(priv, PMC_USB_AO);
	value &= ~(STROBE_VAL_PD | DATA0_VAL_PD | DATA1_VAL_PD);
	padctl_pmc_writel(priv, value, PMC_USB_AO);

	usleep_range(50, 100);

	/* set the wake signaling trigger events */
	value = padctl_pmc_readl(priv, PMC_UHSIC_SLEEP_CFG);
	value &= ~UHSIC_WAKE_VAL(~0);
	value |= UHSIC_WAKE_VAL_SD10;
	padctl_pmc_writel(priv, value, PMC_UHSIC_SLEEP_CFG);

	/* enable the wake detection */
	value = padctl_pmc_readl(priv, PMC_UHSIC_SLEEP_CFG);
	value |= UHSIC_MASTER_ENABLE;
	padctl_pmc_writel(priv, value, PMC_UHSIC_SLEEP_CFG);

	value = padctl_pmc_readl(priv, PMC_UTMIP_UHSIC_LINE_WAKEUP);
	value |= UHSIC_LINE_WAKEUP_EN;
	padctl_pmc_writel(priv, value, PMC_UTMIP_UHSIC_LINE_WAKEUP);

	return 0;
}

static int tegra210_pmc_hsic_disable_phy_sleepwalk(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	struct tegra210_xusb_padctl *priv = to_tegra210_xusb_padctl(padctl);
	u32 value;

	if (!priv->regmap)
		return -EOPNOTSUPP;

	/* disable the wake detection */
	value = padctl_pmc_readl(priv, PMC_UHSIC_SLEEP_CFG);
	value &= ~UHSIC_MASTER_ENABLE;
	padctl_pmc_writel(priv, value, PMC_UHSIC_SLEEP_CFG);

	value = padctl_pmc_readl(priv, PMC_UTMIP_UHSIC_LINE_WAKEUP);
	value &= ~UHSIC_LINE_WAKEUP_EN;
	padctl_pmc_writel(priv, value, PMC_UTMIP_UHSIC_LINE_WAKEUP);

	/* disable wake event triggers of sleepwalk logic */
	value = padctl_pmc_readl(priv, PMC_UHSIC_SLEEP_CFG);
	value &= ~UHSIC_WAKE_VAL(~0);
	value |= UHSIC_WAKE_VAL_NONE;
	padctl_pmc_writel(priv, value, PMC_UHSIC_SLEEP_CFG);

	/* power down the line state detectors of the port */
	value = padctl_pmc_readl(priv, PMC_USB_AO);
	value |= STROBE_VAL_PD | DATA0_VAL_PD | DATA1_VAL_PD;
	padctl_pmc_writel(priv, value, PMC_USB_AO);

	/* clear alarm of the sleepwalk logic */
	value = padctl_pmc_readl(priv, PMC_UTMIP_UHSIC_TRIGGERS);
	value |= UHSIC_CLR_WAKE_ALARM;
	padctl_pmc_writel(priv, value, PMC_UTMIP_UHSIC_TRIGGERS);

	return 0;
}

static int tegra210_usb3_set_lfps_detect(struct tegra_xusb_padctl *padctl,
					 unsigned int index, bool enable)
{
	struct tegra_xusb_port *port;
	struct tegra_xusb_lane *lane;
	u32 value, offset;

	port = tegra_xusb_find_port(padctl, "usb3", index);
	if (!port)
		return -ENODEV;

	lane = port->lane;

	if (lane->pad == padctl->pcie)
		offset = XUSB_PADCTL_UPHY_MISC_PAD_PX_CTL1(lane->index);
	else
		offset = XUSB_PADCTL_UPHY_MISC_PAD_S0_CTL1;

	value = padctl_readl(padctl, offset);

	value &= ~((XUSB_PADCTL_UPHY_MISC_PAD_CTL1_AUX_RX_IDLE_MODE_MASK <<
		    XUSB_PADCTL_UPHY_MISC_PAD_CTL1_AUX_RX_IDLE_MODE_SHIFT) |
		   XUSB_PADCTL_UPHY_MISC_PAD_CTL1_AUX_RX_TERM_EN |
		   XUSB_PADCTL_UPHY_MISC_PAD_CTL1_AUX_RX_MODE_OVRD);

	if (!enable) {
		value |= (XUSB_PADCTL_UPHY_MISC_PAD_CTL1_AUX_RX_IDLE_MODE_VAL <<
			  XUSB_PADCTL_UPHY_MISC_PAD_CTL1_AUX_RX_IDLE_MODE_SHIFT) |
			 XUSB_PADCTL_UPHY_MISC_PAD_CTL1_AUX_RX_TERM_EN |
			 XUSB_PADCTL_UPHY_MISC_PAD_CTL1_AUX_RX_MODE_OVRD;
	}

	padctl_writel(padctl, value, offset);

	return 0;
}

#define TEGRA210_LANE(_name, _offset, _shift, _mask, _type)		\
	{								\
		.name = _name,						\
		.offset = _offset,					\
		.shift = _shift,					\
		.mask = _mask,						\
		.num_funcs = ARRAY_SIZE(tegra210_##_type##_functions),	\
		.funcs = tegra210_##_type##_functions,			\
	}

static const char *tegra210_usb2_functions[] = {
	"snps",
	"xusb",
	"uart"
};

static const struct tegra_xusb_lane_soc tegra210_usb2_lanes[] = {
	TEGRA210_LANE("usb2-0", 0x004,  0, 0x3, usb2),
	TEGRA210_LANE("usb2-1", 0x004,  2, 0x3, usb2),
	TEGRA210_LANE("usb2-2", 0x004,  4, 0x3, usb2),
	TEGRA210_LANE("usb2-3", 0x004,  6, 0x3, usb2),
};

static struct tegra_xusb_lane *
tegra210_usb2_lane_probe(struct tegra_xusb_pad *pad, struct device_node *np,
			 unsigned int index)
{
	struct tegra_xusb_usb2_lane *usb2;
	int err;

	usb2 = kzalloc(sizeof(*usb2), GFP_KERNEL);
	if (!usb2)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&usb2->base.list);
	usb2->base.soc = &pad->soc->lanes[index];
	usb2->base.index = index;
	usb2->base.pad = pad;
	usb2->base.np = np;

	err = tegra_xusb_lane_parse_dt(&usb2->base, np);
	if (err < 0) {
		kfree(usb2);
		return ERR_PTR(err);
	}

	return &usb2->base;
}

static void tegra210_usb2_lane_remove(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_usb2_lane *usb2 = to_usb2_lane(lane);

	kfree(usb2);
}

static const struct tegra_xusb_lane_ops tegra210_usb2_lane_ops = {
	.probe = tegra210_usb2_lane_probe,
	.remove = tegra210_usb2_lane_remove,
	.enable_phy_sleepwalk = tegra210_pmc_utmi_enable_phy_sleepwalk,
	.disable_phy_sleepwalk = tegra210_pmc_utmi_disable_phy_sleepwalk,
	.enable_phy_wake = tegra210_utmi_enable_phy_wake,
	.disable_phy_wake = tegra210_utmi_disable_phy_wake,
	.remote_wake_detected = tegra210_utmi_phy_remote_wake_detected,
};

static int tegra210_usb2_phy_init(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	unsigned int index = lane->index;
	struct tegra_xusb_usb2_port *port;
	int err;
	u32 value;

	port = tegra_xusb_find_usb2_port(padctl, index);
	if (!port) {
		dev_err(&phy->dev, "no port found for USB2 lane %u\n", index);
		return -ENODEV;
	}

	if (port->supply && port->mode == USB_DR_MODE_HOST) {
		err = regulator_enable(port->supply);
		if (err)
			return err;
	}

	mutex_lock(&padctl->lock);

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_PAD_MUX);
	value &= ~(XUSB_PADCTL_USB2_PAD_MUX_USB2_BIAS_PAD_MASK <<
		   XUSB_PADCTL_USB2_PAD_MUX_USB2_BIAS_PAD_SHIFT);
	value |= XUSB_PADCTL_USB2_PAD_MUX_USB2_BIAS_PAD_XUSB <<
		 XUSB_PADCTL_USB2_PAD_MUX_USB2_BIAS_PAD_SHIFT;
	padctl_writel(padctl, value, XUSB_PADCTL_USB2_PAD_MUX);

	mutex_unlock(&padctl->lock);

	return 0;
}

static int tegra210_usb2_phy_exit(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	struct tegra_xusb_usb2_port *port;
	int err;

	port = tegra_xusb_find_usb2_port(padctl, lane->index);
	if (!port) {
		dev_err(&phy->dev, "no port found for USB2 lane %u\n", lane->index);
		return -ENODEV;
	}

	if (port->supply && port->mode == USB_DR_MODE_HOST) {
		err = regulator_disable(port->supply);
		if (err)
			return err;
	}

	return 0;
}

static int tegra210_xusb_padctl_vbus_override(struct tegra_xusb_padctl *padctl,
					      bool status)
{
	u32 value;

	dev_dbg(padctl->dev, "%s vbus override\n", status ? "set" : "clear");

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_VBUS_ID);

	if (status) {
		value |= XUSB_PADCTL_USB2_VBUS_ID_OVERRIDE_VBUS_ON;
		value &= ~(XUSB_PADCTL_USB2_VBUS_ID_OVERRIDE_MASK <<
			   XUSB_PADCTL_USB2_VBUS_ID_OVERRIDE_SHIFT);
		value |= XUSB_PADCTL_USB2_VBUS_ID_OVERRIDE_FLOATING <<
			 XUSB_PADCTL_USB2_VBUS_ID_OVERRIDE_SHIFT;
	} else {
		value &= ~XUSB_PADCTL_USB2_VBUS_ID_OVERRIDE_VBUS_ON;
	}

	padctl_writel(padctl, value, XUSB_PADCTL_USB2_VBUS_ID);

	return 0;
}

static int tegra210_xusb_padctl_id_override(struct tegra_xusb_padctl *padctl,
					    bool status)
{
	u32 value;

	dev_dbg(padctl->dev, "%s id override\n", status ? "set" : "clear");

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_VBUS_ID);

	if (status) {
		if (value & XUSB_PADCTL_USB2_VBUS_ID_OVERRIDE_VBUS_ON) {
			value &= ~XUSB_PADCTL_USB2_VBUS_ID_OVERRIDE_VBUS_ON;
			padctl_writel(padctl, value, XUSB_PADCTL_USB2_VBUS_ID);
			usleep_range(1000, 2000);

			value = padctl_readl(padctl, XUSB_PADCTL_USB2_VBUS_ID);
		}

		value &= ~(XUSB_PADCTL_USB2_VBUS_ID_OVERRIDE_MASK <<
			   XUSB_PADCTL_USB2_VBUS_ID_OVERRIDE_SHIFT);
		value |= XUSB_PADCTL_USB2_VBUS_ID_OVERRIDE_GROUNDED <<
			 XUSB_PADCTL_USB2_VBUS_ID_OVERRIDE_SHIFT;
	} else {
		value &= ~(XUSB_PADCTL_USB2_VBUS_ID_OVERRIDE_MASK <<
			   XUSB_PADCTL_USB2_VBUS_ID_OVERRIDE_SHIFT);
		value |= XUSB_PADCTL_USB2_VBUS_ID_OVERRIDE_FLOATING <<
			 XUSB_PADCTL_USB2_VBUS_ID_OVERRIDE_SHIFT;
	}

	padctl_writel(padctl, value, XUSB_PADCTL_USB2_VBUS_ID);

	return 0;
}

static int tegra210_usb2_phy_set_mode(struct phy *phy, enum phy_mode mode,
				      int submode)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	struct tegra_xusb_usb2_port *port = tegra_xusb_find_usb2_port(padctl,
								lane->index);
	int err = 0;

	mutex_lock(&padctl->lock);

	dev_dbg(&port->base.dev, "%s: mode %d", __func__, mode);

	if (mode == PHY_MODE_USB_OTG) {
		if (submode == USB_ROLE_HOST) {
			tegra210_xusb_padctl_id_override(padctl, true);

			err = regulator_enable(port->supply);
		} else if (submode == USB_ROLE_DEVICE) {
			tegra210_xusb_padctl_vbus_override(padctl, true);
		} else if (submode == USB_ROLE_NONE) {
			/*
			 * When port is peripheral only or role transitions to
			 * USB_ROLE_NONE from USB_ROLE_DEVICE, regulator is not
			 * be enabled.
			 */
			if (regulator_is_enabled(port->supply))
				regulator_disable(port->supply);

			tegra210_xusb_padctl_id_override(padctl, false);
			tegra210_xusb_padctl_vbus_override(padctl, false);
		}
	}

	mutex_unlock(&padctl->lock);

	return err;
}

static int tegra210_usb2_phy_power_on(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_usb2_lane *usb2 = to_usb2_lane(lane);
	struct tegra_xusb_usb2_pad *pad = to_usb2_pad(lane->pad);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	struct tegra210_xusb_padctl *priv;
	struct tegra_xusb_usb2_port *port;
	unsigned int index = lane->index;
	u32 value;
	int err;

	port = tegra_xusb_find_usb2_port(padctl, index);
	if (!port) {
		dev_err(&phy->dev, "no port found for USB2 lane %u\n", index);
		return -ENODEV;
	}

	priv = to_tegra210_xusb_padctl(padctl);

	mutex_lock(&padctl->lock);

	if (port->usb3_port_fake != -1) {
		value = padctl_readl(padctl, XUSB_PADCTL_SS_PORT_MAP);
		value &= ~XUSB_PADCTL_SS_PORT_MAP_PORTX_MAP_MASK(
					port->usb3_port_fake);
		value |= XUSB_PADCTL_SS_PORT_MAP_PORTX_MAP(
					port->usb3_port_fake, index);
		padctl_writel(padctl, value, XUSB_PADCTL_SS_PORT_MAP);

		value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM1);
		value &= ~XUSB_PADCTL_ELPG_PROGRAM1_SSPX_ELPG_VCORE_DOWN(
					port->usb3_port_fake);
		padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM1);

		usleep_range(100, 200);

		value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM1);
		value &= ~XUSB_PADCTL_ELPG_PROGRAM1_SSPX_ELPG_CLAMP_EN_EARLY(
					port->usb3_port_fake);
		padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM1);

		usleep_range(100, 200);

		value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM1);
		value &= ~XUSB_PADCTL_ELPG_PROGRAM1_SSPX_ELPG_CLAMP_EN(
					port->usb3_port_fake);
		padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM1);
	}

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_BIAS_PAD_CTL0);
	value &= ~((XUSB_PADCTL_USB2_BIAS_PAD_CTL0_HS_SQUELCH_LEVEL_MASK <<
		    XUSB_PADCTL_USB2_BIAS_PAD_CTL0_HS_SQUELCH_LEVEL_SHIFT) |
		   (XUSB_PADCTL_USB2_BIAS_PAD_CTL0_HS_DISCON_LEVEL_MASK <<
		    XUSB_PADCTL_USB2_BIAS_PAD_CTL0_HS_DISCON_LEVEL_SHIFT));
	value |= (XUSB_PADCTL_USB2_BIAS_PAD_CTL0_HS_DISCON_LEVEL_VAL <<
		  XUSB_PADCTL_USB2_BIAS_PAD_CTL0_HS_DISCON_LEVEL_SHIFT);

	if (tegra_sku_info.revision < TEGRA_REVISION_A02)
		value |=
			(XUSB_PADCTL_USB2_BIAS_PAD_CTL0_HS_SQUELCH_LEVEL_VAL <<
			XUSB_PADCTL_USB2_BIAS_PAD_CTL0_HS_SQUELCH_LEVEL_SHIFT);

	padctl_writel(padctl, value, XUSB_PADCTL_USB2_BIAS_PAD_CTL0);

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_PORT_CAP);
	value &= ~XUSB_PADCTL_USB2_PORT_CAP_PORTX_CAP_MASK(index);
	if (port->mode == USB_DR_MODE_UNKNOWN)
		value |= XUSB_PADCTL_USB2_PORT_CAP_PORTX_CAP_DISABLED(index);
	else if (port->mode == USB_DR_MODE_PERIPHERAL)
		value |= XUSB_PADCTL_USB2_PORT_CAP_PORTX_CAP_DEVICE(index);
	else if (port->mode == USB_DR_MODE_HOST)
		value |= XUSB_PADCTL_USB2_PORT_CAP_PORTX_CAP_HOST(index);
	else if (port->mode == USB_DR_MODE_OTG)
		value |= XUSB_PADCTL_USB2_PORT_CAP_PORTX_CAP_OTG(index);
	padctl_writel(padctl, value, XUSB_PADCTL_USB2_PORT_CAP);

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_OTG_PADX_CTL0(index));
	value &= ~((XUSB_PADCTL_USB2_OTG_PAD_CTL0_HS_CURR_LEVEL_MASK <<
		    XUSB_PADCTL_USB2_OTG_PAD_CTL0_HS_CURR_LEVEL_SHIFT) |
		   XUSB_PADCTL_USB2_OTG_PAD_CTL0_PD |
		   XUSB_PADCTL_USB2_OTG_PAD_CTL0_PD2 |
		   XUSB_PADCTL_USB2_OTG_PAD_CTL0_PD_ZI);
	value |= (priv->fuse.hs_curr_level[index] +
		  usb2->hs_curr_level_offset) <<
		 XUSB_PADCTL_USB2_OTG_PAD_CTL0_HS_CURR_LEVEL_SHIFT;
	padctl_writel(padctl, value, XUSB_PADCTL_USB2_OTG_PADX_CTL0(index));

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_OTG_PADX_CTL1(index));
	value &= ~((XUSB_PADCTL_USB2_OTG_PAD_CTL1_TERM_RANGE_ADJ_MASK <<
		    XUSB_PADCTL_USB2_OTG_PAD_CTL1_TERM_RANGE_ADJ_SHIFT) |
		   (XUSB_PADCTL_USB2_OTG_PAD_CTL1_RPD_CTRL_MASK <<
		    XUSB_PADCTL_USB2_OTG_PAD_CTL1_RPD_CTRL_SHIFT) |
		   XUSB_PADCTL_USB2_OTG_PAD_CTL1_PD_DR |
		   XUSB_PADCTL_USB2_OTG_PAD_CTL1_PD_CHRP_OVRD |
		   XUSB_PADCTL_USB2_OTG_PAD_CTL1_PD_DISC_OVRD);
	value |= (priv->fuse.hs_term_range_adj <<
		  XUSB_PADCTL_USB2_OTG_PAD_CTL1_TERM_RANGE_ADJ_SHIFT) |
		 (priv->fuse.rpd_ctrl <<
		  XUSB_PADCTL_USB2_OTG_PAD_CTL1_RPD_CTRL_SHIFT);
	padctl_writel(padctl, value, XUSB_PADCTL_USB2_OTG_PADX_CTL1(index));

	value = padctl_readl(padctl,
			     XUSB_PADCTL_USB2_BATTERY_CHRG_OTGPADX_CTL1(index));
	value &= ~(XUSB_PADCTL_USB2_BATTERY_CHRG_OTGPAD_CTL1_VREG_LEV_MASK <<
		   XUSB_PADCTL_USB2_BATTERY_CHRG_OTGPAD_CTL1_VREG_LEV_SHIFT);
	if (port->mode == USB_DR_MODE_HOST)
		value |= XUSB_PADCTL_USB2_BATTERY_CHRG_OTGPAD_CTL1_VREG_FIX18;
	else
		value |=
		      XUSB_PADCTL_USB2_BATTERY_CHRG_OTGPAD_CTL1_VREG_LEV_VAL <<
		      XUSB_PADCTL_USB2_BATTERY_CHRG_OTGPAD_CTL1_VREG_LEV_SHIFT;
	padctl_writel(padctl, value,
		      XUSB_PADCTL_USB2_BATTERY_CHRG_OTGPADX_CTL1(index));

	if (pad->enable > 0) {
		pad->enable++;
		mutex_unlock(&padctl->lock);
		return 0;
	}

	err = clk_prepare_enable(pad->clk);
	if (err)
		goto out;

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_BIAS_PAD_CTL1);
	value &= ~((XUSB_PADCTL_USB2_BIAS_PAD_CTL1_TRK_START_TIMER_MASK <<
		    XUSB_PADCTL_USB2_BIAS_PAD_CTL1_TRK_START_TIMER_SHIFT) |
		   (XUSB_PADCTL_USB2_BIAS_PAD_CTL1_TRK_DONE_RESET_TIMER_MASK <<
		    XUSB_PADCTL_USB2_BIAS_PAD_CTL1_TRK_DONE_RESET_TIMER_SHIFT));
	value |= (XUSB_PADCTL_USB2_BIAS_PAD_CTL1_TRK_START_TIMER_VAL <<
		  XUSB_PADCTL_USB2_BIAS_PAD_CTL1_TRK_START_TIMER_SHIFT) |
		 (XUSB_PADCTL_USB2_BIAS_PAD_CTL1_TRK_DONE_RESET_TIMER_VAL <<
		  XUSB_PADCTL_USB2_BIAS_PAD_CTL1_TRK_DONE_RESET_TIMER_SHIFT);
	padctl_writel(padctl, value, XUSB_PADCTL_USB2_BIAS_PAD_CTL1);

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_BIAS_PAD_CTL0);
	value &= ~XUSB_PADCTL_USB2_BIAS_PAD_CTL0_PD;
	padctl_writel(padctl, value, XUSB_PADCTL_USB2_BIAS_PAD_CTL0);

	udelay(1);

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_BIAS_PAD_CTL1);
	value &= ~XUSB_PADCTL_USB2_BIAS_PAD_CTL1_PD_TRK;
	padctl_writel(padctl, value, XUSB_PADCTL_USB2_BIAS_PAD_CTL1);

	udelay(50);

	clk_disable_unprepare(pad->clk);

	pad->enable++;
	mutex_unlock(&padctl->lock);

	return 0;

out:
	mutex_unlock(&padctl->lock);
	return err;
}

static int tegra210_usb2_phy_power_off(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_usb2_pad *pad = to_usb2_pad(lane->pad);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	struct tegra_xusb_usb2_port *port;
	u32 value;

	port = tegra_xusb_find_usb2_port(padctl, lane->index);
	if (!port) {
		dev_err(&phy->dev, "no port found for USB2 lane %u\n",
			lane->index);
		return -ENODEV;
	}

	mutex_lock(&padctl->lock);

	if (port->usb3_port_fake != -1) {
		value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM1);
		value |= XUSB_PADCTL_ELPG_PROGRAM1_SSPX_ELPG_CLAMP_EN_EARLY(
					port->usb3_port_fake);
		padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM1);

		usleep_range(100, 200);

		value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM1);
		value |= XUSB_PADCTL_ELPG_PROGRAM1_SSPX_ELPG_CLAMP_EN(
					port->usb3_port_fake);
		padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM1);

		usleep_range(250, 350);

		value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM1);
		value |= XUSB_PADCTL_ELPG_PROGRAM1_SSPX_ELPG_VCORE_DOWN(
					port->usb3_port_fake);
		padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM1);

		value = padctl_readl(padctl, XUSB_PADCTL_SS_PORT_MAP);
		value |= XUSB_PADCTL_SS_PORT_MAP_PORTX_MAP(port->usb3_port_fake,
					XUSB_PADCTL_SS_PORT_MAP_PORT_DISABLED);
		padctl_writel(padctl, value, XUSB_PADCTL_SS_PORT_MAP);
	}

	if (WARN_ON(pad->enable == 0))
		goto out;

	if (--pad->enable > 0)
		goto out;

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_BIAS_PAD_CTL0);
	value |= XUSB_PADCTL_USB2_BIAS_PAD_CTL0_PD;
	padctl_writel(padctl, value, XUSB_PADCTL_USB2_BIAS_PAD_CTL0);

out:
	mutex_unlock(&padctl->lock);
	return 0;
}

static const struct phy_ops tegra210_usb2_phy_ops = {
	.init = tegra210_usb2_phy_init,
	.exit = tegra210_usb2_phy_exit,
	.power_on = tegra210_usb2_phy_power_on,
	.power_off = tegra210_usb2_phy_power_off,
	.set_mode = tegra210_usb2_phy_set_mode,
	.owner = THIS_MODULE,
};

static struct tegra_xusb_pad *
tegra210_usb2_pad_probe(struct tegra_xusb_padctl *padctl,
			const struct tegra_xusb_pad_soc *soc,
			struct device_node *np)
{
	struct tegra_xusb_usb2_pad *usb2;
	struct tegra_xusb_pad *pad;
	int err;

	usb2 = kzalloc(sizeof(*usb2), GFP_KERNEL);
	if (!usb2)
		return ERR_PTR(-ENOMEM);

	pad = &usb2->base;
	pad->ops = &tegra210_usb2_lane_ops;
	pad->soc = soc;

	err = tegra_xusb_pad_init(pad, padctl, np);
	if (err < 0) {
		kfree(usb2);
		goto out;
	}

	usb2->clk = devm_clk_get(&pad->dev, "trk");
	if (IS_ERR(usb2->clk)) {
		err = PTR_ERR(usb2->clk);
		dev_err(&pad->dev, "failed to get trk clock: %d\n", err);
		goto unregister;
	}

	err = tegra_xusb_pad_register(pad, &tegra210_usb2_phy_ops);
	if (err < 0)
		goto unregister;

	dev_set_drvdata(&pad->dev, pad);

	return pad;

unregister:
	device_unregister(&pad->dev);
out:
	return ERR_PTR(err);
}

static void tegra210_usb2_pad_remove(struct tegra_xusb_pad *pad)
{
	struct tegra_xusb_usb2_pad *usb2 = to_usb2_pad(pad);

	kfree(usb2);
}

static const struct tegra_xusb_pad_ops tegra210_usb2_ops = {
	.probe = tegra210_usb2_pad_probe,
	.remove = tegra210_usb2_pad_remove,
};

static const struct tegra_xusb_pad_soc tegra210_usb2_pad = {
	.name = "usb2",
	.num_lanes = ARRAY_SIZE(tegra210_usb2_lanes),
	.lanes = tegra210_usb2_lanes,
	.ops = &tegra210_usb2_ops,
};

static const char *tegra210_hsic_functions[] = {
	"snps",
	"xusb",
};

static const struct tegra_xusb_lane_soc tegra210_hsic_lanes[] = {
	TEGRA210_LANE("hsic-0", 0x004, 14, 0x1, hsic),
};

static struct tegra_xusb_lane *
tegra210_hsic_lane_probe(struct tegra_xusb_pad *pad, struct device_node *np,
			 unsigned int index)
{
	struct tegra_xusb_hsic_lane *hsic;
	int err;

	hsic = kzalloc(sizeof(*hsic), GFP_KERNEL);
	if (!hsic)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&hsic->base.list);
	hsic->base.soc = &pad->soc->lanes[index];
	hsic->base.index = index;
	hsic->base.pad = pad;
	hsic->base.np = np;

	err = tegra_xusb_lane_parse_dt(&hsic->base, np);
	if (err < 0) {
		kfree(hsic);
		return ERR_PTR(err);
	}

	return &hsic->base;
}

static void tegra210_hsic_lane_remove(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_hsic_lane *hsic = to_hsic_lane(lane);

	kfree(hsic);
}

static const struct tegra_xusb_lane_ops tegra210_hsic_lane_ops = {
	.probe = tegra210_hsic_lane_probe,
	.remove = tegra210_hsic_lane_remove,
	.enable_phy_sleepwalk = tegra210_pmc_hsic_enable_phy_sleepwalk,
	.disable_phy_sleepwalk = tegra210_pmc_hsic_disable_phy_sleepwalk,
	.enable_phy_wake = tegra210_hsic_enable_phy_wake,
	.disable_phy_wake = tegra210_hsic_disable_phy_wake,
	.remote_wake_detected = tegra210_hsic_phy_remote_wake_detected,
};

static int tegra210_hsic_phy_init(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	u32 value;

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_PAD_MUX);
	value &= ~(XUSB_PADCTL_USB2_PAD_MUX_HSIC_PAD_TRK_MASK <<
		   XUSB_PADCTL_USB2_PAD_MUX_HSIC_PAD_TRK_SHIFT);
	value |= XUSB_PADCTL_USB2_PAD_MUX_HSIC_PAD_TRK_XUSB <<
		 XUSB_PADCTL_USB2_PAD_MUX_HSIC_PAD_TRK_SHIFT;
	padctl_writel(padctl, value, XUSB_PADCTL_USB2_PAD_MUX);

	return 0;
}

static int tegra210_hsic_phy_exit(struct phy *phy)
{
	return 0;
}

static int tegra210_hsic_phy_power_on(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_hsic_lane *hsic = to_hsic_lane(lane);
	struct tegra_xusb_hsic_pad *pad = to_hsic_pad(lane->pad);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	unsigned int index = lane->index;
	u32 value;
	int err;

	err = regulator_enable(pad->supply);
	if (err)
		return err;

	padctl_writel(padctl, hsic->strobe_trim,
		      XUSB_PADCTL_HSIC_STRB_TRIM_CONTROL);

	value = padctl_readl(padctl, XUSB_PADCTL_HSIC_PADX_CTL1(index));
	value &= ~(XUSB_PADCTL_HSIC_PAD_CTL1_TX_RTUNEP_MASK <<
		   XUSB_PADCTL_HSIC_PAD_CTL1_TX_RTUNEP_SHIFT);
	value |= (hsic->tx_rtune_p <<
		  XUSB_PADCTL_HSIC_PAD_CTL1_TX_RTUNEP_SHIFT);
	padctl_writel(padctl, value, XUSB_PADCTL_HSIC_PADX_CTL1(index));

	value = padctl_readl(padctl, XUSB_PADCTL_HSIC_PADX_CTL2(index));
	value &= ~((XUSB_PADCTL_HSIC_PAD_CTL2_RX_STROBE_TRIM_MASK <<
		    XUSB_PADCTL_HSIC_PAD_CTL2_RX_STROBE_TRIM_SHIFT) |
		   (XUSB_PADCTL_HSIC_PAD_CTL2_RX_DATA_TRIM_MASK <<
		    XUSB_PADCTL_HSIC_PAD_CTL2_RX_DATA_TRIM_SHIFT));
	value |= (hsic->rx_strobe_trim <<
		  XUSB_PADCTL_HSIC_PAD_CTL2_RX_STROBE_TRIM_SHIFT) |
		 (hsic->rx_data_trim <<
		  XUSB_PADCTL_HSIC_PAD_CTL2_RX_DATA_TRIM_SHIFT);
	padctl_writel(padctl, value, XUSB_PADCTL_HSIC_PADX_CTL2(index));

	value = padctl_readl(padctl, XUSB_PADCTL_HSIC_PADX_CTL0(index));
	value &= ~(XUSB_PADCTL_HSIC_PAD_CTL0_RPU_DATA0 |
		   XUSB_PADCTL_HSIC_PAD_CTL0_RPU_DATA1 |
		   XUSB_PADCTL_HSIC_PAD_CTL0_RPU_STROBE |
		   XUSB_PADCTL_HSIC_PAD_CTL0_PD_RX_DATA0 |
		   XUSB_PADCTL_HSIC_PAD_CTL0_PD_RX_DATA1 |
		   XUSB_PADCTL_HSIC_PAD_CTL0_PD_RX_STROBE |
		   XUSB_PADCTL_HSIC_PAD_CTL0_PD_ZI_DATA0 |
		   XUSB_PADCTL_HSIC_PAD_CTL0_PD_ZI_DATA1 |
		   XUSB_PADCTL_HSIC_PAD_CTL0_PD_ZI_STROBE |
		   XUSB_PADCTL_HSIC_PAD_CTL0_PD_TX_DATA0 |
		   XUSB_PADCTL_HSIC_PAD_CTL0_PD_TX_DATA1 |
		   XUSB_PADCTL_HSIC_PAD_CTL0_PD_TX_STROBE);
	value |= XUSB_PADCTL_HSIC_PAD_CTL0_RPD_DATA0 |
		 XUSB_PADCTL_HSIC_PAD_CTL0_RPD_DATA1 |
		 XUSB_PADCTL_HSIC_PAD_CTL0_RPD_STROBE;
	padctl_writel(padctl, value, XUSB_PADCTL_HSIC_PADX_CTL0(index));

	err = clk_prepare_enable(pad->clk);
	if (err)
		goto disable;

	value = padctl_readl(padctl, XUSB_PADCTL_HSIC_PAD_TRK_CTL);
	value &= ~((XUSB_PADCTL_HSIC_PAD_TRK_CTL_TRK_START_TIMER_MASK <<
		    XUSB_PADCTL_HSIC_PAD_TRK_CTL_TRK_START_TIMER_SHIFT) |
		   (XUSB_PADCTL_HSIC_PAD_TRK_CTL_TRK_DONE_RESET_TIMER_MASK <<
		    XUSB_PADCTL_HSIC_PAD_TRK_CTL_TRK_DONE_RESET_TIMER_SHIFT));
	value |= (XUSB_PADCTL_HSIC_PAD_TRK_CTL_TRK_START_TIMER_VAL <<
		  XUSB_PADCTL_HSIC_PAD_TRK_CTL_TRK_START_TIMER_SHIFT) |
		 (XUSB_PADCTL_HSIC_PAD_TRK_CTL_TRK_DONE_RESET_TIMER_VAL <<
		  XUSB_PADCTL_HSIC_PAD_TRK_CTL_TRK_DONE_RESET_TIMER_SHIFT);
	padctl_writel(padctl, value, XUSB_PADCTL_HSIC_PAD_TRK_CTL);

	udelay(1);

	value = padctl_readl(padctl, XUSB_PADCTL_HSIC_PAD_TRK_CTL);
	value &= ~XUSB_PADCTL_HSIC_PAD_TRK_CTL_PD_TRK;
	padctl_writel(padctl, value, XUSB_PADCTL_HSIC_PAD_TRK_CTL);

	udelay(50);

	clk_disable_unprepare(pad->clk);

	return 0;

disable:
	regulator_disable(pad->supply);
	return err;
}

static int tegra210_hsic_phy_power_off(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_hsic_pad *pad = to_hsic_pad(lane->pad);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	unsigned int index = lane->index;
	u32 value;

	value = padctl_readl(padctl, XUSB_PADCTL_HSIC_PADX_CTL0(index));
	value |= XUSB_PADCTL_HSIC_PAD_CTL0_PD_RX_DATA0 |
		 XUSB_PADCTL_HSIC_PAD_CTL0_PD_RX_DATA1 |
		 XUSB_PADCTL_HSIC_PAD_CTL0_PD_RX_STROBE |
		 XUSB_PADCTL_HSIC_PAD_CTL0_PD_ZI_DATA0 |
		 XUSB_PADCTL_HSIC_PAD_CTL0_PD_ZI_DATA1 |
		 XUSB_PADCTL_HSIC_PAD_CTL0_PD_ZI_STROBE |
		 XUSB_PADCTL_HSIC_PAD_CTL0_PD_TX_DATA0 |
		 XUSB_PADCTL_HSIC_PAD_CTL0_PD_TX_DATA1 |
		 XUSB_PADCTL_HSIC_PAD_CTL0_PD_TX_STROBE;
	padctl_writel(padctl, value, XUSB_PADCTL_HSIC_PADX_CTL1(index));

	regulator_disable(pad->supply);

	return 0;
}

static const struct phy_ops tegra210_hsic_phy_ops = {
	.init = tegra210_hsic_phy_init,
	.exit = tegra210_hsic_phy_exit,
	.power_on = tegra210_hsic_phy_power_on,
	.power_off = tegra210_hsic_phy_power_off,
	.owner = THIS_MODULE,
};

static struct tegra_xusb_pad *
tegra210_hsic_pad_probe(struct tegra_xusb_padctl *padctl,
			const struct tegra_xusb_pad_soc *soc,
			struct device_node *np)
{
	struct tegra_xusb_hsic_pad *hsic;
	struct tegra_xusb_pad *pad;
	int err;

	hsic = kzalloc(sizeof(*hsic), GFP_KERNEL);
	if (!hsic)
		return ERR_PTR(-ENOMEM);

	pad = &hsic->base;
	pad->ops = &tegra210_hsic_lane_ops;
	pad->soc = soc;

	err = tegra_xusb_pad_init(pad, padctl, np);
	if (err < 0) {
		kfree(hsic);
		goto out;
	}

	hsic->clk = devm_clk_get(&pad->dev, "trk");
	if (IS_ERR(hsic->clk)) {
		err = PTR_ERR(hsic->clk);
		dev_err(&pad->dev, "failed to get trk clock: %d\n", err);
		goto unregister;
	}

	err = tegra_xusb_pad_register(pad, &tegra210_hsic_phy_ops);
	if (err < 0)
		goto unregister;

	dev_set_drvdata(&pad->dev, pad);

	return pad;

unregister:
	device_unregister(&pad->dev);
out:
	return ERR_PTR(err);
}

static void tegra210_hsic_pad_remove(struct tegra_xusb_pad *pad)
{
	struct tegra_xusb_hsic_pad *hsic = to_hsic_pad(pad);

	kfree(hsic);
}

static const struct tegra_xusb_pad_ops tegra210_hsic_ops = {
	.probe = tegra210_hsic_pad_probe,
	.remove = tegra210_hsic_pad_remove,
};

static const struct tegra_xusb_pad_soc tegra210_hsic_pad = {
	.name = "hsic",
	.num_lanes = ARRAY_SIZE(tegra210_hsic_lanes),
	.lanes = tegra210_hsic_lanes,
	.ops = &tegra210_hsic_ops,
};

static void tegra210_uphy_lane_iddq_enable(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	u32 value;

	value = padctl_readl(padctl, lane->soc->regs.misc_ctl2);
	value |= XUSB_PADCTL_UPHY_MISC_PAD_CTL2_TX_IDDQ_OVRD;
	value |= XUSB_PADCTL_UPHY_MISC_PAD_CTL2_RX_IDDQ_OVRD;
	value |= XUSB_PADCTL_UPHY_MISC_PAD_CTL2_TX_PWR_OVRD;
	value |= XUSB_PADCTL_UPHY_MISC_PAD_CTL2_RX_PWR_OVRD;
	value |= XUSB_PADCTL_UPHY_MISC_PAD_CTL2_TX_IDDQ;
	value &= ~XUSB_PADCTL_UPHY_MISC_PAD_CTL2_TX_SLEEP_MASK;
	value |= XUSB_PADCTL_UPHY_MISC_PAD_CTL2_TX_SLEEP_VAL;
	value |= XUSB_PADCTL_UPHY_MISC_PAD_CTL2_RX_IDDQ;
	value &= ~XUSB_PADCTL_UPHY_MISC_PAD_CTL2_RX_SLEEP_MASK;
	value |= XUSB_PADCTL_UPHY_MISC_PAD_CTL2_RX_SLEEP_VAL;
	padctl_writel(padctl, value, lane->soc->regs.misc_ctl2);
}

static void tegra210_uphy_lane_iddq_disable(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	u32 value;

	value = padctl_readl(padctl, lane->soc->regs.misc_ctl2);
	value &= ~XUSB_PADCTL_UPHY_MISC_PAD_CTL2_TX_IDDQ_OVRD;
	value &= ~XUSB_PADCTL_UPHY_MISC_PAD_CTL2_RX_IDDQ_OVRD;
	value &= ~XUSB_PADCTL_UPHY_MISC_PAD_CTL2_TX_PWR_OVRD;
	value &= ~XUSB_PADCTL_UPHY_MISC_PAD_CTL2_RX_PWR_OVRD;
	value |= XUSB_PADCTL_UPHY_MISC_PAD_CTL2_TX_IDDQ;
	value &= ~XUSB_PADCTL_UPHY_MISC_PAD_CTL2_TX_SLEEP_MASK;
	value |= XUSB_PADCTL_UPHY_MISC_PAD_CTL2_TX_SLEEP_VAL;
	value |= XUSB_PADCTL_UPHY_MISC_PAD_CTL2_RX_IDDQ;
	value &= ~XUSB_PADCTL_UPHY_MISC_PAD_CTL2_RX_SLEEP_MASK;
	value |= XUSB_PADCTL_UPHY_MISC_PAD_CTL2_RX_SLEEP_VAL;
	padctl_writel(padctl, value, lane->soc->regs.misc_ctl2);
}

#define TEGRA210_UPHY_LANE(_name, _offset, _shift, _mask, _type, _misc)	\
	{								\
		.name = _name,						\
		.offset = _offset,					\
		.shift = _shift,					\
		.mask = _mask,						\
		.num_funcs = ARRAY_SIZE(tegra210_##_type##_functions),	\
		.funcs = tegra210_##_type##_functions,			\
		.regs.misc_ctl2 = _misc,				\
	}

static const char *tegra210_pcie_functions[] = {
	"pcie-x1",
	"usb3-ss",
	"sata",
	"pcie-x4",
};

static const struct tegra_xusb_lane_soc tegra210_pcie_lanes[] = {
	TEGRA210_UPHY_LANE("pcie-0", 0x028, 12, 0x3, pcie, XUSB_PADCTL_UPHY_MISC_PAD_PX_CTL2(0)),
	TEGRA210_UPHY_LANE("pcie-1", 0x028, 14, 0x3, pcie, XUSB_PADCTL_UPHY_MISC_PAD_PX_CTL2(1)),
	TEGRA210_UPHY_LANE("pcie-2", 0x028, 16, 0x3, pcie, XUSB_PADCTL_UPHY_MISC_PAD_PX_CTL2(2)),
	TEGRA210_UPHY_LANE("pcie-3", 0x028, 18, 0x3, pcie, XUSB_PADCTL_UPHY_MISC_PAD_PX_CTL2(3)),
	TEGRA210_UPHY_LANE("pcie-4", 0x028, 20, 0x3, pcie, XUSB_PADCTL_UPHY_MISC_PAD_PX_CTL2(4)),
	TEGRA210_UPHY_LANE("pcie-5", 0x028, 22, 0x3, pcie, XUSB_PADCTL_UPHY_MISC_PAD_PX_CTL2(5)),
	TEGRA210_UPHY_LANE("pcie-6", 0x028, 24, 0x3, pcie, XUSB_PADCTL_UPHY_MISC_PAD_PX_CTL2(6)),
};

static struct tegra_xusb_usb3_port *
tegra210_lane_to_usb3_port(struct tegra_xusb_lane *lane)
{
	int port;

	if (!lane || !lane->pad || !lane->pad->padctl)
		return NULL;

	port = tegra210_usb3_lane_map(lane);
	if (port < 0)
		return NULL;

	return tegra_xusb_find_usb3_port(lane->pad->padctl, port);
}

static int tegra210_usb3_phy_power_on(struct phy *phy)
{
	struct device *dev = &phy->dev;
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	struct tegra_xusb_usb3_port *usb3 = tegra210_lane_to_usb3_port(lane);
	unsigned int index;
	u32 value;

	if (!usb3) {
		dev_err(dev, "no USB3 port found for lane %u\n", lane->index);
		return -ENODEV;
	}

	index = usb3->base.index;

	value = padctl_readl(padctl, XUSB_PADCTL_SS_PORT_MAP);

	if (!usb3->internal)
		value &= ~XUSB_PADCTL_SS_PORT_MAP_PORTX_INTERNAL(index);
	else
		value |= XUSB_PADCTL_SS_PORT_MAP_PORTX_INTERNAL(index);

	value &= ~XUSB_PADCTL_SS_PORT_MAP_PORTX_MAP_MASK(index);
	value |= XUSB_PADCTL_SS_PORT_MAP_PORTX_MAP(index, usb3->port);
	padctl_writel(padctl, value, XUSB_PADCTL_SS_PORT_MAP);

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_USB3_PADX_ECTL1(index));
	value &= ~(XUSB_PADCTL_UPHY_USB3_PAD_ECTL1_TX_TERM_CTRL_MASK <<
		   XUSB_PADCTL_UPHY_USB3_PAD_ECTL1_TX_TERM_CTRL_SHIFT);
	value |= XUSB_PADCTL_UPHY_USB3_PAD_ECTL1_TX_TERM_CTRL_VAL <<
		 XUSB_PADCTL_UPHY_USB3_PAD_ECTL1_TX_TERM_CTRL_SHIFT;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_USB3_PADX_ECTL1(index));

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_USB3_PADX_ECTL2(index));
	value &= ~(XUSB_PADCTL_UPHY_USB3_PAD_ECTL2_RX_CTLE_MASK <<
		   XUSB_PADCTL_UPHY_USB3_PAD_ECTL2_RX_CTLE_SHIFT);
	value |= XUSB_PADCTL_UPHY_USB3_PAD_ECTL2_RX_CTLE_VAL <<
		 XUSB_PADCTL_UPHY_USB3_PAD_ECTL2_RX_CTLE_SHIFT;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_USB3_PADX_ECTL2(index));

	padctl_writel(padctl, XUSB_PADCTL_UPHY_USB3_PAD_ECTL3_RX_DFE_VAL,
		      XUSB_PADCTL_UPHY_USB3_PADX_ECTL3(index));

	value = padctl_readl(padctl, XUSB_PADCTL_UPHY_USB3_PADX_ECTL4(index));
	value &= ~(XUSB_PADCTL_UPHY_USB3_PAD_ECTL4_RX_CDR_CTRL_MASK <<
		   XUSB_PADCTL_UPHY_USB3_PAD_ECTL4_RX_CDR_CTRL_SHIFT);
	value |= XUSB_PADCTL_UPHY_USB3_PAD_ECTL4_RX_CDR_CTRL_VAL <<
		 XUSB_PADCTL_UPHY_USB3_PAD_ECTL4_RX_CDR_CTRL_SHIFT;
	padctl_writel(padctl, value, XUSB_PADCTL_UPHY_USB3_PADX_ECTL4(index));

	padctl_writel(padctl, XUSB_PADCTL_UPHY_USB3_PAD_ECTL6_RX_EQ_CTRL_H_VAL,
		      XUSB_PADCTL_UPHY_USB3_PADX_ECTL6(index));

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM1);
	value &= ~XUSB_PADCTL_ELPG_PROGRAM1_SSPX_ELPG_VCORE_DOWN(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM1);

	usleep_range(100, 200);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM1);
	value &= ~XUSB_PADCTL_ELPG_PROGRAM1_SSPX_ELPG_CLAMP_EN_EARLY(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM1);

	usleep_range(100, 200);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM1);
	value &= ~XUSB_PADCTL_ELPG_PROGRAM1_SSPX_ELPG_CLAMP_EN(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM1);

	return 0;
}

static int tegra210_usb3_phy_power_off(struct phy *phy)
{
	struct device *dev = &phy->dev;
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	struct tegra_xusb_usb3_port *usb3 = tegra210_lane_to_usb3_port(lane);
	unsigned int index;
	u32 value;

	if (!usb3) {
		dev_err(dev, "no USB3 port found for lane %u\n", lane->index);
		return -ENODEV;
	}

	index = usb3->base.index;

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM1);
	value |= XUSB_PADCTL_ELPG_PROGRAM1_SSPX_ELPG_CLAMP_EN_EARLY(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM1);

	usleep_range(100, 200);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM1);
	value |= XUSB_PADCTL_ELPG_PROGRAM1_SSPX_ELPG_CLAMP_EN(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM1);

	usleep_range(250, 350);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM1);
	value |= XUSB_PADCTL_ELPG_PROGRAM1_SSPX_ELPG_VCORE_DOWN(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM1);

	return 0;
}
static struct tegra_xusb_lane *
tegra210_pcie_lane_probe(struct tegra_xusb_pad *pad, struct device_node *np,
			 unsigned int index)
{
	struct tegra_xusb_pcie_lane *pcie;
	int err;

	pcie = kzalloc(sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&pcie->base.list);
	pcie->base.soc = &pad->soc->lanes[index];
	pcie->base.index = index;
	pcie->base.pad = pad;
	pcie->base.np = np;

	err = tegra_xusb_lane_parse_dt(&pcie->base, np);
	if (err < 0) {
		kfree(pcie);
		return ERR_PTR(err);
	}

	return &pcie->base;
}

static void tegra210_pcie_lane_remove(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_pcie_lane *pcie = to_pcie_lane(lane);

	kfree(pcie);
}

static const struct tegra_xusb_lane_ops tegra210_pcie_lane_ops = {
	.probe = tegra210_pcie_lane_probe,
	.remove = tegra210_pcie_lane_remove,
	.iddq_enable = tegra210_uphy_lane_iddq_enable,
	.iddq_disable = tegra210_uphy_lane_iddq_disable,
	.enable_phy_sleepwalk = tegra210_usb3_enable_phy_sleepwalk,
	.disable_phy_sleepwalk = tegra210_usb3_disable_phy_sleepwalk,
	.enable_phy_wake = tegra210_usb3_enable_phy_wake,
	.disable_phy_wake = tegra210_usb3_disable_phy_wake,
	.remote_wake_detected = tegra210_usb3_phy_remote_wake_detected,
};

static int tegra210_pcie_phy_init(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;

	mutex_lock(&padctl->lock);

	tegra210_uphy_init(padctl);

	mutex_unlock(&padctl->lock);

	return 0;
}

static int tegra210_pcie_phy_power_on(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	int err = 0;

	mutex_lock(&padctl->lock);

	if (tegra_xusb_lane_check(lane, "usb3-ss"))
		err = tegra210_usb3_phy_power_on(phy);

	mutex_unlock(&padctl->lock);
	return err;
}

static int tegra210_pcie_phy_power_off(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	int err = 0;

	mutex_lock(&padctl->lock);

	if (tegra_xusb_lane_check(lane, "usb3-ss"))
		err = tegra210_usb3_phy_power_off(phy);

	mutex_unlock(&padctl->lock);
	return err;
}

static const struct phy_ops tegra210_pcie_phy_ops = {
	.init = tegra210_pcie_phy_init,
	.power_on = tegra210_pcie_phy_power_on,
	.power_off = tegra210_pcie_phy_power_off,
	.owner = THIS_MODULE,
};

static struct tegra_xusb_pad *
tegra210_pcie_pad_probe(struct tegra_xusb_padctl *padctl,
			const struct tegra_xusb_pad_soc *soc,
			struct device_node *np)
{
	struct tegra_xusb_pcie_pad *pcie;
	struct tegra_xusb_pad *pad;
	int err;

	pcie = kzalloc(sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return ERR_PTR(-ENOMEM);

	pad = &pcie->base;
	pad->ops = &tegra210_pcie_lane_ops;
	pad->soc = soc;

	err = tegra_xusb_pad_init(pad, padctl, np);
	if (err < 0) {
		kfree(pcie);
		goto out;
	}

	pcie->pll = devm_clk_get(&pad->dev, "pll");
	if (IS_ERR(pcie->pll)) {
		err = PTR_ERR(pcie->pll);
		dev_err(&pad->dev, "failed to get PLL: %d\n", err);
		goto unregister;
	}

	pcie->rst = devm_reset_control_get(&pad->dev, "phy");
	if (IS_ERR(pcie->rst)) {
		err = PTR_ERR(pcie->rst);
		dev_err(&pad->dev, "failed to get PCIe pad reset: %d\n", err);
		goto unregister;
	}

	err = tegra_xusb_pad_register(pad, &tegra210_pcie_phy_ops);
	if (err < 0)
		goto unregister;

	dev_set_drvdata(&pad->dev, pad);

	return pad;

unregister:
	device_unregister(&pad->dev);
out:
	return ERR_PTR(err);
}

static void tegra210_pcie_pad_remove(struct tegra_xusb_pad *pad)
{
	struct tegra_xusb_pcie_pad *pcie = to_pcie_pad(pad);

	kfree(pcie);
}

static const struct tegra_xusb_pad_ops tegra210_pcie_ops = {
	.probe = tegra210_pcie_pad_probe,
	.remove = tegra210_pcie_pad_remove,
};

static const struct tegra_xusb_pad_soc tegra210_pcie_pad = {
	.name = "pcie",
	.num_lanes = ARRAY_SIZE(tegra210_pcie_lanes),
	.lanes = tegra210_pcie_lanes,
	.ops = &tegra210_pcie_ops,
};

static const struct tegra_xusb_lane_soc tegra210_sata_lanes[] = {
	TEGRA210_UPHY_LANE("sata-0", 0x028, 30, 0x3, pcie, XUSB_PADCTL_UPHY_MISC_PAD_S0_CTL2),
};

static struct tegra_xusb_lane *
tegra210_sata_lane_probe(struct tegra_xusb_pad *pad, struct device_node *np,
			 unsigned int index)
{
	struct tegra_xusb_sata_lane *sata;
	int err;

	sata = kzalloc(sizeof(*sata), GFP_KERNEL);
	if (!sata)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&sata->base.list);
	sata->base.soc = &pad->soc->lanes[index];
	sata->base.index = index;
	sata->base.pad = pad;
	sata->base.np = np;

	err = tegra_xusb_lane_parse_dt(&sata->base, np);
	if (err < 0) {
		kfree(sata);
		return ERR_PTR(err);
	}

	return &sata->base;
}

static void tegra210_sata_lane_remove(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_sata_lane *sata = to_sata_lane(lane);

	kfree(sata);
}

static const struct tegra_xusb_lane_ops tegra210_sata_lane_ops = {
	.probe = tegra210_sata_lane_probe,
	.remove = tegra210_sata_lane_remove,
	.iddq_enable = tegra210_uphy_lane_iddq_enable,
	.iddq_disable = tegra210_uphy_lane_iddq_disable,
	.enable_phy_sleepwalk = tegra210_usb3_enable_phy_sleepwalk,
	.disable_phy_sleepwalk = tegra210_usb3_disable_phy_sleepwalk,
	.enable_phy_wake = tegra210_usb3_enable_phy_wake,
	.disable_phy_wake = tegra210_usb3_disable_phy_wake,
	.remote_wake_detected = tegra210_usb3_phy_remote_wake_detected,
};

static int tegra210_sata_phy_init(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;

	mutex_lock(&padctl->lock);

	tegra210_uphy_init(padctl);

	mutex_unlock(&padctl->lock);
	return 0;
}

static int tegra210_sata_phy_power_on(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	int err = 0;

	mutex_lock(&padctl->lock);

	if (tegra_xusb_lane_check(lane, "usb3-ss"))
		err = tegra210_usb3_phy_power_on(phy);

	mutex_unlock(&padctl->lock);
	return err;
}

static int tegra210_sata_phy_power_off(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	int err = 0;

	mutex_lock(&padctl->lock);

	if (tegra_xusb_lane_check(lane, "usb3-ss"))
		err = tegra210_usb3_phy_power_off(phy);

	mutex_unlock(&padctl->lock);
	return err;
}

static const struct phy_ops tegra210_sata_phy_ops = {
	.init = tegra210_sata_phy_init,
	.power_on = tegra210_sata_phy_power_on,
	.power_off = tegra210_sata_phy_power_off,
	.owner = THIS_MODULE,
};

static struct tegra_xusb_pad *
tegra210_sata_pad_probe(struct tegra_xusb_padctl *padctl,
			const struct tegra_xusb_pad_soc *soc,
			struct device_node *np)
{
	struct tegra_xusb_sata_pad *sata;
	struct tegra_xusb_pad *pad;
	int err;

	sata = kzalloc(sizeof(*sata), GFP_KERNEL);
	if (!sata)
		return ERR_PTR(-ENOMEM);

	pad = &sata->base;
	pad->ops = &tegra210_sata_lane_ops;
	pad->soc = soc;

	err = tegra_xusb_pad_init(pad, padctl, np);
	if (err < 0) {
		kfree(sata);
		goto out;
	}

	sata->rst = devm_reset_control_get(&pad->dev, "phy");
	if (IS_ERR(sata->rst)) {
		err = PTR_ERR(sata->rst);
		dev_err(&pad->dev, "failed to get SATA pad reset: %d\n", err);
		goto unregister;
	}

	err = tegra_xusb_pad_register(pad, &tegra210_sata_phy_ops);
	if (err < 0)
		goto unregister;

	dev_set_drvdata(&pad->dev, pad);

	return pad;

unregister:
	device_unregister(&pad->dev);
out:
	return ERR_PTR(err);
}

static void tegra210_sata_pad_remove(struct tegra_xusb_pad *pad)
{
	struct tegra_xusb_sata_pad *sata = to_sata_pad(pad);

	kfree(sata);
}

static const struct tegra_xusb_pad_ops tegra210_sata_ops = {
	.probe = tegra210_sata_pad_probe,
	.remove = tegra210_sata_pad_remove,
};

static const struct tegra_xusb_pad_soc tegra210_sata_pad = {
	.name = "sata",
	.num_lanes = ARRAY_SIZE(tegra210_sata_lanes),
	.lanes = tegra210_sata_lanes,
	.ops = &tegra210_sata_ops,
};

static const struct tegra_xusb_pad_soc * const tegra210_pads[] = {
	&tegra210_usb2_pad,
	&tegra210_hsic_pad,
	&tegra210_pcie_pad,
	&tegra210_sata_pad,
};

static int tegra210_usb2_port_enable(struct tegra_xusb_port *port)
{
	return 0;
}

static void tegra210_usb2_port_disable(struct tegra_xusb_port *port)
{
}

static struct tegra_xusb_lane *
tegra210_usb2_port_map(struct tegra_xusb_port *port)
{
	return tegra_xusb_find_lane(port->padctl, "usb2", port->index);
}

static const struct tegra_xusb_port_ops tegra210_usb2_port_ops = {
	.release = tegra_xusb_usb2_port_release,
	.remove = tegra_xusb_usb2_port_remove,
	.enable = tegra210_usb2_port_enable,
	.disable = tegra210_usb2_port_disable,
	.map = tegra210_usb2_port_map,
};

static int tegra210_hsic_port_enable(struct tegra_xusb_port *port)
{
	return 0;
}

static void tegra210_hsic_port_disable(struct tegra_xusb_port *port)
{
}

static struct tegra_xusb_lane *
tegra210_hsic_port_map(struct tegra_xusb_port *port)
{
	return tegra_xusb_find_lane(port->padctl, "hsic", port->index);
}

static const struct tegra_xusb_port_ops tegra210_hsic_port_ops = {
	.release = tegra_xusb_hsic_port_release,
	.enable = tegra210_hsic_port_enable,
	.disable = tegra210_hsic_port_disable,
	.map = tegra210_hsic_port_map,
};

static int tegra210_usb3_port_enable(struct tegra_xusb_port *port)
{
	return 0;
}

static void tegra210_usb3_port_disable(struct tegra_xusb_port *port)
{
}

static struct tegra_xusb_lane *
tegra210_usb3_port_map(struct tegra_xusb_port *port)
{
	return tegra_xusb_port_find_lane(port, tegra210_usb3_map, "usb3-ss");
}

static const struct tegra_xusb_port_ops tegra210_usb3_port_ops = {
	.release = tegra_xusb_usb3_port_release,
	.remove = tegra_xusb_usb3_port_remove,
	.enable = tegra210_usb3_port_enable,
	.disable = tegra210_usb3_port_disable,
	.map = tegra210_usb3_port_map,
};

static int tegra210_utmi_port_reset(struct phy *phy)
{
	struct tegra_xusb_padctl *padctl;
	struct tegra_xusb_lane *lane;
	u32 value;

	lane = phy_get_drvdata(phy);
	padctl = lane->pad->padctl;

	value = padctl_readl(padctl,
		     XUSB_PADCTL_USB2_BATTERY_CHRG_OTGPADX_CTL0(lane->index));

	if ((value & XUSB_PADCTL_USB2_BATTERY_CHRG_OTGPAD_CTL0_ZIP) ||
	    (value & XUSB_PADCTL_USB2_BATTERY_CHRG_OTGPAD_CTL0_ZIN)) {
		tegra210_xusb_padctl_vbus_override(padctl, false);
		tegra210_xusb_padctl_vbus_override(padctl, true);
		return 1;
	}

	return 0;
}

static int
tegra210_xusb_read_fuse_calibration(struct tegra210_xusb_fuse_calibration *fuse)
{
	unsigned int i;
	u32 value;
	int err;

	err = tegra_fuse_readl(TEGRA_FUSE_SKU_CALIB_0, &value);
	if (err < 0)
		return err;

	for (i = 0; i < ARRAY_SIZE(fuse->hs_curr_level); i++) {
		fuse->hs_curr_level[i] =
			(value >> FUSE_SKU_CALIB_HS_CURR_LEVEL_PADX_SHIFT(i)) &
			FUSE_SKU_CALIB_HS_CURR_LEVEL_PAD_MASK;
	}

	fuse->hs_term_range_adj =
		(value >> FUSE_SKU_CALIB_HS_TERM_RANGE_ADJ_SHIFT) &
		FUSE_SKU_CALIB_HS_TERM_RANGE_ADJ_MASK;

	err = tegra_fuse_readl(TEGRA_FUSE_USB_CALIB_EXT_0, &value);
	if (err < 0)
		return err;

	fuse->rpd_ctrl =
		(value >> FUSE_USB_CALIB_EXT_RPD_CTRL_SHIFT) &
		FUSE_USB_CALIB_EXT_RPD_CTRL_MASK;

	return 0;
}

static struct tegra_xusb_padctl *
tegra210_xusb_padctl_probe(struct device *dev,
			   const struct tegra_xusb_padctl_soc *soc)
{
	struct tegra210_xusb_padctl *padctl;
	struct platform_device *pdev;
	struct device_node *np;
	int err;

	padctl = devm_kzalloc(dev, sizeof(*padctl), GFP_KERNEL);
	if (!padctl)
		return ERR_PTR(-ENOMEM);

	padctl->base.dev = dev;
	padctl->base.soc = soc;

	err = tegra210_xusb_read_fuse_calibration(&padctl->fuse);
	if (err < 0)
		return ERR_PTR(err);

	np = of_parse_phandle(dev->of_node, "nvidia,pmc", 0);
	if (!np) {
		dev_warn(dev, "nvidia,pmc property is missing\n");
		goto out;
	}

	pdev = of_find_device_by_node(np);
	if (!pdev) {
		dev_warn(dev, "PMC device is not available\n");
		goto out;
	}

	if (!platform_get_drvdata(pdev))
		return ERR_PTR(-EPROBE_DEFER);

	padctl->regmap = dev_get_regmap(&pdev->dev, "usb_sleepwalk");
	if (!padctl->regmap)
		dev_info(dev, "failed to find PMC regmap\n");

out:
	return &padctl->base;
}

static void tegra210_xusb_padctl_remove(struct tegra_xusb_padctl *padctl)
{
}

static void tegra210_xusb_padctl_save(struct tegra_xusb_padctl *padctl)
{
	struct tegra210_xusb_padctl *priv = to_tegra210_xusb_padctl(padctl);

	priv->context.usb2_pad_mux =
		padctl_readl(padctl, XUSB_PADCTL_USB2_PAD_MUX);
	priv->context.usb2_port_cap =
		padctl_readl(padctl, XUSB_PADCTL_USB2_PORT_CAP);
	priv->context.ss_port_map =
		padctl_readl(padctl, XUSB_PADCTL_SS_PORT_MAP);
	priv->context.usb3_pad_mux =
		padctl_readl(padctl, XUSB_PADCTL_USB3_PAD_MUX);
}

static void tegra210_xusb_padctl_restore(struct tegra_xusb_padctl *padctl)
{
	struct tegra210_xusb_padctl *priv = to_tegra210_xusb_padctl(padctl);
	struct tegra_xusb_lane *lane;

	padctl_writel(padctl, priv->context.usb2_pad_mux,
		XUSB_PADCTL_USB2_PAD_MUX);
	padctl_writel(padctl, priv->context.usb2_port_cap,
		XUSB_PADCTL_USB2_PORT_CAP);
	padctl_writel(padctl, priv->context.ss_port_map,
		XUSB_PADCTL_SS_PORT_MAP);

	list_for_each_entry(lane, &padctl->lanes, list) {
		if (lane->pad->ops->iddq_enable)
			tegra210_uphy_lane_iddq_enable(lane);
	}

	padctl_writel(padctl, priv->context.usb3_pad_mux,
		XUSB_PADCTL_USB3_PAD_MUX);

	list_for_each_entry(lane, &padctl->lanes, list) {
		if (lane->pad->ops->iddq_disable)
			tegra210_uphy_lane_iddq_disable(lane);
	}
}

static int tegra210_xusb_padctl_suspend_noirq(struct tegra_xusb_padctl *padctl)
{
	mutex_lock(&padctl->lock);

	tegra210_uphy_deinit(padctl);

	tegra210_xusb_padctl_save(padctl);

	mutex_unlock(&padctl->lock);
	return 0;
}

static int tegra210_xusb_padctl_resume_noirq(struct tegra_xusb_padctl *padctl)
{
	mutex_lock(&padctl->lock);

	tegra210_xusb_padctl_restore(padctl);

	tegra210_uphy_init(padctl);

	mutex_unlock(&padctl->lock);
	return 0;
}

static const struct tegra_xusb_padctl_ops tegra210_xusb_padctl_ops = {
	.probe = tegra210_xusb_padctl_probe,
	.remove = tegra210_xusb_padctl_remove,
	.suspend_noirq = tegra210_xusb_padctl_suspend_noirq,
	.resume_noirq = tegra210_xusb_padctl_resume_noirq,
	.usb3_set_lfps_detect = tegra210_usb3_set_lfps_detect,
	.hsic_set_idle = tegra210_hsic_set_idle,
	.vbus_override = tegra210_xusb_padctl_vbus_override,
	.utmi_port_reset = tegra210_utmi_port_reset,
};

static const char * const tegra210_xusb_padctl_supply_names[] = {
	"avdd-pll-utmip",
	"avdd-pll-uerefe",
	"dvdd-pex-pll",
	"hvdd-pex-pll-e",
};

const struct tegra_xusb_padctl_soc tegra210_xusb_padctl_soc = {
	.num_pads = ARRAY_SIZE(tegra210_pads),
	.pads = tegra210_pads,
	.ports = {
		.usb2 = {
			.ops = &tegra210_usb2_port_ops,
			.count = 4,
		},
		.hsic = {
			.ops = &tegra210_hsic_port_ops,
			.count = 1,
		},
		.usb3 = {
			.ops = &tegra210_usb3_port_ops,
			.count = 4,
		},
	},
	.ops = &tegra210_xusb_padctl_ops,
	.supply_names = tegra210_xusb_padctl_supply_names,
	.num_supplies = ARRAY_SIZE(tegra210_xusb_padctl_supply_names),
	.need_fake_usb3_port = true,
};
EXPORT_SYMBOL_GPL(tegra210_xusb_padctl_soc);

MODULE_AUTHOR("Andrew Bresticker <abrestic@chromium.org>");
MODULE_DESCRIPTION("NVIDIA Tegra 210 XUSB Pad Controller driver");
MODULE_LICENSE("GPL v2");
