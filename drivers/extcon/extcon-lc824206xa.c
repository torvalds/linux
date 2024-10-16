// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ON Semiconductor LC824206XA Micro USB Switch driver
 *
 * Copyright (c) 2024 Hans de Goede <hansg@kernel.org>
 *
 * ON Semiconductor has an "Advance Information" datasheet available
 * (ENA2222-D.PDF), but no full datasheet. So there is no documentation
 * available for the registers.
 *
 * This driver is based on the register info from the extcon-fsa9285.c driver,
 * from the Lollipop Android sources for the Lenovo Yoga Tablet 2 (Pro)
 * 830 / 1050 / 1380 models. Note despite the name this is actually a driver
 * for the LC824206XA not the FSA9285. The Android sources can be downloaded
 * from Lenovo's support page for these tablets, filename:
 * yoga_tab_2_osc_android_to_lollipop_201505.rar.
 */

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/extcon-provider.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>

/*
 * Register defines as mentioned above there is no datasheet with register
 * info, so this may not be 100% accurate.
 */
#define REG00				0x00
#define REG00_INIT_VALUE		0x01

#define REG_STATUS			0x01
#define STATUS_OVP			BIT(0)
#define STATUS_DATA_SHORT		BIT(1)
#define STATUS_VBUS_PRESENT		BIT(2)
#define STATUS_USB_ID			GENMASK(7, 3)
#define STATUS_USB_ID_GND		0x80
#define STATUS_USB_ID_ACA		0xf0
#define STATUS_USB_ID_FLOAT		0xf8

/*
 * This controls the DP/DM muxes + other switches,
 * meaning of individual bits is unknown.
 */
#define REG_SWITCH_CONTROL		0x02
#define SWITCH_STEREO_MIC		0xc8
#define SWITCH_USB_HOST			0xec
#define SWITCH_DISCONNECTED		0xf8
#define SWITCH_USB_DEVICE		0xfc

/* 5 bits? ADC 0x10 GND, 0x1a-0x1f ACA, 0x1f float */
#define REG_ID_PIN_ADC_VALUE		0x03

/* Masks for all 3 interrupt registers */
#define INTR_ID_PIN_CHANGE		BIT(0)
#define INTR_VBUS_CHANGE		BIT(1)
/* Both of these get set after a continuous mode ADC conversion */
#define INTR_ID_PIN_ADC_INT1		BIT(2)
#define INTR_ID_PIN_ADC_INT2		BIT(3)
/* Charger type available in reg 0x09 */
#define INTR_CHARGER_DET_DONE		BIT(4)
#define INTR_OVP			BIT(5)

/* There are 7 interrupt sources, bit 6 use is unknown (OCP?) */
#define INTR_ALL			GENMASK(6, 0)

/* Unmask interrupts this driver cares about */
#define INTR_MASK \
	(INTR_ALL & ~(INTR_ID_PIN_CHANGE | INTR_VBUS_CHANGE | INTR_CHARGER_DET_DONE))

/* Active (event happened and not cleared yet) interrupts */
#define REG_INTR_STATUS			0x04

/*
 * Writing a 1 to a bit here clears it in INTR_STATUS. These bits do NOT
 * auto-reset to 0, so these must be set to 0 manually after clearing.
 */
#define REG_INTR_CLEAR			0x05

/* Interrupts which bit is set to 1 here will not raise the HW IRQ */
#define REG_INTR_MASK			0x06

/* ID pin ADC control, meaning of individual bits is unknown */
#define REG_ID_PIN_ADC_CTRL		0x07
#define ID_PIN_ADC_AUTO			0x40
#define ID_PIN_ADC_CONTINUOUS		0x44

#define REG_CHARGER_DET			0x08
#define CHARGER_DET_ON			BIT(0)
#define CHARGER_DET_CDP_ON		BIT(1)
#define CHARGER_DET_CDP_VAL		BIT(2)

#define REG_CHARGER_TYPE		0x09
#define CHARGER_TYPE_UNKNOWN		0x00
#define CHARGER_TYPE_DCP		0x01
#define CHARGER_TYPE_SDP_OR_CDP		0x04
#define CHARGER_TYPE_QC			0x06

#define REG10				0x10
#define REG10_INIT_VALUE		0x00

struct lc824206xa_data {
	struct work_struct work;
	struct i2c_client *client;
	struct extcon_dev *edev;
	struct power_supply *psy;
	struct regulator *vbus_boost;
	unsigned int usb_type;
	unsigned int cable;
	unsigned int previous_cable;
	u8 switch_control;
	u8 previous_switch_control;
	bool vbus_ok;
	bool vbus_boost_enabled;
	bool fastcharge_over_miclr;
};

static const unsigned int lc824206xa_cables[] = {
	EXTCON_USB_HOST,
	EXTCON_CHG_USB_SDP,
	EXTCON_CHG_USB_CDP,
	EXTCON_CHG_USB_DCP,
	EXTCON_CHG_USB_ACA,
	EXTCON_CHG_USB_FAST,
	EXTCON_NONE,
};

/* read/write reg helpers to add error logging to smbus byte functions */
static int lc824206xa_read_reg(struct lc824206xa_data *data, u8 reg)
{
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, reg);
	if (ret < 0)
		dev_err(&data->client->dev, "Error %d reading reg 0x%02x\n", ret, reg);

	return ret;
}

static int lc824206xa_write_reg(struct lc824206xa_data *data, u8 reg, u8 val)
{
	int ret;

	ret = i2c_smbus_write_byte_data(data->client, reg, val);
	if (ret < 0)
		dev_err(&data->client->dev, "Error %d writing reg 0x%02x\n", ret, reg);

	return ret;
}

static int lc824206xa_get_id(struct lc824206xa_data *data)
{
	int ret;

	ret = lc824206xa_write_reg(data, REG_ID_PIN_ADC_CTRL, ID_PIN_ADC_CONTINUOUS);
	if (ret)
		return ret;

	ret = lc824206xa_read_reg(data, REG_ID_PIN_ADC_VALUE);

	lc824206xa_write_reg(data, REG_ID_PIN_ADC_CTRL, ID_PIN_ADC_AUTO);

	return ret;
}

static void lc824206xa_set_vbus_boost(struct lc824206xa_data *data, bool enable)
{
	int ret;

	if (data->vbus_boost_enabled == enable)
		return;

	if (enable)
		ret = regulator_enable(data->vbus_boost);
	else
		ret = regulator_disable(data->vbus_boost);

	if (ret == 0)
		data->vbus_boost_enabled = enable;
	else
		dev_err(&data->client->dev, "Error updating Vbus boost regulator: %d\n", ret);
}

static void lc824206xa_charger_detect(struct lc824206xa_data *data)
{
	int charger_type, ret;

	charger_type = lc824206xa_read_reg(data, REG_CHARGER_TYPE);
	if (charger_type < 0)
		return;

	dev_dbg(&data->client->dev, "charger type 0x%02x\n", charger_type);

	switch (charger_type) {
	case CHARGER_TYPE_UNKNOWN:
		data->usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		/* Treat as SDP */
		data->cable = EXTCON_CHG_USB_SDP;
		data->switch_control = SWITCH_USB_DEVICE;
		break;
	case CHARGER_TYPE_SDP_OR_CDP:
		data->usb_type = POWER_SUPPLY_USB_TYPE_SDP;
		data->cable = EXTCON_CHG_USB_SDP;
		data->switch_control = SWITCH_USB_DEVICE;

		ret = lc824206xa_write_reg(data, REG_CHARGER_DET,
					   CHARGER_DET_CDP_ON | CHARGER_DET_ON);
		if (ret < 0)
			break;

		msleep(100);
		ret = lc824206xa_read_reg(data, REG_CHARGER_DET);
		if (ret >= 0 && (ret & CHARGER_DET_CDP_VAL)) {
			data->usb_type = POWER_SUPPLY_USB_TYPE_CDP;
			data->cable = EXTCON_CHG_USB_CDP;
		}

		lc824206xa_write_reg(data, REG_CHARGER_DET, CHARGER_DET_ON);
		break;
	case CHARGER_TYPE_DCP:
		data->usb_type = POWER_SUPPLY_USB_TYPE_DCP;
		data->cable = EXTCON_CHG_USB_DCP;
		if (data->fastcharge_over_miclr)
			data->switch_control = SWITCH_STEREO_MIC;
		else
			data->switch_control = SWITCH_DISCONNECTED;
		break;
	case CHARGER_TYPE_QC:
		data->usb_type = POWER_SUPPLY_USB_TYPE_DCP;
		data->cable = EXTCON_CHG_USB_DCP;
		data->switch_control = SWITCH_DISCONNECTED;
		break;
	default:
		dev_warn(&data->client->dev, "Unknown charger type: 0x%02x\n", charger_type);
		break;
	}
}

static void lc824206xa_work(struct work_struct *work)
{
	struct lc824206xa_data *data = container_of(work, struct lc824206xa_data, work);
	bool vbus_boost_enable = false;
	int status, id;

	status = lc824206xa_read_reg(data, REG_STATUS);
	if (status < 0)
		return;

	dev_dbg(&data->client->dev, "status 0x%02x\n", status);

	data->vbus_ok = (status & (STATUS_VBUS_PRESENT | STATUS_OVP)) == STATUS_VBUS_PRESENT;

	/* Read id pin ADC if necessary */
	switch (status & STATUS_USB_ID) {
	case STATUS_USB_ID_GND:
	case STATUS_USB_ID_FLOAT:
		break;
	default:
		/* Happens when the connector is inserted slowly, log at dbg level */
		dev_dbg(&data->client->dev, "Unknown status 0x%02x\n", status);
		fallthrough;
	case STATUS_USB_ID_ACA:
		id = lc824206xa_get_id(data);
		dev_dbg(&data->client->dev, "RID 0x%02x\n", id);
		switch (id) {
		case 0x10:
			status = STATUS_USB_ID_GND;
			break;
		case 0x18 ... 0x1e:
			status = STATUS_USB_ID_ACA;
			break;
		case 0x1f:
			status = STATUS_USB_ID_FLOAT;
			break;
		default:
			dev_warn(&data->client->dev, "Unknown RID 0x%02x\n", id);
			return;
		}
	}

	/* Check for out of spec OTG charging hubs, treat as ACA */
	if ((status & STATUS_USB_ID) == STATUS_USB_ID_GND &&
	    data->vbus_ok && !data->vbus_boost_enabled) {
		dev_info(&data->client->dev, "Out of spec USB host adapter with Vbus present, not enabling 5V output\n");
		status = STATUS_USB_ID_ACA;
	}

	switch (status & STATUS_USB_ID) {
	case STATUS_USB_ID_ACA:
		data->usb_type = POWER_SUPPLY_USB_TYPE_ACA;
		data->cable = EXTCON_CHG_USB_ACA;
		data->switch_control = SWITCH_USB_HOST;
		break;
	case STATUS_USB_ID_GND:
		data->usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		data->cable = EXTCON_USB_HOST;
		data->switch_control = SWITCH_USB_HOST;
		vbus_boost_enable = true;
		break;
	case STATUS_USB_ID_FLOAT:
		/* When fast charging with Vbus > 5V, OVP will be set */
		if (data->fastcharge_over_miclr &&
		    data->switch_control == SWITCH_STEREO_MIC &&
		    (status & STATUS_OVP)) {
			data->cable = EXTCON_CHG_USB_FAST;
			break;
		}

		if (data->vbus_ok) {
			lc824206xa_charger_detect(data);
		} else {
			data->usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
			data->cable = EXTCON_NONE;
			data->switch_control = SWITCH_DISCONNECTED;
		}
		break;
	}

	lc824206xa_set_vbus_boost(data, vbus_boost_enable);

	if (data->switch_control != data->previous_switch_control) {
		lc824206xa_write_reg(data, REG_SWITCH_CONTROL, data->switch_control);
		data->previous_switch_control = data->switch_control;
	}

	if (data->cable != data->previous_cable) {
		extcon_set_state_sync(data->edev, data->previous_cable, false);
		extcon_set_state_sync(data->edev, data->cable, true);
		data->previous_cable = data->cable;
	}

	power_supply_changed(data->psy);
}

static irqreturn_t lc824206xa_irq(int irq, void *_data)
{
	struct lc824206xa_data *data = _data;
	int intr_status;

	intr_status = lc824206xa_read_reg(data, REG_INTR_STATUS);
	if (intr_status < 0)
		intr_status = INTR_ALL; /* Should never happen, clear all */

	dev_dbg(&data->client->dev, "interrupt 0x%02x\n", intr_status);

	lc824206xa_write_reg(data, REG_INTR_CLEAR, intr_status);
	lc824206xa_write_reg(data, REG_INTR_CLEAR, 0);

	schedule_work(&data->work);
	return IRQ_HANDLED;
}

/*
 * Newer charger (power_supply) drivers expect the max input current to be
 * provided by a parent power_supply device for the charger chip.
 */
static int lc824206xa_psy_get_prop(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	struct lc824206xa_data *data = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = data->vbus_ok && !data->vbus_boost_enabled;
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		val->intval = data->usb_type;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		switch (data->usb_type) {
		case POWER_SUPPLY_USB_TYPE_DCP:
		case POWER_SUPPLY_USB_TYPE_ACA:
			val->intval = 2000000;
			break;
		case POWER_SUPPLY_USB_TYPE_CDP:
			val->intval = 1500000;
			break;
		default:
			val->intval = 500000;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const enum power_supply_property lc824206xa_psy_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
};

static const struct power_supply_desc lc824206xa_psy_desc = {
	.name = "lc824206xa-charger-detect",
	.type = POWER_SUPPLY_TYPE_USB,
	.usb_types = BIT(POWER_SUPPLY_USB_TYPE_SDP) |
		     BIT(POWER_SUPPLY_USB_TYPE_CDP) |
		     BIT(POWER_SUPPLY_USB_TYPE_DCP) |
		     BIT(POWER_SUPPLY_USB_TYPE_ACA) |
		     BIT(POWER_SUPPLY_USB_TYPE_UNKNOWN),
	.properties = lc824206xa_psy_props,
	.num_properties = ARRAY_SIZE(lc824206xa_psy_props),
	.get_property = lc824206xa_psy_get_prop,
};

static int lc824206xa_probe(struct i2c_client *client)
{
	struct power_supply_config psy_cfg = { };
	struct device *dev = &client->dev;
	struct lc824206xa_data *data;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	INIT_WORK(&data->work, lc824206xa_work);
	data->cable = EXTCON_NONE;
	data->previous_cable = EXTCON_NONE;
	data->usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
	/* Some designs use a custom fast-charge protocol over the mic L/R inputs */
	data->fastcharge_over_miclr =
		device_property_read_bool(dev, "onnn,enable-miclr-for-dcp");

	data->vbus_boost = devm_regulator_get(dev, "vbus");
	if (IS_ERR(data->vbus_boost))
		return dev_err_probe(dev, PTR_ERR(data->vbus_boost),
				     "getting regulator\n");

	/* Init */
	ret = lc824206xa_write_reg(data, REG00, REG00_INIT_VALUE);
	ret |= lc824206xa_write_reg(data, REG10, REG10_INIT_VALUE);
	msleep(100);
	ret |= lc824206xa_write_reg(data, REG_INTR_CLEAR, INTR_ALL);
	ret |= lc824206xa_write_reg(data, REG_INTR_CLEAR, 0);
	ret |= lc824206xa_write_reg(data, REG_INTR_MASK, INTR_MASK);
	ret |= lc824206xa_write_reg(data, REG_ID_PIN_ADC_CTRL, ID_PIN_ADC_AUTO);
	ret |= lc824206xa_write_reg(data, REG_CHARGER_DET, CHARGER_DET_ON);
	if (ret)
		return -EIO;

	/* Initialize extcon device */
	data->edev = devm_extcon_dev_allocate(dev, lc824206xa_cables);
	if (IS_ERR(data->edev))
		return PTR_ERR(data->edev);

	ret = devm_extcon_dev_register(dev, data->edev);
	if (ret)
		return dev_err_probe(dev, ret, "registering extcon device\n");

	psy_cfg.drv_data = data;
	data->psy = devm_power_supply_register(dev, &lc824206xa_psy_desc, &psy_cfg);
	if (IS_ERR(data->psy))
		return dev_err_probe(dev, PTR_ERR(data->psy), "registering power supply\n");

	ret = devm_request_threaded_irq(dev, client->irq, NULL, lc824206xa_irq,
					IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					KBUILD_MODNAME, data);
	if (ret)
		return dev_err_probe(dev, ret, "requesting IRQ\n");

	/* Sync initial state */
	schedule_work(&data->work);
	return 0;
}

static const struct i2c_device_id lc824206xa_i2c_ids[] = {
	{ "lc824206xa" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lc824206xa_i2c_ids);

static struct i2c_driver lc824206xa_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
	},
	.probe = lc824206xa_probe,
	.id_table = lc824206xa_i2c_ids,
};

module_i2c_driver(lc824206xa_driver);

MODULE_AUTHOR("Hans de Goede <hansg@kernel.org>");
MODULE_DESCRIPTION("LC824206XA Micro USB Switch driver");
MODULE_LICENSE("GPL");
