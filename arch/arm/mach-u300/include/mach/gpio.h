/*
 *
 * arch/arm/mach-u300/include/mach/gpio.h
 *
 *
 * Copyright (C) 2007-2009 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 * GPIO block resgister definitions and inline macros for
 * U300 GPIO COH 901 335 or COH 901 571/3
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 */

#ifndef __MACH_U300_GPIO_H
#define __MACH_U300_GPIO_H

#include <linux/kernel.h>
#include <linux/io.h>
#include <mach/hardware.h>
#include <asm/irq.h>

/* Switch type depending on platform/chip variant */
#if defined(CONFIG_MACH_U300_BS2X) || defined(CONFIG_MACH_U300_BS330)
#define U300_COH901335
#endif
#if defined(CONFIG_MACH_U300_BS365) || defined(CONFIG_MACH_U300_BS335)
#define U300_COH901571_3
#endif

/* Get base address for regs here */
#include "u300-regs.h"
/* IRQ numbers */
#include "irqs.h"

/*
 * This is the GPIO block definitions. GPIO (General Purpose I/O) can be
 * used for anything, and often is. The event/enable etc figures are for
 * the lowermost pin (pin 0 on each port), shift this left to match your
 * pin if you're gonna use these values.
 */
#ifdef U300_COH901335
#define U300_GPIO_PORTX_SPACING				(0x1C)
/* Port X Pin Data Register 32bit, this is both input and output (R/W) */
#define U300_GPIO_PXPDIR				(0x00)
#define U300_GPIO_PXPDOR				(0x00)
/* Port X Pin Config Register 32bit (R/W) */
#define U300_GPIO_PXPCR					(0x04)
#define U300_GPIO_PXPCR_ALL_PINS_MODE_MASK		(0x0000FFFFUL)
#define U300_GPIO_PXPCR_PIN_MODE_MASK			(0x00000003UL)
#define U300_GPIO_PXPCR_PIN_MODE_SHIFT			(0x00000002UL)
#define U300_GPIO_PXPCR_PIN_MODE_INPUT			(0x00000000UL)
#define U300_GPIO_PXPCR_PIN_MODE_OUTPUT_PUSH_PULL	(0x00000001UL)
#define U300_GPIO_PXPCR_PIN_MODE_OUTPUT_OPEN_DRAIN	(0x00000002UL)
#define U300_GPIO_PXPCR_PIN_MODE_OUTPUT_OPEN_SOURCE	(0x00000003UL)
/* Port X Interrupt Event Register 32bit (R/W) */
#define U300_GPIO_PXIEV					(0x08)
#define U300_GPIO_PXIEV_ALL_IRQ_EVENT_MASK		(0x000000FFUL)
#define U300_GPIO_PXIEV_IRQ_EVENT			(0x00000001UL)
/* Port X Interrupt Enable Register 32bit (R/W) */
#define U300_GPIO_PXIEN					(0x0C)
#define U300_GPIO_PXIEN_ALL_IRQ_ENABLE_MASK		(0x000000FFUL)
#define U300_GPIO_PXIEN_IRQ_ENABLE			(0x00000001UL)
/* Port X Interrupt Force Register 32bit (R/W) */
#define U300_GPIO_PXIFR					(0x10)
#define U300_GPIO_PXIFR_ALL_IRQ_FORCE_MASK		(0x000000FFUL)
#define U300_GPIO_PXIFR_IRQ_FORCE			(0x00000001UL)
/* Port X Interrupt Config Register 32bit (R/W) */
#define U300_GPIO_PXICR					(0x14)
#define U300_GPIO_PXICR_ALL_IRQ_CONFIG_MASK		(0x000000FFUL)
#define U300_GPIO_PXICR_IRQ_CONFIG_MASK			(0x00000001UL)
#define U300_GPIO_PXICR_IRQ_CONFIG_FALLING_EDGE		(0x00000000UL)
#define U300_GPIO_PXICR_IRQ_CONFIG_RISING_EDGE		(0x00000001UL)
/* Port X Pull-up Enable Register 32bit (R/W) */
#define U300_GPIO_PXPER					(0x18)
#define U300_GPIO_PXPER_ALL_PULL_UP_DISABLE_MASK	(0x000000FFUL)
#define U300_GPIO_PXPER_PULL_UP_DISABLE			(0x00000001UL)
/* Control Register 32bit (R/W) */
#define U300_GPIO_CR					(0x54)
#define U300_GPIO_CR_BLOCK_CLOCK_ENABLE			(0x00000001UL)
/* three ports of 8 bits each = GPIO pins 0..23 */
#define U300_GPIO_NUM_PORTS 3
#define U300_GPIO_PINS_PER_PORT 8
#define U300_GPIO_MAX	(U300_GPIO_PINS_PER_PORT * U300_GPIO_NUM_PORTS - 1)
#endif

#ifdef U300_COH901571_3
/*
 * Control Register 32bit (R/W)
 * bit 15-9 (mask 0x0000FE00) contains the number of cores. 8*cores
 * gives the number of GPIO pins.
 * bit 8-2  (mask 0x000001FC) contains the core version ID.
 */
#define U300_GPIO_CR					(0x00)
#define U300_GPIO_CR_SYNC_SEL_ENABLE			(0x00000002UL)
#define U300_GPIO_CR_BLOCK_CLKRQ_ENABLE			(0x00000001UL)
#define U300_GPIO_PORTX_SPACING				(0x30)
/* Port X Pin Data INPUT Register 32bit (R/W) */
#define U300_GPIO_PXPDIR				(0x04)
/* Port X Pin Data OUTPUT Register 32bit (R/W) */
#define U300_GPIO_PXPDOR				(0x08)
/* Port X Pin Config Register 32bit (R/W) */
#define U300_GPIO_PXPCR					(0x0C)
#define U300_GPIO_PXPCR_ALL_PINS_MODE_MASK		(0x0000FFFFUL)
#define U300_GPIO_PXPCR_PIN_MODE_MASK			(0x00000003UL)
#define U300_GPIO_PXPCR_PIN_MODE_SHIFT			(0x00000002UL)
#define U300_GPIO_PXPCR_PIN_MODE_INPUT			(0x00000000UL)
#define U300_GPIO_PXPCR_PIN_MODE_OUTPUT_PUSH_PULL	(0x00000001UL)
#define U300_GPIO_PXPCR_PIN_MODE_OUTPUT_OPEN_DRAIN	(0x00000002UL)
#define U300_GPIO_PXPCR_PIN_MODE_OUTPUT_OPEN_SOURCE	(0x00000003UL)
/* Port X Pull-up Enable Register 32bit (R/W) */
#define U300_GPIO_PXPER					(0x10)
#define U300_GPIO_PXPER_ALL_PULL_UP_DISABLE_MASK	(0x000000FFUL)
#define U300_GPIO_PXPER_PULL_UP_DISABLE			(0x00000001UL)
/* Port X Interrupt Event Register 32bit (R/W) */
#define U300_GPIO_PXIEV					(0x14)
#define U300_GPIO_PXIEV_ALL_IRQ_EVENT_MASK		(0x000000FFUL)
#define U300_GPIO_PXIEV_IRQ_EVENT			(0x00000001UL)
/* Port X Interrupt Enable Register 32bit (R/W) */
#define U300_GPIO_PXIEN					(0x18)
#define U300_GPIO_PXIEN_ALL_IRQ_ENABLE_MASK		(0x000000FFUL)
#define U300_GPIO_PXIEN_IRQ_ENABLE			(0x00000001UL)
/* Port X Interrupt Force Register 32bit (R/W) */
#define U300_GPIO_PXIFR					(0x1C)
#define U300_GPIO_PXIFR_ALL_IRQ_FORCE_MASK		(0x000000FFUL)
#define U300_GPIO_PXIFR_IRQ_FORCE			(0x00000001UL)
/* Port X Interrupt Config Register 32bit (R/W) */
#define U300_GPIO_PXICR					(0x20)
#define U300_GPIO_PXICR_ALL_IRQ_CONFIG_MASK		(0x000000FFUL)
#define U300_GPIO_PXICR_IRQ_CONFIG_MASK			(0x00000001UL)
#define U300_GPIO_PXICR_IRQ_CONFIG_FALLING_EDGE		(0x00000000UL)
#define U300_GPIO_PXICR_IRQ_CONFIG_RISING_EDGE		(0x00000001UL)
#ifdef CONFIG_MACH_U300_BS335
/* seven ports of 8 bits each = GPIO pins 0..55 */
#define U300_GPIO_NUM_PORTS 7
#else
/* five ports of 8 bits each = GPIO pins 0..39 */
#define U300_GPIO_NUM_PORTS 5
#endif
#define U300_GPIO_PINS_PER_PORT 8
#define U300_GPIO_MAX (U300_GPIO_PINS_PER_PORT * U300_GPIO_NUM_PORTS - 1)
#endif

/*
 * Individual pin assignments for the B26/S26. Notice that the
 * actual usage of these pins depends on the PAD MUX settings, that
 * is why the same number can potentially appear several times.
 * In the reference design each pin is only used for one purpose.
 * These were determined by inspecting the B26/S26 schematic:
 * 2/1911-ROA 128 1603
 */
#ifdef CONFIG_MACH_U300_BS2X
#define U300_GPIO_PIN_UART_RX		0
#define U300_GPIO_PIN_UART_TX		1
#define U300_GPIO_PIN_GPIO02		2  /* Unrouted */
#define U300_GPIO_PIN_GPIO03		3  /* Unrouted */
#define U300_GPIO_PIN_CAM_SLEEP		4
#define U300_GPIO_PIN_CAM_REG_EN	5
#define U300_GPIO_PIN_GPIO06		6  /* Unrouted */
#define U300_GPIO_PIN_GPIO07		7  /* Unrouted */

#define U300_GPIO_PIN_GPIO08		8  /* Service point SP2321 */
#define U300_GPIO_PIN_GPIO09		9  /* Service point SP2322 */
#define U300_GPIO_PIN_PHFSENSE		10 /* Headphone jack sensing */
#define U300_GPIO_PIN_MMC_CLKRET	11 /* Clock return from MMC/SD card */
#define U300_GPIO_PIN_MMC_CD		12 /* MMC Card insertion detection */
#define U300_GPIO_PIN_FLIPSENSE		13 /* Mechanical flip sensing */
#define U300_GPIO_PIN_GPIO14		14 /* DSP JTAG Port RTCK */
#define U300_GPIO_PIN_GPIO15		15 /* Unrouted */

#define U300_GPIO_PIN_GPIO16		16 /* Unrouted */
#define U300_GPIO_PIN_GPIO17		17 /* Unrouted */
#define U300_GPIO_PIN_GPIO18		18 /* Unrouted */
#define U300_GPIO_PIN_GPIO19		19 /* Unrouted */
#define U300_GPIO_PIN_GPIO20		20 /* Unrouted */
#define U300_GPIO_PIN_GPIO21		21 /* Unrouted */
#define U300_GPIO_PIN_GPIO22		22 /* Unrouted */
#define U300_GPIO_PIN_GPIO23		23 /* Unrouted */
#endif

/*
 * Individual pin assignments for the B330/S330 and B365/S365.
 * Notice that the actual usage of these pins depends on the
 * PAD MUX settings, that is why the same number can potentially
 * appear several times. In the reference design each pin is only
 * used for one purpose. These were determined by inspecting the
 * S365 schematic.
 */
#if defined(CONFIG_MACH_U300_BS330) || defined(CONFIG_MACH_U300_BS365) || \
    defined(CONFIG_MACH_U300_BS335)
#define U300_GPIO_PIN_UART_RX		0
#define U300_GPIO_PIN_UART_TX		1
#define U300_GPIO_PIN_UART_CTS		2
#define U300_GPIO_PIN_UART_RTS		3
#define U300_GPIO_PIN_CAM_MAIN_STANDBY	4 /* Camera MAIN standby */
#define U300_GPIO_PIN_GPIO05		5 /* Unrouted */
#define U300_GPIO_PIN_MS_CD		6 /* Memory Stick Card insertion */
#define U300_GPIO_PIN_GPIO07		7 /* Test point TP2430 */

#define U300_GPIO_PIN_GPIO08		8 /* Test point TP2437 */
#define U300_GPIO_PIN_GPIO09		9 /* Test point TP2431 */
#define U300_GPIO_PIN_GPIO10		10 /* Test point TP2432 */
#define U300_GPIO_PIN_MMC_CLKRET	11 /* Clock return from MMC/SD card */
#define U300_GPIO_PIN_MMC_CD		12 /* MMC Card insertion detection */
#define U300_GPIO_PIN_CAM_SUB_STANDBY	13 /* Camera SUB standby */
#define U300_GPIO_PIN_GPIO14		14 /* Test point TP2436 */
#define U300_GPIO_PIN_GPIO15		15 /* Unrouted */

#define U300_GPIO_PIN_GPIO16		16 /* Test point TP2438 */
#define U300_GPIO_PIN_PHFSENSE		17 /* Headphone jack sensing */
#define U300_GPIO_PIN_GPIO18		18 /* Test point TP2439 */
#define U300_GPIO_PIN_GPIO19		19 /* Routed somewhere */
#define U300_GPIO_PIN_GPIO20		20 /* Unrouted */
#define U300_GPIO_PIN_GPIO21		21 /* Unrouted */
#define U300_GPIO_PIN_GPIO22		22 /* Unrouted */
#define U300_GPIO_PIN_GPIO23		23 /* Unrouted */

#define U300_GPIO_PIN_GPIO24		24 /* Unrouted */
#define U300_GPIO_PIN_GPIO25		25 /* Unrouted */
#define U300_GPIO_PIN_GPIO26		26 /* Unrouted */
#define U300_GPIO_PIN_GPIO27		27 /* Unrouted */
#define U300_GPIO_PIN_GPIO28		28 /* Unrouted */
#define U300_GPIO_PIN_GPIO29		29 /* Unrouted */
#define U300_GPIO_PIN_GPIO30		30 /* Unrouted */
#define U300_GPIO_PIN_GPIO31		31 /* Unrouted */

#define U300_GPIO_PIN_GPIO32		32 /* Unrouted */
#define U300_GPIO_PIN_GPIO33		33 /* Unrouted */
#define U300_GPIO_PIN_GPIO34		34 /* Unrouted */
#define U300_GPIO_PIN_GPIO35		35 /* Unrouted */
#define U300_GPIO_PIN_GPIO36		36 /* Unrouted */
#define U300_GPIO_PIN_GPIO37		37 /* Unrouted */
#define U300_GPIO_PIN_GPIO38		38 /* Unrouted */
#define U300_GPIO_PIN_GPIO39		39 /* Unrouted */

#ifdef CONFIG_MACH_U300_BS335

#define U300_GPIO_PIN_GPIO40		40 /* Unrouted */
#define U300_GPIO_PIN_GPIO41		41 /* Unrouted */
#define U300_GPIO_PIN_GPIO42		42 /* Unrouted */
#define U300_GPIO_PIN_GPIO43		43 /* Unrouted */
#define U300_GPIO_PIN_GPIO44		44 /* Unrouted */
#define U300_GPIO_PIN_GPIO45		45 /* Unrouted */
#define U300_GPIO_PIN_GPIO46		46 /* Unrouted */
#define U300_GPIO_PIN_GPIO47		47 /* Unrouted */

#define U300_GPIO_PIN_GPIO48		48 /* Unrouted */
#define U300_GPIO_PIN_GPIO49		49 /* Unrouted */
#define U300_GPIO_PIN_GPIO50		50 /* Unrouted */
#define U300_GPIO_PIN_GPIO51		51 /* Unrouted */
#define U300_GPIO_PIN_GPIO52		52 /* Unrouted */
#define U300_GPIO_PIN_GPIO53		53 /* Unrouted */
#define U300_GPIO_PIN_GPIO54		54 /* Unrouted */
#define U300_GPIO_PIN_GPIO55		55 /* Unrouted */
#endif

#endif

/* translates a pin number to a port number */
#define PIN_TO_PORT(val) (val >> 3)

/* These can be found in arch/arm/mach-u300/gpio.c */
extern int gpio_request(unsigned gpio, const char *label);
extern void gpio_free(unsigned gpio);
extern int gpio_direction_input(unsigned gpio);
extern int gpio_direction_output(unsigned gpio, int value);
extern int gpio_register_callback(unsigned gpio,
				  int (*func)(void *arg),
				  void *);
extern int gpio_unregister_callback(unsigned gpio);
extern void enable_irq_on_gpio_pin(unsigned gpio, int edge);
extern void disable_irq_on_gpio_pin(unsigned gpio);
extern void gpio_pullup(unsigned gpio, int value);
extern int gpio_get_value(unsigned gpio);
extern void gpio_set_value(unsigned gpio, int value);

/* wrappers to sleep-enable the previous two functions */
static inline unsigned gpio_to_irq(unsigned gpio)
{
	return PIN_TO_PORT(gpio) + IRQ_U300_GPIO_PORT0;
}

static inline unsigned irq_to_gpio(unsigned irq)
{
	/*
	 * FIXME: This is no 1-1 mapping at all, it points to the
	 * whole block of 8 pins.
	 */
	return (irq - IRQ_U300_GPIO_PORT0) << 3;
}

#endif
