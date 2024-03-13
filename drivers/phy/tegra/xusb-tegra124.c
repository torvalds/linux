// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014, NVIDIA CORPORATION.  All rights reserved.
 */

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

#define FUSE_SKU_CALIB_HS_CURR_LEVEL_PADX_SHIFT(x) ((x) ? 15 : 0)
#define FUSE_SKU_CALIB_HS_CURR_LEVEL_PAD_MASK 0x3f
#define FUSE_SKU_CALIB_HS_IREF_CAP_SHIFT 13
#define FUSE_SKU_CALIB_HS_IREF_CAP_MASK 0x3
#define FUSE_SKU_CALIB_HS_SQUELCH_LEVEL_SHIFT 11
#define FUSE_SKU_CALIB_HS_SQUELCH_LEVEL_MASK 0x3
#define FUSE_SKU_CALIB_HS_TERM_RANGE_ADJ_SHIFT 7
#define FUSE_SKU_CALIB_HS_TERM_RANGE_ADJ_MASK 0xf

#define XUSB_PADCTL_USB2_PORT_CAP 0x008
#define XUSB_PADCTL_USB2_PORT_CAP_PORTX_CAP_SHIFT(x) ((x) * 4)
#define XUSB_PADCTL_USB2_PORT_CAP_PORT_CAP_MASK 0x3
#define XUSB_PADCTL_USB2_PORT_CAP_DISABLED 0x0
#define XUSB_PADCTL_USB2_PORT_CAP_HOST 0x1
#define XUSB_PADCTL_USB2_PORT_CAP_DEVICE 0x2
#define XUSB_PADCTL_USB2_PORT_CAP_OTG 0x3

#define XUSB_PADCTL_SS_PORT_MAP 0x014
#define XUSB_PADCTL_SS_PORT_MAP_PORTX_INTERNAL(x) (1 << (((x) * 4) + 3))
#define XUSB_PADCTL_SS_PORT_MAP_PORTX_MAP_SHIFT(x) ((x) * 4)
#define XUSB_PADCTL_SS_PORT_MAP_PORTX_MAP_MASK(x) (0x7 << ((x) * 4))
#define XUSB_PADCTL_SS_PORT_MAP_PORTX_MAP(x, v) (((v) & 0x7) << ((x) * 4))
#define XUSB_PADCTL_SS_PORT_MAP_PORT_MAP_MASK 0x7

#define XUSB_PADCTL_ELPG_PROGRAM 0x01c
#define XUSB_PADCTL_ELPG_PROGRAM_AUX_MUX_LP0_VCORE_DOWN (1 << 26)
#define XUSB_PADCTL_ELPG_PROGRAM_AUX_MUX_LP0_CLAMP_EN_EARLY (1 << 25)
#define XUSB_PADCTL_ELPG_PROGRAM_AUX_MUX_LP0_CLAMP_EN (1 << 24)
#define XUSB_PADCTL_ELPG_PROGRAM_SSPX_ELPG_VCORE_DOWN(x) (1 << (18 + (x) * 4))
#define XUSB_PADCTL_ELPG_PROGRAM_SSPX_ELPG_CLAMP_EN_EARLY(x) \
							(1 << (17 + (x) * 4))
#define XUSB_PADCTL_ELPG_PROGRAM_SSPX_ELPG_CLAMP_EN(x) (1 << (16 + (x) * 4))

#define XUSB_PADCTL_IOPHY_PLL_P0_CTL1 0x040
#define XUSB_PADCTL_IOPHY_PLL_P0_CTL1_PLL0_LOCKDET (1 << 19)
#define XUSB_PADCTL_IOPHY_PLL_P0_CTL1_REFCLK_SEL_MASK (0xf << 12)
#define XUSB_PADCTL_IOPHY_PLL_P0_CTL1_PLL_RST (1 << 1)

#define XUSB_PADCTL_IOPHY_PLL_P0_CTL2 0x044
#define XUSB_PADCTL_IOPHY_PLL_P0_CTL2_REFCLKBUF_EN (1 << 6)
#define XUSB_PADCTL_IOPHY_PLL_P0_CTL2_TXCLKREF_EN (1 << 5)
#define XUSB_PADCTL_IOPHY_PLL_P0_CTL2_TXCLKREF_SEL (1 << 4)

#define XUSB_PADCTL_IOPHY_USB3_PADX_CTL2(x) (0x058 + (x) * 4)
#define XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_CDR_CNTL_SHIFT 24
#define XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_CDR_CNTL_MASK 0xff
#define XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_CDR_CNTL_VAL 0x24
#define XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_EQ_Z_SHIFT 16
#define XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_EQ_Z_MASK 0x3f
#define XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_EQ_G_SHIFT 8
#define XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_EQ_G_MASK 0x3f
#define XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_EQ_SHIFT 8
#define XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_EQ_MASK 0xffff
#define XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_EQ_VAL 0xf070
#define XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_WANDER_SHIFT 4
#define XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_WANDER_MASK 0xf
#define XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_WANDER_VAL 0xf

#define XUSB_PADCTL_IOPHY_USB3_PADX_CTL4(x) (0x068 + (x) * 4)
#define XUSB_PADCTL_IOPHY_USB3_PAD_CTL4_DFE_CNTL_TAP_SHIFT 24
#define XUSB_PADCTL_IOPHY_USB3_PAD_CTL4_DFE_CNTL_TAP_MASK 0x1f
#define XUSB_PADCTL_IOPHY_USB3_PAD_CTL4_DFE_CNTL_AMP_SHIFT 16
#define XUSB_PADCTL_IOPHY_USB3_PAD_CTL4_DFE_CNTL_AMP_MASK 0x7f
#define XUSB_PADCTL_IOPHY_USB3_PAD_CTL4_DFE_CNTL_VAL 0x002008ee

#define XUSB_PADCTL_IOPHY_MISC_PAD_PX_CTL2(x) ((x) < 2 ? 0x078 + (x) * 4 : \
					       0x0f8 + (x) * 4)
#define XUSB_PADCTL_IOPHY_MISC_PAD_CTL2_SPARE_IN_SHIFT 28
#define XUSB_PADCTL_IOPHY_MISC_PAD_CTL2_SPARE_IN_MASK 0x3
#define XUSB_PADCTL_IOPHY_MISC_PAD_CTL2_SPARE_IN_VAL 0x1

#define XUSB_PADCTL_IOPHY_MISC_PAD_PX_CTL5(x) ((x) < 2 ? 0x090 + (x) * 4 : \
					       0x11c + (x) * 4)
#define XUSB_PADCTL_IOPHY_MISC_PAD_CTL5_RX_QEYE_EN (1 << 8)

#define XUSB_PADCTL_IOPHY_MISC_PAD_PX_CTL6(x) ((x) < 2 ? 0x098 + (x) * 4 : \
					       0x128 + (x) * 4)
#define XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SHIFT 24
#define XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_G_Z_MASK 0x3f
#define XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_TAP_MASK 0x1f
#define XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_AMP_MASK 0x7f
#define XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SEL_SHIFT 16
#define XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SEL_MASK 0xff
#define XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SEL_G_Z 0x21
#define XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SEL_TAP 0x32
#define XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SEL_AMP 0x33
#define XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SEL_CTLE_Z 0x48
#define XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SEL_LATCH_G_Z 0xa1

#define XUSB_PADCTL_USB2_OTG_PADX_CTL0(x) (0x0a0 + (x) * 4)
#define XUSB_PADCTL_USB2_OTG_PAD_CTL0_PD_ZI (1 << 21)
#define XUSB_PADCTL_USB2_OTG_PAD_CTL0_PD2 (1 << 20)
#define XUSB_PADCTL_USB2_OTG_PAD_CTL0_PD (1 << 19)
#define XUSB_PADCTL_USB2_OTG_PAD_CTL0_LS_RSLEW_SHIFT 14
#define XUSB_PADCTL_USB2_OTG_PAD_CTL0_LS_RSLEW_MASK 0x3
#define XUSB_PADCTL_USB2_OTG_PAD_CTL0_LS_RSLEW_VAL(x) ((x) ? 0x0 : 0x3)
#define XUSB_PADCTL_USB2_OTG_PAD_CTL0_HS_SLEW_SHIFT 6
#define XUSB_PADCTL_USB2_OTG_PAD_CTL0_HS_SLEW_MASK 0x3f
#define XUSB_PADCTL_USB2_OTG_PAD_CTL0_HS_SLEW_VAL 0x0e
#define XUSB_PADCTL_USB2_OTG_PAD_CTL0_HS_CURR_LEVEL_SHIFT 0
#define XUSB_PADCTL_USB2_OTG_PAD_CTL0_HS_CURR_LEVEL_MASK 0x3f

#define XUSB_PADCTL_USB2_OTG_PADX_CTL1(x) (0x0ac + (x) * 4)
#define XUSB_PADCTL_USB2_OTG_PAD_CTL1_HS_IREF_CAP_SHIFT 9
#define XUSB_PADCTL_USB2_OTG_PAD_CTL1_HS_IREF_CAP_MASK 0x3
#define XUSB_PADCTL_USB2_OTG_PAD_CTL1_TERM_RANGE_ADJ_SHIFT 3
#define XUSB_PADCTL_USB2_OTG_PAD_CTL1_TERM_RANGE_ADJ_MASK 0x7
#define XUSB_PADCTL_USB2_OTG_PAD_CTL1_PD_DR (1 << 2)
#define XUSB_PADCTL_USB2_OTG_PAD_CTL1_PD_DISC_FORCE_POWERUP (1 << 1)
#define XUSB_PADCTL_USB2_OTG_PAD_CTL1_PD_CHRP_FORCE_POWERUP (1 << 0)

#define XUSB_PADCTL_USB2_BIAS_PAD_CTL0 0x0b8
#define XUSB_PADCTL_USB2_BIAS_PAD_CTL0_PD (1 << 12)
#define XUSB_PADCTL_USB2_BIAS_PAD_CTL0_HS_DISCON_LEVEL_SHIFT 2
#define XUSB_PADCTL_USB2_BIAS_PAD_CTL0_HS_DISCON_LEVEL_MASK 0x7
#define XUSB_PADCTL_USB2_BIAS_PAD_CTL0_HS_DISCON_LEVEL_VAL 0x5
#define XUSB_PADCTL_USB2_BIAS_PAD_CTL0_HS_SQUELCH_LEVEL_SHIFT 0
#define XUSB_PADCTL_USB2_BIAS_PAD_CTL0_HS_SQUELCH_LEVEL_MASK 0x3

#define XUSB_PADCTL_HSIC_PADX_CTL0(x) (0x0c0 + (x) * 4)
#define XUSB_PADCTL_HSIC_PAD_CTL0_TX_RSLEWN_SHIFT 12
#define XUSB_PADCTL_HSIC_PAD_CTL0_TX_RSLEWN_MASK 0x7
#define XUSB_PADCTL_HSIC_PAD_CTL0_TX_RSLEWP_SHIFT 8
#define XUSB_PADCTL_HSIC_PAD_CTL0_TX_RSLEWP_MASK 0x7
#define XUSB_PADCTL_HSIC_PAD_CTL0_TX_RTUNEN_SHIFT 4
#define XUSB_PADCTL_HSIC_PAD_CTL0_TX_RTUNEN_MASK 0x7
#define XUSB_PADCTL_HSIC_PAD_CTL0_TX_RTUNEP_SHIFT 0
#define XUSB_PADCTL_HSIC_PAD_CTL0_TX_RTUNEP_MASK 0x7

#define XUSB_PADCTL_HSIC_PADX_CTL1(x) (0x0c8 + (x) * 4)
#define XUSB_PADCTL_HSIC_PAD_CTL1_RPU_STROBE (1 << 10)
#define XUSB_PADCTL_HSIC_PAD_CTL1_RPU_DATA (1 << 9)
#define XUSB_PADCTL_HSIC_PAD_CTL1_RPD_STROBE (1 << 8)
#define XUSB_PADCTL_HSIC_PAD_CTL1_RPD_DATA (1 << 7)
#define XUSB_PADCTL_HSIC_PAD_CTL1_PD_ZI (1 << 5)
#define XUSB_PADCTL_HSIC_PAD_CTL1_PD_RX (1 << 4)
#define XUSB_PADCTL_HSIC_PAD_CTL1_PD_TRX (1 << 3)
#define XUSB_PADCTL_HSIC_PAD_CTL1_PD_TX (1 << 2)
#define XUSB_PADCTL_HSIC_PAD_CTL1_AUTO_TERM_EN (1 << 0)

#define XUSB_PADCTL_HSIC_PADX_CTL2(x) (0x0d0 + (x) * 4)
#define XUSB_PADCTL_HSIC_PAD_CTL2_RX_STROBE_TRIM_SHIFT 4
#define XUSB_PADCTL_HSIC_PAD_CTL2_RX_STROBE_TRIM_MASK 0x7
#define XUSB_PADCTL_HSIC_PAD_CTL2_RX_DATA_TRIM_SHIFT 0
#define XUSB_PADCTL_HSIC_PAD_CTL2_RX_DATA_TRIM_MASK 0x7

#define XUSB_PADCTL_HSIC_STRB_TRIM_CONTROL 0x0e0
#define XUSB_PADCTL_HSIC_STRB_TRIM_CONTROL_STRB_TRIM_MASK 0x1f

#define XUSB_PADCTL_USB3_PAD_MUX 0x134
#define XUSB_PADCTL_USB3_PAD_MUX_PCIE_IDDQ_DISABLE(x) (1 << (1 + (x)))
#define XUSB_PADCTL_USB3_PAD_MUX_SATA_IDDQ_DISABLE(x) (1 << (6 + (x)))

#define XUSB_PADCTL_IOPHY_PLL_S0_CTL1 0x138
#define XUSB_PADCTL_IOPHY_PLL_S0_CTL1_PLL1_LOCKDET (1 << 27)
#define XUSB_PADCTL_IOPHY_PLL_S0_CTL1_PLL1_MODE (1 << 24)
#define XUSB_PADCTL_IOPHY_PLL_S0_CTL1_PLL0_REFCLK_NDIV_SHIFT 20
#define XUSB_PADCTL_IOPHY_PLL_S0_CTL1_PLL0_REFCLK_NDIV_MASK 0x3
#define XUSB_PADCTL_IOPHY_PLL_S0_CTL1_PLL_PWR_OVRD (1 << 3)
#define XUSB_PADCTL_IOPHY_PLL_S0_CTL1_PLL_RST (1 << 1)
#define XUSB_PADCTL_IOPHY_PLL_S0_CTL1_PLL_IDDQ (1 << 0)

#define XUSB_PADCTL_IOPHY_PLL_S0_CTL2 0x13c
#define XUSB_PADCTL_IOPHY_PLL_S0_CTL2_PLL1_CP_CNTL_SHIFT 20
#define XUSB_PADCTL_IOPHY_PLL_S0_CTL2_PLL1_CP_CNTL_MASK 0xf
#define XUSB_PADCTL_IOPHY_PLL_S0_CTL2_PLL0_CP_CNTL_SHIFT 16
#define XUSB_PADCTL_IOPHY_PLL_S0_CTL2_PLL0_CP_CNTL_MASK 0xf
#define XUSB_PADCTL_IOPHY_PLL_S0_CTL2_TCLKOUT_EN (1 << 12)
#define XUSB_PADCTL_IOPHY_PLL_S0_CTL2_TXCLKREF_SEL (1 << 4)
#define XUSB_PADCTL_IOPHY_PLL_S0_CTL2_XDIGCLK_SEL_SHIFT 0
#define XUSB_PADCTL_IOPHY_PLL_S0_CTL2_XDIGCLK_SEL_MASK 0x7

#define XUSB_PADCTL_IOPHY_PLL_S0_CTL3 0x140
#define XUSB_PADCTL_IOPHY_PLL_S0_CTL3_RCAL_BYPASS (1 << 7)

#define XUSB_PADCTL_IOPHY_MISC_PAD_S0_CTL1 0x148
#define XUSB_PADCTL_IOPHY_MISC_PAD_S0_CTL1_IDDQ_OVRD (1 << 1)
#define XUSB_PADCTL_IOPHY_MISC_PAD_S0_CTL1_IDDQ (1 << 0)

#define XUSB_PADCTL_IOPHY_MISC_PAD_S0_CTL2 0x14c

#define XUSB_PADCTL_IOPHY_MISC_PAD_S0_CTL5 0x158

#define XUSB_PADCTL_IOPHY_MISC_PAD_S0_CTL6 0x15c

struct tegra124_xusb_fuse_calibration {
	u32 hs_curr_level[3];
	u32 hs_iref_cap;
	u32 hs_term_range_adj;
	u32 hs_squelch_level;
};

struct tegra124_xusb_padctl {
	struct tegra_xusb_padctl base;

	struct tegra124_xusb_fuse_calibration fuse;
};

static inline struct tegra124_xusb_padctl *
to_tegra124_xusb_padctl(struct tegra_xusb_padctl *padctl)
{
	return container_of(padctl, struct tegra124_xusb_padctl, base);
}

static int tegra124_xusb_padctl_enable(struct tegra_xusb_padctl *padctl)
{
	u32 value;

	mutex_lock(&padctl->lock);

	if (padctl->enable++ > 0)
		goto out;

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM);
	value &= ~XUSB_PADCTL_ELPG_PROGRAM_AUX_MUX_LP0_CLAMP_EN;
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM);

	usleep_range(100, 200);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM);
	value &= ~XUSB_PADCTL_ELPG_PROGRAM_AUX_MUX_LP0_CLAMP_EN_EARLY;
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM);

	usleep_range(100, 200);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM);
	value &= ~XUSB_PADCTL_ELPG_PROGRAM_AUX_MUX_LP0_VCORE_DOWN;
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM);

out:
	mutex_unlock(&padctl->lock);
	return 0;
}

static int tegra124_xusb_padctl_disable(struct tegra_xusb_padctl *padctl)
{
	u32 value;

	mutex_lock(&padctl->lock);

	if (WARN_ON(padctl->enable == 0))
		goto out;

	if (--padctl->enable > 0)
		goto out;

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM);
	value |= XUSB_PADCTL_ELPG_PROGRAM_AUX_MUX_LP0_VCORE_DOWN;
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM);

	usleep_range(100, 200);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM);
	value |= XUSB_PADCTL_ELPG_PROGRAM_AUX_MUX_LP0_CLAMP_EN_EARLY;
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM);

	usleep_range(100, 200);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM);
	value |= XUSB_PADCTL_ELPG_PROGRAM_AUX_MUX_LP0_CLAMP_EN;
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM);

out:
	mutex_unlock(&padctl->lock);
	return 0;
}

static int tegra124_usb3_save_context(struct tegra_xusb_padctl *padctl,
				      unsigned int index)
{
	struct tegra_xusb_usb3_port *port;
	struct tegra_xusb_lane *lane;
	u32 value, offset;

	port = tegra_xusb_find_usb3_port(padctl, index);
	if (!port)
		return -ENODEV;

	port->context_saved = true;
	lane = port->base.lane;

	if (lane->pad == padctl->pcie)
		offset = XUSB_PADCTL_IOPHY_MISC_PAD_PX_CTL6(lane->index);
	else
		offset = XUSB_PADCTL_IOPHY_MISC_PAD_S0_CTL6;

	value = padctl_readl(padctl, offset);
	value &= ~(XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SEL_MASK <<
		   XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SEL_SHIFT);
	value |= XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SEL_TAP <<
		XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SEL_SHIFT;
	padctl_writel(padctl, value, offset);

	value = padctl_readl(padctl, offset) >>
		XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SHIFT;
	port->tap1 = value & XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_TAP_MASK;

	value = padctl_readl(padctl, offset);
	value &= ~(XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SEL_MASK <<
		   XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SEL_SHIFT);
	value |= XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SEL_AMP <<
		XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SEL_SHIFT;
	padctl_writel(padctl, value, offset);

	value = padctl_readl(padctl, offset) >>
		XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SHIFT;
	port->amp = value & XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_AMP_MASK;

	value = padctl_readl(padctl, XUSB_PADCTL_IOPHY_USB3_PADX_CTL4(index));
	value &= ~((XUSB_PADCTL_IOPHY_USB3_PAD_CTL4_DFE_CNTL_TAP_MASK <<
		    XUSB_PADCTL_IOPHY_USB3_PAD_CTL4_DFE_CNTL_TAP_SHIFT) |
		   (XUSB_PADCTL_IOPHY_USB3_PAD_CTL4_DFE_CNTL_AMP_MASK <<
		    XUSB_PADCTL_IOPHY_USB3_PAD_CTL4_DFE_CNTL_AMP_SHIFT));
	value |= (port->tap1 <<
		  XUSB_PADCTL_IOPHY_USB3_PAD_CTL4_DFE_CNTL_TAP_SHIFT) |
		 (port->amp <<
		  XUSB_PADCTL_IOPHY_USB3_PAD_CTL4_DFE_CNTL_AMP_SHIFT);
	padctl_writel(padctl, value, XUSB_PADCTL_IOPHY_USB3_PADX_CTL4(index));

	value = padctl_readl(padctl, offset);
	value &= ~(XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SEL_MASK <<
		   XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SEL_SHIFT);
	value |= XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SEL_LATCH_G_Z <<
		XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SEL_SHIFT;
	padctl_writel(padctl, value, offset);

	value = padctl_readl(padctl, offset);
	value &= ~(XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SEL_MASK <<
		   XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SEL_SHIFT);
	value |= XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SEL_G_Z <<
		XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SEL_SHIFT;
	padctl_writel(padctl, value, offset);

	value = padctl_readl(padctl, offset) >>
		XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SHIFT;
	port->ctle_g = value &
		XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_G_Z_MASK;

	value = padctl_readl(padctl, offset);
	value &= ~(XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SEL_MASK <<
		   XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SEL_SHIFT);
	value |= XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SEL_CTLE_Z <<
		XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SEL_SHIFT;
	padctl_writel(padctl, value, offset);

	value = padctl_readl(padctl, offset) >>
		XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_SHIFT;
	port->ctle_z = value &
		XUSB_PADCTL_IOPHY_MISC_PAD_CTL6_MISC_OUT_G_Z_MASK;

	value = padctl_readl(padctl, XUSB_PADCTL_IOPHY_USB3_PADX_CTL2(index));
	value &= ~((XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_EQ_G_MASK <<
		    XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_EQ_G_SHIFT) |
		   (XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_EQ_Z_MASK <<
		    XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_EQ_Z_SHIFT));
	value |= (port->ctle_g <<
		  XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_EQ_G_SHIFT) |
		 (port->ctle_z <<
		  XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_EQ_Z_SHIFT);
	padctl_writel(padctl, value, XUSB_PADCTL_IOPHY_USB3_PADX_CTL2(index));

	return 0;
}

static int tegra124_hsic_set_idle(struct tegra_xusb_padctl *padctl,
				  unsigned int index, bool idle)
{
	u32 value;

	value = padctl_readl(padctl, XUSB_PADCTL_HSIC_PADX_CTL1(index));

	if (idle)
		value |= XUSB_PADCTL_HSIC_PAD_CTL1_RPD_DATA |
			 XUSB_PADCTL_HSIC_PAD_CTL1_RPU_STROBE;
	else
		value &= ~(XUSB_PADCTL_HSIC_PAD_CTL1_RPD_DATA |
			   XUSB_PADCTL_HSIC_PAD_CTL1_RPU_STROBE);

	padctl_writel(padctl, value, XUSB_PADCTL_HSIC_PADX_CTL1(index));

	return 0;
}

#define TEGRA124_LANE(_name, _offset, _shift, _mask, _type)		\
	{								\
		.name = _name,						\
		.offset = _offset,					\
		.shift = _shift,					\
		.mask = _mask,						\
		.num_funcs = ARRAY_SIZE(tegra124_##_type##_functions),	\
		.funcs = tegra124_##_type##_functions,			\
	}

static const char * const tegra124_usb2_functions[] = {
	"snps",
	"xusb",
	"uart",
};

static const struct tegra_xusb_lane_soc tegra124_usb2_lanes[] = {
	TEGRA124_LANE("usb2-0", 0x004,  0, 0x3, usb2),
	TEGRA124_LANE("usb2-1", 0x004,  2, 0x3, usb2),
	TEGRA124_LANE("usb2-2", 0x004,  4, 0x3, usb2),
};

static struct tegra_xusb_lane *
tegra124_usb2_lane_probe(struct tegra_xusb_pad *pad, struct device_node *np,
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

static void tegra124_usb2_lane_remove(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_usb2_lane *usb2 = to_usb2_lane(lane);

	kfree(usb2);
}

static const struct tegra_xusb_lane_ops tegra124_usb2_lane_ops = {
	.probe = tegra124_usb2_lane_probe,
	.remove = tegra124_usb2_lane_remove,
};

static int tegra124_usb2_phy_init(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);

	return tegra124_xusb_padctl_enable(lane->pad->padctl);
}

static int tegra124_usb2_phy_exit(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);

	return tegra124_xusb_padctl_disable(lane->pad->padctl);
}

static int tegra124_usb2_phy_power_on(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_usb2_lane *usb2 = to_usb2_lane(lane);
	struct tegra_xusb_usb2_pad *pad = to_usb2_pad(lane->pad);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	struct tegra124_xusb_padctl *priv;
	struct tegra_xusb_usb2_port *port;
	unsigned int index = lane->index;
	u32 value;
	int err;

	port = tegra_xusb_find_usb2_port(padctl, index);
	if (!port) {
		dev_err(&phy->dev, "no port found for USB2 lane %u\n", index);
		return -ENODEV;
	}

	priv = to_tegra124_xusb_padctl(padctl);

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_BIAS_PAD_CTL0);
	value &= ~((XUSB_PADCTL_USB2_BIAS_PAD_CTL0_HS_SQUELCH_LEVEL_MASK <<
		    XUSB_PADCTL_USB2_BIAS_PAD_CTL0_HS_SQUELCH_LEVEL_SHIFT) |
		   (XUSB_PADCTL_USB2_BIAS_PAD_CTL0_HS_DISCON_LEVEL_MASK <<
		    XUSB_PADCTL_USB2_BIAS_PAD_CTL0_HS_DISCON_LEVEL_SHIFT));
	value |= (priv->fuse.hs_squelch_level <<
		  XUSB_PADCTL_USB2_BIAS_PAD_CTL0_HS_SQUELCH_LEVEL_SHIFT) |
		 (XUSB_PADCTL_USB2_BIAS_PAD_CTL0_HS_DISCON_LEVEL_VAL <<
		  XUSB_PADCTL_USB2_BIAS_PAD_CTL0_HS_DISCON_LEVEL_SHIFT);
	padctl_writel(padctl, value, XUSB_PADCTL_USB2_BIAS_PAD_CTL0);

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_PORT_CAP);
	value &= ~(XUSB_PADCTL_USB2_PORT_CAP_PORT_CAP_MASK <<
		   XUSB_PADCTL_USB2_PORT_CAP_PORTX_CAP_SHIFT(index));
	value |= XUSB_PADCTL_USB2_PORT_CAP_HOST <<
		XUSB_PADCTL_USB2_PORT_CAP_PORTX_CAP_SHIFT(index);
	padctl_writel(padctl, value, XUSB_PADCTL_USB2_PORT_CAP);

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_OTG_PADX_CTL0(index));
	value &= ~((XUSB_PADCTL_USB2_OTG_PAD_CTL0_HS_CURR_LEVEL_MASK <<
		    XUSB_PADCTL_USB2_OTG_PAD_CTL0_HS_CURR_LEVEL_SHIFT) |
		   (XUSB_PADCTL_USB2_OTG_PAD_CTL0_HS_SLEW_MASK <<
		    XUSB_PADCTL_USB2_OTG_PAD_CTL0_HS_SLEW_SHIFT) |
		   (XUSB_PADCTL_USB2_OTG_PAD_CTL0_LS_RSLEW_MASK <<
		    XUSB_PADCTL_USB2_OTG_PAD_CTL0_LS_RSLEW_SHIFT) |
		   XUSB_PADCTL_USB2_OTG_PAD_CTL0_PD |
		   XUSB_PADCTL_USB2_OTG_PAD_CTL0_PD2 |
		   XUSB_PADCTL_USB2_OTG_PAD_CTL0_PD_ZI);
	value |= (priv->fuse.hs_curr_level[index] +
		  usb2->hs_curr_level_offset) <<
		XUSB_PADCTL_USB2_OTG_PAD_CTL0_HS_CURR_LEVEL_SHIFT;
	value |= XUSB_PADCTL_USB2_OTG_PAD_CTL0_HS_SLEW_VAL <<
		XUSB_PADCTL_USB2_OTG_PAD_CTL0_HS_SLEW_SHIFT;
	value |= XUSB_PADCTL_USB2_OTG_PAD_CTL0_LS_RSLEW_VAL(index) <<
		XUSB_PADCTL_USB2_OTG_PAD_CTL0_LS_RSLEW_SHIFT;
	padctl_writel(padctl, value, XUSB_PADCTL_USB2_OTG_PADX_CTL0(index));

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_OTG_PADX_CTL1(index));
	value &= ~((XUSB_PADCTL_USB2_OTG_PAD_CTL1_TERM_RANGE_ADJ_MASK <<
		    XUSB_PADCTL_USB2_OTG_PAD_CTL1_TERM_RANGE_ADJ_SHIFT) |
		   (XUSB_PADCTL_USB2_OTG_PAD_CTL1_HS_IREF_CAP_MASK <<
		    XUSB_PADCTL_USB2_OTG_PAD_CTL1_HS_IREF_CAP_SHIFT) |
		   XUSB_PADCTL_USB2_OTG_PAD_CTL1_PD_DR |
		   XUSB_PADCTL_USB2_OTG_PAD_CTL1_PD_CHRP_FORCE_POWERUP |
		   XUSB_PADCTL_USB2_OTG_PAD_CTL1_PD_DISC_FORCE_POWERUP);
	value |= (priv->fuse.hs_term_range_adj <<
		  XUSB_PADCTL_USB2_OTG_PAD_CTL1_TERM_RANGE_ADJ_SHIFT) |
		 (priv->fuse.hs_iref_cap <<
		  XUSB_PADCTL_USB2_OTG_PAD_CTL1_HS_IREF_CAP_SHIFT);
	padctl_writel(padctl, value, XUSB_PADCTL_USB2_OTG_PADX_CTL1(index));

	err = regulator_enable(port->supply);
	if (err)
		return err;

	mutex_lock(&pad->lock);

	if (pad->enable++ > 0)
		goto out;

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_BIAS_PAD_CTL0);
	value &= ~XUSB_PADCTL_USB2_BIAS_PAD_CTL0_PD;
	padctl_writel(padctl, value, XUSB_PADCTL_USB2_BIAS_PAD_CTL0);

out:
	mutex_unlock(&pad->lock);
	return 0;
}

static int tegra124_usb2_phy_power_off(struct phy *phy)
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

	mutex_lock(&pad->lock);

	if (WARN_ON(pad->enable == 0))
		goto out;

	if (--pad->enable > 0)
		goto out;

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_BIAS_PAD_CTL0);
	value |= XUSB_PADCTL_USB2_BIAS_PAD_CTL0_PD;
	padctl_writel(padctl, value, XUSB_PADCTL_USB2_BIAS_PAD_CTL0);

out:
	regulator_disable(port->supply);
	mutex_unlock(&pad->lock);
	return 0;
}

static const struct phy_ops tegra124_usb2_phy_ops = {
	.init = tegra124_usb2_phy_init,
	.exit = tegra124_usb2_phy_exit,
	.power_on = tegra124_usb2_phy_power_on,
	.power_off = tegra124_usb2_phy_power_off,
	.owner = THIS_MODULE,
};

static struct tegra_xusb_pad *
tegra124_usb2_pad_probe(struct tegra_xusb_padctl *padctl,
			const struct tegra_xusb_pad_soc *soc,
			struct device_node *np)
{
	struct tegra_xusb_usb2_pad *usb2;
	struct tegra_xusb_pad *pad;
	int err;

	usb2 = kzalloc(sizeof(*usb2), GFP_KERNEL);
	if (!usb2)
		return ERR_PTR(-ENOMEM);

	mutex_init(&usb2->lock);

	pad = &usb2->base;
	pad->ops = &tegra124_usb2_lane_ops;
	pad->soc = soc;

	err = tegra_xusb_pad_init(pad, padctl, np);
	if (err < 0) {
		kfree(usb2);
		goto out;
	}

	err = tegra_xusb_pad_register(pad, &tegra124_usb2_phy_ops);
	if (err < 0)
		goto unregister;

	dev_set_drvdata(&pad->dev, pad);

	return pad;

unregister:
	device_unregister(&pad->dev);
out:
	return ERR_PTR(err);
}

static void tegra124_usb2_pad_remove(struct tegra_xusb_pad *pad)
{
	struct tegra_xusb_usb2_pad *usb2 = to_usb2_pad(pad);

	kfree(usb2);
}

static const struct tegra_xusb_pad_ops tegra124_usb2_ops = {
	.probe = tegra124_usb2_pad_probe,
	.remove = tegra124_usb2_pad_remove,
};

static const struct tegra_xusb_pad_soc tegra124_usb2_pad = {
	.name = "usb2",
	.num_lanes = ARRAY_SIZE(tegra124_usb2_lanes),
	.lanes = tegra124_usb2_lanes,
	.ops = &tegra124_usb2_ops,
};

static const char * const tegra124_ulpi_functions[] = {
	"snps",
	"xusb",
};

static const struct tegra_xusb_lane_soc tegra124_ulpi_lanes[] = {
	TEGRA124_LANE("ulpi-0", 0x004, 12, 0x1, ulpi),
};

static struct tegra_xusb_lane *
tegra124_ulpi_lane_probe(struct tegra_xusb_pad *pad, struct device_node *np,
			 unsigned int index)
{
	struct tegra_xusb_ulpi_lane *ulpi;
	int err;

	ulpi = kzalloc(sizeof(*ulpi), GFP_KERNEL);
	if (!ulpi)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&ulpi->base.list);
	ulpi->base.soc = &pad->soc->lanes[index];
	ulpi->base.index = index;
	ulpi->base.pad = pad;
	ulpi->base.np = np;

	err = tegra_xusb_lane_parse_dt(&ulpi->base, np);
	if (err < 0) {
		kfree(ulpi);
		return ERR_PTR(err);
	}

	return &ulpi->base;
}

static void tegra124_ulpi_lane_remove(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_ulpi_lane *ulpi = to_ulpi_lane(lane);

	kfree(ulpi);
}

static const struct tegra_xusb_lane_ops tegra124_ulpi_lane_ops = {
	.probe = tegra124_ulpi_lane_probe,
	.remove = tegra124_ulpi_lane_remove,
};

static int tegra124_ulpi_phy_init(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);

	return tegra124_xusb_padctl_enable(lane->pad->padctl);
}

static int tegra124_ulpi_phy_exit(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);

	return tegra124_xusb_padctl_disable(lane->pad->padctl);
}

static int tegra124_ulpi_phy_power_on(struct phy *phy)
{
	return 0;
}

static int tegra124_ulpi_phy_power_off(struct phy *phy)
{
	return 0;
}

static const struct phy_ops tegra124_ulpi_phy_ops = {
	.init = tegra124_ulpi_phy_init,
	.exit = tegra124_ulpi_phy_exit,
	.power_on = tegra124_ulpi_phy_power_on,
	.power_off = tegra124_ulpi_phy_power_off,
	.owner = THIS_MODULE,
};

static struct tegra_xusb_pad *
tegra124_ulpi_pad_probe(struct tegra_xusb_padctl *padctl,
			const struct tegra_xusb_pad_soc *soc,
			struct device_node *np)
{
	struct tegra_xusb_ulpi_pad *ulpi;
	struct tegra_xusb_pad *pad;
	int err;

	ulpi = kzalloc(sizeof(*ulpi), GFP_KERNEL);
	if (!ulpi)
		return ERR_PTR(-ENOMEM);

	pad = &ulpi->base;
	pad->ops = &tegra124_ulpi_lane_ops;
	pad->soc = soc;

	err = tegra_xusb_pad_init(pad, padctl, np);
	if (err < 0) {
		kfree(ulpi);
		goto out;
	}

	err = tegra_xusb_pad_register(pad, &tegra124_ulpi_phy_ops);
	if (err < 0)
		goto unregister;

	dev_set_drvdata(&pad->dev, pad);

	return pad;

unregister:
	device_unregister(&pad->dev);
out:
	return ERR_PTR(err);
}

static void tegra124_ulpi_pad_remove(struct tegra_xusb_pad *pad)
{
	struct tegra_xusb_ulpi_pad *ulpi = to_ulpi_pad(pad);

	kfree(ulpi);
}

static const struct tegra_xusb_pad_ops tegra124_ulpi_ops = {
	.probe = tegra124_ulpi_pad_probe,
	.remove = tegra124_ulpi_pad_remove,
};

static const struct tegra_xusb_pad_soc tegra124_ulpi_pad = {
	.name = "ulpi",
	.num_lanes = ARRAY_SIZE(tegra124_ulpi_lanes),
	.lanes = tegra124_ulpi_lanes,
	.ops = &tegra124_ulpi_ops,
};

static const char * const tegra124_hsic_functions[] = {
	"snps",
	"xusb",
};

static const struct tegra_xusb_lane_soc tegra124_hsic_lanes[] = {
	TEGRA124_LANE("hsic-0", 0x004, 14, 0x1, hsic),
	TEGRA124_LANE("hsic-1", 0x004, 15, 0x1, hsic),
};

static struct tegra_xusb_lane *
tegra124_hsic_lane_probe(struct tegra_xusb_pad *pad, struct device_node *np,
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

static void tegra124_hsic_lane_remove(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_hsic_lane *hsic = to_hsic_lane(lane);

	kfree(hsic);
}

static const struct tegra_xusb_lane_ops tegra124_hsic_lane_ops = {
	.probe = tegra124_hsic_lane_probe,
	.remove = tegra124_hsic_lane_remove,
};

static int tegra124_hsic_phy_init(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);

	return tegra124_xusb_padctl_enable(lane->pad->padctl);
}

static int tegra124_hsic_phy_exit(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);

	return tegra124_xusb_padctl_disable(lane->pad->padctl);
}

static int tegra124_hsic_phy_power_on(struct phy *phy)
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

	if (hsic->auto_term)
		value |= XUSB_PADCTL_HSIC_PAD_CTL1_AUTO_TERM_EN;
	else
		value &= ~XUSB_PADCTL_HSIC_PAD_CTL1_AUTO_TERM_EN;

	padctl_writel(padctl, value, XUSB_PADCTL_HSIC_PADX_CTL1(index));

	value = padctl_readl(padctl, XUSB_PADCTL_HSIC_PADX_CTL0(index));
	value &= ~((XUSB_PADCTL_HSIC_PAD_CTL0_TX_RTUNEN_MASK <<
		    XUSB_PADCTL_HSIC_PAD_CTL0_TX_RTUNEN_SHIFT) |
		   (XUSB_PADCTL_HSIC_PAD_CTL0_TX_RTUNEP_MASK <<
		    XUSB_PADCTL_HSIC_PAD_CTL0_TX_RTUNEP_SHIFT) |
		   (XUSB_PADCTL_HSIC_PAD_CTL0_TX_RSLEWN_MASK <<
		    XUSB_PADCTL_HSIC_PAD_CTL0_TX_RSLEWN_SHIFT) |
		   (XUSB_PADCTL_HSIC_PAD_CTL0_TX_RSLEWP_MASK <<
		    XUSB_PADCTL_HSIC_PAD_CTL0_TX_RSLEWP_SHIFT));
	value |= (hsic->tx_rtune_n <<
		  XUSB_PADCTL_HSIC_PAD_CTL0_TX_RTUNEN_SHIFT) |
		(hsic->tx_rtune_p <<
		  XUSB_PADCTL_HSIC_PAD_CTL0_TX_RTUNEP_SHIFT) |
		(hsic->tx_rslew_n <<
		 XUSB_PADCTL_HSIC_PAD_CTL0_TX_RSLEWN_SHIFT) |
		(hsic->tx_rslew_p <<
		 XUSB_PADCTL_HSIC_PAD_CTL0_TX_RSLEWP_SHIFT);
	padctl_writel(padctl, value, XUSB_PADCTL_HSIC_PADX_CTL0(index));

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

	value = padctl_readl(padctl, XUSB_PADCTL_HSIC_PADX_CTL1(index));
	value &= ~(XUSB_PADCTL_HSIC_PAD_CTL1_RPD_STROBE |
		   XUSB_PADCTL_HSIC_PAD_CTL1_RPU_DATA |
		   XUSB_PADCTL_HSIC_PAD_CTL1_PD_RX |
		   XUSB_PADCTL_HSIC_PAD_CTL1_PD_ZI |
		   XUSB_PADCTL_HSIC_PAD_CTL1_PD_TRX |
		   XUSB_PADCTL_HSIC_PAD_CTL1_PD_TX);
	value |= XUSB_PADCTL_HSIC_PAD_CTL1_RPD_DATA |
		 XUSB_PADCTL_HSIC_PAD_CTL1_RPU_STROBE;
	padctl_writel(padctl, value, XUSB_PADCTL_HSIC_PADX_CTL1(index));

	return 0;
}

static int tegra124_hsic_phy_power_off(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_hsic_pad *pad = to_hsic_pad(lane->pad);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	unsigned int index = lane->index;
	u32 value;

	value = padctl_readl(padctl, XUSB_PADCTL_HSIC_PADX_CTL1(index));
	value |= XUSB_PADCTL_HSIC_PAD_CTL1_PD_RX |
		 XUSB_PADCTL_HSIC_PAD_CTL1_PD_ZI |
		 XUSB_PADCTL_HSIC_PAD_CTL1_PD_TRX |
		 XUSB_PADCTL_HSIC_PAD_CTL1_PD_TX;
	padctl_writel(padctl, value, XUSB_PADCTL_HSIC_PADX_CTL1(index));

	regulator_disable(pad->supply);

	return 0;
}

static const struct phy_ops tegra124_hsic_phy_ops = {
	.init = tegra124_hsic_phy_init,
	.exit = tegra124_hsic_phy_exit,
	.power_on = tegra124_hsic_phy_power_on,
	.power_off = tegra124_hsic_phy_power_off,
	.owner = THIS_MODULE,
};

static struct tegra_xusb_pad *
tegra124_hsic_pad_probe(struct tegra_xusb_padctl *padctl,
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
	pad->ops = &tegra124_hsic_lane_ops;
	pad->soc = soc;

	err = tegra_xusb_pad_init(pad, padctl, np);
	if (err < 0) {
		kfree(hsic);
		goto out;
	}

	err = tegra_xusb_pad_register(pad, &tegra124_hsic_phy_ops);
	if (err < 0)
		goto unregister;

	dev_set_drvdata(&pad->dev, pad);

	return pad;

unregister:
	device_unregister(&pad->dev);
out:
	return ERR_PTR(err);
}

static void tegra124_hsic_pad_remove(struct tegra_xusb_pad *pad)
{
	struct tegra_xusb_hsic_pad *hsic = to_hsic_pad(pad);

	kfree(hsic);
}

static const struct tegra_xusb_pad_ops tegra124_hsic_ops = {
	.probe = tegra124_hsic_pad_probe,
	.remove = tegra124_hsic_pad_remove,
};

static const struct tegra_xusb_pad_soc tegra124_hsic_pad = {
	.name = "hsic",
	.num_lanes = ARRAY_SIZE(tegra124_hsic_lanes),
	.lanes = tegra124_hsic_lanes,
	.ops = &tegra124_hsic_ops,
};

static const char * const tegra124_pcie_functions[] = {
	"pcie",
	"usb3-ss",
	"sata",
};

static const struct tegra_xusb_lane_soc tegra124_pcie_lanes[] = {
	TEGRA124_LANE("pcie-0", 0x134, 16, 0x3, pcie),
	TEGRA124_LANE("pcie-1", 0x134, 18, 0x3, pcie),
	TEGRA124_LANE("pcie-2", 0x134, 20, 0x3, pcie),
	TEGRA124_LANE("pcie-3", 0x134, 22, 0x3, pcie),
	TEGRA124_LANE("pcie-4", 0x134, 24, 0x3, pcie),
};

static struct tegra_xusb_lane *
tegra124_pcie_lane_probe(struct tegra_xusb_pad *pad, struct device_node *np,
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

static void tegra124_pcie_lane_remove(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_pcie_lane *pcie = to_pcie_lane(lane);

	kfree(pcie);
}

static const struct tegra_xusb_lane_ops tegra124_pcie_lane_ops = {
	.probe = tegra124_pcie_lane_probe,
	.remove = tegra124_pcie_lane_remove,
};

static int tegra124_pcie_phy_init(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);

	return tegra124_xusb_padctl_enable(lane->pad->padctl);
}

static int tegra124_pcie_phy_exit(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);

	return tegra124_xusb_padctl_disable(lane->pad->padctl);
}

static int tegra124_pcie_phy_power_on(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	unsigned long timeout;
	int err = -ETIMEDOUT;
	u32 value;

	value = padctl_readl(padctl, XUSB_PADCTL_IOPHY_PLL_P0_CTL1);
	value &= ~XUSB_PADCTL_IOPHY_PLL_P0_CTL1_REFCLK_SEL_MASK;
	padctl_writel(padctl, value, XUSB_PADCTL_IOPHY_PLL_P0_CTL1);

	value = padctl_readl(padctl, XUSB_PADCTL_IOPHY_PLL_P0_CTL2);
	value |= XUSB_PADCTL_IOPHY_PLL_P0_CTL2_REFCLKBUF_EN |
		 XUSB_PADCTL_IOPHY_PLL_P0_CTL2_TXCLKREF_EN |
		 XUSB_PADCTL_IOPHY_PLL_P0_CTL2_TXCLKREF_SEL;
	padctl_writel(padctl, value, XUSB_PADCTL_IOPHY_PLL_P0_CTL2);

	value = padctl_readl(padctl, XUSB_PADCTL_IOPHY_PLL_P0_CTL1);
	value |= XUSB_PADCTL_IOPHY_PLL_P0_CTL1_PLL_RST;
	padctl_writel(padctl, value, XUSB_PADCTL_IOPHY_PLL_P0_CTL1);

	timeout = jiffies + msecs_to_jiffies(50);

	while (time_before(jiffies, timeout)) {
		value = padctl_readl(padctl, XUSB_PADCTL_IOPHY_PLL_P0_CTL1);
		if (value & XUSB_PADCTL_IOPHY_PLL_P0_CTL1_PLL0_LOCKDET) {
			err = 0;
			break;
		}

		usleep_range(100, 200);
	}

	value = padctl_readl(padctl, XUSB_PADCTL_USB3_PAD_MUX);
	value |= XUSB_PADCTL_USB3_PAD_MUX_PCIE_IDDQ_DISABLE(lane->index);
	padctl_writel(padctl, value, XUSB_PADCTL_USB3_PAD_MUX);

	return err;
}

static int tegra124_pcie_phy_power_off(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	u32 value;

	value = padctl_readl(padctl, XUSB_PADCTL_USB3_PAD_MUX);
	value &= ~XUSB_PADCTL_USB3_PAD_MUX_PCIE_IDDQ_DISABLE(lane->index);
	padctl_writel(padctl, value, XUSB_PADCTL_USB3_PAD_MUX);

	value = padctl_readl(padctl, XUSB_PADCTL_IOPHY_PLL_P0_CTL1);
	value &= ~XUSB_PADCTL_IOPHY_PLL_P0_CTL1_PLL_RST;
	padctl_writel(padctl, value, XUSB_PADCTL_IOPHY_PLL_P0_CTL1);

	return 0;
}

static const struct phy_ops tegra124_pcie_phy_ops = {
	.init = tegra124_pcie_phy_init,
	.exit = tegra124_pcie_phy_exit,
	.power_on = tegra124_pcie_phy_power_on,
	.power_off = tegra124_pcie_phy_power_off,
	.owner = THIS_MODULE,
};

static struct tegra_xusb_pad *
tegra124_pcie_pad_probe(struct tegra_xusb_padctl *padctl,
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
	pad->ops = &tegra124_pcie_lane_ops;
	pad->soc = soc;

	err = tegra_xusb_pad_init(pad, padctl, np);
	if (err < 0) {
		kfree(pcie);
		goto out;
	}

	err = tegra_xusb_pad_register(pad, &tegra124_pcie_phy_ops);
	if (err < 0)
		goto unregister;

	dev_set_drvdata(&pad->dev, pad);

	return pad;

unregister:
	device_unregister(&pad->dev);
out:
	return ERR_PTR(err);
}

static void tegra124_pcie_pad_remove(struct tegra_xusb_pad *pad)
{
	struct tegra_xusb_pcie_pad *pcie = to_pcie_pad(pad);

	kfree(pcie);
}

static const struct tegra_xusb_pad_ops tegra124_pcie_ops = {
	.probe = tegra124_pcie_pad_probe,
	.remove = tegra124_pcie_pad_remove,
};

static const struct tegra_xusb_pad_soc tegra124_pcie_pad = {
	.name = "pcie",
	.num_lanes = ARRAY_SIZE(tegra124_pcie_lanes),
	.lanes = tegra124_pcie_lanes,
	.ops = &tegra124_pcie_ops,
};

static const struct tegra_xusb_lane_soc tegra124_sata_lanes[] = {
	TEGRA124_LANE("sata-0", 0x134, 26, 0x3, pcie),
};

static struct tegra_xusb_lane *
tegra124_sata_lane_probe(struct tegra_xusb_pad *pad, struct device_node *np,
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

static void tegra124_sata_lane_remove(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_sata_lane *sata = to_sata_lane(lane);

	kfree(sata);
}

static const struct tegra_xusb_lane_ops tegra124_sata_lane_ops = {
	.probe = tegra124_sata_lane_probe,
	.remove = tegra124_sata_lane_remove,
};

static int tegra124_sata_phy_init(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);

	return tegra124_xusb_padctl_enable(lane->pad->padctl);
}

static int tegra124_sata_phy_exit(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);

	return tegra124_xusb_padctl_disable(lane->pad->padctl);
}

static int tegra124_sata_phy_power_on(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	unsigned long timeout;
	int err = -ETIMEDOUT;
	u32 value;

	value = padctl_readl(padctl, XUSB_PADCTL_IOPHY_MISC_PAD_S0_CTL1);
	value &= ~XUSB_PADCTL_IOPHY_MISC_PAD_S0_CTL1_IDDQ_OVRD;
	value &= ~XUSB_PADCTL_IOPHY_MISC_PAD_S0_CTL1_IDDQ;
	padctl_writel(padctl, value, XUSB_PADCTL_IOPHY_MISC_PAD_S0_CTL1);

	value = padctl_readl(padctl, XUSB_PADCTL_IOPHY_PLL_S0_CTL1);
	value &= ~XUSB_PADCTL_IOPHY_PLL_S0_CTL1_PLL_PWR_OVRD;
	value &= ~XUSB_PADCTL_IOPHY_PLL_S0_CTL1_PLL_IDDQ;
	padctl_writel(padctl, value, XUSB_PADCTL_IOPHY_PLL_S0_CTL1);

	value = padctl_readl(padctl, XUSB_PADCTL_IOPHY_PLL_S0_CTL1);
	value |= XUSB_PADCTL_IOPHY_PLL_S0_CTL1_PLL1_MODE;
	padctl_writel(padctl, value, XUSB_PADCTL_IOPHY_PLL_S0_CTL1);

	value = padctl_readl(padctl, XUSB_PADCTL_IOPHY_PLL_S0_CTL1);
	value |= XUSB_PADCTL_IOPHY_PLL_S0_CTL1_PLL_RST;
	padctl_writel(padctl, value, XUSB_PADCTL_IOPHY_PLL_S0_CTL1);

	timeout = jiffies + msecs_to_jiffies(50);

	while (time_before(jiffies, timeout)) {
		value = padctl_readl(padctl, XUSB_PADCTL_IOPHY_PLL_S0_CTL1);
		if (value & XUSB_PADCTL_IOPHY_PLL_S0_CTL1_PLL1_LOCKDET) {
			err = 0;
			break;
		}

		usleep_range(100, 200);
	}

	value = padctl_readl(padctl, XUSB_PADCTL_USB3_PAD_MUX);
	value |= XUSB_PADCTL_USB3_PAD_MUX_SATA_IDDQ_DISABLE(lane->index);
	padctl_writel(padctl, value, XUSB_PADCTL_USB3_PAD_MUX);

	return err;
}

static int tegra124_sata_phy_power_off(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	u32 value;

	value = padctl_readl(padctl, XUSB_PADCTL_USB3_PAD_MUX);
	value &= ~XUSB_PADCTL_USB3_PAD_MUX_SATA_IDDQ_DISABLE(lane->index);
	padctl_writel(padctl, value, XUSB_PADCTL_USB3_PAD_MUX);

	value = padctl_readl(padctl, XUSB_PADCTL_IOPHY_PLL_S0_CTL1);
	value &= ~XUSB_PADCTL_IOPHY_PLL_S0_CTL1_PLL_RST;
	padctl_writel(padctl, value, XUSB_PADCTL_IOPHY_PLL_S0_CTL1);

	value = padctl_readl(padctl, XUSB_PADCTL_IOPHY_PLL_S0_CTL1);
	value &= ~XUSB_PADCTL_IOPHY_PLL_S0_CTL1_PLL1_MODE;
	padctl_writel(padctl, value, XUSB_PADCTL_IOPHY_PLL_S0_CTL1);

	value = padctl_readl(padctl, XUSB_PADCTL_IOPHY_PLL_S0_CTL1);
	value |= XUSB_PADCTL_IOPHY_PLL_S0_CTL1_PLL_PWR_OVRD;
	value |= XUSB_PADCTL_IOPHY_PLL_S0_CTL1_PLL_IDDQ;
	padctl_writel(padctl, value, XUSB_PADCTL_IOPHY_PLL_S0_CTL1);

	value = padctl_readl(padctl, XUSB_PADCTL_IOPHY_MISC_PAD_S0_CTL1);
	value |= ~XUSB_PADCTL_IOPHY_MISC_PAD_S0_CTL1_IDDQ_OVRD;
	value |= ~XUSB_PADCTL_IOPHY_MISC_PAD_S0_CTL1_IDDQ;
	padctl_writel(padctl, value, XUSB_PADCTL_IOPHY_MISC_PAD_S0_CTL1);

	return 0;
}

static const struct phy_ops tegra124_sata_phy_ops = {
	.init = tegra124_sata_phy_init,
	.exit = tegra124_sata_phy_exit,
	.power_on = tegra124_sata_phy_power_on,
	.power_off = tegra124_sata_phy_power_off,
	.owner = THIS_MODULE,
};

static struct tegra_xusb_pad *
tegra124_sata_pad_probe(struct tegra_xusb_padctl *padctl,
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
	pad->ops = &tegra124_sata_lane_ops;
	pad->soc = soc;

	err = tegra_xusb_pad_init(pad, padctl, np);
	if (err < 0) {
		kfree(sata);
		goto out;
	}

	err = tegra_xusb_pad_register(pad, &tegra124_sata_phy_ops);
	if (err < 0)
		goto unregister;

	dev_set_drvdata(&pad->dev, pad);

	return pad;

unregister:
	device_unregister(&pad->dev);
out:
	return ERR_PTR(err);
}

static void tegra124_sata_pad_remove(struct tegra_xusb_pad *pad)
{
	struct tegra_xusb_sata_pad *sata = to_sata_pad(pad);

	kfree(sata);
}

static const struct tegra_xusb_pad_ops tegra124_sata_ops = {
	.probe = tegra124_sata_pad_probe,
	.remove = tegra124_sata_pad_remove,
};

static const struct tegra_xusb_pad_soc tegra124_sata_pad = {
	.name = "sata",
	.num_lanes = ARRAY_SIZE(tegra124_sata_lanes),
	.lanes = tegra124_sata_lanes,
	.ops = &tegra124_sata_ops,
};

static const struct tegra_xusb_pad_soc *tegra124_pads[] = {
	&tegra124_usb2_pad,
	&tegra124_ulpi_pad,
	&tegra124_hsic_pad,
	&tegra124_pcie_pad,
	&tegra124_sata_pad,
};

static int tegra124_usb2_port_enable(struct tegra_xusb_port *port)
{
	return 0;
}

static void tegra124_usb2_port_disable(struct tegra_xusb_port *port)
{
}

static struct tegra_xusb_lane *
tegra124_usb2_port_map(struct tegra_xusb_port *port)
{
	return tegra_xusb_find_lane(port->padctl, "usb2", port->index);
}

static const struct tegra_xusb_port_ops tegra124_usb2_port_ops = {
	.release = tegra_xusb_usb2_port_release,
	.remove = tegra_xusb_usb2_port_remove,
	.enable = tegra124_usb2_port_enable,
	.disable = tegra124_usb2_port_disable,
	.map = tegra124_usb2_port_map,
};

static int tegra124_ulpi_port_enable(struct tegra_xusb_port *port)
{
	return 0;
}

static void tegra124_ulpi_port_disable(struct tegra_xusb_port *port)
{
}

static struct tegra_xusb_lane *
tegra124_ulpi_port_map(struct tegra_xusb_port *port)
{
	return tegra_xusb_find_lane(port->padctl, "ulpi", port->index);
}

static const struct tegra_xusb_port_ops tegra124_ulpi_port_ops = {
	.release = tegra_xusb_ulpi_port_release,
	.enable = tegra124_ulpi_port_enable,
	.disable = tegra124_ulpi_port_disable,
	.map = tegra124_ulpi_port_map,
};

static int tegra124_hsic_port_enable(struct tegra_xusb_port *port)
{
	return 0;
}

static void tegra124_hsic_port_disable(struct tegra_xusb_port *port)
{
}

static struct tegra_xusb_lane *
tegra124_hsic_port_map(struct tegra_xusb_port *port)
{
	return tegra_xusb_find_lane(port->padctl, "hsic", port->index);
}

static const struct tegra_xusb_port_ops tegra124_hsic_port_ops = {
	.release = tegra_xusb_hsic_port_release,
	.enable = tegra124_hsic_port_enable,
	.disable = tegra124_hsic_port_disable,
	.map = tegra124_hsic_port_map,
};

static int tegra124_usb3_port_enable(struct tegra_xusb_port *port)
{
	struct tegra_xusb_usb3_port *usb3 = to_usb3_port(port);
	struct tegra_xusb_padctl *padctl = port->padctl;
	struct tegra_xusb_lane *lane = usb3->base.lane;
	unsigned int index = port->index, offset;
	u32 value;

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
	value = padctl_readl(padctl, XUSB_PADCTL_IOPHY_USB3_PADX_CTL2(index));
	value &= ~((XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_WANDER_MASK <<
		    XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_WANDER_SHIFT) |
		   (XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_EQ_MASK <<
		    XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_EQ_SHIFT) |
		   (XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_CDR_CNTL_MASK <<
		    XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_CDR_CNTL_SHIFT));
	value |= (XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_WANDER_VAL <<
		  XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_WANDER_SHIFT) |
		 (XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_CDR_CNTL_VAL <<
		  XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_CDR_CNTL_SHIFT) |
		 (XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_EQ_VAL <<
		  XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_EQ_SHIFT);

	if (usb3->context_saved) {
		value &= ~((XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_EQ_G_MASK <<
			    XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_EQ_G_SHIFT) |
			   (XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_EQ_Z_MASK <<
			    XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_EQ_Z_SHIFT));
		value |= (usb3->ctle_g <<
			  XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_EQ_G_SHIFT) |
			 (usb3->ctle_z <<
			  XUSB_PADCTL_IOPHY_USB3_PAD_CTL2_RX_EQ_Z_SHIFT);
	}

	padctl_writel(padctl, value, XUSB_PADCTL_IOPHY_USB3_PADX_CTL2(index));

	value = XUSB_PADCTL_IOPHY_USB3_PAD_CTL4_DFE_CNTL_VAL;

	if (usb3->context_saved) {
		value &= ~((XUSB_PADCTL_IOPHY_USB3_PAD_CTL4_DFE_CNTL_TAP_MASK <<
			    XUSB_PADCTL_IOPHY_USB3_PAD_CTL4_DFE_CNTL_TAP_SHIFT) |
			   (XUSB_PADCTL_IOPHY_USB3_PAD_CTL4_DFE_CNTL_AMP_MASK <<
			    XUSB_PADCTL_IOPHY_USB3_PAD_CTL4_DFE_CNTL_AMP_SHIFT));
		value |= (usb3->tap1 <<
			  XUSB_PADCTL_IOPHY_USB3_PAD_CTL4_DFE_CNTL_TAP_SHIFT) |
			 (usb3->amp <<
			  XUSB_PADCTL_IOPHY_USB3_PAD_CTL4_DFE_CNTL_AMP_SHIFT);
	}

	padctl_writel(padctl, value, XUSB_PADCTL_IOPHY_USB3_PADX_CTL4(index));

	if (lane->pad == padctl->pcie)
		offset = XUSB_PADCTL_IOPHY_MISC_PAD_PX_CTL2(lane->index);
	else
		offset = XUSB_PADCTL_IOPHY_MISC_PAD_S0_CTL2;

	value = padctl_readl(padctl, offset);
	value &= ~(XUSB_PADCTL_IOPHY_MISC_PAD_CTL2_SPARE_IN_MASK <<
		   XUSB_PADCTL_IOPHY_MISC_PAD_CTL2_SPARE_IN_SHIFT);
	value |= XUSB_PADCTL_IOPHY_MISC_PAD_CTL2_SPARE_IN_VAL <<
		XUSB_PADCTL_IOPHY_MISC_PAD_CTL2_SPARE_IN_SHIFT;
	padctl_writel(padctl, value, offset);

	if (lane->pad == padctl->pcie)
		offset = XUSB_PADCTL_IOPHY_MISC_PAD_PX_CTL5(lane->index);
	else
		offset = XUSB_PADCTL_IOPHY_MISC_PAD_S0_CTL5;

	value = padctl_readl(padctl, offset);
	value |= XUSB_PADCTL_IOPHY_MISC_PAD_CTL5_RX_QEYE_EN;
	padctl_writel(padctl, value, offset);

	/* Enable SATA PHY when SATA lane is used */
	if (lane->pad == padctl->sata) {
		value = padctl_readl(padctl, XUSB_PADCTL_IOPHY_PLL_S0_CTL1);
		value &= ~(XUSB_PADCTL_IOPHY_PLL_S0_CTL1_PLL0_REFCLK_NDIV_MASK <<
			   XUSB_PADCTL_IOPHY_PLL_S0_CTL1_PLL0_REFCLK_NDIV_SHIFT);
		value |= 0x2 <<
			XUSB_PADCTL_IOPHY_PLL_S0_CTL1_PLL0_REFCLK_NDIV_SHIFT;
		padctl_writel(padctl, value, XUSB_PADCTL_IOPHY_PLL_S0_CTL1);

		value = padctl_readl(padctl, XUSB_PADCTL_IOPHY_PLL_S0_CTL2);
		value &= ~((XUSB_PADCTL_IOPHY_PLL_S0_CTL2_XDIGCLK_SEL_MASK <<
			    XUSB_PADCTL_IOPHY_PLL_S0_CTL2_XDIGCLK_SEL_SHIFT) |
			   (XUSB_PADCTL_IOPHY_PLL_S0_CTL2_PLL1_CP_CNTL_MASK <<
			    XUSB_PADCTL_IOPHY_PLL_S0_CTL2_PLL1_CP_CNTL_SHIFT) |
			   (XUSB_PADCTL_IOPHY_PLL_S0_CTL2_PLL0_CP_CNTL_MASK <<
			    XUSB_PADCTL_IOPHY_PLL_S0_CTL2_PLL0_CP_CNTL_SHIFT) |
			   XUSB_PADCTL_IOPHY_PLL_S0_CTL2_TCLKOUT_EN);
		value |= (0x7 <<
			  XUSB_PADCTL_IOPHY_PLL_S0_CTL2_XDIGCLK_SEL_SHIFT) |
			 (0x8 <<
			  XUSB_PADCTL_IOPHY_PLL_S0_CTL2_PLL1_CP_CNTL_SHIFT) |
			 (0x8 <<
			  XUSB_PADCTL_IOPHY_PLL_S0_CTL2_PLL0_CP_CNTL_SHIFT) |
			 XUSB_PADCTL_IOPHY_PLL_S0_CTL2_TXCLKREF_SEL;
		padctl_writel(padctl, value, XUSB_PADCTL_IOPHY_PLL_S0_CTL2);

		value = padctl_readl(padctl, XUSB_PADCTL_IOPHY_PLL_S0_CTL3);
		value &= ~XUSB_PADCTL_IOPHY_PLL_S0_CTL3_RCAL_BYPASS;
		padctl_writel(padctl, value, XUSB_PADCTL_IOPHY_PLL_S0_CTL3);
	}

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM);
	value &= ~XUSB_PADCTL_ELPG_PROGRAM_SSPX_ELPG_VCORE_DOWN(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM);

	usleep_range(100, 200);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM);
	value &= ~XUSB_PADCTL_ELPG_PROGRAM_SSPX_ELPG_CLAMP_EN_EARLY(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM);

	usleep_range(100, 200);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM);
	value &= ~XUSB_PADCTL_ELPG_PROGRAM_SSPX_ELPG_CLAMP_EN(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM);

	return 0;
}

static void tegra124_usb3_port_disable(struct tegra_xusb_port *port)
{
	struct tegra_xusb_padctl *padctl = port->padctl;
	u32 value;

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM);
	value |= XUSB_PADCTL_ELPG_PROGRAM_SSPX_ELPG_CLAMP_EN_EARLY(port->index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM);

	usleep_range(100, 200);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM);
	value |= XUSB_PADCTL_ELPG_PROGRAM_SSPX_ELPG_CLAMP_EN(port->index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM);

	usleep_range(250, 350);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM);
	value |= XUSB_PADCTL_ELPG_PROGRAM_SSPX_ELPG_VCORE_DOWN(port->index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM);

	value = padctl_readl(padctl, XUSB_PADCTL_SS_PORT_MAP);
	value &= ~XUSB_PADCTL_SS_PORT_MAP_PORTX_MAP_MASK(port->index);
	value |= XUSB_PADCTL_SS_PORT_MAP_PORTX_MAP(port->index, 0x7);
	padctl_writel(padctl, value, XUSB_PADCTL_SS_PORT_MAP);
}

static const struct tegra_xusb_lane_map tegra124_usb3_map[] = {
	{ 0, "pcie", 0 },
	{ 1, "pcie", 1 },
	{ 1, "sata", 0 },
	{ 0, NULL,   0 },
};

static struct tegra_xusb_lane *
tegra124_usb3_port_map(struct tegra_xusb_port *port)
{
	return tegra_xusb_port_find_lane(port, tegra124_usb3_map, "usb3-ss");
}

static const struct tegra_xusb_port_ops tegra124_usb3_port_ops = {
	.release = tegra_xusb_usb3_port_release,
	.remove = tegra_xusb_usb3_port_remove,
	.enable = tegra124_usb3_port_enable,
	.disable = tegra124_usb3_port_disable,
	.map = tegra124_usb3_port_map,
};

static int
tegra124_xusb_read_fuse_calibration(struct tegra124_xusb_fuse_calibration *fuse)
{
	unsigned int i;
	int err;
	u32 value;

	err = tegra_fuse_readl(TEGRA_FUSE_SKU_CALIB_0, &value);
	if (err < 0)
		return err;

	for (i = 0; i < ARRAY_SIZE(fuse->hs_curr_level); i++) {
		fuse->hs_curr_level[i] =
			(value >> FUSE_SKU_CALIB_HS_CURR_LEVEL_PADX_SHIFT(i)) &
			FUSE_SKU_CALIB_HS_CURR_LEVEL_PAD_MASK;
	}
	fuse->hs_iref_cap =
		(value >> FUSE_SKU_CALIB_HS_IREF_CAP_SHIFT) &
		FUSE_SKU_CALIB_HS_IREF_CAP_MASK;
	fuse->hs_term_range_adj =
		(value >> FUSE_SKU_CALIB_HS_TERM_RANGE_ADJ_SHIFT) &
		FUSE_SKU_CALIB_HS_TERM_RANGE_ADJ_MASK;
	fuse->hs_squelch_level =
		(value >> FUSE_SKU_CALIB_HS_SQUELCH_LEVEL_SHIFT) &
		FUSE_SKU_CALIB_HS_SQUELCH_LEVEL_MASK;

	return 0;
}

static struct tegra_xusb_padctl *
tegra124_xusb_padctl_probe(struct device *dev,
			   const struct tegra_xusb_padctl_soc *soc)
{
	struct tegra124_xusb_padctl *padctl;
	int err;

	padctl = devm_kzalloc(dev, sizeof(*padctl), GFP_KERNEL);
	if (!padctl)
		return ERR_PTR(-ENOMEM);

	padctl->base.dev = dev;
	padctl->base.soc = soc;

	err = tegra124_xusb_read_fuse_calibration(&padctl->fuse);
	if (err < 0)
		return ERR_PTR(err);

	return &padctl->base;
}

static void tegra124_xusb_padctl_remove(struct tegra_xusb_padctl *padctl)
{
}

static const struct tegra_xusb_padctl_ops tegra124_xusb_padctl_ops = {
	.probe = tegra124_xusb_padctl_probe,
	.remove = tegra124_xusb_padctl_remove,
	.usb3_save_context = tegra124_usb3_save_context,
	.hsic_set_idle = tegra124_hsic_set_idle,
};

static const char * const tegra124_xusb_padctl_supply_names[] = {
	"avdd-pll-utmip",
	"avdd-pll-erefe",
	"avdd-pex-pll",
	"hvdd-pex-pll-e",
};

const struct tegra_xusb_padctl_soc tegra124_xusb_padctl_soc = {
	.num_pads = ARRAY_SIZE(tegra124_pads),
	.pads = tegra124_pads,
	.ports = {
		.usb2 = {
			.ops = &tegra124_usb2_port_ops,
			.count = 3,
		},
		.ulpi = {
			.ops = &tegra124_ulpi_port_ops,
			.count = 1,
		},
		.hsic = {
			.ops = &tegra124_hsic_port_ops,
			.count = 2,
		},
		.usb3 = {
			.ops = &tegra124_usb3_port_ops,
			.count = 2,
		},
	},
	.ops = &tegra124_xusb_padctl_ops,
	.supply_names = tegra124_xusb_padctl_supply_names,
	.num_supplies = ARRAY_SIZE(tegra124_xusb_padctl_supply_names),
};
EXPORT_SYMBOL_GPL(tegra124_xusb_padctl_soc);

MODULE_AUTHOR("Thierry Reding <treding@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra 124 XUSB Pad Controller driver");
MODULE_LICENSE("GPL v2");
