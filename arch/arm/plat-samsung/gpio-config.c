/* linux/arch/arm/plat-s3c/gpio-config.c
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008-2010 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * S3C series GPIO configuration core
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/io.h>

#include <plat/gpio-core.h>
#include <plat/gpio-cfg.h>
#include <plat/gpio-cfg-helpers.h>

int s3c_gpio_cfgpin(unsigned int pin, unsigned int config)
{
	struct s3c_gpio_chip *chip = s3c_gpiolib_getchip(pin);
	unsigned long flags;
	int offset;
	int ret;

	if (!chip)
		return -EINVAL;

	offset = pin - chip->chip.base;

	s3c_gpio_lock(chip, flags);
	ret = s3c_gpio_do_setcfg(chip, offset, config);
	s3c_gpio_unlock(chip, flags);

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
			  unsigned int cfg, s3c_gpio_pull_t pull)
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
	struct s3c_gpio_chip *chip = s3c_gpiolib_getchip(pin);
	unsigned long flags;
	unsigned ret = 0;
	int offset;

	if (chip) {
		offset = pin - chip->chip.base;

		s3c_gpio_lock(chip, flags);
		ret = s3c_gpio_do_getcfg(chip, offset);
		s3c_gpio_unlock(chip, flags);
	}

	return ret;
}
EXPORT_SYMBOL(s3c_gpio_getcfg);


int s3c_gpio_setpull(unsigned int pin, s3c_gpio_pull_t pull)
{
	struct s3c_gpio_chip *chip = s3c_gpiolib_getchip(pin);
	unsigned long flags;
	int offset, ret;

	if (!chip)
		return -EINVAL;

	offset = pin - chip->chip.base;

	s3c_gpio_lock(chip, flags);
	ret = s3c_gpio_do_setpull(chip, offset, pull);
	s3c_gpio_unlock(chip, flags);

	return ret;
}
EXPORT_SYMBOL(s3c_gpio_setpull);

s3c_gpio_pull_t s3c_gpio_getpull(unsigned int pin)
{
	struct s3c_gpio_chip *chip = s3c_gpiolib_getchip(pin);
	unsigned long flags;
	int offset;
	u32 pup = 0;

	if (chip) {
		offset = pin - chip->chip.base;

		s3c_gpio_lock(chip, flags);
		pup = s3c_gpio_do_getpull(chip, offset);
		s3c_gpio_unlock(chip, flags);
	}

	return (__force s3c_gpio_pull_t)pup;
}
EXPORT_SYMBOL(s3c_gpio_getpull);

#ifdef CONFIG_S3C_GPIO_CFG_S3C24XX
int s3c_gpio_setcfg_s3c24xx_a(struct s3c_gpio_chip *chip,
			      unsigned int off, unsigned int cfg)
{
	void __iomem *reg = chip->base;
	unsigned int shift = off;
	u32 con;

	if (s3c_gpio_is_cfg_special(cfg)) {
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

unsigned s3c_gpio_getcfg_s3c24xx_a(struct s3c_gpio_chip *chip,
				   unsigned int off)
{
	u32 con;

	con = __raw_readl(chip->base);
	con >>= off;
	con &= 1;
	con++;

	return S3C_GPIO_SFN(con);
}

int s3c_gpio_setcfg_s3c24xx(struct s3c_gpio_chip *chip,
			    unsigned int off, unsigned int cfg)
{
	void __iomem *reg = chip->base;
	unsigned int shift = off * 2;
	u32 con;

	if (s3c_gpio_is_cfg_special(cfg)) {
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

unsigned int s3c_gpio_getcfg_s3c24xx(struct s3c_gpio_chip *chip,
				     unsigned int off)
{
	u32 con;

	con = __raw_readl(chip->base);
	con >>= off * 2;
	con &= 3;

	/* this conversion works for IN and OUT as well as special mode */
	return S3C_GPIO_SPECIAL(con);
}
#endif

#ifdef CONFIG_S3C_GPIO_CFG_S3C64XX
int s3c_gpio_setcfg_s3c64xx_4bit(struct s3c_gpio_chip *chip,
				 unsigned int off, unsigned int cfg)
{
	void __iomem *reg = chip->base;
	unsigned int shift = (off & 7) * 4;
	u32 con;

	if (off < 8 && chip->chip.ngpio > 8)
		reg -= 4;

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

unsigned s3c_gpio_getcfg_s3c64xx_4bit(struct s3c_gpio_chip *chip,
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

#endif /* CONFIG_S3C_GPIO_CFG_S3C64XX */

#ifdef CONFIG_S3C_GPIO_PULL_UPDOWN
int s3c_gpio_setpull_updown(struct s3c_gpio_chip *chip,
			    unsigned int off, s3c_gpio_pull_t pull)
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

s3c_gpio_pull_t s3c_gpio_getpull_updown(struct s3c_gpio_chip *chip,
					unsigned int off)
{
	void __iomem *reg = chip->base + 0x08;
	int shift = off * 2;
	u32 pup = __raw_readl(reg);

	pup >>= shift;
	pup &= 0x3;
	return (__force s3c_gpio_pull_t)pup;
}

#ifdef CONFIG_S3C_GPIO_PULL_S3C2443
int s3c_gpio_setpull_s3c2443(struct s3c_gpio_chip *chip,
				unsigned int off, s3c_gpio_pull_t pull)
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
	return s3c_gpio_setpull_updown(chip, off, pull);
}

s3c_gpio_pull_t s3c_gpio_getpull_s3c2443(struct s3c_gpio_chip *chip,
					unsigned int off)
{
	s3c_gpio_pull_t pull;

	pull = s3c_gpio_getpull_updown(chip, off);

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
#endif
#endif

#if defined(CONFIG_S3C_GPIO_PULL_UP) || defined(CONFIG_S3C_GPIO_PULL_DOWN)
static int s3c_gpio_setpull_1(struct s3c_gpio_chip *chip,
			 unsigned int off, s3c_gpio_pull_t pull,
			 s3c_gpio_pull_t updown)
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

static s3c_gpio_pull_t s3c_gpio_getpull_1(struct s3c_gpio_chip *chip,
				     unsigned int off, s3c_gpio_pull_t updown)
{
	void __iomem *reg = chip->base + 0x08;
	u32 pup = __raw_readl(reg);

	pup &= (1 << off);
	return pup ? S3C_GPIO_PULL_NONE : updown;
}
#endif /* CONFIG_S3C_GPIO_PULL_UP || CONFIG_S3C_GPIO_PULL_DOWN */

#ifdef CONFIG_S3C_GPIO_PULL_UP
s3c_gpio_pull_t s3c_gpio_getpull_1up(struct s3c_gpio_chip *chip,
				     unsigned int off)
{
	return s3c_gpio_getpull_1(chip, off, S3C_GPIO_PULL_UP);
}

int s3c_gpio_setpull_1up(struct s3c_gpio_chip *chip,
			 unsigned int off, s3c_gpio_pull_t pull)
{
	return s3c_gpio_setpull_1(chip, off, pull, S3C_GPIO_PULL_UP);
}
#endif /* CONFIG_S3C_GPIO_PULL_UP */

#ifdef CONFIG_S3C_GPIO_PULL_DOWN
s3c_gpio_pull_t s3c_gpio_getpull_1down(struct s3c_gpio_chip *chip,
				     unsigned int off)
{
	return s3c_gpio_getpull_1(chip, off, S3C_GPIO_PULL_DOWN);
}

int s3c_gpio_setpull_1down(struct s3c_gpio_chip *chip,
			 unsigned int off, s3c_gpio_pull_t pull)
{
	return s3c_gpio_setpull_1(chip, off, pull, S3C_GPIO_PULL_DOWN);
}
#endif /* CONFIG_S3C_GPIO_PULL_DOWN */

#ifdef CONFIG_S5P_GPIO_DRVSTR
s5p_gpio_drvstr_t s5p_gpio_get_drvstr(unsigned int pin)
{
	struct s3c_gpio_chip *chip = s3c_gpiolib_getchip(pin);
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
	struct s3c_gpio_chip *chip = s3c_gpiolib_getchip(pin);
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
