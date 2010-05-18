/*
 *  pca953x.c - 4/8/16 bit I/O ports
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
#include <linux/irq.h>
#include <linux/i2c.h>
#include <linux/i2c/pca953x.h>
#include <linux/slab.h>
#ifdef CONFIG_OF_GPIO
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#endif

#define PCA953X_INPUT          0
#define PCA953X_OUTPUT         1
#define PCA953X_INVERT         2
#define PCA953X_DIRECTION      3

#define PCA953X_GPIOS	       0x00FF
#define PCA953X_INT	       0x0100

static const struct i2c_device_id pca953x_id[] = {
	{ "pca9534", 8  | PCA953X_INT, },
	{ "pca9535", 16 | PCA953X_INT, },
	{ "pca9536", 4, },
	{ "pca9537", 4  | PCA953X_INT, },
	{ "pca9538", 8  | PCA953X_INT, },
	{ "pca9539", 16 | PCA953X_INT, },
	{ "pca9554", 8  | PCA953X_INT, },
	{ "pca9555", 16 | PCA953X_INT, },
	{ "pca9556", 8, },
	{ "pca9557", 8, },

	{ "max7310", 8, },
	{ "max7312", 16 | PCA953X_INT, },
	{ "max7313", 16 | PCA953X_INT, },
	{ "max7315", 8  | PCA953X_INT, },
	{ "pca6107", 8  | PCA953X_INT, },
	{ "tca6408", 8  | PCA953X_INT, },
	{ "tca6416", 16 | PCA953X_INT, },
	/* NYET:  { "tca6424", 24, }, */
	{ }
};
MODULE_DEVICE_TABLE(i2c, pca953x_id);

struct pca953x_chip {
	unsigned gpio_start;
	uint16_t reg_output;
	uint16_t reg_direction;

#ifdef CONFIG_GPIO_PCA953X_IRQ
	struct mutex irq_lock;
	uint16_t irq_mask;
	uint16_t irq_stat;
	uint16_t irq_trig_raise;
	uint16_t irq_trig_fall;
	int	 irq_base;
#endif

	struct i2c_client *client;
	struct pca953x_platform_data *dyn_pdata;
	struct gpio_chip gpio_chip;
	char **names;
};

static int pca953x_write_reg(struct pca953x_chip *chip, int reg, uint16_t val)
{
	int ret;

	if (chip->gpio_chip.ngpio <= 8)
		ret = i2c_smbus_write_byte_data(chip->client, reg, val);
	else
		ret = i2c_smbus_write_word_data(chip->client, reg << 1, val);

	if (ret < 0) {
		dev_err(&chip->client->dev, "failed writing register\n");
		return ret;
	}

	return 0;
}

static int pca953x_read_reg(struct pca953x_chip *chip, int reg, uint16_t *val)
{
	int ret;

	if (chip->gpio_chip.ngpio <= 8)
		ret = i2c_smbus_read_byte_data(chip->client, reg);
	else
		ret = i2c_smbus_read_word_data(chip->client, reg << 1);

	if (ret < 0) {
		dev_err(&chip->client->dev, "failed reading register\n");
		return ret;
	}

	*val = (uint16_t)ret;
	return 0;
}

static int pca953x_gpio_direction_input(struct gpio_chip *gc, unsigned off)
{
	struct pca953x_chip *chip;
	uint16_t reg_val;
	int ret;

	chip = container_of(gc, struct pca953x_chip, gpio_chip);

	reg_val = chip->reg_direction | (1u << off);
	ret = pca953x_write_reg(chip, PCA953X_DIRECTION, reg_val);
	if (ret)
		return ret;

	chip->reg_direction = reg_val;
	return 0;
}

static int pca953x_gpio_direction_output(struct gpio_chip *gc,
		unsigned off, int val)
{
	struct pca953x_chip *chip;
	uint16_t reg_val;
	int ret;

	chip = container_of(gc, struct pca953x_chip, gpio_chip);

	/* set output level */
	if (val)
		reg_val = chip->reg_output | (1u << off);
	else
		reg_val = chip->reg_output & ~(1u << off);

	ret = pca953x_write_reg(chip, PCA953X_OUTPUT, reg_val);
	if (ret)
		return ret;

	chip->reg_output = reg_val;

	/* then direction */
	reg_val = chip->reg_direction & ~(1u << off);
	ret = pca953x_write_reg(chip, PCA953X_DIRECTION, reg_val);
	if (ret)
		return ret;

	chip->reg_direction = reg_val;
	return 0;
}

static int pca953x_gpio_get_value(struct gpio_chip *gc, unsigned off)
{
	struct pca953x_chip *chip;
	uint16_t reg_val;
	int ret;

	chip = container_of(gc, struct pca953x_chip, gpio_chip);

	ret = pca953x_read_reg(chip, PCA953X_INPUT, &reg_val);
	if (ret < 0) {
		/* NOTE:  diagnostic already emitted; that's all we should
		 * do unless gpio_*_value_cansleep() calls become different
		 * from their nonsleeping siblings (and report faults).
		 */
		return 0;
	}

	return (reg_val & (1u << off)) ? 1 : 0;
}

static void pca953x_gpio_set_value(struct gpio_chip *gc, unsigned off, int val)
{
	struct pca953x_chip *chip;
	uint16_t reg_val;
	int ret;

	chip = container_of(gc, struct pca953x_chip, gpio_chip);

	if (val)
		reg_val = chip->reg_output | (1u << off);
	else
		reg_val = chip->reg_output & ~(1u << off);

	ret = pca953x_write_reg(chip, PCA953X_OUTPUT, reg_val);
	if (ret)
		return;

	chip->reg_output = reg_val;
}

static void pca953x_setup_gpio(struct pca953x_chip *chip, int gpios)
{
	struct gpio_chip *gc;

	gc = &chip->gpio_chip;

	gc->direction_input  = pca953x_gpio_direction_input;
	gc->direction_output = pca953x_gpio_direction_output;
	gc->get = pca953x_gpio_get_value;
	gc->set = pca953x_gpio_set_value;
	gc->can_sleep = 1;

	gc->base = chip->gpio_start;
	gc->ngpio = gpios;
	gc->label = chip->client->name;
	gc->dev = &chip->client->dev;
	gc->owner = THIS_MODULE;
	gc->names = chip->names;
}

#ifdef CONFIG_GPIO_PCA953X_IRQ
static int pca953x_gpio_to_irq(struct gpio_chip *gc, unsigned off)
{
	struct pca953x_chip *chip;

	chip = container_of(gc, struct pca953x_chip, gpio_chip);
	return chip->irq_base + off;
}

static void pca953x_irq_mask(unsigned int irq)
{
	struct pca953x_chip *chip = get_irq_chip_data(irq);

	chip->irq_mask &= ~(1 << (irq - chip->irq_base));
}

static void pca953x_irq_unmask(unsigned int irq)
{
	struct pca953x_chip *chip = get_irq_chip_data(irq);

	chip->irq_mask |= 1 << (irq - chip->irq_base);
}

static void pca953x_irq_bus_lock(unsigned int irq)
{
	struct pca953x_chip *chip = get_irq_chip_data(irq);

	mutex_lock(&chip->irq_lock);
}

static void pca953x_irq_bus_sync_unlock(unsigned int irq)
{
	struct pca953x_chip *chip = get_irq_chip_data(irq);
	uint16_t new_irqs;
	uint16_t level;

	/* Look for any newly setup interrupt */
	new_irqs = chip->irq_trig_fall | chip->irq_trig_raise;
	new_irqs &= ~chip->reg_direction;

	while (new_irqs) {
		level = __ffs(new_irqs);
		pca953x_gpio_direction_input(&chip->gpio_chip, level);
		new_irqs &= ~(1 << level);
	}

	mutex_unlock(&chip->irq_lock);
}

static int pca953x_irq_set_type(unsigned int irq, unsigned int type)
{
	struct pca953x_chip *chip = get_irq_chip_data(irq);
	uint16_t level = irq - chip->irq_base;
	uint16_t mask = 1 << level;

	if (!(type & IRQ_TYPE_EDGE_BOTH)) {
		dev_err(&chip->client->dev, "irq %d: unsupported type %d\n",
			irq, type);
		return -EINVAL;
	}

	if (type & IRQ_TYPE_EDGE_FALLING)
		chip->irq_trig_fall |= mask;
	else
		chip->irq_trig_fall &= ~mask;

	if (type & IRQ_TYPE_EDGE_RISING)
		chip->irq_trig_raise |= mask;
	else
		chip->irq_trig_raise &= ~mask;

	return 0;
}

static struct irq_chip pca953x_irq_chip = {
	.name			= "pca953x",
	.mask			= pca953x_irq_mask,
	.unmask			= pca953x_irq_unmask,
	.bus_lock		= pca953x_irq_bus_lock,
	.bus_sync_unlock	= pca953x_irq_bus_sync_unlock,
	.set_type		= pca953x_irq_set_type,
};

static uint16_t pca953x_irq_pending(struct pca953x_chip *chip)
{
	uint16_t cur_stat;
	uint16_t old_stat;
	uint16_t pending;
	uint16_t trigger;
	int ret;

	ret = pca953x_read_reg(chip, PCA953X_INPUT, &cur_stat);
	if (ret)
		return 0;

	/* Remove output pins from the equation */
	cur_stat &= chip->reg_direction;

	old_stat = chip->irq_stat;
	trigger = (cur_stat ^ old_stat) & chip->irq_mask;

	if (!trigger)
		return 0;

	chip->irq_stat = cur_stat;

	pending = (old_stat & chip->irq_trig_fall) |
		  (cur_stat & chip->irq_trig_raise);
	pending &= trigger;

	return pending;
}

static irqreturn_t pca953x_irq_handler(int irq, void *devid)
{
	struct pca953x_chip *chip = devid;
	uint16_t pending;
	uint16_t level;

	pending = pca953x_irq_pending(chip);

	if (!pending)
		return IRQ_HANDLED;

	do {
		level = __ffs(pending);
		handle_nested_irq(level + chip->irq_base);

		pending &= ~(1 << level);
	} while (pending);

	return IRQ_HANDLED;
}

static int pca953x_irq_setup(struct pca953x_chip *chip,
			     const struct i2c_device_id *id)
{
	struct i2c_client *client = chip->client;
	struct pca953x_platform_data *pdata = client->dev.platform_data;
	int ret;

	if (pdata->irq_base && (id->driver_data & PCA953X_INT)) {
		int lvl;

		ret = pca953x_read_reg(chip, PCA953X_INPUT,
				       &chip->irq_stat);
		if (ret)
			goto out_failed;

		/*
		 * There is no way to know which GPIO line generated the
		 * interrupt.  We have to rely on the previous read for
		 * this purpose.
		 */
		chip->irq_stat &= chip->reg_direction;
		chip->irq_base = pdata->irq_base;
		mutex_init(&chip->irq_lock);

		for (lvl = 0; lvl < chip->gpio_chip.ngpio; lvl++) {
			int irq = lvl + chip->irq_base;

			set_irq_chip_data(irq, chip);
			set_irq_chip_and_handler(irq, &pca953x_irq_chip,
						 handle_edge_irq);
			set_irq_nested_thread(irq, 1);
#ifdef CONFIG_ARM
			set_irq_flags(irq, IRQF_VALID);
#else
			set_irq_noprobe(irq);
#endif
		}

		ret = request_threaded_irq(client->irq,
					   NULL,
					   pca953x_irq_handler,
					   IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					   dev_name(&client->dev), chip);
		if (ret) {
			dev_err(&client->dev, "failed to request irq %d\n",
				client->irq);
			goto out_failed;
		}

		chip->gpio_chip.to_irq = pca953x_gpio_to_irq;
	}

	return 0;

out_failed:
	chip->irq_base = 0;
	return ret;
}

static void pca953x_irq_teardown(struct pca953x_chip *chip)
{
	if (chip->irq_base)
		free_irq(chip->client->irq, chip);
}
#else /* CONFIG_GPIO_PCA953X_IRQ */
static int pca953x_irq_setup(struct pca953x_chip *chip,
			     const struct i2c_device_id *id)
{
	struct i2c_client *client = chip->client;
	struct pca953x_platform_data *pdata = client->dev.platform_data;

	if (pdata->irq_base && (id->driver_data & PCA953X_INT))
		dev_warn(&client->dev, "interrupt support not compiled in\n");

	return 0;
}

static void pca953x_irq_teardown(struct pca953x_chip *chip)
{
}
#endif

/*
 * Handlers for alternative sources of platform_data
 */
#ifdef CONFIG_OF_GPIO
/*
 * Translate OpenFirmware node properties into platform_data
 */
static struct pca953x_platform_data *
pca953x_get_alt_pdata(struct i2c_client *client)
{
	struct pca953x_platform_data *pdata;
	struct device_node *node;
	const uint16_t *val;

	node = dev_archdata_get_node(&client->dev.archdata);
	if (node == NULL)
		return NULL;

	pdata = kzalloc(sizeof(struct pca953x_platform_data), GFP_KERNEL);
	if (pdata == NULL) {
		dev_err(&client->dev, "Unable to allocate platform_data\n");
		return NULL;
	}

	pdata->gpio_base = -1;
	val = of_get_property(node, "linux,gpio-base", NULL);
	if (val) {
		if (*val < 0)
			dev_warn(&client->dev,
				 "invalid gpio-base in device tree\n");
		else
			pdata->gpio_base = *val;
	}

	val = of_get_property(node, "polarity", NULL);
	if (val)
		pdata->invert = *val;

	return pdata;
}
#else
static struct pca953x_platform_data *
pca953x_get_alt_pdata(struct i2c_client *client)
{
	return NULL;
}
#endif

static int __devinit pca953x_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	struct pca953x_platform_data *pdata;
	struct pca953x_chip *chip;
	int ret;

	chip = kzalloc(sizeof(struct pca953x_chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;

	pdata = client->dev.platform_data;
	if (pdata == NULL) {
		pdata = pca953x_get_alt_pdata(client);
		/*
		 * Unlike normal platform_data, this is allocated
		 * dynamically and must be freed in the driver
		 */
		chip->dyn_pdata = pdata;
	}

	if (pdata == NULL) {
		dev_dbg(&client->dev, "no platform data\n");
		ret = -EINVAL;
		goto out_failed;
	}

	chip->client = client;

	chip->gpio_start = pdata->gpio_base;

	chip->names = pdata->names;

	/* initialize cached registers from their original values.
	 * we can't share this chip with another i2c master.
	 */
	pca953x_setup_gpio(chip, id->driver_data & PCA953X_GPIOS);

	ret = pca953x_read_reg(chip, PCA953X_OUTPUT, &chip->reg_output);
	if (ret)
		goto out_failed;

	ret = pca953x_read_reg(chip, PCA953X_DIRECTION, &chip->reg_direction);
	if (ret)
		goto out_failed;

	/* set platform specific polarity inversion */
	ret = pca953x_write_reg(chip, PCA953X_INVERT, pdata->invert);
	if (ret)
		goto out_failed;

	ret = pca953x_irq_setup(chip, id);
	if (ret)
		goto out_failed;

	ret = gpiochip_add(&chip->gpio_chip);
	if (ret)
		goto out_failed;

	if (pdata->setup) {
		ret = pdata->setup(client, chip->gpio_chip.base,
				chip->gpio_chip.ngpio, pdata->context);
		if (ret < 0)
			dev_warn(&client->dev, "setup failed, %d\n", ret);
	}

	i2c_set_clientdata(client, chip);
	return 0;

out_failed:
	pca953x_irq_teardown(chip);
	kfree(chip->dyn_pdata);
	kfree(chip);
	return ret;
}

static int pca953x_remove(struct i2c_client *client)
{
	struct pca953x_platform_data *pdata = client->dev.platform_data;
	struct pca953x_chip *chip = i2c_get_clientdata(client);
	int ret = 0;

	if (pdata->teardown) {
		ret = pdata->teardown(client, chip->gpio_chip.base,
				chip->gpio_chip.ngpio, pdata->context);
		if (ret < 0) {
			dev_err(&client->dev, "%s failed, %d\n",
					"teardown", ret);
			return ret;
		}
	}

	ret = gpiochip_remove(&chip->gpio_chip);
	if (ret) {
		dev_err(&client->dev, "%s failed, %d\n",
				"gpiochip_remove()", ret);
		return ret;
	}

	pca953x_irq_teardown(chip);
	kfree(chip->dyn_pdata);
	kfree(chip);
	return 0;
}

static struct i2c_driver pca953x_driver = {
	.driver = {
		.name	= "pca953x",
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
