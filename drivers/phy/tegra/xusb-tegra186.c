// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016-2020, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/regulator/consumer.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/slab.h>

#include <soc/tegra/fuse.h>

#include "xusb.h"

/* FUSE USB_CALIB registers */
#define HS_CURR_LEVEL_PADX_SHIFT(x)	((x) ? (11 + (x - 1) * 6) : 0)
#define HS_CURR_LEVEL_PAD_MASK		0x3f
#define HS_TERM_RANGE_ADJ_SHIFT		7
#define HS_TERM_RANGE_ADJ_MASK		0xf
#define HS_SQUELCH_SHIFT		29
#define HS_SQUELCH_MASK			0x7

#define RPD_CTRL_SHIFT			0
#define RPD_CTRL_MASK			0x1f

/* XUSB PADCTL registers */
#define XUSB_PADCTL_USB2_PAD_MUX	0x4
#define  USB2_PORT_SHIFT(x)		((x) * 2)
#define  USB2_PORT_MASK			0x3
#define   PORT_XUSB			1
#define  HSIC_PORT_SHIFT(x)		((x) + 20)
#define  HSIC_PORT_MASK			0x1
#define   PORT_HSIC			0

#define XUSB_PADCTL_USB2_PORT_CAP	0x8
#define XUSB_PADCTL_SS_PORT_CAP		0xc
#define  PORTX_CAP_SHIFT(x)		((x) * 4)
#define  PORT_CAP_MASK			0x3
#define   PORT_CAP_DISABLED		0x0
#define   PORT_CAP_HOST			0x1
#define   PORT_CAP_DEVICE		0x2
#define   PORT_CAP_OTG			0x3

#define XUSB_PADCTL_ELPG_PROGRAM		0x20
#define  USB2_PORT_WAKE_INTERRUPT_ENABLE(x)		BIT(x)
#define  USB2_PORT_WAKEUP_EVENT(x)			BIT((x) +  7)
#define  SS_PORT_WAKE_INTERRUPT_ENABLE(x)		BIT((x) + 14)
#define  SS_PORT_WAKEUP_EVENT(x)			BIT((x) + 21)
#define  USB2_HSIC_PORT_WAKE_INTERRUPT_ENABLE(x)	BIT((x) + 28)
#define  USB2_HSIC_PORT_WAKEUP_EVENT(x)			BIT((x) + 30)
#define  ALL_WAKE_EVENTS						\
	(USB2_PORT_WAKEUP_EVENT(0) | USB2_PORT_WAKEUP_EVENT(1) |	\
	USB2_PORT_WAKEUP_EVENT(2) | SS_PORT_WAKEUP_EVENT(0) |		\
	SS_PORT_WAKEUP_EVENT(1) | SS_PORT_WAKEUP_EVENT(2) |		\
	USB2_HSIC_PORT_WAKEUP_EVENT(0))

#define XUSB_PADCTL_ELPG_PROGRAM_1		0x24
#define  SSPX_ELPG_CLAMP_EN(x)			BIT(0 + (x) * 3)
#define  SSPX_ELPG_CLAMP_EN_EARLY(x)		BIT(1 + (x) * 3)
#define  SSPX_ELPG_VCORE_DOWN(x)		BIT(2 + (x) * 3)
#define XUSB_PADCTL_SS_PORT_CFG			0x2c
#define   PORTX_SPEED_SUPPORT_SHIFT(x)		((x) * 4)
#define   PORTX_SPEED_SUPPORT_MASK		(0x3)
#define     PORT_SPEED_SUPPORT_GEN1		(0x0)

#define XUSB_PADCTL_USB2_OTG_PADX_CTL0(x)	(0x88 + (x) * 0x40)
#define  HS_CURR_LEVEL(x)			((x) & 0x3f)
#define  TERM_SEL				BIT(25)
#define  USB2_OTG_PD				BIT(26)
#define  USB2_OTG_PD2				BIT(27)
#define  USB2_OTG_PD2_OVRD_EN			BIT(28)
#define  USB2_OTG_PD_ZI				BIT(29)

#define XUSB_PADCTL_USB2_OTG_PADX_CTL1(x)	(0x8c + (x) * 0x40)
#define  USB2_OTG_PD_DR				BIT(2)
#define  TERM_RANGE_ADJ(x)			(((x) & 0xf) << 3)
#define  RPD_CTRL(x)				(((x) & 0x1f) << 26)

#define XUSB_PADCTL_USB2_BIAS_PAD_CTL0		0x284
#define  BIAS_PAD_PD				BIT(11)
#define  HS_SQUELCH_LEVEL(x)			(((x) & 0x7) << 0)

#define XUSB_PADCTL_USB2_BIAS_PAD_CTL1		0x288
#define  USB2_TRK_START_TIMER(x)		(((x) & 0x7f) << 12)
#define  USB2_TRK_DONE_RESET_TIMER(x)		(((x) & 0x7f) << 19)
#define  USB2_PD_TRK				BIT(26)

#define XUSB_PADCTL_HSIC_PADX_CTL0(x)		(0x300 + (x) * 0x20)
#define  HSIC_PD_TX_DATA0			BIT(1)
#define  HSIC_PD_TX_STROBE			BIT(3)
#define  HSIC_PD_RX_DATA0			BIT(4)
#define  HSIC_PD_RX_STROBE			BIT(6)
#define  HSIC_PD_ZI_DATA0			BIT(7)
#define  HSIC_PD_ZI_STROBE			BIT(9)
#define  HSIC_RPD_DATA0				BIT(13)
#define  HSIC_RPD_STROBE			BIT(15)
#define  HSIC_RPU_DATA0				BIT(16)
#define  HSIC_RPU_STROBE			BIT(18)

#define XUSB_PADCTL_HSIC_PAD_TRK_CTL0		0x340
#define  HSIC_TRK_START_TIMER(x)		(((x) & 0x7f) << 5)
#define  HSIC_TRK_DONE_RESET_TIMER(x)		(((x) & 0x7f) << 12)
#define  HSIC_PD_TRK				BIT(19)

#define USB2_VBUS_ID				0x360
#define  VBUS_OVERRIDE				BIT(14)
#define  ID_OVERRIDE(x)				(((x) & 0xf) << 18)
#define  ID_OVERRIDE_FLOATING			ID_OVERRIDE(8)
#define  ID_OVERRIDE_GROUNDED			ID_OVERRIDE(0)

/* XUSB AO registers */
#define XUSB_AO_USB_DEBOUNCE_DEL		(0x4)
#define   UHSIC_LINE_DEB_CNT(x)			(((x) & 0xf) << 4)
#define   UTMIP_LINE_DEB_CNT(x)			((x) & 0xf)

#define XUSB_AO_UTMIP_TRIGGERS(x)		(0x40 + (x) * 4)
#define   CLR_WALK_PTR				BIT(0)
#define   CAP_CFG				BIT(1)
#define   CLR_WAKE_ALARM			BIT(3)

#define XUSB_AO_UHSIC_TRIGGERS(x)		(0x60 + (x) * 4)
#define   HSIC_CLR_WALK_PTR			BIT(0)
#define   HSIC_CLR_WAKE_ALARM			BIT(3)
#define   HSIC_CAP_CFG				BIT(4)

#define XUSB_AO_UTMIP_SAVED_STATE(x)		(0x70 + (x) * 4)
#define   SPEED(x)				((x) & 0x3)
#define     UTMI_HS				SPEED(0)
#define     UTMI_FS				SPEED(1)
#define     UTMI_LS				SPEED(2)
#define     UTMI_RST				SPEED(3)

#define XUSB_AO_UHSIC_SAVED_STATE(x)		(0x90 + (x) * 4)
#define   MODE(x)				((x) & 0x1)
#define   MODE_HS				MODE(0)
#define   MODE_RST				MODE(1)

#define XUSB_AO_UTMIP_SLEEPWALK_CFG(x)		(0xd0 + (x) * 4)
#define XUSB_AO_UHSIC_SLEEPWALK_CFG(x)		(0xf0 + (x) * 4)
#define   FAKE_USBOP_VAL			BIT(0)
#define   FAKE_USBON_VAL			BIT(1)
#define   FAKE_USBOP_EN				BIT(2)
#define   FAKE_USBON_EN				BIT(3)
#define   FAKE_STROBE_VAL			BIT(0)
#define   FAKE_DATA_VAL				BIT(1)
#define   FAKE_STROBE_EN			BIT(2)
#define   FAKE_DATA_EN				BIT(3)
#define   WAKE_WALK_EN				BIT(14)
#define   MASTER_ENABLE				BIT(15)
#define   LINEVAL_WALK_EN			BIT(16)
#define   WAKE_VAL(x)				(((x) & 0xf) << 17)
#define     WAKE_VAL_NONE			WAKE_VAL(12)
#define     WAKE_VAL_ANY			WAKE_VAL(15)
#define     WAKE_VAL_DS10			WAKE_VAL(2)
#define   LINE_WAKEUP_EN			BIT(21)
#define   MASTER_CFG_SEL			BIT(22)

#define XUSB_AO_UTMIP_SLEEPWALK(x)		(0x100 + (x) * 4)
/* phase A */
#define   USBOP_RPD_A				BIT(0)
#define   USBON_RPD_A				BIT(1)
#define   AP_A					BIT(4)
#define   AN_A					BIT(5)
#define   HIGHZ_A				BIT(6)
/* phase B */
#define   USBOP_RPD_B				BIT(8)
#define   USBON_RPD_B				BIT(9)
#define   AP_B					BIT(12)
#define   AN_B					BIT(13)
#define   HIGHZ_B				BIT(14)
/* phase C */
#define   USBOP_RPD_C				BIT(16)
#define   USBON_RPD_C				BIT(17)
#define   AP_C					BIT(20)
#define   AN_C					BIT(21)
#define   HIGHZ_C				BIT(22)
/* phase D */
#define   USBOP_RPD_D				BIT(24)
#define   USBON_RPD_D				BIT(25)
#define   AP_D					BIT(28)
#define   AN_D					BIT(29)
#define   HIGHZ_D				BIT(30)

#define XUSB_AO_UHSIC_SLEEPWALK(x)		(0x120 + (x) * 4)
/* phase A */
#define   RPD_STROBE_A				BIT(0)
#define   RPD_DATA0_A				BIT(1)
#define   RPU_STROBE_A				BIT(2)
#define   RPU_DATA0_A				BIT(3)
/* phase B */
#define   RPD_STROBE_B				BIT(8)
#define   RPD_DATA0_B				BIT(9)
#define   RPU_STROBE_B				BIT(10)
#define   RPU_DATA0_B				BIT(11)
/* phase C */
#define   RPD_STROBE_C				BIT(16)
#define   RPD_DATA0_C				BIT(17)
#define   RPU_STROBE_C				BIT(18)
#define   RPU_DATA0_C				BIT(19)
/* phase D */
#define   RPD_STROBE_D				BIT(24)
#define   RPD_DATA0_D				BIT(25)
#define   RPU_STROBE_D				BIT(26)
#define   RPU_DATA0_D				BIT(27)

#define XUSB_AO_UTMIP_PAD_CFG(x)		(0x130 + (x) * 4)
#define   FSLS_USE_XUSB_AO			BIT(3)
#define   TRK_CTRL_USE_XUSB_AO			BIT(4)
#define   RPD_CTRL_USE_XUSB_AO			BIT(5)
#define   RPU_USE_XUSB_AO			BIT(6)
#define   VREG_USE_XUSB_AO			BIT(7)
#define   USBOP_VAL_PD				BIT(8)
#define   USBON_VAL_PD				BIT(9)
#define   E_DPD_OVRD_EN				BIT(10)
#define   E_DPD_OVRD_VAL			BIT(11)

#define XUSB_AO_UHSIC_PAD_CFG(x)		(0x150 + (x) * 4)
#define   STROBE_VAL_PD				BIT(0)
#define   DATA0_VAL_PD				BIT(1)
#define   USE_XUSB_AO				BIT(4)

#define TEGRA186_LANE(_name, _offset, _shift, _mask, _type)		\
	{								\
		.name = _name,						\
		.offset = _offset,					\
		.shift = _shift,					\
		.mask = _mask,						\
		.num_funcs = ARRAY_SIZE(tegra186_##_type##_functions),	\
		.funcs = tegra186_##_type##_functions,			\
	}

struct tegra_xusb_fuse_calibration {
	u32 *hs_curr_level;
	u32 hs_squelch;
	u32 hs_term_range_adj;
	u32 rpd_ctrl;
};

struct tegra186_xusb_padctl_context {
	u32 vbus_id;
	u32 usb2_pad_mux;
	u32 usb2_port_cap;
	u32 ss_port_cap;
};

struct tegra186_xusb_padctl {
	struct tegra_xusb_padctl base;
	void __iomem *ao_regs;

	struct tegra_xusb_fuse_calibration calib;

	/* UTMI bias and tracking */
	struct clk *usb2_trk_clk;
	unsigned int bias_pad_enable;

	/* padctl context */
	struct tegra186_xusb_padctl_context context;
};

static inline void ao_writel(struct tegra186_xusb_padctl *priv, u32 value, unsigned int offset)
{
	writel(value, priv->ao_regs + offset);
}

static inline u32 ao_readl(struct tegra186_xusb_padctl *priv, unsigned int offset)
{
	return readl(priv->ao_regs + offset);
}

static inline struct tegra186_xusb_padctl *
to_tegra186_xusb_padctl(struct tegra_xusb_padctl *padctl)
{
	return container_of(padctl, struct tegra186_xusb_padctl, base);
}

/* USB 2.0 UTMI PHY support */
static struct tegra_xusb_lane *
tegra186_usb2_lane_probe(struct tegra_xusb_pad *pad, struct device_node *np,
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

static void tegra186_usb2_lane_remove(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_usb2_lane *usb2 = to_usb2_lane(lane);

	kfree(usb2);
}

static int tegra186_utmi_enable_phy_sleepwalk(struct tegra_xusb_lane *lane,
					      enum usb_device_speed speed)
{
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	struct tegra186_xusb_padctl *priv = to_tegra186_xusb_padctl(padctl);
	unsigned int index = lane->index;
	u32 value;

	mutex_lock(&padctl->lock);

	/* ensure sleepwalk logic is disabled */
	value = ao_readl(priv, XUSB_AO_UTMIP_SLEEPWALK_CFG(index));
	value &= ~MASTER_ENABLE;
	ao_writel(priv, value, XUSB_AO_UTMIP_SLEEPWALK_CFG(index));

	/* ensure sleepwalk logics are in low power mode */
	value = ao_readl(priv, XUSB_AO_UTMIP_SLEEPWALK_CFG(index));
	value |= MASTER_CFG_SEL;
	ao_writel(priv, value, XUSB_AO_UTMIP_SLEEPWALK_CFG(index));

	/* set debounce time */
	value = ao_readl(priv, XUSB_AO_USB_DEBOUNCE_DEL);
	value &= ~UTMIP_LINE_DEB_CNT(~0);
	value |= UTMIP_LINE_DEB_CNT(1);
	ao_writel(priv, value, XUSB_AO_USB_DEBOUNCE_DEL);

	/* ensure fake events of sleepwalk logic are desiabled */
	value = ao_readl(priv, XUSB_AO_UTMIP_SLEEPWALK_CFG(index));
	value &= ~(FAKE_USBOP_VAL | FAKE_USBON_VAL |
		FAKE_USBOP_EN | FAKE_USBON_EN);
	ao_writel(priv, value, XUSB_AO_UTMIP_SLEEPWALK_CFG(index));

	/* ensure wake events of sleepwalk logic are not latched */
	value = ao_readl(priv, XUSB_AO_UTMIP_SLEEPWALK_CFG(index));
	value &= ~LINE_WAKEUP_EN;
	ao_writel(priv, value, XUSB_AO_UTMIP_SLEEPWALK_CFG(index));

	/* disable wake event triggers of sleepwalk logic */
	value = ao_readl(priv, XUSB_AO_UTMIP_SLEEPWALK_CFG(index));
	value &= ~WAKE_VAL(~0);
	value |= WAKE_VAL_NONE;
	ao_writel(priv, value, XUSB_AO_UTMIP_SLEEPWALK_CFG(index));

	/* power down the line state detectors of the pad */
	value = ao_readl(priv, XUSB_AO_UTMIP_PAD_CFG(index));
	value |= (USBOP_VAL_PD | USBON_VAL_PD);
	ao_writel(priv, value, XUSB_AO_UTMIP_PAD_CFG(index));

	/* save state per speed */
	value = ao_readl(priv, XUSB_AO_UTMIP_SAVED_STATE(index));
	value &= ~SPEED(~0);

	switch (speed) {
	case USB_SPEED_HIGH:
		value |= UTMI_HS;
		break;

	case USB_SPEED_FULL:
		value |= UTMI_FS;
		break;

	case USB_SPEED_LOW:
		value |= UTMI_LS;
		break;

	default:
		value |= UTMI_RST;
		break;
	}

	ao_writel(priv, value, XUSB_AO_UTMIP_SAVED_STATE(index));

	/* enable the trigger of the sleepwalk logic */
	value = ao_readl(priv, XUSB_AO_UTMIP_SLEEPWALK_CFG(index));
	value |= LINEVAL_WALK_EN;
	value &= ~WAKE_WALK_EN;
	ao_writel(priv, value, XUSB_AO_UTMIP_SLEEPWALK_CFG(index));

	/* reset the walk pointer and clear the alarm of the sleepwalk logic,
	 * as well as capture the configuration of the USB2.0 pad
	 */
	value = ao_readl(priv, XUSB_AO_UTMIP_TRIGGERS(index));
	value |= (CLR_WALK_PTR | CLR_WAKE_ALARM | CAP_CFG);
	ao_writel(priv, value, XUSB_AO_UTMIP_TRIGGERS(index));

	/* setup the pull-ups and pull-downs of the signals during the four
	 * stages of sleepwalk.
	 * if device is connected, program sleepwalk logic to maintain a J and
	 * keep driving K upon seeing remote wake.
	 */
	value = USBOP_RPD_A | USBOP_RPD_B | USBOP_RPD_C | USBOP_RPD_D;
	value |= USBON_RPD_A | USBON_RPD_B | USBON_RPD_C | USBON_RPD_D;

	switch (speed) {
	case USB_SPEED_HIGH:
	case USB_SPEED_FULL:
		/* J state: D+/D- = high/low, K state: D+/D- = low/high */
		value |= HIGHZ_A;
		value |= AP_A;
		value |= AN_B | AN_C | AN_D;
		break;

	case USB_SPEED_LOW:
		/* J state: D+/D- = low/high, K state: D+/D- = high/low */
		value |= HIGHZ_A;
		value |= AN_A;
		value |= AP_B | AP_C | AP_D;
		break;

	default:
		value |= HIGHZ_A | HIGHZ_B | HIGHZ_C | HIGHZ_D;
		break;
	}

	ao_writel(priv, value, XUSB_AO_UTMIP_SLEEPWALK(index));

	/* power up the line state detectors of the pad */
	value = ao_readl(priv, XUSB_AO_UTMIP_PAD_CFG(index));
	value &= ~(USBOP_VAL_PD | USBON_VAL_PD);
	ao_writel(priv, value, XUSB_AO_UTMIP_PAD_CFG(index));

	usleep_range(150, 200);

	/* switch the electric control of the USB2.0 pad to XUSB_AO */
	value = ao_readl(priv, XUSB_AO_UTMIP_PAD_CFG(index));
	value |= FSLS_USE_XUSB_AO | TRK_CTRL_USE_XUSB_AO | RPD_CTRL_USE_XUSB_AO |
		 RPU_USE_XUSB_AO | VREG_USE_XUSB_AO;
	ao_writel(priv, value, XUSB_AO_UTMIP_PAD_CFG(index));

	/* set the wake signaling trigger events */
	value = ao_readl(priv, XUSB_AO_UTMIP_SLEEPWALK_CFG(index));
	value &= ~WAKE_VAL(~0);
	value |= WAKE_VAL_ANY;
	ao_writel(priv, value, XUSB_AO_UTMIP_SLEEPWALK_CFG(index));

	/* enable the wake detection */
	value = ao_readl(priv, XUSB_AO_UTMIP_SLEEPWALK_CFG(index));
	value |= MASTER_ENABLE | LINE_WAKEUP_EN;
	ao_writel(priv, value, XUSB_AO_UTMIP_SLEEPWALK_CFG(index));

	mutex_unlock(&padctl->lock);

	return 0;
}

static int tegra186_utmi_disable_phy_sleepwalk(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	struct tegra186_xusb_padctl *priv = to_tegra186_xusb_padctl(padctl);
	unsigned int index = lane->index;
	u32 value;

	mutex_lock(&padctl->lock);

	/* disable the wake detection */
	value = ao_readl(priv, XUSB_AO_UTMIP_SLEEPWALK_CFG(index));
	value &= ~(MASTER_ENABLE | LINE_WAKEUP_EN);
	ao_writel(priv, value, XUSB_AO_UTMIP_SLEEPWALK_CFG(index));

	/* switch the electric control of the USB2.0 pad to XUSB vcore logic */
	value = ao_readl(priv, XUSB_AO_UTMIP_PAD_CFG(index));
	value &= ~(FSLS_USE_XUSB_AO | TRK_CTRL_USE_XUSB_AO | RPD_CTRL_USE_XUSB_AO |
		   RPU_USE_XUSB_AO | VREG_USE_XUSB_AO);
	ao_writel(priv, value, XUSB_AO_UTMIP_PAD_CFG(index));

	/* disable wake event triggers of sleepwalk logic */
	value = ao_readl(priv, XUSB_AO_UTMIP_SLEEPWALK_CFG(index));
	value &= ~WAKE_VAL(~0);
	value |= WAKE_VAL_NONE;
	ao_writel(priv, value, XUSB_AO_UTMIP_SLEEPWALK_CFG(index));

	/* power down the line state detectors of the port */
	value = ao_readl(priv, XUSB_AO_UTMIP_PAD_CFG(index));
	value |= USBOP_VAL_PD | USBON_VAL_PD;
	ao_writel(priv, value, XUSB_AO_UTMIP_PAD_CFG(index));

	/* clear alarm of the sleepwalk logic */
	value = ao_readl(priv, XUSB_AO_UTMIP_TRIGGERS(index));
	value |= CLR_WAKE_ALARM;
	ao_writel(priv, value, XUSB_AO_UTMIP_TRIGGERS(index));

	mutex_unlock(&padctl->lock);

	return 0;
}

static int tegra186_utmi_enable_phy_wake(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	unsigned int index = lane->index;
	u32 value;

	mutex_lock(&padctl->lock);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM);
	value &= ~ALL_WAKE_EVENTS;
	value |= USB2_PORT_WAKEUP_EVENT(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM);

	usleep_range(10, 20);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM);
	value &= ~ALL_WAKE_EVENTS;
	value |= USB2_PORT_WAKE_INTERRUPT_ENABLE(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM);

	mutex_unlock(&padctl->lock);

	return 0;
}

static int tegra186_utmi_disable_phy_wake(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	unsigned int index = lane->index;
	u32 value;

	mutex_lock(&padctl->lock);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM);
	value &= ~ALL_WAKE_EVENTS;
	value &= ~USB2_PORT_WAKE_INTERRUPT_ENABLE(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM);

	usleep_range(10, 20);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM);
	value &= ~ALL_WAKE_EVENTS;
	value |= USB2_PORT_WAKEUP_EVENT(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM);

	mutex_unlock(&padctl->lock);

	return 0;
}

static bool tegra186_utmi_phy_remote_wake_detected(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	unsigned int index = lane->index;
	u32 value;

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM);
	if ((value & USB2_PORT_WAKE_INTERRUPT_ENABLE(index)) &&
	    (value & USB2_PORT_WAKEUP_EVENT(index)))
		return true;

	return false;
}

static const struct tegra_xusb_lane_ops tegra186_usb2_lane_ops = {
	.probe = tegra186_usb2_lane_probe,
	.remove = tegra186_usb2_lane_remove,
	.enable_phy_sleepwalk = tegra186_utmi_enable_phy_sleepwalk,
	.disable_phy_sleepwalk = tegra186_utmi_disable_phy_sleepwalk,
	.enable_phy_wake = tegra186_utmi_enable_phy_wake,
	.disable_phy_wake = tegra186_utmi_disable_phy_wake,
	.remote_wake_detected = tegra186_utmi_phy_remote_wake_detected,
};

static void tegra186_utmi_bias_pad_power_on(struct tegra_xusb_padctl *padctl)
{
	struct tegra186_xusb_padctl *priv = to_tegra186_xusb_padctl(padctl);
	struct device *dev = padctl->dev;
	u32 value;
	int err;

	mutex_lock(&padctl->lock);

	if (priv->bias_pad_enable++ > 0) {
		mutex_unlock(&padctl->lock);
		return;
	}

	err = clk_prepare_enable(priv->usb2_trk_clk);
	if (err < 0)
		dev_warn(dev, "failed to enable USB2 trk clock: %d\n", err);

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_BIAS_PAD_CTL1);
	value &= ~USB2_TRK_START_TIMER(~0);
	value |= USB2_TRK_START_TIMER(0x1e);
	value &= ~USB2_TRK_DONE_RESET_TIMER(~0);
	value |= USB2_TRK_DONE_RESET_TIMER(0xa);
	padctl_writel(padctl, value, XUSB_PADCTL_USB2_BIAS_PAD_CTL1);

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_BIAS_PAD_CTL0);
	value &= ~BIAS_PAD_PD;
	value &= ~HS_SQUELCH_LEVEL(~0);
	value |= HS_SQUELCH_LEVEL(priv->calib.hs_squelch);
	padctl_writel(padctl, value, XUSB_PADCTL_USB2_BIAS_PAD_CTL0);

	udelay(1);

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_BIAS_PAD_CTL1);
	value &= ~USB2_PD_TRK;
	padctl_writel(padctl, value, XUSB_PADCTL_USB2_BIAS_PAD_CTL1);

	mutex_unlock(&padctl->lock);
}

static void tegra186_utmi_bias_pad_power_off(struct tegra_xusb_padctl *padctl)
{
	struct tegra186_xusb_padctl *priv = to_tegra186_xusb_padctl(padctl);
	u32 value;

	mutex_lock(&padctl->lock);

	if (WARN_ON(priv->bias_pad_enable == 0)) {
		mutex_unlock(&padctl->lock);
		return;
	}

	if (--priv->bias_pad_enable > 0) {
		mutex_unlock(&padctl->lock);
		return;
	}

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_BIAS_PAD_CTL1);
	value |= USB2_PD_TRK;
	padctl_writel(padctl, value, XUSB_PADCTL_USB2_BIAS_PAD_CTL1);

	clk_disable_unprepare(priv->usb2_trk_clk);

	mutex_unlock(&padctl->lock);
}

static void tegra_phy_xusb_utmi_pad_power_on(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	struct tegra_xusb_usb2_port *port;
	struct device *dev = padctl->dev;
	unsigned int index = lane->index;
	u32 value;

	if (!phy)
		return;

	port = tegra_xusb_find_usb2_port(padctl, index);
	if (!port) {
		dev_err(dev, "no port found for USB2 lane %u\n", index);
		return;
	}

	tegra186_utmi_bias_pad_power_on(padctl);

	udelay(2);

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_OTG_PADX_CTL0(index));
	value &= ~USB2_OTG_PD;
	padctl_writel(padctl, value, XUSB_PADCTL_USB2_OTG_PADX_CTL0(index));

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_OTG_PADX_CTL1(index));
	value &= ~USB2_OTG_PD_DR;
	padctl_writel(padctl, value, XUSB_PADCTL_USB2_OTG_PADX_CTL1(index));
}

static void tegra_phy_xusb_utmi_pad_power_down(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	unsigned int index = lane->index;
	u32 value;

	if (!phy)
		return;

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_OTG_PADX_CTL0(index));
	value |= USB2_OTG_PD;
	padctl_writel(padctl, value, XUSB_PADCTL_USB2_OTG_PADX_CTL0(index));

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_OTG_PADX_CTL1(index));
	value |= USB2_OTG_PD_DR;
	padctl_writel(padctl, value, XUSB_PADCTL_USB2_OTG_PADX_CTL1(index));

	udelay(2);

	tegra186_utmi_bias_pad_power_off(padctl);
}

static int tegra186_xusb_padctl_vbus_override(struct tegra_xusb_padctl *padctl,
					       bool status)
{
	u32 value;

	dev_dbg(padctl->dev, "%s vbus override\n", status ? "set" : "clear");

	value = padctl_readl(padctl, USB2_VBUS_ID);

	if (status) {
		value |= VBUS_OVERRIDE;
		value &= ~ID_OVERRIDE(~0);
		value |= ID_OVERRIDE_FLOATING;
	} else {
		value &= ~VBUS_OVERRIDE;
	}

	padctl_writel(padctl, value, USB2_VBUS_ID);

	return 0;
}

static int tegra186_xusb_padctl_id_override(struct tegra_xusb_padctl *padctl,
					    bool status)
{
	u32 value;

	dev_dbg(padctl->dev, "%s id override\n", status ? "set" : "clear");

	value = padctl_readl(padctl, USB2_VBUS_ID);

	if (status) {
		if (value & VBUS_OVERRIDE) {
			value &= ~VBUS_OVERRIDE;
			padctl_writel(padctl, value, USB2_VBUS_ID);
			usleep_range(1000, 2000);

			value = padctl_readl(padctl, USB2_VBUS_ID);
		}

		value &= ~ID_OVERRIDE(~0);
		value |= ID_OVERRIDE_GROUNDED;
	} else {
		value &= ~ID_OVERRIDE(~0);
		value |= ID_OVERRIDE_FLOATING;
	}

	padctl_writel(padctl, value, USB2_VBUS_ID);

	return 0;
}

static int tegra186_utmi_phy_set_mode(struct phy *phy, enum phy_mode mode,
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
			tegra186_xusb_padctl_id_override(padctl, true);

			err = regulator_enable(port->supply);
		} else if (submode == USB_ROLE_DEVICE) {
			tegra186_xusb_padctl_vbus_override(padctl, true);
		} else if (submode == USB_ROLE_NONE) {
			/*
			 * When port is peripheral only or role transitions to
			 * USB_ROLE_NONE from USB_ROLE_DEVICE, regulator is not
			 * enabled.
			 */
			if (regulator_is_enabled(port->supply))
				regulator_disable(port->supply);

			tegra186_xusb_padctl_id_override(padctl, false);
			tegra186_xusb_padctl_vbus_override(padctl, false);
		}
	}

	mutex_unlock(&padctl->lock);

	return err;
}

static int tegra186_utmi_phy_power_on(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_usb2_lane *usb2 = to_usb2_lane(lane);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	struct tegra186_xusb_padctl *priv = to_tegra186_xusb_padctl(padctl);
	struct tegra_xusb_usb2_port *port;
	unsigned int index = lane->index;
	struct device *dev = padctl->dev;
	u32 value;

	port = tegra_xusb_find_usb2_port(padctl, index);
	if (!port) {
		dev_err(dev, "no port found for USB2 lane %u\n", index);
		return -ENODEV;
	}

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_PAD_MUX);
	value &= ~(USB2_PORT_MASK << USB2_PORT_SHIFT(index));
	value |= (PORT_XUSB << USB2_PORT_SHIFT(index));
	padctl_writel(padctl, value, XUSB_PADCTL_USB2_PAD_MUX);

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_PORT_CAP);
	value &= ~(PORT_CAP_MASK << PORTX_CAP_SHIFT(index));

	if (port->mode == USB_DR_MODE_UNKNOWN)
		value |= (PORT_CAP_DISABLED << PORTX_CAP_SHIFT(index));
	else if (port->mode == USB_DR_MODE_PERIPHERAL)
		value |= (PORT_CAP_DEVICE << PORTX_CAP_SHIFT(index));
	else if (port->mode == USB_DR_MODE_HOST)
		value |= (PORT_CAP_HOST << PORTX_CAP_SHIFT(index));
	else if (port->mode == USB_DR_MODE_OTG)
		value |= (PORT_CAP_OTG << PORTX_CAP_SHIFT(index));

	padctl_writel(padctl, value, XUSB_PADCTL_USB2_PORT_CAP);

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_OTG_PADX_CTL0(index));
	value &= ~USB2_OTG_PD_ZI;
	value |= TERM_SEL;
	value &= ~HS_CURR_LEVEL(~0);

	if (usb2->hs_curr_level_offset) {
		int hs_current_level;

		hs_current_level = (int)priv->calib.hs_curr_level[index] +
						usb2->hs_curr_level_offset;

		if (hs_current_level < 0)
			hs_current_level = 0;
		if (hs_current_level > 0x3f)
			hs_current_level = 0x3f;

		value |= HS_CURR_LEVEL(hs_current_level);
	} else {
		value |= HS_CURR_LEVEL(priv->calib.hs_curr_level[index]);
	}

	padctl_writel(padctl, value, XUSB_PADCTL_USB2_OTG_PADX_CTL0(index));

	value = padctl_readl(padctl, XUSB_PADCTL_USB2_OTG_PADX_CTL1(index));
	value &= ~TERM_RANGE_ADJ(~0);
	value |= TERM_RANGE_ADJ(priv->calib.hs_term_range_adj);
	value &= ~RPD_CTRL(~0);
	value |= RPD_CTRL(priv->calib.rpd_ctrl);
	padctl_writel(padctl, value, XUSB_PADCTL_USB2_OTG_PADX_CTL1(index));

	/* TODO: pad power saving */
	tegra_phy_xusb_utmi_pad_power_on(phy);
	return 0;
}

static int tegra186_utmi_phy_power_off(struct phy *phy)
{
	/* TODO: pad power saving */
	tegra_phy_xusb_utmi_pad_power_down(phy);

	return 0;
}

static int tegra186_utmi_phy_init(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	struct tegra_xusb_usb2_port *port;
	unsigned int index = lane->index;
	struct device *dev = padctl->dev;
	int err;

	port = tegra_xusb_find_usb2_port(padctl, index);
	if (!port) {
		dev_err(dev, "no port found for USB2 lane %u\n", index);
		return -ENODEV;
	}

	if (port->supply && port->mode == USB_DR_MODE_HOST) {
		err = regulator_enable(port->supply);
		if (err) {
			dev_err(dev, "failed to enable port %u VBUS: %d\n",
				index, err);
			return err;
		}
	}

	return 0;
}

static int tegra186_utmi_phy_exit(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	struct tegra_xusb_usb2_port *port;
	unsigned int index = lane->index;
	struct device *dev = padctl->dev;
	int err;

	port = tegra_xusb_find_usb2_port(padctl, index);
	if (!port) {
		dev_err(dev, "no port found for USB2 lane %u\n", index);
		return -ENODEV;
	}

	if (port->supply && port->mode == USB_DR_MODE_HOST) {
		err = regulator_disable(port->supply);
		if (err) {
			dev_err(dev, "failed to disable port %u VBUS: %d\n",
				index, err);
			return err;
		}
	}

	return 0;
}

static const struct phy_ops utmi_phy_ops = {
	.init = tegra186_utmi_phy_init,
	.exit = tegra186_utmi_phy_exit,
	.power_on = tegra186_utmi_phy_power_on,
	.power_off = tegra186_utmi_phy_power_off,
	.set_mode = tegra186_utmi_phy_set_mode,
	.owner = THIS_MODULE,
};

static struct tegra_xusb_pad *
tegra186_usb2_pad_probe(struct tegra_xusb_padctl *padctl,
			const struct tegra_xusb_pad_soc *soc,
			struct device_node *np)
{
	struct tegra186_xusb_padctl *priv = to_tegra186_xusb_padctl(padctl);
	struct tegra_xusb_usb2_pad *usb2;
	struct tegra_xusb_pad *pad;
	int err;

	usb2 = kzalloc(sizeof(*usb2), GFP_KERNEL);
	if (!usb2)
		return ERR_PTR(-ENOMEM);

	pad = &usb2->base;
	pad->ops = &tegra186_usb2_lane_ops;
	pad->soc = soc;

	err = tegra_xusb_pad_init(pad, padctl, np);
	if (err < 0) {
		kfree(usb2);
		goto out;
	}

	priv->usb2_trk_clk = devm_clk_get(&pad->dev, "trk");
	if (IS_ERR(priv->usb2_trk_clk)) {
		err = PTR_ERR(priv->usb2_trk_clk);
		dev_dbg(&pad->dev, "failed to get usb2 trk clock: %d\n", err);
		goto unregister;
	}

	err = tegra_xusb_pad_register(pad, &utmi_phy_ops);
	if (err < 0)
		goto unregister;

	dev_set_drvdata(&pad->dev, pad);

	return pad;

unregister:
	device_unregister(&pad->dev);
out:
	return ERR_PTR(err);
}

static void tegra186_usb2_pad_remove(struct tegra_xusb_pad *pad)
{
	struct tegra_xusb_usb2_pad *usb2 = to_usb2_pad(pad);

	kfree(usb2);
}

static const struct tegra_xusb_pad_ops tegra186_usb2_pad_ops = {
	.probe = tegra186_usb2_pad_probe,
	.remove = tegra186_usb2_pad_remove,
};

static const char * const tegra186_usb2_functions[] = {
	"xusb",
};

static int tegra186_usb2_port_enable(struct tegra_xusb_port *port)
{
	return 0;
}

static void tegra186_usb2_port_disable(struct tegra_xusb_port *port)
{
}

static struct tegra_xusb_lane *
tegra186_usb2_port_map(struct tegra_xusb_port *port)
{
	return tegra_xusb_find_lane(port->padctl, "usb2", port->index);
}

static const struct tegra_xusb_port_ops tegra186_usb2_port_ops = {
	.release = tegra_xusb_usb2_port_release,
	.remove = tegra_xusb_usb2_port_remove,
	.enable = tegra186_usb2_port_enable,
	.disable = tegra186_usb2_port_disable,
	.map = tegra186_usb2_port_map,
};

/* SuperSpeed PHY support */
static struct tegra_xusb_lane *
tegra186_usb3_lane_probe(struct tegra_xusb_pad *pad, struct device_node *np,
			 unsigned int index)
{
	struct tegra_xusb_usb3_lane *usb3;
	int err;

	usb3 = kzalloc(sizeof(*usb3), GFP_KERNEL);
	if (!usb3)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&usb3->base.list);
	usb3->base.soc = &pad->soc->lanes[index];
	usb3->base.index = index;
	usb3->base.pad = pad;
	usb3->base.np = np;

	err = tegra_xusb_lane_parse_dt(&usb3->base, np);
	if (err < 0) {
		kfree(usb3);
		return ERR_PTR(err);
	}

	return &usb3->base;
}

static void tegra186_usb3_lane_remove(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_usb3_lane *usb3 = to_usb3_lane(lane);

	kfree(usb3);
}

static int tegra186_usb3_enable_phy_sleepwalk(struct tegra_xusb_lane *lane,
					      enum usb_device_speed speed)
{
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	unsigned int index = lane->index;
	u32 value;

	mutex_lock(&padctl->lock);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM_1);
	value |= SSPX_ELPG_CLAMP_EN_EARLY(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM_1);

	usleep_range(100, 200);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM_1);
	value |= SSPX_ELPG_CLAMP_EN(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM_1);

	usleep_range(250, 350);

	mutex_unlock(&padctl->lock);

	return 0;
}

static int tegra186_usb3_disable_phy_sleepwalk(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	unsigned int index = lane->index;
	u32 value;

	mutex_lock(&padctl->lock);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM_1);
	value &= ~SSPX_ELPG_CLAMP_EN_EARLY(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM_1);

	usleep_range(100, 200);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM_1);
	value &= ~SSPX_ELPG_CLAMP_EN(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM_1);

	mutex_unlock(&padctl->lock);

	return 0;
}

static int tegra186_usb3_enable_phy_wake(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	unsigned int index = lane->index;
	u32 value;

	mutex_lock(&padctl->lock);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM);
	value &= ~ALL_WAKE_EVENTS;
	value |= SS_PORT_WAKEUP_EVENT(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM);

	usleep_range(10, 20);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM);
	value &= ~ALL_WAKE_EVENTS;
	value |= SS_PORT_WAKE_INTERRUPT_ENABLE(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM);

	mutex_unlock(&padctl->lock);

	return 0;
}

static int tegra186_usb3_disable_phy_wake(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	unsigned int index = lane->index;
	u32 value;

	mutex_lock(&padctl->lock);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM);
	value &= ~ALL_WAKE_EVENTS;
	value &= ~SS_PORT_WAKE_INTERRUPT_ENABLE(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM);

	usleep_range(10, 20);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM);
	value &= ~ALL_WAKE_EVENTS;
	value |= SS_PORT_WAKEUP_EVENT(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM);

	mutex_unlock(&padctl->lock);

	return 0;
}

static bool tegra186_usb3_phy_remote_wake_detected(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	unsigned int index = lane->index;
	u32 value;

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM);
	if ((value & SS_PORT_WAKE_INTERRUPT_ENABLE(index)) && (value & SS_PORT_WAKEUP_EVENT(index)))
		return true;

	return false;
}

static const struct tegra_xusb_lane_ops tegra186_usb3_lane_ops = {
	.probe = tegra186_usb3_lane_probe,
	.remove = tegra186_usb3_lane_remove,
	.enable_phy_sleepwalk = tegra186_usb3_enable_phy_sleepwalk,
	.disable_phy_sleepwalk = tegra186_usb3_disable_phy_sleepwalk,
	.enable_phy_wake = tegra186_usb3_enable_phy_wake,
	.disable_phy_wake = tegra186_usb3_disable_phy_wake,
	.remote_wake_detected = tegra186_usb3_phy_remote_wake_detected,
};

static int tegra186_usb3_port_enable(struct tegra_xusb_port *port)
{
	return 0;
}

static void tegra186_usb3_port_disable(struct tegra_xusb_port *port)
{
}

static struct tegra_xusb_lane *
tegra186_usb3_port_map(struct tegra_xusb_port *port)
{
	return tegra_xusb_find_lane(port->padctl, "usb3", port->index);
}

static const struct tegra_xusb_port_ops tegra186_usb3_port_ops = {
	.release = tegra_xusb_usb3_port_release,
	.remove = tegra_xusb_usb3_port_remove,
	.enable = tegra186_usb3_port_enable,
	.disable = tegra186_usb3_port_disable,
	.map = tegra186_usb3_port_map,
};

static int tegra186_usb3_phy_power_on(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	struct tegra_xusb_usb3_port *port;
	struct tegra_xusb_usb2_port *usb2;
	unsigned int index = lane->index;
	struct device *dev = padctl->dev;
	u32 value;

	port = tegra_xusb_find_usb3_port(padctl, index);
	if (!port) {
		dev_err(dev, "no port found for USB3 lane %u\n", index);
		return -ENODEV;
	}

	usb2 = tegra_xusb_find_usb2_port(padctl, port->port);
	if (!usb2) {
		dev_err(dev, "no companion port found for USB3 lane %u\n",
			index);
		return -ENODEV;
	}

	mutex_lock(&padctl->lock);

	value = padctl_readl(padctl, XUSB_PADCTL_SS_PORT_CAP);
	value &= ~(PORT_CAP_MASK << PORTX_CAP_SHIFT(index));

	if (usb2->mode == USB_DR_MODE_UNKNOWN)
		value |= (PORT_CAP_DISABLED << PORTX_CAP_SHIFT(index));
	else if (usb2->mode == USB_DR_MODE_PERIPHERAL)
		value |= (PORT_CAP_DEVICE << PORTX_CAP_SHIFT(index));
	else if (usb2->mode == USB_DR_MODE_HOST)
		value |= (PORT_CAP_HOST << PORTX_CAP_SHIFT(index));
	else if (usb2->mode == USB_DR_MODE_OTG)
		value |= (PORT_CAP_OTG << PORTX_CAP_SHIFT(index));

	padctl_writel(padctl, value, XUSB_PADCTL_SS_PORT_CAP);

	if (padctl->soc->supports_gen2 && port->disable_gen2) {
		value = padctl_readl(padctl, XUSB_PADCTL_SS_PORT_CFG);
		value &= ~(PORTX_SPEED_SUPPORT_MASK <<
			PORTX_SPEED_SUPPORT_SHIFT(index));
		value |= (PORT_SPEED_SUPPORT_GEN1 <<
			PORTX_SPEED_SUPPORT_SHIFT(index));
		padctl_writel(padctl, value, XUSB_PADCTL_SS_PORT_CFG);
	}

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM_1);
	value &= ~SSPX_ELPG_VCORE_DOWN(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM_1);

	usleep_range(100, 200);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM_1);
	value &= ~SSPX_ELPG_CLAMP_EN_EARLY(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM_1);

	usleep_range(100, 200);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM_1);
	value &= ~SSPX_ELPG_CLAMP_EN(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM_1);

	mutex_unlock(&padctl->lock);

	return 0;
}

static int tegra186_usb3_phy_power_off(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	struct tegra_xusb_usb3_port *port;
	unsigned int index = lane->index;
	struct device *dev = padctl->dev;
	u32 value;

	port = tegra_xusb_find_usb3_port(padctl, index);
	if (!port) {
		dev_err(dev, "no port found for USB3 lane %u\n", index);
		return -ENODEV;
	}

	mutex_lock(&padctl->lock);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM_1);
	value |= SSPX_ELPG_CLAMP_EN_EARLY(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM_1);

	usleep_range(100, 200);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM_1);
	value |= SSPX_ELPG_CLAMP_EN(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM_1);

	usleep_range(250, 350);

	value = padctl_readl(padctl, XUSB_PADCTL_ELPG_PROGRAM_1);
	value |= SSPX_ELPG_VCORE_DOWN(index);
	padctl_writel(padctl, value, XUSB_PADCTL_ELPG_PROGRAM_1);

	mutex_unlock(&padctl->lock);

	return 0;
}

static int tegra186_usb3_phy_init(struct phy *phy)
{
	return 0;
}

static int tegra186_usb3_phy_exit(struct phy *phy)
{
	return 0;
}

static const struct phy_ops usb3_phy_ops = {
	.init = tegra186_usb3_phy_init,
	.exit = tegra186_usb3_phy_exit,
	.power_on = tegra186_usb3_phy_power_on,
	.power_off = tegra186_usb3_phy_power_off,
	.owner = THIS_MODULE,
};

static struct tegra_xusb_pad *
tegra186_usb3_pad_probe(struct tegra_xusb_padctl *padctl,
			const struct tegra_xusb_pad_soc *soc,
			struct device_node *np)
{
	struct tegra_xusb_usb3_pad *usb3;
	struct tegra_xusb_pad *pad;
	int err;

	usb3 = kzalloc(sizeof(*usb3), GFP_KERNEL);
	if (!usb3)
		return ERR_PTR(-ENOMEM);

	pad = &usb3->base;
	pad->ops = &tegra186_usb3_lane_ops;
	pad->soc = soc;

	err = tegra_xusb_pad_init(pad, padctl, np);
	if (err < 0) {
		kfree(usb3);
		goto out;
	}

	err = tegra_xusb_pad_register(pad, &usb3_phy_ops);
	if (err < 0)
		goto unregister;

	dev_set_drvdata(&pad->dev, pad);

	return pad;

unregister:
	device_unregister(&pad->dev);
out:
	return ERR_PTR(err);
}

static void tegra186_usb3_pad_remove(struct tegra_xusb_pad *pad)
{
	struct tegra_xusb_usb2_pad *usb2 = to_usb2_pad(pad);

	kfree(usb2);
}

static const struct tegra_xusb_pad_ops tegra186_usb3_pad_ops = {
	.probe = tegra186_usb3_pad_probe,
	.remove = tegra186_usb3_pad_remove,
};

static const char * const tegra186_usb3_functions[] = {
	"xusb",
};

static int
tegra186_xusb_read_fuse_calibration(struct tegra186_xusb_padctl *padctl)
{
	struct device *dev = padctl->base.dev;
	unsigned int i, count;
	u32 value, *level;
	int err;

	count = padctl->base.soc->ports.usb2.count;

	level = devm_kcalloc(dev, count, sizeof(u32), GFP_KERNEL);
	if (!level)
		return -ENOMEM;

	err = tegra_fuse_readl(TEGRA_FUSE_SKU_CALIB_0, &value);
	if (err) {
		if (err != -EPROBE_DEFER)
			dev_err(dev, "failed to read calibration fuse: %d\n",
				err);
		return err;
	}

	dev_dbg(dev, "FUSE_USB_CALIB_0 %#x\n", value);

	for (i = 0; i < count; i++)
		level[i] = (value >> HS_CURR_LEVEL_PADX_SHIFT(i)) &
				HS_CURR_LEVEL_PAD_MASK;

	padctl->calib.hs_curr_level = level;

	padctl->calib.hs_squelch = (value >> HS_SQUELCH_SHIFT) &
					HS_SQUELCH_MASK;
	padctl->calib.hs_term_range_adj = (value >> HS_TERM_RANGE_ADJ_SHIFT) &
						HS_TERM_RANGE_ADJ_MASK;

	err = tegra_fuse_readl(TEGRA_FUSE_USB_CALIB_EXT_0, &value);
	if (err) {
		dev_err(dev, "failed to read calibration fuse: %d\n", err);
		return err;
	}

	dev_dbg(dev, "FUSE_USB_CALIB_EXT_0 %#x\n", value);

	padctl->calib.rpd_ctrl = (value >> RPD_CTRL_SHIFT) & RPD_CTRL_MASK;

	return 0;
}

static struct tegra_xusb_padctl *
tegra186_xusb_padctl_probe(struct device *dev,
			   const struct tegra_xusb_padctl_soc *soc)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct tegra186_xusb_padctl *priv;
	struct resource *res;
	int err;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	priv->base.dev = dev;
	priv->base.soc = soc;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ao");
	priv->ao_regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->ao_regs))
		return ERR_CAST(priv->ao_regs);

	err = tegra186_xusb_read_fuse_calibration(priv);
	if (err < 0)
		return ERR_PTR(err);

	return &priv->base;
}

static void tegra186_xusb_padctl_save(struct tegra_xusb_padctl *padctl)
{
	struct tegra186_xusb_padctl *priv = to_tegra186_xusb_padctl(padctl);

	priv->context.vbus_id = padctl_readl(padctl, USB2_VBUS_ID);
	priv->context.usb2_pad_mux = padctl_readl(padctl, XUSB_PADCTL_USB2_PAD_MUX);
	priv->context.usb2_port_cap = padctl_readl(padctl, XUSB_PADCTL_USB2_PORT_CAP);
	priv->context.ss_port_cap = padctl_readl(padctl, XUSB_PADCTL_SS_PORT_CAP);
}

static void tegra186_xusb_padctl_restore(struct tegra_xusb_padctl *padctl)
{
	struct tegra186_xusb_padctl *priv = to_tegra186_xusb_padctl(padctl);

	padctl_writel(padctl, priv->context.usb2_pad_mux, XUSB_PADCTL_USB2_PAD_MUX);
	padctl_writel(padctl, priv->context.usb2_port_cap, XUSB_PADCTL_USB2_PORT_CAP);
	padctl_writel(padctl, priv->context.ss_port_cap, XUSB_PADCTL_SS_PORT_CAP);
	padctl_writel(padctl, priv->context.vbus_id, USB2_VBUS_ID);
}

static int tegra186_xusb_padctl_suspend_noirq(struct tegra_xusb_padctl *padctl)
{
	tegra186_xusb_padctl_save(padctl);

	return 0;
}

static int tegra186_xusb_padctl_resume_noirq(struct tegra_xusb_padctl *padctl)
{
	tegra186_xusb_padctl_restore(padctl);

	return 0;
}

static void tegra186_xusb_padctl_remove(struct tegra_xusb_padctl *padctl)
{
}

static const struct tegra_xusb_padctl_ops tegra186_xusb_padctl_ops = {
	.probe = tegra186_xusb_padctl_probe,
	.remove = tegra186_xusb_padctl_remove,
	.suspend_noirq = tegra186_xusb_padctl_suspend_noirq,
	.resume_noirq = tegra186_xusb_padctl_resume_noirq,
	.vbus_override = tegra186_xusb_padctl_vbus_override,
};

#if IS_ENABLED(CONFIG_ARCH_TEGRA_186_SOC)
static const char * const tegra186_xusb_padctl_supply_names[] = {
	"avdd-pll-erefeut",
	"avdd-usb",
	"vclamp-usb",
	"vddio-hsic",
};

static const struct tegra_xusb_lane_soc tegra186_usb2_lanes[] = {
	TEGRA186_LANE("usb2-0", 0,  0, 0, usb2),
	TEGRA186_LANE("usb2-1", 0,  0, 0, usb2),
	TEGRA186_LANE("usb2-2", 0,  0, 0, usb2),
};

static const struct tegra_xusb_pad_soc tegra186_usb2_pad = {
	.name = "usb2",
	.num_lanes = ARRAY_SIZE(tegra186_usb2_lanes),
	.lanes = tegra186_usb2_lanes,
	.ops = &tegra186_usb2_pad_ops,
};

static const struct tegra_xusb_lane_soc tegra186_usb3_lanes[] = {
	TEGRA186_LANE("usb3-0", 0,  0, 0, usb3),
	TEGRA186_LANE("usb3-1", 0,  0, 0, usb3),
	TEGRA186_LANE("usb3-2", 0,  0, 0, usb3),
};

static const struct tegra_xusb_pad_soc tegra186_usb3_pad = {
	.name = "usb3",
	.num_lanes = ARRAY_SIZE(tegra186_usb3_lanes),
	.lanes = tegra186_usb3_lanes,
	.ops = &tegra186_usb3_pad_ops,
};

static const struct tegra_xusb_pad_soc * const tegra186_pads[] = {
	&tegra186_usb2_pad,
	&tegra186_usb3_pad,
#if 0 /* TODO implement */
	&tegra186_hsic_pad,
#endif
};

const struct tegra_xusb_padctl_soc tegra186_xusb_padctl_soc = {
	.num_pads = ARRAY_SIZE(tegra186_pads),
	.pads = tegra186_pads,
	.ports = {
		.usb2 = {
			.ops = &tegra186_usb2_port_ops,
			.count = 3,
		},
#if 0 /* TODO implement */
		.hsic = {
			.ops = &tegra186_hsic_port_ops,
			.count = 1,
		},
#endif
		.usb3 = {
			.ops = &tegra186_usb3_port_ops,
			.count = 3,
		},
	},
	.ops = &tegra186_xusb_padctl_ops,
	.supply_names = tegra186_xusb_padctl_supply_names,
	.num_supplies = ARRAY_SIZE(tegra186_xusb_padctl_supply_names),
};
EXPORT_SYMBOL_GPL(tegra186_xusb_padctl_soc);
#endif

#if IS_ENABLED(CONFIG_ARCH_TEGRA_194_SOC)
static const char * const tegra194_xusb_padctl_supply_names[] = {
	"avdd-usb",
	"vclamp-usb",
};

static const struct tegra_xusb_lane_soc tegra194_usb2_lanes[] = {
	TEGRA186_LANE("usb2-0", 0,  0, 0, usb2),
	TEGRA186_LANE("usb2-1", 0,  0, 0, usb2),
	TEGRA186_LANE("usb2-2", 0,  0, 0, usb2),
	TEGRA186_LANE("usb2-3", 0,  0, 0, usb2),
};

static const struct tegra_xusb_pad_soc tegra194_usb2_pad = {
	.name = "usb2",
	.num_lanes = ARRAY_SIZE(tegra194_usb2_lanes),
	.lanes = tegra194_usb2_lanes,
	.ops = &tegra186_usb2_pad_ops,
};

static const struct tegra_xusb_lane_soc tegra194_usb3_lanes[] = {
	TEGRA186_LANE("usb3-0", 0,  0, 0, usb3),
	TEGRA186_LANE("usb3-1", 0,  0, 0, usb3),
	TEGRA186_LANE("usb3-2", 0,  0, 0, usb3),
	TEGRA186_LANE("usb3-3", 0,  0, 0, usb3),
};

static const struct tegra_xusb_pad_soc tegra194_usb3_pad = {
	.name = "usb3",
	.num_lanes = ARRAY_SIZE(tegra194_usb3_lanes),
	.lanes = tegra194_usb3_lanes,
	.ops = &tegra186_usb3_pad_ops,
};

static const struct tegra_xusb_pad_soc * const tegra194_pads[] = {
	&tegra194_usb2_pad,
	&tegra194_usb3_pad,
};

const struct tegra_xusb_padctl_soc tegra194_xusb_padctl_soc = {
	.num_pads = ARRAY_SIZE(tegra194_pads),
	.pads = tegra194_pads,
	.ports = {
		.usb2 = {
			.ops = &tegra186_usb2_port_ops,
			.count = 4,
		},
		.usb3 = {
			.ops = &tegra186_usb3_port_ops,
			.count = 4,
		},
	},
	.ops = &tegra186_xusb_padctl_ops,
	.supply_names = tegra194_xusb_padctl_supply_names,
	.num_supplies = ARRAY_SIZE(tegra194_xusb_padctl_supply_names),
	.supports_gen2 = true,
};
EXPORT_SYMBOL_GPL(tegra194_xusb_padctl_soc);
#endif

MODULE_AUTHOR("JC Kuo <jckuo@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra186 XUSB Pad Controller driver");
MODULE_LICENSE("GPL v2");
