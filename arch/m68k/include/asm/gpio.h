/*
 * Coldfire generic GPIO support
 *
 * (C) Copyright 2009, Steven King <sfking@fdwdc.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
*/

#ifndef coldfire_gpio_h
#define coldfire_gpio_h

#include <linux/io.h>
#include <asm-generic/gpio.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>

/*
 * The Freescale Coldfire family is quite varied in how they implement GPIO.
 * Some parts have 8 bit ports, some have 16bit and some have 32bit; some have
 * only one port, others have multiple ports; some have a single data latch
 * for both input and output, others have a separate pin data register to read
 * input; some require a read-modify-write access to change an output, others
 * have set and clear registers for some of the outputs; Some have all the
 * GPIOs in a single control area, others have some GPIOs implemented in
 * different modules.
 *
 * This implementation attempts accomodate the differences while presenting
 * a generic interface that will optimize to as few instructions as possible.
 */
#if defined(CONFIG_M5206) || defined(CONFIG_M5206e) || \
    defined(CONFIG_M520x) || defined(CONFIG_M523x) || \
    defined(CONFIG_M527x) || defined(CONFIG_M528x) || \
    defined(CONFIG_M532x) || defined(CONFIG_M54xx)

/* These parts have GPIO organized by 8 bit ports */

#define MCFGPIO_PORTTYPE		u8
#define MCFGPIO_PORTSIZE		8
#define mcfgpio_read(port)		__raw_readb(port)
#define mcfgpio_write(data, port)	__raw_writeb(data, port)

#elif defined(CONFIG_M5307) || defined(CONFIG_M5407) || defined(CONFIG_M5272)

/* These parts have GPIO organized by 16 bit ports */

#define MCFGPIO_PORTTYPE		u16
#define MCFGPIO_PORTSIZE		16
#define mcfgpio_read(port)		__raw_readw(port)
#define mcfgpio_write(data, port)	__raw_writew(data, port)

#elif defined(CONFIG_M5249)

/* These parts have GPIO organized by 32 bit ports */

#define MCFGPIO_PORTTYPE		u32
#define MCFGPIO_PORTSIZE		32
#define mcfgpio_read(port)		__raw_readl(port)
#define mcfgpio_write(data, port)	__raw_writel(data, port)

#endif

#define mcfgpio_bit(gpio)		(1 << ((gpio) %  MCFGPIO_PORTSIZE))
#define mcfgpio_port(gpio)		((gpio) / MCFGPIO_PORTSIZE)

#if defined(CONFIG_M520x) || defined(CONFIG_M523x) || \
    defined(CONFIG_M527x) || defined(CONFIG_M528x) || defined(CONFIG_M532x)
/*
 * These parts have an 'Edge' Port module (external interrupt/GPIO) which uses
 * read-modify-write to change an output and a GPIO module which has separate
 * set/clr registers to directly change outputs with a single write access.
 */
#if defined(CONFIG_M528x)
/*
 * The 528x also has GPIOs in other modules (GPT, QADC) which use
 * read-modify-write as well as those controlled by the EPORT and GPIO modules.
 */
#define MCFGPIO_SCR_START		40
#else
#define MCFGPIO_SCR_START		8
#endif

#define MCFGPIO_SETR_PORT(gpio)		(MCFGPIO_SETR + \
					mcfgpio_port(gpio - MCFGPIO_SCR_START))

#define MCFGPIO_CLRR_PORT(gpio)		(MCFGPIO_CLRR + \
					mcfgpio_port(gpio - MCFGPIO_SCR_START))
#else

#define MCFGPIO_SCR_START		MCFGPIO_PIN_MAX
/* with MCFGPIO_SCR == MCFGPIO_PIN_MAX, these will be optimized away */
#define MCFGPIO_SETR_PORT(gpio)		0
#define MCFGPIO_CLRR_PORT(gpio)		0

#endif
/*
 * Coldfire specific helper functions
 */

/* return the port pin data register for a gpio */
static inline u32 __mcf_gpio_ppdr(unsigned gpio)
{
#if defined(CONFIG_M5206) || defined(CONFIG_M5206e) || \
    defined(CONFIG_M5307) || defined(CONFIG_M5407)
	return MCFSIM_PADAT;
#elif defined(CONFIG_M5272)
	if (gpio < 16)
		return MCFSIM_PADAT;
	else if (gpio < 32)
		return MCFSIM_PBDAT;
	else
		return MCFSIM_PCDAT;
#elif defined(CONFIG_M5249)
	if (gpio < 32)
		return MCFSIM2_GPIOREAD;
	else
		return MCFSIM2_GPIO1READ;
#elif defined(CONFIG_M520x) || defined(CONFIG_M523x) || \
      defined(CONFIG_M527x) || defined(CONFIG_M528x) || defined(CONFIG_M532x)
	if (gpio < 8)
		return MCFEPORT_EPPDR;
#if defined(CONFIG_M528x)
	else if (gpio < 16)
		return MCFGPTA_GPTPORT;
	else if (gpio < 24)
		return MCFGPTB_GPTPORT;
	else if (gpio < 32)
		return MCFQADC_PORTQA;
	else if (gpio < 40)
		return MCFQADC_PORTQB;
#endif
	else
		return MCFGPIO_PPDR + mcfgpio_port(gpio - MCFGPIO_SCR_START);
#else
	return 0;
#endif
}

/* return the port output data register for a gpio */
static inline u32 __mcf_gpio_podr(unsigned gpio)
{
#if defined(CONFIG_M5206) || defined(CONFIG_M5206e) || \
    defined(CONFIG_M5307) || defined(CONFIG_M5407)
	return MCFSIM_PADAT;
#elif defined(CONFIG_M5272)
	if (gpio < 16)
		return MCFSIM_PADAT;
	else if (gpio < 32)
		return MCFSIM_PBDAT;
	else
		return MCFSIM_PCDAT;
#elif defined(CONFIG_M5249)
	if (gpio < 32)
		return MCFSIM2_GPIOWRITE;
	else
		return MCFSIM2_GPIO1WRITE;
#elif defined(CONFIG_M520x) || defined(CONFIG_M523x) || \
      defined(CONFIG_M527x) || defined(CONFIG_M528x) || defined(CONFIG_M532x)
	if (gpio < 8)
		return MCFEPORT_EPDR;
#if defined(CONFIG_M528x)
	else if (gpio < 16)
		return MCFGPTA_GPTPORT;
	else if (gpio < 24)
		return MCFGPTB_GPTPORT;
	else if (gpio < 32)
		return MCFQADC_PORTQA;
	else if (gpio < 40)
		return MCFQADC_PORTQB;
#endif
	else
		return MCFGPIO_PODR + mcfgpio_port(gpio - MCFGPIO_SCR_START);
#else
	return 0;
#endif
}

/*
 * The Generic GPIO functions
 *
 * If the gpio is a compile time constant and is one of the Coldfire gpios,
 * use the inline version, otherwise dispatch thru gpiolib.
 */

static inline int gpio_get_value(unsigned gpio)
{
	if (__builtin_constant_p(gpio) && gpio < MCFGPIO_PIN_MAX)
		return mcfgpio_read(__mcf_gpio_ppdr(gpio)) & mcfgpio_bit(gpio);
	else
		return __gpio_get_value(gpio);
}

static inline void gpio_set_value(unsigned gpio, int value)
{
	if (__builtin_constant_p(gpio) && gpio < MCFGPIO_PIN_MAX) {
		if (gpio < MCFGPIO_SCR_START) {
			unsigned long flags;
			MCFGPIO_PORTTYPE data;

			local_irq_save(flags);
			data = mcfgpio_read(__mcf_gpio_podr(gpio));
			if (value)
				data |= mcfgpio_bit(gpio);
			else
				data &= ~mcfgpio_bit(gpio);
			mcfgpio_write(data, __mcf_gpio_podr(gpio));
			local_irq_restore(flags);
		} else {
			if (value)
				mcfgpio_write(mcfgpio_bit(gpio),
						MCFGPIO_SETR_PORT(gpio));
			else
				mcfgpio_write(~mcfgpio_bit(gpio),
						MCFGPIO_CLRR_PORT(gpio));
		}
	} else
		__gpio_set_value(gpio, value);
}

static inline int gpio_to_irq(unsigned gpio)
{
	return (gpio < MCFGPIO_IRQ_MAX) ? gpio + MCFGPIO_IRQ_VECBASE : -EINVAL;
}

static inline int irq_to_gpio(unsigned irq)
{
	return (irq >= MCFGPIO_IRQ_VECBASE &&
		irq < (MCFGPIO_IRQ_VECBASE + MCFGPIO_IRQ_MAX)) ?
		irq - MCFGPIO_IRQ_VECBASE : -ENXIO;
}

static inline int gpio_cansleep(unsigned gpio)
{
	return gpio < MCFGPIO_PIN_MAX ? 0 : __gpio_cansleep(gpio);
}

#endif
