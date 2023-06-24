// SPDX-License-Identifier: GPL-2.0-only
/*
 * tusb1210.c - TUSB1210 USB ULPI PHY driver
 *
 * Copyright (C) 2015 Intel Corporation
 *
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 */
#include <linux/module.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/ulpi/driver.h>
#include <linux/ulpi/regs.h>
#include <linux/gpio/consumer.h>
#include <linux/phy/ulpi_phy.h>
#include <linux/power_supply.h>
#include <linux/workqueue.h>

#define TUSB1211_POWER_CONTROL				0x3d
#define TUSB1211_POWER_CONTROL_SET			0x3e
#define TUSB1211_POWER_CONTROL_CLEAR			0x3f
#define TUSB1211_POWER_CONTROL_SW_CONTROL		BIT(0)
#define TUSB1211_POWER_CONTROL_DET_COMP			BIT(1)
#define TUSB1211_POWER_CONTROL_DP_VSRC_EN		BIT(6)

#define TUSB1210_VENDOR_SPECIFIC2			0x80
#define TUSB1210_VENDOR_SPECIFIC2_IHSTX_MASK		GENMASK(3, 0)
#define TUSB1210_VENDOR_SPECIFIC2_ZHSDRV_MASK		GENMASK(5, 4)
#define TUSB1210_VENDOR_SPECIFIC2_DP_MASK		BIT(6)

#define TUSB1211_VENDOR_SPECIFIC3			0x85
#define TUSB1211_VENDOR_SPECIFIC3_SET			0x86
#define TUSB1211_VENDOR_SPECIFIC3_CLEAR			0x87
#define TUSB1211_VENDOR_SPECIFIC3_SW_USB_DET		BIT(4)
#define TUSB1211_VENDOR_SPECIFIC3_CHGD_IDP_SRC_EN	BIT(6)

#define TUSB1210_RESET_TIME_MS				50

#define TUSB1210_CHG_DET_MAX_RETRIES			5

/* TUSB1210 charger detection work states */
enum tusb1210_chg_det_state {
	TUSB1210_CHG_DET_CONNECTING,
	TUSB1210_CHG_DET_START_DET,
	TUSB1210_CHG_DET_READ_DET,
	TUSB1210_CHG_DET_FINISH_DET,
	TUSB1210_CHG_DET_CONNECTED,
	TUSB1210_CHG_DET_DISCONNECTING,
	TUSB1210_CHG_DET_DISCONNECTING_DONE,
	TUSB1210_CHG_DET_DISCONNECTED,
};

struct tusb1210 {
	struct ulpi *ulpi;
	struct phy *phy;
	struct gpio_desc *gpio_reset;
	struct gpio_desc *gpio_cs;
	u8 otg_ctrl;
	u8 vendor_specific2;
#ifdef CONFIG_POWER_SUPPLY
	enum power_supply_usb_type chg_type;
	enum tusb1210_chg_det_state chg_det_state;
	int chg_det_retries;
	struct delayed_work chg_det_work;
	struct notifier_block psy_nb;
	struct power_supply *psy;
	struct power_supply *charger;
#endif
};

static int tusb1210_ulpi_write(struct tusb1210 *tusb, u8 reg, u8 val)
{
	int ret;

	ret = ulpi_write(tusb->ulpi, reg, val);
	if (ret)
		dev_err(&tusb->ulpi->dev, "error %d writing val 0x%02x to reg 0x%02x\n",
			ret, val, reg);

	return ret;
}

static int tusb1210_ulpi_read(struct tusb1210 *tusb, u8 reg, u8 *val)
{
	int ret;

	ret = ulpi_read(tusb->ulpi, reg);
	if (ret >= 0) {
		*val = ret;
		ret = 0;
	} else {
		dev_err(&tusb->ulpi->dev, "error %d reading reg 0x%02x\n", ret, reg);
	}

	return ret;
}

static int tusb1210_power_on(struct phy *phy)
{
	struct tusb1210 *tusb = phy_get_drvdata(phy);

	gpiod_set_value_cansleep(tusb->gpio_reset, 1);
	gpiod_set_value_cansleep(tusb->gpio_cs, 1);

	msleep(TUSB1210_RESET_TIME_MS);

	/* Restore the optional eye diagram optimization value */
	tusb1210_ulpi_write(tusb, TUSB1210_VENDOR_SPECIFIC2, tusb->vendor_specific2);

	return 0;
}

static int tusb1210_power_off(struct phy *phy)
{
	struct tusb1210 *tusb = phy_get_drvdata(phy);

	gpiod_set_value_cansleep(tusb->gpio_reset, 0);
	gpiod_set_value_cansleep(tusb->gpio_cs, 0);

	return 0;
}

static int tusb1210_set_mode(struct phy *phy, enum phy_mode mode, int submode)
{
	struct tusb1210 *tusb = phy_get_drvdata(phy);
	int ret;
	u8 reg;

	ret = tusb1210_ulpi_read(tusb, ULPI_OTG_CTRL, &reg);
	if (ret < 0)
		return ret;

	switch (mode) {
	case PHY_MODE_USB_HOST:
		reg |= (ULPI_OTG_CTRL_DRVVBUS_EXT
			| ULPI_OTG_CTRL_ID_PULLUP
			| ULPI_OTG_CTRL_DP_PULLDOWN
			| ULPI_OTG_CTRL_DM_PULLDOWN);
		tusb1210_ulpi_write(tusb, ULPI_OTG_CTRL, reg);
		reg |= ULPI_OTG_CTRL_DRVVBUS;
		break;
	case PHY_MODE_USB_DEVICE:
		reg &= ~(ULPI_OTG_CTRL_DRVVBUS
			 | ULPI_OTG_CTRL_DP_PULLDOWN
			 | ULPI_OTG_CTRL_DM_PULLDOWN);
		tusb1210_ulpi_write(tusb, ULPI_OTG_CTRL, reg);
		reg &= ~ULPI_OTG_CTRL_DRVVBUS_EXT;
		break;
	default:
		/* nothing */
		return 0;
	}

	tusb->otg_ctrl = reg;
	return tusb1210_ulpi_write(tusb, ULPI_OTG_CTRL, reg);
}

#ifdef CONFIG_POWER_SUPPLY
static const char * const tusb1210_chg_det_states[] = {
	"CHG_DET_CONNECTING",
	"CHG_DET_START_DET",
	"CHG_DET_READ_DET",
	"CHG_DET_FINISH_DET",
	"CHG_DET_CONNECTED",
	"CHG_DET_DISCONNECTING",
	"CHG_DET_DISCONNECTING_DONE",
	"CHG_DET_DISCONNECTED",
};

static void tusb1210_reset(struct tusb1210 *tusb)
{
	gpiod_set_value_cansleep(tusb->gpio_reset, 0);
	usleep_range(200, 500);
	gpiod_set_value_cansleep(tusb->gpio_reset, 1);
}

static void tusb1210_chg_det_set_type(struct tusb1210 *tusb,
				      enum power_supply_usb_type type)
{
	dev_dbg(&tusb->ulpi->dev, "charger type: %d\n", type);
	tusb->chg_type = type;
	tusb->chg_det_retries = 0;
	power_supply_changed(tusb->psy);
}

static void tusb1210_chg_det_set_state(struct tusb1210 *tusb,
				       enum tusb1210_chg_det_state new_state,
				       int delay_ms)
{
	if (delay_ms)
		dev_dbg(&tusb->ulpi->dev, "chg_det new state %s in %d ms\n",
			tusb1210_chg_det_states[new_state], delay_ms);

	tusb->chg_det_state = new_state;
	mod_delayed_work(system_long_wq, &tusb->chg_det_work,
			 msecs_to_jiffies(delay_ms));
}

static void tusb1210_chg_det_handle_ulpi_error(struct tusb1210 *tusb)
{
	tusb1210_reset(tusb);
	if (tusb->chg_det_retries < TUSB1210_CHG_DET_MAX_RETRIES) {
		tusb->chg_det_retries++;
		tusb1210_chg_det_set_state(tusb, TUSB1210_CHG_DET_START_DET,
					   TUSB1210_RESET_TIME_MS);
	} else {
		tusb1210_chg_det_set_state(tusb, TUSB1210_CHG_DET_FINISH_DET,
					   TUSB1210_RESET_TIME_MS);
	}
}

/*
 * Boards using a TUSB121x for charger-detection have 3 power_supply class devs:
 *
 * tusb1211-charger-detect(1) -> charger -> fuel-gauge
 *
 * To determine if an USB charger is connected to the board, the online prop of
 * the charger psy needs to be read. Since the tusb1211-charger-detect psy is
 * the start of the supplier -> supplied-to chain, power_supply_am_i_supplied()
 * cannot be used here.
 *
 * Instead, below is a list of the power_supply names of known chargers for
 * these boards and the charger psy is looked up by name from this list.
 *
 * (1) modelling the external USB charger
 */
static const char * const tusb1210_chargers[] = {
	"bq24190-charger",
};

static bool tusb1210_get_online(struct tusb1210 *tusb)
{
	union power_supply_propval val;
	int i;

	for (i = 0; i < ARRAY_SIZE(tusb1210_chargers) && !tusb->charger; i++)
		tusb->charger = power_supply_get_by_name(tusb1210_chargers[i]);

	if (!tusb->charger)
		return false;

	if (power_supply_get_property(tusb->charger, POWER_SUPPLY_PROP_ONLINE, &val))
		return false;

	return val.intval;
}

static void tusb1210_chg_det_work(struct work_struct *work)
{
	struct tusb1210 *tusb = container_of(work, struct tusb1210, chg_det_work.work);
	bool vbus_present = tusb1210_get_online(tusb);
	int ret;
	u8 val;

	dev_dbg(&tusb->ulpi->dev, "chg_det state %s vbus_present %d\n",
		tusb1210_chg_det_states[tusb->chg_det_state], vbus_present);

	switch (tusb->chg_det_state) {
	case TUSB1210_CHG_DET_CONNECTING:
		tusb->chg_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		tusb->chg_det_retries = 0;
		/* Power on USB controller for ulpi_read()/_write() */
		ret = pm_runtime_resume_and_get(tusb->ulpi->dev.parent);
		if (ret < 0) {
			dev_err(&tusb->ulpi->dev, "error %d runtime-resuming\n", ret);
			/* Should never happen, skip charger detection */
			tusb1210_chg_det_set_state(tusb, TUSB1210_CHG_DET_CONNECTED, 0);
			return;
		}
		tusb1210_chg_det_set_state(tusb, TUSB1210_CHG_DET_START_DET, 0);
		break;
	case TUSB1210_CHG_DET_START_DET:
		/*
		 * Use the builtin charger detection FSM to keep things simple.
		 * This only detects DCP / SDP. This is good enough for the few
		 * boards which actually rely on the phy for charger detection.
		 */
		mutex_lock(&tusb->phy->mutex);
		ret = tusb1210_ulpi_write(tusb, TUSB1211_VENDOR_SPECIFIC3_SET,
					  TUSB1211_VENDOR_SPECIFIC3_SW_USB_DET);
		mutex_unlock(&tusb->phy->mutex);
		if (ret) {
			tusb1210_chg_det_handle_ulpi_error(tusb);
			break;
		}

		/* Wait 400 ms for the charger detection FSM to finish */
		tusb1210_chg_det_set_state(tusb, TUSB1210_CHG_DET_READ_DET, 400);
		break;
	case TUSB1210_CHG_DET_READ_DET:
		mutex_lock(&tusb->phy->mutex);
		ret = tusb1210_ulpi_read(tusb, TUSB1211_POWER_CONTROL, &val);
		mutex_unlock(&tusb->phy->mutex);
		if (ret) {
			tusb1210_chg_det_handle_ulpi_error(tusb);
			break;
		}

		if (val & TUSB1211_POWER_CONTROL_DET_COMP)
			tusb1210_chg_det_set_type(tusb, POWER_SUPPLY_USB_TYPE_DCP);
		else
			tusb1210_chg_det_set_type(tusb, POWER_SUPPLY_USB_TYPE_SDP);

		tusb1210_chg_det_set_state(tusb, TUSB1210_CHG_DET_FINISH_DET, 0);
		break;
	case TUSB1210_CHG_DET_FINISH_DET:
		mutex_lock(&tusb->phy->mutex);

		/* Set SW_CONTROL to stop the charger-det FSM */
		ret = tusb1210_ulpi_write(tusb, TUSB1211_POWER_CONTROL_SET,
					  TUSB1211_POWER_CONTROL_SW_CONTROL);

		/* Clear DP_VSRC_EN which may have been enabled by the charger-det FSM */
		ret |= tusb1210_ulpi_write(tusb, TUSB1211_POWER_CONTROL_CLEAR,
					   TUSB1211_POWER_CONTROL_DP_VSRC_EN);

		/* Clear CHGD_IDP_SRC_EN (may have been enabled by the charger-det FSM) */
		ret |= tusb1210_ulpi_write(tusb, TUSB1211_VENDOR_SPECIFIC3_CLEAR,
					   TUSB1211_VENDOR_SPECIFIC3_CHGD_IDP_SRC_EN);

		/* If any of the above fails reset the phy */
		if (ret) {
			tusb1210_reset(tusb);
			msleep(TUSB1210_RESET_TIME_MS);
		}

		/* Restore phy-parameters and OTG_CTRL register */
		tusb1210_ulpi_write(tusb, ULPI_OTG_CTRL, tusb->otg_ctrl);
		tusb1210_ulpi_write(tusb, TUSB1210_VENDOR_SPECIFIC2,
				    tusb->vendor_specific2);

		mutex_unlock(&tusb->phy->mutex);

		pm_runtime_put(tusb->ulpi->dev.parent);
		tusb1210_chg_det_set_state(tusb, TUSB1210_CHG_DET_CONNECTED, 0);
		break;
	case TUSB1210_CHG_DET_CONNECTED:
		if (!vbus_present)
			tusb1210_chg_det_set_state(tusb, TUSB1210_CHG_DET_DISCONNECTING, 0);
		break;
	case TUSB1210_CHG_DET_DISCONNECTING:
		/*
		 * The phy seems to take approx. 600ms longer then the charger
		 * chip (which is used to get vbus_present) to determine Vbus
		 * session end. Wait 800ms to ensure the phy has detected and
		 * signalled Vbus session end.
		 */
		tusb1210_chg_det_set_state(tusb, TUSB1210_CHG_DET_DISCONNECTING_DONE, 800);
		break;
	case TUSB1210_CHG_DET_DISCONNECTING_DONE:
		/*
		 * The phy often stops reacting to ulpi_read()/_write requests
		 * after a Vbus-session end. Reset it to work around this.
		 */
		tusb1210_reset(tusb);
		tusb1210_chg_det_set_type(tusb, POWER_SUPPLY_USB_TYPE_UNKNOWN);
		tusb1210_chg_det_set_state(tusb, TUSB1210_CHG_DET_DISCONNECTED, 0);
		break;
	case TUSB1210_CHG_DET_DISCONNECTED:
		if (vbus_present)
			tusb1210_chg_det_set_state(tusb, TUSB1210_CHG_DET_CONNECTING, 0);
		break;
	}
}

static int tusb1210_psy_notifier(struct notifier_block *nb,
	unsigned long event, void *ptr)
{
	struct tusb1210 *tusb = container_of(nb, struct tusb1210, psy_nb);
	struct power_supply *psy = ptr;

	if (psy != tusb->psy && psy->desc->type == POWER_SUPPLY_TYPE_USB)
		queue_delayed_work(system_long_wq, &tusb->chg_det_work, 0);

	return NOTIFY_OK;
}

static int tusb1210_psy_get_prop(struct power_supply *psy,
				 enum power_supply_property psp,
				 union power_supply_propval *val)
{
	struct tusb1210 *tusb = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = tusb1210_get_online(tusb);
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		val->intval = tusb->chg_type;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		if (tusb->chg_type == POWER_SUPPLY_USB_TYPE_DCP)
			val->intval = 2000000;
		else
			val->intval = 500000;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const enum power_supply_usb_type tusb1210_psy_usb_types[] = {
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
};

static const enum power_supply_property tusb1210_psy_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
};

static const struct power_supply_desc tusb1210_psy_desc = {
	.name = "tusb1211-charger-detect",
	.type = POWER_SUPPLY_TYPE_USB,
	.usb_types = tusb1210_psy_usb_types,
	.num_usb_types = ARRAY_SIZE(tusb1210_psy_usb_types),
	.properties = tusb1210_psy_props,
	.num_properties = ARRAY_SIZE(tusb1210_psy_props),
	.get_property = tusb1210_psy_get_prop,
};

/* Setup charger detection if requested, on errors continue without chg-det */
static void tusb1210_probe_charger_detect(struct tusb1210 *tusb)
{
	struct power_supply_config psy_cfg = { .drv_data = tusb };
	struct device *dev = &tusb->ulpi->dev;
	int ret;

	if (!device_property_read_bool(dev->parent, "linux,phy_charger_detect"))
		return;

	if (tusb->ulpi->id.product != 0x1508) {
		dev_err(dev, "error charger detection is only supported on the TUSB1211\n");
		return;
	}

	ret = tusb1210_ulpi_read(tusb, ULPI_OTG_CTRL, &tusb->otg_ctrl);
	if (ret)
		return;

	tusb->psy = power_supply_register(dev, &tusb1210_psy_desc, &psy_cfg);
	if (IS_ERR(tusb->psy))
		return;

	/*
	 * Delay initial run by 2 seconds to allow the charger driver,
	 * which is used to determine vbus_present, to load.
	 */
	tusb->chg_det_state = TUSB1210_CHG_DET_DISCONNECTED;
	INIT_DELAYED_WORK(&tusb->chg_det_work, tusb1210_chg_det_work);
	queue_delayed_work(system_long_wq, &tusb->chg_det_work, 2 * HZ);

	tusb->psy_nb.notifier_call = tusb1210_psy_notifier;
	power_supply_reg_notifier(&tusb->psy_nb);
}

static void tusb1210_remove_charger_detect(struct tusb1210 *tusb)
{

	if (!IS_ERR_OR_NULL(tusb->psy)) {
		power_supply_unreg_notifier(&tusb->psy_nb);
		cancel_delayed_work_sync(&tusb->chg_det_work);
		power_supply_unregister(tusb->psy);
	}

	if (tusb->charger)
		power_supply_put(tusb->charger);
}
#else
static void tusb1210_probe_charger_detect(struct tusb1210 *tusb) { }
static void tusb1210_remove_charger_detect(struct tusb1210 *tusb) { }
#endif

static const struct phy_ops phy_ops = {
	.power_on = tusb1210_power_on,
	.power_off = tusb1210_power_off,
	.set_mode = tusb1210_set_mode,
	.owner = THIS_MODULE,
};

static int tusb1210_probe(struct ulpi *ulpi)
{
	struct tusb1210 *tusb;
	u8 val, reg;
	int ret;

	tusb = devm_kzalloc(&ulpi->dev, sizeof(*tusb), GFP_KERNEL);
	if (!tusb)
		return -ENOMEM;

	tusb->ulpi = ulpi;

	tusb->gpio_reset = devm_gpiod_get_optional(&ulpi->dev, "reset",
						   GPIOD_OUT_LOW);
	if (IS_ERR(tusb->gpio_reset))
		return PTR_ERR(tusb->gpio_reset);

	gpiod_set_value_cansleep(tusb->gpio_reset, 1);

	tusb->gpio_cs = devm_gpiod_get_optional(&ulpi->dev, "cs",
						GPIOD_OUT_LOW);
	if (IS_ERR(tusb->gpio_cs))
		return PTR_ERR(tusb->gpio_cs);

	gpiod_set_value_cansleep(tusb->gpio_cs, 1);

	/*
	 * VENDOR_SPECIFIC2 register in TUSB1210 can be used for configuring eye
	 * diagram optimization and DP/DM swap.
	 */

	ret = tusb1210_ulpi_read(tusb, TUSB1210_VENDOR_SPECIFIC2, &reg);
	if (ret)
		return ret;

	/* High speed output drive strength configuration */
	if (!device_property_read_u8(&ulpi->dev, "ihstx", &val))
		u8p_replace_bits(&reg, val, (u8)TUSB1210_VENDOR_SPECIFIC2_IHSTX_MASK);

	/* High speed output impedance configuration */
	if (!device_property_read_u8(&ulpi->dev, "zhsdrv", &val))
		u8p_replace_bits(&reg, val, (u8)TUSB1210_VENDOR_SPECIFIC2_ZHSDRV_MASK);

	/* DP/DM swap control */
	if (!device_property_read_u8(&ulpi->dev, "datapolarity", &val))
		u8p_replace_bits(&reg, val, (u8)TUSB1210_VENDOR_SPECIFIC2_DP_MASK);

	ret = tusb1210_ulpi_write(tusb, TUSB1210_VENDOR_SPECIFIC2, reg);
	if (ret)
		return ret;

	tusb->vendor_specific2 = reg;

	tusb1210_probe_charger_detect(tusb);

	tusb->phy = ulpi_phy_create(ulpi, &phy_ops);
	if (IS_ERR(tusb->phy)) {
		ret = PTR_ERR(tusb->phy);
		goto err_remove_charger;
	}

	phy_set_drvdata(tusb->phy, tusb);
	ulpi_set_drvdata(ulpi, tusb);
	return 0;

err_remove_charger:
	tusb1210_remove_charger_detect(tusb);
	return ret;
}

static void tusb1210_remove(struct ulpi *ulpi)
{
	struct tusb1210 *tusb = ulpi_get_drvdata(ulpi);

	ulpi_phy_destroy(ulpi, tusb->phy);
	tusb1210_remove_charger_detect(tusb);
}

#define TI_VENDOR_ID 0x0451

static const struct ulpi_device_id tusb1210_ulpi_id[] = {
	{ TI_VENDOR_ID, 0x1507, },  /* TUSB1210 */
	{ TI_VENDOR_ID, 0x1508, },  /* TUSB1211 */
	{ },
};
MODULE_DEVICE_TABLE(ulpi, tusb1210_ulpi_id);

static struct ulpi_driver tusb1210_driver = {
	.id_table = tusb1210_ulpi_id,
	.probe = tusb1210_probe,
	.remove = tusb1210_remove,
	.driver = {
		.name = "tusb1210",
		.owner = THIS_MODULE,
	},
};

module_ulpi_driver(tusb1210_driver);

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("TUSB1210 ULPI PHY driver");
