/* linux/arch/arm/mach-s5pv310/gpiolib.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * S5PV310 - GPIOlib support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/gpio.h>

#include <mach/map.h>

#include <plat/gpio-core.h>
#include <plat/gpio-cfg.h>
#include <plat/gpio-cfg-helpers.h>

static struct s3c_gpio_cfg gpio_cfg = {
	.set_config	= s3c_gpio_setcfg_s3c64xx_4bit,
	.set_pull	= s3c_gpio_setpull_updown,
	.get_pull	= s3c_gpio_getpull_updown,
};

static struct s3c_gpio_cfg gpio_cfg_noint = {
	.set_config	= s3c_gpio_setcfg_s3c64xx_4bit,
	.set_pull	= s3c_gpio_setpull_updown,
	.get_pull	= s3c_gpio_getpull_updown,
};

/*
 * Following are the gpio banks in v310.
 *
 * The 'config' member when left to NULL, is initialized to the default
 * structure gpio_cfg in the init function below.
 *
 * The 'base' member is also initialized in the init function below.
 * Note: The initialization of 'base' member of s3c_gpio_chip structure
 * uses the above macro and depends on the banks being listed in order here.
 */
static struct s3c_gpio_chip s5pv310_gpio_part1_4bit[] = {
	{
		.chip	= {
			.base	= S5PV310_GPA0(0),
			.ngpio	= S5PV310_GPIO_A0_NR,
			.label	= "GPA0",
		},
	}, {
		.chip	= {
			.base	= S5PV310_GPA1(0),
			.ngpio	= S5PV310_GPIO_A1_NR,
			.label	= "GPA1",
		},
	}, {
		.chip	= {
			.base	= S5PV310_GPB(0),
			.ngpio	= S5PV310_GPIO_B_NR,
			.label	= "GPB",
		},
	}, {
		.chip	= {
			.base	= S5PV310_GPC0(0),
			.ngpio	= S5PV310_GPIO_C0_NR,
			.label	= "GPC0",
		},
	}, {
		.chip	= {
			.base	= S5PV310_GPC1(0),
			.ngpio	= S5PV310_GPIO_C1_NR,
			.label	= "GPC1",
		},
	}, {
		.chip	= {
			.base	= S5PV310_GPD0(0),
			.ngpio	= S5PV310_GPIO_D0_NR,
			.label	= "GPD0",
		},
	}, {
		.chip	= {
			.base	= S5PV310_GPD1(0),
			.ngpio	= S5PV310_GPIO_D1_NR,
			.label	= "GPD1",
		},
	}, {
		.chip	= {
			.base	= S5PV310_GPE0(0),
			.ngpio	= S5PV310_GPIO_E0_NR,
			.label	= "GPE0",
		},
	}, {
		.chip	= {
			.base	= S5PV310_GPE1(0),
			.ngpio	= S5PV310_GPIO_E1_NR,
			.label	= "GPE1",
		},
	}, {
		.chip	= {
			.base	= S5PV310_GPE2(0),
			.ngpio	= S5PV310_GPIO_E2_NR,
			.label	= "GPE2",
		},
	}, {
		.chip	= {
			.base	= S5PV310_GPE3(0),
			.ngpio	= S5PV310_GPIO_E3_NR,
			.label	= "GPE3",
		},
	}, {
		.chip	= {
			.base	= S5PV310_GPE4(0),
			.ngpio	= S5PV310_GPIO_E4_NR,
			.label	= "GPE4",
		},
	}, {
		.chip	= {
			.base	= S5PV310_GPF0(0),
			.ngpio	= S5PV310_GPIO_F0_NR,
			.label	= "GPF0",
		},
	}, {
		.chip	= {
			.base	= S5PV310_GPF1(0),
			.ngpio	= S5PV310_GPIO_F1_NR,
			.label	= "GPF1",
		},
	}, {
		.chip	= {
			.base	= S5PV310_GPF2(0),
			.ngpio	= S5PV310_GPIO_F2_NR,
			.label	= "GPF2",
		},
	}, {
		.chip	= {
			.base	= S5PV310_GPF3(0),
			.ngpio	= S5PV310_GPIO_F3_NR,
			.label	= "GPF3",
		},
	},
};

static struct s3c_gpio_chip s5pv310_gpio_part2_4bit[] = {
	{
		.chip	= {
			.base	= S5PV310_GPJ0(0),
			.ngpio	= S5PV310_GPIO_J0_NR,
			.label	= "GPJ0",
		},
	}, {
		.chip	= {
			.base	= S5PV310_GPJ1(0),
			.ngpio	= S5PV310_GPIO_J1_NR,
			.label	= "GPJ1",
		},
	}, {
		.chip	= {
			.base	= S5PV310_GPK0(0),
			.ngpio	= S5PV310_GPIO_K0_NR,
			.label	= "GPK0",
		},
	}, {
		.chip	= {
			.base	= S5PV310_GPK1(0),
			.ngpio	= S5PV310_GPIO_K1_NR,
			.label	= "GPK1",
		},
	}, {
		.chip	= {
			.base	= S5PV310_GPK2(0),
			.ngpio	= S5PV310_GPIO_K2_NR,
			.label	= "GPK2",
		},
	}, {
		.chip	= {
			.base	= S5PV310_GPK3(0),
			.ngpio	= S5PV310_GPIO_K3_NR,
			.label	= "GPK3",
		},
	}, {
		.chip	= {
			.base	= S5PV310_GPL0(0),
			.ngpio	= S5PV310_GPIO_L0_NR,
			.label	= "GPL0",
		},
	}, {
		.chip	= {
			.base	= S5PV310_GPL1(0),
			.ngpio	= S5PV310_GPIO_L1_NR,
			.label	= "GPL1",
		},
	}, {
		.chip	= {
			.base	= S5PV310_GPL2(0),
			.ngpio	= S5PV310_GPIO_L2_NR,
			.label	= "GPL2",
		},
	}, {
		.base	= (S5P_VA_GPIO2 + 0xC00),
		.config	= &gpio_cfg_noint,
		.irq_base = IRQ_EINT(0),
		.chip	= {
			.base	= S5PV310_GPX0(0),
			.ngpio	= S5PV310_GPIO_X0_NR,
			.label	= "GPX0",
			.to_irq	= samsung_gpiolib_to_irq,
		},
	}, {
		.base	= (S5P_VA_GPIO2 + 0xC20),
		.config	= &gpio_cfg_noint,
		.irq_base = IRQ_EINT(8),
		.chip	= {
			.base	= S5PV310_GPX1(0),
			.ngpio	= S5PV310_GPIO_X1_NR,
			.label	= "GPX1",
			.to_irq	= samsung_gpiolib_to_irq,
		},
	}, {
		.base	= (S5P_VA_GPIO2 + 0xC40),
		.config	= &gpio_cfg_noint,
		.irq_base = IRQ_EINT(16),
		.chip	= {
			.base	= S5PV310_GPX2(0),
			.ngpio	= S5PV310_GPIO_X2_NR,
			.label	= "GPX2",
			.to_irq	= samsung_gpiolib_to_irq,
		},
	}, {
		.base	= (S5P_VA_GPIO2 + 0xC60),
		.config	= &gpio_cfg_noint,
		.irq_base = IRQ_EINT(24),
		.chip	= {
			.base	= S5PV310_GPX3(0),
			.ngpio	= S5PV310_GPIO_X3_NR,
			.label	= "GPX3",
			.to_irq	= samsung_gpiolib_to_irq,
		},
	},
};

static struct s3c_gpio_chip s5pv310_gpio_part3_4bit[] = {
	{
		.chip	= {
			.base	= S5PV310_GPZ(0),
			.ngpio	= S5PV310_GPIO_Z_NR,
			.label	= "GPZ",
		},
	},
};

static __init int s5pv310_gpiolib_init(void)
{
	struct s3c_gpio_chip *chip;
	int i;
	int nr_chips;

	/* GPIO part 1 */

	chip = s5pv310_gpio_part1_4bit;
	nr_chips = ARRAY_SIZE(s5pv310_gpio_part1_4bit);

	for (i = 0; i < nr_chips; i++, chip++) {
		if (chip->config == NULL)
			chip->config = &gpio_cfg;
		if (chip->base == NULL)
			chip->base = S5P_VA_GPIO1 + (i) * 0x20;
	}

	samsung_gpiolib_add_4bit_chips(s5pv310_gpio_part1_4bit, nr_chips);

	/* GPIO part 2 */

	chip = s5pv310_gpio_part2_4bit;
	nr_chips = ARRAY_SIZE(s5pv310_gpio_part2_4bit);

	for (i = 0; i < nr_chips; i++, chip++) {
		if (chip->config == NULL)
			chip->config = &gpio_cfg;
		if (chip->base == NULL)
			chip->base = S5P_VA_GPIO2 + (i) * 0x20;
	}

	samsung_gpiolib_add_4bit_chips(s5pv310_gpio_part2_4bit, nr_chips);

	/* GPIO part 3 */

	chip = s5pv310_gpio_part3_4bit;
	nr_chips = ARRAY_SIZE(s5pv310_gpio_part3_4bit);

	for (i = 0; i < nr_chips; i++, chip++) {
		if (chip->config == NULL)
			chip->config = &gpio_cfg;
		if (chip->base == NULL)
			chip->base = S5P_VA_GPIO3 + (i) * 0x20;
	}

	samsung_gpiolib_add_4bit_chips(s5pv310_gpio_part3_4bit, nr_chips);

	return 0;
}
core_initcall(s5pv310_gpiolib_init);
