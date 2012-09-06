/*
 * extcon-max77693.c - MAX77693 extcon driver to support MAX77693 MUIC
 *
 * Copyright (C) 2012 Samsung Electrnoics
 * Chanwoo Choi <cw00.choi@samsung.com>
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
#include <linux/mfd/max77693.h>
#include <linux/mfd/max77693-private.h>
#include <linux/extcon.h>
#include <linux/regmap.h>
#include <linux/irqdomain.h>

#define	DEV_NAME			"max77693-muic"

/* MAX77693 MUIC - STATUS1~3 Register */
#define STATUS1_ADC_SHIFT		(0)
#define STATUS1_ADCLOW_SHIFT		(5)
#define STATUS1_ADCERR_SHIFT		(6)
#define STATUS1_ADC1K_SHIFT		(7)
#define STATUS1_ADC_MASK		(0x1f << STATUS1_ADC_SHIFT)
#define STATUS1_ADCLOW_MASK		(0x1 << STATUS1_ADCLOW_SHIFT)
#define STATUS1_ADCERR_MASK		(0x1 << STATUS1_ADCERR_SHIFT)
#define STATUS1_ADC1K_MASK		(0x1 << STATUS1_ADC1K_SHIFT)

#define STATUS2_CHGTYP_SHIFT		(0)
#define STATUS2_CHGDETRUN_SHIFT		(3)
#define STATUS2_DCDTMR_SHIFT		(4)
#define STATUS2_DXOVP_SHIFT		(5)
#define STATUS2_VBVOLT_SHIFT		(6)
#define STATUS2_VIDRM_SHIFT		(7)
#define STATUS2_CHGTYP_MASK		(0x7 << STATUS2_CHGTYP_SHIFT)
#define STATUS2_CHGDETRUN_MASK		(0x1 << STATUS2_CHGDETRUN_SHIFT)
#define STATUS2_DCDTMR_MASK		(0x1 << STATUS2_DCDTMR_SHIFT)
#define STATUS2_DXOVP_MASK		(0x1 << STATUS2_DXOVP_SHIFT)
#define STATUS2_VBVOLT_MASK		(0x1 << STATUS2_VBVOLT_SHIFT)
#define STATUS2_VIDRM_MASK		(0x1 << STATUS2_VIDRM_SHIFT)

#define STATUS3_OVP_SHIFT		(2)
#define STATUS3_OVP_MASK		(0x1 << STATUS3_OVP_SHIFT)

/* MAX77693 CDETCTRL1~2 register */
#define CDETCTRL1_CHGDETEN_SHIFT	(0)
#define CDETCTRL1_CHGTYPMAN_SHIFT	(1)
#define CDETCTRL1_DCDEN_SHIFT		(2)
#define CDETCTRL1_DCD2SCT_SHIFT		(3)
#define CDETCTRL1_CDDELAY_SHIFT		(4)
#define CDETCTRL1_DCDCPL_SHIFT		(5)
#define CDETCTRL1_CDPDET_SHIFT		(7)
#define CDETCTRL1_CHGDETEN_MASK		(0x1 << CDETCTRL1_CHGDETEN_SHIFT)
#define CDETCTRL1_CHGTYPMAN_MASK	(0x1 << CDETCTRL1_CHGTYPMAN_SHIFT)
#define CDETCTRL1_DCDEN_MASK		(0x1 << CDETCTRL1_DCDEN_SHIFT)
#define CDETCTRL1_DCD2SCT_MASK		(0x1 << CDETCTRL1_DCD2SCT_SHIFT)
#define CDETCTRL1_CDDELAY_MASK		(0x1 << CDETCTRL1_CDDELAY_SHIFT)
#define CDETCTRL1_DCDCPL_MASK		(0x1 << CDETCTRL1_DCDCPL_SHIFT)
#define CDETCTRL1_CDPDET_MASK		(0x1 << CDETCTRL1_CDPDET_SHIFT)

#define CDETCTRL2_VIDRMEN_SHIFT		(1)
#define CDETCTRL2_DXOVPEN_SHIFT		(3)
#define CDETCTRL2_VIDRMEN_MASK		(0x1 << CDETCTRL2_VIDRMEN_SHIFT)
#define CDETCTRL2_DXOVPEN_MASK		(0x1 << CDETCTRL2_DXOVPEN_SHIFT)

/* MAX77693 MUIC - CONTROL1~3 register */
#define COMN1SW_SHIFT			(0)
#define COMP2SW_SHIFT			(3)
#define COMN1SW_MASK			(0x7 << COMN1SW_SHIFT)
#define COMP2SW_MASK			(0x7 << COMP2SW_SHIFT)
#define COMP_SW_MASK			(COMP2SW_MASK | COMN1SW_MASK)
#define CONTROL1_SW_USB			((1 << COMP2SW_SHIFT) \
						| (1 << COMN1SW_SHIFT))
#define CONTROL1_SW_AUDIO		((2 << COMP2SW_SHIFT) \
						| (2 << COMN1SW_SHIFT))
#define CONTROL1_SW_UART		((3 << COMP2SW_SHIFT) \
						| (3 << COMN1SW_SHIFT))
#define CONTROL1_SW_OPEN		((0 << COMP2SW_SHIFT) \
						| (0 << COMN1SW_SHIFT))

#define CONTROL2_LOWPWR_SHIFT		(0)
#define CONTROL2_ADCEN_SHIFT		(1)
#define CONTROL2_CPEN_SHIFT		(2)
#define CONTROL2_SFOUTASRT_SHIFT	(3)
#define CONTROL2_SFOUTORD_SHIFT		(4)
#define CONTROL2_ACCDET_SHIFT		(5)
#define CONTROL2_USBCPINT_SHIFT		(6)
#define CONTROL2_RCPS_SHIFT		(7)
#define CONTROL2_LOWPWR_MASK		(0x1 << CONTROL2_LOWPWR_SHIFT)
#define CONTROL2_ADCEN_MASK		(0x1 << CONTROL2_ADCEN_SHIFT)
#define CONTROL2_CPEN_MASK		(0x1 << CONTROL2_CPEN_SHIFT)
#define CONTROL2_SFOUTASRT_MASK		(0x1 << CONTROL2_SFOUTASRT_SHIFT)
#define CONTROL2_SFOUTORD_MASK		(0x1 << CONTROL2_SFOUTORD_SHIFT)
#define CONTROL2_ACCDET_MASK		(0x1 << CONTROL2_ACCDET_SHIFT)
#define CONTROL2_USBCPINT_MASK		(0x1 << CONTROL2_USBCPINT_SHIFT)
#define CONTROL2_RCPS_MASK		(0x1 << CONTROL2_RCPS_SHIFT)

#define CONTROL3_JIGSET_SHIFT		(0)
#define CONTROL3_BTLDSET_SHIFT		(2)
#define CONTROL3_ADCDBSET_SHIFT		(4)
#define CONTROL3_JIGSET_MASK		(0x3 << CONTROL3_JIGSET_SHIFT)
#define CONTROL3_BTLDSET_MASK		(0x3 << CONTROL3_BTLDSET_SHIFT)
#define CONTROL3_ADCDBSET_MASK		(0x3 << CONTROL3_ADCDBSET_SHIFT)

enum max77693_muic_adc_debounce_time {
	ADC_DEBOUNCE_TIME_5MS = 0,
	ADC_DEBOUNCE_TIME_10MS,
	ADC_DEBOUNCE_TIME_25MS,
	ADC_DEBOUNCE_TIME_38_62MS,
};

struct max77693_muic_info {
	struct device *dev;
	struct max77693_dev *max77693;
	struct extcon_dev *edev;
	int prev_adc;
	int prev_adc_gnd;
	int prev_chg_type;
	u8 status[2];

	int irq;
	struct work_struct irq_work;
	struct mutex mutex;
};

enum max77693_muic_charger_type {
	MAX77693_CHARGER_TYPE_NONE = 0,
	MAX77693_CHARGER_TYPE_USB,
	MAX77693_CHARGER_TYPE_DOWNSTREAM_PORT,
	MAX77693_CHARGER_TYPE_DEDICATED_CHG,
	MAX77693_CHARGER_TYPE_APPLE_500MA,
	MAX77693_CHARGER_TYPE_APPLE_1A_2A,
	MAX77693_CHARGER_TYPE_DEAD_BATTERY = 7,
};

/**
 * struct max77693_muic_irq
 * @irq: the index of irq list of MUIC device.
 * @name: the name of irq.
 * @virq: the virtual irq to use irq domain
 */
struct max77693_muic_irq {
	unsigned int irq;
	const char *name;
	unsigned int virq;
};

static struct max77693_muic_irq muic_irqs[] = {
	{ MAX77693_MUIC_IRQ_INT1_ADC,		"muic-ADC" },
	{ MAX77693_MUIC_IRQ_INT1_ADC_LOW,	"muic-ADCLOW" },
	{ MAX77693_MUIC_IRQ_INT1_ADC_ERR,	"muic-ADCError" },
	{ MAX77693_MUIC_IRQ_INT1_ADC1K,		"muic-ADC1K" },
	{ MAX77693_MUIC_IRQ_INT2_CHGTYP,	"muic-CHGTYP" },
	{ MAX77693_MUIC_IRQ_INT2_CHGDETREUN,	"muic-CHGDETREUN" },
	{ MAX77693_MUIC_IRQ_INT2_DCDTMR,	"muic-DCDTMR" },
	{ MAX77693_MUIC_IRQ_INT2_DXOVP,		"muic-DXOVP" },
	{ MAX77693_MUIC_IRQ_INT2_VBVOLT,	"muic-VBVOLT" },
	{ MAX77693_MUIC_IRQ_INT2_VIDRM,		"muic-VIDRM" },
	{ MAX77693_MUIC_IRQ_INT3_EOC,		"muic-EOC" },
	{ MAX77693_MUIC_IRQ_INT3_CGMBC,		"muic-CGMBC" },
	{ MAX77693_MUIC_IRQ_INT3_OVP,		"muic-OVP" },
	{ MAX77693_MUIC_IRQ_INT3_MBCCHG_ERR,	"muic-MBCCHG_ERR" },
	{ MAX77693_MUIC_IRQ_INT3_CHG_ENABLED,	"muic-CHG_ENABLED" },
	{ MAX77693_MUIC_IRQ_INT3_BAT_DET,	"muic-BAT_DET" },
};

/* Define supported accessory type */
enum max77693_muic_acc_type {
	MAX77693_MUIC_ADC_GROUND = 0x0,
	MAX77693_MUIC_ADC_SEND_END_BUTTON,
	MAX77693_MUIC_ADC_REMOTE_S1_BUTTON,
	MAX77693_MUIC_ADC_REMOTE_S2_BUTTON,
	MAX77693_MUIC_ADC_REMOTE_S3_BUTTON,
	MAX77693_MUIC_ADC_REMOTE_S4_BUTTON,
	MAX77693_MUIC_ADC_REMOTE_S5_BUTTON,
	MAX77693_MUIC_ADC_REMOTE_S6_BUTTON,
	MAX77693_MUIC_ADC_REMOTE_S7_BUTTON,
	MAX77693_MUIC_ADC_REMOTE_S8_BUTTON,
	MAX77693_MUIC_ADC_REMOTE_S9_BUTTON,
	MAX77693_MUIC_ADC_REMOTE_S10_BUTTON,
	MAX77693_MUIC_ADC_REMOTE_S11_BUTTON,
	MAX77693_MUIC_ADC_REMOTE_S12_BUTTON,
	MAX77693_MUIC_ADC_RESERVED_ACC_1,
	MAX77693_MUIC_ADC_RESERVED_ACC_2,
	MAX77693_MUIC_ADC_RESERVED_ACC_3,
	MAX77693_MUIC_ADC_RESERVED_ACC_4,
	MAX77693_MUIC_ADC_RESERVED_ACC_5,
	MAX77693_MUIC_ADC_CEA936_AUDIO,
	MAX77693_MUIC_ADC_PHONE_POWERED_DEV,
	MAX77693_MUIC_ADC_TTY_CONVERTER,
	MAX77693_MUIC_ADC_UART_CABLE,
	MAX77693_MUIC_ADC_CEA936A_TYPE1_CHG,
	MAX77693_MUIC_ADC_FACTORY_MODE_USB_OFF,
	MAX77693_MUIC_ADC_FACTORY_MODE_USB_ON,
	MAX77693_MUIC_ADC_AV_CABLE_NOLOAD,
	MAX77693_MUIC_ADC_CEA936A_TYPE2_CHG,
	MAX77693_MUIC_ADC_FACTORY_MODE_UART_OFF,
	MAX77693_MUIC_ADC_FACTORY_MODE_UART_ON,
	MAX77693_MUIC_ADC_AUDIO_MODE_REMOTE,
	MAX77693_MUIC_ADC_OPEN,

	/* The below accessories have same ADC value so ADCLow and
	   ADC1K bit is used to separate specific accessory */
	MAX77693_MUIC_GND_USB_OTG = 0x100,	/* ADC:0x0, ADCLow:0, ADC1K:0 */
	MAX77693_MUIC_GND_AV_CABLE_LOAD = 0x102,/* ADC:0x0, ADCLow:1, ADC1K:0 */
	MAX77693_MUIC_GND_MHL_CABLE = 0x103,	/* ADC:0x0, ADCLow:1, ADC1K:1 */
};

/* MAX77693 MUIC device support below list of accessories(external connector) */
const char *max77693_extcon_cable[] = {
	[0] = "USB",
	[1] = "USB-Host",
	[2] = "TA",
	[3] = "Fast-charger",
	[4] = "Slow-charger",
	[5] = "Charge-downstream",
	[6] = "MHL",
	[7] = "Audio-video-load",
	[8] = "Audio-video-noload",
	[9] = "JIG",

	NULL,
};

static int max77693_muic_set_debounce_time(struct max77693_muic_info *info,
		enum max77693_muic_adc_debounce_time time)
{
	int ret = 0;
	u8 ctrl3;

	switch (time) {
	case ADC_DEBOUNCE_TIME_5MS:
	case ADC_DEBOUNCE_TIME_10MS:
	case ADC_DEBOUNCE_TIME_25MS:
	case ADC_DEBOUNCE_TIME_38_62MS:
		ret = max77693_read_reg(info->max77693->regmap_muic,
				MAX77693_MUIC_REG_CTRL3, &ctrl3);
		ctrl3 &= ~CONTROL3_ADCDBSET_MASK;
		ctrl3 |= (time << CONTROL3_ADCDBSET_SHIFT);

		ret = max77693_write_reg(info->max77693->regmap_muic,
				MAX77693_MUIC_REG_CTRL3, ctrl3);
		if (ret) {
			dev_err(info->dev, "failed to set ADC debounce time\n");
			ret = -EINVAL;
		}
		break;
	default:
		dev_err(info->dev, "invalid ADC debounce time\n");
		ret = -EINVAL;
		break;
	}

	return ret;
};

static int max77693_muic_set_path(struct max77693_muic_info *info,
		u8 val, bool attached)
{
	int ret = 0;
	u8 ctrl1, ctrl2 = 0;

	if (attached)
		ctrl1 = val;
	else
		ctrl1 = CONTROL1_SW_OPEN;

	ret = max77693_update_reg(info->max77693->regmap_muic,
			MAX77693_MUIC_REG_CTRL1, ctrl1, COMP_SW_MASK);
	if (ret < 0) {
		dev_err(info->dev, "failed to update MUIC register\n");
		goto out;
	}

	if (attached)
		ctrl2 |= CONTROL2_CPEN_MASK;	/* LowPwr=0, CPEn=1 */
	else
		ctrl2 |= CONTROL2_LOWPWR_MASK;	/* LowPwr=1, CPEn=0 */

	ret = max77693_update_reg(info->max77693->regmap_muic,
			MAX77693_MUIC_REG_CTRL2, ctrl2,
			CONTROL2_LOWPWR_MASK | CONTROL2_CPEN_MASK);
	if (ret < 0) {
		dev_err(info->dev, "failed to update MUIC register\n");
		goto out;
	}

	dev_info(info->dev,
		"CONTROL1 : 0x%02x, CONTROL2 : 0x%02x, state : %s\n",
		ctrl1, ctrl2, attached ? "attached" : "detached");
out:
	return ret;
}

static int max77693_muic_adc_ground_handler(struct max77693_muic_info *info,
		bool attached)
{
	int ret = 0;
	int type;
	int adc, adc1k, adclow;

	if (attached) {
		adc = info->status[0] & STATUS1_ADC_MASK;
		adclow = info->status[0] & STATUS1_ADCLOW_MASK;
		adclow >>= STATUS1_ADCLOW_SHIFT;
		adc1k = info->status[0] & STATUS1_ADC1K_MASK;
		adc1k >>= STATUS1_ADC1K_SHIFT;

		/**
		 * [0x1][ADCLow][ADC1K]
		 * [0x1    0       0  ]	: USB_OTG
		 * [0x1    1       0  ] : Audio Video Cable with load
		 * [0x1    1       1  ] : MHL
		 */
		type = ((0x1 << 8) | (adclow << 1) | adc1k);

		/* Store previous ADC value to handle accessory
		   when accessory will be detached */
		info->prev_adc = adc;
		info->prev_adc_gnd = type;
	} else
		type = info->prev_adc_gnd;

	switch (type) {
	case MAX77693_MUIC_GND_USB_OTG:
		/* USB_OTG */
		ret = max77693_muic_set_path(info, CONTROL1_SW_USB, attached);
		if (ret < 0)
			goto out;
		extcon_set_cable_state(info->edev, "USB-Host", attached);
		break;
	case MAX77693_MUIC_GND_AV_CABLE_LOAD:
		/* Audio Video Cable with load */
		ret = max77693_muic_set_path(info, CONTROL1_SW_AUDIO, attached);
		if (ret < 0)
			goto out;
		extcon_set_cable_state(info->edev,
				"Audio-video-load", attached);
		break;
	case MAX77693_MUIC_GND_MHL_CABLE:
		/* MHL */
		extcon_set_cable_state(info->edev, "MHL", attached);
		break;
	default:
		dev_err(info->dev, "failed to detect %s accessory\n",
			attached ? "attached" : "detached");
		dev_err(info->dev, "- adc:0x%x, adclow:0x%x, adc1k:0x%x\n",
			adc, adclow, adc1k);
		ret = -EINVAL;
		break;
	}

out:
	return ret;
}

static int max77693_muic_adc_handler(struct max77693_muic_info *info,
		int curr_adc, bool attached)
{
	int ret = 0;
	int adc;

	if (attached) {
		/* Store ADC value to handle accessory
		   when accessory will be detached */
		info->prev_adc = curr_adc;
		adc = curr_adc;
	} else
		adc = info->prev_adc;

	dev_info(info->dev,
		"external connector is %s (adc:0x%02x, prev_adc:0x%x)\n",
		attached ? "attached" : "detached", curr_adc, info->prev_adc);

	switch (adc) {
	case MAX77693_MUIC_ADC_GROUND:
		/* USB_OTG/MHL/Audio */
		max77693_muic_adc_ground_handler(info, attached);
		break;
	case MAX77693_MUIC_ADC_FACTORY_MODE_USB_OFF:
	case MAX77693_MUIC_ADC_FACTORY_MODE_USB_ON:
		/* USB */
		ret = max77693_muic_set_path(info, CONTROL1_SW_USB, attached);
		if (ret < 0)
			goto out;
		extcon_set_cable_state(info->edev, "USB", attached);
		break;
	case MAX77693_MUIC_ADC_FACTORY_MODE_UART_OFF:
	case MAX77693_MUIC_ADC_FACTORY_MODE_UART_ON:
		/* JIG */
		ret = max77693_muic_set_path(info, CONTROL1_SW_UART, attached);
		if (ret < 0)
			goto out;
		extcon_set_cable_state(info->edev, "JIG", attached);
		break;
	case MAX77693_MUIC_ADC_AUDIO_MODE_REMOTE:
		/* Audio Video cable with no-load */
		ret = max77693_muic_set_path(info, CONTROL1_SW_AUDIO, attached);
		if (ret < 0)
			goto out;
		extcon_set_cable_state(info->edev,
				"Audio-video-noload", attached);
		break;
	case MAX77693_MUIC_ADC_SEND_END_BUTTON:
	case MAX77693_MUIC_ADC_REMOTE_S1_BUTTON:
	case MAX77693_MUIC_ADC_REMOTE_S2_BUTTON:
	case MAX77693_MUIC_ADC_REMOTE_S3_BUTTON:
	case MAX77693_MUIC_ADC_REMOTE_S4_BUTTON:
	case MAX77693_MUIC_ADC_REMOTE_S5_BUTTON:
	case MAX77693_MUIC_ADC_REMOTE_S6_BUTTON:
	case MAX77693_MUIC_ADC_REMOTE_S7_BUTTON:
	case MAX77693_MUIC_ADC_REMOTE_S8_BUTTON:
	case MAX77693_MUIC_ADC_REMOTE_S9_BUTTON:
	case MAX77693_MUIC_ADC_REMOTE_S10_BUTTON:
	case MAX77693_MUIC_ADC_REMOTE_S11_BUTTON:
	case MAX77693_MUIC_ADC_REMOTE_S12_BUTTON:
	case MAX77693_MUIC_ADC_RESERVED_ACC_1:
	case MAX77693_MUIC_ADC_RESERVED_ACC_2:
	case MAX77693_MUIC_ADC_RESERVED_ACC_3:
	case MAX77693_MUIC_ADC_RESERVED_ACC_4:
	case MAX77693_MUIC_ADC_RESERVED_ACC_5:
	case MAX77693_MUIC_ADC_CEA936_AUDIO:
	case MAX77693_MUIC_ADC_PHONE_POWERED_DEV:
	case MAX77693_MUIC_ADC_TTY_CONVERTER:
	case MAX77693_MUIC_ADC_UART_CABLE:
	case MAX77693_MUIC_ADC_CEA936A_TYPE1_CHG:
	case MAX77693_MUIC_ADC_AV_CABLE_NOLOAD:
	case MAX77693_MUIC_ADC_CEA936A_TYPE2_CHG:
		/* This accessory isn't used in general case if it is specially
		   needed to detect additional accessory, should implement
		   proper operation when this accessory is attached/detached. */
		dev_info(info->dev,
			"accessory is %s but it isn't used (adc:0x%x)\n",
			attached ? "attached" : "detached", adc);
		goto out;
	default:
		dev_err(info->dev,
			"failed to detect %s accessory (adc:0x%x)\n",
			attached ? "attached" : "detached", adc);
		ret = -EINVAL;
		goto out;
	}

out:
	return ret;
}

static int max77693_muic_chg_handler(struct max77693_muic_info *info,
		int curr_chg_type, bool attached)
{
	int ret = 0;
	int chg_type;

	if (attached) {
		/* Store previous charger type to control
		   when charger accessory will be detached */
		info->prev_chg_type = curr_chg_type;
		chg_type = curr_chg_type;
	} else
		chg_type = info->prev_chg_type;

	dev_info(info->dev,
		"external connector is %s(chg_type:0x%x, prev_chg_type:0x%x)\n",
			attached ? "attached" : "detached",
			curr_chg_type, info->prev_chg_type);

	switch (chg_type) {
	case MAX77693_CHARGER_TYPE_USB:
		ret = max77693_muic_set_path(info, CONTROL1_SW_USB, attached);
		if (ret < 0)
			goto out;
		extcon_set_cable_state(info->edev, "USB", attached);
		break;
	case MAX77693_CHARGER_TYPE_DOWNSTREAM_PORT:
		extcon_set_cable_state(info->edev,
				"Charge-downstream", attached);
		break;
	case MAX77693_CHARGER_TYPE_DEDICATED_CHG:
		extcon_set_cable_state(info->edev, "TA", attached);
		break;
	case MAX77693_CHARGER_TYPE_APPLE_500MA:
		extcon_set_cable_state(info->edev, "Slow-charger", attached);
		break;
	case MAX77693_CHARGER_TYPE_APPLE_1A_2A:
		extcon_set_cable_state(info->edev, "Fast-charger", attached);
		break;
	case MAX77693_CHARGER_TYPE_DEAD_BATTERY:
		break;
	default:
		dev_err(info->dev,
			"failed to detect %s accessory (chg_type:0x%x)\n",
			attached ? "attached" : "detached", chg_type);
		ret = -EINVAL;
		goto out;
	}

out:
	return ret;
}

static void max77693_muic_irq_work(struct work_struct *work)
{
	struct max77693_muic_info *info = container_of(work,
			struct max77693_muic_info, irq_work);
	int curr_adc, curr_chg_type;
	int irq_type = -1;
	int i, ret = 0;
	bool attached = true;

	if (!info->edev)
		return;

	mutex_lock(&info->mutex);

	for (i = 0 ; i < ARRAY_SIZE(muic_irqs) ; i++)
		if (info->irq == muic_irqs[i].virq)
			irq_type = muic_irqs[i].irq;

	ret = max77693_bulk_read(info->max77693->regmap_muic,
			MAX77693_MUIC_REG_STATUS1, 2, info->status);
	if (ret) {
		dev_err(info->dev, "failed to read MUIC register\n");
		mutex_unlock(&info->mutex);
		return;
	}

	switch (irq_type) {
	case MAX77693_MUIC_IRQ_INT1_ADC:
	case MAX77693_MUIC_IRQ_INT1_ADC_LOW:
	case MAX77693_MUIC_IRQ_INT1_ADC_ERR:
	case MAX77693_MUIC_IRQ_INT1_ADC1K:
		/* Handle all of accessory except for
		   type of charger accessory */
		curr_adc = info->status[0] & STATUS1_ADC_MASK;
		curr_adc >>= STATUS1_ADC_SHIFT;

		/* Check accessory state which is either detached or attached */
		if (curr_adc == MAX77693_MUIC_ADC_OPEN)
			attached = false;

		ret = max77693_muic_adc_handler(info, curr_adc, attached);
		break;
	case MAX77693_MUIC_IRQ_INT2_CHGTYP:
	case MAX77693_MUIC_IRQ_INT2_CHGDETREUN:
	case MAX77693_MUIC_IRQ_INT2_DCDTMR:
	case MAX77693_MUIC_IRQ_INT2_DXOVP:
	case MAX77693_MUIC_IRQ_INT2_VBVOLT:
	case MAX77693_MUIC_IRQ_INT2_VIDRM:
		/* Handle charger accessory */
		curr_chg_type = info->status[1] & STATUS2_CHGTYP_MASK;
		curr_chg_type >>= STATUS2_CHGTYP_SHIFT;

		/* Check charger accessory state which
		   is either detached or attached */
		if (curr_chg_type == MAX77693_CHARGER_TYPE_NONE)
			attached = false;

		ret = max77693_muic_chg_handler(info, curr_chg_type, attached);
		break;
	case MAX77693_MUIC_IRQ_INT3_EOC:
	case MAX77693_MUIC_IRQ_INT3_CGMBC:
	case MAX77693_MUIC_IRQ_INT3_OVP:
	case MAX77693_MUIC_IRQ_INT3_MBCCHG_ERR:
	case MAX77693_MUIC_IRQ_INT3_CHG_ENABLED:
	case MAX77693_MUIC_IRQ_INT3_BAT_DET:
		break;
	default:
		dev_err(info->dev, "muic interrupt: irq %d occurred\n",
				irq_type);
		break;
	}

	if (ret < 0)
		dev_err(info->dev, "failed to handle MUIC interrupt\n");

	mutex_unlock(&info->mutex);

	return;
}

static irqreturn_t max77693_muic_irq_handler(int irq, void *data)
{
	struct max77693_muic_info *info = data;

	info->irq = irq;
	schedule_work(&info->irq_work);

	return IRQ_HANDLED;
}

static struct regmap_config max77693_muic_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int max77693_muic_detect_accessory(struct max77693_muic_info *info)
{
	int ret = 0;
	int adc, chg_type;

	mutex_lock(&info->mutex);

	/* Read STATUSx register to detect accessory */
	ret = max77693_bulk_read(info->max77693->regmap_muic,
			MAX77693_MUIC_REG_STATUS1, 2, info->status);
	if (ret) {
		dev_err(info->dev, "failed to read MUIC register\n");
		mutex_unlock(&info->mutex);
		return -EINVAL;
	}

	adc = info->status[0] & STATUS1_ADC_MASK;
	adc >>= STATUS1_ADC_SHIFT;

	if (adc != MAX77693_MUIC_ADC_OPEN) {
		dev_info(info->dev,
			"external connector is attached (adc:0x%02x)\n", adc);

		ret = max77693_muic_adc_handler(info, adc, true);
		if (ret < 0)
			dev_err(info->dev, "failed to detect accessory\n");
		goto out;
	}

	chg_type = info->status[1] & STATUS2_CHGTYP_MASK;
	chg_type >>= STATUS2_CHGTYP_SHIFT;

	if (chg_type != MAX77693_CHARGER_TYPE_NONE) {
		dev_info(info->dev,
			"external connector is attached (chg_type:0x%x)\n",
			chg_type);

		max77693_muic_chg_handler(info, chg_type, true);
		if (ret < 0)
			dev_err(info->dev, "failed to detect charger accessory\n");
	}

out:
	mutex_unlock(&info->mutex);
	return ret;
}

static int __devinit max77693_muic_probe(struct platform_device *pdev)
{
	struct max77693_dev *max77693 = dev_get_drvdata(pdev->dev.parent);
	struct max77693_muic_info *info;
	int ret, i;
	u8 id;

	info = kzalloc(sizeof(struct max77693_muic_info), GFP_KERNEL);
	if (!info) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		ret = -ENOMEM;
		goto err_kfree;
	}
	info->dev = &pdev->dev;
	info->max77693 = max77693;
	info->max77693->regmap_muic = regmap_init_i2c(info->max77693->muic,
					 &max77693_muic_regmap_config);
	if (IS_ERR(info->max77693->regmap_muic)) {
		ret = PTR_ERR(info->max77693->regmap_muic);
		dev_err(max77693->dev,
			"failed to allocate register map: %d\n", ret);
		goto err_regmap;
	}
	platform_set_drvdata(pdev, info);
	mutex_init(&info->mutex);

	INIT_WORK(&info->irq_work, max77693_muic_irq_work);

	/* Support irq domain for MAX77693 MUIC device */
	for (i = 0; i < ARRAY_SIZE(muic_irqs); i++) {
		struct max77693_muic_irq *muic_irq = &muic_irqs[i];
		int virq = 0;

		virq = irq_create_mapping(max77693->irq_domain, muic_irq->irq);
		if (!virq)
			goto err_irq;
		muic_irq->virq = virq;

		ret = request_threaded_irq(virq, NULL,
				max77693_muic_irq_handler,
				IRQF_ONESHOT, muic_irq->name, info);
		if (ret) {
			dev_err(&pdev->dev,
				"failed: irq request (IRQ: %d,"
				" error :%d)\n",
				muic_irq->irq, ret);

			for (i = i - 1; i >= 0; i--)
				free_irq(muic_irq->virq, info);
			goto err_irq;
		}
	}

	/* Initialize extcon device */
	info->edev = kzalloc(sizeof(struct extcon_dev), GFP_KERNEL);
	if (!info->edev) {
		dev_err(&pdev->dev, "failed to allocate memory for extcon\n");
		ret = -ENOMEM;
		goto err_irq;
	}
	info->edev->name = DEV_NAME;
	info->edev->supported_cable = max77693_extcon_cable;
	ret = extcon_dev_register(info->edev, NULL);
	if (ret) {
		dev_err(&pdev->dev, "failed to register extcon device\n");
		goto err_extcon;
	}

	/* Check revision number of MUIC device*/
	ret = max77693_read_reg(info->max77693->regmap_muic,
			MAX77693_MUIC_REG_ID, &id);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to read revision number\n");
		goto err_extcon;
	}
	dev_info(info->dev, "device ID : 0x%x\n", id);

	/* Set ADC debounce time */
	max77693_muic_set_debounce_time(info, ADC_DEBOUNCE_TIME_25MS);

	/* Detect accessory on boot */
	max77693_muic_detect_accessory(info);

	return ret;

err_extcon:
	kfree(info->edev);
err_irq:
err_regmap:
	kfree(info);
err_kfree:
	return ret;
}

static int __devexit max77693_muic_remove(struct platform_device *pdev)
{
	struct max77693_muic_info *info = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < ARRAY_SIZE(muic_irqs); i++)
		free_irq(muic_irqs[i].virq, info);
	cancel_work_sync(&info->irq_work);
	extcon_dev_unregister(info->edev);
	kfree(info);

	return 0;
}

static struct platform_driver max77693_muic_driver = {
	.driver		= {
		.name	= DEV_NAME,
		.owner	= THIS_MODULE,
	},
	.probe		= max77693_muic_probe,
	.remove		= __devexit_p(max77693_muic_remove),
};

module_platform_driver(max77693_muic_driver);

MODULE_DESCRIPTION("Maxim MAX77693 Extcon driver");
MODULE_AUTHOR("Chanwoo Choi <cw00.choi@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:extcon-max77693");
