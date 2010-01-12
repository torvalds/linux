/*
 * arch/arm/plat-s5pc1xx/gpiolib.c
 *
 *  Copyright 2009 Samsung Electronics Co
 *  Kyungmin Park <kyungmin.park@samsung.com>
 *
 * S5PC1XX - GPIOlib support
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
#include <mach/gpio-core.h>

#include <plat/gpio-cfg.h>
#include <plat/gpio-cfg-helpers.h>
#include <plat/regs-gpio.h>

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

#define OFF_GPCON	(0x00)
#define OFF_GPDAT	(0x04)

#define con_4bit_shift(__off) ((__off) * 4)

#if 1
#define gpio_dbg(x...) do { } while (0)
#else
#define gpio_dbg(x...) printk(KERN_DEBUG x)
#endif

/* The s5pc1xx_gpiolib routines are to control the gpio banks where
 * the gpio configuration register (GPxCON) has 4 bits per GPIO, as the
 * following example:
 *
 * base + 0x00: Control register, 4 bits per gpio
 *	        gpio n: 4 bits starting at (4*n)
 *		0000 = input, 0001 = output, others mean special-function
 * base + 0x04: Data register, 1 bit per gpio
 *		bit n: data bit n
 *
 * Note, since the data register is one bit per gpio and is at base + 0x4
 * we can use s3c_gpiolib_get and s3c_gpiolib_set to change the state of
 * the output.
 */

static int s5pc1xx_gpiolib_input(struct gpio_chip *chip, unsigned offset)
{
	struct s3c_gpio_chip *ourchip = to_s3c_gpio(chip);
	void __iomem *base = ourchip->base;
	unsigned long con;

	con = __raw_readl(base + OFF_GPCON);
	con &= ~(0xf << con_4bit_shift(offset));
	__raw_writel(con, base + OFF_GPCON);

	gpio_dbg("%s: %p: CON now %08lx\n", __func__, base, con);

	return 0;
}

static int s5pc1xx_gpiolib_output(struct gpio_chip *chip,
				       unsigned offset, int value)
{
	struct s3c_gpio_chip *ourchip = to_s3c_gpio(chip);
	void __iomem *base = ourchip->base;
	unsigned long con;
	unsigned long dat;

	con = __raw_readl(base + OFF_GPCON);
	con &= ~(0xf << con_4bit_shift(offset));
	con |= 0x1 << con_4bit_shift(offset);

	dat = __raw_readl(base + OFF_GPDAT);
	if (value)
		dat |= 1 << offset;
	else
		dat &= ~(1 << offset);

	__raw_writel(dat, base + OFF_GPDAT);
	__raw_writel(con, base + OFF_GPCON);
	__raw_writel(dat, base + OFF_GPDAT);

	gpio_dbg("%s: %p: CON %08lx, DAT %08lx\n", __func__, base, con, dat);

	return 0;
}

static int s5pc1xx_gpiolib_to_irq(struct gpio_chip *chip, unsigned int offset)
{
	return S3C_IRQ_GPIO(chip->base + offset);
}

static int s5pc1xx_gpiolib_to_eint(struct gpio_chip *chip, unsigned int offset)
{
	int base;

	base = chip->base - S5PC100_GPH0(0);
	if (base == 0)
		return IRQ_EINT(offset);
	base = chip->base - S5PC100_GPH1(0);
	if (base == 0)
		return IRQ_EINT(8 + offset);
	base = chip->base - S5PC100_GPH2(0);
	if (base == 0)
		return IRQ_EINT(16 + offset);
	base = chip->base - S5PC100_GPH3(0);
	if (base == 0)
		return IRQ_EINT(24 + offset);
	return -EINVAL;
}

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

static struct s3c_gpio_chip s5pc100_gpio_chips[] = {
	{
		.base	= S5PC100_GPA0_BASE,
		.config	= &gpio_cfg,
		.chip	= {
			.base	= S5PC100_GPA0(0),
			.ngpio	= S5PC100_GPIO_A0_NR,
			.label	= "GPA0",
		},
	}, {
		.base	= S5PC100_GPA1_BASE,
		.config	= &gpio_cfg,
		.chip	= {
			.base	= S5PC100_GPA1(0),
			.ngpio	= S5PC100_GPIO_A1_NR,
			.label	= "GPA1",
		},
	}, {
		.base	= S5PC100_GPB_BASE,
		.config	= &gpio_cfg,
		.chip	= {
			.base	= S5PC100_GPB(0),
			.ngpio	= S5PC100_GPIO_B_NR,
			.label	= "GPB",
		},
	}, {
		.base	= S5PC100_GPC_BASE,
		.config	= &gpio_cfg,
		.chip	= {
			.base	= S5PC100_GPC(0),
			.ngpio	= S5PC100_GPIO_C_NR,
			.label	= "GPC",
		},
	}, {
		.base	= S5PC100_GPD_BASE,
		.config	= &gpio_cfg,
		.chip	= {
			.base	= S5PC100_GPD(0),
			.ngpio	= S5PC100_GPIO_D_NR,
			.label	= "GPD",
		},
	}, {
		.base	= S5PC100_GPE0_BASE,
		.config	= &gpio_cfg,
		.chip	= {
			.base	= S5PC100_GPE0(0),
			.ngpio	= S5PC100_GPIO_E0_NR,
			.label	= "GPE0",
		},
	}, {
		.base	= S5PC100_GPE1_BASE,
		.config	= &gpio_cfg,
		.chip	= {
			.base	= S5PC100_GPE1(0),
			.ngpio	= S5PC100_GPIO_E1_NR,
			.label	= "GPE1",
		},
	}, {
		.base	= S5PC100_GPF0_BASE,
		.config	= &gpio_cfg,
		.chip	= {
			.base	= S5PC100_GPF0(0),
			.ngpio	= S5PC100_GPIO_F0_NR,
			.label	= "GPF0",
		},
	}, {
		.base	= S5PC100_GPF1_BASE,
		.config	= &gpio_cfg,
		.chip	= {
			.base	= S5PC100_GPF1(0),
			.ngpio	= S5PC100_GPIO_F1_NR,
			.label	= "GPF1",
		},
	}, {
		.base	= S5PC100_GPF2_BASE,
		.config	= &gpio_cfg,
		.chip	= {
			.base	= S5PC100_GPF2(0),
			.ngpio	= S5PC100_GPIO_F2_NR,
			.label	= "GPF2",
		},
	}, {
		.base	= S5PC100_GPF3_BASE,
		.config	= &gpio_cfg,
		.chip	= {
			.base	= S5PC100_GPF3(0),
			.ngpio	= S5PC100_GPIO_F3_NR,
			.label	= "GPF3",
		},
	}, {
		.base	= S5PC100_GPG0_BASE,
		.config	= &gpio_cfg,
		.chip	= {
			.base	= S5PC100_GPG0(0),
			.ngpio	= S5PC100_GPIO_G0_NR,
			.label	= "GPG0",
		},
	}, {
		.base	= S5PC100_GPG1_BASE,
		.config	= &gpio_cfg,
		.chip	= {
			.base	= S5PC100_GPG1(0),
			.ngpio	= S5PC100_GPIO_G1_NR,
			.label	= "GPG1",
		},
	}, {
		.base	= S5PC100_GPG2_BASE,
		.config	= &gpio_cfg,
		.chip	= {
			.base	= S5PC100_GPG2(0),
			.ngpio	= S5PC100_GPIO_G2_NR,
			.label	= "GPG2",
		},
	}, {
		.base	= S5PC100_GPG3_BASE,
		.config	= &gpio_cfg,
		.chip	= {
			.base	= S5PC100_GPG3(0),
			.ngpio	= S5PC100_GPIO_G3_NR,
			.label	= "GPG3",
		},
	}, {
		.base	= S5PC100_GPH0_BASE,
		.config	= &gpio_cfg_eint,
		.chip	= {
			.base	= S5PC100_GPH0(0),
			.ngpio	= S5PC100_GPIO_H0_NR,
			.label	= "GPH0",
		},
	}, {
		.base	= S5PC100_GPH1_BASE,
		.config	= &gpio_cfg_eint,
		.chip	= {
			.base	= S5PC100_GPH1(0),
			.ngpio	= S5PC100_GPIO_H1_NR,
			.label	= "GPH1",
		},
	}, {
		.base	= S5PC100_GPH2_BASE,
		.config	= &gpio_cfg_eint,
		.chip	= {
			.base	= S5PC100_GPH2(0),
			.ngpio	= S5PC100_GPIO_H2_NR,
			.label	= "GPH2",
		},
	}, {
		.base	= S5PC100_GPH3_BASE,
		.config	= &gpio_cfg_eint,
		.chip	= {
			.base	= S5PC100_GPH3(0),
			.ngpio	= S5PC100_GPIO_H3_NR,
			.label	= "GPH3",
		},
	}, {
		.base	= S5PC100_GPI_BASE,
		.config	= &gpio_cfg,
		.chip	= {
			.base	= S5PC100_GPI(0),
			.ngpio	= S5PC100_GPIO_I_NR,
			.label	= "GPI",
		},
	}, {
		.base	= S5PC100_GPJ0_BASE,
		.config	= &gpio_cfg,
		.chip	= {
			.base	= S5PC100_GPJ0(0),
			.ngpio	= S5PC100_GPIO_J0_NR,
			.label	= "GPJ0",
		},
	}, {
		.base	= S5PC100_GPJ1_BASE,
		.config	= &gpio_cfg,
		.chip	= {
			.base	= S5PC100_GPJ1(0),
			.ngpio	= S5PC100_GPIO_J1_NR,
			.label	= "GPJ1",
		},
	}, {
		.base	= S5PC100_GPJ2_BASE,
		.config	= &gpio_cfg,
		.chip	= {
			.base	= S5PC100_GPJ2(0),
			.ngpio	= S5PC100_GPIO_J2_NR,
			.label	= "GPJ2",
		},
	}, {
		.base	= S5PC100_GPJ3_BASE,
		.config	= &gpio_cfg,
		.chip	= {
			.base	= S5PC100_GPJ3(0),
			.ngpio	= S5PC100_GPIO_J3_NR,
			.label	= "GPJ3",
		},
	}, {
		.base	= S5PC100_GPJ4_BASE,
		.config	= &gpio_cfg,
		.chip	= {
			.base	= S5PC100_GPJ4(0),
			.ngpio	= S5PC100_GPIO_J4_NR,
			.label	= "GPJ4",
		},
	}, {
		.base	= S5PC100_GPK0_BASE,
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= S5PC100_GPK0(0),
			.ngpio	= S5PC100_GPIO_K0_NR,
			.label	= "GPK0",
		},
	}, {
		.base	= S5PC100_GPK1_BASE,
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= S5PC100_GPK1(0),
			.ngpio	= S5PC100_GPIO_K1_NR,
			.label	= "GPK1",
		},
	}, {
		.base	= S5PC100_GPK2_BASE,
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= S5PC100_GPK2(0),
			.ngpio	= S5PC100_GPIO_K2_NR,
			.label	= "GPK2",
		},
	}, {
		.base	= S5PC100_GPK3_BASE,
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= S5PC100_GPK3(0),
			.ngpio	= S5PC100_GPIO_K3_NR,
			.label	= "GPK3",
		},
	}, {
		.base	= S5PC100_GPL0_BASE,
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= S5PC100_GPL0(0),
			.ngpio	= S5PC100_GPIO_L0_NR,
			.label	= "GPL0",
		},
	}, {
		.base	= S5PC100_GPL1_BASE,
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= S5PC100_GPL1(0),
			.ngpio	= S5PC100_GPIO_L1_NR,
			.label	= "GPL1",
		},
	}, {
		.base	= S5PC100_GPL2_BASE,
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= S5PC100_GPL2(0),
			.ngpio	= S5PC100_GPIO_L2_NR,
			.label	= "GPL2",
		},
	}, {
		.base	= S5PC100_GPL3_BASE,
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= S5PC100_GPL3(0),
			.ngpio	= S5PC100_GPIO_L3_NR,
			.label	= "GPL3",
		},
	}, {
		.base	= S5PC100_GPL4_BASE,
		.config	= &gpio_cfg_noint,
		.chip	= {
			.base	= S5PC100_GPL4(0),
			.ngpio	= S5PC100_GPIO_L4_NR,
			.label	= "GPL4",
		},
	},
};

/* FIXME move from irq-gpio.c */
extern struct irq_chip s5pc1xx_gpioint;
extern void s5pc1xx_irq_gpioint_handler(unsigned int irq, struct irq_desc *desc);

static __init void s5pc1xx_gpiolib_link(struct s3c_gpio_chip *chip)
{
	chip->chip.direction_input = s5pc1xx_gpiolib_input;
	chip->chip.direction_output = s5pc1xx_gpiolib_output;
	chip->pm = __gpio_pm(&s3c_gpio_pm_4bit);

	/* Interrupt */
	if (chip->config == &gpio_cfg) {
		int i, irq;

		chip->chip.to_irq = s5pc1xx_gpiolib_to_irq;

		for (i = 0;  i < chip->chip.ngpio; i++) {
			irq = S3C_IRQ_GPIO_BASE + chip->chip.base + i;
			set_irq_chip(irq, &s5pc1xx_gpioint);
			set_irq_data(irq, &chip->chip);
			set_irq_handler(irq, handle_level_irq);
			set_irq_flags(irq, IRQF_VALID);
		}
	} else if (chip->config == &gpio_cfg_eint)
		chip->chip.to_irq = s5pc1xx_gpiolib_to_eint;
}

static __init void s5pc1xx_gpiolib_add(struct s3c_gpio_chip *chips,
				       int nr_chips,
				       void (*fn)(struct s3c_gpio_chip *))
{
	for (; nr_chips > 0; nr_chips--, chips++) {
		if (fn)
			(fn)(chips);
		s3c_gpiolib_add(chips);
	}
}

static __init int s5pc1xx_gpiolib_init(void)
{
	struct s3c_gpio_chip *chips;
	int nr_chips;

		chips = s5pc100_gpio_chips;
		nr_chips = ARRAY_SIZE(s5pc100_gpio_chips);

	s5pc1xx_gpiolib_add(chips, nr_chips, s5pc1xx_gpiolib_link);
	/* Interrupt */
	set_irq_chained_handler(IRQ_GPIOINT, s5pc1xx_irq_gpioint_handler);

	return 0;
}
core_initcall(s5pc1xx_gpiolib_init);
