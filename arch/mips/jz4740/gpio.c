/*
 *  Copyright (C) 2009-2010, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 platform GPIO support
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>

#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>

#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include <asm/mach-jz4740/base.h>

#include "irq.h"

#define JZ4740_GPIO_BASE_A (32*0)
#define JZ4740_GPIO_BASE_B (32*1)
#define JZ4740_GPIO_BASE_C (32*2)
#define JZ4740_GPIO_BASE_D (32*3)

#define JZ4740_GPIO_NUM_A 32
#define JZ4740_GPIO_NUM_B 32
#define JZ4740_GPIO_NUM_C 31
#define JZ4740_GPIO_NUM_D 32

#define JZ4740_IRQ_GPIO_BASE_A (JZ4740_IRQ_GPIO(0) + JZ4740_GPIO_BASE_A)
#define JZ4740_IRQ_GPIO_BASE_B (JZ4740_IRQ_GPIO(0) + JZ4740_GPIO_BASE_B)
#define JZ4740_IRQ_GPIO_BASE_C (JZ4740_IRQ_GPIO(0) + JZ4740_GPIO_BASE_C)
#define JZ4740_IRQ_GPIO_BASE_D (JZ4740_IRQ_GPIO(0) + JZ4740_GPIO_BASE_D)

#define JZ_REG_GPIO_PIN			0x00
#define JZ_REG_GPIO_DATA		0x10
#define JZ_REG_GPIO_DATA_SET		0x14
#define JZ_REG_GPIO_DATA_CLEAR		0x18
#define JZ_REG_GPIO_MASK		0x20
#define JZ_REG_GPIO_MASK_SET		0x24
#define JZ_REG_GPIO_MASK_CLEAR		0x28
#define JZ_REG_GPIO_PULL		0x30
#define JZ_REG_GPIO_PULL_SET		0x34
#define JZ_REG_GPIO_PULL_CLEAR		0x38
#define JZ_REG_GPIO_FUNC		0x40
#define JZ_REG_GPIO_FUNC_SET		0x44
#define JZ_REG_GPIO_FUNC_CLEAR		0x48
#define JZ_REG_GPIO_SELECT		0x50
#define JZ_REG_GPIO_SELECT_SET		0x54
#define JZ_REG_GPIO_SELECT_CLEAR	0x58
#define JZ_REG_GPIO_DIRECTION		0x60
#define JZ_REG_GPIO_DIRECTION_SET	0x64
#define JZ_REG_GPIO_DIRECTION_CLEAR	0x68
#define JZ_REG_GPIO_TRIGGER		0x70
#define JZ_REG_GPIO_TRIGGER_SET		0x74
#define JZ_REG_GPIO_TRIGGER_CLEAR	0x78
#define JZ_REG_GPIO_FLAG		0x80
#define JZ_REG_GPIO_FLAG_CLEAR		0x14

#define GPIO_TO_BIT(gpio) BIT(gpio & 0x1f)
#define GPIO_TO_REG(gpio, reg) (gpio_to_jz_gpio_chip(gpio)->base + (reg))
#define CHIP_TO_REG(chip, reg) (gpio_chip_to_jz_gpio_chip(chip)->base + (reg))

struct jz_gpio_chip {
	unsigned int irq;
	unsigned int irq_base;
	uint32_t edge_trigger_both;

	void __iomem *base;

	struct gpio_chip gpio_chip;
};

static struct jz_gpio_chip jz4740_gpio_chips[];

static inline struct jz_gpio_chip *gpio_to_jz_gpio_chip(unsigned int gpio)
{
	return &jz4740_gpio_chips[gpio >> 5];
}

static inline struct jz_gpio_chip *gpio_chip_to_jz_gpio_chip(struct gpio_chip *gpio_chip)
{
	return container_of(gpio_chip, struct jz_gpio_chip, gpio_chip);
}

static inline struct jz_gpio_chip *irq_to_jz_gpio_chip(struct irq_data *data)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(data);
	return gc->private;
}

static inline void jz_gpio_write_bit(unsigned int gpio, unsigned int reg)
{
	writel(GPIO_TO_BIT(gpio), GPIO_TO_REG(gpio, reg));
}

int jz_gpio_set_function(int gpio, enum jz_gpio_function function)
{
	if (function == JZ_GPIO_FUNC_NONE) {
		jz_gpio_write_bit(gpio, JZ_REG_GPIO_FUNC_CLEAR);
		jz_gpio_write_bit(gpio, JZ_REG_GPIO_SELECT_CLEAR);
		jz_gpio_write_bit(gpio, JZ_REG_GPIO_TRIGGER_CLEAR);
	} else {
		jz_gpio_write_bit(gpio, JZ_REG_GPIO_FUNC_SET);
		jz_gpio_write_bit(gpio, JZ_REG_GPIO_TRIGGER_CLEAR);
		switch (function) {
		case JZ_GPIO_FUNC1:
			jz_gpio_write_bit(gpio, JZ_REG_GPIO_SELECT_CLEAR);
			break;
		case JZ_GPIO_FUNC3:
			jz_gpio_write_bit(gpio, JZ_REG_GPIO_TRIGGER_SET);
		case JZ_GPIO_FUNC2: /* Falltrough */
			jz_gpio_write_bit(gpio, JZ_REG_GPIO_SELECT_SET);
			break;
		default:
			BUG();
			break;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(jz_gpio_set_function);

int jz_gpio_bulk_request(const struct jz_gpio_bulk_request *request, size_t num)
{
	size_t i;
	int ret;

	for (i = 0; i < num; ++i, ++request) {
		ret = gpio_request(request->gpio, request->name);
		if (ret)
			goto err;
		jz_gpio_set_function(request->gpio, request->function);
	}

	return 0;

err:
	for (--request; i > 0; --i, --request) {
		gpio_free(request->gpio);
		jz_gpio_set_function(request->gpio, JZ_GPIO_FUNC_NONE);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(jz_gpio_bulk_request);

void jz_gpio_bulk_free(const struct jz_gpio_bulk_request *request, size_t num)
{
	size_t i;

	for (i = 0; i < num; ++i, ++request) {
		gpio_free(request->gpio);
		jz_gpio_set_function(request->gpio, JZ_GPIO_FUNC_NONE);
	}

}
EXPORT_SYMBOL_GPL(jz_gpio_bulk_free);

void jz_gpio_bulk_suspend(const struct jz_gpio_bulk_request *request, size_t num)
{
	size_t i;

	for (i = 0; i < num; ++i, ++request) {
		jz_gpio_set_function(request->gpio, JZ_GPIO_FUNC_NONE);
		jz_gpio_write_bit(request->gpio, JZ_REG_GPIO_DIRECTION_CLEAR);
		jz_gpio_write_bit(request->gpio, JZ_REG_GPIO_PULL_SET);
	}
}
EXPORT_SYMBOL_GPL(jz_gpio_bulk_suspend);

void jz_gpio_bulk_resume(const struct jz_gpio_bulk_request *request, size_t num)
{
	size_t i;

	for (i = 0; i < num; ++i, ++request)
		jz_gpio_set_function(request->gpio, request->function);
}
EXPORT_SYMBOL_GPL(jz_gpio_bulk_resume);

void jz_gpio_enable_pullup(unsigned gpio)
{
	jz_gpio_write_bit(gpio, JZ_REG_GPIO_PULL_CLEAR);
}
EXPORT_SYMBOL_GPL(jz_gpio_enable_pullup);

void jz_gpio_disable_pullup(unsigned gpio)
{
	jz_gpio_write_bit(gpio, JZ_REG_GPIO_PULL_SET);
}
EXPORT_SYMBOL_GPL(jz_gpio_disable_pullup);

static int jz_gpio_get_value(struct gpio_chip *chip, unsigned gpio)
{
	return !!(readl(CHIP_TO_REG(chip, JZ_REG_GPIO_PIN)) & BIT(gpio));
}

static void jz_gpio_set_value(struct gpio_chip *chip, unsigned gpio, int value)
{
	uint32_t __iomem *reg = CHIP_TO_REG(chip, JZ_REG_GPIO_DATA_SET);
	reg += !value;
	writel(BIT(gpio), reg);
}

static int jz_gpio_direction_output(struct gpio_chip *chip, unsigned gpio,
	int value)
{
	writel(BIT(gpio), CHIP_TO_REG(chip, JZ_REG_GPIO_DIRECTION_SET));
	jz_gpio_set_value(chip, gpio, value);

	return 0;
}

static int jz_gpio_direction_input(struct gpio_chip *chip, unsigned gpio)
{
	writel(BIT(gpio), CHIP_TO_REG(chip, JZ_REG_GPIO_DIRECTION_CLEAR));

	return 0;
}

int jz_gpio_port_direction_input(int port, uint32_t mask)
{
	writel(mask, GPIO_TO_REG(port, JZ_REG_GPIO_DIRECTION_CLEAR));

	return 0;
}
EXPORT_SYMBOL(jz_gpio_port_direction_input);

int jz_gpio_port_direction_output(int port, uint32_t mask)
{
	writel(mask, GPIO_TO_REG(port, JZ_REG_GPIO_DIRECTION_SET));

	return 0;
}
EXPORT_SYMBOL(jz_gpio_port_direction_output);

void jz_gpio_port_set_value(int port, uint32_t value, uint32_t mask)
{
	writel(~value & mask, GPIO_TO_REG(port, JZ_REG_GPIO_DATA_CLEAR));
	writel(value & mask, GPIO_TO_REG(port, JZ_REG_GPIO_DATA_SET));
}
EXPORT_SYMBOL(jz_gpio_port_set_value);

uint32_t jz_gpio_port_get_value(int port, uint32_t mask)
{
	uint32_t value = readl(GPIO_TO_REG(port, JZ_REG_GPIO_PIN));

	return value & mask;
}
EXPORT_SYMBOL(jz_gpio_port_get_value);

int gpio_to_irq(unsigned gpio)
{
	return JZ4740_IRQ_GPIO(0) + gpio;
}
EXPORT_SYMBOL_GPL(gpio_to_irq);

int irq_to_gpio(unsigned irq)
{
	return irq - JZ4740_IRQ_GPIO(0);
}
EXPORT_SYMBOL_GPL(irq_to_gpio);

#define IRQ_TO_BIT(irq) BIT(irq_to_gpio(irq) & 0x1f)

static void jz_gpio_check_trigger_both(struct jz_gpio_chip *chip, unsigned int irq)
{
	uint32_t value;
	void __iomem *reg;
	uint32_t mask = IRQ_TO_BIT(irq);

	if (!(chip->edge_trigger_both & mask))
		return;

	reg = chip->base;

	value = readl(chip->base + JZ_REG_GPIO_PIN);
	if (value & mask)
		reg += JZ_REG_GPIO_DIRECTION_CLEAR;
	else
		reg += JZ_REG_GPIO_DIRECTION_SET;

	writel(mask, reg);
}

static void jz_gpio_irq_demux_handler(unsigned int irq, struct irq_desc *desc)
{
	uint32_t flag;
	unsigned int gpio_irq;
	struct jz_gpio_chip *chip = irq_desc_get_handler_data(desc);

	flag = readl(chip->base + JZ_REG_GPIO_FLAG);
	if (!flag)
		return;

	gpio_irq = chip->irq_base + __fls(flag);

	jz_gpio_check_trigger_both(chip, gpio_irq);

	generic_handle_irq(gpio_irq);
};

static inline void jz_gpio_set_irq_bit(struct irq_data *data, unsigned int reg)
{
	struct jz_gpio_chip *chip = irq_to_jz_gpio_chip(data);
	writel(IRQ_TO_BIT(data->irq), chip->base + reg);
}

static void jz_gpio_irq_unmask(struct irq_data *data)
{
	struct jz_gpio_chip *chip = irq_to_jz_gpio_chip(data);

	jz_gpio_check_trigger_both(chip, data->irq);
	irq_gc_unmask_enable_reg(data);
};

/* TODO: Check if function is gpio */
static unsigned int jz_gpio_irq_startup(struct irq_data *data)
{
	jz_gpio_set_irq_bit(data, JZ_REG_GPIO_SELECT_SET);
	jz_gpio_irq_unmask(data);
	return 0;
}

static void jz_gpio_irq_shutdown(struct irq_data *data)
{
	irq_gc_mask_disable_reg(data);

	/* Set direction to input */
	jz_gpio_set_irq_bit(data, JZ_REG_GPIO_DIRECTION_CLEAR);
	jz_gpio_set_irq_bit(data, JZ_REG_GPIO_SELECT_CLEAR);
}

static int jz_gpio_irq_set_type(struct irq_data *data, unsigned int flow_type)
{
	struct jz_gpio_chip *chip = irq_to_jz_gpio_chip(data);
	unsigned int irq = data->irq;

	if (flow_type == IRQ_TYPE_EDGE_BOTH) {
		uint32_t value = readl(chip->base + JZ_REG_GPIO_PIN);
		if (value & IRQ_TO_BIT(irq))
			flow_type = IRQ_TYPE_EDGE_FALLING;
		else
			flow_type = IRQ_TYPE_EDGE_RISING;
		chip->edge_trigger_both |= IRQ_TO_BIT(irq);
	} else {
		chip->edge_trigger_both &= ~IRQ_TO_BIT(irq);
	}

	switch (flow_type) {
	case IRQ_TYPE_EDGE_RISING:
		jz_gpio_set_irq_bit(data, JZ_REG_GPIO_DIRECTION_SET);
		jz_gpio_set_irq_bit(data, JZ_REG_GPIO_TRIGGER_SET);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		jz_gpio_set_irq_bit(data, JZ_REG_GPIO_DIRECTION_CLEAR);
		jz_gpio_set_irq_bit(data, JZ_REG_GPIO_TRIGGER_SET);
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		jz_gpio_set_irq_bit(data, JZ_REG_GPIO_DIRECTION_SET);
		jz_gpio_set_irq_bit(data, JZ_REG_GPIO_TRIGGER_CLEAR);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		jz_gpio_set_irq_bit(data, JZ_REG_GPIO_DIRECTION_CLEAR);
		jz_gpio_set_irq_bit(data, JZ_REG_GPIO_TRIGGER_CLEAR);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int jz_gpio_irq_set_wake(struct irq_data *data, unsigned int on)
{
	struct jz_gpio_chip *chip = irq_to_jz_gpio_chip(data);

	irq_gc_set_wake(data, on);
	irq_set_irq_wake(chip->irq, on);

	return 0;
}

#define JZ4740_GPIO_CHIP(_bank) { \
	.irq_base = JZ4740_IRQ_GPIO_BASE_ ## _bank, \
	.gpio_chip = { \
		.label = "Bank " # _bank, \
		.owner = THIS_MODULE, \
		.set = jz_gpio_set_value, \
		.get = jz_gpio_get_value, \
		.direction_output = jz_gpio_direction_output, \
		.direction_input = jz_gpio_direction_input, \
		.base = JZ4740_GPIO_BASE_ ## _bank, \
		.ngpio = JZ4740_GPIO_NUM_ ## _bank, \
	}, \
}

static struct jz_gpio_chip jz4740_gpio_chips[] = {
	JZ4740_GPIO_CHIP(A),
	JZ4740_GPIO_CHIP(B),
	JZ4740_GPIO_CHIP(C),
	JZ4740_GPIO_CHIP(D),
};

static void jz4740_gpio_chip_init(struct jz_gpio_chip *chip, unsigned int id)
{
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;

	chip->base = ioremap(JZ4740_GPIO_BASE_ADDR + (id * 0x100), 0x100);

	chip->irq = JZ4740_IRQ_INTC_GPIO(id);
	irq_set_handler_data(chip->irq, chip);
	irq_set_chained_handler(chip->irq, jz_gpio_irq_demux_handler);

	gc = irq_alloc_generic_chip(chip->gpio_chip.label, 1, chip->irq_base,
		chip->base, handle_level_irq);

	gc->wake_enabled = IRQ_MSK(chip->gpio_chip.ngpio);
	gc->private = chip;

	ct = gc->chip_types;
	ct->regs.enable = JZ_REG_GPIO_MASK_CLEAR;
	ct->regs.disable = JZ_REG_GPIO_MASK_SET;
	ct->regs.ack = JZ_REG_GPIO_FLAG_CLEAR;

	ct->chip.name = "GPIO";
	ct->chip.irq_mask = irq_gc_mask_disable_reg;
	ct->chip.irq_unmask = jz_gpio_irq_unmask;
	ct->chip.irq_ack = irq_gc_ack_set_bit;
	ct->chip.irq_suspend = jz4740_irq_suspend;
	ct->chip.irq_resume = jz4740_irq_resume;
	ct->chip.irq_startup = jz_gpio_irq_startup;
	ct->chip.irq_shutdown = jz_gpio_irq_shutdown;
	ct->chip.irq_set_type = jz_gpio_irq_set_type;
	ct->chip.irq_set_wake = jz_gpio_irq_set_wake;
	ct->chip.flags = IRQCHIP_SET_TYPE_MASKED;

	irq_setup_generic_chip(gc, IRQ_MSK(chip->gpio_chip.ngpio),
		IRQ_GC_INIT_NESTED_LOCK, 0, IRQ_NOPROBE | IRQ_LEVEL);

	gpiochip_add(&chip->gpio_chip);
}

static int __init jz4740_gpio_init(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(jz4740_gpio_chips); ++i)
		jz4740_gpio_chip_init(&jz4740_gpio_chips[i], i);

	printk(KERN_INFO "JZ4740 GPIO initialized\n");

	return 0;
}
arch_initcall(jz4740_gpio_init);

#ifdef CONFIG_DEBUG_FS

static inline void gpio_seq_reg(struct seq_file *s, struct jz_gpio_chip *chip,
	const char *name, unsigned int reg)
{
	seq_printf(s, "\t%s: %08x\n", name, readl(chip->base + reg));
}

static int gpio_regs_show(struct seq_file *s, void *unused)
{
	struct jz_gpio_chip *chip = jz4740_gpio_chips;
	int i;

	for (i = 0; i < ARRAY_SIZE(jz4740_gpio_chips); ++i, ++chip) {
		seq_printf(s, "==GPIO %d==\n", i);
		gpio_seq_reg(s, chip, "Pin", JZ_REG_GPIO_PIN);
		gpio_seq_reg(s, chip, "Data", JZ_REG_GPIO_DATA);
		gpio_seq_reg(s, chip, "Mask", JZ_REG_GPIO_MASK);
		gpio_seq_reg(s, chip, "Pull", JZ_REG_GPIO_PULL);
		gpio_seq_reg(s, chip, "Func", JZ_REG_GPIO_FUNC);
		gpio_seq_reg(s, chip, "Select", JZ_REG_GPIO_SELECT);
		gpio_seq_reg(s, chip, "Direction", JZ_REG_GPIO_DIRECTION);
		gpio_seq_reg(s, chip, "Trigger", JZ_REG_GPIO_TRIGGER);
		gpio_seq_reg(s, chip, "Flag", JZ_REG_GPIO_FLAG);
	}

	return 0;
}

static int gpio_regs_open(struct inode *inode, struct file *file)
{
	return single_open(file, gpio_regs_show, NULL);
}

static const struct file_operations gpio_regs_operations = {
	.open		= gpio_regs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init gpio_debugfs_init(void)
{
	(void) debugfs_create_file("jz_regs_gpio", S_IFREG | S_IRUGO,
				NULL, NULL, &gpio_regs_operations);
	return 0;
}
subsys_initcall(gpio_debugfs_init);

#endif
