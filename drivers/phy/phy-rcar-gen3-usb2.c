/*
 * Renesas R-Car Gen3 for USB2.0 PHY driver
 *
 * Copyright (C) 2015 Renesas Electronics Corporation
 *
 * This is based on the phy-rcar-gen2 driver:
 * Copyright (C) 2014 Renesas Solutions Corp.
 * Copyright (C) 2014 Cogent Embedded, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/extcon.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

/******* USB2.0 Host registers (original offset is +0x200) *******/
#define USB2_INT_ENABLE		0x000
#define USB2_USBCTR		0x00c
#define USB2_SPD_RSM_TIMSET	0x10c
#define USB2_OC_TIMSET		0x110
#define USB2_COMMCTRL		0x600
#define USB2_OBINTSTA		0x604
#define USB2_OBINTEN		0x608
#define USB2_VBCTRL		0x60c
#define USB2_LINECTRL1		0x610
#define USB2_ADPCTRL		0x630

/* INT_ENABLE */
#define USB2_INT_ENABLE_UCOM_INTEN	BIT(3)
#define USB2_INT_ENABLE_USBH_INTB_EN	BIT(2)
#define USB2_INT_ENABLE_USBH_INTA_EN	BIT(1)
#define USB2_INT_ENABLE_INIT		(USB2_INT_ENABLE_UCOM_INTEN | \
					 USB2_INT_ENABLE_USBH_INTB_EN | \
					 USB2_INT_ENABLE_USBH_INTA_EN)

/* USBCTR */
#define USB2_USBCTR_DIRPD	BIT(2)
#define USB2_USBCTR_PLL_RST	BIT(1)

/* SPD_RSM_TIMSET */
#define USB2_SPD_RSM_TIMSET_INIT	0x014e029b

/* OC_TIMSET */
#define USB2_OC_TIMSET_INIT		0x000209ab

/* COMMCTRL */
#define USB2_COMMCTRL_OTG_PERI		BIT(31)	/* 1 = Peripheral mode */

/* OBINTSTA and OBINTEN */
#define USB2_OBINT_SESSVLDCHG		BIT(12)
#define USB2_OBINT_IDDIGCHG		BIT(11)
#define USB2_OBINT_BITS			(USB2_OBINT_SESSVLDCHG | \
					 USB2_OBINT_IDDIGCHG)

/* VBCTRL */
#define USB2_VBCTRL_DRVVBUSSEL		BIT(8)

/* LINECTRL1 */
#define USB2_LINECTRL1_DPRPD_EN		BIT(19)
#define USB2_LINECTRL1_DP_RPD		BIT(18)
#define USB2_LINECTRL1_DMRPD_EN		BIT(17)
#define USB2_LINECTRL1_DM_RPD		BIT(16)

/* ADPCTRL */
#define USB2_ADPCTRL_OTGSESSVLD		BIT(20)
#define USB2_ADPCTRL_IDDIG		BIT(19)
#define USB2_ADPCTRL_IDPULLUP		BIT(5)	/* 1 = ID sampling is enabled */
#define USB2_ADPCTRL_DRVVBUS		BIT(4)

struct rcar_gen3_chan {
	void __iomem *base;
	struct extcon_dev *extcon;
	struct phy *phy;
	struct regulator *vbus;
	bool has_otg;
};

static void rcar_gen3_set_host_mode(struct rcar_gen3_chan *ch, int host)
{
	void __iomem *usb2_base = ch->base;
	u32 val = readl(usb2_base + USB2_COMMCTRL);

	dev_vdbg(&ch->phy->dev, "%s: %08x, %d\n", __func__, val, host);
	if (host)
		val &= ~USB2_COMMCTRL_OTG_PERI;
	else
		val |= USB2_COMMCTRL_OTG_PERI;
	writel(val, usb2_base + USB2_COMMCTRL);
}

static void rcar_gen3_set_linectrl(struct rcar_gen3_chan *ch, int dp, int dm)
{
	void __iomem *usb2_base = ch->base;
	u32 val = readl(usb2_base + USB2_LINECTRL1);

	dev_vdbg(&ch->phy->dev, "%s: %08x, %d, %d\n", __func__, val, dp, dm);
	val &= ~(USB2_LINECTRL1_DP_RPD | USB2_LINECTRL1_DM_RPD);
	if (dp)
		val |= USB2_LINECTRL1_DP_RPD;
	if (dm)
		val |= USB2_LINECTRL1_DM_RPD;
	writel(val, usb2_base + USB2_LINECTRL1);
}

static void rcar_gen3_enable_vbus_ctrl(struct rcar_gen3_chan *ch, int vbus)
{
	void __iomem *usb2_base = ch->base;
	u32 val = readl(usb2_base + USB2_ADPCTRL);

	dev_vdbg(&ch->phy->dev, "%s: %08x, %d\n", __func__, val, vbus);
	if (vbus)
		val |= USB2_ADPCTRL_DRVVBUS;
	else
		val &= ~USB2_ADPCTRL_DRVVBUS;
	writel(val, usb2_base + USB2_ADPCTRL);
}

static void rcar_gen3_init_for_host(struct rcar_gen3_chan *ch)
{
	rcar_gen3_set_linectrl(ch, 1, 1);
	rcar_gen3_set_host_mode(ch, 1);
	rcar_gen3_enable_vbus_ctrl(ch, 1);

	extcon_set_cable_state_(ch->extcon, EXTCON_USB_HOST, true);
	extcon_set_cable_state_(ch->extcon, EXTCON_USB, false);
}

static void rcar_gen3_init_for_peri(struct rcar_gen3_chan *ch)
{
	rcar_gen3_set_linectrl(ch, 0, 1);
	rcar_gen3_set_host_mode(ch, 0);
	rcar_gen3_enable_vbus_ctrl(ch, 0);

	extcon_set_cable_state_(ch->extcon, EXTCON_USB_HOST, false);
	extcon_set_cable_state_(ch->extcon, EXTCON_USB, true);
}

static bool rcar_gen3_check_vbus(struct rcar_gen3_chan *ch)
{
	return !!(readl(ch->base + USB2_ADPCTRL) &
		  USB2_ADPCTRL_OTGSESSVLD);
}

static bool rcar_gen3_check_id(struct rcar_gen3_chan *ch)
{
	return !!(readl(ch->base + USB2_ADPCTRL) & USB2_ADPCTRL_IDDIG);
}

static void rcar_gen3_device_recognition(struct rcar_gen3_chan *ch)
{
	bool is_host = true;

	/* B-device? */
	if (rcar_gen3_check_id(ch) && rcar_gen3_check_vbus(ch))
		is_host = false;

	if (is_host)
		rcar_gen3_init_for_host(ch);
	else
		rcar_gen3_init_for_peri(ch);
}

static void rcar_gen3_init_otg(struct rcar_gen3_chan *ch)
{
	void __iomem *usb2_base = ch->base;
	u32 val;

	val = readl(usb2_base + USB2_VBCTRL);
	writel(val | USB2_VBCTRL_DRVVBUSSEL, usb2_base + USB2_VBCTRL);
	writel(USB2_OBINT_BITS, usb2_base + USB2_OBINTSTA);
	val = readl(usb2_base + USB2_OBINTEN);
	writel(val | USB2_OBINT_BITS, usb2_base + USB2_OBINTEN);
	val = readl(usb2_base + USB2_ADPCTRL);
	writel(val | USB2_ADPCTRL_IDPULLUP, usb2_base + USB2_ADPCTRL);
	val = readl(usb2_base + USB2_LINECTRL1);
	rcar_gen3_set_linectrl(ch, 0, 0);
	writel(val | USB2_LINECTRL1_DPRPD_EN | USB2_LINECTRL1_DMRPD_EN,
	       usb2_base + USB2_LINECTRL1);

	rcar_gen3_device_recognition(ch);
}

static int rcar_gen3_phy_usb2_init(struct phy *p)
{
	struct rcar_gen3_chan *channel = phy_get_drvdata(p);
	void __iomem *usb2_base = channel->base;

	/* Initialize USB2 part */
	writel(USB2_INT_ENABLE_INIT, usb2_base + USB2_INT_ENABLE);
	writel(USB2_SPD_RSM_TIMSET_INIT, usb2_base + USB2_SPD_RSM_TIMSET);
	writel(USB2_OC_TIMSET_INIT, usb2_base + USB2_OC_TIMSET);

	/* Initialize otg part */
	if (channel->has_otg)
		rcar_gen3_init_otg(channel);

	return 0;
}

static int rcar_gen3_phy_usb2_exit(struct phy *p)
{
	struct rcar_gen3_chan *channel = phy_get_drvdata(p);

	writel(0, channel->base + USB2_INT_ENABLE);

	return 0;
}

static int rcar_gen3_phy_usb2_power_on(struct phy *p)
{
	struct rcar_gen3_chan *channel = phy_get_drvdata(p);
	void __iomem *usb2_base = channel->base;
	u32 val;
	int ret;

	if (channel->vbus) {
		ret = regulator_enable(channel->vbus);
		if (ret)
			return ret;
	}

	val = readl(usb2_base + USB2_USBCTR);
	val |= USB2_USBCTR_PLL_RST;
	writel(val, usb2_base + USB2_USBCTR);
	val &= ~USB2_USBCTR_PLL_RST;
	writel(val, usb2_base + USB2_USBCTR);

	return 0;
}

static int rcar_gen3_phy_usb2_power_off(struct phy *p)
{
	struct rcar_gen3_chan *channel = phy_get_drvdata(p);
	int ret = 0;

	if (channel->vbus)
		ret = regulator_disable(channel->vbus);

	return ret;
}

static struct phy_ops rcar_gen3_phy_usb2_ops = {
	.init		= rcar_gen3_phy_usb2_init,
	.exit		= rcar_gen3_phy_usb2_exit,
	.power_on	= rcar_gen3_phy_usb2_power_on,
	.power_off	= rcar_gen3_phy_usb2_power_off,
	.owner		= THIS_MODULE,
};

static irqreturn_t rcar_gen3_phy_usb2_irq(int irq, void *_ch)
{
	struct rcar_gen3_chan *ch = _ch;
	void __iomem *usb2_base = ch->base;
	u32 status = readl(usb2_base + USB2_OBINTSTA);
	irqreturn_t ret = IRQ_NONE;

	if (status & USB2_OBINT_BITS) {
		dev_vdbg(&ch->phy->dev, "%s: %08x\n", __func__, status);
		writel(USB2_OBINT_BITS, usb2_base + USB2_OBINTSTA);
		rcar_gen3_device_recognition(ch);
		ret = IRQ_HANDLED;
	}

	return ret;
}

static const struct of_device_id rcar_gen3_phy_usb2_match_table[] = {
	{ .compatible = "renesas,usb2-phy-r8a7795" },
	{ .compatible = "renesas,rcar-gen3-usb2-phy" },
	{ }
};
MODULE_DEVICE_TABLE(of, rcar_gen3_phy_usb2_match_table);

static const unsigned int rcar_gen3_phy_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_NONE,
};

static int rcar_gen3_phy_usb2_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rcar_gen3_chan *channel;
	struct phy_provider *provider;
	struct resource *res;
	int irq;

	if (!dev->of_node) {
		dev_err(dev, "This driver needs device tree\n");
		return -EINVAL;
	}

	channel = devm_kzalloc(dev, sizeof(*channel), GFP_KERNEL);
	if (!channel)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	channel->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(channel->base))
		return PTR_ERR(channel->base);

	/* call request_irq for OTG */
	irq = platform_get_irq(pdev, 0);
	if (irq >= 0) {
		int ret;

		irq = devm_request_irq(dev, irq, rcar_gen3_phy_usb2_irq,
				       IRQF_SHARED, dev_name(dev), channel);
		if (irq < 0)
			dev_err(dev, "No irq handler (%d)\n", irq);
		channel->has_otg = true;
		channel->extcon = devm_extcon_dev_allocate(dev,
							rcar_gen3_phy_cable);
		if (IS_ERR(channel->extcon))
			return PTR_ERR(channel->extcon);

		ret = devm_extcon_dev_register(dev, channel->extcon);
		if (ret < 0) {
			dev_err(dev, "Failed to register extcon\n");
			return ret;
		}
	}

	/* devm_phy_create() will call pm_runtime_enable(dev); */
	channel->phy = devm_phy_create(dev, NULL, &rcar_gen3_phy_usb2_ops);
	if (IS_ERR(channel->phy)) {
		dev_err(dev, "Failed to create USB2 PHY\n");
		return PTR_ERR(channel->phy);
	}

	channel->vbus = devm_regulator_get_optional(dev, "vbus");
	if (IS_ERR(channel->vbus)) {
		if (PTR_ERR(channel->vbus) == -EPROBE_DEFER)
			return PTR_ERR(channel->vbus);
		channel->vbus = NULL;
	}

	phy_set_drvdata(channel->phy, channel);

	provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(provider))
		dev_err(dev, "Failed to register PHY provider\n");

	return PTR_ERR_OR_ZERO(provider);
}

static struct platform_driver rcar_gen3_phy_usb2_driver = {
	.driver = {
		.name		= "phy_rcar_gen3_usb2",
		.of_match_table	= rcar_gen3_phy_usb2_match_table,
	},
	.probe	= rcar_gen3_phy_usb2_probe,
};
module_platform_driver(rcar_gen3_phy_usb2_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Renesas R-Car Gen3 USB 2.0 PHY");
MODULE_AUTHOR("Yoshihiro Shimoda <yoshihiro.shimoda.uh@renesas.com>");
