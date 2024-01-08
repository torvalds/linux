/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Coldfire generic GPIO support.
 *
 * (C) Copyright 2009, Steven King <sfking@fdwdc.com>
 */

#ifndef mcfgpio_h
#define mcfgpio_h

int __mcfgpio_get_value(unsigned gpio);
void __mcfgpio_set_value(unsigned gpio, int value);
int __mcfgpio_direction_input(unsigned gpio);
int __mcfgpio_direction_output(unsigned gpio, int value);
int __mcfgpio_request(unsigned gpio);
void __mcfgpio_free(unsigned gpio);

#ifdef CONFIG_GPIOLIB
#include <linux/gpio.h>
#else

/* our alternate 'gpiolib' functions */
static inline int __gpio_get_value(unsigned gpio)
{
	if (gpio < MCFGPIO_PIN_MAX)
		return __mcfgpio_get_value(gpio);
	else
		return -EINVAL;
}

static inline void __gpio_set_value(unsigned gpio, int value)
{
	if (gpio < MCFGPIO_PIN_MAX)
		__mcfgpio_set_value(gpio, value);
}

static inline int __gpio_to_irq(unsigned gpio)
{
	return -EINVAL;
}

static inline int gpio_direction_input(unsigned gpio)
{
	if (gpio < MCFGPIO_PIN_MAX)
		return __mcfgpio_direction_input(gpio);
	else
		return -EINVAL;
}

static inline int gpio_direction_output(unsigned gpio, int value)
{
	if (gpio < MCFGPIO_PIN_MAX)
		return __mcfgpio_direction_output(gpio, value);
	else
		return -EINVAL;
}

static inline int gpio_request(unsigned gpio, const char *label)
{
	if (gpio < MCFGPIO_PIN_MAX)
		return __mcfgpio_request(gpio);
	else
		return -EINVAL;
}

static inline void gpio_free(unsigned gpio)
{
	if (gpio < MCFGPIO_PIN_MAX)
		__mcfgpio_free(gpio);
}

#endif /* CONFIG_GPIOLIB */


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
 * This implementation attempts accommodate the differences while presenting
 * a generic interface that will optimize to as few instructions as possible.
 */
#if defined(CONFIG_M5206) || defined(CONFIG_M5206e) || \
    defined(CONFIG_M520x) || defined(CONFIG_M523x) || \
    defined(CONFIG_M527x) || defined(CONFIG_M528x) || \
    defined(CONFIG_M53xx) || defined(CONFIG_M54xx) || \
    defined(CONFIG_M5441x)

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

#elif defined(CONFIG_M5249) || defined(CONFIG_M525x)

/* These parts have GPIO organized by 32 bit ports */

#define MCFGPIO_PORTTYPE		u32
#define MCFGPIO_PORTSIZE		32
#define mcfgpio_read(port)		__raw_readl(port)
#define mcfgpio_write(data, port)	__raw_writel(data, port)

#endif

#define mcfgpio_bit(gpio)		(1 << ((gpio) %  MCFGPIO_PORTSIZE))
#define mcfgpio_port(gpio)		((gpio) / MCFGPIO_PORTSIZE)

#if defined(CONFIG_M520x) || defined(CONFIG_M523x) || \
    defined(CONFIG_M527x) || defined(CONFIG_M528x) || \
    defined(CONFIG_M53xx) || defined(CONFIG_M54xx) || \
    defined(CONFIG_M5441x)
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
#elif defined(CONFIGM5441x)
/* The m5441x EPORT doesn't have its own GPIO port, uses PORT C */
#define MCFGPIO_SCR_START		0
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
static inline u32 __mcfgpio_ppdr(unsigned gpio)
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
#elif defined(CONFIG_M5249) || defined(CONFIG_M525x)
	if (gpio < 32)
		return MCFSIM2_GPIOREAD;
	else
		return MCFSIM2_GPIO1READ;
#elif defined(CONFIG_M520x) || defined(CONFIG_M523x) || \
      defined(CONFIG_M527x) || defined(CONFIG_M528x) || \
      defined(CONFIG_M53xx) || defined(CONFIG_M54xx) || \
      defined(CONFIG_M5441x)
#if !defined(CONFIG_M5441x)
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
#endif /* defined(CONFIG_M528x) */
	else
#endif /* !defined(CONFIG_M5441x) */
		return MCFGPIO_PPDR + mcfgpio_port(gpio - MCFGPIO_SCR_START);
#else
	return 0;
#endif
}

/* return the port output data register for a gpio */
static inline u32 __mcfgpio_podr(unsigned gpio)
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
#elif defined(CONFIG_M5249) || defined(CONFIG_M525x)
	if (gpio < 32)
		return MCFSIM2_GPIOWRITE;
	else
		return MCFSIM2_GPIO1WRITE;
#elif defined(CONFIG_M520x) || defined(CONFIG_M523x) || \
      defined(CONFIG_M527x) || defined(CONFIG_M528x) || \
      defined(CONFIG_M53xx) || defined(CONFIG_M54xx) || \
      defined(CONFIG_M5441x)
#if !defined(CONFIG_M5441x)
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
#endif /* defined(CONFIG_M528x) */
	else
#endif /* !defined(CONFIG_M5441x) */
		return MCFGPIO_PODR + mcfgpio_port(gpio - MCFGPIO_SCR_START);
#else
	return 0;
#endif
}

/* return the port direction data register for a gpio */
static inline u32 __mcfgpio_pddr(unsigned gpio)
{
#if defined(CONFIG_M5206) || defined(CONFIG_M5206e) || \
    defined(CONFIG_M5307) || defined(CONFIG_M5407)
	return MCFSIM_PADDR;
#elif defined(CONFIG_M5272)
	if (gpio < 16)
		return MCFSIM_PADDR;
	else if (gpio < 32)
		return MCFSIM_PBDDR;
	else
		return MCFSIM_PCDDR;
#elif defined(CONFIG_M5249) || defined(CONFIG_M525x)
	if (gpio < 32)
		return MCFSIM2_GPIOENABLE;
	else
		return MCFSIM2_GPIO1ENABLE;
#elif defined(CONFIG_M520x) || defined(CONFIG_M523x) || \
      defined(CONFIG_M527x) || defined(CONFIG_M528x) || \
      defined(CONFIG_M53xx) || defined(CONFIG_M54xx) || \
      defined(CONFIG_M5441x)
#if !defined(CONFIG_M5441x)
	if (gpio < 8)
		return MCFEPORT_EPDDR;
#if defined(CONFIG_M528x)
	else if (gpio < 16)
		return MCFGPTA_GPTDDR;
	else if (gpio < 24)
		return MCFGPTB_GPTDDR;
	else if (gpio < 32)
		return MCFQADC_DDRQA;
	else if (gpio < 40)
		return MCFQADC_DDRQB;
#endif /* defined(CONFIG_M528x) */
	else
#endif /* !defined(CONFIG_M5441x) */
		return MCFGPIO_PDDR + mcfgpio_port(gpio - MCFGPIO_SCR_START);
#else
	return 0;
#endif
}

#endif /* mcfgpio_h */
