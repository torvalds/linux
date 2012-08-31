/*
 * Driver for LP8727 Micro/Mini USB IC with integrated charger
 *
 *			Copyright (C) 2011 Texas Instruments
 *			Copyright (C) 2011 National Semiconductor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/power_supply.h>
#include <linux/platform_data/lp8727.h>

#define LP8788_NUM_INTREGS	2
#define DEFAULT_DEBOUNCE_MSEC	270

/* Registers */
#define LP8727_CTRL1		0x1
#define LP8727_CTRL2		0x2
#define LP8727_SWCTRL		0x3
#define LP8727_INT1		0x4
#define LP8727_INT2		0x5
#define LP8727_STATUS1		0x6
#define LP8727_STATUS2		0x7
#define LP8727_CHGCTRL2		0x9

/* CTRL1 register */
#define LP8727_CP_EN		BIT(0)
#define LP8727_ADC_EN		BIT(1)
#define LP8727_ID200_EN		BIT(4)

/* CTRL2 register */
#define LP8727_CHGDET_EN	BIT(1)
#define LP8727_INT_EN		BIT(6)

/* SWCTRL register */
#define LP8727_SW_DM1_DM	(0x0 << 0)
#define LP8727_SW_DM1_HiZ	(0x7 << 0)
#define LP8727_SW_DP2_DP	(0x0 << 3)
#define LP8727_SW_DP2_HiZ	(0x7 << 3)

/* INT1 register */
#define LP8727_IDNO		(0xF << 0)
#define LP8727_VBUS		BIT(4)

/* STATUS1 register */
#define LP8727_CHGSTAT		(3 << 4)
#define LP8727_CHPORT		BIT(6)
#define LP8727_DCPORT		BIT(7)
#define LP8727_STAT_EOC		0x30

/* STATUS2 register */
#define LP8727_TEMP_STAT	(3 << 5)
#define LP8727_TEMP_SHIFT	5

/* CHGCTRL2 register */
#define LP8727_ICHG_SHIFT	4

enum lp8727_dev_id {
	LP8727_ID_NONE,
	LP8727_ID_TA,
	LP8727_ID_DEDICATED_CHG,
	LP8727_ID_USB_CHG,
	LP8727_ID_USB_DS,
	LP8727_ID_MAX,
};

enum lp8727_die_temp {
	LP8788_TEMP_75C,
	LP8788_TEMP_95C,
	LP8788_TEMP_115C,
	LP8788_TEMP_135C,
};

struct lp8727_psy {
	struct power_supply ac;
	struct power_supply usb;
	struct power_supply batt;
};

struct lp8727_chg {
	struct device *dev;
	struct i2c_client *client;
	struct mutex xfer_lock;
	struct delayed_work work;
	struct lp8727_platform_data *pdata;
	struct lp8727_psy *psy;
	struct lp8727_chg_param *chg_parm;
	enum lp8727_dev_id devid;
	unsigned long debounce_jiffies;
	int irq;
};

static int lp8727_read_bytes(struct lp8727_chg *pchg, u8 reg, u8 *data, u8 len)
{
	s32 ret;

	mutex_lock(&pchg->xfer_lock);
	ret = i2c_smbus_read_i2c_block_data(pchg->client, reg, len, data);
	mutex_unlock(&pchg->xfer_lock);

	return (ret != len) ? -EIO : 0;
}

static inline int lp8727_read_byte(struct lp8727_chg *pchg, u8 reg, u8 *data)
{
	return lp8727_read_bytes(pchg, reg, data, 1);
}

static int lp8727_write_byte(struct lp8727_chg *pchg, u8 reg, u8 data)
{
	int ret;

	mutex_lock(&pchg->xfer_lock);
	ret = i2c_smbus_write_byte_data(pchg->client, reg, data);
	mutex_unlock(&pchg->xfer_lock);

	return ret;
}

static int lp8727_is_charger_attached(const char *name, int id)
{
	if (name) {
		if (!strcmp(name, "ac"))
			return (id == LP8727_ID_TA || id == LP8727_ID_DEDICATED_CHG) ? 1 : 0;
		else if (!strcmp(name, "usb"))
			return (id == LP8727_ID_USB_CHG) ? 1 : 0;
	}

	return (id >= LP8727_ID_TA && id <= LP8727_ID_USB_CHG) ? 1 : 0;
}

static int lp8727_init_device(struct lp8727_chg *pchg)
{
	u8 val;
	int ret;
	u8 intstat[LP8788_NUM_INTREGS];

	/* clear interrupts */
	ret = lp8727_read_bytes(pchg, LP8727_INT1, intstat, LP8788_NUM_INTREGS);
	if (ret)
		return ret;


	val = LP8727_ID200_EN | LP8727_ADC_EN | LP8727_CP_EN;
	ret = lp8727_write_byte(pchg, LP8727_CTRL1, val);
	if (ret)
		return ret;

	val = LP8727_INT_EN | LP8727_CHGDET_EN;
	ret = lp8727_write_byte(pchg, LP8727_CTRL2, val);
	if (ret)
		return ret;

	return 0;
}

static int lp8727_is_dedicated_charger(struct lp8727_chg *pchg)
{
	u8 val;
	lp8727_read_byte(pchg, LP8727_STATUS1, &val);
	return val & LP8727_DCPORT;
}

static int lp8727_is_usb_charger(struct lp8727_chg *pchg)
{
	u8 val;
	lp8727_read_byte(pchg, LP8727_STATUS1, &val);
	return val & LP8727_CHPORT;
}

static void lp8727_ctrl_switch(struct lp8727_chg *pchg, u8 sw)
{
	lp8727_write_byte(pchg, LP8727_SWCTRL, sw);
}

static void lp8727_id_detection(struct lp8727_chg *pchg, u8 id, int vbusin)
{
	struct lp8727_platform_data *pdata = pchg->pdata;
	u8 devid = LP8727_ID_NONE;
	u8 swctrl = LP8727_SW_DM1_HiZ | LP8727_SW_DP2_HiZ;

	switch (id) {
	case 0x5:
		devid = LP8727_ID_TA;
		pchg->chg_parm = pdata ? pdata->ac : NULL;
		break;
	case 0xB:
		if (lp8727_is_dedicated_charger(pchg)) {
			pchg->chg_parm = pdata ? pdata->ac : NULL;
			devid = LP8727_ID_DEDICATED_CHG;
		} else if (lp8727_is_usb_charger(pchg)) {
			pchg->chg_parm = pdata ? pdata->usb : NULL;
			devid = LP8727_ID_USB_CHG;
			swctrl = LP8727_SW_DM1_DM | LP8727_SW_DP2_DP;
		} else if (vbusin) {
			devid = LP8727_ID_USB_DS;
			swctrl = LP8727_SW_DM1_DM | LP8727_SW_DP2_DP;
		}
		break;
	default:
		devid = LP8727_ID_NONE;
		pchg->chg_parm = NULL;
		break;
	}

	pchg->devid = devid;
	lp8727_ctrl_switch(pchg, swctrl);
}

static void lp8727_enable_chgdet(struct lp8727_chg *pchg)
{
	u8 val;

	lp8727_read_byte(pchg, LP8727_CTRL2, &val);
	val |= LP8727_CHGDET_EN;
	lp8727_write_byte(pchg, LP8727_CTRL2, val);
}

static void lp8727_delayed_func(struct work_struct *_work)
{
	u8 intstat[LP8788_NUM_INTREGS], idno, vbus;
	struct lp8727_chg *pchg =
	    container_of(_work, struct lp8727_chg, work.work);

	if (lp8727_read_bytes(pchg, LP8727_INT1, intstat, LP8788_NUM_INTREGS)) {
		dev_err(pchg->dev, "can not read INT registers\n");
		return;
	}

	idno = intstat[0] & LP8727_IDNO;
	vbus = intstat[0] & LP8727_VBUS;

	lp8727_id_detection(pchg, idno, vbus);
	lp8727_enable_chgdet(pchg);

	power_supply_changed(&pchg->psy->ac);
	power_supply_changed(&pchg->psy->usb);
	power_supply_changed(&pchg->psy->batt);
}

static irqreturn_t lp8727_isr_func(int irq, void *ptr)
{
	struct lp8727_chg *pchg = ptr;

	schedule_delayed_work(&pchg->work, pchg->debounce_jiffies);
	return IRQ_HANDLED;
}

static int lp8727_setup_irq(struct lp8727_chg *pchg)
{
	int ret;
	int irq = pchg->client->irq;
	unsigned delay_msec = pchg->pdata ? pchg->pdata->debounce_msec :
						DEFAULT_DEBOUNCE_MSEC;

	INIT_DELAYED_WORK(&pchg->work, lp8727_delayed_func);

	if (irq <= 0) {
		dev_warn(pchg->dev, "invalid irq number: %d\n", irq);
		return 0;
	}

	ret = request_threaded_irq(irq,	NULL, lp8727_isr_func,
				IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				"lp8727_irq", pchg);

	if (ret)
		return ret;

	pchg->irq = irq;
	pchg->debounce_jiffies = msecs_to_jiffies(delay_msec);

	return 0;
}

static void lp8727_release_irq(struct lp8727_chg *pchg)
{
	cancel_delayed_work_sync(&pchg->work);

	if (pchg->irq)
		free_irq(pchg->irq, pchg);
}

static enum power_supply_property lp8727_charger_prop[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static enum power_supply_property lp8727_battery_prop[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
};

static char *battery_supplied_to[] = {
	"main_batt",
};

static int lp8727_charger_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct lp8727_chg *pchg = dev_get_drvdata(psy->dev->parent);

	if (psp == POWER_SUPPLY_PROP_ONLINE)
		val->intval = lp8727_is_charger_attached(psy->name,
							 pchg->devid);

	return 0;
}

static bool lp8727_is_high_temperature(enum lp8727_die_temp temp)
{
	switch (temp) {
	case LP8788_TEMP_95C:
	case LP8788_TEMP_115C:
	case LP8788_TEMP_135C:
		return true;
	default:
		return false;
	}
}

static int lp8727_battery_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct lp8727_chg *pchg = dev_get_drvdata(psy->dev->parent);
	struct lp8727_platform_data *pdata = pchg->pdata;
	enum lp8727_die_temp temp;
	u8 read;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (lp8727_is_charger_attached(psy->name, pchg->devid)) {
			lp8727_read_byte(pchg, LP8727_STATUS1, &read);

			val->intval = (read & LP8727_CHGSTAT) == LP8727_STAT_EOC ?
				POWER_SUPPLY_STATUS_FULL :
				POWER_SUPPLY_STATUS_CHARGING;
		} else {
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		}
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		lp8727_read_byte(pchg, LP8727_STATUS2, &read);
		temp = (read & LP8727_TEMP_STAT) >> LP8727_TEMP_SHIFT;

		val->intval = lp8727_is_high_temperature(temp) ?
			POWER_SUPPLY_HEALTH_OVERHEAT :
			POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		if (!pdata)
			return -EINVAL;

		if (pdata->get_batt_present)
			val->intval = pchg->pdata->get_batt_present();
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (!pdata)
			return -EINVAL;

		if (pdata->get_batt_level)
			val->intval = pchg->pdata->get_batt_level();
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		if (!pdata)
			return -EINVAL;

		if (pdata->get_batt_capacity)
			val->intval = pchg->pdata->get_batt_capacity();
		break;
	case POWER_SUPPLY_PROP_TEMP:
		if (!pdata)
			return -EINVAL;

		if (pdata->get_batt_temp)
			val->intval = pchg->pdata->get_batt_temp();
		break;
	default:
		break;
	}

	return 0;
}

static void lp8727_charger_changed(struct power_supply *psy)
{
	struct lp8727_chg *pchg = dev_get_drvdata(psy->dev->parent);
	u8 val;
	u8 eoc_level, ichg;

	if (lp8727_is_charger_attached(psy->name, pchg->devid)) {
		if (pchg->chg_parm) {
			eoc_level = pchg->chg_parm->eoc_level;
			ichg = pchg->chg_parm->ichg;
			val = (ichg << LP8727_ICHG_SHIFT) | eoc_level;
			lp8727_write_byte(pchg, LP8727_CHGCTRL2, val);
		}
	}
}

static int lp8727_register_psy(struct lp8727_chg *pchg)
{
	struct lp8727_psy *psy;

	psy = devm_kzalloc(pchg->dev, sizeof(*psy), GFP_KERNEL);
	if (!psy)
		return -ENOMEM;

	pchg->psy = psy;

	psy->ac.name = "ac";
	psy->ac.type = POWER_SUPPLY_TYPE_MAINS;
	psy->ac.properties = lp8727_charger_prop;
	psy->ac.num_properties = ARRAY_SIZE(lp8727_charger_prop);
	psy->ac.get_property = lp8727_charger_get_property;
	psy->ac.supplied_to = battery_supplied_to;
	psy->ac.num_supplicants = ARRAY_SIZE(battery_supplied_to);

	if (power_supply_register(pchg->dev, &psy->ac))
		goto err_psy_ac;

	psy->usb.name = "usb";
	psy->usb.type = POWER_SUPPLY_TYPE_USB;
	psy->usb.properties = lp8727_charger_prop;
	psy->usb.num_properties = ARRAY_SIZE(lp8727_charger_prop);
	psy->usb.get_property = lp8727_charger_get_property;
	psy->usb.supplied_to = battery_supplied_to;
	psy->usb.num_supplicants = ARRAY_SIZE(battery_supplied_to);

	if (power_supply_register(pchg->dev, &psy->usb))
		goto err_psy_usb;

	psy->batt.name = "main_batt";
	psy->batt.type = POWER_SUPPLY_TYPE_BATTERY;
	psy->batt.properties = lp8727_battery_prop;
	psy->batt.num_properties = ARRAY_SIZE(lp8727_battery_prop);
	psy->batt.get_property = lp8727_battery_get_property;
	psy->batt.external_power_changed = lp8727_charger_changed;

	if (power_supply_register(pchg->dev, &psy->batt))
		goto err_psy_batt;

	return 0;

err_psy_batt:
	power_supply_unregister(&psy->usb);
err_psy_usb:
	power_supply_unregister(&psy->ac);
err_psy_ac:
	return -EPERM;
}

static void lp8727_unregister_psy(struct lp8727_chg *pchg)
{
	struct lp8727_psy *psy = pchg->psy;

	if (!psy)
		return;

	power_supply_unregister(&psy->ac);
	power_supply_unregister(&psy->usb);
	power_supply_unregister(&psy->batt);
}

static int lp8727_probe(struct i2c_client *cl, const struct i2c_device_id *id)
{
	struct lp8727_chg *pchg;
	int ret;

	if (!i2c_check_functionality(cl->adapter, I2C_FUNC_SMBUS_I2C_BLOCK))
		return -EIO;

	pchg = devm_kzalloc(&cl->dev, sizeof(*pchg), GFP_KERNEL);
	if (!pchg)
		return -ENOMEM;

	pchg->client = cl;
	pchg->dev = &cl->dev;
	pchg->pdata = cl->dev.platform_data;
	i2c_set_clientdata(cl, pchg);

	mutex_init(&pchg->xfer_lock);

	ret = lp8727_init_device(pchg);
	if (ret) {
		dev_err(pchg->dev, "i2c communication err: %d", ret);
		return ret;
	}

	ret = lp8727_register_psy(pchg);
	if (ret) {
		dev_err(pchg->dev, "power supplies register err: %d", ret);
		return ret;
	}

	ret = lp8727_setup_irq(pchg);
	if (ret) {
		dev_err(pchg->dev, "irq handler err: %d", ret);
		lp8727_unregister_psy(pchg);
		return ret;
	}

	return 0;
}

static int __devexit lp8727_remove(struct i2c_client *cl)
{
	struct lp8727_chg *pchg = i2c_get_clientdata(cl);

	lp8727_release_irq(pchg);
	lp8727_unregister_psy(pchg);
	return 0;
}

static const struct i2c_device_id lp8727_ids[] = {
	{"lp8727", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, lp8727_ids);

static struct i2c_driver lp8727_driver = {
	.driver = {
		   .name = "lp8727",
		   },
	.probe = lp8727_probe,
	.remove = __devexit_p(lp8727_remove),
	.id_table = lp8727_ids,
};
module_i2c_driver(lp8727_driver);

MODULE_DESCRIPTION("TI/National Semiconductor LP8727 charger driver");
MODULE_AUTHOR("Woogyom Kim <milo.kim@ti.com>, "
	      "Daniel Jeong <daniel.jeong@ti.com>");
MODULE_LICENSE("GPL");
