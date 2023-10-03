// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * GPIO testing driver based on configfs.
 *
 * Copyright (C) 2021 Bartosz Golaszewski <brgl@bgdev.pl>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitmap.h>
#include <linux/cleanup.h>
#include <linux/completion.h>
#include <linux/configfs.h>
#include <linux/device.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/machine.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irq_sim.h>
#include <linux/list.h>
#include <linux/minmax.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/string_helpers.h>
#include <linux/sysfs.h>

#include "gpiolib.h"

#define GPIO_SIM_NGPIO_MAX	1024
#define GPIO_SIM_PROP_MAX	4 /* Max 3 properties + sentinel. */
#define GPIO_SIM_NUM_ATTRS	3 /* value, pull and sentinel */

static DEFINE_IDA(gpio_sim_ida);

struct gpio_sim_chip {
	struct gpio_chip gc;
	unsigned long *direction_map;
	unsigned long *value_map;
	unsigned long *pull_map;
	struct irq_domain *irq_sim;
	struct mutex lock;
	const struct attribute_group **attr_groups;
};

struct gpio_sim_attribute {
	struct device_attribute dev_attr;
	unsigned int offset;
};

static struct gpio_sim_attribute *
to_gpio_sim_attr(struct device_attribute *dev_attr)
{
	return container_of(dev_attr, struct gpio_sim_attribute, dev_attr);
}

static int gpio_sim_apply_pull(struct gpio_sim_chip *chip,
			       unsigned int offset, int value)
{
	int irq, irq_type, ret;
	struct gpio_desc *desc;
	struct gpio_chip *gc;

	gc = &chip->gc;
	desc = &gc->gpiodev->descs[offset];

	guard(mutex)(&chip->lock);

	if (test_bit(FLAG_REQUESTED, &desc->flags) &&
	    !test_bit(FLAG_IS_OUT, &desc->flags)) {
		if (value == !!test_bit(offset, chip->value_map))
			goto set_pull;

		/*
		 * This is fine - it just means, nobody is listening
		 * for interrupts on this line, otherwise
		 * irq_create_mapping() would have been called from
		 * the to_irq() callback.
		 */
		irq = irq_find_mapping(chip->irq_sim, offset);
		if (!irq)
			goto set_value;

		irq_type = irq_get_trigger_type(irq);

		if ((value && (irq_type & IRQ_TYPE_EDGE_RISING)) ||
		    (!value && (irq_type & IRQ_TYPE_EDGE_FALLING))) {
			ret = irq_set_irqchip_state(irq, IRQCHIP_STATE_PENDING,
						    true);
			if (ret)
				goto set_pull;
		}
	}

set_value:
	/* Change the value unless we're actively driving the line. */
	if (!test_bit(FLAG_REQUESTED, &desc->flags) ||
	    !test_bit(FLAG_IS_OUT, &desc->flags))
		__assign_bit(offset, chip->value_map, value);

set_pull:
	__assign_bit(offset, chip->pull_map, value);
	return 0;
}

static int gpio_sim_get(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_sim_chip *chip = gpiochip_get_data(gc);

	guard(mutex)(&chip->lock);

	return !!test_bit(offset, chip->value_map);
}

static void gpio_sim_set(struct gpio_chip *gc, unsigned int offset, int value)
{
	struct gpio_sim_chip *chip = gpiochip_get_data(gc);

	scoped_guard(mutex, &chip->lock)
		__assign_bit(offset, chip->value_map, value);
}

static int gpio_sim_get_multiple(struct gpio_chip *gc,
				 unsigned long *mask, unsigned long *bits)
{
	struct gpio_sim_chip *chip = gpiochip_get_data(gc);

	scoped_guard(mutex, &chip->lock)
		bitmap_replace(bits, bits, chip->value_map, mask, gc->ngpio);

	return 0;
}

static void gpio_sim_set_multiple(struct gpio_chip *gc,
				  unsigned long *mask, unsigned long *bits)
{
	struct gpio_sim_chip *chip = gpiochip_get_data(gc);

	scoped_guard(mutex, &chip->lock)
		bitmap_replace(chip->value_map, chip->value_map, bits, mask,
			       gc->ngpio);
}

static int gpio_sim_direction_output(struct gpio_chip *gc,
				     unsigned int offset, int value)
{
	struct gpio_sim_chip *chip = gpiochip_get_data(gc);

	scoped_guard(mutex, &chip->lock) {
		__clear_bit(offset, chip->direction_map);
		__assign_bit(offset, chip->value_map, value);
	}

	return 0;
}

static int gpio_sim_direction_input(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_sim_chip *chip = gpiochip_get_data(gc);

	scoped_guard(mutex, &chip->lock)
		__set_bit(offset, chip->direction_map);

	return 0;
}

static int gpio_sim_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_sim_chip *chip = gpiochip_get_data(gc);
	int direction;

	scoped_guard(mutex, &chip->lock)
		direction = !!test_bit(offset, chip->direction_map);

	return direction ? GPIO_LINE_DIRECTION_IN : GPIO_LINE_DIRECTION_OUT;
}

static int gpio_sim_set_config(struct gpio_chip *gc,
				  unsigned int offset, unsigned long config)
{
	struct gpio_sim_chip *chip = gpiochip_get_data(gc);

	switch (pinconf_to_config_param(config)) {
	case PIN_CONFIG_BIAS_PULL_UP:
		return gpio_sim_apply_pull(chip, offset, 1);
	case PIN_CONFIG_BIAS_PULL_DOWN:
		return gpio_sim_apply_pull(chip, offset, 0);
	default:
		break;
	}

	return -ENOTSUPP;
}

static int gpio_sim_to_irq(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_sim_chip *chip = gpiochip_get_data(gc);

	return irq_create_mapping(chip->irq_sim, offset);
}

static void gpio_sim_free(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_sim_chip *chip = gpiochip_get_data(gc);

	scoped_guard(mutex, &chip->lock)
		__assign_bit(offset, chip->value_map,
			     !!test_bit(offset, chip->pull_map));
}

static ssize_t gpio_sim_sysfs_val_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct gpio_sim_attribute *line_attr = to_gpio_sim_attr(attr);
	struct gpio_sim_chip *chip = dev_get_drvdata(dev);
	int val;

	scoped_guard(mutex, &chip->lock)
		val = !!test_bit(line_attr->offset, chip->value_map);

	return sysfs_emit(buf, "%d\n", val);
}

static ssize_t gpio_sim_sysfs_val_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	/*
	 * Not assigning this function will result in write() returning -EIO
	 * which is confusing. Return -EPERM explicitly.
	 */
	return -EPERM;
}

static const char *const gpio_sim_sysfs_pull_strings[] = {
	[0]	= "pull-down",
	[1]	= "pull-up",
};

static ssize_t gpio_sim_sysfs_pull_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct gpio_sim_attribute *line_attr = to_gpio_sim_attr(attr);
	struct gpio_sim_chip *chip = dev_get_drvdata(dev);
	int pull;

	scoped_guard(mutex, &chip->lock)
		pull = !!test_bit(line_attr->offset, chip->pull_map);

	return sysfs_emit(buf, "%s\n", gpio_sim_sysfs_pull_strings[pull]);
}

static ssize_t gpio_sim_sysfs_pull_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t len)
{
	struct gpio_sim_attribute *line_attr = to_gpio_sim_attr(attr);
	struct gpio_sim_chip *chip = dev_get_drvdata(dev);
	int ret, pull;

	pull = sysfs_match_string(gpio_sim_sysfs_pull_strings, buf);
	if (pull < 0)
		return pull;

	ret = gpio_sim_apply_pull(chip, line_attr->offset, pull);
	if (ret)
		return ret;

	return len;
}

static void gpio_sim_mutex_destroy(void *data)
{
	struct mutex *lock = data;

	mutex_destroy(lock);
}

static void gpio_sim_dispose_mappings(void *data)
{
	struct gpio_sim_chip *chip = data;
	unsigned int i;

	for (i = 0; i < chip->gc.ngpio; i++)
		irq_dispose_mapping(irq_find_mapping(chip->irq_sim, i));
}

static void gpio_sim_sysfs_remove(void *data)
{
	struct gpio_sim_chip *chip = data;

	sysfs_remove_groups(&chip->gc.gpiodev->dev.kobj, chip->attr_groups);
}

static int gpio_sim_setup_sysfs(struct gpio_sim_chip *chip)
{
	struct device_attribute *val_dev_attr, *pull_dev_attr;
	struct gpio_sim_attribute *val_attr, *pull_attr;
	unsigned int num_lines = chip->gc.ngpio;
	struct device *dev = chip->gc.parent;
	struct attribute_group *attr_group;
	struct attribute **attrs;
	int i, ret;

	chip->attr_groups = devm_kcalloc(dev, sizeof(*chip->attr_groups),
					 num_lines + 1, GFP_KERNEL);
	if (!chip->attr_groups)
		return -ENOMEM;

	for (i = 0; i < num_lines; i++) {
		attr_group = devm_kzalloc(dev, sizeof(*attr_group), GFP_KERNEL);
		attrs = devm_kcalloc(dev, GPIO_SIM_NUM_ATTRS, sizeof(*attrs),
				     GFP_KERNEL);
		val_attr = devm_kzalloc(dev, sizeof(*val_attr), GFP_KERNEL);
		pull_attr = devm_kzalloc(dev, sizeof(*pull_attr), GFP_KERNEL);
		if (!attr_group || !attrs || !val_attr || !pull_attr)
			return -ENOMEM;

		attr_group->name = devm_kasprintf(dev, GFP_KERNEL,
						  "sim_gpio%u", i);
		if (!attr_group->name)
			return -ENOMEM;

		val_attr->offset = pull_attr->offset = i;

		val_dev_attr = &val_attr->dev_attr;
		pull_dev_attr = &pull_attr->dev_attr;

		sysfs_attr_init(&val_dev_attr->attr);
		sysfs_attr_init(&pull_dev_attr->attr);

		val_dev_attr->attr.name = "value";
		pull_dev_attr->attr.name = "pull";

		val_dev_attr->attr.mode = pull_dev_attr->attr.mode = 0644;

		val_dev_attr->show = gpio_sim_sysfs_val_show;
		val_dev_attr->store = gpio_sim_sysfs_val_store;
		pull_dev_attr->show = gpio_sim_sysfs_pull_show;
		pull_dev_attr->store = gpio_sim_sysfs_pull_store;

		attrs[0] = &val_dev_attr->attr;
		attrs[1] = &pull_dev_attr->attr;

		attr_group->attrs = attrs;
		chip->attr_groups[i] = attr_group;
	}

	ret = sysfs_create_groups(&chip->gc.gpiodev->dev.kobj,
				  chip->attr_groups);
	if (ret)
		return ret;

	return devm_add_action_or_reset(dev, gpio_sim_sysfs_remove, chip);
}

static int gpio_sim_add_bank(struct fwnode_handle *swnode, struct device *dev)
{
	struct gpio_sim_chip *chip;
	struct gpio_chip *gc;
	const char *label;
	u32 num_lines;
	int ret;

	ret = fwnode_property_read_u32(swnode, "ngpios", &num_lines);
	if (ret)
		return ret;

	if (num_lines > GPIO_SIM_NGPIO_MAX)
		return -ERANGE;

	ret = fwnode_property_read_string(swnode, "gpio-sim,label", &label);
	if (ret) {
		label = devm_kasprintf(dev, GFP_KERNEL, "%s-%pfwP",
				       dev_name(dev), swnode);
		if (!label)
			return -ENOMEM;
	}

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->direction_map = devm_bitmap_alloc(dev, num_lines, GFP_KERNEL);
	if (!chip->direction_map)
		return -ENOMEM;

	/* Default to input mode. */
	bitmap_fill(chip->direction_map, num_lines);

	chip->value_map = devm_bitmap_zalloc(dev, num_lines, GFP_KERNEL);
	if (!chip->value_map)
		return -ENOMEM;

	chip->pull_map = devm_bitmap_zalloc(dev, num_lines, GFP_KERNEL);
	if (!chip->pull_map)
		return -ENOMEM;

	chip->irq_sim = devm_irq_domain_create_sim(dev, swnode, num_lines);
	if (IS_ERR(chip->irq_sim))
		return PTR_ERR(chip->irq_sim);

	ret = devm_add_action_or_reset(dev, gpio_sim_dispose_mappings, chip);
	if (ret)
		return ret;

	mutex_init(&chip->lock);
	ret = devm_add_action_or_reset(dev, gpio_sim_mutex_destroy,
				       &chip->lock);
	if (ret)
		return ret;

	gc = &chip->gc;
	gc->base = -1;
	gc->ngpio = num_lines;
	gc->label = label;
	gc->owner = THIS_MODULE;
	gc->parent = dev;
	gc->fwnode = swnode;
	gc->get = gpio_sim_get;
	gc->set = gpio_sim_set;
	gc->get_multiple = gpio_sim_get_multiple;
	gc->set_multiple = gpio_sim_set_multiple;
	gc->direction_output = gpio_sim_direction_output;
	gc->direction_input = gpio_sim_direction_input;
	gc->get_direction = gpio_sim_get_direction;
	gc->set_config = gpio_sim_set_config;
	gc->to_irq = gpio_sim_to_irq;
	gc->free = gpio_sim_free;
	gc->can_sleep = true;

	ret = devm_gpiochip_add_data(dev, gc, chip);
	if (ret)
		return ret;

	/* Used by sysfs and configfs callbacks. */
	dev_set_drvdata(&gc->gpiodev->dev, chip);

	return gpio_sim_setup_sysfs(chip);
}

static int gpio_sim_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fwnode_handle *swnode;
	int ret;

	device_for_each_child_node(dev, swnode) {
		ret = gpio_sim_add_bank(swnode, dev);
		if (ret) {
			fwnode_handle_put(swnode);
			return ret;
		}
	}

	return 0;
}

static const struct of_device_id gpio_sim_of_match[] = {
	{ .compatible = "gpio-simulator" },
	{ }
};
MODULE_DEVICE_TABLE(of, gpio_sim_of_match);

static struct platform_driver gpio_sim_driver = {
	.driver = {
		.name = "gpio-sim",
		.of_match_table = gpio_sim_of_match,
	},
	.probe = gpio_sim_probe,
};

struct gpio_sim_device {
	struct config_group group;

	/*
	 * If pdev is NULL, the device is 'pending' (waiting for configuration).
	 * Once the pointer is assigned, the device has been created and the
	 * item is 'live'.
	 */
	struct platform_device *pdev;
	int id;

	/*
	 * Each configfs filesystem operation is protected with the subsystem
	 * mutex. Each separate attribute is protected with the buffer mutex.
	 * This structure however can be modified by callbacks of different
	 * attributes so we need another lock.
	 *
	 * We use this lock for protecting all data structures owned by this
	 * object too.
	 */
	struct mutex lock;

	/*
	 * This is used to synchronously wait for the driver's probe to complete
	 * and notify the user-space about any errors.
	 */
	struct notifier_block bus_notifier;
	struct completion probe_completion;
	bool driver_bound;

	struct gpiod_hog *hogs;

	struct list_head bank_list;
};

/* This is called with dev->lock already taken. */
static int gpio_sim_bus_notifier_call(struct notifier_block *nb,
				      unsigned long action, void *data)
{
	struct gpio_sim_device *simdev = container_of(nb,
						      struct gpio_sim_device,
						      bus_notifier);
	struct device *dev = data;
	char devname[32];

	snprintf(devname, sizeof(devname), "gpio-sim.%u", simdev->id);

	if (strcmp(dev_name(dev), devname) == 0) {
		if (action == BUS_NOTIFY_BOUND_DRIVER)
			simdev->driver_bound = true;
		else if (action == BUS_NOTIFY_DRIVER_NOT_BOUND)
			simdev->driver_bound = false;
		else
			return NOTIFY_DONE;

		complete(&simdev->probe_completion);
		return NOTIFY_OK;
	}

	return NOTIFY_DONE;
}

static struct gpio_sim_device *to_gpio_sim_device(struct config_item *item)
{
	struct config_group *group = to_config_group(item);

	return container_of(group, struct gpio_sim_device, group);
}

struct gpio_sim_bank {
	struct config_group group;

	/*
	 * We could have used the ci_parent field of the config_item but
	 * configfs is stupid and calls the item's release callback after
	 * already having cleared the parent pointer even though the parent
	 * is guaranteed to survive the child...
	 *
	 * So we need to store the pointer to the parent struct here. We can
	 * dereference it anywhere we need with no checks and no locking as
	 * it's guaranteed to survive the children and protected by configfs
	 * locks.
	 *
	 * Same for other structures.
	 */
	struct gpio_sim_device *parent;
	struct list_head siblings;

	char *label;
	unsigned int num_lines;

	struct list_head line_list;

	struct fwnode_handle *swnode;
};

static struct gpio_sim_bank *to_gpio_sim_bank(struct config_item *item)
{
	struct config_group *group = to_config_group(item);

	return container_of(group, struct gpio_sim_bank, group);
}

static bool gpio_sim_bank_has_label(struct gpio_sim_bank *bank)
{
	return bank->label && *bank->label;
}

static struct gpio_sim_device *
gpio_sim_bank_get_device(struct gpio_sim_bank *bank)
{
	return bank->parent;
}

struct gpio_sim_hog;

struct gpio_sim_line {
	struct config_group group;

	struct gpio_sim_bank *parent;
	struct list_head siblings;

	unsigned int offset;
	char *name;

	/* There can only be one hog per line. */
	struct gpio_sim_hog *hog;
};

static struct gpio_sim_line *to_gpio_sim_line(struct config_item *item)
{
	struct config_group *group = to_config_group(item);

	return container_of(group, struct gpio_sim_line, group);
}

static struct gpio_sim_device *
gpio_sim_line_get_device(struct gpio_sim_line *line)
{
	struct gpio_sim_bank *bank = line->parent;

	return gpio_sim_bank_get_device(bank);
}

struct gpio_sim_hog {
	struct config_item item;
	struct gpio_sim_line *parent;

	char *name;
	int dir;
};

static struct gpio_sim_hog *to_gpio_sim_hog(struct config_item *item)
{
	return container_of(item, struct gpio_sim_hog, item);
}

static struct gpio_sim_device *gpio_sim_hog_get_device(struct gpio_sim_hog *hog)
{
	struct gpio_sim_line *line = hog->parent;

	return gpio_sim_line_get_device(line);
}

static bool gpio_sim_device_is_live_unlocked(struct gpio_sim_device *dev)
{
	return !!dev->pdev;
}

static char *gpio_sim_strdup_trimmed(const char *str, size_t count)
{
	char *trimmed;

	trimmed = kstrndup(skip_spaces(str), count, GFP_KERNEL);
	if (!trimmed)
		return NULL;

	return strim(trimmed);
}

static ssize_t gpio_sim_device_config_dev_name_show(struct config_item *item,
						    char *page)
{
	struct gpio_sim_device *dev = to_gpio_sim_device(item);
	struct platform_device *pdev;

	guard(mutex)(&dev->lock);

	pdev = dev->pdev;
	if (pdev)
		return sprintf(page, "%s\n", dev_name(&pdev->dev));

	return sprintf(page, "gpio-sim.%d\n", dev->id);
}

CONFIGFS_ATTR_RO(gpio_sim_device_config_, dev_name);

static ssize_t
gpio_sim_device_config_live_show(struct config_item *item, char *page)
{
	struct gpio_sim_device *dev = to_gpio_sim_device(item);
	bool live;

	scoped_guard(mutex, &dev->lock)
		live = gpio_sim_device_is_live_unlocked(dev);

	return sprintf(page, "%c\n", live ? '1' : '0');
}

static unsigned int gpio_sim_get_line_names_size(struct gpio_sim_bank *bank)
{
	struct gpio_sim_line *line;
	unsigned int size = 0;

	list_for_each_entry(line, &bank->line_list, siblings) {
		if (!line->name || (line->offset >= bank->num_lines))
			continue;

		size = max(size, line->offset + 1);
	}

	return size;
}

static void
gpio_sim_set_line_names(struct gpio_sim_bank *bank, char **line_names)
{
	struct gpio_sim_line *line;

	list_for_each_entry(line, &bank->line_list, siblings) {
		if (!line->name || (line->offset >= bank->num_lines))
			continue;

		line_names[line->offset] = line->name;
	}
}

static void gpio_sim_remove_hogs(struct gpio_sim_device *dev)
{
	struct gpiod_hog *hog;

	if (!dev->hogs)
		return;

	gpiod_remove_hogs(dev->hogs);

	for (hog = dev->hogs; hog->chip_label; hog++) {
		kfree(hog->chip_label);
		kfree(hog->line_name);
	}

	kfree(dev->hogs);
	dev->hogs = NULL;
}

static int gpio_sim_add_hogs(struct gpio_sim_device *dev)
{
	unsigned int num_hogs = 0, idx = 0;
	struct gpio_sim_bank *bank;
	struct gpio_sim_line *line;
	struct gpiod_hog *hog;

	list_for_each_entry(bank, &dev->bank_list, siblings) {
		list_for_each_entry(line, &bank->line_list, siblings) {
			if (line->offset >= bank->num_lines)
				continue;

			if (line->hog)
				num_hogs++;
		}
	}

	if (!num_hogs)
		return 0;

	/* Allocate one more for the sentinel. */
	dev->hogs = kcalloc(num_hogs + 1, sizeof(*dev->hogs), GFP_KERNEL);
	if (!dev->hogs)
		return -ENOMEM;

	list_for_each_entry(bank, &dev->bank_list, siblings) {
		list_for_each_entry(line, &bank->line_list, siblings) {
			if (line->offset >= bank->num_lines)
				continue;

			if (!line->hog)
				continue;

			hog = &dev->hogs[idx++];

			/*
			 * We need to make this string manually because at this
			 * point the device doesn't exist yet and so dev_name()
			 * is not available.
			 */
			if (gpio_sim_bank_has_label(bank))
				hog->chip_label = kstrdup(bank->label,
							  GFP_KERNEL);
			else
				hog->chip_label = kasprintf(GFP_KERNEL,
							"gpio-sim.%u-%pfwP",
							dev->id,
							bank->swnode);
			if (!hog->chip_label) {
				gpio_sim_remove_hogs(dev);
				return -ENOMEM;
			}

			/*
			 * We need to duplicate this because the hog config
			 * item can be removed at any time (and we can't block
			 * it) and gpiolib doesn't make a deep copy of the hog
			 * data.
			 */
			if (line->hog->name) {
				hog->line_name = kstrdup(line->hog->name,
							 GFP_KERNEL);
				if (!hog->line_name) {
					gpio_sim_remove_hogs(dev);
					return -ENOMEM;
				}
			}

			hog->chip_hwnum = line->offset;
			hog->dflags = line->hog->dir;
		}
	}

	gpiod_add_hogs(dev->hogs);

	return 0;
}

static struct fwnode_handle *
gpio_sim_make_bank_swnode(struct gpio_sim_bank *bank,
			  struct fwnode_handle *parent)
{
	struct property_entry properties[GPIO_SIM_PROP_MAX];
	unsigned int prop_idx = 0, line_names_size;
	char **line_names __free(kfree) = NULL;

	memset(properties, 0, sizeof(properties));

	properties[prop_idx++] = PROPERTY_ENTRY_U32("ngpios", bank->num_lines);

	if (gpio_sim_bank_has_label(bank))
		properties[prop_idx++] = PROPERTY_ENTRY_STRING("gpio-sim,label",
							       bank->label);

	line_names_size = gpio_sim_get_line_names_size(bank);
	if (line_names_size) {
		line_names = kcalloc(line_names_size, sizeof(*line_names),
				     GFP_KERNEL);
		if (!line_names)
			return ERR_PTR(-ENOMEM);

		gpio_sim_set_line_names(bank, line_names);

		properties[prop_idx++] = PROPERTY_ENTRY_STRING_ARRAY_LEN(
						"gpio-line-names",
						line_names, line_names_size);
	}

	return fwnode_create_software_node(properties, parent);
}

static void gpio_sim_remove_swnode_recursive(struct fwnode_handle *swnode)
{
	struct fwnode_handle *child;

	fwnode_for_each_child_node(swnode, child)
		fwnode_remove_software_node(child);

	fwnode_remove_software_node(swnode);
}

static bool gpio_sim_bank_labels_non_unique(struct gpio_sim_device *dev)
{
	struct gpio_sim_bank *this, *pos;

	list_for_each_entry(this, &dev->bank_list, siblings) {
		list_for_each_entry(pos, &dev->bank_list, siblings) {
			if (this == pos || (!this->label || !pos->label))
				continue;

			if (strcmp(this->label, pos->label) == 0)
				return true;
		}
	}

	return false;
}

static int gpio_sim_device_activate_unlocked(struct gpio_sim_device *dev)
{
	struct platform_device_info pdevinfo;
	struct fwnode_handle *swnode;
	struct platform_device *pdev;
	struct gpio_sim_bank *bank;
	int ret;

	if (list_empty(&dev->bank_list))
		return -ENODATA;

	/*
	 * Non-unique GPIO device labels are a corner-case we don't support
	 * as it would interfere with machine hogging mechanism and has little
	 * use in real life.
	 */
	if (gpio_sim_bank_labels_non_unique(dev))
		return -EINVAL;

	memset(&pdevinfo, 0, sizeof(pdevinfo));

	swnode = fwnode_create_software_node(NULL, NULL);
	if (IS_ERR(swnode))
		return PTR_ERR(swnode);

	list_for_each_entry(bank, &dev->bank_list, siblings) {
		bank->swnode = gpio_sim_make_bank_swnode(bank, swnode);
		if (IS_ERR(bank->swnode)) {
			ret = PTR_ERR(bank->swnode);
			gpio_sim_remove_swnode_recursive(swnode);
			return ret;
		}
	}

	ret = gpio_sim_add_hogs(dev);
	if (ret) {
		gpio_sim_remove_swnode_recursive(swnode);
		return ret;
	}

	pdevinfo.name = "gpio-sim";
	pdevinfo.fwnode = swnode;
	pdevinfo.id = dev->id;

	reinit_completion(&dev->probe_completion);
	dev->driver_bound = false;
	bus_register_notifier(&platform_bus_type, &dev->bus_notifier);

	pdev = platform_device_register_full(&pdevinfo);
	if (IS_ERR(pdev)) {
		bus_unregister_notifier(&platform_bus_type, &dev->bus_notifier);
		gpio_sim_remove_hogs(dev);
		gpio_sim_remove_swnode_recursive(swnode);
		return PTR_ERR(pdev);
	}

	wait_for_completion(&dev->probe_completion);
	bus_unregister_notifier(&platform_bus_type, &dev->bus_notifier);

	if (!dev->driver_bound) {
		/* Probe failed, check kernel log. */
		platform_device_unregister(pdev);
		gpio_sim_remove_hogs(dev);
		gpio_sim_remove_swnode_recursive(swnode);
		return -ENXIO;
	}

	dev->pdev = pdev;

	return 0;
}

static void gpio_sim_device_deactivate_unlocked(struct gpio_sim_device *dev)
{
	struct fwnode_handle *swnode;

	swnode = dev_fwnode(&dev->pdev->dev);
	platform_device_unregister(dev->pdev);
	gpio_sim_remove_hogs(dev);
	gpio_sim_remove_swnode_recursive(swnode);
	dev->pdev = NULL;
}

static ssize_t
gpio_sim_device_config_live_store(struct config_item *item,
				  const char *page, size_t count)
{
	struct gpio_sim_device *dev = to_gpio_sim_device(item);
	bool live;
	int ret;

	ret = kstrtobool(page, &live);
	if (ret)
		return ret;

	guard(mutex)(&dev->lock);

	if (live == gpio_sim_device_is_live_unlocked(dev))
		ret = -EPERM;
	else if (live)
		ret = gpio_sim_device_activate_unlocked(dev);
	else
		gpio_sim_device_deactivate_unlocked(dev);

	return ret ?: count;
}

CONFIGFS_ATTR(gpio_sim_device_config_, live);

static struct configfs_attribute *gpio_sim_device_config_attrs[] = {
	&gpio_sim_device_config_attr_dev_name,
	&gpio_sim_device_config_attr_live,
	NULL
};

struct gpio_sim_chip_name_ctx {
	struct fwnode_handle *swnode;
	char *page;
};

static int gpio_sim_emit_chip_name(struct device *dev, void *data)
{
	struct gpio_sim_chip_name_ctx *ctx = data;

	/* This would be the sysfs device exported in /sys/class/gpio. */
	if (dev->class)
		return 0;

	if (device_match_fwnode(dev, ctx->swnode))
		return sprintf(ctx->page, "%s\n", dev_name(dev));

	return 0;
}

static ssize_t gpio_sim_bank_config_chip_name_show(struct config_item *item,
						   char *page)
{
	struct gpio_sim_bank *bank = to_gpio_sim_bank(item);
	struct gpio_sim_device *dev = gpio_sim_bank_get_device(bank);
	struct gpio_sim_chip_name_ctx ctx = { bank->swnode, page };

	guard(mutex)(&dev->lock);

	if (gpio_sim_device_is_live_unlocked(dev))
		return device_for_each_child(&dev->pdev->dev, &ctx,
					     gpio_sim_emit_chip_name);

	return sprintf(page, "none\n");
}

CONFIGFS_ATTR_RO(gpio_sim_bank_config_, chip_name);

static ssize_t
gpio_sim_bank_config_label_show(struct config_item *item, char *page)
{
	struct gpio_sim_bank *bank = to_gpio_sim_bank(item);
	struct gpio_sim_device *dev = gpio_sim_bank_get_device(bank);

	guard(mutex)(&dev->lock);

	return sprintf(page, "%s\n", bank->label ?: "");
}

static ssize_t gpio_sim_bank_config_label_store(struct config_item *item,
						const char *page, size_t count)
{
	struct gpio_sim_bank *bank = to_gpio_sim_bank(item);
	struct gpio_sim_device *dev = gpio_sim_bank_get_device(bank);
	char *trimmed;

	guard(mutex)(&dev->lock);

	if (gpio_sim_device_is_live_unlocked(dev))
		return -EBUSY;

	trimmed = gpio_sim_strdup_trimmed(page, count);
	if (!trimmed)
		return -ENOMEM;

	kfree(bank->label);
	bank->label = trimmed;

	return count;
}

CONFIGFS_ATTR(gpio_sim_bank_config_, label);

static ssize_t
gpio_sim_bank_config_num_lines_show(struct config_item *item, char *page)
{
	struct gpio_sim_bank *bank = to_gpio_sim_bank(item);
	struct gpio_sim_device *dev = gpio_sim_bank_get_device(bank);

	guard(mutex)(&dev->lock);

	return sprintf(page, "%u\n", bank->num_lines);
}

static ssize_t
gpio_sim_bank_config_num_lines_store(struct config_item *item,
				     const char *page, size_t count)
{
	struct gpio_sim_bank *bank = to_gpio_sim_bank(item);
	struct gpio_sim_device *dev = gpio_sim_bank_get_device(bank);
	unsigned int num_lines;
	int ret;

	ret = kstrtouint(page, 0, &num_lines);
	if (ret)
		return ret;

	if (num_lines == 0)
		return -EINVAL;

	guard(mutex)(&dev->lock);

	if (gpio_sim_device_is_live_unlocked(dev))
		return -EBUSY;

	bank->num_lines = num_lines;

	return count;
}

CONFIGFS_ATTR(gpio_sim_bank_config_, num_lines);

static struct configfs_attribute *gpio_sim_bank_config_attrs[] = {
	&gpio_sim_bank_config_attr_chip_name,
	&gpio_sim_bank_config_attr_label,
	&gpio_sim_bank_config_attr_num_lines,
	NULL
};

static ssize_t
gpio_sim_line_config_name_show(struct config_item *item, char *page)
{
	struct gpio_sim_line *line = to_gpio_sim_line(item);
	struct gpio_sim_device *dev = gpio_sim_line_get_device(line);

	guard(mutex)(&dev->lock);

	return sprintf(page, "%s\n", line->name ?: "");
}

static ssize_t gpio_sim_line_config_name_store(struct config_item *item,
					       const char *page, size_t count)
{
	struct gpio_sim_line *line = to_gpio_sim_line(item);
	struct gpio_sim_device *dev = gpio_sim_line_get_device(line);
	char *trimmed;

	guard(mutex)(&dev->lock);

	if (gpio_sim_device_is_live_unlocked(dev))
		return -EBUSY;

	trimmed = gpio_sim_strdup_trimmed(page, count);
	if (!trimmed)
		return -ENOMEM;

	kfree(line->name);
	line->name = trimmed;

	return count;
}

CONFIGFS_ATTR(gpio_sim_line_config_, name);

static struct configfs_attribute *gpio_sim_line_config_attrs[] = {
	&gpio_sim_line_config_attr_name,
	NULL
};

static ssize_t gpio_sim_hog_config_name_show(struct config_item *item,
					     char *page)
{
	struct gpio_sim_hog *hog = to_gpio_sim_hog(item);
	struct gpio_sim_device *dev = gpio_sim_hog_get_device(hog);

	guard(mutex)(&dev->lock);

	return sprintf(page, "%s\n", hog->name ?: "");
}

static ssize_t gpio_sim_hog_config_name_store(struct config_item *item,
					      const char *page, size_t count)
{
	struct gpio_sim_hog *hog = to_gpio_sim_hog(item);
	struct gpio_sim_device *dev = gpio_sim_hog_get_device(hog);
	char *trimmed;

	guard(mutex)(&dev->lock);

	if (gpio_sim_device_is_live_unlocked(dev))
		return -EBUSY;

	trimmed = gpio_sim_strdup_trimmed(page, count);
	if (!trimmed)
		return -ENOMEM;

	kfree(hog->name);
	hog->name = trimmed;

	return count;
}

CONFIGFS_ATTR(gpio_sim_hog_config_, name);

static ssize_t gpio_sim_hog_config_direction_show(struct config_item *item,
						  char *page)
{
	struct gpio_sim_hog *hog = to_gpio_sim_hog(item);
	struct gpio_sim_device *dev = gpio_sim_hog_get_device(hog);
	char *repr;
	int dir;

	scoped_guard(mutex, &dev->lock)
		dir = hog->dir;

	switch (dir) {
	case GPIOD_IN:
		repr = "input";
		break;
	case GPIOD_OUT_HIGH:
		repr = "output-high";
		break;
	case GPIOD_OUT_LOW:
		repr = "output-low";
		break;
	default:
		/* This would be a programmer bug. */
		WARN(1, "Unexpected hog direction value: %d", dir);
		return -EINVAL;
	}

	return sprintf(page, "%s\n", repr);
}

static ssize_t
gpio_sim_hog_config_direction_store(struct config_item *item,
				    const char *page, size_t count)
{
	struct gpio_sim_hog *hog = to_gpio_sim_hog(item);
	struct gpio_sim_device *dev = gpio_sim_hog_get_device(hog);
	int dir;

	guard(mutex)(&dev->lock);

	if (gpio_sim_device_is_live_unlocked(dev))
		return -EBUSY;

	if (sysfs_streq(page, "input"))
		dir = GPIOD_IN;
	else if (sysfs_streq(page, "output-high"))
		dir = GPIOD_OUT_HIGH;
	else if (sysfs_streq(page, "output-low"))
		dir = GPIOD_OUT_LOW;
	else
		return -EINVAL;

	hog->dir = dir;

	return count;
}

CONFIGFS_ATTR(gpio_sim_hog_config_, direction);

static struct configfs_attribute *gpio_sim_hog_config_attrs[] = {
	&gpio_sim_hog_config_attr_name,
	&gpio_sim_hog_config_attr_direction,
	NULL
};

static void gpio_sim_hog_config_item_release(struct config_item *item)
{
	struct gpio_sim_hog *hog = to_gpio_sim_hog(item);
	struct gpio_sim_line *line = hog->parent;
	struct gpio_sim_device *dev = gpio_sim_hog_get_device(hog);

	scoped_guard(mutex, &dev->lock)
		line->hog = NULL;

	kfree(hog->name);
	kfree(hog);
}

static struct configfs_item_operations gpio_sim_hog_config_item_ops = {
	.release	= gpio_sim_hog_config_item_release,
};

static const struct config_item_type gpio_sim_hog_config_type = {
	.ct_item_ops	= &gpio_sim_hog_config_item_ops,
	.ct_attrs	= gpio_sim_hog_config_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_item *
gpio_sim_line_config_make_hog_item(struct config_group *group, const char *name)
{
	struct gpio_sim_line *line = to_gpio_sim_line(&group->cg_item);
	struct gpio_sim_device *dev = gpio_sim_line_get_device(line);
	struct gpio_sim_hog *hog;

	if (strcmp(name, "hog") != 0)
		return ERR_PTR(-EINVAL);

	guard(mutex)(&dev->lock);

	hog = kzalloc(sizeof(*hog), GFP_KERNEL);
	if (!hog)
		return ERR_PTR(-ENOMEM);

	config_item_init_type_name(&hog->item, name,
				   &gpio_sim_hog_config_type);

	hog->dir = GPIOD_IN;
	hog->name = NULL;
	hog->parent = line;
	line->hog = hog;

	return &hog->item;
}

static void gpio_sim_line_config_group_release(struct config_item *item)
{
	struct gpio_sim_line *line = to_gpio_sim_line(item);
	struct gpio_sim_device *dev = gpio_sim_line_get_device(line);

	scoped_guard(mutex, &dev->lock)
		list_del(&line->siblings);

	kfree(line->name);
	kfree(line);
}

static struct configfs_item_operations gpio_sim_line_config_item_ops = {
	.release	= gpio_sim_line_config_group_release,
};

static struct configfs_group_operations gpio_sim_line_config_group_ops = {
	.make_item	= gpio_sim_line_config_make_hog_item,
};

static const struct config_item_type gpio_sim_line_config_type = {
	.ct_item_ops	= &gpio_sim_line_config_item_ops,
	.ct_group_ops	= &gpio_sim_line_config_group_ops,
	.ct_attrs	= gpio_sim_line_config_attrs,
	.ct_owner       = THIS_MODULE,
};

static struct config_group *
gpio_sim_bank_config_make_line_group(struct config_group *group,
				     const char *name)
{
	struct gpio_sim_bank *bank = to_gpio_sim_bank(&group->cg_item);
	struct gpio_sim_device *dev = gpio_sim_bank_get_device(bank);
	struct gpio_sim_line *line;
	unsigned int offset;
	int ret, nchar;

	ret = sscanf(name, "line%u%n", &offset, &nchar);
	if (ret != 1 || nchar != strlen(name))
		return ERR_PTR(-EINVAL);

	guard(mutex)(&dev->lock);

	if (gpio_sim_device_is_live_unlocked(dev))
		return ERR_PTR(-EBUSY);

	line = kzalloc(sizeof(*line), GFP_KERNEL);
	if (!line)
		return ERR_PTR(-ENOMEM);

	config_group_init_type_name(&line->group, name,
				    &gpio_sim_line_config_type);

	line->parent = bank;
	line->offset = offset;
	list_add_tail(&line->siblings, &bank->line_list);

	return &line->group;
}

static void gpio_sim_bank_config_group_release(struct config_item *item)
{
	struct gpio_sim_bank *bank = to_gpio_sim_bank(item);
	struct gpio_sim_device *dev = gpio_sim_bank_get_device(bank);

	scoped_guard(mutex, &dev->lock)
		list_del(&bank->siblings);

	kfree(bank->label);
	kfree(bank);
}

static struct configfs_item_operations gpio_sim_bank_config_item_ops = {
	.release	= gpio_sim_bank_config_group_release,
};

static struct configfs_group_operations gpio_sim_bank_config_group_ops = {
	.make_group	= gpio_sim_bank_config_make_line_group,
};

static const struct config_item_type gpio_sim_bank_config_group_type = {
	.ct_item_ops	= &gpio_sim_bank_config_item_ops,
	.ct_group_ops	= &gpio_sim_bank_config_group_ops,
	.ct_attrs	= gpio_sim_bank_config_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_group *
gpio_sim_device_config_make_bank_group(struct config_group *group,
				       const char *name)
{
	struct gpio_sim_device *dev = to_gpio_sim_device(&group->cg_item);
	struct gpio_sim_bank *bank;

	guard(mutex)(&dev->lock);

	if (gpio_sim_device_is_live_unlocked(dev))
		return ERR_PTR(-EBUSY);

	bank = kzalloc(sizeof(*bank), GFP_KERNEL);
	if (!bank)
		return ERR_PTR(-ENOMEM);

	config_group_init_type_name(&bank->group, name,
				    &gpio_sim_bank_config_group_type);
	bank->num_lines = 1;
	bank->parent = dev;
	INIT_LIST_HEAD(&bank->line_list);
	list_add_tail(&bank->siblings, &dev->bank_list);

	return &bank->group;
}

static void gpio_sim_device_config_group_release(struct config_item *item)
{
	struct gpio_sim_device *dev = to_gpio_sim_device(item);

	scoped_guard(mutex, &dev->lock) {
		if (gpio_sim_device_is_live_unlocked(dev))
			gpio_sim_device_deactivate_unlocked(dev);
	}

	mutex_destroy(&dev->lock);
	ida_free(&gpio_sim_ida, dev->id);
	kfree(dev);
}

static struct configfs_item_operations gpio_sim_device_config_item_ops = {
	.release	= gpio_sim_device_config_group_release,
};

static struct configfs_group_operations gpio_sim_device_config_group_ops = {
	.make_group	= gpio_sim_device_config_make_bank_group,
};

static const struct config_item_type gpio_sim_device_config_group_type = {
	.ct_item_ops	= &gpio_sim_device_config_item_ops,
	.ct_group_ops	= &gpio_sim_device_config_group_ops,
	.ct_attrs	= gpio_sim_device_config_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_group *
gpio_sim_config_make_device_group(struct config_group *group, const char *name)
{
	struct gpio_sim_device *dev __free(kfree) = NULL;
	int id;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	id = ida_alloc(&gpio_sim_ida, GFP_KERNEL);
	if (id < 0)
		return ERR_PTR(id);

	config_group_init_type_name(&dev->group, name,
				    &gpio_sim_device_config_group_type);
	dev->id = id;
	mutex_init(&dev->lock);
	INIT_LIST_HEAD(&dev->bank_list);

	dev->bus_notifier.notifier_call = gpio_sim_bus_notifier_call;
	init_completion(&dev->probe_completion);

	return &no_free_ptr(dev)->group;
}

static struct configfs_group_operations gpio_sim_config_group_ops = {
	.make_group	= gpio_sim_config_make_device_group,
};

static const struct config_item_type gpio_sim_config_type = {
	.ct_group_ops	= &gpio_sim_config_group_ops,
	.ct_owner	= THIS_MODULE,
};

static struct configfs_subsystem gpio_sim_config_subsys = {
	.su_group = {
		.cg_item = {
			.ci_namebuf	= "gpio-sim",
			.ci_type	= &gpio_sim_config_type,
		},
	},
};

static int __init gpio_sim_init(void)
{
	int ret;

	ret = platform_driver_register(&gpio_sim_driver);
	if (ret) {
		pr_err("Error %d while registering the platform driver\n", ret);
		return ret;
	}

	config_group_init(&gpio_sim_config_subsys.su_group);
	mutex_init(&gpio_sim_config_subsys.su_mutex);
	ret = configfs_register_subsystem(&gpio_sim_config_subsys);
	if (ret) {
		pr_err("Error %d while registering the configfs subsystem %s\n",
		       ret, gpio_sim_config_subsys.su_group.cg_item.ci_namebuf);
		mutex_destroy(&gpio_sim_config_subsys.su_mutex);
		platform_driver_unregister(&gpio_sim_driver);
		return ret;
	}

	return 0;
}
module_init(gpio_sim_init);

static void __exit gpio_sim_exit(void)
{
	configfs_unregister_subsystem(&gpio_sim_config_subsys);
	mutex_destroy(&gpio_sim_config_subsys.su_mutex);
	platform_driver_unregister(&gpio_sim_driver);
}
module_exit(gpio_sim_exit);

MODULE_AUTHOR("Bartosz Golaszewski <brgl@bgdev.pl");
MODULE_DESCRIPTION("GPIO Simulator Module");
MODULE_LICENSE("GPL");
