// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPIO driver for AMD
 *
 * Copyright (c) 2014,2015 AMD Corporation.
 * Authors: Ken Xue <Ken.Xue@amd.com>
 *      Wu, Jeff <Jeff.Wu@amd.com>
 *
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
#include <linux/pinctrl/pinmux.h>

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
	bool tmr_large;

	char *level_trig;
	char *active_level;
	char *interrupt_mask;
	char *wake_cntrl0;
	char *wake_cntrl1;
	char *wake_cntrl2;
	char *pin_sts;
	char *interrupt_sts;
	char *wake_sts;
	char *pull_up_sel;
	char *orientation;
	char debounce_value[40];
	char *debounce_enable;
	char *wake_cntrlz;

	for (bank = 0; bank < gpio_dev->hwbank_num; bank++) {
		unsigned int time = 0;
		unsigned int unit = 0;

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
		seq_printf(s, "GPIO bank%d\n", bank);
		seq_puts(s, "gpio\t  int|active|trigger|S0i3| S3|S4/S5| Z|wake|pull|  orient|       debounce|reg\n");
		for (; i < pin_num; i++) {
			seq_printf(s, "#%d\t", i);
			raw_spin_lock_irqsave(&gpio_dev->lock, flags);
			pin_reg = readl(gpio_dev->base + i * 4);
			raw_spin_unlock_irqrestore(&gpio_dev->lock, flags);

			if (pin_reg & BIT(INTERRUPT_ENABLE_OFF)) {
				u8 level = (pin_reg >> ACTIVE_LEVEL_OFF) &
						ACTIVE_LEVEL_MASK;

				if (level == ACTIVE_LEVEL_HIGH)
					active_level = "↑";
				else if (level == ACTIVE_LEVEL_LOW)
					active_level = "↓";
				else if (!(pin_reg & BIT(LEVEL_TRIG_OFF)) &&
					 level == ACTIVE_LEVEL_BOTH)
					active_level = "b";
				else
					active_level = "?";

				if (pin_reg & BIT(LEVEL_TRIG_OFF))
					level_trig = "level";
				else
					level_trig = " edge";

				if (pin_reg & BIT(INTERRUPT_MASK_OFF))
					interrupt_mask = "😛";
				else
					interrupt_mask = "😷";

				if (pin_reg & BIT(INTERRUPT_STS_OFF))
					interrupt_sts = "🔥";
				else
					interrupt_sts = "  ";

				seq_printf(s, "%s %s|     %s|  %s|",
				   interrupt_sts,
				   interrupt_mask,
				   active_level,
				   level_trig);
			} else
				seq_puts(s, "    ∅|      |       |");

			if (pin_reg & BIT(WAKE_CNTRL_OFF_S0I3))
				wake_cntrl0 = "⏰";
			else
				wake_cntrl0 = "  ";
			seq_printf(s, "  %s| ", wake_cntrl0);

			if (pin_reg & BIT(WAKE_CNTRL_OFF_S3))
				wake_cntrl1 = "⏰";
			else
				wake_cntrl1 = "  ";
			seq_printf(s, "%s|", wake_cntrl1);

			if (pin_reg & BIT(WAKE_CNTRL_OFF_S4))
				wake_cntrl2 = "⏰";
			else
				wake_cntrl2 = "  ";
			seq_printf(s, "   %s|", wake_cntrl2);

			if (pin_reg & BIT(WAKECNTRL_Z_OFF))
				wake_cntrlz = "⏰";
			else
				wake_cntrlz = "  ";
			seq_printf(s, "%s|", wake_cntrlz);

			if (pin_reg & BIT(WAKE_STS_OFF))
				wake_sts = "🔥";
			else
				wake_sts = " ";
			seq_printf(s, "   %s|", wake_sts);

			if (pin_reg & BIT(PULL_UP_ENABLE_OFF)) {
				if (pin_reg & BIT(PULL_UP_SEL_OFF))
					pull_up_sel = "8k";
				else
					pull_up_sel = "4k";
				seq_printf(s, "%s ↑|",
					   pull_up_sel);
			} else if (pin_reg & BIT(PULL_DOWN_ENABLE_OFF)) {
				seq_puts(s, "   ↓|");
			} else  {
				seq_puts(s, "    |");
			}

			if (pin_reg & BIT(OUTPUT_ENABLE_OFF)) {
				pin_sts = "output";
				if (pin_reg & BIT(OUTPUT_VALUE_OFF))
					orientation = "↑";
				else
					orientation = "↓";
			} else {
				pin_sts = "input ";
				if (pin_reg & BIT(PIN_STS_OFF))
					orientation = "↑";
				else
					orientation = "↓";
			}
			seq_printf(s, "%s %s|", pin_sts, orientation);

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
					debounce_enable = "b";
				else if ((DB_TYPE_PRESERVE_LOW_GLITCH << DB_CNTRL_OFF) == db_cntrl)
					debounce_enable = "↓";
				else
					debounce_enable = "↑";
				snprintf(debounce_value, sizeof(debounce_value), "%06u", time * unit);
				seq_printf(s, "%s (🕑 %sus)|", debounce_enable, debounce_value);
			} else {
				seq_puts(s, "               |");
			}
			seq_printf(s, "0x%x\n", pin_reg);
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

	gpiochip_enable_irq(gc, d->hwirq);

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

	gpiochip_disable_irq(gc, d->hwirq);
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

static const struct irq_chip amd_gpio_irqchip = {
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
	.flags        = IRQCHIP_ENABLE_WAKEUP_ON_SUSPEND | IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

#define PIN_IRQ_PENDING	(BIT(INTERRUPT_STS_OFF) | BIT(WAKE_STS_OFF))

static bool do_amd_gpio_irq_handler(int irq, void *dev_id)
{
	struct amd_gpio *gpio_dev = dev_id;
	struct gpio_chip *gc = &gpio_dev->gc;
	unsigned int i, irqnr;
	unsigned long flags;
	u32 __iomem *regs;
	bool ret = false;
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

			if (regval & PIN_IRQ_PENDING)
				dev_dbg(&gpio_dev->pdev->dev,
					"GPIO %d is active: 0x%x",
					irqnr + i, regval);

			/* caused wake on resume context for shared IRQ */
			if (irq < 0 && (regval & BIT(WAKE_STS_OFF)))
				return true;

			if (!(regval & PIN_IRQ_PENDING) ||
			    !(regval & BIT(INTERRUPT_MASK_OFF)))
				continue;
			generic_handle_domain_irq_safe(gc->irq.domain, irqnr + i);

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
			ret = true;
		}
	}
	/* did not cause wake on resume context for shared IRQ */
	if (irq < 0)
		return false;

	/* Signal EOI to the GPIO unit */
	raw_spin_lock_irqsave(&gpio_dev->lock, flags);
	regval = readl(gpio_dev->base + WAKE_INT_MASTER_REG);
	regval |= EOI_MASK;
	writel(regval, gpio_dev->base + WAKE_INT_MASTER_REG);
	raw_spin_unlock_irqrestore(&gpio_dev->lock, flags);

	return ret;
}

static irqreturn_t amd_gpio_irq_handler(int irq, void *dev_id)
{
	return IRQ_RETVAL(do_amd_gpio_irq_handler(irq, dev_id));
}

static bool __maybe_unused amd_gpio_check_wake(void *dev_id)
{
	return do_amd_gpio_irq_handler(-1, dev_id);
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

static void amd_gpio_irq_init(struct amd_gpio *gpio_dev)
{
	struct pinctrl_desc *desc = gpio_dev->pctrl->desc;
	unsigned long flags;
	u32 pin_reg, mask;
	int i;

	mask = BIT(WAKE_CNTRL_OFF_S0I3) | BIT(WAKE_CNTRL_OFF_S3) |
		BIT(INTERRUPT_MASK_OFF) | BIT(INTERRUPT_ENABLE_OFF) |
		BIT(WAKE_CNTRL_OFF_S4);

	for (i = 0; i < desc->npins; i++) {
		int pin = desc->pins[i].number;
		const struct pin_desc *pd = pin_desc_get(gpio_dev->pctrl, pin);

		if (!pd)
			continue;

		raw_spin_lock_irqsave(&gpio_dev->lock, flags);

		pin_reg = readl(gpio_dev->base + i * 4);
		pin_reg &= ~mask;
		writel(pin_reg, gpio_dev->base + i * 4);

		raw_spin_unlock_irqrestore(&gpio_dev->lock, flags);
	}
}

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
	unsigned long flags;
	int i;

	for (i = 0; i < desc->npins; i++) {
		int pin = desc->pins[i].number;

		if (!amd_gpio_should_save(gpio_dev, pin))
			continue;

		raw_spin_lock_irqsave(&gpio_dev->lock, flags);
		gpio_dev->saved_regs[i] = readl(gpio_dev->base + pin * 4) & ~PIN_IRQ_PENDING;
		raw_spin_unlock_irqrestore(&gpio_dev->lock, flags);
	}

	return 0;
}

static int amd_gpio_resume(struct device *dev)
{
	struct amd_gpio *gpio_dev = dev_get_drvdata(dev);
	struct pinctrl_desc *desc = gpio_dev->pctrl->desc;
	unsigned long flags;
	int i;

	for (i = 0; i < desc->npins; i++) {
		int pin = desc->pins[i].number;

		if (!amd_gpio_should_save(gpio_dev, pin))
			continue;

		raw_spin_lock_irqsave(&gpio_dev->lock, flags);
		gpio_dev->saved_regs[i] |= readl(gpio_dev->base + pin * 4) & PIN_IRQ_PENDING;
		writel(gpio_dev->saved_regs[i], gpio_dev->base + pin * 4);
		raw_spin_unlock_irqrestore(&gpio_dev->lock, flags);
	}

	return 0;
}

static const struct dev_pm_ops amd_gpio_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(amd_gpio_suspend,
				     amd_gpio_resume)
};
#endif

static int amd_get_functions_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(pmx_functions);
}

static const char *amd_get_fname(struct pinctrl_dev *pctrldev, unsigned int selector)
{
	return pmx_functions[selector].name;
}

static int amd_get_groups(struct pinctrl_dev *pctrldev, unsigned int selector,
			  const char * const **groups,
			  unsigned int * const num_groups)
{
	struct amd_gpio *gpio_dev = pinctrl_dev_get_drvdata(pctrldev);

	if (!gpio_dev->iomux_base) {
		dev_err(&gpio_dev->pdev->dev, "iomux function %d group not supported\n", selector);
		return -EINVAL;
	}

	*groups = pmx_functions[selector].groups;
	*num_groups = pmx_functions[selector].ngroups;
	return 0;
}

static int amd_set_mux(struct pinctrl_dev *pctrldev, unsigned int function, unsigned int group)
{
	struct amd_gpio *gpio_dev = pinctrl_dev_get_drvdata(pctrldev);
	struct device *dev = &gpio_dev->pdev->dev;
	struct pin_desc *pd;
	int ind, index;

	if (!gpio_dev->iomux_base)
		return -EINVAL;

	for (index = 0; index < NSELECTS; index++) {
		if (strcmp(gpio_dev->groups[group].name,  pmx_functions[function].groups[index]))
			continue;

		if (readb(gpio_dev->iomux_base + pmx_functions[function].index) ==
				FUNCTION_INVALID) {
			dev_err(dev, "IOMUX_GPIO 0x%x not present or supported\n",
				pmx_functions[function].index);
			return -EINVAL;
		}

		writeb(index, gpio_dev->iomux_base + pmx_functions[function].index);

		if (index != (readb(gpio_dev->iomux_base + pmx_functions[function].index) &
					FUNCTION_MASK)) {
			dev_err(dev, "IOMUX_GPIO 0x%x not present or supported\n",
				pmx_functions[function].index);
			return -EINVAL;
		}

		for (ind = 0; ind < gpio_dev->groups[group].npins; ind++) {
			if (strncmp(gpio_dev->groups[group].name, "IMX_F", strlen("IMX_F")))
				continue;

			pd = pin_desc_get(gpio_dev->pctrl, gpio_dev->groups[group].pins[ind]);
			pd->mux_owner = gpio_dev->groups[group].name;
		}
		break;
	}

	return 0;
}

static const struct pinmux_ops amd_pmxops = {
	.get_functions_count = amd_get_functions_count,
	.get_function_name = amd_get_fname,
	.get_function_groups = amd_get_groups,
	.set_mux = amd_set_mux,
};

static struct pinctrl_desc amd_pinctrl_desc = {
	.pins	= kerncz_pins,
	.npins = ARRAY_SIZE(kerncz_pins),
	.pctlops = &amd_pinctrl_ops,
	.pmxops = &amd_pmxops,
	.confops = &amd_pinconf_ops,
	.owner = THIS_MODULE,
};

static void amd_get_iomux_res(struct amd_gpio *gpio_dev)
{
	struct pinctrl_desc *desc = &amd_pinctrl_desc;
	struct device *dev = &gpio_dev->pdev->dev;
	int index;

	index = device_property_match_string(dev, "pinctrl-resource-names",  "iomux");
	if (index < 0) {
		dev_dbg(dev, "iomux not supported\n");
		goto out_no_pinmux;
	}

	gpio_dev->iomux_base = devm_platform_ioremap_resource(gpio_dev->pdev, index);
	if (IS_ERR(gpio_dev->iomux_base)) {
		dev_dbg(dev, "iomux not supported %d io resource\n", index);
		goto out_no_pinmux;
	}

	return;

out_no_pinmux:
	desc->pmxops = NULL;
}

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

	gpio_dev->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(gpio_dev->base)) {
		dev_err(&pdev->dev, "Failed to get gpio io resource.\n");
		return PTR_ERR(gpio_dev->base);
	}

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

	gpio_dev->hwbank_num = gpio_dev->gc.ngpio / 64;
	gpio_dev->groups = kerncz_groups;
	gpio_dev->ngroups = ARRAY_SIZE(kerncz_groups);

	amd_pinctrl_desc.name = dev_name(&pdev->dev);
	amd_get_iomux_res(gpio_dev);
	gpio_dev->pctrl = devm_pinctrl_register(&pdev->dev, &amd_pinctrl_desc,
						gpio_dev);
	if (IS_ERR(gpio_dev->pctrl)) {
		dev_err(&pdev->dev, "Couldn't register pinctrl driver\n");
		return PTR_ERR(gpio_dev->pctrl);
	}

	/* Disable and mask interrupts */
	amd_gpio_irq_init(gpio_dev);

	girq = &gpio_dev->gc.irq;
	gpio_irq_chip_set_chip(girq, &amd_gpio_irqchip);
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
	acpi_register_wakeup_handler(gpio_dev->irq, amd_gpio_check_wake, gpio_dev);

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
	acpi_unregister_wakeup_handler(amd_gpio_check_wake, gpio_dev);

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

MODULE_AUTHOR("Ken Xue <Ken.Xue@amd.com>, Jeff Wu <Jeff.Wu@amd.com>");
MODULE_DESCRIPTION("AMD GPIO pinctrl driver");
