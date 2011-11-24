/*
 * max8997-muic.c - MAX8997 muic driver for the Maxim 8997
 *
 *  Copyright (C) 2011 Samsung Electrnoics
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
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
	struct max8997_dev *iodev;
	struct i2c_client *muic;
	struct max8997_muic_platform_data *muic_pdata;

	int irq;
	struct work_struct irq_work;

	enum max8997_muic_charger_type pre_charger_type;
	int pre_adc;

	struct mutex mutex;
};

static int max8997_muic_handle_usb(struct max8997_muic_info *info,
			enum max8997_muic_usb_type usb_type, bool attached)
{
	struct max8997_muic_platform_data *mdata = info->muic_pdata;
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

	if (mdata->usb_callback)
		mdata->usb_callback(usb_type, attached);
out:
	return ret;
}

static void max8997_muic_handle_mhl(struct max8997_muic_info *info,
			bool attached)
{
	struct max8997_muic_platform_data *mdata = info->muic_pdata;

	if (mdata->mhl_callback)
		mdata->mhl_callback(attached);
}

static int max8997_muic_handle_dock(struct max8997_muic_info *info,
			int adc, bool attached)
{
	struct max8997_muic_platform_data *mdata = info->muic_pdata;
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
		if (mdata->deskdock_callback)
			mdata->deskdock_callback(attached);
		break;
	case MAX8997_ADC_CARDOCK:
		if (mdata->cardock_callback)
			mdata->cardock_callback(attached);
		break;
	default:
		break;
	}
out:
	return ret;
}

static int max8997_muic_handle_jig_uart(struct max8997_muic_info *info,
			bool attached)
{
	struct max8997_muic_platform_data *mdata = info->muic_pdata;
	int ret = 0;

	/* switch to UART */
	ret = max8997_update_reg(info->muic, MAX8997_MUIC_REG_CONTROL1,
				attached ? MAX8997_SW_UART : MAX8997_SW_OPEN,
				SW_MASK);
	if (ret) {
		dev_err(info->dev, "failed to update muic register\n");
		goto out;
	}

	if (mdata->uart_callback)
		mdata->uart_callback(attached);
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
		max8997_muic_handle_mhl(info, false);
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
		max8997_muic_handle_mhl(info, true);
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
		break;
	}

	info->pre_adc = adc;

	return ret;
}

static int max8997_muic_handle_charger_type(struct max8997_muic_info *info,
				enum max8997_muic_charger_type charger_type)
{
	struct max8997_muic_platform_data *mdata = info->muic_pdata;
	u8 adc;
	int ret;

	ret = max8997_read_reg(info->muic, MAX8997_MUIC_REG_STATUS1, &adc);
	if (ret) {
		dev_err(info->dev, "failed to read muic register\n");
		goto out;
	}

	switch (charger_type) {
	case MAX8997_CHARGER_TYPE_NONE:
		if (mdata->charger_callback)
			mdata->charger_callback(false, charger_type);
		if (info->pre_charger_type == MAX8997_CHARGER_TYPE_USB) {
			max8997_muic_handle_usb(info,
					MAX8997_USB_DEVICE, false);
		}
		break;
	case MAX8997_CHARGER_TYPE_USB:
		if ((adc & STATUS1_ADC_MASK) == MAX8997_ADC_OPEN) {
			max8997_muic_handle_usb(info,
					MAX8997_USB_DEVICE, true);
		}
		if (mdata->charger_callback)
			mdata->charger_callback(true, charger_type);
		break;
	case MAX8997_CHARGER_TYPE_DOWNSTREAM_PORT:
	case MAX8997_CHARGER_TYPE_DEDICATED_CHG:
	case MAX8997_CHARGER_TYPE_500MA:
	case MAX8997_CHARGER_TYPE_1A:
		if (mdata->charger_callback)
			mdata->charger_callback(true, charger_type);
		break;
	default:
		break;
	}

	info->pre_charger_type = charger_type;
out:
	return ret;
}

static void max8997_muic_irq_work(struct work_struct *work)
{
	struct max8997_muic_info *info = container_of(work,
			struct max8997_muic_info, irq_work);
	struct max8997_platform_data *pdata =
				dev_get_platdata(info->iodev->dev);
	char env_name_str[30], env_state_str[15];
	char *envp[] = { env_name_str, env_state_str, NULL };
	u8 status[3];
	u8 adc, adc_low, adc_err;
	u8 vb_volt, db_chg, dcd_tmr, cdr, chg_type, ovp;

	int irq_type = info->irq - pdata->irq_base;
	int ret;

	mutex_lock(&info->mutex);

	ret = max8997_bulk_read(info->muic, MAX8997_MUIC_REG_STATUS1,
				3, status);
	if (ret) {
		dev_err(info->dev, "failed to read muic register\n");
		mutex_unlock(&info->mutex);
		return;
	}

	dev_dbg(info->dev, "%s: STATUS1:0x%x, 2:0x%x\n", __func__,
			status[0], status[1]);

	switch (irq_type) {
	case MAX8997_MUICIRQ_ADCError:
		adc_err = status[0] & STATUS1_ADCERR_MASK;
		adc_err >>= STATUS1_ADCERR_SHIFT;
		sprintf(env_name_str, "IRQ=%s", "ADC_Error");
		sprintf(env_state_str, "STATE=%d", adc_err);
		break;
	case MAX8997_MUICIRQ_ADCLow:
		adc_low = status[0] & STATUS1_ADCLOW_MASK;
		adc_low >>= STATUS1_ADCLOW_SHIFT;
		sprintf(env_name_str, "IRQ=%s", "ADC_Low");
		sprintf(env_state_str, "STATE=%d", adc_low);
		break;
	case MAX8997_MUICIRQ_ADC:
		adc = status[0] & STATUS1_ADC_MASK;
		adc >>= STATUS1_ADC_SHIFT;

		max8997_muic_handle_adc(info, adc);

		sprintf(env_name_str, "IRQ=%s", "ADC");
		sprintf(env_state_str, "STATE=%d", adc);
		break;
	case MAX8997_MUICIRQ_VBVolt:
		vb_volt = status[1] & STATUS2_VBVOLT_MASK;
		vb_volt >>= STATUS2_VBVOLT_SHIFT;
		sprintf(env_name_str, "IRQ=%s", "VB_Volt");
		sprintf(env_state_str, "STATE=%d", vb_volt);
		break;
	case MAX8997_MUICIRQ_DBChg:
		db_chg = status[1] & STATUS2_DBCHG_MASK;
		db_chg >>= STATUS2_DBCHG_SHIFT;
		sprintf(env_name_str, "IRQ=%s", "DB_CHARGER");
		sprintf(env_state_str, "STATE=%d", db_chg);
		break;
	case MAX8997_MUICIRQ_DCDTmr:
		dcd_tmr = status[1] & STATUS2_DCDTMR_MASK;
		dcd_tmr >>= STATUS2_DCDTMR_SHIFT;
		sprintf(env_name_str, "IRQ=%s", "DCD_TIMER");
		sprintf(env_state_str, "STATE=%d", dcd_tmr);
		break;
	case MAX8997_MUICIRQ_ChgDetRun:
		cdr = status[1] & STATUS2_CHGDETRUN_MASK;
		cdr >>= STATUS2_CHGDETRUN_SHIFT;
		sprintf(env_name_str, "IRQ=%s", "CHG_DET_RUN");
		sprintf(env_state_str, "STATE=%d", cdr);
		break;
	case MAX8997_MUICIRQ_ChgTyp:
		chg_type = status[1] & STATUS2_CHGTYP_MASK;
		chg_type >>= STATUS2_CHGTYP_SHIFT;

		max8997_muic_handle_charger_type(info, chg_type);

		sprintf(env_name_str, "IRQ=%s", "CHARGER_TYPE");
		sprintf(env_state_str, "STATE=%d", chg_type);
		break;
	case MAX8997_MUICIRQ_OVP:
		ovp = status[2] & STATUS3_OVP_MASK;
		ovp >>= STATUS3_OVP_SHIFT;
		sprintf(env_name_str, "IRQ=%s", "OVER-VOLTAGE");
		sprintf(env_state_str, "STATE=%d", ovp);
		break;
	default:
		break;
	}

	kobject_uevent_env(&info->dev->kobj, KOBJ_CHANGE, envp);

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

static ssize_t max8997_muic_show_manualsw(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max8997_muic_info *info = dev_get_drvdata(dev);
	int ret;
	u8 value, manual_switch;

	ret = max8997_read_reg(info->muic, MAX8997_MUIC_REG_CONTROL1, &value);
	if (ret) {
		dev_err(info->dev, "failed to read muic register\n");
		return sprintf(buf, "UNKNOWN\n");
	}

	manual_switch = value & (COMN1SW_MASK | COMP2SW_MASK);

	switch (manual_switch) {
	case MAX8997_SW_USB:
		return sprintf(buf, "USB\n");
	case MAX8997_SW_AUDIO:
		return sprintf(buf, "AUDIO\n");
	case MAX8997_SW_UART:
		return sprintf(buf, "UART\n");
	default:
		return sprintf(buf, "OPEN\n");
	}
}

static ssize_t max8997_muic_store_manualsw(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct max8997_muic_info *info = dev_get_drvdata(dev);
	int ret;
	u8 manual_switch;

	if (!strncmp(buf, "USB", 3)) {
		manual_switch = MAX8997_SW_USB;
	} else if (!strncmp(buf, "AUDIO", 5)) {
		manual_switch = MAX8997_SW_AUDIO;
	} else if (!strncmp(buf, "UART", 4)) {
		manual_switch = MAX8997_SW_UART;
	} else if (!strncmp(buf, "OPEN", 4)) {
		manual_switch = MAX8997_SW_OPEN;
	} else {
		dev_err(info->dev, "invalid parameter\n");
		goto out;
	}

	ret = max8997_update_reg(info->muic, MAX8997_MUIC_REG_CONTROL1,
			manual_switch, SW_MASK);
	if (ret)
		dev_err(info->dev, "failed to update muic register\n");
out:
	return count;
}

static DEVICE_ATTR(switch, S_IRUGO | S_IWUSR,
		max8997_muic_show_manualsw, max8997_muic_store_manualsw);

static struct attribute *max8997_muic_attributes[] = {
	&dev_attr_switch.attr,
	NULL
};

static const struct attribute_group max8997_muic_group = {
	.attrs = max8997_muic_attributes,
};

static void max8997_initialize_device(struct max8997_muic_info *info)
{
	struct max8997_muic_platform_data *mdata = info->muic_pdata;
	int i;

	for (i = 0; i < mdata->num_init_data; i++) {
		max8997_write_reg(info->muic, mdata->init_data[i].addr,
				mdata->init_data[i].data);
	}
}

static int __devinit max8997_muic_probe(struct platform_device *pdev)
{
	struct max8997_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct max8997_platform_data *pdata = dev_get_platdata(iodev->dev);
	struct max8997_muic_info *info;
	int ret, i;

	info = kzalloc(sizeof(struct max8997_muic_info), GFP_KERNEL);
	if (!info) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		ret = -ENOMEM;
		goto err_kfree;
	}

	if (!pdata->muic_pdata) {
		dev_err(&pdev->dev, "failed to get platform_data\n");
		ret = -EINVAL;
		goto err_pdata;
	}
	info->muic_pdata = pdata->muic_pdata;

	info->dev = &pdev->dev;
	info->iodev = iodev;
	info->muic = iodev->muic;

	platform_set_drvdata(pdev, info);
	mutex_init(&info->mutex);

	ret = sysfs_create_group(&pdev->dev.kobj, &max8997_muic_group);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to create max8997 muic attribute group\n");
		goto err_sysfs;
	}

	INIT_WORK(&info->irq_work, max8997_muic_irq_work);

	for (i = 0; i < ARRAY_SIZE(muic_irqs); i++) {
		struct max8997_muic_irq *muic_irq = &muic_irqs[i];

		ret = request_threaded_irq(pdata->irq_base + muic_irq->irq,
				NULL, max8997_muic_irq_handler,
				0, muic_irq->name,
				info);
		if (ret) {
			dev_err(&pdev->dev,
				"failed: irq request (IRQ: %d,"
				" error :%d)\n",
				muic_irq->irq, ret);

			for (i = i - 1; i >= 0; i--)
				free_irq(muic_irq->irq, info);

			goto err_irq;
		}
	}

	/* Initialize registers according to platform data */
	max8997_initialize_device(info);

	/* Initial device detection */
	max8997_muic_detect_dev(info);

	return ret;

err_irq:
	sysfs_remove_group(&pdev->dev.kobj, &max8997_muic_group);
err_sysfs:
err_pdata:
	kfree(info);
err_kfree:
	return ret;
}

static int __devexit max8997_muic_remove(struct platform_device *pdev)
{
	struct max8997_muic_info *info = platform_get_drvdata(pdev);
	struct max8997_platform_data *pdata =
				dev_get_platdata(info->iodev->dev);
	int i;

	sysfs_remove_group(&pdev->dev.kobj, &max8997_muic_group);

	for (i = 0; i < ARRAY_SIZE(muic_irqs); i++)
		free_irq(pdata->irq_base + muic_irqs[i].irq, info);

	kfree(info);

	return 0;
}

static struct platform_driver max8997_muic_driver = {
	.driver		= {
		.name	= "max8997-muic",
		.owner	= THIS_MODULE,
	},
	.probe		= max8997_muic_probe,
	.remove		= __devexit_p(max8997_muic_remove),
};

static int __init max8997_muic_init(void)
{
	return platform_driver_register(&max8997_muic_driver);
}
module_init(max8997_muic_init);

static void __exit max8997_muic_exit(void)
{
	platform_driver_unregister(&max8997_muic_driver);
}
module_exit(max8997_muic_exit);

MODULE_DESCRIPTION("Maxim MAX8997 MUIC driver");
MODULE_AUTHOR("Donggeun Kim <dg77.kim@samsung.com>");
MODULE_LICENSE("GPL");
