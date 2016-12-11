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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/platform_data/pca953x.h>
#include <linux/slab.h>
#ifdef CONFIG_OF_GPIO
#include <linux/of_platform.h>
#endif
#include <linux/acpi.h>

#define PCA953X_INPUT		0
#define PCA953X_OUTPUT		1
#define PCA953X_INVERT		2
#define PCA953X_DIRECTION	3

#define REG_ADDR_AI		0x80

#define PCA957X_IN		0
#define PCA957X_INVRT		1
#define PCA957X_BKEN		2
#define PCA957X_PUPD		3
#define PCA957X_CFG		4
#define PCA957X_OUT		5
#define PCA957X_MSK		6
#define PCA957X_INTS		7

#define PCA_GPIO_MASK		0x00FF
#define PCA_INT			0x0100
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

	{ "max7310", 8  | PCA953X_TYPE, },
	{ "max7312", 16 | PCA953X_TYPE | PCA_INT, },
	{ "max7313", 16 | PCA953X_TYPE | PCA_INT, },
	{ "max7315", 8  | PCA953X_TYPE | PCA_INT, },
	{ "pca6107", 8  | PCA953X_TYPE | PCA_INT, },
	{ "tca6408", 8  | PCA953X_TYPE | PCA_INT, },
	{ "tca6416", 16 | PCA953X_TYPE | PCA_INT, },
	{ "tca6424", 24 | PCA953X_TYPE | PCA_INT, },
	{ "tca9539", 16 | PCA953X_TYPE | PCA_INT, },
	{ "xra1202", 8  | PCA953X_TYPE },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pca953x_id);

static const struct acpi_device_id pca953x_acpi_ids[] = {
	{ "INT3491", 16 | PCA953X_TYPE | PCA_INT, },
	{ }
};
MODULE_DEVICE_TABLE(acpi, pca953x_acpi_ids);

#define MAX_BANK 5
#define BANK_SZ 8

#define NBANK(chip) DIV_ROUND_UP(chip->gpio_chip.ngpio, BANK_SZ)

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
	int	chip_type;
	unsigned long driver_data;
};

static inline struct pca953x_chip *to_pca(struct gpio_chip *gc)
{
	return container_of(gc, struct pca953x_chip, gpio_chip);
}

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
	int ret = 0;
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

static int pca953x_write_regs(struct pca953x_chip *chip, int reg, u8 *val)
{
	int ret = 0;

	if (chip->gpio_chip.ngpio <= 8)
		ret = i2c_smbus_write_byte_data(chip->client, reg, *val);
	else if (chip->gpio_chip.ngpio >= 24) {
		int bank_shift = fls((chip->gpio_chip.ngpio - 1) / BANK_SZ);
		ret = i2c_smbus_write_i2c_block_data(chip->client,
					(reg << bank_shift) | REG_ADDR_AI,
					NBANK(chip), val);
	} else {
		switch (chip->chip_type) {
		case PCA953X_TYPE:
			ret = i2c_smbus_write_word_data(chip->client,
							reg << 1, (u16) *val);
			break;
		case PCA957X_TYPE:
			ret = i2c_smbus_write_byte_data(chip->client, reg << 1,
							val[0]);
			if (ret < 0)
				break;
			ret = i2c_smbus_write_byte_data(chip->client,
							(reg << 1) + 1,
							val[1]);
			break;
		}
	}

	if (ret < 0) {
		dev_err(&chip->client->dev, "failed writing register\n");
		return ret;
	}

	return 0;
}

static int pca953x_read_regs(struct pca953x_chip *chip, int reg, u8 *val)
{
	int ret;

	if (chip->gpio_chip.ngpio <= 8) {
		ret = i2c_smbus_read_byte_data(chip->client, reg);
		*val = ret;
	} else if (chip->gpio_chip.ngpio >= 24) {
		int bank_shift = fls((chip->gpio_chip.ngpio - 1) / BANK_SZ);

		ret = i2c_smbus_read_i2c_block_data(chip->client,
					(reg << bank_shift) | REG_ADDR_AI,
					NBANK(chip), val);
	} else {
		ret = i2c_smbus_read_word_data(chip->client, reg << 1);
		val[0] = (u16)ret & 0xFF;
		val[1] = (u16)ret >> 8;
	}
	if (ret < 0) {
		dev_err(&chip->client->dev, "failed reading register\n");
		return ret;
	}

	return 0;
}

static int pca953x_gpio_direction_input(struct gpio_chip *gc, unsigned off)
{
	struct pca953x_chip *chip = to_pca(gc);
	u8 reg_val;
	int ret, offset = 0;

	mutex_lock(&chip->i2c_lock);
	reg_val = chip->reg_direction[off / BANK_SZ] | (1u << (off % BANK_SZ));

	switch (chip->chip_type) {
	case PCA953X_TYPE:
		offset = PCA953X_DIRECTION;
		break;
	case PCA957X_TYPE:
		offset = PCA957X_CFG;
		break;
	}
	ret = pca953x_write_single(chip, offset, reg_val, off);
	if (ret)
		goto exit;

	chip->reg_direction[off / BANK_SZ] = reg_val;
	ret = 0;
exit:
	mutex_unlock(&chip->i2c_lock);
	return ret;
}

static int pca953x_gpio_direction_output(struct gpio_chip *gc,
		unsigned off, int val)
{
	struct pca953x_chip *chip = to_pca(gc);
	u8 reg_val;
	int ret, offset = 0;

	mutex_lock(&chip->i2c_lock);
	/* set output level */
	if (val)
		reg_val = chip->reg_output[off / BANK_SZ]
			| (1u << (off % BANK_SZ));
	else
		reg_val = chip->reg_output[off / BANK_SZ]
			& ~(1u << (off % BANK_SZ));

	switch (chip->chip_type) {
	case PCA953X_TYPE:
		offset = PCA953X_OUTPUT;
		break;
	case PCA957X_TYPE:
		offset = PCA957X_OUT;
		break;
	}
	ret = pca953x_write_single(chip, offset, reg_val, off);
	if (ret)
		goto exit;

	chip->reg_output[off / BANK_SZ] = reg_val;

	/* then direction */
	reg_val = chip->reg_direction[off / BANK_SZ] & ~(1u << (off % BANK_SZ));
	switch (chip->chip_type) {
	case PCA953X_TYPE:
		offset = PCA953X_DIRECTION;
		break;
	case PCA957X_TYPE:
		offset = PCA957X_CFG;
		break;
	}
	ret = pca953x_write_single(chip, offset, reg_val, off);
	if (ret)
		goto exit;

	chip->reg_direction[off / BANK_SZ] = reg_val;
	ret = 0;
exit:
	mutex_unlock(&chip->i2c_lock);
	return ret;
}

static int pca953x_gpio_get_value(struct gpio_chip *gc, unsigned off)
{
	struct pca953x_chip *chip = to_pca(gc);
	u32 reg_val;
	int ret, offset = 0;

	mutex_lock(&chip->i2c_lock);
	switch (chip->chip_type) {
	case PCA953X_TYPE:
		offset = PCA953X_INPUT;
		break;
	case PCA957X_TYPE:
		offset = PCA957X_IN;
		break;
	}
	ret = pca953x_read_single(chip, offset, &reg_val, off);
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
	struct pca953x_chip *chip = to_pca(gc);
	u8 reg_val;
	int ret, offset = 0;

	mutex_lock(&chip->i2c_lock);
	if (val)
		reg_val = chip->reg_output[off / BANK_SZ]
			| (1u << (off % BANK_SZ));
	else
		reg_val = chip->reg_output[off / BANK_SZ]
			& ~(1u << (off % BANK_SZ));

	switch (chip->chip_type) {
	case PCA953X_TYPE:
		offset = PCA953X_OUTPUT;
		break;
	case PCA957X_TYPE:
		offset = PCA957X_OUT;
		break;
	}
	ret = pca953x_write_single(chip, offset, reg_val, off);
	if (ret)
		goto exit;

	chip->reg_output[off / BANK_SZ] = reg_val;
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
	gc->can_sleep = true;

	gc->base = chip->gpio_start;
	gc->ngpio = gpios;
	gc->label = chip->client->name;
	gc->dev = &chip->client->dev;
	gc->owner = THIS_MODULE;
	gc->names = chip->names;
}

#ifdef CONFIG_GPIO_PCA953X_IRQ
static void pca953x_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct pca953x_chip *chip = to_pca(gc);

	chip->irq_mask[d->hwirq / BANK_SZ] &= ~(1 << (d->hwirq % BANK_SZ));
}

static void pca953x_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct pca953x_chip *chip = to_pca(gc);

	chip->irq_mask[d->hwirq / BANK_SZ] |= 1 << (d->hwirq % BANK_SZ);
}

static void pca953x_irq_bus_lock(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct pca953x_chip *chip = to_pca(gc);

	mutex_lock(&chip->irq_lock);
}

static void pca953x_irq_bus_sync_unlock(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct pca953x_chip *chip = to_pca(gc);
	u8 new_irqs;
	int level, i;

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
	struct pca953x_chip *chip = to_pca(gc);
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

static struct irq_chip pca953x_irq_chip = {
	.name			= "pca953x",
	.irq_mask		= pca953x_irq_mask,
	.irq_unmask		= pca953x_irq_unmask,
	.irq_bus_lock		= pca953x_irq_bus_lock,
	.irq_bus_sync_unlock	= pca953x_irq_bus_sync_unlock,
	.irq_set_type		= pca953x_irq_set_type,
};

static bool pca953x_irq_pending(struct pca953x_chip *chip, u8 *pending)
{
	u8 cur_stat[MAX_BANK];
	u8 old_stat[MAX_BANK];
	bool pending_seen = false;
	bool trigger_seen = false;
	u8 trigger[MAX_BANK];
	int ret, i, offset = 0;

	switch (chip->chip_type) {
	case PCA953X_TYPE:
		offset = PCA953X_INPUT;
		break;
	case PCA957X_TYPE:
		offset = PCA957X_IN;
		break;
	}
	ret = pca953x_read_regs(chip, offset, cur_stat);
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
			handle_nested_irq(irq_find_mapping(chip->gpio_chip.irqdomain,
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
	int ret, i, offset = 0;

	if (client->irq && irq_base != -1
			&& (chip->driver_data & PCA_INT)) {

		switch (chip->chip_type) {
		case PCA953X_TYPE:
			offset = PCA953X_INPUT;
			break;
		case PCA957X_TYPE:
			offset = PCA957X_IN;
			break;
		}
		ret = pca953x_read_regs(chip, offset, chip->irq_stat);
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

		ret =  gpiochip_irqchip_add(&chip->gpio_chip,
					    &pca953x_irq_chip,
					    irq_base,
					    handle_simple_irq,
					    IRQ_TYPE_NONE);
		if (ret) {
			dev_err(&client->dev,
				"could not connect irqchip to gpiochip\n");
			return ret;
		}

		gpiochip_set_chained_irqchip(&chip->gpio_chip,
					     &pca953x_irq_chip,
					     client->irq, NULL);
	}

	return 0;
}

#else /* CONFIG_GPIO_PCA953X_IRQ */
static int pca953x_irq_setup(struct pca953x_chip *chip,
			     int irq_base)
{
	struct i2c_client *client = chip->client;

	if (irq_base != -1 && (chip->driver_data & PCA_INT))
		dev_warn(&client->dev, "interrupt support not compiled in\n");

	return 0;
}
#endif

static int device_pca953x_init(struct pca953x_chip *chip, u32 invert)
{
	int ret;
	u8 val[MAX_BANK];

	ret = pca953x_read_regs(chip, PCA953X_OUTPUT, chip->reg_output);
	if (ret)
		goto out;

	ret = pca953x_read_regs(chip, PCA953X_DIRECTION,
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

	ret = pca953x_read_regs(chip, PCA957X_OUT, chip->reg_output);
	if (ret)
		goto out;
	ret = pca953x_read_regs(chip, PCA957X_CFG, chip->reg_direction);
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

static int pca953x_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	struct pca953x_platform_data *pdata;
	struct pca953x_chip *chip;
	int irq_base = 0;
	int ret;
	u32 invert = 0;

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
		chip->gpio_start = -1;
		irq_base = 0;
	}

	chip->client = client;

	if (id) {
		chip->driver_data = id->driver_data;
	} else {
		const struct acpi_device_id *id;

		id = acpi_match_device(pca953x_acpi_ids, &client->dev);
		if (!id)
			return -ENODEV;

		chip->driver_data = id->driver_data;
	}

	chip->chip_type = PCA_CHIP_TYPE(chip->driver_data);

	mutex_init(&chip->i2c_lock);

	/* initialize cached registers from their original values.
	 * we can't share this chip with another i2c master.
	 */
	pca953x_setup_gpio(chip, chip->driver_data & PCA_GPIO_MASK);

	if (chip->chip_type == PCA953X_TYPE)
		ret = device_pca953x_init(chip, invert);
	else
		ret = device_pca957x_init(chip, invert);
	if (ret)
		return ret;

	ret = gpiochip_add(&chip->gpio_chip);
	if (ret)
		return ret;

	ret = pca953x_irq_setup(chip, irq_base);
	if (ret)
		return ret;

	if (pdata && pdata->setup) {
		ret = pdata->setup(client, chip->gpio_chip.base,
				chip->gpio_chip.ngpio, pdata->context);
		if (ret < 0)
			dev_warn(&client->dev, "setup failed, %d\n", ret);
	}

	i2c_set_clientdata(client, chip);
	return 0;
}

static int pca953x_remove(struct i2c_client *client)
{
	struct pca953x_platform_data *pdata = dev_get_platdata(&client->dev);
	struct pca953x_chip *chip = i2c_get_clientdata(client);
	int ret = 0;

	if (pdata && pdata->teardown) {
		ret = pdata->teardown(client, chip->gpio_chip.base,
				chip->gpio_chip.ngpio, pdata->context);
		if (ret < 0) {
			dev_err(&client->dev, "%s failed, %d\n",
					"teardown", ret);
			return ret;
		}
	}

	gpiochip_remove(&chip->gpio_chip);

	return 0;
}

static const struct of_device_id pca953x_dt_ids[] = {
	{ .compatible = "nxp,pca9505", },
	{ .compatible = "nxp,pca9534", },
	{ .compatible = "nxp,pca9535", },
	{ .compatible = "nxp,pca9536", },
	{ .compatible = "nxp,pca9537", },
	{ .compatible = "nxp,pca9538", },
	{ .compatible = "nxp,pca9539", },
	{ .compatible = "nxp,pca9554", },
	{ .compatible = "nxp,pca9555", },
	{ .compatible = "nxp,pca9556", },
	{ .compatible = "nxp,pca9557", },
	{ .compatible = "nxp,pca9574", },
	{ .compatible = "nxp,pca9575", },
	{ .compatible = "nxp,pca9698", },

	{ .compatible = "maxim,max7310", },
	{ .compatible = "maxim,max7312", },
	{ .compatible = "maxim,max7313", },
	{ .compatible = "maxim,max7315", },

	{ .compatible = "ti,pca6107", },
	{ .compatible = "ti,tca6408", },
	{ .compatible = "ti,tca6416", },
	{ .compatible = "ti,tca6424", },

	{ .compatible = "exar,xra1202", },
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
