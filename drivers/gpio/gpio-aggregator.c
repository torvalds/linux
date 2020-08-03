// SPDX-License-Identifier: GPL-2.0-only
//
// GPIO Aggregator
//
// Copyright (C) 2019-2020 Glider bv

#define DRV_NAME       "gpio-aggregator"
#define pr_fmt(fmt)	DRV_NAME ": " fmt

#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/ctype.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/machine.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/overflow.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/string.h>


/*
 * GPIO Aggregator sysfs interface
 */

struct gpio_aggregator {
	struct gpiod_lookup_table *lookups;
	struct platform_device *pdev;
	char args[];
};

static DEFINE_MUTEX(gpio_aggregator_lock);	/* protects idr */
static DEFINE_IDR(gpio_aggregator_idr);

static char *get_arg(char **args)
{
	char *start = *args, *end;

	start = skip_spaces(start);
	if (!*start)
		return NULL;

	if (*start == '"') {
		/* Quoted arg */
		end = strchr(++start, '"');
		if (!end)
			return ERR_PTR(-EINVAL);
	} else {
		/* Unquoted arg */
		for (end = start; *end && !isspace(*end); end++) ;
	}

	if (*end)
		*end++ = '\0';

	*args = end;
	return start;
}

static bool isrange(const char *s)
{
	size_t n;

	if (IS_ERR_OR_NULL(s))
		return false;

	while (1) {
		n = strspn(s, "0123456789");
		if (!n)
			return false;

		s += n;

		switch (*s++) {
		case '\0':
			return true;

		case '-':
		case ',':
			break;

		default:
			return false;
		}
	}
}

static int aggr_add_gpio(struct gpio_aggregator *aggr, const char *key,
			 int hwnum, unsigned int *n)
{
	struct gpiod_lookup_table *lookups;

	lookups = krealloc(aggr->lookups, struct_size(lookups, table, *n + 2),
			   GFP_KERNEL);
	if (!lookups)
		return -ENOMEM;

	lookups->table[*n] =
		(struct gpiod_lookup)GPIO_LOOKUP_IDX(key, hwnum, NULL, *n, 0);

	(*n)++;
	memset(&lookups->table[*n], 0, sizeof(lookups->table[*n]));

	aggr->lookups = lookups;
	return 0;
}

static int aggr_parse(struct gpio_aggregator *aggr)
{
	unsigned int first_index, last_index, i, n = 0;
	char *name, *offsets, *first, *last, *next;
	char *args = aggr->args;
	int error;

	for (name = get_arg(&args), offsets = get_arg(&args); name;
	     offsets = get_arg(&args)) {
		if (IS_ERR(name)) {
			pr_err("Cannot get GPIO specifier: %pe\n", name);
			return PTR_ERR(name);
		}

		if (!isrange(offsets)) {
			/* Named GPIO line */
			error = aggr_add_gpio(aggr, name, U16_MAX, &n);
			if (error)
				return error;

			name = offsets;
			continue;
		}

		/* GPIO chip + offset(s) */
		for (first = offsets; *first; first = next) {
			next = strchrnul(first, ',');
			if (*next)
				*next++ = '\0';

			last = strchr(first, '-');
			if (last)
				*last++ = '\0';

			if (kstrtouint(first, 10, &first_index)) {
				pr_err("Cannot parse GPIO index %s\n", first);
				return -EINVAL;
			}

			if (!last) {
				last_index = first_index;
			} else if (kstrtouint(last, 10, &last_index)) {
				pr_err("Cannot parse GPIO index %s\n", last);
				return -EINVAL;
			}

			for (i = first_index; i <= last_index; i++) {
				error = aggr_add_gpio(aggr, name, i, &n);
				if (error)
					return error;
			}
		}

		name = get_arg(&args);
	}

	if (!n) {
		pr_err("No GPIOs specified\n");
		return -EINVAL;
	}

	return 0;
}

static ssize_t new_device_store(struct device_driver *driver, const char *buf,
				size_t count)
{
	struct gpio_aggregator *aggr;
	struct platform_device *pdev;
	int res, id;

	/* kernfs guarantees string termination, so count + 1 is safe */
	aggr = kzalloc(sizeof(*aggr) + count + 1, GFP_KERNEL);
	if (!aggr)
		return -ENOMEM;

	memcpy(aggr->args, buf, count + 1);

	aggr->lookups = kzalloc(struct_size(aggr->lookups, table, 1),
				GFP_KERNEL);
	if (!aggr->lookups) {
		res = -ENOMEM;
		goto free_ga;
	}

	mutex_lock(&gpio_aggregator_lock);
	id = idr_alloc(&gpio_aggregator_idr, aggr, 0, 0, GFP_KERNEL);
	mutex_unlock(&gpio_aggregator_lock);

	if (id < 0) {
		res = id;
		goto free_table;
	}

	aggr->lookups->dev_id = kasprintf(GFP_KERNEL, "%s.%d", DRV_NAME, id);
	if (!aggr->lookups->dev_id) {
		res = -ENOMEM;
		goto remove_idr;
	}

	res = aggr_parse(aggr);
	if (res)
		goto free_dev_id;

	gpiod_add_lookup_table(aggr->lookups);

	pdev = platform_device_register_simple(DRV_NAME, id, NULL, 0);
	if (IS_ERR(pdev)) {
		res = PTR_ERR(pdev);
		goto remove_table;
	}

	aggr->pdev = pdev;
	return count;

remove_table:
	gpiod_remove_lookup_table(aggr->lookups);
free_dev_id:
	kfree(aggr->lookups->dev_id);
remove_idr:
	mutex_lock(&gpio_aggregator_lock);
	idr_remove(&gpio_aggregator_idr, id);
	mutex_unlock(&gpio_aggregator_lock);
free_table:
	kfree(aggr->lookups);
free_ga:
	kfree(aggr);
	return res;
}

static DRIVER_ATTR_WO(new_device);

static void gpio_aggregator_free(struct gpio_aggregator *aggr)
{
	platform_device_unregister(aggr->pdev);
	gpiod_remove_lookup_table(aggr->lookups);
	kfree(aggr->lookups->dev_id);
	kfree(aggr->lookups);
	kfree(aggr);
}

static ssize_t delete_device_store(struct device_driver *driver,
				   const char *buf, size_t count)
{
	struct gpio_aggregator *aggr;
	unsigned int id;
	int error;

	if (!str_has_prefix(buf, DRV_NAME "."))
		return -EINVAL;

	error = kstrtouint(buf + strlen(DRV_NAME "."), 10, &id);
	if (error)
		return error;

	mutex_lock(&gpio_aggregator_lock);
	aggr = idr_remove(&gpio_aggregator_idr, id);
	mutex_unlock(&gpio_aggregator_lock);
	if (!aggr)
		return -ENOENT;

	gpio_aggregator_free(aggr);
	return count;
}
static DRIVER_ATTR_WO(delete_device);

static struct attribute *gpio_aggregator_attrs[] = {
	&driver_attr_new_device.attr,
	&driver_attr_delete_device.attr,
	NULL,
};
ATTRIBUTE_GROUPS(gpio_aggregator);

static int __exit gpio_aggregator_idr_remove(int id, void *p, void *data)
{
	gpio_aggregator_free(p);
	return 0;
}

static void __exit gpio_aggregator_remove_all(void)
{
	mutex_lock(&gpio_aggregator_lock);
	idr_for_each(&gpio_aggregator_idr, gpio_aggregator_idr_remove, NULL);
	idr_destroy(&gpio_aggregator_idr);
	mutex_unlock(&gpio_aggregator_lock);
}


/*
 *  GPIO Forwarder
 */

struct gpiochip_fwd {
	struct gpio_chip chip;
	struct gpio_desc **descs;
	union {
		struct mutex mlock;	/* protects tmp[] if can_sleep */
		spinlock_t slock;	/* protects tmp[] if !can_sleep */
	};
	unsigned long tmp[];		/* values and descs for multiple ops */
};

static int gpio_fwd_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct gpiochip_fwd *fwd = gpiochip_get_data(chip);

	return gpiod_get_direction(fwd->descs[offset]);
}

static int gpio_fwd_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	struct gpiochip_fwd *fwd = gpiochip_get_data(chip);

	return gpiod_direction_input(fwd->descs[offset]);
}

static int gpio_fwd_direction_output(struct gpio_chip *chip,
				     unsigned int offset, int value)
{
	struct gpiochip_fwd *fwd = gpiochip_get_data(chip);

	return gpiod_direction_output(fwd->descs[offset], value);
}

static int gpio_fwd_get(struct gpio_chip *chip, unsigned int offset)
{
	struct gpiochip_fwd *fwd = gpiochip_get_data(chip);

	return gpiod_get_value(fwd->descs[offset]);
}

static int gpio_fwd_get_multiple(struct gpio_chip *chip, unsigned long *mask,
				 unsigned long *bits)
{
	struct gpiochip_fwd *fwd = gpiochip_get_data(chip);
	unsigned long *values, flags = 0;
	struct gpio_desc **descs;
	unsigned int i, j = 0;
	int error;

	if (chip->can_sleep)
		mutex_lock(&fwd->mlock);
	else
		spin_lock_irqsave(&fwd->slock, flags);

	/* Both values bitmap and desc pointers are stored in tmp[] */
	values = &fwd->tmp[0];
	descs = (void *)&fwd->tmp[BITS_TO_LONGS(fwd->chip.ngpio)];

	bitmap_clear(values, 0, fwd->chip.ngpio);
	for_each_set_bit(i, mask, fwd->chip.ngpio)
		descs[j++] = fwd->descs[i];

	error = gpiod_get_array_value(j, descs, NULL, values);
	if (!error) {
		j = 0;
		for_each_set_bit(i, mask, fwd->chip.ngpio)
			__assign_bit(i, bits, test_bit(j++, values));
	}

	if (chip->can_sleep)
		mutex_unlock(&fwd->mlock);
	else
		spin_unlock_irqrestore(&fwd->slock, flags);

	return error;
}

static void gpio_fwd_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct gpiochip_fwd *fwd = gpiochip_get_data(chip);

	gpiod_set_value(fwd->descs[offset], value);
}

static void gpio_fwd_set_multiple(struct gpio_chip *chip, unsigned long *mask,
				  unsigned long *bits)
{
	struct gpiochip_fwd *fwd = gpiochip_get_data(chip);
	unsigned long *values, flags = 0;
	struct gpio_desc **descs;
	unsigned int i, j = 0;

	if (chip->can_sleep)
		mutex_lock(&fwd->mlock);
	else
		spin_lock_irqsave(&fwd->slock, flags);

	/* Both values bitmap and desc pointers are stored in tmp[] */
	values = &fwd->tmp[0];
	descs = (void *)&fwd->tmp[BITS_TO_LONGS(fwd->chip.ngpio)];

	for_each_set_bit(i, mask, fwd->chip.ngpio) {
		__assign_bit(j, values, test_bit(i, bits));
		descs[j++] = fwd->descs[i];
	}

	gpiod_set_array_value(j, descs, NULL, values);

	if (chip->can_sleep)
		mutex_unlock(&fwd->mlock);
	else
		spin_unlock_irqrestore(&fwd->slock, flags);
}

static int gpio_fwd_set_config(struct gpio_chip *chip, unsigned int offset,
			       unsigned long config)
{
	struct gpiochip_fwd *fwd = gpiochip_get_data(chip);

	return gpiod_set_config(fwd->descs[offset], config);
}

/**
 * gpiochip_fwd_create() - Create a new GPIO forwarder
 * @dev: Parent device pointer
 * @ngpios: Number of GPIOs in the forwarder.
 * @descs: Array containing the GPIO descriptors to forward to.
 *         This array must contain @ngpios entries, and must not be deallocated
 *         before the forwarder has been destroyed again.
 *
 * This function creates a new gpiochip, which forwards all GPIO operations to
 * the passed GPIO descriptors.
 *
 * Return: An opaque object pointer, or an ERR_PTR()-encoded negative error
 *         code on failure.
 */
static struct gpiochip_fwd *gpiochip_fwd_create(struct device *dev,
						unsigned int ngpios,
						struct gpio_desc *descs[])
{
	const char *label = dev_name(dev);
	struct gpiochip_fwd *fwd;
	struct gpio_chip *chip;
	unsigned int i;
	int error;

	fwd = devm_kzalloc(dev, struct_size(fwd, tmp,
			   BITS_TO_LONGS(ngpios) + ngpios), GFP_KERNEL);
	if (!fwd)
		return ERR_PTR(-ENOMEM);

	chip = &fwd->chip;

	/*
	 * If any of the GPIO lines are sleeping, then the entire forwarder
	 * will be sleeping.
	 * If any of the chips support .set_config(), then the forwarder will
	 * support setting configs.
	 */
	for (i = 0; i < ngpios; i++) {
		struct gpio_chip *parent = gpiod_to_chip(descs[i]);

		dev_dbg(dev, "%u => gpio-%d\n", i, desc_to_gpio(descs[i]));

		if (gpiod_cansleep(descs[i]))
			chip->can_sleep = true;
		if (parent && parent->set_config)
			chip->set_config = gpio_fwd_set_config;
	}

	chip->label = label;
	chip->parent = dev;
	chip->owner = THIS_MODULE;
	chip->get_direction = gpio_fwd_get_direction;
	chip->direction_input = gpio_fwd_direction_input;
	chip->direction_output = gpio_fwd_direction_output;
	chip->get = gpio_fwd_get;
	chip->get_multiple = gpio_fwd_get_multiple;
	chip->set = gpio_fwd_set;
	chip->set_multiple = gpio_fwd_set_multiple;
	chip->base = -1;
	chip->ngpio = ngpios;
	fwd->descs = descs;

	if (chip->can_sleep)
		mutex_init(&fwd->mlock);
	else
		spin_lock_init(&fwd->slock);

	error = devm_gpiochip_add_data(dev, chip, fwd);
	if (error)
		return ERR_PTR(error);

	return fwd;
}


/*
 *  GPIO Aggregator platform device
 */

static int gpio_aggregator_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gpio_desc **descs;
	struct gpiochip_fwd *fwd;
	int i, n;

	n = gpiod_count(dev, NULL);
	if (n < 0)
		return n;

	descs = devm_kmalloc_array(dev, n, sizeof(*descs), GFP_KERNEL);
	if (!descs)
		return -ENOMEM;

	for (i = 0; i < n; i++) {
		descs[i] = devm_gpiod_get_index(dev, NULL, i, GPIOD_ASIS);
		if (IS_ERR(descs[i]))
			return PTR_ERR(descs[i]);
	}

	fwd = gpiochip_fwd_create(dev, n, descs);
	if (IS_ERR(fwd))
		return PTR_ERR(fwd);

	platform_set_drvdata(pdev, fwd);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id gpio_aggregator_dt_ids[] = {
	/*
	 * Add GPIO-operated devices controlled from userspace below,
	 * or use "driver_override" in sysfs
	 */
	{},
};
MODULE_DEVICE_TABLE(of, gpio_aggregator_dt_ids);
#endif

static struct platform_driver gpio_aggregator_driver = {
	.probe = gpio_aggregator_probe,
	.driver = {
		.name = DRV_NAME,
		.groups = gpio_aggregator_groups,
		.of_match_table = of_match_ptr(gpio_aggregator_dt_ids),
	},
};

static int __init gpio_aggregator_init(void)
{
	return platform_driver_register(&gpio_aggregator_driver);
}
module_init(gpio_aggregator_init);

static void __exit gpio_aggregator_exit(void)
{
	gpio_aggregator_remove_all();
	platform_driver_unregister(&gpio_aggregator_driver);
}
module_exit(gpio_aggregator_exit);

MODULE_AUTHOR("Geert Uytterhoeven <geert+renesas@glider.be>");
MODULE_DESCRIPTION("GPIO Aggregator");
MODULE_LICENSE("GPL v2");
