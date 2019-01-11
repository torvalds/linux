/*
 * drivers/leds/leds-mlxcpld.c
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Vadim Pasternak <vadimp@mellanox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/io.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define MLXPLAT_CPLD_LPC_REG_BASE_ADRR     0x2500 /* LPC bus access */

/* Color codes for LEDs */
#define MLXCPLD_LED_OFFSET_HALF		0x01 /* Offset from solid: 3Hz blink */
#define MLXCPLD_LED_OFFSET_FULL		0x02 /* Offset from solid: 6Hz blink */
#define MLXCPLD_LED_IS_OFF		0x00 /* Off */
#define MLXCPLD_LED_RED_STATIC_ON	0x05 /* Solid red */
#define MLXCPLD_LED_RED_BLINK_HALF	(MLXCPLD_LED_RED_STATIC_ON + \
					 MLXCPLD_LED_OFFSET_HALF)
#define MLXCPLD_LED_RED_BLINK_FULL	(MLXCPLD_LED_RED_STATIC_ON + \
					 MLXCPLD_LED_OFFSET_FULL)
#define MLXCPLD_LED_GREEN_STATIC_ON	0x0D /* Solid green */
#define MLXCPLD_LED_GREEN_BLINK_HALF	(MLXCPLD_LED_GREEN_STATIC_ON + \
					 MLXCPLD_LED_OFFSET_HALF)
#define MLXCPLD_LED_GREEN_BLINK_FULL	(MLXCPLD_LED_GREEN_STATIC_ON + \
					 MLXCPLD_LED_OFFSET_FULL)
#define MLXCPLD_LED_BLINK_3HZ		167 /* ~167 msec off/on */
#define MLXCPLD_LED_BLINK_6HZ		83 /* ~83 msec off/on */

/**
 * mlxcpld_param - LED access parameters:
 * @offset - offset for LED access in CPLD device
 * @mask - mask for LED access in CPLD device
 * @base_color - base color code for LED
**/
struct mlxcpld_param {
	u8 offset;
	u8 mask;
	u8 base_color;
};

/**
 * mlxcpld_led_priv - LED private data:
 * @cled - LED class device instance
 * @param - LED CPLD access parameters
**/
struct mlxcpld_led_priv {
	struct led_classdev cdev;
	struct mlxcpld_param param;
};

#define cdev_to_priv(c)		container_of(c, struct mlxcpld_led_priv, cdev)

/**
 * mlxcpld_led_profile - system LED profile (defined per system class):
 * @offset - offset for LED access in CPLD device
 * @mask - mask for LED access in CPLD device
 * @base_color - base color code
 * @brightness - default brightness setting (on/off)
 * @name - LED name
**/
struct mlxcpld_led_profile {
	u8 offset;
	u8 mask;
	u8 base_color;
	enum led_brightness brightness;
	const char *name;
};

/**
 * mlxcpld_led_pdata - system LED private data
 * @pdev - platform device pointer
 * @pled - LED class device instance
 * @profile - system configuration profile
 * @num_led_instances - number of LED instances
 * @lock - device access lock
**/
struct mlxcpld_led_pdata {
	struct platform_device *pdev;
	struct mlxcpld_led_priv *pled;
	struct mlxcpld_led_profile *profile;
	int num_led_instances;
	spinlock_t lock;
};

static struct mlxcpld_led_pdata *mlxcpld_led;

/* Default profile fit the next Mellanox systems:
 * "msx6710", "msx6720", "msb7700", "msn2700", "msx1410",
 * "msn2410", "msb7800", "msn2740"
 */
static struct mlxcpld_led_profile mlxcpld_led_default_profile[] = {
	{
		0x21, 0xf0, MLXCPLD_LED_GREEN_STATIC_ON, 1,
		"mlxcpld:fan1:green",
	},
	{
		0x21, 0xf0, MLXCPLD_LED_RED_STATIC_ON, LED_OFF,
		"mlxcpld:fan1:red",
	},
	{
		0x21, 0x0f, MLXCPLD_LED_GREEN_STATIC_ON, 1,
		"mlxcpld:fan2:green",
	},
	{
		0x21, 0x0f, MLXCPLD_LED_RED_STATIC_ON, LED_OFF,
		"mlxcpld:fan2:red",
	},
	{
		0x22, 0xf0, MLXCPLD_LED_GREEN_STATIC_ON, 1,
		"mlxcpld:fan3:green",
	},
	{
		0x22, 0xf0, MLXCPLD_LED_RED_STATIC_ON, LED_OFF,
		"mlxcpld:fan3:red",
	},
	{
		0x22, 0x0f, MLXCPLD_LED_GREEN_STATIC_ON, 1,
		"mlxcpld:fan4:green",
	},
	{
		0x22, 0x0f, MLXCPLD_LED_RED_STATIC_ON, LED_OFF,
		"mlxcpld:fan4:red",
	},
	{
		0x20, 0x0f, MLXCPLD_LED_GREEN_STATIC_ON, 1,
		"mlxcpld:psu:green",
	},
	{
		0x20, 0x0f, MLXCPLD_LED_RED_STATIC_ON, LED_OFF,
		"mlxcpld:psu:red",
	},
	{
		0x20, 0xf0, MLXCPLD_LED_GREEN_STATIC_ON, 1,
		"mlxcpld:status:green",
	},
	{
		0x20, 0xf0, MLXCPLD_LED_RED_STATIC_ON, LED_OFF,
		"mlxcpld:status:red",
	},
};

/* Profile fit the Mellanox systems based on "msn2100" */
static struct mlxcpld_led_profile mlxcpld_led_msn2100_profile[] = {
	{
		0x21, 0xf0, MLXCPLD_LED_GREEN_STATIC_ON, 1,
		"mlxcpld:fan:green",
	},
	{
		0x21, 0xf0, MLXCPLD_LED_RED_STATIC_ON, LED_OFF,
		"mlxcpld:fan:red",
	},
	{
		0x23, 0xf0, MLXCPLD_LED_GREEN_STATIC_ON, 1,
		"mlxcpld:psu1:green",
	},
	{
		0x23, 0xf0, MLXCPLD_LED_RED_STATIC_ON, LED_OFF,
		"mlxcpld:psu1:red",
	},
	{
		0x23, 0x0f, MLXCPLD_LED_GREEN_STATIC_ON, 1,
		"mlxcpld:psu2:green",
	},
	{
		0x23, 0x0f, MLXCPLD_LED_RED_STATIC_ON, LED_OFF,
		"mlxcpld:psu2:red",
	},
	{
		0x20, 0xf0, MLXCPLD_LED_GREEN_STATIC_ON, 1,
		"mlxcpld:status:green",
	},
	{
		0x20, 0xf0, MLXCPLD_LED_RED_STATIC_ON, LED_OFF,
		"mlxcpld:status:red",
	},
	{
		0x24, 0xf0, MLXCPLD_LED_GREEN_STATIC_ON, LED_OFF,
		"mlxcpld:uid:blue",
	},
};

enum mlxcpld_led_platform_types {
	MLXCPLD_LED_PLATFORM_DEFAULT,
	MLXCPLD_LED_PLATFORM_MSN2100,
};

static const char *mlx_product_names[] = {
	"DEFAULT",
	"MSN2100",
};

static enum
mlxcpld_led_platform_types mlxcpld_led_platform_check_sys_type(void)
{
	const char *mlx_product_name;
	int i;

	mlx_product_name = dmi_get_system_info(DMI_PRODUCT_NAME);
	if (!mlx_product_name)
		return MLXCPLD_LED_PLATFORM_DEFAULT;

	for (i = 1;  i < ARRAY_SIZE(mlx_product_names); i++) {
		if (strstr(mlx_product_name, mlx_product_names[i]))
			return i;
	}

	return MLXCPLD_LED_PLATFORM_DEFAULT;
}

static void mlxcpld_led_bus_access_func(u16 base, u8 offset, u8 rw_flag,
					u8 *data)
{
	u32 addr = base + offset;

	if (rw_flag == 0)
		outb(*data, addr);
	else
		*data = inb(addr);
}

static void mlxcpld_led_store_hw(u8 mask, u8 off, u8 vset)
{
	u8 nib, val;

	/*
	 * Each LED is controlled through low or high nibble of the relevant
	 * CPLD register. Register offset is specified by off parameter.
	 * Parameter vset provides color code: 0x0 for off, 0x5 for solid red,
	 * 0x6 for 3Hz blink red, 0xd for solid green, 0xe for 3Hz blink
	 * green.
	 * Parameter mask specifies which nibble is used for specific LED: mask
	 * 0xf0 - lower nibble is to be used (bits from 0 to 3), mask 0x0f -
	 * higher nibble (bits from 4 to 7).
	 */
	spin_lock(&mlxcpld_led->lock);
	mlxcpld_led_bus_access_func(MLXPLAT_CPLD_LPC_REG_BASE_ADRR, off, 1,
				    &val);
	nib = (mask == 0xf0) ? vset : (vset << 4);
	val = (val & mask) | nib;
	mlxcpld_led_bus_access_func(MLXPLAT_CPLD_LPC_REG_BASE_ADRR, off, 0,
				    &val);
	spin_unlock(&mlxcpld_led->lock);
}

static void mlxcpld_led_brightness_set(struct led_classdev *led,
				       enum led_brightness value)
{
	struct mlxcpld_led_priv *pled = cdev_to_priv(led);

	if (value) {
		mlxcpld_led_store_hw(pled->param.mask, pled->param.offset,
				     pled->param.base_color);
		return;
	}

	mlxcpld_led_store_hw(pled->param.mask, pled->param.offset,
			     MLXCPLD_LED_IS_OFF);
}

static int mlxcpld_led_blink_set(struct led_classdev *led,
				 unsigned long *delay_on,
				 unsigned long *delay_off)
{
	struct mlxcpld_led_priv *pled = cdev_to_priv(led);

	/*
	 * HW supports two types of blinking: full (6Hz) and half (3Hz).
	 * For delay on/off zero default setting 3Hz is used.
	 */
	if (!(*delay_on == 0 && *delay_off == 0) &&
	    !(*delay_on == MLXCPLD_LED_BLINK_3HZ &&
	      *delay_off == MLXCPLD_LED_BLINK_3HZ) &&
	    !(*delay_on == MLXCPLD_LED_BLINK_6HZ &&
	      *delay_off == MLXCPLD_LED_BLINK_6HZ))
		return -EINVAL;

	if (*delay_on == MLXCPLD_LED_BLINK_6HZ)
		mlxcpld_led_store_hw(pled->param.mask, pled->param.offset,
				     pled->param.base_color +
				     MLXCPLD_LED_OFFSET_FULL);
	else
		mlxcpld_led_store_hw(pled->param.mask, pled->param.offset,
				     pled->param.base_color +
				     MLXCPLD_LED_OFFSET_HALF);

	return 0;
}

static int mlxcpld_led_config(struct device *dev,
			      struct mlxcpld_led_pdata *cpld)
{
	int i;
	int err;

	cpld->pled = devm_kcalloc(dev,
				  cpld->num_led_instances,
				  sizeof(struct mlxcpld_led_priv),
				  GFP_KERNEL);
	if (!cpld->pled)
		return -ENOMEM;

	for (i = 0; i < cpld->num_led_instances; i++) {
		cpld->pled[i].cdev.name = cpld->profile[i].name;
		cpld->pled[i].cdev.brightness = cpld->profile[i].brightness;
		cpld->pled[i].cdev.max_brightness = 1;
		cpld->pled[i].cdev.brightness_set = mlxcpld_led_brightness_set;
		cpld->pled[i].cdev.blink_set = mlxcpld_led_blink_set;
		cpld->pled[i].cdev.flags = LED_CORE_SUSPENDRESUME;
		err = devm_led_classdev_register(dev, &cpld->pled[i].cdev);
		if (err)
			return err;

		cpld->pled[i].param.offset = mlxcpld_led->profile[i].offset;
		cpld->pled[i].param.mask = mlxcpld_led->profile[i].mask;
		cpld->pled[i].param.base_color =
					mlxcpld_led->profile[i].base_color;

		if (mlxcpld_led->profile[i].brightness)
			mlxcpld_led_brightness_set(&cpld->pled[i].cdev,
					mlxcpld_led->profile[i].brightness);
	}

	return 0;
}

static int __init mlxcpld_led_probe(struct platform_device *pdev)
{
	enum mlxcpld_led_platform_types mlxcpld_led_plat =
					mlxcpld_led_platform_check_sys_type();

	mlxcpld_led = devm_kzalloc(&pdev->dev, sizeof(*mlxcpld_led),
				   GFP_KERNEL);
	if (!mlxcpld_led)
		return -ENOMEM;

	mlxcpld_led->pdev = pdev;

	switch (mlxcpld_led_plat) {
	case MLXCPLD_LED_PLATFORM_MSN2100:
		mlxcpld_led->profile = mlxcpld_led_msn2100_profile;
		mlxcpld_led->num_led_instances =
				ARRAY_SIZE(mlxcpld_led_msn2100_profile);
		break;

	default:
		mlxcpld_led->profile = mlxcpld_led_default_profile;
		mlxcpld_led->num_led_instances =
				ARRAY_SIZE(mlxcpld_led_default_profile);
		break;
	}

	spin_lock_init(&mlxcpld_led->lock);

	return mlxcpld_led_config(&pdev->dev, mlxcpld_led);
}

static struct platform_driver mlxcpld_led_driver = {
	.driver = {
		.name	= KBUILD_MODNAME,
	},
};

static int __init mlxcpld_led_init(void)
{
	struct platform_device *pdev;
	int err;

	if (!dmi_match(DMI_CHASSIS_VENDOR, "Mellanox Technologies Ltd."))
		return -ENODEV;

	pdev = platform_device_register_simple(KBUILD_MODNAME, -1, NULL, 0);
	if (IS_ERR(pdev)) {
		pr_err("Device allocation failed\n");
		return PTR_ERR(pdev);
	}

	err = platform_driver_probe(&mlxcpld_led_driver, mlxcpld_led_probe);
	if (err) {
		pr_err("Probe platform driver failed\n");
		platform_device_unregister(pdev);
	}

	return err;
}

static void __exit mlxcpld_led_exit(void)
{
	platform_device_unregister(mlxcpld_led->pdev);
	platform_driver_unregister(&mlxcpld_led_driver);
}

module_init(mlxcpld_led_init);
module_exit(mlxcpld_led_exit);

MODULE_AUTHOR("Vadim Pasternak <vadimp@mellanox.com>");
MODULE_DESCRIPTION("Mellanox board LED driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("platform:leds_mlxcpld");
