// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2012 Freescale Semiconductor, Inc.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/usb/otg.h>

#include "ci_hdrc_imx.h"

#define MX25_USB_PHY_CTRL_OFFSET	0x08
#define MX25_BM_EXTERNAL_VBUS_DIVIDER	BIT(23)

#define MX25_EHCI_INTERFACE_SINGLE_UNI	(2 << 0)
#define MX25_EHCI_INTERFACE_DIFF_UNI	(0 << 0)
#define MX25_EHCI_INTERFACE_MASK	(0xf)

#define MX25_OTG_SIC_SHIFT		29
#define MX25_OTG_SIC_MASK		(0x3 << MX25_OTG_SIC_SHIFT)
#define MX25_OTG_PM_BIT			BIT(24)
#define MX25_OTG_PP_BIT			BIT(11)
#define MX25_OTG_OCPOL_BIT		BIT(3)

#define MX25_H1_SIC_SHIFT		21
#define MX25_H1_SIC_MASK		(0x3 << MX25_H1_SIC_SHIFT)
#define MX25_H1_PP_BIT			BIT(18)
#define MX25_H1_PM_BIT			BIT(16)
#define MX25_H1_IPPUE_UP_BIT		BIT(7)
#define MX25_H1_IPPUE_DOWN_BIT		BIT(6)
#define MX25_H1_TLL_BIT			BIT(5)
#define MX25_H1_USBTE_BIT		BIT(4)
#define MX25_H1_OCPOL_BIT		BIT(2)

#define MX27_H1_PM_BIT			BIT(8)
#define MX27_H2_PM_BIT			BIT(16)
#define MX27_OTG_PM_BIT			BIT(24)

#define MX53_USB_OTG_PHY_CTRL_0_OFFSET	0x08
#define MX53_USB_OTG_PHY_CTRL_1_OFFSET	0x0c
#define MX53_USB_CTRL_1_OFFSET	        0x10
#define MX53_USB_CTRL_1_H2_XCVR_CLK_SEL_MASK (0x11 << 2)
#define MX53_USB_CTRL_1_H2_XCVR_CLK_SEL_ULPI BIT(2)
#define MX53_USB_CTRL_1_H3_XCVR_CLK_SEL_MASK (0x11 << 6)
#define MX53_USB_CTRL_1_H3_XCVR_CLK_SEL_ULPI BIT(6)
#define MX53_USB_UH2_CTRL_OFFSET	0x14
#define MX53_USB_UH3_CTRL_OFFSET	0x18
#define MX53_USB_CLKONOFF_CTRL_OFFSET	0x24
#define MX53_USB_CLKONOFF_CTRL_H2_INT60CKOFF BIT(21)
#define MX53_USB_CLKONOFF_CTRL_H3_INT60CKOFF BIT(22)
#define MX53_BM_OVER_CUR_DIS_H1		BIT(5)
#define MX53_BM_OVER_CUR_DIS_OTG	BIT(8)
#define MX53_BM_OVER_CUR_DIS_UHx	BIT(30)
#define MX53_USB_CTRL_1_UH2_ULPI_EN	BIT(26)
#define MX53_USB_CTRL_1_UH3_ULPI_EN	BIT(27)
#define MX53_USB_UHx_CTRL_WAKE_UP_EN	BIT(7)
#define MX53_USB_UHx_CTRL_ULPI_INT_EN	BIT(8)
#define MX53_USB_PHYCTRL1_PLLDIV_MASK	0x3
#define MX53_USB_PLL_DIV_24_MHZ		0x01

#define MX6_BM_NON_BURST_SETTING	BIT(1)
#define MX6_BM_OVER_CUR_DIS		BIT(7)
#define MX6_BM_OVER_CUR_POLARITY	BIT(8)
#define MX6_BM_PWR_POLARITY		BIT(9)
#define MX6_BM_WAKEUP_ENABLE		BIT(10)
#define MX6_BM_UTMI_ON_CLOCK		BIT(13)
#define MX6_BM_ID_WAKEUP		BIT(16)
#define MX6_BM_VBUS_WAKEUP		BIT(17)
#define MX6SX_BM_DPDM_WAKEUP_EN		BIT(29)
#define MX6_BM_WAKEUP_INTR		BIT(31)

#define MX6_USB_HSIC_CTRL_OFFSET	0x10
/* Send resume signal without 480Mhz PHY clock */
#define MX6SX_BM_HSIC_AUTO_RESUME	BIT(23)
/* set before portsc.suspendM = 1 */
#define MX6_BM_HSIC_DEV_CONN		BIT(21)
/* HSIC enable */
#define MX6_BM_HSIC_EN			BIT(12)
/* Force HSIC module 480M clock on, even when in Host is in suspend mode */
#define MX6_BM_HSIC_CLK_ON		BIT(11)

#define MX6_USB_OTG1_PHY_CTRL		0x18
/* For imx6dql, it is host-only controller, for later imx6, it is otg's */
#define MX6_USB_OTG2_PHY_CTRL		0x1c
#define MX6SX_USB_VBUS_WAKEUP_SOURCE(v)	(v << 8)
#define MX6SX_USB_VBUS_WAKEUP_SOURCE_VBUS	MX6SX_USB_VBUS_WAKEUP_SOURCE(0)
#define MX6SX_USB_VBUS_WAKEUP_SOURCE_AVALID	MX6SX_USB_VBUS_WAKEUP_SOURCE(1)
#define MX6SX_USB_VBUS_WAKEUP_SOURCE_BVALID	MX6SX_USB_VBUS_WAKEUP_SOURCE(2)
#define MX6SX_USB_VBUS_WAKEUP_SOURCE_SESS_END	MX6SX_USB_VBUS_WAKEUP_SOURCE(3)

#define VF610_OVER_CUR_DIS		BIT(7)

#define MX7D_USBNC_USB_CTRL2		0x4
#define MX7D_USB_VBUS_WAKEUP_SOURCE_MASK	0x3
#define MX7D_USB_VBUS_WAKEUP_SOURCE(v)		(v << 0)
#define MX7D_USB_VBUS_WAKEUP_SOURCE_VBUS	MX7D_USB_VBUS_WAKEUP_SOURCE(0)
#define MX7D_USB_VBUS_WAKEUP_SOURCE_AVALID	MX7D_USB_VBUS_WAKEUP_SOURCE(1)
#define MX7D_USB_VBUS_WAKEUP_SOURCE_BVALID	MX7D_USB_VBUS_WAKEUP_SOURCE(2)
#define MX7D_USB_VBUS_WAKEUP_SOURCE_SESS_END	MX7D_USB_VBUS_WAKEUP_SOURCE(3)
#define MX7D_USBNC_AUTO_RESUME				BIT(2)
/* The default DM/DP value is pull-down */
#define MX7D_USBNC_USB_CTRL2_OPMODE(v)			(v << 6)
#define MX7D_USBNC_USB_CTRL2_OPMODE_NON_DRIVING	MX7D_USBNC_USB_CTRL2_OPMODE(1)
#define MX7D_USBNC_USB_CTRL2_OPMODE_OVERRIDE_MASK	(BIT(7) | BIT(6))
#define MX7D_USBNC_USB_CTRL2_OPMODE_OVERRIDE_EN		BIT(8)
#define MX7D_USBNC_USB_CTRL2_DP_OVERRIDE_VAL		BIT(12)
#define MX7D_USBNC_USB_CTRL2_DP_OVERRIDE_EN		BIT(13)
#define MX7D_USBNC_USB_CTRL2_DM_OVERRIDE_VAL		BIT(14)
#define MX7D_USBNC_USB_CTRL2_DM_OVERRIDE_EN		BIT(15)
#define MX7D_USBNC_USB_CTRL2_DP_DM_MASK			(BIT(12) | BIT(13) | \
							BIT(14) | BIT(15))

#define MX7D_USB_OTG_PHY_CFG2_CHRG_CHRGSEL	BIT(0)
#define MX7D_USB_OTG_PHY_CFG2_CHRG_VDATDETENB0	BIT(1)
#define MX7D_USB_OTG_PHY_CFG2_CHRG_VDATSRCENB0	BIT(2)
#define MX7D_USB_OTG_PHY_CFG2_CHRG_DCDENB	BIT(3)
#define MX7D_USB_OTG_PHY_CFG2_DRVVBUS0		BIT(16)

#define MX7D_USB_OTG_PHY_CFG2		0x34

#define MX7D_USB_OTG_PHY_STATUS		0x3c
#define MX7D_USB_OTG_PHY_STATUS_LINE_STATE0	BIT(0)
#define MX7D_USB_OTG_PHY_STATUS_LINE_STATE1	BIT(1)
#define MX7D_USB_OTG_PHY_STATUS_VBUS_VLD	BIT(3)
#define MX7D_USB_OTG_PHY_STATUS_CHRGDET		BIT(29)

#define MX7D_USB_OTG_PHY_CFG1		0x30
#define TXPREEMPAMPTUNE0_BIT		28
#define TXPREEMPAMPTUNE0_MASK		(3 << 28)
#define TXRISETUNE0_BIT			24
#define TXRISETUNE0_MASK		(3 << 24)
#define TXVREFTUNE0_BIT			20
#define TXVREFTUNE0_MASK		(0xf << 20)

#define MX6_USB_OTG_WAKEUP_BITS (MX6_BM_WAKEUP_ENABLE | MX6_BM_VBUS_WAKEUP | \
				 MX6_BM_ID_WAKEUP | MX6SX_BM_DPDM_WAKEUP_EN)

struct usbmisc_ops {
	/* It's called once when probe a usb device */
	int (*init)(struct imx_usbmisc_data *data);
	/* It's called once after adding a usb device */
	int (*post)(struct imx_usbmisc_data *data);
	/* It's called when we need to enable/disable usb wakeup */
	int (*set_wakeup)(struct imx_usbmisc_data *data, bool enabled);
	/* It's called before setting portsc.suspendM */
	int (*hsic_set_connect)(struct imx_usbmisc_data *data);
	/* It's called during suspend/resume */
	int (*hsic_set_clk)(struct imx_usbmisc_data *data, bool enabled);
	/* usb charger detection */
	int (*charger_detection)(struct imx_usbmisc_data *data);
	/* It's called when system resume from usb power lost */
	int (*power_lost_check)(struct imx_usbmisc_data *data);
	void (*vbus_comparator_on)(struct imx_usbmisc_data *data, bool on);
};

struct imx_usbmisc {
	void __iomem *base;
	spinlock_t lock;
	const struct usbmisc_ops *ops;
};

static inline bool is_imx53_usbmisc(struct imx_usbmisc_data *data);

static int usbmisc_imx25_init(struct imx_usbmisc_data *data)
{
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);
	unsigned long flags;
	u32 val = 0;

	if (data->index > 1)
		return -EINVAL;

	spin_lock_irqsave(&usbmisc->lock, flags);
	switch (data->index) {
	case 0:
		val = readl(usbmisc->base);
		val &= ~(MX25_OTG_SIC_MASK | MX25_OTG_PP_BIT);
		val |= (MX25_EHCI_INTERFACE_DIFF_UNI & MX25_EHCI_INTERFACE_MASK) << MX25_OTG_SIC_SHIFT;
		val |= (MX25_OTG_PM_BIT | MX25_OTG_OCPOL_BIT);

		/*
		 * If the polarity is not configured assume active high for
		 * historical reasons.
		 */
		if (data->oc_pol_configured && data->oc_pol_active_low)
			val &= ~MX25_OTG_OCPOL_BIT;

		writel(val, usbmisc->base);
		break;
	case 1:
		val = readl(usbmisc->base);
		val &= ~(MX25_H1_SIC_MASK | MX25_H1_PP_BIT |  MX25_H1_IPPUE_UP_BIT);
		val |= (MX25_EHCI_INTERFACE_SINGLE_UNI & MX25_EHCI_INTERFACE_MASK) << MX25_H1_SIC_SHIFT;
		val |= (MX25_H1_PM_BIT | MX25_H1_OCPOL_BIT | MX25_H1_TLL_BIT |
			MX25_H1_USBTE_BIT | MX25_H1_IPPUE_DOWN_BIT);

		/*
		 * If the polarity is not configured assume active high for
		 * historical reasons.
		 */
		if (data->oc_pol_configured && data->oc_pol_active_low)
			val &= ~MX25_H1_OCPOL_BIT;

		writel(val, usbmisc->base);

		break;
	}
	spin_unlock_irqrestore(&usbmisc->lock, flags);

	return 0;
}

static int usbmisc_imx25_post(struct imx_usbmisc_data *data)
{
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);
	void __iomem *reg;
	unsigned long flags;
	u32 val;

	if (data->index > 2)
		return -EINVAL;

	if (data->index)
		return 0;

	spin_lock_irqsave(&usbmisc->lock, flags);
	reg = usbmisc->base + MX25_USB_PHY_CTRL_OFFSET;
	val = readl(reg);

	if (data->evdo)
		val |= MX25_BM_EXTERNAL_VBUS_DIVIDER;
	else
		val &= ~MX25_BM_EXTERNAL_VBUS_DIVIDER;

	writel(val, reg);
	spin_unlock_irqrestore(&usbmisc->lock, flags);
	usleep_range(5000, 10000); /* needed to stabilize voltage */

	return 0;
}

static int usbmisc_imx27_init(struct imx_usbmisc_data *data)
{
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);
	unsigned long flags;
	u32 val;

	switch (data->index) {
	case 0:
		val = MX27_OTG_PM_BIT;
		break;
	case 1:
		val = MX27_H1_PM_BIT;
		break;
	case 2:
		val = MX27_H2_PM_BIT;
		break;
	default:
		return -EINVAL;
	}

	spin_lock_irqsave(&usbmisc->lock, flags);
	if (data->disable_oc)
		val = readl(usbmisc->base) | val;
	else
		val = readl(usbmisc->base) & ~val;
	writel(val, usbmisc->base);
	spin_unlock_irqrestore(&usbmisc->lock, flags);

	return 0;
}

static int usbmisc_imx53_init(struct imx_usbmisc_data *data)
{
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);
	void __iomem *reg = NULL;
	unsigned long flags;
	u32 val = 0;

	if (data->index > 3)
		return -EINVAL;

	/* Select a 24 MHz reference clock for the PHY  */
	val = readl(usbmisc->base + MX53_USB_OTG_PHY_CTRL_1_OFFSET);
	val &= ~MX53_USB_PHYCTRL1_PLLDIV_MASK;
	val |= MX53_USB_PLL_DIV_24_MHZ;
	writel(val, usbmisc->base + MX53_USB_OTG_PHY_CTRL_1_OFFSET);

	spin_lock_irqsave(&usbmisc->lock, flags);

	switch (data->index) {
	case 0:
		if (data->disable_oc) {
			reg = usbmisc->base + MX53_USB_OTG_PHY_CTRL_0_OFFSET;
			val = readl(reg) | MX53_BM_OVER_CUR_DIS_OTG;
			writel(val, reg);
		}
		break;
	case 1:
		if (data->disable_oc) {
			reg = usbmisc->base + MX53_USB_OTG_PHY_CTRL_0_OFFSET;
			val = readl(reg) | MX53_BM_OVER_CUR_DIS_H1;
			writel(val, reg);
		}
		break;
	case 2:
		if (data->ulpi) {
			/* set USBH2 into ULPI-mode. */
			reg = usbmisc->base + MX53_USB_CTRL_1_OFFSET;
			val = readl(reg) | MX53_USB_CTRL_1_UH2_ULPI_EN;
			/* select ULPI clock */
			val &= ~MX53_USB_CTRL_1_H2_XCVR_CLK_SEL_MASK;
			val |= MX53_USB_CTRL_1_H2_XCVR_CLK_SEL_ULPI;
			writel(val, reg);
			/* Set interrupt wake up enable */
			reg = usbmisc->base + MX53_USB_UH2_CTRL_OFFSET;
			val = readl(reg) | MX53_USB_UHx_CTRL_WAKE_UP_EN
				| MX53_USB_UHx_CTRL_ULPI_INT_EN;
			writel(val, reg);
			if (is_imx53_usbmisc(data)) {
				/* Disable internal 60Mhz clock */
				reg = usbmisc->base +
					MX53_USB_CLKONOFF_CTRL_OFFSET;
				val = readl(reg) |
					MX53_USB_CLKONOFF_CTRL_H2_INT60CKOFF;
				writel(val, reg);
			}

		}
		if (data->disable_oc) {
			reg = usbmisc->base + MX53_USB_UH2_CTRL_OFFSET;
			val = readl(reg) | MX53_BM_OVER_CUR_DIS_UHx;
			writel(val, reg);
		}
		break;
	case 3:
		if (data->ulpi) {
			/* set USBH3 into ULPI-mode. */
			reg = usbmisc->base + MX53_USB_CTRL_1_OFFSET;
			val = readl(reg) | MX53_USB_CTRL_1_UH3_ULPI_EN;
			/* select ULPI clock */
			val &= ~MX53_USB_CTRL_1_H3_XCVR_CLK_SEL_MASK;
			val |= MX53_USB_CTRL_1_H3_XCVR_CLK_SEL_ULPI;
			writel(val, reg);
			/* Set interrupt wake up enable */
			reg = usbmisc->base + MX53_USB_UH3_CTRL_OFFSET;
			val = readl(reg) | MX53_USB_UHx_CTRL_WAKE_UP_EN
				| MX53_USB_UHx_CTRL_ULPI_INT_EN;
			writel(val, reg);

			if (is_imx53_usbmisc(data)) {
				/* Disable internal 60Mhz clock */
				reg = usbmisc->base +
					MX53_USB_CLKONOFF_CTRL_OFFSET;
				val = readl(reg) |
					MX53_USB_CLKONOFF_CTRL_H3_INT60CKOFF;
				writel(val, reg);
			}
		}
		if (data->disable_oc) {
			reg = usbmisc->base + MX53_USB_UH3_CTRL_OFFSET;
			val = readl(reg) | MX53_BM_OVER_CUR_DIS_UHx;
			writel(val, reg);
		}
		break;
	}

	spin_unlock_irqrestore(&usbmisc->lock, flags);

	return 0;
}

static u32 usbmisc_wakeup_setting(struct imx_usbmisc_data *data)
{
	u32 wakeup_setting = MX6_USB_OTG_WAKEUP_BITS;

	if (data->ext_id || data->available_role != USB_DR_MODE_OTG)
		wakeup_setting &= ~MX6_BM_ID_WAKEUP;

	if (data->ext_vbus || data->available_role == USB_DR_MODE_HOST)
		wakeup_setting &= ~MX6_BM_VBUS_WAKEUP;

	return wakeup_setting;
}

static int usbmisc_imx6q_set_wakeup
	(struct imx_usbmisc_data *data, bool enabled)
{
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);
	unsigned long flags;
	u32 val;
	int ret = 0;

	if (data->index > 3)
		return -EINVAL;

	spin_lock_irqsave(&usbmisc->lock, flags);
	val = readl(usbmisc->base + data->index * 4);
	if (enabled) {
		val &= ~MX6_USB_OTG_WAKEUP_BITS;
		val |= usbmisc_wakeup_setting(data);
	} else {
		if (val & MX6_BM_WAKEUP_INTR)
			pr_debug("wakeup int at ci_hdrc.%d\n", data->index);
		val &= ~MX6_USB_OTG_WAKEUP_BITS;
	}
	writel(val, usbmisc->base + data->index * 4);
	spin_unlock_irqrestore(&usbmisc->lock, flags);

	return ret;
}

static int usbmisc_imx6q_init(struct imx_usbmisc_data *data)
{
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);
	unsigned long flags;
	u32 reg;

	if (data->index > 3)
		return -EINVAL;

	spin_lock_irqsave(&usbmisc->lock, flags);

	reg = readl(usbmisc->base + data->index * 4);
	if (data->disable_oc) {
		reg |= MX6_BM_OVER_CUR_DIS;
	} else {
		reg &= ~MX6_BM_OVER_CUR_DIS;

		/*
		 * If the polarity is not configured keep it as setup by the
		 * bootloader.
		 */
		if (data->oc_pol_configured && data->oc_pol_active_low)
			reg |= MX6_BM_OVER_CUR_POLARITY;
		else if (data->oc_pol_configured)
			reg &= ~MX6_BM_OVER_CUR_POLARITY;
	}
	/* If the polarity is not set keep it as setup by the bootloader */
	if (data->pwr_pol == 1)
		reg |= MX6_BM_PWR_POLARITY;
	writel(reg, usbmisc->base + data->index * 4);

	/* SoC non-burst setting */
	reg = readl(usbmisc->base + data->index * 4);
	writel(reg | MX6_BM_NON_BURST_SETTING,
			usbmisc->base + data->index * 4);

	/* For HSIC controller */
	if (data->hsic) {
		reg = readl(usbmisc->base + data->index * 4);
		writel(reg | MX6_BM_UTMI_ON_CLOCK,
			usbmisc->base + data->index * 4);
		reg = readl(usbmisc->base + MX6_USB_HSIC_CTRL_OFFSET
			+ (data->index - 2) * 4);
		reg |= MX6_BM_HSIC_EN | MX6_BM_HSIC_CLK_ON;
		writel(reg, usbmisc->base + MX6_USB_HSIC_CTRL_OFFSET
			+ (data->index - 2) * 4);
	}

	spin_unlock_irqrestore(&usbmisc->lock, flags);

	usbmisc_imx6q_set_wakeup(data, false);

	return 0;
}

static int usbmisc_imx6_hsic_get_reg_offset(struct imx_usbmisc_data *data)
{
	int offset, ret = 0;

	if (data->index == 2 || data->index == 3) {
		offset = (data->index - 2) * 4;
	} else if (data->index == 0) {
		/*
		 * For SoCs like i.MX7D and later, each USB controller has
		 * its own non-core register region. For SoCs before i.MX7D,
		 * the first two USB controllers are non-HSIC controllers.
		 */
		offset = 0;
	} else {
		dev_err(data->dev, "index is error for usbmisc\n");
		ret = -EINVAL;
	}

	return ret ? ret : offset;
}

static int usbmisc_imx6_hsic_set_connect(struct imx_usbmisc_data *data)
{
	unsigned long flags;
	u32 val;
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);
	int offset;

	spin_lock_irqsave(&usbmisc->lock, flags);
	offset = usbmisc_imx6_hsic_get_reg_offset(data);
	if (offset < 0) {
		spin_unlock_irqrestore(&usbmisc->lock, flags);
		return offset;
	}

	val = readl(usbmisc->base + MX6_USB_HSIC_CTRL_OFFSET + offset);
	if (!(val & MX6_BM_HSIC_DEV_CONN))
		writel(val | MX6_BM_HSIC_DEV_CONN,
			usbmisc->base + MX6_USB_HSIC_CTRL_OFFSET + offset);

	spin_unlock_irqrestore(&usbmisc->lock, flags);

	return 0;
}

static int usbmisc_imx6_hsic_set_clk(struct imx_usbmisc_data *data, bool on)
{
	unsigned long flags;
	u32 val;
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);
	int offset;

	spin_lock_irqsave(&usbmisc->lock, flags);
	offset = usbmisc_imx6_hsic_get_reg_offset(data);
	if (offset < 0) {
		spin_unlock_irqrestore(&usbmisc->lock, flags);
		return offset;
	}

	val = readl(usbmisc->base + MX6_USB_HSIC_CTRL_OFFSET + offset);
	val |= MX6_BM_HSIC_EN | MX6_BM_HSIC_CLK_ON;
	if (on)
		val |= MX6_BM_HSIC_CLK_ON;
	else
		val &= ~MX6_BM_HSIC_CLK_ON;

	writel(val, usbmisc->base + MX6_USB_HSIC_CTRL_OFFSET + offset);
	spin_unlock_irqrestore(&usbmisc->lock, flags);

	return 0;
}


static int usbmisc_imx6sx_init(struct imx_usbmisc_data *data)
{
	void __iomem *reg = NULL;
	unsigned long flags;
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);
	u32 val;

	usbmisc_imx6q_init(data);

	if (data->index == 0 || data->index == 1) {
		reg = usbmisc->base + MX6_USB_OTG1_PHY_CTRL + data->index * 4;
		spin_lock_irqsave(&usbmisc->lock, flags);
		/* Set vbus wakeup source as bvalid */
		val = readl(reg);
		writel(val | MX6SX_USB_VBUS_WAKEUP_SOURCE_BVALID, reg);
		/*
		 * Disable dp/dm wakeup in device mode when vbus is
		 * not there.
		 */
		val = readl(usbmisc->base + data->index * 4);
		writel(val & ~MX6SX_BM_DPDM_WAKEUP_EN,
			usbmisc->base + data->index * 4);
		spin_unlock_irqrestore(&usbmisc->lock, flags);
	}

	/* For HSIC controller */
	if (data->hsic) {
		val = readl(usbmisc->base + MX6_USB_HSIC_CTRL_OFFSET);
		val |= MX6SX_BM_HSIC_AUTO_RESUME;
		writel(val, usbmisc->base + MX6_USB_HSIC_CTRL_OFFSET);
	}

	return 0;
}

static int usbmisc_vf610_init(struct imx_usbmisc_data *data)
{
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);
	u32 reg;

	/*
	 * Vybrid only has one misc register set, but in two different
	 * areas. These is reflected in two instances of this driver.
	 */
	if (data->index >= 1)
		return -EINVAL;

	if (data->disable_oc) {
		reg = readl(usbmisc->base);
		writel(reg | VF610_OVER_CUR_DIS, usbmisc->base);
	}

	return 0;
}

static int usbmisc_imx7d_set_wakeup
	(struct imx_usbmisc_data *data, bool enabled)
{
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&usbmisc->lock, flags);
	val = readl(usbmisc->base);
	if (enabled) {
		val &= ~MX6_USB_OTG_WAKEUP_BITS;
		val |= usbmisc_wakeup_setting(data);
		writel(val, usbmisc->base);
	} else {
		if (val & MX6_BM_WAKEUP_INTR)
			dev_dbg(data->dev, "wakeup int\n");
		writel(val & ~MX6_USB_OTG_WAKEUP_BITS, usbmisc->base);
	}
	spin_unlock_irqrestore(&usbmisc->lock, flags);

	return 0;
}

static int usbmisc_imx7d_init(struct imx_usbmisc_data *data)
{
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);
	unsigned long flags;
	u32 reg;

	if (data->index >= 1)
		return -EINVAL;

	spin_lock_irqsave(&usbmisc->lock, flags);
	reg = readl(usbmisc->base);
	if (data->disable_oc) {
		reg |= MX6_BM_OVER_CUR_DIS;
	} else {
		reg &= ~MX6_BM_OVER_CUR_DIS;

		/*
		 * If the polarity is not configured keep it as setup by the
		 * bootloader.
		 */
		if (data->oc_pol_configured && data->oc_pol_active_low)
			reg |= MX6_BM_OVER_CUR_POLARITY;
		else if (data->oc_pol_configured)
			reg &= ~MX6_BM_OVER_CUR_POLARITY;
	}
	/* If the polarity is not set keep it as setup by the bootloader */
	if (data->pwr_pol == 1)
		reg |= MX6_BM_PWR_POLARITY;
	writel(reg, usbmisc->base);

	/* SoC non-burst setting */
	reg = readl(usbmisc->base);
	writel(reg | MX6_BM_NON_BURST_SETTING, usbmisc->base);

	if (!data->hsic) {
		reg = readl(usbmisc->base + MX7D_USBNC_USB_CTRL2);
		reg &= ~MX7D_USB_VBUS_WAKEUP_SOURCE_MASK;
		writel(reg | MX7D_USB_VBUS_WAKEUP_SOURCE_BVALID
			| MX7D_USBNC_AUTO_RESUME,
			usbmisc->base + MX7D_USBNC_USB_CTRL2);
		/* PHY tuning for signal quality */
		reg = readl(usbmisc->base + MX7D_USB_OTG_PHY_CFG1);
		if (data->emp_curr_control >= 0 &&
			data->emp_curr_control <=
			(TXPREEMPAMPTUNE0_MASK >> TXPREEMPAMPTUNE0_BIT)) {
			reg &= ~TXPREEMPAMPTUNE0_MASK;
			reg |= (data->emp_curr_control << TXPREEMPAMPTUNE0_BIT);
		}

		if (data->dc_vol_level_adjust >= 0 &&
			data->dc_vol_level_adjust <=
			(TXVREFTUNE0_MASK >> TXVREFTUNE0_BIT)) {
			reg &= ~TXVREFTUNE0_MASK;
			reg |= (data->dc_vol_level_adjust << TXVREFTUNE0_BIT);
		}

		if (data->rise_fall_time_adjust >= 0 &&
			data->rise_fall_time_adjust <=
			(TXRISETUNE0_MASK >> TXRISETUNE0_BIT)) {
			reg &= ~TXRISETUNE0_MASK;
			reg |= (data->rise_fall_time_adjust << TXRISETUNE0_BIT);
		}

		writel(reg, usbmisc->base + MX7D_USB_OTG_PHY_CFG1);
	}

	spin_unlock_irqrestore(&usbmisc->lock, flags);

	usbmisc_imx7d_set_wakeup(data, false);

	return 0;
}

static int imx7d_charger_secondary_detection(struct imx_usbmisc_data *data)
{
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);
	struct usb_phy *usb_phy = data->usb_phy;
	int val;
	unsigned long flags;

	/* Clear VDATSRCENB0 to disable VDP_SRC and IDM_SNK required by BC 1.2 spec */
	spin_lock_irqsave(&usbmisc->lock, flags);
	val = readl(usbmisc->base + MX7D_USB_OTG_PHY_CFG2);
	val &= ~MX7D_USB_OTG_PHY_CFG2_CHRG_VDATSRCENB0;
	writel(val, usbmisc->base + MX7D_USB_OTG_PHY_CFG2);
	spin_unlock_irqrestore(&usbmisc->lock, flags);

	/* TVDMSRC_DIS */
	msleep(20);

	/* VDM_SRC is connected to D- and IDP_SINK is connected to D+ */
	spin_lock_irqsave(&usbmisc->lock, flags);
	val = readl(usbmisc->base + MX7D_USB_OTG_PHY_CFG2);
	writel(val | MX7D_USB_OTG_PHY_CFG2_CHRG_VDATSRCENB0 |
			MX7D_USB_OTG_PHY_CFG2_CHRG_VDATDETENB0 |
			MX7D_USB_OTG_PHY_CFG2_CHRG_CHRGSEL,
				usbmisc->base + MX7D_USB_OTG_PHY_CFG2);
	spin_unlock_irqrestore(&usbmisc->lock, flags);

	/* TVDMSRC_ON */
	msleep(40);

	/*
	 * Per BC 1.2, check voltage of D+:
	 * DCP: if greater than VDAT_REF;
	 * CDP: if less than VDAT_REF.
	 */
	val = readl(usbmisc->base + MX7D_USB_OTG_PHY_STATUS);
	if (val & MX7D_USB_OTG_PHY_STATUS_CHRGDET) {
		dev_dbg(data->dev, "It is a dedicate charging port\n");
		usb_phy->chg_type = DCP_TYPE;
	} else {
		dev_dbg(data->dev, "It is a charging downstream port\n");
		usb_phy->chg_type = CDP_TYPE;
	}

	return 0;
}

static void imx7_disable_charger_detector(struct imx_usbmisc_data *data)
{
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&usbmisc->lock, flags);
	val = readl(usbmisc->base + MX7D_USB_OTG_PHY_CFG2);
	val &= ~(MX7D_USB_OTG_PHY_CFG2_CHRG_DCDENB |
			MX7D_USB_OTG_PHY_CFG2_CHRG_VDATSRCENB0 |
			MX7D_USB_OTG_PHY_CFG2_CHRG_VDATDETENB0 |
			MX7D_USB_OTG_PHY_CFG2_CHRG_CHRGSEL);
	writel(val, usbmisc->base + MX7D_USB_OTG_PHY_CFG2);

	/* Set OPMODE to be 2'b00 and disable its override */
	val = readl(usbmisc->base + MX7D_USBNC_USB_CTRL2);
	val &= ~MX7D_USBNC_USB_CTRL2_OPMODE_OVERRIDE_MASK;
	writel(val, usbmisc->base + MX7D_USBNC_USB_CTRL2);

	val = readl(usbmisc->base + MX7D_USBNC_USB_CTRL2);
	writel(val & ~MX7D_USBNC_USB_CTRL2_OPMODE_OVERRIDE_EN,
			usbmisc->base + MX7D_USBNC_USB_CTRL2);
	spin_unlock_irqrestore(&usbmisc->lock, flags);
}

static int imx7d_charger_data_contact_detect(struct imx_usbmisc_data *data)
{
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);
	unsigned long flags;
	u32 val;
	int i, data_pin_contact_count = 0;

	/* Enable Data Contact Detect (DCD) per the USB BC 1.2 */
	spin_lock_irqsave(&usbmisc->lock, flags);
	val = readl(usbmisc->base + MX7D_USB_OTG_PHY_CFG2);
	writel(val | MX7D_USB_OTG_PHY_CFG2_CHRG_DCDENB,
			usbmisc->base + MX7D_USB_OTG_PHY_CFG2);
	spin_unlock_irqrestore(&usbmisc->lock, flags);

	for (i = 0; i < 100; i = i + 1) {
		val = readl(usbmisc->base + MX7D_USB_OTG_PHY_STATUS);
		if (!(val & MX7D_USB_OTG_PHY_STATUS_LINE_STATE0)) {
			if (data_pin_contact_count++ > 5)
				/* Data pin makes contact */
				break;
			usleep_range(5000, 10000);
		} else {
			data_pin_contact_count = 0;
			usleep_range(5000, 6000);
		}
	}

	/* Disable DCD after finished data contact check */
	spin_lock_irqsave(&usbmisc->lock, flags);
	val = readl(usbmisc->base + MX7D_USB_OTG_PHY_CFG2);
	writel(val & ~MX7D_USB_OTG_PHY_CFG2_CHRG_DCDENB,
			usbmisc->base + MX7D_USB_OTG_PHY_CFG2);
	spin_unlock_irqrestore(&usbmisc->lock, flags);

	if (i == 100) {
		dev_err(data->dev,
			"VBUS is coming from a dedicated power supply.\n");
		return -ENXIO;
	}

	return 0;
}

static int imx7d_charger_primary_detection(struct imx_usbmisc_data *data)
{
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);
	struct usb_phy *usb_phy = data->usb_phy;
	unsigned long flags;
	u32 val;

	/* VDP_SRC is connected to D+ and IDM_SINK is connected to D- */
	spin_lock_irqsave(&usbmisc->lock, flags);
	val = readl(usbmisc->base + MX7D_USB_OTG_PHY_CFG2);
	val &= ~MX7D_USB_OTG_PHY_CFG2_CHRG_CHRGSEL;
	writel(val | MX7D_USB_OTG_PHY_CFG2_CHRG_VDATSRCENB0 |
			MX7D_USB_OTG_PHY_CFG2_CHRG_VDATDETENB0,
				usbmisc->base + MX7D_USB_OTG_PHY_CFG2);
	spin_unlock_irqrestore(&usbmisc->lock, flags);

	/* TVDPSRC_ON */
	msleep(40);

	/* Check if D- is less than VDAT_REF to determine an SDP per BC 1.2 */
	val = readl(usbmisc->base + MX7D_USB_OTG_PHY_STATUS);
	if (!(val & MX7D_USB_OTG_PHY_STATUS_CHRGDET)) {
		dev_dbg(data->dev, "It is a standard downstream port\n");
		usb_phy->chg_type = SDP_TYPE;
	}

	return 0;
}

/*
 * Whole charger detection process:
 * 1. OPMODE override to be non-driving
 * 2. Data contact check
 * 3. Primary detection
 * 4. Secondary detection
 * 5. Disable charger detection
 */
static int imx7d_charger_detection(struct imx_usbmisc_data *data)
{
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);
	struct usb_phy *usb_phy = data->usb_phy;
	unsigned long flags;
	u32 val;
	int ret;

	/* Check if vbus is valid */
	val = readl(usbmisc->base + MX7D_USB_OTG_PHY_STATUS);
	if (!(val & MX7D_USB_OTG_PHY_STATUS_VBUS_VLD)) {
		dev_err(data->dev, "vbus is error\n");
		return -EINVAL;
	}

	/*
	 * Keep OPMODE to be non-driving mode during the whole
	 * charger detection process.
	 */
	spin_lock_irqsave(&usbmisc->lock, flags);
	val = readl(usbmisc->base + MX7D_USBNC_USB_CTRL2);
	val &= ~MX7D_USBNC_USB_CTRL2_OPMODE_OVERRIDE_MASK;
	val |= MX7D_USBNC_USB_CTRL2_OPMODE_NON_DRIVING;
	writel(val, usbmisc->base + MX7D_USBNC_USB_CTRL2);

	val = readl(usbmisc->base + MX7D_USBNC_USB_CTRL2);
	writel(val | MX7D_USBNC_USB_CTRL2_OPMODE_OVERRIDE_EN,
			usbmisc->base + MX7D_USBNC_USB_CTRL2);
	spin_unlock_irqrestore(&usbmisc->lock, flags);

	ret = imx7d_charger_data_contact_detect(data);
	if (ret)
		return ret;

	ret = imx7d_charger_primary_detection(data);
	if (!ret && usb_phy->chg_type != SDP_TYPE)
		ret = imx7d_charger_secondary_detection(data);

	imx7_disable_charger_detector(data);

	return ret;
}

static void usbmisc_imx7d_vbus_comparator_on(struct imx_usbmisc_data *data,
					     bool on)
{
	unsigned long flags;
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);
	u32 val;

	if (data->hsic)
		return;

	spin_lock_irqsave(&usbmisc->lock, flags);
	/*
	 * Disable VBUS valid comparator when in suspend mode,
	 * when OTG is disabled and DRVVBUS0 is asserted case
	 * the Bandgap circuitry and VBUS Valid comparator are
	 * still powered, even in Suspend or Sleep mode.
	 */
	val = readl(usbmisc->base + MX7D_USB_OTG_PHY_CFG2);
	if (on)
		val |= MX7D_USB_OTG_PHY_CFG2_DRVVBUS0;
	else
		val &= ~MX7D_USB_OTG_PHY_CFG2_DRVVBUS0;

	writel(val, usbmisc->base + MX7D_USB_OTG_PHY_CFG2);
	spin_unlock_irqrestore(&usbmisc->lock, flags);
}

static int usbmisc_imx7ulp_init(struct imx_usbmisc_data *data)
{
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);
	unsigned long flags;
	u32 reg;

	if (data->index >= 1)
		return -EINVAL;

	spin_lock_irqsave(&usbmisc->lock, flags);
	reg = readl(usbmisc->base);
	if (data->disable_oc) {
		reg |= MX6_BM_OVER_CUR_DIS;
	} else {
		reg &= ~MX6_BM_OVER_CUR_DIS;

		/*
		 * If the polarity is not configured keep it as setup by the
		 * bootloader.
		 */
		if (data->oc_pol_configured && data->oc_pol_active_low)
			reg |= MX6_BM_OVER_CUR_POLARITY;
		else if (data->oc_pol_configured)
			reg &= ~MX6_BM_OVER_CUR_POLARITY;
	}
	/* If the polarity is not set keep it as setup by the bootloader */
	if (data->pwr_pol == 1)
		reg |= MX6_BM_PWR_POLARITY;

	writel(reg, usbmisc->base);

	/* SoC non-burst setting */
	reg = readl(usbmisc->base);
	writel(reg | MX6_BM_NON_BURST_SETTING, usbmisc->base);

	if (data->hsic) {
		reg = readl(usbmisc->base);
		writel(reg | MX6_BM_UTMI_ON_CLOCK, usbmisc->base);

		reg = readl(usbmisc->base + MX6_USB_HSIC_CTRL_OFFSET);
		reg |= MX6_BM_HSIC_EN | MX6_BM_HSIC_CLK_ON;
		writel(reg, usbmisc->base + MX6_USB_HSIC_CTRL_OFFSET);

		/*
		 * For non-HSIC controller, the autoresume is enabled
		 * at MXS PHY driver (usbphy_ctrl bit18).
		 */
		reg = readl(usbmisc->base + MX7D_USBNC_USB_CTRL2);
		writel(reg | MX7D_USBNC_AUTO_RESUME,
			usbmisc->base + MX7D_USBNC_USB_CTRL2);
	} else {
		reg = readl(usbmisc->base + MX7D_USBNC_USB_CTRL2);
		reg &= ~MX7D_USB_VBUS_WAKEUP_SOURCE_MASK;
		writel(reg | MX7D_USB_VBUS_WAKEUP_SOURCE_BVALID,
			 usbmisc->base + MX7D_USBNC_USB_CTRL2);
	}

	spin_unlock_irqrestore(&usbmisc->lock, flags);

	usbmisc_imx7d_set_wakeup(data, false);

	return 0;
}

static int usbmisc_imx7d_power_lost_check(struct imx_usbmisc_data *data)
{
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&usbmisc->lock, flags);
	val = readl(usbmisc->base);
	spin_unlock_irqrestore(&usbmisc->lock, flags);
	/*
	 * Here use a power on reset value to judge
	 * if the controller experienced a power lost
	 */
	if (val == 0x30001000)
		return 1;
	else
		return 0;
}

static int usbmisc_imx6sx_power_lost_check(struct imx_usbmisc_data *data)
{
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&usbmisc->lock, flags);
	val = readl(usbmisc->base + data->index * 4);
	spin_unlock_irqrestore(&usbmisc->lock, flags);
	/*
	 * Here use a power on reset value to judge
	 * if the controller experienced a power lost
	 */
	if (val == 0x30001000)
		return 1;
	else
		return 0;
}

static const struct usbmisc_ops imx25_usbmisc_ops = {
	.init = usbmisc_imx25_init,
	.post = usbmisc_imx25_post,
};

static const struct usbmisc_ops imx27_usbmisc_ops = {
	.init = usbmisc_imx27_init,
};

static const struct usbmisc_ops imx51_usbmisc_ops = {
	.init = usbmisc_imx53_init,
};

static const struct usbmisc_ops imx53_usbmisc_ops = {
	.init = usbmisc_imx53_init,
};

static const struct usbmisc_ops imx6q_usbmisc_ops = {
	.set_wakeup = usbmisc_imx6q_set_wakeup,
	.init = usbmisc_imx6q_init,
	.hsic_set_connect = usbmisc_imx6_hsic_set_connect,
	.hsic_set_clk   = usbmisc_imx6_hsic_set_clk,
};

static const struct usbmisc_ops vf610_usbmisc_ops = {
	.init = usbmisc_vf610_init,
};

static const struct usbmisc_ops imx6sx_usbmisc_ops = {
	.set_wakeup = usbmisc_imx6q_set_wakeup,
	.init = usbmisc_imx6sx_init,
	.hsic_set_connect = usbmisc_imx6_hsic_set_connect,
	.hsic_set_clk = usbmisc_imx6_hsic_set_clk,
	.power_lost_check = usbmisc_imx6sx_power_lost_check,
};

static const struct usbmisc_ops imx7d_usbmisc_ops = {
	.init = usbmisc_imx7d_init,
	.set_wakeup = usbmisc_imx7d_set_wakeup,
	.charger_detection = imx7d_charger_detection,
	.power_lost_check = usbmisc_imx7d_power_lost_check,
	.vbus_comparator_on = usbmisc_imx7d_vbus_comparator_on,
};

static const struct usbmisc_ops imx7ulp_usbmisc_ops = {
	.init = usbmisc_imx7ulp_init,
	.set_wakeup = usbmisc_imx7d_set_wakeup,
	.hsic_set_connect = usbmisc_imx6_hsic_set_connect,
	.hsic_set_clk = usbmisc_imx6_hsic_set_clk,
	.power_lost_check = usbmisc_imx7d_power_lost_check,
};

static inline bool is_imx53_usbmisc(struct imx_usbmisc_data *data)
{
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);

	return usbmisc->ops == &imx53_usbmisc_ops;
}

int imx_usbmisc_init(struct imx_usbmisc_data *data)
{
	struct imx_usbmisc *usbmisc;

	if (!data)
		return 0;

	usbmisc = dev_get_drvdata(data->dev);
	if (!usbmisc->ops->init)
		return 0;
	return usbmisc->ops->init(data);
}
EXPORT_SYMBOL_GPL(imx_usbmisc_init);

int imx_usbmisc_init_post(struct imx_usbmisc_data *data)
{
	struct imx_usbmisc *usbmisc;
	int ret = 0;

	if (!data)
		return 0;

	usbmisc = dev_get_drvdata(data->dev);
	if (usbmisc->ops->post)
		ret = usbmisc->ops->post(data);
	if (ret) {
		dev_err(data->dev, "post init failed, ret=%d\n", ret);
		return ret;
	}

	if (usbmisc->ops->set_wakeup)
		ret = usbmisc->ops->set_wakeup(data, false);
	if (ret) {
		dev_err(data->dev, "set_wakeup failed, ret=%d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(imx_usbmisc_init_post);

int imx_usbmisc_hsic_set_connect(struct imx_usbmisc_data *data)
{
	struct imx_usbmisc *usbmisc;

	if (!data)
		return 0;

	usbmisc = dev_get_drvdata(data->dev);
	if (!usbmisc->ops->hsic_set_connect || !data->hsic)
		return 0;
	return usbmisc->ops->hsic_set_connect(data);
}
EXPORT_SYMBOL_GPL(imx_usbmisc_hsic_set_connect);

int imx_usbmisc_charger_detection(struct imx_usbmisc_data *data, bool connect)
{
	struct imx_usbmisc *usbmisc;
	struct usb_phy *usb_phy;
	int ret = 0;

	if (!data)
		return -EINVAL;

	usbmisc = dev_get_drvdata(data->dev);
	usb_phy = data->usb_phy;
	if (!usbmisc->ops->charger_detection)
		return -ENOTSUPP;

	if (connect) {
		ret = usbmisc->ops->charger_detection(data);
		if (ret) {
			dev_err(data->dev,
					"Error occurs during detection: %d\n",
					ret);
			usb_phy->chg_state = USB_CHARGER_ABSENT;
		} else {
			usb_phy->chg_state = USB_CHARGER_PRESENT;
		}
	} else {
		usb_phy->chg_state = USB_CHARGER_ABSENT;
		usb_phy->chg_type = UNKNOWN_TYPE;
	}
	return ret;
}
EXPORT_SYMBOL_GPL(imx_usbmisc_charger_detection);

int imx_usbmisc_suspend(struct imx_usbmisc_data *data, bool wakeup)
{
	struct imx_usbmisc *usbmisc;
	int ret = 0;

	if (!data)
		return 0;

	usbmisc = dev_get_drvdata(data->dev);

	if (usbmisc->ops->vbus_comparator_on)
		usbmisc->ops->vbus_comparator_on(data, false);

	if (wakeup && usbmisc->ops->set_wakeup)
		ret = usbmisc->ops->set_wakeup(data, true);
	if (ret) {
		dev_err(data->dev, "set_wakeup failed, ret=%d\n", ret);
		return ret;
	}

	if (usbmisc->ops->hsic_set_clk && data->hsic)
		ret = usbmisc->ops->hsic_set_clk(data, false);
	if (ret) {
		dev_err(data->dev, "hsic_set_clk failed, ret=%d\n", ret);
		return ret;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(imx_usbmisc_suspend);

int imx_usbmisc_resume(struct imx_usbmisc_data *data, bool wakeup)
{
	struct imx_usbmisc *usbmisc;
	int ret = 0;

	if (!data)
		return 0;

	usbmisc = dev_get_drvdata(data->dev);

	if (usbmisc->ops->power_lost_check)
		ret = usbmisc->ops->power_lost_check(data);
	if (ret > 0) {
		/* re-init if resume from power lost */
		ret = imx_usbmisc_init(data);
		if (ret) {
			dev_err(data->dev, "re-init failed, ret=%d\n", ret);
			return ret;
		}
	}

	if (wakeup && usbmisc->ops->set_wakeup)
		ret = usbmisc->ops->set_wakeup(data, false);
	if (ret) {
		dev_err(data->dev, "set_wakeup failed, ret=%d\n", ret);
		return ret;
	}

	if (usbmisc->ops->hsic_set_clk && data->hsic)
		ret = usbmisc->ops->hsic_set_clk(data, true);
	if (ret) {
		dev_err(data->dev, "hsic_set_clk failed, ret=%d\n", ret);
		goto hsic_set_clk_fail;
	}

	if (usbmisc->ops->vbus_comparator_on)
		usbmisc->ops->vbus_comparator_on(data, true);

	return 0;

hsic_set_clk_fail:
	if (wakeup && usbmisc->ops->set_wakeup)
		usbmisc->ops->set_wakeup(data, true);
	return ret;
}
EXPORT_SYMBOL_GPL(imx_usbmisc_resume);

static const struct of_device_id usbmisc_imx_dt_ids[] = {
	{
		.compatible = "fsl,imx25-usbmisc",
		.data = &imx25_usbmisc_ops,
	},
	{
		.compatible = "fsl,imx35-usbmisc",
		.data = &imx25_usbmisc_ops,
	},
	{
		.compatible = "fsl,imx27-usbmisc",
		.data = &imx27_usbmisc_ops,
	},
	{
		.compatible = "fsl,imx51-usbmisc",
		.data = &imx51_usbmisc_ops,
	},
	{
		.compatible = "fsl,imx53-usbmisc",
		.data = &imx53_usbmisc_ops,
	},
	{
		.compatible = "fsl,imx6q-usbmisc",
		.data = &imx6q_usbmisc_ops,
	},
	{
		.compatible = "fsl,vf610-usbmisc",
		.data = &vf610_usbmisc_ops,
	},
	{
		.compatible = "fsl,imx6sx-usbmisc",
		.data = &imx6sx_usbmisc_ops,
	},
	{
		.compatible = "fsl,imx6ul-usbmisc",
		.data = &imx6sx_usbmisc_ops,
	},
	{
		.compatible = "fsl,imx7d-usbmisc",
		.data = &imx7d_usbmisc_ops,
	},
	{
		.compatible = "fsl,imx7ulp-usbmisc",
		.data = &imx7ulp_usbmisc_ops,
	},
	{
		.compatible = "fsl,imx8ulp-usbmisc",
		.data = &imx7ulp_usbmisc_ops,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, usbmisc_imx_dt_ids);

static int usbmisc_imx_probe(struct platform_device *pdev)
{
	struct imx_usbmisc *data;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	spin_lock_init(&data->lock);

	data->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(data->base))
		return PTR_ERR(data->base);

	data->ops = of_device_get_match_data(&pdev->dev);
	platform_set_drvdata(pdev, data);

	return 0;
}

static struct platform_driver usbmisc_imx_driver = {
	.probe = usbmisc_imx_probe,
	.driver = {
		.name = "usbmisc_imx",
		.of_match_table = usbmisc_imx_dt_ids,
	 },
};

module_platform_driver(usbmisc_imx_driver);

MODULE_ALIAS("platform:usbmisc-imx");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("driver for imx usb non-core registers");
MODULE_AUTHOR("Richard Zhao <richard.zhao@freescale.com>");
