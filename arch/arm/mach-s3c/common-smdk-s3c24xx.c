// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2006 Simtec Electronics
//	Ben Dooks <ben@simtec.co.uk>
//
// Common code for SMDK2410 and SMDK2440 boards
//
// http://www.fluff.org/ben/smdk2440/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/gpio/machine.h>
#include <linux/device.h>
#include <linux/platform_device.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/nand-ecc-sw-hamming.h>
#include <linux/mtd/partitions.h>
#include <linux/io.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/mach-types.h>
#include <asm/irq.h>

#include "regs-gpio.h"
#include "gpio-samsung.h"
#include <linux/platform_data/leds-s3c24xx.h>
#include <linux/platform_data/mtd-nand-s3c2410.h>

#include "gpio-cfg.h"
#include "devs.h"
#include "pm.h"

#include "common-smdk-s3c24xx.h"

/* LED devices */

static struct gpiod_lookup_table smdk_led4_gpio_table = {
	.dev_id = "s3c24xx_led.0",
	.table = {
		GPIO_LOOKUP("GPF", 4, NULL, GPIO_ACTIVE_LOW | GPIO_OPEN_DRAIN),
		{ },
	},
};

static struct gpiod_lookup_table smdk_led5_gpio_table = {
	.dev_id = "s3c24xx_led.1",
	.table = {
		GPIO_LOOKUP("GPF", 5, NULL, GPIO_ACTIVE_LOW | GPIO_OPEN_DRAIN),
		{ },
	},
};

static struct gpiod_lookup_table smdk_led6_gpio_table = {
	.dev_id = "s3c24xx_led.2",
	.table = {
		GPIO_LOOKUP("GPF", 6, NULL, GPIO_ACTIVE_LOW | GPIO_OPEN_DRAIN),
		{ },
	},
};

static struct gpiod_lookup_table smdk_led7_gpio_table = {
	.dev_id = "s3c24xx_led.3",
	.table = {
		GPIO_LOOKUP("GPF", 7, NULL, GPIO_ACTIVE_LOW | GPIO_OPEN_DRAIN),
		{ },
	},
};

static struct s3c24xx_led_platdata smdk_pdata_led4 = {
	.name		= "led4",
	.def_trigger	= "timer",
};

static struct s3c24xx_led_platdata smdk_pdata_led5 = {
	.name		= "led5",
	.def_trigger	= "nand-disk",
};

static struct s3c24xx_led_platdata smdk_pdata_led6 = {
	.name		= "led6",
};

static struct s3c24xx_led_platdata smdk_pdata_led7 = {
	.name		= "led7",
};

static struct platform_device smdk_led4 = {
	.name		= "s3c24xx_led",
	.id		= 0,
	.dev		= {
		.platform_data = &smdk_pdata_led4,
	},
};

static struct platform_device smdk_led5 = {
	.name		= "s3c24xx_led",
	.id		= 1,
	.dev		= {
		.platform_data = &smdk_pdata_led5,
	},
};

static struct platform_device smdk_led6 = {
	.name		= "s3c24xx_led",
	.id		= 2,
	.dev		= {
		.platform_data = &smdk_pdata_led6,
	},
};

static struct platform_device smdk_led7 = {
	.name		= "s3c24xx_led",
	.id		= 3,
	.dev		= {
		.platform_data = &smdk_pdata_led7,
	},
};

/* NAND parititon from 2.4.18-swl5 */

static struct mtd_partition smdk_default_nand_part[] = {
	[0] = {
		.name	= "Boot Agent",
		.size	= SZ_16K,
		.offset	= 0,
	},
	[1] = {
		.name	= "S3C2410 flash partition 1",
		.offset = 0,
		.size	= SZ_2M,
	},
	[2] = {
		.name	= "S3C2410 flash partition 2",
		.offset = SZ_4M,
		.size	= SZ_4M,
	},
	[3] = {
		.name	= "S3C2410 flash partition 3",
		.offset	= SZ_8M,
		.size	= SZ_2M,
	},
	[4] = {
		.name	= "S3C2410 flash partition 4",
		.offset = SZ_1M * 10,
		.size	= SZ_4M,
	},
	[5] = {
		.name	= "S3C2410 flash partition 5",
		.offset	= SZ_1M * 14,
		.size	= SZ_1M * 10,
	},
	[6] = {
		.name	= "S3C2410 flash partition 6",
		.offset	= SZ_1M * 24,
		.size	= SZ_1M * 24,
	},
	[7] = {
		.name	= "S3C2410 flash partition 7",
		.offset = SZ_1M * 48,
		.size	= MTDPART_SIZ_FULL,
	}
};

static struct s3c2410_nand_set smdk_nand_sets[] = {
	[0] = {
		.name		= "NAND",
		.nr_chips	= 1,
		.nr_partitions	= ARRAY_SIZE(smdk_default_nand_part),
		.partitions	= smdk_default_nand_part,
	},
};

/* choose a set of timings which should suit most 512Mbit
 * chips and beyond.
*/

static struct s3c2410_platform_nand smdk_nand_info = {
	.tacls		= 20,
	.twrph0		= 60,
	.twrph1		= 20,
	.nr_sets	= ARRAY_SIZE(smdk_nand_sets),
	.sets		= smdk_nand_sets,
	.engine_type	= NAND_ECC_ENGINE_TYPE_SOFT,
};

/* devices we initialise */

static struct platform_device __initdata *smdk_devs[] = {
	&s3c_device_nand,
	&smdk_led4,
	&smdk_led5,
	&smdk_led6,
	&smdk_led7,
};

void __init smdk_machine_init(void)
{
	if (machine_is_smdk2443())
		smdk_nand_info.twrph0 = 50;

	s3c_nand_set_platdata(&smdk_nand_info);

	/* Disable pull-up on the LED lines */
	s3c_gpio_setpull(S3C2410_GPF(4), S3C_GPIO_PULL_NONE);
	s3c_gpio_setpull(S3C2410_GPF(5), S3C_GPIO_PULL_NONE);
	s3c_gpio_setpull(S3C2410_GPF(6), S3C_GPIO_PULL_NONE);
	s3c_gpio_setpull(S3C2410_GPF(7), S3C_GPIO_PULL_NONE);

	/* Add lookups for the lines */
	gpiod_add_lookup_table(&smdk_led4_gpio_table);
	gpiod_add_lookup_table(&smdk_led5_gpio_table);
	gpiod_add_lookup_table(&smdk_led6_gpio_table);
	gpiod_add_lookup_table(&smdk_led7_gpio_table);

	platform_add_devices(smdk_devs, ARRAY_SIZE(smdk_devs));

	s3c_pm_init();
}
