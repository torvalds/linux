/*
 * arch/arm/mach-pnx4008/gpio.c
 *
 * PNX4008 GPIO driver
 *
 * Author: Dmitry Chigirev <source@mvista.com>
 *
 * Based on reference code by Iwo Mergler and Z.Tabaaloute from Philips:
 * Copyright (c) 2005 Koninklijke Philips Electronics N.V.
 *
 * 2005 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <mach/hardware.h>
#include <mach/platform.h>
#include <mach/gpio-pnx4008.h>

/* register definitions */
#define PIO_VA_BASE	IO_ADDRESS(PNX4008_PIO_BASE)

#define PIO_INP_STATE	(0x00U)
#define PIO_OUTP_SET	(0x04U)
#define PIO_OUTP_CLR	(0x08U)
#define PIO_OUTP_STATE	(0x0CU)
#define PIO_DRV_SET	(0x10U)
#define PIO_DRV_CLR	(0x14U)
#define PIO_DRV_STATE	(0x18U)
#define PIO_SDINP_STATE	(0x1CU)
#define PIO_SDOUTP_SET	(0x20U)
#define PIO_SDOUTP_CLR	(0x24U)
#define PIO_MUX_SET	(0x28U)
#define PIO_MUX_CLR	(0x2CU)
#define PIO_MUX_STATE	(0x30U)

static inline void gpio_lock(void)
{
	local_irq_disable();
}

static inline void gpio_unlock(void)
{
	local_irq_enable();
}

/* Inline functions */
static inline int gpio_read_bit(u32 reg, int gpio)
{
	u32 bit, val;
	int ret = -EFAULT;

	if (gpio < 0)
		goto out;

	bit = GPIO_BIT(gpio);
	if (bit) {
		val = __raw_readl(PIO_VA_BASE + reg);
		ret = (val & bit) ? 1 : 0;
	}
out:
	return ret;
}

static inline int gpio_set_bit(u32 reg, int gpio)
{
	u32 bit, val;
	int ret = -EFAULT;

	if (gpio < 0)
		goto out;

	bit = GPIO_BIT(gpio);
	if (bit) {
		val = __raw_readl(PIO_VA_BASE + reg);
		val |= bit;
		__raw_writel(val, PIO_VA_BASE + reg);
		ret = 0;
	}
out:
	return ret;
}

/* Very simple access control, bitmap for allocated/free */
static unsigned long access_map[4];
#define INP_INDEX	0
#define OUTP_INDEX	1
#define GPIO_INDEX	2
#define MUX_INDEX	3

/*GPIO to Input Mapping */
static short gpio_to_inp_map[32] = {
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, 10, 11, 12, 13, 14, 24, -1
};

/*GPIO to Mux Mapping */
static short gpio_to_mux_map[32] = {
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, 0, 1, 4, 5, -1
};

/*Output to Mux Mapping */
static short outp_to_mux_map[32] = {
	-1, -1, -1, 6, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, 2, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1
};

int pnx4008_gpio_register_pin(unsigned short pin)
{
	unsigned long bit = GPIO_BIT(pin);
	int ret = -EBUSY;	/* Already in use */

	gpio_lock();

	if (GPIO_ISBID(pin)) {
		if (access_map[GPIO_INDEX] & bit)
			goto out;
		access_map[GPIO_INDEX] |= bit;

	} else if (GPIO_ISRAM(pin)) {
		if (access_map[GPIO_INDEX] & bit)
			goto out;
		access_map[GPIO_INDEX] |= bit;

	} else if (GPIO_ISMUX(pin)) {
		if (access_map[MUX_INDEX] & bit)
			goto out;
		access_map[MUX_INDEX] |= bit;

	} else if (GPIO_ISOUT(pin)) {
		if (access_map[OUTP_INDEX] & bit)
			goto out;
		access_map[OUTP_INDEX] |= bit;

	} else if (GPIO_ISIN(pin)) {
		if (access_map[INP_INDEX] & bit)
			goto out;
		access_map[INP_INDEX] |= bit;
	} else
		goto out;
	ret = 0;

out:
	gpio_unlock();
	return ret;
}

EXPORT_SYMBOL(pnx4008_gpio_register_pin);

int pnx4008_gpio_unregister_pin(unsigned short pin)
{
	unsigned long bit = GPIO_BIT(pin);
	int ret = -EFAULT;	/* Not registered */

	gpio_lock();

	if (GPIO_ISBID(pin)) {
		if (~access_map[GPIO_INDEX] & bit)
			goto out;
		access_map[GPIO_INDEX] &= ~bit;
	} else if (GPIO_ISRAM(pin)) {
		if (~access_map[GPIO_INDEX] & bit)
			goto out;
		access_map[GPIO_INDEX] &= ~bit;
	} else if (GPIO_ISMUX(pin)) {
		if (~access_map[MUX_INDEX] & bit)
			goto out;
		access_map[MUX_INDEX] &= ~bit;
	} else if (GPIO_ISOUT(pin)) {
		if (~access_map[OUTP_INDEX] & bit)
			goto out;
		access_map[OUTP_INDEX] &= ~bit;
	} else if (GPIO_ISIN(pin)) {
		if (~access_map[INP_INDEX] & bit)
			goto out;
		access_map[INP_INDEX] &= ~bit;
	} else
		goto out;
	ret = 0;

out:
	gpio_unlock();
	return ret;
}

EXPORT_SYMBOL(pnx4008_gpio_unregister_pin);

unsigned long pnx4008_gpio_read_pin(unsigned short pin)
{
	unsigned long ret = -EFAULT;
	int gpio = GPIO_BIT_MASK(pin);
	gpio_lock();
	if (GPIO_ISOUT(pin)) {
		ret = gpio_read_bit(PIO_OUTP_STATE, gpio);
	} else if (GPIO_ISRAM(pin)) {
		if (gpio_read_bit(PIO_DRV_STATE, gpio) == 0) {
			ret = gpio_read_bit(PIO_SDINP_STATE, gpio);
		}
	} else if (GPIO_ISBID(pin)) {
		ret = gpio_read_bit(PIO_DRV_STATE, gpio);
		if (ret > 0)
			ret = gpio_read_bit(PIO_OUTP_STATE, gpio);
		else if (ret == 0)
			ret =
			    gpio_read_bit(PIO_INP_STATE, gpio_to_inp_map[gpio]);
	} else if (GPIO_ISIN(pin)) {
		ret = gpio_read_bit(PIO_INP_STATE, gpio);
	}
	gpio_unlock();
	return ret;
}

EXPORT_SYMBOL(pnx4008_gpio_read_pin);

/* Write Value to output */
int pnx4008_gpio_write_pin(unsigned short pin, int output)
{
	int gpio = GPIO_BIT_MASK(pin);
	int ret = -EFAULT;

	gpio_lock();
	if (GPIO_ISOUT(pin)) {
		printk( "writing '%x' to '%x'\n",
				gpio, output ? PIO_OUTP_SET : PIO_OUTP_CLR );
		ret = gpio_set_bit(output ? PIO_OUTP_SET : PIO_OUTP_CLR, gpio);
	} else if (GPIO_ISRAM(pin)) {
		if (gpio_read_bit(PIO_DRV_STATE, gpio) > 0)
			ret = gpio_set_bit(output ? PIO_SDOUTP_SET :
					   PIO_SDOUTP_CLR, gpio);
	} else if (GPIO_ISBID(pin)) {
		if (gpio_read_bit(PIO_DRV_STATE, gpio) > 0)
			ret = gpio_set_bit(output ? PIO_OUTP_SET :
					   PIO_OUTP_CLR, gpio);
	}
	gpio_unlock();
	return ret;
}

EXPORT_SYMBOL(pnx4008_gpio_write_pin);

/* Value = 1 : Set GPIO pin as output */
/* Value = 0 : Set GPIO pin as input */
int pnx4008_gpio_set_pin_direction(unsigned short pin, int output)
{
	int gpio = GPIO_BIT_MASK(pin);
	int ret = -EFAULT;

	gpio_lock();
	if (GPIO_ISBID(pin) || GPIO_ISRAM(pin)) {
		ret = gpio_set_bit(output ? PIO_DRV_SET : PIO_DRV_CLR, gpio);
	}
	gpio_unlock();
	return ret;
}

EXPORT_SYMBOL(pnx4008_gpio_set_pin_direction);

/* Read GPIO pin direction: 0= pin used as input, 1= pin used as output*/
int pnx4008_gpio_read_pin_direction(unsigned short pin)
{
	int gpio = GPIO_BIT_MASK(pin);
	int ret = -EFAULT;

	gpio_lock();
	if (GPIO_ISBID(pin) || GPIO_ISRAM(pin)) {
		ret = gpio_read_bit(PIO_DRV_STATE, gpio);
	}
	gpio_unlock();
	return ret;
}

EXPORT_SYMBOL(pnx4008_gpio_read_pin_direction);

/* Value = 1 : Set pin to muxed function  */
/* Value = 0 : Set pin as GPIO */
int pnx4008_gpio_set_pin_mux(unsigned short pin, int output)
{
	int gpio = GPIO_BIT_MASK(pin);
	int ret = -EFAULT;

	gpio_lock();
	if (GPIO_ISBID(pin)) {
		ret =
		    gpio_set_bit(output ? PIO_MUX_SET : PIO_MUX_CLR,
				 gpio_to_mux_map[gpio]);
	} else if (GPIO_ISOUT(pin)) {
		ret =
		    gpio_set_bit(output ? PIO_MUX_SET : PIO_MUX_CLR,
				 outp_to_mux_map[gpio]);
	} else if (GPIO_ISMUX(pin)) {
		ret = gpio_set_bit(output ? PIO_MUX_SET : PIO_MUX_CLR, gpio);
	}
	gpio_unlock();
	return ret;
}

EXPORT_SYMBOL(pnx4008_gpio_set_pin_mux);

/* Read pin mux function: 0= pin used as GPIO, 1= pin used for muxed function*/
int pnx4008_gpio_read_pin_mux(unsigned short pin)
{
	int gpio = GPIO_BIT_MASK(pin);
	int ret = -EFAULT;

	gpio_lock();
	if (GPIO_ISBID(pin)) {
		ret = gpio_read_bit(PIO_MUX_STATE, gpio_to_mux_map[gpio]);
	} else if (GPIO_ISOUT(pin)) {
		ret = gpio_read_bit(PIO_MUX_STATE, outp_to_mux_map[gpio]);
	} else if (GPIO_ISMUX(pin)) {
		ret = gpio_read_bit(PIO_MUX_STATE, gpio);
	}
	gpio_unlock();
	return ret;
}

EXPORT_SYMBOL(pnx4008_gpio_read_pin_mux);
