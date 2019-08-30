// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014, NVIDIA CORPORATION.  All rights reserved.
 * Copyright (C) 2015 Google, Inc.
 */

#include <linux/clk.h>
#include <linux/clk/tegra.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
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
#define XUSB_PADCTL_USB2_PORT_CAP_PORTX_CAP_HOST(x) (0x1 << ((x) * 4))
#define XUSB_PADCTL_USB2_PORT_CAP_PORTX_CAP_MASK(x) (0x3 << ((x) * 4))

#define XUSB_PADCTL_SS_PORT_MAP 0x014
#define XUSB_PADCTL_SS_PORT_MAP_PORTX_INTERNAL(x) (1 << (((x) * 5) + 4))
#define XUSB_PADCTL_SS_PORT_MAP_PORTX_MAP_SHIFT(x) ((x) * 5)
#define XUSB_PADCTL_SS_PORT_MAP_PORTX_MAP_MASK(x) (0x7 << ((x) * 5))
#define XUSB_PADCTL_SS_PORT_MAP_PORTX_MAP(x, v) (((v) & 0x7) << ((x) * 5))

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

#define XUSB_PADCTL_USB2_BATTERY_CHRG_OTGPADX_CTL1(x) (0x084 + (x) * 0x40)
#define XUSB_PADCTL_USB2_BATTERY_CHRG_OTGPAD_CTL1_VREG_LEV_SHIFT 7
#define XUSB_PADCTL_USB2_BATTERY_CHRG_OTGPAD_CTL1_VREG_LEV_MASK 0x3
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

#define XUSB_PADCTL_UPHY_PLL_S0_CTL1 0x860

#define XUSB_PADCTL_UPHY_PLL_S0_CTL2 0x864

#define XUSB_PADCTL_UPHY_PLL_S0_CTL4 0x86c

#define XUSB_PADCTL_UPHY_PLL_S0_CTL5 0x870

#define XUSB_PADCTL_UPHY_PLL_S0_CTL8 0x87c

#define XUSB_PADCTL_UPHY_MISC_PAD_S0_CTL1 0x960

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

struct tegra210_xusb_fuse_calibration {
	u32 hs_curr_level[4];
	u32 hs_term_range_adj;
	u32 rpd_ctrl;
};

struct tegra210_xusb_padctl {
	struct tegra_xusb_padctl base;

	struct tegra210_xusb_fuse_calibration fuse;
};

static inline struct tegra210_xusb_padctl *
to_tegra210_xusb_padctl(struct tegra_xusb_padctl *padctl)
{
	return container_of(padctl, struct tegra210_xusb_padctl, base);
}

/* must be called under padctl->lock */
static int tegra210_pex_uphy_enable(struct tegra_xusb_padctl *padctl)
{
	struct tegra_xusb_pcie_pad *pcie = to_pcie_pad(padctl->pcie);
	unsigned long timeout;
	u32 value;
	int err;

	if (pcie->enable > 0) {
		pcie->enable++;
		return 0;
	}

	err = clk_prepare_enable(pcie->pll);
	if (err < 0)
		return err;

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

	pcie->enable++;

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

	mutex_lock(&padctl->lock);

	if (WARN_ON(pcie->enable == 0))
		goto unlock;

	if (--pcie->enable > 0)
		goto unlock;

	reset_control_assert(pcie->rst);
	clk_disable_unprepare(pcie->pll);

unlock:
	mutex_unlock(&padctl->lock);
}

/* must be called under padctl->lock */
static int tegra210_sata_uphy_enable(struct tegra_xusb_padctl *padctl, bool usb)
{
	struct tegra_xusb_sata_pad *sata = to_sata_pad(padctl->sata);
	unsigned long timeout;
	u32 value;
	int err;

	if (sata->enable > 0) {
		sata->enable++;
		return 0;
	}

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

	sata->enable++;

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

	mutex_lock(&padctl->lock);

	if (WARN_ON(sata->enable == 0))
		goto unlock;

	if (--sata->enable > 0)
		goto unlock;

	reset_control_assert(sata->rst);
	clk_disable_unprepare(sata->pll);

unlock:
	mutex_unlock(&padctl->lock);
}

static int tegra210_xusb_padctl_enable(struct tegra_xusb_padctl *padctl)
{
	u32 value;

	mutex_lock(&padctl->lock);

	if (padctl->enable++ > 0)
		goto out;

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

out:
	mutex_unlock(&padctl->lock);
	return 0;
}

static int tegra210_xusb_padctl_disable(struct tegra_xusb_padctl *padctl)
{
	u32 value;

	mutex_lock(&padctl->lock);

	if (WARN_ON(padctl->enable == 0))
		goto out;

	if (--padctl->enable > 0)
		goto out;

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

out:
	mutex_unlock(&padctl->lock);
	return 0;
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
};

static int tegra210_usb2_phy_init(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	u32 value;

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_PAD_MUX);
	value &= ~(XUSB_PADCTL_USB2_PAD_MUX_USB2_BIAS_PAD_MASK <<
		   XUSB_PADCTL_USB2_PAD_MUX_USB2_BIAS_PAD_SHIFT);
	value |= XUSB_PADCTL_USB2_PAD_MUX_USB2_BIAS_PAD_XUSB <<
		 XUSB_PADCTL_USB2_PAD_MUX_USB2_BIAS_PAD_SHIFT;
	padctl_writel(padctl, value, XUSB_PADCTL_USB2_PAD_MUX);

	return tegra210_xusb_padctl_enable(padctl);
}

static int tegra210_usb2_phy_exit(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);

	return tegra210_xusb_padctl_disable(lane->pad->padctl);
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
	value |= XUSB_PADCTL_USB2_PORT_CAP_PORTX_CAP_HOST(index);
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
	value |= XUSB_PADCTL_USB2_BATTERY_CHRG_OTGPAD_CTL1_VREG_FIX18;
	padctl_writel(padctl, value,
		      XUSB_PADCTL_USB2_BATTERY_CHRG_OTGPADX_CTL1(index));

	err = regulator_enable(port->supply);
	if (err)
		return err;

	mutex_lock(&padctl->lock);

	if (pad->enable > 0) {
		pad->enable++;
		mutex_unlock(&padctl->lock);
		return 0;
	}

	err = clk_prepare_enable(pad->clk);
	if (err)
		goto disable_regulator;

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

disable_regulator:
	regulator_disable(port->supply);
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

	if (WARN_ON(pad->enable == 0))
		goto out;

	if (--pad->enable > 0)
		goto out;

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_BIAS_PAD_CTL0);
	value |= XUSB_PADCTL_USB2_BIAS_PAD_CTL0_PD;
	padctl_writel(padctl, value, XUSB_PADCTL_USB2_BIAS_PAD_CTL0);

out:
	regulator_disable(port->supply);
	mutex_unlock(&padctl->lock);
	return 0;
}

static const struct phy_ops tegra210_usb2_phy_ops = {
	.init = tegra210_usb2_phy_init,
	.exit = tegra210_usb2_phy_exit,
	.power_on = tegra210_usb2_phy_power_on,
	.power_off = tegra210_usb2_phy_power_off,
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

	return tegra210_xusb_padctl_enable(padctl);
}

static int tegra210_hsic_phy_exit(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);

	return tegra210_xusb_padctl_disable(lane->pad->padctl);
}

static int tegra210_hsic_phy_power_on(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_hsic_lane *hsic = to_hsic_lane(lane);
	struct tegra_xusb_hsic_pad *pad = to_hsic_pad(lane->pad);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	struct tegra210_xusb_padctl *priv;
	unsigned int index = lane->index;
	u32 value;
	int err;

	priv = to_tegra210_xusb_padctl(padctl);

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

static const char *tegra210_pcie_functions[] = {
	"pcie-x1",
	"usb3-ss",
	"sata",
	"pcie-x4",
};

static const struct tegra_xusb_lane_soc tegra210_pcie_lanes[] = {
	TEGRA210_LANE("pcie-0", 0x028, 12, 0x3, pcie),
	TEGRA210_LANE("pcie-1", 0x028, 14, 0x3, pcie),
	TEGRA210_LANE("pcie-2", 0x028, 16, 0x3, pcie),
	TEGRA210_LANE("pcie-3", 0x028, 18, 0x3, pcie),
	TEGRA210_LANE("pcie-4", 0x028, 20, 0x3, pcie),
	TEGRA210_LANE("pcie-5", 0x028, 22, 0x3, pcie),
	TEGRA210_LANE("pcie-6", 0x028, 24, 0x3, pcie),
};

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
};

static int tegra210_pcie_phy_init(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);

	return tegra210_xusb_padctl_enable(lane->pad->padctl);
}

static int tegra210_pcie_phy_exit(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);

	return tegra210_xusb_padctl_disable(lane->pad->padctl);
}

static int tegra210_pcie_phy_power_on(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	u32 value;
	int err;

	mutex_lock(&padctl->lock);

	err = tegra210_pex_uphy_enable(padctl);
	if (err < 0)
		goto unlock;

	value = padctl_readl(padctl, XUSB_PADCTL_USB3_PAD_MUX);
	value |= XUSB_PADCTL_USB3_PAD_MUX_PCIE_IDDQ_DISABLE(lane->index);
	padctl_writel(padctl, value, XUSB_PADCTL_USB3_PAD_MUX);

unlock:
	mutex_unlock(&padctl->lock);
	return err;
}

static int tegra210_pcie_phy_power_off(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	u32 value;

	value = padctl_readl(padctl, XUSB_PADCTL_USB3_PAD_MUX);
	value &= ~XUSB_PADCTL_USB3_PAD_MUX_PCIE_IDDQ_DISABLE(lane->index);
	padctl_writel(padctl, value, XUSB_PADCTL_USB3_PAD_MUX);

	tegra210_pex_uphy_disable(padctl);

	return 0;
}

static const struct phy_ops tegra210_pcie_phy_ops = {
	.init = tegra210_pcie_phy_init,
	.exit = tegra210_pcie_phy_exit,
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
	TEGRA210_LANE("sata-0", 0x028, 30, 0x3, pcie),
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
};

static int tegra210_sata_phy_init(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);

	return tegra210_xusb_padctl_enable(lane->pad->padctl);
}

static int tegra210_sata_phy_exit(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);

	return tegra210_xusb_padctl_disable(lane->pad->padctl);
}

static int tegra210_sata_phy_power_on(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	u32 value;
	int err;

	mutex_lock(&padctl->lock);

	err = tegra210_sata_uphy_enable(padctl, false);
	if (err < 0)
		goto unlock;

	value = padctl_readl(padctl, XUSB_PADCTL_USB3_PAD_MUX);
	value |= XUSB_PADCTL_USB3_PAD_MUX_SATA_IDDQ_DISABLE(lane->index);
	padctl_writel(padctl, value, XUSB_PADCTL_USB3_PAD_MUX);

unlock:
	mutex_unlock(&padctl->lock);
	return err;
}

static int tegra210_sata_phy_power_off(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	u32 value;

	value = padctl_readl(padctl, XUSB_PADCTL_USB3_PAD_MUX);
	value &= ~XUSB_PADCTL_USB3_PAD_MUX_SATA_IDDQ_DISABLE(lane->index);
	padctl_writel(padctl, value, XUSB_PADCTL_USB3_PAD_MUX);

	tegra210_sata_uphy_disable(lane->pad->padctl);

	return 0;
}

static const struct phy_ops tegra210_sata_phy_ops = {
	.init = tegra210_sata_phy_init,
	.exit = tegra210_sata_phy_exit,
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
	.enable = tegra210_hsic_port_enable,
	.disable = tegra210_hsic_port_disable,
	.map = tegra210_hsic_port_map,
};

static int tegra210_usb3_port_enable(struct tegra_xusb_port *port)
{
	struct tegra_xusb_usb3_port *usb3 = to_usb3_port(port);
	struct tegra_xusb_padctl *padctl = port->padctl;
	struct tegra_xusb_lane *lane = usb3->base.lane;
	unsigned int index = port->index;
	u32 value;
	int err;

	value = padctl_readl(padctl, XUSB_PADCTL_SS_PORT_MAP);

	if (!usb3->internal)
		value &= ~XUSB_PADCTL_SS_PORT_MAP_PORTX_INTERNAL(index);
	else
		value |= XUSB_PADCTL_SS_PORT_MAP_PORTX_INTERNAL(index);

	value &= ~XUSB_PADCTL_SS_PORT_MAP_PORTX_MAP_MASK(index);
	value |= XUSB_PADCTL_SS_PORT_MAP_PORTX_MAP(index, usb3->port);
	padctl_writel(padctl, value, XUSB_PADCTL_SS_PORT_MAP);

	/*
	 * TODO: move this code into the PCIe/SATA PHY ->power_on() callbacks
	 * and conditionalize based on mux function? This seems to work, but
	 * might not be the exact proper sequence.
	 */
	err = regulator_enable(usb3->supply);
	if (err < 0)
		return err;

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

	if (lane->pad == padctl->sata)
		err = tegra210_sata_uphy_enable(padctl, true);
	else
		err = tegra210_pex_uphy_enable(padctl);

	if (err) {
		dev_err(&port->dev, "%s: failed to enable UPHY: %d\n",
			__func__, err);
		return err;
	}

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

static void tegra210_usb3_port_disable(struct tegra_xusb_port *port)
{
	struct tegra_xusb_usb3_port *usb3 = to_usb3_port(port);
	struct tegra_xusb_padctl *padctl = port->padctl;
	struct tegra_xusb_lane *lane = port->lane;
	unsigned int index = port->index;
	u32 value;

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

	if (lane->pad == padctl->sata)
		tegra210_sata_uphy_disable(padctl);
	else
		tegra210_pex_uphy_disable(padctl);

	regulator_disable(usb3->supply);

	value = padctl_readl(padctl, XUSB_PADCTL_SS_PORT_MAP);
	value &= ~XUSB_PADCTL_SS_PORT_MAP_PORTX_MAP_MASK(index);
	value |= XUSB_PADCTL_SS_PORT_MAP_PORTX_MAP(index, 0x7);
	padctl_writel(padctl, value, XUSB_PADCTL_SS_PORT_MAP);
}

static const struct tegra_xusb_lane_map tegra210_usb3_map[] = {
	{ 0, "pcie", 6 },
	{ 1, "pcie", 5 },
	{ 2, "pcie", 0 },
	{ 2, "pcie", 3 },
	{ 3, "pcie", 4 },
	{ 3, "pcie", 4 },
	{ 0, NULL,   0 }
};

static struct tegra_xusb_lane *
tegra210_usb3_port_map(struct tegra_xusb_port *port)
{
	return tegra_xusb_port_find_lane(port, tegra210_usb3_map, "usb3-ss");
}

static const struct tegra_xusb_port_ops tegra210_usb3_port_ops = {
	.enable = tegra210_usb3_port_enable,
	.disable = tegra210_usb3_port_disable,
	.map = tegra210_usb3_port_map,
};

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
	int err;

	padctl = devm_kzalloc(dev, sizeof(*padctl), GFP_KERNEL);
	if (!padctl)
		return ERR_PTR(-ENOMEM);

	padctl->base.dev = dev;
	padctl->base.soc = soc;

	err = tegra210_xusb_read_fuse_calibration(&padctl->fuse);
	if (err < 0)
		return ERR_PTR(err);

	return &padctl->base;
}

static void tegra210_xusb_padctl_remove(struct tegra_xusb_padctl *padctl)
{
}

static const struct tegra_xusb_padctl_ops tegra210_xusb_padctl_ops = {
	.probe = tegra210_xusb_padctl_probe,
	.remove = tegra210_xusb_padctl_remove,
	.usb3_set_lfps_detect = tegra210_usb3_set_lfps_detect,
	.hsic_set_idle = tegra210_hsic_set_idle,
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
};
EXPORT_SYMBOL_GPL(tegra210_xusb_padctl_soc);

MODULE_AUTHOR("Andrew Bresticker <abrestic@chromium.org>");
MODULE_DESCRIPTION("NVIDIA Tegra 210 XUSB Pad Controller driver");
MODULE_LICENSE("GPL v2");
