/*
 * Copyright (c) 2009-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * SAMSUNG - GPIOlib support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/of_address.h>

#include <asm/irq.h>

#include <mach/map.h>
#include <mach/regs-gpio.h>
#include <mach/gpio-samsung.h>

#include <plat/cpu.h>
#include <plat/gpio-core.h>
#include <plat/gpio-cfg.h>
#include <plat/gpio-cfg-helpers.h>
#include <plat/pm.h>

int samsung_gpio_setpull_updown(struct samsung_gpio_chip *chip,
				unsigned int off, samsung_gpio_pull_t pull)
{
	void __iomem *reg = chip->base + 0x08;
	int shift = off * 2;
	u32 pup;

	pup = __raw_readl(reg);
	pup &= ~(3 << shift);
	pup |= pull << shift;
	__raw_writel(pup, reg);

	return 0;
}

samsung_gpio_pull_t samsung_gpio_getpull_updown(struct samsung_gpio_chip *chip,
						unsigned int off)
{
	void __iomem *reg = chip->base + 0x08;
	int shift = off * 2;
	u32 pup = __raw_readl(reg);

	pup >>= shift;
	pup &= 0x3;

	return (__force samsung_gpio_pull_t)pup;
}

int s3c2443_gpio_setpull(struct samsung_gpio_chip *chip,
			 unsigned int off, samsung_gpio_pull_t pull)
{
	switch (pull) {
	case S3C_GPIO_PULL_NONE:
		pull = 0x01;
		break;
	case S3C_GPIO_PULL_UP:
		pull = 0x00;
		break;
	case S3C_GPIO_PULL_DOWN:
		pull = 0x02;
		break;
	}
	return samsung_gpio_setpull_updown(chip, off, pull);
}

samsung_gpio_pull_t s3c2443_gpio_getpull(struct samsung_gpio_chip *chip,
					 unsigned int off)
{
	samsung_gpio_pull_t pull;

	pull = samsung_gpio_getpull_updown(chip, off);

	switch (pull) {
	case 0x00:
		pull = S3C_GPIO_PULL_UP;
		break;
	case 0x01:
	case 0x03:
		pull = S3C_GPIO_PULL_NONE;
		break;
	case 0x02:
		pull = S3C_GPIO_PULL_DOWN;
		break;
	}

	return pull;
}

static int s3c24xx_gpio_setpull_1(struct samsung_gpio_chip *chip,
				  unsigned int off, samsung_gpio_pull_t pull,
				  samsung_gpio_pull_t updown)
{
	void __iomem *reg = chip->base + 0x08;
	u32 pup = __raw_readl(reg);

	if (pull == updown)
		pup &= ~(1 << off);
	else if (pull == S3C_GPIO_PULL_NONE)
		pup |= (1 << off);
	else
		return -EINVAL;

	__raw_writel(pup, reg);
	return 0;
}

static samsung_gpio_pull_t s3c24xx_gpio_getpull_1(struct samsung_gpio_chip *chip,
						  unsigned int off,
						  samsung_gpio_pull_t updown)
{
	void __iomem *reg = chip->base + 0x08;
	u32 pup = __raw_readl(reg);

	pup &= (1 << off);
	return pup ? S3C_GPIO_PULL_NONE : updown;
}

samsung_gpio_pull_t s3c24xx_gpio_getpull_1up(struct samsung_gpio_chip *chip,
					     unsigned int off)
{
	return s3c24xx_gpio_getpull_1(chip, off, S3C_GPIO_PULL_UP);
}

int s3c24xx_gpio_setpull_1up(struct samsung_gpio_chip *chip,
			     unsigned int off, samsung_gpio_pull_t pull)
{
	return s3c24xx_gpio_setpull_1(chip, off, pull, S3C_GPIO_PULL_UP);
}

samsung_gpio_pull_t s3c24xx_gpio_getpull_1down(struct samsung_gpio_chip *chip,
					       unsigned int off)
{
	return s3c24xx_gpio_getpull_1(chip, off, S3C_GPIO_PULL_DOWN);
}

int s3c24xx_gpio_setpull_1down(struct samsung_gpio_chip *chip,
			       unsigned int off, samsung_gpio_pull_t pull)
{
	return s3c24xx_gpio_setpull_1(chip, off, pull, S3C_GPIO_PULL_DOWN);
}

/*
 * samsung_gpio_setcfg_2bit - Samsung 2bit style GPIO configuration.
 * @chip: The gpio chip that is being configured.
 * @off: The offset for the GPIO being configured.
 * @cfg: The configuration value to set.
 *
 * This helper deal with the GPIO cases where the control register
 * has two bits of configuration per gpio, which have the following
 * functions:
 *	00 = input
 *	01 = output
 *	1x = special function
 */

static int samsung_gpio_setcfg_2bit(struct samsung_gpio_chip *chip,
				    unsigned int off, unsigned int cfg)
{
	void __iomem *reg = chip->base;
	unsigned int shift = off * 2;
	u32 con;

	if (samsung_gpio_is_cfg_special(cfg)) {
		cfg &= 0xf;
		if (cfg > 3)
			return -EINVAL;

		cfg <<= shift;
	}

	con = __raw_readl(reg);
	con &= ~(0x3 << shift);
	con |= cfg;
	__raw_writel(con, reg);

	return 0;
}

/*
 * samsung_gpio_getcfg_2bit - Samsung 2bit style GPIO configuration read.
 * @chip: The gpio chip that is being configured.
 * @off: The offset for the GPIO being configured.
 *
 * The reverse of samsung_gpio_setcfg_2bit(). Will return a value which
 * could be directly passed back to samsung_gpio_setcfg_2bit(), from the
 * S3C_GPIO_SPECIAL() macro.
 */

static unsigned int samsung_gpio_getcfg_2bit(struct samsung_gpio_chip *chip,
					     unsigned int off)
{
	u32 con;

	con = __raw_readl(chip->base);
	con >>= off * 2;
	con &= 3;

	/* this conversion works for IN and OUT as well as special mode */
	return S3C_GPIO_SPECIAL(con);
}

/*
 * samsung_gpio_setcfg_4bit - Samsung 4bit single register GPIO config.
 * @chip: The gpio chip that is being configured.
 * @off: The offset for the GPIO being configured.
 * @cfg: The configuration value to set.
 *
 * This helper deal with the GPIO cases where the control register has 4 bits
 * of control per GPIO, generally in the form of:
 *	0000 = Input
 *	0001 = Output
 *	others = Special functions (dependent on bank)
 *
 * Note, since the code to deal with the case where there are two control
 * registers instead of one, we do not have a separate set of functions for
 * each case.
 */

static int samsung_gpio_setcfg_4bit(struct samsung_gpio_chip *chip,
				    unsigned int off, unsigned int cfg)
{
	void __iomem *reg = chip->base;
	unsigned int shift = (off & 7) * 4;
	u32 con;

	if (off < 8 && chip->chip.ngpio > 8)
		reg -= 4;

	if (samsung_gpio_is_cfg_special(cfg)) {
		cfg &= 0xf;
		cfg <<= shift;
	}

	con = __raw_readl(reg);
	con &= ~(0xf << shift);
	con |= cfg;
	__raw_writel(con, reg);

	return 0;
}

/*
 * samsung_gpio_getcfg_4bit - Samsung 4bit single register GPIO config read.
 * @chip: The gpio chip that is being configured.
 * @off: The offset for the GPIO being configured.
 *
 * The reverse of samsung_gpio_setcfg_4bit(), turning a gpio configuration
 * register setting into a value the software can use, such as could be passed
 * to samsung_gpio_setcfg_4bit().
 *
 * @sa samsung_gpio_getcfg_2bit
 */

static unsigned samsung_gpio_getcfg_4bit(struct samsung_gpio_chip *chip,
					 unsigned int off)
{
	void __iomem *reg = chip->base;
	unsigned int shift = (off & 7) * 4;
	u32 con;

	if (off < 8 && chip->chip.ngpio > 8)
		reg -= 4;

	con = __raw_readl(reg);
	con >>= shift;
	con &= 0xf;

	/* this conversion works for IN and OUT as well as special mode */
	return S3C_GPIO_SPECIAL(con);
}

#ifdef CONFIG_PLAT_S3C24XX
/*
 * s3c24xx_gpio_setcfg_abank - S3C24XX style GPIO configuration (Bank A)
 * @chip: The gpio chip that is being configured.
 * @off: The offset for the GPIO being configured.
 * @cfg: The configuration value to set.
 *
 * This helper deal with the GPIO cases where the control register
 * has one bit of configuration for the gpio, where setting the bit
 * means the pin is in special function mode and unset means output.
 */

static int s3c24xx_gpio_setcfg_abank(struct samsung_gpio_chip *chip,
				     unsigned int off, unsigned int cfg)
{
	void __iomem *reg = chip->base;
	unsigned int shift = off;
	u32 con;

	if (samsung_gpio_is_cfg_special(cfg)) {
		cfg &= 0xf;

		/* Map output to 0, and SFN2 to 1 */
		cfg -= 1;
		if (cfg > 1)
			return -EINVAL;

		cfg <<= shift;
	}

	con = __raw_readl(reg);
	con &= ~(0x1 << shift);
	con |= cfg;
	__raw_writel(con, reg);

	return 0;
}

/*
 * s3c24xx_gpio_getcfg_abank - S3C24XX style GPIO configuration read (Bank A)
 * @chip: The gpio chip that is being configured.
 * @off: The offset for the GPIO being configured.
 *
 * The reverse of s3c24xx_gpio_setcfg_abank() turning an GPIO into a usable
 * GPIO configuration value.
 *
 * @sa samsung_gpio_getcfg_2bit
 * @sa samsung_gpio_getcfg_4bit
 */

static unsigned s3c24xx_gpio_getcfg_abank(struct samsung_gpio_chip *chip,
					  unsigned int off)
{
	u32 con;

	con = __raw_readl(chip->base);
	con >>= off;
	con &= 1;
	con++;

	return S3C_GPIO_SFN(con);
}
#endif

#if defined(CONFIG_CPU_S5P6440) || defined(CONFIG_CPU_S5P6450)
static int s5p64x0_gpio_setcfg_rbank(struct samsung_gpio_chip *chip,
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
		break;
	default:
		shift = ((off + 1) & 7) * 4;
		break;
	}

	if (samsung_gpio_is_cfg_special(cfg)) {
		cfg &= 0xf;
		cfg <<= shift;
	}

	con = __raw_readl(reg);
	con &= ~(0xf << shift);
	con |= cfg;
	__raw_writel(con, reg);

	return 0;
}
#endif

static void __init samsung_gpiolib_set_cfg(struct samsung_gpio_cfg *chipcfg,
					   int nr_chips)
{
	for (; nr_chips > 0; nr_chips--, chipcfg++) {
		if (!chipcfg->set_config)
			chipcfg->set_config = samsung_gpio_setcfg_4bit;
		if (!chipcfg->get_config)
			chipcfg->get_config = samsung_gpio_getcfg_4bit;
		if (!chipcfg->set_pull)
			chipcfg->set_pull = samsung_gpio_setpull_updown;
		if (!chipcfg->get_pull)
			chipcfg->get_pull = samsung_gpio_getpull_updown;
	}
}

struct samsung_gpio_cfg s3c24xx_gpiocfg_default = {
	.set_config	= samsung_gpio_setcfg_2bit,
	.get_config	= samsung_gpio_getcfg_2bit,
};

#ifdef CONFIG_PLAT_S3C24XX
static struct samsung_gpio_cfg s3c24xx_gpiocfg_banka = {
	.set_config	= s3c24xx_gpio_setcfg_abank,
	.get_config	= s3c24xx_gpio_getcfg_abank,
};
#endif

#if defined(CONFIG_CPU_S5P6440) || defined(CONFIG_CPU_S5P6450)
static struct samsung_gpio_cfg s5p64x0_gpio_cfg_rbank = {
	.cfg_eint	= 0x3,
	.set_config	= s5p64x0_gpio_setcfg_rbank,
	.get_config	= samsung_gpio_getcfg_4bit,
	.set_pull	= samsung_gpio_setpull_updown,
	.get_pull	= samsung_gpio_getpull_updown,
};
#endif

static struct samsung_gpio_cfg samsung_gpio_cfgs[] = {
	[0] = {
		.cfg_eint	= 0x0,
	},
	[1] = {
		.cfg_eint	= 0x3,
	},
	[2] = {
		.cfg_eint	= 0x7,
	},
	[3] = {
		.cfg_eint	= 0xF,
	},
	[4] = {
		.cfg_eint	= 0x0,
		.set_config	= samsung_gpio_setcfg_2bit,
		.get_config	= samsung_gpio_getcfg_2bit,
	},
	[5] = {
		.cfg_eint	= 0x2,
		.set_config	= samsung_gpio_setcfg_2bit,
		.get_config	= samsung_gpio_getcfg_2bit,
	},
	[6] = {
		.cfg_eint	= 0x3,
		.set_config	= samsung_gpio_setcfg_2bit,
		.get_config	= samsung_gpio_getcfg_2bit,
	},
	[7] = {
		.set_config	= samsung_gpio_setcfg_2bit,
		.get_config	= samsung_gpio_getcfg_2bit,
	},
};

/*
 * Default routines for controlling GPIO, based on the original S3C24XX
 * GPIO functions which deal with the case where each gpio bank of the
 * chip is as following:
 *
 * base + 0x00: Control register, 2 bits per gpio
 *	        gpio n: 2 bits starting at (2*n)
 *		00 = input, 01 = output, others mean special-function
 * base + 0x04: Data register, 1 bit per gpio
 *		bit n: data bit n
*/

static int samsung_gpiolib_2bit_input(struct gpio_chip *chip, unsigned offset)
{
	struct samsung_gpio_chip *ourchip = to_samsung_gpio(chip);
	void __iomem *base = ourchip->base;
	unsigned long flags;
	unsigned long con;

	samsung_gpio_lock(ourchip, flags);

	con = __raw_readl(base + 0x00);
	con &= ~(3 << (offset * 2));

	__raw_writel(con, base + 0x00);

	samsung_gpio_unlock(ourchip, flags);
	return 0;
}

static int samsung_gpiolib_2bit_output(struct gpio_chip *chip,
				       unsigned offset, int value)
{
	struct samsung_gpio_chip *ourchip = to_samsung_gpio(chip);
	void __iomem *base = ourchip->base;
	unsigned long flags;
	unsigned long dat;
	unsigned long con;

	samsung_gpio_lock(ourchip, flags);

	dat = __raw_readl(base + 0x04);
	dat &= ~(1 << offset);
	if (value)
		dat |= 1 << offset;
	__raw_writel(dat, base + 0x04);

	con = __raw_readl(base + 0x00);
	con &= ~(3 << (offset * 2));
	con |= 1 << (offset * 2);

	__raw_writel(con, base + 0x00);
	__raw_writel(dat, base + 0x04);

	samsung_gpio_unlock(ourchip, flags);
	return 0;
}

/*
 * The samsung_gpiolib_4bit routines are to control the gpio banks where
 * the gpio configuration register (GPxCON) has 4 bits per GPIO, as the
 * following example:
 *
 * base + 0x00: Control register, 4 bits per gpio
 *		gpio n: 4 bits starting at (4*n)
 *		0000 = input, 0001 = output, others mean special-function
 * base + 0x04: Data register, 1 bit per gpio
 *		bit n: data bit n
 *
 * Note, since the data register is one bit per gpio and is at base + 0x4
 * we can use samsung_gpiolib_get and samsung_gpiolib_set to change the
 * state of the output.
 */

static int samsung_gpiolib_4bit_input(struct gpio_chip *chip,
				      unsigned int offset)
{
	struct samsung_gpio_chip *ourchip = to_samsung_gpio(chip);
	void __iomem *base = ourchip->base;
	unsigned long con;

	con = __raw_readl(base + GPIOCON_OFF);
	if (ourchip->bitmap_gpio_int & BIT(offset))
		con |= 0xf << con_4bit_shift(offset);
	else
		con &= ~(0xf << con_4bit_shift(offset));
	__raw_writel(con, base + GPIOCON_OFF);

	pr_debug("%s: %p: CON now %08lx\n", __func__, base, con);

	return 0;
}

static int samsung_gpiolib_4bit_output(struct gpio_chip *chip,
				       unsigned int offset, int value)
{
	struct samsung_gpio_chip *ourchip = to_samsung_gpio(chip);
	void __iomem *base = ourchip->base;
	unsigned long con;
	unsigned long dat;

	con = __raw_readl(base + GPIOCON_OFF);
	con &= ~(0xf << con_4bit_shift(offset));
	con |= 0x1 << con_4bit_shift(offset);

	dat = __raw_readl(base + GPIODAT_OFF);

	if (value)
		dat |= 1 << offset;
	else
		dat &= ~(1 << offset);

	__raw_writel(dat, base + GPIODAT_OFF);
	__raw_writel(con, base + GPIOCON_OFF);
	__raw_writel(dat, base + GPIODAT_OFF);

	pr_debug("%s: %p: CON %08lx, DAT %08lx\n", __func__, base, con, dat);

	return 0;
}

/*
 * The next set of routines are for the case where the GPIO configuration
 * registers are 4 bits per GPIO but there is more than one register (the
 * bank has more than 8 GPIOs.
 *
 * This case is the similar to the 4 bit case, but the registers are as
 * follows:
 *
 * base + 0x00: Control register, 4 bits per gpio (lower 8 GPIOs)
 *		gpio n: 4 bits starting at (4*n)
 *		0000 = input, 0001 = output, others mean special-function
 * base + 0x04: Control register, 4 bits per gpio (up to 8 additions GPIOs)
 *		gpio n: 4 bits starting at (4*n)
 *		0000 = input, 0001 = output, others mean special-function
 * base + 0x08: Data register, 1 bit per gpio
 *		bit n: data bit n
 *
 * To allow us to use the samsung_gpiolib_get and samsung_gpiolib_set
 * routines we store the 'base + 0x4' address so that these routines see
 * the data register at ourchip->base + 0x04.
 */

static int samsung_gpiolib_4bit2_input(struct gpio_chip *chip,
				       unsigned int offset)
{
	struct samsung_gpio_chip *ourchip = to_samsung_gpio(chip);
	void __iomem *base = ourchip->base;
	void __iomem *regcon = base;
	unsigned long con;

	if (offset > 7)
		offset -= 8;
	else
		regcon -= 4;

	con = __raw_readl(regcon);
	con &= ~(0xf << con_4bit_shift(offset));
	__raw_writel(con, regcon);

	pr_debug("%s: %p: CON %08lx\n", __func__, base, con);

	return 0;
}

static int samsung_gpiolib_4bit2_output(struct gpio_chip *chip,
					unsigned int offset, int value)
{
	struct samsung_gpio_chip *ourchip = to_samsung_gpio(chip);
	void __iomem *base = ourchip->base;
	void __iomem *regcon = base;
	unsigned long con;
	unsigned long dat;
	unsigned con_offset = offset;

	if (con_offset > 7)
		con_offset -= 8;
	else
		regcon -= 4;

	con = __raw_readl(regcon);
	con &= ~(0xf << con_4bit_shift(con_offset));
	con |= 0x1 << con_4bit_shift(con_offset);

	dat = __raw_readl(base + GPIODAT_OFF);

	if (value)
		dat |= 1 << offset;
	else
		dat &= ~(1 << offset);

	__raw_writel(dat, base + GPIODAT_OFF);
	__raw_writel(con, regcon);
	__raw_writel(dat, base + GPIODAT_OFF);

	pr_debug("%s: %p: CON %08lx, DAT %08lx\n", __func__, base, con, dat);

	return 0;
}

#ifdef CONFIG_PLAT_S3C24XX
/* The next set of routines are for the case of s3c24xx bank a */

static int s3c24xx_gpiolib_banka_input(struct gpio_chip *chip, unsigned offset)
{
	return -EINVAL;
}

static int s3c24xx_gpiolib_banka_output(struct gpio_chip *chip,
					unsigned offset, int value)
{
	struct samsung_gpio_chip *ourchip = to_samsung_gpio(chip);
	void __iomem *base = ourchip->base;
	unsigned long flags;
	unsigned long dat;
	unsigned long con;

	local_irq_save(flags);

	con = __raw_readl(base + 0x00);
	dat = __raw_readl(base + 0x04);

	dat &= ~(1 << offset);
	if (value)
		dat |= 1 << offset;

	__raw_writel(dat, base + 0x04);

	con &= ~(1 << offset);

	__raw_writel(con, base + 0x00);
	__raw_writel(dat, base + 0x04);

	local_irq_restore(flags);
	return 0;
}
#endif

/* The next set of routines are for the case of s5p64x0 bank r */

static int s5p64x0_gpiolib_rbank_input(struct gpio_chip *chip,
				       unsigned int offset)
{
	struct samsung_gpio_chip *ourchip = to_samsung_gpio(chip);
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

	samsung_gpio_lock(ourchip, flags);

	con = __raw_readl(regcon);
	con &= ~(0xf << con_4bit_shift(offset));
	__raw_writel(con, regcon);

	samsung_gpio_unlock(ourchip, flags);

	return 0;
}

static int s5p64x0_gpiolib_rbank_output(struct gpio_chip *chip,
					unsigned int offset, int value)
{
	struct samsung_gpio_chip *ourchip = to_samsung_gpio(chip);
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

	samsung_gpio_lock(ourchip, flags);

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

	samsung_gpio_unlock(ourchip, flags);

	return 0;
}

static void samsung_gpiolib_set(struct gpio_chip *chip,
				unsigned offset, int value)
{
	struct samsung_gpio_chip *ourchip = to_samsung_gpio(chip);
	void __iomem *base = ourchip->base;
	unsigned long flags;
	unsigned long dat;

	samsung_gpio_lock(ourchip, flags);

	dat = __raw_readl(base + 0x04);
	dat &= ~(1 << offset);
	if (value)
		dat |= 1 << offset;
	__raw_writel(dat, base + 0x04);

	samsung_gpio_unlock(ourchip, flags);
}

static int samsung_gpiolib_get(struct gpio_chip *chip, unsigned offset)
{
	struct samsung_gpio_chip *ourchip = to_samsung_gpio(chip);
	unsigned long val;

	val = __raw_readl(ourchip->base + 0x04);
	val >>= offset;
	val &= 1;

	return val;
}

/*
 * CONFIG_S3C_GPIO_TRACK enables the tracking of the s3c specific gpios
 * for use with the configuration calls, and other parts of the s3c gpiolib
 * support code.
 *
 * Not all s3c support code will need this, as some configurations of cpu
 * may only support one or two different configuration options and have an
 * easy gpio to samsung_gpio_chip mapping function. If this is the case, then
 * the machine support file should provide its own samsung_gpiolib_getchip()
 * and any other necessary functions.
 */

#ifdef CONFIG_S3C_GPIO_TRACK
struct samsung_gpio_chip *s3c_gpios[S3C_GPIO_END];

static __init void s3c_gpiolib_track(struct samsung_gpio_chip *chip)
{
	unsigned int gpn;
	int i;

	gpn = chip->chip.base;
	for (i = 0; i < chip->chip.ngpio; i++, gpn++) {
		BUG_ON(gpn >= ARRAY_SIZE(s3c_gpios));
		s3c_gpios[gpn] = chip;
	}
}
#endif /* CONFIG_S3C_GPIO_TRACK */

/*
 * samsung_gpiolib_add() - add the Samsung gpio_chip.
 * @chip: The chip to register
 *
 * This is a wrapper to gpiochip_add() that takes our specific gpio chip
 * information and makes the necessary alterations for the platform and
 * notes the information for use with the configuration systems and any
 * other parts of the system.
 */

static void __init samsung_gpiolib_add(struct samsung_gpio_chip *chip)
{
	struct gpio_chip *gc = &chip->chip;
	int ret;

	BUG_ON(!chip->base);
	BUG_ON(!gc->label);
	BUG_ON(!gc->ngpio);

	spin_lock_init(&chip->lock);

	if (!gc->direction_input)
		gc->direction_input = samsung_gpiolib_2bit_input;
	if (!gc->direction_output)
		gc->direction_output = samsung_gpiolib_2bit_output;
	if (!gc->set)
		gc->set = samsung_gpiolib_set;
	if (!gc->get)
		gc->get = samsung_gpiolib_get;

#ifdef CONFIG_PM
	if (chip->pm != NULL) {
		if (!chip->pm->save || !chip->pm->resume)
			pr_err("gpio: %s has missing PM functions\n",
			       gc->label);
	} else
		pr_err("gpio: %s has no PM function\n", gc->label);
#endif

	/* gpiochip_add() prints own failure message on error. */
	ret = gpiochip_add(gc);
	if (ret >= 0)
		s3c_gpiolib_track(chip);
}

static void __init s3c24xx_gpiolib_add_chips(struct samsung_gpio_chip *chip,
					     int nr_chips, void __iomem *base)
{
	int i;
	struct gpio_chip *gc = &chip->chip;

	for (i = 0 ; i < nr_chips; i++, chip++) {
		/* skip banks not present on SoC */
		if (chip->chip.base >= S3C_GPIO_END)
			continue;

		if (!chip->config)
			chip->config = &s3c24xx_gpiocfg_default;
		if (!chip->pm)
			chip->pm = __gpio_pm(&samsung_gpio_pm_2bit);
		if ((base != NULL) && (chip->base == NULL))
			chip->base = base + ((i) * 0x10);

		if (!gc->direction_input)
			gc->direction_input = samsung_gpiolib_2bit_input;
		if (!gc->direction_output)
			gc->direction_output = samsung_gpiolib_2bit_output;

		samsung_gpiolib_add(chip);
	}
}

static void __init samsung_gpiolib_add_2bit_chips(struct samsung_gpio_chip *chip,
						  int nr_chips, void __iomem *base,
						  unsigned int offset)
{
	int i;

	for (i = 0 ; i < nr_chips; i++, chip++) {
		chip->chip.direction_input = samsung_gpiolib_2bit_input;
		chip->chip.direction_output = samsung_gpiolib_2bit_output;

		if (!chip->config)
			chip->config = &samsung_gpio_cfgs[7];
		if (!chip->pm)
			chip->pm = __gpio_pm(&samsung_gpio_pm_2bit);
		if ((base != NULL) && (chip->base == NULL))
			chip->base = base + ((i) * offset);

		samsung_gpiolib_add(chip);
	}
}

/*
 * samsung_gpiolib_add_4bit_chips - 4bit single register GPIO config.
 * @chip: The gpio chip that is being configured.
 * @nr_chips: The no of chips (gpio ports) for the GPIO being configured.
 *
 * This helper deal with the GPIO cases where the control register has 4 bits
 * of control per GPIO, generally in the form of:
 * 0000 = Input
 * 0001 = Output
 * others = Special functions (dependent on bank)
 *
 * Note, since the code to deal with the case where there are two control
 * registers instead of one, we do not have a separate set of function
 * (samsung_gpiolib_add_4bit2_chips)for each case.
 */

static void __init samsung_gpiolib_add_4bit_chips(struct samsung_gpio_chip *chip,
						  int nr_chips, void __iomem *base)
{
	int i;

	for (i = 0 ; i < nr_chips; i++, chip++) {
		chip->chip.direction_input = samsung_gpiolib_4bit_input;
		chip->chip.direction_output = samsung_gpiolib_4bit_output;

		if (!chip->config)
			chip->config = &samsung_gpio_cfgs[2];
		if (!chip->pm)
			chip->pm = __gpio_pm(&samsung_gpio_pm_4bit);
		if ((base != NULL) && (chip->base == NULL))
			chip->base = base + ((i) * 0x20);

		chip->bitmap_gpio_int = 0;

		samsung_gpiolib_add(chip);
	}
}

static void __init samsung_gpiolib_add_4bit2_chips(struct samsung_gpio_chip *chip,
						   int nr_chips)
{
	for (; nr_chips > 0; nr_chips--, chip++) {
		chip->chip.direction_input = samsung_gpiolib_4bit2_input;
		chip->chip.direction_output = samsung_gpiolib_4bit2_output;

		if (!chip->config)
			chip->config = &samsung_gpio_cfgs[2];
		if (!chip->pm)
			chip->pm = __gpio_pm(&samsung_gpio_pm_4bit);

		samsung_gpiolib_add(chip);
	}
}

static void __init s5p64x0_gpiolib_add_rbank(struct samsung_gpio_chip *chip,
					     int nr_chips)
{
	for (; nr_chips > 0; nr_chips--, chip++) {
		chip->chip.direction_input = s5p64x0_gpiolib_rbank_input;
		chip->chip.direction_output = s5p64x0_gpiolib_rbank_output;

		if (!chip->pm)
			chip->pm = __gpio_pm(&samsung_gpio_pm_4bit);

		samsung_gpiolib_add(chip);
	}
}

int samsung_gpiolib_to_irq(struct gpio_chip *chip, unsigned int offset)
{
	struct samsung_gpio_chip *samsung_chip = container_of(chip, struct samsung_gpio_chip, chip);

	return samsung_chip->irq_base + offset;
}

#ifdef CONFIG_PLAT_S3C24XX
static int s3c24xx_gpiolib_fbank_to_irq(struct gpio_chip *chip, unsigned offset)
{
	if (offset < 4) {
		if (soc_is_s3c2412())
			return IRQ_EINT0_2412 + offset;
		else
			return IRQ_EINT0 + offset;
	}

	if (offset < 8)
		return IRQ_EINT4 + offset - 4;

	return -EINVAL;
}
#endif

#ifdef CONFIG_ARCH_S3C64XX
static int s3c64xx_gpiolib_mbank_to_irq(struct gpio_chip *chip, unsigned pin)
{
	return pin < 5 ? IRQ_EINT(23) + pin : -ENXIO;
}

static int s3c64xx_gpiolib_lbank_to_irq(struct gpio_chip *chip, unsigned pin)
{
	return pin >= 8 ? IRQ_EINT(16) + pin - 8 : -ENXIO;
}
#endif

struct samsung_gpio_chip s3c24xx_gpios[] = {
#ifdef CONFIG_PLAT_S3C24XX
	{
		.config	= &s3c24xx_gpiocfg_banka,
		.chip	= {
			.base			= S3C2410_GPA(0),
			.owner			= THIS_MODULE,
			.label			= "GPIOA",
			.ngpio			= 27,
			.direction_input	= s3c24xx_gpiolib_banka_input,
			.direction_output	= s3c24xx_gpiolib_banka_output,
		},
	}, {
		.chip	= {
			.base	= S3C2410_GPB(0),
			.owner	= THIS_MODULE,
			.label	= "GPIOB",
			.ngpio	= 11,
		},
	}, {
		.chip	= {
			.base	= S3C2410_GPC(0),
			.owner	= THIS_MODULE,
			.label	= "GPIOC",
			.ngpio	= 16,
		},
	}, {
		.chip	= {
			.base	= S3C2410_GPD(0),
			.owner	= THIS_MODULE,
			.label	= "GPIOD",
			.ngpio	= 16,
		},
	}, {
		.chip	= {
			.base	= S3C2410_GPE(0),
			.label	= "GPIOE",
			.owner	= THIS_MODULE,
			.ngpio	= 16,
		},
	}, {
		.chip	= {
			.base	= S3C2410_GPF(0),
			.owner	= THIS_MODULE,
			.label	= "GPIOF",
			.ngpio	= 8,
			.to_irq	= s3c24xx_gpiolib_fbank_to_irq,
		},
	}, {
		.irq_base = IRQ_EINT8,
		.chip	= {
			.base	= S3C2410_GPG(0),
			.owner	= THIS_MODULE,
			.label	= "GPIOG",
			.ngpio	= 16,
			.to_irq	= samsung_gpiolib_to_irq,
		},
	}, {
		.chip	= {
			.base	= S3C2410_GPH(0),
			.owner	= THIS_MODULE,
			.label	= "GPIOH",
			.ngpio	= 15,
		},
	},
		/* GPIOS for the S3C2443 and later devices. */
	{
		.base	= S3C2440_GPJCON,
		.chip	= {
			.base	= S3C2410_GPJ(0),
			.owner	= THIS_MODULE,
			.label	= "GPIOJ",
			.ngpio	= 16,
		},
	}, {
		.base	= S3C2443_GPKCON,
		.chip	= {
			.base	= S3C2410_GPK(0),
			.owner	= THIS_MODULE,
			.label	= "GPIOK",
			.ngpio	= 16,
		},
	}, {
		.base	= S3C2443_GPLCON,
		.chip	= {
			.base	= S3C2410_GPL(0),
			.owner	= THIS_MODULE,
			.label	= "GPIOL",
			.ngpio	= 15,
		},
	}, {
		.base	= S3C2443_GPMCON,
		.chip	= {
			.base	= S3C2410_GPM(0),
			.owner	= THIS_MODULE,
			.label	= "GPIOM",
			.ngpio	= 2,
		},
	},
#endif
};

/*
 * GPIO bank summary:
 *
 * Bank	GPIOs	Style	SlpCon	ExtInt Group
 * A	8	4Bit	Yes	1
 * B	7	4Bit	Yes	1
 * C	8	4Bit	Yes	2
 * D	5	4Bit	Yes	3
 * E	5	4Bit	Yes	None
 * F	16	2Bit	Yes	4 [1]
 * G	7	4Bit	Yes	5
 * H	10	4Bit[2]	Yes	6
 * I	16	2Bit	Yes	None
 * J	12	2Bit	Yes	None
 * K	16	4Bit[2]	No	None
 * L	15	4Bit[2] No	None
 * M	6	4Bit	No	IRQ_EINT
 * N	16	2Bit	No	IRQ_EINT
 * O	16	2Bit	Yes	7
 * P	15	2Bit	Yes	8
 * Q	9	2Bit	Yes	9
 *
 * [1] BANKF pins 14,15 do not form part of the external interrupt sources
 * [2] BANK has two control registers, GPxCON0 and GPxCON1
 */

static struct samsung_gpio_chip s3c64xx_gpios_4bit[] = {
#ifdef CONFIG_ARCH_S3C64XX
	{
		.chip	= {
			.base	= S3C64XX_GPA(0),
			.ngpio	= S3C64XX_GPIO_A_NR,
			.label	= "GPA",
		},
	}, {
		.chip	= {
			.base	= S3C64XX_GPB(0),
			.ngpio	= S3C64XX_GPIO_B_NR,
			.label	= "GPB",
		},
	}, {
		.chip	= {
			.base	= S3C64XX_GPC(0),
			.ngpio	= S3C64XX_GPIO_C_NR,
			.label	= "GPC",
		},
	}, {
		.chip	= {
			.base	= S3C64XX_GPD(0),
			.ngpio	= S3C64XX_GPIO_D_NR,
			.label	= "GPD",
		},
	}, {
		.config	= &samsung_gpio_cfgs[0],
		.chip	= {
			.base	= S3C64XX_GPE(0),
			.ngpio	= S3C64XX_GPIO_E_NR,
			.label	= "GPE",
		},
	}, {
		.base	= S3C64XX_GPG_BASE,
		.chip	= {
			.base	= S3C64XX_GPG(0),
			.ngpio	= S3C64XX_GPIO_G_NR,
			.label	= "GPG",
		},
	}, {
		.base	= S3C64XX_GPM_BASE,
		.config	= &samsung_gpio_cfgs[1],
		.chip	= {
			.base	= S3C64XX_GPM(0),
			.ngpio	= S3C64XX_GPIO_M_NR,
			.label	= "GPM",
			.to_irq = s3c64xx_gpiolib_mbank_to_irq,
		},
	},
#endif
};

static struct samsung_gpio_chip s3c64xx_gpios_4bit2[] = {
#ifdef CONFIG_ARCH_S3C64XX
	{
		.base	= S3C64XX_GPH_BASE + 0x4,
		.chip	= {
			.base	= S3C64XX_GPH(0),
			.ngpio	= S3C64XX_GPIO_H_NR,
			.label	= "GPH",
		},
	}, {
		.base	= S3C64XX_GPK_BASE + 0x4,
		.config	= &samsung_gpio_cfgs[0],
		.chip	= {
			.base	= S3C64XX_GPK(0),
			.ngpio	= S3C64XX_GPIO_K_NR,
			.label	= "GPK",
		},
	}, {
		.base	= S3C64XX_GPL_BASE + 0x4,
		.config	= &samsung_gpio_cfgs[1],
		.chip	= {
			.base	= S3C64XX_GPL(0),
			.ngpio	= S3C64XX_GPIO_L_NR,
			.label	= "GPL",
			.to_irq = s3c64xx_gpiolib_lbank_to_irq,
		},
	},
#endif
};

static struct samsung_gpio_chip s3c64xx_gpios_2bit[] = {
#ifdef CONFIG_ARCH_S3C64XX
	{
		.base	= S3C64XX_GPF_BASE,
		.config	= &samsung_gpio_cfgs[6],
		.chip	= {
			.base	= S3C64XX_GPF(0),
			.ngpio	= S3C64XX_GPIO_F_NR,
			.label	= "GPF",
		},
	}, {
		.config	= &samsung_gpio_cfgs[7],
		.chip	= {
			.base	= S3C64XX_GPI(0),
			.ngpio	= S3C64XX_GPIO_I_NR,
			.label	= "GPI",
		},
	}, {
		.config	= &samsung_gpio_cfgs[7],
		.chip	= {
			.base	= S3C64XX_GPJ(0),
			.ngpio	= S3C64XX_GPIO_J_NR,
			.label	= "GPJ",
		},
	}, {
		.config	= &samsung_gpio_cfgs[6],
		.chip	= {
			.base	= S3C64XX_GPO(0),
			.ngpio	= S3C64XX_GPIO_O_NR,
			.label	= "GPO",
		},
	}, {
		.config	= &samsung_gpio_cfgs[6],
		.chip	= {
			.base	= S3C64XX_GPP(0),
			.ngpio	= S3C64XX_GPIO_P_NR,
			.label	= "GPP",
		},
	}, {
		.config	= &samsung_gpio_cfgs[6],
		.chip	= {
			.base	= S3C64XX_GPQ(0),
			.ngpio	= S3C64XX_GPIO_Q_NR,
			.label	= "GPQ",
		},
	}, {
		.base	= S3C64XX_GPN_BASE,
		.irq_base = IRQ_EINT(0),
		.config	= &samsung_gpio_cfgs[5],
		.chip	= {
			.base	= S3C64XX_GPN(0),
			.ngpio	= S3C64XX_GPIO_N_NR,
			.label	= "GPN",
			.to_irq = samsung_gpiolib_to_irq,
		},
	},
#endif
};

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
 */

static struct samsung_gpio_chip s5p6440_gpios_4bit[] = {
#ifdef CONFIG_CPU_S5P6440
	{
		.chip	= {
			.base	= S5P6440_GPA(0),
			.ngpio	= S5P6440_GPIO_A_NR,
			.label	= "GPA",
		},
	}, {
		.chip	= {
			.base	= S5P6440_GPB(0),
			.ngpio	= S5P6440_GPIO_B_NR,
			.label	= "GPB",
		},
	}, {
		.chip	= {
			.base	= S5P6440_GPC(0),
			.ngpio	= S5P6440_GPIO_C_NR,
			.label	= "GPC",
		},
	}, {
		.base	= S5P64X0_GPG_BASE,
		.chip	= {
			.base	= S5P6440_GPG(0),
			.ngpio	= S5P6440_GPIO_G_NR,
			.label	= "GPG",
		},
	},
#endif
};

static struct samsung_gpio_chip s5p6440_gpios_4bit2[] = {
#ifdef CONFIG_CPU_S5P6440
	{
		.base	= S5P64X0_GPH_BASE + 0x4,
		.chip	= {
			.base	= S5P6440_GPH(0),
			.ngpio	= S5P6440_GPIO_H_NR,
			.label	= "GPH",
		},
	},
#endif
};

static struct samsung_gpio_chip s5p6440_gpios_rbank[] = {
#ifdef CONFIG_CPU_S5P6440
	{
		.base	= S5P64X0_GPR_BASE + 0x4,
		.config	= &s5p64x0_gpio_cfg_rbank,
		.chip	= {
			.base	= S5P6440_GPR(0),
			.ngpio	= S5P6440_GPIO_R_NR,
			.label	= "GPR",
		},
	},
#endif
};

static struct samsung_gpio_chip s5p6440_gpios_2bit[] = {
#ifdef CONFIG_CPU_S5P6440
	{
		.base	= S5P64X0_GPF_BASE,
		.config	= &samsung_gpio_cfgs[6],
		.chip	= {
			.base	= S5P6440_GPF(0),
			.ngpio	= S5P6440_GPIO_F_NR,
			.label	= "GPF",
		},
	}, {
		.base	= S5P64X0_GPI_BASE,
		.config	= &samsung_gpio_cfgs[4],
		.chip	= {
			.base	= S5P6440_GPI(0),
			.ngpio	= S5P6440_GPIO_I_NR,
			.label	= "GPI",
		},
	}, {
		.base	= S5P64X0_GPJ_BASE,
		.config	= &samsung_gpio_cfgs[4],
		.chip	= {
			.base	= S5P6440_GPJ(0),
			.ngpio	= S5P6440_GPIO_J_NR,
			.label	= "GPJ",
		},
	}, {
		.base	= S5P64X0_GPN_BASE,
		.config	= &samsung_gpio_cfgs[5],
		.chip	= {
			.base	= S5P6440_GPN(0),
			.ngpio	= S5P6440_GPIO_N_NR,
			.label	= "GPN",
		},
	}, {
		.base	= S5P64X0_GPP_BASE,
		.config	= &samsung_gpio_cfgs[6],
		.chip	= {
			.base	= S5P6440_GPP(0),
			.ngpio	= S5P6440_GPIO_P_NR,
			.label	= "GPP",
		},
	},
#endif
};

/*
 * S5P6450 GPIO bank summary:
 *
 * Bank	GPIOs	Style	SlpCon	ExtInt Group
 * A	6	4Bit	Yes	1
 * B	7	4Bit	Yes	1
 * C	8	4Bit	Yes	2
 * D	8	4Bit	Yes	None
 * F	2	2Bit	Yes	None
 * G	14	4Bit[2]	Yes	5
 * H	10	4Bit[2]	Yes	6
 * I	16	2Bit	Yes	None
 * J	12	2Bit	Yes	None
 * K	5	4Bit	Yes	None
 * N	16	2Bit	No	IRQ_EINT
 * P	11	2Bit	Yes	8
 * Q	14	2Bit	Yes	None
 * R	15	4Bit[2]	Yes	None
 * S	8	2Bit	Yes	None
 *
 * [1] BANKF pins 14,15 do not form part of the external interrupt sources
 * [2] BANK has two control registers, GPxCON0 and GPxCON1
 */

static struct samsung_gpio_chip s5p6450_gpios_4bit[] = {
#ifdef CONFIG_CPU_S5P6450
	{
		.chip	= {
			.base	= S5P6450_GPA(0),
			.ngpio	= S5P6450_GPIO_A_NR,
			.label	= "GPA",
		},
	}, {
		.chip	= {
			.base	= S5P6450_GPB(0),
			.ngpio	= S5P6450_GPIO_B_NR,
			.label	= "GPB",
		},
	}, {
		.chip	= {
			.base	= S5P6450_GPC(0),
			.ngpio	= S5P6450_GPIO_C_NR,
			.label	= "GPC",
		},
	}, {
		.chip	= {
			.base	= S5P6450_GPD(0),
			.ngpio	= S5P6450_GPIO_D_NR,
			.label	= "GPD",
		},
	}, {
		.base	= S5P6450_GPK_BASE,
		.chip	= {
			.base	= S5P6450_GPK(0),
			.ngpio	= S5P6450_GPIO_K_NR,
			.label	= "GPK",
		},
	},
#endif
};

static struct samsung_gpio_chip s5p6450_gpios_4bit2[] = {
#ifdef CONFIG_CPU_S5P6450
	{
		.base	= S5P64X0_GPG_BASE + 0x4,
		.chip	= {
			.base	= S5P6450_GPG(0),
			.ngpio	= S5P6450_GPIO_G_NR,
			.label	= "GPG",
		},
	}, {
		.base	= S5P64X0_GPH_BASE + 0x4,
		.chip	= {
			.base	= S5P6450_GPH(0),
			.ngpio	= S5P6450_GPIO_H_NR,
			.label	= "GPH",
		},
	},
#endif
};

static struct samsung_gpio_chip s5p6450_gpios_rbank[] = {
#ifdef CONFIG_CPU_S5P6450
	{
		.base	= S5P64X0_GPR_BASE + 0x4,
		.config	= &s5p64x0_gpio_cfg_rbank,
		.chip	= {
			.base	= S5P6450_GPR(0),
			.ngpio	= S5P6450_GPIO_R_NR,
			.label	= "GPR",
		},
	},
#endif
};

static struct samsung_gpio_chip s5p6450_gpios_2bit[] = {
#ifdef CONFIG_CPU_S5P6450
	{
		.base	= S5P64X0_GPF_BASE,
		.config	= &samsung_gpio_cfgs[6],
		.chip	= {
			.base	= S5P6450_GPF(0),
			.ngpio	= S5P6450_GPIO_F_NR,
			.label	= "GPF",
		},
	}, {
		.base	= S5P64X0_GPI_BASE,
		.config	= &samsung_gpio_cfgs[4],
		.chip	= {
			.base	= S5P6450_GPI(0),
			.ngpio	= S5P6450_GPIO_I_NR,
			.label	= "GPI",
		},
	}, {
		.base	= S5P64X0_GPJ_BASE,
		.config	= &samsung_gpio_cfgs[4],
		.chip	= {
			.base	= S5P6450_GPJ(0),
			.ngpio	= S5P6450_GPIO_J_NR,
			.label	= "GPJ",
		},
	}, {
		.base	= S5P64X0_GPN_BASE,
		.config	= &samsung_gpio_cfgs[5],
		.chip	= {
			.base	= S5P6450_GPN(0),
			.ngpio	= S5P6450_GPIO_N_NR,
			.label	= "GPN",
		},
	}, {
		.base	= S5P64X0_GPP_BASE,
		.config	= &samsung_gpio_cfgs[6],
		.chip	= {
			.base	= S5P6450_GPP(0),
			.ngpio	= S5P6450_GPIO_P_NR,
			.label	= "GPP",
		},
	}, {
		.base	= S5P6450_GPQ_BASE,
		.config	= &samsung_gpio_cfgs[5],
		.chip	= {
			.base	= S5P6450_GPQ(0),
			.ngpio	= S5P6450_GPIO_Q_NR,
			.label	= "GPQ",
		},
	}, {
		.base	= S5P6450_GPS_BASE,
		.config	= &samsung_gpio_cfgs[6],
		.chip	= {
			.base	= S5P6450_GPS(0),
			.ngpio	= S5P6450_GPIO_S_NR,
			.label	= "GPS",
		},
	},
#endif
};

/*
 * S5PC100 GPIO bank summary:
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

static struct samsung_gpio_chip s5pc100_gpios_4bit[] = {
#ifdef CONFIG_CPU_S5PC100
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
		.chip	= {
			.base	= S5PC100_GPK0(0),
			.ngpio	= S5PC100_GPIO_K0_NR,
			.label	= "GPK0",
		},
	}, {
		.chip	= {
			.base	= S5PC100_GPK1(0),
			.ngpio	= S5PC100_GPIO_K1_NR,
			.label	= "GPK1",
		},
	}, {
		.chip	= {
			.base	= S5PC100_GPK2(0),
			.ngpio	= S5PC100_GPIO_K2_NR,
			.label	= "GPK2",
		},
	}, {
		.chip	= {
			.base	= S5PC100_GPK3(0),
			.ngpio	= S5PC100_GPIO_K3_NR,
			.label	= "GPK3",
		},
	}, {
		.chip	= {
			.base	= S5PC100_GPL0(0),
			.ngpio	= S5PC100_GPIO_L0_NR,
			.label	= "GPL0",
		},
	}, {
		.chip	= {
			.base	= S5PC100_GPL1(0),
			.ngpio	= S5PC100_GPIO_L1_NR,
			.label	= "GPL1",
		},
	}, {
		.chip	= {
			.base	= S5PC100_GPL2(0),
			.ngpio	= S5PC100_GPIO_L2_NR,
			.label	= "GPL2",
		},
	}, {
		.chip	= {
			.base	= S5PC100_GPL3(0),
			.ngpio	= S5PC100_GPIO_L3_NR,
			.label	= "GPL3",
		},
	}, {
		.chip	= {
			.base	= S5PC100_GPL4(0),
			.ngpio	= S5PC100_GPIO_L4_NR,
			.label	= "GPL4",
		},
	}, {
		.base	= (S5P_VA_GPIO + 0xC00),
		.irq_base = IRQ_EINT(0),
		.chip	= {
			.base	= S5PC100_GPH0(0),
			.ngpio	= S5PC100_GPIO_H0_NR,
			.label	= "GPH0",
			.to_irq = samsung_gpiolib_to_irq,
		},
	}, {
		.base	= (S5P_VA_GPIO + 0xC20),
		.irq_base = IRQ_EINT(8),
		.chip	= {
			.base	= S5PC100_GPH1(0),
			.ngpio	= S5PC100_GPIO_H1_NR,
			.label	= "GPH1",
			.to_irq = samsung_gpiolib_to_irq,
		},
	}, {
		.base	= (S5P_VA_GPIO + 0xC40),
		.irq_base = IRQ_EINT(16),
		.chip	= {
			.base	= S5PC100_GPH2(0),
			.ngpio	= S5PC100_GPIO_H2_NR,
			.label	= "GPH2",
			.to_irq = samsung_gpiolib_to_irq,
		},
	}, {
		.base	= (S5P_VA_GPIO + 0xC60),
		.irq_base = IRQ_EINT(24),
		.chip	= {
			.base	= S5PC100_GPH3(0),
			.ngpio	= S5PC100_GPIO_H3_NR,
			.label	= "GPH3",
			.to_irq = samsung_gpiolib_to_irq,
		},
	},
#endif
};

/*
 * Followings are the gpio banks in S5PV210/S5PC110
 *
 * The 'config' member when left to NULL, is initialized to the default
 * structure samsung_gpio_cfgs[3] in the init function below.
 *
 * The 'base' member is also initialized in the init function below.
 * Note: The initialization of 'base' member of samsung_gpio_chip structure
 * uses the above macro and depends on the banks being listed in order here.
 */

static struct samsung_gpio_chip s5pv210_gpios_4bit[] = {
#ifdef CONFIG_CPU_S5PV210
	{
		.chip	= {
			.base	= S5PV210_GPA0(0),
			.ngpio	= S5PV210_GPIO_A0_NR,
			.label	= "GPA0",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPA1(0),
			.ngpio	= S5PV210_GPIO_A1_NR,
			.label	= "GPA1",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPB(0),
			.ngpio	= S5PV210_GPIO_B_NR,
			.label	= "GPB",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPC0(0),
			.ngpio	= S5PV210_GPIO_C0_NR,
			.label	= "GPC0",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPC1(0),
			.ngpio	= S5PV210_GPIO_C1_NR,
			.label	= "GPC1",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPD0(0),
			.ngpio	= S5PV210_GPIO_D0_NR,
			.label	= "GPD0",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPD1(0),
			.ngpio	= S5PV210_GPIO_D1_NR,
			.label	= "GPD1",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPE0(0),
			.ngpio	= S5PV210_GPIO_E0_NR,
			.label	= "GPE0",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPE1(0),
			.ngpio	= S5PV210_GPIO_E1_NR,
			.label	= "GPE1",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPF0(0),
			.ngpio	= S5PV210_GPIO_F0_NR,
			.label	= "GPF0",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPF1(0),
			.ngpio	= S5PV210_GPIO_F1_NR,
			.label	= "GPF1",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPF2(0),
			.ngpio	= S5PV210_GPIO_F2_NR,
			.label	= "GPF2",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPF3(0),
			.ngpio	= S5PV210_GPIO_F3_NR,
			.label	= "GPF3",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPG0(0),
			.ngpio	= S5PV210_GPIO_G0_NR,
			.label	= "GPG0",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPG1(0),
			.ngpio	= S5PV210_GPIO_G1_NR,
			.label	= "GPG1",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPG2(0),
			.ngpio	= S5PV210_GPIO_G2_NR,
			.label	= "GPG2",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPG3(0),
			.ngpio	= S5PV210_GPIO_G3_NR,
			.label	= "GPG3",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPI(0),
			.ngpio	= S5PV210_GPIO_I_NR,
			.label	= "GPI",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPJ0(0),
			.ngpio	= S5PV210_GPIO_J0_NR,
			.label	= "GPJ0",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPJ1(0),
			.ngpio	= S5PV210_GPIO_J1_NR,
			.label	= "GPJ1",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPJ2(0),
			.ngpio	= S5PV210_GPIO_J2_NR,
			.label	= "GPJ2",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPJ3(0),
			.ngpio	= S5PV210_GPIO_J3_NR,
			.label	= "GPJ3",
		},
	}, {
		.chip	= {
			.base	= S5PV210_GPJ4(0),
			.ngpio	= S5PV210_GPIO_J4_NR,
			.label	= "GPJ4",
		},
	}, {
		.chip	= {
			.base	= S5PV210_MP01(0),
			.ngpio	= S5PV210_GPIO_MP01_NR,
			.label	= "MP01",
		},
	}, {
		.chip	= {
			.base	= S5PV210_MP02(0),
			.ngpio	= S5PV210_GPIO_MP02_NR,
			.label	= "MP02",
		},
	}, {
		.chip	= {
			.base	= S5PV210_MP03(0),
			.ngpio	= S5PV210_GPIO_MP03_NR,
			.label	= "MP03",
		},
	}, {
		.chip	= {
			.base	= S5PV210_MP04(0),
			.ngpio	= S5PV210_GPIO_MP04_NR,
			.label	= "MP04",
		},
	}, {
		.chip	= {
			.base	= S5PV210_MP05(0),
			.ngpio	= S5PV210_GPIO_MP05_NR,
			.label	= "MP05",
		},
	}, {
		.base	= (S5P_VA_GPIO + 0xC00),
		.irq_base = IRQ_EINT(0),
		.chip	= {
			.base	= S5PV210_GPH0(0),
			.ngpio	= S5PV210_GPIO_H0_NR,
			.label	= "GPH0",
			.to_irq = samsung_gpiolib_to_irq,
		},
	}, {
		.base	= (S5P_VA_GPIO + 0xC20),
		.irq_base = IRQ_EINT(8),
		.chip	= {
			.base	= S5PV210_GPH1(0),
			.ngpio	= S5PV210_GPIO_H1_NR,
			.label	= "GPH1",
			.to_irq = samsung_gpiolib_to_irq,
		},
	}, {
		.base	= (S5P_VA_GPIO + 0xC40),
		.irq_base = IRQ_EINT(16),
		.chip	= {
			.base	= S5PV210_GPH2(0),
			.ngpio	= S5PV210_GPIO_H2_NR,
			.label	= "GPH2",
			.to_irq = samsung_gpiolib_to_irq,
		},
	}, {
		.base	= (S5P_VA_GPIO + 0xC60),
		.irq_base = IRQ_EINT(24),
		.chip	= {
			.base	= S5PV210_GPH3(0),
			.ngpio	= S5PV210_GPIO_H3_NR,
			.label	= "GPH3",
			.to_irq = samsung_gpiolib_to_irq,
		},
	},
#endif
};

/* TODO: cleanup soc_is_* */
static __init int samsung_gpiolib_init(void)
{
	struct samsung_gpio_chip *chip;
	int i, nr_chips;
	int group = 0;

	/*
	 * Currently there are two drivers that can provide GPIO support for
	 * Samsung SoCs. For device tree enabled platforms, the new
	 * pinctrl-samsung driver is used, providing both GPIO and pin control
	 * interfaces. For legacy (non-DT) platforms this driver is used.
	 */
	if (of_have_populated_dt())
		return -ENODEV;

	samsung_gpiolib_set_cfg(samsung_gpio_cfgs, ARRAY_SIZE(samsung_gpio_cfgs));

	if (soc_is_s3c24xx()) {
		s3c24xx_gpiolib_add_chips(s3c24xx_gpios,
				ARRAY_SIZE(s3c24xx_gpios), S3C24XX_VA_GPIO);
	} else if (soc_is_s3c64xx()) {
		samsung_gpiolib_add_2bit_chips(s3c64xx_gpios_2bit,
				ARRAY_SIZE(s3c64xx_gpios_2bit),
				S3C64XX_VA_GPIO + 0xE0, 0x20);
		samsung_gpiolib_add_4bit_chips(s3c64xx_gpios_4bit,
				ARRAY_SIZE(s3c64xx_gpios_4bit),
				S3C64XX_VA_GPIO);
		samsung_gpiolib_add_4bit2_chips(s3c64xx_gpios_4bit2,
				ARRAY_SIZE(s3c64xx_gpios_4bit2));
	} else if (soc_is_s5p6440()) {
		samsung_gpiolib_add_2bit_chips(s5p6440_gpios_2bit,
				ARRAY_SIZE(s5p6440_gpios_2bit), NULL, 0x0);
		samsung_gpiolib_add_4bit_chips(s5p6440_gpios_4bit,
				ARRAY_SIZE(s5p6440_gpios_4bit), S5P_VA_GPIO);
		samsung_gpiolib_add_4bit2_chips(s5p6440_gpios_4bit2,
				ARRAY_SIZE(s5p6440_gpios_4bit2));
		s5p64x0_gpiolib_add_rbank(s5p6440_gpios_rbank,
				ARRAY_SIZE(s5p6440_gpios_rbank));
	} else if (soc_is_s5p6450()) {
		samsung_gpiolib_add_2bit_chips(s5p6450_gpios_2bit,
				ARRAY_SIZE(s5p6450_gpios_2bit), NULL, 0x0);
		samsung_gpiolib_add_4bit_chips(s5p6450_gpios_4bit,
				ARRAY_SIZE(s5p6450_gpios_4bit), S5P_VA_GPIO);
		samsung_gpiolib_add_4bit2_chips(s5p6450_gpios_4bit2,
				ARRAY_SIZE(s5p6450_gpios_4bit2));
		s5p64x0_gpiolib_add_rbank(s5p6450_gpios_rbank,
				ARRAY_SIZE(s5p6450_gpios_rbank));
	} else if (soc_is_s5pc100()) {
		group = 0;
		chip = s5pc100_gpios_4bit;
		nr_chips = ARRAY_SIZE(s5pc100_gpios_4bit);

		for (i = 0; i < nr_chips; i++, chip++) {
			if (!chip->config) {
				chip->config = &samsung_gpio_cfgs[3];
				chip->group = group++;
			}
		}
		samsung_gpiolib_add_4bit_chips(s5pc100_gpios_4bit, nr_chips, S5P_VA_GPIO);
#if defined(CONFIG_CPU_S5PC100) && defined(CONFIG_S5P_GPIO_INT)
		s5p_register_gpioint_bank(IRQ_GPIOINT, 0, S5P_GPIOINT_GROUP_MAXNR);
#endif
	} else if (soc_is_s5pv210()) {
		group = 0;
		chip = s5pv210_gpios_4bit;
		nr_chips = ARRAY_SIZE(s5pv210_gpios_4bit);

		for (i = 0; i < nr_chips; i++, chip++) {
			if (!chip->config) {
				chip->config = &samsung_gpio_cfgs[3];
				chip->group = group++;
			}
		}
		samsung_gpiolib_add_4bit_chips(s5pv210_gpios_4bit, nr_chips, S5P_VA_GPIO);
#if defined(CONFIG_CPU_S5PV210) && defined(CONFIG_S5P_GPIO_INT)
		s5p_register_gpioint_bank(IRQ_GPIOINT, 0, S5P_GPIOINT_GROUP_MAXNR);
#endif
	} else {
		WARN(1, "Unknown SoC in gpio-samsung, no GPIOs added\n");
		return -ENODEV;
	}

	return 0;
}
core_initcall(samsung_gpiolib_init);

int s3c_gpio_cfgpin(unsigned int pin, unsigned int config)
{
	struct samsung_gpio_chip *chip = samsung_gpiolib_getchip(pin);
	unsigned long flags;
	int offset;
	int ret;

	if (!chip)
		return -EINVAL;

	offset = pin - chip->chip.base;

	samsung_gpio_lock(chip, flags);
	ret = samsung_gpio_do_setcfg(chip, offset, config);
	samsung_gpio_unlock(chip, flags);

	return ret;
}
EXPORT_SYMBOL(s3c_gpio_cfgpin);

int s3c_gpio_cfgpin_range(unsigned int start, unsigned int nr,
			  unsigned int cfg)
{
	int ret;

	for (; nr > 0; nr--, start++) {
		ret = s3c_gpio_cfgpin(start, cfg);
		if (ret != 0)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(s3c_gpio_cfgpin_range);

int s3c_gpio_cfgall_range(unsigned int start, unsigned int nr,
			  unsigned int cfg, samsung_gpio_pull_t pull)
{
	int ret;

	for (; nr > 0; nr--, start++) {
		s3c_gpio_setpull(start, pull);
		ret = s3c_gpio_cfgpin(start, cfg);
		if (ret != 0)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(s3c_gpio_cfgall_range);

unsigned s3c_gpio_getcfg(unsigned int pin)
{
	struct samsung_gpio_chip *chip = samsung_gpiolib_getchip(pin);
	unsigned long flags;
	unsigned ret = 0;
	int offset;

	if (chip) {
		offset = pin - chip->chip.base;

		samsung_gpio_lock(chip, flags);
		ret = samsung_gpio_do_getcfg(chip, offset);
		samsung_gpio_unlock(chip, flags);
	}

	return ret;
}
EXPORT_SYMBOL(s3c_gpio_getcfg);

int s3c_gpio_setpull(unsigned int pin, samsung_gpio_pull_t pull)
{
	struct samsung_gpio_chip *chip = samsung_gpiolib_getchip(pin);
	unsigned long flags;
	int offset, ret;

	if (!chip)
		return -EINVAL;

	offset = pin - chip->chip.base;

	samsung_gpio_lock(chip, flags);
	ret = samsung_gpio_do_setpull(chip, offset, pull);
	samsung_gpio_unlock(chip, flags);

	return ret;
}
EXPORT_SYMBOL(s3c_gpio_setpull);

samsung_gpio_pull_t s3c_gpio_getpull(unsigned int pin)
{
	struct samsung_gpio_chip *chip = samsung_gpiolib_getchip(pin);
	unsigned long flags;
	int offset;
	u32 pup = 0;

	if (chip) {
		offset = pin - chip->chip.base;

		samsung_gpio_lock(chip, flags);
		pup = samsung_gpio_do_getpull(chip, offset);
		samsung_gpio_unlock(chip, flags);
	}

	return (__force samsung_gpio_pull_t)pup;
}
EXPORT_SYMBOL(s3c_gpio_getpull);

#ifdef CONFIG_S5P_GPIO_DRVSTR
s5p_gpio_drvstr_t s5p_gpio_get_drvstr(unsigned int pin)
{
	struct samsung_gpio_chip *chip = samsung_gpiolib_getchip(pin);
	unsigned int off;
	void __iomem *reg;
	int shift;
	u32 drvstr;

	if (!chip)
		return -EINVAL;

	off = pin - chip->chip.base;
	shift = off * 2;
	reg = chip->base + 0x0C;

	drvstr = __raw_readl(reg);
	drvstr = drvstr >> shift;
	drvstr &= 0x3;

	return (__force s5p_gpio_drvstr_t)drvstr;
}
EXPORT_SYMBOL(s5p_gpio_get_drvstr);

int s5p_gpio_set_drvstr(unsigned int pin, s5p_gpio_drvstr_t drvstr)
{
	struct samsung_gpio_chip *chip = samsung_gpiolib_getchip(pin);
	unsigned int off;
	void __iomem *reg;
	int shift;
	u32 tmp;

	if (!chip)
		return -EINVAL;

	off = pin - chip->chip.base;
	shift = off * 2;
	reg = chip->base + 0x0C;

	tmp = __raw_readl(reg);
	tmp &= ~(0x3 << shift);
	tmp |= drvstr << shift;

	__raw_writel(tmp, reg);

	return 0;
}
EXPORT_SYMBOL(s5p_gpio_set_drvstr);
#endif	/* CONFIG_S5P_GPIO_DRVSTR */

#ifdef CONFIG_PLAT_S3C24XX
unsigned int s3c2410_modify_misccr(unsigned int clear, unsigned int change)
{
	unsigned long flags;
	unsigned long misccr;

	local_irq_save(flags);
	misccr = __raw_readl(S3C24XX_MISCCR);
	misccr &= ~clear;
	misccr ^= change;
	__raw_writel(misccr, S3C24XX_MISCCR);
	local_irq_restore(flags);

	return misccr;
}
EXPORT_SYMBOL(s3c2410_modify_misccr);
#endif
