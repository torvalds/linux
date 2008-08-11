#ifndef _ASM_SH_HD64465_GPIO_
#define _ASM_SH_HD64465_GPIO_ 1
/*
 * $Id: gpio.h,v 1.3 2003/05/04 19:30:14 lethal Exp $
 *
 * Hitachi HD64465 companion chip: General Purpose IO pins support.
 * This layer enables other device drivers to configure GPIO
 * pins, get and set their values, and register an interrupt
 * routine for when input pins change in hardware.
 *
 * by Greg Banks <gbanks@pocketpenguins.com>
 * (c) 2000 PocketPenguins Inc.
 */
#include <asm/hd64465.h>

/* Macro to construct a portpin number (used in all
 * subsequent functions) from a port letter and a pin
 * number, e.g. HD64465_GPIO_PORTPIN('A', 5).
 */
#define HD64465_GPIO_PORTPIN(port,pin)	(((port)-'A')<<3|(pin))

/* Pin configuration constants for _configure() */
#define HD64465_GPIO_FUNCTION2	0	/* use the pin's *other* function */
#define HD64465_GPIO_OUT	1	/* output */
#define HD64465_GPIO_IN_PULLUP	2	/* input, pull-up MOS on */
#define HD64465_GPIO_IN		3	/* input */

/* Configure a pin's direction */
extern void hd64465_gpio_configure(int portpin, int direction);

/* Get, set value */
extern void hd64465_gpio_set_pin(int portpin, unsigned int value);
extern unsigned int hd64465_gpio_get_pin(int portpin);
extern void hd64465_gpio_set_port(int port, unsigned int value);
extern unsigned int hd64465_gpio_get_port(int port);

/* mode constants for _register_irq() */
#define HD64465_GPIO_FALLING	0
#define HD64465_GPIO_RISING	1

/* Interrupt on external value change */
extern void hd64465_gpio_register_irq(int portpin, int mode,
	void (*handler)(int portpin, void *dev), void *dev);
extern void hd64465_gpio_unregister_irq(int portpin);

#endif /* _ASM_SH_HD64465_GPIO_  */
