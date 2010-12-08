/*
 *  linux/arch/arm/plat-omap/gpio.c
 *
 * Support functions for OMAP GPIO
 *
 * Copyright (C) 2003-2005 Nokia Corporation
 * Written by Juha Yrjölä <juha.yrjola@nokia.com>
 *
 * Copyright (C) 2009 Texas Instruments
 * Added OMAP4 support - Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/sysdev.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>

#include <mach/hardware.h>
#include <asm/irq.h>
#include <mach/irqs.h>
#include <mach/gpio.h>
#include <asm/mach/irq.h>
#include <plat/powerdomain.h>

/*
 * OMAP1510 GPIO registers
 */
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
 * OMAP7XX specific GPIO registers
 */
#define OMAP7XX_GPIO_DATA_INPUT		0x00
#define OMAP7XX_GPIO_DATA_OUTPUT	0x04
#define OMAP7XX_GPIO_DIR_CONTROL	0x08
#define OMAP7XX_GPIO_INT_CONTROL	0x0c
#define OMAP7XX_GPIO_INT_MASK		0x10
#define OMAP7XX_GPIO_INT_STATUS		0x14

/*
 * omap2+ specific GPIO registers
 */
#define OMAP24XX_GPIO_REVISION		0x0000
#define OMAP24XX_GPIO_IRQSTATUS1	0x0018
#define OMAP24XX_GPIO_IRQSTATUS2	0x0028
#define OMAP24XX_GPIO_IRQENABLE2	0x002c
#define OMAP24XX_GPIO_IRQENABLE1	0x001c
#define OMAP24XX_GPIO_WAKE_EN		0x0020
#define OMAP24XX_GPIO_CTRL		0x0030
#define OMAP24XX_GPIO_OE		0x0034
#define OMAP24XX_GPIO_DATAIN		0x0038
#define OMAP24XX_GPIO_DATAOUT		0x003c
#define OMAP24XX_GPIO_LEVELDETECT0	0x0040
#define OMAP24XX_GPIO_LEVELDETECT1	0x0044
#define OMAP24XX_GPIO_RISINGDETECT	0x0048
#define OMAP24XX_GPIO_FALLINGDETECT	0x004c
#define OMAP24XX_GPIO_DEBOUNCE_EN	0x0050
#define OMAP24XX_GPIO_DEBOUNCE_VAL	0x0054
#define OMAP24XX_GPIO_CLEARIRQENABLE1	0x0060
#define OMAP24XX_GPIO_SETIRQENABLE1	0x0064
#define OMAP24XX_GPIO_CLEARWKUENA	0x0080
#define OMAP24XX_GPIO_SETWKUENA		0x0084
#define OMAP24XX_GPIO_CLEARDATAOUT	0x0090
#define OMAP24XX_GPIO_SETDATAOUT	0x0094

#define OMAP4_GPIO_REVISION		0x0000
#define OMAP4_GPIO_EOI			0x0020
#define OMAP4_GPIO_IRQSTATUSRAW0	0x0024
#define OMAP4_GPIO_IRQSTATUSRAW1	0x0028
#define OMAP4_GPIO_IRQSTATUS0		0x002c
#define OMAP4_GPIO_IRQSTATUS1		0x0030
#define OMAP4_GPIO_IRQSTATUSSET0	0x0034
#define OMAP4_GPIO_IRQSTATUSSET1	0x0038
#define OMAP4_GPIO_IRQSTATUSCLR0	0x003c
#define OMAP4_GPIO_IRQSTATUSCLR1	0x0040
#define OMAP4_GPIO_IRQWAKEN0		0x0044
#define OMAP4_GPIO_IRQWAKEN1		0x0048
#define OMAP4_GPIO_IRQENABLE1		0x011c
#define OMAP4_GPIO_WAKE_EN		0x0120
#define OMAP4_GPIO_IRQSTATUS2		0x0128
#define OMAP4_GPIO_IRQENABLE2		0x012c
#define OMAP4_GPIO_CTRL			0x0130
#define OMAP4_GPIO_OE			0x0134
#define OMAP4_GPIO_DATAIN		0x0138
#define OMAP4_GPIO_DATAOUT		0x013c
#define OMAP4_GPIO_LEVELDETECT0		0x0140
#define OMAP4_GPIO_LEVELDETECT1		0x0144
#define OMAP4_GPIO_RISINGDETECT		0x0148
#define OMAP4_GPIO_FALLINGDETECT	0x014c
#define OMAP4_GPIO_DEBOUNCENABLE	0x0150
#define OMAP4_GPIO_DEBOUNCINGTIME	0x0154
#define OMAP4_GPIO_CLEARIRQENABLE1	0x0160
#define OMAP4_GPIO_SETIRQENABLE1	0x0164
#define OMAP4_GPIO_CLEARWKUENA		0x0180
#define OMAP4_GPIO_SETWKUENA		0x0184
#define OMAP4_GPIO_CLEARDATAOUT		0x0190
#define OMAP4_GPIO_SETDATAOUT		0x0194

struct gpio_bank {
	unsigned long pbase;
	void __iomem *base;
	u16 irq;
	u16 virtual_irq_start;
	int method;
#if defined(CONFIG_ARCH_OMAP16XX) || defined(CONFIG_ARCH_OMAP2PLUS)
	u32 suspend_wakeup;
	u32 saved_wakeup;
#endif
	u32 non_wakeup_gpios;
	u32 enabled_non_wakeup_gpios;

	u32 saved_datain;
	u32 saved_fallingdetect;
	u32 saved_risingdetect;
	u32 level_mask;
	u32 toggle_mask;
	spinlock_t lock;
	struct gpio_chip chip;
	struct clk *dbck;
	u32 mod_usage;
	u32 dbck_enable_mask;
	struct device *dev;
	bool dbck_flag;
};

#ifdef CONFIG_ARCH_OMAP3
struct omap3_gpio_regs {
	u32 irqenable1;
	u32 irqenable2;
	u32 wake_en;
	u32 ctrl;
	u32 oe;
	u32 leveldetect0;
	u32 leveldetect1;
	u32 risingdetect;
	u32 fallingdetect;
	u32 dataout;
};

static struct omap3_gpio_regs gpio_context[OMAP34XX_NR_GPIOS];
#endif

/*
 * TODO: Cleanup gpio_bank usage as it is having information
 * related to all instances of the device
 */
static struct gpio_bank *gpio_bank;

static int bank_width;

/* TODO: Analyze removing gpio_bank_count usage from driver code */
int gpio_bank_count;

static inline struct gpio_bank *get_gpio_bank(int gpio)
{
	if (cpu_is_omap15xx()) {
		if (OMAP_GPIO_IS_MPUIO(gpio))
			return &gpio_bank[0];
		return &gpio_bank[1];
	}
	if (cpu_is_omap16xx()) {
		if (OMAP_GPIO_IS_MPUIO(gpio))
			return &gpio_bank[0];
		return &gpio_bank[1 + (gpio >> 4)];
	}
	if (cpu_is_omap7xx()) {
		if (OMAP_GPIO_IS_MPUIO(gpio))
			return &gpio_bank[0];
		return &gpio_bank[1 + (gpio >> 5)];
	}
	if (cpu_is_omap24xx())
		return &gpio_bank[gpio >> 5];
	if (cpu_is_omap34xx() || cpu_is_omap44xx())
		return &gpio_bank[gpio >> 5];
	BUG();
	return NULL;
}

static inline int get_gpio_index(int gpio)
{
	if (cpu_is_omap7xx())
		return gpio & 0x1f;
	if (cpu_is_omap24xx())
		return gpio & 0x1f;
	if (cpu_is_omap34xx() || cpu_is_omap44xx())
		return gpio & 0x1f;
	return gpio & 0x0f;
}

static inline int gpio_valid(int gpio)
{
	if (gpio < 0)
		return -1;
	if (cpu_class_is_omap1() && OMAP_GPIO_IS_MPUIO(gpio)) {
		if (gpio >= OMAP_MAX_GPIO_LINES + 16)
			return -1;
		return 0;
	}
	if (cpu_is_omap15xx() && gpio < 16)
		return 0;
	if ((cpu_is_omap16xx()) && gpio < 64)
		return 0;
	if (cpu_is_omap7xx() && gpio < 192)
		return 0;
	if (cpu_is_omap2420() && gpio < 128)
		return 0;
	if (cpu_is_omap2430() && gpio < 160)
		return 0;
	if ((cpu_is_omap34xx() || cpu_is_omap44xx()) && gpio < 192)
		return 0;
	return -1;
}

static int check_gpio(int gpio)
{
	if (unlikely(gpio_valid(gpio) < 0)) {
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
#ifdef CONFIG_ARCH_OMAP1
	case METHOD_MPUIO:
		reg += OMAP_MPUIO_IO_CNTL;
		break;
#endif
#ifdef CONFIG_ARCH_OMAP15XX
	case METHOD_GPIO_1510:
		reg += OMAP1510_GPIO_DIR_CONTROL;
		break;
#endif
#ifdef CONFIG_ARCH_OMAP16XX
	case METHOD_GPIO_1610:
		reg += OMAP1610_GPIO_DIRECTION;
		break;
#endif
#if defined(CONFIG_ARCH_OMAP730) || defined(CONFIG_ARCH_OMAP850)
	case METHOD_GPIO_7XX:
		reg += OMAP7XX_GPIO_DIR_CONTROL;
		break;
#endif
#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3)
	case METHOD_GPIO_24XX:
		reg += OMAP24XX_GPIO_OE;
		break;
#endif
#if defined(CONFIG_ARCH_OMAP4)
	case METHOD_GPIO_44XX:
		reg += OMAP4_GPIO_OE;
		break;
#endif
	default:
		WARN_ON(1);
		return;
	}
	l = __raw_readl(reg);
	if (is_input)
		l |= 1 << gpio;
	else
		l &= ~(1 << gpio);
	__raw_writel(l, reg);
}

static void _set_gpio_dataout(struct gpio_bank *bank, int gpio, int enable)
{
	void __iomem *reg = bank->base;
	u32 l = 0;

	switch (bank->method) {
#ifdef CONFIG_ARCH_OMAP1
	case METHOD_MPUIO:
		reg += OMAP_MPUIO_OUTPUT;
		l = __raw_readl(reg);
		if (enable)
			l |= 1 << gpio;
		else
			l &= ~(1 << gpio);
		break;
#endif
#ifdef CONFIG_ARCH_OMAP15XX
	case METHOD_GPIO_1510:
		reg += OMAP1510_GPIO_DATA_OUTPUT;
		l = __raw_readl(reg);
		if (enable)
			l |= 1 << gpio;
		else
			l &= ~(1 << gpio);
		break;
#endif
#ifdef CONFIG_ARCH_OMAP16XX
	case METHOD_GPIO_1610:
		if (enable)
			reg += OMAP1610_GPIO_SET_DATAOUT;
		else
			reg += OMAP1610_GPIO_CLEAR_DATAOUT;
		l = 1 << gpio;
		break;
#endif
#if defined(CONFIG_ARCH_OMAP730) || defined(CONFIG_ARCH_OMAP850)
	case METHOD_GPIO_7XX:
		reg += OMAP7XX_GPIO_DATA_OUTPUT;
		l = __raw_readl(reg);
		if (enable)
			l |= 1 << gpio;
		else
			l &= ~(1 << gpio);
		break;
#endif
#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3)
	case METHOD_GPIO_24XX:
		if (enable)
			reg += OMAP24XX_GPIO_SETDATAOUT;
		else
			reg += OMAP24XX_GPIO_CLEARDATAOUT;
		l = 1 << gpio;
		break;
#endif
#ifdef CONFIG_ARCH_OMAP4
	case METHOD_GPIO_44XX:
		if (enable)
			reg += OMAP4_GPIO_SETDATAOUT;
		else
			reg += OMAP4_GPIO_CLEARDATAOUT;
		l = 1 << gpio;
		break;
#endif
	default:
		WARN_ON(1);
		return;
	}
	__raw_writel(l, reg);
}

static int _get_gpio_datain(struct gpio_bank *bank, int gpio)
{
	void __iomem *reg;

	if (check_gpio(gpio) < 0)
		return -EINVAL;
	reg = bank->base;
	switch (bank->method) {
#ifdef CONFIG_ARCH_OMAP1
	case METHOD_MPUIO:
		reg += OMAP_MPUIO_INPUT_LATCH;
		break;
#endif
#ifdef CONFIG_ARCH_OMAP15XX
	case METHOD_GPIO_1510:
		reg += OMAP1510_GPIO_DATA_INPUT;
		break;
#endif
#ifdef CONFIG_ARCH_OMAP16XX
	case METHOD_GPIO_1610:
		reg += OMAP1610_GPIO_DATAIN;
		break;
#endif
#if defined(CONFIG_ARCH_OMAP730) || defined(CONFIG_ARCH_OMAP850)
	case METHOD_GPIO_7XX:
		reg += OMAP7XX_GPIO_DATA_INPUT;
		break;
#endif
#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3)
	case METHOD_GPIO_24XX:
		reg += OMAP24XX_GPIO_DATAIN;
		break;
#endif
#ifdef CONFIG_ARCH_OMAP4
	case METHOD_GPIO_44XX:
		reg += OMAP4_GPIO_DATAIN;
		break;
#endif
	default:
		return -EINVAL;
	}
	return (__raw_readl(reg)
			& (1 << get_gpio_index(gpio))) != 0;
}

static int _get_gpio_dataout(struct gpio_bank *bank, int gpio)
{
	void __iomem *reg;

	if (check_gpio(gpio) < 0)
		return -EINVAL;
	reg = bank->base;

	switch (bank->method) {
#ifdef CONFIG_ARCH_OMAP1
	case METHOD_MPUIO:
		reg += OMAP_MPUIO_OUTPUT;
		break;
#endif
#ifdef CONFIG_ARCH_OMAP15XX
	case METHOD_GPIO_1510:
		reg += OMAP1510_GPIO_DATA_OUTPUT;
		break;
#endif
#ifdef CONFIG_ARCH_OMAP16XX
	case METHOD_GPIO_1610:
		reg += OMAP1610_GPIO_DATAOUT;
		break;
#endif
#if defined(CONFIG_ARCH_OMAP730) || defined(CONFIG_ARCH_OMAP850)
	case METHOD_GPIO_7XX:
		reg += OMAP7XX_GPIO_DATA_OUTPUT;
		break;
#endif
#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3)
	case METHOD_GPIO_24XX:
		reg += OMAP24XX_GPIO_DATAOUT;
		break;
#endif
#ifdef CONFIG_ARCH_OMAP4
	case METHOD_GPIO_44XX:
		reg += OMAP4_GPIO_DATAOUT;
		break;
#endif
	default:
		return -EINVAL;
	}

	return (__raw_readl(reg) & (1 << get_gpio_index(gpio))) != 0;
}

#define MOD_REG_BIT(reg, bit_mask, set)	\
do {	\
	int l = __raw_readl(base + reg); \
	if (set) l |= bit_mask; \
	else l &= ~bit_mask; \
	__raw_writel(l, base + reg); \
} while(0)

/**
 * _set_gpio_debounce - low level gpio debounce time
 * @bank: the gpio bank we're acting upon
 * @gpio: the gpio number on this @gpio
 * @debounce: debounce time to use
 *
 * OMAP's debounce time is in 31us steps so we need
 * to convert and round up to the closest unit.
 */
static void _set_gpio_debounce(struct gpio_bank *bank, unsigned gpio,
		unsigned debounce)
{
	void __iomem		*reg = bank->base;
	u32			val;
	u32			l;

	if (!bank->dbck_flag)
		return;

	if (debounce < 32)
		debounce = 0x01;
	else if (debounce > 7936)
		debounce = 0xff;
	else
		debounce = (debounce / 0x1f) - 1;

	l = 1 << get_gpio_index(gpio);

	if (bank->method == METHOD_GPIO_44XX)
		reg += OMAP4_GPIO_DEBOUNCINGTIME;
	else
		reg += OMAP24XX_GPIO_DEBOUNCE_VAL;

	__raw_writel(debounce, reg);

	reg = bank->base;
	if (bank->method == METHOD_GPIO_44XX)
		reg += OMAP4_GPIO_DEBOUNCENABLE;
	else
		reg += OMAP24XX_GPIO_DEBOUNCE_EN;

	val = __raw_readl(reg);

	if (debounce) {
		val |= l;
		clk_enable(bank->dbck);
	} else {
		val &= ~l;
		clk_disable(bank->dbck);
	}
	bank->dbck_enable_mask = val;

	__raw_writel(val, reg);
}

#ifdef CONFIG_ARCH_OMAP2PLUS
static inline void set_24xx_gpio_triggering(struct gpio_bank *bank, int gpio,
						int trigger)
{
	void __iomem *base = bank->base;
	u32 gpio_bit = 1 << gpio;
	u32 val;

	if (cpu_is_omap44xx()) {
		MOD_REG_BIT(OMAP4_GPIO_LEVELDETECT0, gpio_bit,
			trigger & IRQ_TYPE_LEVEL_LOW);
		MOD_REG_BIT(OMAP4_GPIO_LEVELDETECT1, gpio_bit,
			trigger & IRQ_TYPE_LEVEL_HIGH);
		MOD_REG_BIT(OMAP4_GPIO_RISINGDETECT, gpio_bit,
			trigger & IRQ_TYPE_EDGE_RISING);
		MOD_REG_BIT(OMAP4_GPIO_FALLINGDETECT, gpio_bit,
			trigger & IRQ_TYPE_EDGE_FALLING);
	} else {
		MOD_REG_BIT(OMAP24XX_GPIO_LEVELDETECT0, gpio_bit,
			trigger & IRQ_TYPE_LEVEL_LOW);
		MOD_REG_BIT(OMAP24XX_GPIO_LEVELDETECT1, gpio_bit,
			trigger & IRQ_TYPE_LEVEL_HIGH);
		MOD_REG_BIT(OMAP24XX_GPIO_RISINGDETECT, gpio_bit,
			trigger & IRQ_TYPE_EDGE_RISING);
		MOD_REG_BIT(OMAP24XX_GPIO_FALLINGDETECT, gpio_bit,
			trigger & IRQ_TYPE_EDGE_FALLING);
	}
	if (likely(!(bank->non_wakeup_gpios & gpio_bit))) {
		if (cpu_is_omap44xx()) {
			if (trigger != 0)
				__raw_writel(1 << gpio, bank->base+
						OMAP4_GPIO_IRQWAKEN0);
			else {
				val = __raw_readl(bank->base +
							OMAP4_GPIO_IRQWAKEN0);
				__raw_writel(val & (~(1 << gpio)), bank->base +
							 OMAP4_GPIO_IRQWAKEN0);
			}
		} else {
			/*
			 * GPIO wakeup request can only be generated on edge
			 * transitions
			 */
			if (trigger & IRQ_TYPE_EDGE_BOTH)
				__raw_writel(1 << gpio, bank->base
					+ OMAP24XX_GPIO_SETWKUENA);
			else
				__raw_writel(1 << gpio, bank->base
					+ OMAP24XX_GPIO_CLEARWKUENA);
		}
	}
	/* This part needs to be executed always for OMAP34xx */
	if (cpu_is_omap34xx() || (bank->non_wakeup_gpios & gpio_bit)) {
		/*
		 * Log the edge gpio and manually trigger the IRQ
		 * after resume if the input level changes
		 * to avoid irq lost during PER RET/OFF mode
		 * Applies for omap2 non-wakeup gpio and all omap3 gpios
		 */
		if (trigger & IRQ_TYPE_EDGE_BOTH)
			bank->enabled_non_wakeup_gpios |= gpio_bit;
		else
			bank->enabled_non_wakeup_gpios &= ~gpio_bit;
	}

	if (cpu_is_omap44xx()) {
		bank->level_mask =
			__raw_readl(bank->base + OMAP4_GPIO_LEVELDETECT0) |
			__raw_readl(bank->base + OMAP4_GPIO_LEVELDETECT1);
	} else {
		bank->level_mask =
			__raw_readl(bank->base + OMAP24XX_GPIO_LEVELDETECT0) |
			__raw_readl(bank->base + OMAP24XX_GPIO_LEVELDETECT1);
	}
}
#endif

#ifdef CONFIG_ARCH_OMAP1
/*
 * This only applies to chips that can't do both rising and falling edge
 * detection at once.  For all other chips, this function is a noop.
 */
static void _toggle_gpio_edge_triggering(struct gpio_bank *bank, int gpio)
{
	void __iomem *reg = bank->base;
	u32 l = 0;

	switch (bank->method) {
	case METHOD_MPUIO:
		reg += OMAP_MPUIO_GPIO_INT_EDGE;
		break;
#ifdef CONFIG_ARCH_OMAP15XX
	case METHOD_GPIO_1510:
		reg += OMAP1510_GPIO_INT_CONTROL;
		break;
#endif
#if defined(CONFIG_ARCH_OMAP730) || defined(CONFIG_ARCH_OMAP850)
	case METHOD_GPIO_7XX:
		reg += OMAP7XX_GPIO_INT_CONTROL;
		break;
#endif
	default:
		return;
	}

	l = __raw_readl(reg);
	if ((l >> gpio) & 1)
		l &= ~(1 << gpio);
	else
		l |= 1 << gpio;

	__raw_writel(l, reg);
}
#endif

static int _set_gpio_triggering(struct gpio_bank *bank, int gpio, int trigger)
{
	void __iomem *reg = bank->base;
	u32 l = 0;

	switch (bank->method) {
#ifdef CONFIG_ARCH_OMAP1
	case METHOD_MPUIO:
		reg += OMAP_MPUIO_GPIO_INT_EDGE;
		l = __raw_readl(reg);
		if ((trigger & IRQ_TYPE_SENSE_MASK) == IRQ_TYPE_EDGE_BOTH)
			bank->toggle_mask |= 1 << gpio;
		if (trigger & IRQ_TYPE_EDGE_RISING)
			l |= 1 << gpio;
		else if (trigger & IRQ_TYPE_EDGE_FALLING)
			l &= ~(1 << gpio);
		else
			goto bad;
		break;
#endif
#ifdef CONFIG_ARCH_OMAP15XX
	case METHOD_GPIO_1510:
		reg += OMAP1510_GPIO_INT_CONTROL;
		l = __raw_readl(reg);
		if ((trigger & IRQ_TYPE_SENSE_MASK) == IRQ_TYPE_EDGE_BOTH)
			bank->toggle_mask |= 1 << gpio;
		if (trigger & IRQ_TYPE_EDGE_RISING)
			l |= 1 << gpio;
		else if (trigger & IRQ_TYPE_EDGE_FALLING)
			l &= ~(1 << gpio);
		else
			goto bad;
		break;
#endif
#ifdef CONFIG_ARCH_OMAP16XX
	case METHOD_GPIO_1610:
		if (gpio & 0x08)
			reg += OMAP1610_GPIO_EDGE_CTRL2;
		else
			reg += OMAP1610_GPIO_EDGE_CTRL1;
		gpio &= 0x07;
		l = __raw_readl(reg);
		l &= ~(3 << (gpio << 1));
		if (trigger & IRQ_TYPE_EDGE_RISING)
			l |= 2 << (gpio << 1);
		if (trigger & IRQ_TYPE_EDGE_FALLING)
			l |= 1 << (gpio << 1);
		if (trigger)
			/* Enable wake-up during idle for dynamic tick */
			__raw_writel(1 << gpio, bank->base + OMAP1610_GPIO_SET_WAKEUPENA);
		else
			__raw_writel(1 << gpio, bank->base + OMAP1610_GPIO_CLEAR_WAKEUPENA);
		break;
#endif
#if defined(CONFIG_ARCH_OMAP730) || defined(CONFIG_ARCH_OMAP850)
	case METHOD_GPIO_7XX:
		reg += OMAP7XX_GPIO_INT_CONTROL;
		l = __raw_readl(reg);
		if ((trigger & IRQ_TYPE_SENSE_MASK) == IRQ_TYPE_EDGE_BOTH)
			bank->toggle_mask |= 1 << gpio;
		if (trigger & IRQ_TYPE_EDGE_RISING)
			l |= 1 << gpio;
		else if (trigger & IRQ_TYPE_EDGE_FALLING)
			l &= ~(1 << gpio);
		else
			goto bad;
		break;
#endif
#ifdef CONFIG_ARCH_OMAP2PLUS
	case METHOD_GPIO_24XX:
	case METHOD_GPIO_44XX:
		set_24xx_gpio_triggering(bank, gpio, trigger);
		break;
#endif
	default:
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
	unsigned long flags;

	if (!cpu_class_is_omap2() && irq > IH_MPUIO_BASE)
		gpio = OMAP_MPUIO(irq - IH_MPUIO_BASE);
	else
		gpio = irq - IH_GPIO_BASE;

	if (check_gpio(gpio) < 0)
		return -EINVAL;

	if (type & ~IRQ_TYPE_SENSE_MASK)
		return -EINVAL;

	/* OMAP1 allows only only edge triggering */
	if (!cpu_class_is_omap2()
			&& (type & (IRQ_TYPE_LEVEL_LOW|IRQ_TYPE_LEVEL_HIGH)))
		return -EINVAL;

	bank = get_irq_chip_data(irq);
	spin_lock_irqsave(&bank->lock, flags);
	retval = _set_gpio_triggering(bank, get_gpio_index(gpio), type);
	if (retval == 0) {
		irq_desc[irq].status &= ~IRQ_TYPE_SENSE_MASK;
		irq_desc[irq].status |= type;
	}
	spin_unlock_irqrestore(&bank->lock, flags);

	if (type & (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_LEVEL_HIGH))
		__set_irq_handler_unlocked(irq, handle_level_irq);
	else if (type & (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING))
		__set_irq_handler_unlocked(irq, handle_edge_irq);

	return retval;
}

static void _clear_gpio_irqbank(struct gpio_bank *bank, int gpio_mask)
{
	void __iomem *reg = bank->base;

	switch (bank->method) {
#ifdef CONFIG_ARCH_OMAP1
	case METHOD_MPUIO:
		/* MPUIO irqstatus is reset by reading the status register,
		 * so do nothing here */
		return;
#endif
#ifdef CONFIG_ARCH_OMAP15XX
	case METHOD_GPIO_1510:
		reg += OMAP1510_GPIO_INT_STATUS;
		break;
#endif
#ifdef CONFIG_ARCH_OMAP16XX
	case METHOD_GPIO_1610:
		reg += OMAP1610_GPIO_IRQSTATUS1;
		break;
#endif
#if defined(CONFIG_ARCH_OMAP730) || defined(CONFIG_ARCH_OMAP850)
	case METHOD_GPIO_7XX:
		reg += OMAP7XX_GPIO_INT_STATUS;
		break;
#endif
#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3)
	case METHOD_GPIO_24XX:
		reg += OMAP24XX_GPIO_IRQSTATUS1;
		break;
#endif
#if defined(CONFIG_ARCH_OMAP4)
	case METHOD_GPIO_44XX:
		reg += OMAP4_GPIO_IRQSTATUS0;
		break;
#endif
	default:
		WARN_ON(1);
		return;
	}
	__raw_writel(gpio_mask, reg);

	/* Workaround for clearing DSP GPIO interrupts to allow retention */
	if (cpu_is_omap24xx() || cpu_is_omap34xx())
		reg = bank->base + OMAP24XX_GPIO_IRQSTATUS2;
	else if (cpu_is_omap44xx())
		reg = bank->base + OMAP4_GPIO_IRQSTATUS1;

	if (cpu_is_omap24xx() || cpu_is_omap34xx() || cpu_is_omap44xx()) {
		__raw_writel(gpio_mask, reg);

	/* Flush posted write for the irq status to avoid spurious interrupts */
	__raw_readl(reg);
	}
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
#ifdef CONFIG_ARCH_OMAP1
	case METHOD_MPUIO:
		reg += OMAP_MPUIO_GPIO_MASKIT;
		mask = 0xffff;
		inv = 1;
		break;
#endif
#ifdef CONFIG_ARCH_OMAP15XX
	case METHOD_GPIO_1510:
		reg += OMAP1510_GPIO_INT_MASK;
		mask = 0xffff;
		inv = 1;
		break;
#endif
#ifdef CONFIG_ARCH_OMAP16XX
	case METHOD_GPIO_1610:
		reg += OMAP1610_GPIO_IRQENABLE1;
		mask = 0xffff;
		break;
#endif
#if defined(CONFIG_ARCH_OMAP730) || defined(CONFIG_ARCH_OMAP850)
	case METHOD_GPIO_7XX:
		reg += OMAP7XX_GPIO_INT_MASK;
		mask = 0xffffffff;
		inv = 1;
		break;
#endif
#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3)
	case METHOD_GPIO_24XX:
		reg += OMAP24XX_GPIO_IRQENABLE1;
		mask = 0xffffffff;
		break;
#endif
#if defined(CONFIG_ARCH_OMAP4)
	case METHOD_GPIO_44XX:
		reg += OMAP4_GPIO_IRQSTATUSSET0;
		mask = 0xffffffff;
		break;
#endif
	default:
		WARN_ON(1);
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
#ifdef CONFIG_ARCH_OMAP1
	case METHOD_MPUIO:
		reg += OMAP_MPUIO_GPIO_MASKIT;
		l = __raw_readl(reg);
		if (enable)
			l &= ~(gpio_mask);
		else
			l |= gpio_mask;
		break;
#endif
#ifdef CONFIG_ARCH_OMAP15XX
	case METHOD_GPIO_1510:
		reg += OMAP1510_GPIO_INT_MASK;
		l = __raw_readl(reg);
		if (enable)
			l &= ~(gpio_mask);
		else
			l |= gpio_mask;
		break;
#endif
#ifdef CONFIG_ARCH_OMAP16XX
	case METHOD_GPIO_1610:
		if (enable)
			reg += OMAP1610_GPIO_SET_IRQENABLE1;
		else
			reg += OMAP1610_GPIO_CLEAR_IRQENABLE1;
		l = gpio_mask;
		break;
#endif
#if defined(CONFIG_ARCH_OMAP730) || defined(CONFIG_ARCH_OMAP850)
	case METHOD_GPIO_7XX:
		reg += OMAP7XX_GPIO_INT_MASK;
		l = __raw_readl(reg);
		if (enable)
			l &= ~(gpio_mask);
		else
			l |= gpio_mask;
		break;
#endif
#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3)
	case METHOD_GPIO_24XX:
		if (enable)
			reg += OMAP24XX_GPIO_SETIRQENABLE1;
		else
			reg += OMAP24XX_GPIO_CLEARIRQENABLE1;
		l = gpio_mask;
		break;
#endif
#ifdef CONFIG_ARCH_OMAP4
	case METHOD_GPIO_44XX:
		if (enable)
			reg += OMAP4_GPIO_IRQSTATUSSET0;
		else
			reg += OMAP4_GPIO_IRQSTATUSCLR0;
		l = gpio_mask;
		break;
#endif
	default:
		WARN_ON(1);
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
	unsigned long uninitialized_var(flags);

	switch (bank->method) {
#ifdef CONFIG_ARCH_OMAP16XX
	case METHOD_MPUIO:
	case METHOD_GPIO_1610:
		spin_lock_irqsave(&bank->lock, flags);
		if (enable)
			bank->suspend_wakeup |= (1 << gpio);
		else
			bank->suspend_wakeup &= ~(1 << gpio);
		spin_unlock_irqrestore(&bank->lock, flags);
		return 0;
#endif
#ifdef CONFIG_ARCH_OMAP2PLUS
	case METHOD_GPIO_24XX:
	case METHOD_GPIO_44XX:
		if (bank->non_wakeup_gpios & (1 << gpio)) {
			printk(KERN_ERR "Unable to modify wakeup on "
					"non-wakeup GPIO%d\n",
					(bank - gpio_bank) * 32 + gpio);
			return -EINVAL;
		}
		spin_lock_irqsave(&bank->lock, flags);
		if (enable)
			bank->suspend_wakeup |= (1 << gpio);
		else
			bank->suspend_wakeup &= ~(1 << gpio);
		spin_unlock_irqrestore(&bank->lock, flags);
		return 0;
#endif
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
	_set_gpio_triggering(bank, get_gpio_index(gpio), IRQ_TYPE_NONE);
}

/* Use disable_irq_wake() and enable_irq_wake() functions from drivers */
static int gpio_wake_enable(unsigned int irq, unsigned int enable)
{
	unsigned int gpio = irq - IH_GPIO_BASE;
	struct gpio_bank *bank;
	int retval;

	if (check_gpio(gpio) < 0)
		return -ENODEV;
	bank = get_irq_chip_data(irq);
	retval = _set_gpio_wakeup(bank, get_gpio_index(gpio), enable);

	return retval;
}

static int omap_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	struct gpio_bank *bank = container_of(chip, struct gpio_bank, chip);
	unsigned long flags;

	spin_lock_irqsave(&bank->lock, flags);

	/* Set trigger to none. You need to enable the desired trigger with
	 * request_irq() or set_irq_type().
	 */
	_set_gpio_triggering(bank, offset, IRQ_TYPE_NONE);

#ifdef CONFIG_ARCH_OMAP15XX
	if (bank->method == METHOD_GPIO_1510) {
		void __iomem *reg;

		/* Claim the pin for MPU */
		reg = bank->base + OMAP1510_GPIO_PIN_CONTROL;
		__raw_writel(__raw_readl(reg) | (1 << offset), reg);
	}
#endif
	if (!cpu_class_is_omap1()) {
		if (!bank->mod_usage) {
			void __iomem *reg = bank->base;
			u32 ctrl;

			if (cpu_is_omap24xx() || cpu_is_omap34xx())
				reg += OMAP24XX_GPIO_CTRL;
			else if (cpu_is_omap44xx())
				reg += OMAP4_GPIO_CTRL;
			ctrl = __raw_readl(reg);
			/* Module is enabled, clocks are not gated */
			ctrl &= 0xFFFFFFFE;
			__raw_writel(ctrl, reg);
		}
		bank->mod_usage |= 1 << offset;
	}
	spin_unlock_irqrestore(&bank->lock, flags);

	return 0;
}

static void omap_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	struct gpio_bank *bank = container_of(chip, struct gpio_bank, chip);
	unsigned long flags;

	spin_lock_irqsave(&bank->lock, flags);
#ifdef CONFIG_ARCH_OMAP16XX
	if (bank->method == METHOD_GPIO_1610) {
		/* Disable wake-up during idle for dynamic tick */
		void __iomem *reg = bank->base + OMAP1610_GPIO_CLEAR_WAKEUPENA;
		__raw_writel(1 << offset, reg);
	}
#endif
#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3)
	if (bank->method == METHOD_GPIO_24XX) {
		/* Disable wake-up during idle for dynamic tick */
		void __iomem *reg = bank->base + OMAP24XX_GPIO_CLEARWKUENA;
		__raw_writel(1 << offset, reg);
	}
#endif
#ifdef CONFIG_ARCH_OMAP4
	if (bank->method == METHOD_GPIO_44XX) {
		/* Disable wake-up during idle for dynamic tick */
		void __iomem *reg = bank->base + OMAP4_GPIO_IRQWAKEN0;
		__raw_writel(1 << offset, reg);
	}
#endif
	if (!cpu_class_is_omap1()) {
		bank->mod_usage &= ~(1 << offset);
		if (!bank->mod_usage) {
			void __iomem *reg = bank->base;
			u32 ctrl;

			if (cpu_is_omap24xx() || cpu_is_omap34xx())
				reg += OMAP24XX_GPIO_CTRL;
			else if (cpu_is_omap44xx())
				reg += OMAP4_GPIO_CTRL;
			ctrl = __raw_readl(reg);
			/* Module is disabled, clocks are gated */
			ctrl |= 1;
			__raw_writel(ctrl, reg);
		}
	}
	_reset_gpio(bank, bank->chip.base + offset);
	spin_unlock_irqrestore(&bank->lock, flags);
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
	unsigned int gpio_irq, gpio_index;
	struct gpio_bank *bank;
	u32 retrigger = 0;
	int unmasked = 0;

	desc->chip->ack(irq);

	bank = get_irq_data(irq);
#ifdef CONFIG_ARCH_OMAP1
	if (bank->method == METHOD_MPUIO)
		isr_reg = bank->base + OMAP_MPUIO_GPIO_INT;
#endif
#ifdef CONFIG_ARCH_OMAP15XX
	if (bank->method == METHOD_GPIO_1510)
		isr_reg = bank->base + OMAP1510_GPIO_INT_STATUS;
#endif
#if defined(CONFIG_ARCH_OMAP16XX)
	if (bank->method == METHOD_GPIO_1610)
		isr_reg = bank->base + OMAP1610_GPIO_IRQSTATUS1;
#endif
#if defined(CONFIG_ARCH_OMAP730) || defined(CONFIG_ARCH_OMAP850)
	if (bank->method == METHOD_GPIO_7XX)
		isr_reg = bank->base + OMAP7XX_GPIO_INT_STATUS;
#endif
#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3)
	if (bank->method == METHOD_GPIO_24XX)
		isr_reg = bank->base + OMAP24XX_GPIO_IRQSTATUS1;
#endif
#if defined(CONFIG_ARCH_OMAP4)
	if (bank->method == METHOD_GPIO_44XX)
		isr_reg = bank->base + OMAP4_GPIO_IRQSTATUS0;
#endif

	if (WARN_ON(!isr_reg))
		goto exit;

	while(1) {
		u32 isr_saved, level_mask = 0;
		u32 enabled;

		enabled = _get_gpio_irqbank_mask(bank);
		isr_saved = isr = __raw_readl(isr_reg) & enabled;

		if (cpu_is_omap15xx() && (bank->method == METHOD_MPUIO))
			isr &= 0x0000ffff;

		if (cpu_class_is_omap2()) {
			level_mask = bank->level_mask & enabled;
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
			gpio_index = get_gpio_index(irq_to_gpio(gpio_irq));

			if (!(isr & 1))
				continue;

#ifdef CONFIG_ARCH_OMAP1
			/*
			 * Some chips can't respond to both rising and falling
			 * at the same time.  If this irq was requested with
			 * both flags, we need to flip the ICR data for the IRQ
			 * to respond to the IRQ for the opposite direction.
			 * This will be indicated in the bank toggle_mask.
			 */
			if (bank->toggle_mask & (1 << gpio_index))
				_toggle_gpio_edge_triggering(bank, gpio_index);
#endif

			generic_handle_irq(gpio_irq);
		}
	}
	/* if bank has any level sensitive GPIO pin interrupt
	configured, we must unmask the bank interrupt only after
	handler(s) are executed in order to avoid spurious bank
	interrupt */
exit:
	if (!unmasked)
		desc->chip->unmask(irq);

}

static void gpio_irq_shutdown(unsigned int irq)
{
	unsigned int gpio = irq - IH_GPIO_BASE;
	struct gpio_bank *bank = get_irq_chip_data(irq);

	_reset_gpio(bank, gpio);
}

static void gpio_ack_irq(unsigned int irq)
{
	unsigned int gpio = irq - IH_GPIO_BASE;
	struct gpio_bank *bank = get_irq_chip_data(irq);

	_clear_gpio_irqstatus(bank, gpio);
}

static void gpio_mask_irq(unsigned int irq)
{
	unsigned int gpio = irq - IH_GPIO_BASE;
	struct gpio_bank *bank = get_irq_chip_data(irq);

	_set_gpio_irqenable(bank, gpio, 0);
	_set_gpio_triggering(bank, get_gpio_index(gpio), IRQ_TYPE_NONE);
}

static void gpio_unmask_irq(unsigned int irq)
{
	unsigned int gpio = irq - IH_GPIO_BASE;
	struct gpio_bank *bank = get_irq_chip_data(irq);
	unsigned int irq_mask = 1 << get_gpio_index(gpio);
	struct irq_desc *desc = irq_to_desc(irq);
	u32 trigger = desc->status & IRQ_TYPE_SENSE_MASK;

	if (trigger)
		_set_gpio_triggering(bank, get_gpio_index(gpio), trigger);

	/* For level-triggered GPIOs, the clearing must be done after
	 * the HW source is cleared, thus after the handler has run */
	if (bank->level_mask & irq_mask) {
		_set_gpio_irqenable(bank, gpio, 0);
		_clear_gpio_irqstatus(bank, gpio);
	}

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

/*---------------------------------------------------------------------*/

#ifdef CONFIG_ARCH_OMAP1

/* MPUIO uses the always-on 32k clock */

static void mpuio_ack_irq(unsigned int irq)
{
	/* The ISR is reset automatically, so do nothing here. */
}

static void mpuio_mask_irq(unsigned int irq)
{
	unsigned int gpio = OMAP_MPUIO(irq - IH_MPUIO_BASE);
	struct gpio_bank *bank = get_irq_chip_data(irq);

	_set_gpio_irqenable(bank, gpio, 0);
}

static void mpuio_unmask_irq(unsigned int irq)
{
	unsigned int gpio = OMAP_MPUIO(irq - IH_MPUIO_BASE);
	struct gpio_bank *bank = get_irq_chip_data(irq);

	_set_gpio_irqenable(bank, gpio, 1);
}

static struct irq_chip mpuio_irq_chip = {
	.name		= "MPUIO",
	.ack		= mpuio_ack_irq,
	.mask		= mpuio_mask_irq,
	.unmask		= mpuio_unmask_irq,
	.set_type	= gpio_irq_type,
#ifdef CONFIG_ARCH_OMAP16XX
	/* REVISIT: assuming only 16xx supports MPUIO wake events */
	.set_wake	= gpio_wake_enable,
#endif
};


#define bank_is_mpuio(bank)	((bank)->method == METHOD_MPUIO)


#ifdef CONFIG_ARCH_OMAP16XX

#include <linux/platform_device.h>

static int omap_mpuio_suspend_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct gpio_bank	*bank = platform_get_drvdata(pdev);
	void __iomem		*mask_reg = bank->base + OMAP_MPUIO_GPIO_MASKIT;
	unsigned long		flags;

	spin_lock_irqsave(&bank->lock, flags);
	bank->saved_wakeup = __raw_readl(mask_reg);
	__raw_writel(0xffff & ~bank->suspend_wakeup, mask_reg);
	spin_unlock_irqrestore(&bank->lock, flags);

	return 0;
}

static int omap_mpuio_resume_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct gpio_bank	*bank = platform_get_drvdata(pdev);
	void __iomem		*mask_reg = bank->base + OMAP_MPUIO_GPIO_MASKIT;
	unsigned long		flags;

	spin_lock_irqsave(&bank->lock, flags);
	__raw_writel(bank->saved_wakeup, mask_reg);
	spin_unlock_irqrestore(&bank->lock, flags);

	return 0;
}

static const struct dev_pm_ops omap_mpuio_dev_pm_ops = {
	.suspend_noirq = omap_mpuio_suspend_noirq,
	.resume_noirq = omap_mpuio_resume_noirq,
};

/* use platform_driver for this, now that there's no longer any
 * point to sys_device (other than not disturbing old code).
 */
static struct platform_driver omap_mpuio_driver = {
	.driver		= {
		.name	= "mpuio",
		.pm	= &omap_mpuio_dev_pm_ops,
	},
};

static struct platform_device omap_mpuio_device = {
	.name		= "mpuio",
	.id		= -1,
	.dev = {
		.driver = &omap_mpuio_driver.driver,
	}
	/* could list the /proc/iomem resources */
};

static inline void mpuio_init(void)
{
	struct gpio_bank *bank = get_gpio_bank(OMAP_MPUIO(0));
	platform_set_drvdata(&omap_mpuio_device, bank);

	if (platform_driver_register(&omap_mpuio_driver) == 0)
		(void) platform_device_register(&omap_mpuio_device);
}

#else
static inline void mpuio_init(void) {}
#endif	/* 16xx */

#else

extern struct irq_chip mpuio_irq_chip;

#define bank_is_mpuio(bank)	0
static inline void mpuio_init(void) {}

#endif

/*---------------------------------------------------------------------*/

/* REVISIT these are stupid implementations!  replace by ones that
 * don't switch on METHOD_* and which mostly avoid spinlocks
 */

static int gpio_input(struct gpio_chip *chip, unsigned offset)
{
	struct gpio_bank *bank;
	unsigned long flags;

	bank = container_of(chip, struct gpio_bank, chip);
	spin_lock_irqsave(&bank->lock, flags);
	_set_gpio_direction(bank, offset, 1);
	spin_unlock_irqrestore(&bank->lock, flags);
	return 0;
}

static int gpio_is_input(struct gpio_bank *bank, int mask)
{
	void __iomem *reg = bank->base;

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
	case METHOD_GPIO_7XX:
		reg += OMAP7XX_GPIO_DIR_CONTROL;
		break;
	case METHOD_GPIO_24XX:
		reg += OMAP24XX_GPIO_OE;
		break;
	case METHOD_GPIO_44XX:
		reg += OMAP4_GPIO_OE;
		break;
	default:
		WARN_ONCE(1, "gpio_is_input: incorrect OMAP GPIO method");
		return -EINVAL;
	}
	return __raw_readl(reg) & mask;
}

static int gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct gpio_bank *bank;
	void __iomem *reg;
	int gpio;
	u32 mask;

	gpio = chip->base + offset;
	bank = get_gpio_bank(gpio);
	reg = bank->base;
	mask = 1 << get_gpio_index(gpio);

	if (gpio_is_input(bank, mask))
		return _get_gpio_datain(bank, gpio);
	else
		return _get_gpio_dataout(bank, gpio);
}

static int gpio_output(struct gpio_chip *chip, unsigned offset, int value)
{
	struct gpio_bank *bank;
	unsigned long flags;

	bank = container_of(chip, struct gpio_bank, chip);
	spin_lock_irqsave(&bank->lock, flags);
	_set_gpio_dataout(bank, offset, value);
	_set_gpio_direction(bank, offset, 0);
	spin_unlock_irqrestore(&bank->lock, flags);
	return 0;
}

static int gpio_debounce(struct gpio_chip *chip, unsigned offset,
		unsigned debounce)
{
	struct gpio_bank *bank;
	unsigned long flags;

	bank = container_of(chip, struct gpio_bank, chip);

	if (!bank->dbck) {
		bank->dbck = clk_get(bank->dev, "dbclk");
		if (IS_ERR(bank->dbck))
			dev_err(bank->dev, "Could not get gpio dbck\n");
	}

	spin_lock_irqsave(&bank->lock, flags);
	_set_gpio_debounce(bank, offset, debounce);
	spin_unlock_irqrestore(&bank->lock, flags);

	return 0;
}

static void gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct gpio_bank *bank;
	unsigned long flags;

	bank = container_of(chip, struct gpio_bank, chip);
	spin_lock_irqsave(&bank->lock, flags);
	_set_gpio_dataout(bank, offset, value);
	spin_unlock_irqrestore(&bank->lock, flags);
}

static int gpio_2irq(struct gpio_chip *chip, unsigned offset)
{
	struct gpio_bank *bank;

	bank = container_of(chip, struct gpio_bank, chip);
	return bank->virtual_irq_start + offset;
}

/*---------------------------------------------------------------------*/

static void __init omap_gpio_show_rev(struct gpio_bank *bank)
{
	u32 rev;

	if (cpu_is_omap16xx() && !(bank->method != METHOD_MPUIO))
		rev = __raw_readw(bank->base + OMAP1610_GPIO_REVISION);
	else if (cpu_is_omap24xx() || cpu_is_omap34xx())
		rev = __raw_readl(bank->base + OMAP24XX_GPIO_REVISION);
	else if (cpu_is_omap44xx())
		rev = __raw_readl(bank->base + OMAP4_GPIO_REVISION);
	else
		return;

	printk(KERN_INFO "OMAP GPIO hardware version %d.%d\n",
		(rev >> 4) & 0x0f, rev & 0x0f);
}

/* This lock class tells lockdep that GPIO irqs are in a different
 * category than their parents, so it won't report false recursion.
 */
static struct lock_class_key gpio_lock_class;

static inline int init_gpio_info(struct platform_device *pdev)
{
	/* TODO: Analyze removing gpio_bank_count usage from driver code */
	gpio_bank = kzalloc(gpio_bank_count * sizeof(struct gpio_bank),
				GFP_KERNEL);
	if (!gpio_bank) {
		dev_err(&pdev->dev, "Memory alloc failed for gpio_bank\n");
		return -ENOMEM;
	}
	return 0;
}

/* TODO: Cleanup cpu_is_* checks */
static void omap_gpio_mod_init(struct gpio_bank *bank, int id)
{
	if (cpu_class_is_omap2()) {
		if (cpu_is_omap44xx()) {
			__raw_writel(0xffffffff, bank->base +
					OMAP4_GPIO_IRQSTATUSCLR0);
			__raw_writel(0x00000000, bank->base +
					 OMAP4_GPIO_DEBOUNCENABLE);
			/* Initialize interface clk ungated, module enabled */
			__raw_writel(0, bank->base + OMAP4_GPIO_CTRL);
		} else if (cpu_is_omap34xx()) {
			__raw_writel(0x00000000, bank->base +
					OMAP24XX_GPIO_IRQENABLE1);
			__raw_writel(0xffffffff, bank->base +
					OMAP24XX_GPIO_IRQSTATUS1);
			__raw_writel(0x00000000, bank->base +
					OMAP24XX_GPIO_DEBOUNCE_EN);

			/* Initialize interface clk ungated, module enabled */
			__raw_writel(0, bank->base + OMAP24XX_GPIO_CTRL);
		} else if (cpu_is_omap24xx()) {
			static const u32 non_wakeup_gpios[] = {
				0xe203ffc0, 0x08700040
			};
			if (id < ARRAY_SIZE(non_wakeup_gpios))
				bank->non_wakeup_gpios = non_wakeup_gpios[id];
		}
	} else if (cpu_class_is_omap1()) {
		if (bank_is_mpuio(bank))
			__raw_writew(0xffff, bank->base
						+ OMAP_MPUIO_GPIO_MASKIT);
		if (cpu_is_omap15xx() && bank->method == METHOD_GPIO_1510) {
			__raw_writew(0xffff, bank->base
						+ OMAP1510_GPIO_INT_MASK);
			__raw_writew(0x0000, bank->base
						+ OMAP1510_GPIO_INT_STATUS);
		}
		if (cpu_is_omap16xx() && bank->method == METHOD_GPIO_1610) {
			__raw_writew(0x0000, bank->base
						+ OMAP1610_GPIO_IRQENABLE1);
			__raw_writew(0xffff, bank->base
						+ OMAP1610_GPIO_IRQSTATUS1);
			__raw_writew(0x0014, bank->base
						+ OMAP1610_GPIO_SYSCONFIG);

			/*
			 * Enable system clock for GPIO module.
			 * The CAM_CLK_CTRL *is* really the right place.
			 */
			omap_writel(omap_readl(ULPD_CAM_CLK_CTRL) | 0x04,
						ULPD_CAM_CLK_CTRL);
		}
		if (cpu_is_omap7xx() && bank->method == METHOD_GPIO_7XX) {
			__raw_writel(0xffffffff, bank->base
						+ OMAP7XX_GPIO_INT_MASK);
			__raw_writel(0x00000000, bank->base
						+ OMAP7XX_GPIO_INT_STATUS);
		}
	}
}

static void __init omap_gpio_chip_init(struct gpio_bank *bank)
{
	int j;
	static int gpio;

	bank->mod_usage = 0;
	/*
	 * REVISIT eventually switch from OMAP-specific gpio structs
	 * over to the generic ones
	 */
	bank->chip.request = omap_gpio_request;
	bank->chip.free = omap_gpio_free;
	bank->chip.direction_input = gpio_input;
	bank->chip.get = gpio_get;
	bank->chip.direction_output = gpio_output;
	bank->chip.set_debounce = gpio_debounce;
	bank->chip.set = gpio_set;
	bank->chip.to_irq = gpio_2irq;
	if (bank_is_mpuio(bank)) {
		bank->chip.label = "mpuio";
#ifdef CONFIG_ARCH_OMAP16XX
		bank->chip.dev = &omap_mpuio_device.dev;
#endif
		bank->chip.base = OMAP_MPUIO(0);
	} else {
		bank->chip.label = "gpio";
		bank->chip.base = gpio;
		gpio += bank_width;
	}
	bank->chip.ngpio = bank_width;

	gpiochip_add(&bank->chip);

	for (j = bank->virtual_irq_start;
		     j < bank->virtual_irq_start + bank_width; j++) {
		lockdep_set_class(&irq_desc[j].lock, &gpio_lock_class);
		set_irq_chip_data(j, bank);
		if (bank_is_mpuio(bank))
			set_irq_chip(j, &mpuio_irq_chip);
		else
			set_irq_chip(j, &gpio_irq_chip);
		set_irq_handler(j, handle_simple_irq);
		set_irq_flags(j, IRQF_VALID);
	}
	set_irq_chained_handler(bank->irq, gpio_irq_handler);
	set_irq_data(bank->irq, bank);
}

static int __devinit omap_gpio_probe(struct platform_device *pdev)
{
	static int gpio_init_done;
	struct omap_gpio_platform_data *pdata;
	struct resource *res;
	int id;
	struct gpio_bank *bank;

	if (!pdev->dev.platform_data)
		return -EINVAL;

	pdata = pdev->dev.platform_data;

	if (!gpio_init_done) {
		int ret;

		ret = init_gpio_info(pdev);
		if (ret)
			return ret;
	}

	id = pdev->id;
	bank = &gpio_bank[id];

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (unlikely(!res)) {
		dev_err(&pdev->dev, "GPIO Bank %i Invalid IRQ resource\n", id);
		return -ENODEV;
	}

	bank->irq = res->start;
	bank->virtual_irq_start = pdata->virtual_irq_start;
	bank->method = pdata->bank_type;
	bank->dev = &pdev->dev;
	bank->dbck_flag = pdata->dbck_flag;
	bank_width = pdata->bank_width;

	spin_lock_init(&bank->lock);

	/* Static mapping, never released */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(!res)) {
		dev_err(&pdev->dev, "GPIO Bank %i Invalid mem resource\n", id);
		return -ENODEV;
	}

	bank->base = ioremap(res->start, resource_size(res));
	if (!bank->base) {
		dev_err(&pdev->dev, "Could not ioremap gpio bank%i\n", id);
		return -ENOMEM;
	}

	pm_runtime_enable(bank->dev);
	pm_runtime_get_sync(bank->dev);

	omap_gpio_mod_init(bank, id);
	omap_gpio_chip_init(bank);
	omap_gpio_show_rev(bank);

	if (!gpio_init_done)
		gpio_init_done = 1;

	return 0;
}

#if defined(CONFIG_ARCH_OMAP16XX) || defined(CONFIG_ARCH_OMAP2PLUS)
static int omap_gpio_suspend(struct sys_device *dev, pm_message_t mesg)
{
	int i;

	if (!cpu_class_is_omap2() && !cpu_is_omap16xx())
		return 0;

	for (i = 0; i < gpio_bank_count; i++) {
		struct gpio_bank *bank = &gpio_bank[i];
		void __iomem *wake_status;
		void __iomem *wake_clear;
		void __iomem *wake_set;
		unsigned long flags;

		switch (bank->method) {
#ifdef CONFIG_ARCH_OMAP16XX
		case METHOD_GPIO_1610:
			wake_status = bank->base + OMAP1610_GPIO_WAKEUPENABLE;
			wake_clear = bank->base + OMAP1610_GPIO_CLEAR_WAKEUPENA;
			wake_set = bank->base + OMAP1610_GPIO_SET_WAKEUPENA;
			break;
#endif
#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3)
		case METHOD_GPIO_24XX:
			wake_status = bank->base + OMAP24XX_GPIO_WAKE_EN;
			wake_clear = bank->base + OMAP24XX_GPIO_CLEARWKUENA;
			wake_set = bank->base + OMAP24XX_GPIO_SETWKUENA;
			break;
#endif
#ifdef CONFIG_ARCH_OMAP4
		case METHOD_GPIO_44XX:
			wake_status = bank->base + OMAP4_GPIO_IRQWAKEN0;
			wake_clear = bank->base + OMAP4_GPIO_IRQWAKEN0;
			wake_set = bank->base + OMAP4_GPIO_IRQWAKEN0;
			break;
#endif
		default:
			continue;
		}

		spin_lock_irqsave(&bank->lock, flags);
		bank->saved_wakeup = __raw_readl(wake_status);
		__raw_writel(0xffffffff, wake_clear);
		__raw_writel(bank->suspend_wakeup, wake_set);
		spin_unlock_irqrestore(&bank->lock, flags);
	}

	return 0;
}

static int omap_gpio_resume(struct sys_device *dev)
{
	int i;

	if (!cpu_class_is_omap2() && !cpu_is_omap16xx())
		return 0;

	for (i = 0; i < gpio_bank_count; i++) {
		struct gpio_bank *bank = &gpio_bank[i];
		void __iomem *wake_clear;
		void __iomem *wake_set;
		unsigned long flags;

		switch (bank->method) {
#ifdef CONFIG_ARCH_OMAP16XX
		case METHOD_GPIO_1610:
			wake_clear = bank->base + OMAP1610_GPIO_CLEAR_WAKEUPENA;
			wake_set = bank->base + OMAP1610_GPIO_SET_WAKEUPENA;
			break;
#endif
#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3)
		case METHOD_GPIO_24XX:
			wake_clear = bank->base + OMAP24XX_GPIO_CLEARWKUENA;
			wake_set = bank->base + OMAP24XX_GPIO_SETWKUENA;
			break;
#endif
#ifdef CONFIG_ARCH_OMAP4
		case METHOD_GPIO_44XX:
			wake_clear = bank->base + OMAP4_GPIO_IRQWAKEN0;
			wake_set = bank->base + OMAP4_GPIO_IRQWAKEN0;
			break;
#endif
		default:
			continue;
		}

		spin_lock_irqsave(&bank->lock, flags);
		__raw_writel(0xffffffff, wake_clear);
		__raw_writel(bank->saved_wakeup, wake_set);
		spin_unlock_irqrestore(&bank->lock, flags);
	}

	return 0;
}

static struct sysdev_class omap_gpio_sysclass = {
	.name		= "gpio",
	.suspend	= omap_gpio_suspend,
	.resume		= omap_gpio_resume,
};

static struct sys_device omap_gpio_device = {
	.id		= 0,
	.cls		= &omap_gpio_sysclass,
};

#endif

#ifdef CONFIG_ARCH_OMAP2PLUS

static int workaround_enabled;

void omap2_gpio_prepare_for_idle(int power_state)
{
	int i, c = 0;
	int min = 0;

	if (cpu_is_omap34xx())
		min = 1;

	for (i = min; i < gpio_bank_count; i++) {
		struct gpio_bank *bank = &gpio_bank[i];
		u32 l1 = 0, l2 = 0;
		int j;

		for (j = 0; j < hweight_long(bank->dbck_enable_mask); j++)
			clk_disable(bank->dbck);

		if (power_state > PWRDM_POWER_OFF)
			continue;

		/* If going to OFF, remove triggering for all
		 * non-wakeup GPIOs.  Otherwise spurious IRQs will be
		 * generated.  See OMAP2420 Errata item 1.101. */
		if (!(bank->enabled_non_wakeup_gpios))
			continue;

		if (cpu_is_omap24xx() || cpu_is_omap34xx()) {
			bank->saved_datain = __raw_readl(bank->base +
					OMAP24XX_GPIO_DATAIN);
			l1 = __raw_readl(bank->base +
					OMAP24XX_GPIO_FALLINGDETECT);
			l2 = __raw_readl(bank->base +
					OMAP24XX_GPIO_RISINGDETECT);
		}

		if (cpu_is_omap44xx()) {
			bank->saved_datain = __raw_readl(bank->base +
						OMAP4_GPIO_DATAIN);
			l1 = __raw_readl(bank->base +
						OMAP4_GPIO_FALLINGDETECT);
			l2 = __raw_readl(bank->base +
						OMAP4_GPIO_RISINGDETECT);
		}

		bank->saved_fallingdetect = l1;
		bank->saved_risingdetect = l2;
		l1 &= ~bank->enabled_non_wakeup_gpios;
		l2 &= ~bank->enabled_non_wakeup_gpios;

		if (cpu_is_omap24xx() || cpu_is_omap34xx()) {
			__raw_writel(l1, bank->base +
					OMAP24XX_GPIO_FALLINGDETECT);
			__raw_writel(l2, bank->base +
					OMAP24XX_GPIO_RISINGDETECT);
		}

		if (cpu_is_omap44xx()) {
			__raw_writel(l1, bank->base + OMAP4_GPIO_FALLINGDETECT);
			__raw_writel(l2, bank->base + OMAP4_GPIO_RISINGDETECT);
		}

		c++;
	}
	if (!c) {
		workaround_enabled = 0;
		return;
	}
	workaround_enabled = 1;
}

void omap2_gpio_resume_after_idle(void)
{
	int i;
	int min = 0;

	if (cpu_is_omap34xx())
		min = 1;
	for (i = min; i < gpio_bank_count; i++) {
		struct gpio_bank *bank = &gpio_bank[i];
		u32 l = 0, gen, gen0, gen1;
		int j;

		for (j = 0; j < hweight_long(bank->dbck_enable_mask); j++)
			clk_enable(bank->dbck);

		if (!workaround_enabled)
			continue;

		if (!(bank->enabled_non_wakeup_gpios))
			continue;

		if (cpu_is_omap24xx() || cpu_is_omap34xx()) {
			__raw_writel(bank->saved_fallingdetect,
				 bank->base + OMAP24XX_GPIO_FALLINGDETECT);
			__raw_writel(bank->saved_risingdetect,
				 bank->base + OMAP24XX_GPIO_RISINGDETECT);
			l = __raw_readl(bank->base + OMAP24XX_GPIO_DATAIN);
		}

		if (cpu_is_omap44xx()) {
			__raw_writel(bank->saved_fallingdetect,
				 bank->base + OMAP4_GPIO_FALLINGDETECT);
			__raw_writel(bank->saved_risingdetect,
				 bank->base + OMAP4_GPIO_RISINGDETECT);
			l = __raw_readl(bank->base + OMAP4_GPIO_DATAIN);
		}

		/* Check if any of the non-wakeup interrupt GPIOs have changed
		 * state.  If so, generate an IRQ by software.  This is
		 * horribly racy, but it's the best we can do to work around
		 * this silicon bug. */
		l ^= bank->saved_datain;
		l &= bank->enabled_non_wakeup_gpios;

		/*
		 * No need to generate IRQs for the rising edge for gpio IRQs
		 * configured with falling edge only; and vice versa.
		 */
		gen0 = l & bank->saved_fallingdetect;
		gen0 &= bank->saved_datain;

		gen1 = l & bank->saved_risingdetect;
		gen1 &= ~(bank->saved_datain);

		/* FIXME: Consider GPIO IRQs with level detections properly! */
		gen = l & (~(bank->saved_fallingdetect) &
				~(bank->saved_risingdetect));
		/* Consider all GPIO IRQs needed to be updated */
		gen |= gen0 | gen1;

		if (gen) {
			u32 old0, old1;

			if (cpu_is_omap24xx() || cpu_is_omap34xx()) {
				old0 = __raw_readl(bank->base +
					OMAP24XX_GPIO_LEVELDETECT0);
				old1 = __raw_readl(bank->base +
					OMAP24XX_GPIO_LEVELDETECT1);
				__raw_writel(old0 | gen, bank->base +
					OMAP24XX_GPIO_LEVELDETECT0);
				__raw_writel(old1 | gen, bank->base +
					OMAP24XX_GPIO_LEVELDETECT1);
				__raw_writel(old0, bank->base +
					OMAP24XX_GPIO_LEVELDETECT0);
				__raw_writel(old1, bank->base +
					OMAP24XX_GPIO_LEVELDETECT1);
			}

			if (cpu_is_omap44xx()) {
				old0 = __raw_readl(bank->base +
						OMAP4_GPIO_LEVELDETECT0);
				old1 = __raw_readl(bank->base +
						OMAP4_GPIO_LEVELDETECT1);
				__raw_writel(old0 | l, bank->base +
						OMAP4_GPIO_LEVELDETECT0);
				__raw_writel(old1 | l, bank->base +
						OMAP4_GPIO_LEVELDETECT1);
				__raw_writel(old0, bank->base +
						OMAP4_GPIO_LEVELDETECT0);
				__raw_writel(old1, bank->base +
						OMAP4_GPIO_LEVELDETECT1);
			}
		}
	}

}

#endif

#ifdef CONFIG_ARCH_OMAP3
/* save the registers of bank 2-6 */
void omap_gpio_save_context(void)
{
	int i;

	/* saving banks from 2-6 only since GPIO1 is in WKUP */
	for (i = 1; i < gpio_bank_count; i++) {
		struct gpio_bank *bank = &gpio_bank[i];
		gpio_context[i].irqenable1 =
			__raw_readl(bank->base + OMAP24XX_GPIO_IRQENABLE1);
		gpio_context[i].irqenable2 =
			__raw_readl(bank->base + OMAP24XX_GPIO_IRQENABLE2);
		gpio_context[i].wake_en =
			__raw_readl(bank->base + OMAP24XX_GPIO_WAKE_EN);
		gpio_context[i].ctrl =
			__raw_readl(bank->base + OMAP24XX_GPIO_CTRL);
		gpio_context[i].oe =
			__raw_readl(bank->base + OMAP24XX_GPIO_OE);
		gpio_context[i].leveldetect0 =
			__raw_readl(bank->base + OMAP24XX_GPIO_LEVELDETECT0);
		gpio_context[i].leveldetect1 =
			__raw_readl(bank->base + OMAP24XX_GPIO_LEVELDETECT1);
		gpio_context[i].risingdetect =
			__raw_readl(bank->base + OMAP24XX_GPIO_RISINGDETECT);
		gpio_context[i].fallingdetect =
			__raw_readl(bank->base + OMAP24XX_GPIO_FALLINGDETECT);
		gpio_context[i].dataout =
			__raw_readl(bank->base + OMAP24XX_GPIO_DATAOUT);
	}
}

/* restore the required registers of bank 2-6 */
void omap_gpio_restore_context(void)
{
	int i;

	for (i = 1; i < gpio_bank_count; i++) {
		struct gpio_bank *bank = &gpio_bank[i];
		__raw_writel(gpio_context[i].irqenable1,
				bank->base + OMAP24XX_GPIO_IRQENABLE1);
		__raw_writel(gpio_context[i].irqenable2,
				bank->base + OMAP24XX_GPIO_IRQENABLE2);
		__raw_writel(gpio_context[i].wake_en,
				bank->base + OMAP24XX_GPIO_WAKE_EN);
		__raw_writel(gpio_context[i].ctrl,
				bank->base + OMAP24XX_GPIO_CTRL);
		__raw_writel(gpio_context[i].oe,
				bank->base + OMAP24XX_GPIO_OE);
		__raw_writel(gpio_context[i].leveldetect0,
				bank->base + OMAP24XX_GPIO_LEVELDETECT0);
		__raw_writel(gpio_context[i].leveldetect1,
				bank->base + OMAP24XX_GPIO_LEVELDETECT1);
		__raw_writel(gpio_context[i].risingdetect,
				bank->base + OMAP24XX_GPIO_RISINGDETECT);
		__raw_writel(gpio_context[i].fallingdetect,
				bank->base + OMAP24XX_GPIO_FALLINGDETECT);
		__raw_writel(gpio_context[i].dataout,
				bank->base + OMAP24XX_GPIO_DATAOUT);
	}
}
#endif

static struct platform_driver omap_gpio_driver = {
	.probe		= omap_gpio_probe,
	.driver		= {
		.name	= "omap_gpio",
	},
};

/*
 * gpio driver register needs to be done before
 * machine_init functions access gpio APIs.
 * Hence omap_gpio_drv_reg() is a postcore_initcall.
 */
static int __init omap_gpio_drv_reg(void)
{
	return platform_driver_register(&omap_gpio_driver);
}
postcore_initcall(omap_gpio_drv_reg);

static int __init omap_gpio_sysinit(void)
{
	int ret = 0;

	mpuio_init();

#if defined(CONFIG_ARCH_OMAP16XX) || defined(CONFIG_ARCH_OMAP2PLUS)
	if (cpu_is_omap16xx() || cpu_class_is_omap2()) {
		if (ret == 0) {
			ret = sysdev_class_register(&omap_gpio_sysclass);
			if (ret == 0)
				ret = sysdev_register(&omap_gpio_device);
		}
	}
#endif

	return ret;
}

arch_initcall(omap_gpio_sysinit);
