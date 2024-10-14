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
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/string.h>
#include <linux/usb/of.h>
#include <linux/workqueue.h>

/******* USB2.0 Host registers (original offset is +0x200) *******/
#define USB2_INT_ENABLE		0x000
#define USB2_AHB_BUS_CTR	0x008
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
#define USB2_INT_ENABLE_USBH_INTB_EN	BIT(2)	/* For EHCI */
#define USB2_INT_ENABLE_USBH_INTA_EN	BIT(1)	/* For OHCI */

/* AHB_BUS_CTR */
#define USB2_AHB_BUS_CTR_MBL_MASK	GENMASK(1, 0)
#define USB2_AHB_BUS_CTR_MBL_INCR4	2

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
#define USB2_VBCTRL_OCCLREN		BIT(16)
#define USB2_VBCTRL_DRVVBUSSEL		BIT(8)
#define USB2_VBCTRL_VBOUT		BIT(0)

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

/*  RZ/G2L specific */
#define USB2_OBINT_IDCHG_EN		BIT(0)
#define USB2_LINECTRL1_USB2_IDMON	BIT(0)

#define NUM_OF_PHYS			4
enum rcar_gen3_phy_index {
	PHY_INDEX_BOTH_HC,
	PHY_INDEX_OHCI,
	PHY_INDEX_EHCI,
	PHY_INDEX_HSUSB
};

static const u32 rcar_gen3_int_enable[NUM_OF_PHYS] = {
	USB2_INT_ENABLE_USBH_INTB_EN | USB2_INT_ENABLE_USBH_INTA_EN,
	USB2_INT_ENABLE_USBH_INTA_EN,
	USB2_INT_ENABLE_USBH_INTB_EN,
	0
};

struct rcar_gen3_phy {
	struct phy *phy;
	struct rcar_gen3_chan *ch;
	u32 int_enable_bits;
	bool initialized;
	bool otg_initialized;
	bool powered;
};

struct rcar_gen3_chan {
	void __iomem *base;
	struct device *dev;	/* platform_device's device */
	struct extcon_dev *extcon;
	struct rcar_gen3_phy rphys[NUM_OF_PHYS];
	struct regulator *vbus;
	struct reset_control *rstc;
	struct work_struct work;
	struct mutex lock;	/* protects rphys[...].powered */
	enum usb_dr_mode dr_mode;
	int irq;
	u32 obint_enable_bits;
	bool extcon_host;
	bool is_otg_channel;
	bool uses_otg_pins;
	bool soc_no_adp_ctrl;
};

struct rcar_gen3_phy_drv_data {
	const struct phy_ops *phy_usb2_ops;
	bool no_adp_ctrl;
	bool init_bus;
};

/*
 * Combination about is_otg_channel and uses_otg_pins:
 *
 * Parameters				|| Behaviors
 * is_otg_channel	| uses_otg_pins	|| irqs		| role sysfs
 * ---------------------+---------------++--------------+------------
 * true			| true		|| enabled	| enabled
 * true                 | false		|| disabled	| enabled
 * false                | any		|| disabled	| disabled
 */

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

	dev_vdbg(ch->dev, "%s: %08x, %d\n", __func__, val, host);
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

	dev_vdbg(ch->dev, "%s: %08x, %d, %d\n", __func__, val, dp, dm);
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
	u32 vbus_ctrl_reg = USB2_ADPCTRL;
	u32 vbus_ctrl_val = USB2_ADPCTRL_DRVVBUS;
	u32 val;

	dev_vdbg(ch->dev, "%s: %08x, %d\n", __func__, val, vbus);
	if (ch->soc_no_adp_ctrl) {
		if (ch->vbus)
			regulator_hardware_enable(ch->vbus, vbus);

		vbus_ctrl_reg = USB2_VBCTRL;
		vbus_ctrl_val = USB2_VBCTRL_VBOUT;
	}

	val = readl(usb2_base + vbus_ctrl_reg);
	if (vbus)
		val |= vbus_ctrl_val;
	else
		val &= ~vbus_ctrl_val;
	writel(val, usb2_base + vbus_ctrl_reg);
}

static void rcar_gen3_control_otg_irq(struct rcar_gen3_chan *ch, int enable)
{
	void __iomem *usb2_base = ch->base;
	u32 val = readl(usb2_base + USB2_OBINTEN);

	if (ch->uses_otg_pins && enable)
		val |= ch->obint_enable_bits;
	else
		val &= ~ch->obint_enable_bits;
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
	if (!ch->uses_otg_pins)
		return (ch->dr_mode == USB_DR_MODE_HOST) ? false : true;

	if (ch->soc_no_adp_ctrl)
		return !!(readl(ch->base + USB2_LINECTRL1) & USB2_LINECTRL1_USB2_IDMON);

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

static bool rcar_gen3_is_any_rphy_initialized(struct rcar_gen3_chan *ch)
{
	int i;

	for (i = 0; i < NUM_OF_PHYS; i++) {
		if (ch->rphys[i].initialized)
			return true;
	}

	return false;
}

static bool rcar_gen3_needs_init_otg(struct rcar_gen3_chan *ch)
{
	int i;

	for (i = 0; i < NUM_OF_PHYS; i++) {
		if (ch->rphys[i].otg_initialized)
			return false;
	}

	return true;
}

static bool rcar_gen3_are_all_rphys_power_off(struct rcar_gen3_chan *ch)
{
	int i;

	for (i = 0; i < NUM_OF_PHYS; i++) {
		if (ch->rphys[i].powered)
			return false;
	}

	return true;
}

static ssize_t role_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct rcar_gen3_chan *ch = dev_get_drvdata(dev);
	bool is_b_device;
	enum phy_mode cur_mode, new_mode;

	if (!ch->is_otg_channel || !rcar_gen3_is_any_rphy_initialized(ch))
		return -EIO;

	if (sysfs_streq(buf, "host"))
		new_mode = PHY_MODE_USB_HOST;
	else if (sysfs_streq(buf, "peripheral"))
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

	if (!ch->is_otg_channel || !rcar_gen3_is_any_rphy_initialized(ch))
		return -EIO;

	return sprintf(buf, "%s\n", rcar_gen3_is_host(ch) ? "host" :
							    "peripheral");
}
static DEVICE_ATTR_RW(role);

static void rcar_gen3_init_otg(struct rcar_gen3_chan *ch)
{
	void __iomem *usb2_base = ch->base;
	u32 val;

	/* Should not use functions of read-modify-write a register */
	val = readl(usb2_base + USB2_LINECTRL1);
	val = (val & ~USB2_LINECTRL1_DP_RPD) | USB2_LINECTRL1_DPRPD_EN |
	      USB2_LINECTRL1_DMRPD_EN | USB2_LINECTRL1_DM_RPD;
	writel(val, usb2_base + USB2_LINECTRL1);

	if (!ch->soc_no_adp_ctrl) {
		val = readl(usb2_base + USB2_VBCTRL);
		val &= ~USB2_VBCTRL_OCCLREN;
		writel(val | USB2_VBCTRL_DRVVBUSSEL, usb2_base + USB2_VBCTRL);
		val = readl(usb2_base + USB2_ADPCTRL);
		writel(val | USB2_ADPCTRL_IDPULLUP, usb2_base + USB2_ADPCTRL);
	}
	msleep(20);

	writel(0xffffffff, usb2_base + USB2_OBINTSTA);
	writel(ch->obint_enable_bits, usb2_base + USB2_OBINTEN);

	rcar_gen3_device_recognition(ch);
}

static irqreturn_t rcar_gen3_phy_usb2_irq(int irq, void *_ch)
{
	struct rcar_gen3_chan *ch = _ch;
	void __iomem *usb2_base = ch->base;
	u32 status = readl(usb2_base + USB2_OBINTSTA);
	irqreturn_t ret = IRQ_NONE;

	if (status & ch->obint_enable_bits) {
		dev_vdbg(ch->dev, "%s: %08x\n", __func__, status);
		writel(ch->obint_enable_bits, usb2_base + USB2_OBINTSTA);
		rcar_gen3_device_recognition(ch);
		ret = IRQ_HANDLED;
	}

	return ret;
}

static int rcar_gen3_phy_usb2_init(struct phy *p)
{
	struct rcar_gen3_phy *rphy = phy_get_drvdata(p);
	struct rcar_gen3_chan *channel = rphy->ch;
	void __iomem *usb2_base = channel->base;
	u32 val;
	int ret;

	if (!rcar_gen3_is_any_rphy_initialized(channel) && channel->irq >= 0) {
		INIT_WORK(&channel->work, rcar_gen3_phy_usb2_work);
		ret = request_irq(channel->irq, rcar_gen3_phy_usb2_irq,
				  IRQF_SHARED, dev_name(channel->dev), channel);
		if (ret < 0) {
			dev_err(channel->dev, "No irq handler (%d)\n", channel->irq);
			return ret;
		}
	}

	/* Initialize USB2 part */
	val = readl(usb2_base + USB2_INT_ENABLE);
	val |= USB2_INT_ENABLE_UCOM_INTEN | rphy->int_enable_bits;
	writel(val, usb2_base + USB2_INT_ENABLE);
	writel(USB2_SPD_RSM_TIMSET_INIT, usb2_base + USB2_SPD_RSM_TIMSET);
	writel(USB2_OC_TIMSET_INIT, usb2_base + USB2_OC_TIMSET);

	/* Initialize otg part */
	if (channel->is_otg_channel) {
		if (rcar_gen3_needs_init_otg(channel))
			rcar_gen3_init_otg(channel);
		rphy->otg_initialized = true;
	}

	rphy->initialized = true;

	return 0;
}

static int rcar_gen3_phy_usb2_exit(struct phy *p)
{
	struct rcar_gen3_phy *rphy = phy_get_drvdata(p);
	struct rcar_gen3_chan *channel = rphy->ch;
	void __iomem *usb2_base = channel->base;
	u32 val;

	rphy->initialized = false;

	if (channel->is_otg_channel)
		rphy->otg_initialized = false;

	val = readl(usb2_base + USB2_INT_ENABLE);
	val &= ~rphy->int_enable_bits;
	if (!rcar_gen3_is_any_rphy_initialized(channel))
		val &= ~USB2_INT_ENABLE_UCOM_INTEN;
	writel(val, usb2_base + USB2_INT_ENABLE);

	if (channel->irq >= 0 && !rcar_gen3_is_any_rphy_initialized(channel))
		free_irq(channel->irq, channel);

	return 0;
}

static int rcar_gen3_phy_usb2_power_on(struct phy *p)
{
	struct rcar_gen3_phy *rphy = phy_get_drvdata(p);
	struct rcar_gen3_chan *channel = rphy->ch;
	void __iomem *usb2_base = channel->base;
	u32 val;
	int ret = 0;

	mutex_lock(&channel->lock);
	if (!rcar_gen3_are_all_rphys_power_off(channel))
		goto out;

	if (channel->vbus) {
		ret = regulator_enable(channel->vbus);
		if (ret)
			goto out;
	}

	val = readl(usb2_base + USB2_USBCTR);
	val |= USB2_USBCTR_PLL_RST;
	writel(val, usb2_base + USB2_USBCTR);
	val &= ~USB2_USBCTR_PLL_RST;
	writel(val, usb2_base + USB2_USBCTR);

out:
	/* The powered flag should be set for any other phys anyway */
	rphy->powered = true;
	mutex_unlock(&channel->lock);

	return 0;
}

static int rcar_gen3_phy_usb2_power_off(struct phy *p)
{
	struct rcar_gen3_phy *rphy = phy_get_drvdata(p);
	struct rcar_gen3_chan *channel = rphy->ch;
	int ret = 0;

	mutex_lock(&channel->lock);
	rphy->powered = false;

	if (!rcar_gen3_are_all_rphys_power_off(channel))
		goto out;

	if (channel->vbus)
		ret = regulator_disable(channel->vbus);

out:
	mutex_unlock(&channel->lock);

	return ret;
}

static const struct phy_ops rcar_gen3_phy_usb2_ops = {
	.init		= rcar_gen3_phy_usb2_init,
	.exit		= rcar_gen3_phy_usb2_exit,
	.power_on	= rcar_gen3_phy_usb2_power_on,
	.power_off	= rcar_gen3_phy_usb2_power_off,
	.owner		= THIS_MODULE,
};

static const struct phy_ops rz_g1c_phy_usb2_ops = {
	.init		= rcar_gen3_phy_usb2_init,
	.exit		= rcar_gen3_phy_usb2_exit,
	.owner		= THIS_MODULE,
};

static const struct rcar_gen3_phy_drv_data rcar_gen3_phy_usb2_data = {
	.phy_usb2_ops = &rcar_gen3_phy_usb2_ops,
	.no_adp_ctrl = false,
};

static const struct rcar_gen3_phy_drv_data rz_g1c_phy_usb2_data = {
	.phy_usb2_ops = &rz_g1c_phy_usb2_ops,
	.no_adp_ctrl = false,
};

static const struct rcar_gen3_phy_drv_data rz_g2l_phy_usb2_data = {
	.phy_usb2_ops = &rcar_gen3_phy_usb2_ops,
	.no_adp_ctrl = true,
};

static const struct rcar_gen3_phy_drv_data rz_g3s_phy_usb2_data = {
	.phy_usb2_ops = &rcar_gen3_phy_usb2_ops,
	.no_adp_ctrl = true,
	.init_bus = true,
};

static const struct of_device_id rcar_gen3_phy_usb2_match_table[] = {
	{
		.compatible = "renesas,usb2-phy-r8a77470",
		.data = &rz_g1c_phy_usb2_data,
	},
	{
		.compatible = "renesas,usb2-phy-r8a7795",
		.data = &rcar_gen3_phy_usb2_data,
	},
	{
		.compatible = "renesas,usb2-phy-r8a7796",
		.data = &rcar_gen3_phy_usb2_data,
	},
	{
		.compatible = "renesas,usb2-phy-r8a77965",
		.data = &rcar_gen3_phy_usb2_data,
	},
	{
		.compatible = "renesas,rzg2l-usb2-phy",
		.data = &rz_g2l_phy_usb2_data,
	},
	{
		.compatible = "renesas,usb2-phy-r9a08g045",
		.data = &rz_g3s_phy_usb2_data,
	},
	{
		.compatible = "renesas,rcar-gen3-usb2-phy",
		.data = &rcar_gen3_phy_usb2_data,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, rcar_gen3_phy_usb2_match_table);

static const unsigned int rcar_gen3_phy_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_NONE,
};

static struct phy *rcar_gen3_phy_usb2_xlate(struct device *dev,
					    const struct of_phandle_args *args)
{
	struct rcar_gen3_chan *ch = dev_get_drvdata(dev);

	if (args->args_count == 0)	/* For old version dts */
		return ch->rphys[PHY_INDEX_BOTH_HC].phy;
	else if (args->args_count > 1)	/* Prevent invalid args count */
		return ERR_PTR(-ENODEV);

	if (args->args[0] >= NUM_OF_PHYS)
		return ERR_PTR(-ENODEV);

	return ch->rphys[args->args[0]].phy;
}

static enum usb_dr_mode rcar_gen3_get_dr_mode(struct device_node *np)
{
	enum usb_dr_mode candidate = USB_DR_MODE_UNKNOWN;
	int i;

	/*
	 * If one of device nodes has other dr_mode except UNKNOWN,
	 * this function returns UNKNOWN. To achieve backward compatibility,
	 * this loop starts the index as 0.
	 */
	for (i = 0; i < NUM_OF_PHYS; i++) {
		enum usb_dr_mode mode = of_usb_get_dr_mode_by_phy(np, i);

		if (mode != USB_DR_MODE_UNKNOWN) {
			if (candidate == USB_DR_MODE_UNKNOWN)
				candidate = mode;
			else if (candidate != mode)
				return USB_DR_MODE_UNKNOWN;
		}
	}

	return candidate;
}

static int rcar_gen3_phy_usb2_init_bus(struct rcar_gen3_chan *channel)
{
	struct device *dev = channel->dev;
	int ret;
	u32 val;

	channel->rstc = devm_reset_control_array_get_shared(dev);
	if (IS_ERR(channel->rstc))
		return PTR_ERR(channel->rstc);

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	ret = reset_control_deassert(channel->rstc);
	if (ret)
		goto rpm_put;

	val = readl(channel->base + USB2_AHB_BUS_CTR);
	val &= ~USB2_AHB_BUS_CTR_MBL_MASK;
	val |= USB2_AHB_BUS_CTR_MBL_INCR4;
	writel(val, channel->base + USB2_AHB_BUS_CTR);

rpm_put:
	pm_runtime_put(dev);

	return ret;
}

static int rcar_gen3_phy_usb2_probe(struct platform_device *pdev)
{
	const struct rcar_gen3_phy_drv_data *phy_data;
	struct device *dev = &pdev->dev;
	struct rcar_gen3_chan *channel;
	struct phy_provider *provider;
	int ret = 0, i;

	if (!dev->of_node) {
		dev_err(dev, "This driver needs device tree\n");
		return -EINVAL;
	}

	channel = devm_kzalloc(dev, sizeof(*channel), GFP_KERNEL);
	if (!channel)
		return -ENOMEM;

	channel->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(channel->base))
		return PTR_ERR(channel->base);

	channel->obint_enable_bits = USB2_OBINT_BITS;
	/* get irq number here and request_irq for OTG in phy_init */
	channel->irq = platform_get_irq_optional(pdev, 0);
	channel->dr_mode = rcar_gen3_get_dr_mode(dev->of_node);
	if (channel->dr_mode != USB_DR_MODE_UNKNOWN) {
		channel->is_otg_channel = true;
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

	phy_data = of_device_get_match_data(dev);
	if (!phy_data) {
		ret = -EINVAL;
		goto error;
	}

	platform_set_drvdata(pdev, channel);
	channel->dev = dev;

	if (phy_data->init_bus) {
		ret = rcar_gen3_phy_usb2_init_bus(channel);
		if (ret)
			goto error;
	}

	channel->soc_no_adp_ctrl = phy_data->no_adp_ctrl;
	if (phy_data->no_adp_ctrl)
		channel->obint_enable_bits = USB2_OBINT_IDCHG_EN;

	mutex_init(&channel->lock);
	for (i = 0; i < NUM_OF_PHYS; i++) {
		channel->rphys[i].phy = devm_phy_create(dev, NULL,
							phy_data->phy_usb2_ops);
		if (IS_ERR(channel->rphys[i].phy)) {
			dev_err(dev, "Failed to create USB2 PHY\n");
			ret = PTR_ERR(channel->rphys[i].phy);
			goto error;
		}
		channel->rphys[i].ch = channel;
		channel->rphys[i].int_enable_bits = rcar_gen3_int_enable[i];
		phy_set_drvdata(channel->rphys[i].phy, &channel->rphys[i]);
	}

	if (channel->soc_no_adp_ctrl && channel->is_otg_channel)
		channel->vbus = devm_regulator_get_exclusive(dev, "vbus");
	else
		channel->vbus = devm_regulator_get_optional(dev, "vbus");
	if (IS_ERR(channel->vbus)) {
		if (PTR_ERR(channel->vbus) == -EPROBE_DEFER) {
			ret = PTR_ERR(channel->vbus);
			goto error;
		}
		channel->vbus = NULL;
	}

	provider = devm_of_phy_provider_register(dev, rcar_gen3_phy_usb2_xlate);
	if (IS_ERR(provider)) {
		dev_err(dev, "Failed to register PHY provider\n");
		ret = PTR_ERR(provider);
		goto error;
	} else if (channel->is_otg_channel) {
		ret = device_create_file(dev, &dev_attr_role);
		if (ret < 0)
			goto error;
	}

	return 0;

error:
	pm_runtime_disable(dev);

	return ret;
}

static void rcar_gen3_phy_usb2_remove(struct platform_device *pdev)
{
	struct rcar_gen3_chan *channel = platform_get_drvdata(pdev);

	if (channel->is_otg_channel)
		device_remove_file(&pdev->dev, &dev_attr_role);

	reset_control_assert(channel->rstc);
	pm_runtime_disable(&pdev->dev);
};

static struct platform_driver rcar_gen3_phy_usb2_driver = {
	.driver = {
		.name		= "phy_rcar_gen3_usb2",
		.of_match_table	= rcar_gen3_phy_usb2_match_table,
	},
	.probe	= rcar_gen3_phy_usb2_probe,
	.remove_new = rcar_gen3_phy_usb2_remove,
};
module_platform_driver(rcar_gen3_phy_usb2_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Renesas R-Car Gen3 USB 2.0 PHY");
MODULE_AUTHOR("Yoshihiro Shimoda <yoshihiro.shimoda.uh@renesas.com>");
