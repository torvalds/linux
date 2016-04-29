/*
 * Copyright 2012-2015 Freescale Semiconductor, Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

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
#define MX53_USB_UH2_CTRL_OFFSET	0x14
#define MX53_USB_UH3_CTRL_OFFSET	0x18
#define MX53_BM_OVER_CUR_DIS_H1		BIT(5)
#define MX53_BM_OVER_CUR_DIS_OTG	BIT(8)
#define MX53_BM_OVER_CUR_DIS_UHx	BIT(30)
#define MX53_USB_PHYCTRL1_PLLDIV_MASK	0x3
#define MX53_USB_PLL_DIV_24_MHZ		0x01

#define MX6_BM_NON_BURST_SETTING	BIT(1)
#define MX6_BM_OVER_CUR_DIS		BIT(7)
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

#define ANADIG_ANA_MISC0		0x150
#define ANADIG_ANA_MISC0_SET		0x154
#define ANADIG_ANA_MISC0_CLK_DELAY(x)	((x >> 26) & 0x7)

#define ANADIG_USB1_CHRG_DETECT_SET	0x1b4
#define ANADIG_USB1_CHRG_DETECT_CLR	0x1b8
#define ANADIG_USB1_CHRG_DETECT_EN_B		BIT(20)
#define ANADIG_USB1_CHRG_DETECT_CHK_CHRG_B	BIT(19)
#define ANADIG_USB1_CHRG_DETECT_CHK_CONTACT	BIT(18)

#define ANADIG_USB1_VBUS_DET_STAT	0x1c0
#define ANADIG_USB1_VBUS_DET_STAT_VBUS_VALID	BIT(3)

#define ANADIG_USB1_CHRG_DET_STAT	0x1d0
#define ANADIG_USB1_CHRG_DET_STAT_DM_STATE	BIT(2)
#define ANADIG_USB1_CHRG_DET_STAT_CHRG_DETECTED	BIT(1)
#define ANADIG_USB1_CHRG_DET_STAT_PLUG_CONTACT	BIT(0)

#define MX7D_USBNC_USB_CTRL2		0x4
#define MX7D_USBNC_USB_CTRL2_OPMODE_OVERRIDE_EN		BIT(8)
#define MX7D_USBNC_USB_CTRL2_OPMODE_OVERRIDE_MASK	(BIT(7) | BIT(6))
#define MX7D_USBNC_USB_CTRL2_OPMODE(v)			(v << 6)
#define MX7D_USBNC_USB_CTRL2_OPMODE_NON_DRIVING	MX7D_USBNC_USB_CTRL2_OPMODE(1)

#define MX7D_USB_VBUS_WAKEUP_SOURCE_MASK	0x3
#define MX7D_USB_VBUS_WAKEUP_SOURCE(v)		(v << 0)
#define MX7D_USB_VBUS_WAKEUP_SOURCE_VBUS	MX7D_USB_VBUS_WAKEUP_SOURCE(0)
#define MX7D_USB_VBUS_WAKEUP_SOURCE_AVALID	MX7D_USB_VBUS_WAKEUP_SOURCE(1)
#define MX7D_USB_VBUS_WAKEUP_SOURCE_BVALID	MX7D_USB_VBUS_WAKEUP_SOURCE(2)
#define MX7D_USB_VBUS_WAKEUP_SOURCE_SESS_END	MX7D_USB_VBUS_WAKEUP_SOURCE(3)
#define MX7D_USB_TERMSEL_OVERRIDE	BIT(4)
#define MX7D_USB_TERMSEL_OVERRIDE_EN	BIT(5)

#define MX7D_USB_OTG_PHY_CFG2_CHRG_DCDENB	BIT(3)
#define MX7D_USB_OTG_PHY_CFG2_CHRG_VDATSRCENB0	BIT(2)
#define MX7D_USB_OTG_PHY_CFG2_CHRG_VDATDETENB0	BIT(1)
#define MX7D_USB_OTG_PHY_CFG2_CHRG_CHRGSEL	BIT(0)
#define MX7D_USB_OTG_PHY_CFG2		0x34

#define MX7D_USB_OTG_PHY_STATUS		0x3c
#define MX7D_USB_OTG_PHY_STATUS_CHRGDET		BIT(29)
#define MX7D_USB_OTG_PHY_STATUS_VBUS_VLD	BIT(3)
#define MX7D_USB_OTG_PHY_STATUS_LINE_STATE1	BIT(1)
#define MX7D_USB_OTG_PHY_STATUS_LINE_STATE0	BIT(0)

struct usbmisc_ops {
	/* It's called once when probe a usb device */
	int (*init)(struct imx_usbmisc_data *data);
	/* It's called once after adding a usb device */
	int (*post)(struct imx_usbmisc_data *data);
	/* It's called when we need to enable/disable usb wakeup */
	int (*set_wakeup)(struct imx_usbmisc_data *data, bool enabled);
	/* usb charger contact and primary detection */
	int (*charger_primary_detection)(struct imx_usbmisc_data *data);
	/* usb charger secondary detection */
	int (*charger_secondary_detection)(struct imx_usbmisc_data *data);
	/* It's called when system resume from usb power lost */
	int (*power_lost_check)(struct imx_usbmisc_data *data);
	/* It's called before setting portsc.suspendM */
	int (*hsic_set_connect)(struct imx_usbmisc_data *data);
	/* It's called during suspend/resume */
	int (*hsic_set_clk)(struct imx_usbmisc_data *data, bool enabled);
	/* override UTMI termination select */
	int (*term_select_override)(struct imx_usbmisc_data *data,
						bool enable, int val);
};

struct imx_usbmisc {
	void __iomem *base;
	spinlock_t lock;
	const struct usbmisc_ops *ops;
};

static struct regulator *vbus_wakeup_reg;

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
		writel(val, usbmisc->base);
		break;
	case 1:
		val = readl(usbmisc->base);
		val &= ~(MX25_H1_SIC_MASK | MX25_H1_PP_BIT |  MX25_H1_IPPUE_UP_BIT);
		val |= (MX25_EHCI_INTERFACE_SINGLE_UNI & MX25_EHCI_INTERFACE_MASK) << MX25_H1_SIC_SHIFT;
		val |= (MX25_H1_PM_BIT | MX25_H1_OCPOL_BIT | MX25_H1_TLL_BIT |
			MX25_H1_USBTE_BIT | MX25_H1_IPPUE_DOWN_BIT);

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

	if (data->evdo) {
		spin_lock_irqsave(&usbmisc->lock, flags);
		reg = usbmisc->base + MX25_USB_PHY_CTRL_OFFSET;
		val = readl(reg);
		writel(val | MX25_BM_EXTERNAL_VBUS_DIVIDER, reg);
		spin_unlock_irqrestore(&usbmisc->lock, flags);
		usleep_range(5000, 10000); /* needed to stabilize voltage */
	}

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

	if (data->disable_oc) {
		spin_lock_irqsave(&usbmisc->lock, flags);
		switch (data->index) {
		case 0:
			reg = usbmisc->base + MX53_USB_OTG_PHY_CTRL_0_OFFSET;
			val = readl(reg) | MX53_BM_OVER_CUR_DIS_OTG;
			break;
		case 1:
			reg = usbmisc->base + MX53_USB_OTG_PHY_CTRL_0_OFFSET;
			val = readl(reg) | MX53_BM_OVER_CUR_DIS_H1;
			break;
		case 2:
			reg = usbmisc->base + MX53_USB_UH2_CTRL_OFFSET;
			val = readl(reg) | MX53_BM_OVER_CUR_DIS_UHx;
			break;
		case 3:
			reg = usbmisc->base + MX53_USB_UH3_CTRL_OFFSET;
			val = readl(reg) | MX53_BM_OVER_CUR_DIS_UHx;
			break;
		}
		if (reg && val)
			writel(val, reg);
		spin_unlock_irqrestore(&usbmisc->lock, flags);
	}

	return 0;
}

static int usbmisc_imx6_hsic_set_connect(struct imx_usbmisc_data *data)
{
	unsigned long flags;
	u32 val;
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);

	spin_lock_irqsave(&usbmisc->lock, flags);
	if (data->index == 2 || data->index == 3) {
		val = readl(usbmisc->base + MX6_USB_HSIC_CTRL_OFFSET
						+ (data->index - 2) * 4);
		if (!(val & MX6_BM_HSIC_DEV_CONN))
			writel(val | MX6_BM_HSIC_DEV_CONN,
				usbmisc->base + MX6_USB_HSIC_CTRL_OFFSET
						+ (data->index - 2) * 4);
	}
	spin_unlock_irqrestore(&usbmisc->lock, flags);

	return 0;
}

static int usbmisc_imx6_hsic_set_clk(struct imx_usbmisc_data *data, bool on)
{
	unsigned long flags;
	u32 val;
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);

	spin_lock_irqsave(&usbmisc->lock, flags);
	if (data->index == 2 || data->index == 3) {
		val = readl(usbmisc->base + MX6_USB_HSIC_CTRL_OFFSET
						+ (data->index - 2) * 4);
		val |= MX6_BM_HSIC_EN | MX6_BM_HSIC_CLK_ON;
		if (on)
			val |= MX6_BM_HSIC_CLK_ON;
		else
			val &= ~MX6_BM_HSIC_CLK_ON;
		writel(val, usbmisc->base + MX6_USB_HSIC_CTRL_OFFSET
						+ (data->index - 2) * 4);
	}
	spin_unlock_irqrestore(&usbmisc->lock, flags);

	return 0;
}

static u32 imx6q_finalize_wakeup_setting(struct imx_usbmisc_data *data)
{
	if (data->available_role == USB_DR_MODE_PERIPHERAL)
		return MX6_BM_VBUS_WAKEUP;
	else if (data->available_role == USB_DR_MODE_OTG)
		return MX6_BM_VBUS_WAKEUP | MX6_BM_ID_WAKEUP;

	return 0;
}

static int usbmisc_imx6q_set_wakeup
	(struct imx_usbmisc_data *data, bool enabled)
{
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);
	unsigned long flags;
	int ret = 0;
	u32 val, wakeup_setting = MX6_BM_WAKEUP_ENABLE;

	if (data->index > 3)
		return -EINVAL;

	spin_lock_irqsave(&usbmisc->lock, flags);
	val = readl(usbmisc->base + data->index * 4);
	if (enabled) {
		wakeup_setting |= imx6q_finalize_wakeup_setting(data);
		writel(val | wakeup_setting, usbmisc->base + data->index * 4);
		spin_unlock_irqrestore(&usbmisc->lock, flags);
		if (vbus_wakeup_reg)
			ret = regulator_enable(vbus_wakeup_reg);
	} else {
		if (val & MX6_BM_WAKEUP_INTR)
			pr_debug("wakeup int at ci_hdrc.%d\n", data->index);
		wakeup_setting |= MX6_BM_VBUS_WAKEUP | MX6_BM_ID_WAKEUP;
		writel(val & ~wakeup_setting, usbmisc->base + data->index * 4);
		spin_unlock_irqrestore(&usbmisc->lock, flags);
		if (vbus_wakeup_reg && regulator_is_enabled(vbus_wakeup_reg))
			regulator_disable(vbus_wakeup_reg);
	}

	return ret;
}

static int usbmisc_imx6q_init(struct imx_usbmisc_data *data)
{
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);
	unsigned long flags;
	u32 reg, val;

	if (data->index > 3)
		return -EINVAL;

	spin_lock_irqsave(&usbmisc->lock, flags);

	if (data->disable_oc) {
		reg = readl(usbmisc->base + data->index * 4);
		writel(reg | MX6_BM_OVER_CUR_DIS,
			usbmisc->base + data->index * 4);
	}

	/* SoC non-burst setting */
	reg = readl(usbmisc->base + data->index * 4);
	writel(reg | MX6_BM_NON_BURST_SETTING,
			usbmisc->base + data->index * 4);

	/* For HSIC controller */
	if (data->index == 2 || data->index == 3) {
		val = readl(usbmisc->base + data->index * 4);
		writel(val | MX6_BM_UTMI_ON_CLOCK,
			usbmisc->base + data->index * 4);
		val = readl(usbmisc->base + MX6_USB_HSIC_CTRL_OFFSET
			+ (data->index - 2) * 4);
		val |= MX6_BM_HSIC_EN | MX6_BM_HSIC_CLK_ON;
		writel(val, usbmisc->base + MX6_USB_HSIC_CTRL_OFFSET
			+ (data->index - 2) * 4);

		/*
		 * Need to add delay to wait 24M OSC to be stable,
		 * It is board specific.
		 */
		regmap_read(data->anatop, ANADIG_ANA_MISC0, &val);
		/* 0 <= data->osc_clkgate_delay <= 7 */
		if (data->osc_clkgate_delay > ANADIG_ANA_MISC0_CLK_DELAY(val))
			regmap_write(data->anatop, ANADIG_ANA_MISC0_SET,
				(data->osc_clkgate_delay) << 26);
	}
	spin_unlock_irqrestore(&usbmisc->lock, flags);

	usbmisc_imx6q_set_wakeup(data, false);

	return 0;
}

static int usbmisc_imx6sx_init(struct imx_usbmisc_data *data)
{
	void __iomem *reg = NULL;
	unsigned long flags;
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);
	u32 val;

	usbmisc_imx6q_init(data);

	spin_lock_irqsave(&usbmisc->lock, flags);
	if (data->index == 0 || data->index == 1) {
		reg = usbmisc->base + MX6_USB_OTG1_PHY_CTRL + data->index * 4;
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
	}

	/* For HSIC controller */
	if (data->index == 2) {
		val = readl(usbmisc->base + MX6_USB_HSIC_CTRL_OFFSET
						+ (data->index - 2) * 4);
		val |= MX6SX_BM_HSIC_AUTO_RESUME;
		writel(val, usbmisc->base + MX6_USB_HSIC_CTRL_OFFSET
						+ (data->index - 2) * 4);
	}
	spin_unlock_irqrestore(&usbmisc->lock, flags);

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

/***************************************************************************/
/*                         imx usb charger detecton                        */
/***************************************************************************/
static void usb_charger_is_present(struct usb_charger *charger, bool present)
{
	if (present)
		charger->present = 1;
	else
		charger->present = 0;

	power_supply_changed(charger->psy);
	sysfs_notify(&charger->psy->dev.kobj, NULL, "present");
}

static void imx6_disable_charger_detector(struct imx_usbmisc_data *data)
{
	struct regmap *regmap = data->anatop;

	regmap_write(regmap, ANADIG_USB1_CHRG_DETECT_SET,
				ANADIG_USB1_CHRG_DETECT_EN_B |
				ANADIG_USB1_CHRG_DETECT_CHK_CHRG_B);
}

static int imx6_charger_data_contact_detect(struct imx_usbmisc_data *data)
{
	struct regmap *regmap = data->anatop;
	struct usb_charger *charger = data->charger;
	u32 val;
	int i, data_pin_contact_count = 0;

	/* check if vbus is valid */
	regmap_read(regmap, ANADIG_USB1_VBUS_DET_STAT, &val);
	if (!(val & ANADIG_USB1_VBUS_DET_STAT_VBUS_VALID)) {
		dev_err(charger->dev, "vbus is error\n");
		return -EINVAL;
	}

	/* Enable charger detector */
	regmap_write(regmap, ANADIG_USB1_CHRG_DETECT_CLR,
				ANADIG_USB1_CHRG_DETECT_EN_B);
	/*
	 * - Do not check whether a charger is connected to the USB port
	 * - Check whether the USB plug has been in contact with each other
	 */
	regmap_write(regmap, ANADIG_USB1_CHRG_DETECT_SET,
			ANADIG_USB1_CHRG_DETECT_CHK_CONTACT |
			ANADIG_USB1_CHRG_DETECT_CHK_CHRG_B);

	/* Check if plug is connected */
	for (i = 0; i < 100; i = i + 1) {
		regmap_read(regmap, ANADIG_USB1_CHRG_DET_STAT, &val);
		if (val & ANADIG_USB1_CHRG_DET_STAT_PLUG_CONTACT) {
			data_pin_contact_count++;
			if (data_pin_contact_count > 5)
				/* Data pin makes contact */
				break;
			else
				usleep_range(5000, 10000);
		} else {
			data_pin_contact_count = 0;
			usleep_range(5000, 6000);
		}
	}

	if (i == 100) {
		dev_err(charger->dev,
			"VBUS is coming from a dedicated power supply.\n");
		imx6_disable_charger_detector(data);
		return -ENXIO;
	}

	return 0;
}

static int imx6_charger_primary_detection(struct imx_usbmisc_data *data)
{
	struct regmap *regmap = data->anatop;
	struct usb_charger *charger = data->charger;
	u32 val;
	int ret;

	ret = imx6_charger_data_contact_detect(data);
	if (ret)
		return ret;

	/*
	 * - Do check whether a charger is connected to the USB port
	 * - Do not Check whether the USB plug has been in contact with
	 * each other
	 */
	regmap_write(regmap, ANADIG_USB1_CHRG_DETECT_CLR,
			ANADIG_USB1_CHRG_DETECT_CHK_CONTACT |
			ANADIG_USB1_CHRG_DETECT_CHK_CHRG_B);

	msleep(100);

	/* Check if it is a charger */
	regmap_read(regmap, ANADIG_USB1_CHRG_DET_STAT, &val);
	if (!(val & ANADIG_USB1_CHRG_DET_STAT_CHRG_DETECTED)) {
		dev_dbg(charger->dev, "It is a stardard downstream port\n");
		charger->psy_desc.type = POWER_SUPPLY_TYPE_USB;
		charger->max_current = 500;
	}

	imx6_disable_charger_detector(data);
	return 0;
}

/*
 * It must be called after dp is pulled up (from USB controller driver),
 * That is used to differentiate DCP and CDP
 */
int imx6_charger_secondary_detection(struct imx_usbmisc_data *data)
{
	struct regmap *regmap = data->anatop;
	struct usb_charger *charger = data->charger;
	int val;

	msleep(80);

	mutex_lock(&charger->lock);
	regmap_read(regmap, ANADIG_USB1_CHRG_DET_STAT, &val);
	if (val & ANADIG_USB1_CHRG_DET_STAT_DM_STATE) {
		dev_dbg(charger->dev, "It is a dedicate charging port\n");
		charger->psy_desc.type = POWER_SUPPLY_TYPE_USB_DCP;
		charger->max_current = 1500;
	} else {
		dev_dbg(charger->dev, "It is a charging downstream port\n");
		charger->psy_desc.type = POWER_SUPPLY_TYPE_USB_CDP;
		charger->max_current = 900;
	}

	usb_charger_is_present(charger, true);
	mutex_unlock(&charger->lock);

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

static int usbmisc_imx7d_set_wakeup
	(struct imx_usbmisc_data *data, bool enabled)
{
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);
	unsigned long flags;
	u32 val;
	u32 wakeup_setting = MX6_BM_WAKEUP_ENABLE;
	int ret = 0;

	spin_lock_irqsave(&usbmisc->lock, flags);
	val = readl(usbmisc->base);
	if (enabled) {
		wakeup_setting |= imx6q_finalize_wakeup_setting(data);
		writel(val | wakeup_setting, usbmisc->base);
	} else {
		if (val & MX6_BM_WAKEUP_INTR)
			dev_dbg(data->dev, "wakeup int\n");
		wakeup_setting |= MX6_BM_VBUS_WAKEUP | MX6_BM_ID_WAKEUP;
		writel(val & ~wakeup_setting, usbmisc->base);
	}
	spin_unlock_irqrestore(&usbmisc->lock, flags);

	return ret;
}

static int usbmisc_imx7d_init(struct imx_usbmisc_data *data)
{
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);
	unsigned long flags;
	u32 reg;

	if (data->index >= 1)
		return -EINVAL;

	spin_lock_irqsave(&usbmisc->lock, flags);
	if (data->disable_oc) {
		reg = readl(usbmisc->base);
		writel(reg | MX6_BM_OVER_CUR_DIS, usbmisc->base);
	}

	reg = readl(usbmisc->base + MX7D_USBNC_USB_CTRL2);
	reg &= ~MX7D_USB_VBUS_WAKEUP_SOURCE_MASK;
	writel(reg | MX7D_USB_VBUS_WAKEUP_SOURCE_BVALID,
		 usbmisc->base + MX7D_USBNC_USB_CTRL2);
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
	struct usb_charger *charger = data->charger;
	unsigned long flags;
	u32 val;
	int i, data_pin_contact_count = 0;

	spin_lock_irqsave(&usbmisc->lock, flags);

	/* check if vbus is valid */
	val = readl(usbmisc->base + MX7D_USB_OTG_PHY_STATUS);
	if (!(val & MX7D_USB_OTG_PHY_STATUS_VBUS_VLD)) {
		dev_err(charger->dev, "vbus is error\n");
		spin_unlock_irqrestore(&usbmisc->lock, flags);
		return -EINVAL;
	}

	/*
	 * - Do not check whether a charger is connected to the USB port
	 * - Check whether the USB plug has been in contact with each other
	 */
	val = readl(usbmisc->base + MX7D_USB_OTG_PHY_CFG2);
	writel(val | MX7D_USB_OTG_PHY_CFG2_CHRG_DCDENB,
			usbmisc->base + MX7D_USB_OTG_PHY_CFG2);

	spin_unlock_irqrestore(&usbmisc->lock, flags);

	/* Check if plug is connected */
	for (i = 0; i < 100; i = i + 1) {
		val = readl(usbmisc->base + MX7D_USB_OTG_PHY_STATUS);
		if (!(val & MX7D_USB_OTG_PHY_STATUS_LINE_STATE0)) {
			if (data_pin_contact_count++ > 5)
				/* Data pin makes contact */
				break;
			else
				usleep_range(5000, 10000);
		} else {
			data_pin_contact_count = 0;
			usleep_range(5000, 6000);
		}
	}

	if (i == 100) {
		dev_err(charger->dev,
			"VBUS is coming from a dedicated power supply.\n");
		imx7_disable_charger_detector(data);
		return -ENXIO;
	}

	return 0;
}

static int imx7d_charger_primary_detection(struct imx_usbmisc_data *data)
{
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);
	struct usb_charger *charger = data->charger;
	unsigned long flags;
	u32 val;
	int ret;

	ret = imx7d_charger_data_contact_detect(data);
	if (ret)
		return ret;

	spin_lock_irqsave(&usbmisc->lock, flags);
	/* Set OPMODE to be non-driving mode */
	val = readl(usbmisc->base + MX7D_USBNC_USB_CTRL2);
	val &= ~MX7D_USBNC_USB_CTRL2_OPMODE_OVERRIDE_MASK;
	val |= MX7D_USBNC_USB_CTRL2_OPMODE_NON_DRIVING;
	writel(val, usbmisc->base + MX7D_USBNC_USB_CTRL2);

	val = readl(usbmisc->base + MX7D_USBNC_USB_CTRL2);
	writel(val | MX7D_USBNC_USB_CTRL2_OPMODE_OVERRIDE_EN,
			usbmisc->base + MX7D_USBNC_USB_CTRL2);

	/*
	 * - Do check whether a charger is connected to the USB port
	 * - Do not Check whether the USB plug has been in contact with
	 * each other
	 */
	val = readl(usbmisc->base + MX7D_USB_OTG_PHY_CFG2);
	writel(val | MX7D_USB_OTG_PHY_CFG2_CHRG_VDATSRCENB0 |
			MX7D_USB_OTG_PHY_CFG2_CHRG_VDATDETENB0,
				usbmisc->base + MX7D_USB_OTG_PHY_CFG2);
	spin_unlock_irqrestore(&usbmisc->lock, flags);

	usleep_range(1000, 2000);

	/* Check if it is a charger */
	val = readl(usbmisc->base + MX7D_USB_OTG_PHY_STATUS);
	if (!(val & MX7D_USB_OTG_PHY_STATUS_CHRGDET)) {
		dev_dbg(charger->dev, "It is a stardard downstream port\n");
		charger->psy_desc.type = POWER_SUPPLY_TYPE_USB;
		charger->max_current = 500;
	}

	imx7_disable_charger_detector(data);

	return 0;
}

/*
 * It must be called after dp is pulled up (from USB controller driver),
 * That is used to differentiate DCP and CDP
 */
int imx7d_charger_secondary_detection(struct imx_usbmisc_data *data)
{
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);
	struct usb_charger *charger = data->charger;
	int val;

	msleep(80);

	mutex_lock(&charger->lock);
	val = readl(usbmisc->base + MX7D_USB_OTG_PHY_STATUS);
	if (val & MX7D_USB_OTG_PHY_STATUS_LINE_STATE1) {
		dev_dbg(charger->dev, "It is a dedicate charging port\n");
		charger->psy_desc.type = POWER_SUPPLY_TYPE_USB_DCP;
		charger->max_current = 1500;
	} else {
		dev_dbg(charger->dev, "It is a charging downstream port\n");
		charger->psy_desc.type = POWER_SUPPLY_TYPE_USB_CDP;
		charger->max_current = 900;
	}

	usb_charger_is_present(charger, true);
	mutex_unlock(&charger->lock);

	return 0;
}

static int usbmisc_term_select_override(struct imx_usbmisc_data *data,
						bool enable, int val)
{
	struct imx_usbmisc *usbmisc = dev_get_drvdata(data->dev);
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&usbmisc->lock, flags);

	reg = readl(usbmisc->base + MX7D_USBNC_USB_CTRL2);
	if (enable) {
		if (val)
			writel(reg | MX7D_USB_TERMSEL_OVERRIDE,
				usbmisc->base + MX7D_USBNC_USB_CTRL2);
		else
			writel(reg & ~MX7D_USB_TERMSEL_OVERRIDE,
				usbmisc->base + MX7D_USBNC_USB_CTRL2);

		reg = readl(usbmisc->base + MX7D_USBNC_USB_CTRL2);
		writel(reg | MX7D_USB_TERMSEL_OVERRIDE_EN,
			usbmisc->base + MX7D_USBNC_USB_CTRL2);
	} else {
		writel(reg & ~MX7D_USB_TERMSEL_OVERRIDE_EN,
			usbmisc->base + MX7D_USBNC_USB_CTRL2);
	}

	spin_unlock_irqrestore(&usbmisc->lock, flags);

	return 0;
}

static const struct usbmisc_ops imx25_usbmisc_ops = {
	.init = usbmisc_imx25_init,
	.post = usbmisc_imx25_post,
};

static const struct usbmisc_ops imx27_usbmisc_ops = {
	.init = usbmisc_imx27_init,
};

static const struct usbmisc_ops imx53_usbmisc_ops = {
	.init = usbmisc_imx53_init,
};

static const struct usbmisc_ops imx6q_usbmisc_ops = {
	.set_wakeup = usbmisc_imx6q_set_wakeup,
	.init = usbmisc_imx6q_init,
	.charger_primary_detection = imx6_charger_primary_detection,
	.charger_secondary_detection = imx6_charger_secondary_detection,
	.hsic_set_connect = usbmisc_imx6_hsic_set_connect,
	.hsic_set_clk   = usbmisc_imx6_hsic_set_clk,
};

static const struct usbmisc_ops vf610_usbmisc_ops = {
	.init = usbmisc_vf610_init,
};

static const struct usbmisc_ops imx6sx_usbmisc_ops = {
	.set_wakeup = usbmisc_imx6q_set_wakeup,
	.init = usbmisc_imx6sx_init,
	.charger_primary_detection = imx6_charger_primary_detection,
	.charger_secondary_detection = imx6_charger_secondary_detection,
	.power_lost_check = usbmisc_imx6sx_power_lost_check,
	.hsic_set_connect = usbmisc_imx6_hsic_set_connect,
	.hsic_set_clk = usbmisc_imx6_hsic_set_clk,
};

static const struct usbmisc_ops imx7d_usbmisc_ops = {
	.init = usbmisc_imx7d_init,
	.set_wakeup = usbmisc_imx7d_set_wakeup,
	.power_lost_check = usbmisc_imx7d_power_lost_check,
	.charger_primary_detection = imx7d_charger_primary_detection,
	.charger_secondary_detection = imx7d_charger_secondary_detection,
	.term_select_override = usbmisc_term_select_override,
};

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

	if (!data)
		return 0;

	usbmisc = dev_get_drvdata(data->dev);
	if (!usbmisc->ops->post)
		return 0;
	return usbmisc->ops->post(data);
}
EXPORT_SYMBOL_GPL(imx_usbmisc_init_post);

int imx_usbmisc_set_wakeup(struct imx_usbmisc_data *data, bool enabled)
{
	struct imx_usbmisc *usbmisc;

	if (!data)
		return 0;

	usbmisc = dev_get_drvdata(data->dev);
	if (!usbmisc->ops->set_wakeup)
		return 0;
	return usbmisc->ops->set_wakeup(data, enabled);
}
EXPORT_SYMBOL_GPL(imx_usbmisc_set_wakeup);

int imx_usbmisc_charger_detection(struct imx_usbmisc_data *data, bool connect)
{
	struct imx_usbmisc *usbmisc;
	struct usb_charger *charger;
	int ret = 0;

	if (!data)
		return -EINVAL;

	charger = data->charger;
	usbmisc = dev_get_drvdata(data->dev);
	if (!usbmisc->ops->charger_primary_detection)
		return -ENOTSUPP;

	mutex_lock(&charger->lock);
	if (connect) {
		charger->online = 1;
		ret = usbmisc->ops->charger_primary_detection(data);
		if (ret) {
			dev_err(charger->dev,
					"Error occurs during detection: %d\n",
					ret);
		} else {
			if (charger->psy_desc.type == POWER_SUPPLY_TYPE_USB)
				usb_charger_is_present(charger, true);
		}
	} else {
		charger->online = 0;
		charger->max_current = 0;
		charger->psy_desc.type = POWER_SUPPLY_TYPE_MAINS;

		usb_charger_is_present(charger, false);
	}
	mutex_unlock(&charger->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(imx_usbmisc_charger_detection);

int imx_usbmisc_charger_secondary_detection(struct imx_usbmisc_data *data)
{
	struct imx_usbmisc *usbmisc;

	if (!data)
		return 0;

	usbmisc = dev_get_drvdata(data->dev);
	if (!usbmisc->ops->charger_secondary_detection)
		return 0;
	return usbmisc->ops->charger_secondary_detection(data);
}
EXPORT_SYMBOL_GPL(imx_usbmisc_charger_secondary_detection);

int imx_usbmisc_power_lost_check(struct imx_usbmisc_data *data)
{
	struct imx_usbmisc *usbmisc;

	if (!data)
		return 0;

	usbmisc = dev_get_drvdata(data->dev);
	if (!usbmisc->ops->power_lost_check)
		return 0;
	return usbmisc->ops->power_lost_check(data);
}
EXPORT_SYMBOL_GPL(imx_usbmisc_power_lost_check);

int imx_usbmisc_hsic_set_connect(struct imx_usbmisc_data *data)
{
	struct imx_usbmisc *usbmisc;

	if (!data)
		return 0;

	usbmisc = dev_get_drvdata(data->dev);
	if (!usbmisc->ops->hsic_set_connect)
		return 0;
	return usbmisc->ops->hsic_set_connect(data);
}
EXPORT_SYMBOL_GPL(imx_usbmisc_hsic_set_connect);

int imx_usbmisc_hsic_set_clk(struct imx_usbmisc_data *data, bool on)
{
	struct imx_usbmisc *usbmisc;

	if (!data)
		return 0;

	usbmisc = dev_get_drvdata(data->dev);
	if (!usbmisc->ops->hsic_set_clk)
		return 0;
	return usbmisc->ops->hsic_set_clk(data, on);
}
EXPORT_SYMBOL_GPL(imx_usbmisc_hsic_set_clk);

int imx_usbmisc_term_select_override(struct imx_usbmisc_data *data,
						bool enable, int val)
{
	struct imx_usbmisc *usbmisc;

	if (!data)
		return 0;

	usbmisc = dev_get_drvdata(data->dev);
	if (!usbmisc->ops->term_select_override)
		return 0;
	return usbmisc->ops->term_select_override(data, enable, val);
}
EXPORT_SYMBOL_GPL(imx_usbmisc_term_select_override);

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
		.data = &imx53_usbmisc_ops,
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
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, usbmisc_imx_dt_ids);

static int usbmisc_imx_probe(struct platform_device *pdev)
{
	struct resource	*res;
	struct imx_usbmisc *data;
	struct of_device_id *tmp_dev;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	spin_lock_init(&data->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(data->base))
		return PTR_ERR(data->base);

	tmp_dev = (struct of_device_id *)
		of_match_device(usbmisc_imx_dt_ids, &pdev->dev);
	data->ops = (const struct usbmisc_ops *)tmp_dev->data;
	platform_set_drvdata(pdev, data);

	vbus_wakeup_reg = devm_regulator_get(&pdev->dev, "vbus-wakeup");
	if (PTR_ERR(vbus_wakeup_reg) == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	else if (PTR_ERR(vbus_wakeup_reg) == -ENODEV)
		/* no vbus regualator is needed */
		vbus_wakeup_reg = NULL;
	else if (IS_ERR(vbus_wakeup_reg)) {
		dev_err(&pdev->dev, "Getting regulator error: %ld\n",
			PTR_ERR(vbus_wakeup_reg));
		return PTR_ERR(vbus_wakeup_reg);
	}

	return 0;
}

static int usbmisc_imx_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver usbmisc_imx_driver = {
	.probe = usbmisc_imx_probe,
	.remove = usbmisc_imx_remove,
	.driver = {
		.name = "usbmisc_imx",
		.of_match_table = usbmisc_imx_dt_ids,
	 },
};

module_platform_driver(usbmisc_imx_driver);

MODULE_ALIAS("platform:usbmisc-imx");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("driver for imx usb non-core registers");
MODULE_AUTHOR("Richard Zhao <richard.zhao@freescale.com>");
