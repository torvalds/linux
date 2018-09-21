// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas R-Car Gen3 for USB2.0 PHY driver
 *
 * Copyright (C) 2015-2017 Renesas Electronics Corporation
 *
 * This is based on the phy-rcar-gen2 driver:
 * Copyright (C) 2014 Renesas Solutions Corp.
 * Copyright (C) 2014 Cogent Embedded, Inc.
 */

#include <linux/extcon-provider.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/usb/of.h>
#include <linux/workqueue.h>

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
#define USB2_LINECTRL1_OPMODE_NODRV	BIT(6)

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
	struct work_struct work;
	enum usb_dr_mode dr_mode;
	bool extcon_host;
	bool uses_otg_pins;
};

static void rcar_gen3_phy_usb2_work(struct work_struct *work)
{
	struct rcar_gen3_chan *ch = container_of(work, struct rcar_gen3_chan,
						 work);

	if (ch->extcon_host) {
		extcon_set_state_sync(ch->extcon, EXTCON_USB_HOST, true);
		extcon_set_state_sync(ch->extcon, EXTCON_USB, false);
	} else {
		extcon_set_state_sync(ch->extcon, EXTCON_USB_HOST, false);
		extcon_set_state_sync(ch->extcon, EXTCON_USB, true);
	}
}

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

static void rcar_gen3_control_otg_irq(struct rcar_gen3_chan *ch, int enable)
{
	void __iomem *usb2_base = ch->base;
	u32 val = readl(usb2_base + USB2_OBINTEN);

	if (enable)
		val |= USB2_OBINT_BITS;
	else
		val &= ~USB2_OBINT_BITS;
	writel(val, usb2_base + USB2_OBINTEN);
}

static void rcar_gen3_init_for_host(struct rcar_gen3_chan *ch)
{
	rcar_gen3_set_linectrl(ch, 1, 1);
	rcar_gen3_set_host_mode(ch, 1);
	rcar_gen3_enable_vbus_ctrl(ch, 1);

	ch->extcon_host = true;
	schedule_work(&ch->work);
}

static void rcar_gen3_init_for_peri(struct rcar_gen3_chan *ch)
{
	rcar_gen3_set_linectrl(ch, 0, 1);
	rcar_gen3_set_host_mode(ch, 0);
	rcar_gen3_enable_vbus_ctrl(ch, 0);

	ch->extcon_host = false;
	schedule_work(&ch->work);
}

static void rcar_gen3_init_for_b_host(struct rcar_gen3_chan *ch)
{
	void __iomem *usb2_base = ch->base;
	u32 val;

	val = readl(usb2_base + USB2_LINECTRL1);
	writel(val | USB2_LINECTRL1_OPMODE_NODRV, usb2_base + USB2_LINECTRL1);

	rcar_gen3_set_linectrl(ch, 1, 1);
	rcar_gen3_set_host_mode(ch, 1);
	rcar_gen3_enable_vbus_ctrl(ch, 0);

	val = readl(usb2_base + USB2_LINECTRL1);
	writel(val & ~USB2_LINECTRL1_OPMODE_NODRV, usb2_base + USB2_LINECTRL1);
}

static void rcar_gen3_init_for_a_peri(struct rcar_gen3_chan *ch)
{
	rcar_gen3_set_linectrl(ch, 0, 1);
	rcar_gen3_set_host_mode(ch, 0);
	rcar_gen3_enable_vbus_ctrl(ch, 1);
}

static void rcar_gen3_init_from_a_peri_to_a_host(struct rcar_gen3_chan *ch)
{
	rcar_gen3_control_otg_irq(ch, 0);

	rcar_gen3_enable_vbus_ctrl(ch, 1);
	rcar_gen3_init_for_host(ch);

	rcar_gen3_control_otg_irq(ch, 1);
}

static bool rcar_gen3_check_id(struct rcar_gen3_chan *ch)
{
	return !!(readl(ch->base + USB2_ADPCTRL) & USB2_ADPCTRL_IDDIG);
}

static void rcar_gen3_device_recognition(struct rcar_gen3_chan *ch)
{
	if (!rcar_gen3_check_id(ch))
		rcar_gen3_init_for_host(ch);
	else
		rcar_gen3_init_for_peri(ch);
}

static bool rcar_gen3_is_host(struct rcar_gen3_chan *ch)
{
	return !(readl(ch->base + USB2_COMMCTRL) & USB2_COMMCTRL_OTG_PERI);
}

static enum phy_mode rcar_gen3_get_phy_mode(struct rcar_gen3_chan *ch)
{
	if (rcar_gen3_is_host(ch))
		return PHY_MODE_USB_HOST;

	return PHY_MODE_USB_DEVICE;
}

static ssize_t role_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct rcar_gen3_chan *ch = dev_get_drvdata(dev);
	bool is_b_device;
	enum phy_mode cur_mode, new_mode;

	if (!ch->uses_otg_pins || !ch->phy->init_count)
		return -EIO;

	if (!strncmp(buf, "host", strlen("host")))
		new_mode = PHY_MODE_USB_HOST;
	else if (!strncmp(buf, "peripheral", strlen("peripheral")))
		new_mode = PHY_MODE_USB_DEVICE;
	else
		return -EINVAL;

	/* is_b_device: true is B-Device. false is A-Device. */
	is_b_device = rcar_gen3_check_id(ch);
	cur_mode = rcar_gen3_get_phy_mode(ch);

	/* If current and new mode is the same, this returns the error */
	if (cur_mode == new_mode)
		return -EINVAL;

	if (new_mode == PHY_MODE_USB_HOST) { /* And is_host must be false */
		if (!is_b_device)	/* A-Peripheral */
			rcar_gen3_init_from_a_peri_to_a_host(ch);
		else			/* B-Peripheral */
			rcar_gen3_init_for_b_host(ch);
	} else {			/* And is_host must be true */
		if (!is_b_device)	/* A-Host */
			rcar_gen3_init_for_a_peri(ch);
		else			/* B-Host */
			rcar_gen3_init_for_peri(ch);
	}

	return count;
}

static ssize_t role_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct rcar_gen3_chan *ch = dev_get_drvdata(dev);

	if (!ch->uses_otg_pins || !ch->phy->init_count)
		return -EIO;

	return sprintf(buf, "%s\n", rcar_gen3_is_host(ch) ? "host" :
							    "peripheral");
}
static DEVICE_ATTR_RW(role);

static void rcar_gen3_init_otg(struct rcar_gen3_chan *ch)
{
	void __iomem *usb2_base = ch->base;
	u32 val;

	val = readl(usb2_base + USB2_VBCTRL);
	writel(val | USB2_VBCTRL_DRVVBUSSEL, usb2_base + USB2_VBCTRL);
	writel(USB2_OBINT_BITS, usb2_base + USB2_OBINTSTA);
	rcar_gen3_control_otg_irq(ch, 1);
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
	if (channel->uses_otg_pins)
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

static const struct phy_ops rcar_gen3_phy_usb2_ops = {
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
	{ .compatible = "renesas,usb2-phy-r8a7796" },
	{ .compatible = "renesas,usb2-phy-r8a77965" },
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
	int irq, ret = 0;

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
		INIT_WORK(&channel->work, rcar_gen3_phy_usb2_work);
		irq = devm_request_irq(dev, irq, rcar_gen3_phy_usb2_irq,
				       IRQF_SHARED, dev_name(dev), channel);
		if (irq < 0)
			dev_err(dev, "No irq handler (%d)\n", irq);
	}

	channel->dr_mode = of_usb_get_dr_mode_by_phy(dev->of_node, 0);
	if (channel->dr_mode != USB_DR_MODE_UNKNOWN) {
		int ret;

		channel->uses_otg_pins = !of_property_read_bool(dev->of_node,
							"renesas,no-otg-pins");
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

	/*
	 * devm_phy_create() will call pm_runtime_enable(&phy->dev);
	 * And then, phy-core will manage runtime pm for this device.
	 */
	pm_runtime_enable(dev);
	channel->phy = devm_phy_create(dev, NULL, &rcar_gen3_phy_usb2_ops);
	if (IS_ERR(channel->phy)) {
		dev_err(dev, "Failed to create USB2 PHY\n");
		ret = PTR_ERR(channel->phy);
		goto error;
	}

	channel->vbus = devm_regulator_get_optional(dev, "vbus");
	if (IS_ERR(channel->vbus)) {
		if (PTR_ERR(channel->vbus) == -EPROBE_DEFER) {
			ret = PTR_ERR(channel->vbus);
			goto error;
		}
		channel->vbus = NULL;
	}

	platform_set_drvdata(pdev, channel);
	phy_set_drvdata(channel->phy, channel);

	provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(provider)) {
		dev_err(dev, "Failed to register PHY provider\n");
		ret = PTR_ERR(provider);
		goto error;
	} else if (channel->uses_otg_pins) {
		int ret;

		ret = device_create_file(dev, &dev_attr_role);
		if (ret < 0)
			goto error;
	}

	return 0;

error:
	pm_runtime_disable(dev);

	return ret;
}

static int rcar_gen3_phy_usb2_remove(struct platform_device *pdev)
{
	struct rcar_gen3_chan *channel = platform_get_drvdata(pdev);

	if (channel->uses_otg_pins)
		device_remove_file(&pdev->dev, &dev_attr_role);

	pm_runtime_disable(&pdev->dev);

	return 0;
};

static struct platform_driver rcar_gen3_phy_usb2_driver = {
	.driver = {
		.name		= "phy_rcar_gen3_usb2",
		.of_match_table	= rcar_gen3_phy_usb2_match_table,
	},
	.probe	= rcar_gen3_phy_usb2_probe,
	.remove = rcar_gen3_phy_usb2_remove,
};
module_platform_driver(rcar_gen3_phy_usb2_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Renesas R-Car Gen3 USB 2.0 PHY");
MODULE_AUTHOR("Yoshihiro Shimoda <yoshihiro.shimoda.uh@renesas.com>");
