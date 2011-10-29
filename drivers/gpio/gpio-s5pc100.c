/*
 * S5PC100 - GPIOlib support
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 *  Copyright 2009 Samsung Electronics Co
 *  Kyungmin Park <kyungmin.park@samsung.com>
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
#include <mach/regs-gpio.h>

#include <plat/gpio-core.h>
#include <plat/gpio-cfg.h>
#include <plat/gpio-cfg-helpers.h>

/* S5PC100 GPIO bank summary:
 *
 * Bank	GPIOs	Style	INT Type
 * A0	8	4Bit	GPIO_INT0
 * A1	5	4Bit	GPIO_INT1
 * B	8	4Bit	GPIO_INT2
 * C	5	4Bit	GPIO_INT3
 * D	7	4Bit	GPIO_INT4
 * E0	8	4Bit	GPIO_INT5
 * E1	6	4Bit	GPIO_INT6
 * F0	8	4Bit	GPIO_INT7
 * F1	8	4Bit	GPIO_INT8
 * F2	8	4Bit	GPIO_INT9
 * F3	4	4Bit	GPIO_INT10
 * G0	8	4Bit	GPIO_INT11
 * G1	3	4Bit	GPIO_INT12
 * G2	7	4Bit	GPIO_INT13
 * G3	7	4Bit	GPIO_INT14
 * H0	8	4Bit	WKUP_INT
 * H1	8	4Bit	WKUP_INT
 * H2	8	4Bit	WKUP_INT
 * H3	8	4Bit	WKUP_INT
 * I	8	4Bit	GPIO_INT15
 * J0	8	4Bit	GPIO_INT16
 * J1	5	4Bit	GPIO_INT17
 * J2	8	4Bit	GPIO_INT18
 * J3	8	4Bit	GPIO_INT19
 * J4	4	4Bit	GPIO_INT20
 * K0	8	4Bit	None
 * K1	6	4Bit	None
 * K2	8	4Bit	None
 * K3	8	4Bit	None
 * L0	8	4Bit	None
 * L1	8	4Bit	None
 * L2	8	4Bit	None
 * L3	8	4Bit	None
 */

static struct s3c_gpio_cfg gpio_cfg = {
	.set_config	= s3c_gpio_setcfg_s3c64xx_4bit,
	.set_pull	= s3c_gpio_setpull_updown,
	.get_pull	= s3c_gpio_getpull_updown,
};

static struct s3c_gpio_cfg gpio_cfg_eint = {
	.cfg_eint	= 0xf,
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
 * GPIO bank's base address given the index of the bank in the
 * list of all gpio banks.
 */
#define S5PC100_BANK_BASE(bank_nr)	(S5P_VA_GPIO + ((bank_nr) * 0x20))

/*
 * Following are the gpio banks in S5PC100.
 *
 * The 'config' member when left to NULL, is initialized to the default
 * structure gpio_cfg in the init function below.
 *
 * The 'base' member is also initialized in the init function below.
 * Note: The initialization of 'base' member of s3c_gpio_chip structure
 * uses the above macro and depends on the banks being listed in order here.
 */
static struct s3c_gpio_chip s5pc100_gpio_chips[] = {
	{
		.chip	= {
			.base	= S5PC100_GPA0(0),
			.ngpio	= S5PC100_GPIO_A0_NR,
			.label	= "GPA0",
		},
	}, {
		.chip	= {
			.base	= S5PC100_GPA1(0),
			.ngpio	= S5PC100_GPIO_A1_NR,
			.label	= "GPA1",
		},
	}, {
		.chip	= {
			.base	= S5PC100_GPB(0),
			.ngpio	= S5PC100_GPIO_B_NR,
			.label	= "GPB",
		},
	}, {
		.chip	= {
			.base	= S5PC100_GPC(0),
			.ngpio	= S5PC100_GPIO_C_NR,
			.label	= "GPC",
		},
	}, {
		.chip	= {
			.base	= S5PC100_GPD(0),
			.ngpio	= S5PC100_GPIO_D_NR,
			.label	= "GPD",
		},
	}, {
		.chip	= {
			.base	= S5PC100_GPE0(0),
			.ngpio	= S5PC100_GPIO_E0_NR,
			.label	= "GPE0",
		},
	}, {
		.chip	= {
			.base	= S5PC100_GPE1(0),
			.ngpio	= S5PC100_GPIO_E1_NR,
			.label	= "GPE1",
		},
	}, {
		.chip	= {
			.base	= S5PC100_GPF0(0),
			.ngpio	= S5PC100_GPIO_F0_NR,
			.label	= "GPF0",
		},
	}, {
		.chip	= {
			.base	= S5PC100_GPF1(0),
			.ngpio	= S5PC100_GPIO_F1_NR,
			.label	= "GPF1",
		},
	}, {
		.chip	= {
			.base	= S5PC100_GPF2(0),
			.ngpio	= S5PC100_GPIO_F2_NR,
			.label	= "GPF2",
		},
	}, {
		.chip	= {
			.base	= S5PC100_GPF3(0),
			.ngpio	= S5PC100_GPIO_F3_NR,
			.label	= "GPF3",
		},
	}, {
		.chip	= {
			.base	= S5PC100_GPG0(0),
			.ngpio	= S5PC100_GPIO_G0_NR,
			.label	= "GPG0",
		},
	}, {
		.chip	= {
			.base	= S5PC100_GPG1(0),
			.ngpio	= S5PC100_GPIO_G1_NR,
			.label	= "GPG1",
		},
	}, {
		.chip	= {
			.base	= S5PC100_GPG2(0),
			.ngpio	= S5PC100_GPIO_G2_NR,
			.label	= "GPG2",
		},
	}, {
		.chip	= {
			.base	= S5PC100_GPG3(0),
			.ngpio	= S5PC100_GPIO_G3_NR,
			.label	= "GPG3",
		},
	}, {
		.chip	= {
			.base	= S5PC100_GPI(0),
			.ngpio	= S5PC100_GPIO_I_NR,
			.label	= "GPI",
		},
	}, {
		.chip	= {
			.base	= S5PC100_GPJ0(0),
			.ngpio	= S5PC100_GPIO_J0_NR,
			.label	= "GPJ0",
		},
	}, {
		.chip	= {
			.base	= S5PC100_GPJ1(0),
			.ngpio	= S5PC100_GPIO_J1_NR,
			.label	= "GPJ1",
		},
	}, {
		.chip	= {
			.base	= S5PC100_GPJ2(0),
			.ngpio	= S5PC100_GPIO_J2_NR,
			.label	= "GPJ2",
		},
	}, {
		.chip	= {
			.base	= S5PC100_GPJ3(0),
			.ngpio	= S5PC100_GPIO_J3_NR,
			.label	= "GPJ3",
		},
	}, {
		.chip	= {
			.base	= S5PC100_GPJ4(0),
			.ngpio	= S5PC100_GPIO_J4_NR,
			.label	= "GPJ4",
		},
	}, {
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= S5PC100_GPK0(0),
			.ngpio	= S5PC100_GPIO_K0_NR,
			.label	= "GPK0",
		},
	}, {
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= S5PC100_GPK1(0),
			.ngpio	= S5PC100_GPIO_K1_NR,
			.label	= "GPK1",
		},
	}, {
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= S5PC100_GPK2(0),
			.ngpio	= S5PC100_GPIO_K2_NR,
			.label	= "GPK2",
		},
	}, {
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= S5PC100_GPK3(0),
			.ngpio	= S5PC100_GPIO_K3_NR,
			.label	= "GPK3",
		},
	}, {
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= S5PC100_GPL0(0),
			.ngpio	= S5PC100_GPIO_L0_NR,
			.label	= "GPL0",
		},
	}, {
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= S5PC100_GPL1(0),
			.ngpio	= S5PC100_GPIO_L1_NR,
			.label	= "GPL1",
		},
	}, {
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= S5PC100_GPL2(0),
			.ngpio	= S5PC100_GPIO_L2_NR,
			.label	= "GPL2",
		},
	}, {
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= S5PC100_GPL3(0),
			.ngpio	= S5PC100_GPIO_L3_NR,
			.label	= "GPL3",
		},
	}, {
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= S5PC100_GPL4(0),
			.ngpio	= S5PC100_GPIO_L4_NR,
			.label	= "GPL4",
		},
	}, {
		.base	= (S5P_VA_GPIO + 0xC00),
		.config	= &gpio_cfg_eint,
		.irq_base = IRQ_EINT(0),
		.chip	= {
			.base	= S5PC100_GPH0(0),
			.ngpio	= S5PC100_GPIO_H0_NR,
			.label	= "GPH0",
			.to_irq = samsung_gpiolib_to_irq,
		},
	}, {
		.base	= (S5P_VA_GPIO + 0xC20),
		.config	= &gpio_cfg_eint,
		.irq_base = IRQ_EINT(8),
		.chip	= {
			.base	= S5PC100_GPH1(0),
			.ngpio	= S5PC100_GPIO_H1_NR,
			.label	= "GPH1",
			.to_irq = samsung_gpiolib_to_irq,
		},
	}, {
		.base	= (S5P_VA_GPIO + 0xC40),
		.config	= &gpio_cfg_eint,
		.irq_base = IRQ_EINT(16),
		.chip	= {
			.base	= S5PC100_GPH2(0),
			.ngpio	= S5PC100_GPIO_H2_NR,
			.label	= "GPH2",
			.to_irq = samsung_gpiolib_to_irq,
		},
	}, {
		.base	= (S5P_VA_GPIO + 0xC60),
		.config	= &gpio_cfg_eint,
		.irq_base = IRQ_EINT(24),
		.chip	= {
			.base	= S5PC100_GPH3(0),
			.ngpio	= S5PC100_GPIO_H3_NR,
			.label	= "GPH3",
			.to_irq = samsung_gpiolib_to_irq,
		},
	},
};

static __init int s5pc100_gpiolib_init(void)
{
	struct s3c_gpio_chip *chip = s5pc100_gpio_chips;
	int nr_chips = ARRAY_SIZE(s5pc100_gpio_chips);
	int gpioint_group = 0;
	int i;

	for (i = 0; i < nr_chips; i++, chip++) {
		if (chip->config == NULL) {
			chip->config = &gpio_cfg;
			chip->group = gpioint_group++;
		}
		if (chip->base == NULL)
			chip->base = S5PC100_BANK_BASE(i);
	}

	samsung_gpiolib_add_4bit_chips(s5pc100_gpio_chips, nr_chips);
	s5p_register_gpioint_bank(IRQ_GPIOINT, 0, S5P_GPIOINT_GROUP_MAXNR);

	return 0;
}
core_initcall(s5pc100_gpiolib_init);
