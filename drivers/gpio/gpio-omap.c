/*
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
#include <linux/syscore_ops.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/pm_runtime.h>
#include <linux/pm.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/irqdomain.h>
#include <linux/gpio.h>
#include <linux/platform_data/gpio-omap.h>

#include <asm/mach/irq.h>

#define OFF_MODE	1

static LIST_HEAD(omap_gpio_list);

struct gpio_regs {
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
	u32 debounce;
	u32 debounce_en;
};

struct gpio_bank {
	struct list_head node;
	void __iomem *base;
	u16 irq;
	int irq_base;
	struct irq_domain *domain;
	u32 non_wakeup_gpios;
	u32 enabled_non_wakeup_gpios;
	struct gpio_regs context;
	u32 saved_datain;
	u32 level_mask;
	u32 toggle_mask;
	spinlock_t lock;
	struct gpio_chip chip;
	struct clk *dbck;
	u32 mod_usage;
	u32 dbck_enable_mask;
	bool dbck_enabled;
	struct device *dev;
	bool is_mpuio;
	bool dbck_flag;
	bool loses_context;
	int stride;
	u32 width;
	int context_loss_count;
	int power_mode;
	bool workaround_enabled;

	void (*set_dataout)(struct gpio_bank *bank, int gpio, int enable);
	int (*get_context_loss_count)(struct device *dev);

	struct omap_gpio_reg_offs *regs;
};

#define GPIO_INDEX(bank, gpio) (gpio % bank->width)
#define GPIO_BIT(bank, gpio) (1 << GPIO_INDEX(bank, gpio))
#define GPIO_MOD_CTRL_BIT	BIT(0)

static int irq_to_gpio(struct gpio_bank *bank, unsigned int gpio_irq)
{
	return gpio_irq - bank->irq_base + bank->chip.base;
}

static void _set_gpio_direction(struct gpio_bank *bank, int gpio, int is_input)
{
	void __iomem *reg = bank->base;
	u32 l;

	reg += bank->regs->direction;
	l = __raw_readl(reg);
	if (is_input)
		l |= 1 << gpio;
	else
		l &= ~(1 << gpio);
	__raw_writel(l, reg);
	bank->context.oe = l;
}


/* set data out value using dedicate set/clear register */
static void _set_gpio_dataout_reg(struct gpio_bank *bank, int gpio, int enable)
{
	void __iomem *reg = bank->base;
	u32 l = GPIO_BIT(bank, gpio);

	if (enable) {
		reg += bank->regs->set_dataout;
		bank->context.dataout |= l;
	} else {
		reg += bank->regs->clr_dataout;
		bank->context.dataout &= ~l;
	}

	__raw_writel(l, reg);
}

/* set data out value using mask register */
static void _set_gpio_dataout_mask(struct gpio_bank *bank, int gpio, int enable)
{
	void __iomem *reg = bank->base + bank->regs->dataout;
	u32 gpio_bit = GPIO_BIT(bank, gpio);
	u32 l;

	l = __raw_readl(reg);
	if (enable)
		l |= gpio_bit;
	else
		l &= ~gpio_bit;
	__raw_writel(l, reg);
	bank->context.dataout = l;
}

static int _get_gpio_datain(struct gpio_bank *bank, int offset)
{
	void __iomem *reg = bank->base + bank->regs->datain;

	return (__raw_readl(reg) & (1 << offset)) != 0;
}

static int _get_gpio_dataout(struct gpio_bank *bank, int offset)
{
	void __iomem *reg = bank->base + bank->regs->dataout;

	return (__raw_readl(reg) & (1 << offset)) != 0;
}

static inline void _gpio_rmw(void __iomem *base, u32 reg, u32 mask, bool set)
{
	int l = __raw_readl(base + reg);

	if (set)
		l |= mask;
	else
		l &= ~mask;

	__raw_writel(l, base + reg);
}

static inline void _gpio_dbck_enable(struct gpio_bank *bank)
{
	if (bank->dbck_enable_mask && !bank->dbck_enabled) {
		clk_enable(bank->dbck);
		bank->dbck_enabled = true;

		__raw_writel(bank->dbck_enable_mask,
			     bank->base + bank->regs->debounce_en);
	}
}

static inline void _gpio_dbck_disable(struct gpio_bank *bank)
{
	if (bank->dbck_enable_mask && bank->dbck_enabled) {
		/*
		 * Disable debounce before cutting it's clock. If debounce is
		 * enabled but the clock is not, GPIO module seems to be unable
		 * to detect events and generate interrupts at least on OMAP3.
		 */
		__raw_writel(0, bank->base + bank->regs->debounce_en);

		clk_disable(bank->dbck);
		bank->dbck_enabled = false;
	}
}

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
	void __iomem		*reg;
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

	l = GPIO_BIT(bank, gpio);

	clk_enable(bank->dbck);
	reg = bank->base + bank->regs->debounce;
	__raw_writel(debounce, reg);

	reg = bank->base + bank->regs->debounce_en;
	val = __raw_readl(reg);

	if (debounce)
		val |= l;
	else
		val &= ~l;
	bank->dbck_enable_mask = val;

	__raw_writel(val, reg);
	clk_disable(bank->dbck);
	/*
	 * Enable debounce clock per module.
	 * This call is mandatory because in omap_gpio_request() when
	 * *_runtime_get_sync() is called,  _gpio_dbck_enable() within
	 * runtime callbck fails to turn on dbck because dbck_enable_mask
	 * used within _gpio_dbck_enable() is still not initialized at
	 * that point. Therefore we have to enable dbck here.
	 */
	_gpio_dbck_enable(bank);
	if (bank->dbck_enable_mask) {
		bank->context.debounce = debounce;
		bank->context.debounce_en = val;
	}
}

/**
 * _clear_gpio_debounce - clear debounce settings for a gpio
 * @bank: the gpio bank we're acting upon
 * @gpio: the gpio number on this @gpio
 *
 * If a gpio is using debounce, then clear the debounce enable bit and if
 * this is the only gpio in this bank using debounce, then clear the debounce
 * time too. The debounce clock will also be disabled when calling this function
 * if this is the only gpio in the bank using debounce.
 */
static void _clear_gpio_debounce(struct gpio_bank *bank, unsigned gpio)
{
	u32 gpio_bit = GPIO_BIT(bank, gpio);

	if (!bank->dbck_flag)
		return;

	if (!(bank->dbck_enable_mask & gpio_bit))
		return;

	bank->dbck_enable_mask &= ~gpio_bit;
	bank->context.debounce_en &= ~gpio_bit;
	__raw_writel(bank->context.debounce_en,
		     bank->base + bank->regs->debounce_en);

	if (!bank->dbck_enable_mask) {
		bank->context.debounce = 0;
		__raw_writel(bank->context.debounce, bank->base +
			     bank->regs->debounce);
		clk_disable(bank->dbck);
		bank->dbck_enabled = false;
	}
}

static inline void set_gpio_trigger(struct gpio_bank *bank, int gpio,
						unsigned trigger)
{
	void __iomem *base = bank->base;
	u32 gpio_bit = 1 << gpio;

	_gpio_rmw(base, bank->regs->leveldetect0, gpio_bit,
		  trigger & IRQ_TYPE_LEVEL_LOW);
	_gpio_rmw(base, bank->regs->leveldetect1, gpio_bit,
		  trigger & IRQ_TYPE_LEVEL_HIGH);
	_gpio_rmw(base, bank->regs->risingdetect, gpio_bit,
		  trigger & IRQ_TYPE_EDGE_RISING);
	_gpio_rmw(base, bank->regs->fallingdetect, gpio_bit,
		  trigger & IRQ_TYPE_EDGE_FALLING);

	bank->context.leveldetect0 =
			__raw_readl(bank->base + bank->regs->leveldetect0);
	bank->context.leveldetect1 =
			__raw_readl(bank->base + bank->regs->leveldetect1);
	bank->context.risingdetect =
			__raw_readl(bank->base + bank->regs->risingdetect);
	bank->context.fallingdetect =
			__raw_readl(bank->base + bank->regs->fallingdetect);

	if (likely(!(bank->non_wakeup_gpios & gpio_bit))) {
		_gpio_rmw(base, bank->regs->wkup_en, gpio_bit, trigger != 0);
		bank->context.wake_en =
			__raw_readl(bank->base + bank->regs->wkup_en);
	}

	/* This part needs to be executed always for OMAP{34xx, 44xx} */
	if (!bank->regs->irqctrl) {
		/* On omap24xx proceed only when valid GPIO bit is set */
		if (bank->non_wakeup_gpios) {
			if (!(bank->non_wakeup_gpios & gpio_bit))
				goto exit;
		}

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

exit:
	bank->level_mask =
		__raw_readl(bank->base + bank->regs->leveldetect0) |
		__raw_readl(bank->base + bank->regs->leveldetect1);
}

#ifdef CONFIG_ARCH_OMAP1
/*
 * This only applies to chips that can't do both rising and falling edge
 * detection at once.  For all other chips, this function is a noop.
 */
static void _toggle_gpio_edge_triggering(struct gpio_bank *bank, int gpio)
{
	void __iomem *reg = bank->base;
	u32 l = 0;

	if (!bank->regs->irqctrl)
		return;

	reg += bank->regs->irqctrl;

	l = __raw_readl(reg);
	if ((l >> gpio) & 1)
		l &= ~(1 << gpio);
	else
		l |= 1 << gpio;

	__raw_writel(l, reg);
}
#else
static void _toggle_gpio_edge_triggering(struct gpio_bank *bank, int gpio) {}
#endif

static int _set_gpio_triggering(struct gpio_bank *bank, int gpio,
							unsigned trigger)
{
	void __iomem *reg = bank->base;
	void __iomem *base = bank->base;
	u32 l = 0;

	if (bank->regs->leveldetect0 && bank->regs->wkup_en) {
		set_gpio_trigger(bank, gpio, trigger);
	} else if (bank->regs->irqctrl) {
		reg += bank->regs->irqctrl;

		l = __raw_readl(reg);
		if ((trigger & IRQ_TYPE_SENSE_MASK) == IRQ_TYPE_EDGE_BOTH)
			bank->toggle_mask |= 1 << gpio;
		if (trigger & IRQ_TYPE_EDGE_RISING)
			l |= 1 << gpio;
		else if (trigger & IRQ_TYPE_EDGE_FALLING)
			l &= ~(1 << gpio);
		else
			return -EINVAL;

		__raw_writel(l, reg);
	} else if (bank->regs->edgectrl1) {
		if (gpio & 0x08)
			reg += bank->regs->edgectrl2;
		else
			reg += bank->regs->edgectrl1;

		gpio &= 0x07;
		l = __raw_readl(reg);
		l &= ~(3 << (gpio << 1));
		if (trigger & IRQ_TYPE_EDGE_RISING)
			l |= 2 << (gpio << 1);
		if (trigger & IRQ_TYPE_EDGE_FALLING)
			l |= 1 << (gpio << 1);

		/* Enable wake-up during idle for dynamic tick */
		_gpio_rmw(base, bank->regs->wkup_en, 1 << gpio, trigger);
		bank->context.wake_en =
			__raw_readl(bank->base + bank->regs->wkup_en);
		__raw_writel(l, reg);
	}
	return 0;
}

static int gpio_irq_type(struct irq_data *d, unsigned type)
{
	struct gpio_bank *bank = irq_data_get_irq_chip_data(d);
	unsigned gpio = 0;
	int retval;
	unsigned long flags;

#ifdef CONFIG_ARCH_OMAP1
	if (d->irq > IH_MPUIO_BASE)
		gpio = OMAP_MPUIO(d->irq - IH_MPUIO_BASE);
#endif

	if (!gpio)
		gpio = irq_to_gpio(bank, d->irq);

	if (type & ~IRQ_TYPE_SENSE_MASK)
		return -EINVAL;

	if (!bank->regs->leveldetect0 &&
		(type & (IRQ_TYPE_LEVEL_LOW|IRQ_TYPE_LEVEL_HIGH)))
		return -EINVAL;

	spin_lock_irqsave(&bank->lock, flags);
	retval = _set_gpio_triggering(bank, GPIO_INDEX(bank, gpio), type);
	spin_unlock_irqrestore(&bank->lock, flags);

	if (type & (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_LEVEL_HIGH))
		__irq_set_handler_locked(d->irq, handle_level_irq);
	else if (type & (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING))
		__irq_set_handler_locked(d->irq, handle_edge_irq);

	return retval;
}

static void _clear_gpio_irqbank(struct gpio_bank *bank, int gpio_mask)
{
	void __iomem *reg = bank->base;

	reg += bank->regs->irqstatus;
	__raw_writel(gpio_mask, reg);

	/* Workaround for clearing DSP GPIO interrupts to allow retention */
	if (bank->regs->irqstatus2) {
		reg = bank->base + bank->regs->irqstatus2;
		__raw_writel(gpio_mask, reg);
	}

	/* Flush posted write for the irq status to avoid spurious interrupts */
	__raw_readl(reg);
}

static inline void _clear_gpio_irqstatus(struct gpio_bank *bank, int gpio)
{
	_clear_gpio_irqbank(bank, GPIO_BIT(bank, gpio));
}

static u32 _get_gpio_irqbank_mask(struct gpio_bank *bank)
{
	void __iomem *reg = bank->base;
	u32 l;
	u32 mask = (1 << bank->width) - 1;

	reg += bank->regs->irqenable;
	l = __raw_readl(reg);
	if (bank->regs->irqenable_inv)
		l = ~l;
	l &= mask;
	return l;
}

static void _enable_gpio_irqbank(struct gpio_bank *bank, int gpio_mask)
{
	void __iomem *reg = bank->base;
	u32 l;

	if (bank->regs->set_irqenable) {
		reg += bank->regs->set_irqenable;
		l = gpio_mask;
		bank->context.irqenable1 |= gpio_mask;
	} else {
		reg += bank->regs->irqenable;
		l = __raw_readl(reg);
		if (bank->regs->irqenable_inv)
			l &= ~gpio_mask;
		else
			l |= gpio_mask;
		bank->context.irqenable1 = l;
	}

	__raw_writel(l, reg);
}

static void _disable_gpio_irqbank(struct gpio_bank *bank, int gpio_mask)
{
	void __iomem *reg = bank->base;
	u32 l;

	if (bank->regs->clr_irqenable) {
		reg += bank->regs->clr_irqenable;
		l = gpio_mask;
		bank->context.irqenable1 &= ~gpio_mask;
	} else {
		reg += bank->regs->irqenable;
		l = __raw_readl(reg);
		if (bank->regs->irqenable_inv)
			l |= gpio_mask;
		else
			l &= ~gpio_mask;
		bank->context.irqenable1 = l;
	}

	__raw_writel(l, reg);
}

static inline void _set_gpio_irqenable(struct gpio_bank *bank, int gpio, int enable)
{
	if (enable)
		_enable_gpio_irqbank(bank, GPIO_BIT(bank, gpio));
	else
		_disable_gpio_irqbank(bank, GPIO_BIT(bank, gpio));
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
	u32 gpio_bit = GPIO_BIT(bank, gpio);
	unsigned long flags;

	if (bank->non_wakeup_gpios & gpio_bit) {
		dev_err(bank->dev,
			"Unable to modify wakeup on non-wakeup GPIO%d\n", gpio);
		return -EINVAL;
	}

	spin_lock_irqsave(&bank->lock, flags);
	if (enable)
		bank->context.wake_en |= gpio_bit;
	else
		bank->context.wake_en &= ~gpio_bit;

	__raw_writel(bank->context.wake_en, bank->base + bank->regs->wkup_en);
	spin_unlock_irqrestore(&bank->lock, flags);

	return 0;
}

static void _reset_gpio(struct gpio_bank *bank, int gpio)
{
	_set_gpio_direction(bank, GPIO_INDEX(bank, gpio), 1);
	_set_gpio_irqenable(bank, gpio, 0);
	_clear_gpio_irqstatus(bank, gpio);
	_set_gpio_triggering(bank, GPIO_INDEX(bank, gpio), IRQ_TYPE_NONE);
	_clear_gpio_debounce(bank, gpio);
}

/* Use disable_irq_wake() and enable_irq_wake() functions from drivers */
static int gpio_wake_enable(struct irq_data *d, unsigned int enable)
{
	struct gpio_bank *bank = irq_data_get_irq_chip_data(d);
	unsigned int gpio = irq_to_gpio(bank, d->irq);

	return _set_gpio_wakeup(bank, gpio, enable);
}

static int omap_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	struct gpio_bank *bank = container_of(chip, struct gpio_bank, chip);
	unsigned long flags;

	/*
	 * If this is the first gpio_request for the bank,
	 * enable the bank module.
	 */
	if (!bank->mod_usage)
		pm_runtime_get_sync(bank->dev);

	spin_lock_irqsave(&bank->lock, flags);
	/* Set trigger to none. You need to enable the desired trigger with
	 * request_irq() or set_irq_type().
	 */
	_set_gpio_triggering(bank, offset, IRQ_TYPE_NONE);

	if (bank->regs->pinctrl) {
		void __iomem *reg = bank->base + bank->regs->pinctrl;

		/* Claim the pin for MPU */
		__raw_writel(__raw_readl(reg) | (1 << offset), reg);
	}

	if (bank->regs->ctrl && !bank->mod_usage) {
		void __iomem *reg = bank->base + bank->regs->ctrl;
		u32 ctrl;

		ctrl = __raw_readl(reg);
		/* Module is enabled, clocks are not gated */
		ctrl &= ~GPIO_MOD_CTRL_BIT;
		__raw_writel(ctrl, reg);
		bank->context.ctrl = ctrl;
	}

	bank->mod_usage |= 1 << offset;

	spin_unlock_irqrestore(&bank->lock, flags);

	return 0;
}

static void omap_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	struct gpio_bank *bank = container_of(chip, struct gpio_bank, chip);
	void __iomem *base = bank->base;
	unsigned long flags;

	spin_lock_irqsave(&bank->lock, flags);

	if (bank->regs->wkup_en) {
		/* Disable wake-up during idle for dynamic tick */
		_gpio_rmw(base, bank->regs->wkup_en, 1 << offset, 0);
		bank->context.wake_en =
			__raw_readl(bank->base + bank->regs->wkup_en);
	}

	bank->mod_usage &= ~(1 << offset);

	if (bank->regs->ctrl && !bank->mod_usage) {
		void __iomem *reg = bank->base + bank->regs->ctrl;
		u32 ctrl;

		ctrl = __raw_readl(reg);
		/* Module is disabled, clocks are gated */
		ctrl |= GPIO_MOD_CTRL_BIT;
		__raw_writel(ctrl, reg);
		bank->context.ctrl = ctrl;
	}

	_reset_gpio(bank, bank->chip.base + offset);
	spin_unlock_irqrestore(&bank->lock, flags);

	/*
	 * If this is the last gpio to be freed in the bank,
	 * disable the bank module.
	 */
	if (!bank->mod_usage)
		pm_runtime_put(bank->dev);
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
	int unmasked = 0;
	struct irq_chip *chip = irq_desc_get_chip(desc);

	chained_irq_enter(chip, desc);

	bank = irq_get_handler_data(irq);
	isr_reg = bank->base + bank->regs->irqstatus;
	pm_runtime_get_sync(bank->dev);

	if (WARN_ON(!isr_reg))
		goto exit;

	while(1) {
		u32 isr_saved, level_mask = 0;
		u32 enabled;

		enabled = _get_gpio_irqbank_mask(bank);
		isr_saved = isr = __raw_readl(isr_reg) & enabled;

		if (bank->level_mask)
			level_mask = bank->level_mask & enabled;

		/* clear edge sensitive interrupts before handler(s) are
		called so that we don't miss any interrupt occurred while
		executing them */
		_disable_gpio_irqbank(bank, isr_saved & ~level_mask);
		_clear_gpio_irqbank(bank, isr_saved & ~level_mask);
		_enable_gpio_irqbank(bank, isr_saved & ~level_mask);

		/* if there is only edge sensitive GPIO pin interrupts
		configured, we could unmask GPIO bank interrupt immediately */
		if (!level_mask && !unmasked) {
			unmasked = 1;
			chained_irq_exit(chip, desc);
		}

		if (!isr)
			break;

		gpio_irq = bank->irq_base;
		for (; isr != 0; isr >>= 1, gpio_irq++) {
			int gpio = irq_to_gpio(bank, gpio_irq);

			if (!(isr & 1))
				continue;

			gpio_index = GPIO_INDEX(bank, gpio);

			/*
			 * Some chips can't respond to both rising and falling
			 * at the same time.  If this irq was requested with
			 * both flags, we need to flip the ICR data for the IRQ
			 * to respond to the IRQ for the opposite direction.
			 * This will be indicated in the bank toggle_mask.
			 */
			if (bank->toggle_mask & (1 << gpio_index))
				_toggle_gpio_edge_triggering(bank, gpio_index);

			generic_handle_irq(gpio_irq);
		}
	}
	/* if bank has any level sensitive GPIO pin interrupt
	configured, we must unmask the bank interrupt only after
	handler(s) are executed in order to avoid spurious bank
	interrupt */
exit:
	if (!unmasked)
		chained_irq_exit(chip, desc);
	pm_runtime_put(bank->dev);
}

static void gpio_irq_shutdown(struct irq_data *d)
{
	struct gpio_bank *bank = irq_data_get_irq_chip_data(d);
	unsigned int gpio = irq_to_gpio(bank, d->irq);
	unsigned long flags;

	spin_lock_irqsave(&bank->lock, flags);
	_reset_gpio(bank, gpio);
	spin_unlock_irqrestore(&bank->lock, flags);
}

static void gpio_ack_irq(struct irq_data *d)
{
	struct gpio_bank *bank = irq_data_get_irq_chip_data(d);
	unsigned int gpio = irq_to_gpio(bank, d->irq);

	_clear_gpio_irqstatus(bank, gpio);
}

static void gpio_mask_irq(struct irq_data *d)
{
	struct gpio_bank *bank = irq_data_get_irq_chip_data(d);
	unsigned int gpio = irq_to_gpio(bank, d->irq);
	unsigned long flags;

	spin_lock_irqsave(&bank->lock, flags);
	_set_gpio_irqenable(bank, gpio, 0);
	_set_gpio_triggering(bank, GPIO_INDEX(bank, gpio), IRQ_TYPE_NONE);
	spin_unlock_irqrestore(&bank->lock, flags);
}

static void gpio_unmask_irq(struct irq_data *d)
{
	struct gpio_bank *bank = irq_data_get_irq_chip_data(d);
	unsigned int gpio = irq_to_gpio(bank, d->irq);
	unsigned int irq_mask = GPIO_BIT(bank, gpio);
	u32 trigger = irqd_get_trigger_type(d);
	unsigned long flags;

	spin_lock_irqsave(&bank->lock, flags);
	if (trigger)
		_set_gpio_triggering(bank, GPIO_INDEX(bank, gpio), trigger);

	/* For level-triggered GPIOs, the clearing must be done after
	 * the HW source is cleared, thus after the handler has run */
	if (bank->level_mask & irq_mask) {
		_set_gpio_irqenable(bank, gpio, 0);
		_clear_gpio_irqstatus(bank, gpio);
	}

	_set_gpio_irqenable(bank, gpio, 1);
	spin_unlock_irqrestore(&bank->lock, flags);
}

static struct irq_chip gpio_irq_chip = {
	.name		= "GPIO",
	.irq_shutdown	= gpio_irq_shutdown,
	.irq_ack	= gpio_ack_irq,
	.irq_mask	= gpio_mask_irq,
	.irq_unmask	= gpio_unmask_irq,
	.irq_set_type	= gpio_irq_type,
	.irq_set_wake	= gpio_wake_enable,
};

/*---------------------------------------------------------------------*/

static int omap_mpuio_suspend_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct gpio_bank	*bank = platform_get_drvdata(pdev);
	void __iomem		*mask_reg = bank->base +
					OMAP_MPUIO_GPIO_MASKIT / bank->stride;
	unsigned long		flags;

	spin_lock_irqsave(&bank->lock, flags);
	__raw_writel(0xffff & ~bank->context.wake_en, mask_reg);
	spin_unlock_irqrestore(&bank->lock, flags);

	return 0;
}

static int omap_mpuio_resume_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct gpio_bank	*bank = platform_get_drvdata(pdev);
	void __iomem		*mask_reg = bank->base +
					OMAP_MPUIO_GPIO_MASKIT / bank->stride;
	unsigned long		flags;

	spin_lock_irqsave(&bank->lock, flags);
	__raw_writel(bank->context.wake_en, mask_reg);
	spin_unlock_irqrestore(&bank->lock, flags);

	return 0;
}

static const struct dev_pm_ops omap_mpuio_dev_pm_ops = {
	.suspend_noirq = omap_mpuio_suspend_noirq,
	.resume_noirq = omap_mpuio_resume_noirq,
};

/* use platform_driver for this. */
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

static inline void mpuio_init(struct gpio_bank *bank)
{
	platform_set_drvdata(&omap_mpuio_device, bank);

	if (platform_driver_register(&omap_mpuio_driver) == 0)
		(void) platform_device_register(&omap_mpuio_device);
}

/*---------------------------------------------------------------------*/

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
	void __iomem *reg = bank->base + bank->regs->direction;

	return __raw_readl(reg) & mask;
}

static int gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct gpio_bank *bank;
	u32 mask;

	bank = container_of(chip, struct gpio_bank, chip);
	mask = (1 << offset);

	if (gpio_is_input(bank, mask))
		return _get_gpio_datain(bank, offset);
	else
		return _get_gpio_dataout(bank, offset);
}

static int gpio_output(struct gpio_chip *chip, unsigned offset, int value)
{
	struct gpio_bank *bank;
	unsigned long flags;

	bank = container_of(chip, struct gpio_bank, chip);
	spin_lock_irqsave(&bank->lock, flags);
	bank->set_dataout(bank, offset, value);
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
	bank->set_dataout(bank, offset, value);
	spin_unlock_irqrestore(&bank->lock, flags);
}

static int gpio_2irq(struct gpio_chip *chip, unsigned offset)
{
	struct gpio_bank *bank;

	bank = container_of(chip, struct gpio_bank, chip);
	return bank->irq_base + offset;
}

/*---------------------------------------------------------------------*/

static void __init omap_gpio_show_rev(struct gpio_bank *bank)
{
	static bool called;
	u32 rev;

	if (called || bank->regs->revision == USHRT_MAX)
		return;

	rev = __raw_readw(bank->base + bank->regs->revision);
	pr_info("OMAP GPIO hardware version %d.%d\n",
		(rev >> 4) & 0x0f, rev & 0x0f);

	called = true;
}

/* This lock class tells lockdep that GPIO irqs are in a different
 * category than their parents, so it won't report false recursion.
 */
static struct lock_class_key gpio_lock_class;

static void omap_gpio_mod_init(struct gpio_bank *bank)
{
	void __iomem *base = bank->base;
	u32 l = 0xffffffff;

	if (bank->width == 16)
		l = 0xffff;

	if (bank->is_mpuio) {
		__raw_writel(l, bank->base + bank->regs->irqenable);
		return;
	}

	_gpio_rmw(base, bank->regs->irqenable, l, bank->regs->irqenable_inv);
	_gpio_rmw(base, bank->regs->irqstatus, l, !bank->regs->irqenable_inv);
	if (bank->regs->debounce_en)
		__raw_writel(0, base + bank->regs->debounce_en);

	/* Save OE default value (0xffffffff) in the context */
	bank->context.oe = __raw_readl(bank->base + bank->regs->direction);
	 /* Initialize interface clk ungated, module enabled */
	if (bank->regs->ctrl)
		__raw_writel(0, base + bank->regs->ctrl);

	bank->dbck = clk_get(bank->dev, "dbclk");
	if (IS_ERR(bank->dbck))
		dev_err(bank->dev, "Could not get gpio dbck\n");
}

static __devinit void
omap_mpuio_alloc_gc(struct gpio_bank *bank, unsigned int irq_start,
		    unsigned int num)
{
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;

	gc = irq_alloc_generic_chip("MPUIO", 1, irq_start, bank->base,
				    handle_simple_irq);
	if (!gc) {
		dev_err(bank->dev, "Memory alloc failed for gc\n");
		return;
	}

	ct = gc->chip_types;

	/* NOTE: No ack required, reading IRQ status clears it. */
	ct->chip.irq_mask = irq_gc_mask_set_bit;
	ct->chip.irq_unmask = irq_gc_mask_clr_bit;
	ct->chip.irq_set_type = gpio_irq_type;

	if (bank->regs->wkup_en)
		ct->chip.irq_set_wake = gpio_wake_enable,

	ct->regs.mask = OMAP_MPUIO_GPIO_INT / bank->stride;
	irq_setup_generic_chip(gc, IRQ_MSK(num), IRQ_GC_INIT_MASK_CACHE,
			       IRQ_NOREQUEST | IRQ_NOPROBE, 0);
}

static void __devinit omap_gpio_chip_init(struct gpio_bank *bank)
{
	int j;
	static int gpio;

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
	if (bank->is_mpuio) {
		bank->chip.label = "mpuio";
		if (bank->regs->wkup_en)
			bank->chip.dev = &omap_mpuio_device.dev;
		bank->chip.base = OMAP_MPUIO(0);
	} else {
		bank->chip.label = "gpio";
		bank->chip.base = gpio;
		gpio += bank->width;
	}
	bank->chip.ngpio = bank->width;

	gpiochip_add(&bank->chip);

	for (j = bank->irq_base; j < bank->irq_base + bank->width; j++) {
		irq_set_lockdep_class(j, &gpio_lock_class);
		irq_set_chip_data(j, bank);
		if (bank->is_mpuio) {
			omap_mpuio_alloc_gc(bank, j, bank->width);
		} else {
			irq_set_chip(j, &gpio_irq_chip);
			irq_set_handler(j, handle_simple_irq);
			set_irq_flags(j, IRQF_VALID);
		}
	}
	irq_set_chained_handler(bank->irq, gpio_irq_handler);
	irq_set_handler_data(bank->irq, bank);
}

static const struct of_device_id omap_gpio_match[];

static int __devinit omap_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	const struct of_device_id *match;
	const struct omap_gpio_platform_data *pdata;
	struct resource *res;
	struct gpio_bank *bank;
	int ret = 0;

	match = of_match_device(of_match_ptr(omap_gpio_match), dev);

	pdata = match ? match->data : dev->platform_data;
	if (!pdata)
		return -EINVAL;

	bank = devm_kzalloc(&pdev->dev, sizeof(struct gpio_bank), GFP_KERNEL);
	if (!bank) {
		dev_err(dev, "Memory alloc failed\n");
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (unlikely(!res)) {
		dev_err(dev, "Invalid IRQ resource\n");
		return -ENODEV;
	}

	bank->irq = res->start;
	bank->dev = dev;
	bank->dbck_flag = pdata->dbck_flag;
	bank->stride = pdata->bank_stride;
	bank->width = pdata->bank_width;
	bank->is_mpuio = pdata->is_mpuio;
	bank->non_wakeup_gpios = pdata->non_wakeup_gpios;
	bank->loses_context = pdata->loses_context;
	bank->regs = pdata->regs;
#ifdef CONFIG_OF_GPIO
	bank->chip.of_node = of_node_get(node);
#endif

	bank->irq_base = irq_alloc_descs(-1, 0, bank->width, 0);
	if (bank->irq_base < 0) {
		dev_err(dev, "Couldn't allocate IRQ numbers\n");
		return -ENODEV;
	}

	bank->domain = irq_domain_add_legacy(node, bank->width, bank->irq_base,
					     0, &irq_domain_simple_ops, NULL);

	if (bank->regs->set_dataout && bank->regs->clr_dataout)
		bank->set_dataout = _set_gpio_dataout_reg;
	else
		bank->set_dataout = _set_gpio_dataout_mask;

	spin_lock_init(&bank->lock);

	/* Static mapping, never released */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(!res)) {
		dev_err(dev, "Invalid mem resource\n");
		return -ENODEV;
	}

	if (!devm_request_mem_region(dev, res->start, resource_size(res),
				     pdev->name)) {
		dev_err(dev, "Region already claimed\n");
		return -EBUSY;
	}

	bank->base = devm_ioremap(dev, res->start, resource_size(res));
	if (!bank->base) {
		dev_err(dev, "Could not ioremap\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, bank);

	pm_runtime_enable(bank->dev);
	pm_runtime_irq_safe(bank->dev);
	pm_runtime_get_sync(bank->dev);

	if (bank->is_mpuio)
		mpuio_init(bank);

	omap_gpio_mod_init(bank);
	omap_gpio_chip_init(bank);
	omap_gpio_show_rev(bank);

	if (bank->loses_context)
		bank->get_context_loss_count = pdata->get_context_loss_count;

	pm_runtime_put(bank->dev);

	list_add_tail(&bank->node, &omap_gpio_list);

	return ret;
}

#ifdef CONFIG_ARCH_OMAP2PLUS

#if defined(CONFIG_PM_RUNTIME)
static void omap_gpio_restore_context(struct gpio_bank *bank);

static int omap_gpio_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct gpio_bank *bank = platform_get_drvdata(pdev);
	u32 l1 = 0, l2 = 0;
	unsigned long flags;
	u32 wake_low, wake_hi;

	spin_lock_irqsave(&bank->lock, flags);

	/*
	 * Only edges can generate a wakeup event to the PRCM.
	 *
	 * Therefore, ensure any wake-up capable GPIOs have
	 * edge-detection enabled before going idle to ensure a wakeup
	 * to the PRCM is generated on a GPIO transition. (c.f. 34xx
	 * NDA TRM 25.5.3.1)
	 *
	 * The normal values will be restored upon ->runtime_resume()
	 * by writing back the values saved in bank->context.
	 */
	wake_low = bank->context.leveldetect0 & bank->context.wake_en;
	if (wake_low)
		__raw_writel(wake_low | bank->context.fallingdetect,
			     bank->base + bank->regs->fallingdetect);
	wake_hi = bank->context.leveldetect1 & bank->context.wake_en;
	if (wake_hi)
		__raw_writel(wake_hi | bank->context.risingdetect,
			     bank->base + bank->regs->risingdetect);

	if (!bank->enabled_non_wakeup_gpios)
		goto update_gpio_context_count;

	if (bank->power_mode != OFF_MODE) {
		bank->power_mode = 0;
		goto update_gpio_context_count;
	}
	/*
	 * If going to OFF, remove triggering for all
	 * non-wakeup GPIOs.  Otherwise spurious IRQs will be
	 * generated.  See OMAP2420 Errata item 1.101.
	 */
	bank->saved_datain = __raw_readl(bank->base +
						bank->regs->datain);
	l1 = bank->context.fallingdetect;
	l2 = bank->context.risingdetect;

	l1 &= ~bank->enabled_non_wakeup_gpios;
	l2 &= ~bank->enabled_non_wakeup_gpios;

	__raw_writel(l1, bank->base + bank->regs->fallingdetect);
	__raw_writel(l2, bank->base + bank->regs->risingdetect);

	bank->workaround_enabled = true;

update_gpio_context_count:
	if (bank->get_context_loss_count)
		bank->context_loss_count =
				bank->get_context_loss_count(bank->dev);

	_gpio_dbck_disable(bank);
	spin_unlock_irqrestore(&bank->lock, flags);

	return 0;
}

static int omap_gpio_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct gpio_bank *bank = platform_get_drvdata(pdev);
	int context_lost_cnt_after;
	u32 l = 0, gen, gen0, gen1;
	unsigned long flags;

	spin_lock_irqsave(&bank->lock, flags);
	_gpio_dbck_enable(bank);

	/*
	 * In ->runtime_suspend(), level-triggered, wakeup-enabled
	 * GPIOs were set to edge trigger also in order to be able to
	 * generate a PRCM wakeup.  Here we restore the
	 * pre-runtime_suspend() values for edge triggering.
	 */
	__raw_writel(bank->context.fallingdetect,
		     bank->base + bank->regs->fallingdetect);
	__raw_writel(bank->context.risingdetect,
		     bank->base + bank->regs->risingdetect);

	if (bank->get_context_loss_count) {
		context_lost_cnt_after =
			bank->get_context_loss_count(bank->dev);
		if (context_lost_cnt_after != bank->context_loss_count) {
			omap_gpio_restore_context(bank);
		} else {
			spin_unlock_irqrestore(&bank->lock, flags);
			return 0;
		}
	}

	if (!bank->workaround_enabled) {
		spin_unlock_irqrestore(&bank->lock, flags);
		return 0;
	}

	__raw_writel(bank->context.fallingdetect,
			bank->base + bank->regs->fallingdetect);
	__raw_writel(bank->context.risingdetect,
			bank->base + bank->regs->risingdetect);
	l = __raw_readl(bank->base + bank->regs->datain);

	/*
	 * Check if any of the non-wakeup interrupt GPIOs have changed
	 * state.  If so, generate an IRQ by software.  This is
	 * horribly racy, but it's the best we can do to work around
	 * this silicon bug.
	 */
	l ^= bank->saved_datain;
	l &= bank->enabled_non_wakeup_gpios;

	/*
	 * No need to generate IRQs for the rising edge for gpio IRQs
	 * configured with falling edge only; and vice versa.
	 */
	gen0 = l & bank->context.fallingdetect;
	gen0 &= bank->saved_datain;

	gen1 = l & bank->context.risingdetect;
	gen1 &= ~(bank->saved_datain);

	/* FIXME: Consider GPIO IRQs with level detections properly! */
	gen = l & (~(bank->context.fallingdetect) &
					 ~(bank->context.risingdetect));
	/* Consider all GPIO IRQs needed to be updated */
	gen |= gen0 | gen1;

	if (gen) {
		u32 old0, old1;

		old0 = __raw_readl(bank->base + bank->regs->leveldetect0);
		old1 = __raw_readl(bank->base + bank->regs->leveldetect1);

		if (!bank->regs->irqstatus_raw0) {
			__raw_writel(old0 | gen, bank->base +
						bank->regs->leveldetect0);
			__raw_writel(old1 | gen, bank->base +
						bank->regs->leveldetect1);
		}

		if (bank->regs->irqstatus_raw0) {
			__raw_writel(old0 | l, bank->base +
						bank->regs->leveldetect0);
			__raw_writel(old1 | l, bank->base +
						bank->regs->leveldetect1);
		}
		__raw_writel(old0, bank->base + bank->regs->leveldetect0);
		__raw_writel(old1, bank->base + bank->regs->leveldetect1);
	}

	bank->workaround_enabled = false;
	spin_unlock_irqrestore(&bank->lock, flags);

	return 0;
}
#endif /* CONFIG_PM_RUNTIME */

void omap2_gpio_prepare_for_idle(int pwr_mode)
{
	struct gpio_bank *bank;

	list_for_each_entry(bank, &omap_gpio_list, node) {
		if (!bank->mod_usage || !bank->loses_context)
			continue;

		bank->power_mode = pwr_mode;

		pm_runtime_put_sync_suspend(bank->dev);
	}
}

void omap2_gpio_resume_after_idle(void)
{
	struct gpio_bank *bank;

	list_for_each_entry(bank, &omap_gpio_list, node) {
		if (!bank->mod_usage || !bank->loses_context)
			continue;

		pm_runtime_get_sync(bank->dev);
	}
}

#if defined(CONFIG_PM_RUNTIME)
static void omap_gpio_restore_context(struct gpio_bank *bank)
{
	__raw_writel(bank->context.wake_en,
				bank->base + bank->regs->wkup_en);
	__raw_writel(bank->context.ctrl, bank->base + bank->regs->ctrl);
	__raw_writel(bank->context.leveldetect0,
				bank->base + bank->regs->leveldetect0);
	__raw_writel(bank->context.leveldetect1,
				bank->base + bank->regs->leveldetect1);
	__raw_writel(bank->context.risingdetect,
				bank->base + bank->regs->risingdetect);
	__raw_writel(bank->context.fallingdetect,
				bank->base + bank->regs->fallingdetect);
	if (bank->regs->set_dataout && bank->regs->clr_dataout)
		__raw_writel(bank->context.dataout,
				bank->base + bank->regs->set_dataout);
	else
		__raw_writel(bank->context.dataout,
				bank->base + bank->regs->dataout);
	__raw_writel(bank->context.oe, bank->base + bank->regs->direction);

	if (bank->dbck_enable_mask) {
		__raw_writel(bank->context.debounce, bank->base +
					bank->regs->debounce);
		__raw_writel(bank->context.debounce_en,
					bank->base + bank->regs->debounce_en);
	}

	__raw_writel(bank->context.irqenable1,
				bank->base + bank->regs->irqenable);
	__raw_writel(bank->context.irqenable2,
				bank->base + bank->regs->irqenable2);
}
#endif /* CONFIG_PM_RUNTIME */
#else
#define omap_gpio_runtime_suspend NULL
#define omap_gpio_runtime_resume NULL
#endif

static const struct dev_pm_ops gpio_pm_ops = {
	SET_RUNTIME_PM_OPS(omap_gpio_runtime_suspend, omap_gpio_runtime_resume,
									NULL)
};

#if defined(CONFIG_OF)
static struct omap_gpio_reg_offs omap2_gpio_regs = {
	.revision =		OMAP24XX_GPIO_REVISION,
	.direction =		OMAP24XX_GPIO_OE,
	.datain =		OMAP24XX_GPIO_DATAIN,
	.dataout =		OMAP24XX_GPIO_DATAOUT,
	.set_dataout =		OMAP24XX_GPIO_SETDATAOUT,
	.clr_dataout =		OMAP24XX_GPIO_CLEARDATAOUT,
	.irqstatus =		OMAP24XX_GPIO_IRQSTATUS1,
	.irqstatus2 =		OMAP24XX_GPIO_IRQSTATUS2,
	.irqenable =		OMAP24XX_GPIO_IRQENABLE1,
	.irqenable2 =		OMAP24XX_GPIO_IRQENABLE2,
	.set_irqenable =	OMAP24XX_GPIO_SETIRQENABLE1,
	.clr_irqenable =	OMAP24XX_GPIO_CLEARIRQENABLE1,
	.debounce =		OMAP24XX_GPIO_DEBOUNCE_VAL,
	.debounce_en =		OMAP24XX_GPIO_DEBOUNCE_EN,
	.ctrl =			OMAP24XX_GPIO_CTRL,
	.wkup_en =		OMAP24XX_GPIO_WAKE_EN,
	.leveldetect0 =		OMAP24XX_GPIO_LEVELDETECT0,
	.leveldetect1 =		OMAP24XX_GPIO_LEVELDETECT1,
	.risingdetect =		OMAP24XX_GPIO_RISINGDETECT,
	.fallingdetect =	OMAP24XX_GPIO_FALLINGDETECT,
};

static struct omap_gpio_reg_offs omap4_gpio_regs = {
	.revision =		OMAP4_GPIO_REVISION,
	.direction =		OMAP4_GPIO_OE,
	.datain =		OMAP4_GPIO_DATAIN,
	.dataout =		OMAP4_GPIO_DATAOUT,
	.set_dataout =		OMAP4_GPIO_SETDATAOUT,
	.clr_dataout =		OMAP4_GPIO_CLEARDATAOUT,
	.irqstatus =		OMAP4_GPIO_IRQSTATUS0,
	.irqstatus2 =		OMAP4_GPIO_IRQSTATUS1,
	.irqenable =		OMAP4_GPIO_IRQSTATUSSET0,
	.irqenable2 =		OMAP4_GPIO_IRQSTATUSSET1,
	.set_irqenable =	OMAP4_GPIO_IRQSTATUSSET0,
	.clr_irqenable =	OMAP4_GPIO_IRQSTATUSCLR0,
	.debounce =		OMAP4_GPIO_DEBOUNCINGTIME,
	.debounce_en =		OMAP4_GPIO_DEBOUNCENABLE,
	.ctrl =			OMAP4_GPIO_CTRL,
	.wkup_en =		OMAP4_GPIO_IRQWAKEN0,
	.leveldetect0 =		OMAP4_GPIO_LEVELDETECT0,
	.leveldetect1 =		OMAP4_GPIO_LEVELDETECT1,
	.risingdetect =		OMAP4_GPIO_RISINGDETECT,
	.fallingdetect =	OMAP4_GPIO_FALLINGDETECT,
};

const static struct omap_gpio_platform_data omap2_pdata = {
	.regs = &omap2_gpio_regs,
	.bank_width = 32,
	.dbck_flag = false,
};

const static struct omap_gpio_platform_data omap3_pdata = {
	.regs = &omap2_gpio_regs,
	.bank_width = 32,
	.dbck_flag = true,
};

const static struct omap_gpio_platform_data omap4_pdata = {
	.regs = &omap4_gpio_regs,
	.bank_width = 32,
	.dbck_flag = true,
};

static const struct of_device_id omap_gpio_match[] = {
	{
		.compatible = "ti,omap4-gpio",
		.data = &omap4_pdata,
	},
	{
		.compatible = "ti,omap3-gpio",
		.data = &omap3_pdata,
	},
	{
		.compatible = "ti,omap2-gpio",
		.data = &omap2_pdata,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, omap_gpio_match);
#endif

static struct platform_driver omap_gpio_driver = {
	.probe		= omap_gpio_probe,
	.driver		= {
		.name	= "omap_gpio",
		.pm	= &gpio_pm_ops,
		.of_match_table = of_match_ptr(omap_gpio_match),
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
