// SPDX-License-Identifier: GPL-2.0+
//
// extcon-max8997.c - MAX8997 extcon driver to support MAX8997 MUIC
//
//  Copyright (C) 2012 Samsung Electronics
//  Donggeun Kim <dg77.kim@samsung.com>

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
#include <linux/extcon-provider.h>
#include <linux/irqdomain.h>

#define	DEV_NAME			"max8997-muic"
#define	DELAY_MS_DEFAULT		20000		/* unit: millisecond */

enum max8997_muic_adc_debounce_time {
	ADC_DEBOUNCE_TIME_0_5MS = 0,	/* 0.5ms */
	ADC_DEBOUNCE_TIME_10MS,		/* 10ms */
	ADC_DEBOUNCE_TIME_25MS,		/* 25ms */
	ADC_DEBOUNCE_TIME_38_62MS,	/* 38.62ms */
};

struct max8997_muic_irq {
	unsigned int irq;
	const char *name;
	unsigned int virq;
};

static struct max8997_muic_irq muic_irqs[] = {
	{ MAX8997_MUICIRQ_ADCError,	"muic-ADCERROR" },
	{ MAX8997_MUICIRQ_ADCLow,	"muic-ADCLOW" },
	{ MAX8997_MUICIRQ_ADC,		"muic-ADC" },
	{ MAX8997_MUICIRQ_VBVolt,	"muic-VBVOLT" },
	{ MAX8997_MUICIRQ_DBChg,	"muic-DBCHG" },
	{ MAX8997_MUICIRQ_DCDTmr,	"muic-DCDTMR" },
	{ MAX8997_MUICIRQ_ChgDetRun,	"muic-CHGDETRUN" },
	{ MAX8997_MUICIRQ_ChgTyp,	"muic-CHGTYP" },
	{ MAX8997_MUICIRQ_OVP,		"muic-OVP" },
};

/* Define supported cable type */
enum max8997_muic_acc_type {
	MAX8997_MUIC_ADC_GROUND = 0x0,
	MAX8997_MUIC_ADC_MHL,			/* MHL*/
	MAX8997_MUIC_ADC_REMOTE_S1_BUTTON,
	MAX8997_MUIC_ADC_REMOTE_S2_BUTTON,
	MAX8997_MUIC_ADC_REMOTE_S3_BUTTON,
	MAX8997_MUIC_ADC_REMOTE_S4_BUTTON,
	MAX8997_MUIC_ADC_REMOTE_S5_BUTTON,
	MAX8997_MUIC_ADC_REMOTE_S6_BUTTON,
	MAX8997_MUIC_ADC_REMOTE_S7_BUTTON,
	MAX8997_MUIC_ADC_REMOTE_S8_BUTTON,
	MAX8997_MUIC_ADC_REMOTE_S9_BUTTON,
	MAX8997_MUIC_ADC_REMOTE_S10_BUTTON,
	MAX8997_MUIC_ADC_REMOTE_S11_BUTTON,
	MAX8997_MUIC_ADC_REMOTE_S12_BUTTON,
	MAX8997_MUIC_ADC_RESERVED_ACC_1,
	MAX8997_MUIC_ADC_RESERVED_ACC_2,
	MAX8997_MUIC_ADC_RESERVED_ACC_3,
	MAX8997_MUIC_ADC_RESERVED_ACC_4,
	MAX8997_MUIC_ADC_RESERVED_ACC_5,
	MAX8997_MUIC_ADC_CEA936_AUDIO,
	MAX8997_MUIC_ADC_PHONE_POWERED_DEV,
	MAX8997_MUIC_ADC_TTY_CONVERTER,
	MAX8997_MUIC_ADC_UART_CABLE,
	MAX8997_MUIC_ADC_CEA936A_TYPE1_CHG,
	MAX8997_MUIC_ADC_FACTORY_MODE_USB_OFF,	/* JIG-USB-OFF */
	MAX8997_MUIC_ADC_FACTORY_MODE_USB_ON,	/* JIG-USB-ON */
	MAX8997_MUIC_ADC_AV_CABLE_NOLOAD,	/* DESKDOCK */
	MAX8997_MUIC_ADC_CEA936A_TYPE2_CHG,
	MAX8997_MUIC_ADC_FACTORY_MODE_UART_OFF,	/* JIG-UART */
	MAX8997_MUIC_ADC_FACTORY_MODE_UART_ON,	/* CARDOCK */
	MAX8997_MUIC_ADC_AUDIO_MODE_REMOTE,
	MAX8997_MUIC_ADC_OPEN,			/* OPEN */
};

enum max8997_muic_cable_group {
	MAX8997_CABLE_GROUP_ADC = 0,
	MAX8997_CABLE_GROUP_ADC_GND,
	MAX8997_CABLE_GROUP_CHG,
	MAX8997_CABLE_GROUP_VBVOLT,
};

enum max8997_muic_usb_type {
	MAX8997_USB_HOST,
	MAX8997_USB_DEVICE,
};

enum max8997_muic_charger_type {
	MAX8997_CHARGER_TYPE_NONE = 0,
	MAX8997_CHARGER_TYPE_USB,
	MAX8997_CHARGER_TYPE_DOWNSTREAM_PORT,
	MAX8997_CHARGER_TYPE_DEDICATED_CHG,
	MAX8997_CHARGER_TYPE_500MA,
	MAX8997_CHARGER_TYPE_1A,
	MAX8997_CHARGER_TYPE_DEAD_BATTERY = 7,
};

struct max8997_muic_info {
	struct device *dev;
	struct i2c_client *muic;
	struct extcon_dev *edev;
	int prev_cable_type;
	int prev_chg_type;
	u8 status[2];

	int irq;
	struct work_struct irq_work;
	struct mutex mutex;

	struct max8997_muic_platform_data *muic_pdata;
	enum max8997_muic_charger_type pre_charger_type;

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

static const unsigned int max8997_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_CHG_USB_SDP,
	EXTCON_CHG_USB_DCP,
	EXTCON_CHG_USB_FAST,
	EXTCON_CHG_USB_SLOW,
	EXTCON_CHG_USB_CDP,
	EXTCON_DISP_MHL,
	EXTCON_DOCK,
	EXTCON_JIG,
	EXTCON_NONE,
};

/*
 * max8997_muic_set_debounce_time - Set the debounce time of ADC
 * @info: the instance including private data of max8997 MUIC
 * @time: the debounce time of ADC
 */
static int max8997_muic_set_debounce_time(struct max8997_muic_info *info,
		enum max8997_muic_adc_debounce_time time)
{
	int ret;

	switch (time) {
	case ADC_DEBOUNCE_TIME_0_5MS:
	case ADC_DEBOUNCE_TIME_10MS:
	case ADC_DEBOUNCE_TIME_25MS:
	case ADC_DEBOUNCE_TIME_38_62MS:
		ret = max8997_update_reg(info->muic,
					  MAX8997_MUIC_REG_CONTROL3,
					  time << CONTROL3_ADCDBSET_SHIFT,
					  CONTROL3_ADCDBSET_MASK);
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
 * max8997_muic_set_path - Set hardware line according to attached cable
 * @info: the instance including private data of max8997 MUIC
 * @value: the path according to attached cable
 * @attached: the state of cable (true:attached, false:detached)
 *
 * The max8997 MUIC device share outside H/W line among a varity of cables,
 * so this function set internal path of H/W line according to the type of
 * attached cable.
 */
static int max8997_muic_set_path(struct max8997_muic_info *info,
		u8 val, bool attached)
{
	int ret;
	u8 ctrl1, ctrl2 = 0;

	if (attached)
		ctrl1 = val;
	else
		ctrl1 = CONTROL1_SW_OPEN;

	ret = max8997_update_reg(info->muic,
			MAX8997_MUIC_REG_CONTROL1, ctrl1, COMP_SW_MASK);
	if (ret < 0) {
		dev_err(info->dev, "failed to update MUIC register\n");
		return ret;
	}

	if (attached)
		ctrl2 |= CONTROL2_CPEN_MASK;	/* LowPwr=0, CPEn=1 */
	else
		ctrl2 |= CONTROL2_LOWPWR_MASK;	/* LowPwr=1, CPEn=0 */

	ret = max8997_update_reg(info->muic,
			MAX8997_MUIC_REG_CONTROL2, ctrl2,
			CONTROL2_LOWPWR_MASK | CONTROL2_CPEN_MASK);
	if (ret < 0) {
		dev_err(info->dev, "failed to update MUIC register\n");
		return ret;
	}

	dev_info(info->dev,
		"CONTROL1 : 0x%02x, CONTROL2 : 0x%02x, state : %s\n",
		ctrl1, ctrl2, attached ? "attached" : "detached");

	return 0;
}

/*
 * max8997_muic_get_cable_type - Return cable type and check cable state
 * @info: the instance including private data of max8997 MUIC
 * @group: the path according to attached cable
 * @attached: store cable state and return
 *
 * This function check the cable state either attached or detached,
 * and then divide precise type of cable according to cable group.
 *	- MAX8997_CABLE_GROUP_ADC
 *	- MAX8997_CABLE_GROUP_CHG
 */
static int max8997_muic_get_cable_type(struct max8997_muic_info *info,
		enum max8997_muic_cable_group group, bool *attached)
{
	int cable_type = 0;
	int adc;
	int chg_type;

	switch (group) {
	case MAX8997_CABLE_GROUP_ADC:
		/*
		 * Read ADC value to check cable type and decide cable state
		 * according to cable type
		 */
		adc = info->status[0] & STATUS1_ADC_MASK;
		adc >>= STATUS1_ADC_SHIFT;

		/*
		 * Check current cable state/cable type and store cable type
		 * (info->prev_cable_type) for handling cable when cable is
		 * detached.
		 */
		if (adc == MAX8997_MUIC_ADC_OPEN) {
			*attached = false;

			cable_type = info->prev_cable_type;
			info->prev_cable_type = MAX8997_MUIC_ADC_OPEN;
		} else {
			*attached = true;

			cable_type = info->prev_cable_type = adc;
		}
		break;
	case MAX8997_CABLE_GROUP_CHG:
		/*
		 * Read charger type to check cable type and decide cable state
		 * according to type of charger cable.
		 */
		chg_type = info->status[1] & STATUS2_CHGTYP_MASK;
		chg_type >>= STATUS2_CHGTYP_SHIFT;

		if (chg_type == MAX8997_CHARGER_TYPE_NONE) {
			*attached = false;

			cable_type = info->prev_chg_type;
			info->prev_chg_type = MAX8997_CHARGER_TYPE_NONE;
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

static int max8997_muic_handle_usb(struct max8997_muic_info *info,
			enum max8997_muic_usb_type usb_type, bool attached)
{
	int ret = 0;

	if (usb_type == MAX8997_USB_HOST) {
		ret = max8997_muic_set_path(info, info->path_usb, attached);
		if (ret < 0) {
			dev_err(info->dev, "failed to update muic register\n");
			return ret;
		}
	}

	switch (usb_type) {
	case MAX8997_USB_HOST:
		extcon_set_state_sync(info->edev, EXTCON_USB_HOST, attached);
		break;
	case MAX8997_USB_DEVICE:
		extcon_set_state_sync(info->edev, EXTCON_USB, attached);
		extcon_set_state_sync(info->edev, EXTCON_CHG_USB_SDP,
					attached);
		break;
	default:
		dev_err(info->dev, "failed to detect %s usb cable\n",
			attached ? "attached" : "detached");
		return -EINVAL;
	}

	return 0;
}

static int max8997_muic_handle_dock(struct max8997_muic_info *info,
			int cable_type, bool attached)
{
	int ret = 0;

	ret = max8997_muic_set_path(info, CONTROL1_SW_AUDIO, attached);
	if (ret) {
		dev_err(info->dev, "failed to update muic register\n");
		return ret;
	}

	switch (cable_type) {
	case MAX8997_MUIC_ADC_AV_CABLE_NOLOAD:
	case MAX8997_MUIC_ADC_FACTORY_MODE_UART_ON:
		extcon_set_state_sync(info->edev, EXTCON_DOCK, attached);
		break;
	default:
		dev_err(info->dev, "failed to detect %s dock device\n",
			attached ? "attached" : "detached");
		return -EINVAL;
	}

	return 0;
}

static int max8997_muic_handle_jig_uart(struct max8997_muic_info *info,
			bool attached)
{
	int ret = 0;

	/* switch to UART */
	ret = max8997_muic_set_path(info, info->path_uart, attached);
	if (ret) {
		dev_err(info->dev, "failed to update muic register\n");
		return ret;
	}

	extcon_set_state_sync(info->edev, EXTCON_JIG, attached);

	return 0;
}

static int max8997_muic_adc_handler(struct max8997_muic_info *info)
{
	int cable_type;
	bool attached;
	int ret = 0;

	/* Check cable state which is either detached or attached */
	cable_type = max8997_muic_get_cable_type(info,
				MAX8997_CABLE_GROUP_ADC, &attached);

	switch (cable_type) {
	case MAX8997_MUIC_ADC_GROUND:
		ret = max8997_muic_handle_usb(info, MAX8997_USB_HOST, attached);
		if (ret < 0)
			return ret;
		break;
	case MAX8997_MUIC_ADC_MHL:
		extcon_set_state_sync(info->edev, EXTCON_DISP_MHL, attached);
		break;
	case MAX8997_MUIC_ADC_FACTORY_MODE_USB_OFF:
	case MAX8997_MUIC_ADC_FACTORY_MODE_USB_ON:
		ret = max8997_muic_handle_usb(info,
					     MAX8997_USB_DEVICE, attached);
		if (ret < 0)
			return ret;
		break;
	case MAX8997_MUIC_ADC_AV_CABLE_NOLOAD:
	case MAX8997_MUIC_ADC_FACTORY_MODE_UART_ON:
		ret = max8997_muic_handle_dock(info, cable_type, attached);
		if (ret < 0)
			return ret;
		break;
	case MAX8997_MUIC_ADC_FACTORY_MODE_UART_OFF:
		ret = max8997_muic_handle_jig_uart(info, attached);
		break;
	case MAX8997_MUIC_ADC_REMOTE_S1_BUTTON:
	case MAX8997_MUIC_ADC_REMOTE_S2_BUTTON:
	case MAX8997_MUIC_ADC_REMOTE_S3_BUTTON:
	case MAX8997_MUIC_ADC_REMOTE_S4_BUTTON:
	case MAX8997_MUIC_ADC_REMOTE_S5_BUTTON:
	case MAX8997_MUIC_ADC_REMOTE_S6_BUTTON:
	case MAX8997_MUIC_ADC_REMOTE_S7_BUTTON:
	case MAX8997_MUIC_ADC_REMOTE_S8_BUTTON:
	case MAX8997_MUIC_ADC_REMOTE_S9_BUTTON:
	case MAX8997_MUIC_ADC_REMOTE_S10_BUTTON:
	case MAX8997_MUIC_ADC_REMOTE_S11_BUTTON:
	case MAX8997_MUIC_ADC_REMOTE_S12_BUTTON:
	case MAX8997_MUIC_ADC_RESERVED_ACC_1:
	case MAX8997_MUIC_ADC_RESERVED_ACC_2:
	case MAX8997_MUIC_ADC_RESERVED_ACC_3:
	case MAX8997_MUIC_ADC_RESERVED_ACC_4:
	case MAX8997_MUIC_ADC_RESERVED_ACC_5:
	case MAX8997_MUIC_ADC_CEA936_AUDIO:
	case MAX8997_MUIC_ADC_PHONE_POWERED_DEV:
	case MAX8997_MUIC_ADC_TTY_CONVERTER:
	case MAX8997_MUIC_ADC_UART_CABLE:
	case MAX8997_MUIC_ADC_CEA936A_TYPE1_CHG:
	case MAX8997_MUIC_ADC_CEA936A_TYPE2_CHG:
	case MAX8997_MUIC_ADC_AUDIO_MODE_REMOTE:
		/*
		 * This cable isn't used in general case if it is specially
		 * needed to detect additional cable, should implement
		 * proper operation when this cable is attached/detached.
		 */
		dev_info(info->dev,
			"cable is %s but it isn't used (type:0x%x)\n",
			attached ? "attached" : "detached", cable_type);
		return -EAGAIN;
	default:
		dev_err(info->dev,
			"failed to detect %s unknown cable (type:0x%x)\n",
			attached ? "attached" : "detached", cable_type);
		return -EINVAL;
	}

	return 0;
}

static int max8997_muic_chg_handler(struct max8997_muic_info *info)
{
	int chg_type;
	bool attached;
	int adc;

	chg_type = max8997_muic_get_cable_type(info,
				MAX8997_CABLE_GROUP_CHG, &attached);

	switch (chg_type) {
	case MAX8997_CHARGER_TYPE_NONE:
		break;
	case MAX8997_CHARGER_TYPE_USB:
		adc = info->status[0] & STATUS1_ADC_MASK;
		adc >>= STATUS1_ADC_SHIFT;

		if ((adc & STATUS1_ADC_MASK) == MAX8997_MUIC_ADC_OPEN) {
			max8997_muic_handle_usb(info,
					MAX8997_USB_DEVICE, attached);
		}
		break;
	case MAX8997_CHARGER_TYPE_DOWNSTREAM_PORT:
		extcon_set_state_sync(info->edev, EXTCON_CHG_USB_CDP,
					attached);
		break;
	case MAX8997_CHARGER_TYPE_DEDICATED_CHG:
		extcon_set_state_sync(info->edev, EXTCON_CHG_USB_DCP,
					attached);
		break;
	case MAX8997_CHARGER_TYPE_500MA:
		extcon_set_state_sync(info->edev, EXTCON_CHG_USB_SLOW,
					attached);
		break;
	case MAX8997_CHARGER_TYPE_1A:
		extcon_set_state_sync(info->edev, EXTCON_CHG_USB_FAST,
					attached);
		break;
	default:
		dev_err(info->dev,
			"failed to detect %s unknown chg cable (type:0x%x)\n",
			attached ? "attached" : "detached", chg_type);
		return -EINVAL;
	}

	return 0;
}

static void max8997_muic_irq_work(struct work_struct *work)
{
	struct max8997_muic_info *info = container_of(work,
			struct max8997_muic_info, irq_work);
	int irq_type = 0;
	int i, ret;

	if (!info->edev)
		return;

	mutex_lock(&info->mutex);

	for (i = 0; i < ARRAY_SIZE(muic_irqs); i++)
		if (info->irq == muic_irqs[i].virq)
			irq_type = muic_irqs[i].irq;

	ret = max8997_bulk_read(info->muic, MAX8997_MUIC_REG_STATUS1,
				2, info->status);
	if (ret) {
		dev_err(info->dev, "failed to read muic register\n");
		mutex_unlock(&info->mutex);
		return;
	}

	switch (irq_type) {
	case MAX8997_MUICIRQ_ADCError:
	case MAX8997_MUICIRQ_ADCLow:
	case MAX8997_MUICIRQ_ADC:
		/* Handle all of cable except for charger cable */
		ret = max8997_muic_adc_handler(info);
		break;
	case MAX8997_MUICIRQ_VBVolt:
	case MAX8997_MUICIRQ_DBChg:
	case MAX8997_MUICIRQ_DCDTmr:
	case MAX8997_MUICIRQ_ChgDetRun:
	case MAX8997_MUICIRQ_ChgTyp:
		/* Handle charger cable */
		ret = max8997_muic_chg_handler(info);
		break;
	case MAX8997_MUICIRQ_OVP:
		break;
	default:
		dev_info(info->dev, "misc interrupt: irq %d occurred\n",
				irq_type);
		mutex_unlock(&info->mutex);
		return;
	}

	if (ret < 0)
		dev_err(info->dev, "failed to handle MUIC interrupt\n");

	mutex_unlock(&info->mutex);
}

static irqreturn_t max8997_muic_irq_handler(int irq, void *data)
{
	struct max8997_muic_info *info = data;

	dev_dbg(info->dev, "irq:%d\n", irq);
	info->irq = irq;

	schedule_work(&info->irq_work);

	return IRQ_HANDLED;
}

static int max8997_muic_detect_dev(struct max8997_muic_info *info)
{
	int ret = 0;
	int adc;
	int chg_type;
	bool attached;

	mutex_lock(&info->mutex);

	/* Read STATUSx register to detect accessory */
	ret = max8997_bulk_read(info->muic,
			MAX8997_MUIC_REG_STATUS1, 2, info->status);
	if (ret) {
		dev_err(info->dev, "failed to read MUIC register\n");
		mutex_unlock(&info->mutex);
		return ret;
	}

	adc = max8997_muic_get_cable_type(info, MAX8997_CABLE_GROUP_ADC,
					&attached);
	if (attached && adc != MAX8997_MUIC_ADC_OPEN) {
		ret = max8997_muic_adc_handler(info);
		if (ret < 0) {
			dev_err(info->dev, "Cannot detect ADC cable\n");
			mutex_unlock(&info->mutex);
			return ret;
		}
	}

	chg_type = max8997_muic_get_cable_type(info, MAX8997_CABLE_GROUP_CHG,
					&attached);
	if (attached && chg_type != MAX8997_CHARGER_TYPE_NONE) {
		ret = max8997_muic_chg_handler(info);
		if (ret < 0) {
			dev_err(info->dev, "Cannot detect charger cable\n");
			mutex_unlock(&info->mutex);
			return ret;
		}
	}

	mutex_unlock(&info->mutex);

	return 0;
}

static void max8997_muic_detect_cable_wq(struct work_struct *work)
{
	struct max8997_muic_info *info = container_of(to_delayed_work(work),
				struct max8997_muic_info, wq_detcable);
	int ret;

	ret = max8997_muic_detect_dev(info);
	if (ret < 0)
		dev_err(info->dev, "failed to detect cable type\n");
}

static int max8997_muic_probe(struct platform_device *pdev)
{
	struct max8997_dev *max8997 = dev_get_drvdata(pdev->dev.parent);
	struct max8997_platform_data *pdata = dev_get_platdata(max8997->dev);
	struct max8997_muic_info *info;
	int delay_jiffies;
	int ret, i;

	info = devm_kzalloc(&pdev->dev, sizeof(struct max8997_muic_info),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	info->muic = max8997->muic;

	platform_set_drvdata(pdev, info);
	mutex_init(&info->mutex);

	INIT_WORK(&info->irq_work, max8997_muic_irq_work);

	for (i = 0; i < ARRAY_SIZE(muic_irqs); i++) {
		struct max8997_muic_irq *muic_irq = &muic_irqs[i];
		unsigned int virq = 0;

		virq = irq_create_mapping(max8997->irq_domain, muic_irq->irq);
		if (!virq) {
			ret = -EINVAL;
			goto err_irq;
		}
		muic_irq->virq = virq;

		ret = request_threaded_irq(virq, NULL,
				max8997_muic_irq_handler,
				IRQF_NO_SUSPEND,
				muic_irq->name, info);
		if (ret) {
			dev_err(&pdev->dev,
				"failed: irq request (IRQ: %d, error :%d)\n",
				muic_irq->irq, ret);
			goto err_irq;
		}
	}

	/* External connector */
	info->edev = devm_extcon_dev_allocate(&pdev->dev, max8997_extcon_cable);
	if (IS_ERR(info->edev)) {
		dev_err(&pdev->dev, "failed to allocate memory for extcon\n");
		ret = -ENOMEM;
		goto err_irq;
	}

	ret = devm_extcon_dev_register(&pdev->dev, info->edev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register extcon device\n");
		goto err_irq;
	}

	if (pdata && pdata->muic_pdata) {
		struct max8997_muic_platform_data *muic_pdata
			= pdata->muic_pdata;

		/* Initialize registers according to platform data */
		for (i = 0; i < muic_pdata->num_init_data; i++) {
			max8997_write_reg(info->muic,
					muic_pdata->init_data[i].addr,
					muic_pdata->init_data[i].data);
		}

		/*
		 * Default usb/uart path whether UART/USB or AUX_UART/AUX_USB
		 * h/w path of COMP2/COMN1 on CONTROL1 register.
		 */
		if (muic_pdata->path_uart)
			info->path_uart = muic_pdata->path_uart;
		else
			info->path_uart = CONTROL1_SW_UART;

		if (muic_pdata->path_usb)
			info->path_usb = muic_pdata->path_usb;
		else
			info->path_usb = CONTROL1_SW_USB;

		/*
		 * Default delay time for detecting cable state
		 * after certain time.
		 */
		if (muic_pdata->detcable_delay_ms)
			delay_jiffies =
				msecs_to_jiffies(muic_pdata->detcable_delay_ms);
		else
			delay_jiffies = msecs_to_jiffies(DELAY_MS_DEFAULT);
	} else {
		info->path_uart = CONTROL1_SW_UART;
		info->path_usb = CONTROL1_SW_USB;
		delay_jiffies = msecs_to_jiffies(DELAY_MS_DEFAULT);
	}

	/* Set initial path for UART */
	 max8997_muic_set_path(info, info->path_uart, true);

	/* Set ADC debounce time */
	max8997_muic_set_debounce_time(info, ADC_DEBOUNCE_TIME_25MS);

	/*
	 * Detect accessory after completing the initialization of platform
	 *
	 * - Use delayed workqueue to detect cable state and then
	 * notify cable state to notifiee/platform through uevent.
	 * After completing the booting of platform, the extcon provider
	 * driver should notify cable state to upper layer.
	 */
	INIT_DELAYED_WORK(&info->wq_detcable, max8997_muic_detect_cable_wq);
	queue_delayed_work(system_power_efficient_wq, &info->wq_detcable,
			delay_jiffies);

	return 0;

err_irq:
	while (--i >= 0)
		free_irq(muic_irqs[i].virq, info);
	return ret;
}

static int max8997_muic_remove(struct platform_device *pdev)
{
	struct max8997_muic_info *info = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < ARRAY_SIZE(muic_irqs); i++)
		free_irq(muic_irqs[i].virq, info);
	cancel_work_sync(&info->irq_work);

	return 0;
}

static struct platform_driver max8997_muic_driver = {
	.driver		= {
		.name	= DEV_NAME,
	},
	.probe		= max8997_muic_probe,
	.remove		= max8997_muic_remove,
};

module_platform_driver(max8997_muic_driver);

MODULE_DESCRIPTION("Maxim MAX8997 Extcon driver");
MODULE_AUTHOR("Donggeun Kim <dg77.kim@samsung.com>");
MODULE_LICENSE("GPL");
