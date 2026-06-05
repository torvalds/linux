// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * max8903_charger.c - Maxim 8903 USB/Adapter Charger Driver
 *
 * Copyright (C) 2011 Samsung Electronics
 * MyungJoo Ham <myungjoo.ham@samsung.com>
 */

#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/platform_device.h>

/*
 * IUSB pin: hardcoded by silicon to 100 mA (low) / 500 mA (high).
 * MAX8903A/B/C/D/E/F/G/H/I datasheet, "Pin Description" table:
 *   "USB Current-Limit Set Input. Drive IUSB logic-low to set the
 *    USB current limit to 100mA. Drive IUSB logic-high to set the
 *    USB current limit to 500mA."
 * Not a board parameter - never DT-configurable.
 */
#define MAX8903_USB_CURRENT_LIMIT_LOW_UA	100000
#define MAX8903_USB_CURRENT_LIMIT_HIGH_UA	500000

struct max8903_current_limit_mapping {
	u32 limit_ua;		/* Current limit in microamps */
	u32 gpio_value;		/* GPIO bit pattern */
};

struct max8903_data {
	struct device *dev;
	struct power_supply *psy;
	struct power_supply_desc psy_desc;
	/*
	 * GPIOs
	 * chg, flt, dcm and usus are optional.
	 * dok or uok must be present.
	 * If dok is present, cen must be present.
	 */
	struct gpio_desc *cen; /* Charger Enable input */
	struct gpio_desc *dok; /* DC (Adapter) Power OK output */
	struct gpio_desc *uok; /* USB Power OK output */
	struct gpio_desc *chg; /* Charger status output */
	struct gpio_desc *flt; /* Fault output */
	struct gpio_desc *dcm; /* Current-Limit Mode input (1: DC, 2: USB) */
	struct gpio_desc *usus; /* USB Suspend Input (1: suspended) */

	/* DC current limit control (ISET pins) */
	struct gpio_descs *dc_current_limit_gpios;
	struct max8903_current_limit_mapping *dc_current_limit_map;
	u32 dc_current_limit_map_size;
	u32 dc_current_limit_ua;	/* Current setting in uA */

	/* USB current limit control (IUSB pin) */
	struct gpio_desc *usb_current_limit_gpio;
	u32 usb_current_limit_ua;	/* Current setting in uA */

	/*
	 * Serialises ta_in / usb_in updates against
	 * max8903_set_property() which steers the current-limit write to
	 * the DC or USB path based on which source is currently online.
	 * The IRQ handlers are requested with IRQF_ONESHOT (threaded), so
	 * a sleepable mutex is the right primitive in both contexts.
	 */
	struct mutex source_lock;
	bool fault;
	bool usb_in;
	bool ta_in;
};

static enum power_supply_property max8903_charger_props[] = {
	POWER_SUPPLY_PROP_STATUS, /* Charger status output */
	POWER_SUPPLY_PROP_ONLINE, /* External power source */
	POWER_SUPPLY_PROP_HEALTH, /* Fault or OK */
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, /* Input current limit */
};

static int max8903_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct max8903_data *data = power_supply_get_drvdata(psy);
	bool ta_in, usb_in;
	u32 dc_limit, usb_limit;

	/*
	 * Snapshot the source flags and current-limit settings under the
	 * source_lock that the IRQs (max8903_dcin / max8903_usbin) and
	 * max8903_set_property() take when updating them, so we never
	 * observe a torn pair of (source-online flag, current-limit ua).
	 * The gpiod_get_value() reads further down deliberately stay
	 * outside the lock — they hit the GPIO controller, not driver
	 * state, and the IRQs do not touch them under the lock either.
	 */
	mutex_lock(&data->source_lock);
	ta_in = data->ta_in;
	usb_in = data->usb_in;
	dc_limit = data->dc_current_limit_ua;
	usb_limit = data->usb_current_limit_ua;
	mutex_unlock(&data->source_lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
		if (data->chg) {
			if (gpiod_get_value(data->chg))
				/* CHG asserted */
				val->intval = POWER_SUPPLY_STATUS_CHARGING;
			else if (usb_in || ta_in)
				val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
			else
				val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		}
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = (ta_in || usb_in) ? 1 : 0;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		/*
		 * data->fault is a single bool toggled from one IRQ
		 * handler, so a torn read is not possible; no need to
		 * extend source_lock coverage here.
		 */
		val->intval = data->fault ? POWER_SUPPLY_HEALTH_UNSPEC_FAILURE
					  : POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		/*
		 * Hardware prioritises DC over USB - when ta_in is asserted
		 * the part draws from the DC input regardless of USB state.
		 * So always report the DC-side limit when DC is online, and
		 * refuse rather than silently fall back to the USB cap if
		 * the DC GPIOs are not configured - that would mis-describe
		 * the active source. Same policy applies in the set path.
		 */
		if (ta_in) {
			if (!data->dc_current_limit_gpios)
				return -ENODATA;
			val->intval = dc_limit;
		} else if (usb_in && data->usb_current_limit_gpio) {
			val->intval = usb_limit;
		} else {
			return -ENODATA;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int max8903_set_dc_current_limit(struct max8903_data *data, u32 limit_ua)
{
	int i, best_idx = -1;
	/*
	 * The mapping's gpio_value fits in the lowest ndescs bits of one
	 * unsigned long (parse_dc_current_limit enforces ndescs < 32 and
	 * gpio_value < BIT(ndescs)); a single-word bitmap is sufficient
	 * on both 32- and 64-bit builds. Don't use bitmap_from_arr32() -
	 * that macro reinterprets its source pointer as unsigned long on
	 * 64-bit and would read past the on-stack u32.
	 */
	DECLARE_BITMAP(values, BITS_PER_TYPE(u32));

	if (!data->dc_current_limit_gpios)
		return -EOPNOTSUPP;

	/*
	 * Find the highest supported current <= requested. Use a -1
	 * "not found" sentinel rather than tracking best_limit > 0 so
	 * that a 0 uA entry (used to disable charging) can be selected
	 * by a 0 uA request.
	 */
	for (i = 0; i < data->dc_current_limit_map_size; i++) {
		if (data->dc_current_limit_map[i].limit_ua > limit_ua)
			continue;
		if (best_idx < 0 ||
		    data->dc_current_limit_map[i].limit_ua >
				data->dc_current_limit_map[best_idx].limit_ua)
			best_idx = i;
	}

	if (best_idx < 0)
		return -EINVAL;

	bitmap_zero(values, BITS_PER_TYPE(u32));
	values[0] = data->dc_current_limit_map[best_idx].gpio_value;
	gpiod_set_array_value_cansleep(data->dc_current_limit_gpios->ndescs,
				       data->dc_current_limit_gpios->desc,
				       data->dc_current_limit_gpios->info,
				       values);

	data->dc_current_limit_ua = data->dc_current_limit_map[best_idx].limit_ua;
	dev_dbg(data->dev, "DC current limit set to %u uA\n",
		data->dc_current_limit_ua);

	return 0;
}

static int max8903_set_usb_current_limit(struct max8903_data *data, u32 limit_ua)
{
	u32 selected;
	int gpio_val;

	if (!data->usb_current_limit_gpio)
		return -EOPNOTSUPP;

	/*
	 * IUSB is a single-bit input with two silicon-fixed settings;
	 * pick HIGH (500 mA) iff the caller's cap can absorb it, else
	 * LOW (100 mA), else refuse rather than program a higher current
	 * than the request allows.
	 */
	if (limit_ua >= MAX8903_USB_CURRENT_LIMIT_HIGH_UA) {
		selected = MAX8903_USB_CURRENT_LIMIT_HIGH_UA;
		gpio_val = 1;
	} else if (limit_ua >= MAX8903_USB_CURRENT_LIMIT_LOW_UA) {
		selected = MAX8903_USB_CURRENT_LIMIT_LOW_UA;
		gpio_val = 0;
	} else {
		return -EINVAL;
	}

	gpiod_set_value_cansleep(data->usb_current_limit_gpio, gpio_val);
	data->usb_current_limit_ua = selected;

	dev_dbg(data->dev, "USB current limit set to %u uA\n",
		data->usb_current_limit_ua);

	return 0;
}

static int max8903_set_property(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct max8903_data *data = power_supply_get_drvdata(psy);
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		/*
		 * val->intval is signed; the set_*_current_limit() helpers
		 * take a u32. Reject negatives explicitly so a negative
		 * request cannot widen into a huge unsigned value, bypass
		 * the "limit <= cap" bounds check inside the helper, and
		 * silently program the maximum permitted current.
		 */
		if (val->intval < 0)
			return -EINVAL;
		/*
		 * Hold source_lock across the source check and the
		 * resulting hardware write so the IRQ handler cannot
		 * flip ta_in/usb_in between them and have us program the
		 * limit for a source that has just gone offline. Mirror
		 * the DC-priority policy of the get path: if DC is online
		 * route to the DC helper (refuse if DC GPIOs aren't
		 * configured) rather than fall through to USB.
		 */
		mutex_lock(&data->source_lock);
		if (data->ta_in)
			ret = data->dc_current_limit_gpios ?
			      max8903_set_dc_current_limit(data, val->intval) :
			      -ENODEV;
		else if (data->usb_in && data->usb_current_limit_gpio)
			ret = max8903_set_usb_current_limit(data, val->intval);
		else
			ret = -EINVAL;
		mutex_unlock(&data->source_lock);
		return ret;
	default:
		return -EINVAL;
	}
}

static int max8903_property_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	struct max8903_data *data = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return data->dc_current_limit_gpios ||
		       data->usb_current_limit_gpio;
	default:
		return 0;
	}
}

static irqreturn_t max8903_dcin(int irq, void *_data)
{
	struct max8903_data *data = _data;
	bool ta_in;
	enum power_supply_type old_type;

	/*
	 * This means the line is asserted.
	 *
	 * The signal is active low, but the inversion is handled in the GPIO
	 * library as the line should be flagged GPIO_ACTIVE_LOW in the device
	 * tree.
	 */
	/*
	 * Hold source_lock across the full read-modify-evaluate block:
	 *   - so a concurrent max8903_set_property() sees a consistent
	 *     state (lock release would otherwise expose a window where
	 *     data->ta_in is updated but the cen/dcm writes still pend);
	 *   - so the cen enable calculation reads a stable data->usb_in
	 *     rather than racing with max8903_usbin() and writing the
	 *     wrong enable state.
	 */
	mutex_lock(&data->source_lock);
	ta_in = gpiod_get_value(data->dok);
	if (ta_in == data->ta_in) {
		mutex_unlock(&data->source_lock);
		return IRQ_HANDLED;
	}

	data->ta_in = ta_in;

	/* Set Current-Limit-Mode 1:DC 0:USB */
	if (data->dcm)
		gpiod_set_value(data->dcm, ta_in);

	/* Charger Enable / Disable */
	if (data->cen) {
		int val;

		if (ta_in)
			/* Certainly enable if DOK is asserted */
			val = 1;
		else if (data->usb_in)
			/* Enable if the USB charger is enabled */
			val = 1;
		else
			/* Else default-disable */
			val = 0;

		gpiod_set_value(data->cen, val);
	}

	old_type = data->psy_desc.type;

	if (data->ta_in)
		data->psy_desc.type = POWER_SUPPLY_TYPE_MAINS;
	else if (data->usb_in)
		data->psy_desc.type = POWER_SUPPLY_TYPE_USB;
	else
		data->psy_desc.type = POWER_SUPPLY_TYPE_BATTERY;
	mutex_unlock(&data->source_lock);

	dev_dbg(data->dev, "TA(DC-IN) Charger %s.\n", ta_in ?
			"Connected" : "Disconnected");

	if (old_type != data->psy_desc.type)
		power_supply_changed(data->psy);

	return IRQ_HANDLED;
}

static irqreturn_t max8903_usbin(int irq, void *_data)
{
	struct max8903_data *data = _data;
	bool usb_in;
	enum power_supply_type old_type;

	/*
	 * This means the line is asserted.
	 *
	 * The signal is active low, but the inversion is handled in the GPIO
	 * library as the line should be flagged GPIO_ACTIVE_LOW in the device
	 * tree.
	 */
	/* See max8903_dcin(): hold the lock across the full update. */
	mutex_lock(&data->source_lock);
	usb_in = gpiod_get_value(data->uok);
	if (usb_in == data->usb_in) {
		mutex_unlock(&data->source_lock);
		return IRQ_HANDLED;
	}

	data->usb_in = usb_in;

	/* Do not touch Current-Limit-Mode */

	/* Charger Enable / Disable */
	if (data->cen) {
		int val;

		if (usb_in)
			/* Certainly enable if UOK is asserted */
			val = 1;
		else if (data->ta_in)
			/* Enable if the DC charger is enabled */
			val = 1;
		else
			/* Else default-disable */
			val = 0;

		gpiod_set_value(data->cen, val);
	}

	old_type = data->psy_desc.type;

	if (data->ta_in)
		data->psy_desc.type = POWER_SUPPLY_TYPE_MAINS;
	else if (data->usb_in)
		data->psy_desc.type = POWER_SUPPLY_TYPE_USB;
	else
		data->psy_desc.type = POWER_SUPPLY_TYPE_BATTERY;
	mutex_unlock(&data->source_lock);

	dev_dbg(data->dev, "USB Charger %s.\n", usb_in ?
			"Connected" : "Disconnected");

	if (old_type != data->psy_desc.type)
		power_supply_changed(data->psy);

	return IRQ_HANDLED;
}

static irqreturn_t max8903_fault(int irq, void *_data)
{
	struct max8903_data *data = _data;
	bool fault;

	/*
	 * This means the line is asserted.
	 *
	 * The signal is active low, but the inversion is handled in the GPIO
	 * library as the line should be flagged GPIO_ACTIVE_LOW in the device
	 * tree.
	 */
	fault = gpiod_get_value(data->flt);

	if (fault == data->fault)
		return IRQ_HANDLED;

	data->fault = fault;

	if (fault)
		dev_err(data->dev, "Charger suffers a fault and stops.\n");
	else
		dev_err(data->dev, "Charger recovered from a fault.\n");

	return IRQ_HANDLED;
}

static int max8903_parse_dc_current_limit(struct platform_device *pdev,
					  struct max8903_data *data)
{
	struct device *dev = &pdev->dev;
	int ret, i, map_size;
	u32 *map;

	data->dc_current_limit_gpios = devm_gpiod_get_array_optional(dev,
					"dc-current-limit", GPIOD_OUT_LOW);
	if (IS_ERR(data->dc_current_limit_gpios))
		return dev_err_probe(dev, PTR_ERR(data->dc_current_limit_gpios),
				     "failed to get DC current limit GPIOs");

	if (!data->dc_current_limit_gpios)
		return 0;	/* Optional feature not present */

	/*
	 * gpio_value entries below are bit patterns indexed into the
	 * dc-current-limit GPIO array. The driver represents them in
	 * a single unsigned long for gpiod_set_array_value_cansleep(),
	 * and BIT(ndescs) further down assumes ndescs fits in a u32
	 * shift; reject pathological DTs at parse time instead of
	 * relying on undefined-behaviour-free dtschema. The binding
	 * already caps maxItems at 4 so this is purely defensive.
	 */
	if (data->dc_current_limit_gpios->ndescs >= BITS_PER_TYPE(u32)) {
		dev_err(dev, "dc-current-limit-gpios: %u GPIOs exceeds %u-bit cap\n",
			data->dc_current_limit_gpios->ndescs,
			(unsigned int)BITS_PER_TYPE(u32));
		return -EINVAL;
	}

	/* Parse mapping: pairs of (current_ua, gpio_value) */
	map_size = device_property_count_u32(dev, "dc-current-limit-mapping");
	if (map_size <= 0 || map_size % 2) {
		dev_err(dev, "invalid dc-current-limit-mapping\n");
		return -EINVAL;
	}

	/*
	 * map[] is a scratch buffer used only inside this function to
	 * read the property and unpack it into data->dc_current_limit_map.
	 * Use a plain kmalloc + kfree rather than devm_*: there is no
	 * reason to keep the raw mirror around for the lifetime of the
	 * device.
	 */
	map = kmalloc_array(map_size, sizeof(*map), GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	ret = device_property_read_u32_array(dev, "dc-current-limit-mapping",
					     map, map_size);
	if (ret) {
		dev_err(dev, "failed to read dc-current-limit-mapping\n");
		kfree(map);
		return ret;
	}

	data->dc_current_limit_map_size = map_size / 2;
	data->dc_current_limit_map = devm_kcalloc(dev,
					data->dc_current_limit_map_size,
					sizeof(*data->dc_current_limit_map),
					GFP_KERNEL);
	if (!data->dc_current_limit_map) {
		kfree(map);
		return -ENOMEM;
	}

	for (i = 0; i < data->dc_current_limit_map_size; i++) {
		u32 gpio_value = map[i * 2 + 1];

		/*
		 * gpio_value is the bitmap programmed across the
		 * dc-current-limit GPIOs, so it cannot represent more
		 * bits than the GPIO array width. A larger value would
		 * be silently truncated by gpiod_set_array_value() and
		 * select the wrong limit; reject it at parse time so
		 * the bogus DT is visible to the integrator.
		 */
		if (gpio_value >= BIT(data->dc_current_limit_gpios->ndescs)) {
			dev_err(dev,
				"dc-current-limit-mapping entry %d: gpio_value 0x%x exceeds %u-GPIO range\n",
				i, gpio_value,
				data->dc_current_limit_gpios->ndescs);
			kfree(map);
			return -EINVAL;
		}
		data->dc_current_limit_map[i].limit_ua = map[i * 2];
		data->dc_current_limit_map[i].gpio_value = gpio_value;
	}

	kfree(map);

	/*
	 * devm_gpiod_get_array_optional() above asked for GPIOD_OUT_LOW,
	 * so the hardware mux starts at gpio_value 0. Require the DT
	 * mapping to include a gpio_value=0 entry so the software
	 * current-limit state has a definite initial value matching the
	 * hardware. Without this entry we would have to guess and the
	 * reported INPUT_CURRENT_LIMIT could disagree with what the
	 * mux is actually wired to until a set_property write picks a
	 * real value.
	 */
	for (i = 0; i < data->dc_current_limit_map_size; i++)
		if (data->dc_current_limit_map[i].gpio_value == 0)
			break;
	if (i == data->dc_current_limit_map_size) {
		dev_err(dev,
			"dc-current-limit-mapping must include a gpio_value=0 entry to describe the boot-time mux state\n");
		return -EINVAL;
	}
	data->dc_current_limit_ua = data->dc_current_limit_map[i].limit_ua;

	dev_dbg(dev, "DC current limit control: %d levels available, initial %u uA\n",
		data->dc_current_limit_map_size, data->dc_current_limit_ua);

	return 0;
}

static int max8903_parse_usb_current_limit(struct platform_device *pdev,
					   struct max8903_data *data)
{
	struct device *dev = &pdev->dev;

	data->usb_current_limit_gpio = devm_gpiod_get_optional(dev,
					"usb-current-limit", GPIOD_OUT_LOW);
	if (IS_ERR(data->usb_current_limit_gpio))
		return dev_err_probe(dev, PTR_ERR(data->usb_current_limit_gpio),
				     "failed to get USB current limit GPIO");

	if (!data->usb_current_limit_gpio)
		return 0;	/* Optional feature not present */

	/* Start at low current (IUSB low = 100 mA) for safety */
	data->usb_current_limit_ua = MAX8903_USB_CURRENT_LIMIT_LOW_UA;

	return 0;
}

static int max8903_setup_gpios(struct platform_device *pdev)
{
	struct max8903_data *data = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	bool ta_in = false;
	bool usb_in = false;
	enum gpiod_flags flags;

	data->dok = devm_gpiod_get_optional(dev, "dok", GPIOD_IN);
	if (IS_ERR(data->dok))
		return dev_err_probe(dev, PTR_ERR(data->dok),
				     "failed to get DOK GPIO");
	if (data->dok) {
		gpiod_set_consumer_name(data->dok, data->psy_desc.name);
		/*
		 * The DC OK is pulled up to 1 and goes low when a charger
		 * is plugged in (active low) but in the device tree the
		 * line is marked as GPIO_ACTIVE_LOW so we get a 1 (asserted)
		 * here if the DC charger is plugged in.
		 */
		ta_in = gpiod_get_value(data->dok);
	}

	data->uok = devm_gpiod_get_optional(dev, "uok", GPIOD_IN);
	if (IS_ERR(data->uok))
		return dev_err_probe(dev, PTR_ERR(data->uok),
				     "failed to get UOK GPIO");
	if (data->uok) {
		gpiod_set_consumer_name(data->uok, data->psy_desc.name);
		/*
		 * The USB OK is pulled up to 1 and goes low when a USB charger
		 * is plugged in (active low) but in the device tree the
		 * line is marked as GPIO_ACTIVE_LOW so we get a 1 (asserted)
		 * here if the USB charger is plugged in.
		 */
		usb_in = gpiod_get_value(data->uok);
	}

	/* Either DC OK or USB OK must be provided */
	if (!data->dok && !data->uok) {
		dev_err(dev, "no valid power source\n");
		return -EINVAL;
	}

	/*
	 * If either charger is already connected at this point,
	 * assert the CEN line and enable charging from the start.
	 *
	 * The line is active low but also marked with GPIO_ACTIVE_LOW
	 * in the device tree, so when we assert the line with
	 * GPIOD_OUT_HIGH the line will be driven low.
	 */
	flags = (ta_in || usb_in) ? GPIOD_OUT_HIGH : GPIOD_OUT_LOW;
	/*
	 * If DC OK is provided, Charger Enable CEN is compulsory
	 * so this is not optional here.
	 */
	data->cen = devm_gpiod_get(dev, "cen", flags);
	if (IS_ERR(data->cen))
		return dev_err_probe(dev, PTR_ERR(data->cen),
				     "failed to get CEN GPIO");
	gpiod_set_consumer_name(data->cen, data->psy_desc.name);

	/*
	 * If the DC charger is connected, then select it.
	 *
	 * The DCM line should be marked GPIO_ACTIVE_HIGH in the
	 * device tree. Driving it high will enable the DC charger
	 * input over the USB charger input.
	 */
	flags = ta_in ? GPIOD_OUT_HIGH : GPIOD_OUT_LOW;
	data->dcm = devm_gpiod_get_optional(dev, "dcm", flags);
	if (IS_ERR(data->dcm))
		return dev_err_probe(dev, PTR_ERR(data->dcm),
				     "failed to get DCM GPIO");
	gpiod_set_consumer_name(data->dcm, data->psy_desc.name);

	data->chg = devm_gpiod_get_optional(dev, "chg", GPIOD_IN);
	if (IS_ERR(data->chg))
		return dev_err_probe(dev, PTR_ERR(data->chg),
				     "failed to get CHG GPIO");
	gpiod_set_consumer_name(data->chg, data->psy_desc.name);

	data->flt = devm_gpiod_get_optional(dev, "flt", GPIOD_IN);
	if (IS_ERR(data->flt))
		return dev_err_probe(dev, PTR_ERR(data->flt),
				     "failed to get FLT GPIO");
	gpiod_set_consumer_name(data->flt, data->psy_desc.name);

	data->usus = devm_gpiod_get_optional(dev, "usus", GPIOD_IN);
	if (IS_ERR(data->usus))
		return dev_err_probe(dev, PTR_ERR(data->usus),
				     "failed to get USUS GPIO");
	gpiod_set_consumer_name(data->usus, data->psy_desc.name);

	data->fault = false;
	data->ta_in = ta_in;
	data->usb_in = usb_in;

	return 0;
}

static int max8903_probe(struct platform_device *pdev)
{
	struct max8903_data *data;
	struct device *dev = &pdev->dev;
	struct power_supply_config psy_cfg = {};
	int ret = 0;

	data = devm_kzalloc(dev, sizeof(struct max8903_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev;
	mutex_init(&data->source_lock);
	platform_set_drvdata(pdev, data);

	ret = max8903_setup_gpios(pdev);
	if (ret)
		return ret;

	ret = max8903_parse_dc_current_limit(pdev, data);
	if (ret)
		return ret;

	ret = max8903_parse_usb_current_limit(pdev, data);
	if (ret)
		return ret;

	data->psy_desc.name = "max8903_charger";
	data->psy_desc.type = (data->ta_in) ? POWER_SUPPLY_TYPE_MAINS :
			((data->usb_in) ? POWER_SUPPLY_TYPE_USB :
			 POWER_SUPPLY_TYPE_BATTERY);
	data->psy_desc.get_property = max8903_get_property;
	data->psy_desc.set_property = max8903_set_property;
	data->psy_desc.property_is_writeable = max8903_property_is_writeable;
	data->psy_desc.properties = max8903_charger_props;
	data->psy_desc.num_properties = ARRAY_SIZE(max8903_charger_props);

	psy_cfg.fwnode = dev_fwnode(dev);
	psy_cfg.drv_data = data;

	data->psy = devm_power_supply_register(dev, &data->psy_desc, &psy_cfg);
	if (IS_ERR(data->psy)) {
		dev_err(dev, "failed: power supply register.\n");
		return PTR_ERR(data->psy);
	}

	if (data->dok) {
		ret = devm_request_threaded_irq(dev, gpiod_to_irq(data->dok),
					NULL, max8903_dcin,
					IRQF_TRIGGER_FALLING |
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					"MAX8903 DC IN", data);
		if (ret)
			return ret;
	}

	if (data->uok) {
		ret = devm_request_threaded_irq(dev, gpiod_to_irq(data->uok),
					NULL, max8903_usbin,
					IRQF_TRIGGER_FALLING |
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					"MAX8903 USB IN", data);
		if (ret)
			return ret;
	}

	if (data->flt) {
		ret = devm_request_threaded_irq(dev, gpiod_to_irq(data->flt),
					NULL, max8903_fault,
					IRQF_TRIGGER_FALLING |
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					"MAX8903 Fault", data);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct of_device_id max8903_match_ids[] = {
	{ .compatible = "maxim,max8903", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, max8903_match_ids);

static struct platform_driver max8903_driver = {
	.probe	= max8903_probe,
	.driver = {
		.name	= "max8903-charger",
		.of_match_table = max8903_match_ids
	},
};

module_platform_driver(max8903_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MAX8903 Charger Driver");
MODULE_AUTHOR("MyungJoo Ham <myungjoo.ham@samsung.com>");
MODULE_ALIAS("platform:max8903-charger");
