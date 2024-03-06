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
 * GPIO0-31 from GPIO1 block,	GPIO200-215 from GPIO2 block.
 */
#define ALCHEMY_GPIO1_BASE	0
#define ALCHEMY_GPIO2_BASE	200

#define ALCHEMY_GPIO1_NUM	32
#define ALCHEMY_GPIO2_NUM	16
#define ALCHEMY_GPIO1_MAX	(ALCHEMY_GPIO1_BASE + ALCHEMY_GPIO1_NUM - 1)
#define ALCHEMY_GPIO2_MAX	(ALCHEMY_GPIO2_BASE + ALCHEMY_GPIO2_NUM - 1)

#define MAKE_IRQ(intc, off)	(AU1000_INTC##intc##_INT_BASE + (off))

/* GPIO1 registers within SYS_ area */
#define AU1000_SYS_TRIOUTRD	0x100
#define AU1000_SYS_TRIOUTCLR	0x100
#define AU1000_SYS_OUTPUTRD	0x108
#define AU1000_SYS_OUTPUTSET	0x108
#define AU1000_SYS_OUTPUTCLR	0x10C
#define AU1000_SYS_PINSTATERD	0x110
#define AU1000_SYS_PININPUTEN	0x110

/* register offsets within GPIO2 block */
#define AU1000_GPIO2_DIR	0x00
#define AU1000_GPIO2_OUTPUT	0x08
#define AU1000_GPIO2_PINSTATE	0x0C
#define AU1000_GPIO2_INTENABLE	0x10
#define AU1000_GPIO2_ENABLE	0x14

struct gpio;

static inline int au1000_gpio1_to_irq(int gpio)
{
	return MAKE_IRQ(1, gpio - ALCHEMY_GPIO1_BASE);
}

static inline int au1000_gpio2_to_irq(int gpio)
{
	return -ENXIO;
}

static inline int au1000_irq_to_gpio(int irq)
{
	if ((irq >= AU1000_GPIO0_INT) && (irq <= AU1000_GPIO31_INT))
		return ALCHEMY_GPIO1_BASE + (irq - AU1000_GPIO0_INT) + 0;

	return -ENXIO;
}

static inline int au1500_gpio1_to_irq(int gpio)
{
	gpio -= ALCHEMY_GPIO1_BASE;

	switch (gpio) {
	case 0 ... 15:
	case 20:
	case 23 ... 28: return MAKE_IRQ(1, gpio);
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

static inline int au1500_irq_to_gpio(int irq)
{
	switch (irq) {
	case AU1500_GPIO0_INT ... AU1500_GPIO15_INT:
	case AU1500_GPIO20_INT:
	case AU1500_GPIO23_INT ... AU1500_GPIO28_INT:
		return ALCHEMY_GPIO1_BASE + (irq - AU1500_GPIO0_INT) + 0;
	case AU1500_GPIO200_INT ... AU1500_GPIO203_INT:
		return ALCHEMY_GPIO2_BASE + (irq - AU1500_GPIO200_INT) + 0;
	case AU1500_GPIO204_INT ... AU1500_GPIO205_INT:
		return ALCHEMY_GPIO2_BASE + (irq - AU1500_GPIO204_INT) + 4;
	case AU1500_GPIO206_INT ... AU1500_GPIO207_INT:
		return ALCHEMY_GPIO2_BASE + (irq - AU1500_GPIO206_INT) + 6;
	case AU1500_GPIO208_215_INT:
		return ALCHEMY_GPIO2_BASE + 8;
	}

	return -ENXIO;
}

static inline int au1100_gpio1_to_irq(int gpio)
{
	return MAKE_IRQ(1, gpio - ALCHEMY_GPIO1_BASE);
}

static inline int au1100_gpio2_to_irq(int gpio)
{
	gpio -= ALCHEMY_GPIO2_BASE;

	if ((gpio >= 8) && (gpio <= 15))
		return MAKE_IRQ(0, 29);		/* shared GPIO208_215 */

	return -ENXIO;
}

static inline int au1100_irq_to_gpio(int irq)
{
	switch (irq) {
	case AU1100_GPIO0_INT ... AU1100_GPIO31_INT:
		return ALCHEMY_GPIO1_BASE + (irq - AU1100_GPIO0_INT) + 0;
	case AU1100_GPIO208_215_INT:
		return ALCHEMY_GPIO2_BASE + 8;
	}

	return -ENXIO;
}

static inline int au1550_gpio1_to_irq(int gpio)
{
	gpio -= ALCHEMY_GPIO1_BASE;

	switch (gpio) {
	case 0 ... 15:
	case 20 ... 28: return MAKE_IRQ(1, gpio);
	case 16 ... 17: return MAKE_IRQ(1, 18 + gpio - 16);
	}

	return -ENXIO;
}

static inline int au1550_gpio2_to_irq(int gpio)
{
	gpio -= ALCHEMY_GPIO2_BASE;

	switch (gpio) {
	case 0:		return MAKE_IRQ(1, 16);
	case 1 ... 5:	return MAKE_IRQ(1, 17); /* shared GPIO201_205 */
	case 6 ... 7:	return MAKE_IRQ(1, 29 + gpio - 6);
	case 8 ... 15:	return MAKE_IRQ(1, 31); /* shared GPIO208_215 */
	}

	return -ENXIO;
}

static inline int au1550_irq_to_gpio(int irq)
{
	switch (irq) {
	case AU1550_GPIO0_INT ... AU1550_GPIO15_INT:
		return ALCHEMY_GPIO1_BASE + (irq - AU1550_GPIO0_INT) + 0;
	case AU1550_GPIO200_INT:
	case AU1550_GPIO201_205_INT:
		return ALCHEMY_GPIO2_BASE + (irq - AU1550_GPIO200_INT) + 0;
	case AU1550_GPIO16_INT ... AU1550_GPIO28_INT:
		return ALCHEMY_GPIO1_BASE + (irq - AU1550_GPIO16_INT) + 16;
	case AU1550_GPIO206_INT ... AU1550_GPIO208_215_INT:
		return ALCHEMY_GPIO2_BASE + (irq - AU1550_GPIO206_INT) + 6;
	}

	return -ENXIO;
}

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
	case 8 ... 15:	return MAKE_IRQ(0, 28); /* shared GPIO208_215 */
	}

	return -ENXIO;
}

static inline int au1200_irq_to_gpio(int irq)
{
	switch (irq) {
	case AU1200_GPIO0_INT ... AU1200_GPIO31_INT:
		return ALCHEMY_GPIO1_BASE + (irq - AU1200_GPIO0_INT) + 0;
	case AU1200_GPIO200_INT ... AU1200_GPIO202_INT:
		return ALCHEMY_GPIO2_BASE + (irq - AU1200_GPIO200_INT) + 0;
	case AU1200_GPIO203_INT:
		return ALCHEMY_GPIO2_BASE + 3;
	case AU1200_GPIO204_INT ... AU1200_GPIO208_215_INT:
		return ALCHEMY_GPIO2_BASE + (irq - AU1200_GPIO204_INT) + 4;
	}

	return -ENXIO;
}

/*
 * GPIO1 block macros for common linux gpio functions.
 */
static inline void alchemy_gpio1_set_value(int gpio, int v)
{
	unsigned long mask = 1 << (gpio - ALCHEMY_GPIO1_BASE);
	unsigned long r = v ? AU1000_SYS_OUTPUTSET : AU1000_SYS_OUTPUTCLR;
	alchemy_wrsys(mask, r);
}

static inline int alchemy_gpio1_get_value(int gpio)
{
	unsigned long mask = 1 << (gpio - ALCHEMY_GPIO1_BASE);
	return alchemy_rdsys(AU1000_SYS_PINSTATERD) & mask;
}

static inline int alchemy_gpio1_direction_input(int gpio)
{
	unsigned long mask = 1 << (gpio - ALCHEMY_GPIO1_BASE);
	alchemy_wrsys(mask, AU1000_SYS_TRIOUTCLR);
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
	switch (alchemy_get_cputype()) {
	case ALCHEMY_CPU_AU1000:
		return au1000_gpio1_to_irq(gpio);
	case ALCHEMY_CPU_AU1100:
		return au1100_gpio1_to_irq(gpio);
	case ALCHEMY_CPU_AU1500:
		return au1500_gpio1_to_irq(gpio);
	case ALCHEMY_CPU_AU1550:
		return au1550_gpio1_to_irq(gpio);
	case ALCHEMY_CPU_AU1200:
		return au1200_gpio1_to_irq(gpio);
	}
	return -ENXIO;
}

/* On Au1000, Au1500 and Au1100 GPIOs won't work as inputs before
 * SYS_PININPUTEN is written to at least once.  On Au1550/Au1200/Au1300 this
 * register enables use of GPIOs as wake source.
 */
static inline void alchemy_gpio1_input_enable(void)
{
	void __iomem *base = (void __iomem *)KSEG1ADDR(AU1000_SYS_PHYS_ADDR);
	__raw_writel(0, base + 0x110);		/* the write op is key */
	wmb();
}

/*
 * GPIO2 block macros for common linux GPIO functions. The 'gpio'
 * parameter must be in range of ALCHEMY_GPIO2_BASE..ALCHEMY_GPIO2_MAX.
 */
static inline void __alchemy_gpio2_mod_dir(int gpio, int to_out)
{
	void __iomem *base = (void __iomem *)KSEG1ADDR(AU1500_GPIO2_PHYS_ADDR);
	unsigned long mask = 1 << (gpio - ALCHEMY_GPIO2_BASE);
	unsigned long d = __raw_readl(base + AU1000_GPIO2_DIR);

	if (to_out)
		d |= mask;
	else
		d &= ~mask;
	__raw_writel(d, base + AU1000_GPIO2_DIR);
	wmb();
}

static inline void alchemy_gpio2_set_value(int gpio, int v)
{
	void __iomem *base = (void __iomem *)KSEG1ADDR(AU1500_GPIO2_PHYS_ADDR);
	unsigned long mask;
	mask = ((v) ? 0x00010001 : 0x00010000) << (gpio - ALCHEMY_GPIO2_BASE);
	__raw_writel(mask, base + AU1000_GPIO2_OUTPUT);
	wmb();
}

static inline int alchemy_gpio2_get_value(int gpio)
{
	void __iomem *base = (void __iomem *)KSEG1ADDR(AU1500_GPIO2_PHYS_ADDR);
	return __raw_readl(base + AU1000_GPIO2_PINSTATE) &
				(1 << (gpio - ALCHEMY_GPIO2_BASE));
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
	switch (alchemy_get_cputype()) {
	case ALCHEMY_CPU_AU1000:
		return au1000_gpio2_to_irq(gpio);
	case ALCHEMY_CPU_AU1100:
		return au1100_gpio2_to_irq(gpio);
	case ALCHEMY_CPU_AU1500:
		return au1500_gpio2_to_irq(gpio);
	case ALCHEMY_CPU_AU1550:
		return au1550_gpio2_to_irq(gpio);
	case ALCHEMY_CPU_AU1200:
		return au1200_gpio2_to_irq(gpio);
	}
	return -ENXIO;
}

/**********************************************************************/

/* GPIO2 shared interrupts and control */

static inline void __alchemy_gpio2_mod_int(int gpio2, int en)
{
	void __iomem *base = (void __iomem *)KSEG1ADDR(AU1500_GPIO2_PHYS_ADDR);
	unsigned long r = __raw_readl(base + AU1000_GPIO2_INTENABLE);
	if (en)
		r |= 1 << gpio2;
	else
		r &= ~(1 << gpio2);
	__raw_writel(r, base + AU1000_GPIO2_INTENABLE);
	wmb();
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

	/* Au1100/Au1500 have GPIO208-215 enable bits at 0..7 */
	switch (alchemy_get_cputype()) {
	case ALCHEMY_CPU_AU1100:
	case ALCHEMY_CPU_AU1500:
		gpio2 -= 8;
	}

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

	/* Au1100/Au1500 have GPIO208-215 enable bits at 0..7 */
	switch (alchemy_get_cputype()) {
	case ALCHEMY_CPU_AU1100:
	case ALCHEMY_CPU_AU1500:
		gpio2 -= 8;
	}

	local_irq_save(flags);
	__alchemy_gpio2_mod_int(gpio2, 0);
	local_irq_restore(flags);
}

/**
 * alchemy_gpio2_enable -  Activate GPIO2 block.
 *
 * The GPIO2 block must be enabled explicitly to work.	 On systems
 * where this isn't done by the bootloader, this macro can be used.
 */
static inline void alchemy_gpio2_enable(void)
{
	void __iomem *base = (void __iomem *)KSEG1ADDR(AU1500_GPIO2_PHYS_ADDR);
	__raw_writel(3, base + AU1000_GPIO2_ENABLE);	/* reset, clock enabled */
	wmb();
	__raw_writel(1, base + AU1000_GPIO2_ENABLE);	/* clock enabled */
	wmb();
}

/**
 * alchemy_gpio2_disable - disable GPIO2 block.
 *
 * Disable and put GPIO2 block in low-power mode.
 */
static inline void alchemy_gpio2_disable(void)
{
	void __iomem *base = (void __iomem *)KSEG1ADDR(AU1500_GPIO2_PHYS_ADDR);
	__raw_writel(2, base + AU1000_GPIO2_ENABLE);	/* reset, clock disabled */
	wmb();
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

static inline int alchemy_gpio_to_irq(int gpio)
{
	return (gpio >= ALCHEMY_GPIO2_BASE) ?
		alchemy_gpio2_to_irq(gpio) :
		alchemy_gpio1_to_irq(gpio);
}

static inline int alchemy_irq_to_gpio(int irq)
{
	switch (alchemy_get_cputype()) {
	case ALCHEMY_CPU_AU1000:
		return au1000_irq_to_gpio(irq);
	case ALCHEMY_CPU_AU1100:
		return au1100_irq_to_gpio(irq);
	case ALCHEMY_CPU_AU1500:
		return au1500_irq_to_gpio(irq);
	case ALCHEMY_CPU_AU1550:
		return au1550_irq_to_gpio(irq);
	case ALCHEMY_CPU_AU1200:
		return au1200_irq_to_gpio(irq);
	}
	return -ENXIO;
}

#endif /* _ALCHEMY_GPIO_AU1000_H_ */
