/*
 * GPIO Testing Device Driver
 *
 * Copyright (C) 2014  Kamlakant Patel <kamlakant.patel@broadcom.com>
 * Copyright (C) 2015-2016  Bamvor Jian Zhang <bamvor.zhangjian@linaro.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irq_work.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>

#include "gpiolib.h"

#define GPIO_MOCKUP_NAME	"gpio-mockup"
#define	GPIO_MOCKUP_MAX_GC	10

enum {
	DIR_IN = 0,
	DIR_OUT,
};

/*
 * struct gpio_pin_status - structure describing a GPIO status
 * @dir:       Configures direction of gpio as "in" or "out", 0=in, 1=out
 * @value:     Configures status of the gpio as 0(low) or 1(high)
 */
struct gpio_mockup_line_status {
	int dir;
	bool value;
};

struct gpio_mockup_irq_context {
	struct irq_work work;
	int irq;
};

struct gpio_mockup_chip {
	struct gpio_chip gc;
	struct gpio_mockup_line_status *lines;
	struct gpio_mockup_irq_context irq_ctx;
	struct dentry *dbg_dir;
};

struct gpio_mockup_dbgfs_private {
	struct gpio_mockup_chip *chip;
	struct gpio_desc *desc;
	int offset;
};

static int gpio_mockup_ranges[GPIO_MOCKUP_MAX_GC << 1];
static int gpio_mockup_params_nr;
module_param_array(gpio_mockup_ranges, int, &gpio_mockup_params_nr, 0400);

static bool gpio_mockup_named_lines;
module_param_named(gpio_mockup_named_lines,
		   gpio_mockup_named_lines, bool, 0400);

static const char gpio_mockup_name_start = 'A';
static struct dentry *gpio_mockup_dbg_dir;

static int gpio_mockup_get(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_mockup_chip *chip = gpiochip_get_data(gc);

	return chip->lines[offset].value;
}

static void gpio_mockup_set(struct gpio_chip *gc, unsigned int offset,
			    int value)
{
	struct gpio_mockup_chip *chip = gpiochip_get_data(gc);

	chip->lines[offset].value = !!value;
}

static int gpio_mockup_dirout(struct gpio_chip *gc, unsigned int offset,
			      int value)
{
	struct gpio_mockup_chip *chip = gpiochip_get_data(gc);

	gpio_mockup_set(gc, offset, value);
	chip->lines[offset].dir = DIR_OUT;

	return 0;
}

static int gpio_mockup_dirin(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_mockup_chip *chip = gpiochip_get_data(gc);

	chip->lines[offset].dir = DIR_IN;

	return 0;
}

static int gpio_mockup_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_mockup_chip *chip = gpiochip_get_data(gc);

	return chip->lines[offset].dir;
}

static int gpio_mockup_name_lines(struct device *dev,
				  struct gpio_mockup_chip *chip)
{
	struct gpio_chip *gc = &chip->gc;
	char **names;
	int i;

	names = devm_kzalloc(dev, sizeof(char *) * gc->ngpio, GFP_KERNEL);
	if (!names)
		return -ENOMEM;

	for (i = 0; i < gc->ngpio; i++) {
		names[i] = devm_kasprintf(dev, GFP_KERNEL,
					  "%s-%d", gc->label, i);
		if (!names[i])
			return -ENOMEM;
	}

	gc->names = (const char *const *)names;

	return 0;
}

static int gpio_mockup_to_irq(struct gpio_chip *chip, unsigned int offset)
{
	return chip->irq_base + offset;
}

/*
 * While we should generally support irqmask and irqunmask, this driver is
 * for testing purposes only so we don't care.
 */
static void gpio_mockup_irqmask(struct irq_data *d) { }
static void gpio_mockup_irqunmask(struct irq_data *d) { }

static struct irq_chip gpio_mockup_irqchip = {
	.name		= GPIO_MOCKUP_NAME,
	.irq_mask	= gpio_mockup_irqmask,
	.irq_unmask	= gpio_mockup_irqunmask,
};

static void gpio_mockup_handle_irq(struct irq_work *work)
{
	struct gpio_mockup_irq_context *irq_ctx;

	irq_ctx = container_of(work, struct gpio_mockup_irq_context, work);
	handle_simple_irq(irq_to_desc(irq_ctx->irq));
}

static int gpio_mockup_irqchip_setup(struct device *dev,
				     struct gpio_mockup_chip *chip)
{
	struct gpio_chip *gc = &chip->gc;
	int irq_base, i;

	irq_base = irq_alloc_descs(-1, 0, gc->ngpio, 0);
	if (irq_base < 0)
		return irq_base;

	gc->irq_base = irq_base;
	gc->irqchip = &gpio_mockup_irqchip;

	for (i = 0; i < gc->ngpio; i++) {
		irq_set_chip(irq_base + i, gc->irqchip);
		irq_set_handler(irq_base + i, &handle_simple_irq);
		irq_modify_status(irq_base + i,
				  IRQ_NOREQUEST | IRQ_NOAUTOEN, IRQ_NOPROBE);
	}

	init_irq_work(&chip->irq_ctx.work, gpio_mockup_handle_irq);

	return 0;
}

static ssize_t gpio_mockup_event_write(struct file *file,
				       const char __user *usr_buf,
				       size_t size, loff_t *ppos)
{
	struct gpio_mockup_dbgfs_private *priv;
	struct gpio_mockup_chip *chip;
	struct seq_file *sfile;
	struct gpio_desc *desc;
	struct gpio_chip *gc;
	int val;
	char buf;

	sfile = file->private_data;
	priv = sfile->private;
	desc = priv->desc;
	chip = priv->chip;
	gc = &chip->gc;

	if (copy_from_user(&buf, usr_buf, 1))
		return -EFAULT;

	if (buf == '0')
		val = 0;
	else if (buf == '1')
		val = 1;
	else
		return -EINVAL;

	gpiod_set_value_cansleep(desc, val);
	priv->chip->irq_ctx.irq = gc->irq_base + priv->offset;
	irq_work_queue(&priv->chip->irq_ctx.work);

	return size;
}

static int gpio_mockup_event_open(struct inode *inode, struct file *file)
{
	return single_open(file, NULL, inode->i_private);
}

static const struct file_operations gpio_mockup_event_ops = {
	.owner = THIS_MODULE,
	.open = gpio_mockup_event_open,
	.write = gpio_mockup_event_write,
	.llseek = no_llseek,
};

static void gpio_mockup_debugfs_setup(struct device *dev,
				      struct gpio_mockup_chip *chip)
{
	struct gpio_mockup_dbgfs_private *priv;
	struct dentry *evfile;
	struct gpio_chip *gc;
	char *name;
	int i;

	gc = &chip->gc;

	chip->dbg_dir = debugfs_create_dir(gc->label, gpio_mockup_dbg_dir);
	if (!chip->dbg_dir)
		goto err;

	for (i = 0; i < gc->ngpio; i++) {
		name = devm_kasprintf(dev, GFP_KERNEL, "%d", i);
		if (!name)
			goto err;

		priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
		if (!priv)
			goto err;

		priv->chip = chip;
		priv->offset = i;
		priv->desc = &gc->gpiodev->descs[i];

		evfile = debugfs_create_file(name, 0200, chip->dbg_dir, priv,
					     &gpio_mockup_event_ops);
		if (!evfile)
			goto err;
	}

	return;

err:
	dev_err(dev, "error creating debugfs directory\n");
}

static int gpio_mockup_add(struct device *dev,
			   struct gpio_mockup_chip *chip,
			   const char *name, int base, int ngpio)
{
	struct gpio_chip *gc = &chip->gc;
	int ret;

	gc->base = base;
	gc->ngpio = ngpio;
	gc->label = name;
	gc->owner = THIS_MODULE;
	gc->parent = dev;
	gc->get = gpio_mockup_get;
	gc->set = gpio_mockup_set;
	gc->direction_output = gpio_mockup_dirout;
	gc->direction_input = gpio_mockup_dirin;
	gc->get_direction = gpio_mockup_get_direction;
	gc->to_irq = gpio_mockup_to_irq;

	chip->lines = devm_kzalloc(dev, sizeof(*chip->lines) * gc->ngpio,
				   GFP_KERNEL);
	if (!chip->lines)
		return -ENOMEM;

	if (gpio_mockup_named_lines) {
		ret = gpio_mockup_name_lines(dev, chip);
		if (ret)
			return ret;
	}

	ret = gpio_mockup_irqchip_setup(dev, chip);
	if (ret)
		return ret;

	ret = devm_gpiochip_add_data(dev, &chip->gc, chip);
	if (ret)
		return ret;

	if (gpio_mockup_dbg_dir)
		gpio_mockup_debugfs_setup(dev, chip);

	return 0;
}

static int gpio_mockup_probe(struct platform_device *pdev)
{
	struct gpio_mockup_chip *chips;
	struct device *dev = &pdev->dev;
	int ret, i, base, ngpio;
	char *chip_name;

	if (gpio_mockup_params_nr < 2)
		return -EINVAL;

	chips = devm_kzalloc(dev,
			     sizeof(*chips) * (gpio_mockup_params_nr >> 1),
			     GFP_KERNEL);
	if (!chips)
		return -ENOMEM;

	platform_set_drvdata(pdev, chips);

	for (i = 0; i < gpio_mockup_params_nr >> 1; i++) {
		base = gpio_mockup_ranges[i * 2];

		if (base == -1)
			ngpio = gpio_mockup_ranges[i * 2 + 1];
		else
			ngpio = gpio_mockup_ranges[i * 2 + 1] - base;

		if (ngpio >= 0) {
			chip_name = devm_kasprintf(dev, GFP_KERNEL,
						   "%s-%c", GPIO_MOCKUP_NAME,
						   gpio_mockup_name_start + i);
			if (!chip_name)
				return -ENOMEM;

			ret = gpio_mockup_add(dev, &chips[i],
					      chip_name, base, ngpio);
		} else {
			ret = -1;
		}

		if (ret) {
			dev_err(dev, "gpio<%d..%d> add failed\n",
				base, base < 0 ? ngpio : base + ngpio);

			return ret;
		}

		dev_info(dev, "gpio<%d..%d> add successful!",
			 base, base + ngpio);
	}

	return 0;
}

static int gpio_mockup_remove(struct platform_device *pdev)
{
	struct gpio_mockup_chip *chips;
	int i;

	chips = platform_get_drvdata(pdev);

	for (i = 0; i < gpio_mockup_params_nr >> 1; i++)
		irq_free_descs(chips[i].gc.irq_base, chips[i].gc.ngpio);

	return 0;
}

static struct platform_driver gpio_mockup_driver = {
	.driver = {
		.name = GPIO_MOCKUP_NAME,
	},
	.probe = gpio_mockup_probe,
	.remove = gpio_mockup_remove,
};

static struct platform_device *pdev;
static int __init mock_device_init(void)
{
	int err;

	gpio_mockup_dbg_dir = debugfs_create_dir("gpio-mockup-event", NULL);
	if (!gpio_mockup_dbg_dir)
		pr_err("%s: error creating debugfs directory\n",
		       GPIO_MOCKUP_NAME);

	pdev = platform_device_alloc(GPIO_MOCKUP_NAME, -1);
	if (!pdev)
		return -ENOMEM;

	err = platform_device_add(pdev);
	if (err) {
		platform_device_put(pdev);
		return err;
	}

	err = platform_driver_register(&gpio_mockup_driver);
	if (err) {
		platform_device_unregister(pdev);
		return err;
	}

	return 0;
}

static void __exit mock_device_exit(void)
{
	debugfs_remove_recursive(gpio_mockup_dbg_dir);
	platform_driver_unregister(&gpio_mockup_driver);
	platform_device_unregister(pdev);
}

module_init(mock_device_init);
module_exit(mock_device_exit);

MODULE_AUTHOR("Kamlakant Patel <kamlakant.patel@broadcom.com>");
MODULE_AUTHOR("Bamvor Jian Zhang <bamvor.zhangjian@linaro.org>");
MODULE_DESCRIPTION("GPIO Testing driver");
MODULE_LICENSE("GPL v2");
