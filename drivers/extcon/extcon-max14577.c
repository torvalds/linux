/*
 * extcon-max14577.c - MAX14577/77836 extcon driver to support MUIC
 *
 * Copyright (C) 2013,2014 Samsung Electrnoics
 * Chanwoo Choi <cw00.choi@samsung.com>
 * Krzysztof Kozlowski <k.kozlowski@samsung.com>
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
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/mfd/max14577.h>
#include <linux/mfd/max14577-private.h>
#include <linux/extcon.h>

#define	DELAY_MS_DEFAULT		17000		/* unit: millisecond */

enum max14577_muic_adc_debounce_time {
	ADC_DEBOUNCE_TIME_5MS = 0,
	ADC_DEBOUNCE_TIME_10MS,
	ADC_DEBOUNCE_TIME_25MS,
	ADC_DEBOUNCE_TIME_38_62MS,
};

enum max14577_muic_status {
	MAX14577_MUIC_STATUS1 = 0,
	MAX14577_MUIC_STATUS2 = 1,
	MAX14577_MUIC_STATUS_END,
};

/**
 * struct max14577_muic_irq
 * @irq: the index of irq list of MUIC device.
 * @name: the name of irq.
 * @virq: the virtual irq to use irq domain
 */
struct max14577_muic_irq {
	unsigned int irq;
	const char *name;
	unsigned int virq;
};

static struct max14577_muic_irq max14577_muic_irqs[] = {
	{ MAX14577_IRQ_INT1_ADC,	"muic-ADC" },
	{ MAX14577_IRQ_INT1_ADCLOW,	"muic-ADCLOW" },
	{ MAX14577_IRQ_INT1_ADCERR,	"muic-ADCError" },
	{ MAX14577_IRQ_INT2_CHGTYP,	"muic-CHGTYP" },
	{ MAX14577_IRQ_INT2_CHGDETRUN,	"muic-CHGDETRUN" },
	{ MAX14577_IRQ_INT2_DCDTMR,	"muic-DCDTMR" },
	{ MAX14577_IRQ_INT2_DBCHG,	"muic-DBCHG" },
	{ MAX14577_IRQ_INT2_VBVOLT,	"muic-VBVOLT" },
};

static struct max14577_muic_irq max77836_muic_irqs[] = {
	{ MAX14577_IRQ_INT1_ADC,	"muic-ADC" },
	{ MAX14577_IRQ_INT1_ADCLOW,	"muic-ADCLOW" },
	{ MAX14577_IRQ_INT1_ADCERR,	"muic-ADCError" },
	{ MAX77836_IRQ_INT1_ADC1K,	"muic-ADC1K" },
	{ MAX14577_IRQ_INT2_CHGTYP,	"muic-CHGTYP" },
	{ MAX14577_IRQ_INT2_CHGDETRUN,	"muic-CHGDETRUN" },
	{ MAX14577_IRQ_INT2_DCDTMR,	"muic-DCDTMR" },
	{ MAX14577_IRQ_INT2_DBCHG,	"muic-DBCHG" },
	{ MAX14577_IRQ_INT2_VBVOLT,	"muic-VBVOLT" },
	{ MAX77836_IRQ_INT2_VIDRM,	"muic-VIDRM" },
};

struct max14577_muic_info {
	struct device *dev;
	struct max14577 *max14577;
	struct extcon_dev *edev;
	int prev_cable_type;
	int prev_chg_type;
	u8 status[MAX14577_MUIC_STATUS_END];

	struct max14577_muic_irq *muic_irqs;
	unsigned int muic_irqs_num;
	bool irq_adc;
	bool irq_chg;
	struct work_struct irq_work;
	struct mutex mutex;

	/*
	 * Use delayed workqueue to detect cable state and then
	 * notify cable state to notifiee/platform through uevent.
	 * After completing the booting of platform, the extcon provider
	 * driver should notify cable state to upper layer.
	 */
	struct delayed_work wq_detcable;

	/*
	 * Default usb/uart path whether UART/USB or AUX_UART/AUX_USB
	 * h/w path of COMP2/COMN1 on CONTROL1 register.
	 */
	int path_usb;
	int path_uart;
};

enum max14577_muic_cable_group {
	MAX14577_CABLE_GROUP_ADC = 0,
	MAX14577_CABLE_GROUP_CHG,
};

/* Define supported accessory type */
enum max14577_muic_acc_type {
	MAX14577_MUIC_ADC_GROUND = 0x0,
	MAX14577_MUIC_ADC_SEND_END_BUTTON,
	MAX14577_MUIC_ADC_REMOTE_S1_BUTTON,
	MAX14577_MUIC_ADC_REMOTE_S2_BUTTON,
	MAX14577_MUIC_ADC_REMOTE_S3_BUTTON,
	MAX14577_MUIC_ADC_REMOTE_S4_BUTTON,
	MAX14577_MUIC_ADC_REMOTE_S5_BUTTON,
	MAX14577_MUIC_ADC_REMOTE_S6_BUTTON,
	MAX14577_MUIC_ADC_REMOTE_S7_BUTTON,
	MAX14577_MUIC_ADC_REMOTE_S8_BUTTON,
	MAX14577_MUIC_ADC_REMOTE_S9_BUTTON,
	MAX14577_MUIC_ADC_REMOTE_S10_BUTTON,
	MAX14577_MUIC_ADC_REMOTE_S11_BUTTON,
	MAX14577_MUIC_ADC_REMOTE_S12_BUTTON,
	MAX14577_MUIC_ADC_RESERVED_ACC_1,
	MAX14577_MUIC_ADC_RESERVED_ACC_2,
	MAX14577_MUIC_ADC_RESERVED_ACC_3,
	MAX14577_MUIC_ADC_RESERVED_ACC_4,
	MAX14577_MUIC_ADC_RESERVED_ACC_5,
	MAX14577_MUIC_ADC_AUDIO_DEVICE_TYPE2,
	MAX14577_MUIC_ADC_PHONE_POWERED_DEV,
	MAX14577_MUIC_ADC_TTY_CONVERTER,
	MAX14577_MUIC_ADC_UART_CABLE,
	MAX14577_MUIC_ADC_CEA936A_TYPE1_CHG,
	MAX14577_MUIC_ADC_FACTORY_MODE_USB_OFF,
	MAX14577_MUIC_ADC_FACTORY_MODE_USB_ON,
	MAX14577_MUIC_ADC_AV_CABLE_NOLOAD,
	MAX14577_MUIC_ADC_CEA936A_TYPE2_CHG,
	MAX14577_MUIC_ADC_FACTORY_MODE_UART_OFF,
	MAX14577_MUIC_ADC_FACTORY_MODE_UART_ON,
	MAX14577_MUIC_ADC_AUDIO_DEVICE_TYPE1, /* with Remote and Simple Ctrl */
	MAX14577_MUIC_ADC_OPEN,
};

/* max14577 MUIC device support below list of accessories(external connector) */
enum {
	EXTCON_CABLE_USB = 0,
	EXTCON_CABLE_TA,
	EXTCON_CABLE_FAST_CHARGER,
	EXTCON_CABLE_SLOW_CHARGER,
	EXTCON_CABLE_CHARGE_DOWNSTREAM,
	EXTCON_CABLE_JIG_USB_ON,
	EXTCON_CABLE_JIG_USB_OFF,
	EXTCON_CABLE_JIG_UART_OFF,
	EXTCON_CABLE_JIG_UART_ON,

	_EXTCON_CABLE_NUM,
};

static const char *max14577_extcon_cable[] = {
	[EXTCON_CABLE_USB]			= "USB",
	[EXTCON_CABLE_TA]			= "TA",
	[EXTCON_CABLE_FAST_CHARGER]		= "Fast-charger",
	[EXTCON_CABLE_SLOW_CHARGER]		= "Slow-charger",
	[EXTCON_CABLE_CHARGE_DOWNSTREAM]	= "Charge-downstream",
	[EXTCON_CABLE_JIG_USB_ON]		= "JIG-USB-ON",
	[EXTCON_CABLE_JIG_USB_OFF]		= "JIG-USB-OFF",
	[EXTCON_CABLE_JIG_UART_OFF]		= "JIG-UART-OFF",
	[EXTCON_CABLE_JIG_UART_ON]		= "JIG-UART-ON",

	NULL,
};

/*
 * max14577_muic_set_debounce_time - Set the debounce time of ADC
 * @info: the instance including private data of max14577 MUIC
 * @time: the debounce time of ADC
 */
static int max14577_muic_set_debounce_time(struct max14577_muic_info *info,
		enum max14577_muic_adc_debounce_time time)
{
	u8 ret;

	switch (time) {
	case ADC_DEBOUNCE_TIME_5MS:
	case ADC_DEBOUNCE_TIME_10MS:
	case ADC_DEBOUNCE_TIME_25MS:
	case ADC_DEBOUNCE_TIME_38_62MS:
		ret = max14577_update_reg(info->max14577->regmap,
					  MAX14577_MUIC_REG_CONTROL3,
					  CTRL3_ADCDBSET_MASK,
					  time << CTRL3_ADCDBSET_SHIFT);
		if (ret) {
			dev_err(info->dev, "failed to set ADC debounce time\n");
			return ret;
		}
		break;
	default:
		dev_err(info->dev, "invalid ADC debounce time\n");
		return -EINVAL;
	}

	return 0;
};

/*
 * max14577_muic_set_path - Set hardware line according to attached cable
 * @info: the instance including private data of max14577 MUIC
 * @value: the path according to attached cable
 * @attached: the state of cable (true:attached, false:detached)
 *
 * The max14577 MUIC device share outside H/W line among a varity of cables
 * so, this function set internal path of H/W line according to the type of
 * attached cable.
 */
static int max14577_muic_set_path(struct max14577_muic_info *info,
		u8 val, bool attached)
{
	int ret = 0;
	u8 ctrl1, ctrl2 = 0;

	/* Set open state to path before changing hw path */
	ret = max14577_update_reg(info->max14577->regmap,
				MAX14577_MUIC_REG_CONTROL1,
				CLEAR_IDBEN_MICEN_MASK, CTRL1_SW_OPEN);
	if (ret < 0) {
		dev_err(info->dev, "failed to update MUIC register\n");
		return ret;
	}

	if (attached)
		ctrl1 = val;
	else
		ctrl1 = CTRL1_SW_OPEN;

	ret = max14577_update_reg(info->max14577->regmap,
				MAX14577_MUIC_REG_CONTROL1,
				CLEAR_IDBEN_MICEN_MASK, ctrl1);
	if (ret < 0) {
		dev_err(info->dev, "failed to update MUIC register\n");
		return ret;
	}

	if (attached)
		ctrl2 |= CTRL2_CPEN_MASK;	/* LowPwr=0, CPEn=1 */
	else
		ctrl2 |= CTRL2_LOWPWR_MASK;	/* LowPwr=1, CPEn=0 */

	ret = max14577_update_reg(info->max14577->regmap,
			MAX14577_REG_CONTROL2,
			CTRL2_LOWPWR_MASK | CTRL2_CPEN_MASK, ctrl2);
	if (ret < 0) {
		dev_err(info->dev, "failed to update MUIC register\n");
		return ret;
	}

	dev_dbg(info->dev,
		"CONTROL1 : 0x%02x, CONTROL2 : 0x%02x, state : %s\n",
		ctrl1, ctrl2, attached ? "attached" : "detached");

	return 0;
}

/*
 * max14577_muic_get_cable_type - Return cable type and check cable state
 * @info: the instance including private data of max14577 MUIC
 * @group: the path according to attached cable
 * @attached: store cable state and return
 *
 * This function check the cable state either attached or detached,
 * and then divide precise type of cable according to cable group.
 *	- max14577_CABLE_GROUP_ADC
 *	- max14577_CABLE_GROUP_CHG
 */
static int max14577_muic_get_cable_type(struct max14577_muic_info *info,
		enum max14577_muic_cable_group group, bool *attached)
{
	int cable_type = 0;
	int adc;
	int chg_type;

	switch (group) {
	case MAX14577_CABLE_GROUP_ADC:
		/*
		 * Read ADC value to check cable type and decide cable state
		 * according to cable type
		 */
		adc = info->status[MAX14577_MUIC_STATUS1] & STATUS1_ADC_MASK;
		adc >>= STATUS1_ADC_SHIFT;

		/*
		 * Check current cable state/cable type and store cable type
		 * (info->prev_cable_type) for handling cable when cable is
		 * detached.
		 */
		if (adc == MAX14577_MUIC_ADC_OPEN) {
			*attached = false;

			cable_type = info->prev_cable_type;
			info->prev_cable_type = MAX14577_MUIC_ADC_OPEN;
		} else {
			*attached = true;

			cable_type = info->prev_cable_type = adc;
		}
		break;
	case MAX14577_CABLE_GROUP_CHG:
		/*
		 * Read charger type to check cable type and decide cable state
		 * according to type of charger cable.
		 */
		chg_type = info->status[MAX14577_MUIC_STATUS2] &
			STATUS2_CHGTYP_MASK;
		chg_type >>= STATUS2_CHGTYP_SHIFT;

		if (chg_type == MAX14577_CHARGER_TYPE_NONE) {
			*attached = false;

			cable_type = info->prev_chg_type;
			info->prev_chg_type = MAX14577_CHARGER_TYPE_NONE;
		} else {
			*attached = true;

			/*
			 * Check current cable state/cable type and store cable
			 * type(info->prev_chg_type) for handling cable when
			 * charger cable is detached.
			 */
			cable_type = info->prev_chg_type = chg_type;
		}

		break;
	default:
		dev_err(info->dev, "Unknown cable group (%d)\n", group);
		cable_type = -EINVAL;
		break;
	}

	return cable_type;
}

static int max14577_muic_jig_handler(struct max14577_muic_info *info,
		int cable_type, bool attached)
{
	char cable_name[32];
	int ret = 0;
	u8 path = CTRL1_SW_OPEN;

	dev_dbg(info->dev,
		"external connector is %s (adc:0x%02x)\n",
		attached ? "attached" : "detached", cable_type);

	switch (cable_type) {
	case MAX14577_MUIC_ADC_FACTORY_MODE_USB_OFF:	/* ADC_JIG_USB_OFF */
		/* PATH:AP_USB */
		strcpy(cable_name, "JIG-USB-OFF");
		path = CTRL1_SW_USB;
		break;
	case MAX14577_MUIC_ADC_FACTORY_MODE_USB_ON:	/* ADC_JIG_USB_ON */
		/* PATH:AP_USB */
		strcpy(cable_name, "JIG-USB-ON");
		path = CTRL1_SW_USB;
		break;
	case MAX14577_MUIC_ADC_FACTORY_MODE_UART_OFF:	/* ADC_JIG_UART_OFF */
		/* PATH:AP_UART */
		strcpy(cable_name, "JIG-UART-OFF");
		path = CTRL1_SW_UART;
		break;
	default:
		dev_err(info->dev, "failed to detect %s jig cable\n",
			attached ? "attached" : "detached");
		return -EINVAL;
	}

	ret = max14577_muic_set_path(info, path, attached);
	if (ret < 0)
		return ret;

	extcon_set_cable_state(info->edev, cable_name, attached);

	return 0;
}

static int max14577_muic_adc_handler(struct max14577_muic_info *info)
{
	int cable_type;
	bool attached;
	int ret = 0;

	/* Check accessory state which is either detached or attached */
	cable_type = max14577_muic_get_cable_type(info,
				MAX14577_CABLE_GROUP_ADC, &attached);

	dev_dbg(info->dev,
		"external connector is %s (adc:0x%02x, prev_adc:0x%x)\n",
		attached ? "attached" : "detached", cable_type,
		info->prev_cable_type);

	switch (cable_type) {
	case MAX14577_MUIC_ADC_FACTORY_MODE_USB_OFF:
	case MAX14577_MUIC_ADC_FACTORY_MODE_USB_ON:
	case MAX14577_MUIC_ADC_FACTORY_MODE_UART_OFF:
		/* JIG */
		ret = max14577_muic_jig_handler(info, cable_type, attached);
		if (ret < 0)
			return ret;
		break;
	case MAX14577_MUIC_ADC_GROUND:
	case MAX14577_MUIC_ADC_SEND_END_BUTTON:
	case MAX14577_MUIC_ADC_REMOTE_S1_BUTTON:
	case MAX14577_MUIC_ADC_REMOTE_S2_BUTTON:
	case MAX14577_MUIC_ADC_REMOTE_S3_BUTTON:
	case MAX14577_MUIC_ADC_REMOTE_S4_BUTTON:
	case MAX14577_MUIC_ADC_REMOTE_S5_BUTTON:
	case MAX14577_MUIC_ADC_REMOTE_S6_BUTTON:
	case MAX14577_MUIC_ADC_REMOTE_S7_BUTTON:
	case MAX14577_MUIC_ADC_REMOTE_S8_BUTTON:
	case MAX14577_MUIC_ADC_REMOTE_S9_BUTTON:
	case MAX14577_MUIC_ADC_REMOTE_S10_BUTTON:
	case MAX14577_MUIC_ADC_REMOTE_S11_BUTTON:
	case MAX14577_MUIC_ADC_REMOTE_S12_BUTTON:
	case MAX14577_MUIC_ADC_RESERVED_ACC_1:
	case MAX14577_MUIC_ADC_RESERVED_ACC_2:
	case MAX14577_MUIC_ADC_RESERVED_ACC_3:
	case MAX14577_MUIC_ADC_RESERVED_ACC_4:
	case MAX14577_MUIC_ADC_RESERVED_ACC_5:
	case MAX14577_MUIC_ADC_AUDIO_DEVICE_TYPE2:
	case MAX14577_MUIC_ADC_PHONE_POWERED_DEV:
	case MAX14577_MUIC_ADC_TTY_CONVERTER:
	case MAX14577_MUIC_ADC_UART_CABLE:
	case MAX14577_MUIC_ADC_CEA936A_TYPE1_CHG:
	case MAX14577_MUIC_ADC_AV_CABLE_NOLOAD:
	case MAX14577_MUIC_ADC_CEA936A_TYPE2_CHG:
	case MAX14577_MUIC_ADC_FACTORY_MODE_UART_ON:
	case MAX14577_MUIC_ADC_AUDIO_DEVICE_TYPE1:
		/*
		 * This accessory isn't used in general case if it is specially
		 * needed to detect additional accessory, should implement
		 * proper operation when this accessory is attached/detached.
		 */
		dev_info(info->dev,
			"accessory is %s but it isn't used (adc:0x%x)\n",
			attached ? "attached" : "detached", cable_type);
		return -EAGAIN;
	default:
		dev_err(info->dev,
			"failed to detect %s accessory (adc:0x%x)\n",
			attached ? "attached" : "detached", cable_type);
		return -EINVAL;
	}

	return 0;
}

static int max14577_muic_chg_handler(struct max14577_muic_info *info)
{
	int chg_type;
	bool attached;
	int ret = 0;

	chg_type = max14577_muic_get_cable_type(info,
				MAX14577_CABLE_GROUP_CHG, &attached);

	dev_dbg(info->dev,
		"external connector is %s(chg_type:0x%x, prev_chg_type:0x%x)\n",
			attached ? "attached" : "detached",
			chg_type, info->prev_chg_type);

	switch (chg_type) {
	case MAX14577_CHARGER_TYPE_USB:
		/* PATH:AP_USB */
		ret = max14577_muic_set_path(info, info->path_usb, attached);
		if (ret < 0)
			return ret;

		extcon_set_cable_state(info->edev, "USB", attached);
		break;
	case MAX14577_CHARGER_TYPE_DEDICATED_CHG:
		extcon_set_cable_state(info->edev, "TA", attached);
		break;
	case MAX14577_CHARGER_TYPE_DOWNSTREAM_PORT:
		extcon_set_cable_state(info->edev,
				"Charge-downstream", attached);
		break;
	case MAX14577_CHARGER_TYPE_SPECIAL_500MA:
		extcon_set_cable_state(info->edev, "Slow-charger", attached);
		break;
	case MAX14577_CHARGER_TYPE_SPECIAL_1A:
		extcon_set_cable_state(info->edev, "Fast-charger", attached);
		break;
	case MAX14577_CHARGER_TYPE_NONE:
	case MAX14577_CHARGER_TYPE_DEAD_BATTERY:
		break;
	default:
		dev_err(info->dev,
			"failed to detect %s accessory (chg_type:0x%x)\n",
			attached ? "attached" : "detached", chg_type);
		return -EINVAL;
	}

	return 0;
}

static void max14577_muic_irq_work(struct work_struct *work)
{
	struct max14577_muic_info *info = container_of(work,
			struct max14577_muic_info, irq_work);
	int ret = 0;

	if (!info->edev)
		return;

	mutex_lock(&info->mutex);

	ret = max14577_bulk_read(info->max14577->regmap,
			MAX14577_MUIC_REG_STATUS1, info->status, 2);
	if (ret) {
		dev_err(info->dev, "failed to read MUIC register\n");
		mutex_unlock(&info->mutex);
		return;
	}

	if (info->irq_adc) {
		ret = max14577_muic_adc_handler(info);
		info->irq_adc = false;
	}
	if (info->irq_chg) {
		ret = max14577_muic_chg_handler(info);
		info->irq_chg = false;
	}

	if (ret < 0)
		dev_err(info->dev, "failed to handle MUIC interrupt\n");

	mutex_unlock(&info->mutex);

	return;
}

/*
 * Sets irq_adc or irq_chg in max14577_muic_info and returns 1.
 * Returns 0 if irq_type does not match registered IRQ for this device type.
 */
static int max14577_parse_irq(struct max14577_muic_info *info, int irq_type)
{
	switch (irq_type) {
	case MAX14577_IRQ_INT1_ADC:
	case MAX14577_IRQ_INT1_ADCLOW:
	case MAX14577_IRQ_INT1_ADCERR:
		/* Handle all of accessory except for
		   type of charger accessory */
		info->irq_adc = true;
		return 1;
	case MAX14577_IRQ_INT2_CHGTYP:
	case MAX14577_IRQ_INT2_CHGDETRUN:
	case MAX14577_IRQ_INT2_DCDTMR:
	case MAX14577_IRQ_INT2_DBCHG:
	case MAX14577_IRQ_INT2_VBVOLT:
		/* Handle charger accessory */
		info->irq_chg = true;
		return 1;
	default:
		return 0;
	}
}

/*
 * Sets irq_adc or irq_chg in max14577_muic_info and returns 1.
 * Returns 0 if irq_type does not match registered IRQ for this device type.
 */
static int max77836_parse_irq(struct max14577_muic_info *info, int irq_type)
{
	/* First check common max14577 interrupts */
	if (max14577_parse_irq(info, irq_type))
		return 1;

	switch (irq_type) {
	case MAX77836_IRQ_INT1_ADC1K:
		info->irq_adc = true;
		return 1;
	case MAX77836_IRQ_INT2_VIDRM:
		/* Handle charger accessory */
		info->irq_chg = true;
		return 1;
	default:
		return 0;
	}
}

static irqreturn_t max14577_muic_irq_handler(int irq, void *data)
{
	struct max14577_muic_info *info = data;
	int i, irq_type = -1;
	bool irq_parsed;

	/*
	 * We may be called multiple times for different nested IRQ-s.
	 * Including changes in INT1_ADC and INT2_CGHTYP at once.
	 * However we only need to know whether it was ADC, charger
	 * or both interrupts so decode IRQ and turn on proper flags.
	 */
	for (i = 0; i < info->muic_irqs_num; i++)
		if (irq == info->muic_irqs[i].virq)
			irq_type = info->muic_irqs[i].irq;

	switch (info->max14577->dev_type) {
	case MAXIM_DEVICE_TYPE_MAX77836:
		irq_parsed = max77836_parse_irq(info, irq_type);
		break;
	case MAXIM_DEVICE_TYPE_MAX14577:
	default:
		irq_parsed = max14577_parse_irq(info, irq_type);
		break;
	}

	if (!irq_parsed) {
		dev_err(info->dev, "muic interrupt: irq %d occurred, skipped\n",
				irq_type);
		return IRQ_HANDLED;
	}
	schedule_work(&info->irq_work);

	return IRQ_HANDLED;
}

static int max14577_muic_detect_accessory(struct max14577_muic_info *info)
{
	int ret = 0;
	int adc;
	int chg_type;
	bool attached;

	mutex_lock(&info->mutex);

	/* Read STATUSx register to detect accessory */
	ret = max14577_bulk_read(info->max14577->regmap,
			MAX14577_MUIC_REG_STATUS1, info->status, 2);
	if (ret) {
		dev_err(info->dev, "failed to read MUIC register\n");
		mutex_unlock(&info->mutex);
		return ret;
	}

	adc = max14577_muic_get_cable_type(info, MAX14577_CABLE_GROUP_ADC,
					&attached);
	if (attached && adc != MAX14577_MUIC_ADC_OPEN) {
		ret = max14577_muic_adc_handler(info);
		if (ret < 0) {
			dev_err(info->dev, "Cannot detect accessory\n");
			mutex_unlock(&info->mutex);
			return ret;
		}
	}

	chg_type = max14577_muic_get_cable_type(info, MAX14577_CABLE_GROUP_CHG,
					&attached);
	if (attached && chg_type != MAX14577_CHARGER_TYPE_NONE) {
		ret = max14577_muic_chg_handler(info);
		if (ret < 0) {
			dev_err(info->dev, "Cannot detect charger accessory\n");
			mutex_unlock(&info->mutex);
			return ret;
		}
	}

	mutex_unlock(&info->mutex);

	return 0;
}

static void max14577_muic_detect_cable_wq(struct work_struct *work)
{
	struct max14577_muic_info *info = container_of(to_delayed_work(work),
				struct max14577_muic_info, wq_detcable);

	max14577_muic_detect_accessory(info);
}

static int max14577_muic_probe(struct platform_device *pdev)
{
	struct max14577 *max14577 = dev_get_drvdata(pdev->dev.parent);
	struct max14577_muic_info *info;
	int delay_jiffies;
	int ret;
	int i;
	u8 id;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	info->max14577 = max14577;

	platform_set_drvdata(pdev, info);
	mutex_init(&info->mutex);

	INIT_WORK(&info->irq_work, max14577_muic_irq_work);

	switch (max14577->dev_type) {
	case MAXIM_DEVICE_TYPE_MAX77836:
		info->muic_irqs = max77836_muic_irqs;
		info->muic_irqs_num = ARRAY_SIZE(max77836_muic_irqs);
		break;
	case MAXIM_DEVICE_TYPE_MAX14577:
	default:
		info->muic_irqs = max14577_muic_irqs;
		info->muic_irqs_num = ARRAY_SIZE(max14577_muic_irqs);
	}

	/* Support irq domain for max14577 MUIC device */
	for (i = 0; i < info->muic_irqs_num; i++) {
		struct max14577_muic_irq *muic_irq = &info->muic_irqs[i];
		unsigned int virq = 0;

		virq = regmap_irq_get_virq(max14577->irq_data, muic_irq->irq);
		if (virq <= 0)
			return -EINVAL;
		muic_irq->virq = virq;

		ret = devm_request_threaded_irq(&pdev->dev, virq, NULL,
				max14577_muic_irq_handler,
				IRQF_NO_SUSPEND,
				muic_irq->name, info);
		if (ret) {
			dev_err(&pdev->dev,
				"failed: irq request (IRQ: %d,"
				" error :%d)\n",
				muic_irq->irq, ret);
			return ret;
		}
	}

	/* Initialize extcon device */
	info->edev = devm_extcon_dev_allocate(&pdev->dev,
					      max14577_extcon_cable);
	if (IS_ERR(info->edev)) {
		dev_err(&pdev->dev, "failed to allocate memory for extcon\n");
		return -ENOMEM;
	}

	info->edev->name = dev_name(&pdev->dev);

	ret = devm_extcon_dev_register(&pdev->dev, info->edev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register extcon device\n");
		return ret;
	}

	/* Default h/w line path */
	info->path_usb = CTRL1_SW_USB;
	info->path_uart = CTRL1_SW_UART;
	delay_jiffies = msecs_to_jiffies(DELAY_MS_DEFAULT);

	/* Set initial path for UART */
	max14577_muic_set_path(info, info->path_uart, true);

	/* Check revision number of MUIC device*/
	ret = max14577_read_reg(info->max14577->regmap,
			MAX14577_REG_DEVICEID, &id);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to read revision number\n");
		return ret;
	}
	dev_info(info->dev, "device ID : 0x%x\n", id);

	/* Set ADC debounce time */
	max14577_muic_set_debounce_time(info, ADC_DEBOUNCE_TIME_25MS);

	/*
	 * Detect accessory after completing the initialization of platform
	 *
	 * - Use delayed workqueue to detect cable state and then
	 * notify cable state to notifiee/platform through uevent.
	 * After completing the booting of platform, the extcon provider
	 * driver should notify cable state to upper layer.
	 */
	INIT_DELAYED_WORK(&info->wq_detcable, max14577_muic_detect_cable_wq);
	queue_delayed_work(system_power_efficient_wq, &info->wq_detcable,
			delay_jiffies);

	return ret;
}

static int max14577_muic_remove(struct platform_device *pdev)
{
	struct max14577_muic_info *info = platform_get_drvdata(pdev);

	cancel_work_sync(&info->irq_work);

	return 0;
}

static const struct platform_device_id max14577_muic_id[] = {
	{ "max14577-muic", MAXIM_DEVICE_TYPE_MAX14577, },
	{ "max77836-muic", MAXIM_DEVICE_TYPE_MAX77836, },
	{ }
};
MODULE_DEVICE_TABLE(platform, max14577_muic_id);

static struct platform_driver max14577_muic_driver = {
	.driver		= {
		.name	= "max14577-muic",
	},
	.probe		= max14577_muic_probe,
	.remove		= max14577_muic_remove,
	.id_table	= max14577_muic_id,
};

module_platform_driver(max14577_muic_driver);

MODULE_DESCRIPTION("Maxim 14577/77836 Extcon driver");
MODULE_AUTHOR("Chanwoo Choi <cw00.choi@samsung.com>, Krzysztof Kozlowski <k.kozlowski@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:extcon-max14577");
