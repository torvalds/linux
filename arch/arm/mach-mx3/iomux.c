/*
 * Copyright 2004-2006 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2008 by Sascha Hauer <kernel@pengutronix.de>
 * Copyright (C) 2009 by Valentin Longchamp <valentin.longchamp@epfl.ch>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <mach/hardware.h>
#include <mach/gpio.h>
#include <mach/iomux-mx3.h>

/*
 * IOMUX register (base) addresses
 */
#define IOMUX_BASE	IO_ADDRESS(IOMUXC_BASE_ADDR)
#define IOMUXINT_OBS1	(IOMUX_BASE + 0x000)
#define IOMUXINT_OBS2	(IOMUX_BASE + 0x004)
#define IOMUXGPR	(IOMUX_BASE + 0x008)
#define IOMUXSW_MUX_CTL	(IOMUX_BASE + 0x00C)
#define IOMUXSW_PAD_CTL	(IOMUX_BASE + 0x154)

static DEFINE_SPINLOCK(gpio_mux_lock);

#define IOMUX_REG_MASK (IOMUX_PADNUM_MASK & ~0x3)

unsigned long mxc_pin_alloc_map[NB_PORTS * 32 / BITS_PER_LONG];
/*
 * set the mode for a IOMUX pin.
 */
int mxc_iomux_mode(unsigned int pin_mode)
{
	u32 field, l, mode, ret = 0;
	void __iomem *reg;

	reg = IOMUXSW_MUX_CTL + (pin_mode & IOMUX_REG_MASK);
	field = pin_mode & 0x3;
	mode = (pin_mode & IOMUX_MODE_MASK) >> IOMUX_MODE_SHIFT;

	spin_lock(&gpio_mux_lock);

	l = __raw_readl(reg);
	l &= ~(0xff << (field * 8));
	l |= mode << (field * 8);
	__raw_writel(l, reg);

	spin_unlock(&gpio_mux_lock);

	return ret;
}
EXPORT_SYMBOL(mxc_iomux_mode);

/*
 * This function configures the pad value for a IOMUX pin.
 */
void mxc_iomux_set_pad(enum iomux_pins pin, u32 config)
{
	u32 field, l;
	void __iomem *reg;

	pin &= IOMUX_PADNUM_MASK;
	reg = IOMUXSW_PAD_CTL + (pin + 2) / 3 * 4;
	field = (pin + 2) % 3;

	pr_debug("%s: reg offset = 0x%x, field = %d\n",
			__func__, (pin + 2) / 3, field);

	spin_lock(&gpio_mux_lock);

	l = __raw_readl(reg);
	l &= ~(0x1ff << (field * 10));
	l |= config << (field * 10);
	__raw_writel(l, reg);

	spin_unlock(&gpio_mux_lock);
}
EXPORT_SYMBOL(mxc_iomux_set_pad);

/*
 * setups a single pin:
 * 	- reserves the pin so that it is not claimed by another driver
 * 	- setups the iomux according to the configuration
 * 	- if the pin is configured as a GPIO, we claim it through kernel gpiolib
 */
int mxc_iomux_setup_pin(const unsigned int pin, const char *label)
{
	unsigned pad = pin & IOMUX_PADNUM_MASK;
	unsigned gpio;

	if (pad >= (PIN_MAX + 1)) {
		printk(KERN_ERR "mxc_iomux: Attempt to request nonexistant pin %u for \"%s\"\n",
			pad, label ? label : "?");
		return -EINVAL;
	}

	if (test_and_set_bit(pad, mxc_pin_alloc_map)) {
		printk(KERN_ERR "mxc_iomux: pin %u already used. Allocation for \"%s\" failed\n",
			pad, label ? label : "?");
		return -EINVAL;
	}
	mxc_iomux_mode(pin);

	/* if we have a gpio, we can allocate it */
	gpio = (pin & IOMUX_GPIONUM_MASK) >> IOMUX_GPIONUM_SHIFT;
	if (gpio < (GPIO_PORT_MAX + 1) * 32)
		if (gpio_request(gpio, label))
			return -EINVAL;

	return 0;
}
EXPORT_SYMBOL(mxc_iomux_setup_pin);

int mxc_iomux_setup_multiple_pins(unsigned int *pin_list, unsigned count,
		const char *label)
{
	unsigned int *p = pin_list;
	int i;
	int ret = -EINVAL;

	for (i = 0; i < count; i++) {
		if (mxc_iomux_setup_pin(*p, label))
			goto setup_error;
		p++;
	}
	return 0;

setup_error:
	mxc_iomux_release_multiple_pins(pin_list, i);
	return ret;
}
EXPORT_SYMBOL(mxc_iomux_setup_multiple_pins);

void mxc_iomux_release_pin(const unsigned int pin)
{
	unsigned pad = pin & IOMUX_PADNUM_MASK;
	unsigned gpio;

	if (pad < (PIN_MAX + 1))
		clear_bit(pad, mxc_pin_alloc_map);

	gpio = (pin & IOMUX_GPIONUM_MASK) >> IOMUX_GPIONUM_SHIFT;
	if (gpio < (GPIO_PORT_MAX + 1) * 32)
		gpio_free(gpio);
}
EXPORT_SYMBOL(mxc_iomux_release_pin);

void mxc_iomux_release_multiple_pins(unsigned int *pin_list, int count)
{
	unsigned int *p = pin_list;
	int i;

	for (i = 0; i < count; i++) {
		mxc_iomux_release_pin(*p);
		p++;
	}
}
EXPORT_SYMBOL(mxc_iomux_release_multiple_pins);

/*
 * This function enables/disables the general purpose function for a particular
 * signal.
 */
void mxc_iomux_set_gpr(enum iomux_gp_func gp, bool en)
{
	u32 l;

	spin_lock(&gpio_mux_lock);
	l = __raw_readl(IOMUXGPR);
	if (en)
		l |= gp;
	else
		l &= ~gp;

	__raw_writel(l, IOMUXGPR);
	spin_unlock(&gpio_mux_lock);
}
EXPORT_SYMBOL(mxc_iomux_set_gpr);
