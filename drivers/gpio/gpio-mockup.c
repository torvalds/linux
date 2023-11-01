// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * GPIO Testing Device Driver
 *
 * Copyright (C) 2014  Kamlakant Patel <kamlakant.patel@broadcom.com>
 * Copyright (C) 2015-2016  Bamvor Jian Zhang <bamv2005@gmail.com>
 * Copyright (C) 2017 Bartosz Golaszewski <brgl@bgdev.pl>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cleanup.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irq_sim.h>
#include <linux/irqdomain.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string_helpers.h>
#include <linux/uaccess.h>

#define GPIO_MOCKUP_MAX_GC	10
/*
 * We're storing two values per chip: the GPIO base and the number
 * of GPIO lines.
 */
#define GPIO_MOCKUP_MAX_RANGES	(GPIO_MOCKUP_MAX_GC * 2)
/* Maximum of four properties + the sentinel. */
#define GPIO_MOCKUP_MAX_PROP	5

/*
 * struct gpio_pin_status - structure describing a GPIO status
 * @dir:       Configures direction of gpio as "in" or "out"
 * @value:     Configures status of the gpio as 0(low) or 1(high)
 * @pull:      Configures the current pull of the GPIO as 0 (pull-down) or
 *             1 (pull-up)
 * @requested: Request status of this GPIO
 */
struct gpio_mockup_line_status {
	int dir;
	int value;
	int pull;
	bool requested;
};

struct gpio_mockup_chip {
	struct gpio_chip gc;
	struct gpio_mockup_line_status *lines;
	struct irq_domain *irq_sim_domain;
	struct dentry *dbg_dir;
	struct mutex lock;
};

struct gpio_mockup_dbgfs_private {
	struct gpio_mockup_chip *chip;
	unsigned int offset;
};

static int gpio_mockup_ranges[GPIO_MOCKUP_MAX_RANGES];
static int gpio_mockup_num_ranges;
module_param_array(gpio_mockup_ranges, int, &gpio_mockup_num_ranges, 0400);

static bool gpio_mockup_named_lines;
module_param_named(gpio_mockup_named_lines,
		   gpio_mockup_named_lines, bool, 0400);

static struct dentry *gpio_mockup_dbg_dir;

static int gpio_mockup_range_base(unsigned int index)
{
	return gpio_mockup_ranges[index * 2];
}

static int gpio_mockup_range_ngpio(unsigned int index)
{
	return gpio_mockup_ranges[index * 2 + 1];
}

static int __gpio_mockup_get(struct gpio_mockup_chip *chip,
			     unsigned int offset)
{
	return chip->lines[offset].value;
}

static int gpio_mockup_get(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_mockup_chip *chip = gpiochip_get_data(gc);
	int val;

	scoped_guard(mutex, &chip->lock)
		val = __gpio_mockup_get(chip, offset);

	return val;
}

static int gpio_mockup_get_multiple(struct gpio_chip *gc,
				    unsigned long *mask, unsigned long *bits)
{
	struct gpio_mockup_chip *chip = gpiochip_get_data(gc);
	unsigned int bit, val;

	scoped_guard(mutex, &chip->lock) {
		for_each_set_bit(bit, mask, gc->ngpio) {
			val = __gpio_mockup_get(chip, bit);
			__assign_bit(bit, bits, val);
		}
	}

	return 0;
}

static void __gpio_mockup_set(struct gpio_mockup_chip *chip,
			      unsigned int offset, int value)
{
	chip->lines[offset].value = !!value;
}

static void gpio_mockup_set(struct gpio_chip *gc,
			   unsigned int offset, int value)
{
	struct gpio_mockup_chip *chip = gpiochip_get_data(gc);

	guard(mutex)(&chip->lock);

	__gpio_mockup_set(chip, offset, value);
}

static void gpio_mockup_set_multiple(struct gpio_chip *gc,
				     unsigned long *mask, unsigned long *bits)
{
	struct gpio_mockup_chip *chip = gpiochip_get_data(gc);
	unsigned int bit;

	guard(mutex)(&chip->lock);

	for_each_set_bit(bit, mask, gc->ngpio)
		__gpio_mockup_set(chip, bit, test_bit(bit, bits));
}

static int gpio_mockup_apply_pull(struct gpio_mockup_chip *chip,
				  unsigned int offset, int value)
{
	struct gpio_mockup_line_status *line = &chip->lines[offset];
	int curr, irq, irq_type, ret = 0;

	guard(mutex)(&chip->lock);

	if (line->requested && line->dir == GPIO_LINE_DIRECTION_IN) {
		curr = __gpio_mockup_get(chip, offset);
		if (curr == value)
			goto out;

		irq = irq_find_mapping(chip->irq_sim_domain, offset);
		if (!irq)
			/*
			 * This is fine - it just means, nobody is listening
			 * for interrupts on this line, otherwise
			 * irq_create_mapping() would have been called from
			 * the to_irq() callback.
			 */
			goto set_value;

		irq_type = irq_get_trigger_type(irq);

		if ((value == 1 && (irq_type & IRQ_TYPE_EDGE_RISING)) ||
		    (value == 0 && (irq_type & IRQ_TYPE_EDGE_FALLING))) {
			ret = irq_set_irqchip_state(irq, IRQCHIP_STATE_PENDING,
						    true);
			if (ret)
				goto out;
		}
	}

set_value:
	/* Change the value unless we're actively driving the line. */
	if (!line->requested || line->dir == GPIO_LINE_DIRECTION_IN)
		__gpio_mockup_set(chip, offset, value);

out:
	chip->lines[offset].pull = value;
	return ret;
}

static int gpio_mockup_set_config(struct gpio_chip *gc,
				  unsigned int offset, unsigned long config)
{
	struct gpio_mockup_chip *chip = gpiochip_get_data(gc);

	switch (pinconf_to_config_param(config)) {
	case PIN_CONFIG_BIAS_PULL_UP:
		return gpio_mockup_apply_pull(chip, offset, 1);
	case PIN_CONFIG_BIAS_PULL_DOWN:
		return gpio_mockup_apply_pull(chip, offset, 0);
	default:
		break;
	}
	return -ENOTSUPP;
}

static int gpio_mockup_dirout(struct gpio_chip *gc,
			      unsigned int offset, int value)
{
	struct gpio_mockup_chip *chip = gpiochip_get_data(gc);

	scoped_guard(mutex, &chip->lock) {
		chip->lines[offset].dir = GPIO_LINE_DIRECTION_OUT;
		__gpio_mockup_set(chip, offset, value);
	}

	return 0;
}

static int gpio_mockup_dirin(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_mockup_chip *chip = gpiochip_get_data(gc);

	scoped_guard(mutex, &chip->lock)
		chip->lines[offset].dir = GPIO_LINE_DIRECTION_IN;

	return 0;
}

static int gpio_mockup_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_mockup_chip *chip = gpiochip_get_data(gc);
	int direction;

	scoped_guard(mutex, &chip->lock)
		direction = chip->lines[offset].dir;

	return direction;
}

static int gpio_mockup_to_irq(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_mockup_chip *chip = gpiochip_get_data(gc);

	return irq_create_mapping(chip->irq_sim_domain, offset);
}

static int gpio_mockup_request(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_mockup_chip *chip = gpiochip_get_data(gc);

	scoped_guard(mutex, &chip->lock)
		chip->lines[offset].requested = true;

	return 0;
}

static void gpio_mockup_free(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_mockup_chip *chip = gpiochip_get_data(gc);

	guard(mutex)(&chip->lock);

	chip->lines[offset].requested = false;
	__gpio_mockup_set(chip, offset, chip->lines[offset].pull);
}

static ssize_t gpio_mockup_debugfs_read(struct file *file,
					char __user *usr_buf,
					size_t size, loff_t *ppos)
{
	struct gpio_mockup_dbgfs_private *priv;
	struct gpio_mockup_chip *chip;
	struct seq_file *sfile;
	struct gpio_chip *gc;
	int val, cnt;
	char buf[3];

	if (*ppos != 0)
		return 0;

	sfile = file->private_data;
	priv = sfile->private;
	chip = priv->chip;
	gc = &chip->gc;

	val = gpio_mockup_get(gc, priv->offset);
	cnt = snprintf(buf, sizeof(buf), "%d\n", val);

	return simple_read_from_buffer(usr_buf, size, ppos, buf, cnt);
}

static ssize_t gpio_mockup_debugfs_write(struct file *file,
					 const char __user *usr_buf,
					 size_t size, loff_t *ppos)
{
	struct gpio_mockup_dbgfs_private *priv;
	int rv, val;
	struct seq_file *sfile;

	if (*ppos != 0)
		return -EINVAL;

	rv = kstrtoint_from_user(usr_buf, size, 0, &val);
	if (rv)
		return rv;
	if (val != 0 && val != 1)
		return -EINVAL;

	sfile = file->private_data;
	priv = sfile->private;
	rv = gpio_mockup_apply_pull(priv->chip, priv->offset, val);
	if (rv)
		return rv;

	return size;
}

static int gpio_mockup_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, NULL, inode->i_private);
}

/*
 * Each mockup chip is represented by a directory named after the chip's device
 * name under /sys/kernel/debug/gpio-mockup/. Each line is represented by
 * a file using the line's offset as the name under the chip's directory.
 *
 * Reading from the line's file yields the current *value*, writing to the
 * line's file changes the current *pull*. Default pull for mockup lines is
 * down.
 *
 * Examples:
 * - when a line pulled down is requested in output mode and driven high, its
 *   value will return to 0 once it's released
 * - when the line is requested in output mode and driven high, writing 0 to
 *   the corresponding debugfs file will change the pull to down but the
 *   reported value will still be 1 until the line is released
 * - line requested in input mode always reports the same value as its pull
 *   configuration
 * - when the line is requested in input mode and monitored for events, writing
 *   the same value to the debugfs file will be a noop, while writing the
 *   opposite value will generate a dummy interrupt with an appropriate edge
 */
static const struct file_operations gpio_mockup_debugfs_ops = {
	.owner = THIS_MODULE,
	.open = gpio_mockup_debugfs_open,
	.read = gpio_mockup_debugfs_read,
	.write = gpio_mockup_debugfs_write,
	.llseek = no_llseek,
	.release = single_release,
};

static void gpio_mockup_debugfs_setup(struct device *dev,
				      struct gpio_mockup_chip *chip)
{
	struct device *child __free(put_device) = NULL;
	struct gpio_mockup_dbgfs_private *priv;
	struct gpio_chip *gc;
	const char *devname;
	char *name;
	int i;

	gc = &chip->gc;

	/*
	 * There can only be a single GPIO device per platform device in
	 * gpio-mockup so using device_find_any_child() is OK.
	 */
	child = device_find_any_child(dev);
	if (!child)
		return;

	devname = dev_name(child);
	chip->dbg_dir = debugfs_create_dir(devname, gpio_mockup_dbg_dir);

	for (i = 0; i < gc->ngpio; i++) {
		name = devm_kasprintf(dev, GFP_KERNEL, "%d", i);
		if (!name)
			return;

		priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
		if (!priv)
			return;

		priv->chip = chip;
		priv->offset = i;

		debugfs_create_file(name, 0600, chip->dbg_dir, priv,
				    &gpio_mockup_debugfs_ops);
	}
}

static void gpio_mockup_debugfs_cleanup(void *data)
{
	struct gpio_mockup_chip *chip = data;

	debugfs_remove_recursive(chip->dbg_dir);
}

static void gpio_mockup_dispose_mappings(void *data)
{
	struct gpio_mockup_chip *chip = data;
	struct gpio_chip *gc = &chip->gc;
	int i, irq;

	for (i = 0; i < gc->ngpio; i++) {
		irq = irq_find_mapping(chip->irq_sim_domain, i);
		if (irq)
			irq_dispose_mapping(irq);
	}
}

static int gpio_mockup_probe(struct platform_device *pdev)
{
	struct gpio_mockup_chip *chip;
	struct gpio_chip *gc;
	struct device *dev;
	const char *name;
	int rv, base, i;
	u16 ngpio;

	dev = &pdev->dev;

	rv = device_property_read_u32(dev, "gpio-base", &base);
	if (rv)
		base = -1;

	rv = device_property_read_u16(dev, "nr-gpios", &ngpio);
	if (rv)
		return rv;

	rv = device_property_read_string(dev, "chip-label", &name);
	if (rv)
		name = dev_name(dev);

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	mutex_init(&chip->lock);

	gc = &chip->gc;
	gc->base = base;
	gc->ngpio = ngpio;
	gc->label = name;
	gc->owner = THIS_MODULE;
	gc->parent = dev;
	gc->get = gpio_mockup_get;
	gc->set = gpio_mockup_set;
	gc->get_multiple = gpio_mockup_get_multiple;
	gc->set_multiple = gpio_mockup_set_multiple;
	gc->direction_output = gpio_mockup_dirout;
	gc->direction_input = gpio_mockup_dirin;
	gc->get_direction = gpio_mockup_get_direction;
	gc->set_config = gpio_mockup_set_config;
	gc->to_irq = gpio_mockup_to_irq;
	gc->request = gpio_mockup_request;
	gc->free = gpio_mockup_free;

	chip->lines = devm_kcalloc(dev, gc->ngpio,
				   sizeof(*chip->lines), GFP_KERNEL);
	if (!chip->lines)
		return -ENOMEM;

	for (i = 0; i < gc->ngpio; i++)
		chip->lines[i].dir = GPIO_LINE_DIRECTION_IN;

	chip->irq_sim_domain = devm_irq_domain_create_sim(dev, NULL,
							  gc->ngpio);
	if (IS_ERR(chip->irq_sim_domain))
		return PTR_ERR(chip->irq_sim_domain);

	rv = devm_add_action_or_reset(dev, gpio_mockup_dispose_mappings, chip);
	if (rv)
		return rv;

	rv = devm_gpiochip_add_data(dev, &chip->gc, chip);
	if (rv)
		return rv;

	gpio_mockup_debugfs_setup(dev, chip);

	return devm_add_action_or_reset(dev, gpio_mockup_debugfs_cleanup, chip);
}

static const struct of_device_id gpio_mockup_of_match[] = {
	{ .compatible = "gpio-mockup", },
	{},
};
MODULE_DEVICE_TABLE(of, gpio_mockup_of_match);

static struct platform_driver gpio_mockup_driver = {
	.driver = {
		.name = "gpio-mockup",
		.of_match_table = gpio_mockup_of_match,
	},
	.probe = gpio_mockup_probe,
};

static struct platform_device *gpio_mockup_pdevs[GPIO_MOCKUP_MAX_GC];

static void gpio_mockup_unregister_pdevs(void)
{
	struct platform_device *pdev;
	struct fwnode_handle *fwnode;
	int i;

	for (i = 0; i < GPIO_MOCKUP_MAX_GC; i++) {
		pdev = gpio_mockup_pdevs[i];
		if (!pdev)
			continue;

		fwnode = dev_fwnode(&pdev->dev);
		platform_device_unregister(pdev);
		fwnode_remove_software_node(fwnode);
	}
}

static int __init gpio_mockup_register_chip(int idx)
{
	struct property_entry properties[GPIO_MOCKUP_MAX_PROP];
	struct platform_device_info pdevinfo;
	struct platform_device *pdev;
	struct fwnode_handle *fwnode;
	char **line_names = NULL;
	char chip_label[32];
	int prop = 0, base;
	u16 ngpio;

	memset(properties, 0, sizeof(properties));
	memset(&pdevinfo, 0, sizeof(pdevinfo));

	snprintf(chip_label, sizeof(chip_label), "gpio-mockup-%c", idx + 'A');
	properties[prop++] = PROPERTY_ENTRY_STRING("chip-label", chip_label);

	base = gpio_mockup_range_base(idx);
	if (base >= 0)
		properties[prop++] = PROPERTY_ENTRY_U32("gpio-base", base);

	ngpio = base < 0 ? gpio_mockup_range_ngpio(idx)
			 : gpio_mockup_range_ngpio(idx) - base;
	properties[prop++] = PROPERTY_ENTRY_U16("nr-gpios", ngpio);

	if (gpio_mockup_named_lines) {
		line_names = kasprintf_strarray(GFP_KERNEL, chip_label, ngpio);
		if (!line_names)
			return -ENOMEM;

		properties[prop++] = PROPERTY_ENTRY_STRING_ARRAY_LEN(
					"gpio-line-names", line_names, ngpio);
	}

	fwnode = fwnode_create_software_node(properties, NULL);
	if (IS_ERR(fwnode)) {
		kfree_strarray(line_names, ngpio);
		return PTR_ERR(fwnode);
	}

	pdevinfo.name = "gpio-mockup";
	pdevinfo.id = idx;
	pdevinfo.fwnode = fwnode;

	pdev = platform_device_register_full(&pdevinfo);
	kfree_strarray(line_names, ngpio);
	if (IS_ERR(pdev)) {
		fwnode_remove_software_node(fwnode);
		pr_err("error registering device");
		return PTR_ERR(pdev);
	}

	gpio_mockup_pdevs[idx] = pdev;

	return 0;
}

static int __init gpio_mockup_init(void)
{
	int i, num_chips, err;

	if ((gpio_mockup_num_ranges % 2) ||
	    (gpio_mockup_num_ranges > GPIO_MOCKUP_MAX_RANGES))
		return -EINVAL;

	/* Each chip is described by two values. */
	num_chips = gpio_mockup_num_ranges / 2;

	/*
	 * The second value in the <base GPIO - number of GPIOS> pair must
	 * always be greater than 0.
	 */
	for (i = 0; i < num_chips; i++) {
		if (gpio_mockup_range_ngpio(i) < 0)
			return -EINVAL;
	}

	gpio_mockup_dbg_dir = debugfs_create_dir("gpio-mockup", NULL);

	err = platform_driver_register(&gpio_mockup_driver);
	if (err) {
		pr_err("error registering platform driver\n");
		debugfs_remove_recursive(gpio_mockup_dbg_dir);
		return err;
	}

	for (i = 0; i < num_chips; i++) {
		err = gpio_mockup_register_chip(i);
		if (err) {
			platform_driver_unregister(&gpio_mockup_driver);
			gpio_mockup_unregister_pdevs();
			debugfs_remove_recursive(gpio_mockup_dbg_dir);
			return err;
		}
	}

	return 0;
}

static void __exit gpio_mockup_exit(void)
{
	gpio_mockup_unregister_pdevs();
	debugfs_remove_recursive(gpio_mockup_dbg_dir);
	platform_driver_unregister(&gpio_mockup_driver);
}

module_init(gpio_mockup_init);
module_exit(gpio_mockup_exit);

MODULE_AUTHOR("Kamlakant Patel <kamlakant.patel@broadcom.com>");
MODULE_AUTHOR("Bamvor Jian Zhang <bamv2005@gmail.com>");
MODULE_AUTHOR("Bartosz Golaszewski <brgl@bgdev.pl>");
MODULE_DESCRIPTION("GPIO Testing driver");
MODULE_LICENSE("GPL v2");
