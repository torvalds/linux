/*
 *  linux/arch/arm/plat-omap/gpio.c
 *
 * Support functions for OMAP GPIO
 *
 * Copyright (C) 2003-2005 Nokia Corporation
 * Written by Juha Yrjölä <juha.yrjola@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/sysdev.h>
#include <linux/err.h>
#include <linux/clk.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/arch/irqs.h>
#include <asm/arch/gpio.h>
#include <asm/mach/irq.h>

#include <asm/io.h>

/*
 * OMAP1510 GPIO registers
 */
#define OMAP1510_GPIO_BASE		(void __iomem *)0xfffce000
#define OMAP1510_GPIO_DATA_INPUT	0x00
#define OMAP1510_GPIO_DATA_OUTPUT	0x04
#define OMAP1510_GPIO_DIR_CONTROL	0x08
#define OMAP1510_GPIO_INT_CONTROL	0x0c
#define OMAP1510_GPIO_INT_MASK		0x10
#define OMAP1510_GPIO_INT_STATUS	0x14
#define OMAP1510_GPIO_PIN_CONTROL	0x18

#define OMAP1510_IH_GPIO_BASE		64

/*
 * OMAP1610 specific GPIO registers
 */
#define OMAP1610_GPIO1_BASE		(void __iomem *)0xfffbe400
#define OMAP1610_GPIO2_BASE		(void __iomem *)0xfffbec00
#define OMAP1610_GPIO3_BASE		(void __iomem *)0xfffbb400
#define OMAP1610_GPIO4_BASE		(void __iomem *)0xfffbbc00
#define OMAP1610_GPIO_REVISION		0x0000
#define OMAP1610_GPIO_SYSCONFIG		0x0010
#define OMAP1610_GPIO_SYSSTATUS		0x0014
#define OMAP1610_GPIO_IRQSTATUS1	0x0018
#define OMAP1610_GPIO_IRQENABLE1	0x001c
#define OMAP1610_GPIO_WAKEUPENABLE	0x0028
#define OMAP1610_GPIO_DATAIN		0x002c
#define OMAP1610_GPIO_DATAOUT		0x0030
#define OMAP1610_GPIO_DIRECTION		0x0034
#define OMAP1610_GPIO_EDGE_CTRL1	0x0038
#define OMAP1610_GPIO_EDGE_CTRL2	0x003c
#define OMAP1610_GPIO_CLEAR_IRQENABLE1	0x009c
#define OMAP1610_GPIO_CLEAR_WAKEUPENA	0x00a8
#define OMAP1610_GPIO_CLEAR_DATAOUT	0x00b0
#define OMAP1610_GPIO_SET_IRQENABLE1	0x00dc
#define OMAP1610_GPIO_SET_WAKEUPENA	0x00e8
#define OMAP1610_GPIO_SET_DATAOUT	0x00f0

/*
 * OMAP730 specific GPIO registers
 */
#define OMAP730_GPIO1_BASE		(void __iomem *)0xfffbc000
#define OMAP730_GPIO2_BASE		(void __iomem *)0xfffbc800
#define OMAP730_GPIO3_BASE		(void __iomem *)0xfffbd000
#define OMAP730_GPIO4_BASE		(void __iomem *)0xfffbd800
#define OMAP730_GPIO5_BASE		(void __iomem *)0xfffbe000
#define OMAP730_GPIO6_BASE		(void __iomem *)0xfffbe800
#define OMAP730_GPIO_DATA_INPUT		0x00
#define OMAP730_GPIO_DATA_OUTPUT	0x04
#define OMAP730_GPIO_DIR_CONTROL	0x08
#define OMAP730_GPIO_INT_CONTROL	0x0c
#define OMAP730_GPIO_INT_MASK		0x10
#define OMAP730_GPIO_INT_STATUS		0x14

/*
 * omap24xx specific GPIO registers
 */
#define OMAP24XX_GPIO1_BASE		(void __iomem *)0x48018000
#define OMAP24XX_GPIO2_BASE		(void __iomem *)0x4801a000
#define OMAP24XX_GPIO3_BASE		(void __iomem *)0x4801c000
#define OMAP24XX_GPIO4_BASE		(void __iomem *)0x4801e000
#define OMAP24XX_GPIO_REVISION		0x0000
#define OMAP24XX_GPIO_SYSCONFIG		0x0010
#define OMAP24XX_GPIO_SYSSTATUS		0x0014
#define OMAP24XX_GPIO_IRQSTATUS1	0x0018
#define OMAP24XX_GPIO_IRQSTATUS2	0x0028
#define OMAP24XX_GPIO_IRQENABLE2	0x002c
#define OMAP24XX_GPIO_IRQENABLE1	0x001c
#define OMAP24XX_GPIO_CTRL		0x0030
#define OMAP24XX_GPIO_OE		0x0034
#define OMAP24XX_GPIO_DATAIN		0x0038
#define OMAP24XX_GPIO_DATAOUT		0x003c
#define OMAP24XX_GPIO_LEVELDETECT0	0x0040
#define OMAP24XX_GPIO_LEVELDETECT1	0x0044
#define OMAP24XX_GPIO_RISINGDETECT	0x0048
#define OMAP24XX_GPIO_FALLINGDETECT	0x004c
#define OMAP24XX_GPIO_CLEARIRQENABLE1	0x0060
#define OMAP24XX_GPIO_SETIRQENABLE1	0x0064
#define OMAP24XX_GPIO_CLEARWKUENA	0x0080
#define OMAP24XX_GPIO_SETWKUENA		0x0084
#define OMAP24XX_GPIO_CLEARDATAOUT	0x0090
#define OMAP24XX_GPIO_SETDATAOUT	0x0094

struct gpio_bank {
	void __iomem *base;
	u16 irq;
	u16 virtual_irq_start;
	int method;
	u32 reserved_map;
	u32 suspend_wakeup;
	u32 saved_wakeup;
	spinlock_t lock;
};

#define METHOD_MPUIO		0
#define METHOD_GPIO_1510	1
#define METHOD_GPIO_1610	2
#define METHOD_GPIO_730		3
#define METHOD_GPIO_24XX	4

#ifdef CONFIG_ARCH_OMAP16XX
static struct gpio_bank gpio_bank_1610[5] = {
	{ OMAP_MPUIO_BASE,     INT_MPUIO,	    IH_MPUIO_BASE,     METHOD_MPUIO},
	{ OMAP1610_GPIO1_BASE, INT_GPIO_BANK1,	    IH_GPIO_BASE,      METHOD_GPIO_1610 },
	{ OMAP1610_GPIO2_BASE, INT_1610_GPIO_BANK2, IH_GPIO_BASE + 16, METHOD_GPIO_1610 },
	{ OMAP1610_GPIO3_BASE, INT_1610_GPIO_BANK3, IH_GPIO_BASE + 32, METHOD_GPIO_1610 },
	{ OMAP1610_GPIO4_BASE, INT_1610_GPIO_BANK4, IH_GPIO_BASE + 48, METHOD_GPIO_1610 },
};
#endif

#ifdef CONFIG_ARCH_OMAP15XX
static struct gpio_bank gpio_bank_1510[2] = {
	{ OMAP_MPUIO_BASE,    INT_MPUIO,      IH_MPUIO_BASE, METHOD_MPUIO },
	{ OMAP1510_GPIO_BASE, INT_GPIO_BANK1, IH_GPIO_BASE,  METHOD_GPIO_1510 }
};
#endif

#ifdef CONFIG_ARCH_OMAP730
static struct gpio_bank gpio_bank_730[7] = {
	{ OMAP_MPUIO_BASE,     INT_730_MPUIO,	    IH_MPUIO_BASE,	METHOD_MPUIO },
	{ OMAP730_GPIO1_BASE,  INT_730_GPIO_BANK1,  IH_GPIO_BASE,	METHOD_GPIO_730 },
	{ OMAP730_GPIO2_BASE,  INT_730_GPIO_BANK2,  IH_GPIO_BASE + 32,	METHOD_GPIO_730 },
	{ OMAP730_GPIO3_BASE,  INT_730_GPIO_BANK3,  IH_GPIO_BASE + 64,	METHOD_GPIO_730 },
	{ OMAP730_GPIO4_BASE,  INT_730_GPIO_BANK4,  IH_GPIO_BASE + 96,	METHOD_GPIO_730 },
	{ OMAP730_GPIO5_BASE,  INT_730_GPIO_BANK5,  IH_GPIO_BASE + 128, METHOD_GPIO_730 },
	{ OMAP730_GPIO6_BASE,  INT_730_GPIO_BANK6,  IH_GPIO_BASE + 160, METHOD_GPIO_730 },
};
#endif

#ifdef CONFIG_ARCH_OMAP24XX
static struct gpio_bank gpio_bank_24xx[4] = {
	{ OMAP24XX_GPIO1_BASE, INT_24XX_GPIO_BANK1, IH_GPIO_BASE,	METHOD_GPIO_24XX },
	{ OMAP24XX_GPIO2_BASE, INT_24XX_GPIO_BANK2, IH_GPIO_BASE + 32,	METHOD_GPIO_24XX },
	{ OMAP24XX_GPIO3_BASE, INT_24XX_GPIO_BANK3, IH_GPIO_BASE + 64,	METHOD_GPIO_24XX },
	{ OMAP24XX_GPIO4_BASE, INT_24XX_GPIO_BANK4, IH_GPIO_BASE + 96,	METHOD_GPIO_24XX },
};
#endif

static struct gpio_bank *gpio_bank;
static int gpio_bank_count;

static inline struct gpio_bank *get_gpio_bank(int gpio)
{
#ifdef CONFIG_ARCH_OMAP15XX
	if (cpu_is_omap15xx()) {
		if (OMAP_GPIO_IS_MPUIO(gpio))
			return &gpio_bank[0];
		return &gpio_bank[1];
	}
#endif
#if defined(CONFIG_ARCH_OMAP16XX)
	if (cpu_is_omap16xx()) {
		if (OMAP_GPIO_IS_MPUIO(gpio))
			return &gpio_bank[0];
		return &gpio_bank[1 + (gpio >> 4)];
	}
#endif
#ifdef CONFIG_ARCH_OMAP730
	if (cpu_is_omap730()) {
		if (OMAP_GPIO_IS_MPUIO(gpio))
			return &gpio_bank[0];
		return &gpio_bank[1 + (gpio >> 5)];
	}
#endif
#ifdef CONFIG_ARCH_OMAP24XX
	if (cpu_is_omap24xx())
		return &gpio_bank[gpio >> 5];
#endif
}

static inline int get_gpio_index(int gpio)
{
#ifdef CONFIG_ARCH_OMAP730
	if (cpu_is_omap730())
		return gpio & 0x1f;
#endif
#ifdef CONFIG_ARCH_OMAP24XX
	if (cpu_is_omap24xx())
		return gpio & 0x1f;
#endif
	return gpio & 0x0f;
}

static inline int gpio_valid(int gpio)
{
	if (gpio < 0)
		return -1;
#ifndef CONFIG_ARCH_OMAP24XX
	if (OMAP_GPIO_IS_MPUIO(gpio)) {
		if (gpio >= OMAP_MAX_GPIO_LINES + 16)
			return -1;
		return 0;
	}
#endif
#ifdef CONFIG_ARCH_OMAP15XX
	if (cpu_is_omap15xx() && gpio < 16)
		return 0;
#endif
#if defined(CONFIG_ARCH_OMAP16XX)
	if ((cpu_is_omap16xx()) && gpio < 64)
		return 0;
#endif
#ifdef CONFIG_ARCH_OMAP730
	if (cpu_is_omap730() && gpio < 192)
		return 0;
#endif
#ifdef CONFIG_ARCH_OMAP24XX
	if (cpu_is_omap24xx() && gpio < 128)
		return 0;
#endif
	return -1;
}

static int check_gpio(int gpio)
{
	if (unlikely(gpio_valid(gpio)) < 0) {
		printk(KERN_ERR "omap-gpio: invalid GPIO %d\n", gpio);
		dump_stack();
		return -1;
	}
	return 0;
}

static void _set_gpio_direction(struct gpio_bank *bank, int gpio, int is_input)
{
	void __iomem *reg = bank->base;
	u32 l;

	switch (bank->method) {
	case METHOD_MPUIO:
		reg += OMAP_MPUIO_IO_CNTL;
		break;
	case METHOD_GPIO_1510:
		reg += OMAP1510_GPIO_DIR_CONTROL;
		break;
	case METHOD_GPIO_1610:
		reg += OMAP1610_GPIO_DIRECTION;
		break;
	case METHOD_GPIO_730:
		reg += OMAP730_GPIO_DIR_CONTROL;
		break;
	case METHOD_GPIO_24XX:
		reg += OMAP24XX_GPIO_OE;
		break;
	}
	l = __raw_readl(reg);
	if (is_input)
		l |= 1 << gpio;
	else
		l &= ~(1 << gpio);
	__raw_writel(l, reg);
}

void omap_set_gpio_direction(int gpio, int is_input)
{
	struct gpio_bank *bank;

	if (check_gpio(gpio) < 0)
		return;
	bank = get_gpio_bank(gpio);
	spin_lock(&bank->lock);
	_set_gpio_direction(bank, get_gpio_index(gpio), is_input);
	spin_unlock(&bank->lock);
}

static void _set_gpio_dataout(struct gpio_bank *bank, int gpio, int enable)
{
	void __iomem *reg = bank->base;
	u32 l = 0;

	switch (bank->method) {
	case METHOD_MPUIO:
		reg += OMAP_MPUIO_OUTPUT;
		l = __raw_readl(reg);
		if (enable)
			l |= 1 << gpio;
		else
			l &= ~(1 << gpio);
		break;
	case METHOD_GPIO_1510:
		reg += OMAP1510_GPIO_DATA_OUTPUT;
		l = __raw_readl(reg);
		if (enable)
			l |= 1 << gpio;
		else
			l &= ~(1 << gpio);
		break;
	case METHOD_GPIO_1610:
		if (enable)
			reg += OMAP1610_GPIO_SET_DATAOUT;
		else
			reg += OMAP1610_GPIO_CLEAR_DATAOUT;
		l = 1 << gpio;
		break;
	case METHOD_GPIO_730:
		reg += OMAP730_GPIO_DATA_OUTPUT;
		l = __raw_readl(reg);
		if (enable)
			l |= 1 << gpio;
		else
			l &= ~(1 << gpio);
		break;
	case METHOD_GPIO_24XX:
		if (enable)
			reg += OMAP24XX_GPIO_SETDATAOUT;
		else
			reg += OMAP24XX_GPIO_CLEARDATAOUT;
		l = 1 << gpio;
		break;
	default:
		BUG();
		return;
	}
	__raw_writel(l, reg);
}

void omap_set_gpio_dataout(int gpio, int enable)
{
	struct gpio_bank *bank;

	if (check_gpio(gpio) < 0)
		return;
	bank = get_gpio_bank(gpio);
	spin_lock(&bank->lock);
	_set_gpio_dataout(bank, get_gpio_index(gpio), enable);
	spin_unlock(&bank->lock);
}

int omap_get_gpio_datain(int gpio)
{
	struct gpio_bank *bank;
	void __iomem *reg;

	if (check_gpio(gpio) < 0)
		return -1;
	bank = get_gpio_bank(gpio);
	reg = bank->base;
	switch (bank->method) {
	case METHOD_MPUIO:
		reg += OMAP_MPUIO_INPUT_LATCH;
		break;
	case METHOD_GPIO_1510:
		reg += OMAP1510_GPIO_DATA_INPUT;
		break;
	case METHOD_GPIO_1610:
		reg += OMAP1610_GPIO_DATAIN;
		break;
	case METHOD_GPIO_730:
		reg += OMAP730_GPIO_DATA_INPUT;
		break;
	case METHOD_GPIO_24XX:
		reg += OMAP24XX_GPIO_DATAIN;
		break;
	default:
		BUG();
		return -1;
	}
	return (__raw_readl(reg)
			& (1 << get_gpio_index(gpio))) != 0;
}

#define MOD_REG_BIT(reg, bit_mask, set)	\
do {	\
	int l = __raw_readl(base + reg); \
	if (set) l |= bit_mask; \
	else l &= ~bit_mask; \
	__raw_writel(l, base + reg); \
} while(0)

static inline void set_24xx_gpio_triggering(void __iomem *base, int gpio, int trigger)
{
	u32 gpio_bit = 1 << gpio;

	MOD_REG_BIT(OMAP24XX_GPIO_LEVELDETECT0, gpio_bit,
		trigger & __IRQT_LOWLVL);
	MOD_REG_BIT(OMAP24XX_GPIO_LEVELDETECT1, gpio_bit,
		trigger & __IRQT_HIGHLVL);
	MOD_REG_BIT(OMAP24XX_GPIO_RISINGDETECT, gpio_bit,
		trigger & __IRQT_RISEDGE);
	MOD_REG_BIT(OMAP24XX_GPIO_FALLINGDETECT, gpio_bit,
		trigger & __IRQT_FALEDGE);
	/* FIXME: Possibly do 'set_irq_handler(j, handle_level_irq)' if only level
	 * triggering requested. */
}

static int _set_gpio_triggering(struct gpio_bank *bank, int gpio, int trigger)
{
	void __iomem *reg = bank->base;
	u32 l = 0;

	switch (bank->method) {
	case METHOD_MPUIO:
		reg += OMAP_MPUIO_GPIO_INT_EDGE;
		l = __raw_readl(reg);
		if (trigger & __IRQT_RISEDGE)
			l |= 1 << gpio;
		else if (trigger & __IRQT_FALEDGE)
			l &= ~(1 << gpio);
		else
			goto bad;
		break;
	case METHOD_GPIO_1510:
		reg += OMAP1510_GPIO_INT_CONTROL;
		l = __raw_readl(reg);
		if (trigger & __IRQT_RISEDGE)
			l |= 1 << gpio;
		else if (trigger & __IRQT_FALEDGE)
			l &= ~(1 << gpio);
		else
			goto bad;
		break;
	case METHOD_GPIO_1610:
		if (gpio & 0x08)
			reg += OMAP1610_GPIO_EDGE_CTRL2;
		else
			reg += OMAP1610_GPIO_EDGE_CTRL1;
		gpio &= 0x07;
		/* We allow only edge triggering, i.e. two lowest bits */
		if (trigger & (__IRQT_LOWLVL | __IRQT_HIGHLVL))
			BUG();
		l = __raw_readl(reg);
		l &= ~(3 << (gpio << 1));
		if (trigger & __IRQT_RISEDGE)
			l |= 2 << (gpio << 1);
		if (trigger & __IRQT_FALEDGE)
			l |= 1 << (gpio << 1);
		break;
	case METHOD_GPIO_730:
		reg += OMAP730_GPIO_INT_CONTROL;
		l = __raw_readl(reg);
		if (trigger & __IRQT_RISEDGE)
			l |= 1 << gpio;
		else if (trigger & __IRQT_FALEDGE)
			l &= ~(1 << gpio);
		else
			goto bad;
		break;
	case METHOD_GPIO_24XX:
		set_24xx_gpio_triggering(reg, gpio, trigger);
		break;
	default:
		BUG();
		goto bad;
	}
	__raw_writel(l, reg);
	return 0;
bad:
	return -EINVAL;
}

static int gpio_irq_type(unsigned irq, unsigned type)
{
	struct gpio_bank *bank;
	unsigned gpio;
	int retval;

	if (irq > IH_MPUIO_BASE)
		gpio = OMAP_MPUIO(irq - IH_MPUIO_BASE);
	else
		gpio = irq - IH_GPIO_BASE;

	if (check_gpio(gpio) < 0)
		return -EINVAL;

	if (type & IRQT_PROBE)
		return -EINVAL;
	if (!cpu_is_omap24xx() && (type & (__IRQT_LOWLVL|__IRQT_HIGHLVL)))
		return -EINVAL;

	bank = get_gpio_bank(gpio);
	spin_lock(&bank->lock);
	retval = _set_gpio_triggering(bank, get_gpio_index(gpio), type);
	spin_unlock(&bank->lock);
	return retval;
}

static void _clear_gpio_irqbank(struct gpio_bank *bank, int gpio_mask)
{
	void __iomem *reg = bank->base;

	switch (bank->method) {
	case METHOD_MPUIO:
		/* MPUIO irqstatus is reset by reading the status register,
		 * so do nothing here */
		return;
	case METHOD_GPIO_1510:
		reg += OMAP1510_GPIO_INT_STATUS;
		break;
	case METHOD_GPIO_1610:
		reg += OMAP1610_GPIO_IRQSTATUS1;
		break;
	case METHOD_GPIO_730:
		reg += OMAP730_GPIO_INT_STATUS;
		break;
	case METHOD_GPIO_24XX:
		reg += OMAP24XX_GPIO_IRQSTATUS1;
		break;
	default:
		BUG();
		return;
	}
	__raw_writel(gpio_mask, reg);

	/* Workaround for clearing DSP GPIO interrupts to allow retention */
	if (cpu_is_omap2420())
		__raw_writel(gpio_mask, bank->base + OMAP24XX_GPIO_IRQSTATUS2);
}

static inline void _clear_gpio_irqstatus(struct gpio_bank *bank, int gpio)
{
	_clear_gpio_irqbank(bank, 1 << get_gpio_index(gpio));
}

static u32 _get_gpio_irqbank_mask(struct gpio_bank *bank)
{
	void __iomem *reg = bank->base;
	int inv = 0;
	u32 l;
	u32 mask;

	switch (bank->method) {
	case METHOD_MPUIO:
		reg += OMAP_MPUIO_GPIO_MASKIT;
		mask = 0xffff;
		inv = 1;
		break;
	case METHOD_GPIO_1510:
		reg += OMAP1510_GPIO_INT_MASK;
		mask = 0xffff;
		inv = 1;
		break;
	case METHOD_GPIO_1610:
		reg += OMAP1610_GPIO_IRQENABLE1;
		mask = 0xffff;
		break;
	case METHOD_GPIO_730:
		reg += OMAP730_GPIO_INT_MASK;
		mask = 0xffffffff;
		inv = 1;
		break;
	case METHOD_GPIO_24XX:
		reg += OMAP24XX_GPIO_IRQENABLE1;
		mask = 0xffffffff;
		break;
	default:
		BUG();
		return 0;
	}

	l = __raw_readl(reg);
	if (inv)
		l = ~l;
	l &= mask;
	return l;
}

static void _enable_gpio_irqbank(struct gpio_bank *bank, int gpio_mask, int enable)
{
	void __iomem *reg = bank->base;
	u32 l;

	switch (bank->method) {
	case METHOD_MPUIO:
		reg += OMAP_MPUIO_GPIO_MASKIT;
		l = __raw_readl(reg);
		if (enable)
			l &= ~(gpio_mask);
		else
			l |= gpio_mask;
		break;
	case METHOD_GPIO_1510:
		reg += OMAP1510_GPIO_INT_MASK;
		l = __raw_readl(reg);
		if (enable)
			l &= ~(gpio_mask);
		else
			l |= gpio_mask;
		break;
	case METHOD_GPIO_1610:
		if (enable)
			reg += OMAP1610_GPIO_SET_IRQENABLE1;
		else
			reg += OMAP1610_GPIO_CLEAR_IRQENABLE1;
		l = gpio_mask;
		break;
	case METHOD_GPIO_730:
		reg += OMAP730_GPIO_INT_MASK;
		l = __raw_readl(reg);
		if (enable)
			l &= ~(gpio_mask);
		else
			l |= gpio_mask;
		break;
	case METHOD_GPIO_24XX:
		if (enable)
			reg += OMAP24XX_GPIO_SETIRQENABLE1;
		else
			reg += OMAP24XX_GPIO_CLEARIRQENABLE1;
		l = gpio_mask;
		break;
	default:
		BUG();
		return;
	}
	__raw_writel(l, reg);
}

static inline void _set_gpio_irqenable(struct gpio_bank *bank, int gpio, int enable)
{
	_enable_gpio_irqbank(bank, 1 << get_gpio_index(gpio), enable);
}

/*
 * Note that ENAWAKEUP needs to be enabled in GPIO_SYSCONFIG register.
 * 1510 does not seem to have a wake-up register. If JTAG is connected
 * to the target, system will wake up always on GPIO events. While
 * system is running all registered GPIO interrupts need to have wake-up
 * enabled. When system is suspended, only selected GPIO interrupts need
 * to have wake-up enabled.
 */
static int _set_gpio_wakeup(struct gpio_bank *bank, int gpio, int enable)
{
	switch (bank->method) {
	case METHOD_GPIO_1610:
	case METHOD_GPIO_24XX:
		spin_lock(&bank->lock);
		if (enable)
			bank->suspend_wakeup |= (1 << gpio);
		else
			bank->suspend_wakeup &= ~(1 << gpio);
		spin_unlock(&bank->lock);
		return 0;
	default:
		printk(KERN_ERR "Can't enable GPIO wakeup for method %i\n",
		       bank->method);
		return -EINVAL;
	}
}

static void _reset_gpio(struct gpio_bank *bank, int gpio)
{
	_set_gpio_direction(bank, get_gpio_index(gpio), 1);
	_set_gpio_irqenable(bank, gpio, 0);
	_clear_gpio_irqstatus(bank, gpio);
	_set_gpio_triggering(bank, get_gpio_index(gpio), IRQT_NOEDGE);
}

/* Use disable_irq_wake() and enable_irq_wake() functions from drivers */
static int gpio_wake_enable(unsigned int irq, unsigned int enable)
{
	unsigned int gpio = irq - IH_GPIO_BASE;
	struct gpio_bank *bank;
	int retval;

	if (check_gpio(gpio) < 0)
		return -ENODEV;
	bank = get_gpio_bank(gpio);
	retval = _set_gpio_wakeup(bank, get_gpio_index(gpio), enable);

	return retval;
}

int omap_request_gpio(int gpio)
{
	struct gpio_bank *bank;

	if (check_gpio(gpio) < 0)
		return -EINVAL;

	bank = get_gpio_bank(gpio);
	spin_lock(&bank->lock);
	if (unlikely(bank->reserved_map & (1 << get_gpio_index(gpio)))) {
		printk(KERN_ERR "omap-gpio: GPIO %d is already reserved!\n", gpio);
		dump_stack();
		spin_unlock(&bank->lock);
		return -1;
	}
	bank->reserved_map |= (1 << get_gpio_index(gpio));

	/* Set trigger to none. You need to enable the desired trigger with
	 * request_irq() or set_irq_type().
	 */
	_set_gpio_triggering(bank, get_gpio_index(gpio), IRQT_NOEDGE);

#ifdef CONFIG_ARCH_OMAP15XX
	if (bank->method == METHOD_GPIO_1510) {
		void __iomem *reg;

		/* Claim the pin for MPU */
		reg = bank->base + OMAP1510_GPIO_PIN_CONTROL;
		__raw_writel(__raw_readl(reg) | (1 << get_gpio_index(gpio)), reg);
	}
#endif
#ifdef CONFIG_ARCH_OMAP16XX
	if (bank->method == METHOD_GPIO_1610) {
		/* Enable wake-up during idle for dynamic tick */
		void __iomem *reg = bank->base + OMAP1610_GPIO_SET_WAKEUPENA;
		__raw_writel(1 << get_gpio_index(gpio), reg);
	}
#endif
#ifdef CONFIG_ARCH_OMAP24XX
	if (bank->method == METHOD_GPIO_24XX) {
		/* Enable wake-up during idle for dynamic tick */
		void __iomem *reg = bank->base + OMAP24XX_GPIO_SETWKUENA;
		__raw_writel(1 << get_gpio_index(gpio), reg);
	}
#endif
	spin_unlock(&bank->lock);

	return 0;
}

void omap_free_gpio(int gpio)
{
	struct gpio_bank *bank;

	if (check_gpio(gpio) < 0)
		return;
	bank = get_gpio_bank(gpio);
	spin_lock(&bank->lock);
	if (unlikely(!(bank->reserved_map & (1 << get_gpio_index(gpio))))) {
		printk(KERN_ERR "omap-gpio: GPIO %d wasn't reserved!\n", gpio);
		dump_stack();
		spin_unlock(&bank->lock);
		return;
	}
#ifdef CONFIG_ARCH_OMAP16XX
	if (bank->method == METHOD_GPIO_1610) {
		/* Disable wake-up during idle for dynamic tick */
		void __iomem *reg = bank->base + OMAP1610_GPIO_CLEAR_WAKEUPENA;
		__raw_writel(1 << get_gpio_index(gpio), reg);
	}
#endif
#ifdef CONFIG_ARCH_OMAP24XX
	if (bank->method == METHOD_GPIO_24XX) {
		/* Disable wake-up during idle for dynamic tick */
		void __iomem *reg = bank->base + OMAP24XX_GPIO_CLEARWKUENA;
		__raw_writel(1 << get_gpio_index(gpio), reg);
	}
#endif
	bank->reserved_map &= ~(1 << get_gpio_index(gpio));
	_reset_gpio(bank, gpio);
	spin_unlock(&bank->lock);
}

/*
 * We need to unmask the GPIO bank interrupt as soon as possible to
 * avoid missing GPIO interrupts for other lines in the bank.
 * Then we need to mask-read-clear-unmask the triggered GPIO lines
 * in the bank to avoid missing nested interrupts for a GPIO line.
 * If we wait to unmask individual GPIO lines in the bank after the
 * line's interrupt handler has been run, we may miss some nested
 * interrupts.
 */
static void gpio_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	void __iomem *isr_reg = NULL;
	u32 isr;
	unsigned int gpio_irq;
	struct gpio_bank *bank;
	u32 retrigger = 0;
	int unmasked = 0;

	desc->chip->ack(irq);

	bank = get_irq_data(irq);
	if (bank->method == METHOD_MPUIO)
		isr_reg = bank->base + OMAP_MPUIO_GPIO_INT;
#ifdef CONFIG_ARCH_OMAP15XX
	if (bank->method == METHOD_GPIO_1510)
		isr_reg = bank->base + OMAP1510_GPIO_INT_STATUS;
#endif
#if defined(CONFIG_ARCH_OMAP16XX)
	if (bank->method == METHOD_GPIO_1610)
		isr_reg = bank->base + OMAP1610_GPIO_IRQSTATUS1;
#endif
#ifdef CONFIG_ARCH_OMAP730
	if (bank->method == METHOD_GPIO_730)
		isr_reg = bank->base + OMAP730_GPIO_INT_STATUS;
#endif
#ifdef CONFIG_ARCH_OMAP24XX
	if (bank->method == METHOD_GPIO_24XX)
		isr_reg = bank->base + OMAP24XX_GPIO_IRQSTATUS1;
#endif
	while(1) {
		u32 isr_saved, level_mask = 0;
		u32 enabled;

		enabled = _get_gpio_irqbank_mask(bank);
		isr_saved = isr = __raw_readl(isr_reg) & enabled;

		if (cpu_is_omap15xx() && (bank->method == METHOD_MPUIO))
			isr &= 0x0000ffff;

		if (cpu_is_omap24xx()) {
			level_mask =
				__raw_readl(bank->base +
					OMAP24XX_GPIO_LEVELDETECT0) |
				__raw_readl(bank->base +
					OMAP24XX_GPIO_LEVELDETECT1);
			level_mask &= enabled;
		}

		/* clear edge sensitive interrupts before handler(s) are
		called so that we don't miss any interrupt occurred while
		executing them */
		_enable_gpio_irqbank(bank, isr_saved & ~level_mask, 0);
		_clear_gpio_irqbank(bank, isr_saved & ~level_mask);
		_enable_gpio_irqbank(bank, isr_saved & ~level_mask, 1);

		/* if there is only edge sensitive GPIO pin interrupts
		configured, we could unmask GPIO bank interrupt immediately */
		if (!level_mask && !unmasked) {
			unmasked = 1;
			desc->chip->unmask(irq);
		}

		isr |= retrigger;
		retrigger = 0;
		if (!isr)
			break;

		gpio_irq = bank->virtual_irq_start;
		for (; isr != 0; isr >>= 1, gpio_irq++) {
			struct irq_desc *d;
			int irq_mask;
			if (!(isr & 1))
				continue;
			d = irq_desc + gpio_irq;
			/* Don't run the handler if it's already running
			 * or was disabled lazely.
			 */
			if (unlikely((d->depth ||
				      (d->status & IRQ_INPROGRESS)))) {
				irq_mask = 1 <<
					(gpio_irq - bank->virtual_irq_start);
				/* The unmasking will be done by
				 * enable_irq in case it is disabled or
				 * after returning from the handler if
				 * it's already running.
				 */
				_enable_gpio_irqbank(bank, irq_mask, 0);
				if (!d->depth) {
					/* Level triggered interrupts
					 * won't ever be reentered
					 */
					BUG_ON(level_mask & irq_mask);
					d->status |= IRQ_PENDING;
				}
				continue;
			}

			desc_handle_irq(gpio_irq, d);

			if (unlikely((d->status & IRQ_PENDING) && !d->depth)) {
				irq_mask = 1 <<
					(gpio_irq - bank->virtual_irq_start);
				d->status &= ~IRQ_PENDING;
				_enable_gpio_irqbank(bank, irq_mask, 1);
				retrigger |= irq_mask;
			}
		}

		if (cpu_is_omap24xx()) {
			/* clear level sensitive interrupts after handler(s) */
			_enable_gpio_irqbank(bank, isr_saved & level_mask, 0);
			_clear_gpio_irqbank(bank, isr_saved & level_mask);
			_enable_gpio_irqbank(bank, isr_saved & level_mask, 1);
		}

	}
	/* if bank has any level sensitive GPIO pin interrupt
	configured, we must unmask the bank interrupt only after
	handler(s) are executed in order to avoid spurious bank
	interrupt */
	if (!unmasked)
		desc->chip->unmask(irq);

}

static void gpio_irq_shutdown(unsigned int irq)
{
	unsigned int gpio = irq - IH_GPIO_BASE;
	struct gpio_bank *bank = get_gpio_bank(gpio);

	_reset_gpio(bank, gpio);
}

static void gpio_ack_irq(unsigned int irq)
{
	unsigned int gpio = irq - IH_GPIO_BASE;
	struct gpio_bank *bank = get_gpio_bank(gpio);

	_clear_gpio_irqstatus(bank, gpio);
}

static void gpio_mask_irq(unsigned int irq)
{
	unsigned int gpio = irq - IH_GPIO_BASE;
	struct gpio_bank *bank = get_gpio_bank(gpio);

	_set_gpio_irqenable(bank, gpio, 0);
}

static void gpio_unmask_irq(unsigned int irq)
{
	unsigned int gpio = irq - IH_GPIO_BASE;
	unsigned int gpio_idx = get_gpio_index(gpio);
	struct gpio_bank *bank = get_gpio_bank(gpio);

	_set_gpio_irqenable(bank, gpio_idx, 1);
}

static void mpuio_ack_irq(unsigned int irq)
{
	/* The ISR is reset automatically, so do nothing here. */
}

static void mpuio_mask_irq(unsigned int irq)
{
	unsigned int gpio = OMAP_MPUIO(irq - IH_MPUIO_BASE);
	struct gpio_bank *bank = get_gpio_bank(gpio);

	_set_gpio_irqenable(bank, gpio, 0);
}

static void mpuio_unmask_irq(unsigned int irq)
{
	unsigned int gpio = OMAP_MPUIO(irq - IH_MPUIO_BASE);
	struct gpio_bank *bank = get_gpio_bank(gpio);

	_set_gpio_irqenable(bank, gpio, 1);
}

static struct irq_chip gpio_irq_chip = {
	.name		= "GPIO",
	.shutdown	= gpio_irq_shutdown,
	.ack		= gpio_ack_irq,
	.mask		= gpio_mask_irq,
	.unmask		= gpio_unmask_irq,
	.set_type	= gpio_irq_type,
	.set_wake	= gpio_wake_enable,
};

static struct irq_chip mpuio_irq_chip = {
	.name	  = "MPUIO",
	.ack	  = mpuio_ack_irq,
	.mask	  = mpuio_mask_irq,
	.unmask	  = mpuio_unmask_irq,
	.set_type = gpio_irq_type,
};

static int initialized;
static struct clk * gpio_ick;
static struct clk * gpio_fck;

static int __init _omap_gpio_init(void)
{
	int i;
	struct gpio_bank *bank;

	initialized = 1;

	if (cpu_is_omap15xx()) {
		gpio_ick = clk_get(NULL, "arm_gpio_ck");
		if (IS_ERR(gpio_ick))
			printk("Could not get arm_gpio_ck\n");
		else
			clk_enable(gpio_ick);
	}
	if (cpu_is_omap24xx()) {
		gpio_ick = clk_get(NULL, "gpios_ick");
		if (IS_ERR(gpio_ick))
			printk("Could not get gpios_ick\n");
		else
			clk_enable(gpio_ick);
		gpio_fck = clk_get(NULL, "gpios_fck");
		if (IS_ERR(gpio_fck))
			printk("Could not get gpios_fck\n");
		else
			clk_enable(gpio_fck);
	}

#ifdef CONFIG_ARCH_OMAP15XX
	if (cpu_is_omap15xx()) {
		printk(KERN_INFO "OMAP1510 GPIO hardware\n");
		gpio_bank_count = 2;
		gpio_bank = gpio_bank_1510;
	}
#endif
#if defined(CONFIG_ARCH_OMAP16XX)
	if (cpu_is_omap16xx()) {
		u32 rev;

		gpio_bank_count = 5;
		gpio_bank = gpio_bank_1610;
		rev = omap_readw(gpio_bank[1].base + OMAP1610_GPIO_REVISION);
		printk(KERN_INFO "OMAP GPIO hardware version %d.%d\n",
		       (rev >> 4) & 0x0f, rev & 0x0f);
	}
#endif
#ifdef CONFIG_ARCH_OMAP730
	if (cpu_is_omap730()) {
		printk(KERN_INFO "OMAP730 GPIO hardware\n");
		gpio_bank_count = 7;
		gpio_bank = gpio_bank_730;
	}
#endif
#ifdef CONFIG_ARCH_OMAP24XX
	if (cpu_is_omap24xx()) {
		int rev;

		gpio_bank_count = 4;
		gpio_bank = gpio_bank_24xx;
		rev = omap_readl(gpio_bank[0].base + OMAP24XX_GPIO_REVISION);
		printk(KERN_INFO "OMAP24xx GPIO hardware version %d.%d\n",
			(rev >> 4) & 0x0f, rev & 0x0f);
	}
#endif
	for (i = 0; i < gpio_bank_count; i++) {
		int j, gpio_count = 16;

		bank = &gpio_bank[i];
		bank->reserved_map = 0;
		bank->base = IO_ADDRESS(bank->base);
		spin_lock_init(&bank->lock);
		if (bank->method == METHOD_MPUIO) {
			omap_writew(0xFFFF, OMAP_MPUIO_BASE + OMAP_MPUIO_GPIO_MASKIT);
		}
#ifdef CONFIG_ARCH_OMAP15XX
		if (bank->method == METHOD_GPIO_1510) {
			__raw_writew(0xffff, bank->base + OMAP1510_GPIO_INT_MASK);
			__raw_writew(0x0000, bank->base + OMAP1510_GPIO_INT_STATUS);
		}
#endif
#if defined(CONFIG_ARCH_OMAP16XX)
		if (bank->method == METHOD_GPIO_1610) {
			__raw_writew(0x0000, bank->base + OMAP1610_GPIO_IRQENABLE1);
			__raw_writew(0xffff, bank->base + OMAP1610_GPIO_IRQSTATUS1);
			__raw_writew(0x0014, bank->base + OMAP1610_GPIO_SYSCONFIG);
		}
#endif
#ifdef CONFIG_ARCH_OMAP730
		if (bank->method == METHOD_GPIO_730) {
			__raw_writel(0xffffffff, bank->base + OMAP730_GPIO_INT_MASK);
			__raw_writel(0x00000000, bank->base + OMAP730_GPIO_INT_STATUS);

			gpio_count = 32; /* 730 has 32-bit GPIOs */
		}
#endif
#ifdef CONFIG_ARCH_OMAP24XX
		if (bank->method == METHOD_GPIO_24XX) {
			__raw_writel(0x00000000, bank->base + OMAP24XX_GPIO_IRQENABLE1);
			__raw_writel(0xffffffff, bank->base + OMAP24XX_GPIO_IRQSTATUS1);

			gpio_count = 32;
		}
#endif
		for (j = bank->virtual_irq_start;
		     j < bank->virtual_irq_start + gpio_count; j++) {
			if (bank->method == METHOD_MPUIO)
				set_irq_chip(j, &mpuio_irq_chip);
			else
				set_irq_chip(j, &gpio_irq_chip);
			set_irq_handler(j, handle_simple_irq);
			set_irq_flags(j, IRQF_VALID);
		}
		set_irq_chained_handler(bank->irq, gpio_irq_handler);
		set_irq_data(bank->irq, bank);
	}

	/* Enable system clock for GPIO module.
	 * The CAM_CLK_CTRL *is* really the right place. */
	if (cpu_is_omap16xx())
		omap_writel(omap_readl(ULPD_CAM_CLK_CTRL) | 0x04, ULPD_CAM_CLK_CTRL);

	return 0;
}

#if defined (CONFIG_ARCH_OMAP16XX) || defined (CONFIG_ARCH_OMAP24XX)
static int omap_gpio_suspend(struct sys_device *dev, pm_message_t mesg)
{
	int i;

	if (!cpu_is_omap24xx() && !cpu_is_omap16xx())
		return 0;

	for (i = 0; i < gpio_bank_count; i++) {
		struct gpio_bank *bank = &gpio_bank[i];
		void __iomem *wake_status;
		void __iomem *wake_clear;
		void __iomem *wake_set;

		switch (bank->method) {
		case METHOD_GPIO_1610:
			wake_status = bank->base + OMAP1610_GPIO_WAKEUPENABLE;
			wake_clear = bank->base + OMAP1610_GPIO_CLEAR_WAKEUPENA;
			wake_set = bank->base + OMAP1610_GPIO_SET_WAKEUPENA;
			break;
		case METHOD_GPIO_24XX:
			wake_status = bank->base + OMAP24XX_GPIO_SETWKUENA;
			wake_clear = bank->base + OMAP24XX_GPIO_CLEARWKUENA;
			wake_set = bank->base + OMAP24XX_GPIO_SETWKUENA;
			break;
		default:
			continue;
		}

		spin_lock(&bank->lock);
		bank->saved_wakeup = __raw_readl(wake_status);
		__raw_writel(0xffffffff, wake_clear);
		__raw_writel(bank->suspend_wakeup, wake_set);
		spin_unlock(&bank->lock);
	}

	return 0;
}

static int omap_gpio_resume(struct sys_device *dev)
{
	int i;

	if (!cpu_is_omap24xx() && !cpu_is_omap16xx())
		return 0;

	for (i = 0; i < gpio_bank_count; i++) {
		struct gpio_bank *bank = &gpio_bank[i];
		void __iomem *wake_clear;
		void __iomem *wake_set;

		switch (bank->method) {
		case METHOD_GPIO_1610:
			wake_clear = bank->base + OMAP1610_GPIO_CLEAR_WAKEUPENA;
			wake_set = bank->base + OMAP1610_GPIO_SET_WAKEUPENA;
			break;
		case METHOD_GPIO_24XX:
			wake_clear = bank->base + OMAP24XX_GPIO_CLEARWKUENA;
			wake_set = bank->base + OMAP24XX_GPIO_SETWKUENA;
			break;
		default:
			continue;
		}

		spin_lock(&bank->lock);
		__raw_writel(0xffffffff, wake_clear);
		__raw_writel(bank->saved_wakeup, wake_set);
		spin_unlock(&bank->lock);
	}

	return 0;
}

static struct sysdev_class omap_gpio_sysclass = {
	set_kset_name("gpio"),
	.suspend	= omap_gpio_suspend,
	.resume		= omap_gpio_resume,
};

static struct sys_device omap_gpio_device = {
	.id		= 0,
	.cls		= &omap_gpio_sysclass,
};
#endif

/*
 * This may get called early from board specific init
 * for boards that have interrupts routed via FPGA.
 */
int omap_gpio_init(void)
{
	if (!initialized)
		return _omap_gpio_init();
	else
		return 0;
}

static int __init omap_gpio_sysinit(void)
{
	int ret = 0;

	if (!initialized)
		ret = _omap_gpio_init();

#if defined(CONFIG_ARCH_OMAP16XX) || defined(CONFIG_ARCH_OMAP24XX)
	if (cpu_is_omap16xx() || cpu_is_omap24xx()) {
		if (ret == 0) {
			ret = sysdev_class_register(&omap_gpio_sysclass);
			if (ret == 0)
				ret = sysdev_register(&omap_gpio_device);
		}
	}
#endif

	return ret;
}

EXPORT_SYMBOL(omap_request_gpio);
EXPORT_SYMBOL(omap_free_gpio);
EXPORT_SYMBOL(omap_set_gpio_direction);
EXPORT_SYMBOL(omap_set_gpio_dataout);
EXPORT_SYMBOL(omap_get_gpio_datain);

arch_initcall(omap_gpio_sysinit);
