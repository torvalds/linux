/*
 * GPIO driver for AMD
 *
 * Copyright (c) 2014,2015 AMD Corporation.
 * Authors: Ken Xue <Ken.Xue@amd.com>
 *      Wu, Jeff <Jeff.Wu@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
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
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/acpi.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/bitops.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>

#include "pinctrl-utils.h"
#include "pinctrl-amd.h"

static inline struct amd_gpio *to_amd_gpio(struct gpio_chip *gc)
{
	return container_of(gc, struct amd_gpio, gc);
}

static int amd_gpio_direction_input(struct gpio_chip *gc, unsigned offset)
{
	unsigned long flags;
	u32 pin_reg;
	struct amd_gpio *gpio_dev = to_amd_gpio(gc);

	spin_lock_irqsave(&gpio_dev->lock, flags);
	pin_reg = readl(gpio_dev->base + offset * 4);
	/*
	 * Suppose BIOS or Bootloader sets specific debounce for the
	 * GPIO. if not, set debounce to be  2.75ms and remove glitch.
	*/
	if ((pin_reg & DB_TMR_OUT_MASK) == 0) {
		pin_reg |= 0xf;
		pin_reg |= BIT(DB_TMR_OUT_UNIT_OFF);
		pin_reg |= DB_TYPE_REMOVE_GLITCH << DB_CNTRL_OFF;
		pin_reg &= ~BIT(DB_TMR_LARGE_OFF);
	}

	pin_reg &= ~BIT(OUTPUT_ENABLE_OFF);
	writel(pin_reg, gpio_dev->base + offset * 4);
	spin_unlock_irqrestore(&gpio_dev->lock, flags);

	return 0;
}

static int amd_gpio_direction_output(struct gpio_chip *gc, unsigned offset,
		int value)
{
	u32 pin_reg;
	unsigned long flags;
	struct amd_gpio *gpio_dev = to_amd_gpio(gc);

	spin_lock_irqsave(&gpio_dev->lock, flags);
	pin_reg = readl(gpio_dev->base + offset * 4);
	pin_reg |= BIT(OUTPUT_ENABLE_OFF);
	if (value)
		pin_reg |= BIT(OUTPUT_VALUE_OFF);
	else
		pin_reg &= ~BIT(OUTPUT_VALUE_OFF);
	writel(pin_reg, gpio_dev->base + offset * 4);
	spin_unlock_irqrestore(&gpio_dev->lock, flags);

	return 0;
}

static int amd_gpio_get_value(struct gpio_chip *gc, unsigned offset)
{
	u32 pin_reg;
	unsigned long flags;
	struct amd_gpio *gpio_dev = to_amd_gpio(gc);

	spin_lock_irqsave(&gpio_dev->lock, flags);
	pin_reg = readl(gpio_dev->base + offset * 4);
	spin_unlock_irqrestore(&gpio_dev->lock, flags);

	return !!(pin_reg & BIT(PIN_STS_OFF));
}

static void amd_gpio_set_value(struct gpio_chip *gc, unsigned offset, int value)
{
	u32 pin_reg;
	unsigned long flags;
	struct amd_gpio *gpio_dev = to_amd_gpio(gc);

	spin_lock_irqsave(&gpio_dev->lock, flags);
	pin_reg = readl(gpio_dev->base + offset * 4);
	if (value)
		pin_reg |= BIT(OUTPUT_VALUE_OFF);
	else
		pin_reg &= ~BIT(OUTPUT_VALUE_OFF);
	writel(pin_reg, gpio_dev->base + offset * 4);
	spin_unlock_irqrestore(&gpio_dev->lock, flags);
}

static int amd_gpio_set_debounce(struct gpio_chip *gc, unsigned offset,
		unsigned debounce)
{
	u32 pin_reg;
	u32 time;
	unsigned long flags;
	struct amd_gpio *gpio_dev = to_amd_gpio(gc);

	spin_lock_irqsave(&gpio_dev->lock, flags);
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
			time = debounce / 15600;
			pin_reg |= time & DB_TMR_OUT_MASK;
			pin_reg &= ~BIT(DB_TMR_OUT_UNIT_OFF);
			pin_reg |= BIT(DB_TMR_LARGE_OFF);
		} else if (debounce < 1000000) {
			time = debounce / 62500;
			pin_reg |= time & DB_TMR_OUT_MASK;
			pin_reg |= BIT(DB_TMR_OUT_UNIT_OFF);
			pin_reg |= BIT(DB_TMR_LARGE_OFF);
		} else {
			pin_reg &= ~DB_CNTRl_MASK;
			return -EINVAL;
		}
	} else {
		pin_reg &= ~BIT(DB_TMR_OUT_UNIT_OFF);
		pin_reg &= ~BIT(DB_TMR_LARGE_OFF);
		pin_reg &= ~DB_TMR_OUT_MASK;
		pin_reg &= ~DB_CNTRl_MASK;
	}
	writel(pin_reg, gpio_dev->base + offset * 4);
	spin_unlock_irqrestore(&gpio_dev->lock, flags);

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static void amd_gpio_dbg_show(struct seq_file *s, struct gpio_chip *gc)
{
	u32 pin_reg;
	unsigned long flags;
	unsigned int bank, i, pin_num;
	struct amd_gpio *gpio_dev = to_amd_gpio(gc);

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

	for (bank = 0; bank < AMD_GPIO_TOTAL_BANKS; bank++) {
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
		}

		for (; i < pin_num; i++) {
			seq_printf(s, "pin%d\t", i);
			spin_lock_irqsave(&gpio_dev->lock, flags);
			pin_reg = readl(gpio_dev->base + i * 4);
			spin_unlock_irqrestore(&gpio_dev->lock, flags);

			if (pin_reg & BIT(INTERRUPT_ENABLE_OFF)) {
				interrupt_enable = "interrupt is enabled|";

				if (!(pin_reg & BIT(ACTIVE_LEVEL_OFF))
				&& !(pin_reg & BIT(ACTIVE_LEVEL_OFF+1)))
					active_level = "Active low|";
				else if (pin_reg & BIT(ACTIVE_LEVEL_OFF)
				&& !(pin_reg & BIT(ACTIVE_LEVEL_OFF+1)))
					active_level = "Active high|";
				else if (!(pin_reg & BIT(ACTIVE_LEVEL_OFF))
					&& pin_reg & BIT(ACTIVE_LEVEL_OFF+1))
					active_level = "Active on both|";
				else
					active_level = "Unknow Active level|";

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

			if (pin_reg & BIT(WAKE_CNTRL_OFF))
				wake_cntrl0 = "enable wakeup in S0i3 state|";
			else
				wake_cntrl0 = "disable wakeup in S0i3 state|";

			if (pin_reg & BIT(WAKE_CNTRL_OFF))
				wake_cntrl1 = "enable wakeup in S3 state|";
			else
				wake_cntrl1 = "disable wakeup in S3 state|";

			if (pin_reg & BIT(WAKE_CNTRL_OFF))
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

			seq_printf(s, "%s %s %s %s %s %s\n"
				" %s %s %s %s %s %s %s 0x%x\n",
				level_trig, active_level, interrupt_enable,
				interrupt_mask, wake_cntrl0, wake_cntrl1,
				wake_cntrl2, pin_sts, pull_up_sel,
				pull_up_enable, pull_down_enable,
				output_value, output_enable, pin_reg);
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
	struct amd_gpio *gpio_dev = to_amd_gpio(gc);

	spin_lock_irqsave(&gpio_dev->lock, flags);
	pin_reg = readl(gpio_dev->base + (d->hwirq)*4);
	/*
		Suppose BIOS or Bootloader sets specific debounce for the
		GPIO. if not, set debounce to be  2.75ms.
	*/
	if ((pin_reg & DB_TMR_OUT_MASK) == 0) {
		pin_reg |= 0xf;
		pin_reg |= BIT(DB_TMR_OUT_UNIT_OFF);
		pin_reg &= ~BIT(DB_TMR_LARGE_OFF);
	}
	pin_reg |= BIT(INTERRUPT_ENABLE_OFF);
	pin_reg |= BIT(INTERRUPT_MASK_OFF);
	writel(pin_reg, gpio_dev->base + (d->hwirq)*4);
	spin_unlock_irqrestore(&gpio_dev->lock, flags);
}

static void amd_gpio_irq_disable(struct irq_data *d)
{
	u32 pin_reg;
	unsigned long flags;
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct amd_gpio *gpio_dev = to_amd_gpio(gc);

	spin_lock_irqsave(&gpio_dev->lock, flags);
	pin_reg = readl(gpio_dev->base + (d->hwirq)*4);
	pin_reg &= ~BIT(INTERRUPT_ENABLE_OFF);
	pin_reg &= ~BIT(INTERRUPT_MASK_OFF);
	writel(pin_reg, gpio_dev->base + (d->hwirq)*4);
	spin_unlock_irqrestore(&gpio_dev->lock, flags);
}

static void amd_gpio_irq_mask(struct irq_data *d)
{
	u32 pin_reg;
	unsigned long flags;
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct amd_gpio *gpio_dev = to_amd_gpio(gc);

	spin_lock_irqsave(&gpio_dev->lock, flags);
	pin_reg = readl(gpio_dev->base + (d->hwirq)*4);
	pin_reg &= ~BIT(INTERRUPT_MASK_OFF);
	writel(pin_reg, gpio_dev->base + (d->hwirq)*4);
	spin_unlock_irqrestore(&gpio_dev->lock, flags);
}

static void amd_gpio_irq_unmask(struct irq_data *d)
{
	u32 pin_reg;
	unsigned long flags;
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct amd_gpio *gpio_dev = to_amd_gpio(gc);

	spin_lock_irqsave(&gpio_dev->lock, flags);
	pin_reg = readl(gpio_dev->base + (d->hwirq)*4);
	pin_reg |= BIT(INTERRUPT_MASK_OFF);
	writel(pin_reg, gpio_dev->base + (d->hwirq)*4);
	spin_unlock_irqrestore(&gpio_dev->lock, flags);
}

static void amd_gpio_irq_eoi(struct irq_data *d)
{
	u32 reg;
	unsigned long flags;
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct amd_gpio *gpio_dev = to_amd_gpio(gc);

	spin_lock_irqsave(&gpio_dev->lock, flags);
	reg = readl(gpio_dev->base + WAKE_INT_MASTER_REG);
	reg |= EOI_MASK;
	writel(reg, gpio_dev->base + WAKE_INT_MASTER_REG);
	spin_unlock_irqrestore(&gpio_dev->lock, flags);
}

static int amd_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	int ret = 0;
	u32 pin_reg;
	unsigned long flags;
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct amd_gpio *gpio_dev = to_amd_gpio(gc);

	spin_lock_irqsave(&gpio_dev->lock, flags);
	pin_reg = readl(gpio_dev->base + (d->hwirq)*4);

	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_RISING:
		pin_reg &= ~BIT(LEVEL_TRIG_OFF);
		pin_reg &= ~(ACTIVE_LEVEL_MASK << ACTIVE_LEVEL_OFF);
		pin_reg |= ACTIVE_HIGH << ACTIVE_LEVEL_OFF;
		pin_reg |= DB_TYPE_REMOVE_GLITCH << DB_CNTRL_OFF;
		__irq_set_handler_locked(d->irq, handle_edge_irq);
		break;

	case IRQ_TYPE_EDGE_FALLING:
		pin_reg &= ~BIT(LEVEL_TRIG_OFF);
		pin_reg &= ~(ACTIVE_LEVEL_MASK << ACTIVE_LEVEL_OFF);
		pin_reg |= ACTIVE_LOW << ACTIVE_LEVEL_OFF;
		pin_reg |= DB_TYPE_REMOVE_GLITCH << DB_CNTRL_OFF;
		__irq_set_handler_locked(d->irq, handle_edge_irq);
		break;

	case IRQ_TYPE_EDGE_BOTH:
		pin_reg &= ~BIT(LEVEL_TRIG_OFF);
		pin_reg &= ~(ACTIVE_LEVEL_MASK << ACTIVE_LEVEL_OFF);
		pin_reg |= BOTH_EADGE << ACTIVE_LEVEL_OFF;
		pin_reg |= DB_TYPE_REMOVE_GLITCH << DB_CNTRL_OFF;
		__irq_set_handler_locked(d->irq, handle_edge_irq);
		break;

	case IRQ_TYPE_LEVEL_HIGH:
		pin_reg |= LEVEL_TRIGGER << LEVEL_TRIG_OFF;
		pin_reg &= ~(ACTIVE_LEVEL_MASK << ACTIVE_LEVEL_OFF);
		pin_reg |= ACTIVE_HIGH << ACTIVE_LEVEL_OFF;
		pin_reg &= ~(DB_CNTRl_MASK << DB_CNTRL_OFF);
		pin_reg |= DB_TYPE_PRESERVE_LOW_GLITCH << DB_CNTRL_OFF;
		__irq_set_handler_locked(d->irq, handle_level_irq);
		break;

	case IRQ_TYPE_LEVEL_LOW:
		pin_reg |= LEVEL_TRIGGER << LEVEL_TRIG_OFF;
		pin_reg &= ~(ACTIVE_LEVEL_MASK << ACTIVE_LEVEL_OFF);
		pin_reg |= ACTIVE_LOW << ACTIVE_LEVEL_OFF;
		pin_reg &= ~(DB_CNTRl_MASK << DB_CNTRL_OFF);
		pin_reg |= DB_TYPE_PRESERVE_HIGH_GLITCH << DB_CNTRL_OFF;
		__irq_set_handler_locked(d->irq, handle_level_irq);
		break;

	case IRQ_TYPE_NONE:
		break;

	default:
		dev_err(&gpio_dev->pdev->dev, "Invalid type value\n");
		ret = -EINVAL;
		goto exit;
	}

	pin_reg |= CLR_INTR_STAT << INTERRUPT_STS_OFF;
	writel(pin_reg, gpio_dev->base + (d->hwirq)*4);
	spin_unlock_irqrestore(&gpio_dev->lock, flags);

exit:
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
	.irq_eoi      = amd_gpio_irq_eoi,
	.irq_set_type = amd_gpio_irq_set_type,
};

static void amd_gpio_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	u32 i;
	u32 off;
	u32 reg;
	u32 pin_reg;
	u64 reg64;
	int handled = 0;
	unsigned long flags;
	struct irq_chip *chip = irq_get_chip(irq);
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct amd_gpio *gpio_dev = to_amd_gpio(gc);

	chained_irq_enter(chip, desc);
	/*enable GPIO interrupt again*/
	spin_lock_irqsave(&gpio_dev->lock, flags);
	reg = readl(gpio_dev->base + WAKE_INT_STATUS_REG1);
	reg64 = reg;
	reg64 = reg64 << 32;

	reg = readl(gpio_dev->base + WAKE_INT_STATUS_REG0);
	reg64 |= reg;
	spin_unlock_irqrestore(&gpio_dev->lock, flags);

	/*
	 * first 46 bits indicates interrupt status.
	 * one bit represents four interrupt sources.
	*/
	for (off = 0; off < 46 ; off++) {
		if (reg64 & BIT(off)) {
			for (i = 0; i < 4; i++) {
				pin_reg = readl(gpio_dev->base +
						(off * 4 + i) * 4);
				if ((pin_reg & BIT(INTERRUPT_STS_OFF)) ||
					(pin_reg & BIT(WAKE_STS_OFF))) {
					irq = irq_find_mapping(gc->irqdomain,
								off * 4 + i);
					generic_handle_irq(irq);
					writel(pin_reg,
						gpio_dev->base
						+ (off * 4 + i) * 4);
					handled++;
				}
			}
		}
	}

	if (handled == 0)
		handle_bad_irq(irq, desc);

	spin_lock_irqsave(&gpio_dev->lock, flags);
	reg = readl(gpio_dev->base + WAKE_INT_MASTER_REG);
	reg |= EOI_MASK;
	writel(reg, gpio_dev->base + WAKE_INT_MASTER_REG);
	spin_unlock_irqrestore(&gpio_dev->lock, flags);

	chained_irq_exit(chip, desc);
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
	.dt_free_map		= pinctrl_utils_dt_free_map,
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

	spin_lock_irqsave(&gpio_dev->lock, flags);
	pin_reg = readl(gpio_dev->base + pin*4);
	spin_unlock_irqrestore(&gpio_dev->lock, flags);
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
	u32 pin_reg;
	u32 arg;
	unsigned long flags;
	enum pin_config_param param;
	struct amd_gpio *gpio_dev = pinctrl_dev_get_drvdata(pctldev);

	spin_lock_irqsave(&gpio_dev->lock, flags);
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
			return -ENOTSUPP;
		}

		writel(pin_reg, gpio_dev->base + pin*4);
	}
	spin_unlock_irqrestore(&gpio_dev->lock, flags);

	return 0;
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
	u32 irq_base;
	struct resource *res;
	struct amd_gpio *gpio_dev;

	gpio_dev = devm_kzalloc(&pdev->dev,
				sizeof(struct amd_gpio), GFP_KERNEL);
	if (!gpio_dev)
		return -ENOMEM;

	spin_lock_init(&gpio_dev->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get gpio io resource.\n");
		return -EINVAL;
	}

	gpio_dev->base = devm_ioremap_nocache(&pdev->dev, res->start,
						resource_size(res));
	if (IS_ERR(gpio_dev->base))
		return PTR_ERR(gpio_dev->base);

	irq_base = platform_get_irq(pdev, 0);
	if (irq_base < 0) {
		dev_err(&pdev->dev, "Failed to get gpio IRQ.\n");
		return -EINVAL;
	}

	gpio_dev->pdev = pdev;
	gpio_dev->gc.direction_input	= amd_gpio_direction_input;
	gpio_dev->gc.direction_output	= amd_gpio_direction_output;
	gpio_dev->gc.get			= amd_gpio_get_value;
	gpio_dev->gc.set			= amd_gpio_set_value;
	gpio_dev->gc.set_debounce	= amd_gpio_set_debounce;
	gpio_dev->gc.dbg_show		= amd_gpio_dbg_show;

	gpio_dev->gc.base			= 0;
	gpio_dev->gc.label			= pdev->name;
	gpio_dev->gc.owner			= THIS_MODULE;
	gpio_dev->gc.dev			= &pdev->dev;
	gpio_dev->gc.ngpio			= TOTAL_NUMBER_OF_PINS;
#if defined(CONFIG_OF_GPIO)
	gpio_dev->gc.of_node			= pdev->dev.of_node;
#endif

	gpio_dev->groups = kerncz_groups;
	gpio_dev->ngroups = ARRAY_SIZE(kerncz_groups);

	amd_pinctrl_desc.name = dev_name(&pdev->dev);
	gpio_dev->pctrl = pinctrl_register(&amd_pinctrl_desc,
					&pdev->dev, gpio_dev);
	if (!gpio_dev->pctrl) {
		dev_err(&pdev->dev, "Couldn't register pinctrl driver\n");
		return -ENODEV;
	}

	ret = gpiochip_add(&gpio_dev->gc);
	if (ret)
		goto out1;

	ret = gpiochip_add_pin_range(&gpio_dev->gc, dev_name(&pdev->dev),
				0, 0, TOTAL_NUMBER_OF_PINS);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add pin range\n");
		goto out2;
	}

	ret = gpiochip_irqchip_add(&gpio_dev->gc,
				&amd_gpio_irqchip,
				0,
				handle_simple_irq,
				IRQ_TYPE_NONE);
	if (ret) {
		dev_err(&pdev->dev, "could not add irqchip\n");
		ret = -ENODEV;
		goto out2;
	}

	gpiochip_set_chained_irqchip(&gpio_dev->gc,
				 &amd_gpio_irqchip,
				 irq_base,
				 amd_gpio_irq_handler);

	platform_set_drvdata(pdev, gpio_dev);

	dev_dbg(&pdev->dev, "amd gpio driver loaded\n");
	return ret;

out2:
	gpiochip_remove(&gpio_dev->gc);

out1:
	pinctrl_unregister(gpio_dev->pctrl);
	return ret;
}

static int amd_gpio_remove(struct platform_device *pdev)
{
	struct amd_gpio *gpio_dev;

	gpio_dev = platform_get_drvdata(pdev);

	gpiochip_remove(&gpio_dev->gc);
	pinctrl_unregister(gpio_dev->pctrl);

	return 0;
}

static const struct acpi_device_id amd_gpio_acpi_match[] = {
	{ "AMD0030", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, amd_gpio_acpi_match);

static struct platform_driver amd_gpio_driver = {
	.driver		= {
		.name	= "amd_gpio",
		.owner	= THIS_MODULE,
		.acpi_match_table = ACPI_PTR(amd_gpio_acpi_match),
	},
	.probe		= amd_gpio_probe,
	.remove		= amd_gpio_remove,
};

module_platform_driver(amd_gpio_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Ken Xue <Ken.Xue@amd.com>, Jeff Wu <Jeff.Wu@amd.com>");
MODULE_DESCRIPTION("AMD GPIO pinctrl driver");
