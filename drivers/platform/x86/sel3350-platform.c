// SPDX-License-Identifier: GPL-2.0-only OR BSD-3-Clause
/*
 * Copyright 2023 Schweitzer Engineering Laboratories, Inc.
 * 2350 NE Hopkins Court, Pullman, WA 99163 USA
 *
 * Platform support for the b2093 mainboard used in SEL-3350 computers.
 * Consumes GPIO from the SoC to provide standard LED and power supply
 * devices.
 */

#include <linux/acpi.h>
#include <linux/array_size.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>

/* Broxton communities */
#define BXT_NW "INT3452:01"
#define BXT_W  "INT3452:02"
#define BXT_SW "INT3452:03"

#define B2093_GPIO_ACPI_ID "SEL0003"

#define SEL_PS_A        "sel_ps_a"
#define SEL_PS_A_DETECT "sel_ps_a_detect"
#define SEL_PS_A_GOOD   "sel_ps_a_good"
#define SEL_PS_B        "sel_ps_b"
#define SEL_PS_B_DETECT "sel_ps_b_detect"
#define SEL_PS_B_GOOD   "sel_ps_b_good"

#define AUX_LED_GRN1        "sel_aux_led_grn1"
#define AUX_LED_GRN2        "sel_aux_led_grn2"
#define AUX_LED_GRN3        "sel_aux_led_grn3"
#define AUX_LED_GRN4        "sel_aux_led_grn4"
#define ALARM_STATE_USER    "sel_alarm_state_user"
#define ENABLE_STATE_USER   "sel_enable_state_user"
#define AUX_LED_RED1        "sel_aux_led_red1"
#define AUX_LED_RED2        "sel_aux_led_red2"
#define AUX_LED_RED3        "sel_aux_led_red3"
#define AUX_LED_RED4        "sel_aux_led_red4"

static const char *const sel3350_leds_gpio_names[] = {
	AUX_LED_GRN1,
	AUX_LED_GRN2,
	AUX_LED_GRN3,
	AUX_LED_GRN4,
	ALARM_STATE_USER,
	ENABLE_STATE_USER,
	AUX_LED_RED1,
	AUX_LED_RED2,
	AUX_LED_RED3,
	AUX_LED_RED4,
};

/* LEDs */
static struct gpio_led sel3350_leds[] = {
	{ .name = "sel:green:aux1",
	  .default_state = LEDS_GPIO_DEFSTATE_KEEP,
	  .retain_state_suspended = 1,
	  .retain_state_shutdown = 1,
	},
	{ .name = "sel:green:aux2",
	  .default_state = LEDS_GPIO_DEFSTATE_KEEP,
	  .retain_state_suspended = 1,
	  .retain_state_shutdown = 1,
	},
	{ .name = "sel:green:aux3",
	  .default_state = LEDS_GPIO_DEFSTATE_KEEP,
	  .retain_state_suspended = 1,
	  .retain_state_shutdown = 1,
	},
	{ .name = "sel:green:aux4",
	  .default_state = LEDS_GPIO_DEFSTATE_KEEP,
	  .retain_state_suspended = 1,
	  .retain_state_shutdown = 1,
	},
	{ .name = "sel:red:alarm",
	  .default_state = LEDS_GPIO_DEFSTATE_KEEP,
	  .retain_state_suspended = 1,
	  .retain_state_shutdown = 1,
	},
	{ .name = "sel:green:enabled",
	  .default_state = LEDS_GPIO_DEFSTATE_KEEP,
	  .retain_state_suspended = 1,
	  .retain_state_shutdown = 1,
	},
	{ .name = "sel:red:aux1",
	  .default_state = LEDS_GPIO_DEFSTATE_KEEP,
	  .retain_state_suspended = 1,
	  .retain_state_shutdown = 1,
	},
	{ .name = "sel:red:aux2",
	  .default_state = LEDS_GPIO_DEFSTATE_KEEP,
	  .retain_state_suspended = 1,
	  .retain_state_shutdown = 1,
	},
	{ .name = "sel:red:aux3",
	  .default_state = LEDS_GPIO_DEFSTATE_KEEP,
	  .retain_state_suspended = 1,
	  .retain_state_shutdown = 1,
	},
	{ .name = "sel:red:aux4",
	  .default_state = LEDS_GPIO_DEFSTATE_KEEP,
	  .retain_state_suspended = 1,
	  .retain_state_shutdown = 1,
	},
};

static const struct gpio_led_platform_data sel3350_leds_pdata = {
	.num_leds = ARRAY_SIZE(sel3350_leds),
	.leds = sel3350_leds,
};

static struct gpiod_lookup_table sel3350_gpios_table = {
	.dev_id = B2093_GPIO_ACPI_ID ":00",
	.table = {
		GPIO_LOOKUP(BXT_NW, 44, SEL_PS_A_DETECT, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP(BXT_NW, 45, SEL_PS_A_GOOD,   GPIO_ACTIVE_LOW),
		GPIO_LOOKUP(BXT_NW, 46, SEL_PS_B_DETECT, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP(BXT_NW, 47, SEL_PS_B_GOOD,   GPIO_ACTIVE_LOW),
		GPIO_LOOKUP(BXT_NW, 49, AUX_LED_GRN1, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP(BXT_NW, 50, AUX_LED_GRN2, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP(BXT_NW, 51, AUX_LED_GRN3, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP(BXT_NW, 52, AUX_LED_GRN4, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP(BXT_W,  20, ALARM_STATE_USER, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP(BXT_W,  21, ENABLE_STATE_USER, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP(BXT_SW, 37, AUX_LED_RED1, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP(BXT_SW, 38, AUX_LED_RED2, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP(BXT_SW, 39, AUX_LED_RED3, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP(BXT_SW, 40, AUX_LED_RED4, GPIO_ACTIVE_HIGH),
		{},
	}
};

/* Power Supplies */

struct sel3350_power_cfg_data {
	struct gpio_desc *ps_detect;
	struct gpio_desc *ps_good;
};

static int sel3350_power_get_property(struct power_supply *psy,
				      enum power_supply_property psp,
				      union power_supply_propval *val)
{
	struct sel3350_power_cfg_data *data = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_HEALTH:
		if (gpiod_get_value(data->ps_detect)) {
			if (gpiod_get_value(data->ps_good))
				val->intval = POWER_SUPPLY_HEALTH_GOOD;
			else
				val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		} else {
			val->intval = POWER_SUPPLY_HEALTH_UNKNOWN;
		}
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = gpiod_get_value(data->ps_detect);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = gpiod_get_value(data->ps_good);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static const enum power_supply_property sel3350_power_properties[] = {
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
};

static const struct power_supply_desc sel3350_ps_a_desc = {
	.name = SEL_PS_A,
	.type = POWER_SUPPLY_TYPE_MAINS,
	.properties = sel3350_power_properties,
	.num_properties = ARRAY_SIZE(sel3350_power_properties),
	.get_property = sel3350_power_get_property,
};

static const struct power_supply_desc sel3350_ps_b_desc = {
	.name = SEL_PS_B,
	.type = POWER_SUPPLY_TYPE_MAINS,
	.properties = sel3350_power_properties,
	.num_properties = ARRAY_SIZE(sel3350_power_properties),
	.get_property = sel3350_power_get_property,
};

struct sel3350_data {
	struct platform_device *leds_pdev;
	struct power_supply *ps_a;
	struct power_supply *ps_b;
	struct sel3350_power_cfg_data ps_a_cfg_data;
	struct sel3350_power_cfg_data ps_b_cfg_data;
};

static int sel3350_probe(struct platform_device *pdev)
{
	int rs;
	int i;
	struct sel3350_data *sel3350;
	struct power_supply_config ps_cfg = {};

	sel3350 = devm_kzalloc(&pdev->dev, sizeof(struct sel3350_data), GFP_KERNEL);
	if (!sel3350)
		return -ENOMEM;

	platform_set_drvdata(pdev, sel3350);

	gpiod_add_lookup_table(&sel3350_gpios_table);

	for (i = 0; i < ARRAY_SIZE(sel3350_leds); ++i) {
		sel3350_leds[i].gpiod = devm_gpiod_get(&pdev->dev,
						       sel3350_leds_gpio_names[i],
						       GPIOD_ASIS);
		if (IS_ERR_OR_NULL(sel3350_leds[i].gpiod)) {
			rs = -EPROBE_DEFER;
			goto err_gpio_loop;
		}
		gpiod_set_consumer_name(sel3350_leds[i].gpiod, sel3350_leds[i].name);
	}

	sel3350->leds_pdev = platform_device_register_data(
			NULL,
			"leds-gpio",
			PLATFORM_DEVID_NONE,
			&sel3350_leds_pdata,
			sizeof(sel3350_leds_pdata));
	if (IS_ERR(sel3350->leds_pdev)) {
		rs = PTR_ERR(sel3350->leds_pdev);
		dev_err(&pdev->dev, "Failed registering platform device: %d\n", rs);
		goto err_platform;
	}

	/* Power Supply A */
	sel3350->ps_a_cfg_data.ps_detect = devm_gpiod_get(&pdev->dev,
							  SEL_PS_A_DETECT,
							  GPIOD_IN);
	sel3350->ps_a_cfg_data.ps_good = devm_gpiod_get(&pdev->dev,
							SEL_PS_A_GOOD,
							GPIOD_IN);
	ps_cfg.drv_data = &sel3350->ps_a_cfg_data;
	sel3350->ps_a = devm_power_supply_register(&pdev->dev,
						   &sel3350_ps_a_desc,
						   &ps_cfg);
	if (IS_ERR(sel3350->ps_a)) {
		rs = PTR_ERR(sel3350->ps_a);
		dev_err(&pdev->dev, "Failed registering power supply A: %d\n", rs);
		goto err_ps;
	}

	/* Power Supply B */
	sel3350->ps_b_cfg_data.ps_detect = devm_gpiod_get(&pdev->dev,
							  SEL_PS_B_DETECT,
							  GPIOD_IN);
	sel3350->ps_b_cfg_data.ps_good = devm_gpiod_get(&pdev->dev,
							SEL_PS_B_GOOD,
							GPIOD_IN);
	ps_cfg.drv_data = &sel3350->ps_b_cfg_data;
	sel3350->ps_b = devm_power_supply_register(&pdev->dev,
						   &sel3350_ps_b_desc,
						   &ps_cfg);
	if (IS_ERR(sel3350->ps_b)) {
		rs = PTR_ERR(sel3350->ps_b);
		dev_err(&pdev->dev, "Failed registering power supply B: %d\n", rs);
		goto err_ps;
	}

	return 0;

err_gpio_loop:
	while (i--)
		devm_gpiod_put(&pdev->dev, sel3350_leds[i].gpiod);
	goto err_platform;

err_ps:
	platform_device_unregister(sel3350->leds_pdev);
err_platform:
	gpiod_remove_lookup_table(&sel3350_gpios_table);

	return rs;
}

static void sel3350_remove(struct platform_device *pdev)
{
	struct sel3350_data *sel3350 = platform_get_drvdata(pdev);

	platform_device_unregister(sel3350->leds_pdev);
	gpiod_remove_lookup_table(&sel3350_gpios_table);
}

static const struct acpi_device_id sel3350_device_ids[] = {
	{ B2093_GPIO_ACPI_ID, 0 },
	{ "", 0 },
};
MODULE_DEVICE_TABLE(acpi, sel3350_device_ids);

static struct platform_driver sel3350_platform_driver = {
	.probe = sel3350_probe,
	.remove = sel3350_remove,
	.driver = {
		.name = "sel3350-platform",
		.acpi_match_table = sel3350_device_ids,
	},
};
module_platform_driver(sel3350_platform_driver);

MODULE_AUTHOR("Schweitzer Engineering Laboratories");
MODULE_DESCRIPTION("SEL-3350 platform driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_SOFTDEP("pre: pinctrl_broxton leds-gpio");
