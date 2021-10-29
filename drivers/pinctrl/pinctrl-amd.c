// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPIO driver for AMD
 *
 * Copyright (c) 2014,2015 AMD Corporation.
 * Authors: Ken Xue <Ken.Xue@amd.com>
 *      Wu, Jeff <Jeff.Wu@amd.com>
 *
 * Contact Information: Nehal Shah <Nehal-bakulchandra.Shah@amd.com>
 *			Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 */

#include <linux/err.h>
#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/log2.h>
#include <linux/io.h>
#include <linux/gpio/driver.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/acpi.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/bitops.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>

#include "core.h"
#include "pinctrl-utils.h"
#include "pinctrl-amd.h"

static int amd_gpio_get_direction(struct gpio_chip *gc, unsigned offset)
{
	unsigned long flags;
	u32 pin_reg;
	struct amd_gpio *gpio_dev = gpiochip_get_data(gc);

	raw_spin_lock_irqsave(&gpio_dev->lock, flags);
	pin_reg = readl(gpio_dev->base + offset * 4);
	raw_spin_unlock_irqrestore(&gpio_dev->lock, flags);

	if (pin_reg & BIT(OUTPUT_ENABLE_OFF))
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static int amd_gpio_direction_input(struct gpio_chip *gc, unsigned offset)
{
	unsigned long flags;
	u32 pin_reg;
	struct amd_gpio *gpio_dev = gpiochip_get_data(gc);

	raw_spin_lock_irqsave(&gpio_dev->lock, flags);
	pin_reg = readl(gpio_dev->base + offset * 4);
	pin_reg &= ~BIT(OUTPUT_ENABLE_OFF);
	writel(pin_reg, gpio_dev->base + offset * 4);
	raw_spin_unlock_irqrestore(&gpio_dev->lock, flags);

	return 0;
}

static int amd_gpio_direction_output(struct gpio_chip *gc, unsigned offset,
		int value)
{
	u32 pin_reg;
	unsigned long flags;
	struct amd_gpio *gpio_dev = gpiochip_get_data(gc);

	raw_spin_lock_irqsave(&gpio_dev->lock, flags);
	pin_reg = readl(gpio_dev->base + offset * 4);
	pin_reg |= BIT(OUTPUT_ENABLE_OFF);
	if (value)
		pin_reg |= BIT(OUTPUT_VALUE_OFF);
	else
		pin_reg &= ~BIT(OUTPUT_VALUE_OFF);
	writel(pin_reg, gpio_dev->base + offset * 4);
	raw_spin_unlock_irqrestore(&gpio_dev->lock, flags);

	return 0;
}

static int amd_gpio_get_value(struct gpio_chip *gc, unsigned offset)
{
	u32 pin_reg;
	unsigned long flags;
	struct amd_gpio *gpio_dev = gpiochip_get_data(gc);

	raw_spin_lock_irqsave(&gpio_dev->lock, flags);
	pin_reg = readl(gpio_dev->base + offset * 4);
	raw_spin_unlock_irqrestore(&gpio_dev->lock, flags);

	return !!(pin_reg & BIT(PIN_STS_OFF));
}

static void amd_gpio_set_value(struct gpio_chip *gc, unsigned offset, int value)
{
	u32 pin_reg;
	unsigned long flags;
	struct amd_gpio *gpio_dev = gpiochip_get_data(gc);

	raw_spin_lock_irqsave(&gpio_dev->lock, flags);
	pin_reg = readl(gpio_dev->base + offset * 4);
	if (value)
		pin_reg |= BIT(OUTPUT_VALUE_OFF);
	else
		pin_reg &= ~BIT(OUTPUT_VALUE_OFF);
	writel(pin_reg, gpio_dev->base + offset * 4);
	raw_spin_unlock_irqrestore(&gpio_dev->lock, flags);
}

static int amd_gpio_set_debounce(struct gpio_chip *gc, unsigned offset,
		unsigned debounce)
{
	u32 time;
	u32 pin_reg;
	int ret = 0;
	unsigned long flags;
	struct amd_gpio *gpio_dev = gpiochip_get_data(gc);

	raw_spin_lock_irqsave(&gpio_dev->lock, flags);
	pin_reg = readl(gpio_dev->base + offset * 4);

	if (debounce) {
		pin_reg |= DB_TYPE_REMOVE_GLITCH << DB_CNTRL_OFF;
		pin_reg &= ~DB_TMR_OUT_MASK;
		/*
		Debounce	Debounce	Timer	Max
		TmrLarge	TmrOutUnit	Unit	Debounce
							Time
		0	0	61 usec (2 RtcClk)	976 usec
		0	1	244 usec (8 RtcClk)	3.9 msec
		1	0	15.6 msec (512 RtcClk)	250 msec
		1	1	62.5 msec (2048 RtcClk)	1 sec
		*/

		if (debounce < 61) {
			pin_reg |= 1;
			pin_reg &= ~BIT(DB_TMR_OUT_UNIT_OFF);
			pin_reg &= ~BIT(DB_TMR_LARGE_OFF);
		} else if (debounce < 976) {
			time = debounce / 61;
			pin_reg |= time & DB_TMR_OUT_MASK;
			pin_reg &= ~BIT(DB_TMR_OUT_UNIT_OFF);
			pin_reg &= ~BIT(DB_TMR_LARGE_OFF);
		} else if (debounce < 3900) {
			time = debounce / 244;
			pin_reg |= time & DB_TMR_OUT_MASK;
			pin_reg |= BIT(DB_TMR_OUT_UNIT_OFF);
			pin_reg &= ~BIT(DB_TMR_LARGE_OFF);
		} else if (debounce < 250000) {
			time = debounce / 15625;
			pin_reg |= time & DB_TMR_OUT_MASK;
			pin_reg &= ~BIT(DB_TMR_OUT_UNIT_OFF);
			pin_reg |= BIT(DB_TMR_LARGE_OFF);
		} else if (debounce < 1000000) {
			time = debounce / 62500;
			pin_reg |= time & DB_TMR_OUT_MASK;
			pin_reg |= BIT(DB_TMR_OUT_UNIT_OFF);
			pin_reg |= BIT(DB_TMR_LARGE_OFF);
		} else {
			pin_reg &= ~(DB_CNTRl_MASK << DB_CNTRL_OFF);
			ret = -EINVAL;
		}
	} else {
		pin_reg &= ~BIT(DB_TMR_OUT_UNIT_OFF);
		pin_reg &= ~BIT(DB_TMR_LARGE_OFF);
		pin_reg &= ~DB_TMR_OUT_MASK;
		pin_reg &= ~(DB_CNTRl_MASK << DB_CNTRL_OFF);
	}
	writel(pin_reg, gpio_dev->base + offset * 4);
	raw_spin_unlock_irqrestore(&gpio_dev->lock, flags);

	return ret;
}

static int amd_gpio_set_config(struct gpio_chip *gc, unsigned offset,
			       unsigned long config)
{
	u32 debounce;

	if (pinconf_to_config_param(config) != PIN_CONFIG_INPUT_DEBOUNCE)
		return -ENOTSUPP;

	debounce = pinconf_to_config_argument(config);
	return amd_gpio_set_debounce(gc, offset, debounce);
}

#ifdef CONFIG_DEBUG_FS
static void amd_gpio_dbg_show(struct seq_file *s, struct gpio_chip *gc)
{
	u32 pin_reg;
	u32 db_cntrl;
	unsigned long flags;
	unsigned int bank, i, pin_num;
	struct amd_gpio *gpio_dev = gpiochip_get_data(gc);

	bool tmr_out_unit;
	unsigned int time;
	unsigned int unit;
	bool tmr_large;

	char *level_trig;
	char *active_level;
	char *interrupt_enable;
	char *interrupt_mask;
	char *wake_cntrl0;
	char *wake_cntrl1;
	char *wake_cntrl2;
	char *pin_sts;
	char *pull_up_sel;
	char *pull_up_enable;
	char *pull_down_enable;
	char *output_value;
	char *output_enable;
	char debounce_value[40];
	char *debounce_enable;

	for (bank = 0; bank < gpio_dev->hwbank_num; bank++) {
		seq_printf(s, "GPIO bank%d\t", bank);

		switch (bank) {
		case 0:
			i = 0;
			pin_num = AMD_GPIO_PINS_BANK0;
			break;
		case 1:
			i = 64;
			pin_num = AMD_GPIO_PINS_BANK1 + i;
			break;
		case 2:
			i = 128;
			pin_num = AMD_GPIO_PINS_BANK2 + i;
			break;
		case 3:
			i = 192;
			pin_num = AMD_GPIO_PINS_BANK3 + i;
			break;
		default:
			/* Illegal bank number, ignore */
			continue;
		}
		for (; i < pin_num; i++) {
			seq_printf(s, "pin%d\t", i);
			raw_spin_lock_irqsave(&gpio_dev->lock, flags);
			pin_reg = readl(gpio_dev->base + i * 4);
			raw_spin_unlock_irqrestore(&gpio_dev->lock, flags);

			if (pin_reg & BIT(INTERRUPT_ENABLE_OFF)) {
				u8 level = (pin_reg >> ACTIVE_LEVEL_OFF) &
						ACTIVE_LEVEL_MASK;
				interrupt_enable = "interrupt is enabled|";

				if (level == ACTIVE_LEVEL_HIGH)
					active_level = "Active high|";
				else if (level == ACTIVE_LEVEL_LOW)
					active_level = "Active low|";
				else if (!(pin_reg & BIT(LEVEL_TRIG_OFF)) &&
					 level == ACTIVE_LEVEL_BOTH)
					active_level = "Active on both|";
				else
					active_level = "Unknown Active level|";

				if (pin_reg & BIT(LEVEL_TRIG_OFF))
					level_trig = "Level trigger|";
				else
					level_trig = "Edge trigger|";

			} else {
				interrupt_enable =
					"interrupt is disabled|";
				active_level = " ";
				level_trig = " ";
			}

			if (pin_reg & BIT(INTERRUPT_MASK_OFF))
				interrupt_mask =
					"interrupt is unmasked|";
			else
				interrupt_mask =
					"interrupt is masked|";

			if (pin_reg & BIT(WAKE_CNTRL_OFF_S0I3))
				wake_cntrl0 = "enable wakeup in S0i3 state|";
			else
				wake_cntrl0 = "disable wakeup in S0i3 state|";

			if (pin_reg & BIT(WAKE_CNTRL_OFF_S3))
				wake_cntrl1 = "enable wakeup in S3 state|";
			else
				wake_cntrl1 = "disable wakeup in S3 state|";

			if (pin_reg & BIT(WAKE_CNTRL_OFF_S4))
				wake_cntrl2 = "enable wakeup in S4/S5 state|";
			else
				wake_cntrl2 = "disable wakeup in S4/S5 state|";

			if (pin_reg & BIT(PULL_UP_ENABLE_OFF)) {
				pull_up_enable = "pull-up is enabled|";
				if (pin_reg & BIT(PULL_UP_SEL_OFF))
					pull_up_sel = "8k pull-up|";
				else
					pull_up_sel = "4k pull-up|";
			} else {
				pull_up_enable = "pull-up is disabled|";
				pull_up_sel = " ";
			}

			if (pin_reg & BIT(PULL_DOWN_ENABLE_OFF))
				pull_down_enable = "pull-down is enabled|";
			else
				pull_down_enable = "Pull-down is disabled|";

			if (pin_reg & BIT(OUTPUT_ENABLE_OFF)) {
				pin_sts = " ";
				output_enable = "output is enabled|";
				if (pin_reg & BIT(OUTPUT_VALUE_OFF))
					output_value = "output is high|";
				else
					output_value = "output is low|";
			} else {
				output_enable = "output is disabled|";
				output_value = " ";

				if (pin_reg & BIT(PIN_STS_OFF))
					pin_sts = "input is high|";
				else
					pin_sts = "input is low|";
			}

			db_cntrl = (DB_CNTRl_MASK << DB_CNTRL_OFF) & pin_reg;
			if (db_cntrl) {
				tmr_out_unit = pin_reg & BIT(DB_TMR_OUT_UNIT_OFF);
				tmr_large = pin_reg & BIT(DB_TMR_LARGE_OFF);
				time = pin_reg & DB_TMR_OUT_MASK;
				if (tmr_large) {
					if (tmr_out_unit)
						unit = 62500;
					else
						unit = 15625;
				} else {
					if (tmr_out_unit)
						unit = 244;
					else
						unit = 61;
				}
				if ((DB_TYPE_REMOVE_GLITCH << DB_CNTRL_OFF) == db_cntrl)
					debounce_enable = "debouncing filter (high and low) enabled|";
				else if ((DB_TYPE_PRESERVE_LOW_GLITCH << DB_CNTRL_OFF) == db_cntrl)
					debounce_enable = "debouncing filter (low) enabled|";
				else
					debounce_enable = "debouncing filter (high) enabled|";

				snprintf(debounce_value, sizeof(debounce_value),
					 "debouncing timeout is %u (us)|", time * unit);
			} else {
				debounce_enable = "debouncing filter disabled|";
				snprintf(debounce_value, sizeof(debounce_value), " ");
			}

			seq_printf(s, "%s %s %s %s %s %s\n"
				" %s %s %s %s %s %s %s %s %s 0x%x\n",
				level_trig, active_level, interrupt_enable,
				interrupt_mask, wake_cntrl0, wake_cntrl1,
				wake_cntrl2, pin_sts, pull_up_sel,
				pull_up_enable, pull_down_enable,
				output_value, output_enable,
				debounce_enable, debounce_value, pin_reg);
		}
	}
}
#else
#define amd_gpio_dbg_show NULL
#endif

static void amd_gpio_irq_enable(struct irq_data *d)
{
	u32 pin_reg;
	unsigned long flags;
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct amd_gpio *gpio_dev = gpiochip_get_data(gc);

	raw_spin_lock_irqsave(&gpio_dev->lock, flags);
	pin_reg = readl(gpio_dev->base + (d->hwirq)*4);
	pin_reg |= BIT(INTERRUPT_ENABLE_OFF);
	pin_reg |= BIT(INTERRUPT_MASK_OFF);
	writel(pin_reg, gpio_dev->base + (d->hwirq)*4);
	raw_spin_unlock_irqrestore(&gpio_dev->lock, flags);
}

static void amd_gpio_irq_disable(struct irq_data *d)
{
	u32 pin_reg;
	unsigned long flags;
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct amd_gpio *gpio_dev = gpiochip_get_data(gc);

	raw_spin_lock_irqsave(&gpio_dev->lock, flags);
	pin_reg = readl(gpio_dev->base + (d->hwirq)*4);
	pin_reg &= ~BIT(INTERRUPT_ENABLE_OFF);
	pin_reg &= ~BIT(INTERRUPT_MASK_OFF);
	writel(pin_reg, gpio_dev->base + (d->hwirq)*4);
	raw_spin_unlock_irqrestore(&gpio_dev->lock, flags);
}

static void amd_gpio_irq_mask(struct irq_data *d)
{
	u32 pin_reg;
	unsigned long flags;
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct amd_gpio *gpio_dev = gpiochip_get_data(gc);

	raw_spin_lock_irqsave(&gpio_dev->lock, flags);
	pin_reg = readl(gpio_dev->base + (d->hwirq)*4);
	pin_reg &= ~BIT(INTERRUPT_MASK_OFF);
	writel(pin_reg, gpio_dev->base + (d->hwirq)*4);
	raw_spin_unlock_irqrestore(&gpio_dev->lock, flags);
}

static void amd_gpio_irq_unmask(struct irq_data *d)
{
	u32 pin_reg;
	unsigned long flags;
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct amd_gpio *gpio_dev = gpiochip_get_data(gc);

	raw_spin_lock_irqsave(&gpio_dev->lock, flags);
	pin_reg = readl(gpio_dev->base + (d->hwirq)*4);
	pin_reg |= BIT(INTERRUPT_MASK_OFF);
	writel(pin_reg, gpio_dev->base + (d->hwirq)*4);
	raw_spin_unlock_irqrestore(&gpio_dev->lock, flags);
}

static int amd_gpio_irq_set_wake(struct irq_data *d, unsigned int on)
{
	u32 pin_reg;
	unsigned long flags;
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct amd_gpio *gpio_dev = gpiochip_get_data(gc);
	u32 wake_mask = BIT(WAKE_CNTRL_OFF_S0I3) | BIT(WAKE_CNTRL_OFF_S3);
	int err;

	raw_spin_lock_irqsave(&gpio_dev->lock, flags);
	pin_reg = readl(gpio_dev->base + (d->hwirq)*4);

	if (on)
		pin_reg |= wake_mask;
	else
		pin_reg &= ~wake_mask;

	writel(pin_reg, gpio_dev->base + (d->hwirq)*4);
	raw_spin_unlock_irqrestore(&gpio_dev->lock, flags);

	if (on)
		err = enable_irq_wake(gpio_dev->irq);
	else
		err = disable_irq_wake(gpio_dev->irq);

	if (err)
		dev_err(&gpio_dev->pdev->dev, "failed to %s wake-up interrupt\n",
			on ? "enable" : "disable");

	return 0;
}

static void amd_gpio_irq_eoi(struct irq_data *d)
{
	u32 reg;
	unsigned long flags;
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct amd_gpio *gpio_dev = gpiochip_get_data(gc);

	raw_spin_lock_irqsave(&gpio_dev->lock, flags);
	reg = readl(gpio_dev->base + WAKE_INT_MASTER_REG);
	reg |= EOI_MASK;
	writel(reg, gpio_dev->base + WAKE_INT_MASTER_REG);
	raw_spin_unlock_irqrestore(&gpio_dev->lock, flags);
}

static int amd_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	int ret = 0;
	u32 pin_reg, pin_reg_irq_en, mask;
	unsigned long flags;
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct amd_gpio *gpio_dev = gpiochip_get_data(gc);

	raw_spin_lock_irqsave(&gpio_dev->lock, flags);
	pin_reg = readl(gpio_dev->base + (d->hwirq)*4);

	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_RISING:
		pin_reg &= ~BIT(LEVEL_TRIG_OFF);
		pin_reg &= ~(ACTIVE_LEVEL_MASK << ACTIVE_LEVEL_OFF);
		pin_reg |= ACTIVE_HIGH << ACTIVE_LEVEL_OFF;
		irq_set_handler_locked(d, handle_edge_irq);
		break;

	case IRQ_TYPE_EDGE_FALLING:
		pin_reg &= ~BIT(LEVEL_TRIG_OFF);
		pin_reg &= ~(ACTIVE_LEVEL_MASK << ACTIVE_LEVEL_OFF);
		pin_reg |= ACTIVE_LOW << ACTIVE_LEVEL_OFF;
		irq_set_handler_locked(d, handle_edge_irq);
		break;

	case IRQ_TYPE_EDGE_BOTH:
		pin_reg &= ~BIT(LEVEL_TRIG_OFF);
		pin_reg &= ~(ACTIVE_LEVEL_MASK << ACTIVE_LEVEL_OFF);
		pin_reg |= BOTH_EADGE << ACTIVE_LEVEL_OFF;
		irq_set_handler_locked(d, handle_edge_irq);
		break;

	case IRQ_TYPE_LEVEL_HIGH:
		pin_reg |= LEVEL_TRIGGER << LEVEL_TRIG_OFF;
		pin_reg &= ~(ACTIVE_LEVEL_MASK << ACTIVE_LEVEL_OFF);
		pin_reg |= ACTIVE_HIGH << ACTIVE_LEVEL_OFF;
		irq_set_handler_locked(d, handle_level_irq);
		break;

	case IRQ_TYPE_LEVEL_LOW:
		pin_reg |= LEVEL_TRIGGER << LEVEL_TRIG_OFF;
		pin_reg &= ~(ACTIVE_LEVEL_MASK << ACTIVE_LEVEL_OFF);
		pin_reg |= ACTIVE_LOW << ACTIVE_LEVEL_OFF;
		irq_set_handler_locked(d, handle_level_irq);
		break;

	case IRQ_TYPE_NONE:
		break;

	default:
		dev_err(&gpio_dev->pdev->dev, "Invalid type value\n");
		ret = -EINVAL;
	}

	pin_reg |= CLR_INTR_STAT << INTERRUPT_STS_OFF;
	/*
	 * If WAKE_INT_MASTER_REG.MaskStsEn is set, a software write to the
	 * debounce registers of any GPIO will block wake/interrupt status
	 * generation for *all* GPIOs for a length of time that depends on
	 * WAKE_INT_MASTER_REG.MaskStsLength[11:0].  During this period the
	 * INTERRUPT_ENABLE bit will read as 0.
	 *
	 * We temporarily enable irq for the GPIO whose configuration is
	 * changing, and then wait for it to read back as 1 to know when
	 * debounce has settled and then disable the irq again.
	 * We do this polling with the spinlock held to ensure other GPIO
	 * access routines do not read an incorrect value for the irq enable
	 * bit of other GPIOs.  We keep the GPIO masked while polling to avoid
	 * spurious irqs, and disable the irq again after polling.
	 */
	mask = BIT(INTERRUPT_ENABLE_OFF);
	pin_reg_irq_en = pin_reg;
	pin_reg_irq_en |= mask;
	pin_reg_irq_en &= ~BIT(INTERRUPT_MASK_OFF);
	writel(pin_reg_irq_en, gpio_dev->base + (d->hwirq)*4);
	while ((readl(gpio_dev->base + (d->hwirq)*4) & mask) != mask)
		continue;
	writel(pin_reg, gpio_dev->base + (d->hwirq)*4);
	raw_spin_unlock_irqrestore(&gpio_dev->lock, flags);

	return ret;
}

static void amd_irq_ack(struct irq_data *d)
{
	/*
	 * based on HW design,there is no need to ack HW
	 * before handle current irq. But this routine is
	 * necessary for handle_edge_irq
	*/
}

static struct irq_chip amd_gpio_irqchip = {
	.name         = "amd_gpio",
	.irq_ack      = amd_irq_ack,
	.irq_enable   = amd_gpio_irq_enable,
	.irq_disable  = amd_gpio_irq_disable,
	.irq_mask     = amd_gpio_irq_mask,
	.irq_unmask   = amd_gpio_irq_unmask,
	.irq_set_wake = amd_gpio_irq_set_wake,
	.irq_eoi      = amd_gpio_irq_eoi,
	.irq_set_type = amd_gpio_irq_set_type,
	/*
	 * We need to set IRQCHIP_ENABLE_WAKEUP_ON_SUSPEND so that a wake event
	 * also generates an IRQ. We need the IRQ so the irq_handler can clear
	 * the wake event. Otherwise the wake event will never clear and
	 * prevent the system from suspending.
	 */
	.flags        = IRQCHIP_ENABLE_WAKEUP_ON_SUSPEND,
};

#define PIN_IRQ_PENDING	(BIT(INTERRUPT_STS_OFF) | BIT(WAKE_STS_OFF))

static irqreturn_t amd_gpio_irq_handler(int irq, void *dev_id)
{
	struct amd_gpio *gpio_dev = dev_id;
	struct gpio_chip *gc = &gpio_dev->gc;
	irqreturn_t ret = IRQ_NONE;
	unsigned int i, irqnr;
	unsigned long flags;
	u32 __iomem *regs;
	u32  regval;
	u64 status, mask;

	/* Read the wake status */
	raw_spin_lock_irqsave(&gpio_dev->lock, flags);
	status = readl(gpio_dev->base + WAKE_INT_STATUS_REG1);
	status <<= 32;
	status |= readl(gpio_dev->base + WAKE_INT_STATUS_REG0);
	raw_spin_unlock_irqrestore(&gpio_dev->lock, flags);

	/* Bit 0-45 contain the relevant status bits */
	status &= (1ULL << 46) - 1;
	regs = gpio_dev->base;
	for (mask = 1, irqnr = 0; status; mask <<= 1, regs += 4, irqnr += 4) {
		if (!(status & mask))
			continue;
		status &= ~mask;

		/* Each status bit covers four pins */
		for (i = 0; i < 4; i++) {
			regval = readl(regs + i);
			if (!(regval & PIN_IRQ_PENDING) ||
			    !(regval & BIT(INTERRUPT_MASK_OFF)))
				continue;
			generic_handle_domain_irq(gc->irq.domain, irqnr + i);

			/* Clear interrupt.
			 * We must read the pin register again, in case the
			 * value was changed while executing
			 * generic_handle_domain_irq() above.
			 * If we didn't find a mapping for the interrupt,
			 * disable it in order to avoid a system hang caused
			 * by an interrupt storm.
			 */
			raw_spin_lock_irqsave(&gpio_dev->lock, flags);
			regval = readl(regs + i);
			if (irq == 0) {
				regval &= ~BIT(INTERRUPT_ENABLE_OFF);
				dev_dbg(&gpio_dev->pdev->dev,
					"Disabling spurious GPIO IRQ %d\n",
					irqnr + i);
			}
			writel(regval, regs + i);
			raw_spin_unlock_irqrestore(&gpio_dev->lock, flags);
			ret = IRQ_HANDLED;
		}
	}

	/* Signal EOI to the GPIO unit */
	raw_spin_lock_irqsave(&gpio_dev->lock, flags);
	regval = readl(gpio_dev->base + WAKE_INT_MASTER_REG);
	regval |= EOI_MASK;
	writel(regval, gpio_dev->base + WAKE_INT_MASTER_REG);
	raw_spin_unlock_irqrestore(&gpio_dev->lock, flags);

	return ret;
}

static int amd_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct amd_gpio *gpio_dev = pinctrl_dev_get_drvdata(pctldev);

	return gpio_dev->ngroups;
}

static const char *amd_get_group_name(struct pinctrl_dev *pctldev,
				      unsigned group)
{
	struct amd_gpio *gpio_dev = pinctrl_dev_get_drvdata(pctldev);

	return gpio_dev->groups[group].name;
}

static int amd_get_group_pins(struct pinctrl_dev *pctldev,
			      unsigned group,
			      const unsigned **pins,
			      unsigned *num_pins)
{
	struct amd_gpio *gpio_dev = pinctrl_dev_get_drvdata(pctldev);

	*pins = gpio_dev->groups[group].pins;
	*num_pins = gpio_dev->groups[group].npins;
	return 0;
}

static const struct pinctrl_ops amd_pinctrl_ops = {
	.get_groups_count	= amd_get_groups_count,
	.get_group_name		= amd_get_group_name,
	.get_group_pins		= amd_get_group_pins,
#ifdef CONFIG_OF
	.dt_node_to_map		= pinconf_generic_dt_node_to_map_group,
	.dt_free_map		= pinctrl_utils_free_map,
#endif
};

static int amd_pinconf_get(struct pinctrl_dev *pctldev,
			  unsigned int pin,
			  unsigned long *config)
{
	u32 pin_reg;
	unsigned arg;
	unsigned long flags;
	struct amd_gpio *gpio_dev = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);

	raw_spin_lock_irqsave(&gpio_dev->lock, flags);
	pin_reg = readl(gpio_dev->base + pin*4);
	raw_spin_unlock_irqrestore(&gpio_dev->lock, flags);
	switch (param) {
	case PIN_CONFIG_INPUT_DEBOUNCE:
		arg = pin_reg & DB_TMR_OUT_MASK;
		break;

	case PIN_CONFIG_BIAS_PULL_DOWN:
		arg = (pin_reg >> PULL_DOWN_ENABLE_OFF) & BIT(0);
		break;

	case PIN_CONFIG_BIAS_PULL_UP:
		arg = (pin_reg >> PULL_UP_SEL_OFF) & (BIT(0) | BIT(1));
		break;

	case PIN_CONFIG_DRIVE_STRENGTH:
		arg = (pin_reg >> DRV_STRENGTH_SEL_OFF) & DRV_STRENGTH_SEL_MASK;
		break;

	default:
		dev_err(&gpio_dev->pdev->dev, "Invalid config param %04x\n",
			param);
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int amd_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
				unsigned long *configs, unsigned num_configs)
{
	int i;
	u32 arg;
	int ret = 0;
	u32 pin_reg;
	unsigned long flags;
	enum pin_config_param param;
	struct amd_gpio *gpio_dev = pinctrl_dev_get_drvdata(pctldev);

	raw_spin_lock_irqsave(&gpio_dev->lock, flags);
	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);
		pin_reg = readl(gpio_dev->base + pin*4);

		switch (param) {
		case PIN_CONFIG_INPUT_DEBOUNCE:
			pin_reg &= ~DB_TMR_OUT_MASK;
			pin_reg |= arg & DB_TMR_OUT_MASK;
			break;

		case PIN_CONFIG_BIAS_PULL_DOWN:
			pin_reg &= ~BIT(PULL_DOWN_ENABLE_OFF);
			pin_reg |= (arg & BIT(0)) << PULL_DOWN_ENABLE_OFF;
			break;

		case PIN_CONFIG_BIAS_PULL_UP:
			pin_reg &= ~BIT(PULL_UP_SEL_OFF);
			pin_reg |= (arg & BIT(0)) << PULL_UP_SEL_OFF;
			pin_reg &= ~BIT(PULL_UP_ENABLE_OFF);
			pin_reg |= ((arg>>1) & BIT(0)) << PULL_UP_ENABLE_OFF;
			break;

		case PIN_CONFIG_DRIVE_STRENGTH:
			pin_reg &= ~(DRV_STRENGTH_SEL_MASK
					<< DRV_STRENGTH_SEL_OFF);
			pin_reg |= (arg & DRV_STRENGTH_SEL_MASK)
					<< DRV_STRENGTH_SEL_OFF;
			break;

		default:
			dev_err(&gpio_dev->pdev->dev,
				"Invalid config param %04x\n", param);
			ret = -ENOTSUPP;
		}

		writel(pin_reg, gpio_dev->base + pin*4);
	}
	raw_spin_unlock_irqrestore(&gpio_dev->lock, flags);

	return ret;
}

static int amd_pinconf_group_get(struct pinctrl_dev *pctldev,
				unsigned int group,
				unsigned long *config)
{
	const unsigned *pins;
	unsigned npins;
	int ret;

	ret = amd_get_group_pins(pctldev, group, &pins, &npins);
	if (ret)
		return ret;

	if (amd_pinconf_get(pctldev, pins[0], config))
			return -ENOTSUPP;

	return 0;
}

static int amd_pinconf_group_set(struct pinctrl_dev *pctldev,
				unsigned group, unsigned long *configs,
				unsigned num_configs)
{
	const unsigned *pins;
	unsigned npins;
	int i, ret;

	ret = amd_get_group_pins(pctldev, group, &pins, &npins);
	if (ret)
		return ret;
	for (i = 0; i < npins; i++) {
		if (amd_pinconf_set(pctldev, pins[i], configs, num_configs))
			return -ENOTSUPP;
	}
	return 0;
}

static const struct pinconf_ops amd_pinconf_ops = {
	.pin_config_get		= amd_pinconf_get,
	.pin_config_set		= amd_pinconf_set,
	.pin_config_group_get = amd_pinconf_group_get,
	.pin_config_group_set = amd_pinconf_group_set,
};

#ifdef CONFIG_PM_SLEEP
static bool amd_gpio_should_save(struct amd_gpio *gpio_dev, unsigned int pin)
{
	const struct pin_desc *pd = pin_desc_get(gpio_dev->pctrl, pin);

	if (!pd)
		return false;

	/*
	 * Only restore the pin if it is actually in use by the kernel (or
	 * by userspace).
	 */
	if (pd->mux_owner || pd->gpio_owner ||
	    gpiochip_line_is_irq(&gpio_dev->gc, pin))
		return true;

	return false;
}

static int amd_gpio_suspend(struct device *dev)
{
	struct amd_gpio *gpio_dev = dev_get_drvdata(dev);
	struct pinctrl_desc *desc = gpio_dev->pctrl->desc;
	int i;

	for (i = 0; i < desc->npins; i++) {
		int pin = desc->pins[i].number;

		if (!amd_gpio_should_save(gpio_dev, pin))
			continue;

		gpio_dev->saved_regs[i] = readl(gpio_dev->base + pin*4);
	}

	return 0;
}

static int amd_gpio_resume(struct device *dev)
{
	struct amd_gpio *gpio_dev = dev_get_drvdata(dev);
	struct pinctrl_desc *desc = gpio_dev->pctrl->desc;
	int i;

	for (i = 0; i < desc->npins; i++) {
		int pin = desc->pins[i].number;

		if (!amd_gpio_should_save(gpio_dev, pin))
			continue;

		writel(gpio_dev->saved_regs[i], gpio_dev->base + pin*4);
	}

	return 0;
}

static const struct dev_pm_ops amd_gpio_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(amd_gpio_suspend,
				     amd_gpio_resume)
};
#endif

static struct pinctrl_desc amd_pinctrl_desc = {
	.pins	= kerncz_pins,
	.npins = ARRAY_SIZE(kerncz_pins),
	.pctlops = &amd_pinctrl_ops,
	.confops = &amd_pinconf_ops,
	.owner = THIS_MODULE,
};

static int amd_gpio_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct resource *res;
	struct amd_gpio *gpio_dev;
	struct gpio_irq_chip *girq;

	gpio_dev = devm_kzalloc(&pdev->dev,
				sizeof(struct amd_gpio), GFP_KERNEL);
	if (!gpio_dev)
		return -ENOMEM;

	raw_spin_lock_init(&gpio_dev->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get gpio io resource.\n");
		return -EINVAL;
	}

	gpio_dev->base = devm_ioremap(&pdev->dev, res->start,
						resource_size(res));
	if (!gpio_dev->base)
		return -ENOMEM;

	gpio_dev->irq = platform_get_irq(pdev, 0);
	if (gpio_dev->irq < 0)
		return gpio_dev->irq;

#ifdef CONFIG_PM_SLEEP
	gpio_dev->saved_regs = devm_kcalloc(&pdev->dev, amd_pinctrl_desc.npins,
					    sizeof(*gpio_dev->saved_regs),
					    GFP_KERNEL);
	if (!gpio_dev->saved_regs)
		return -ENOMEM;
#endif

	gpio_dev->pdev = pdev;
	gpio_dev->gc.get_direction	= amd_gpio_get_direction;
	gpio_dev->gc.direction_input	= amd_gpio_direction_input;
	gpio_dev->gc.direction_output	= amd_gpio_direction_output;
	gpio_dev->gc.get			= amd_gpio_get_value;
	gpio_dev->gc.set			= amd_gpio_set_value;
	gpio_dev->gc.set_config		= amd_gpio_set_config;
	gpio_dev->gc.dbg_show		= amd_gpio_dbg_show;

	gpio_dev->gc.base		= -1;
	gpio_dev->gc.label			= pdev->name;
	gpio_dev->gc.owner			= THIS_MODULE;
	gpio_dev->gc.parent			= &pdev->dev;
	gpio_dev->gc.ngpio			= resource_size(res) / 4;
#if defined(CONFIG_OF_GPIO)
	gpio_dev->gc.of_node			= pdev->dev.of_node;
#endif

	gpio_dev->hwbank_num = gpio_dev->gc.ngpio / 64;
	gpio_dev->groups = kerncz_groups;
	gpio_dev->ngroups = ARRAY_SIZE(kerncz_groups);

	amd_pinctrl_desc.name = dev_name(&pdev->dev);
	gpio_dev->pctrl = devm_pinctrl_register(&pdev->dev, &amd_pinctrl_desc,
						gpio_dev);
	if (IS_ERR(gpio_dev->pctrl)) {
		dev_err(&pdev->dev, "Couldn't register pinctrl driver\n");
		return PTR_ERR(gpio_dev->pctrl);
	}

	girq = &gpio_dev->gc.irq;
	girq->chip = &amd_gpio_irqchip;
	/* This will let us handle the parent IRQ in the driver */
	girq->parent_handler = NULL;
	girq->num_parents = 0;
	girq->parents = NULL;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_simple_irq;

	ret = gpiochip_add_data(&gpio_dev->gc, gpio_dev);
	if (ret)
		return ret;

	ret = gpiochip_add_pin_range(&gpio_dev->gc, dev_name(&pdev->dev),
				0, 0, gpio_dev->gc.ngpio);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add pin range\n");
		goto out2;
	}

	ret = devm_request_irq(&pdev->dev, gpio_dev->irq, amd_gpio_irq_handler,
			       IRQF_SHARED, KBUILD_MODNAME, gpio_dev);
	if (ret)
		goto out2;

	platform_set_drvdata(pdev, gpio_dev);

	dev_dbg(&pdev->dev, "amd gpio driver loaded\n");
	return ret;

out2:
	gpiochip_remove(&gpio_dev->gc);

	return ret;
}

static int amd_gpio_remove(struct platform_device *pdev)
{
	struct amd_gpio *gpio_dev;

	gpio_dev = platform_get_drvdata(pdev);

	gpiochip_remove(&gpio_dev->gc);

	return 0;
}

#ifdef CONFIG_ACPI
static const struct acpi_device_id amd_gpio_acpi_match[] = {
	{ "AMD0030", 0 },
	{ "AMDI0030", 0},
	{ "AMDI0031", 0},
	{ },
};
MODULE_DEVICE_TABLE(acpi, amd_gpio_acpi_match);
#endif

static struct platform_driver amd_gpio_driver = {
	.driver		= {
		.name	= "amd_gpio",
		.acpi_match_table = ACPI_PTR(amd_gpio_acpi_match),
#ifdef CONFIG_PM_SLEEP
		.pm	= &amd_gpio_pm_ops,
#endif
	},
	.probe		= amd_gpio_probe,
	.remove		= amd_gpio_remove,
};

module_platform_driver(amd_gpio_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Ken Xue <Ken.Xue@amd.com>, Jeff Wu <Jeff.Wu@amd.com>");
MODULE_DESCRIPTION("AMD GPIO pinctrl driver");
