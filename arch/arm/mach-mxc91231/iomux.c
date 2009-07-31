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
#include <linux/kernel.h>
#include <mach/hardware.h>
#include <mach/gpio.h>
#include <mach/iomux-mxc91231.h>

/*
 * IOMUX register (base) addresses
 */
#define IOMUX_AP_BASE		MXC91231_IO_ADDRESS(MXC91231_IOMUX_AP_BASE_ADDR)
#define IOMUX_COM_BASE		MXC91231_IO_ADDRESS(MXC91231_IOMUX_COM_BASE_ADDR)
#define IOMUXSW_AP_MUX_CTL	(IOMUX_AP_BASE + 0x000)
#define IOMUXSW_SP_MUX_CTL	(IOMUX_COM_BASE + 0x000)
#define IOMUXSW_PAD_CTL		(IOMUX_COM_BASE + 0x200)

#define IOMUXINT_OBS1		(IOMUX_AP_BASE + 0x600)
#define IOMUXINT_OBS2		(IOMUX_AP_BASE + 0x004)

static DEFINE_SPINLOCK(gpio_mux_lock);

#define NB_PORTS			((PIN_MAX + 32) / 32)
#define PIN_GLOBAL_NUM(pin) \
	(((pin & MUX_SIDE_MASK) >> MUX_SIDE_SHIFT)*PIN_AP_MAX +	\
	 ((pin & MUX_REG_MASK) >> MUX_REG_SHIFT)*4 +		\
	 ((pin & MUX_FIELD_MASK) >> MUX_FIELD_SHIFT))

unsigned long mxc_pin_alloc_map[NB_PORTS * 32 / BITS_PER_LONG];
/*
 * set the mode for a IOMUX pin.
 */
int mxc_iomux_mode(const unsigned int pin_mode)
{
	u32 side, field, l, mode, ret = 0;
	void __iomem *reg;

	side = (pin_mode & MUX_SIDE_MASK) >> MUX_SIDE_SHIFT;
	switch (side) {
	case MUX_SIDE_AP:
		reg = IOMUXSW_AP_MUX_CTL;
		break;
	case MUX_SIDE_SP:
		reg = IOMUXSW_SP_MUX_CTL;
		break;
	default:
		return -EINVAL;
	}
	reg += ((pin_mode & MUX_REG_MASK) >> MUX_REG_SHIFT) * 4;
	field = (pin_mode & MUX_FIELD_MASK) >> MUX_FIELD_SHIFT;
	mode = (pin_mode & MUX_MODE_MASK) >> MUX_MODE_SHIFT;

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
	u32 padgrp, field, l;
	void __iomem *reg;

	padgrp = (pin & MUX_PADGRP_MASK) >> MUX_PADGRP_SHIFT;
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
 * allocs a single pin:
 * 	- reserves the pin so that it is not claimed by another driver
 * 	- setups the iomux according to the configuration
 */
int mxc_iomux_alloc_pin(const unsigned int pin_mode, const char *label)
{
	unsigned pad = PIN_GLOBAL_NUM(pin_mode);
	if (pad >= (PIN_MAX + 1)) {
		printk(KERN_ERR "mxc_iomux: Attempt to request nonexistant pin %u for \"%s\"\n",
			pad, label ? label : "?");
		return -EINVAL;
	}

	if (test_and_set_bit(pad, mxc_pin_alloc_map)) {
		printk(KERN_ERR "mxc_iomux: pin %u already used. Allocation for \"%s\" failed\n",
			pad, label ? label : "?");
		return -EBUSY;
	}
	mxc_iomux_mode(pin_mode);

	return 0;
}
EXPORT_SYMBOL(mxc_iomux_alloc_pin);

int mxc_iomux_setup_multiple_pins(unsigned int *pin_list, unsigned count,
		const char *label)
{
	unsigned int *p = pin_list;
	int i;
	int ret = -EINVAL;

	for (i = 0; i < count; i++) {
		ret = mxc_iomux_alloc_pin(*p, label);
		if (ret)
			goto setup_error;
		p++;
	}
	return 0;

setup_error:
	mxc_iomux_release_multiple_pins(pin_list, i);
	return ret;
}
EXPORT_SYMBOL(mxc_iomux_setup_multiple_pins);

void mxc_iomux_release_pin(const unsigned int pin_mode)
{
	unsigned pad = PIN_GLOBAL_NUM(pin_mode);

	if (pad < (PIN_MAX + 1))
		clear_bit(pad, mxc_pin_alloc_map);
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
