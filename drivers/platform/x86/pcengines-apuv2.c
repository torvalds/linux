// SPDX-License-Identifier: GPL-2.0+

/*
 * PC-Engines APUv2/APUv3 board platform driver
 * for GPIO buttons and LEDs
 *
 * Copyright (C) 2018 metux IT consult
 * Author: Enrico Weigelt <info@metux.net>
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/dmi.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio_keys.h>
#include <linux/gpio/machine.h>
#include <linux/input.h>
#include <linux/platform_data/gpio/gpio-amd-fch.h>

/*
 * NOTE: this driver only supports APUv2/3 - not APUv1, as this one
 * has completely different register layouts.
 */

/* Register mappings */
#define APU2_GPIO_REG_LED1		AMD_FCH_GPIO_REG_GPIO57
#define APU2_GPIO_REG_LED2		AMD_FCH_GPIO_REG_GPIO58
#define APU2_GPIO_REG_LED3		AMD_FCH_GPIO_REG_GPIO59_DEVSLP1
#define APU2_GPIO_REG_MODESW		AMD_FCH_GPIO_REG_GPIO32_GE1
#define APU2_GPIO_REG_SIMSWAP		AMD_FCH_GPIO_REG_GPIO33_GE2
#define APU2_GPIO_REG_MPCIE2		AMD_FCH_GPIO_REG_GPIO55_DEVSLP0
#define APU2_GPIO_REG_MPCIE3		AMD_FCH_GPIO_REG_GPIO51

/* Order in which the GPIO lines are defined in the register list */
#define APU2_GPIO_LINE_LED1		0
#define APU2_GPIO_LINE_LED2		1
#define APU2_GPIO_LINE_LED3		2
#define APU2_GPIO_LINE_MODESW		3
#define APU2_GPIO_LINE_SIMSWAP		4
#define APU2_GPIO_LINE_MPCIE2		5
#define APU2_GPIO_LINE_MPCIE3		6

/* GPIO device */

static int apu2_gpio_regs[] = {
	[APU2_GPIO_LINE_LED1]		= APU2_GPIO_REG_LED1,
	[APU2_GPIO_LINE_LED2]		= APU2_GPIO_REG_LED2,
	[APU2_GPIO_LINE_LED3]		= APU2_GPIO_REG_LED3,
	[APU2_GPIO_LINE_MODESW]		= APU2_GPIO_REG_MODESW,
	[APU2_GPIO_LINE_SIMSWAP]	= APU2_GPIO_REG_SIMSWAP,
	[APU2_GPIO_LINE_MPCIE2]		= APU2_GPIO_REG_MPCIE2,
	[APU2_GPIO_LINE_MPCIE3]		= APU2_GPIO_REG_MPCIE3,
};

static const char * const apu2_gpio_names[] = {
	[APU2_GPIO_LINE_LED1]		= "front-led1",
	[APU2_GPIO_LINE_LED2]		= "front-led2",
	[APU2_GPIO_LINE_LED3]		= "front-led3",
	[APU2_GPIO_LINE_MODESW]		= "front-button",
	[APU2_GPIO_LINE_SIMSWAP]	= "simswap",
	[APU2_GPIO_LINE_MPCIE2]		= "mpcie2_reset",
	[APU2_GPIO_LINE_MPCIE3]		= "mpcie3_reset",
};

static const struct amd_fch_gpio_pdata board_apu2 = {
	.gpio_num	= ARRAY_SIZE(apu2_gpio_regs),
	.gpio_reg	= apu2_gpio_regs,
	.gpio_names	= apu2_gpio_names,
};

/* GPIO LEDs device */

static const struct gpio_led apu2_leds[] = {
	{ .name = "apu:green:1" },
	{ .name = "apu:green:2" },
	{ .name = "apu:green:3" },
};

static const struct gpio_led_platform_data apu2_leds_pdata = {
	.num_leds	= ARRAY_SIZE(apu2_leds),
	.leds		= apu2_leds,
};

static struct gpiod_lookup_table gpios_led_table = {
	.dev_id = "leds-gpio",
	.table = {
		GPIO_LOOKUP_IDX(AMD_FCH_GPIO_DRIVER_NAME, APU2_GPIO_LINE_LED1,
				NULL, 0, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX(AMD_FCH_GPIO_DRIVER_NAME, APU2_GPIO_LINE_LED2,
				NULL, 1, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX(AMD_FCH_GPIO_DRIVER_NAME, APU2_GPIO_LINE_LED3,
				NULL, 2, GPIO_ACTIVE_LOW),
		{} /* Terminating entry */
	}
};

/* GPIO keyboard device */

static struct gpio_keys_button apu2_keys_buttons[] = {
	{
		.code			= KEY_RESTART,
		.active_low		= 1,
		.desc			= "front button",
		.type			= EV_KEY,
		.debounce_interval	= 10,
		.value			= 1,
	},
};

static const struct gpio_keys_platform_data apu2_keys_pdata = {
	.buttons	= apu2_keys_buttons,
	.nbuttons	= ARRAY_SIZE(apu2_keys_buttons),
	.poll_interval	= 100,
	.rep		= 0,
	.name		= "apu2-keys",
};

static struct gpiod_lookup_table gpios_key_table = {
	.dev_id = "gpio-keys-polled",
	.table = {
		GPIO_LOOKUP_IDX(AMD_FCH_GPIO_DRIVER_NAME, APU2_GPIO_LINE_MODESW,
				NULL, 0, GPIO_ACTIVE_LOW),
		{} /* Terminating entry */
	}
};

/* Board setup */

/* Note: matching works on string prefix, so "apu2" must come before "apu" */
static const struct dmi_system_id apu_gpio_dmi_table[] __initconst = {

	/* APU2 w/ legacy BIOS < 4.0.8 */
	{
		.ident		= "apu2",
		.matches	= {
			DMI_MATCH(DMI_SYS_VENDOR, "PC Engines"),
			DMI_MATCH(DMI_BOARD_NAME, "APU2")
		},
		.driver_data	= (void *)&board_apu2,
	},
	/* APU2 w/ legacy BIOS >= 4.0.8 */
	{
		.ident		= "apu2",
		.matches	= {
			DMI_MATCH(DMI_SYS_VENDOR, "PC Engines"),
			DMI_MATCH(DMI_BOARD_NAME, "apu2")
		},
		.driver_data	= (void *)&board_apu2,
	},
	/* APU2 w/ mainline BIOS */
	{
		.ident		= "apu2",
		.matches	= {
			DMI_MATCH(DMI_SYS_VENDOR, "PC Engines"),
			DMI_MATCH(DMI_BOARD_NAME, "PC Engines apu2")
		},
		.driver_data	= (void *)&board_apu2,
	},

	/* APU3 w/ legacy BIOS < 4.0.8 */
	{
		.ident		= "apu3",
		.matches	= {
			DMI_MATCH(DMI_SYS_VENDOR, "PC Engines"),
			DMI_MATCH(DMI_BOARD_NAME, "APU3")
		},
		.driver_data = (void *)&board_apu2,
	},
	/* APU3 w/ legacy BIOS >= 4.0.8 */
	{
		.ident       = "apu3",
		.matches     = {
			DMI_MATCH(DMI_SYS_VENDOR, "PC Engines"),
			DMI_MATCH(DMI_BOARD_NAME, "apu3")
		},
		.driver_data = (void *)&board_apu2,
	},
	/* APU3 w/ mainline BIOS */
	{
		.ident       = "apu3",
		.matches     = {
			DMI_MATCH(DMI_SYS_VENDOR, "PC Engines"),
			DMI_MATCH(DMI_BOARD_NAME, "PC Engines apu3")
		},
		.driver_data = (void *)&board_apu2,
	},
	/* APU4 w/ legacy BIOS < 4.0.8 */
	{
		.ident        = "apu4",
		.matches    = {
			DMI_MATCH(DMI_SYS_VENDOR, "PC Engines"),
			DMI_MATCH(DMI_BOARD_NAME, "APU4")
		},
		.driver_data = (void *)&board_apu2,
	},
	/* APU4 w/ legacy BIOS >= 4.0.8 */
	{
		.ident       = "apu4",
		.matches     = {
			DMI_MATCH(DMI_SYS_VENDOR, "PC Engines"),
			DMI_MATCH(DMI_BOARD_NAME, "apu4")
		},
		.driver_data = (void *)&board_apu2,
	},
	/* APU4 w/ mainline BIOS */
	{
		.ident       = "apu4",
		.matches     = {
			DMI_MATCH(DMI_SYS_VENDOR, "PC Engines"),
			DMI_MATCH(DMI_BOARD_NAME, "PC Engines apu4")
		},
		.driver_data = (void *)&board_apu2,
	},
	{}
};

static struct platform_device *apu_gpio_pdev;
static struct platform_device *apu_leds_pdev;
static struct platform_device *apu_keys_pdev;

static struct platform_device * __init apu_create_pdev(
	const char *name,
	const void *pdata,
	size_t sz)
{
	struct platform_device *pdev;

	pdev = platform_device_register_resndata(NULL,
		name,
		PLATFORM_DEVID_NONE,
		NULL,
		0,
		pdata,
		sz);

	if (IS_ERR(pdev))
		pr_err("failed registering %s: %ld\n", name, PTR_ERR(pdev));

	return pdev;
}

static int __init apu_board_init(void)
{
	const struct dmi_system_id *id;

	id = dmi_first_match(apu_gpio_dmi_table);
	if (!id) {
		pr_err("failed to detect APU board via DMI\n");
		return -ENODEV;
	}

	gpiod_add_lookup_table(&gpios_led_table);
	gpiod_add_lookup_table(&gpios_key_table);

	apu_gpio_pdev = apu_create_pdev(
		AMD_FCH_GPIO_DRIVER_NAME,
		id->driver_data,
		sizeof(struct amd_fch_gpio_pdata));

	apu_leds_pdev = apu_create_pdev(
		"leds-gpio",
		&apu2_leds_pdata,
		sizeof(apu2_leds_pdata));

	apu_keys_pdev = apu_create_pdev(
		"gpio-keys-polled",
		&apu2_keys_pdata,
		sizeof(apu2_keys_pdata));

	return 0;
}

static void __exit apu_board_exit(void)
{
	gpiod_remove_lookup_table(&gpios_led_table);
	gpiod_remove_lookup_table(&gpios_key_table);

	platform_device_unregister(apu_keys_pdev);
	platform_device_unregister(apu_leds_pdev);
	platform_device_unregister(apu_gpio_pdev);
}

module_init(apu_board_init);
module_exit(apu_board_exit);

MODULE_AUTHOR("Enrico Weigelt, metux IT consult <info@metux.net>");
MODULE_DESCRIPTION("PC Engines APUv2/APUv3 board GPIO/LEDs/keys driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(dmi, apu_gpio_dmi_table);
MODULE_SOFTDEP("pre: platform:" AMD_FCH_GPIO_DRIVER_NAME " platform:leds-gpio platform:gpio_keys_polled");
