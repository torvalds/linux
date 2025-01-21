// SPDX-License-Identifier: GPL-2.0-only
/*
 * Support functions for OMAP GPIO
 *
 * Copyright (C) 2003-2005 Nokia Corporation
 * Written by Juha Yrjölä <juha.yrjola@nokia.com>
 *
 * Copyright (C) 2009 Texas Instruments
 * Added OMAP4 support - Santosh Shilimkar <santosh.shilimkar@ti.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/seq_file.h>
#include <linux/syscore_ops.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/cpu_pm.h>
#include <linux/device.h>
#include <linux/pm_runtime.h>
#include <linux/pm.h>
#include <linux/of.h>
#include <linux/gpio/driver.h>
#include <linux/bitops.h>
#include <linux/platform_data/gpio-omap.h>

#define OMAP4_GPIO_DEBOUNCINGTIME_MASK 0xFF

struct gpio_regs {
	u32 sysconfig;
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
	void __iomem *base;
	const struct omap_gpio_reg_offs *regs;
	struct device *dev;

	int irq;
	u32 non_wakeup_gpios;
	u32 enabled_non_wakeup_gpios;
	struct gpio_regs context;
	u32 saved_datain;
	u32 level_mask;
	u32 toggle_mask;
	raw_spinlock_t lock;
	raw_spinlock_t wa_lock;
	struct gpio_chip chip;
	struct clk *dbck;
	struct notifier_block nb;
	unsigned int is_suspended:1;
	unsigned int needs_resume:1;
	u32 mod_usage;
	u32 irq_usage;
	u32 dbck_enable_mask;
	bool dbck_enabled;
	bool is_mpuio;
	bool dbck_flag;
	bool loses_context;
	bool context_valid;
	int stride;
	u32 width;
	int context_loss_count;

	void (*set_dataout)(struct gpio_bank *bank, unsigned gpio, int enable);
	int (*get_context_loss_count)(struct device *dev);
};

#define GPIO_MOD_CTRL_BIT	BIT(0)

#define BANK_USED(bank) (bank->mod_usage || bank->irq_usage)
#define LINE_USED(line, offset) (line & (BIT(offset)))

static void omap_gpio_unmask_irq(struct irq_data *d);

static inline struct gpio_bank *omap_irq_data_get_bank(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);
	return gpiochip_get_data(chip);
}

static inline u32 omap_gpio_rmw(void __iomem *reg, u32 mask, bool set)
{
	u32 val = readl_relaxed(reg);

	if (set)
		val |= mask;
	else
		val &= ~mask;

	writel_relaxed(val, reg);

	return val;
}

static void omap_set_gpio_direction(struct gpio_bank *bank, int gpio,
				    int is_input)
{
	bank->context.oe = omap_gpio_rmw(bank->base + bank->regs->direction,
					 BIT(gpio), is_input);
}


/* set data out value using dedicate set/clear register */
static void omap_set_gpio_dataout_reg(struct gpio_bank *bank, unsigned offset,
				      int enable)
{
	void __iomem *reg = bank->base;
	u32 l = BIT(offset);

	if (enable) {
		reg += bank->regs->set_dataout;
		bank->context.dataout |= l;
	} else {
		reg += bank->regs->clr_dataout;
		bank->context.dataout &= ~l;
	}

	writel_relaxed(l, reg);
}

/* set data out value using mask register */
static void omap_set_gpio_dataout_mask(struct gpio_bank *bank, unsigned offset,
				       int enable)
{
	bank->context.dataout = omap_gpio_rmw(bank->base + bank->regs->dataout,
					      BIT(offset), enable);
}

static inline void omap_gpio_dbck_enable(struct gpio_bank *bank)
{
	if (bank->dbck_enable_mask && !bank->dbck_enabled) {
		clk_enable(bank->dbck);
		bank->dbck_enabled = true;

		writel_relaxed(bank->dbck_enable_mask,
			     bank->base + bank->regs->debounce_en);
	}
}

static inline void omap_gpio_dbck_disable(struct gpio_bank *bank)
{
	if (bank->dbck_enable_mask && bank->dbck_enabled) {
		/*
		 * Disable debounce before cutting it's clock. If debounce is
		 * enabled but the clock is not, GPIO module seems to be unable
		 * to detect events and generate interrupts at least on OMAP3.
		 */
		writel_relaxed(0, bank->base + bank->regs->debounce_en);

		clk_disable(bank->dbck);
		bank->dbck_enabled = false;
	}
}

/**
 * omap2_set_gpio_debounce - low level gpio debounce time
 * @bank: the gpio bank we're acting upon
 * @offset: the gpio number on this @bank
 * @debounce: debounce time to use
 *
 * OMAP's debounce time is in 31us steps
 *   <debounce time> = (GPIO_DEBOUNCINGTIME[7:0].DEBOUNCETIME + 1) x 31
 * so we need to convert and round up to the closest unit.
 *
 * Return: 0 on success, negative error otherwise.
 */
static int omap2_set_gpio_debounce(struct gpio_bank *bank, unsigned offset,
				   unsigned debounce)
{
	u32			val;
	u32			l;
	bool			enable = !!debounce;

	if (!bank->dbck_flag)
		return -ENOTSUPP;

	if (enable) {
		debounce = DIV_ROUND_UP(debounce, 31) - 1;
		if ((debounce & OMAP4_GPIO_DEBOUNCINGTIME_MASK) != debounce)
			return -EINVAL;
	}

	l = BIT(offset);

	clk_enable(bank->dbck);
	writel_relaxed(debounce, bank->base + bank->regs->debounce);

	val = omap_gpio_rmw(bank->base + bank->regs->debounce_en, l, enable);
	bank->dbck_enable_mask = val;

	clk_disable(bank->dbck);
	/*
	 * Enable debounce clock per module.
	 * This call is mandatory because in omap_gpio_request() when
	 * *_runtime_get_sync() is called,  _gpio_dbck_enable() within
	 * runtime callbck fails to turn on dbck because dbck_enable_mask
	 * used within _gpio_dbck_enable() is still not initialized at
	 * that point. Therefore we have to enable dbck here.
	 */
	omap_gpio_dbck_enable(bank);
	if (bank->dbck_enable_mask) {
		bank->context.debounce = debounce;
		bank->context.debounce_en = val;
	}

	return 0;
}

/**
 * omap_clear_gpio_debounce - clear debounce settings for a gpio
 * @bank: the gpio bank we're acting upon
 * @offset: the gpio number on this @bank
 *
 * If a gpio is using debounce, then clear the debounce enable bit and if
 * this is the only gpio in this bank using debounce, then clear the debounce
 * time too. The debounce clock will also be disabled when calling this function
 * if this is the only gpio in the bank using debounce.
 */
static void omap_clear_gpio_debounce(struct gpio_bank *bank, unsigned offset)
{
	u32 gpio_bit = BIT(offset);

	if (!bank->dbck_flag)
		return;

	if (!(bank->dbck_enable_mask & gpio_bit))
		return;

	bank->dbck_enable_mask &= ~gpio_bit;
	bank->context.debounce_en &= ~gpio_bit;
        writel_relaxed(bank->context.debounce_en,
		     bank->base + bank->regs->debounce_en);

	if (!bank->dbck_enable_mask) {
		bank->context.debounce = 0;
		writel_relaxed(bank->context.debounce, bank->base +
			     bank->regs->debounce);
		clk_disable(bank->dbck);
		bank->dbck_enabled = false;
	}
}

/*
 * Off mode wake-up capable GPIOs in bank(s) that are in the wakeup domain.
 * See TRM section for GPIO for "Wake-Up Generation" for the list of GPIOs
 * in wakeup domain. If bank->non_wakeup_gpios is not configured, assume none
 * are capable waking up the system from off mode.
 */
static bool omap_gpio_is_off_wakeup_capable(struct gpio_bank *bank, u32 gpio_mask)
{
	u32 no_wake = bank->non_wakeup_gpios;

	if (no_wake)
		return !!(~no_wake & gpio_mask);

	return false;
}

static inline void omap_set_gpio_trigger(struct gpio_bank *bank, int gpio,
						unsigned trigger)
{
	void __iomem *base = bank->base;
	u32 gpio_bit = BIT(gpio);

	omap_gpio_rmw(base + bank->regs->leveldetect0, gpio_bit,
		      trigger & IRQ_TYPE_LEVEL_LOW);
	omap_gpio_rmw(base + bank->regs->leveldetect1, gpio_bit,
		      trigger & IRQ_TYPE_LEVEL_HIGH);

	/*
	 * We need the edge detection enabled for to allow the GPIO block
	 * to be woken from idle state.  Set the appropriate edge detection
	 * in addition to the level detection.
	 */
	omap_gpio_rmw(base + bank->regs->risingdetect, gpio_bit,
		      trigger & (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_LEVEL_HIGH));
	omap_gpio_rmw(base + bank->regs->fallingdetect, gpio_bit,
		      trigger & (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_LEVEL_LOW));

	bank->context.leveldetect0 =
			readl_relaxed(bank->base + bank->regs->leveldetect0);
	bank->context.leveldetect1 =
			readl_relaxed(bank->base + bank->regs->leveldetect1);
	bank->context.risingdetect =
			readl_relaxed(bank->base + bank->regs->risingdetect);
	bank->context.fallingdetect =
			readl_relaxed(bank->base + bank->regs->fallingdetect);

	bank->level_mask = bank->context.leveldetect0 |
			   bank->context.leveldetect1;

	/* This part needs to be executed always for OMAP{34xx, 44xx} */
	if (!bank->regs->irqctrl && !omap_gpio_is_off_wakeup_capable(bank, gpio)) {
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
}

/*
 * This only applies to chips that can't do both rising and falling edge
 * detection at once.  For all other chips, this function is a noop.
 */
static void omap_toggle_gpio_edge_triggering(struct gpio_bank *bank, int gpio)
{
	if (IS_ENABLED(CONFIG_ARCH_OMAP1) && bank->regs->irqctrl) {
		void __iomem *reg = bank->base + bank->regs->irqctrl;

		writel_relaxed(readl_relaxed(reg) ^ BIT(gpio), reg);
	}
}

static int omap_set_gpio_triggering(struct gpio_bank *bank, int gpio,
				    unsigned trigger)
{
	void __iomem *reg = bank->base;
	u32 l = 0;

	if (bank->regs->leveldetect0 && bank->regs->wkup_en) {
		omap_set_gpio_trigger(bank, gpio, trigger);
	} else if (bank->regs->irqctrl) {
		reg += bank->regs->irqctrl;

		l = readl_relaxed(reg);
		if ((trigger & IRQ_TYPE_SENSE_MASK) == IRQ_TYPE_EDGE_BOTH)
			bank->toggle_mask |= BIT(gpio);
		if (trigger & IRQ_TYPE_EDGE_RISING)
			l |= BIT(gpio);
		else if (trigger & IRQ_TYPE_EDGE_FALLING)
			l &= ~(BIT(gpio));
		else
			return -EINVAL;

		writel_relaxed(l, reg);
	} else if (bank->regs->edgectrl1) {
		if (gpio & 0x08)
			reg += bank->regs->edgectrl2;
		else
			reg += bank->regs->edgectrl1;

		gpio &= 0x07;
		l = readl_relaxed(reg);
		l &= ~(3 << (gpio << 1));
		if (trigger & IRQ_TYPE_EDGE_RISING)
			l |= 2 << (gpio << 1);
		if (trigger & IRQ_TYPE_EDGE_FALLING)
			l |= BIT(gpio << 1);
		writel_relaxed(l, reg);
	}
	return 0;
}

static void omap_enable_gpio_module(struct gpio_bank *bank, unsigned offset)
{
	if (bank->regs->pinctrl) {
		void __iomem *reg = bank->base + bank->regs->pinctrl;

		/* Claim the pin for MPU */
		writel_relaxed(readl_relaxed(reg) | (BIT(offset)), reg);
	}

	if (bank->regs->ctrl && !BANK_USED(bank)) {
		void __iomem *reg = bank->base + bank->regs->ctrl;
		u32 ctrl;

		ctrl = readl_relaxed(reg);
		/* Module is enabled, clocks are not gated */
		ctrl &= ~GPIO_MOD_CTRL_BIT;
		writel_relaxed(ctrl, reg);
		bank->context.ctrl = ctrl;
	}
}

static void omap_disable_gpio_module(struct gpio_bank *bank, unsigned offset)
{
	if (bank->regs->ctrl && !BANK_USED(bank)) {
		void __iomem *reg = bank->base + bank->regs->ctrl;
		u32 ctrl;

		ctrl = readl_relaxed(reg);
		/* Module is disabled, clocks are gated */
		ctrl |= GPIO_MOD_CTRL_BIT;
		writel_relaxed(ctrl, reg);
		bank->context.ctrl = ctrl;
	}
}

static int omap_gpio_is_input(struct gpio_bank *bank, unsigned offset)
{
	void __iomem *reg = bank->base + bank->regs->direction;

	return readl_relaxed(reg) & BIT(offset);
}

static void omap_gpio_init_irq(struct gpio_bank *bank, unsigned offset)
{
	if (!LINE_USED(bank->mod_usage, offset)) {
		omap_enable_gpio_module(bank, offset);
		omap_set_gpio_direction(bank, offset, 1);
	}
	bank->irq_usage |= BIT(offset);
}

static int omap_gpio_irq_type(struct irq_data *d, unsigned type)
{
	struct gpio_bank *bank = omap_irq_data_get_bank(d);
	int retval;
	unsigned long flags;
	unsigned offset = d->hwirq;

	if (type & ~IRQ_TYPE_SENSE_MASK)
		return -EINVAL;

	if (!bank->regs->leveldetect0 &&
		(type & (IRQ_TYPE_LEVEL_LOW|IRQ_TYPE_LEVEL_HIGH)))
		return -EINVAL;

	raw_spin_lock_irqsave(&bank->lock, flags);
	retval = omap_set_gpio_triggering(bank, offset, type);
	if (retval) {
		raw_spin_unlock_irqrestore(&bank->lock, flags);
		goto error;
	}
	omap_gpio_init_irq(bank, offset);
	if (!omap_gpio_is_input(bank, offset)) {
		raw_spin_unlock_irqrestore(&bank->lock, flags);
		retval = -EINVAL;
		goto error;
	}
	raw_spin_unlock_irqrestore(&bank->lock, flags);

	if (type & (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_LEVEL_HIGH))
		irq_set_handler_locked(d, handle_level_irq);
	else if (type & (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING))
		/*
		 * Edge IRQs are already cleared/acked in irq_handler and
		 * not need to be masked, as result handle_edge_irq()
		 * logic is excessed here and may cause lose of interrupts.
		 * So just use handle_simple_irq.
		 */
		irq_set_handler_locked(d, handle_simple_irq);

	return 0;

error:
	return retval;
}

static void omap_clear_gpio_irqbank(struct gpio_bank *bank, int gpio_mask)
{
	void __iomem *reg = bank->base;

	reg += bank->regs->irqstatus;
	writel_relaxed(gpio_mask, reg);

	/* Workaround for clearing DSP GPIO interrupts to allow retention */
	if (bank->regs->irqstatus2) {
		reg = bank->base + bank->regs->irqstatus2;
		writel_relaxed(gpio_mask, reg);
	}

	/* Flush posted write for the irq status to avoid spurious interrupts */
	readl_relaxed(reg);
}

static inline void omap_clear_gpio_irqstatus(struct gpio_bank *bank,
					     unsigned offset)
{
	omap_clear_gpio_irqbank(bank, BIT(offset));
}

static u32 omap_get_gpio_irqbank_mask(struct gpio_bank *bank)
{
	void __iomem *reg = bank->base;
	u32 l;
	u32 mask = (BIT(bank->width)) - 1;

	reg += bank->regs->irqenable;
	l = readl_relaxed(reg);
	if (bank->regs->irqenable_inv)
		l = ~l;
	l &= mask;
	return l;
}

static inline void omap_set_gpio_irqenable(struct gpio_bank *bank,
					   unsigned offset, int enable)
{
	void __iomem *reg = bank->base;
	u32 gpio_mask = BIT(offset);

	if (bank->regs->set_irqenable && bank->regs->clr_irqenable) {
		if (enable) {
			reg += bank->regs->set_irqenable;
			bank->context.irqenable1 |= gpio_mask;
		} else {
			reg += bank->regs->clr_irqenable;
			bank->context.irqenable1 &= ~gpio_mask;
		}
		writel_relaxed(gpio_mask, reg);
	} else {
		bank->context.irqenable1 =
			omap_gpio_rmw(reg + bank->regs->irqenable, gpio_mask,
				      enable ^ bank->regs->irqenable_inv);
	}

	/*
	 * Program GPIO wakeup along with IRQ enable to satisfy OMAP4430 TRM
	 * note requiring correlation between the IRQ enable registers and
	 * the wakeup registers.  In any case, we want wakeup from idle
	 * enabled for the GPIOs which support this feature.
	 */
	if (bank->regs->wkup_en &&
	    (bank->regs->edgectrl1 || !(bank->non_wakeup_gpios & gpio_mask))) {
		bank->context.wake_en =
			omap_gpio_rmw(bank->base + bank->regs->wkup_en,
				      gpio_mask, enable);
	}
}

/* Use disable_irq_wake() and enable_irq_wake() functions from drivers */
static int omap_gpio_wake_enable(struct irq_data *d, unsigned int enable)
{
	struct gpio_bank *bank = omap_irq_data_get_bank(d);

	return irq_set_irq_wake(bank->irq, enable);
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
static irqreturn_t omap_gpio_irq_handler(int irq, void *gpiobank)
{
	void __iomem *isr_reg = NULL;
	u32 enabled, isr, edge;
	unsigned int bit;
	struct gpio_bank *bank = gpiobank;
	unsigned long wa_lock_flags;
	unsigned long lock_flags;

	isr_reg = bank->base + bank->regs->irqstatus;
	if (WARN_ON(!isr_reg))
		goto exit;

	if (WARN_ONCE(!pm_runtime_active(bank->chip.parent),
		      "gpio irq%i while runtime suspended?\n", irq))
		return IRQ_NONE;

	while (1) {
		raw_spin_lock_irqsave(&bank->lock, lock_flags);

		enabled = omap_get_gpio_irqbank_mask(bank);
		isr = readl_relaxed(isr_reg) & enabled;

		/*
		 * Clear edge sensitive interrupts before calling handler(s)
		 * so subsequent edge transitions are not missed while the
		 * handlers are running.
		 */
		edge = isr & ~bank->level_mask;
		if (edge)
			omap_clear_gpio_irqbank(bank, edge);

		raw_spin_unlock_irqrestore(&bank->lock, lock_flags);

		if (!isr)
			break;

		while (isr) {
			bit = __ffs(isr);
			isr &= ~(BIT(bit));

			raw_spin_lock_irqsave(&bank->lock, lock_flags);
			/*
			 * Some chips can't respond to both rising and falling
			 * at the same time.  If this irq was requested with
			 * both flags, we need to flip the ICR data for the IRQ
			 * to respond to the IRQ for the opposite direction.
			 * This will be indicated in the bank toggle_mask.
			 */
			if (bank->toggle_mask & (BIT(bit)))
				omap_toggle_gpio_edge_triggering(bank, bit);

			raw_spin_unlock_irqrestore(&bank->lock, lock_flags);

			raw_spin_lock_irqsave(&bank->wa_lock, wa_lock_flags);

			generic_handle_domain_irq(bank->chip.irq.domain, bit);

			raw_spin_unlock_irqrestore(&bank->wa_lock,
						   wa_lock_flags);
		}
	}
exit:
	return IRQ_HANDLED;
}

static unsigned int omap_gpio_irq_startup(struct irq_data *d)
{
	struct gpio_bank *bank = omap_irq_data_get_bank(d);
	unsigned long flags;
	unsigned offset = d->hwirq;

	raw_spin_lock_irqsave(&bank->lock, flags);

	if (!LINE_USED(bank->mod_usage, offset))
		omap_set_gpio_direction(bank, offset, 1);
	omap_enable_gpio_module(bank, offset);
	bank->irq_usage |= BIT(offset);

	raw_spin_unlock_irqrestore(&bank->lock, flags);
	omap_gpio_unmask_irq(d);

	return 0;
}

static void omap_gpio_irq_shutdown(struct irq_data *d)
{
	struct gpio_bank *bank = omap_irq_data_get_bank(d);
	unsigned long flags;
	unsigned offset = d->hwirq;

	raw_spin_lock_irqsave(&bank->lock, flags);
	bank->irq_usage &= ~(BIT(offset));
	omap_set_gpio_triggering(bank, offset, IRQ_TYPE_NONE);
	omap_clear_gpio_irqstatus(bank, offset);
	omap_set_gpio_irqenable(bank, offset, 0);
	if (!LINE_USED(bank->mod_usage, offset))
		omap_clear_gpio_debounce(bank, offset);
	omap_disable_gpio_module(bank, offset);
	raw_spin_unlock_irqrestore(&bank->lock, flags);
}

static void omap_gpio_irq_bus_lock(struct irq_data *data)
{
	struct gpio_bank *bank = omap_irq_data_get_bank(data);

	pm_runtime_get_sync(bank->chip.parent);
}

static void gpio_irq_bus_sync_unlock(struct irq_data *data)
{
	struct gpio_bank *bank = omap_irq_data_get_bank(data);

	pm_runtime_put(bank->chip.parent);
}

static void omap_gpio_mask_irq(struct irq_data *d)
{
	struct gpio_bank *bank = omap_irq_data_get_bank(d);
	unsigned offset = d->hwirq;
	unsigned long flags;

	raw_spin_lock_irqsave(&bank->lock, flags);
	omap_set_gpio_triggering(bank, offset, IRQ_TYPE_NONE);
	omap_set_gpio_irqenable(bank, offset, 0);
	raw_spin_unlock_irqrestore(&bank->lock, flags);
	gpiochip_disable_irq(&bank->chip, offset);
}

static void omap_gpio_unmask_irq(struct irq_data *d)
{
	struct gpio_bank *bank = omap_irq_data_get_bank(d);
	unsigned offset = d->hwirq;
	u32 trigger = irqd_get_trigger_type(d);
	unsigned long flags;

	gpiochip_enable_irq(&bank->chip, offset);
	raw_spin_lock_irqsave(&bank->lock, flags);
	omap_set_gpio_irqenable(bank, offset, 1);

	/*
	 * For level-triggered GPIOs, clearing must be done after the source
	 * is cleared, thus after the handler has run. OMAP4 needs this done
	 * after enabing the interrupt to clear the wakeup status.
	 */
	if (bank->regs->leveldetect0 && bank->regs->wkup_en &&
	    trigger & (IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_LEVEL_LOW))
		omap_clear_gpio_irqstatus(bank, offset);

	if (trigger)
		omap_set_gpio_triggering(bank, offset, trigger);

	raw_spin_unlock_irqrestore(&bank->lock, flags);
}

static void omap_gpio_irq_print_chip(struct irq_data *d, struct seq_file *p)
{
	struct gpio_bank *bank = omap_irq_data_get_bank(d);

	seq_puts(p, dev_name(bank->dev));
}

static const struct irq_chip omap_gpio_irq_chip = {
	.irq_startup = omap_gpio_irq_startup,
	.irq_shutdown = omap_gpio_irq_shutdown,
	.irq_mask = omap_gpio_mask_irq,
	.irq_unmask = omap_gpio_unmask_irq,
	.irq_set_type = omap_gpio_irq_type,
	.irq_set_wake = omap_gpio_wake_enable,
	.irq_bus_lock = omap_gpio_irq_bus_lock,
	.irq_bus_sync_unlock = gpio_irq_bus_sync_unlock,
	.irq_print_chip = omap_gpio_irq_print_chip,
	.flags = IRQCHIP_MASK_ON_SUSPEND | IRQCHIP_IMMUTABLE,
	 GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static const struct irq_chip omap_gpio_irq_chip_nowake = {
	.irq_startup = omap_gpio_irq_startup,
	.irq_shutdown = omap_gpio_irq_shutdown,
	.irq_mask = omap_gpio_mask_irq,
	.irq_unmask = omap_gpio_unmask_irq,
	.irq_set_type = omap_gpio_irq_type,
	.irq_bus_lock = omap_gpio_irq_bus_lock,
	.irq_bus_sync_unlock = gpio_irq_bus_sync_unlock,
	.irq_print_chip = omap_gpio_irq_print_chip,
	.flags = IRQCHIP_MASK_ON_SUSPEND | IRQCHIP_IMMUTABLE,
	 GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

/*---------------------------------------------------------------------*/

static int omap_mpuio_suspend_noirq(struct device *dev)
{
	struct gpio_bank	*bank = dev_get_drvdata(dev);
	void __iomem		*mask_reg = bank->base +
					OMAP_MPUIO_GPIO_MASKIT / bank->stride;
	unsigned long		flags;

	raw_spin_lock_irqsave(&bank->lock, flags);
	writel_relaxed(0xffff & ~bank->context.wake_en, mask_reg);
	raw_spin_unlock_irqrestore(&bank->lock, flags);

	return 0;
}

static int omap_mpuio_resume_noirq(struct device *dev)
{
	struct gpio_bank	*bank = dev_get_drvdata(dev);
	void __iomem		*mask_reg = bank->base +
					OMAP_MPUIO_GPIO_MASKIT / bank->stride;
	unsigned long		flags;

	raw_spin_lock_irqsave(&bank->lock, flags);
	writel_relaxed(bank->context.wake_en, mask_reg);
	raw_spin_unlock_irqrestore(&bank->lock, flags);

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

static inline void omap_mpuio_init(struct gpio_bank *bank)
{
	platform_set_drvdata(&omap_mpuio_device, bank);

	if (platform_driver_register(&omap_mpuio_driver) == 0)
		(void) platform_device_register(&omap_mpuio_device);
}

/*---------------------------------------------------------------------*/

static int omap_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	struct gpio_bank *bank = gpiochip_get_data(chip);
	unsigned long flags;

	pm_runtime_get_sync(chip->parent);

	raw_spin_lock_irqsave(&bank->lock, flags);
	omap_enable_gpio_module(bank, offset);
	bank->mod_usage |= BIT(offset);
	raw_spin_unlock_irqrestore(&bank->lock, flags);

	return 0;
}

static void omap_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	struct gpio_bank *bank = gpiochip_get_data(chip);
	unsigned long flags;

	raw_spin_lock_irqsave(&bank->lock, flags);
	bank->mod_usage &= ~(BIT(offset));
	if (!LINE_USED(bank->irq_usage, offset)) {
		omap_set_gpio_direction(bank, offset, 1);
		omap_clear_gpio_debounce(bank, offset);
	}
	omap_disable_gpio_module(bank, offset);
	raw_spin_unlock_irqrestore(&bank->lock, flags);

	pm_runtime_put(chip->parent);
}

static int omap_gpio_get_direction(struct gpio_chip *chip, unsigned offset)
{
	struct gpio_bank *bank = gpiochip_get_data(chip);

	if (readl_relaxed(bank->base + bank->regs->direction) & BIT(offset))
		return GPIO_LINE_DIRECTION_IN;

	return GPIO_LINE_DIRECTION_OUT;
}

static int omap_gpio_input(struct gpio_chip *chip, unsigned offset)
{
	struct gpio_bank *bank;
	unsigned long flags;

	bank = gpiochip_get_data(chip);
	raw_spin_lock_irqsave(&bank->lock, flags);
	omap_set_gpio_direction(bank, offset, 1);
	raw_spin_unlock_irqrestore(&bank->lock, flags);
	return 0;
}

static int omap_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct gpio_bank *bank = gpiochip_get_data(chip);
	void __iomem *reg;

	if (omap_gpio_is_input(bank, offset))
		reg = bank->base + bank->regs->datain;
	else
		reg = bank->base + bank->regs->dataout;

	return (readl_relaxed(reg) & BIT(offset)) != 0;
}

static int omap_gpio_output(struct gpio_chip *chip, unsigned offset, int value)
{
	struct gpio_bank *bank;
	unsigned long flags;

	bank = gpiochip_get_data(chip);
	raw_spin_lock_irqsave(&bank->lock, flags);
	bank->set_dataout(bank, offset, value);
	omap_set_gpio_direction(bank, offset, 0);
	raw_spin_unlock_irqrestore(&bank->lock, flags);
	return 0;
}

static int omap_gpio_get_multiple(struct gpio_chip *chip, unsigned long *mask,
				  unsigned long *bits)
{
	struct gpio_bank *bank = gpiochip_get_data(chip);
	void __iomem *base = bank->base;
	u32 direction, m, val = 0;

	direction = readl_relaxed(base + bank->regs->direction);

	m = direction & *mask;
	if (m)
		val |= readl_relaxed(base + bank->regs->datain) & m;

	m = ~direction & *mask;
	if (m)
		val |= readl_relaxed(base + bank->regs->dataout) & m;

	*bits = val;

	return 0;
}

static int omap_gpio_debounce(struct gpio_chip *chip, unsigned offset,
			      unsigned debounce)
{
	struct gpio_bank *bank;
	unsigned long flags;
	int ret;

	bank = gpiochip_get_data(chip);

	raw_spin_lock_irqsave(&bank->lock, flags);
	ret = omap2_set_gpio_debounce(bank, offset, debounce);
	raw_spin_unlock_irqrestore(&bank->lock, flags);

	if (ret)
		dev_info(chip->parent,
			 "Could not set line %u debounce to %u microseconds (%d)",
			 offset, debounce, ret);

	return ret;
}

static int omap_gpio_set_config(struct gpio_chip *chip, unsigned offset,
				unsigned long config)
{
	u32 debounce;
	int ret = -ENOTSUPP;

	switch (pinconf_to_config_param(config)) {
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_BIAS_PULL_DOWN:
		ret = gpiochip_generic_config(chip, offset, config);
		break;
	case PIN_CONFIG_INPUT_DEBOUNCE:
		debounce = pinconf_to_config_argument(config);
		ret = omap_gpio_debounce(chip, offset, debounce);
		break;
	default:
		break;
	}

	return ret;
}

static void omap_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct gpio_bank *bank;
	unsigned long flags;

	bank = gpiochip_get_data(chip);
	raw_spin_lock_irqsave(&bank->lock, flags);
	bank->set_dataout(bank, offset, value);
	raw_spin_unlock_irqrestore(&bank->lock, flags);
}

static void omap_gpio_set_multiple(struct gpio_chip *chip, unsigned long *mask,
				   unsigned long *bits)
{
	struct gpio_bank *bank = gpiochip_get_data(chip);
	void __iomem *reg = bank->base + bank->regs->dataout;
	unsigned long flags;
	u32 l;

	raw_spin_lock_irqsave(&bank->lock, flags);
	l = (readl_relaxed(reg) & ~*mask) | (*bits & *mask);
	writel_relaxed(l, reg);
	bank->context.dataout = l;
	raw_spin_unlock_irqrestore(&bank->lock, flags);
}

/*---------------------------------------------------------------------*/

static void omap_gpio_show_rev(struct gpio_bank *bank)
{
	static bool called;
	u32 rev;

	if (called || bank->regs->revision == USHRT_MAX)
		return;

	rev = readw_relaxed(bank->base + bank->regs->revision);
	pr_info("OMAP GPIO hardware version %d.%d\n",
		(rev >> 4) & 0x0f, rev & 0x0f);

	called = true;
}

static void omap_gpio_mod_init(struct gpio_bank *bank)
{
	void __iomem *base = bank->base;
	u32 l = 0xffffffff;

	if (bank->width == 16)
		l = 0xffff;

	if (bank->is_mpuio) {
		writel_relaxed(l, bank->base + bank->regs->irqenable);
		return;
	}

	omap_gpio_rmw(base + bank->regs->irqenable, l,
		      bank->regs->irqenable_inv);
	omap_gpio_rmw(base + bank->regs->irqstatus, l,
		      !bank->regs->irqenable_inv);
	if (bank->regs->debounce_en)
		writel_relaxed(0, base + bank->regs->debounce_en);

	/* Save OE default value (0xffffffff) in the context */
	bank->context.oe = readl_relaxed(bank->base + bank->regs->direction);
	 /* Initialize interface clk ungated, module enabled */
	if (bank->regs->ctrl)
		writel_relaxed(0, base + bank->regs->ctrl);
}

static int omap_gpio_chip_init(struct gpio_bank *bank, struct device *pm_dev)
{
	struct gpio_irq_chip *irq;
	static int gpio;
	const char *label;
	int ret;

	/*
	 * REVISIT eventually switch from OMAP-specific gpio structs
	 * over to the generic ones
	 */
	bank->chip.request = omap_gpio_request;
	bank->chip.free = omap_gpio_free;
	bank->chip.get_direction = omap_gpio_get_direction;
	bank->chip.direction_input = omap_gpio_input;
	bank->chip.get = omap_gpio_get;
	bank->chip.get_multiple = omap_gpio_get_multiple;
	bank->chip.direction_output = omap_gpio_output;
	bank->chip.set_config = omap_gpio_set_config;
	bank->chip.set = omap_gpio_set;
	bank->chip.set_multiple = omap_gpio_set_multiple;
	if (bank->is_mpuio) {
		bank->chip.label = "mpuio";
		if (bank->regs->wkup_en)
			bank->chip.parent = &omap_mpuio_device.dev;
	} else {
		label = devm_kasprintf(bank->chip.parent, GFP_KERNEL, "gpio-%d-%d",
				       gpio, gpio + bank->width - 1);
		if (!label)
			return -ENOMEM;
		bank->chip.label = label;
	}
	bank->chip.base = -1;
	bank->chip.ngpio = bank->width;

	irq = &bank->chip.irq;
	/* MPUIO is a bit different, reading IRQ status clears it */
	if (bank->is_mpuio && !bank->regs->wkup_en)
		gpio_irq_chip_set_chip(irq, &omap_gpio_irq_chip_nowake);
	else
		gpio_irq_chip_set_chip(irq, &omap_gpio_irq_chip);
	irq->handler = handle_bad_irq;
	irq->default_type = IRQ_TYPE_NONE;
	irq->num_parents = 1;
	irq->parents = &bank->irq;

	ret = gpiochip_add_data(&bank->chip, bank);
	if (ret)
		return dev_err_probe(bank->chip.parent, ret, "Could not register gpio chip\n");

	irq_domain_set_pm_device(bank->chip.irq.domain, pm_dev);
	ret = devm_request_irq(bank->chip.parent, bank->irq,
			       omap_gpio_irq_handler,
			       0, dev_name(bank->chip.parent), bank);
	if (ret)
		gpiochip_remove(&bank->chip);

	if (!bank->is_mpuio)
		gpio += bank->width;

	return ret;
}

static void omap_gpio_init_context(struct gpio_bank *p)
{
	const struct omap_gpio_reg_offs *regs = p->regs;
	void __iomem *base = p->base;

	p->context.sysconfig	= readl_relaxed(base + regs->sysconfig);
	p->context.ctrl		= readl_relaxed(base + regs->ctrl);
	p->context.oe		= readl_relaxed(base + regs->direction);
	p->context.wake_en	= readl_relaxed(base + regs->wkup_en);
	p->context.leveldetect0	= readl_relaxed(base + regs->leveldetect0);
	p->context.leveldetect1	= readl_relaxed(base + regs->leveldetect1);
	p->context.risingdetect	= readl_relaxed(base + regs->risingdetect);
	p->context.fallingdetect = readl_relaxed(base + regs->fallingdetect);
	p->context.irqenable1	= readl_relaxed(base + regs->irqenable);
	p->context.irqenable2	= readl_relaxed(base + regs->irqenable2);
	p->context.dataout	= readl_relaxed(base + regs->dataout);

	p->context_valid = true;
}

static void omap_gpio_restore_context(struct gpio_bank *bank)
{
	const struct omap_gpio_reg_offs *regs = bank->regs;
	void __iomem *base = bank->base;

	writel_relaxed(bank->context.sysconfig, base + regs->sysconfig);
	writel_relaxed(bank->context.wake_en, base + regs->wkup_en);
	writel_relaxed(bank->context.ctrl, base + regs->ctrl);
	writel_relaxed(bank->context.leveldetect0, base + regs->leveldetect0);
	writel_relaxed(bank->context.leveldetect1, base + regs->leveldetect1);
	writel_relaxed(bank->context.risingdetect, base + regs->risingdetect);
	writel_relaxed(bank->context.fallingdetect, base + regs->fallingdetect);
	writel_relaxed(bank->context.dataout, base + regs->dataout);
	writel_relaxed(bank->context.oe, base + regs->direction);

	if (bank->dbck_enable_mask) {
		writel_relaxed(bank->context.debounce, base + regs->debounce);
		writel_relaxed(bank->context.debounce_en,
			       base + regs->debounce_en);
	}

	writel_relaxed(bank->context.irqenable1, base + regs->irqenable);
	writel_relaxed(bank->context.irqenable2, base + regs->irqenable2);
}

static void omap_gpio_idle(struct gpio_bank *bank, bool may_lose_context)
{
	struct device *dev = bank->chip.parent;
	void __iomem *base = bank->base;
	u32 mask, nowake;

	bank->saved_datain = readl_relaxed(base + bank->regs->datain);

	/* Save syconfig, it's runtime value can be different from init value */
	if (bank->loses_context)
		bank->context.sysconfig = readl_relaxed(base + bank->regs->sysconfig);

	if (!bank->enabled_non_wakeup_gpios)
		goto update_gpio_context_count;

	/* Check for pending EDGE_FALLING, ignore EDGE_BOTH */
	mask = bank->enabled_non_wakeup_gpios & bank->context.fallingdetect;
	mask &= ~bank->context.risingdetect;
	bank->saved_datain |= mask;

	/* Check for pending EDGE_RISING, ignore EDGE_BOTH */
	mask = bank->enabled_non_wakeup_gpios & bank->context.risingdetect;
	mask &= ~bank->context.fallingdetect;
	bank->saved_datain &= ~mask;

	if (!may_lose_context)
		goto update_gpio_context_count;

	/*
	 * If going to OFF, remove triggering for all wkup domain
	 * non-wakeup GPIOs.  Otherwise spurious IRQs will be
	 * generated.  See OMAP2420 Errata item 1.101.
	 */
	if (!bank->loses_context && bank->enabled_non_wakeup_gpios) {
		nowake = bank->enabled_non_wakeup_gpios;
		omap_gpio_rmw(base + bank->regs->fallingdetect, nowake, ~nowake);
		omap_gpio_rmw(base + bank->regs->risingdetect, nowake, ~nowake);
	}

update_gpio_context_count:
	if (bank->get_context_loss_count)
		bank->context_loss_count =
				bank->get_context_loss_count(dev);

	omap_gpio_dbck_disable(bank);
}

static void omap_gpio_unidle(struct gpio_bank *bank)
{
	struct device *dev = bank->chip.parent;
	u32 l = 0, gen, gen0, gen1;
	int c;

	/*
	 * On the first resume during the probe, the context has not
	 * been initialised and so initialise it now. Also initialise
	 * the context loss count.
	 */
	if (bank->loses_context && !bank->context_valid) {
		omap_gpio_init_context(bank);

		if (bank->get_context_loss_count)
			bank->context_loss_count =
				bank->get_context_loss_count(dev);
	}

	omap_gpio_dbck_enable(bank);

	if (bank->loses_context) {
		if (!bank->get_context_loss_count) {
			omap_gpio_restore_context(bank);
		} else {
			c = bank->get_context_loss_count(dev);
			if (c != bank->context_loss_count) {
				omap_gpio_restore_context(bank);
			} else {
				return;
			}
		}
	} else {
		/* Restore changes done for OMAP2420 errata 1.101 */
		writel_relaxed(bank->context.fallingdetect,
			       bank->base + bank->regs->fallingdetect);
		writel_relaxed(bank->context.risingdetect,
			       bank->base + bank->regs->risingdetect);
	}

	l = readl_relaxed(bank->base + bank->regs->datain);

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

		old0 = readl_relaxed(bank->base + bank->regs->leveldetect0);
		old1 = readl_relaxed(bank->base + bank->regs->leveldetect1);

		if (!bank->regs->irqstatus_raw0) {
			writel_relaxed(old0 | gen, bank->base +
						bank->regs->leveldetect0);
			writel_relaxed(old1 | gen, bank->base +
						bank->regs->leveldetect1);
		}

		if (bank->regs->irqstatus_raw0) {
			writel_relaxed(old0 | l, bank->base +
						bank->regs->leveldetect0);
			writel_relaxed(old1 | l, bank->base +
						bank->regs->leveldetect1);
		}
		writel_relaxed(old0, bank->base + bank->regs->leveldetect0);
		writel_relaxed(old1, bank->base + bank->regs->leveldetect1);
	}
}

static int gpio_omap_cpu_notifier(struct notifier_block *nb,
				  unsigned long cmd, void *v)
{
	struct gpio_bank *bank;
	unsigned long flags;
	int ret = NOTIFY_OK;
	u32 isr, mask;

	bank = container_of(nb, struct gpio_bank, nb);

	raw_spin_lock_irqsave(&bank->lock, flags);
	if (bank->is_suspended)
		goto out_unlock;

	switch (cmd) {
	case CPU_CLUSTER_PM_ENTER:
		mask = omap_get_gpio_irqbank_mask(bank);
		isr = readl_relaxed(bank->base + bank->regs->irqstatus) & mask;
		if (isr) {
			ret = NOTIFY_BAD;
			break;
		}
		omap_gpio_idle(bank, true);
		break;
	case CPU_CLUSTER_PM_ENTER_FAILED:
	case CPU_CLUSTER_PM_EXIT:
		omap_gpio_unidle(bank);
		break;
	}

out_unlock:
	raw_spin_unlock_irqrestore(&bank->lock, flags);

	return ret;
}

static const struct omap_gpio_reg_offs omap2_gpio_regs = {
	.revision =		OMAP24XX_GPIO_REVISION,
	.sysconfig =		OMAP24XX_GPIO_SYSCONFIG,
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

static const struct omap_gpio_reg_offs omap4_gpio_regs = {
	.revision =		OMAP4_GPIO_REVISION,
	.sysconfig =		OMAP4_GPIO_SYSCONFIG,
	.direction =		OMAP4_GPIO_OE,
	.datain =		OMAP4_GPIO_DATAIN,
	.dataout =		OMAP4_GPIO_DATAOUT,
	.set_dataout =		OMAP4_GPIO_SETDATAOUT,
	.clr_dataout =		OMAP4_GPIO_CLEARDATAOUT,
	.irqstatus =		OMAP4_GPIO_IRQSTATUS0,
	.irqstatus2 =		OMAP4_GPIO_IRQSTATUS1,
	.irqstatus_raw0 =	OMAP4_GPIO_IRQSTATUSRAW0,
	.irqstatus_raw1 =	OMAP4_GPIO_IRQSTATUSRAW1,
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

static const struct omap_gpio_platform_data omap2_pdata = {
	.regs = &omap2_gpio_regs,
	.bank_width = 32,
	.dbck_flag = false,
};

static const struct omap_gpio_platform_data omap3_pdata = {
	.regs = &omap2_gpio_regs,
	.bank_width = 32,
	.dbck_flag = true,
};

static const struct omap_gpio_platform_data omap4_pdata = {
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

static int omap_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	const struct omap_gpio_platform_data *pdata;
	struct gpio_bank *bank;
	int ret;

	pdata = device_get_match_data(dev);

	pdata = pdata ?: dev_get_platdata(dev);
	if (!pdata)
		return -EINVAL;

	bank = devm_kzalloc(dev, sizeof(*bank), GFP_KERNEL);
	if (!bank)
		return -ENOMEM;

	bank->dev = dev;

	bank->irq = platform_get_irq(pdev, 0);
	if (bank->irq < 0)
		return bank->irq;

	bank->chip.parent = dev;
	bank->chip.owner = THIS_MODULE;
	bank->dbck_flag = pdata->dbck_flag;
	bank->stride = pdata->bank_stride;
	bank->width = pdata->bank_width;
	bank->is_mpuio = pdata->is_mpuio;
	bank->non_wakeup_gpios = pdata->non_wakeup_gpios;
	bank->regs = pdata->regs;

	if (node) {
		if (!of_property_read_bool(node, "ti,gpio-always-on"))
			bank->loses_context = true;
	} else {
		bank->loses_context = pdata->loses_context;

		if (bank->loses_context)
			bank->get_context_loss_count =
				pdata->get_context_loss_count;
	}

	if (bank->regs->set_dataout && bank->regs->clr_dataout)
		bank->set_dataout = omap_set_gpio_dataout_reg;
	else
		bank->set_dataout = omap_set_gpio_dataout_mask;

	raw_spin_lock_init(&bank->lock);
	raw_spin_lock_init(&bank->wa_lock);

	/* Static mapping, never released */
	bank->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(bank->base)) {
		return PTR_ERR(bank->base);
	}

	if (bank->dbck_flag) {
		bank->dbck = devm_clk_get(dev, "dbclk");
		if (IS_ERR(bank->dbck)) {
			dev_err(dev,
				"Could not get gpio dbck. Disable debounce\n");
			bank->dbck_flag = false;
		} else {
			clk_prepare(bank->dbck);
		}
	}

	platform_set_drvdata(pdev, bank);

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	if (bank->is_mpuio)
		omap_mpuio_init(bank);

	omap_gpio_mod_init(bank);

	ret = omap_gpio_chip_init(bank, dev);
	if (ret) {
		pm_runtime_put_sync(dev);
		pm_runtime_disable(dev);
		if (bank->dbck_flag)
			clk_unprepare(bank->dbck);
		return ret;
	}

	omap_gpio_show_rev(bank);

	bank->nb.notifier_call = gpio_omap_cpu_notifier;
	cpu_pm_register_notifier(&bank->nb);

	pm_runtime_put(dev);

	return 0;
}

static void omap_gpio_remove(struct platform_device *pdev)
{
	struct gpio_bank *bank = platform_get_drvdata(pdev);

	cpu_pm_unregister_notifier(&bank->nb);
	gpiochip_remove(&bank->chip);
	pm_runtime_disable(&pdev->dev);
	if (bank->dbck_flag)
		clk_unprepare(bank->dbck);
}

static int __maybe_unused omap_gpio_runtime_suspend(struct device *dev)
{
	struct gpio_bank *bank = dev_get_drvdata(dev);
	unsigned long flags;

	raw_spin_lock_irqsave(&bank->lock, flags);
	omap_gpio_idle(bank, true);
	bank->is_suspended = true;
	raw_spin_unlock_irqrestore(&bank->lock, flags);

	return 0;
}

static int __maybe_unused omap_gpio_runtime_resume(struct device *dev)
{
	struct gpio_bank *bank = dev_get_drvdata(dev);
	unsigned long flags;

	raw_spin_lock_irqsave(&bank->lock, flags);
	omap_gpio_unidle(bank);
	bank->is_suspended = false;
	raw_spin_unlock_irqrestore(&bank->lock, flags);

	return 0;
}

static int __maybe_unused omap_gpio_suspend(struct device *dev)
{
	struct gpio_bank *bank = dev_get_drvdata(dev);

	if (bank->is_suspended)
		return 0;

	bank->needs_resume = 1;

	return omap_gpio_runtime_suspend(dev);
}

static int __maybe_unused omap_gpio_resume(struct device *dev)
{
	struct gpio_bank *bank = dev_get_drvdata(dev);

	if (!bank->needs_resume)
		return 0;

	bank->needs_resume = 0;

	return omap_gpio_runtime_resume(dev);
}

static const struct dev_pm_ops gpio_pm_ops = {
	SET_RUNTIME_PM_OPS(omap_gpio_runtime_suspend, omap_gpio_runtime_resume,
									NULL)
	SET_LATE_SYSTEM_SLEEP_PM_OPS(omap_gpio_suspend, omap_gpio_resume)
};

static struct platform_driver omap_gpio_driver = {
	.probe		= omap_gpio_probe,
	.remove		= omap_gpio_remove,
	.driver		= {
		.name	= "omap_gpio",
		.pm	= &gpio_pm_ops,
		.of_match_table = omap_gpio_match,
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

static void __exit omap_gpio_exit(void)
{
	platform_driver_unregister(&omap_gpio_driver);
}
module_exit(omap_gpio_exit);

MODULE_DESCRIPTION("omap gpio driver");
MODULE_ALIAS("platform:gpio-omap");
MODULE_LICENSE("GPL v2");
