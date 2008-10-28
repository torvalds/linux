/*
 *  arch/arm/mach-mxc/generic.c
 *
 *  author: Sascha Hauer
 *  Created: april 20th, 2004
 *  Copyright: Synertronixx GmbH
 *
 *  Common code for i.MX machines
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/gpio.h>

#include <mach/hardware.h>
#include <asm/mach/map.h>
#include <mach/iomux-mx1-mx2.h>

void mxc_gpio_mode(int gpio_mode)
{
	unsigned int pin = gpio_mode & GPIO_PIN_MASK;
	unsigned int port = (gpio_mode & GPIO_PORT_MASK) >> GPIO_PORT_SHIFT;
	unsigned int ocr = (gpio_mode & GPIO_OCR_MASK) >> GPIO_OCR_SHIFT;
	unsigned int tmp;

	/* Pullup enable */
	tmp = __raw_readl(VA_GPIO_BASE + MXC_PUEN(port));
	if (gpio_mode & GPIO_PUEN)
		tmp |= (1 << pin);
	else
		tmp &= ~(1 << pin);
	__raw_writel(tmp, VA_GPIO_BASE + MXC_PUEN(port));

	/* Data direction */
	tmp = __raw_readl(VA_GPIO_BASE + MXC_DDIR(port));
	if (gpio_mode & GPIO_OUT)
		tmp |= 1 << pin;
	else
		tmp &= ~(1 << pin);
	__raw_writel(tmp, VA_GPIO_BASE + MXC_DDIR(port));

	/* Primary / alternate function */
	tmp = __raw_readl(VA_GPIO_BASE + MXC_GPR(port));
	if (gpio_mode & GPIO_AF)
		tmp |= (1 << pin);
	else
		tmp &= ~(1 << pin);
	__raw_writel(tmp, VA_GPIO_BASE + MXC_GPR(port));

	/* use as gpio? */
	tmp = __raw_readl(VA_GPIO_BASE + MXC_GIUS(port));
	if (gpio_mode & (GPIO_PF | GPIO_AF))
		tmp &= ~(1 << pin);
	else
		tmp |= (1 << pin);
	__raw_writel(tmp, VA_GPIO_BASE + MXC_GIUS(port));

	if (pin < 16) {
		tmp = __raw_readl(VA_GPIO_BASE + MXC_OCR1(port));
		tmp &= ~(3 << (pin * 2));
		tmp |= (ocr << (pin * 2));
		__raw_writel(tmp, VA_GPIO_BASE + MXC_OCR1(port));

		tmp = __raw_readl(VA_GPIO_BASE + MXC_ICONFA1(port));
		tmp &= ~(3 << (pin * 2));
		tmp |= ((gpio_mode >> GPIO_AOUT_SHIFT) & 3) << (pin * 2);
		__raw_writel(tmp, VA_GPIO_BASE + MXC_ICONFA1(port));

		tmp = __raw_readl(VA_GPIO_BASE + MXC_ICONFB1(port));
		tmp &= ~(3 << (pin * 2));
		tmp |= ((gpio_mode >> GPIO_BOUT_SHIFT) & 3) << (pin * 2);
		__raw_writel(tmp, VA_GPIO_BASE + MXC_ICONFB1(port));
	} else {
		pin -= 16;

		tmp = __raw_readl(VA_GPIO_BASE + MXC_OCR2(port));
		tmp &= ~(3 << (pin * 2));
		tmp |= (ocr << (pin * 2));
		__raw_writel(tmp, VA_GPIO_BASE + MXC_OCR2(port));

		tmp = __raw_readl(VA_GPIO_BASE + MXC_ICONFA2(port));
		tmp &= ~(3 << (pin * 2));
		tmp |= ((gpio_mode >> GPIO_AOUT_SHIFT) & 3) << (pin * 2);
		__raw_writel(tmp, VA_GPIO_BASE + MXC_ICONFA2(port));

		tmp = __raw_readl(VA_GPIO_BASE + MXC_ICONFB2(port));
		tmp &= ~(3 << (pin * 2));
		tmp |= ((gpio_mode >> GPIO_BOUT_SHIFT) & 3) << (pin * 2);
		__raw_writel(tmp, VA_GPIO_BASE + MXC_ICONFB2(port));
	}
}
EXPORT_SYMBOL(mxc_gpio_mode);

int mxc_gpio_setup_multiple_pins(const int *pin_list, unsigned count,
				int alloc_mode, const char *label)
{
	const int *p = pin_list;
	int i;
	unsigned gpio;
	unsigned mode;

	for (i = 0; i < count; i++) {
		gpio = *p & (GPIO_PIN_MASK | GPIO_PORT_MASK);
		mode = *p & ~(GPIO_PIN_MASK | GPIO_PORT_MASK);

		if (gpio >= (GPIO_PORT_MAX + 1) * 32)
			goto setup_error;

		if (alloc_mode & MXC_GPIO_ALLOC_MODE_RELEASE)
			gpio_free(gpio);
		else if (!(alloc_mode & MXC_GPIO_ALLOC_MODE_NO_ALLOC))
			if (gpio_request(gpio, label)
			   && !(alloc_mode & MXC_GPIO_ALLOC_MODE_TRY_ALLOC))
				goto setup_error;

		if (!(alloc_mode & (MXC_GPIO_ALLOC_MODE_ALLOC_ONLY |
				    MXC_GPIO_ALLOC_MODE_RELEASE)))
			mxc_gpio_mode(gpio | mode);

		p++;
	}
	return 0;

setup_error:
	if (alloc_mode & (MXC_GPIO_ALLOC_MODE_NO_ALLOC |
	    MXC_GPIO_ALLOC_MODE_TRY_ALLOC))
		return -EINVAL;

	while (p != pin_list) {
		p--;
		gpio = *p & (GPIO_PIN_MASK | GPIO_PORT_MASK);
		gpio_free(gpio);
	}

	return -EINVAL;
}
EXPORT_SYMBOL(mxc_gpio_setup_multiple_pins);

