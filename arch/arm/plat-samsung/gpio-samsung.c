// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2009-2011 Samsung Electronics Co., Ltd.
//		http://www.samsung.com/
//
// Copyright 2008 Openmoko, Inc.
// Copyright 2008 Simtec Electronics
//      Ben Dooks <ben@simtec.co.uk>
//      http://armlinux.simtec.co.uk/
//
// SAMSUNG - GPIOlib support

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

#include <mach/irqs.h>
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
	ret = gpiochip_add_data(gc, chip);
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

int samsung_gpiolib_to_irq(struct gpio_chip *chip, unsigned int offset)
{
	struct samsung_gpio_chip *samsung_chip = gpiochip_get_data(chip);

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

/* TODO: cleanup soc_is_* */
static __init int samsung_gpiolib_init(void)
{
	/*
	 * Currently there are two drivers that can provide GPIO support for
	 * Samsung SoCs. For device tree enabled platforms, the new
	 * pinctrl-samsung driver is used, providing both GPIO and pin control
	 * interfaces. For legacy (non-DT) platforms this driver is used.
	 */
	if (of_have_populated_dt())
		return 0;

	if (soc_is_s3c24xx()) {
		samsung_gpiolib_set_cfg(samsung_gpio_cfgs,
				ARRAY_SIZE(samsung_gpio_cfgs));
		s3c24xx_gpiolib_add_chips(s3c24xx_gpios,
				ARRAY_SIZE(s3c24xx_gpios), S3C24XX_VA_GPIO);
	} else if (soc_is_s3c64xx()) {
		samsung_gpiolib_set_cfg(samsung_gpio_cfgs,
				ARRAY_SIZE(samsung_gpio_cfgs));
		samsung_gpiolib_add_2bit_chips(s3c64xx_gpios_2bit,
				ARRAY_SIZE(s3c64xx_gpios_2bit),
				S3C64XX_VA_GPIO + 0xE0, 0x20);
		samsung_gpiolib_add_4bit_chips(s3c64xx_gpios_4bit,
				ARRAY_SIZE(s3c64xx_gpios_4bit),
				S3C64XX_VA_GPIO);
		samsung_gpiolib_add_4bit2_chips(s3c64xx_gpios_4bit2,
				ARRAY_SIZE(s3c64xx_gpios_4bit2));
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
