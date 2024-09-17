// SPDX-License-Identifier: GPL-2.0+

/*
 * Cisco Meraki MX100 (Tinkerbell) board platform driver
 *
 * Based off of arch/x86/platform/meraki/tink.c from the
 * Meraki GPL release meraki-firmware-sources-r23-20150601
 *
 * Format inspired by platform/x86/pcengines-apuv2.c
 *
 * Copyright (C) 2021 Chris Blake <chrisrblake93@gmail.com>
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/dmi.h>
#include <linux/err.h>
#include <linux/gpio_keys.h>
#include <linux/gpio/machine.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define TINK_GPIO_DRIVER_NAME "gpio_ich"

/* LEDs */
static const struct gpio_led tink_leds[] = {
	{
		.name = "mx100:green:internet",
		.default_trigger = "default-on",
	},
	{
		.name = "mx100:green:lan2",
	},
	{
		.name = "mx100:green:lan3",
	},
	{
		.name = "mx100:green:lan4",
	},
	{
		.name = "mx100:green:lan5",
	},
	{
		.name = "mx100:green:lan6",
	},
	{
		.name = "mx100:green:lan7",
	},
	{
		.name = "mx100:green:lan8",
	},
	{
		.name = "mx100:green:lan9",
	},
	{
		.name = "mx100:green:lan10",
	},
	{
		.name = "mx100:green:lan11",
	},
	{
		.name = "mx100:green:ha",
	},
	{
		.name = "mx100:orange:ha",
	},
	{
		.name = "mx100:green:usb",
	},
	{
		.name = "mx100:orange:usb",
	},
};

static const struct gpio_led_platform_data tink_leds_pdata = {
	.num_leds	= ARRAY_SIZE(tink_leds),
	.leds		= tink_leds,
};

static struct gpiod_lookup_table tink_leds_table = {
	.dev_id = "leds-gpio",
	.table = {
		GPIO_LOOKUP_IDX(TINK_GPIO_DRIVER_NAME, 11,
				NULL, 0, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX(TINK_GPIO_DRIVER_NAME, 18,
				NULL, 1, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX(TINK_GPIO_DRIVER_NAME, 20,
				NULL, 2, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX(TINK_GPIO_DRIVER_NAME, 22,
				NULL, 3, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX(TINK_GPIO_DRIVER_NAME, 23,
				NULL, 4, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX(TINK_GPIO_DRIVER_NAME, 32,
				NULL, 5, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX(TINK_GPIO_DRIVER_NAME, 34,
				NULL, 6, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX(TINK_GPIO_DRIVER_NAME, 35,
				NULL, 7, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX(TINK_GPIO_DRIVER_NAME, 36,
				NULL, 8, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX(TINK_GPIO_DRIVER_NAME, 37,
				NULL, 9, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX(TINK_GPIO_DRIVER_NAME, 48,
				NULL, 10, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX(TINK_GPIO_DRIVER_NAME, 16,
				NULL, 11, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX(TINK_GPIO_DRIVER_NAME, 7,
				NULL, 12, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX(TINK_GPIO_DRIVER_NAME, 21,
				NULL, 13, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX(TINK_GPIO_DRIVER_NAME, 19,
				NULL, 14, GPIO_ACTIVE_LOW),
		{} /* Terminating entry */
	}
};

/* Reset Button */
static struct gpio_keys_button tink_buttons[] = {
	{
		.desc			= "Reset",
		.type			= EV_KEY,
		.code			= KEY_RESTART,
		.active_low             = 1,
		.debounce_interval      = 100,
	},
};

static const struct gpio_keys_platform_data tink_buttons_pdata = {
	.buttons	= tink_buttons,
	.nbuttons	= ARRAY_SIZE(tink_buttons),
	.poll_interval  = 20,
	.rep		= 0,
	.name		= "mx100-keys",
};

static struct gpiod_lookup_table tink_keys_table = {
	.dev_id = "gpio-keys-polled",
	.table = {
		GPIO_LOOKUP_IDX(TINK_GPIO_DRIVER_NAME, 60,
				NULL, 0, GPIO_ACTIVE_LOW),
		{} /* Terminating entry */
	}
};

/* Board setup */
static const struct dmi_system_id tink_systems[] __initconst = {
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Cisco"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "MX100-HW"),
		},
	},
	{} /* Terminating entry */
};
MODULE_DEVICE_TABLE(dmi, tink_systems);

static struct platform_device *tink_leds_pdev;
static struct platform_device *tink_keys_pdev;

static struct platform_device * __init tink_create_dev(
	const char *name, const void *pdata, size_t sz)
{
	struct platform_device *pdev;

	pdev = platform_device_register_data(NULL,
		name, PLATFORM_DEVID_NONE, pdata, sz);
	if (IS_ERR(pdev))
		pr_err("failed registering %s: %ld\n", name, PTR_ERR(pdev));

	return pdev;
}

static int __init tink_board_init(void)
{
	int ret;

	if (!dmi_first_match(tink_systems))
		return -ENODEV;

	/*
	 * We need to make sure that GPIO60 isn't set to native mode as is default since it's our
	 * Reset Button. To do this, write to GPIO_USE_SEL2 to have GPIO60 set to GPIO mode.
	 * This is documented on page 1609 of the PCH datasheet, order number 327879-005US
	 */
	outl(inl(0x530) | BIT(28), 0x530);

	gpiod_add_lookup_table(&tink_leds_table);
	gpiod_add_lookup_table(&tink_keys_table);

	tink_leds_pdev = tink_create_dev("leds-gpio",
		&tink_leds_pdata, sizeof(tink_leds_pdata));
	if (IS_ERR(tink_leds_pdev)) {
		ret = PTR_ERR(tink_leds_pdev);
		goto err;
	}

	tink_keys_pdev = tink_create_dev("gpio-keys-polled",
		&tink_buttons_pdata, sizeof(tink_buttons_pdata));
	if (IS_ERR(tink_keys_pdev)) {
		ret = PTR_ERR(tink_keys_pdev);
		platform_device_unregister(tink_leds_pdev);
		goto err;
	}

	return 0;

err:
	gpiod_remove_lookup_table(&tink_keys_table);
	gpiod_remove_lookup_table(&tink_leds_table);
	return ret;
}
module_init(tink_board_init);

static void __exit tink_board_exit(void)
{
	platform_device_unregister(tink_keys_pdev);
	platform_device_unregister(tink_leds_pdev);
	gpiod_remove_lookup_table(&tink_keys_table);
	gpiod_remove_lookup_table(&tink_leds_table);
}
module_exit(tink_board_exit);

MODULE_AUTHOR("Chris Blake <chrisrblake93@gmail.com>");
MODULE_DESCRIPTION("Cisco Meraki MX100 Platform Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:meraki-mx100");
