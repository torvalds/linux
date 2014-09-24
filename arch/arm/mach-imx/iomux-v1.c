/*
 * arch/arm/plat-mxc/iomux-v1.c
 *
 * Copyright (C) 2004 Sascha Hauer, Synertronixx GmbH
 * Copyright (C) 2009 Uwe Kleine-Koenig, Pengutronix
 *
 * Common code for i.MX1, i.MX21 and i.MX27
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/gpio.h>

#include <asm/mach/map.h>

#include "hardware.h"
#include "iomux-v1.h"

static void __iomem *imx_iomuxv1_baseaddr;
static unsigned imx_iomuxv1_numports;

static inline unsigned long imx_iomuxv1_readl(unsigned offset)
{
	return __raw_readl(imx_iomuxv1_baseaddr + offset);
}

static inline void imx_iomuxv1_writel(unsigned long val, unsigned offset)
{
	__raw_writel(val, imx_iomuxv1_baseaddr + offset);
}

static inline void imx_iomuxv1_rmwl(unsigned offset,
		unsigned long mask, unsigned long value)
{
	unsigned long reg = imx_iomuxv1_readl(offset);

	reg &= ~mask;
	reg |= value;

	imx_iomuxv1_writel(reg, offset);
}

static inline void imx_iomuxv1_set_puen(
		unsigned int port, unsigned int pin, int on)
{
	unsigned long mask = 1 << pin;

	imx_iomuxv1_rmwl(MXC_PUEN(port), mask, on ? mask : 0);
}

static inline void imx_iomuxv1_set_ddir(
		unsigned int port, unsigned int pin, int out)
{
	unsigned long mask = 1 << pin;

	imx_iomuxv1_rmwl(MXC_DDIR(port), mask, out ? mask : 0);
}

static inline void imx_iomuxv1_set_gpr(
		unsigned int port, unsigned int pin, int af)
{
	unsigned long mask = 1 << pin;

	imx_iomuxv1_rmwl(MXC_GPR(port), mask, af ? mask : 0);
}

static inline void imx_iomuxv1_set_gius(
		unsigned int port, unsigned int pin, int inuse)
{
	unsigned long mask = 1 << pin;

	imx_iomuxv1_rmwl(MXC_GIUS(port), mask, inuse ? mask : 0);
}

static inline void imx_iomuxv1_set_ocr(
		unsigned int port, unsigned int pin, unsigned int ocr)
{
	unsigned long shift = (pin & 0xf) << 1;
	unsigned long mask = 3 << shift;
	unsigned long value = ocr << shift;
	unsigned long offset = pin < 16 ? MXC_OCR1(port) : MXC_OCR2(port);

	imx_iomuxv1_rmwl(offset, mask, value);
}

static inline void imx_iomuxv1_set_iconfa(
		unsigned int port, unsigned int pin, unsigned int aout)
{
	unsigned long shift = (pin & 0xf) << 1;
	unsigned long mask = 3 << shift;
	unsigned long value = aout << shift;
	unsigned long offset = pin < 16 ? MXC_ICONFA1(port) : MXC_ICONFA2(port);

	imx_iomuxv1_rmwl(offset, mask, value);
}

static inline void imx_iomuxv1_set_iconfb(
		unsigned int port, unsigned int pin, unsigned int bout)
{
	unsigned long shift = (pin & 0xf) << 1;
	unsigned long mask = 3 << shift;
	unsigned long value = bout << shift;
	unsigned long offset = pin < 16 ? MXC_ICONFB1(port) : MXC_ICONFB2(port);

	imx_iomuxv1_rmwl(offset, mask, value);
}

int mxc_gpio_mode(int gpio_mode)
{
	unsigned int pin = gpio_mode & GPIO_PIN_MASK;
	unsigned int port = (gpio_mode & GPIO_PORT_MASK) >> GPIO_PORT_SHIFT;
	unsigned int ocr = (gpio_mode & GPIO_OCR_MASK) >> GPIO_OCR_SHIFT;
	unsigned int aout = (gpio_mode >> GPIO_AOUT_SHIFT) & 3;
	unsigned int bout = (gpio_mode >> GPIO_BOUT_SHIFT) & 3;

	if (port >= imx_iomuxv1_numports)
		return -EINVAL;

	/* Pullup enable */
	imx_iomuxv1_set_puen(port, pin, gpio_mode & GPIO_PUEN);

	/* Data direction */
	imx_iomuxv1_set_ddir(port, pin, gpio_mode & GPIO_OUT);

	/* Primary / alternate function */
	imx_iomuxv1_set_gpr(port, pin, gpio_mode & GPIO_AF);

	/* use as gpio? */
	imx_iomuxv1_set_gius(port, pin, !(gpio_mode & (GPIO_PF | GPIO_AF)));

	imx_iomuxv1_set_ocr(port, pin, ocr);

	imx_iomuxv1_set_iconfa(port, pin, aout);

	imx_iomuxv1_set_iconfb(port, pin, bout);

	return 0;
}

static int imx_iomuxv1_setup_multiple(const int *list, unsigned count)
{
	size_t i;
	int ret = 0;

	for (i = 0; i < count; ++i) {
		ret = mxc_gpio_mode(list[i]);

		if (ret)
			return ret;
	}

	return ret;
}

int mxc_gpio_setup_multiple_pins(const int *pin_list, unsigned count,
		const char *label)
{
	int ret;

	ret = imx_iomuxv1_setup_multiple(pin_list, count);
	return ret;
}

int __init imx_iomuxv1_init(void __iomem *base, int numports)
{
	imx_iomuxv1_baseaddr = base;
	imx_iomuxv1_numports = numports;

	return 0;
}
