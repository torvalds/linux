// SPDX-License-Identifier: GPL-2.0
/*
 * extcon driver for Basin Cove PMIC
 *
 * Copyright (c) 2019, Intel Corporation.
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 */

#include <linux/extcon-provider.h>
#include <linux/interrupt.h>
#include <linux/mfd/intel_soc_pmic.h>
#include <linux/mfd/intel_soc_pmic_mrfld.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "extcon-intel.h"

#define BCOVE_USBIDCTRL			0x19
#define BCOVE_USBIDCTRL_ID		BIT(0)
#define BCOVE_USBIDCTRL_ACA		BIT(1)
#define BCOVE_USBIDCTRL_ALL	(BCOVE_USBIDCTRL_ID | BCOVE_USBIDCTRL_ACA)

#define BCOVE_USBIDSTS			0x1a
#define BCOVE_USBIDSTS_GND		BIT(0)
#define BCOVE_USBIDSTS_RARBRC_MASK	GENMASK(2, 1)
#define BCOVE_USBIDSTS_RARBRC_SHIFT	1
#define BCOVE_USBIDSTS_NO_ACA		0
#define BCOVE_USBIDSTS_R_ID_A		1
#define BCOVE_USBIDSTS_R_ID_B		2
#define BCOVE_USBIDSTS_R_ID_C		3
#define BCOVE_USBIDSTS_FLOAT		BIT(3)
#define BCOVE_USBIDSTS_SHORT		BIT(4)

#define BCOVE_CHGRIRQ_ALL	(BCOVE_CHGRIRQ_VBUSDET | BCOVE_CHGRIRQ_DCDET | \
				 BCOVE_CHGRIRQ_BATTDET | BCOVE_CHGRIRQ_USBIDDET)

#define BCOVE_CHGRCTRL0			0x4b
#define BCOVE_CHGRCTRL0_CHGRRESET	BIT(0)
#define BCOVE_CHGRCTRL0_EMRGCHREN	BIT(1)
#define BCOVE_CHGRCTRL0_EXTCHRDIS	BIT(2)
#define BCOVE_CHGRCTRL0_SWCONTROL	BIT(3)
#define BCOVE_CHGRCTRL0_TTLCK		BIT(4)
#define BCOVE_CHGRCTRL0_BIT_5		BIT(5)
#define BCOVE_CHGRCTRL0_BIT_6		BIT(6)
#define BCOVE_CHGRCTRL0_CHR_WDT_NOKICK	BIT(7)

struct mrfld_extcon_data {
	struct device *dev;
	struct regmap *regmap;
	struct extcon_dev *edev;
	unsigned int status;
	unsigned int id;
};

static const unsigned int mrfld_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_CHG_USB_SDP,
	EXTCON_CHG_USB_CDP,
	EXTCON_CHG_USB_DCP,
	EXTCON_CHG_USB_ACA,
	EXTCON_NONE,
};

static int mrfld_extcon_clear(struct mrfld_extcon_data *data, unsigned int reg,
			      unsigned int mask)
{
	return regmap_update_bits(data->regmap, reg, mask, 0x00);
}

static int mrfld_extcon_set(struct mrfld_extcon_data *data, unsigned int reg,
			    unsigned int mask)
{
	return regmap_update_bits(data->regmap, reg, mask, 0xff);
}

static int mrfld_extcon_sw_control(struct mrfld_extcon_data *data, bool enable)
{
	unsigned int mask = BCOVE_CHGRCTRL0_SWCONTROL;
	struct device *dev = data->dev;
	int ret;

	if (enable)
		ret = mrfld_extcon_set(data, BCOVE_CHGRCTRL0, mask);
	else
		ret = mrfld_extcon_clear(data, BCOVE_CHGRCTRL0, mask);
	if (ret)
		dev_err(dev, "can't set SW control: %d\n", ret);
	return ret;
}

static int mrfld_extcon_get_id(struct mrfld_extcon_data *data)
{
	struct regmap *regmap = data->regmap;
	unsigned int id;
	bool ground;
	int ret;

	ret = regmap_read(regmap, BCOVE_USBIDSTS, &id);
	if (ret)
		return ret;

	if (id & BCOVE_USBIDSTS_FLOAT)
		return INTEL_USB_ID_FLOAT;

	switch ((id & BCOVE_USBIDSTS_RARBRC_MASK) >> BCOVE_USBIDSTS_RARBRC_SHIFT) {
	case BCOVE_USBIDSTS_R_ID_A:
		return INTEL_USB_RID_A;
	case BCOVE_USBIDSTS_R_ID_B:
		return INTEL_USB_RID_B;
	case BCOVE_USBIDSTS_R_ID_C:
		return INTEL_USB_RID_C;
	}

	/*
	 * PMIC A0 reports USBIDSTS_GND = 1 for ID_GND,
	 * but PMIC B0 reports USBIDSTS_GND = 0 for ID_GND.
	 * Thus we must check this bit at last.
	 */
	ground = id & BCOVE_USBIDSTS_GND;
	switch ('A' + BCOVE_MAJOR(data->id)) {
	case 'A':
		return ground ? INTEL_USB_ID_GND : INTEL_USB_ID_FLOAT;
	case 'B':
		return ground ? INTEL_USB_ID_FLOAT : INTEL_USB_ID_GND;
	}

	/* Unknown or unsupported type */
	return INTEL_USB_ID_FLOAT;
}

static int mrfld_extcon_role_detect(struct mrfld_extcon_data *data)
{
	unsigned int id;
	bool usb_host;
	int ret;

	ret = mrfld_extcon_get_id(data);
	if (ret < 0)
		return ret;

	id = ret;

	usb_host = (id == INTEL_USB_ID_GND) || (id == INTEL_USB_RID_A);
	extcon_set_state_sync(data->edev, EXTCON_USB_HOST, usb_host);

	return 0;
}

static int mrfld_extcon_cable_detect(struct mrfld_extcon_data *data)
{
	struct regmap *regmap = data->regmap;
	unsigned int status, change;
	int ret;

	/*
	 * It seems SCU firmware clears the content of BCOVE_CHGRIRQ1
	 * and makes it useless for OS. Instead we compare a previously
	 * stored status to the current one, provided by BCOVE_SCHGRIRQ1.
	 */
	ret = regmap_read(regmap, BCOVE_SCHGRIRQ1, &status);
	if (ret)
		return ret;

	change = status ^ data->status;
	if (!change)
		return -ENODATA;

	if (change & BCOVE_CHGRIRQ_USBIDDET) {
		ret = mrfld_extcon_role_detect(data);
		if (ret)
			return ret;
	}

	data->status = status;

	return 0;
}

static irqreturn_t mrfld_extcon_interrupt(int irq, void *dev_id)
{
	struct mrfld_extcon_data *data = dev_id;
	int ret;

	ret = mrfld_extcon_cable_detect(data);

	mrfld_extcon_clear(data, BCOVE_MIRQLVL1, BCOVE_LVL1_CHGR);

	return ret ? IRQ_NONE: IRQ_HANDLED;
}

static int mrfld_extcon_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct intel_soc_pmic *pmic = dev_get_drvdata(dev->parent);
	struct regmap *regmap = pmic->regmap;
	struct mrfld_extcon_data *data;
	unsigned int status;
	unsigned int id;
	int irq, ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev;
	data->regmap = regmap;

	data->edev = devm_extcon_dev_allocate(dev, mrfld_extcon_cable);
	if (IS_ERR(data->edev))
		return -ENOMEM;

	ret = devm_extcon_dev_register(dev, data->edev);
	if (ret < 0) {
		dev_err(dev, "can't register extcon device: %d\n", ret);
		return ret;
	}

	ret = devm_request_threaded_irq(dev, irq, NULL, mrfld_extcon_interrupt,
					IRQF_ONESHOT | IRQF_SHARED, pdev->name,
					data);
	if (ret) {
		dev_err(dev, "can't register IRQ handler: %d\n", ret);
		return ret;
	}

	ret = regmap_read(regmap, BCOVE_ID, &id);
	if (ret) {
		dev_err(dev, "can't read PMIC ID: %d\n", ret);
		return ret;
	}

	data->id = id;

	ret = mrfld_extcon_sw_control(data, true);
	if (ret)
		return ret;

	/* Get initial state */
	mrfld_extcon_role_detect(data);

	/*
	 * Cached status value is used for cable detection, see comments
	 * in mrfld_extcon_cable_detect(), we need to sync cached value
	 * with a real state of the hardware.
	 */
	regmap_read(regmap, BCOVE_SCHGRIRQ1, &status);
	data->status = status;

	mrfld_extcon_clear(data, BCOVE_MIRQLVL1, BCOVE_LVL1_CHGR);
	mrfld_extcon_clear(data, BCOVE_MCHGRIRQ1, BCOVE_CHGRIRQ_ALL);

	mrfld_extcon_set(data, BCOVE_USBIDCTRL, BCOVE_USBIDCTRL_ALL);

	platform_set_drvdata(pdev, data);

	return 0;
}

static int mrfld_extcon_remove(struct platform_device *pdev)
{
	struct mrfld_extcon_data *data = platform_get_drvdata(pdev);

	mrfld_extcon_sw_control(data, false);

	return 0;
}

static const struct platform_device_id mrfld_extcon_id_table[] = {
	{ .name = "mrfld_bcove_pwrsrc" },
	{}
};
MODULE_DEVICE_TABLE(platform, mrfld_extcon_id_table);

static struct platform_driver mrfld_extcon_driver = {
	.driver = {
		.name	= "mrfld_bcove_pwrsrc",
	},
	.probe		= mrfld_extcon_probe,
	.remove		= mrfld_extcon_remove,
	.id_table	= mrfld_extcon_id_table,
};
module_platform_driver(mrfld_extcon_driver);

MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
MODULE_DESCRIPTION("extcon driver for Intel Merrifield Basin Cove PMIC");
MODULE_LICENSE("GPL v2");
