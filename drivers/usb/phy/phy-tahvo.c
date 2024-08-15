// SPDX-License-Identifier: GPL-2.0
/*
 * Tahvo USB transceiver driver
 *
 * Copyright (C) 2005-2006 Nokia Corporation
 *
 * Parts copied from isp1301_omap.c.
 * Copyright (C) 2004 Texas Instruments
 * Copyright (C) 2004 David Brownell
 *
 * Original driver written by Juha Yrjölä, Tony Lindgren and Timo Teräs.
 * Modified for Retu/Tahvo MFD by Aaro Koskinen.
 */

#include <linux/io.h>
#include <linux/clk.h>
#include <linux/usb.h>
#include <linux/extcon-provider.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb/otg.h>
#include <linux/mfd/retu.h>
#include <linux/usb/gadget.h>
#include <linux/platform_device.h>

#define DRIVER_NAME     "tahvo-usb"

#define TAHVO_REG_IDSR	0x02
#define TAHVO_REG_USBR	0x06

#define USBR_SLAVE_CONTROL	(1 << 8)
#define USBR_VPPVIO_SW		(1 << 7)
#define USBR_SPEED		(1 << 6)
#define USBR_REGOUT		(1 << 5)
#define USBR_MASTER_SW2		(1 << 4)
#define USBR_MASTER_SW1		(1 << 3)
#define USBR_SLAVE_SW		(1 << 2)
#define USBR_NSUSPEND		(1 << 1)
#define USBR_SEMODE		(1 << 0)

#define TAHVO_MODE_HOST		0
#define TAHVO_MODE_PERIPHERAL	1

struct tahvo_usb {
	struct platform_device	*pt_dev;
	struct usb_phy		phy;
	int			vbus_state;
	struct mutex		serialize;
	struct clk		*ick;
	int			irq;
	int			tahvo_mode;
	struct extcon_dev	*extcon;
};

static const unsigned int tahvo_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,

	EXTCON_NONE,
};

static ssize_t vbus_show(struct device *device,
			       struct device_attribute *attr, char *buf)
{
	struct tahvo_usb *tu = dev_get_drvdata(device);
	return sprintf(buf, "%s\n", tu->vbus_state ? "on" : "off");
}
static DEVICE_ATTR_RO(vbus);

static void check_vbus_state(struct tahvo_usb *tu)
{
	struct retu_dev *rdev = dev_get_drvdata(tu->pt_dev->dev.parent);
	int reg, prev_state;

	reg = retu_read(rdev, TAHVO_REG_IDSR);
	if (reg & TAHVO_STAT_VBUS) {
		switch (tu->phy.otg->state) {
		case OTG_STATE_B_IDLE:
			/* Enable the gadget driver */
			if (tu->phy.otg->gadget)
				usb_gadget_vbus_connect(tu->phy.otg->gadget);
			tu->phy.otg->state = OTG_STATE_B_PERIPHERAL;
			usb_phy_set_event(&tu->phy, USB_EVENT_ENUMERATED);
			break;
		case OTG_STATE_A_IDLE:
			/*
			 * Session is now valid assuming the USB hub is driving
			 * Vbus.
			 */
			tu->phy.otg->state = OTG_STATE_A_HOST;
			break;
		default:
			break;
		}
		dev_info(&tu->pt_dev->dev, "USB cable connected\n");
	} else {
		switch (tu->phy.otg->state) {
		case OTG_STATE_B_PERIPHERAL:
			if (tu->phy.otg->gadget)
				usb_gadget_vbus_disconnect(tu->phy.otg->gadget);
			tu->phy.otg->state = OTG_STATE_B_IDLE;
			usb_phy_set_event(&tu->phy, USB_EVENT_NONE);
			break;
		case OTG_STATE_A_HOST:
			tu->phy.otg->state = OTG_STATE_A_IDLE;
			break;
		default:
			break;
		}
		dev_info(&tu->pt_dev->dev, "USB cable disconnected\n");
	}

	prev_state = tu->vbus_state;
	tu->vbus_state = reg & TAHVO_STAT_VBUS;
	if (prev_state != tu->vbus_state) {
		extcon_set_state_sync(tu->extcon, EXTCON_USB, tu->vbus_state);
		sysfs_notify(&tu->pt_dev->dev.kobj, NULL, "vbus_state");
	}
}

static void tahvo_usb_become_host(struct tahvo_usb *tu)
{
	struct retu_dev *rdev = dev_get_drvdata(tu->pt_dev->dev.parent);

	extcon_set_state_sync(tu->extcon, EXTCON_USB_HOST, true);

	/* Power up the transceiver in USB host mode */
	retu_write(rdev, TAHVO_REG_USBR, USBR_REGOUT | USBR_NSUSPEND |
		   USBR_MASTER_SW2 | USBR_MASTER_SW1);
	tu->phy.otg->state = OTG_STATE_A_IDLE;

	check_vbus_state(tu);
}

static void tahvo_usb_stop_host(struct tahvo_usb *tu)
{
	tu->phy.otg->state = OTG_STATE_A_IDLE;
}

static void tahvo_usb_become_peripheral(struct tahvo_usb *tu)
{
	struct retu_dev *rdev = dev_get_drvdata(tu->pt_dev->dev.parent);

	extcon_set_state_sync(tu->extcon, EXTCON_USB_HOST, false);

	/* Power up transceiver and set it in USB peripheral mode */
	retu_write(rdev, TAHVO_REG_USBR, USBR_SLAVE_CONTROL | USBR_REGOUT |
		   USBR_NSUSPEND | USBR_SLAVE_SW);
	tu->phy.otg->state = OTG_STATE_B_IDLE;

	check_vbus_state(tu);
}

static void tahvo_usb_stop_peripheral(struct tahvo_usb *tu)
{
	if (tu->phy.otg->gadget)
		usb_gadget_vbus_disconnect(tu->phy.otg->gadget);
	tu->phy.otg->state = OTG_STATE_B_IDLE;
}

static void tahvo_usb_power_off(struct tahvo_usb *tu)
{
	struct retu_dev *rdev = dev_get_drvdata(tu->pt_dev->dev.parent);

	/* Disable gadget controller if any */
	if (tu->phy.otg->gadget)
		usb_gadget_vbus_disconnect(tu->phy.otg->gadget);

	/* Power off transceiver */
	retu_write(rdev, TAHVO_REG_USBR, 0);
	tu->phy.otg->state = OTG_STATE_UNDEFINED;
}

static int tahvo_usb_set_suspend(struct usb_phy *dev, int suspend)
{
	struct tahvo_usb *tu = container_of(dev, struct tahvo_usb, phy);
	struct retu_dev *rdev = dev_get_drvdata(tu->pt_dev->dev.parent);
	u16 w;

	dev_dbg(&tu->pt_dev->dev, "%s\n", __func__);

	w = retu_read(rdev, TAHVO_REG_USBR);
	if (suspend)
		w &= ~USBR_NSUSPEND;
	else
		w |= USBR_NSUSPEND;
	retu_write(rdev, TAHVO_REG_USBR, w);

	return 0;
}

static int tahvo_usb_set_host(struct usb_otg *otg, struct usb_bus *host)
{
	struct tahvo_usb *tu = container_of(otg->usb_phy, struct tahvo_usb,
					    phy);

	mutex_lock(&tu->serialize);

	if (host == NULL) {
		if (tu->tahvo_mode == TAHVO_MODE_HOST)
			tahvo_usb_power_off(tu);
		otg->host = NULL;
		mutex_unlock(&tu->serialize);
		return 0;
	}

	if (tu->tahvo_mode == TAHVO_MODE_HOST) {
		otg->host = NULL;
		tahvo_usb_become_host(tu);
	}

	otg->host = host;

	mutex_unlock(&tu->serialize);

	return 0;
}

static int tahvo_usb_set_peripheral(struct usb_otg *otg,
				    struct usb_gadget *gadget)
{
	struct tahvo_usb *tu = container_of(otg->usb_phy, struct tahvo_usb,
					    phy);

	mutex_lock(&tu->serialize);

	if (!gadget) {
		if (tu->tahvo_mode == TAHVO_MODE_PERIPHERAL)
			tahvo_usb_power_off(tu);
		tu->phy.otg->gadget = NULL;
		mutex_unlock(&tu->serialize);
		return 0;
	}

	tu->phy.otg->gadget = gadget;
	if (tu->tahvo_mode == TAHVO_MODE_PERIPHERAL)
		tahvo_usb_become_peripheral(tu);

	mutex_unlock(&tu->serialize);

	return 0;
}

static irqreturn_t tahvo_usb_vbus_interrupt(int irq, void *_tu)
{
	struct tahvo_usb *tu = _tu;

	mutex_lock(&tu->serialize);
	check_vbus_state(tu);
	mutex_unlock(&tu->serialize);

	return IRQ_HANDLED;
}

static ssize_t otg_mode_show(struct device *device,
			     struct device_attribute *attr, char *buf)
{
	struct tahvo_usb *tu = dev_get_drvdata(device);

	switch (tu->tahvo_mode) {
	case TAHVO_MODE_HOST:
		return sprintf(buf, "host\n");
	case TAHVO_MODE_PERIPHERAL:
		return sprintf(buf, "peripheral\n");
	}

	return -EINVAL;
}

static ssize_t otg_mode_store(struct device *device,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct tahvo_usb *tu = dev_get_drvdata(device);
	int r;

	mutex_lock(&tu->serialize);
	if (count >= 4 && strncmp(buf, "host", 4) == 0) {
		if (tu->tahvo_mode == TAHVO_MODE_PERIPHERAL)
			tahvo_usb_stop_peripheral(tu);
		tu->tahvo_mode = TAHVO_MODE_HOST;
		if (tu->phy.otg->host) {
			dev_info(device, "HOST mode: host controller present\n");
			tahvo_usb_become_host(tu);
		} else {
			dev_info(device, "HOST mode: no host controller, powering off\n");
			tahvo_usb_power_off(tu);
		}
		r = strlen(buf);
	} else if (count >= 10 && strncmp(buf, "peripheral", 10) == 0) {
		if (tu->tahvo_mode == TAHVO_MODE_HOST)
			tahvo_usb_stop_host(tu);
		tu->tahvo_mode = TAHVO_MODE_PERIPHERAL;
		if (tu->phy.otg->gadget) {
			dev_info(device, "PERIPHERAL mode: gadget driver present\n");
			tahvo_usb_become_peripheral(tu);
		} else {
			dev_info(device, "PERIPHERAL mode: no gadget driver, powering off\n");
			tahvo_usb_power_off(tu);
		}
		r = strlen(buf);
	} else {
		r = -EINVAL;
	}
	mutex_unlock(&tu->serialize);

	return r;
}
static DEVICE_ATTR_RW(otg_mode);

static struct attribute *tahvo_attrs[] = {
	&dev_attr_vbus.attr,
	&dev_attr_otg_mode.attr,
	NULL
};
ATTRIBUTE_GROUPS(tahvo);

static int tahvo_usb_probe(struct platform_device *pdev)
{
	struct retu_dev *rdev = dev_get_drvdata(pdev->dev.parent);
	struct tahvo_usb *tu;
	int ret;

	tu = devm_kzalloc(&pdev->dev, sizeof(*tu), GFP_KERNEL);
	if (!tu)
		return -ENOMEM;

	tu->phy.otg = devm_kzalloc(&pdev->dev, sizeof(*tu->phy.otg),
				   GFP_KERNEL);
	if (!tu->phy.otg)
		return -ENOMEM;

	tu->pt_dev = pdev;

	/* Default mode */
#ifdef CONFIG_TAHVO_USB_HOST_BY_DEFAULT
	tu->tahvo_mode = TAHVO_MODE_HOST;
#else
	tu->tahvo_mode = TAHVO_MODE_PERIPHERAL;
#endif

	mutex_init(&tu->serialize);

	tu->ick = devm_clk_get(&pdev->dev, "usb_l4_ick");
	if (!IS_ERR(tu->ick))
		clk_enable(tu->ick);

	/*
	 * Set initial state, so that we generate kevents only on state changes.
	 */
	tu->vbus_state = retu_read(rdev, TAHVO_REG_IDSR) & TAHVO_STAT_VBUS;

	tu->extcon = devm_extcon_dev_allocate(&pdev->dev, tahvo_cable);
	if (IS_ERR(tu->extcon)) {
		dev_err(&pdev->dev, "failed to allocate memory for extcon\n");
		ret = PTR_ERR(tu->extcon);
		goto err_disable_clk;
	}

	ret = devm_extcon_dev_register(&pdev->dev, tu->extcon);
	if (ret) {
		dev_err(&pdev->dev, "could not register extcon device: %d\n",
			ret);
		goto err_disable_clk;
	}

	/* Set the initial cable state. */
	extcon_set_state_sync(tu->extcon, EXTCON_USB_HOST,
			       tu->tahvo_mode == TAHVO_MODE_HOST);
	extcon_set_state_sync(tu->extcon, EXTCON_USB, tu->vbus_state);

	/* Create OTG interface */
	tahvo_usb_power_off(tu);
	tu->phy.dev = &pdev->dev;
	tu->phy.otg->state = OTG_STATE_UNDEFINED;
	tu->phy.label = DRIVER_NAME;
	tu->phy.set_suspend = tahvo_usb_set_suspend;

	tu->phy.otg->usb_phy = &tu->phy;
	tu->phy.otg->set_host = tahvo_usb_set_host;
	tu->phy.otg->set_peripheral = tahvo_usb_set_peripheral;

	ret = usb_add_phy(&tu->phy, USB_PHY_TYPE_USB2);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot register USB transceiver: %d\n",
			ret);
		goto err_disable_clk;
	}

	dev_set_drvdata(&pdev->dev, tu);

	tu->irq = ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		goto err_remove_phy;
	ret = request_threaded_irq(tu->irq, NULL, tahvo_usb_vbus_interrupt,
				   IRQF_ONESHOT,
				   "tahvo-vbus", tu);
	if (ret) {
		dev_err(&pdev->dev, "could not register tahvo-vbus irq: %d\n",
			ret);
		goto err_remove_phy;
	}

	return 0;

err_remove_phy:
	usb_remove_phy(&tu->phy);
err_disable_clk:
	if (!IS_ERR(tu->ick))
		clk_disable(tu->ick);

	return ret;
}

static int tahvo_usb_remove(struct platform_device *pdev)
{
	struct tahvo_usb *tu = platform_get_drvdata(pdev);

	free_irq(tu->irq, tu);
	usb_remove_phy(&tu->phy);
	if (!IS_ERR(tu->ick))
		clk_disable(tu->ick);

	return 0;
}

static struct platform_driver tahvo_usb_driver = {
	.probe		= tahvo_usb_probe,
	.remove		= tahvo_usb_remove,
	.driver		= {
		.name	= "tahvo-usb",
		.dev_groups = tahvo_groups,
	},
};
module_platform_driver(tahvo_usb_driver);

MODULE_DESCRIPTION("Tahvo USB transceiver driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Juha Yrjölä, Tony Lindgren, and Timo Teräs");
MODULE_AUTHOR("Aaro Koskinen <aaro.koskinen@iki.fi>");
