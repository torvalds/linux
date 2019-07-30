/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  gpio.h: GPIO Support for PNX833X.
 *
 *  Copyright 2008 NXP Semiconductors
 *	  Chris Steel <chris.steel@nxp.com>
 *    Daniel Laird <daniel.j.laird@nxp.com>
 */
#ifndef __ASM_MIPS_MACH_PNX833X_GPIO_H
#define __ASM_MIPS_MACH_PNX833X_GPIO_H

/* BIG FAT WARNING: races danger!
   No protections exist here. Current users are only early init code,
   when locking is not needed because no concurrency yet exists there,
   and GPIO IRQ dispatcher, which does locking.
   However, if many uses will ever happen, proper locking will be needed
   - including locking between different uses
*/

#include <asm/mach-pnx833x/pnx833x.h>

#define SET_REG_BIT(reg, bit)		do { (reg |= (1 << (bit))); } while (0)
#define CLEAR_REG_BIT(reg, bit)		do { (reg &= ~(1 << (bit))); } while (0)

/* Initialize GPIO to a known state */
static inline void pnx833x_gpio_init(void)
{
	PNX833X_PIO_DIR = 0;
	PNX833X_PIO_DIR2 = 0;
	PNX833X_PIO_SEL = 0;
	PNX833X_PIO_SEL2 = 0;
	PNX833X_PIO_INT_EDGE = 0;
	PNX833X_PIO_INT_HI = 0;
	PNX833X_PIO_INT_LO = 0;

	/* clear any GPIO interrupt requests */
	PNX833X_PIO_INT_CLEAR = 0xffff;
	PNX833X_PIO_INT_CLEAR = 0;
	PNX833X_PIO_INT_ENABLE = 0;
}

/* Select GPIO direction for a pin */
static inline void pnx833x_gpio_select_input(unsigned int pin)
{
	if (pin < 32)
		CLEAR_REG_BIT(PNX833X_PIO_DIR, pin);
	else
		CLEAR_REG_BIT(PNX833X_PIO_DIR2, pin & 31);
}
static inline void pnx833x_gpio_select_output(unsigned int pin)
{
	if (pin < 32)
		SET_REG_BIT(PNX833X_PIO_DIR, pin);
	else
		SET_REG_BIT(PNX833X_PIO_DIR2, pin & 31);
}

/* Select GPIO or alternate function for a pin */
static inline void pnx833x_gpio_select_function_io(unsigned int pin)
{
	if (pin < 32)
		CLEAR_REG_BIT(PNX833X_PIO_SEL, pin);
	else
		CLEAR_REG_BIT(PNX833X_PIO_SEL2, pin & 31);
}
static inline void pnx833x_gpio_select_function_alt(unsigned int pin)
{
	if (pin < 32)
		SET_REG_BIT(PNX833X_PIO_SEL, pin);
	else
		SET_REG_BIT(PNX833X_PIO_SEL2, pin & 31);
}

/* Read GPIO pin */
static inline int pnx833x_gpio_read(unsigned int pin)
{
	if (pin < 32)
		return (PNX833X_PIO_IN >> pin) & 1;
	else
		return (PNX833X_PIO_IN2 >> (pin & 31)) & 1;
}

/* Write GPIO pin */
static inline void pnx833x_gpio_write(unsigned int val, unsigned int pin)
{
	if (pin < 32) {
		if (val)
			SET_REG_BIT(PNX833X_PIO_OUT, pin);
		else
			CLEAR_REG_BIT(PNX833X_PIO_OUT, pin);
	} else {
		if (val)
			SET_REG_BIT(PNX833X_PIO_OUT2, pin & 31);
		else
			CLEAR_REG_BIT(PNX833X_PIO_OUT2, pin & 31);
	}
}

/* Configure GPIO interrupt */
#define GPIO_INT_NONE		0
#define GPIO_INT_LEVEL_LOW	1
#define GPIO_INT_LEVEL_HIGH	2
#define GPIO_INT_EDGE_RISING	3
#define GPIO_INT_EDGE_FALLING	4
#define GPIO_INT_EDGE_BOTH	5
static inline void pnx833x_gpio_setup_irq(int when, unsigned int pin)
{
	switch (when) {
	case GPIO_INT_LEVEL_LOW:
		CLEAR_REG_BIT(PNX833X_PIO_INT_EDGE, pin);
		CLEAR_REG_BIT(PNX833X_PIO_INT_HI, pin);
		SET_REG_BIT(PNX833X_PIO_INT_LO, pin);
		break;
	case GPIO_INT_LEVEL_HIGH:
		CLEAR_REG_BIT(PNX833X_PIO_INT_EDGE, pin);
		SET_REG_BIT(PNX833X_PIO_INT_HI, pin);
		CLEAR_REG_BIT(PNX833X_PIO_INT_LO, pin);
		break;
	case GPIO_INT_EDGE_RISING:
		SET_REG_BIT(PNX833X_PIO_INT_EDGE, pin);
		SET_REG_BIT(PNX833X_PIO_INT_HI, pin);
		CLEAR_REG_BIT(PNX833X_PIO_INT_LO, pin);
		break;
	case GPIO_INT_EDGE_FALLING:
		SET_REG_BIT(PNX833X_PIO_INT_EDGE, pin);
		CLEAR_REG_BIT(PNX833X_PIO_INT_HI, pin);
		SET_REG_BIT(PNX833X_PIO_INT_LO, pin);
		break;
	case GPIO_INT_EDGE_BOTH:
		SET_REG_BIT(PNX833X_PIO_INT_EDGE, pin);
		SET_REG_BIT(PNX833X_PIO_INT_HI, pin);
		SET_REG_BIT(PNX833X_PIO_INT_LO, pin);
		break;
	default:
		CLEAR_REG_BIT(PNX833X_PIO_INT_EDGE, pin);
		CLEAR_REG_BIT(PNX833X_PIO_INT_HI, pin);
		CLEAR_REG_BIT(PNX833X_PIO_INT_LO, pin);
		break;
	}
}

/* Enable/disable GPIO interrupt */
static inline void pnx833x_gpio_enable_irq(unsigned int pin)
{
	SET_REG_BIT(PNX833X_PIO_INT_ENABLE, pin);
}
static inline void pnx833x_gpio_disable_irq(unsigned int pin)
{
	CLEAR_REG_BIT(PNX833X_PIO_INT_ENABLE, pin);
}

/* Clear GPIO interrupt request */
static inline void pnx833x_gpio_clear_irq(unsigned int pin)
{
	SET_REG_BIT(PNX833X_PIO_INT_CLEAR, pin);
	CLEAR_REG_BIT(PNX833X_PIO_INT_CLEAR, pin);
}

#endif
