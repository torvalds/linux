/*
 *  PCA953x 4/8/16/24/40 bit I/O ports
 *
 *  Copyright (C) 2005 Ben Gardner <bgardner@wabtec.com>
 *  Copyright (C) 2007 Marvell International Ltd.
 *
 *  Derived from drivers/i2c/chips/pca9539.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 */

#include <linux/acpi.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_data/pca953x.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#include <asm/unaligned.h>

#define PCA953X_INPUT		0x00
#define PCA953X_OUTPUT		0x01
#define PCA953X_INVERT		0x02
#define PCA953X_DIRECTION	0x03

#define REG_ADDR_AI		0x80

#define PCA957X_IN		0x00
#define PCA957X_INVRT		0x01
#define PCA957X_BKEN		0x02
#define PCA957X_PUPD		0x03
#define PCA957X_CFG		0x04
#define PCA957X_OUT		0x05
#define PCA957X_MSK		0x06
#define PCA957X_INTS		0x07

#define PCAL953X_OUT_STRENGTH	0x20
#define PCAL953X_IN_LATCH	0x22
#define PCAL953X_PULL_EN	0x23
#define PCAL953X_PULL_SEL	0x24
#define PCAL953X_INT_MASK	0x25
#define PCAL953X_INT_STAT	0x26
#define PCAL953X_OUT_CONF	0x27

#define PCAL6524_INT_EDGE	0x28
#define PCAL6524_INT_CLR	0x2a
#define PCAL6524_IN_STATUS	0x2b
#define PCAL6524_OUT_INDCONF	0x2c
#define PCAL6524_DEBOUNCE	0x2d

#define PCA_GPIO_MASK		0x00FF

#define PCAL_GPIO_MASK		0x1f
#define PCAL_PINCTRL_MASK	0xe0

#define PCA_INT			0x0100
#define PCA_PCAL		0x0200
#define PCA_LATCH_INT (PCA_PCAL | PCA_INT)
#define PCA953X_TYPE		0x1000
#define PCA957X_TYPE		0x2000
#define PCA_TYPE_MASK		0xF000

#define PCA_CHIP_TYPE(x)	((x) & PCA_TYPE_MASK)

static const struct i2c_device_id pca953x_id[] = {
	{ "pca9505", 40 | PCA953X_TYPE | PCA_INT, },
	{ "pca9534", 8  | PCA953X_TYPE | PCA_INT, },
	{ "pca9535", 16 | PCA953X_TYPE | PCA_INT, },
	{ "pca9536", 4  | PCA953X_TYPE, },
	{ "pca9537", 4  | PCA953X_TYPE | PCA_INT, },
	{ "pca9538", 8  | PCA953X_TYPE | PCA_INT, },
	{ "pca9539", 16 | PCA953X_TYPE | PCA_INT, },
	{ "pca9554", 8  | PCA953X_TYPE | PCA_INT, },
	{ "pca9555", 16 | PCA953X_TYPE | PCA_INT, },
	{ "pca9556", 8  | PCA953X_TYPE, },
	{ "pca9557", 8  | PCA953X_TYPE, },
	{ "pca9574", 8  | PCA957X_TYPE | PCA_INT, },
	{ "pca9575", 16 | PCA957X_TYPE | PCA_INT, },
	{ "pca9698", 40 | PCA953X_TYPE, },

	{ "pcal6524", 24 | PCA953X_TYPE | PCA_INT | PCA_PCAL, },
	{ "pcal9555a", 16 | PCA953X_TYPE | PCA_INT | PCA_PCAL, },

	{ "max7310", 8  | PCA953X_TYPE, },
	{ "max7312", 16 | PCA953X_TYPE | PCA_INT, },
	{ "max7313", 16 | PCA953X_TYPE | PCA_INT, },
	{ "max7315", 8  | PCA953X_TYPE | PCA_INT, },
	{ "max7318", 16 | PCA953X_TYPE | PCA_INT, },
	{ "pca6107", 8  | PCA953X_TYPE | PCA_INT, },
	{ "tca6408", 8  | PCA953X_TYPE | PCA_INT, },
	{ "tca6416", 16 | PCA953X_TYPE | PCA_INT, },
	{ "tca6424", 24 | PCA953X_TYPE | PCA_INT, },
	{ "tca9539", 16 | PCA953X_TYPE | PCA_INT, },
	{ "tca9554", 8  | PCA953X_TYPE | PCA_INT, },
	{ "xra1202", 8  | PCA953X_TYPE },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pca953x_id);

static const struct acpi_device_id pca953x_acpi_ids[] = {
	{ "INT3491", 16 | PCA953X_TYPE | PCA_INT | PCA_PCAL, },
	{ }
};
MODULE_DEVICE_TABLE(acpi, pca953x_acpi_ids);

#define MAX_BANK 5
#define BANK_SZ 8

#define NBANK(chip) DIV_ROUND_UP(chip->gpio_chip.ngpio, BANK_SZ)

struct pca953x_reg_config {
	int direction;
	int output;
	int input;
};

static const struct pca953x_reg_config pca953x_regs = {
	.direction = PCA953X_DIRECTION,
	.output = PCA953X_OUTPUT,
	.input = PCA953X_INPUT,
};

static const struct pca953x_reg_config pca957x_regs = {
	.direction = PCA957X_CFG,
	.output = PCA957X_OUT,
	.input = PCA957X_IN,
};

struct pca953x_chip {
	unsigned gpio_start;
	u8 reg_output[MAX_BANK];
	u8 reg_direction[MAX_BANK];
	struct mutex i2c_lock;

#ifdef CONFIG_GPIO_PCA953X_IRQ
	struct mutex irq_lock;
	u8 irq_mask[MAX_BANK];
	u8 irq_stat[MAX_BANK];
	u8 irq_trig_raise[MAX_BANK];
	u8 irq_trig_fall[MAX_BANK];
#endif

	struct i2c_client *client;
	struct gpio_chip gpio_chip;
	const char *const *names;
	unsigned long driver_data;
	struct regulator *regulator;

	const struct pca953x_reg_config *regs;

	int (*write_regs)(struct pca953x_chip *, int, u8 *);
	int (*read_regs)(struct pca953x_chip *, int, u8 *);
};

static int pca953x_read_single(struct pca953x_chip *chip, int reg, u32 *val,
				int off)
{
	int ret;
	int bank_shift = fls((chip->gpio_chip.ngpio - 1) / BANK_SZ);
	int offset = off / BANK_SZ;

	ret = i2c_smbus_read_byte_data(chip->client,
				(reg << bank_shift) + offset);
	*val = ret;

	if (ret < 0) {
		dev_err(&chip->client->dev, "failed reading register\n");
		return ret;
	}

	return 0;
}

static int pca953x_write_single(struct pca953x_chip *chip, int reg, u32 val,
				int off)
{
	int ret;
	int bank_shift = fls((chip->gpio_chip.ngpio - 1) / BANK_SZ);
	int offset = off / BANK_SZ;

	ret = i2c_smbus_write_byte_data(chip->client,
					(reg << bank_shift) + offset, val);

	if (ret < 0) {
		dev_err(&chip->client->dev, "failed writing register\n");
		return ret;
	}

	return 0;
}

static int pca953x_write_regs_8(struct pca953x_chip *chip, int reg, u8 *val)
{
	return i2c_smbus_write_byte_data(chip->client, reg, *val);
}

static int pca953x_write_regs_16(struct pca953x_chip *chip, int reg, u8 *val)
{
	u16 word = get_unaligned((u16 *)val);

	return i2c_smbus_write_word_data(chip->client, reg << 1, word);
}

static int pca957x_write_regs_16(struct pca953x_chip *chip, int reg, u8 *val)
{
	int ret;

	ret = i2c_smbus_write_byte_data(chip->client, reg << 1, val[0]);
	if (ret < 0)
		return ret;

	return i2c_smbus_write_byte_data(chip->client, (reg << 1) + 1, val[1]);
}

static int pca953x_write_regs_24(struct pca953x_chip *chip, int reg, u8 *val)
{
	int bank_shift = fls((chip->gpio_chip.ngpio - 1) / BANK_SZ);
	int addr = (reg & PCAL_GPIO_MASK) << bank_shift;
	int pinctrl = (reg & PCAL_PINCTRL_MASK) << 1;

	return i2c_smbus_write_i2c_block_data(chip->client,
					      pinctrl | addr | REG_ADDR_AI,
					      NBANK(chip), val);
}

static int pca953x_write_regs(struct pca953x_chip *chip, int reg, u8 *val)
{
	int ret = 0;

	ret = chip->write_regs(chip, reg, val);
	if (ret < 0) {
		dev_err(&chip->client->dev, "failed writing register\n");
		return ret;
	}

	return 0;
}

static int pca953x_read_regs_8(struct pca953x_chip *chip, int reg, u8 *val)
{
	int ret;

	ret = i2c_smbus_read_byte_data(chip->client, reg);
	*val = ret;

	return ret;
}

static int pca953x_read_regs_16(struct pca953x_chip *chip, int reg, u8 *val)
{
	int ret;

	ret = i2c_smbus_read_word_data(chip->client, reg << 1);
	put_unaligned(ret, (u16 *)val);

	return ret;
}

static int pca953x_read_regs_24(struct pca953x_chip *chip, int reg, u8 *val)
{
	int bank_shift = fls((chip->gpio_chip.ngpio - 1) / BANK_SZ);
	int addr = (reg & PCAL_GPIO_MASK) << bank_shift;
	int pinctrl = (reg & PCAL_PINCTRL_MASK) << 1;

	return i2c_smbus_read_i2c_block_data(chip->client,
					     pinctrl | addr | REG_ADDR_AI,
					     NBANK(chip), val);
}

static int pca953x_read_regs(struct pca953x_chip *chip, int reg, u8 *val)
{
	int ret;

	ret = chip->read_regs(chip, reg, val);
	if (ret < 0) {
		dev_err(&chip->client->dev, "failed reading register\n");
		return ret;
	}

	return 0;
}

static int pca953x_gpio_direction_input(struct gpio_chip *gc, unsigned off)
{
	struct pca953x_chip *chip = gpiochip_get_data(gc);
	u8 reg_val;
	int ret;

	mutex_lock(&chip->i2c_lock);
	reg_val = chip->reg_direction[off / BANK_SZ] | (1u << (off % BANK_SZ));

	ret = pca953x_write_single(chip, chip->regs->direction, reg_val, off);
	if (ret)
		goto exit;

	chip->reg_direction[off / BANK_SZ] = reg_val;
exit:
	mutex_unlock(&chip->i2c_lock);
	return ret;
}

static int pca953x_gpio_direction_output(struct gpio_chip *gc,
		unsigned off, int val)
{
	struct pca953x_chip *chip = gpiochip_get_data(gc);
	u8 reg_val;
	int ret;

	mutex_lock(&chip->i2c_lock);
	/* set output level */
	if (val)
		reg_val = chip->reg_output[off / BANK_SZ]
			| (1u << (off % BANK_SZ));
	else
		reg_val = chip->reg_output[off / BANK_SZ]
			& ~(1u << (off % BANK_SZ));

	ret = pca953x_write_single(chip, chip->regs->output, reg_val, off);
	if (ret)
		goto exit;

	chip->reg_output[off / BANK_SZ] = reg_val;

	/* then direction */
	reg_val = chip->reg_direction[off / BANK_SZ] & ~(1u << (off % BANK_SZ));
	ret = pca953x_write_single(chip, chip->regs->direction, reg_val, off);
	if (ret)
		goto exit;

	chip->reg_direction[off / BANK_SZ] = reg_val;
exit:
	mutex_unlock(&chip->i2c_lock);
	return ret;
}

static int pca953x_gpio_get_value(struct gpio_chip *gc, unsigned off)
{
	struct pca953x_chip *chip = gpiochip_get_data(gc);
	u32 reg_val;
	int ret;

	mutex_lock(&chip->i2c_lock);
	ret = pca953x_read_single(chip, chip->regs->input, &reg_val, off);
	mutex_unlock(&chip->i2c_lock);
	if (ret < 0) {
		/* NOTE:  diagnostic already emitted; that's all we should
		 * do unless gpio_*_value_cansleep() calls become different
		 * from their nonsleeping siblings (and report faults).
		 */
		return 0;
	}

	return (reg_val & (1u << (off % BANK_SZ))) ? 1 : 0;
}

static void pca953x_gpio_set_value(struct gpio_chip *gc, unsigned off, int val)
{
	struct pca953x_chip *chip = gpiochip_get_data(gc);
	u8 reg_val;
	int ret;

	mutex_lock(&chip->i2c_lock);
	if (val)
		reg_val = chip->reg_output[off / BANK_SZ]
			| (1u << (off % BANK_SZ));
	else
		reg_val = chip->reg_output[off / BANK_SZ]
			& ~(1u << (off % BANK_SZ));

	ret = pca953x_write_single(chip, chip->regs->output, reg_val, off);
	if (ret)
		goto exit;

	chip->reg_output[off / BANK_SZ] = reg_val;
exit:
	mutex_unlock(&chip->i2c_lock);
}

static int pca953x_gpio_get_direction(struct gpio_chip *gc, unsigned off)
{
	struct pca953x_chip *chip = gpiochip_get_data(gc);
	u32 reg_val;
	int ret;

	mutex_lock(&chip->i2c_lock);
	ret = pca953x_read_single(chip, chip->regs->direction, &reg_val, off);
	mutex_unlock(&chip->i2c_lock);
	if (ret < 0)
		return ret;

	return !!(reg_val & (1u << (off % BANK_SZ)));
}

static void pca953x_gpio_set_multiple(struct gpio_chip *gc,
				      unsigned long *mask, unsigned long *bits)
{
	struct pca953x_chip *chip = gpiochip_get_data(gc);
	unsigned int bank_mask, bank_val;
	int bank_shift, bank;
	u8 reg_val[MAX_BANK];
	int ret;

	bank_shift = fls((chip->gpio_chip.ngpio - 1) / BANK_SZ);

	mutex_lock(&chip->i2c_lock);
	memcpy(reg_val, chip->reg_output, NBANK(chip));
	for (bank = 0; bank < NBANK(chip); bank++) {
		bank_mask = mask[bank / sizeof(*mask)] >>
			   ((bank % sizeof(*mask)) * 8);
		if (bank_mask) {
			bank_val = bits[bank / sizeof(*bits)] >>
				  ((bank % sizeof(*bits)) * 8);
			bank_val &= bank_mask;
			reg_val[bank] = (reg_val[bank] & ~bank_mask) | bank_val;
		}
	}

	ret = i2c_smbus_write_i2c_block_data(chip->client,
					     chip->regs->output << bank_shift,
					     NBANK(chip), reg_val);
	if (ret)
		goto exit;

	memcpy(chip->reg_output, reg_val, NBANK(chip));
exit:
	mutex_unlock(&chip->i2c_lock);
}

static void pca953x_setup_gpio(struct pca953x_chip *chip, int gpios)
{
	struct gpio_chip *gc;

	gc = &chip->gpio_chip;

	gc->direction_input  = pca953x_gpio_direction_input;
	gc->direction_output = pca953x_gpio_direction_output;
	gc->get = pca953x_gpio_get_value;
	gc->set = pca953x_gpio_set_value;
	gc->get_direction = pca953x_gpio_get_direction;
	gc->set_multiple = pca953x_gpio_set_multiple;
	gc->can_sleep = true;

	gc->base = chip->gpio_start;
	gc->ngpio = gpios;
	gc->label = chip->client->name;
	gc->parent = &chip->client->dev;
	gc->owner = THIS_MODULE;
	gc->names = chip->names;
}

#ifdef CONFIG_GPIO_PCA953X_IRQ
static void pca953x_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct pca953x_chip *chip = gpiochip_get_data(gc);

	chip->irq_mask[d->hwirq / BANK_SZ] &= ~(1 << (d->hwirq % BANK_SZ));
}

static void pca953x_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct pca953x_chip *chip = gpiochip_get_data(gc);

	chip->irq_mask[d->hwirq / BANK_SZ] |= 1 << (d->hwirq % BANK_SZ);
}

static void pca953x_irq_bus_lock(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct pca953x_chip *chip = gpiochip_get_data(gc);

	mutex_lock(&chip->irq_lock);
}

static void pca953x_irq_bus_sync_unlock(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct pca953x_chip *chip = gpiochip_get_data(gc);
	u8 new_irqs;
	int level, i;
	u8 invert_irq_mask[MAX_BANK];

	if (chip->driver_data & PCA_PCAL) {
		/* Enable latch on interrupt-enabled inputs */
		pca953x_write_regs(chip, PCAL953X_IN_LATCH, chip->irq_mask);

		for (i = 0; i < NBANK(chip); i++)
			invert_irq_mask[i] = ~chip->irq_mask[i];

		/* Unmask enabled interrupts */
		pca953x_write_regs(chip, PCAL953X_INT_MASK, invert_irq_mask);
	}

	/* Look for any newly setup interrupt */
	for (i = 0; i < NBANK(chip); i++) {
		new_irqs = chip->irq_trig_fall[i] | chip->irq_trig_raise[i];
		new_irqs &= ~chip->reg_direction[i];

		while (new_irqs) {
			level = __ffs(new_irqs);
			pca953x_gpio_direction_input(&chip->gpio_chip,
							level + (BANK_SZ * i));
			new_irqs &= ~(1 << level);
		}
	}

	mutex_unlock(&chip->irq_lock);
}

static int pca953x_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct pca953x_chip *chip = gpiochip_get_data(gc);
	int bank_nb = d->hwirq / BANK_SZ;
	u8 mask = 1 << (d->hwirq % BANK_SZ);

	if (!(type & IRQ_TYPE_EDGE_BOTH)) {
		dev_err(&chip->client->dev, "irq %d: unsupported type %d\n",
			d->irq, type);
		return -EINVAL;
	}

	if (type & IRQ_TYPE_EDGE_FALLING)
		chip->irq_trig_fall[bank_nb] |= mask;
	else
		chip->irq_trig_fall[bank_nb] &= ~mask;

	if (type & IRQ_TYPE_EDGE_RISING)
		chip->irq_trig_raise[bank_nb] |= mask;
	else
		chip->irq_trig_raise[bank_nb] &= ~mask;

	return 0;
}

static void pca953x_irq_shutdown(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct pca953x_chip *chip = gpiochip_get_data(gc);
	u8 mask = 1 << (d->hwirq % BANK_SZ);

	chip->irq_trig_raise[d->hwirq / BANK_SZ] &= ~mask;
	chip->irq_trig_fall[d->hwirq / BANK_SZ] &= ~mask;
}

static struct irq_chip pca953x_irq_chip = {
	.name			= "pca953x",
	.irq_mask		= pca953x_irq_mask,
	.irq_unmask		= pca953x_irq_unmask,
	.irq_bus_lock		= pca953x_irq_bus_lock,
	.irq_bus_sync_unlock	= pca953x_irq_bus_sync_unlock,
	.irq_set_type		= pca953x_irq_set_type,
	.irq_shutdown		= pca953x_irq_shutdown,
};

static bool pca953x_irq_pending(struct pca953x_chip *chip, u8 *pending)
{
	u8 cur_stat[MAX_BANK];
	u8 old_stat[MAX_BANK];
	bool pending_seen = false;
	bool trigger_seen = false;
	u8 trigger[MAX_BANK];
	int ret, i;

	if (chip->driver_data & PCA_PCAL) {
		/* Read the current interrupt status from the device */
		ret = pca953x_read_regs(chip, PCAL953X_INT_STAT, trigger);
		if (ret)
			return false;

		/* Check latched inputs and clear interrupt status */
		ret = pca953x_read_regs(chip, PCA953X_INPUT, cur_stat);
		if (ret)
			return false;

		for (i = 0; i < NBANK(chip); i++) {
			/* Apply filter for rising/falling edge selection */
			pending[i] = (~cur_stat[i] & chip->irq_trig_fall[i]) |
				(cur_stat[i] & chip->irq_trig_raise[i]);
			pending[i] &= trigger[i];
			if (pending[i])
				pending_seen = true;
		}

		return pending_seen;
	}

	ret = pca953x_read_regs(chip, chip->regs->input, cur_stat);
	if (ret)
		return false;

	/* Remove output pins from the equation */
	for (i = 0; i < NBANK(chip); i++)
		cur_stat[i] &= chip->reg_direction[i];

	memcpy(old_stat, chip->irq_stat, NBANK(chip));

	for (i = 0; i < NBANK(chip); i++) {
		trigger[i] = (cur_stat[i] ^ old_stat[i]) & chip->irq_mask[i];
		if (trigger[i])
			trigger_seen = true;
	}

	if (!trigger_seen)
		return false;

	memcpy(chip->irq_stat, cur_stat, NBANK(chip));

	for (i = 0; i < NBANK(chip); i++) {
		pending[i] = (old_stat[i] & chip->irq_trig_fall[i]) |
			(cur_stat[i] & chip->irq_trig_raise[i]);
		pending[i] &= trigger[i];
		if (pending[i])
			pending_seen = true;
	}

	return pending_seen;
}

static irqreturn_t pca953x_irq_handler(int irq, void *devid)
{
	struct pca953x_chip *chip = devid;
	u8 pending[MAX_BANK];
	u8 level;
	unsigned nhandled = 0;
	int i;

	if (!pca953x_irq_pending(chip, pending))
		return IRQ_NONE;

	for (i = 0; i < NBANK(chip); i++) {
		while (pending[i]) {
			level = __ffs(pending[i]);
			handle_nested_irq(irq_find_mapping(chip->gpio_chip.irq.domain,
							level + (BANK_SZ * i)));
			pending[i] &= ~(1 << level);
			nhandled++;
		}
	}

	return (nhandled > 0) ? IRQ_HANDLED : IRQ_NONE;
}

static int pca953x_irq_setup(struct pca953x_chip *chip,
			     int irq_base)
{
	struct i2c_client *client = chip->client;
	int ret, i;

	if (client->irq && irq_base != -1
			&& (chip->driver_data & PCA_INT)) {
		ret = pca953x_read_regs(chip,
					chip->regs->input, chip->irq_stat);
		if (ret)
			return ret;

		/*
		 * There is no way to know which GPIO line generated the
		 * interrupt.  We have to rely on the previous read for
		 * this purpose.
		 */
		for (i = 0; i < NBANK(chip); i++)
			chip->irq_stat[i] &= chip->reg_direction[i];
		mutex_init(&chip->irq_lock);

		ret = devm_request_threaded_irq(&client->dev,
					client->irq,
					   NULL,
					   pca953x_irq_handler,
					   IRQF_TRIGGER_LOW | IRQF_ONESHOT |
						   IRQF_SHARED,
					   dev_name(&client->dev), chip);
		if (ret) {
			dev_err(&client->dev, "failed to request irq %d\n",
				client->irq);
			return ret;
		}

		ret =  gpiochip_irqchip_add_nested(&chip->gpio_chip,
						   &pca953x_irq_chip,
						   irq_base,
						   handle_simple_irq,
						   IRQ_TYPE_NONE);
		if (ret) {
			dev_err(&client->dev,
				"could not connect irqchip to gpiochip\n");
			return ret;
		}

		gpiochip_set_nested_irqchip(&chip->gpio_chip,
					    &pca953x_irq_chip,
					    client->irq);
	}

	return 0;
}

#else /* CONFIG_GPIO_PCA953X_IRQ */
static int pca953x_irq_setup(struct pca953x_chip *chip,
			     int irq_base)
{
	struct i2c_client *client = chip->client;

	if (client->irq && irq_base != -1 && (chip->driver_data & PCA_INT))
		dev_warn(&client->dev, "interrupt support not compiled in\n");

	return 0;
}
#endif

static int device_pca953x_init(struct pca953x_chip *chip, u32 invert)
{
	int ret;
	u8 val[MAX_BANK];

	chip->regs = &pca953x_regs;

	ret = pca953x_read_regs(chip, chip->regs->output, chip->reg_output);
	if (ret)
		goto out;

	ret = pca953x_read_regs(chip, chip->regs->direction,
				chip->reg_direction);
	if (ret)
		goto out;

	/* set platform specific polarity inversion */
	if (invert)
		memset(val, 0xFF, NBANK(chip));
	else
		memset(val, 0, NBANK(chip));

	ret = pca953x_write_regs(chip, PCA953X_INVERT, val);
out:
	return ret;
}

static int device_pca957x_init(struct pca953x_chip *chip, u32 invert)
{
	int ret;
	u8 val[MAX_BANK];

	chip->regs = &pca957x_regs;

	ret = pca953x_read_regs(chip, chip->regs->output, chip->reg_output);
	if (ret)
		goto out;
	ret = pca953x_read_regs(chip, chip->regs->direction,
				chip->reg_direction);
	if (ret)
		goto out;

	/* set platform specific polarity inversion */
	if (invert)
		memset(val, 0xFF, NBANK(chip));
	else
		memset(val, 0, NBANK(chip));
	ret = pca953x_write_regs(chip, PCA957X_INVRT, val);
	if (ret)
		goto out;

	/* To enable register 6, 7 to control pull up and pull down */
	memset(val, 0x02, NBANK(chip));
	ret = pca953x_write_regs(chip, PCA957X_BKEN, val);
	if (ret)
		goto out;

	return 0;
out:
	return ret;
}

static const struct of_device_id pca953x_dt_ids[];

static int pca953x_probe(struct i2c_client *client,
				   const struct i2c_device_id *i2c_id)
{
	struct pca953x_platform_data *pdata;
	struct pca953x_chip *chip;
	int irq_base = 0;
	int ret;
	u32 invert = 0;
	struct regulator *reg;

	chip = devm_kzalloc(&client->dev,
			sizeof(struct pca953x_chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;

	pdata = dev_get_platdata(&client->dev);
	if (pdata) {
		irq_base = pdata->irq_base;
		chip->gpio_start = pdata->gpio_base;
		invert = pdata->invert;
		chip->names = pdata->names;
	} else {
		struct gpio_desc *reset_gpio;

		chip->gpio_start = -1;
		irq_base = 0;

		/*
		 * See if we need to de-assert a reset pin.
		 *
		 * There is no known ACPI-enabled platforms that are
		 * using "reset" GPIO. Otherwise any of those platform
		 * must use _DSD method with corresponding property.
		 */
		reset_gpio = devm_gpiod_get_optional(&client->dev, "reset",
						     GPIOD_OUT_LOW);
		if (IS_ERR(reset_gpio))
			return PTR_ERR(reset_gpio);
	}

	chip->client = client;

	reg = devm_regulator_get(&client->dev, "vcc");
	if (IS_ERR(reg)) {
		ret = PTR_ERR(reg);
		if (ret != -EPROBE_DEFER)
			dev_err(&client->dev, "reg get err: %d\n", ret);
		return ret;
	}
	ret = regulator_enable(reg);
	if (ret) {
		dev_err(&client->dev, "reg en err: %d\n", ret);
		return ret;
	}
	chip->regulator = reg;

	if (i2c_id) {
		chip->driver_data = i2c_id->driver_data;
	} else {
		const struct acpi_device_id *acpi_id;
		struct device *dev = &client->dev;

		chip->driver_data = (uintptr_t)of_device_get_match_data(dev);
		if (!chip->driver_data) {
			acpi_id = acpi_match_device(pca953x_acpi_ids, dev);
			if (!acpi_id) {
				ret = -ENODEV;
				goto err_exit;
			}

			chip->driver_data = acpi_id->driver_data;
		}
	}

	mutex_init(&chip->i2c_lock);
	/*
	 * In case we have an i2c-mux controlled by a GPIO provided by an
	 * expander using the same driver higher on the device tree, read the
	 * i2c adapter nesting depth and use the retrieved value as lockdep
	 * subclass for chip->i2c_lock.
	 *
	 * REVISIT: This solution is not complete. It protects us from lockdep
	 * false positives when the expander controlling the i2c-mux is on
	 * a different level on the device tree, but not when it's on the same
	 * level on a different branch (in which case the subclass number
	 * would be the same).
	 *
	 * TODO: Once a correct solution is developed, a similar fix should be
	 * applied to all other i2c-controlled GPIO expanders (and potentially
	 * regmap-i2c).
	 */
	lockdep_set_subclass(&chip->i2c_lock,
			     i2c_adapter_depth(client->adapter));

	/* initialize cached registers from their original values.
	 * we can't share this chip with another i2c master.
	 */
	pca953x_setup_gpio(chip, chip->driver_data & PCA_GPIO_MASK);

	if (chip->gpio_chip.ngpio <= 8) {
		chip->write_regs = pca953x_write_regs_8;
		chip->read_regs = pca953x_read_regs_8;
	} else if (chip->gpio_chip.ngpio >= 24) {
		chip->write_regs = pca953x_write_regs_24;
		chip->read_regs = pca953x_read_regs_24;
	} else {
		if (PCA_CHIP_TYPE(chip->driver_data) == PCA953X_TYPE)
			chip->write_regs = pca953x_write_regs_16;
		else
			chip->write_regs = pca957x_write_regs_16;
		chip->read_regs = pca953x_read_regs_16;
	}

	if (PCA_CHIP_TYPE(chip->driver_data) == PCA953X_TYPE)
		ret = device_pca953x_init(chip, invert);
	else
		ret = device_pca957x_init(chip, invert);
	if (ret)
		goto err_exit;

	ret = devm_gpiochip_add_data(&client->dev, &chip->gpio_chip, chip);
	if (ret)
		goto err_exit;

	ret = pca953x_irq_setup(chip, irq_base);
	if (ret)
		goto err_exit;

	if (pdata && pdata->setup) {
		ret = pdata->setup(client, chip->gpio_chip.base,
				chip->gpio_chip.ngpio, pdata->context);
		if (ret < 0)
			dev_warn(&client->dev, "setup failed, %d\n", ret);
	}

	i2c_set_clientdata(client, chip);
	return 0;

err_exit:
	regulator_disable(chip->regulator);
	return ret;
}

static int pca953x_remove(struct i2c_client *client)
{
	struct pca953x_platform_data *pdata = dev_get_platdata(&client->dev);
	struct pca953x_chip *chip = i2c_get_clientdata(client);
	int ret;

	if (pdata && pdata->teardown) {
		ret = pdata->teardown(client, chip->gpio_chip.base,
				chip->gpio_chip.ngpio, pdata->context);
		if (ret < 0)
			dev_err(&client->dev, "%s failed, %d\n",
					"teardown", ret);
	} else {
		ret = 0;
	}

	regulator_disable(chip->regulator);

	return ret;
}

/* convenience to stop overlong match-table lines */
#define OF_953X(__nrgpio, __int) (void *)(__nrgpio | PCA953X_TYPE | __int)
#define OF_957X(__nrgpio, __int) (void *)(__nrgpio | PCA957X_TYPE | __int)

static const struct of_device_id pca953x_dt_ids[] = {
	{ .compatible = "nxp,pca9505", .data = OF_953X(40, PCA_INT), },
	{ .compatible = "nxp,pca9534", .data = OF_953X( 8, PCA_INT), },
	{ .compatible = "nxp,pca9535", .data = OF_953X(16, PCA_INT), },
	{ .compatible = "nxp,pca9536", .data = OF_953X( 4, 0), },
	{ .compatible = "nxp,pca9537", .data = OF_953X( 4, PCA_INT), },
	{ .compatible = "nxp,pca9538", .data = OF_953X( 8, PCA_INT), },
	{ .compatible = "nxp,pca9539", .data = OF_953X(16, PCA_INT), },
	{ .compatible = "nxp,pca9554", .data = OF_953X( 8, PCA_INT), },
	{ .compatible = "nxp,pca9555", .data = OF_953X(16, PCA_INT), },
	{ .compatible = "nxp,pca9556", .data = OF_953X( 8, 0), },
	{ .compatible = "nxp,pca9557", .data = OF_953X( 8, 0), },
	{ .compatible = "nxp,pca9574", .data = OF_957X( 8, PCA_INT), },
	{ .compatible = "nxp,pca9575", .data = OF_957X(16, PCA_INT), },
	{ .compatible = "nxp,pca9698", .data = OF_953X(40, 0), },

	{ .compatible = "nxp,pcal6524", .data = OF_953X(24, PCA_LATCH_INT), },
	{ .compatible = "nxp,pcal9555a", .data = OF_953X(16, PCA_LATCH_INT), },

	{ .compatible = "maxim,max7310", .data = OF_953X( 8, 0), },
	{ .compatible = "maxim,max7312", .data = OF_953X(16, PCA_INT), },
	{ .compatible = "maxim,max7313", .data = OF_953X(16, PCA_INT), },
	{ .compatible = "maxim,max7315", .data = OF_953X( 8, PCA_INT), },
	{ .compatible = "maxim,max7318", .data = OF_953X(16, PCA_INT), },

	{ .compatible = "ti,pca6107", .data = OF_953X( 8, PCA_INT), },
	{ .compatible = "ti,pca9536", .data = OF_953X( 4, 0), },
	{ .compatible = "ti,tca6408", .data = OF_953X( 8, PCA_INT), },
	{ .compatible = "ti,tca6416", .data = OF_953X(16, PCA_INT), },
	{ .compatible = "ti,tca6424", .data = OF_953X(24, PCA_INT), },

	{ .compatible = "onnn,pca9654", .data = OF_953X( 8, PCA_INT), },

	{ .compatible = "exar,xra1202", .data = OF_953X( 8, 0), },
	{ }
};

MODULE_DEVICE_TABLE(of, pca953x_dt_ids);

static struct i2c_driver pca953x_driver = {
	.driver = {
		.name	= "pca953x",
		.of_match_table = pca953x_dt_ids,
		.acpi_match_table = ACPI_PTR(pca953x_acpi_ids),
	},
	.probe		= pca953x_probe,
	.remove		= pca953x_remove,
	.id_table	= pca953x_id,
};

static int __init pca953x_init(void)
{
	return i2c_add_driver(&pca953x_driver);
}
/* register after i2c postcore initcall and before
 * subsys initcalls that may rely on these GPIOs
 */
subsys_initcall(pca953x_init);

static void __exit pca953x_exit(void)
{
	i2c_del_driver(&pca953x_driver);
}
module_exit(pca953x_exit);

MODULE_AUTHOR("eric miao <eric.miao@marvell.com>");
MODULE_DESCRIPTION("GPIO expander driver for PCA953x");
MODULE_LICENSE("GPL");
