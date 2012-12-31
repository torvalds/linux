/* driver/gpio/gpio-exynos5.c
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS5 - GPIOlib support
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
#include <plat/cpu.h>

int s3c_gpio_setpull_exynos5(struct s3c_gpio_chip *chip,
				unsigned int off, s3c_gpio_pull_t pull)
{
	if (pull == S3C_GPIO_PULL_UP)
		pull = 3;

	return s3c_gpio_setpull_updown(chip, off, pull);
}

s3c_gpio_pull_t s3c_gpio_getpull_exynos5(struct s3c_gpio_chip *chip,
						unsigned int off)
{
	s3c_gpio_pull_t pull;

	pull = s3c_gpio_getpull_updown(chip, off);
	if (pull == 3)
		pull = S3C_GPIO_PULL_UP;

	return pull;
}

static struct s3c_gpio_cfg gpio_cfg = {
	.set_config	= s3c_gpio_setcfg_s3c64xx_4bit,
	.set_pull	= s3c_gpio_setpull_exynos5,
	.get_pull	= s3c_gpio_getpull_exynos5,
};

static struct s3c_gpio_cfg gpio_cfg_noint = {
	.set_config	= s3c_gpio_setcfg_s3c64xx_4bit,
	.set_pull	= s3c_gpio_setpull_exynos5,
	.get_pull	= s3c_gpio_getpull_exynos5,
};

/*
 * Following are the gpio banks in exynos5.
 *
 * The 'config' member when left to NULL, is initialized to the default
 * structure gpio_cfg in the init function below.
 *
 * The 'base' member is also initialized in the init function below.
 * Note: The initialization of 'base' member of s3c_gpio_chip structure
 * uses the above macro and depends on the banks being listed in order here.
 */
static struct s3c_gpio_chip exynos5_gpio_common_4bit[] = {
	{
		.base	= S5P_VA_GPIO1,
		.eint_offset = 0x0,
		.group	= 0,
		.chip	= {
			.base	= EXYNOS5_GPA0(0),
			.ngpio	= EXYNOS5_GPIO_A0_NR,
			.label	= "GPA0",
		},
	}, {
		.base	= (S5P_VA_GPIO1 + 0x20),
		.eint_offset = 0x4,
		.group	= 1,
		.chip	= {
			.base	= EXYNOS5_GPA1(0),
			.ngpio	= EXYNOS5_GPIO_A1_NR,
			.label	= "GPA1",
		},
	}, {
		.base	= (S5P_VA_GPIO1 + 0x40),
		.eint_offset = 0x8,
		.group	= 2,
		.chip	= {
			.base	= EXYNOS5_GPA2(0),
			.ngpio	= EXYNOS5_GPIO_A2_NR,
			.label	= "GPA2",
		},
	}, {
		.base	= (S5P_VA_GPIO1 + 0x60),
		.eint_offset = 0xC,
		.group	= 3,
		.chip	= {
			.base	= EXYNOS5_GPB0(0),
			.ngpio	= EXYNOS5_GPIO_B0_NR,
			.label	= "GPB0",
		},
	}, {
		.base	= (S5P_VA_GPIO1 + 0x80),
		.eint_offset = 0x10,
		.group	= 4,
		.chip	= {
			.base	= EXYNOS5_GPB1(0),
			.ngpio	= EXYNOS5_GPIO_B1_NR,
			.label	= "GPB1",
		},
	}, {
		.base	= (S5P_VA_GPIO1 + 0xA0),
		.eint_offset = 0x14,
		.group	= 5,
		.chip	= {
			.base	= EXYNOS5_GPB2(0),
			.ngpio	= EXYNOS5_GPIO_B2_NR,
			.label	= "GPB2",
		},
	}, {
		.base	= (S5P_VA_GPIO1 + 0xC0),
		.eint_offset = 0x18,
		.group	= 6,
		.chip	= {
			.base	= EXYNOS5_GPB3(0),
			.ngpio	= EXYNOS5_GPIO_B3_NR,
			.label	= "GPB3",
		},
	}, {
		.base   = (S5P_VA_GPIO1 + 0xE0),
		.eint_offset = 0x1C,
		.group	= 7,
		.chip	= {
			.base	= EXYNOS5_GPC0(0),
			.ngpio	= EXYNOS5_GPIO_C0_NR,
			.label	= "GPC0",
		},
	}, {
		.base   = (S5P_VA_GPIO1 + 0x100),
		.eint_offset = 0x20,
		.group	= 8,
		.chip	= {
			.base	= EXYNOS5_GPC1(0),
			.ngpio	= EXYNOS5_GPIO_C1_NR,
			.label	= "GPC1",
		},
	}, {
		.base   = (S5P_VA_GPIO1 + 0x120),
		.eint_offset = 0x24,
		.group	= 9,
		.chip	= {
			.base	= EXYNOS5_GPC2(0),
			.ngpio	= EXYNOS5_GPIO_C2_NR,
			.label	= "GPC2",
		},
	}, {
		.base   = (S5P_VA_GPIO1 + 0x140),
		.eint_offset = 0x28,
		.group	= 10,
		.chip	= {
			.base	= EXYNOS5_GPC3(0),
			.ngpio	= EXYNOS5_GPIO_C3_NR,
			.label	= "GPC3",
		},
	}, {
		.base   = (S5P_VA_GPIO1 + 0x160),
		.eint_offset = 0x2C,
		.group	= 11,
		.chip	= {
			.base	= EXYNOS5_GPD0(0),
			.ngpio	= EXYNOS5_GPIO_D0_NR,
			.label	= "GPD0",
		},
	}, {
		.base   = (S5P_VA_GPIO1 + 0x180),
		.eint_offset = 0x30,
		.group	= 12,
		.chip	= {
			.base	= EXYNOS5_GPD1(0),
			.ngpio	= EXYNOS5_GPIO_D1_NR,
			.label	= "GPD1",
		},
	}, {
		.base   = (S5P_VA_GPIO1 + 0x1A0),
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= EXYNOS5_GPY0(0),
			.ngpio	= EXYNOS5_GPIO_Y0_NR,
			.label	= "GPY0",
		},
	}, {
		.base   = (S5P_VA_GPIO1 + 0x1C0),
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= EXYNOS5_GPY1(0),
			.ngpio	= EXYNOS5_GPIO_Y1_NR,
			.label	= "GPY1",
		},
	}, {
		.base   = (S5P_VA_GPIO1 + 0x1E0),
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= EXYNOS5_GPY2(0),
			.ngpio	= EXYNOS5_GPIO_Y2_NR,
			.label	= "GPY2",
		},
	}, {
		.base   = (S5P_VA_GPIO1 + 0x200),
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= EXYNOS5_GPY3(0),
			.ngpio	= EXYNOS5_GPIO_Y3_NR,
			.label	= "GPY3",
		},
	}, {
		.base   = (S5P_VA_GPIO1 + 0x220),
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= EXYNOS5_GPY4(0),
			.ngpio	= EXYNOS5_GPIO_Y4_NR,
			.label	= "GPY4",
		},
	}, {
		.base   = (S5P_VA_GPIO1 + 0x240),
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= EXYNOS5_GPY5(0),
			.ngpio	= EXYNOS5_GPIO_Y5_NR,
			.label	= "GPY5",
		},
	}, {
		.base   = (S5P_VA_GPIO1 + 0x260),
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= EXYNOS5_GPY6(0),
			.ngpio	= EXYNOS5_GPIO_Y6_NR,
			.label	= "GPY6",
		},
	}, {
		.base	= (S5P_VA_GPIO1 + 0xC00),
		.config	= &gpio_cfg_noint,
		.irq_base = IRQ_EINT(0),
		.chip	= {
			.base	= EXYNOS5_GPX0(0),
			.ngpio	= EXYNOS5_GPIO_X0_NR,
			.label	= "GPX0",
			.to_irq	= samsung_gpiolib_to_irq,
		},
	}, {
		.base	= (S5P_VA_GPIO1 + 0xC20),
		.config	= &gpio_cfg_noint,
		.irq_base = IRQ_EINT(8),
		.chip	= {
			.base	= EXYNOS5_GPX1(0),
			.ngpio	= EXYNOS5_GPIO_X1_NR,
			.label	= "GPX1",
			.to_irq	= samsung_gpiolib_to_irq,
		},
	}, {
		.base	= (S5P_VA_GPIO1 + 0xC40),
		.config	= &gpio_cfg_noint,
		.irq_base = IRQ_EINT(16),
		.chip	= {
			.base	= EXYNOS5_GPX2(0),
			.ngpio	= EXYNOS5_GPIO_X2_NR,
			.label	= "GPX2",
			.to_irq	= samsung_gpiolib_to_irq,
		},
	}, {
		.base	= (S5P_VA_GPIO1 + 0xC60),
		.config	= &gpio_cfg_noint,
		.irq_base = IRQ_EINT(24),
		.chip	= {
			.base	= EXYNOS5_GPX3(0),
			.ngpio	= EXYNOS5_GPIO_X3_NR,
			.label	= "GPX3",
			.to_irq	= samsung_gpiolib_to_irq,
		},
	}, {
		.base   = S5P_VA_GPIO2,
		.eint_offset = 0x0,
		.group	= 13,
		.chip	= {
			.base	= EXYNOS5_GPE0(0),
			.ngpio	= EXYNOS5_GPIO_E0_NR,
			.label	= "GPE0",
		},
	}, {
		.base   = (S5P_VA_GPIO2 + 0x20),
		.eint_offset = 0x4,
		.group	= 14,
		.chip	= {
			.base	= EXYNOS5_GPE1(0),
			.ngpio	= EXYNOS5_GPIO_E1_NR,
			.label	= "GPE1",
		},
	}, {
		.base   = (S5P_VA_GPIO2 + 0x40),
		.eint_offset = 0x8,
		.group	= 15,
		.chip	= {
			.base	= EXYNOS5_GPF0(0),
			.ngpio	= EXYNOS5_GPIO_F0_NR,
			.label	= "GPF0",
		},
	}, {
		.base   = (S5P_VA_GPIO2 + 0x60),
		.eint_offset = 0xC,
		.group	= 16,
		.chip	= {
			.base	= EXYNOS5_GPF1(0),
			.ngpio	= EXYNOS5_GPIO_F1_NR,
			.label	= "GPF1",
		},
	}, {
		.base   = (S5P_VA_GPIO2 + 0x80),
		.eint_offset = 0x10,
		.group	= 17,
		.chip	= {
			.base	= EXYNOS5_GPG0(0),
			.ngpio	= EXYNOS5_GPIO_G0_NR,
			.label	= "GPG0",
		},
	}, {
		.base   = (S5P_VA_GPIO2 + 0xA0),
		.eint_offset = 0x14,
		.group	= 18,
		.chip	= {
			.base	= EXYNOS5_GPG1(0),
			.ngpio	= EXYNOS5_GPIO_G1_NR,
			.label	= "GPG1",
		},
	}, {
		.base   = (S5P_VA_GPIO2 + 0xC0),
		.eint_offset = 0x18,
		.group	= 19,
		.chip	= {
			.base	= EXYNOS5_GPG2(0),
			.ngpio	= EXYNOS5_GPIO_G2_NR,
			.label	= "GPG2",
		},
	}, {
		.base   = (S5P_VA_GPIO2 + 0xE0),
		.eint_offset = 0x1C,
		.group	= 20,
		.chip	= {
			.base	= EXYNOS5_GPH0(0),
			.ngpio	= EXYNOS5_GPIO_H0_NR,
			.label	= "GPH0",
		},
	}, {
		.base   = (S5P_VA_GPIO2 + 0x100),
		.eint_offset = 0x20,
		.group	= 21,
		.chip	= {
			.base	= EXYNOS5_GPH1(0),
			.ngpio	= EXYNOS5_GPIO_H1_NR,
			.label	= "GPH1",
		},
	}, {
		.base   = S5P_VA_GPIO3,
		.chip	= {
			.base	= EXYNOS5_GPV0(0),
			.ngpio	= EXYNOS5_GPIO_V0_NR,
			.label	= "GPV0",
		},
	}, {
		.base   = (S5P_VA_GPIO3 + 0x20),
		.chip	= {
			.base	= EXYNOS5_GPV1(0),
			.ngpio	= EXYNOS5_GPIO_V1_NR,
			.label	= "GPV1",
		},
	}, {
		.base   = (S5P_VA_GPIO3 + 0x60),
		.chip	= {
			.base	= EXYNOS5_GPV2(0),
			.ngpio	= EXYNOS5_GPIO_V2_NR,
			.label	= "GPV2",
		},
	}, {
		.base   = (S5P_VA_GPIO3 + 0x80),
		.chip	= {
			.base	= EXYNOS5_GPV3(0),
			.ngpio	= EXYNOS5_GPIO_V3_NR,
			.label	= "GPV3",
		},
	}, {
		.base   = (S5P_VA_GPIO3 + 0xC0),
		.chip	= {
			.base	= EXYNOS5_GPV4(0),
			.ngpio	= EXYNOS5_GPIO_V4_NR,
			.label	= "GPV4",
		},
	}, {
		.base   = S5P_VA_GPIO4,
		.chip	= {
			.base	= EXYNOS5_GPZ(0),
			.ngpio	= EXYNOS5_GPIO_Z_NR,
			.label	= "GPZ",
		},
	},
};

static __init int exynos5_gpiolib_init(void)
{
	struct s3c_gpio_chip *chip;
	int i;
	int nr_chips;

	/* GPIO common part  */

	chip = exynos5_gpio_common_4bit;
	nr_chips = ARRAY_SIZE(exynos5_gpio_common_4bit);

	for (i = 0; i < nr_chips; i++, chip++) {
		if (chip->config == NULL)
			chip->config = &gpio_cfg;
		if (chip->base == NULL)
			pr_err("No allocation of base address for [common gpio]");
	}

	samsung_gpiolib_add_4bit_chips(exynos5_gpio_common_4bit, nr_chips);

	s5p_register_gpioint_bank(IRQ_GPIO_XA, 0, IRQ_GPIO1_NR_GROUPS);
	s5p_register_gpioint_bank(IRQ_GPIO_XB, IRQ_GPIO1_NR_GROUPS, IRQ_GPIO2_NR_GROUPS);

	return 0;
}
core_initcall(exynos5_gpiolib_init);
