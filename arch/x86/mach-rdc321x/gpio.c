/*
 *  GPIO support for RDC SoC R3210/R8610
 *
 *  Copyright (C) 2007, Florian Fainelli <florian@openwrt.org>
 *  Copyright (C) 2008, Volker Weiss <dev@tintuc.de>
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */


#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/types.h>
#include <linux/module.h>

#include <asm/gpio.h>
#include <asm/mach-rdc321x/rdc321x_defs.h>


/* spin lock to protect our private copy of GPIO data register plus
   the access to PCI conf registers. */
static DEFINE_SPINLOCK(gpio_lock);

/* copy of GPIO data registers */
static u32 gpio_data_reg1;
static u32 gpio_data_reg2;

static u32 gpio_request_data[2];


static inline void rdc321x_conf_write(unsigned addr, u32 value)
{
	outl((1 << 31) | (7 << 11) | addr, RDC3210_CFGREG_ADDR);
	outl(value, RDC3210_CFGREG_DATA);
}

static inline void rdc321x_conf_or(unsigned addr, u32 value)
{
	outl((1 << 31) | (7 << 11) | addr, RDC3210_CFGREG_ADDR);
	value |= inl(RDC3210_CFGREG_DATA);
	outl(value, RDC3210_CFGREG_DATA);
}

static inline u32 rdc321x_conf_read(unsigned addr)
{
	outl((1 << 31) | (7 << 11) | addr, RDC3210_CFGREG_ADDR);

	return inl(RDC3210_CFGREG_DATA);
}

/* configure pin as GPIO */
static void rdc321x_configure_gpio(unsigned gpio)
{
	unsigned long flags;

	spin_lock_irqsave(&gpio_lock, flags);
	rdc321x_conf_or(gpio < 32
		? RDC321X_GPIO_CTRL_REG1 : RDC321X_GPIO_CTRL_REG2,
		1 << (gpio & 0x1f));
	spin_unlock_irqrestore(&gpio_lock, flags);
}

/* initially setup the 2 copies of the gpio data registers.
   This function must be called by the platform setup code. */
void __init rdc321x_gpio_setup()
{
	/* this might not be, what others (BIOS, bootloader, etc.)
	   wrote to these registers before, but it's a good guess. Still
	   better than just using 0xffffffff. */

	gpio_data_reg1 = rdc321x_conf_read(RDC321X_GPIO_DATA_REG1);
	gpio_data_reg2 = rdc321x_conf_read(RDC321X_GPIO_DATA_REG2);
}

/* determine, if gpio number is valid */
static inline int rdc321x_is_gpio(unsigned gpio)
{
	return gpio <= RDC321X_MAX_GPIO;
}

/* request GPIO */
int rdc_gpio_request(unsigned gpio, const char *label)
{
	unsigned long flags;

	if (!rdc321x_is_gpio(gpio))
		return -EINVAL;

	spin_lock_irqsave(&gpio_lock, flags);
	if (gpio_request_data[(gpio & 0x20) ? 1 : 0] & (1 << (gpio & 0x1f)))
		goto inuse;
	gpio_request_data[(gpio & 0x20) ? 1 : 0] |= (1 << (gpio & 0x1f));
	spin_unlock_irqrestore(&gpio_lock, flags);

	return 0;
inuse:
	spin_unlock_irqrestore(&gpio_lock, flags);
	return -EINVAL;
}
EXPORT_SYMBOL(rdc_gpio_request);

/* release previously-claimed GPIO */
void rdc_gpio_free(unsigned gpio)
{
	unsigned long flags;

	if (!rdc321x_is_gpio(gpio))
		return;

	spin_lock_irqsave(&gpio_lock, flags);
	gpio_request_data[(gpio & 0x20) ? 1 : 0] &= ~(1 << (gpio & 0x1f));
	spin_unlock_irqrestore(&gpio_lock, flags);
}
EXPORT_SYMBOL(rdc_gpio_free);

/* read GPIO pin */
int rdc_gpio_get_value(unsigned gpio)
{
	u32 reg;
	unsigned long flags;

	spin_lock_irqsave(&gpio_lock, flags);
	reg = rdc321x_conf_read(gpio < 32
		? RDC321X_GPIO_DATA_REG1 : RDC321X_GPIO_DATA_REG2);
	spin_unlock_irqrestore(&gpio_lock, flags);

	return (1 << (gpio & 0x1f)) & reg ? 1 : 0;
}
EXPORT_SYMBOL(rdc_gpio_get_value);

/* set GPIO pin to value */
void rdc_gpio_set_value(unsigned gpio, int value)
{
	unsigned long flags;
	u32 reg;

	reg = 1 << (gpio & 0x1f);
	if (gpio < 32) {
		spin_lock_irqsave(&gpio_lock, flags);
		if (value)
			gpio_data_reg1 |= reg;
		else
			gpio_data_reg1 &= ~reg;
		rdc321x_conf_write(RDC321X_GPIO_DATA_REG1, gpio_data_reg1);
		spin_unlock_irqrestore(&gpio_lock, flags);
	} else {
		spin_lock_irqsave(&gpio_lock, flags);
		if (value)
			gpio_data_reg2 |= reg;
		else
			gpio_data_reg2 &= ~reg;
		rdc321x_conf_write(RDC321X_GPIO_DATA_REG2, gpio_data_reg2);
		spin_unlock_irqrestore(&gpio_lock, flags);
	}
}
EXPORT_SYMBOL(rdc_gpio_set_value);

/* configure GPIO pin as input */
int rdc_gpio_direction_input(unsigned gpio)
{
	if (!rdc321x_is_gpio(gpio))
		return -EINVAL;

	rdc321x_configure_gpio(gpio);

	return 0;
}
EXPORT_SYMBOL(rdc_gpio_direction_input);

/* configure GPIO pin as output and set value */
int rdc_gpio_direction_output(unsigned gpio, int value)
{
	if (!rdc321x_is_gpio(gpio))
		return -EINVAL;

	gpio_set_value(gpio, value);
	rdc321x_configure_gpio(gpio);

	return 0;
}
EXPORT_SYMBOL(rdc_gpio_direction_output);
