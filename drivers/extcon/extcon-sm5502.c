/*
 * extcon-sm5502.c - Silicon Mitus SM5502 extcon drvier to support USB switches
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd
 * Author: Chanwoo Choi <cw00.choi@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/extcon.h>

#include "extcon-sm5502.h"

#define	DELAY_MS_DEFAULT		17000	/* unit: millisecond */

struct muic_irq {
	unsigned int irq;
	const char *name;
	unsigned int virq;
};

struct reg_data {
	u8 reg;
	unsigned int val;
	bool invert;
};

struct sm5502_muic_info {
	struct device *dev;
	struct extcon_dev *edev;

	struct i2c_client *i2c;
	struct regmap *regmap;

	struct regmap_irq_chip_data *irq_data;
	struct muic_irq *muic_irqs;
	unsigned int num_muic_irqs;
	int irq;
	bool irq_attach;
	bool irq_detach;
	struct work_struct irq_work;

	struct reg_data *reg_data;
	unsigned int num_reg_data;

	struct mutex mutex;

	/*
	 * Use delayed workqueue to detect cable state and then
	 * notify cable state to notifiee/platform through uevent.
	 * After completing the booting of platform, the extcon provider
	 * driver should notify cable state to upper layer.
	 */
	struct delayed_work wq_detcable;
};

/* Default value of SM5502 register to bring up MUIC device. */
static struct reg_data sm5502_reg_data[] = {
	{
		.reg = SM5502_REG_CONTROL,
		.val = SM5502_REG_CONTROL_MASK_INT_MASK,
		.invert = false,
	}, {
		.reg = SM5502_REG_INTMASK1,
		.val = SM5502_REG_INTM1_KP_MASK
			| SM5502_REG_INTM1_LKP_MASK
			| SM5502_REG_INTM1_LKR_MASK,
		.invert = true,
	}, {
		.reg = SM5502_REG_INTMASK2,
		.val = SM5502_REG_INTM2_VBUS_DET_MASK
			| SM5502_REG_INTM2_REV_ACCE_MASK
			| SM5502_REG_INTM2_ADC_CHG_MASK
			| SM5502_REG_INTM2_STUCK_KEY_MASK
			| SM5502_REG_INTM2_STUCK_KEY_RCV_MASK
			| SM5502_REG_INTM2_MHL_MASK,
		.invert = true,
	},
	{ }
};

/* List of detectable cables */
static const unsigned int sm5502_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_CHG_USB_SDP,
	EXTCON_CHG_USB_DCP,
	EXTCON_NONE,
};

/* Define supported accessory type */
enum sm5502_muic_acc_type {
	SM5502_MUIC_ADC_GROUND = 0x0,
	SM5502_MUIC_ADC_SEND_END_BUTTON,
	SM5502_MUIC_ADC_REMOTE_S1_BUTTON,
	SM5502_MUIC_ADC_REMOTE_S2_BUTTON,
	SM5502_MUIC_ADC_REMOTE_S3_BUTTON,
	SM5502_MUIC_ADC_REMOTE_S4_BUTTON,
	SM5502_MUIC_ADC_REMOTE_S5_BUTTON,
	SM5502_MUIC_ADC_REMOTE_S6_BUTTON,
	SM5502_MUIC_ADC_REMOTE_S7_BUTTON,
	SM5502_MUIC_ADC_REMOTE_S8_BUTTON,
	SM5502_MUIC_ADC_REMOTE_S9_BUTTON,
	SM5502_MUIC_ADC_REMOTE_S10_BUTTON,
	SM5502_MUIC_ADC_REMOTE_S11_BUTTON,
	SM5502_MUIC_ADC_REMOTE_S12_BUTTON,
	SM5502_MUIC_ADC_RESERVED_ACC_1,
	SM5502_MUIC_ADC_RESERVED_ACC_2,
	SM5502_MUIC_ADC_RESERVED_ACC_3,
	SM5502_MUIC_ADC_RESERVED_ACC_4,
	SM5502_MUIC_ADC_RESERVED_ACC_5,
	SM5502_MUIC_ADC_AUDIO_TYPE2,
	SM5502_MUIC_ADC_PHONE_POWERED_DEV,
	SM5502_MUIC_ADC_TTY_CONVERTER,
	SM5502_MUIC_ADC_UART_CABLE,
	SM5502_MUIC_ADC_TYPE1_CHARGER,
	SM5502_MUIC_ADC_FACTORY_MODE_BOOT_OFF_USB,
	SM5502_MUIC_ADC_FACTORY_MODE_BOOT_ON_USB,
	SM5502_MUIC_ADC_AUDIO_VIDEO_CABLE,
	SM5502_MUIC_ADC_TYPE2_CHARGER,
	SM5502_MUIC_ADC_FACTORY_MODE_BOOT_OFF_UART,
	SM5502_MUIC_ADC_FACTORY_MODE_BOOT_ON_UART,
	SM5502_MUIC_ADC_AUDIO_TYPE1,
	SM5502_MUIC_ADC_OPEN = 0x1f,

	/* The below accessories have same ADC value (0x1f or 0x1e).
	   So, Device type1 is used to separate specific accessory. */
							/* |---------|--ADC| */
							/* |    [7:5]|[4:0]| */
	SM5502_MUIC_ADC_AUDIO_TYPE1_FULL_REMOTE = 0x3e,	/* |      001|11110| */
	SM5502_MUIC_ADC_AUDIO_TYPE1_SEND_END = 0x5e,	/* |      010|11110| */
							/* |Dev Type1|--ADC| */
	SM5502_MUIC_ADC_OPEN_USB = 0x5f,		/* |      010|11111| */
	SM5502_MUIC_ADC_OPEN_TA = 0xdf,			/* |      110|11111| */
	SM5502_MUIC_ADC_OPEN_USB_OTG = 0xff,		/* |      111|11111| */
};

/* List of supported interrupt for SM5502 */
static struct muic_irq sm5502_muic_irqs[] = {
	{ SM5502_IRQ_INT1_ATTACH,	"muic-attach" },
	{ SM5502_IRQ_INT1_DETACH,	"muic-detach" },
	{ SM5502_IRQ_INT1_KP,		"muic-kp" },
	{ SM5502_IRQ_INT1_LKP,		"muic-lkp" },
	{ SM5502_IRQ_INT1_LKR,		"muic-lkr" },
	{ SM5502_IRQ_INT1_OVP_EVENT,	"muic-ovp-event" },
	{ SM5502_IRQ_INT1_OCP_EVENT,	"muic-ocp-event" },
	{ SM5502_IRQ_INT1_OVP_OCP_DIS,	"muic-ovp-ocp-dis" },
	{ SM5502_IRQ_INT2_VBUS_DET,	"muic-vbus-det" },
	{ SM5502_IRQ_INT2_REV_ACCE,	"muic-rev-acce" },
	{ SM5502_IRQ_INT2_ADC_CHG,	"muic-adc-chg" },
	{ SM5502_IRQ_INT2_STUCK_KEY,	"muic-stuck-key" },
	{ SM5502_IRQ_INT2_STUCK_KEY_RCV, "muic-stuck-key-rcv" },
	{ SM5502_IRQ_INT2_MHL,		"muic-mhl" },
};

/* Define interrupt list of SM5502 to register regmap_irq */
static const struct regmap_irq sm5502_irqs[] = {
	/* INT1 interrupts */
	{ .reg_offset = 0, .mask = SM5502_IRQ_INT1_ATTACH_MASK, },
	{ .reg_offset = 0, .mask = SM5502_IRQ_INT1_DETACH_MASK, },
	{ .reg_offset = 0, .mask = SM5502_IRQ_INT1_KP_MASK, },
	{ .reg_offset = 0, .mask = SM5502_IRQ_INT1_LKP_MASK, },
	{ .reg_offset = 0, .mask = SM5502_IRQ_INT1_LKR_MASK, },
	{ .reg_offset = 0, .mask = SM5502_IRQ_INT1_OVP_EVENT_MASK, },
	{ .reg_offset = 0, .mask = SM5502_IRQ_INT1_OCP_EVENT_MASK, },
	{ .reg_offset = 0, .mask = SM5502_IRQ_INT1_OVP_OCP_DIS_MASK, },

	/* INT2 interrupts */
	{ .reg_offset = 1, .mask = SM5502_IRQ_INT2_VBUS_DET_MASK,},
	{ .reg_offset = 1, .mask = SM5502_IRQ_INT2_REV_ACCE_MASK, },
	{ .reg_offset = 1, .mask = SM5502_IRQ_INT2_ADC_CHG_MASK, },
	{ .reg_offset = 1, .mask = SM5502_IRQ_INT2_STUCK_KEY_MASK, },
	{ .reg_offset = 1, .mask = SM5502_IRQ_INT2_STUCK_KEY_RCV_MASK, },
	{ .reg_offset = 1, .mask = SM5502_IRQ_INT2_MHL_MASK, },
};

static const struct regmap_irq_chip sm5502_muic_irq_chip = {
	.name			= "sm5502",
	.status_base		= SM5502_REG_INT1,
	.mask_base		= SM5502_REG_INTMASK1,
	.mask_invert		= false,
	.num_regs		= 2,
	.irqs			= sm5502_irqs,
	.num_irqs		= ARRAY_SIZE(sm5502_irqs),
};

/* Define regmap configuration of SM5502 for I2C communication  */
static bool sm5502_muic_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SM5502_REG_INTMASK1:
	case SM5502_REG_INTMASK2:
		return true;
	default:
		break;
	}
	return false;
}

static const struct regmap_config sm5502_muic_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.volatile_reg	= sm5502_muic_volatile_reg,
	.max_register	= SM5502_REG_END,
};

/* Change DM_CON/DP_CON/VBUSIN switch according to cable type */
static int sm5502_muic_set_path(struct sm5502_muic_info *info,
				unsigned int con_sw, unsigned int vbus_sw,
				bool attached)
{
	int ret;

	if (!attached) {
		con_sw	= DM_DP_SWITCH_OPEN;
		vbus_sw	= VBUSIN_SWITCH_OPEN;
	}

	switch (con_sw) {
	case DM_DP_SWITCH_OPEN:
	case DM_DP_SWITCH_USB:
	case DM_DP_SWITCH_AUDIO:
	case DM_DP_SWITCH_UART:
		ret = regmap_update_bits(info->regmap, SM5502_REG_MANUAL_SW1,
					 SM5502_REG_MANUAL_SW1_DP_MASK |
					 SM5502_REG_MANUAL_SW1_DM_MASK,
					 con_sw);
		if (ret < 0) {
			dev_err(info->dev,
				"cannot update DM_CON/DP_CON switch\n");
			return ret;
		}
		break;
	default:
		dev_err(info->dev, "Unknown DM_CON/DP_CON switch type (%d)\n",
				con_sw);
		return -EINVAL;
	};

	switch (vbus_sw) {
	case VBUSIN_SWITCH_OPEN:
	case VBUSIN_SWITCH_VBUSOUT:
	case VBUSIN_SWITCH_MIC:
	case VBUSIN_SWITCH_VBUSOUT_WITH_USB:
		ret = regmap_update_bits(info->regmap, SM5502_REG_MANUAL_SW1,
					 SM5502_REG_MANUAL_SW1_VBUSIN_MASK,
					 vbus_sw);
		if (ret < 0) {
			dev_err(info->dev,
				"cannot update VBUSIN switch\n");
			return ret;
		}
		break;
	default:
		dev_err(info->dev, "Unknown VBUS switch type (%d)\n", vbus_sw);
		return -EINVAL;
	};

	return 0;
}

/* Return cable type of attached or detached accessories */
static unsigned int sm5502_muic_get_cable_type(struct sm5502_muic_info *info)
{
	unsigned int cable_type = -1, adc, dev_type1;
	int ret;

	/* Read ADC value according to external cable or button */
	ret = regmap_read(info->regmap, SM5502_REG_ADC, &adc);
	if (ret) {
		dev_err(info->dev, "failed to read ADC register\n");
		return ret;
	}

	/*
	 * If ADC is SM5502_MUIC_ADC_GROUND(0x0), external cable hasn't
	 * connected with to MUIC device.
	 */
	cable_type = adc & SM5502_REG_ADC_MASK;
	if (cable_type == SM5502_MUIC_ADC_GROUND)
		return SM5502_MUIC_ADC_GROUND;

	switch (cable_type) {
	case SM5502_MUIC_ADC_GROUND:
	case SM5502_MUIC_ADC_SEND_END_BUTTON:
	case SM5502_MUIC_ADC_REMOTE_S1_BUTTON:
	case SM5502_MUIC_ADC_REMOTE_S2_BUTTON:
	case SM5502_MUIC_ADC_REMOTE_S3_BUTTON:
	case SM5502_MUIC_ADC_REMOTE_S4_BUTTON:
	case SM5502_MUIC_ADC_REMOTE_S5_BUTTON:
	case SM5502_MUIC_ADC_REMOTE_S6_BUTTON:
	case SM5502_MUIC_ADC_REMOTE_S7_BUTTON:
	case SM5502_MUIC_ADC_REMOTE_S8_BUTTON:
	case SM5502_MUIC_ADC_REMOTE_S9_BUTTON:
	case SM5502_MUIC_ADC_REMOTE_S10_BUTTON:
	case SM5502_MUIC_ADC_REMOTE_S11_BUTTON:
	case SM5502_MUIC_ADC_REMOTE_S12_BUTTON:
	case SM5502_MUIC_ADC_RESERVED_ACC_1:
	case SM5502_MUIC_ADC_RESERVED_ACC_2:
	case SM5502_MUIC_ADC_RESERVED_ACC_3:
	case SM5502_MUIC_ADC_RESERVED_ACC_4:
	case SM5502_MUIC_ADC_RESERVED_ACC_5:
	case SM5502_MUIC_ADC_AUDIO_TYPE2:
	case SM5502_MUIC_ADC_PHONE_POWERED_DEV:
	case SM5502_MUIC_ADC_TTY_CONVERTER:
	case SM5502_MUIC_ADC_UART_CABLE:
	case SM5502_MUIC_ADC_TYPE1_CHARGER:
	case SM5502_MUIC_ADC_FACTORY_MODE_BOOT_OFF_USB:
	case SM5502_MUIC_ADC_FACTORY_MODE_BOOT_ON_USB:
	case SM5502_MUIC_ADC_AUDIO_VIDEO_CABLE:
	case SM5502_MUIC_ADC_TYPE2_CHARGER:
	case SM5502_MUIC_ADC_FACTORY_MODE_BOOT_OFF_UART:
	case SM5502_MUIC_ADC_FACTORY_MODE_BOOT_ON_UART:
		break;
	case SM5502_MUIC_ADC_AUDIO_TYPE1:
		/*
		 * Check whether cable type is
		 * SM5502_MUIC_ADC_AUDIO_TYPE1_FULL_REMOTE
		 * or SM5502_MUIC_ADC_AUDIO_TYPE1_SEND_END
		 * by using Button event.
		 */
		break;
	case SM5502_MUIC_ADC_OPEN:
		ret = regmap_read(info->regmap, SM5502_REG_DEV_TYPE1,
				  &dev_type1);
		if (ret) {
			dev_err(info->dev, "failed to read DEV_TYPE1 reg\n");
			return ret;
		}

		switch (dev_type1) {
		case SM5502_REG_DEV_TYPE1_USB_SDP_MASK:
			cable_type = SM5502_MUIC_ADC_OPEN_USB;
			break;
		case SM5502_REG_DEV_TYPE1_DEDICATED_CHG_MASK:
			cable_type = SM5502_MUIC_ADC_OPEN_TA;
			break;
		case SM5502_REG_DEV_TYPE1_USB_OTG_MASK:
			cable_type = SM5502_MUIC_ADC_OPEN_USB_OTG;
			break;
		default:
			dev_dbg(info->dev,
				"cannot identify the cable type: adc(0x%x)\n",
				adc);
			return -EINVAL;
		};
		break;
	default:
		dev_err(info->dev,
			"failed to identify the cable type: adc(0x%x)\n", adc);
		return -EINVAL;
	};

	return cable_type;
}

static int sm5502_muic_cable_handler(struct sm5502_muic_info *info,
				     bool attached)
{
	static unsigned int prev_cable_type = SM5502_MUIC_ADC_GROUND;
	unsigned int cable_type = SM5502_MUIC_ADC_GROUND;
	unsigned int con_sw = DM_DP_SWITCH_OPEN;
	unsigned int vbus_sw = VBUSIN_SWITCH_OPEN;
	unsigned int id;
	int ret;

	/* Get the type of attached or detached cable */
	if (attached)
		cable_type = sm5502_muic_get_cable_type(info);
	else
		cable_type = prev_cable_type;
	prev_cable_type = cable_type;

	switch (cable_type) {
	case SM5502_MUIC_ADC_OPEN_USB:
		id	= EXTCON_USB;
		con_sw	= DM_DP_SWITCH_USB;
		vbus_sw	= VBUSIN_SWITCH_VBUSOUT_WITH_USB;
		break;
	case SM5502_MUIC_ADC_OPEN_TA:
		id	= EXTCON_CHG_USB_DCP;
		con_sw	= DM_DP_SWITCH_OPEN;
		vbus_sw	= VBUSIN_SWITCH_VBUSOUT;
		break;
	case SM5502_MUIC_ADC_OPEN_USB_OTG:
		id	= EXTCON_USB_HOST;
		con_sw	= DM_DP_SWITCH_USB;
		vbus_sw	= VBUSIN_SWITCH_OPEN;
		break;
	default:
		dev_dbg(info->dev,
			"cannot handle this cable_type (0x%x)\n", cable_type);
		return 0;
	};

	/* Change internal hardware path(DM_CON/DP_CON, VBUSIN) */
	ret = sm5502_muic_set_path(info, con_sw, vbus_sw, attached);
	if (ret < 0)
		return ret;

	/* Change the state of external accessory */
	extcon_set_state_sync(info->edev, id, attached);
	if (id == EXTCON_USB)
		extcon_set_state_sync(info->edev, EXTCON_CHG_USB_SDP,
					attached);

	return 0;
}

static void sm5502_muic_irq_work(struct work_struct *work)
{
	struct sm5502_muic_info *info = container_of(work,
			struct sm5502_muic_info, irq_work);
	int ret = 0;

	if (!info->edev)
		return;

	mutex_lock(&info->mutex);

	/* Detect attached or detached cables */
	if (info->irq_attach) {
		ret = sm5502_muic_cable_handler(info, true);
		info->irq_attach = false;
	}
	if (info->irq_detach) {
		ret = sm5502_muic_cable_handler(info, false);
		info->irq_detach = false;
	}

	if (ret < 0)
		dev_err(info->dev, "failed to handle MUIC interrupt\n");

	mutex_unlock(&info->mutex);
}

/*
 * Sets irq_attach or irq_detach in sm5502_muic_info and returns 0.
 * Returns -ESRCH if irq_type does not match registered IRQ for this dev type.
 */
static int sm5502_parse_irq(struct sm5502_muic_info *info, int irq_type)
{
	switch (irq_type) {
	case SM5502_IRQ_INT1_ATTACH:
		info->irq_attach = true;
		break;
	case SM5502_IRQ_INT1_DETACH:
		info->irq_detach = true;
		break;
	case SM5502_IRQ_INT1_KP:
	case SM5502_IRQ_INT1_LKP:
	case SM5502_IRQ_INT1_LKR:
	case SM5502_IRQ_INT1_OVP_EVENT:
	case SM5502_IRQ_INT1_OCP_EVENT:
	case SM5502_IRQ_INT1_OVP_OCP_DIS:
	case SM5502_IRQ_INT2_VBUS_DET:
	case SM5502_IRQ_INT2_REV_ACCE:
	case SM5502_IRQ_INT2_ADC_CHG:
	case SM5502_IRQ_INT2_STUCK_KEY:
	case SM5502_IRQ_INT2_STUCK_KEY_RCV:
	case SM5502_IRQ_INT2_MHL:
	default:
		break;
	}

	return 0;
}

static irqreturn_t sm5502_muic_irq_handler(int irq, void *data)
{
	struct sm5502_muic_info *info = data;
	int i, irq_type = -1, ret;

	for (i = 0; i < info->num_muic_irqs; i++)
		if (irq == info->muic_irqs[i].virq)
			irq_type = info->muic_irqs[i].irq;

	ret = sm5502_parse_irq(info, irq_type);
	if (ret < 0) {
		dev_warn(info->dev, "cannot handle is interrupt:%d\n",
				    irq_type);
		return IRQ_HANDLED;
	}
	schedule_work(&info->irq_work);

	return IRQ_HANDLED;
}

static void sm5502_muic_detect_cable_wq(struct work_struct *work)
{
	struct sm5502_muic_info *info = container_of(to_delayed_work(work),
				struct sm5502_muic_info, wq_detcable);
	int ret;

	/* Notify the state of connector cable or not  */
	ret = sm5502_muic_cable_handler(info, true);
	if (ret < 0)
		dev_warn(info->dev, "failed to detect cable state\n");
}

static void sm5502_init_dev_type(struct sm5502_muic_info *info)
{
	unsigned int reg_data, vendor_id, version_id;
	int i, ret;

	/* To test I2C, Print version_id and vendor_id of SM5502 */
	ret = regmap_read(info->regmap, SM5502_REG_DEVICE_ID, &reg_data);
	if (ret) {
		dev_err(info->dev,
			"failed to read DEVICE_ID register: %d\n", ret);
		return;
	}

	vendor_id = ((reg_data & SM5502_REG_DEVICE_ID_VENDOR_MASK) >>
				SM5502_REG_DEVICE_ID_VENDOR_SHIFT);
	version_id = ((reg_data & SM5502_REG_DEVICE_ID_VERSION_MASK) >>
				SM5502_REG_DEVICE_ID_VERSION_SHIFT);

	dev_info(info->dev, "Device type: version: 0x%x, vendor: 0x%x\n",
			    version_id, vendor_id);

	/* Initiazle the register of SM5502 device to bring-up */
	for (i = 0; i < info->num_reg_data; i++) {
		unsigned int val = 0;

		if (!info->reg_data[i].invert)
			val |= ~info->reg_data[i].val;
		else
			val = info->reg_data[i].val;
		regmap_write(info->regmap, info->reg_data[i].reg, val);
	}
}

static int sm5022_muic_i2c_probe(struct i2c_client *i2c,
				 const struct i2c_device_id *id)
{
	struct device_node *np = i2c->dev.of_node;
	struct sm5502_muic_info *info;
	int i, ret, irq_flags;

	if (!np)
		return -EINVAL;

	info = devm_kzalloc(&i2c->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	i2c_set_clientdata(i2c, info);

	info->dev = &i2c->dev;
	info->i2c = i2c;
	info->irq = i2c->irq;
	info->muic_irqs = sm5502_muic_irqs;
	info->num_muic_irqs = ARRAY_SIZE(sm5502_muic_irqs);
	info->reg_data = sm5502_reg_data;
	info->num_reg_data = ARRAY_SIZE(sm5502_reg_data);

	mutex_init(&info->mutex);

	INIT_WORK(&info->irq_work, sm5502_muic_irq_work);

	info->regmap = devm_regmap_init_i2c(i2c, &sm5502_muic_regmap_config);
	if (IS_ERR(info->regmap)) {
		ret = PTR_ERR(info->regmap);
		dev_err(info->dev, "failed to allocate register map: %d\n",
				   ret);
		return ret;
	}

	/* Support irq domain for SM5502 MUIC device */
	irq_flags = IRQF_TRIGGER_FALLING | IRQF_ONESHOT | IRQF_SHARED;
	ret = regmap_add_irq_chip(info->regmap, info->irq, irq_flags, 0,
				  &sm5502_muic_irq_chip, &info->irq_data);
	if (ret != 0) {
		dev_err(info->dev, "failed to request IRQ %d: %d\n",
				    info->irq, ret);
		return ret;
	}

	for (i = 0; i < info->num_muic_irqs; i++) {
		struct muic_irq *muic_irq = &info->muic_irqs[i];
		int virq = 0;

		virq = regmap_irq_get_virq(info->irq_data, muic_irq->irq);
		if (virq <= 0)
			return -EINVAL;
		muic_irq->virq = virq;

		ret = devm_request_threaded_irq(info->dev, virq, NULL,
						sm5502_muic_irq_handler,
						IRQF_NO_SUSPEND,
						muic_irq->name, info);
		if (ret) {
			dev_err(info->dev,
				"failed: irq request (IRQ: %d, error :%d)\n",
				muic_irq->irq, ret);
			return ret;
		}
	}

	/* Allocate extcon device */
	info->edev = devm_extcon_dev_allocate(info->dev, sm5502_extcon_cable);
	if (IS_ERR(info->edev)) {
		dev_err(info->dev, "failed to allocate memory for extcon\n");
		return -ENOMEM;
	}

	/* Register extcon device */
	ret = devm_extcon_dev_register(info->dev, info->edev);
	if (ret) {
		dev_err(info->dev, "failed to register extcon device\n");
		return ret;
	}

	/*
	 * Detect accessory after completing the initialization of platform
	 *
	 * - Use delayed workqueue to detect cable state and then
	 * notify cable state to notifiee/platform through uevent.
	 * After completing the booting of platform, the extcon provider
	 * driver should notify cable state to upper layer.
	 */
	INIT_DELAYED_WORK(&info->wq_detcable, sm5502_muic_detect_cable_wq);
	queue_delayed_work(system_power_efficient_wq, &info->wq_detcable,
			msecs_to_jiffies(DELAY_MS_DEFAULT));

	/* Initialize SM5502 device and print vendor id and version id */
	sm5502_init_dev_type(info);

	return 0;
}

static int sm5502_muic_i2c_remove(struct i2c_client *i2c)
{
	struct sm5502_muic_info *info = i2c_get_clientdata(i2c);

	regmap_del_irq_chip(info->irq, info->irq_data);

	return 0;
}

static const struct of_device_id sm5502_dt_match[] = {
	{ .compatible = "siliconmitus,sm5502-muic" },
	{ },
};
MODULE_DEVICE_TABLE(of, sm5502_dt_match);

#ifdef CONFIG_PM_SLEEP
static int sm5502_muic_suspend(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	struct sm5502_muic_info *info = i2c_get_clientdata(i2c);

	enable_irq_wake(info->irq);

	return 0;
}

static int sm5502_muic_resume(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	struct sm5502_muic_info *info = i2c_get_clientdata(i2c);

	disable_irq_wake(info->irq);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(sm5502_muic_pm_ops,
			 sm5502_muic_suspend, sm5502_muic_resume);

static const struct i2c_device_id sm5502_i2c_id[] = {
	{ "sm5502", TYPE_SM5502 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sm5502_i2c_id);

static struct i2c_driver sm5502_muic_i2c_driver = {
	.driver		= {
		.name	= "sm5502",
		.pm	= &sm5502_muic_pm_ops,
		.of_match_table = sm5502_dt_match,
	},
	.probe	= sm5022_muic_i2c_probe,
	.remove	= sm5502_muic_i2c_remove,
	.id_table = sm5502_i2c_id,
};

static int __init sm5502_muic_i2c_init(void)
{
	return i2c_add_driver(&sm5502_muic_i2c_driver);
}
subsys_initcall(sm5502_muic_i2c_init);

MODULE_DESCRIPTION("Silicon Mitus SM5502 Extcon driver");
MODULE_AUTHOR("Chanwoo Choi <cw00.choi@samsung.com>");
MODULE_LICENSE("GPL");
