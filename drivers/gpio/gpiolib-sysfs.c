// SPDX-License-Identifier: GPL-2.0

#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kdev_t.h>
#include <linux/kstrtox.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/srcu.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>

#include <uapi/linux/gpio.h>

#include "gpiolib.h"
#include "gpiolib-sysfs.h"

#if IS_ENABLED(CONFIG_GPIO_SYSFS_LEGACY)

struct kernfs_node;

#define GPIO_IRQF_TRIGGER_NONE		0
#define GPIO_IRQF_TRIGGER_FALLING	BIT(0)
#define GPIO_IRQF_TRIGGER_RISING	BIT(1)
#define GPIO_IRQF_TRIGGER_BOTH		(GPIO_IRQF_TRIGGER_FALLING | \
					 GPIO_IRQF_TRIGGER_RISING)

enum {
	GPIO_SYSFS_LINE_CLASS_ATTR_DIRECTION = 0,
	GPIO_SYSFS_LINE_CLASS_ATTR_VALUE,
	GPIO_SYSFS_LINE_CLASS_ATTR_EDGE,
	GPIO_SYSFS_LINE_CLASS_ATTR_ACTIVE_LOW,
	GPIO_SYSFS_LINE_CLASS_ATTR_SENTINEL,
	GPIO_SYSFS_LINE_CLASS_ATTR_SIZE,
};

#endif /* CONFIG_GPIO_SYSFS_LEGACY */

enum {
	GPIO_SYSFS_LINE_CHIP_ATTR_DIRECTION = 0,
	GPIO_SYSFS_LINE_CHIP_ATTR_VALUE,
	GPIO_SYSFS_LINE_CHIP_ATTR_SENTINEL,
	GPIO_SYSFS_LINE_CHIP_ATTR_SIZE,
};

struct gpiod_data {
	struct list_head list;

	struct gpio_desc *desc;
	struct device *dev;

	struct mutex mutex;
#if IS_ENABLED(CONFIG_GPIO_SYSFS_LEGACY)
	struct kernfs_node *value_kn;
	int irq;
	unsigned char irq_flags;
#endif /* CONFIG_GPIO_SYSFS_LEGACY */

	bool direction_can_change;

	struct kobject *parent;
	struct device_attribute dir_attr;
	struct device_attribute val_attr;

#if IS_ENABLED(CONFIG_GPIO_SYSFS_LEGACY)
	struct device_attribute edge_attr;
	struct device_attribute active_low_attr;

	struct attribute *class_attrs[GPIO_SYSFS_LINE_CLASS_ATTR_SIZE];
	struct attribute_group class_attr_group;
	const struct attribute_group *class_attr_groups[2];
#endif /* CONFIG_GPIO_SYSFS_LEGACY */

	struct attribute *chip_attrs[GPIO_SYSFS_LINE_CHIP_ATTR_SIZE];
	struct attribute_group chip_attr_group;
	const struct attribute_group *chip_attr_groups[2];
};

struct gpiodev_data {
	struct list_head exported_lines;
	struct gpio_device *gdev;
	struct device *cdev_id; /* Class device by GPIO device ID */
#if IS_ENABLED(CONFIG_GPIO_SYSFS_LEGACY)
	struct device *cdev_base; /* Class device by GPIO base */
#endif /* CONFIG_GPIO_SYSFS_LEGACY */
};

/*
 * Lock to serialise gpiod export and unexport, and prevent re-export of
 * gpiod whose chip is being unregistered.
 */
static DEFINE_MUTEX(sysfs_lock);

/*
 * /sys/class/gpio/gpioN... only for GPIOs that are exported
 *   /direction
 *      * MAY BE OMITTED if kernel won't allow direction changes
 *      * is read/write as "in" or "out"
 *      * may also be written as "high" or "low", initializing
 *        output value as specified ("out" implies "low")
 *   /value
 *      * always readable, subject to hardware behavior
 *      * may be writable, as zero/nonzero
 *   /edge
 *      * configures behavior of poll(2) on /value
 *      * available only if pin can generate IRQs on input
 *      * is read/write as "none", "falling", "rising", or "both"
 *   /active_low
 *      * configures polarity of /value
 *      * is read/write as zero/nonzero
 *      * also affects existing and subsequent "falling" and "rising"
 *        /edge configuration
 */

static ssize_t direction_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct gpiod_data *data = container_of(attr, struct gpiod_data,
					       dir_attr);
	struct gpio_desc *desc = data->desc;
	int value;

	scoped_guard(mutex, &data->mutex) {
		gpiod_get_direction(desc);
		value = !!test_bit(GPIOD_FLAG_IS_OUT, &desc->flags);
	}

	return sysfs_emit(buf, "%s\n", value ? "out" : "in");
}

static ssize_t direction_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t size)
{
	struct gpiod_data *data = container_of(attr, struct gpiod_data,
					       dir_attr);
	struct gpio_desc *desc = data->desc;
	ssize_t status;

	guard(mutex)(&data->mutex);

	if (sysfs_streq(buf, "high"))
		status = gpiod_direction_output_raw(desc, 1);
	else if (sysfs_streq(buf, "out") || sysfs_streq(buf, "low"))
		status = gpiod_direction_output_raw(desc, 0);
	else if (sysfs_streq(buf, "in"))
		status = gpiod_direction_input(desc);
	else
		status = -EINVAL;

	return status ? : size;
}

static ssize_t value_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct gpiod_data *data = container_of(attr, struct gpiod_data,
					       val_attr);
	struct gpio_desc *desc = data->desc;
	ssize_t status;

	scoped_guard(mutex, &data->mutex)
		status = gpiod_get_value_cansleep(desc);

	if (status < 0)
		return status;

	return sysfs_emit(buf, "%zd\n", status);
}

static ssize_t value_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t size)
{
	struct gpiod_data *data = container_of(attr, struct gpiod_data,
					       val_attr);
	struct gpio_desc *desc = data->desc;
	ssize_t status;
	long value;

	status = kstrtol(buf, 0, &value);
	if (status)
		return status;

	guard(mutex)(&data->mutex);

	status = gpiod_set_value_cansleep(desc, value);
	if (status)
		return status;

	return size;
}

#if IS_ENABLED(CONFIG_GPIO_SYSFS_LEGACY)
static irqreturn_t gpio_sysfs_irq(int irq, void *priv)
{
	struct gpiod_data *data = priv;

	sysfs_notify_dirent(data->value_kn);

	return IRQ_HANDLED;
}

/* Caller holds gpiod-data mutex. */
static int gpio_sysfs_request_irq(struct gpiod_data *data, unsigned char flags)
{
	struct gpio_desc *desc = data->desc;
	unsigned long irq_flags;
	int ret;

	CLASS(gpio_chip_guard, guard)(desc);
	if (!guard.gc)
		return -ENODEV;

	data->irq = gpiod_to_irq(desc);
	if (data->irq < 0)
		return -EIO;

	irq_flags = IRQF_SHARED;
	if (flags & GPIO_IRQF_TRIGGER_FALLING) {
		irq_flags |= test_bit(GPIOD_FLAG_ACTIVE_LOW, &desc->flags) ?
				IRQF_TRIGGER_RISING : IRQF_TRIGGER_FALLING;
		set_bit(GPIOD_FLAG_EDGE_FALLING, &desc->flags);
	}
	if (flags & GPIO_IRQF_TRIGGER_RISING) {
		irq_flags |= test_bit(GPIOD_FLAG_ACTIVE_LOW, &desc->flags) ?
				IRQF_TRIGGER_FALLING : IRQF_TRIGGER_RISING;
		set_bit(GPIOD_FLAG_EDGE_RISING, &desc->flags);
	}

	/*
	 * FIXME: This should be done in the irq_request_resources callback
	 * when the irq is requested, but a few drivers currently fail to do
	 * so.
	 *
	 * Remove this redundant call (along with the corresponding unlock)
	 * when those drivers have been fixed.
	 */
	ret = gpiochip_lock_as_irq(guard.gc, gpio_chip_hwgpio(desc));
	if (ret < 0)
		goto err_clr_bits;

	ret = request_any_context_irq(data->irq, gpio_sysfs_irq, irq_flags,
				"gpiolib", data);
	if (ret < 0)
		goto err_unlock;

	data->irq_flags = flags;

	return 0;

err_unlock:
	gpiochip_unlock_as_irq(guard.gc, gpio_chip_hwgpio(desc));
err_clr_bits:
	clear_bit(GPIOD_FLAG_EDGE_RISING, &desc->flags);
	clear_bit(GPIOD_FLAG_EDGE_FALLING, &desc->flags);

	return ret;
}

/*
 * Caller holds gpiod-data mutex (unless called after class-device
 * deregistration).
 */
static void gpio_sysfs_free_irq(struct gpiod_data *data)
{
	struct gpio_desc *desc = data->desc;

	CLASS(gpio_chip_guard, guard)(desc);
	if (!guard.gc)
		return;

	data->irq_flags = 0;
	free_irq(data->irq, data);
	gpiochip_unlock_as_irq(guard.gc, gpio_chip_hwgpio(desc));
	clear_bit(GPIOD_FLAG_EDGE_RISING, &desc->flags);
	clear_bit(GPIOD_FLAG_EDGE_FALLING, &desc->flags);
}

static const char *const trigger_names[] = {
	[GPIO_IRQF_TRIGGER_NONE]	= "none",
	[GPIO_IRQF_TRIGGER_FALLING]	= "falling",
	[GPIO_IRQF_TRIGGER_RISING]	= "rising",
	[GPIO_IRQF_TRIGGER_BOTH]	= "both",
};

static ssize_t edge_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct gpiod_data *data = container_of(attr, struct gpiod_data,
					       edge_attr);
	int flags;

	scoped_guard(mutex, &data->mutex)
		flags = data->irq_flags;

	if (flags >= ARRAY_SIZE(trigger_names))
		return 0;

	return sysfs_emit(buf, "%s\n", trigger_names[flags]);
}

static ssize_t edge_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t size)
{
	struct gpiod_data *data = container_of(attr, struct gpiod_data,
					       edge_attr);
	ssize_t status = size;
	int flags;

	flags = sysfs_match_string(trigger_names, buf);
	if (flags < 0)
		return flags;

	guard(mutex)(&data->mutex);

	if (flags == data->irq_flags)
		return size;

	if (data->irq_flags)
		gpio_sysfs_free_irq(data);

	if (!flags)
		return size;

	status = gpio_sysfs_request_irq(data, flags);
	if (status)
		return status;

	gpiod_line_state_notify(data->desc, GPIO_V2_LINE_CHANGED_CONFIG);

	return size;
}

/* Caller holds gpiod-data mutex. */
static int gpio_sysfs_set_active_low(struct gpiod_data *data, int value)
{
	unsigned int flags = data->irq_flags;
	struct gpio_desc *desc = data->desc;
	int status = 0;

	if (!!test_bit(GPIOD_FLAG_ACTIVE_LOW, &desc->flags) == !!value)
		return 0;

	assign_bit(GPIOD_FLAG_ACTIVE_LOW, &desc->flags, value);

	/* reconfigure poll(2) support if enabled on one edge only */
	if (flags == GPIO_IRQF_TRIGGER_FALLING ||
	    flags == GPIO_IRQF_TRIGGER_RISING) {
		gpio_sysfs_free_irq(data);
		status = gpio_sysfs_request_irq(data, flags);
	}

	gpiod_line_state_notify(desc, GPIO_V2_LINE_CHANGED_CONFIG);

	return status;
}

static ssize_t active_low_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct gpiod_data *data = container_of(attr, struct gpiod_data,
					       active_low_attr);
	struct gpio_desc *desc = data->desc;
	int value;

	scoped_guard(mutex, &data->mutex)
		value = !!test_bit(GPIOD_FLAG_ACTIVE_LOW, &desc->flags);

	return sysfs_emit(buf, "%d\n", value);
}

static ssize_t active_low_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct gpiod_data *data = container_of(attr, struct gpiod_data,
					       active_low_attr);
	ssize_t status;
	long value;

	status = kstrtol(buf, 0, &value);
	if (status)
		return status;

	guard(mutex)(&data->mutex);

	return gpio_sysfs_set_active_low(data, value) ?: size;
}
#endif /* CONFIG_GPIO_SYSFS_LEGACY */

static umode_t gpio_is_visible(struct kobject *kobj, struct attribute *attr,
			       int n)
{
	struct device_attribute *dev_attr = container_of(attr,
						struct device_attribute, attr);
	umode_t mode = attr->mode;
	struct gpiod_data *data;

	if (strcmp(attr->name, "direction") == 0) {
		data = container_of(dev_attr, struct gpiod_data, dir_attr);

		if (!data->direction_can_change)
			mode = 0;
#if IS_ENABLED(CONFIG_GPIO_SYSFS_LEGACY)
	} else if (strcmp(attr->name, "edge") == 0) {
		data = container_of(dev_attr, struct gpiod_data, edge_attr);

		if (gpiod_to_irq(data->desc) < 0)
			mode = 0;

		if (!data->direction_can_change &&
		    test_bit(GPIOD_FLAG_IS_OUT, &data->desc->flags))
			mode = 0;
#endif /* CONFIG_GPIO_SYSFS_LEGACY */
	}

	return mode;
}

/*
 * /sys/class/gpio/gpiochipN/
 *   /base ... matching gpio_chip.base (N)
 *   /label ... matching gpio_chip.label
 *   /ngpio ... matching gpio_chip.ngpio
 *
 * AND
 *
 * /sys/class/gpio/chipX/
 *   /export ... export GPIO at given offset
 *   /unexport ... unexport GPIO at given offset
 *   /label ... matching gpio_chip.label
 *   /ngpio ... matching gpio_chip.ngpio
 */

#if IS_ENABLED(CONFIG_GPIO_SYSFS_LEGACY)
static ssize_t base_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	const struct gpiodev_data *data = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", data->gdev->base);
}
static DEVICE_ATTR_RO(base);
#endif /* CONFIG_GPIO_SYSFS_LEGACY */

static ssize_t label_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	const struct gpiodev_data *data = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", data->gdev->label);
}
static DEVICE_ATTR_RO(label);

static ssize_t ngpio_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	const struct gpiodev_data *data = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", data->gdev->ngpio);
}
static DEVICE_ATTR_RO(ngpio);

static int export_gpio_desc(struct gpio_desc *desc)
{
	int offset, ret;

	CLASS(gpio_chip_guard, guard)(desc);
	if (!guard.gc)
		return -ENODEV;

	offset = gpio_chip_hwgpio(desc);
	if (!gpiochip_line_is_valid(guard.gc, offset)) {
		pr_debug_ratelimited("%s: GPIO %d masked\n", __func__,
				     gpio_chip_hwgpio(desc));
		return -EINVAL;
	}

	/*
	 * No extra locking here; GPIOD_FLAG_SYSFS just signifies that the
	 * request and export were done by on behalf of userspace, so
	 * they may be undone on its behalf too.
	 */

	ret = gpiod_request_user(desc, "sysfs");
	if (ret)
		return ret;

	ret = gpiod_set_transitory(desc, false);
	if (ret) {
		gpiod_free(desc);
		return ret;
	}

	ret = gpiod_export(desc, true);
	if (ret < 0) {
		gpiod_free(desc);
	} else {
		set_bit(GPIOD_FLAG_SYSFS, &desc->flags);
		gpiod_line_state_notify(desc, GPIO_V2_LINE_CHANGED_REQUESTED);
	}

	return ret;
}

static int unexport_gpio_desc(struct gpio_desc *desc)
{
	/*
	 * No extra locking here; GPIOD_FLAG_SYSFS just signifies that the
	 * request and export were done by on behalf of userspace, so
	 * they may be undone on its behalf too.
	 */
	if (!test_and_clear_bit(GPIOD_FLAG_SYSFS, &desc->flags))
		return -EINVAL;

	gpiod_unexport(desc);
	gpiod_free(desc);

	return 0;
}

static ssize_t do_chip_export_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, ssize_t size,
				    int (*handler)(struct gpio_desc *desc))
{
	struct gpiodev_data *data = dev_get_drvdata(dev);
	struct gpio_device *gdev = data->gdev;
	struct gpio_desc *desc;
	unsigned int gpio;
	int ret;

	ret = kstrtouint(buf, 0, &gpio);
	if (ret)
		return ret;

	desc = gpio_device_get_desc(gdev, gpio);
	if (IS_ERR(desc))
		return PTR_ERR(desc);

	ret = handler(desc);
	if (ret)
		return ret;

	return size;
}

static ssize_t chip_export_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t size)
{
	return do_chip_export_store(dev, attr, buf, size, export_gpio_desc);
}

static struct device_attribute dev_attr_export = __ATTR(export, 0200, NULL,
							chip_export_store);

static ssize_t chip_unexport_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	return do_chip_export_store(dev, attr, buf, size, unexport_gpio_desc);
}

static struct device_attribute dev_attr_unexport = __ATTR(unexport, 0200,
							  NULL,
							  chip_unexport_store);

#if IS_ENABLED(CONFIG_GPIO_SYSFS_LEGACY)
static struct attribute *gpiochip_attrs[] = {
	&dev_attr_base.attr,
	&dev_attr_label.attr,
	&dev_attr_ngpio.attr,
	NULL,
};
ATTRIBUTE_GROUPS(gpiochip);
#endif /* CONFIG_GPIO_SYSFS_LEGACY */

static struct attribute *gpiochip_ext_attrs[] = {
	&dev_attr_label.attr,
	&dev_attr_ngpio.attr,
	&dev_attr_export.attr,
	&dev_attr_unexport.attr,
	NULL
};
ATTRIBUTE_GROUPS(gpiochip_ext);

#if IS_ENABLED(CONFIG_GPIO_SYSFS_LEGACY)
/*
 * /sys/class/gpio/export ... write-only
 *	integer N ... number of GPIO to export (full access)
 * /sys/class/gpio/unexport ... write-only
 *	integer N ... number of GPIO to unexport
 */
static ssize_t export_store(const struct class *class,
			    const struct class_attribute *attr,
			    const char *buf, size_t len)
{
	struct gpio_desc *desc;
	int status;
	long gpio;

	status = kstrtol(buf, 0, &gpio);
	if (status)
		return status;

	desc = gpio_to_desc(gpio);
	/* reject invalid GPIOs */
	if (!desc) {
		pr_debug_ratelimited("%s: invalid GPIO %ld\n", __func__, gpio);
		return -EINVAL;
	}

	status = export_gpio_desc(desc);
	if (status)
		pr_debug("%s: status %d\n", __func__, status);
	return status ? : len;
}
static CLASS_ATTR_WO(export);

static ssize_t unexport_store(const struct class *class,
			      const struct class_attribute *attr,
			      const char *buf, size_t len)
{
	struct gpio_desc *desc;
	int status;
	long gpio;

	status = kstrtol(buf, 0, &gpio);
	if (status < 0)
		return status;

	desc = gpio_to_desc(gpio);
	/* reject bogus commands (gpiod_unexport() ignores them) */
	if (!desc) {
		pr_debug_ratelimited("%s: invalid GPIO %ld\n", __func__, gpio);
		return -EINVAL;
	}

	status = unexport_gpio_desc(desc);
	if (status)
		pr_debug("%s: status %d\n", __func__, status);
	return status ? : len;
}
static CLASS_ATTR_WO(unexport);

static struct attribute *gpio_class_attrs[] = {
	&class_attr_export.attr,
	&class_attr_unexport.attr,
	NULL,
};
ATTRIBUTE_GROUPS(gpio_class);
#endif /* CONFIG_GPIO_SYSFS_LEGACY */

static const struct class gpio_class = {
	.name =		"gpio",
#if IS_ENABLED(CONFIG_GPIO_SYSFS_LEGACY)
	.class_groups =	gpio_class_groups,
#endif /* CONFIG_GPIO_SYSFS_LEGACY */
};

static int match_gdev(struct device *dev, const void *desc)
{
	struct gpiodev_data *data = dev_get_drvdata(dev);
	const struct gpio_device *gdev = desc;

	return data && data->gdev == gdev;
}

static struct gpiodev_data *
gdev_get_data(struct gpio_device *gdev) __must_hold(&sysfs_lock)
{
	/*
	 * Find the first device in GPIO class that matches. Whether that's
	 * the one indexed by GPIO base or device ID doesn't matter, it has
	 * the same address set as driver data.
	 */
	struct device *cdev __free(put_device) = class_find_device(&gpio_class,
								   NULL, gdev,
								   match_gdev);
	if (!cdev)
		return NULL;

	return dev_get_drvdata(cdev);
};

static void gpiod_attr_init(struct device_attribute *dev_attr, const char *name,
			    ssize_t (*show)(struct device *dev,
					    struct device_attribute *attr,
					    char *buf),
			    ssize_t (*store)(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t count))
{
	sysfs_attr_init(&dev_attr->attr);
	dev_attr->attr.name = name;
	dev_attr->attr.mode = 0644;
	dev_attr->show = show;
	dev_attr->store = store;
}

/**
 * gpiod_export - export a GPIO through sysfs
 * @desc: GPIO to make available, already requested
 * @direction_may_change: true if userspace may change GPIO direction
 * Context: arch_initcall or later
 *
 * When drivers want to make a GPIO accessible to userspace after they
 * have requested it -- perhaps while debugging, or as part of their
 * public interface -- they may use this routine.  If the GPIO can
 * change direction (some can't) and the caller allows it, userspace
 * will see "direction" sysfs attribute which may be used to change
 * the gpio's direction.  A "value" attribute will always be provided.
 *
 * Returns:
 * 0 on success, or negative errno on failure.
 */
int gpiod_export(struct gpio_desc *desc, bool direction_may_change)
{
	char *path __free(kfree) = NULL;
	struct gpiodev_data *gdev_data;
	struct gpiod_data *desc_data;
	struct gpio_device *gdev;
	struct attribute **attrs;
	int status;

	/* can't export until sysfs is available ... */
	if (!class_is_registered(&gpio_class)) {
		pr_debug("%s: called too early!\n", __func__);
		return -ENOENT;
	}

	if (!desc) {
		pr_debug("%s: invalid gpio descriptor\n", __func__);
		return -EINVAL;
	}

	CLASS(gpio_chip_guard, guard)(desc);
	if (!guard.gc)
		return -ENODEV;

	if (test_and_set_bit(GPIOD_FLAG_EXPORT, &desc->flags))
		return -EPERM;

	gdev = desc->gdev;

	guard(mutex)(&sysfs_lock);

	if (!test_bit(GPIOD_FLAG_REQUESTED, &desc->flags)) {
		gpiod_dbg(desc, "%s: unavailable (not requested)\n", __func__);
		status = -EPERM;
		goto err_clear_bit;
	}

	desc_data = kzalloc(sizeof(*desc_data), GFP_KERNEL);
	if (!desc_data) {
		status = -ENOMEM;
		goto err_clear_bit;
	}

	desc_data->desc = desc;
	mutex_init(&desc_data->mutex);
	if (guard.gc->direction_input && guard.gc->direction_output)
		desc_data->direction_can_change = direction_may_change;
	else
		desc_data->direction_can_change = false;

	gpiod_attr_init(&desc_data->dir_attr, "direction",
			direction_show, direction_store);
	gpiod_attr_init(&desc_data->val_attr, "value", value_show, value_store);

#if IS_ENABLED(CONFIG_GPIO_SYSFS_LEGACY)
	gpiod_attr_init(&desc_data->edge_attr, "edge", edge_show, edge_store);
	gpiod_attr_init(&desc_data->active_low_attr, "active_low",
			active_low_show, active_low_store);

	attrs = desc_data->class_attrs;
	desc_data->class_attr_group.is_visible = gpio_is_visible;
	attrs[GPIO_SYSFS_LINE_CLASS_ATTR_DIRECTION] = &desc_data->dir_attr.attr;
	attrs[GPIO_SYSFS_LINE_CLASS_ATTR_VALUE] = &desc_data->val_attr.attr;
	attrs[GPIO_SYSFS_LINE_CLASS_ATTR_EDGE] = &desc_data->edge_attr.attr;
	attrs[GPIO_SYSFS_LINE_CLASS_ATTR_ACTIVE_LOW] = &desc_data->active_low_attr.attr;

	desc_data->class_attr_group.attrs = desc_data->class_attrs;
	desc_data->class_attr_groups[0] = &desc_data->class_attr_group;

	/*
	 * Note: we need to continue passing desc_data here as there's still
	 * at least one known user of gpiod_export_link() in the tree. This
	 * function still uses class_find_device() internally.
	 */
	desc_data->dev = device_create_with_groups(&gpio_class, &gdev->dev,
						   MKDEV(0, 0), desc_data,
						   desc_data->class_attr_groups,
						   "gpio%u",
						   desc_to_gpio(desc));
	if (IS_ERR(desc_data->dev)) {
		status = PTR_ERR(desc_data->dev);
		goto err_free_data;
	}

	desc_data->value_kn = sysfs_get_dirent(desc_data->dev->kobj.sd,
						       "value");
	if (!desc_data->value_kn) {
		status = -ENODEV;
		goto err_unregister_device;
	}
#endif /* CONFIG_GPIO_SYSFS_LEGACY */

	gdev_data = gdev_get_data(gdev);
	if (!gdev_data) {
		status = -ENODEV;
		goto err_put_dirent;
	}

	desc_data->chip_attr_group.name = kasprintf(GFP_KERNEL, "gpio%u",
						    gpio_chip_hwgpio(desc));
	if (!desc_data->chip_attr_group.name) {
		status = -ENOMEM;
		goto err_put_dirent;
	}

	attrs = desc_data->chip_attrs;
	desc_data->chip_attr_group.is_visible = gpio_is_visible;
	attrs[GPIO_SYSFS_LINE_CHIP_ATTR_DIRECTION] = &desc_data->dir_attr.attr;
	attrs[GPIO_SYSFS_LINE_CHIP_ATTR_VALUE] = &desc_data->val_attr.attr;

	desc_data->chip_attr_group.attrs = attrs;
	desc_data->chip_attr_groups[0] = &desc_data->chip_attr_group;

	desc_data->parent = &gdev_data->cdev_id->kobj;
	status = sysfs_create_groups(desc_data->parent,
				     desc_data->chip_attr_groups);
	if (status)
		goto err_free_name;

	path = kasprintf(GFP_KERNEL, "gpio%u/value", gpio_chip_hwgpio(desc));
	if (!path) {
		status = -ENOMEM;
		goto err_remove_groups;
	}

	list_add(&desc_data->list, &gdev_data->exported_lines);

	return 0;

err_remove_groups:
	sysfs_remove_groups(desc_data->parent, desc_data->chip_attr_groups);
err_free_name:
	kfree(desc_data->chip_attr_group.name);
err_put_dirent:
#if IS_ENABLED(CONFIG_GPIO_SYSFS_LEGACY)
	sysfs_put(desc_data->value_kn);
err_unregister_device:
	device_unregister(desc_data->dev);
err_free_data:
#endif /* CONFIG_GPIO_SYSFS_LEGACY */
	kfree(desc_data);
err_clear_bit:
	clear_bit(GPIOD_FLAG_EXPORT, &desc->flags);
	gpiod_dbg(desc, "%s: status %d\n", __func__, status);
	return status;
}
EXPORT_SYMBOL_GPL(gpiod_export);

#if IS_ENABLED(CONFIG_GPIO_SYSFS_LEGACY)
static int match_export(struct device *dev, const void *desc)
{
	struct gpiod_data *data = dev_get_drvdata(dev);

	return gpiod_is_equal(data->desc, desc);
}
#endif /* CONFIG_GPIO_SYSFS_LEGACY */

/**
 * gpiod_export_link - create a sysfs link to an exported GPIO node
 * @dev: device under which to create symlink
 * @name: name of the symlink
 * @desc: GPIO to create symlink to, already exported
 *
 * Set up a symlink from /sys/.../dev/name to /sys/class/gpio/gpioN
 * node. Caller is responsible for unlinking.
 *
 * Returns:
 * 0 on success, or negative errno on failure.
 */
int gpiod_export_link(struct device *dev, const char *name,
		      struct gpio_desc *desc)
{
#if IS_ENABLED(CONFIG_GPIO_SYSFS_LEGACY)
	struct device *cdev;
	int ret;

	if (!desc) {
		pr_warn("%s: invalid GPIO\n", __func__);
		return -EINVAL;
	}

	cdev = class_find_device(&gpio_class, NULL, desc, match_export);
	if (!cdev)
		return -ENODEV;

	ret = sysfs_create_link(&dev->kobj, &cdev->kobj, name);
	put_device(cdev);

	return ret;
#else
	return -EOPNOTSUPP;
#endif /* CONFIG_GPIO_SYSFS_LEGACY */
}
EXPORT_SYMBOL_GPL(gpiod_export_link);

/**
 * gpiod_unexport - reverse effect of gpiod_export()
 * @desc: GPIO to make unavailable
 *
 * This is implicit on gpiod_free().
 */
void gpiod_unexport(struct gpio_desc *desc)
{
	struct gpiod_data *tmp, *desc_data = NULL;
	struct gpiodev_data *gdev_data;
	struct gpio_device *gdev;

	if (!desc) {
		pr_warn("%s: invalid GPIO\n", __func__);
		return;
	}

	scoped_guard(mutex, &sysfs_lock) {
		if (!test_bit(GPIOD_FLAG_EXPORT, &desc->flags))
			return;

		gdev = gpiod_to_gpio_device(desc);
		gdev_data = gdev_get_data(gdev);
		if (!gdev_data)
			return;

		list_for_each_entry(tmp, &gdev_data->exported_lines, list) {
			if (gpiod_is_equal(desc, tmp->desc)) {
				desc_data = tmp;
				break;
			}
		}

		if (!desc_data)
			return;

		list_del(&desc_data->list);
		clear_bit(GPIOD_FLAG_EXPORT, &desc->flags);
#if IS_ENABLED(CONFIG_GPIO_SYSFS_LEGACY)
		sysfs_put(desc_data->value_kn);
		device_unregister(desc_data->dev);

		/*
		 * Release irq after deregistration to prevent race with
		 * edge_store.
		 */
		if (desc_data->irq_flags)
			gpio_sysfs_free_irq(desc_data);
#endif /* CONFIG_GPIO_SYSFS_LEGACY */

		sysfs_remove_groups(desc_data->parent,
				    desc_data->chip_attr_groups);
	}

	mutex_destroy(&desc_data->mutex);
	kfree(desc_data);
}
EXPORT_SYMBOL_GPL(gpiod_unexport);

int gpiochip_sysfs_register(struct gpio_device *gdev)
{
	struct gpiodev_data *data;
	struct gpio_chip *chip;
	struct device *parent;
	int err;

	/*
	 * Many systems add gpio chips for SOC support very early,
	 * before driver model support is available.  In those cases we
	 * register later, in gpiolib_sysfs_init() ... here we just
	 * verify that _some_ field of gpio_class got initialized.
	 */
	if (!class_is_registered(&gpio_class))
		return 0;

	guard(srcu)(&gdev->srcu);

	chip = srcu_dereference(gdev->chip, &gdev->srcu);
	if (!chip)
		return -ENODEV;

	/*
	 * For sysfs backward compatibility we need to preserve this
	 * preferred parenting to the gpio_chip parent field, if set.
	 */
	if (chip->parent)
		parent = chip->parent;
	else
		parent = &gdev->dev;

	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->gdev = gdev;
	INIT_LIST_HEAD(&data->exported_lines);

	guard(mutex)(&sysfs_lock);

#if IS_ENABLED(CONFIG_GPIO_SYSFS_LEGACY)
	/* use chip->base for the ID; it's already known to be unique */
	data->cdev_base = device_create_with_groups(&gpio_class, parent,
						    MKDEV(0, 0), data,
						    gpiochip_groups,
						    GPIOCHIP_NAME "%d",
						    chip->base);
	if (IS_ERR(data->cdev_base)) {
		err = PTR_ERR(data->cdev_base);
		kfree(data);
		return err;
	}
#endif /* CONFIG_GPIO_SYSFS_LEGACY */

	data->cdev_id = device_create_with_groups(&gpio_class, parent,
						  MKDEV(0, 0), data,
						  gpiochip_ext_groups,
						  "chip%d", gdev->id);
	if (IS_ERR(data->cdev_id)) {
#if IS_ENABLED(CONFIG_GPIO_SYSFS_LEGACY)
		device_unregister(data->cdev_base);
#endif /* CONFIG_GPIO_SYSFS_LEGACY */
		err = PTR_ERR(data->cdev_id);
		kfree(data);
		return err;
	}

	return 0;
}

void gpiochip_sysfs_unregister(struct gpio_device *gdev)
{
	struct gpiodev_data *data;
	struct gpio_desc *desc;
	struct gpio_chip *chip;

	scoped_guard(mutex, &sysfs_lock) {
		data = gdev_get_data(gdev);
		if (!data)
			return;

#if IS_ENABLED(CONFIG_GPIO_SYSFS_LEGACY)
		device_unregister(data->cdev_base);
#endif /* CONFIG_GPIO_SYSFS_LEGACY */
		device_unregister(data->cdev_id);
		kfree(data);
	}

	guard(srcu)(&gdev->srcu);

	chip = srcu_dereference(gdev->chip, &gdev->srcu);
	if (!chip)
		return;

	/* unregister gpiod class devices owned by sysfs */
	for_each_gpio_desc_with_flag(chip, desc, GPIOD_FLAG_SYSFS) {
		gpiod_unexport(desc);
		gpiod_free(desc);
	}
}

/*
 * We're not really looking for a device - we just want to iterate over the
 * list and call this callback for each GPIO device. This is why this function
 * always returns 0.
 */
static int gpiofind_sysfs_register(struct gpio_chip *gc, const void *data)
{
	struct gpio_device *gdev = gc->gpiodev;
	int ret;

	ret = gpiochip_sysfs_register(gdev);
	if (ret)
		chip_err(gc, "failed to register the sysfs entry: %d\n", ret);

	return 0;
}

static int __init gpiolib_sysfs_init(void)
{
	int status;

	status = class_register(&gpio_class);
	if (status < 0)
		return status;

	/* Scan and register the gpio_chips which registered very
	 * early (e.g. before the class_register above was called).
	 *
	 * We run before arch_initcall() so chip->dev nodes can have
	 * registered, and so arch_initcall() can always gpiod_export().
	 */
	(void)gpio_device_find(NULL, gpiofind_sysfs_register);

	return 0;
}
postcore_initcall(gpiolib_sysfs_init);
