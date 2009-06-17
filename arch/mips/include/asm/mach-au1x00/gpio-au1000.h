/*
 * GPIO functions for Au1000, Au1500, Au1100, Au1550, Au1200
 *
 * Copyright (c) 2009 Manuel Lauss.
 *
 * Licensed under the terms outlined in the file COPYING.
 */

#ifndef _ALCHEMY_GPIO_AU1000_H_
#define _ALCHEMY_GPIO_AU1000_H_

#include <asm/mach-au1x00/au1000.h>

/* The default GPIO numberspace as documented in the Alchemy manuals.
 * GPIO0-31 from GPIO1 block,   GPIO200-215 from GPIO2 block.
 */
#define ALCHEMY_GPIO1_BASE	0
#define ALCHEMY_GPIO2_BASE	200

#define ALCHEMY_GPIO1_NUM	32
#define ALCHEMY_GPIO2_NUM	16
#define ALCHEMY_GPIO1_MAX 	(ALCHEMY_GPIO1_BASE + ALCHEMY_GPIO1_NUM - 1)
#define ALCHEMY_GPIO2_MAX	(ALCHEMY_GPIO2_BASE + ALCHEMY_GPIO2_NUM - 1)

#define MAKE_IRQ(intc, off)	(AU1000_INTC##intc##_INT_BASE + (off))


static inline int au1000_gpio1_to_irq(int gpio)
{
	return MAKE_IRQ(1, gpio - ALCHEMY_GPIO1_BASE);
}

static inline int au1000_gpio2_to_irq(int gpio)
{
	return -ENXIO;
}

#ifdef CONFIG_SOC_AU1000
static inline int au1000_irq_to_gpio(int irq)
{
	if ((irq >= AU1000_GPIO_0) && (irq <= AU1000_GPIO_31))
		return ALCHEMY_GPIO1_BASE + (irq - AU1000_GPIO_0) + 0;

	return -ENXIO;
}
#endif

static inline int au1500_gpio1_to_irq(int gpio)
{
	gpio -= ALCHEMY_GPIO1_BASE;

	switch (gpio) {
	case 0 ... 15:
	case 20:
	case 23 ... 28:	return MAKE_IRQ(1, gpio);
	}

	return -ENXIO;
}

static inline int au1500_gpio2_to_irq(int gpio)
{
	gpio -= ALCHEMY_GPIO2_BASE;

	switch (gpio) {
	case 0 ... 3:	return MAKE_IRQ(1, 16 + gpio - 0);
	case 4 ... 5:	return MAKE_IRQ(1, 21 + gpio - 4);
	case 6 ... 7:	return MAKE_IRQ(1, 29 + gpio - 6);
	}

	return -ENXIO;
}

#ifdef CONFIG_SOC_AU1500
static inline int au1500_irq_to_gpio(int irq)
{
	switch (irq) {
	case AU1000_GPIO_0 ... AU1000_GPIO_15:
	case AU1500_GPIO_20:
	case AU1500_GPIO_23 ... AU1500_GPIO_28:
		return ALCHEMY_GPIO1_BASE + (irq - AU1000_GPIO_0) + 0;
	case AU1500_GPIO_200 ... AU1500_GPIO_203:
		return ALCHEMY_GPIO2_BASE + (irq - AU1500_GPIO_200) + 0;
	case AU1500_GPIO_204 ... AU1500_GPIO_205:
		return ALCHEMY_GPIO2_BASE + (irq - AU1500_GPIO_204) + 4;
	case AU1500_GPIO_206 ... AU1500_GPIO_207:
		return ALCHEMY_GPIO2_BASE + (irq - AU1500_GPIO_206) + 6;
	case AU1500_GPIO_208_215:
		return ALCHEMY_GPIO2_BASE + 8;
	}

	return -ENXIO;
}
#endif

static inline int au1100_gpio1_to_irq(int gpio)
{
	return MAKE_IRQ(1, gpio - ALCHEMY_GPIO1_BASE);
}

static inline int au1100_gpio2_to_irq(int gpio)
{
	gpio -= ALCHEMY_GPIO2_BASE;

	if ((gpio >= 8) && (gpio <= 15))
		return MAKE_IRQ(0, 29);		/* shared GPIO208_215 */
}

#ifdef CONFIG_SOC_AU1100
static inline int au1100_irq_to_gpio(int irq)
{
	switch (irq) {
	case AU1000_GPIO_0 ... AU1000_GPIO_31:
		return ALCHEMY_GPIO1_BASE + (irq - AU1000_GPIO_0) + 0;
	case AU1100_GPIO_208_215:
		return ALCHEMY_GPIO2_BASE + 8;
	}

	return -ENXIO;
}
#endif

static inline int au1550_gpio1_to_irq(int gpio)
{
	gpio -= ALCHEMY_GPIO1_BASE;

	switch (gpio) {
	case 0 ... 15:
	case 20 ... 28:	return MAKE_IRQ(1, gpio);
	case 16 ... 17:	return MAKE_IRQ(1, 18 + gpio - 16);
	}

	return -ENXIO;
}

static inline int au1550_gpio2_to_irq(int gpio)
{
	gpio -= ALCHEMY_GPIO2_BASE;

	switch (gpio) {
	case 0:		return MAKE_IRQ(1, 16);
	case 1 ... 5:	return MAKE_IRQ(1, 17);	/* shared GPIO201_205 */
	case 6 ... 7:	return MAKE_IRQ(1, 29 + gpio - 6);
	case 8 ... 15:	return MAKE_IRQ(1, 31);	/* shared GPIO208_215 */
	}

	return -ENXIO;
}

#ifdef CONFIG_SOC_AU1550
static inline int au1550_irq_to_gpio(int irq)
{
	switch (irq) {
	case AU1000_GPIO_0 ... AU1000_GPIO_15:
		return ALCHEMY_GPIO1_BASE + (irq - AU1000_GPIO_0) + 0;
	case AU1550_GPIO_200:
	case AU1500_GPIO_201_205:
		return ALCHEMY_GPIO2_BASE + (irq - AU1550_GPIO_200) + 0;
	case AU1500_GPIO_16 ... AU1500_GPIO_28:
		return ALCHEMY_GPIO1_BASE + (irq - AU1500_GPIO_16) + 16;
	case AU1500_GPIO_206 ... AU1500_GPIO_208_218:
		return ALCHEMY_GPIO2_BASE + (irq - AU1500_GPIO_206) + 6;
	}

	return -ENXIO;
}
#endif

static inline int au1200_gpio1_to_irq(int gpio)
{
	return MAKE_IRQ(1, gpio - ALCHEMY_GPIO1_BASE);
}

static inline int au1200_gpio2_to_irq(int gpio)
{
	gpio -= ALCHEMY_GPIO2_BASE;

	switch (gpio) {
	case 0 ... 2:	return MAKE_IRQ(0, 5 + gpio - 0);
	case 3:		return MAKE_IRQ(0, 22);
	case 4 ... 7:	return MAKE_IRQ(0, 24 + gpio - 4);
	case 8 ... 15:	return MAKE_IRQ(0, 28);	/* shared GPIO208_215 */
	}

	return -ENXIO;
}

#ifdef CONFIG_SOC_AU1200
static inline int au1200_irq_to_gpio(int irq)
{
	switch (irq) {
	case AU1000_GPIO_0 ... AU1000_GPIO_31:
		return ALCHEMY_GPIO1_BASE + (irq - AU1000_GPIO_0) + 0;
	case AU1200_GPIO_200 ... AU1200_GPIO_202:
		return ALCHEMY_GPIO2_BASE + (irq - AU1200_GPIO_200) + 0;
	case AU1200_GPIO_203:
		return ALCHEMY_GPIO2_BASE + 3;
	case AU1200_GPIO_204 ... AU1200_GPIO_208_215:
		return ALCHEMY_GPIO2_BASE + (irq - AU1200_GPIO_204) + 4;
	}

	return -ENXIO;
}
#endif

/*
 * GPIO1 block macros for common linux gpio functions.
 */
static inline void alchemy_gpio1_set_value(int gpio, int v)
{
	unsigned long mask = 1 << (gpio - ALCHEMY_GPIO1_BASE);
	unsigned long r = v ? SYS_OUTPUTSET : SYS_OUTPUTCLR;
	au_writel(mask, r);
	au_sync();
}

static inline int alchemy_gpio1_get_value(int gpio)
{
	unsigned long mask = 1 << (gpio - ALCHEMY_GPIO1_BASE);
	return au_readl(SYS_PINSTATERD) & mask;
}

static inline int alchemy_gpio1_direction_input(int gpio)
{
	unsigned long mask = 1 << (gpio - ALCHEMY_GPIO1_BASE);
	au_writel(mask, SYS_TRIOUTCLR);
	au_sync();
	return 0;
}

static inline int alchemy_gpio1_direction_output(int gpio, int v)
{
	/* hardware switches to "output" mode when one of the two
	 * "set_value" registers is accessed.
	 */
	alchemy_gpio1_set_value(gpio, v);
	return 0;
}

static inline int alchemy_gpio1_is_valid(int gpio)
{
	return ((gpio >= ALCHEMY_GPIO1_BASE) && (gpio <= ALCHEMY_GPIO1_MAX));
}

static inline int alchemy_gpio1_to_irq(int gpio)
{
#if defined(CONFIG_SOC_AU1000)
	return au1000_gpio1_to_irq(gpio);
#elif defined(CONFIG_SOC_AU1100)
	return au1100_gpio1_to_irq(gpio);
#elif defined(CONFIG_SOC_AU1500)
	return au1500_gpio1_to_irq(gpio);
#elif defined(CONFIG_SOC_AU1550)
	return au1550_gpio1_to_irq(gpio);
#elif defined(CONFIG_SOC_AU1200)
	return au1200_gpio1_to_irq(gpio);
#else
	return -ENXIO;
#endif
}

/*
 * GPIO2 block macros for common linux GPIO functions. The 'gpio'
 * parameter must be in range of ALCHEMY_GPIO2_BASE..ALCHEMY_GPIO2_MAX.
 */
static inline void __alchemy_gpio2_mod_dir(int gpio, int to_out)
{
	unsigned long mask = 1 << (gpio - ALCHEMY_GPIO2_BASE);
	unsigned long d = au_readl(GPIO2_DIR);
	if (to_out)
		d |= mask;
	else
		d &= ~mask;
	au_writel(d, GPIO2_DIR);
	au_sync();
}

static inline void alchemy_gpio2_set_value(int gpio, int v)
{
	unsigned long mask;
	mask = ((v) ? 0x00010001 : 0x00010000) << (gpio - ALCHEMY_GPIO2_BASE);
	au_writel(mask, GPIO2_OUTPUT);
	au_sync();
}

static inline int alchemy_gpio2_get_value(int gpio)
{
	return au_readl(GPIO2_PINSTATE) & (1 << (gpio - ALCHEMY_GPIO2_BASE));
}

static inline int alchemy_gpio2_direction_input(int gpio)
{
	unsigned long flags;
	local_irq_save(flags);
	__alchemy_gpio2_mod_dir(gpio, 0);
	local_irq_restore(flags);
	return 0;
}

static inline int alchemy_gpio2_direction_output(int gpio, int v)
{
	unsigned long flags;
	alchemy_gpio2_set_value(gpio, v);
	local_irq_save(flags);
	__alchemy_gpio2_mod_dir(gpio, 1);
	local_irq_restore(flags);
	return 0;
}

static inline int alchemy_gpio2_is_valid(int gpio)
{
	return ((gpio >= ALCHEMY_GPIO2_BASE) && (gpio <= ALCHEMY_GPIO2_MAX));
}

static inline int alchemy_gpio2_to_irq(int gpio)
{
#if defined(CONFIG_SOC_AU1000)
	return au1000_gpio2_to_irq(gpio);
#elif defined(CONFIG_SOC_AU1100)
	return au1100_gpio2_to_irq(gpio);
#elif defined(CONFIG_SOC_AU1500)
	return au1500_gpio2_to_irq(gpio);
#elif defined(CONFIG_SOC_AU1550)
	return au1550_gpio2_to_irq(gpio);
#elif defined(CONFIG_SOC_AU1200)
	return au1200_gpio2_to_irq(gpio);
#else
	return -ENXIO;
#endif
}

/**********************************************************************/

/* On Au1000, Au1500 and Au1100 GPIOs won't work as inputs before
 * SYS_PININPUTEN is written to at least once.  On Au1550/Au1200 this
 * register enables use of GPIOs as wake source.
 */
static inline void alchemy_gpio1_input_enable(void)
{
	au_writel(0, SYS_PININPUTEN);	/* the write op is key */
	au_sync();
}

/* GPIO2 shared interrupts and control */

static inline void __alchemy_gpio2_mod_int(int gpio2, int en)
{
	unsigned long r = au_readl(GPIO2_INTENABLE);
	if (en)
		r |= 1 << gpio2;
	else
		r &= ~(1 << gpio2);
	au_writel(r, GPIO2_INTENABLE);
	au_sync();
}

/**
 * alchemy_gpio2_enable_int - Enable a GPIO2 pins' shared irq contribution.
 * @gpio2:	The GPIO2 pin to activate (200...215).
 *
 * GPIO208-215 have one shared interrupt line to the INTC.  They are
 * and'ed with a per-pin enable bit and finally or'ed together to form
 * a single irq request (useful for active-high sources).
 * With this function, a pins' individual contribution to the int request
 * can be enabled.  As with all other GPIO-based interrupts, the INTC
 * must be programmed to accept the GPIO208_215 interrupt as well.
 *
 * NOTE: Calling this macro is only necessary for GPIO208-215; all other
 * GPIO2-based interrupts have their own request to the INTC.  Please
 * consult your Alchemy databook for more information!
 *
 * NOTE: On the Au1550, GPIOs 201-205 also have a shared interrupt request
 * line to the INTC, GPIO201_205.  This function can be used for those
 * as well.
 *
 * NOTE: 'gpio2' parameter must be in range of the GPIO2 numberspace
 * (200-215 by default). No sanity checks are made,
 */
static inline void alchemy_gpio2_enable_int(int gpio2)
{
	unsigned long flags;

	gpio2 -= ALCHEMY_GPIO2_BASE;

#if defined(CONFIG_SOC_AU1100) || defined(CONFIG_SOC_AU1500)
	/* Au1100/Au1500 have GPIO208-215 enable bits at 0..7 */
	gpio2 -= 8;
#endif
	local_irq_save(flags);
	__alchemy_gpio2_mod_int(gpio2, 1);
	local_irq_restore(flags);
}

/**
 * alchemy_gpio2_disable_int - Disable a GPIO2 pins' shared irq contribution.
 * @gpio2:	The GPIO2 pin to activate (200...215).
 *
 * see function alchemy_gpio2_enable_int() for more information.
 */
static inline void alchemy_gpio2_disable_int(int gpio2)
{
	unsigned long flags;

	gpio2 -= ALCHEMY_GPIO2_BASE;

#if defined(CONFIG_SOC_AU1100) || defined(CONFIG_SOC_AU1500)
	/* Au1100/Au1500 have GPIO208-215 enable bits at 0..7 */
	gpio2 -= 8;
#endif
	local_irq_save(flags);
	__alchemy_gpio2_mod_int(gpio2, 0);
	local_irq_restore(flags);
}

/**
 * alchemy_gpio2_enable -  Activate GPIO2 block.
 *
 * The GPIO2 block must be enabled excplicitly to work.  On systems
 * where this isn't done by the bootloader, this macro can be used.
 */
static inline void alchemy_gpio2_enable(void)
{
	au_writel(3, GPIO2_ENABLE);	/* reset, clock enabled */
	au_sync();
	au_writel(1, GPIO2_ENABLE);	/* clock enabled */
	au_sync();
}

/**
 * alchemy_gpio2_disable - disable GPIO2 block.
 *
 * Disable and put GPIO2 block in low-power mode.
 */
static inline void alchemy_gpio2_disable(void)
{
	au_writel(2, GPIO2_ENABLE);	/* reset, clock disabled */
	au_sync();
}

/**********************************************************************/

/* wrappers for on-chip gpios; can be used before gpio chips have been
 * registered with gpiolib.
 */
static inline int alchemy_gpio_direction_input(int gpio)
{
	return (gpio >= ALCHEMY_GPIO2_BASE) ?
		alchemy_gpio2_direction_input(gpio) :
		alchemy_gpio1_direction_input(gpio);
}

static inline int alchemy_gpio_direction_output(int gpio, int v)
{
	return (gpio >= ALCHEMY_GPIO2_BASE) ?
		alchemy_gpio2_direction_output(gpio, v) :
		alchemy_gpio1_direction_output(gpio, v);
}

static inline int alchemy_gpio_get_value(int gpio)
{
	return (gpio >= ALCHEMY_GPIO2_BASE) ?
		alchemy_gpio2_get_value(gpio) :
		alchemy_gpio1_get_value(gpio);
}

static inline void alchemy_gpio_set_value(int gpio, int v)
{
	if (gpio >= ALCHEMY_GPIO2_BASE)
		alchemy_gpio2_set_value(gpio, v);
	else
		alchemy_gpio1_set_value(gpio, v);
}

static inline int alchemy_gpio_is_valid(int gpio)
{
	return (gpio >= ALCHEMY_GPIO2_BASE) ?
		alchemy_gpio2_is_valid(gpio) :
		alchemy_gpio1_is_valid(gpio);
}

static inline int alchemy_gpio_cansleep(int gpio)
{
	return 0;	/* Alchemy never gets tired */
}

static inline int alchemy_gpio_to_irq(int gpio)
{
	return (gpio >= ALCHEMY_GPIO2_BASE) ?
		alchemy_gpio2_to_irq(gpio) :
		alchemy_gpio1_to_irq(gpio);
}

static inline int alchemy_irq_to_gpio(int irq)
{
#if defined(CONFIG_SOC_AU1000)
	return au1000_irq_to_gpio(irq);
#elif defined(CONFIG_SOC_AU1100)
	return au1100_irq_to_gpio(irq);
#elif defined(CONFIG_SOC_AU1500)
	return au1500_irq_to_gpio(irq);
#elif defined(CONFIG_SOC_AU1550)
	return au1550_irq_to_gpio(irq);
#elif defined(CONFIG_SOC_AU1200)
	return au1200_irq_to_gpio(irq);
#else
	return -ENXIO;
#endif
}

/**********************************************************************/

/* Linux gpio framework integration.
 *
 * 4 use cases of Au1000-Au1200 GPIOS:
 *(1) GPIOLIB=y, ALCHEMY_GPIO_INDIRECT=y:
 *	Board must register gpiochips.
 *(2) GPIOLIB=y, ALCHEMY_GPIO_INDIRECT=n:
 *	2 (1 for Au1000) gpio_chips are registered.
 *
 *(3) GPIOLIB=n, ALCHEMY_GPIO_INDIRECT=y:
 *	the boards' gpio.h must provide	the linux gpio wrapper functions,
 *
 *(4) GPIOLIB=n, ALCHEMY_GPIO_INDIRECT=n:
 *	inlinable gpio functions are provided which enable access to the
 *	Au1000 gpios only by using the numbers straight out of the data-
 *	sheets.

 * Cases 1 and 3 are intended for boards which want to provide their own
 * GPIO namespace and -operations (i.e. for example you have 8 GPIOs
 * which are in part provided by spare Au1000 GPIO pins and in part by
 * an external FPGA but you still want them to be accssible in linux
 * as gpio0-7. The board can of course use the alchemy_gpioX_* functions
 * as required).
 */

#ifndef CONFIG_GPIOLIB


#ifndef CONFIG_ALCHEMY_GPIO_INDIRECT	/* case (4) */

static inline int gpio_direction_input(int gpio)
{
	return alchemy_gpio_direction_input(gpio);
}

static inline int gpio_direction_output(int gpio, int v)
{
	return alchemy_gpio_direction_output(gpio, v);
}

static inline int gpio_get_value(int gpio)
{
	return alchemy_gpio_get_value(gpio);
}

static inline void gpio_set_value(int gpio, int v)
{
	alchemy_gpio_set_value(gpio, v);
}

static inline int gpio_is_valid(int gpio)
{
	return alchemy_gpio_is_valid(gpio);
}

static inline int gpio_cansleep(int gpio)
{
	return alchemy_gpio_cansleep(gpio);
}

static inline int gpio_to_irq(int gpio)
{
	return alchemy_gpio_to_irq(gpio);
}

static inline int irq_to_gpio(int irq)
{
	return alchemy_irq_to_gpio(irq);
}

#endif	/* !CONFIG_ALCHEMY_GPIO_INDIRECT */


#else	/* CONFIG GPIOLIB */


 /* using gpiolib to provide up to 2 gpio_chips for on-chip gpios */
#ifndef CONFIG_ALCHEMY_GPIO_INDIRECT	/* case (2) */

/* get everything through gpiolib */
#define gpio_to_irq	__gpio_to_irq
#define gpio_get_value	__gpio_get_value
#define gpio_set_value	__gpio_set_value
#define gpio_cansleep	__gpio_cansleep
#define irq_to_gpio	alchemy_irq_to_gpio

#include <asm-generic/gpio.h>

#endif	/* !CONFIG_ALCHEMY_GPIO_INDIRECT */


#endif	/* !CONFIG_GPIOLIB */

#endif /* _ALCHEMY_GPIO_AU1000_H_ */
