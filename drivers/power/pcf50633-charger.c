/* NXP PCF50633 Main Battery Charger Driver
 *
 * (C) 2006-2008 by Openmoko, Inc.
 * Author: Balaji Rao <balajirrao@openmoko.org>
 * All rights reserved.
 *
 * Broken down from monstrous PCF50633 driver mainly by
 * Harald Welte, Andy Green and Werner Almesberger
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>

#include <linux/mfd/pcf50633/core.h>
#include <linux/mfd/pcf50633/mbc.h>

struct pcf50633_mbc {
	struct pcf50633 *pcf;

	int adapter_active;
	int adapter_online;
	int usb_active;
	int usb_online;

	struct power_supply usb;
	struct power_supply adapter;
};

int pcf50633_mbc_usb_curlim_set(struct pcf50633 *pcf, int ma)
{
	struct pcf50633_mbc *mbc = platform_get_drvdata(pcf->mbc_pdev);
	int ret = 0;
	u8 bits;

	if (ma >= 1000)
		bits = PCF50633_MBCC7_USB_1000mA;
	else if (ma >= 500)
		bits = PCF50633_MBCC7_USB_500mA;
	else if (ma >= 100)
		bits = PCF50633_MBCC7_USB_100mA;
	else
		bits = PCF50633_MBCC7_USB_SUSPEND;

	ret = pcf50633_reg_set_bit_mask(pcf, PCF50633_REG_MBCC7,
					PCF50633_MBCC7_USB_MASK, bits);
	if (ret)
		dev_err(pcf->dev, "error setting usb curlim to %d mA\n", ma);
	else
		dev_info(pcf->dev, "usb curlim to %d mA\n", ma);

	power_supply_changed(&mbc->usb);

	return ret;
}
EXPORT_SYMBOL_GPL(pcf50633_mbc_usb_curlim_set);

int pcf50633_mbc_get_status(struct pcf50633 *pcf)
{
	struct pcf50633_mbc *mbc  = platform_get_drvdata(pcf->mbc_pdev);
	int status = 0;

	if (mbc->usb_online)
		status |= PCF50633_MBC_USB_ONLINE;
	if (mbc->usb_active)
		status |= PCF50633_MBC_USB_ACTIVE;
	if (mbc->adapter_online)
		status |= PCF50633_MBC_ADAPTER_ONLINE;
	if (mbc->adapter_active)
		status |= PCF50633_MBC_ADAPTER_ACTIVE;

	return status;
}
EXPORT_SYMBOL_GPL(pcf50633_mbc_get_status);

void pcf50633_mbc_set_status(struct pcf50633 *pcf, int what, int status)
{
	struct pcf50633_mbc *mbc = platform_get_drvdata(pcf->mbc_pdev);

	if (what & PCF50633_MBC_USB_ONLINE)
		mbc->usb_online = !!status;
	if (what & PCF50633_MBC_USB_ACTIVE)
		mbc->usb_active = !!status;
	if (what & PCF50633_MBC_ADAPTER_ONLINE)
		mbc->adapter_online = !!status;
	if (what & PCF50633_MBC_ADAPTER_ACTIVE)
		mbc->adapter_active = !!status;
}
EXPORT_SYMBOL_GPL(pcf50633_mbc_set_status);

static ssize_t
show_chgmode(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pcf50633_mbc *mbc = dev_get_drvdata(dev);

	u8 mbcs2 = pcf50633_reg_read(mbc->pcf, PCF50633_REG_MBCS2);
	u8 chgmod = (mbcs2 & PCF50633_MBCS2_MBC_MASK);

	return sprintf(buf, "%d\n", chgmod);
}
static DEVICE_ATTR(chgmode, S_IRUGO, show_chgmode, NULL);

static ssize_t
show_usblim(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pcf50633_mbc *mbc = dev_get_drvdata(dev);
	u8 usblim = pcf50633_reg_read(mbc->pcf, PCF50633_REG_MBCC7) &
						PCF50633_MBCC7_USB_MASK;
	unsigned int ma;

	if (usblim == PCF50633_MBCC7_USB_1000mA)
		ma = 1000;
	else if (usblim == PCF50633_MBCC7_USB_500mA)
		ma = 500;
	else if (usblim == PCF50633_MBCC7_USB_100mA)
		ma = 100;
	else
		ma = 0;

	return sprintf(buf, "%u\n", ma);
}

static ssize_t set_usblim(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct pcf50633_mbc *mbc = dev_get_drvdata(dev);
	unsigned long ma;
	int ret;

	ret = strict_strtoul(buf, 10, &ma);
	if (ret)
		return -EINVAL;

	pcf50633_mbc_usb_curlim_set(mbc->pcf, ma);

	return count;
}

static DEVICE_ATTR(usb_curlim, S_IRUGO | S_IWUSR, show_usblim, set_usblim);

static struct attribute *pcf50633_mbc_sysfs_entries[] = {
	&dev_attr_chgmode.attr,
	&dev_attr_usb_curlim.attr,
	NULL,
};

static struct attribute_group mbc_attr_group = {
	.name	= NULL,			/* put in device directory */
	.attrs	= pcf50633_mbc_sysfs_entries,
};

static void
pcf50633_mbc_irq_handler(int irq, void *data)
{
	struct pcf50633_mbc *mbc = data;

	/* USB */
	if (irq == PCF50633_IRQ_USBINS) {
		mbc->usb_online = 1;
	} else if (irq == PCF50633_IRQ_USBREM) {
		mbc->usb_online = 0;
		mbc->usb_active = 0;
		pcf50633_mbc_usb_curlim_set(mbc->pcf, 0);
	}

	/* Adapter */
	if (irq == PCF50633_IRQ_ADPINS) {
		mbc->adapter_online = 1;
		mbc->adapter_active = 1;
	} else if (irq == PCF50633_IRQ_ADPREM) {
		mbc->adapter_online = 0;
		mbc->adapter_active = 0;
	}

	if (irq == PCF50633_IRQ_BATFULL) {
		mbc->usb_active = 0;
		mbc->adapter_active = 0;
	}

	power_supply_changed(&mbc->usb);
	power_supply_changed(&mbc->adapter);

	if (mbc->pcf->pdata->mbc_event_callback)
		mbc->pcf->pdata->mbc_event_callback(mbc->pcf, irq);
}

static int adapter_get_property(struct power_supply *psy,
			enum power_supply_property psp,
			union power_supply_propval *val)
{
	struct pcf50633_mbc *mbc = container_of(psy,
				struct pcf50633_mbc, adapter);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval =  mbc->adapter_online;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int usb_get_property(struct power_supply *psy,
			enum power_supply_property psp,
			union power_supply_propval *val)
{
	struct pcf50633_mbc *mbc = container_of(psy, struct pcf50633_mbc, usb);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = mbc->usb_online;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static enum power_supply_property power_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static const u8 mbc_irq_handlers[] = {
	PCF50633_IRQ_ADPINS,
	PCF50633_IRQ_ADPREM,
	PCF50633_IRQ_USBINS,
	PCF50633_IRQ_USBREM,
	PCF50633_IRQ_BATFULL,
	PCF50633_IRQ_CHGHALT,
	PCF50633_IRQ_THLIMON,
	PCF50633_IRQ_THLIMOFF,
	PCF50633_IRQ_USBLIMON,
	PCF50633_IRQ_USBLIMOFF,
	PCF50633_IRQ_LOWSYS,
	PCF50633_IRQ_LOWBAT,
};

static int __devinit pcf50633_mbc_probe(struct platform_device *pdev)
{
	struct pcf50633_mbc *mbc;
	struct pcf50633_subdev_pdata *pdata = pdev->dev.platform_data;
	int ret;
	int i;
	u8 mbcs1;

	mbc = kzalloc(sizeof(*mbc), GFP_KERNEL);
	if (!mbc)
		return -ENOMEM;

	platform_set_drvdata(pdev, mbc);
	mbc->pcf = pdata->pcf;

	/* Set up IRQ handlers */
	for (i = 0; i < ARRAY_SIZE(mbc_irq_handlers); i++)
		pcf50633_register_irq(mbc->pcf, mbc_irq_handlers[i],
					pcf50633_mbc_irq_handler, mbc);

	/* Create power supplies */
	mbc->adapter.name		= "adapter";
	mbc->adapter.type		= POWER_SUPPLY_TYPE_MAINS;
	mbc->adapter.properties		= power_props;
	mbc->adapter.num_properties	= ARRAY_SIZE(power_props);
	mbc->adapter.get_property	= &adapter_get_property;
	mbc->adapter.supplied_to	= mbc->pcf->pdata->batteries;
	mbc->adapter.num_supplicants	= mbc->pcf->pdata->num_batteries;

	mbc->usb.name			= "usb";
	mbc->usb.type			= POWER_SUPPLY_TYPE_USB;
	mbc->usb.properties		= power_props;
	mbc->usb.num_properties		= ARRAY_SIZE(power_props);
	mbc->usb.get_property		= usb_get_property;
	mbc->usb.supplied_to		= mbc->pcf->pdata->batteries;
	mbc->usb.num_supplicants	= mbc->pcf->pdata->num_batteries;

	ret = power_supply_register(&pdev->dev, &mbc->adapter);
	if (ret) {
		dev_err(mbc->pcf->dev, "failed to register adapter\n");
		kfree(mbc);
		return ret;
	}

	ret = power_supply_register(&pdev->dev, &mbc->usb);
	if (ret) {
		dev_err(mbc->pcf->dev, "failed to register usb\n");
		power_supply_unregister(&mbc->adapter);
		kfree(mbc);
		return ret;
	}

	ret = sysfs_create_group(&pdev->dev.kobj, &mbc_attr_group);
	if (ret)
		dev_err(mbc->pcf->dev, "failed to create sysfs entries\n");

	mbcs1 = pcf50633_reg_read(mbc->pcf, PCF50633_REG_MBCS1);
	if (mbcs1 & PCF50633_MBCS1_USBPRES)
		pcf50633_mbc_irq_handler(PCF50633_IRQ_USBINS, mbc);
	if (mbcs1 & PCF50633_MBCS1_ADAPTPRES)
		pcf50633_mbc_irq_handler(PCF50633_IRQ_ADPINS, mbc);

	return 0;
}

static int __devexit pcf50633_mbc_remove(struct platform_device *pdev)
{
	struct pcf50633_mbc *mbc = platform_get_drvdata(pdev);
	int i;

	/* Remove IRQ handlers */
	for (i = 0; i < ARRAY_SIZE(mbc_irq_handlers); i++)
		pcf50633_free_irq(mbc->pcf, mbc_irq_handlers[i]);

	power_supply_unregister(&mbc->usb);
	power_supply_unregister(&mbc->adapter);

	kfree(mbc);

	return 0;
}

static struct platform_driver pcf50633_mbc_driver = {
	.driver = {
		.name = "pcf50633-mbc",
	},
	.probe = pcf50633_mbc_probe,
	.remove = __devexit_p(pcf50633_mbc_remove),
};

static int __init pcf50633_mbc_init(void)
{
	return platform_driver_register(&pcf50633_mbc_driver);
}
module_init(pcf50633_mbc_init);

static void __exit pcf50633_mbc_exit(void)
{
	platform_driver_unregister(&pcf50633_mbc_driver);
}
module_exit(pcf50633_mbc_exit);

MODULE_AUTHOR("Balaji Rao <balajirrao@openmoko.org>");
MODULE_DESCRIPTION("PCF50633 mbc driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pcf50633-mbc");
