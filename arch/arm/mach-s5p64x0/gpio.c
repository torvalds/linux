/* linux/arch/arm/mach-s5p64x0/gpio.c
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * S5P64X0 - GPIOlib support
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

/* To be implemented S5P6450 GPIO */

/*
 * S5P6440 GPIO bank summary:
 *
 * Bank	GPIOs	Style	SlpCon	ExtInt Group
 * A	6	4Bit	Yes	1
 * B	7	4Bit	Yes	1
 * C	8	4Bit	Yes	2
 * F	2	2Bit	Yes	4 [1]
 * G	7	4Bit	Yes	5
 * H	10	4Bit[2]	Yes	6
 * I	16	2Bit	Yes	None
 * J	12	2Bit	Yes	None
 * N	16	2Bit	No	IRQ_EINT
 * P	8	2Bit	Yes	8
 * R	15	4Bit[2]	Yes	8
 *
 * [1] BANKF pins 14,15 do not form part of the external interrupt sources
 * [2] BANK has two control registers, GPxCON0 and GPxCON1
 */

static int s5p64x0_gpiolib_rbank_4bit2_input(struct gpio_chip *chip,
					     unsigned int offset)
{
	struct s3c_gpio_chip *ourchip = to_s3c_gpio(chip);
	void __iomem *base = ourchip->base;
	void __iomem *regcon = base;
	unsigned long con;
	unsigned long flags;

	switch (offset) {
	case 6:
		offset += 1;
	case 0:
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
		regcon -= 4;
		break;
	default:
		offset -= 7;
		break;
	}

	s3c_gpio_lock(ourchip, flags);

	con = __raw_readl(regcon);
	con &= ~(0xf << con_4bit_shift(offset));
	__raw_writel(con, regcon);

	s3c_gpio_unlock(ourchip, flags);

	return 0;
}

static int s5p64x0_gpiolib_rbank_4bit2_output(struct gpio_chip *chip,
					      unsigned int offset, int value)
{
	struct s3c_gpio_chip *ourchip = to_s3c_gpio(chip);
	void __iomem *base = ourchip->base;
	void __iomem *regcon = base;
	unsigned long con;
	unsigned long dat;
	unsigned long flags;
	unsigned con_offset  = offset;

	switch (con_offset) {
	case 6:
		con_offset += 1;
	case 0:
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
		regcon -= 4;
		break;
	default:
		con_offset -= 7;
		break;
	}

	s3c_gpio_lock(ourchip, flags);

	con = __raw_readl(regcon);
	con &= ~(0xf << con_4bit_shift(con_offset));
	con |= 0x1 << con_4bit_shift(con_offset);

	dat = __raw_readl(base + GPIODAT_OFF);
	if (value)
		dat |= 1 << offset;
	else
		dat &= ~(1 << offset);

	__raw_writel(con, regcon);
	__raw_writel(dat, base + GPIODAT_OFF);

	s3c_gpio_unlock(ourchip, flags);

	return 0;
}

int s5p64x0_gpio_setcfg_4bit_rbank(struct s3c_gpio_chip *chip,
				   unsigned int off, unsigned int cfg)
{
	void __iomem *reg = chip->base;
	unsigned int shift;
	u32 con;

	switch (off) {
	case 0:
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
		shift = (off & 7) * 4;
		reg -= 4;
		break;
	case 6:
		shift = ((off + 1) & 7) * 4;
		reg -= 4;
	default:
		shift = ((off + 1) & 7) * 4;
		break;
	}

	if (s3c_gpio_is_cfg_special(cfg)) {
		cfg &= 0xf;
		cfg <<= shift;
	}

	con = __raw_readl(reg);
	con &= ~(0xf << shift);
	con |= cfg;
	__raw_writel(con, reg);

	return 0;
}

static struct s3c_gpio_cfg s5p64x0_gpio_cfgs[] = {
	{
		.cfg_eint	= 0,
	}, {
		.cfg_eint	= 7,
	}, {
		.cfg_eint	= 3,
		.set_config	= s5p64x0_gpio_setcfg_4bit_rbank,
	}, {
		.cfg_eint	= 0,
		.set_config	= s3c_gpio_setcfg_s3c24xx,
		.get_config	= s3c_gpio_getcfg_s3c24xx,
	}, {
		.cfg_eint	= 2,
		.set_config	= s3c_gpio_setcfg_s3c24xx,
		.get_config	= s3c_gpio_getcfg_s3c24xx,
	}, {
		.cfg_eint	= 3,
		.set_config	= s3c_gpio_setcfg_s3c24xx,
		.get_config	= s3c_gpio_getcfg_s3c24xx,
	},
};

static struct s3c_gpio_chip s5p6440_gpio_4bit[] = {
	{
		.base	= S5P6440_GPA_BASE,
		.config	= &s5p64x0_gpio_cfgs[1],
		.chip	= {
			.base	= S5P6440_GPA(0),
			.ngpio	= S5P6440_GPIO_A_NR,
			.label	= "GPA",
		},
	}, {
		.base	= S5P6440_GPB_BASE,
		.config	= &s5p64x0_gpio_cfgs[1],
		.chip	= {
			.base	= S5P6440_GPB(0),
			.ngpio	= S5P6440_GPIO_B_NR,
			.label	= "GPB",
		},
	}, {
		.base	= S5P6440_GPC_BASE,
		.config	= &s5p64x0_gpio_cfgs[1],
		.chip	= {
			.base	= S5P6440_GPC(0),
			.ngpio	= S5P6440_GPIO_C_NR,
			.label	= "GPC",
		},
	}, {
		.base	= S5P6440_GPG_BASE,
		.config	= &s5p64x0_gpio_cfgs[1],
		.chip	= {
			.base	= S5P6440_GPG(0),
			.ngpio	= S5P6440_GPIO_G_NR,
			.label	= "GPG",
		},
	},
};

static struct s3c_gpio_chip s5p6440_gpio_4bit2[] = {
	{
		.base	= S5P6440_GPH_BASE + 0x4,
		.config	= &s5p64x0_gpio_cfgs[1],
		.chip	= {
			.base	= S5P6440_GPH(0),
			.ngpio	= S5P6440_GPIO_H_NR,
			.label	= "GPH",
		},
	},
};

static struct s3c_gpio_chip s5p6440_gpio_rbank_4bit2[] = {
	{
		.base	= S5P6440_GPR_BASE + 0x4,
		.config	= &s5p64x0_gpio_cfgs[2],
		.chip	= {
			.base	= S5P6440_GPR(0),
			.ngpio	= S5P6440_GPIO_R_NR,
			.label	= "GPR",
		},
	},
};

static struct s3c_gpio_chip s5p6440_gpio_2bit[] = {
	{
		.base	= S5P6440_GPF_BASE,
		.config	= &s5p64x0_gpio_cfgs[5],
		.chip	= {
			.base	= S5P6440_GPF(0),
			.ngpio	= S5P6440_GPIO_F_NR,
			.label	= "GPF",
		},
	}, {
		.base	= S5P6440_GPI_BASE,
		.config	= &s5p64x0_gpio_cfgs[3],
		.chip	= {
			.base	= S5P6440_GPI(0),
			.ngpio	= S5P6440_GPIO_I_NR,
			.label	= "GPI",
		},
	}, {
		.base	= S5P6440_GPJ_BASE,
		.config	= &s5p64x0_gpio_cfgs[3],
		.chip	= {
			.base	= S5P6440_GPJ(0),
			.ngpio	= S5P6440_GPIO_J_NR,
			.label	= "GPJ",
		},
	}, {
		.base	= S5P6440_GPN_BASE,
		.config	= &s5p64x0_gpio_cfgs[4],
		.chip	= {
			.base	= S5P6440_GPN(0),
			.ngpio	= S5P6440_GPIO_N_NR,
			.label	= "GPN",
		},
	}, {
		.base	= S5P6440_GPP_BASE,
		.config	= &s5p64x0_gpio_cfgs[5],
		.chip	= {
			.base	= S5P6440_GPP(0),
			.ngpio	= S5P6440_GPIO_P_NR,
			.label	= "GPP",
		},
	},
};

void __init s5p64x0_gpiolib_set_cfg(struct s3c_gpio_cfg *chipcfg, int nr_chips)
{
	for (; nr_chips > 0; nr_chips--, chipcfg++) {
		if (!chipcfg->set_config)
			chipcfg->set_config	= s3c_gpio_setcfg_s3c64xx_4bit;
		if (!chipcfg->get_config)
			chipcfg->get_config	= s3c_gpio_getcfg_s3c64xx_4bit;
		if (!chipcfg->set_pull)
			chipcfg->set_pull	= s3c_gpio_setpull_updown;
		if (!chipcfg->get_pull)
			chipcfg->get_pull	= s3c_gpio_getpull_updown;
	}
}

static void __init s5p64x0_gpio_add_rbank_4bit2(struct s3c_gpio_chip *chip,
						int nr_chips)
{
	for (; nr_chips > 0; nr_chips--, chip++) {
		chip->chip.direction_input = s5p64x0_gpiolib_rbank_4bit2_input;
		chip->chip.direction_output =
					s5p64x0_gpiolib_rbank_4bit2_output;
		s3c_gpiolib_add(chip);
	}
}

static int __init s5p6440_gpiolib_init(void)
{
	struct s3c_gpio_chip *chips = s5p6440_gpio_2bit;
	int nr_chips = ARRAY_SIZE(s5p6440_gpio_2bit);

	s5p64x0_gpiolib_set_cfg(s5p64x0_gpio_cfgs,
				ARRAY_SIZE(s5p64x0_gpio_cfgs));

	for (; nr_chips > 0; nr_chips--, chips++)
		s3c_gpiolib_add(chips);

	samsung_gpiolib_add_4bit_chips(s5p6440_gpio_4bit,
				ARRAY_SIZE(s5p6440_gpio_4bit));

	samsung_gpiolib_add_4bit2_chips(s5p6440_gpio_4bit2,
				ARRAY_SIZE(s5p6440_gpio_4bit2));

	s5p64x0_gpio_add_rbank_4bit2(s5p6440_gpio_rbank_4bit2,
				ARRAY_SIZE(s5p6440_gpio_rbank_4bit2));

	return 0;
}
arch_initcall(s5p6440_gpiolib_init);
