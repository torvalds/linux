// SPDX-License-Identifier: GPL-2.0-only
/*
 * Analog Devices ADP5585 I/O expander, PWM controller and keypad controller
 *
 * Copyright 2022 NXP
 * Copyright 2024 Ideas on Board Oy
 * Copyright 2025 Analog Devices Inc.
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/gpio/consumer.h>
#include <linux/mfd/adp5585.h>
#include <linux/mfd/core.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>

enum {
	ADP5585_DEV_GPIO,
	ADP5585_DEV_PWM,
	ADP5585_DEV_INPUT,
	ADP5585_DEV_MAX
};

static const struct mfd_cell adp5585_devs[ADP5585_DEV_MAX] = {
	MFD_CELL_NAME("adp5585-gpio"),
	MFD_CELL_NAME("adp5585-pwm"),
	MFD_CELL_NAME("adp5585-keys"),
};

static const struct mfd_cell adp5589_devs[] = {
	MFD_CELL_NAME("adp5589-gpio"),
	MFD_CELL_NAME("adp5589-pwm"),
	MFD_CELL_NAME("adp5589-keys"),
};

static const struct regmap_range adp5585_volatile_ranges[] = {
	regmap_reg_range(ADP5585_ID, ADP5585_GPI_STATUS_B),
};

static const struct regmap_access_table adp5585_volatile_regs = {
	.yes_ranges = adp5585_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(adp5585_volatile_ranges),
};

static const struct regmap_range adp5589_volatile_ranges[] = {
	regmap_reg_range(ADP5585_ID, ADP5589_GPI_STATUS_C),
};

static const struct regmap_access_table adp5589_volatile_regs = {
	.yes_ranges = adp5589_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(adp5589_volatile_ranges),
};

/*
 * Chip variants differ in the default configuration of pull-up and pull-down
 * resistors, and therefore have different default register values:
 *
 * - The -00, -01 and -03 variants (collectively referred to as
 *   ADP5585_REGMAP_00) have pull-up on all GPIO pins by default.
 * - The -02 variant has no default pull-up or pull-down resistors.
 * - The -04 variant has default pull-down resistors on all GPIO pins.
 */

static const u8 adp5585_regmap_defaults_00[ADP5585_MAX_REG + 1] = {
	/* 0x00 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x08 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x10 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x18 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x20 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x28 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x30 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x38 */ 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const u8 adp5585_regmap_defaults_02[ADP5585_MAX_REG + 1] = {
	/* 0x00 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x08 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x10 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc3,
	/* 0x18 */ 0x03, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x20 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x28 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x30 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x38 */ 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const u8 adp5585_regmap_defaults_04[ADP5585_MAX_REG + 1] = {
	/* 0x00 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x08 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x10 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55,
	/* 0x18 */ 0x05, 0x55, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x20 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x28 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x30 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x38 */ 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const u8 adp5589_regmap_defaults_00[ADP5589_MAX_REG + 1] = {
	/* 0x00 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x08 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x10 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x18 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x20 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x28 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x30 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x38 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x40 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x48 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const u8 adp5589_regmap_defaults_01[ADP5589_MAX_REG + 1] = {
	/* 0x00 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x08 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x10 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x18 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x20 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x28 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x30 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x38 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00,
	/* 0x40 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x48 */ 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00,
};

static const u8 adp5589_regmap_defaults_02[ADP5589_MAX_REG + 1] = {
	/* 0x00 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x08 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x10 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x18 */ 0x00, 0x41, 0x01, 0x00, 0x11, 0x04, 0x00, 0x00,
	/* 0x20 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x28 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x30 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x38 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x40 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x48 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const u8 *adp5585_regmap_defaults[ADP5585_MAX] = {
	[ADP5585_00] = adp5585_regmap_defaults_00,
	[ADP5585_01] = adp5585_regmap_defaults_00,
	[ADP5585_02] = adp5585_regmap_defaults_02,
	[ADP5585_03] = adp5585_regmap_defaults_00,
	[ADP5585_04] = adp5585_regmap_defaults_04,
	[ADP5589_00] = adp5589_regmap_defaults_00,
	[ADP5589_01] = adp5589_regmap_defaults_01,
	[ADP5589_02] = adp5589_regmap_defaults_02,
};

static const struct regmap_config adp5585_regmap_config_template = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = ADP5585_MAX_REG,
	.volatile_table = &adp5585_volatile_regs,
	.cache_type = REGCACHE_MAPLE,
	.num_reg_defaults_raw = ADP5585_MAX_REG + 1,
};

static const struct regmap_config adp5589_regmap_config_template = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = ADP5589_MAX_REG,
	.volatile_table = &adp5589_volatile_regs,
	.cache_type = REGCACHE_MAPLE,
	.num_reg_defaults_raw = ADP5589_MAX_REG + 1,
};

static const struct adp5585_regs adp5585_regs = {
	.ext_cfg = ADP5585_PIN_CONFIG_C,
	.int_en = ADP5585_INT_EN,
	.gen_cfg = ADP5585_GENERAL_CFG,
	.poll_ptime_cfg = ADP5585_POLL_PTIME_CFG,
	.reset_cfg = ADP5585_RESET_CFG,
	.reset1_event_a = ADP5585_RESET1_EVENT_A,
	.reset2_event_a = ADP5585_RESET2_EVENT_A,
	.pin_cfg_a = ADP5585_PIN_CONFIG_A,
};

static const struct adp5585_regs adp5589_regs = {
	.ext_cfg = ADP5589_PIN_CONFIG_D,
	.int_en = ADP5589_INT_EN,
	.gen_cfg = ADP5589_GENERAL_CFG,
	.poll_ptime_cfg = ADP5589_POLL_PTIME_CFG,
	.reset_cfg = ADP5589_RESET_CFG,
	.reset1_event_a = ADP5589_RESET1_EVENT_A,
	.reset2_event_a = ADP5589_RESET2_EVENT_A,
	.pin_cfg_a = ADP5589_PIN_CONFIG_A,
};

static int adp5585_validate_event(const struct adp5585_dev *adp5585, unsigned int ev)
{
	if (adp5585->has_pin6) {
		if (ev >= ADP5585_ROW5_KEY_EVENT_START && ev <= ADP5585_ROW5_KEY_EVENT_END)
			return 0;
		if (ev >= ADP5585_GPI_EVENT_START && ev <= ADP5585_GPI_EVENT_END)
			return 0;

		return dev_err_probe(adp5585->dev, -EINVAL,
				     "Invalid unlock/reset event(%u) for this device\n", ev);
	}

	if (ev >= ADP5585_KEY_EVENT_START && ev <= ADP5585_KEY_EVENT_END)
		return 0;
	if (ev >= ADP5585_GPI_EVENT_START && ev <= ADP5585_GPI_EVENT_END) {
		/*
		 * Some variants of the adp5585 do not have the Row 5
		 * (meaning pin 6 or GPIO 6) available. Instead that pin serves
		 * as a reset pin. So, we need to make sure no event is
		 * configured for it.
		 */
		if (ev == (ADP5585_GPI_EVENT_START + 5))
			return dev_err_probe(adp5585->dev, -EINVAL,
					     "Invalid unlock/reset event(%u). R5 not available\n",
					     ev);
		return 0;
	}

	return dev_err_probe(adp5585->dev, -EINVAL,
			     "Invalid unlock/reset event(%u) for this device\n", ev);
}

static int adp5589_validate_event(const struct adp5585_dev *adp5585, unsigned int ev)
{
	if (ev >= ADP5589_KEY_EVENT_START && ev <= ADP5589_KEY_EVENT_END)
		return 0;
	if (ev >= ADP5589_GPI_EVENT_START && ev <= ADP5589_GPI_EVENT_END)
		return 0;

	return dev_err_probe(adp5585->dev, -EINVAL,
			     "Invalid unlock/reset event(%u) for this device\n", ev);
}

static struct regmap_config *adp5585_fill_variant_config(struct adp5585_dev *adp5585)
{
	struct regmap_config *regmap_config;

	switch (adp5585->variant) {
	case ADP5585_00:
	case ADP5585_01:
	case ADP5585_02:
	case ADP5585_03:
	case ADP5585_04:
		adp5585->id = ADP5585_MAN_ID_VALUE;
		adp5585->regs = &adp5585_regs;
		adp5585->n_pins = ADP5585_PIN_MAX;
		adp5585->reset2_out = ADP5585_RESET2_OUT;
		if (adp5585->variant == ADP5585_01)
			adp5585->has_pin6 = true;
		regmap_config = devm_kmemdup(adp5585->dev, &adp5585_regmap_config_template,
					     sizeof(*regmap_config), GFP_KERNEL);
		break;
	case ADP5589_00:
	case ADP5589_01:
	case ADP5589_02:
		adp5585->id = ADP5589_MAN_ID_VALUE;
		adp5585->regs = &adp5589_regs;
		adp5585->has_unlock = true;
		adp5585->has_pin6 = true;
		adp5585->n_pins = ADP5589_PIN_MAX;
		adp5585->reset2_out = ADP5589_RESET2_OUT;
		regmap_config = devm_kmemdup(adp5585->dev, &adp5589_regmap_config_template,
					     sizeof(*regmap_config), GFP_KERNEL);
		break;
	default:
		return ERR_PTR(-ENODEV);
	}

	if (!regmap_config)
		return ERR_PTR(-ENOMEM);

	regmap_config->reg_defaults_raw = adp5585_regmap_defaults[adp5585->variant];

	return regmap_config;
}

static int adp5585_parse_ev_array(const struct adp5585_dev *adp5585, const char *prop, u32 *events,
				  u32 *n_events, u32 max_evs, bool reset_ev)
{
	struct device *dev = adp5585->dev;
	unsigned int ev;
	int ret;

	/*
	 * The device has the capability of handling special events through GPIs or a Keypad:
	 *  unlock events: Unlock the keymap until one of the configured events is detected.
	 *  reset events: Generate a reset pulse when one of the configured events is detected.
	 */
	ret = device_property_count_u32(dev, prop);
	if (ret < 0)
		return 0;

	*n_events = ret;

	if (!adp5585->has_unlock && !reset_ev)
		return dev_err_probe(dev, -EOPNOTSUPP, "Unlock keys not supported\n");

	if (*n_events > max_evs)
		return dev_err_probe(dev, -EINVAL,
				     "Invalid number of keys(%u > %u) for %s\n",
				     *n_events, max_evs, prop);

	ret = device_property_read_u32_array(dev, prop, events, *n_events);
	if (ret)
		return ret;

	for (ev = 0; ev < *n_events; ev++) {
		if (!reset_ev && events[ev] == ADP5589_UNLOCK_WILDCARD)
			continue;

		if (adp5585->id == ADP5585_MAN_ID_VALUE)
			ret = adp5585_validate_event(adp5585, events[ev]);
		else
			ret = adp5589_validate_event(adp5585, events[ev]);
		if (ret)
			return ret;
	}

	return 0;
}

static int adp5585_unlock_ev_parse(struct adp5585_dev *adp5585)
{
	struct device *dev = adp5585->dev;
	int ret;

	ret = adp5585_parse_ev_array(adp5585, "adi,unlock-events", adp5585->unlock_keys,
				     &adp5585->nkeys_unlock, ARRAY_SIZE(adp5585->unlock_keys),
				     false);
	if (ret)
		return ret;
	if (!adp5585->nkeys_unlock)
		return 0;

	ret = device_property_read_u32(dev, "adi,unlock-trigger-sec", &adp5585->unlock_time);
	if (!ret) {
		if (adp5585->unlock_time > ADP5585_MAX_UNLOCK_TIME_SEC)
			return dev_err_probe(dev, -EINVAL,
					     "Invalid unlock time(%u > %d)\n",
					     adp5585->unlock_time,
					     ADP5585_MAX_UNLOCK_TIME_SEC);
	}

	return 0;
}

static int adp5585_reset_ev_parse(struct adp5585_dev *adp5585)
{
	struct device *dev = adp5585->dev;
	u32 prop_val;
	int ret;

	ret = adp5585_parse_ev_array(adp5585, "adi,reset1-events", adp5585->reset1_keys,
				     &adp5585->nkeys_reset1,
				     ARRAY_SIZE(adp5585->reset1_keys), true);
	if (ret)
		return ret;

	ret = adp5585_parse_ev_array(adp5585, "adi,reset2-events",
				     adp5585->reset2_keys,
				     &adp5585->nkeys_reset2,
				     ARRAY_SIZE(adp5585->reset2_keys), true);
	if (ret)
		return ret;

	if (!adp5585->nkeys_reset1 && !adp5585->nkeys_reset2)
		return 0;

	if (adp5585->nkeys_reset1 && device_property_read_bool(dev, "adi,reset1-active-high"))
		adp5585->reset_cfg |= FIELD_PREP(ADP5585_RESET1_POL, 1);

	if (adp5585->nkeys_reset2 && device_property_read_bool(dev, "adi,reset2-active-high"))
		adp5585->reset_cfg |= FIELD_PREP(ADP5585_RESET2_POL, 1);

	if (device_property_read_bool(dev, "adi,rst-passthrough-enable"))
		adp5585->reset_cfg |= FIELD_PREP(ADP5585_RST_PASSTHRU_EN, 1);

	ret = device_property_read_u32(dev, "adi,reset-trigger-ms", &prop_val);
	if (!ret) {
		switch (prop_val) {
		case 0:
			adp5585->reset_cfg |= FIELD_PREP(ADP5585_RESET_TRIG_TIME, 0);
			break;
		case 1000:
			adp5585->reset_cfg |= FIELD_PREP(ADP5585_RESET_TRIG_TIME, 1);
			break;
		case 1500:
			adp5585->reset_cfg |= FIELD_PREP(ADP5585_RESET_TRIG_TIME, 2);
			break;
		case 2000:
			adp5585->reset_cfg |= FIELD_PREP(ADP5585_RESET_TRIG_TIME, 3);
			break;
		case 2500:
			adp5585->reset_cfg |= FIELD_PREP(ADP5585_RESET_TRIG_TIME, 4);
			break;
		case 3000:
			adp5585->reset_cfg |= FIELD_PREP(ADP5585_RESET_TRIG_TIME, 5);
			break;
		case 3500:
			adp5585->reset_cfg |= FIELD_PREP(ADP5585_RESET_TRIG_TIME, 6);
			break;
		case 4000:
			adp5585->reset_cfg |= FIELD_PREP(ADP5585_RESET_TRIG_TIME, 7);
			break;
		default:
			return dev_err_probe(dev, -EINVAL,
					     "Invalid value(%u) for adi,reset-trigger-ms\n",
					     prop_val);
		}
	}

	ret = device_property_read_u32(dev, "adi,reset-pulse-width-us", &prop_val);
	if (!ret) {
		switch (prop_val) {
		case 500:
			adp5585->reset_cfg |= FIELD_PREP(ADP5585_PULSE_WIDTH, 0);
			break;
		case 1000:
			adp5585->reset_cfg |= FIELD_PREP(ADP5585_PULSE_WIDTH, 1);
			break;
		case 2000:
			adp5585->reset_cfg |= FIELD_PREP(ADP5585_PULSE_WIDTH, 2);
			break;
		case 10000:
			adp5585->reset_cfg |= FIELD_PREP(ADP5585_PULSE_WIDTH, 3);
			break;
		default:
			return dev_err_probe(dev, -EINVAL,
					     "Invalid value(%u) for adi,reset-pulse-width-us\n",
					     prop_val);
		}
	}

	return 0;
}

static int adp5585_add_devices(const struct adp5585_dev *adp5585)
{
	struct device *dev = adp5585->dev;
	const struct mfd_cell *cells;
	int ret;

	if (adp5585->id == ADP5585_MAN_ID_VALUE)
		cells = adp5585_devs;
	else
		cells = adp5589_devs;

	if (device_property_present(dev, "#pwm-cells")) {
		/* Make sure the PWM output pin is not used by the GPIO or INPUT devices */
		__set_bit(ADP5585_PWM_OUT, adp5585->pin_usage);
		ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO,
					   &cells[ADP5585_DEV_PWM], 1, NULL, 0, NULL);
		if (ret)
			return dev_err_probe(dev, ret, "Failed to add PWM device\n");
	}

	if (device_property_present(dev, "#gpio-cells")) {
		ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO,
					   &cells[ADP5585_DEV_GPIO], 1, NULL, 0, NULL);
		if (ret)
			return dev_err_probe(dev, ret, "Failed to add GPIO device\n");
	}

	if (device_property_present(adp5585->dev, "adi,keypad-pins")) {
		ret = devm_mfd_add_devices(adp5585->dev, PLATFORM_DEVID_AUTO,
					   &cells[ADP5585_DEV_INPUT], 1, NULL, 0, NULL);
		if (ret)
			return dev_err_probe(dev, ret, "Failed to add input device\n");
	}

	return 0;
}

static void adp5585_osc_disable(void *data)
{
	const struct adp5585_dev *adp5585 = data;

	regmap_write(adp5585->regmap, ADP5585_GENERAL_CFG, 0);
}

static void adp5585_report_events(struct adp5585_dev *adp5585, int ev_cnt)
{
	unsigned int i;

	for (i = 0; i < ev_cnt; i++) {
		unsigned long key_val, key_press;
		unsigned int key;
		int ret;

		ret = regmap_read(adp5585->regmap, ADP5585_FIFO_1 + i, &key);
		if (ret)
			return;

		key_val = FIELD_GET(ADP5585_KEY_EVENT_MASK, key);
		key_press = FIELD_GET(ADP5585_KEV_EV_PRESS_MASK, key);

		blocking_notifier_call_chain(&adp5585->event_notifier, key_val, (void *)key_press);
	}
}

static irqreturn_t adp5585_irq(int irq, void *data)
{
	struct adp5585_dev *adp5585 = data;
	unsigned int status, ev_cnt;
	int ret;

	ret = regmap_read(adp5585->regmap, ADP5585_INT_STATUS, &status);
	if (ret)
		return IRQ_HANDLED;

	if (status & ADP5585_OVRFLOW_INT)
		dev_err_ratelimited(adp5585->dev, "Event overflow error\n");

	if (!(status & ADP5585_EVENT_INT))
		goto out_irq;

	ret = regmap_read(adp5585->regmap, ADP5585_STATUS, &ev_cnt);
	if (ret)
		goto out_irq;

	ev_cnt = FIELD_GET(ADP5585_EC_MASK, ev_cnt);
	if (!ev_cnt)
		goto out_irq;

	adp5585_report_events(adp5585, ev_cnt);
out_irq:
	regmap_write(adp5585->regmap, ADP5585_INT_STATUS, status);
	return IRQ_HANDLED;
}

static int adp5585_setup(struct adp5585_dev *adp5585)
{
	const struct adp5585_regs *regs = adp5585->regs;
	unsigned int reg_val = 0, i;
	int ret;

	/* If pin_6 (ROW5/GPI6) is not available, make sure to mark it as "busy" */
	if (!adp5585->has_pin6)
		__set_bit(ADP5585_ROW5, adp5585->pin_usage);

	/* Configure the device with reset and unlock events */
	for (i = 0; i < adp5585->nkeys_unlock; i++) {
		ret = regmap_write(adp5585->regmap, ADP5589_UNLOCK1 + i,
				   adp5585->unlock_keys[i] | ADP5589_UNLOCK_EV_PRESS);
		if (ret)
			return ret;
	}

	if (adp5585->nkeys_unlock) {
		ret = regmap_update_bits(adp5585->regmap, ADP5589_UNLOCK_TIMERS,
					 ADP5589_UNLOCK_TIMER, adp5585->unlock_time);
		if (ret)
			return ret;

		ret = regmap_set_bits(adp5585->regmap, ADP5589_LOCK_CFG, ADP5589_LOCK_EN);
		if (ret)
			return ret;
	}

	for (i = 0; i < adp5585->nkeys_reset1; i++) {
		ret = regmap_write(adp5585->regmap, regs->reset1_event_a + i,
				   adp5585->reset1_keys[i] | ADP5585_RESET_EV_PRESS);
		if (ret)
			return ret;

		/* Mark that pin as not usable for the INPUT and GPIO devices. */
		__set_bit(ADP5585_RESET1_OUT, adp5585->pin_usage);
	}

	for (i = 0; i < adp5585->nkeys_reset2; i++) {
		ret = regmap_write(adp5585->regmap, regs->reset2_event_a + i,
				   adp5585->reset2_keys[i] | ADP5585_RESET_EV_PRESS);
		if (ret)
			return ret;

		__set_bit(adp5585->reset2_out, adp5585->pin_usage);
	}

	if (adp5585->nkeys_reset1 || adp5585->nkeys_reset2) {
		ret = regmap_write(adp5585->regmap, regs->reset_cfg, adp5585->reset_cfg);
		if (ret)
			return ret;

		/* If there's a reset1 event, then R4 is used as an output for the reset signal */
		if (adp5585->nkeys_reset1)
			reg_val = ADP5585_R4_EXTEND_CFG_RESET1;
		/* If there's a reset2 event, then C4 is used as an output for the reset signal */
		if (adp5585->nkeys_reset2)
			reg_val |= ADP5585_C4_EXTEND_CFG_RESET2;

		ret = regmap_update_bits(adp5585->regmap, regs->ext_cfg,
					 ADP5585_C4_EXTEND_CFG_MASK | ADP5585_R4_EXTEND_CFG_MASK,
					 reg_val);
		if (ret)
			return ret;
	}

	/* Clear any possible event by reading all the FIFO entries */
	for (i = 0; i < ADP5585_EV_MAX; i++) {
		ret = regmap_read(adp5585->regmap, ADP5585_FIFO_1 + i, &reg_val);
		if (ret)
			return ret;
	}

	ret = regmap_write(adp5585->regmap, regs->poll_ptime_cfg, adp5585->ev_poll_time);
	if (ret)
		return ret;

	/*
	 * Enable the internal oscillator, as it's shared between multiple
	 * functions.
	 */
	ret = regmap_write(adp5585->regmap, regs->gen_cfg,
			   ADP5585_OSC_FREQ_500KHZ | ADP5585_INT_CFG | ADP5585_OSC_EN);
	if (ret)
		return ret;

	return devm_add_action_or_reset(adp5585->dev, adp5585_osc_disable, adp5585);
}

static int adp5585_parse_fw(struct adp5585_dev *adp5585)
{
	unsigned int prop_val;
	int ret;

	ret = device_property_read_u32(adp5585->dev, "poll-interval", &prop_val);
	if (!ret) {
		adp5585->ev_poll_time = prop_val / 10 - 1;
		/*
		 * ev_poll_time is the raw value to be written on the register and 0 to 3 are the
		 * valid values.
		 */
		if (adp5585->ev_poll_time > 3)
			return dev_err_probe(adp5585->dev, -EINVAL,
					     "Invalid value(%u) for poll-interval\n", prop_val);
	}

	ret = adp5585_unlock_ev_parse(adp5585);
	if (ret)
		return ret;

	return adp5585_reset_ev_parse(adp5585);
}

static void adp5585_irq_disable(void *data)
{
	struct adp5585_dev *adp5585 = data;

	regmap_write(adp5585->regmap, adp5585->regs->int_en, 0);
}

static int adp5585_irq_enable(struct i2c_client *i2c,
			      struct adp5585_dev *adp5585)
{
	const struct adp5585_regs *regs = adp5585->regs;
	unsigned int stat;
	int ret;

	if (i2c->irq <= 0)
		return 0;

	ret = devm_request_threaded_irq(&i2c->dev, i2c->irq, NULL, adp5585_irq,
					IRQF_ONESHOT, i2c->name, adp5585);
	if (ret)
		return ret;

	/*
	 * Clear any possible outstanding interrupt before enabling them. We do that by reading
	 * the status register and writing back the same value.
	 */
	ret = regmap_read(adp5585->regmap, ADP5585_INT_STATUS, &stat);
	if (ret)
		return ret;

	ret = regmap_write(adp5585->regmap, ADP5585_INT_STATUS, stat);
	if (ret)
		return ret;

	ret = regmap_write(adp5585->regmap, regs->int_en, ADP5585_OVRFLOW_IEN | ADP5585_EVENT_IEN);
	if (ret)
		return ret;

	return devm_add_action_or_reset(&i2c->dev, adp5585_irq_disable, adp5585);
}

static int adp5585_i2c_probe(struct i2c_client *i2c)
{
	struct regmap_config *regmap_config;
	struct adp5585_dev *adp5585;
	struct gpio_desc *gpio;
	unsigned int id;
	int ret;

	adp5585 = devm_kzalloc(&i2c->dev, sizeof(*adp5585), GFP_KERNEL);
	if (!adp5585)
		return -ENOMEM;

	i2c_set_clientdata(i2c, adp5585);
	adp5585->dev = &i2c->dev;
	adp5585->irq = i2c->irq;
	BLOCKING_INIT_NOTIFIER_HEAD(&adp5585->event_notifier);

	adp5585->variant = (enum adp5585_variant)(uintptr_t)i2c_get_match_data(i2c);
	if (!adp5585->variant)
		return -ENODEV;

	regmap_config = adp5585_fill_variant_config(adp5585);
	if (IS_ERR(regmap_config))
		return PTR_ERR(regmap_config);

	ret = devm_regulator_get_enable(&i2c->dev, "vdd");
	if (ret)
		return ret;

	gpio = devm_gpiod_get_optional(&i2c->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(gpio))
		return PTR_ERR(gpio);

	/*
	 * Note the timings are not documented anywhere in the datasheet. They are just
	 * reasonable values that work.
	 */
	if (gpio) {
		fsleep(30);
		gpiod_set_value_cansleep(gpio, 0);
		fsleep(60);
	}

	adp5585->regmap = devm_regmap_init_i2c(i2c, regmap_config);
	if (IS_ERR(adp5585->regmap))
		return dev_err_probe(&i2c->dev, PTR_ERR(adp5585->regmap),
				     "Failed to initialize register map\n");

	ret = regmap_read(adp5585->regmap, ADP5585_ID, &id);
	if (ret)
		return dev_err_probe(&i2c->dev, ret,
				     "Failed to read device ID\n");

	id &= ADP5585_MAN_ID_MASK;
	if (id != adp5585->id)
		return dev_err_probe(&i2c->dev, -ENODEV,
				     "Invalid device ID 0x%02x\n", id);

	adp5585->pin_usage = devm_bitmap_zalloc(&i2c->dev, adp5585->n_pins, GFP_KERNEL);
	if (!adp5585->pin_usage)
		return -ENOMEM;

	ret = adp5585_parse_fw(adp5585);
	if (ret)
		return ret;

	ret = adp5585_setup(adp5585);
	if (ret)
		return ret;

	ret = adp5585_add_devices(adp5585);
	if (ret)
		return ret;

	return adp5585_irq_enable(i2c, adp5585);
}

static int adp5585_suspend(struct device *dev)
{
	struct adp5585_dev *adp5585 = dev_get_drvdata(dev);

	if (adp5585->irq)
		disable_irq(adp5585->irq);

	regcache_cache_only(adp5585->regmap, true);

	return 0;
}

static int adp5585_resume(struct device *dev)
{
	struct adp5585_dev *adp5585 = dev_get_drvdata(dev);
	int ret;

	regcache_cache_only(adp5585->regmap, false);
	regcache_mark_dirty(adp5585->regmap);

	ret = regcache_sync(adp5585->regmap);
	if (ret)
		return ret;

	if (adp5585->irq)
		enable_irq(adp5585->irq);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(adp5585_pm, adp5585_suspend, adp5585_resume);

static const struct of_device_id adp5585_of_match[] = {
	{
		.compatible = "adi,adp5585-00",
		.data = (void *)ADP5585_00,
	}, {
		.compatible = "adi,adp5585-01",
		.data = (void *)ADP5585_01,
	}, {
		.compatible = "adi,adp5585-02",
		.data = (void *)ADP5585_02,
	}, {
		.compatible = "adi,adp5585-03",
		.data = (void *)ADP5585_03,
	}, {
		.compatible = "adi,adp5585-04",
		.data = (void *)ADP5585_04,
	}, {
		.compatible = "adi,adp5589-00",
		.data = (void *)ADP5589_00,
	}, {
		.compatible = "adi,adp5589-01",
		.data = (void *)ADP5589_01,
	}, {
		.compatible = "adi,adp5589-02",
		.data = (void *)ADP5589_02,
	}, {
		.compatible = "adi,adp5589",
		.data = (void *)ADP5589_00,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, adp5585_of_match);

static struct i2c_driver adp5585_i2c_driver = {
	.driver = {
		.name = "adp5585",
		.of_match_table = adp5585_of_match,
		.pm = pm_sleep_ptr(&adp5585_pm),
	},
	.probe = adp5585_i2c_probe,
};
module_i2c_driver(adp5585_i2c_driver);

MODULE_DESCRIPTION("ADP5585 core driver");
MODULE_AUTHOR("Haibo Chen <haibo.chen@nxp.com>");
MODULE_LICENSE("GPL");
