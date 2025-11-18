// SPDX-License-Identifier: GPL-2.0-only
//
// GPIO Aggregator
//
// Copyright (C) 2019-2020 Glider bv

#define DRV_NAME       "gpio-aggregator"
#define pr_fmt(fmt)	DRV_NAME ": " fmt

#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/configfs.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/overflow.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/forwarder.h>
#include <linux/gpio/machine.h>

#include "dev-sync-probe.h"

#define AGGREGATOR_MAX_GPIOS 512
#define AGGREGATOR_LEGACY_PREFIX "_sysfs"

/*
 * GPIO Aggregator sysfs interface
 */

struct gpio_aggregator {
	struct dev_sync_probe_data probe_data;
	struct config_group group;
	struct gpiod_lookup_table *lookups;
	struct mutex lock;
	int id;

	/* List of gpio_aggregator_line. Always added in order */
	struct list_head list_head;

	/* used by legacy sysfs interface only */
	bool init_via_sysfs;
	char args[];
};

struct gpio_aggregator_line {
	struct config_group group;
	struct gpio_aggregator *parent;
	struct list_head entry;

	/* Line index within the aggregator device */
	unsigned int idx;

	/* Custom name for the virtual line */
	const char *name;
	/* GPIO chip label or line name */
	const char *key;
	/* Can be negative to indicate lookup by line name */
	int offset;

	enum gpio_lookup_flags flags;
};

struct gpio_aggregator_pdev_meta {
	bool init_via_sysfs;
};

static DEFINE_MUTEX(gpio_aggregator_lock);	/* protects idr */
static DEFINE_IDR(gpio_aggregator_idr);

static int gpio_aggregator_alloc(struct gpio_aggregator **aggr, size_t arg_size)
{
	int ret;

	struct gpio_aggregator *new __free(kfree) = kzalloc(
					sizeof(*new) + arg_size, GFP_KERNEL);
	if (!new)
		return -ENOMEM;

	scoped_guard(mutex, &gpio_aggregator_lock)
		ret = idr_alloc(&gpio_aggregator_idr, new, 0, 0, GFP_KERNEL);

	if (ret < 0)
		return ret;

	new->id = ret;
	INIT_LIST_HEAD(&new->list_head);
	mutex_init(&new->lock);
	*aggr = no_free_ptr(new);
	return 0;
}

static void gpio_aggregator_free(struct gpio_aggregator *aggr)
{
	scoped_guard(mutex, &gpio_aggregator_lock)
		idr_remove(&gpio_aggregator_idr, aggr->id);

	mutex_destroy(&aggr->lock);
	kfree(aggr);
}

static int gpio_aggregator_add_gpio(struct gpio_aggregator *aggr,
				    const char *key, int hwnum, unsigned int *n)
{
	struct gpiod_lookup_table *lookups;

	lookups = krealloc(aggr->lookups, struct_size(lookups, table, *n + 2),
			   GFP_KERNEL);
	if (!lookups)
		return -ENOMEM;

	lookups->table[*n] = GPIO_LOOKUP_IDX(key, hwnum, NULL, *n, 0);

	(*n)++;
	memset(&lookups->table[*n], 0, sizeof(lookups->table[*n]));

	aggr->lookups = lookups;
	return 0;
}

static bool gpio_aggregator_is_active(struct gpio_aggregator *aggr)
{
	lockdep_assert_held(&aggr->lock);

	return aggr->probe_data.pdev && platform_get_drvdata(aggr->probe_data.pdev);
}

/* Only aggregators created via legacy sysfs can be "activating". */
static bool gpio_aggregator_is_activating(struct gpio_aggregator *aggr)
{
	lockdep_assert_held(&aggr->lock);

	return aggr->probe_data.pdev && !platform_get_drvdata(aggr->probe_data.pdev);
}

static size_t gpio_aggregator_count_lines(struct gpio_aggregator *aggr)
{
	lockdep_assert_held(&aggr->lock);

	return list_count_nodes(&aggr->list_head);
}

static struct gpio_aggregator_line *
gpio_aggregator_line_alloc(struct gpio_aggregator *parent, unsigned int idx,
			   char *key, int offset)
{
	struct gpio_aggregator_line *line;

	line = kzalloc(sizeof(*line), GFP_KERNEL);
	if (!line)
		return ERR_PTR(-ENOMEM);

	if (key) {
		line->key = kstrdup(key, GFP_KERNEL);
		if (!line->key) {
			kfree(line);
			return ERR_PTR(-ENOMEM);
		}
	}

	line->flags = GPIO_LOOKUP_FLAGS_DEFAULT;
	line->parent = parent;
	line->idx = idx;
	line->offset = offset;
	INIT_LIST_HEAD(&line->entry);

	return line;
}

static void gpio_aggregator_line_add(struct gpio_aggregator *aggr,
				     struct gpio_aggregator_line *line)
{
	struct gpio_aggregator_line *tmp;

	lockdep_assert_held(&aggr->lock);

	list_for_each_entry(tmp, &aggr->list_head, entry) {
		if (tmp->idx > line->idx) {
			list_add_tail(&line->entry, &tmp->entry);
			return;
		}
	}
	list_add_tail(&line->entry, &aggr->list_head);
}

static void gpio_aggregator_line_del(struct gpio_aggregator *aggr,
				     struct gpio_aggregator_line *line)
{
	lockdep_assert_held(&aggr->lock);

	list_del(&line->entry);
}

static void gpio_aggregator_free_lines(struct gpio_aggregator *aggr)
{
	struct gpio_aggregator_line *line, *tmp;

	list_for_each_entry_safe(line, tmp, &aggr->list_head, entry) {
		configfs_unregister_group(&line->group);
		/*
		 * Normally, we acquire aggr->lock within the configfs
		 * callback. However, in the legacy sysfs interface case,
		 * calling configfs_(un)register_group while holding
		 * aggr->lock could cause a deadlock. Fortunately, this is
		 * unnecessary because the new_device/delete_device path
		 * and the module unload path are mutually exclusive,
		 * thanks to an explicit try_module_get. That's why this
		 * minimal scoped_guard suffices.
		 */
		scoped_guard(mutex, &aggr->lock)
			gpio_aggregator_line_del(aggr, line);
		kfree(line->key);
		kfree(line->name);
		kfree(line);
	}
}


/*
 *  GPIO Forwarder
 */

struct gpiochip_fwd_timing {
	u32 ramp_up_us;
	u32 ramp_down_us;
};

struct gpiochip_fwd {
	struct gpio_chip chip;
	struct gpio_desc **descs;
	union {
		struct mutex mlock;	/* protects tmp[] if can_sleep */
		spinlock_t slock;	/* protects tmp[] if !can_sleep */
	};
	struct gpiochip_fwd_timing *delay_timings;
	void *data;
	unsigned long *valid_mask;
	unsigned long tmp[];		/* values and descs for multiple ops */
};

#define fwd_tmp_values(fwd)	(&(fwd)->tmp[0])
#define fwd_tmp_descs(fwd)	((void *)&(fwd)->tmp[BITS_TO_LONGS((fwd)->chip.ngpio)])

#define fwd_tmp_size(ngpios)	(BITS_TO_LONGS((ngpios)) + (ngpios))

static int gpio_fwd_request(struct gpio_chip *chip, unsigned int offset)
{
	struct gpiochip_fwd *fwd = gpiochip_get_data(chip);

	return test_bit(offset, fwd->valid_mask) ? 0 : -ENODEV;
}

static int gpio_fwd_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct gpiochip_fwd *fwd = gpiochip_get_data(chip);

	/*
	 * get_direction() is called during gpiochip registration, return
	 * -ENODEV if there is no GPIO desc for the line.
	 */
	if (!test_bit(offset, fwd->valid_mask))
		return -ENODEV;

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

	return chip->can_sleep ? gpiod_get_value_cansleep(fwd->descs[offset])
			       : gpiod_get_value(fwd->descs[offset]);
}

static int gpio_fwd_get_multiple(struct gpiochip_fwd *fwd, unsigned long *mask,
				 unsigned long *bits)
{
	struct gpio_desc **descs = fwd_tmp_descs(fwd);
	unsigned long *values = fwd_tmp_values(fwd);
	unsigned int i, j = 0;
	int error;

	bitmap_clear(values, 0, fwd->chip.ngpio);
	for_each_set_bit(i, mask, fwd->chip.ngpio)
		descs[j++] = fwd->descs[i];

	if (fwd->chip.can_sleep)
		error = gpiod_get_array_value_cansleep(j, descs, NULL, values);
	else
		error = gpiod_get_array_value(j, descs, NULL, values);
	if (error)
		return error;

	j = 0;
	for_each_set_bit(i, mask, fwd->chip.ngpio)
		__assign_bit(i, bits, test_bit(j++, values));

	return 0;
}

static int gpio_fwd_get_multiple_locked(struct gpio_chip *chip,
					unsigned long *mask, unsigned long *bits)
{
	struct gpiochip_fwd *fwd = gpiochip_get_data(chip);
	unsigned long flags;
	int error;

	if (chip->can_sleep) {
		mutex_lock(&fwd->mlock);
		error = gpio_fwd_get_multiple(fwd, mask, bits);
		mutex_unlock(&fwd->mlock);
	} else {
		spin_lock_irqsave(&fwd->slock, flags);
		error = gpio_fwd_get_multiple(fwd, mask, bits);
		spin_unlock_irqrestore(&fwd->slock, flags);
	}

	return error;
}

static void gpio_fwd_delay(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct gpiochip_fwd *fwd = gpiochip_get_data(chip);
	const struct gpiochip_fwd_timing *delay_timings;
	bool is_active_low = gpiod_is_active_low(fwd->descs[offset]);
	u32 delay_us;

	delay_timings = &fwd->delay_timings[offset];
	if ((!is_active_low && value) || (is_active_low && !value))
		delay_us = delay_timings->ramp_up_us;
	else
		delay_us = delay_timings->ramp_down_us;
	if (!delay_us)
		return;

	if (chip->can_sleep)
		fsleep(delay_us);
	else
		udelay(delay_us);
}

static int gpio_fwd_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct gpiochip_fwd *fwd = gpiochip_get_data(chip);
	int ret;

	if (chip->can_sleep)
		ret = gpiod_set_value_cansleep(fwd->descs[offset], value);
	else
		ret = gpiod_set_value(fwd->descs[offset], value);
	if (ret)
		return ret;

	if (fwd->delay_timings)
		gpio_fwd_delay(chip, offset, value);

	return ret;
}

static int gpio_fwd_set_multiple(struct gpiochip_fwd *fwd, unsigned long *mask,
				 unsigned long *bits)
{
	struct gpio_desc **descs = fwd_tmp_descs(fwd);
	unsigned long *values = fwd_tmp_values(fwd);
	unsigned int i, j = 0, ret;

	for_each_set_bit(i, mask, fwd->chip.ngpio) {
		__assign_bit(j, values, test_bit(i, bits));
		descs[j++] = fwd->descs[i];
	}

	if (fwd->chip.can_sleep)
		ret = gpiod_set_array_value_cansleep(j, descs, NULL, values);
	else
		ret = gpiod_set_array_value(j, descs, NULL, values);

	return ret;
}

static int gpio_fwd_set_multiple_locked(struct gpio_chip *chip,
					unsigned long *mask, unsigned long *bits)
{
	struct gpiochip_fwd *fwd = gpiochip_get_data(chip);
	unsigned long flags;
	int ret;

	if (chip->can_sleep) {
		mutex_lock(&fwd->mlock);
		ret = gpio_fwd_set_multiple(fwd, mask, bits);
		mutex_unlock(&fwd->mlock);
	} else {
		spin_lock_irqsave(&fwd->slock, flags);
		ret = gpio_fwd_set_multiple(fwd, mask, bits);
		spin_unlock_irqrestore(&fwd->slock, flags);
	}

	return ret;
}

static int gpio_fwd_set_config(struct gpio_chip *chip, unsigned int offset,
			       unsigned long config)
{
	struct gpiochip_fwd *fwd = gpiochip_get_data(chip);

	return gpiod_set_config(fwd->descs[offset], config);
}

static int gpio_fwd_to_irq(struct gpio_chip *chip, unsigned int offset)
{
	struct gpiochip_fwd *fwd = gpiochip_get_data(chip);

	return gpiod_to_irq(fwd->descs[offset]);
}

/*
 * The GPIO delay provides a way to configure platform specific delays
 * for the GPIO ramp-up or ramp-down delays. This can serve the following
 * purposes:
 *   - Open-drain output using an RC filter
 */
#define FWD_FEATURE_DELAY		BIT(0)

#ifdef CONFIG_OF_GPIO
static int gpiochip_fwd_delay_of_xlate(struct gpio_chip *chip,
				       const struct of_phandle_args *gpiospec,
				       u32 *flags)
{
	struct gpiochip_fwd *fwd = gpiochip_get_data(chip);
	struct gpiochip_fwd_timing *timings;
	u32 line;

	if (gpiospec->args_count != chip->of_gpio_n_cells)
		return -EINVAL;

	line = gpiospec->args[0];
	if (line >= chip->ngpio)
		return -EINVAL;

	timings = &fwd->delay_timings[line];
	timings->ramp_up_us = gpiospec->args[1];
	timings->ramp_down_us = gpiospec->args[2];

	return line;
}

static int gpiochip_fwd_setup_delay_line(struct gpiochip_fwd *fwd)
{
	struct gpio_chip *chip = &fwd->chip;

	fwd->delay_timings = devm_kcalloc(chip->parent, chip->ngpio,
					  sizeof(*fwd->delay_timings),
					  GFP_KERNEL);
	if (!fwd->delay_timings)
		return -ENOMEM;

	chip->of_xlate = gpiochip_fwd_delay_of_xlate;
	chip->of_gpio_n_cells = 3;

	return 0;
}
#else
static int gpiochip_fwd_setup_delay_line(struct gpiochip_fwd *fwd)
{
	return 0;
}
#endif	/* !CONFIG_OF_GPIO */

/**
 * gpiochip_fwd_get_gpiochip - Get the GPIO chip for the GPIO forwarder
 * @fwd: GPIO forwarder
 *
 * Returns: The GPIO chip for the GPIO forwarder
 */
struct gpio_chip *gpiochip_fwd_get_gpiochip(struct gpiochip_fwd *fwd)
{
	return &fwd->chip;
}
EXPORT_SYMBOL_NS_GPL(gpiochip_fwd_get_gpiochip, "GPIO_FORWARDER");

/**
 * gpiochip_fwd_get_data - Get driver-private data for the GPIO forwarder
 * @fwd: GPIO forwarder
 *
 * Returns: The driver-private data for the GPIO forwarder
 */
void *gpiochip_fwd_get_data(struct gpiochip_fwd *fwd)
{
	return fwd->data;
}
EXPORT_SYMBOL_NS_GPL(gpiochip_fwd_get_data, "GPIO_FORWARDER");

/**
 * gpiochip_fwd_gpio_request - Request a line of the GPIO forwarder
 * @fwd: GPIO forwarder
 * @offset: the offset of the line to request
 *
 * Returns: 0 on success, or negative errno on failure.
 */
int gpiochip_fwd_gpio_request(struct gpiochip_fwd *fwd, unsigned int offset)
{
	struct gpio_chip *gc = gpiochip_fwd_get_gpiochip(fwd);

	return gpio_fwd_request(gc, offset);
}
EXPORT_SYMBOL_NS_GPL(gpiochip_fwd_gpio_request, "GPIO_FORWARDER");

/**
 * gpiochip_fwd_gpio_get_direction - Return the current direction of a GPIO forwarder line
 * @fwd: GPIO forwarder
 * @offset: the offset of the line
 *
 * Returns: 0 for output, 1 for input, or an error code in case of error.
 */
int gpiochip_fwd_gpio_get_direction(struct gpiochip_fwd *fwd, unsigned int offset)
{
	struct gpio_chip *gc = gpiochip_fwd_get_gpiochip(fwd);

	return gpio_fwd_get_direction(gc, offset);
}
EXPORT_SYMBOL_NS_GPL(gpiochip_fwd_gpio_get_direction, "GPIO_FORWARDER");

/**
 * gpiochip_fwd_gpio_direction_output - Set a GPIO forwarder line direction to
 * output
 * @fwd: GPIO forwarder
 * @offset: the offset of the line
 * @value: value to set
 *
 * Returns: 0 on success, or negative errno on failure.
 */
int gpiochip_fwd_gpio_direction_output(struct gpiochip_fwd *fwd, unsigned int offset,
				       int value)
{
	struct gpio_chip *gc = gpiochip_fwd_get_gpiochip(fwd);

	return gpio_fwd_direction_output(gc, offset, value);
}
EXPORT_SYMBOL_NS_GPL(gpiochip_fwd_gpio_direction_output, "GPIO_FORWARDER");

/**
 * gpiochip_fwd_gpio_direction_input - Set a GPIO forwarder line direction to input
 * @fwd: GPIO forwarder
 * @offset: the offset of the line
 *
 * Returns: 0 on success, or negative errno on failure.
 */
int gpiochip_fwd_gpio_direction_input(struct gpiochip_fwd *fwd, unsigned int offset)
{
	struct gpio_chip *gc = gpiochip_fwd_get_gpiochip(fwd);

	return gpio_fwd_direction_input(gc, offset);
}
EXPORT_SYMBOL_NS_GPL(gpiochip_fwd_gpio_direction_input, "GPIO_FORWARDER");

/**
 * gpiochip_fwd_gpio_get - Return a GPIO forwarder line's value
 * @fwd: GPIO forwarder
 * @offset: the offset of the line
 *
 * Returns: The GPIO's logical value, i.e. taking the ACTIVE_LOW status into
 * account, or negative errno on failure.
 */
int gpiochip_fwd_gpio_get(struct gpiochip_fwd *fwd, unsigned int offset)
{
	struct gpio_chip *gc = gpiochip_fwd_get_gpiochip(fwd);

	return gpio_fwd_get(gc, offset);
}
EXPORT_SYMBOL_NS_GPL(gpiochip_fwd_gpio_get, "GPIO_FORWARDER");

/**
 * gpiochip_fwd_gpio_get_multiple - Get values for multiple GPIO forwarder lines
 * @fwd: GPIO forwarder
 * @mask: bit mask array; one bit per line; BITS_PER_LONG bits per word defines
 *        which lines are to be read
 * @bits: bit value array; one bit per line; BITS_PER_LONG bits per word will
 *        contains the read values for the lines specified by mask
 *
 * Returns: 0 on success, or negative errno on failure.
 */
int gpiochip_fwd_gpio_get_multiple(struct gpiochip_fwd *fwd, unsigned long *mask,
				   unsigned long *bits)
{
	struct gpio_chip *gc = gpiochip_fwd_get_gpiochip(fwd);

	return gpio_fwd_get_multiple_locked(gc, mask, bits);
}
EXPORT_SYMBOL_NS_GPL(gpiochip_fwd_gpio_get_multiple, "GPIO_FORWARDER");

/**
 * gpiochip_fwd_gpio_set - Assign value to a GPIO forwarder line.
 * @fwd: GPIO forwarder
 * @offset: the offset of the line
 * @value: value to set
 *
 * Returns: 0 on success, or negative errno on failure.
 */
int gpiochip_fwd_gpio_set(struct gpiochip_fwd *fwd, unsigned int offset, int value)
{
	struct gpio_chip *gc = gpiochip_fwd_get_gpiochip(fwd);

	return gpio_fwd_set(gc, offset, value);
}
EXPORT_SYMBOL_NS_GPL(gpiochip_fwd_gpio_set, "GPIO_FORWARDER");

/**
 * gpiochip_fwd_gpio_set_multiple - Assign values to multiple GPIO forwarder lines
 * @fwd: GPIO forwarder
 * @mask: bit mask array; one bit per output; BITS_PER_LONG bits per word
 *        defines which outputs are to be changed
 * @bits: bit value array; one bit per output; BITS_PER_LONG bits per word
 *        defines the values the outputs specified by mask are to be set to
 *
 * Returns: 0 on success, or negative errno on failure.
 */
int gpiochip_fwd_gpio_set_multiple(struct gpiochip_fwd *fwd, unsigned long *mask,
				   unsigned long *bits)
{
	struct gpio_chip *gc = gpiochip_fwd_get_gpiochip(fwd);

	return gpio_fwd_set_multiple_locked(gc, mask, bits);
}
EXPORT_SYMBOL_NS_GPL(gpiochip_fwd_gpio_set_multiple, "GPIO_FORWARDER");

/**
 * gpiochip_fwd_gpio_set_config - Set @config for a GPIO forwarder line
 * @fwd: GPIO forwarder
 * @offset: the offset of the line
 * @config: Same packed config format as generic pinconf
 *
 * Returns: 0 on success, %-ENOTSUPP if the controller doesn't support setting
 * the configuration.
 */
int gpiochip_fwd_gpio_set_config(struct gpiochip_fwd *fwd, unsigned int offset,
				 unsigned long config)
{
	struct gpio_chip *gc = gpiochip_fwd_get_gpiochip(fwd);

	return gpio_fwd_set_config(gc, offset, config);
}
EXPORT_SYMBOL_NS_GPL(gpiochip_fwd_gpio_set_config, "GPIO_FORWARDER");

/**
 * gpiochip_fwd_gpio_to_irq - Return the IRQ corresponding to a GPIO forwarder line
 * @fwd: GPIO forwarder
 * @offset: the offset of the line
 *
 * Returns: The Linux IRQ corresponding to the passed line, or an error code in
 * case of error.
 */
int gpiochip_fwd_gpio_to_irq(struct gpiochip_fwd *fwd, unsigned int offset)
{
	struct gpio_chip *gc = gpiochip_fwd_get_gpiochip(fwd);

	return gpio_fwd_to_irq(gc, offset);
}
EXPORT_SYMBOL_NS_GPL(gpiochip_fwd_gpio_to_irq, "GPIO_FORWARDER");

/**
 * devm_gpiochip_fwd_alloc - Allocate and initialize a new GPIO forwarder
 * @dev: Parent device pointer
 * @ngpios: Number of GPIOs in the forwarder
 *
 * Returns: An opaque object pointer, or an ERR_PTR()-encoded negative error
 * code on failure.
 */
struct gpiochip_fwd *devm_gpiochip_fwd_alloc(struct device *dev,
					     unsigned int ngpios)
{
	struct gpiochip_fwd *fwd;
	struct gpio_chip *chip;

	fwd = devm_kzalloc(dev, struct_size(fwd, tmp, fwd_tmp_size(ngpios)), GFP_KERNEL);
	if (!fwd)
		return ERR_PTR(-ENOMEM);

	fwd->descs = devm_kcalloc(dev, ngpios, sizeof(*fwd->descs), GFP_KERNEL);
	if (!fwd->descs)
		return ERR_PTR(-ENOMEM);

	fwd->valid_mask = devm_bitmap_zalloc(dev, ngpios, GFP_KERNEL);
	if (!fwd->valid_mask)
		return ERR_PTR(-ENOMEM);

	chip = &fwd->chip;

	chip->label = dev_name(dev);
	chip->parent = dev;
	chip->owner = THIS_MODULE;
	chip->request = gpio_fwd_request;
	chip->get_direction = gpio_fwd_get_direction;
	chip->direction_input = gpio_fwd_direction_input;
	chip->direction_output = gpio_fwd_direction_output;
	chip->get = gpio_fwd_get;
	chip->get_multiple = gpio_fwd_get_multiple_locked;
	chip->set = gpio_fwd_set;
	chip->set_multiple = gpio_fwd_set_multiple_locked;
	chip->set_config = gpio_fwd_set_config;
	chip->to_irq = gpio_fwd_to_irq;
	chip->base = -1;
	chip->ngpio = ngpios;

	return fwd;
}
EXPORT_SYMBOL_NS_GPL(devm_gpiochip_fwd_alloc, "GPIO_FORWARDER");

/**
 * gpiochip_fwd_desc_add - Add a GPIO desc in the forwarder
 * @fwd: GPIO forwarder
 * @desc: GPIO descriptor to register
 * @offset: offset for the GPIO in the forwarder
 *
 * Returns: 0 on success, or negative errno on failure.
 */
int gpiochip_fwd_desc_add(struct gpiochip_fwd *fwd, struct gpio_desc *desc,
			  unsigned int offset)
{
	struct gpio_chip *chip = &fwd->chip;

	if (offset >= chip->ngpio)
		return -EINVAL;

	if (test_and_set_bit(offset, fwd->valid_mask))
		return -EEXIST;

	/*
	 * If any of the GPIO lines are sleeping, then the entire forwarder
	 * will be sleeping.
	 */
	if (gpiod_cansleep(desc))
		chip->can_sleep = true;

	fwd->descs[offset] = desc;

	dev_dbg(chip->parent, "%u => gpio %d irq %d\n", offset,
		desc_to_gpio(desc), gpiod_to_irq(desc));

	return 0;
}
EXPORT_SYMBOL_NS_GPL(gpiochip_fwd_desc_add, "GPIO_FORWARDER");

/**
 * gpiochip_fwd_desc_free - Remove a GPIO desc from the forwarder
 * @fwd: GPIO forwarder
 * @offset: offset of GPIO desc to remove
 */
void gpiochip_fwd_desc_free(struct gpiochip_fwd *fwd, unsigned int offset)
{
	if (test_and_clear_bit(offset, fwd->valid_mask))
		gpiod_put(fwd->descs[offset]);
}
EXPORT_SYMBOL_NS_GPL(gpiochip_fwd_desc_free, "GPIO_FORWARDER");

/**
 * gpiochip_fwd_register - Register a GPIO forwarder
 * @fwd: GPIO forwarder
 * @data: driver-private data associated with this forwarder
 *
 * Returns: 0 on success, or negative errno on failure.
 */
int gpiochip_fwd_register(struct gpiochip_fwd *fwd, void *data)
{
	struct gpio_chip *chip = &fwd->chip;

	/*
	 * Some gpio_desc were not registered. They will be registered at runtime
	 * but we have to suppose they can sleep.
	 */
	if (!bitmap_full(fwd->valid_mask, chip->ngpio))
		chip->can_sleep = true;

	if (chip->can_sleep)
		mutex_init(&fwd->mlock);
	else
		spin_lock_init(&fwd->slock);

	fwd->data = data;

	return devm_gpiochip_add_data(chip->parent, chip, fwd);
}
EXPORT_SYMBOL_NS_GPL(gpiochip_fwd_register, "GPIO_FORWARDER");

/**
 * gpiochip_fwd_create() - Create a new GPIO forwarder
 * @dev: Parent device pointer
 * @ngpios: Number of GPIOs in the forwarder.
 * @descs: Array containing the GPIO descriptors to forward to.
 *         This array must contain @ngpios entries, and can be deallocated
 *         as the forwarder has its own array.
 * @features: Bitwise ORed features as defined with FWD_FEATURE_*.
 *
 * This function creates a new gpiochip, which forwards all GPIO operations to
 * the passed GPIO descriptors.
 *
 * Return: An opaque object pointer, or an ERR_PTR()-encoded negative error
 *         code on failure.
 */
static struct gpiochip_fwd *gpiochip_fwd_create(struct device *dev,
						unsigned int ngpios,
						struct gpio_desc *descs[],
						unsigned long features)
{
	struct gpiochip_fwd *fwd;
	unsigned int i;
	int error;

	fwd = devm_gpiochip_fwd_alloc(dev, ngpios);
	if (IS_ERR(fwd))
		return fwd;

	for (i = 0; i < ngpios; i++) {
		error = gpiochip_fwd_desc_add(fwd, descs[i], i);
		if (error)
			return ERR_PTR(error);
	}

	if (features & FWD_FEATURE_DELAY) {
		error = gpiochip_fwd_setup_delay_line(fwd);
		if (error)
			return ERR_PTR(error);
	}

	error = gpiochip_fwd_register(fwd, NULL);
	if (error)
		return ERR_PTR(error);

	return fwd;
}

/*
 * Configfs interface
 */

static struct gpio_aggregator *
to_gpio_aggregator(struct config_item *item)
{
	struct config_group *group = to_config_group(item);

	return container_of(group, struct gpio_aggregator, group);
}

static struct gpio_aggregator_line *
to_gpio_aggregator_line(struct config_item *item)
{
	struct config_group *group = to_config_group(item);

	return container_of(group, struct gpio_aggregator_line, group);
}

static struct fwnode_handle *
gpio_aggregator_make_device_sw_node(struct gpio_aggregator *aggr)
{
	struct property_entry properties[2];
	struct gpio_aggregator_line *line;
	size_t num_lines;
	int n = 0;

	memset(properties, 0, sizeof(properties));

	num_lines = gpio_aggregator_count_lines(aggr);
	if (num_lines == 0)
		return NULL;

	const char **line_names __free(kfree) = kcalloc(
				num_lines, sizeof(*line_names), GFP_KERNEL);
	if (!line_names)
		return ERR_PTR(-ENOMEM);

	/* The list is always sorted as new elements are inserted in order. */
	list_for_each_entry(line, &aggr->list_head, entry)
		line_names[n++] = line->name ?: "";

	properties[0] = PROPERTY_ENTRY_STRING_ARRAY_LEN(
					"gpio-line-names",
					line_names, num_lines);

	return fwnode_create_software_node(properties, NULL);
}

static int gpio_aggregator_activate(struct gpio_aggregator *aggr)
{
	struct platform_device_info pdevinfo;
	struct gpio_aggregator_line *line;
	struct fwnode_handle *swnode;
	unsigned int n = 0;
	int ret = 0;

	if (gpio_aggregator_count_lines(aggr) == 0)
		return -EINVAL;

	aggr->lookups = kzalloc(struct_size(aggr->lookups, table, 1),
				GFP_KERNEL);
	if (!aggr->lookups)
		return -ENOMEM;

	swnode = gpio_aggregator_make_device_sw_node(aggr);
	if (IS_ERR(swnode)) {
		ret = PTR_ERR(swnode);
		goto err_remove_lookups;
	}

	memset(&pdevinfo, 0, sizeof(pdevinfo));
	pdevinfo.name = DRV_NAME;
	pdevinfo.id = aggr->id;
	pdevinfo.fwnode = swnode;

	/* The list is always sorted as new elements are inserted in order. */
	list_for_each_entry(line, &aggr->list_head, entry) {
		/*
		 * - Either GPIO chip label or line name must be configured
		 *   (i.e. line->key must be non-NULL)
		 * - Line directories must be named with sequential numeric
		 *   suffixes starting from 0. (i.e. ./line0, ./line1, ...)
		 */
		if (!line->key || line->idx != n) {
			ret = -EINVAL;
			goto err_remove_swnode;
		}

		if (line->offset < 0)
			ret = gpio_aggregator_add_gpio(aggr, line->key,
						       U16_MAX, &n);
		else
			ret = gpio_aggregator_add_gpio(aggr, line->key,
						       line->offset, &n);
		if (ret)
			goto err_remove_swnode;
	}

	aggr->lookups->dev_id = kasprintf(GFP_KERNEL, "%s.%d", DRV_NAME, aggr->id);
	if (!aggr->lookups->dev_id) {
		ret = -ENOMEM;
		goto err_remove_swnode;
	}

	gpiod_add_lookup_table(aggr->lookups);

	ret = dev_sync_probe_register(&aggr->probe_data, &pdevinfo);
	if (ret)
		goto err_remove_lookup_table;

	return 0;

err_remove_lookup_table:
	kfree(aggr->lookups->dev_id);
	gpiod_remove_lookup_table(aggr->lookups);
err_remove_swnode:
	fwnode_remove_software_node(swnode);
err_remove_lookups:
	kfree(aggr->lookups);

	return ret;
}

static void gpio_aggregator_deactivate(struct gpio_aggregator *aggr)
{
	dev_sync_probe_unregister(&aggr->probe_data);
	gpiod_remove_lookup_table(aggr->lookups);
	kfree(aggr->lookups->dev_id);
	kfree(aggr->lookups);
}

static void gpio_aggregator_lockup_configfs(struct gpio_aggregator *aggr,
					    bool lock)
{
	struct configfs_subsystem *subsys = aggr->group.cg_subsys;
	struct gpio_aggregator_line *line;

	/*
	 * The device only needs to depend on leaf lines. This is
	 * sufficient to lock up all the configfs entries that the
	 * instantiated, alive device depends on.
	 */
	list_for_each_entry(line, &aggr->list_head, entry) {
		if (lock)
			configfs_depend_item_unlocked(
					subsys, &line->group.cg_item);
		else
			configfs_undepend_item_unlocked(
					&line->group.cg_item);
	}
}

static ssize_t
gpio_aggregator_line_key_show(struct config_item *item, char *page)
{
	struct gpio_aggregator_line *line = to_gpio_aggregator_line(item);
	struct gpio_aggregator *aggr = line->parent;

	guard(mutex)(&aggr->lock);

	return sysfs_emit(page, "%s\n", line->key ?: "");
}

static ssize_t
gpio_aggregator_line_key_store(struct config_item *item, const char *page,
			       size_t count)
{
	struct gpio_aggregator_line *line = to_gpio_aggregator_line(item);
	struct gpio_aggregator *aggr = line->parent;

	char *key __free(kfree) = kstrndup(skip_spaces(page), count,
					   GFP_KERNEL);
	if (!key)
		return -ENOMEM;

	strim(key);

	guard(mutex)(&aggr->lock);

	if (gpio_aggregator_is_activating(aggr) ||
	    gpio_aggregator_is_active(aggr))
		return -EBUSY;

	kfree(line->key);
	line->key = no_free_ptr(key);

	return count;
}
CONFIGFS_ATTR(gpio_aggregator_line_, key);

static ssize_t
gpio_aggregator_line_name_show(struct config_item *item, char *page)
{
	struct gpio_aggregator_line *line = to_gpio_aggregator_line(item);
	struct gpio_aggregator *aggr = line->parent;

	guard(mutex)(&aggr->lock);

	return sysfs_emit(page, "%s\n", line->name ?: "");
}

static ssize_t
gpio_aggregator_line_name_store(struct config_item *item, const char *page,
				size_t count)
{
	struct gpio_aggregator_line *line = to_gpio_aggregator_line(item);
	struct gpio_aggregator *aggr = line->parent;

	char *name __free(kfree) = kstrndup(skip_spaces(page), count,
					    GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	strim(name);

	guard(mutex)(&aggr->lock);

	if (gpio_aggregator_is_activating(aggr) ||
	    gpio_aggregator_is_active(aggr))
		return -EBUSY;

	kfree(line->name);
	line->name = no_free_ptr(name);

	return count;
}
CONFIGFS_ATTR(gpio_aggregator_line_, name);

static ssize_t
gpio_aggregator_line_offset_show(struct config_item *item, char *page)
{
	struct gpio_aggregator_line *line = to_gpio_aggregator_line(item);
	struct gpio_aggregator *aggr = line->parent;

	guard(mutex)(&aggr->lock);

	return sysfs_emit(page, "%d\n", line->offset);
}

static ssize_t
gpio_aggregator_line_offset_store(struct config_item *item, const char *page,
				  size_t count)
{
	struct gpio_aggregator_line *line = to_gpio_aggregator_line(item);
	struct gpio_aggregator *aggr = line->parent;
	int offset, ret;

	ret = kstrtoint(page, 0, &offset);
	if (ret)
		return ret;

	/*
	 * When offset == -1, 'key' represents a line name to lookup.
	 * When 0 <= offset < 65535, 'key' represents the label of the chip with
	 * the 'offset' value representing the line within that chip.
	 *
	 * GPIOLIB uses the U16_MAX value to indicate lookup by line name so
	 * the greatest offset we can accept is (U16_MAX - 1).
	 */
	if (offset > (U16_MAX - 1) || offset < -1)
		return -EINVAL;

	guard(mutex)(&aggr->lock);

	if (gpio_aggregator_is_activating(aggr) ||
	    gpio_aggregator_is_active(aggr))
		return -EBUSY;

	line->offset = offset;

	return count;
}
CONFIGFS_ATTR(gpio_aggregator_line_, offset);

static struct configfs_attribute *gpio_aggregator_line_attrs[] = {
	&gpio_aggregator_line_attr_key,
	&gpio_aggregator_line_attr_name,
	&gpio_aggregator_line_attr_offset,
	NULL
};

static ssize_t
gpio_aggregator_device_dev_name_show(struct config_item *item, char *page)
{
	struct gpio_aggregator *aggr = to_gpio_aggregator(item);
	struct platform_device *pdev;

	guard(mutex)(&aggr->lock);

	pdev = aggr->probe_data.pdev;
	if (pdev)
		return sysfs_emit(page, "%s\n", dev_name(&pdev->dev));

	return sysfs_emit(page, "%s.%d\n", DRV_NAME, aggr->id);
}
CONFIGFS_ATTR_RO(gpio_aggregator_device_, dev_name);

static ssize_t
gpio_aggregator_device_live_show(struct config_item *item, char *page)
{
	struct gpio_aggregator *aggr = to_gpio_aggregator(item);

	guard(mutex)(&aggr->lock);

	return sysfs_emit(page, "%c\n",
			  gpio_aggregator_is_active(aggr) ? '1' : '0');
}

static ssize_t
gpio_aggregator_device_live_store(struct config_item *item, const char *page,
				  size_t count)
{
	struct gpio_aggregator *aggr = to_gpio_aggregator(item);
	int ret = 0;
	bool live;

	ret = kstrtobool(page, &live);
	if (ret)
		return ret;

	if (!try_module_get(THIS_MODULE))
		return -ENOENT;

	if (live && !aggr->init_via_sysfs)
		gpio_aggregator_lockup_configfs(aggr, true);

	scoped_guard(mutex, &aggr->lock) {
		if (gpio_aggregator_is_activating(aggr) ||
		    (live == gpio_aggregator_is_active(aggr)))
			ret = -EPERM;
		else if (live)
			ret = gpio_aggregator_activate(aggr);
		else
			gpio_aggregator_deactivate(aggr);
	}

	/*
	 * Undepend is required only if device disablement (live == 0)
	 * succeeds or if device enablement (live == 1) fails.
	 */
	if (live == !!ret && !aggr->init_via_sysfs)
		gpio_aggregator_lockup_configfs(aggr, false);

	module_put(THIS_MODULE);

	return ret ?: count;
}
CONFIGFS_ATTR(gpio_aggregator_device_, live);

static struct configfs_attribute *gpio_aggregator_device_attrs[] = {
	&gpio_aggregator_device_attr_dev_name,
	&gpio_aggregator_device_attr_live,
	NULL
};

static void
gpio_aggregator_line_release(struct config_item *item)
{
	struct gpio_aggregator_line *line = to_gpio_aggregator_line(item);
	struct gpio_aggregator *aggr = line->parent;

	guard(mutex)(&aggr->lock);

	gpio_aggregator_line_del(aggr, line);
	kfree(line->key);
	kfree(line->name);
	kfree(line);
}

static struct configfs_item_operations gpio_aggregator_line_item_ops = {
	.release	= gpio_aggregator_line_release,
};

static const struct config_item_type gpio_aggregator_line_type = {
	.ct_item_ops	= &gpio_aggregator_line_item_ops,
	.ct_attrs	= gpio_aggregator_line_attrs,
	.ct_owner	= THIS_MODULE,
};

static void gpio_aggregator_device_release(struct config_item *item)
{
	struct gpio_aggregator *aggr = to_gpio_aggregator(item);

	/*
	 * At this point, aggr is neither active nor activating,
	 * so calling gpio_aggregator_deactivate() is always unnecessary.
	 */
	gpio_aggregator_free(aggr);
}

static struct configfs_item_operations gpio_aggregator_device_item_ops = {
	.release	= gpio_aggregator_device_release,
};

static struct config_group *
gpio_aggregator_device_make_group(struct config_group *group, const char *name)
{
	struct gpio_aggregator *aggr = to_gpio_aggregator(&group->cg_item);
	struct gpio_aggregator_line *line;
	unsigned int idx;
	int ret, nchar;

	ret = sscanf(name, "line%u%n", &idx, &nchar);
	if (ret != 1 || nchar != strlen(name))
		return ERR_PTR(-EINVAL);

	if (aggr->init_via_sysfs)
		/*
		 * Aggregators created via legacy sysfs interface are exposed as
		 * default groups, which means rmdir(2) is prohibited for them.
		 * For simplicity, and to avoid confusion, we also prohibit
		 * mkdir(2).
		 */
		return ERR_PTR(-EPERM);

	guard(mutex)(&aggr->lock);

	if (gpio_aggregator_is_active(aggr))
		return ERR_PTR(-EBUSY);

	list_for_each_entry(line, &aggr->list_head, entry)
		if (line->idx == idx)
			return ERR_PTR(-EINVAL);

	line = gpio_aggregator_line_alloc(aggr, idx, NULL, -1);
	if (IS_ERR(line))
		return ERR_CAST(line);

	config_group_init_type_name(&line->group, name, &gpio_aggregator_line_type);

	gpio_aggregator_line_add(aggr, line);

	return &line->group;
}

static struct configfs_group_operations gpio_aggregator_device_group_ops = {
	.make_group	= gpio_aggregator_device_make_group,
};

static const struct config_item_type gpio_aggregator_device_type = {
	.ct_group_ops	= &gpio_aggregator_device_group_ops,
	.ct_item_ops	= &gpio_aggregator_device_item_ops,
	.ct_attrs	= gpio_aggregator_device_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_group *
gpio_aggregator_make_group(struct config_group *group, const char *name)
{
	struct gpio_aggregator *aggr;
	int ret;

	/*
	 * "_sysfs" prefix is reserved for auto-generated config group
	 * for devices create via legacy sysfs interface.
	 */
	if (strncmp(name, AGGREGATOR_LEGACY_PREFIX,
		    sizeof(AGGREGATOR_LEGACY_PREFIX) - 1) == 0)
		return ERR_PTR(-EINVAL);

	/* arg space is unneeded */
	ret = gpio_aggregator_alloc(&aggr, 0);
	if (ret)
		return ERR_PTR(ret);

	config_group_init_type_name(&aggr->group, name, &gpio_aggregator_device_type);
	dev_sync_probe_init(&aggr->probe_data);

	return &aggr->group;
}

static struct configfs_group_operations gpio_aggregator_group_ops = {
	.make_group	= gpio_aggregator_make_group,
};

static const struct config_item_type gpio_aggregator_type = {
	.ct_group_ops	= &gpio_aggregator_group_ops,
	.ct_owner	= THIS_MODULE,
};

static struct configfs_subsystem gpio_aggregator_subsys = {
	.su_group = {
		.cg_item = {
			.ci_namebuf	= DRV_NAME,
			.ci_type	= &gpio_aggregator_type,
		},
	},
};

/*
 * Sysfs interface
 */
static int gpio_aggregator_parse(struct gpio_aggregator *aggr)
{
	char *args = skip_spaces(aggr->args);
	struct gpio_aggregator_line *line;
	char name[CONFIGFS_ITEM_NAME_LEN];
	char *key, *offsets, *p;
	unsigned int i, n = 0;
	int error = 0;

	unsigned long *bitmap __free(bitmap) =
			bitmap_alloc(AGGREGATOR_MAX_GPIOS, GFP_KERNEL);
	if (!bitmap)
		return -ENOMEM;

	args = next_arg(args, &key, &p);
	while (*args) {
		args = next_arg(args, &offsets, &p);

		p = get_options(offsets, 0, &error);
		if (error == 0 || *p) {
			/* Named GPIO line */
			scnprintf(name, sizeof(name), "line%u", n);
			line = gpio_aggregator_line_alloc(aggr, n, key, -1);
			if (IS_ERR(line)) {
				error = PTR_ERR(line);
				goto err;
			}
			config_group_init_type_name(&line->group, name,
						    &gpio_aggregator_line_type);
			error = configfs_register_group(&aggr->group,
							&line->group);
			if (error)
				goto err;
			scoped_guard(mutex, &aggr->lock)
				gpio_aggregator_line_add(aggr, line);

			error = gpio_aggregator_add_gpio(aggr, key, U16_MAX, &n);
			if (error)
				goto err;

			key = offsets;
			continue;
		}

		/* GPIO chip + offset(s) */
		error = bitmap_parselist(offsets, bitmap, AGGREGATOR_MAX_GPIOS);
		if (error) {
			pr_err("Cannot parse %s: %d\n", offsets, error);
			goto err;
		}

		for_each_set_bit(i, bitmap, AGGREGATOR_MAX_GPIOS) {
			scnprintf(name, sizeof(name), "line%u", n);
			line = gpio_aggregator_line_alloc(aggr, n, key, i);
			if (IS_ERR(line)) {
				error = PTR_ERR(line);
				goto err;
			}
			config_group_init_type_name(&line->group, name,
						    &gpio_aggregator_line_type);
			error = configfs_register_group(&aggr->group,
							&line->group);
			if (error)
				goto err;
			scoped_guard(mutex, &aggr->lock)
				gpio_aggregator_line_add(aggr, line);

			error = gpio_aggregator_add_gpio(aggr, key, i, &n);
			if (error)
				goto err;
		}

		args = next_arg(args, &key, &p);
	}

	if (!n) {
		pr_err("No GPIOs specified\n");
		error = -EINVAL;
		goto err;
	}

	return 0;

err:
	gpio_aggregator_free_lines(aggr);
	return error;
}

static ssize_t gpio_aggregator_new_device_store(struct device_driver *driver,
						const char *buf, size_t count)
{
	struct gpio_aggregator_pdev_meta meta = { .init_via_sysfs = true };
	char name[CONFIGFS_ITEM_NAME_LEN];
	struct gpio_aggregator *aggr;
	struct platform_device *pdev;
	int res;

	if (!try_module_get(THIS_MODULE))
		return -ENOENT;

	/* kernfs guarantees string termination, so count + 1 is safe */
	res = gpio_aggregator_alloc(&aggr, count + 1);
	if (res)
		goto put_module;

	memcpy(aggr->args, buf, count + 1);

	aggr->init_via_sysfs = true;
	aggr->lookups = kzalloc(struct_size(aggr->lookups, table, 1),
				GFP_KERNEL);
	if (!aggr->lookups) {
		res = -ENOMEM;
		goto free_ga;
	}

	aggr->lookups->dev_id = kasprintf(GFP_KERNEL, "%s.%d", DRV_NAME, aggr->id);
	if (!aggr->lookups->dev_id) {
		res = -ENOMEM;
		goto free_table;
	}

	scnprintf(name, sizeof(name), "%s.%d", AGGREGATOR_LEGACY_PREFIX, aggr->id);
	config_group_init_type_name(&aggr->group, name, &gpio_aggregator_device_type);

	/*
	 * Since the device created by sysfs might be toggled via configfs
	 * 'live' attribute later, this initialization is needed.
	 */
	dev_sync_probe_init(&aggr->probe_data);

	/* Expose to configfs */
	res = configfs_register_group(&gpio_aggregator_subsys.su_group,
				      &aggr->group);
	if (res)
		goto free_dev_id;

	res = gpio_aggregator_parse(aggr);
	if (res)
		goto unregister_group;

	gpiod_add_lookup_table(aggr->lookups);

	pdev = platform_device_register_data(NULL, DRV_NAME, aggr->id, &meta, sizeof(meta));
	if (IS_ERR(pdev)) {
		res = PTR_ERR(pdev);
		goto remove_table;
	}

	aggr->probe_data.pdev = pdev;
	module_put(THIS_MODULE);
	return count;

remove_table:
	gpiod_remove_lookup_table(aggr->lookups);
unregister_group:
	configfs_unregister_group(&aggr->group);
free_dev_id:
	kfree(aggr->lookups->dev_id);
free_table:
	kfree(aggr->lookups);
free_ga:
	gpio_aggregator_free(aggr);
put_module:
	module_put(THIS_MODULE);
	return res;
}

static struct driver_attribute driver_attr_gpio_aggregator_new_device =
	__ATTR(new_device, 0200, NULL, gpio_aggregator_new_device_store);

static void gpio_aggregator_destroy(struct gpio_aggregator *aggr)
{
	scoped_guard(mutex, &aggr->lock) {
		if (gpio_aggregator_is_activating(aggr) ||
		    gpio_aggregator_is_active(aggr))
			gpio_aggregator_deactivate(aggr);
	}
	gpio_aggregator_free_lines(aggr);
	configfs_unregister_group(&aggr->group);
	kfree(aggr);
}

static ssize_t gpio_aggregator_delete_device_store(struct device_driver *driver,
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

	if (!try_module_get(THIS_MODULE))
		return -ENOENT;

	mutex_lock(&gpio_aggregator_lock);
	aggr = idr_find(&gpio_aggregator_idr, id);
	/*
	 * For simplicity, devices created via configfs cannot be deleted
	 * via sysfs.
	 */
	if (aggr && aggr->init_via_sysfs)
		idr_remove(&gpio_aggregator_idr, id);
	else {
		mutex_unlock(&gpio_aggregator_lock);
		module_put(THIS_MODULE);
		return -ENOENT;
	}
	mutex_unlock(&gpio_aggregator_lock);

	gpio_aggregator_destroy(aggr);
	module_put(THIS_MODULE);
	return count;
}

static struct driver_attribute driver_attr_gpio_aggregator_delete_device =
	__ATTR(delete_device, 0200, NULL, gpio_aggregator_delete_device_store);

static struct attribute *gpio_aggregator_attrs[] = {
	&driver_attr_gpio_aggregator_new_device.attr,
	&driver_attr_gpio_aggregator_delete_device.attr,
	NULL
};
ATTRIBUTE_GROUPS(gpio_aggregator);

/*
 *  GPIO Aggregator platform device
 */

static int gpio_aggregator_probe(struct platform_device *pdev)
{
	struct gpio_aggregator_pdev_meta *meta;
	struct device *dev = &pdev->dev;
	bool init_via_sysfs = false;
	struct gpio_desc **descs;
	struct gpiochip_fwd *fwd;
	unsigned long features;
	int i, n;

	n = gpiod_count(dev, NULL);
	if (n < 0)
		return n;

	descs = devm_kmalloc_array(dev, n, sizeof(*descs), GFP_KERNEL);
	if (!descs)
		return -ENOMEM;

	meta = dev_get_platdata(&pdev->dev);
	if (meta && meta->init_via_sysfs)
		init_via_sysfs = true;

	for (i = 0; i < n; i++) {
		descs[i] = devm_gpiod_get_index(dev, NULL, i, GPIOD_ASIS);
		if (IS_ERR(descs[i])) {
			/*
			 * Deferred probing is not suitable when the aggregator
			 * is created via configfs. They should just retry later
			 * whenever they like. For device creation via sysfs,
			 * error is propagated without overriding for backward
			 * compatibility. .prevent_deferred_probe is kept unset
			 * for other cases.
			 */
			if (!init_via_sysfs && !dev_of_node(dev) &&
			    descs[i] == ERR_PTR(-EPROBE_DEFER)) {
				pr_warn("Deferred probe canceled for creation via configfs.\n");
				return -ENODEV;
			}
			return PTR_ERR(descs[i]);
		}
	}

	features = (uintptr_t)device_get_match_data(dev);
	fwd = gpiochip_fwd_create(dev, n, descs, features);
	if (IS_ERR(fwd))
		return PTR_ERR(fwd);

	platform_set_drvdata(pdev, fwd);
	devm_kfree(dev, descs);
	return 0;
}

static const struct of_device_id gpio_aggregator_dt_ids[] = {
	{
		.compatible = "gpio-delay",
		.data = (void *)FWD_FEATURE_DELAY,
	},
	/*
	 * Add GPIO-operated devices controlled from userspace below,
	 * or use "driver_override" in sysfs.
	 */
	{}
};
MODULE_DEVICE_TABLE(of, gpio_aggregator_dt_ids);

static struct platform_driver gpio_aggregator_driver = {
	.probe = gpio_aggregator_probe,
	.driver = {
		.name = DRV_NAME,
		.groups = gpio_aggregator_groups,
		.of_match_table = gpio_aggregator_dt_ids,
	},
};

static int __exit gpio_aggregator_idr_remove(int id, void *p, void *data)
{
	/*
	 * There should be no aggregator created via configfs, as their
	 * presence would prevent module unloading.
	 */
	gpio_aggregator_destroy(p);
	return 0;
}

static void __exit gpio_aggregator_remove_all(void)
{
	/*
	 * Configfs callbacks acquire gpio_aggregator_lock when accessing
	 * gpio_aggregator_idr, so to prevent lock inversion deadlock, we
	 * cannot protect idr_for_each invocation here with
	 * gpio_aggregator_lock, as gpio_aggregator_idr_remove() accesses
	 * configfs groups. Fortunately, the new_device/delete_device path
	 * and the module unload path are mutually exclusive, thanks to an
	 * explicit try_module_get inside of those driver attr handlers.
	 * Also, when we reach here, no configfs entries present or being
	 * created. Therefore, no need to protect with gpio_aggregator_lock
	 * below.
	 */
	idr_for_each(&gpio_aggregator_idr, gpio_aggregator_idr_remove, NULL);
	idr_destroy(&gpio_aggregator_idr);
}

static int __init gpio_aggregator_init(void)
{
	int ret = 0;

	config_group_init(&gpio_aggregator_subsys.su_group);
	mutex_init(&gpio_aggregator_subsys.su_mutex);
	ret = configfs_register_subsystem(&gpio_aggregator_subsys);
	if (ret) {
		pr_err("Failed to register the '%s' configfs subsystem: %d\n",
		       gpio_aggregator_subsys.su_group.cg_item.ci_namebuf, ret);
		mutex_destroy(&gpio_aggregator_subsys.su_mutex);
		return ret;
	}

	/*
	 * CAVEAT: This must occur after configfs registration. Otherwise,
	 * a race condition could arise: driver attribute groups might be
	 * exposed and accessed by users before configfs registration
	 * completes. new_device_store() does not expect a partially
	 * initialized configfs state.
	 */
	ret = platform_driver_register(&gpio_aggregator_driver);
	if (ret) {
		pr_err("Failed to register the platform driver: %d\n", ret);
		mutex_destroy(&gpio_aggregator_subsys.su_mutex);
		configfs_unregister_subsystem(&gpio_aggregator_subsys);
	}

	return ret;
}
module_init(gpio_aggregator_init);

static void __exit gpio_aggregator_exit(void)
{
	gpio_aggregator_remove_all();
	platform_driver_unregister(&gpio_aggregator_driver);
	configfs_unregister_subsystem(&gpio_aggregator_subsys);
}
module_exit(gpio_aggregator_exit);

MODULE_AUTHOR("Geert Uytterhoeven <geert+renesas@glider.be>");
MODULE_DESCRIPTION("GPIO Aggregator");
MODULE_LICENSE("GPL v2");
