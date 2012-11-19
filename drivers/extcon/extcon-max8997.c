/*
 * extcon-max8997.c - MAX8997 extcon driver to support MAX8997 MUIC
 *
 *  Copyright (C) 2012 Samsung Electrnoics
 *  Donggeun Kim <dg77.kim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/kobject.h>
#include <linux/mfd/max8997.h>
#include <linux/mfd/max8997-private.h>
#include <linux/extcon.h>
#include <linux/irqdomain.h>

#define	DEV_NAME			"max8997-muic"

/* MAX8997-MUIC STATUS1 register */
#define STATUS1_ADC_SHIFT		0
#define STATUS1_ADCLOW_SHIFT		5
#define STATUS1_ADCERR_SHIFT		6
#define STATUS1_ADC_MASK		(0x1f << STATUS1_ADC_SHIFT)
#define STATUS1_ADCLOW_MASK		(0x1 << STATUS1_ADCLOW_SHIFT)
#define STATUS1_ADCERR_MASK		(0x1 << STATUS1_ADCERR_SHIFT)

/* MAX8997-MUIC STATUS2 register */
#define STATUS2_CHGTYP_SHIFT		0
#define STATUS2_CHGDETRUN_SHIFT		3
#define STATUS2_DCDTMR_SHIFT		4
#define STATUS2_DBCHG_SHIFT		5
#define STATUS2_VBVOLT_SHIFT		6
#define STATUS2_CHGTYP_MASK		(0x7 << STATUS2_CHGTYP_SHIFT)
#define STATUS2_CHGDETRUN_MASK		(0x1 << STATUS2_CHGDETRUN_SHIFT)
#define STATUS2_DCDTMR_MASK		(0x1 << STATUS2_DCDTMR_SHIFT)
#define STATUS2_DBCHG_MASK		(0x1 << STATUS2_DBCHG_SHIFT)
#define STATUS2_VBVOLT_MASK		(0x1 << STATUS2_VBVOLT_SHIFT)

/* MAX8997-MUIC STATUS3 register */
#define STATUS3_OVP_SHIFT		2
#define STATUS3_OVP_MASK		(0x1 << STATUS3_OVP_SHIFT)

/* MAX8997-MUIC CONTROL1 register */
#define COMN1SW_SHIFT			0
#define COMP2SW_SHIFT			3
#define COMN1SW_MASK			(0x7 << COMN1SW_SHIFT)
#define COMP2SW_MASK			(0x7 << COMP2SW_SHIFT)
#define SW_MASK				(COMP2SW_MASK | COMN1SW_MASK)

#define MAX8997_SW_USB		((1 << COMP2SW_SHIFT) | (1 << COMN1SW_SHIFT))
#define MAX8997_SW_AUDIO	((2 << COMP2SW_SHIFT) | (2 << COMN1SW_SHIFT))
#define MAX8997_SW_UART		((3 << COMP2SW_SHIFT) | (3 << COMN1SW_SHIFT))
#define MAX8997_SW_OPEN		((0 << COMP2SW_SHIFT) | (0 << COMN1SW_SHIFT))

#define	MAX8997_ADC_GROUND		0x00
#define	MAX8997_ADC_MHL			0x01
#define	MAX8997_ADC_JIG_USB_1		0x18
#define	MAX8997_ADC_JIG_USB_2		0x19
#define	MAX8997_ADC_DESKDOCK		0x1a
#define	MAX8997_ADC_JIG_UART		0x1c
#define	MAX8997_ADC_CARDOCK		0x1d
#define	MAX8997_ADC_OPEN		0x1f

struct max8997_muic_irq {
	unsigned int irq;
	const char *name;
	unsigned int virq;
};

static struct max8997_muic_irq muic_irqs[] = {
	{ MAX8997_MUICIRQ_ADCError, "muic-ADC_error" },
	{ MAX8997_MUICIRQ_ADCLow, "muic-ADC_low" },
	{ MAX8997_MUICIRQ_ADC, "muic-ADC" },
	{ MAX8997_MUICIRQ_VBVolt, "muic-VB_voltage" },
	{ MAX8997_MUICIRQ_DBChg, "muic-DB_charger" },
	{ MAX8997_MUICIRQ_DCDTmr, "muic-DCD_timer" },
	{ MAX8997_MUICIRQ_ChgDetRun, "muic-CDR_status" },
	{ MAX8997_MUICIRQ_ChgTyp, "muic-charger_type" },
	{ MAX8997_MUICIRQ_OVP, "muic-over_voltage" },
};

struct max8997_muic_info {
	struct device *dev;
	struct i2c_client *muic;
	struct max8997_muic_platform_data *muic_pdata;

	int irq;
	struct work_struct irq_work;

	enum max8997_muic_charger_type pre_charger_type;
	int pre_adc;

	struct mutex mutex;

	struct extcon_dev	*edev;
};

const char *max8997_extcon_cable[] = {
	[0] = "USB",
	[1] = "USB-Host",
	[2] = "TA",
	[3] = "Fast-charger",
	[4] = "Slow-charger",
	[5] = "Charge-downstream",
	[6] = "MHL",
	[7] = "Dock-desk",
	[8] = "Dock-card",
	[9] = "JIG",

	NULL,
};

static int max8997_muic_handle_usb(struct max8997_muic_info *info,
			enum max8997_muic_usb_type usb_type, bool attached)
{
	int ret = 0;

	if (usb_type == MAX8997_USB_HOST) {
		/* switch to USB */
		ret = max8997_update_reg(info->muic, MAX8997_MUIC_REG_CONTROL1,
				attached ? MAX8997_SW_USB : MAX8997_SW_OPEN,
				SW_MASK);
		if (ret) {
			dev_err(info->dev, "failed to update muic register\n");
			goto out;
		}
	}

	switch (usb_type) {
	case MAX8997_USB_HOST:
		extcon_set_cable_state(info->edev, "USB-Host", attached);
		break;
	case MAX8997_USB_DEVICE:
		extcon_set_cable_state(info->edev, "USB", attached);
		break;
	default:
		ret = -EINVAL;
		break;
	}

out:
	return ret;
}

static int max8997_muic_handle_dock(struct max8997_muic_info *info,
			int adc, bool attached)
{
	int ret = 0;

	/* switch to AUDIO */
	ret = max8997_update_reg(info->muic, MAX8997_MUIC_REG_CONTROL1,
				attached ? MAX8997_SW_AUDIO : MAX8997_SW_OPEN,
				SW_MASK);
	if (ret) {
		dev_err(info->dev, "failed to update muic register\n");
		goto out;
	}

	switch (adc) {
	case MAX8997_ADC_DESKDOCK:
		extcon_set_cable_state(info->edev, "Dock-desk", attached);
		break;
	case MAX8997_ADC_CARDOCK:
		extcon_set_cable_state(info->edev, "Dock-card", attached);
		break;
	default:
		ret = -EINVAL;
		break;
	}
out:
	return ret;
}

static int max8997_muic_handle_jig_uart(struct max8997_muic_info *info,
			bool attached)
{
	int ret = 0;

	/* switch to UART */
	ret = max8997_update_reg(info->muic, MAX8997_MUIC_REG_CONTROL1,
				attached ? MAX8997_SW_UART : MAX8997_SW_OPEN,
				SW_MASK);
	if (ret) {
		dev_err(info->dev, "failed to update muic register\n");
		goto out;
	}

	extcon_set_cable_state(info->edev, "JIG", attached);
out:
	return ret;
}

static int max8997_muic_handle_adc_detach(struct max8997_muic_info *info)
{
	int ret = 0;

	switch (info->pre_adc) {
	case MAX8997_ADC_GROUND:
		ret = max8997_muic_handle_usb(info, MAX8997_USB_HOST, false);
		break;
	case MAX8997_ADC_MHL:
		extcon_set_cable_state(info->edev, "MHL", false);
		break;
	case MAX8997_ADC_JIG_USB_1:
	case MAX8997_ADC_JIG_USB_2:
		ret = max8997_muic_handle_usb(info, MAX8997_USB_DEVICE, false);
		break;
	case MAX8997_ADC_DESKDOCK:
	case MAX8997_ADC_CARDOCK:
		ret = max8997_muic_handle_dock(info, info->pre_adc, false);
		break;
	case MAX8997_ADC_JIG_UART:
		ret = max8997_muic_handle_jig_uart(info, false);
		break;
	default:
		break;
	}

	return ret;
}

static int max8997_muic_handle_adc(struct max8997_muic_info *info, int adc)
{
	int ret = 0;

	switch (adc) {
	case MAX8997_ADC_GROUND:
		ret = max8997_muic_handle_usb(info, MAX8997_USB_HOST, true);
		break;
	case MAX8997_ADC_MHL:
		extcon_set_cable_state(info->edev, "MHL", true);
		break;
	case MAX8997_ADC_JIG_USB_1:
	case MAX8997_ADC_JIG_USB_2:
		ret = max8997_muic_handle_usb(info, MAX8997_USB_DEVICE, true);
		break;
	case MAX8997_ADC_DESKDOCK:
	case MAX8997_ADC_CARDOCK:
		ret = max8997_muic_handle_dock(info, adc, true);
		break;
	case MAX8997_ADC_JIG_UART:
		ret = max8997_muic_handle_jig_uart(info, true);
		break;
	case MAX8997_ADC_OPEN:
		ret = max8997_muic_handle_adc_detach(info);
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

	info->pre_adc = adc;
out:
	return ret;
}

static int max8997_muic_handle_charger_type_detach(
				struct max8997_muic_info *info)
{
	switch (info->pre_charger_type) {
	case MAX8997_CHARGER_TYPE_USB:
		extcon_set_cable_state(info->edev, "USB", false);
		break;
	case MAX8997_CHARGER_TYPE_DOWNSTREAM_PORT:
		extcon_set_cable_state(info->edev, "Charge-downstream", false);
		break;
	case MAX8997_CHARGER_TYPE_DEDICATED_CHG:
		extcon_set_cable_state(info->edev, "TA", false);
		break;
	case MAX8997_CHARGER_TYPE_500MA:
		extcon_set_cable_state(info->edev, "Slow-charger", false);
		break;
	case MAX8997_CHARGER_TYPE_1A:
		extcon_set_cable_state(info->edev, "Fast-charger", false);
		break;
	default:
		return -EINVAL;
		break;
	}

	return 0;
}

static int max8997_muic_handle_charger_type(struct max8997_muic_info *info,
				enum max8997_muic_charger_type charger_type)
{
	u8 adc;
	int ret;

	ret = max8997_read_reg(info->muic, MAX8997_MUIC_REG_STATUS1, &adc);
	if (ret) {
		dev_err(info->dev, "failed to read muic register\n");
		goto out;
	}

	switch (charger_type) {
	case MAX8997_CHARGER_TYPE_NONE:
		ret = max8997_muic_handle_charger_type_detach(info);
		break;
	case MAX8997_CHARGER_TYPE_USB:
		if ((adc & STATUS1_ADC_MASK) == MAX8997_ADC_OPEN) {
			max8997_muic_handle_usb(info,
					MAX8997_USB_DEVICE, true);
		}
		break;
	case MAX8997_CHARGER_TYPE_DOWNSTREAM_PORT:
		extcon_set_cable_state(info->edev, "Charge-downstream", true);
		break;
	case MAX8997_CHARGER_TYPE_DEDICATED_CHG:
		extcon_set_cable_state(info->edev, "TA", true);
		break;
	case MAX8997_CHARGER_TYPE_500MA:
		extcon_set_cable_state(info->edev, "Slow-charger", true);
		break;
	case MAX8997_CHARGER_TYPE_1A:
		extcon_set_cable_state(info->edev, "Fast-charger", true);
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

	info->pre_charger_type = charger_type;
out:
	return ret;
}

static void max8997_muic_irq_work(struct work_struct *work)
{
	struct max8997_muic_info *info = container_of(work,
			struct max8997_muic_info, irq_work);
	u8 status[2];
	u8 adc, chg_type;
	int irq_type = 0;
	int i, ret;

	mutex_lock(&info->mutex);

	ret = max8997_bulk_read(info->muic, MAX8997_MUIC_REG_STATUS1,
				2, status);
	if (ret) {
		dev_err(info->dev, "failed to read muic register\n");
		mutex_unlock(&info->mutex);
		return;
	}

	dev_dbg(info->dev, "%s: STATUS1:0x%x, 2:0x%x\n", __func__,
			status[0], status[1]);

	for (i = 0 ; i < ARRAY_SIZE(muic_irqs) ; i++)
		if (info->irq == muic_irqs[i].virq)
			irq_type = muic_irqs[i].irq;

	switch (irq_type) {
	case MAX8997_MUICIRQ_ADC:
		adc = status[0] & STATUS1_ADC_MASK;
		adc >>= STATUS1_ADC_SHIFT;

		max8997_muic_handle_adc(info, adc);
		break;
	case MAX8997_MUICIRQ_ChgTyp:
		chg_type = status[1] & STATUS2_CHGTYP_MASK;
		chg_type >>= STATUS2_CHGTYP_SHIFT;

		max8997_muic_handle_charger_type(info, chg_type);
		break;
	default:
		dev_info(info->dev, "misc interrupt: irq %d occurred\n",
				irq_type);
		break;
	}

	mutex_unlock(&info->mutex);

	return;
}

static irqreturn_t max8997_muic_irq_handler(int irq, void *data)
{
	struct max8997_muic_info *info = data;

	dev_dbg(info->dev, "irq:%d\n", irq);
	info->irq = irq;

	schedule_work(&info->irq_work);

	return IRQ_HANDLED;
}

static void max8997_muic_detect_dev(struct max8997_muic_info *info)
{
	int ret;
	u8 status[2], adc, chg_type;

	ret = max8997_bulk_read(info->muic, MAX8997_MUIC_REG_STATUS1,
				2, status);
	if (ret) {
		dev_err(info->dev, "failed to read muic register\n");
		return;
	}

	dev_info(info->dev, "STATUS1:0x%x, STATUS2:0x%x\n",
			status[0], status[1]);

	adc = status[0] & STATUS1_ADC_MASK;
	adc >>= STATUS1_ADC_SHIFT;

	chg_type = status[1] & STATUS2_CHGTYP_MASK;
	chg_type >>= STATUS2_CHGTYP_SHIFT;

	max8997_muic_handle_adc(info, adc);
	max8997_muic_handle_charger_type(info, chg_type);
}

static int max8997_muic_probe(struct platform_device *pdev)
{
	struct max8997_dev *max8997 = dev_get_drvdata(pdev->dev.parent);
	struct max8997_platform_data *pdata = dev_get_platdata(max8997->dev);
	struct max8997_muic_info *info;
	int ret, i;

	info = kzalloc(sizeof(struct max8997_muic_info), GFP_KERNEL);
	if (!info) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		ret = -ENOMEM;
		goto err_kfree;
	}

	info->dev = &pdev->dev;
	info->muic = max8997->muic;

	platform_set_drvdata(pdev, info);
	mutex_init(&info->mutex);

	INIT_WORK(&info->irq_work, max8997_muic_irq_work);

	for (i = 0; i < ARRAY_SIZE(muic_irqs); i++) {
		struct max8997_muic_irq *muic_irq = &muic_irqs[i];
		int virq = 0;

		virq = irq_create_mapping(max8997->irq_domain, muic_irq->irq);
		if (!virq)
			goto err_irq;
		muic_irq->virq = virq;

		ret = request_threaded_irq(virq, NULL,max8997_muic_irq_handler,
				0, muic_irq->name, info);
		if (ret) {
			dev_err(&pdev->dev,
				"failed: irq request (IRQ: %d,"
				" error :%d)\n",
				muic_irq->irq, ret);
			goto err_irq;
		}
	}

	/* External connector */
	info->edev = kzalloc(sizeof(struct extcon_dev), GFP_KERNEL);
	if (!info->edev) {
		dev_err(&pdev->dev, "failed to allocate memory for extcon\n");
		ret = -ENOMEM;
		goto err_irq;
	}
	info->edev->name = DEV_NAME;
	info->edev->supported_cable = max8997_extcon_cable;
	ret = extcon_dev_register(info->edev, NULL);
	if (ret) {
		dev_err(&pdev->dev, "failed to register extcon device\n");
		goto err_extcon;
	}

	/* Initialize registers according to platform data */
	if (pdata->muic_pdata) {
		struct max8997_muic_platform_data *mdata = info->muic_pdata;

		for (i = 0; i < mdata->num_init_data; i++) {
			max8997_write_reg(info->muic, mdata->init_data[i].addr,
					mdata->init_data[i].data);
		}
	}

	/* Initial device detection */
	max8997_muic_detect_dev(info);

	return ret;

err_extcon:
	kfree(info->edev);
err_irq:
	while (--i >= 0)
		free_irq(muic_irqs[i].virq, info);
	kfree(info);
err_kfree:
	return ret;
}

static int max8997_muic_remove(struct platform_device *pdev)
{
	struct max8997_muic_info *info = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < ARRAY_SIZE(muic_irqs); i++)
		free_irq(muic_irqs[i].virq, info);
	cancel_work_sync(&info->irq_work);

	extcon_dev_unregister(info->edev);

	kfree(info->edev);
	kfree(info);

	return 0;
}

static struct platform_driver max8997_muic_driver = {
	.driver		= {
		.name	= DEV_NAME,
		.owner	= THIS_MODULE,
	},
	.probe		= max8997_muic_probe,
	.remove		= __devexit_p(max8997_muic_remove),
};

module_platform_driver(max8997_muic_driver);

MODULE_DESCRIPTION("Maxim MAX8997 Extcon driver");
MODULE_AUTHOR("Donggeun Kim <dg77.kim@samsung.com>");
MODULE_LICENSE("GPL");
