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

#include <mach/hardware.h>
#include <asm/mach/map.h>
#include <mach/iomux-v1.h>

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
EXPORT_SYMBOL(mxc_gpio_mode);

static int imx_iomuxv1_setup_multiple(const int *list, unsigned count)
{
	size_t i;
	int ret;

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
	size_t i;
	int ret;

	for (i = 0; i < count; ++i) {
		unsigned gpio = pin_list[i] & (GPIO_PIN_MASK | GPIO_PORT_MASK);

		ret = gpio_request(gpio, label);
		if (ret)
			goto err_gpio_request;
	}

	ret = imx_iomuxv1_setup_multiple(pin_list, count);
	if (ret)
		goto err_setup;

	return 0;

err_setup:
	BUG_ON(i != count);

err_gpio_request:
	mxc_gpio_release_multiple_pins(pin_list, i);

	return ret;
}
EXPORT_SYMBOL(mxc_gpio_setup_multiple_pins);

void mxc_gpio_release_multiple_pins(const int *pin_list, int count)
{
	size_t i;

	for (i = 0; i < count; ++i) {
		unsigned gpio = pin_list[i] & (GPIO_PIN_MASK | GPIO_PORT_MASK);

		gpio_free(gpio);
	}
}
EXPORT_SYMBOL(mxc_gpio_release_multiple_pins);

static int imx_iomuxv1_init(void)
{
#ifdef CONFIG_ARCH_MX1
	if (cpu_is_mx1()) {
		imx_iomuxv1_baseaddr = MX1_IO_ADDRESS(MX1_GPIO_BASE_ADDR);
		imx_iomuxv1_numports = MX1_NUM_GPIO_PORT;
	} else
#endif
#ifdef CONFIG_MACH_MX21
	if (cpu_is_mx21()) {
		imx_iomuxv1_baseaddr = MX21_IO_ADDRESS(MX21_GPIO_BASE_ADDR);
		imx_iomuxv1_numports = MX21_NUM_GPIO_PORT;
	} else
#endif
#ifdef CONFIG_MACH_MX27
	if (cpu_is_mx27()) {
		imx_iomuxv1_baseaddr = MX27_IO_ADDRESS(MX27_GPIO_BASE_ADDR);
		imx_iomuxv1_numports = MX27_NUM_GPIO_PORT;
	} else
#endif
		return -ENODEV;

	return 0;
}
pure_initcall(imx_iomuxv1_init);
