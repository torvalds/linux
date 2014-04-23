#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/idr.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/gpio/driver.h>

#include "gpiolib.h"

#define CREATE_TRACE_POINTS
#include <trace/events/gpio.h>

/* Implementation infrastructure for GPIO interfaces.
 *
 * The GPIO programming interface allows for inlining speed-critical
 * get/set operations for common cases, so that access to SOC-integrated
 * GPIOs can sometimes cost only an instruction or two per bit.
 */


/* When debugging, extend minimal trust to callers and platform code.
 * Also emit diagnostic messages that may help initial bringup, when
 * board setup or driver bugs are most common.
 *
 * Otherwise, minimize overhead in what may be bitbanging codepaths.
 */
#ifdef	DEBUG
#define	extra_checks	1
#else
#define	extra_checks	0
#endif

/* gpio_lock prevents conflicts during gpio_desc[] table updates.
 * While any GPIO is requested, its gpio_chip is not removable;
 * each GPIO's "requested" flag serves as a lock and refcount.
 */
static DEFINE_SPINLOCK(gpio_lock);

struct gpio_desc {
	struct gpio_chip	*chip;
	unsigned long		flags;
/* flag symbols are bit numbers */
#define FLAG_REQUESTED	0
#define FLAG_IS_OUT	1
#define FLAG_EXPORT	2	/* protected by sysfs_lock */
#define FLAG_SYSFS	3	/* exported via /sys/class/gpio/control */
#define FLAG_TRIG_FALL	4	/* trigger on falling edge */
#define FLAG_TRIG_RISE	5	/* trigger on rising edge */
#define FLAG_ACTIVE_LOW	6	/* value has active low */
#define FLAG_OPEN_DRAIN	7	/* Gpio is open drain type */
#define FLAG_OPEN_SOURCE 8	/* Gpio is open source type */
#define FLAG_USED_AS_IRQ 9	/* GPIO is connected to an IRQ */

#define ID_SHIFT	16	/* add new flags before this one */

#define GPIO_FLAGS_MASK		((1 << ID_SHIFT) - 1)
#define GPIO_TRIGGER_MASK	(BIT(FLAG_TRIG_FALL) | BIT(FLAG_TRIG_RISE))

#ifdef CONFIG_DEBUG_FS
	const char		*label;
#endif
};
static struct gpio_desc gpio_desc[ARCH_NR_GPIOS];

#define GPIO_OFFSET_VALID(chip, offset) (offset >= 0 && offset < chip->ngpio)

static DEFINE_MUTEX(gpio_lookup_lock);
static LIST_HEAD(gpio_lookup_list);
static LIST_HEAD(gpio_chips);

#ifdef CONFIG_GPIO_SYSFS
static DEFINE_IDR(dirent_idr);
#endif

static int gpiod_request(struct gpio_desc *desc, const char *label);
static void gpiod_free(struct gpio_desc *desc);

/* With descriptor prefix */

#ifdef CONFIG_DEBUG_FS
#define gpiod_emerg(desc, fmt, ...)					       \
	pr_emerg("gpio-%d (%s): " fmt, desc_to_gpio(desc), desc->label ? : "?",\
                 ##__VA_ARGS__)
#define gpiod_crit(desc, fmt, ...)					       \
	pr_crit("gpio-%d (%s): " fmt, desc_to_gpio(desc), desc->label ? : "?", \
                 ##__VA_ARGS__)
#define gpiod_err(desc, fmt, ...)					       \
	pr_err("gpio-%d (%s): " fmt, desc_to_gpio(desc), desc->label ? : "?",  \
                 ##__VA_ARGS__)
#define gpiod_warn(desc, fmt, ...)					       \
	pr_warn("gpio-%d (%s): " fmt, desc_to_gpio(desc), desc->label ? : "?", \
                 ##__VA_ARGS__)
#define gpiod_info(desc, fmt, ...)					       \
	pr_info("gpio-%d (%s): " fmt, desc_to_gpio(desc), desc->label ? : "?", \
                ##__VA_ARGS__)
#define gpiod_dbg(desc, fmt, ...)					       \
	pr_debug("gpio-%d (%s): " fmt, desc_to_gpio(desc), desc->label ? : "?",\
                 ##__VA_ARGS__)
#else
#define gpiod_emerg(desc, fmt, ...)					\
	pr_emerg("gpio-%d: " fmt, desc_to_gpio(desc), ##__VA_ARGS__)
#define gpiod_crit(desc, fmt, ...)					\
	pr_crit("gpio-%d: " fmt, desc_to_gpio(desc), ##__VA_ARGS__)
#define gpiod_err(desc, fmt, ...)					\
	pr_err("gpio-%d: " fmt, desc_to_gpio(desc), ##__VA_ARGS__)
#define gpiod_warn(desc, fmt, ...)					\
	pr_warn("gpio-%d: " fmt, desc_to_gpio(desc), ##__VA_ARGS__)
#define gpiod_info(desc, fmt, ...)					\
	pr_info("gpio-%d: " fmt, desc_to_gpio(desc), ##__VA_ARGS__)
#define gpiod_dbg(desc, fmt, ...)					\
	pr_debug("gpio-%d: " fmt, desc_to_gpio(desc), ##__VA_ARGS__)
#endif

/* With chip prefix */

#define chip_emerg(chip, fmt, ...)					\
	pr_emerg("GPIO chip %s: " fmt, chip->label, ##__VA_ARGS__)
#define chip_crit(chip, fmt, ...)					\
	pr_crit("GPIO chip %s: " fmt, chip->label, ##__VA_ARGS__)
#define chip_err(chip, fmt, ...)					\
	pr_err("GPIO chip %s: " fmt, chip->label, ##__VA_ARGS__)
#define chip_warn(chip, fmt, ...)					\
	pr_warn("GPIO chip %s: " fmt, chip->label, ##__VA_ARGS__)
#define chip_info(chip, fmt, ...)					\
	pr_info("GPIO chip %s: " fmt, chip->label, ##__VA_ARGS__)
#define chip_dbg(chip, fmt, ...)					\
	pr_debug("GPIO chip %s: " fmt, chip->label, ##__VA_ARGS__)

static inline void desc_set_label(struct gpio_desc *d, const char *label)
{
#ifdef CONFIG_DEBUG_FS
	d->label = label;
#endif
}

/*
 * Return the GPIO number of the passed descriptor relative to its chip
 */
static int gpio_chip_hwgpio(const struct gpio_desc *desc)
{
	return desc - &desc->chip->desc[0];
}

/**
 * Convert a GPIO number to its descriptor
 */
struct gpio_desc *gpio_to_desc(unsigned gpio)
{
	if (WARN(!gpio_is_valid(gpio), "invalid GPIO %d\n", gpio))
		return NULL;
	else
		return &gpio_desc[gpio];
}
EXPORT_SYMBOL_GPL(gpio_to_desc);

/**
 * Get the GPIO descriptor corresponding to the given hw number for this chip.
 */
struct gpio_desc *gpiochip_get_desc(struct gpio_chip *chip,
				    u16 hwnum)
{
	if (hwnum >= chip->ngpio)
		return ERR_PTR(-EINVAL);

	return &chip->desc[hwnum];
}
EXPORT_SYMBOL_GPL(gpiochip_get_desc);

/**
 * Convert a GPIO descriptor to the integer namespace.
 * This should disappear in the future but is needed since we still
 * use GPIO numbers for error messages and sysfs nodes
 */
int desc_to_gpio(const struct gpio_desc *desc)
{
	return desc - &gpio_desc[0];
}
EXPORT_SYMBOL_GPL(desc_to_gpio);


/* Warn when drivers omit gpio_request() calls -- legal but ill-advised
 * when setting direction, and otherwise illegal.  Until board setup code
 * and drivers use explicit requests everywhere (which won't happen when
 * those calls have no teeth) we can't avoid autorequesting.  This nag
 * message should motivate switching to explicit requests... so should
 * the weaker cleanup after faults, compared to gpio_request().
 *
 * NOTE: the autorequest mechanism is going away; at this point it's
 * only "legal" in the sense that (old) code using it won't break yet,
 * but instead only triggers a WARN() stack dump.
 */
static int gpio_ensure_requested(struct gpio_desc *desc)
{
	const struct gpio_chip *chip = desc->chip;
	const int gpio = desc_to_gpio(desc);

	if (WARN(test_and_set_bit(FLAG_REQUESTED, &desc->flags) == 0,
			"autorequest GPIO-%d\n", gpio)) {
		if (!try_module_get(chip->owner)) {
			gpiod_err(desc, "%s: module can't be gotten\n",
					__func__);
			clear_bit(FLAG_REQUESTED, &desc->flags);
			/* lose */
			return -EIO;
		}
		desc_set_label(desc, "[auto]");
		/* caller must chip->request() w/o spinlock */
		if (chip->request)
			return 1;
	}
	return 0;
}

/**
 * gpiod_to_chip - Return the GPIO chip to which a GPIO descriptor belongs
 * @desc:	descriptor to return the chip of
 */
struct gpio_chip *gpiod_to_chip(const struct gpio_desc *desc)
{
	return desc ? desc->chip : NULL;
}
EXPORT_SYMBOL_GPL(gpiod_to_chip);

/* dynamic allocation of GPIOs, e.g. on a hotplugged device */
static int gpiochip_find_base(int ngpio)
{
	struct gpio_chip *chip;
	int base = ARCH_NR_GPIOS - ngpio;

	list_for_each_entry_reverse(chip, &gpio_chips, list) {
		/* found a free space? */
		if (chip->base + chip->ngpio <= base)
			break;
		else
			/* nope, check the space right before the chip */
			base = chip->base - ngpio;
	}

	if (gpio_is_valid(base)) {
		pr_debug("%s: found new base at %d\n", __func__, base);
		return base;
	} else {
		pr_err("%s: cannot find free range\n", __func__);
		return -ENOSPC;
	}
}

/**
 * gpiod_get_direction - return the current direction of a GPIO
 * @desc:	GPIO to get the direction of
 *
 * Return GPIOF_DIR_IN or GPIOF_DIR_OUT, or an error code in case of error.
 *
 * This function may sleep if gpiod_cansleep() is true.
 */
int gpiod_get_direction(const struct gpio_desc *desc)
{
	struct gpio_chip	*chip;
	unsigned		offset;
	int			status = -EINVAL;

	chip = gpiod_to_chip(desc);
	offset = gpio_chip_hwgpio(desc);

	if (!chip->get_direction)
		return status;

	status = chip->get_direction(chip, offset);
	if (status > 0) {
		/* GPIOF_DIR_IN, or other positive */
		status = 1;
		/* FLAG_IS_OUT is just a cache of the result of get_direction(),
		 * so it does not affect constness per se */
		clear_bit(FLAG_IS_OUT, &((struct gpio_desc *)desc)->flags);
	}
	if (status == 0) {
		/* GPIOF_DIR_OUT */
		set_bit(FLAG_IS_OUT, &((struct gpio_desc *)desc)->flags);
	}
	return status;
}
EXPORT_SYMBOL_GPL(gpiod_get_direction);

#ifdef CONFIG_GPIO_SYSFS

/* lock protects against unexport_gpio() being called while
 * sysfs files are active.
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

static ssize_t gpio_direction_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	const struct gpio_desc	*desc = dev_get_drvdata(dev);
	ssize_t			status;

	mutex_lock(&sysfs_lock);

	if (!test_bit(FLAG_EXPORT, &desc->flags)) {
		status = -EIO;
	} else {
		gpiod_get_direction(desc);
		status = sprintf(buf, "%s\n",
			test_bit(FLAG_IS_OUT, &desc->flags)
				? "out" : "in");
	}

	mutex_unlock(&sysfs_lock);
	return status;
}

static ssize_t gpio_direction_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct gpio_desc	*desc = dev_get_drvdata(dev);
	ssize_t			status;

	mutex_lock(&sysfs_lock);

	if (!test_bit(FLAG_EXPORT, &desc->flags))
		status = -EIO;
	else if (sysfs_streq(buf, "high"))
		status = gpiod_direction_output_raw(desc, 1);
	else if (sysfs_streq(buf, "out") || sysfs_streq(buf, "low"))
		status = gpiod_direction_output_raw(desc, 0);
	else if (sysfs_streq(buf, "in"))
		status = gpiod_direction_input(desc);
	else
		status = -EINVAL;

	mutex_unlock(&sysfs_lock);
	return status ? : size;
}

static /* const */ DEVICE_ATTR(direction, 0644,
		gpio_direction_show, gpio_direction_store);

static ssize_t gpio_value_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct gpio_desc	*desc = dev_get_drvdata(dev);
	ssize_t			status;

	mutex_lock(&sysfs_lock);

	if (!test_bit(FLAG_EXPORT, &desc->flags))
		status = -EIO;
	else
		status = sprintf(buf, "%d\n", gpiod_get_value_cansleep(desc));

	mutex_unlock(&sysfs_lock);
	return status;
}

static ssize_t gpio_value_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct gpio_desc	*desc = dev_get_drvdata(dev);
	ssize_t			status;

	mutex_lock(&sysfs_lock);

	if (!test_bit(FLAG_EXPORT, &desc->flags))
		status = -EIO;
	else if (!test_bit(FLAG_IS_OUT, &desc->flags))
		status = -EPERM;
	else {
		long		value;

		status = kstrtol(buf, 0, &value);
		if (status == 0) {
			gpiod_set_value_cansleep(desc, value);
			status = size;
		}
	}

	mutex_unlock(&sysfs_lock);
	return status;
}

static const DEVICE_ATTR(value, 0644,
		gpio_value_show, gpio_value_store);

static irqreturn_t gpio_sysfs_irq(int irq, void *priv)
{
	struct kernfs_node	*value_sd = priv;

	sysfs_notify_dirent(value_sd);
	return IRQ_HANDLED;
}

static int gpio_setup_irq(struct gpio_desc *desc, struct device *dev,
		unsigned long gpio_flags)
{
	struct kernfs_node	*value_sd;
	unsigned long		irq_flags;
	int			ret, irq, id;

	if ((desc->flags & GPIO_TRIGGER_MASK) == gpio_flags)
		return 0;

	irq = gpiod_to_irq(desc);
	if (irq < 0)
		return -EIO;

	id = desc->flags >> ID_SHIFT;
	value_sd = idr_find(&dirent_idr, id);
	if (value_sd)
		free_irq(irq, value_sd);

	desc->flags &= ~GPIO_TRIGGER_MASK;

	if (!gpio_flags) {
		gpiod_unlock_as_irq(desc);
		ret = 0;
		goto free_id;
	}

	irq_flags = IRQF_SHARED;
	if (test_bit(FLAG_TRIG_FALL, &gpio_flags))
		irq_flags |= test_bit(FLAG_ACTIVE_LOW, &desc->flags) ?
			IRQF_TRIGGER_RISING : IRQF_TRIGGER_FALLING;
	if (test_bit(FLAG_TRIG_RISE, &gpio_flags))
		irq_flags |= test_bit(FLAG_ACTIVE_LOW, &desc->flags) ?
			IRQF_TRIGGER_FALLING : IRQF_TRIGGER_RISING;

	if (!value_sd) {
		value_sd = sysfs_get_dirent(dev->kobj.sd, "value");
		if (!value_sd) {
			ret = -ENODEV;
			goto err_out;
		}

		ret = idr_alloc(&dirent_idr, value_sd, 1, 0, GFP_KERNEL);
		if (ret < 0)
			goto free_sd;
		id = ret;

		desc->flags &= GPIO_FLAGS_MASK;
		desc->flags |= (unsigned long)id << ID_SHIFT;

		if (desc->flags >> ID_SHIFT != id) {
			ret = -ERANGE;
			goto free_id;
		}
	}

	ret = request_any_context_irq(irq, gpio_sysfs_irq, irq_flags,
				"gpiolib", value_sd);
	if (ret < 0)
		goto free_id;

	ret = gpiod_lock_as_irq(desc);
	if (ret < 0) {
		gpiod_warn(desc, "failed to flag the GPIO for IRQ\n");
		goto free_id;
	}

	desc->flags |= gpio_flags;
	return 0;

free_id:
	idr_remove(&dirent_idr, id);
	desc->flags &= GPIO_FLAGS_MASK;
free_sd:
	if (value_sd)
		sysfs_put(value_sd);
err_out:
	return ret;
}

static const struct {
	const char *name;
	unsigned long flags;
} trigger_types[] = {
	{ "none",    0 },
	{ "falling", BIT(FLAG_TRIG_FALL) },
	{ "rising",  BIT(FLAG_TRIG_RISE) },
	{ "both",    BIT(FLAG_TRIG_FALL) | BIT(FLAG_TRIG_RISE) },
};

static ssize_t gpio_edge_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	const struct gpio_desc	*desc = dev_get_drvdata(dev);
	ssize_t			status;

	mutex_lock(&sysfs_lock);

	if (!test_bit(FLAG_EXPORT, &desc->flags))
		status = -EIO;
	else {
		int i;

		status = 0;
		for (i = 0; i < ARRAY_SIZE(trigger_types); i++)
			if ((desc->flags & GPIO_TRIGGER_MASK)
					== trigger_types[i].flags) {
				status = sprintf(buf, "%s\n",
						 trigger_types[i].name);
				break;
			}
	}

	mutex_unlock(&sysfs_lock);
	return status;
}

static ssize_t gpio_edge_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct gpio_desc	*desc = dev_get_drvdata(dev);
	ssize_t			status;
	int			i;

	for (i = 0; i < ARRAY_SIZE(trigger_types); i++)
		if (sysfs_streq(trigger_types[i].name, buf))
			goto found;
	return -EINVAL;

found:
	mutex_lock(&sysfs_lock);

	if (!test_bit(FLAG_EXPORT, &desc->flags))
		status = -EIO;
	else {
		status = gpio_setup_irq(desc, dev, trigger_types[i].flags);
		if (!status)
			status = size;
	}

	mutex_unlock(&sysfs_lock);

	return status;
}

static DEVICE_ATTR(edge, 0644, gpio_edge_show, gpio_edge_store);

static int sysfs_set_active_low(struct gpio_desc *desc, struct device *dev,
				int value)
{
	int			status = 0;

	if (!!test_bit(FLAG_ACTIVE_LOW, &desc->flags) == !!value)
		return 0;

	if (value)
		set_bit(FLAG_ACTIVE_LOW, &desc->flags);
	else
		clear_bit(FLAG_ACTIVE_LOW, &desc->flags);

	/* reconfigure poll(2) support if enabled on one edge only */
	if (dev != NULL && (!!test_bit(FLAG_TRIG_RISE, &desc->flags) ^
				!!test_bit(FLAG_TRIG_FALL, &desc->flags))) {
		unsigned long trigger_flags = desc->flags & GPIO_TRIGGER_MASK;

		gpio_setup_irq(desc, dev, 0);
		status = gpio_setup_irq(desc, dev, trigger_flags);
	}

	return status;
}

static ssize_t gpio_active_low_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	const struct gpio_desc	*desc = dev_get_drvdata(dev);
	ssize_t			status;

	mutex_lock(&sysfs_lock);

	if (!test_bit(FLAG_EXPORT, &desc->flags))
		status = -EIO;
	else
		status = sprintf(buf, "%d\n",
				!!test_bit(FLAG_ACTIVE_LOW, &desc->flags));

	mutex_unlock(&sysfs_lock);

	return status;
}

static ssize_t gpio_active_low_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct gpio_desc	*desc = dev_get_drvdata(dev);
	ssize_t			status;

	mutex_lock(&sysfs_lock);

	if (!test_bit(FLAG_EXPORT, &desc->flags)) {
		status = -EIO;
	} else {
		long		value;

		status = kstrtol(buf, 0, &value);
		if (status == 0)
			status = sysfs_set_active_low(desc, dev, value != 0);
	}

	mutex_unlock(&sysfs_lock);

	return status ? : size;
}

static const DEVICE_ATTR(active_low, 0644,
		gpio_active_low_show, gpio_active_low_store);

static const struct attribute *gpio_attrs[] = {
	&dev_attr_value.attr,
	&dev_attr_active_low.attr,
	NULL,
};

static const struct attribute_group gpio_attr_group = {
	.attrs = (struct attribute **) gpio_attrs,
};

/*
 * /sys/class/gpio/gpiochipN/
 *   /base ... matching gpio_chip.base (N)
 *   /label ... matching gpio_chip.label
 *   /ngpio ... matching gpio_chip.ngpio
 */

static ssize_t chip_base_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	const struct gpio_chip	*chip = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", chip->base);
}
static DEVICE_ATTR(base, 0444, chip_base_show, NULL);

static ssize_t chip_label_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	const struct gpio_chip	*chip = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", chip->label ? : "");
}
static DEVICE_ATTR(label, 0444, chip_label_show, NULL);

static ssize_t chip_ngpio_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	const struct gpio_chip	*chip = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", chip->ngpio);
}
static DEVICE_ATTR(ngpio, 0444, chip_ngpio_show, NULL);

static const struct attribute *gpiochip_attrs[] = {
	&dev_attr_base.attr,
	&dev_attr_label.attr,
	&dev_attr_ngpio.attr,
	NULL,
};

static const struct attribute_group gpiochip_attr_group = {
	.attrs = (struct attribute **) gpiochip_attrs,
};

/*
 * /sys/class/gpio/export ... write-only
 *	integer N ... number of GPIO to export (full access)
 * /sys/class/gpio/unexport ... write-only
 *	integer N ... number of GPIO to unexport
 */
static ssize_t export_store(struct class *class,
				struct class_attribute *attr,
				const char *buf, size_t len)
{
	long			gpio;
	struct gpio_desc	*desc;
	int			status;

	status = kstrtol(buf, 0, &gpio);
	if (status < 0)
		goto done;

	desc = gpio_to_desc(gpio);
	/* reject invalid GPIOs */
	if (!desc) {
		pr_warn("%s: invalid GPIO %ld\n", __func__, gpio);
		return -EINVAL;
	}

	/* No extra locking here; FLAG_SYSFS just signifies that the
	 * request and export were done by on behalf of userspace, so
	 * they may be undone on its behalf too.
	 */

	status = gpiod_request(desc, "sysfs");
	if (status < 0) {
		if (status == -EPROBE_DEFER)
			status = -ENODEV;
		goto done;
	}
	status = gpiod_export(desc, true);
	if (status < 0)
		gpiod_free(desc);
	else
		set_bit(FLAG_SYSFS, &desc->flags);

done:
	if (status)
		pr_debug("%s: status %d\n", __func__, status);
	return status ? : len;
}

static ssize_t unexport_store(struct class *class,
				struct class_attribute *attr,
				const char *buf, size_t len)
{
	long			gpio;
	struct gpio_desc	*desc;
	int			status;

	status = kstrtol(buf, 0, &gpio);
	if (status < 0)
		goto done;

	desc = gpio_to_desc(gpio);
	/* reject bogus commands (gpio_unexport ignores them) */
	if (!desc) {
		pr_warn("%s: invalid GPIO %ld\n", __func__, gpio);
		return -EINVAL;
	}

	status = -EINVAL;

	/* No extra locking here; FLAG_SYSFS just signifies that the
	 * request and export were done by on behalf of userspace, so
	 * they may be undone on its behalf too.
	 */
	if (test_and_clear_bit(FLAG_SYSFS, &desc->flags)) {
		status = 0;
		gpiod_free(desc);
	}
done:
	if (status)
		pr_debug("%s: status %d\n", __func__, status);
	return status ? : len;
}

static struct class_attribute gpio_class_attrs[] = {
	__ATTR(export, 0200, NULL, export_store),
	__ATTR(unexport, 0200, NULL, unexport_store),
	__ATTR_NULL,
};

static struct class gpio_class = {
	.name =		"gpio",
	.owner =	THIS_MODULE,

	.class_attrs =	gpio_class_attrs,
};


/**
 * gpiod_export - export a GPIO through sysfs
 * @gpio: gpio to make available, already requested
 * @direction_may_change: true if userspace may change gpio direction
 * Context: arch_initcall or later
 *
 * When drivers want to make a GPIO accessible to userspace after they
 * have requested it -- perhaps while debugging, or as part of their
 * public interface -- they may use this routine.  If the GPIO can
 * change direction (some can't) and the caller allows it, userspace
 * will see "direction" sysfs attribute which may be used to change
 * the gpio's direction.  A "value" attribute will always be provided.
 *
 * Returns zero on success, else an error.
 */
int gpiod_export(struct gpio_desc *desc, bool direction_may_change)
{
	unsigned long		flags;
	int			status;
	const char		*ioname = NULL;
	struct device		*dev;
	int			offset;

	/* can't export until sysfs is available ... */
	if (!gpio_class.p) {
		pr_debug("%s: called too early!\n", __func__);
		return -ENOENT;
	}

	if (!desc) {
		pr_debug("%s: invalid gpio descriptor\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&sysfs_lock);

	spin_lock_irqsave(&gpio_lock, flags);
	if (!test_bit(FLAG_REQUESTED, &desc->flags) ||
	     test_bit(FLAG_EXPORT, &desc->flags)) {
		spin_unlock_irqrestore(&gpio_lock, flags);
		gpiod_dbg(desc, "%s: unavailable (requested=%d, exported=%d)\n",
				__func__,
				test_bit(FLAG_REQUESTED, &desc->flags),
				test_bit(FLAG_EXPORT, &desc->flags));
		status = -EPERM;
		goto fail_unlock;
	}

	if (!desc->chip->direction_input || !desc->chip->direction_output)
		direction_may_change = false;
	spin_unlock_irqrestore(&gpio_lock, flags);

	offset = gpio_chip_hwgpio(desc);
	if (desc->chip->names && desc->chip->names[offset])
		ioname = desc->chip->names[offset];

	dev = device_create(&gpio_class, desc->chip->dev, MKDEV(0, 0),
			    desc, ioname ? ioname : "gpio%u",
			    desc_to_gpio(desc));
	if (IS_ERR(dev)) {
		status = PTR_ERR(dev);
		goto fail_unlock;
	}

	status = sysfs_create_group(&dev->kobj, &gpio_attr_group);
	if (status)
		goto fail_unregister_device;

	if (direction_may_change) {
		status = device_create_file(dev, &dev_attr_direction);
		if (status)
			goto fail_unregister_device;
	}

	if (gpiod_to_irq(desc) >= 0 && (direction_may_change ||
				       !test_bit(FLAG_IS_OUT, &desc->flags))) {
		status = device_create_file(dev, &dev_attr_edge);
		if (status)
			goto fail_unregister_device;
	}

	set_bit(FLAG_EXPORT, &desc->flags);
	mutex_unlock(&sysfs_lock);
	return 0;

fail_unregister_device:
	device_unregister(dev);
fail_unlock:
	mutex_unlock(&sysfs_lock);
	gpiod_dbg(desc, "%s: status %d\n", __func__, status);
	return status;
}
EXPORT_SYMBOL_GPL(gpiod_export);

static int match_export(struct device *dev, const void *data)
{
	return dev_get_drvdata(dev) == data;
}

/**
 * gpiod_export_link - create a sysfs link to an exported GPIO node
 * @dev: device under which to create symlink
 * @name: name of the symlink
 * @gpio: gpio to create symlink to, already exported
 *
 * Set up a symlink from /sys/.../dev/name to /sys/class/gpio/gpioN
 * node. Caller is responsible for unlinking.
 *
 * Returns zero on success, else an error.
 */
int gpiod_export_link(struct device *dev, const char *name,
		      struct gpio_desc *desc)
{
	int			status = -EINVAL;

	if (!desc) {
		pr_warn("%s: invalid GPIO\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&sysfs_lock);

	if (test_bit(FLAG_EXPORT, &desc->flags)) {
		struct device *tdev;

		tdev = class_find_device(&gpio_class, NULL, desc, match_export);
		if (tdev != NULL) {
			status = sysfs_create_link(&dev->kobj, &tdev->kobj,
						name);
		} else {
			status = -ENODEV;
		}
	}

	mutex_unlock(&sysfs_lock);

	if (status)
		gpiod_dbg(desc, "%s: status %d\n", __func__, status);

	return status;
}
EXPORT_SYMBOL_GPL(gpiod_export_link);

/**
 * gpiod_sysfs_set_active_low - set the polarity of gpio sysfs value
 * @gpio: gpio to change
 * @value: non-zero to use active low, i.e. inverted values
 *
 * Set the polarity of /sys/class/gpio/gpioN/value sysfs attribute.
 * The GPIO does not have to be exported yet.  If poll(2) support has
 * been enabled for either rising or falling edge, it will be
 * reconfigured to follow the new polarity.
 *
 * Returns zero on success, else an error.
 */
int gpiod_sysfs_set_active_low(struct gpio_desc *desc, int value)
{
	struct device		*dev = NULL;
	int			status = -EINVAL;

	if (!desc) {
		pr_warn("%s: invalid GPIO\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&sysfs_lock);

	if (test_bit(FLAG_EXPORT, &desc->flags)) {
		dev = class_find_device(&gpio_class, NULL, desc, match_export);
		if (dev == NULL) {
			status = -ENODEV;
			goto unlock;
		}
	}

	status = sysfs_set_active_low(desc, dev, value);

unlock:
	mutex_unlock(&sysfs_lock);

	if (status)
		gpiod_dbg(desc, "%s: status %d\n", __func__, status);

	return status;
}
EXPORT_SYMBOL_GPL(gpiod_sysfs_set_active_low);

/**
 * gpiod_unexport - reverse effect of gpio_export()
 * @gpio: gpio to make unavailable
 *
 * This is implicit on gpio_free().
 */
void gpiod_unexport(struct gpio_desc *desc)
{
	int			status = 0;
	struct device		*dev = NULL;

	if (!desc) {
		pr_warn("%s: invalid GPIO\n", __func__);
		return;
	}

	mutex_lock(&sysfs_lock);

	if (test_bit(FLAG_EXPORT, &desc->flags)) {

		dev = class_find_device(&gpio_class, NULL, desc, match_export);
		if (dev) {
			gpio_setup_irq(desc, dev, 0);
			clear_bit(FLAG_EXPORT, &desc->flags);
		} else
			status = -ENODEV;
	}

	mutex_unlock(&sysfs_lock);

	if (dev) {
		device_unregister(dev);
		put_device(dev);
	}

	if (status)
		gpiod_dbg(desc, "%s: status %d\n", __func__, status);
}
EXPORT_SYMBOL_GPL(gpiod_unexport);

static int gpiochip_export(struct gpio_chip *chip)
{
	int		status;
	struct device	*dev;

	/* Many systems register gpio chips for SOC support very early,
	 * before driver model support is available.  In those cases we
	 * export this later, in gpiolib_sysfs_init() ... here we just
	 * verify that _some_ field of gpio_class got initialized.
	 */
	if (!gpio_class.p)
		return 0;

	/* use chip->base for the ID; it's already known to be unique */
	mutex_lock(&sysfs_lock);
	dev = device_create(&gpio_class, chip->dev, MKDEV(0, 0), chip,
				"gpiochip%d", chip->base);
	if (!IS_ERR(dev)) {
		status = sysfs_create_group(&dev->kobj,
				&gpiochip_attr_group);
	} else
		status = PTR_ERR(dev);
	chip->exported = (status == 0);
	mutex_unlock(&sysfs_lock);

	if (status) {
		unsigned long	flags;
		unsigned	gpio;

		spin_lock_irqsave(&gpio_lock, flags);
		gpio = 0;
		while (gpio < chip->ngpio)
			chip->desc[gpio++].chip = NULL;
		spin_unlock_irqrestore(&gpio_lock, flags);

		chip_dbg(chip, "%s: status %d\n", __func__, status);
	}

	return status;
}

static void gpiochip_unexport(struct gpio_chip *chip)
{
	int			status;
	struct device		*dev;

	mutex_lock(&sysfs_lock);
	dev = class_find_device(&gpio_class, NULL, chip, match_export);
	if (dev) {
		put_device(dev);
		device_unregister(dev);
		chip->exported = false;
		status = 0;
	} else
		status = -ENODEV;
	mutex_unlock(&sysfs_lock);

	if (status)
		chip_dbg(chip, "%s: status %d\n", __func__, status);
}

static int __init gpiolib_sysfs_init(void)
{
	int		status;
	unsigned long	flags;
	struct gpio_chip *chip;

	status = class_register(&gpio_class);
	if (status < 0)
		return status;

	/* Scan and register the gpio_chips which registered very
	 * early (e.g. before the class_register above was called).
	 *
	 * We run before arch_initcall() so chip->dev nodes can have
	 * registered, and so arch_initcall() can always gpio_export().
	 */
	spin_lock_irqsave(&gpio_lock, flags);
	list_for_each_entry(chip, &gpio_chips, list) {
		if (!chip || chip->exported)
			continue;

		spin_unlock_irqrestore(&gpio_lock, flags);
		status = gpiochip_export(chip);
		spin_lock_irqsave(&gpio_lock, flags);
	}
	spin_unlock_irqrestore(&gpio_lock, flags);


	return status;
}
postcore_initcall(gpiolib_sysfs_init);

#else
static inline int gpiochip_export(struct gpio_chip *chip)
{
	return 0;
}

static inline void gpiochip_unexport(struct gpio_chip *chip)
{
}

#endif /* CONFIG_GPIO_SYSFS */

/*
 * Add a new chip to the global chips list, keeping the list of chips sorted
 * by base order.
 *
 * Return -EBUSY if the new chip overlaps with some other chip's integer
 * space.
 */
static int gpiochip_add_to_list(struct gpio_chip *chip)
{
	struct list_head *pos = &gpio_chips;
	struct gpio_chip *_chip;
	int err = 0;

	/* find where to insert our chip */
	list_for_each(pos, &gpio_chips) {
		_chip = list_entry(pos, struct gpio_chip, list);
		/* shall we insert before _chip? */
		if (_chip->base >= chip->base + chip->ngpio)
			break;
	}

	/* are we stepping on the chip right before? */
	if (pos != &gpio_chips && pos->prev != &gpio_chips) {
		_chip = list_entry(pos->prev, struct gpio_chip, list);
		if (_chip->base + _chip->ngpio > chip->base) {
			dev_err(chip->dev,
			       "GPIO integer space overlap, cannot add chip\n");
			err = -EBUSY;
		}
	}

	if (!err)
		list_add_tail(&chip->list, pos);

	return err;
}

/**
 * gpiochip_add() - register a gpio_chip
 * @chip: the chip to register, with chip->base initialized
 * Context: potentially before irqs or kmalloc will work
 *
 * Returns a negative errno if the chip can't be registered, such as
 * because the chip->base is invalid or already associated with a
 * different chip.  Otherwise it returns zero as a success code.
 *
 * When gpiochip_add() is called very early during boot, so that GPIOs
 * can be freely used, the chip->dev device must be registered before
 * the gpio framework's arch_initcall().  Otherwise sysfs initialization
 * for GPIOs will fail rudely.
 *
 * If chip->base is negative, this requests dynamic assignment of
 * a range of valid GPIOs.
 */
int gpiochip_add(struct gpio_chip *chip)
{
	unsigned long	flags;
	int		status = 0;
	unsigned	id;
	int		base = chip->base;

	if ((!gpio_is_valid(base) || !gpio_is_valid(base + chip->ngpio - 1))
			&& base >= 0) {
		status = -EINVAL;
		goto fail;
	}

	spin_lock_irqsave(&gpio_lock, flags);

	if (base < 0) {
		base = gpiochip_find_base(chip->ngpio);
		if (base < 0) {
			status = base;
			goto unlock;
		}
		chip->base = base;
	}

	status = gpiochip_add_to_list(chip);

	if (status == 0) {
		chip->desc = &gpio_desc[chip->base];

		for (id = 0; id < chip->ngpio; id++) {
			struct gpio_desc *desc = &chip->desc[id];
			desc->chip = chip;

			/* REVISIT:  most hardware initializes GPIOs as
			 * inputs (often with pullups enabled) so power
			 * usage is minimized.  Linux code should set the
			 * gpio direction first thing; but until it does,
			 * and in case chip->get_direction is not set,
			 * we may expose the wrong direction in sysfs.
			 */
			desc->flags = !chip->direction_input
				? (1 << FLAG_IS_OUT)
				: 0;
		}
	}

	spin_unlock_irqrestore(&gpio_lock, flags);

#ifdef CONFIG_PINCTRL
	INIT_LIST_HEAD(&chip->pin_ranges);
#endif

	of_gpiochip_add(chip);
	acpi_gpiochip_add(chip);

	if (status)
		goto fail;

	status = gpiochip_export(chip);
	if (status)
		goto fail;

	pr_debug("%s: registered GPIOs %d to %d on device: %s\n", __func__,
		chip->base, chip->base + chip->ngpio - 1,
		chip->label ? : "generic");

	return 0;

unlock:
	spin_unlock_irqrestore(&gpio_lock, flags);
fail:
	/* failures here can mean systems won't boot... */
	pr_err("%s: GPIOs %d..%d (%s) failed to register\n", __func__,
		chip->base, chip->base + chip->ngpio - 1,
		chip->label ? : "generic");
	return status;
}
EXPORT_SYMBOL_GPL(gpiochip_add);

/* Forward-declaration */
static void gpiochip_irqchip_remove(struct gpio_chip *gpiochip);

/**
 * gpiochip_remove() - unregister a gpio_chip
 * @chip: the chip to unregister
 *
 * A gpio_chip with any GPIOs still requested may not be removed.
 */
int gpiochip_remove(struct gpio_chip *chip)
{
	unsigned long	flags;
	int		status = 0;
	unsigned	id;

	acpi_gpiochip_remove(chip);

	spin_lock_irqsave(&gpio_lock, flags);

	gpiochip_irqchip_remove(chip);
	gpiochip_remove_pin_ranges(chip);
	of_gpiochip_remove(chip);

	for (id = 0; id < chip->ngpio; id++) {
		if (test_bit(FLAG_REQUESTED, &chip->desc[id].flags)) {
			status = -EBUSY;
			break;
		}
	}
	if (status == 0) {
		for (id = 0; id < chip->ngpio; id++)
			chip->desc[id].chip = NULL;

		list_del(&chip->list);
	}

	spin_unlock_irqrestore(&gpio_lock, flags);

	if (status == 0)
		gpiochip_unexport(chip);

	return status;
}
EXPORT_SYMBOL_GPL(gpiochip_remove);

/**
 * gpiochip_find() - iterator for locating a specific gpio_chip
 * @data: data to pass to match function
 * @callback: Callback function to check gpio_chip
 *
 * Similar to bus_find_device.  It returns a reference to a gpio_chip as
 * determined by a user supplied @match callback.  The callback should return
 * 0 if the device doesn't match and non-zero if it does.  If the callback is
 * non-zero, this function will return to the caller and not iterate over any
 * more gpio_chips.
 */
struct gpio_chip *gpiochip_find(void *data,
				int (*match)(struct gpio_chip *chip,
					     void *data))
{
	struct gpio_chip *chip;
	unsigned long flags;

	spin_lock_irqsave(&gpio_lock, flags);
	list_for_each_entry(chip, &gpio_chips, list)
		if (match(chip, data))
			break;

	/* No match? */
	if (&chip->list == &gpio_chips)
		chip = NULL;
	spin_unlock_irqrestore(&gpio_lock, flags);

	return chip;
}
EXPORT_SYMBOL_GPL(gpiochip_find);

static int gpiochip_match_name(struct gpio_chip *chip, void *data)
{
	const char *name = data;

	return !strcmp(chip->label, name);
}

static struct gpio_chip *find_chip_by_name(const char *name)
{
	return gpiochip_find((void *)name, gpiochip_match_name);
}

#ifdef CONFIG_GPIOLIB_IRQCHIP

/*
 * The following is irqchip helper code for gpiochips.
 */

/**
 * gpiochip_add_chained_irqchip() - adds a chained irqchip to a gpiochip
 * @gpiochip: the gpiochip to add the irqchip to
 * @irqchip: the irqchip to add to the gpiochip
 * @parent_irq: the irq number corresponding to the parent IRQ for this
 * chained irqchip
 * @parent_handler: the parent interrupt handler for the accumulated IRQ
 * coming out of the gpiochip
 */
void gpiochip_set_chained_irqchip(struct gpio_chip *gpiochip,
				  struct irq_chip *irqchip,
				  int parent_irq,
				  irq_flow_handler_t parent_handler)
{
	if (gpiochip->can_sleep) {
		chip_err(gpiochip, "you cannot have chained interrupts on a chip that may sleep\n");
		return;
	}

	irq_set_chained_handler(parent_irq, parent_handler);
	/*
	 * The parent irqchip is already using the chip_data for this
	 * irqchip, so our callbacks simply use the handler_data.
	 */
	irq_set_handler_data(parent_irq, gpiochip);
}
EXPORT_SYMBOL_GPL(gpiochip_set_chained_irqchip);

/*
 * This lock class tells lockdep that GPIO irqs are in a different
 * category than their parents, so it won't report false recursion.
 */
static struct lock_class_key gpiochip_irq_lock_class;

/**
 * gpiochip_irq_map() - maps an IRQ into a GPIO irqchip
 * @d: the irqdomain used by this irqchip
 * @irq: the global irq number used by this GPIO irqchip irq
 * @hwirq: the local IRQ/GPIO line offset on this gpiochip
 *
 * This function will set up the mapping for a certain IRQ line on a
 * gpiochip by assigning the gpiochip as chip data, and using the irqchip
 * stored inside the gpiochip.
 */
static int gpiochip_irq_map(struct irq_domain *d, unsigned int irq,
			    irq_hw_number_t hwirq)
{
	struct gpio_chip *chip = d->host_data;

	irq_set_chip_data(irq, chip);
	irq_set_lockdep_class(irq, &gpiochip_irq_lock_class);
	irq_set_chip_and_handler(irq, chip->irqchip, chip->irq_handler);
	/* Chips that can sleep need nested thread handlers */
	if (chip->can_sleep)
		irq_set_nested_thread(irq, 1);
#ifdef CONFIG_ARM
	set_irq_flags(irq, IRQF_VALID);
#else
	irq_set_noprobe(irq);
#endif
	/*
	 * No set-up of the hardware will happen if IRQ_TYPE_NONE
	 * is passed as default type.
	 */
	if (chip->irq_default_type != IRQ_TYPE_NONE)
		irq_set_irq_type(irq, chip->irq_default_type);

	return 0;
}

static void gpiochip_irq_unmap(struct irq_domain *d, unsigned int irq)
{
	struct gpio_chip *chip = d->host_data;

#ifdef CONFIG_ARM
	set_irq_flags(irq, 0);
#endif
	if (chip->can_sleep)
		irq_set_nested_thread(irq, 0);
	irq_set_chip_and_handler(irq, NULL, NULL);
	irq_set_chip_data(irq, NULL);
}

static const struct irq_domain_ops gpiochip_domain_ops = {
	.map	= gpiochip_irq_map,
	.unmap	= gpiochip_irq_unmap,
	/* Virtually all GPIO irqchips are twocell:ed */
	.xlate	= irq_domain_xlate_twocell,
};

static int gpiochip_irq_reqres(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);

	if (gpio_lock_as_irq(chip, d->hwirq)) {
		chip_err(chip,
			"unable to lock HW IRQ %lu for IRQ\n",
			d->hwirq);
		return -EINVAL;
	}
	return 0;
}

static void gpiochip_irq_relres(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);

	gpio_unlock_as_irq(chip, d->hwirq);
}

static int gpiochip_to_irq(struct gpio_chip *chip, unsigned offset)
{
	return irq_find_mapping(chip->irqdomain, offset);
}

/**
 * gpiochip_irqchip_remove() - removes an irqchip added to a gpiochip
 * @gpiochip: the gpiochip to remove the irqchip from
 *
 * This is called only from gpiochip_remove()
 */
static void gpiochip_irqchip_remove(struct gpio_chip *gpiochip)
{
	unsigned int offset;

	/* Remove all IRQ mappings and delete the domain */
	if (gpiochip->irqdomain) {
		for (offset = 0; offset < gpiochip->ngpio; offset++)
			irq_dispose_mapping(gpiochip->irq_base + offset);
		irq_domain_remove(gpiochip->irqdomain);
	}

	if (gpiochip->irqchip) {
		gpiochip->irqchip->irq_request_resources = NULL;
		gpiochip->irqchip->irq_release_resources = NULL;
		gpiochip->irqchip = NULL;
	}
}

/**
 * gpiochip_irqchip_add() - adds an irqchip to a gpiochip
 * @gpiochip: the gpiochip to add the irqchip to
 * @irqchip: the irqchip to add to the gpiochip
 * @first_irq: if not dynamically assigned, the base (first) IRQ to
 * allocate gpiochip irqs from
 * @handler: the irq handler to use (often a predefined irq core function)
 * @type: the default type for IRQs on this irqchip, pass IRQ_TYPE_NONE
 * to have the core avoid setting up any default type in the hardware.
 *
 * This function closely associates a certain irqchip with a certain
 * gpiochip, providing an irq domain to translate the local IRQs to
 * global irqs in the gpiolib core, and making sure that the gpiochip
 * is passed as chip data to all related functions. Driver callbacks
 * need to use container_of() to get their local state containers back
 * from the gpiochip passed as chip data. An irqdomain will be stored
 * in the gpiochip that shall be used by the driver to handle IRQ number
 * translation. The gpiochip will need to be initialized and registered
 * before calling this function.
 *
 * This function will handle two cell:ed simple IRQs and assumes all
 * the pins on the gpiochip can generate a unique IRQ. Everything else
 * need to be open coded.
 */
int gpiochip_irqchip_add(struct gpio_chip *gpiochip,
			 struct irq_chip *irqchip,
			 unsigned int first_irq,
			 irq_flow_handler_t handler,
			 unsigned int type)
{
	struct device_node *of_node;
	unsigned int offset;
	unsigned irq_base = 0;

	if (!gpiochip || !irqchip)
		return -EINVAL;

	if (!gpiochip->dev) {
		pr_err("missing gpiochip .dev parent pointer\n");
		return -EINVAL;
	}
	of_node = gpiochip->dev->of_node;
#ifdef CONFIG_OF_GPIO
	/*
	 * If the gpiochip has an assigned OF node this takes precendence
	 * FIXME: get rid of this and use gpiochip->dev->of_node everywhere
	 */
	if (gpiochip->of_node)
		of_node = gpiochip->of_node;
#endif
	gpiochip->irqchip = irqchip;
	gpiochip->irq_handler = handler;
	gpiochip->irq_default_type = type;
	gpiochip->to_irq = gpiochip_to_irq;
	gpiochip->irqdomain = irq_domain_add_simple(of_node,
					gpiochip->ngpio, first_irq,
					&gpiochip_domain_ops, gpiochip);
	if (!gpiochip->irqdomain) {
		gpiochip->irqchip = NULL;
		return -EINVAL;
	}
	irqchip->irq_request_resources = gpiochip_irq_reqres;
	irqchip->irq_release_resources = gpiochip_irq_relres;

	/*
	 * Prepare the mapping since the irqchip shall be orthogonal to
	 * any gpiochip calls. If the first_irq was zero, this is
	 * necessary to allocate descriptors for all IRQs.
	 */
	for (offset = 0; offset < gpiochip->ngpio; offset++) {
		irq_base = irq_create_mapping(gpiochip->irqdomain, offset);
		if (offset == 0)
			/*
			 * Store the base into the gpiochip to be used when
			 * unmapping the irqs.
			 */
			gpiochip->irq_base = irq_base;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(gpiochip_irqchip_add);

#else /* CONFIG_GPIOLIB_IRQCHIP */

static void gpiochip_irqchip_remove(struct gpio_chip *gpiochip) {}

#endif /* CONFIG_GPIOLIB_IRQCHIP */

#ifdef CONFIG_PINCTRL

/**
 * gpiochip_add_pingroup_range() - add a range for GPIO <-> pin mapping
 * @chip: the gpiochip to add the range for
 * @pinctrl: the dev_name() of the pin controller to map to
 * @gpio_offset: the start offset in the current gpio_chip number space
 * @pin_group: name of the pin group inside the pin controller
 */
int gpiochip_add_pingroup_range(struct gpio_chip *chip,
			struct pinctrl_dev *pctldev,
			unsigned int gpio_offset, const char *pin_group)
{
	struct gpio_pin_range *pin_range;
	int ret;

	pin_range = kzalloc(sizeof(*pin_range), GFP_KERNEL);
	if (!pin_range) {
		chip_err(chip, "failed to allocate pin ranges\n");
		return -ENOMEM;
	}

	/* Use local offset as range ID */
	pin_range->range.id = gpio_offset;
	pin_range->range.gc = chip;
	pin_range->range.name = chip->label;
	pin_range->range.base = chip->base + gpio_offset;
	pin_range->pctldev = pctldev;

	ret = pinctrl_get_group_pins(pctldev, pin_group,
					&pin_range->range.pins,
					&pin_range->range.npins);
	if (ret < 0) {
		kfree(pin_range);
		return ret;
	}

	pinctrl_add_gpio_range(pctldev, &pin_range->range);

	chip_dbg(chip, "created GPIO range %d->%d ==> %s PINGRP %s\n",
		 gpio_offset, gpio_offset + pin_range->range.npins - 1,
		 pinctrl_dev_get_devname(pctldev), pin_group);

	list_add_tail(&pin_range->node, &chip->pin_ranges);

	return 0;
}
EXPORT_SYMBOL_GPL(gpiochip_add_pingroup_range);

/**
 * gpiochip_add_pin_range() - add a range for GPIO <-> pin mapping
 * @chip: the gpiochip to add the range for
 * @pinctrl_name: the dev_name() of the pin controller to map to
 * @gpio_offset: the start offset in the current gpio_chip number space
 * @pin_offset: the start offset in the pin controller number space
 * @npins: the number of pins from the offset of each pin space (GPIO and
 *	pin controller) to accumulate in this range
 */
int gpiochip_add_pin_range(struct gpio_chip *chip, const char *pinctl_name,
			   unsigned int gpio_offset, unsigned int pin_offset,
			   unsigned int npins)
{
	struct gpio_pin_range *pin_range;
	int ret;

	pin_range = kzalloc(sizeof(*pin_range), GFP_KERNEL);
	if (!pin_range) {
		chip_err(chip, "failed to allocate pin ranges\n");
		return -ENOMEM;
	}

	/* Use local offset as range ID */
	pin_range->range.id = gpio_offset;
	pin_range->range.gc = chip;
	pin_range->range.name = chip->label;
	pin_range->range.base = chip->base + gpio_offset;
	pin_range->range.pin_base = pin_offset;
	pin_range->range.npins = npins;
	pin_range->pctldev = pinctrl_find_and_add_gpio_range(pinctl_name,
			&pin_range->range);
	if (IS_ERR(pin_range->pctldev)) {
		ret = PTR_ERR(pin_range->pctldev);
		chip_err(chip, "could not create pin range\n");
		kfree(pin_range);
		return ret;
	}
	chip_dbg(chip, "created GPIO range %d->%d ==> %s PIN %d->%d\n",
		 gpio_offset, gpio_offset + npins - 1,
		 pinctl_name,
		 pin_offset, pin_offset + npins - 1);

	list_add_tail(&pin_range->node, &chip->pin_ranges);

	return 0;
}
EXPORT_SYMBOL_GPL(gpiochip_add_pin_range);

/**
 * gpiochip_remove_pin_ranges() - remove all the GPIO <-> pin mappings
 * @chip: the chip to remove all the mappings for
 */
void gpiochip_remove_pin_ranges(struct gpio_chip *chip)
{
	struct gpio_pin_range *pin_range, *tmp;

	list_for_each_entry_safe(pin_range, tmp, &chip->pin_ranges, node) {
		list_del(&pin_range->node);
		pinctrl_remove_gpio_range(pin_range->pctldev,
				&pin_range->range);
		kfree(pin_range);
	}
}
EXPORT_SYMBOL_GPL(gpiochip_remove_pin_ranges);

#endif /* CONFIG_PINCTRL */

/* These "optional" allocation calls help prevent drivers from stomping
 * on each other, and help provide better diagnostics in debugfs.
 * They're called even less than the "set direction" calls.
 */
static int __gpiod_request(struct gpio_desc *desc, const char *label)
{
	struct gpio_chip	*chip = desc->chip;
	int			status;
	unsigned long		flags;

	spin_lock_irqsave(&gpio_lock, flags);

	/* NOTE:  gpio_request() can be called in early boot,
	 * before IRQs are enabled, for non-sleeping (SOC) GPIOs.
	 */

	if (test_and_set_bit(FLAG_REQUESTED, &desc->flags) == 0) {
		desc_set_label(desc, label ? : "?");
		status = 0;
	} else {
		status = -EBUSY;
		goto done;
	}

	if (chip->request) {
		/* chip->request may sleep */
		spin_unlock_irqrestore(&gpio_lock, flags);
		status = chip->request(chip, gpio_chip_hwgpio(desc));
		spin_lock_irqsave(&gpio_lock, flags);

		if (status < 0) {
			desc_set_label(desc, NULL);
			clear_bit(FLAG_REQUESTED, &desc->flags);
			goto done;
		}
	}
	if (chip->get_direction) {
		/* chip->get_direction may sleep */
		spin_unlock_irqrestore(&gpio_lock, flags);
		gpiod_get_direction(desc);
		spin_lock_irqsave(&gpio_lock, flags);
	}
done:
	spin_unlock_irqrestore(&gpio_lock, flags);
	return status;
}

static int gpiod_request(struct gpio_desc *desc, const char *label)
{
	int status = -EPROBE_DEFER;
	struct gpio_chip *chip;

	if (!desc) {
		pr_warn("%s: invalid GPIO\n", __func__);
		return -EINVAL;
	}

	chip = desc->chip;
	if (!chip)
		goto done;

	if (try_module_get(chip->owner)) {
		status = __gpiod_request(desc, label);
		if (status < 0)
			module_put(chip->owner);
	}

done:
	if (status)
		gpiod_dbg(desc, "%s: status %d\n", __func__, status);

	return status;
}

int gpio_request(unsigned gpio, const char *label)
{
	return gpiod_request(gpio_to_desc(gpio), label);
}
EXPORT_SYMBOL_GPL(gpio_request);

static bool __gpiod_free(struct gpio_desc *desc)
{
	bool			ret = false;
	unsigned long		flags;
	struct gpio_chip	*chip;

	might_sleep();

	gpiod_unexport(desc);

	spin_lock_irqsave(&gpio_lock, flags);

	chip = desc->chip;
	if (chip && test_bit(FLAG_REQUESTED, &desc->flags)) {
		if (chip->free) {
			spin_unlock_irqrestore(&gpio_lock, flags);
			might_sleep_if(chip->can_sleep);
			chip->free(chip, gpio_chip_hwgpio(desc));
			spin_lock_irqsave(&gpio_lock, flags);
		}
		desc_set_label(desc, NULL);
		clear_bit(FLAG_ACTIVE_LOW, &desc->flags);
		clear_bit(FLAG_REQUESTED, &desc->flags);
		clear_bit(FLAG_OPEN_DRAIN, &desc->flags);
		clear_bit(FLAG_OPEN_SOURCE, &desc->flags);
		ret = true;
	}

	spin_unlock_irqrestore(&gpio_lock, flags);
	return ret;
}

static void gpiod_free(struct gpio_desc *desc)
{
	if (desc && __gpiod_free(desc))
		module_put(desc->chip->owner);
	else
		WARN_ON(extra_checks);
}

void gpio_free(unsigned gpio)
{
	gpiod_free(gpio_to_desc(gpio));
}
EXPORT_SYMBOL_GPL(gpio_free);

/**
 * gpio_request_one - request a single GPIO with initial configuration
 * @gpio:	the GPIO number
 * @flags:	GPIO configuration as specified by GPIOF_*
 * @label:	a literal description string of this GPIO
 */
int gpio_request_one(unsigned gpio, unsigned long flags, const char *label)
{
	struct gpio_desc *desc;
	int err;

	desc = gpio_to_desc(gpio);

	err = gpiod_request(desc, label);
	if (err)
		return err;

	if (flags & GPIOF_OPEN_DRAIN)
		set_bit(FLAG_OPEN_DRAIN, &desc->flags);

	if (flags & GPIOF_OPEN_SOURCE)
		set_bit(FLAG_OPEN_SOURCE, &desc->flags);

	if (flags & GPIOF_DIR_IN)
		err = gpiod_direction_input(desc);
	else
		err = gpiod_direction_output_raw(desc,
				(flags & GPIOF_INIT_HIGH) ? 1 : 0);

	if (err)
		goto free_gpio;

	if (flags & GPIOF_EXPORT) {
		err = gpiod_export(desc, flags & GPIOF_EXPORT_CHANGEABLE);
		if (err)
			goto free_gpio;
	}

	return 0;

 free_gpio:
	gpiod_free(desc);
	return err;
}
EXPORT_SYMBOL_GPL(gpio_request_one);

/**
 * gpio_request_array - request multiple GPIOs in a single call
 * @array:	array of the 'struct gpio'
 * @num:	how many GPIOs in the array
 */
int gpio_request_array(const struct gpio *array, size_t num)
{
	int i, err;

	for (i = 0; i < num; i++, array++) {
		err = gpio_request_one(array->gpio, array->flags, array->label);
		if (err)
			goto err_free;
	}
	return 0;

err_free:
	while (i--)
		gpio_free((--array)->gpio);
	return err;
}
EXPORT_SYMBOL_GPL(gpio_request_array);

/**
 * gpio_free_array - release multiple GPIOs in a single call
 * @array:	array of the 'struct gpio'
 * @num:	how many GPIOs in the array
 */
void gpio_free_array(const struct gpio *array, size_t num)
{
	while (num--)
		gpio_free((array++)->gpio);
}
EXPORT_SYMBOL_GPL(gpio_free_array);

/**
 * gpiochip_is_requested - return string iff signal was requested
 * @chip: controller managing the signal
 * @offset: of signal within controller's 0..(ngpio - 1) range
 *
 * Returns NULL if the GPIO is not currently requested, else a string.
 * If debugfs support is enabled, the string returned is the label passed
 * to gpio_request(); otherwise it is a meaningless constant.
 *
 * This function is for use by GPIO controller drivers.  The label can
 * help with diagnostics, and knowing that the signal is used as a GPIO
 * can help avoid accidentally multiplexing it to another controller.
 */
const char *gpiochip_is_requested(struct gpio_chip *chip, unsigned offset)
{
	struct gpio_desc *desc;

	if (!GPIO_OFFSET_VALID(chip, offset))
		return NULL;

	desc = &chip->desc[offset];

	if (test_bit(FLAG_REQUESTED, &desc->flags) == 0)
		return NULL;
#ifdef CONFIG_DEBUG_FS
	return desc->label;
#else
	return "?";
#endif
}
EXPORT_SYMBOL_GPL(gpiochip_is_requested);

/**
 * gpiochip_request_own_desc - Allow GPIO chip to request its own descriptor
 * @desc: GPIO descriptor to request
 * @label: label for the GPIO
 *
 * Function allows GPIO chip drivers to request and use their own GPIO
 * descriptors via gpiolib API. Difference to gpiod_request() is that this
 * function will not increase reference count of the GPIO chip module. This
 * allows the GPIO chip module to be unloaded as needed (we assume that the
 * GPIO chip driver handles freeing the GPIOs it has requested).
 */
int gpiochip_request_own_desc(struct gpio_desc *desc, const char *label)
{
	if (!desc || !desc->chip)
		return -EINVAL;

	return __gpiod_request(desc, label);
}

/**
 * gpiochip_free_own_desc - Free GPIO requested by the chip driver
 * @desc: GPIO descriptor to free
 *
 * Function frees the given GPIO requested previously with
 * gpiochip_request_own_desc().
 */
void gpiochip_free_own_desc(struct gpio_desc *desc)
{
	if (desc)
		__gpiod_free(desc);
}

/* Drivers MUST set GPIO direction before making get/set calls.  In
 * some cases this is done in early boot, before IRQs are enabled.
 *
 * As a rule these aren't called more than once (except for drivers
 * using the open-drain emulation idiom) so these are natural places
 * to accumulate extra debugging checks.  Note that we can't (yet)
 * rely on gpio_request() having been called beforehand.
 */

/**
 * gpiod_direction_input - set the GPIO direction to input
 * @desc:	GPIO to set to input
 *
 * Set the direction of the passed GPIO to input, such as gpiod_get_value() can
 * be called safely on it.
 *
 * Return 0 in case of success, else an error code.
 */
int gpiod_direction_input(struct gpio_desc *desc)
{
	unsigned long		flags;
	struct gpio_chip	*chip;
	int			status = -EINVAL;
	int			offset;

	if (!desc || !desc->chip) {
		pr_warn("%s: invalid GPIO\n", __func__);
		return -EINVAL;
	}

	chip = desc->chip;
	if (!chip->get || !chip->direction_input) {
		gpiod_warn(desc,
			"%s: missing get() or direction_input() operations\n",
			__func__);
		return -EIO;
	}

	spin_lock_irqsave(&gpio_lock, flags);

	status = gpio_ensure_requested(desc);
	if (status < 0)
		goto fail;

	/* now we know the gpio is valid and chip won't vanish */

	spin_unlock_irqrestore(&gpio_lock, flags);

	might_sleep_if(chip->can_sleep);

	offset = gpio_chip_hwgpio(desc);
	if (status) {
		status = chip->request(chip, offset);
		if (status < 0) {
			gpiod_dbg(desc, "%s: chip request fail, %d\n",
					__func__, status);
			/* and it's not available to anyone else ...
			 * gpio_request() is the fully clean solution.
			 */
			goto lose;
		}
	}

	status = chip->direction_input(chip, offset);
	if (status == 0)
		clear_bit(FLAG_IS_OUT, &desc->flags);

	trace_gpio_direction(desc_to_gpio(desc), 1, status);
lose:
	return status;
fail:
	spin_unlock_irqrestore(&gpio_lock, flags);
	if (status)
		gpiod_dbg(desc, "%s: status %d\n", __func__, status);
	return status;
}
EXPORT_SYMBOL_GPL(gpiod_direction_input);

static int _gpiod_direction_output_raw(struct gpio_desc *desc, int value)
{
	unsigned long		flags;
	struct gpio_chip	*chip;
	int			status = -EINVAL;
	int offset;

	/* GPIOs used for IRQs shall not be set as output */
	if (test_bit(FLAG_USED_AS_IRQ, &desc->flags)) {
		gpiod_err(desc,
			  "%s: tried to set a GPIO tied to an IRQ as output\n",
			  __func__);
		return -EIO;
	}

	/* Open drain pin should not be driven to 1 */
	if (value && test_bit(FLAG_OPEN_DRAIN,  &desc->flags))
		return gpiod_direction_input(desc);

	/* Open source pin should not be driven to 0 */
	if (!value && test_bit(FLAG_OPEN_SOURCE,  &desc->flags))
		return gpiod_direction_input(desc);

	chip = desc->chip;
	if (!chip->set || !chip->direction_output) {
		gpiod_warn(desc,
		       "%s: missing set() or direction_output() operations\n",
		       __func__);
		return -EIO;
	}

	spin_lock_irqsave(&gpio_lock, flags);

	status = gpio_ensure_requested(desc);
	if (status < 0)
		goto fail;

	/* now we know the gpio is valid and chip won't vanish */

	spin_unlock_irqrestore(&gpio_lock, flags);

	might_sleep_if(chip->can_sleep);

	offset = gpio_chip_hwgpio(desc);
	if (status) {
		status = chip->request(chip, offset);
		if (status < 0) {
			gpiod_dbg(desc, "%s: chip request fail, %d\n",
					__func__, status);
			/* and it's not available to anyone else ...
			 * gpio_request() is the fully clean solution.
			 */
			goto lose;
		}
	}

	status = chip->direction_output(chip, offset, value);
	if (status == 0)
		set_bit(FLAG_IS_OUT, &desc->flags);
	trace_gpio_value(desc_to_gpio(desc), 0, value);
	trace_gpio_direction(desc_to_gpio(desc), 0, status);
lose:
	return status;
fail:
	spin_unlock_irqrestore(&gpio_lock, flags);
	if (status)
		gpiod_dbg(desc, "%s: gpio status %d\n", __func__, status);
	return status;
}

/**
 * gpiod_direction_output_raw - set the GPIO direction to output
 * @desc:	GPIO to set to output
 * @value:	initial output value of the GPIO
 *
 * Set the direction of the passed GPIO to output, such as gpiod_set_value() can
 * be called safely on it. The initial value of the output must be specified
 * as raw value on the physical line without regard for the ACTIVE_LOW status.
 *
 * Return 0 in case of success, else an error code.
 */
int gpiod_direction_output_raw(struct gpio_desc *desc, int value)
{
	if (!desc || !desc->chip) {
		pr_warn("%s: invalid GPIO\n", __func__);
		return -EINVAL;
	}
	return _gpiod_direction_output_raw(desc, value);
}
EXPORT_SYMBOL_GPL(gpiod_direction_output_raw);

/**
 * gpiod_direction_output - set the GPIO direction to output
 * @desc:	GPIO to set to output
 * @value:	initial output value of the GPIO
 *
 * Set the direction of the passed GPIO to output, such as gpiod_set_value() can
 * be called safely on it. The initial value of the output must be specified
 * as the logical value of the GPIO, i.e. taking its ACTIVE_LOW status into
 * account.
 *
 * Return 0 in case of success, else an error code.
 */
int gpiod_direction_output(struct gpio_desc *desc, int value)
{
	if (!desc || !desc->chip) {
		pr_warn("%s: invalid GPIO\n", __func__);
		return -EINVAL;
	}
	if (test_bit(FLAG_ACTIVE_LOW, &desc->flags))
		value = !value;
	return _gpiod_direction_output_raw(desc, value);
}
EXPORT_SYMBOL_GPL(gpiod_direction_output);

/**
 * gpiod_set_debounce - sets @debounce time for a @gpio
 * @gpio: the gpio to set debounce time
 * @debounce: debounce time is microseconds
 *
 * returns -ENOTSUPP if the controller does not support setting
 * debounce.
 */
int gpiod_set_debounce(struct gpio_desc *desc, unsigned debounce)
{
	unsigned long		flags;
	struct gpio_chip	*chip;
	int			status = -EINVAL;
	int			offset;

	if (!desc || !desc->chip) {
		pr_warn("%s: invalid GPIO\n", __func__);
		return -EINVAL;
	}

	chip = desc->chip;
	if (!chip->set || !chip->set_debounce) {
		gpiod_dbg(desc,
			  "%s: missing set() or set_debounce() operations\n",
			  __func__);
		return -ENOTSUPP;
	}

	spin_lock_irqsave(&gpio_lock, flags);

	status = gpio_ensure_requested(desc);
	if (status < 0)
		goto fail;

	/* now we know the gpio is valid and chip won't vanish */

	spin_unlock_irqrestore(&gpio_lock, flags);

	might_sleep_if(chip->can_sleep);

	offset = gpio_chip_hwgpio(desc);
	return chip->set_debounce(chip, offset, debounce);

fail:
	spin_unlock_irqrestore(&gpio_lock, flags);
	if (status)
		gpiod_dbg(desc, "%s: status %d\n", __func__, status);

	return status;
}
EXPORT_SYMBOL_GPL(gpiod_set_debounce);

/**
 * gpiod_is_active_low - test whether a GPIO is active-low or not
 * @desc: the gpio descriptor to test
 *
 * Returns 1 if the GPIO is active-low, 0 otherwise.
 */
int gpiod_is_active_low(const struct gpio_desc *desc)
{
	return test_bit(FLAG_ACTIVE_LOW, &desc->flags);
}
EXPORT_SYMBOL_GPL(gpiod_is_active_low);

/* I/O calls are only valid after configuration completed; the relevant
 * "is this a valid GPIO" error checks should already have been done.
 *
 * "Get" operations are often inlinable as reading a pin value register,
 * and masking the relevant bit in that register.
 *
 * When "set" operations are inlinable, they involve writing that mask to
 * one register to set a low value, or a different register to set it high.
 * Otherwise locking is needed, so there may be little value to inlining.
 *
 *------------------------------------------------------------------------
 *
 * IMPORTANT!!!  The hot paths -- get/set value -- assume that callers
 * have requested the GPIO.  That can include implicit requesting by
 * a direction setting call.  Marking a gpio as requested locks its chip
 * in memory, guaranteeing that these table lookups need no more locking
 * and that gpiochip_remove() will fail.
 *
 * REVISIT when debugging, consider adding some instrumentation to ensure
 * that the GPIO was actually requested.
 */

static bool _gpiod_get_raw_value(const struct gpio_desc *desc)
{
	struct gpio_chip	*chip;
	bool value;
	int offset;

	chip = desc->chip;
	offset = gpio_chip_hwgpio(desc);
	value = chip->get ? chip->get(chip, offset) : false;
	trace_gpio_value(desc_to_gpio(desc), 1, value);
	return value;
}

/**
 * gpiod_get_raw_value() - return a gpio's raw value
 * @desc: gpio whose value will be returned
 *
 * Return the GPIO's raw value, i.e. the value of the physical line disregarding
 * its ACTIVE_LOW status.
 *
 * This function should be called from contexts where we cannot sleep, and will
 * complain if the GPIO chip functions potentially sleep.
 */
int gpiod_get_raw_value(const struct gpio_desc *desc)
{
	if (!desc)
		return 0;
	/* Should be using gpio_get_value_cansleep() */
	WARN_ON(desc->chip->can_sleep);
	return _gpiod_get_raw_value(desc);
}
EXPORT_SYMBOL_GPL(gpiod_get_raw_value);

/**
 * gpiod_get_value() - return a gpio's value
 * @desc: gpio whose value will be returned
 *
 * Return the GPIO's logical value, i.e. taking the ACTIVE_LOW status into
 * account.
 *
 * This function should be called from contexts where we cannot sleep, and will
 * complain if the GPIO chip functions potentially sleep.
 */
int gpiod_get_value(const struct gpio_desc *desc)
{
	int value;
	if (!desc)
		return 0;
	/* Should be using gpio_get_value_cansleep() */
	WARN_ON(desc->chip->can_sleep);

	value = _gpiod_get_raw_value(desc);
	if (test_bit(FLAG_ACTIVE_LOW, &desc->flags))
		value = !value;

	return value;
}
EXPORT_SYMBOL_GPL(gpiod_get_value);

/*
 *  _gpio_set_open_drain_value() - Set the open drain gpio's value.
 * @desc: gpio descriptor whose state need to be set.
 * @value: Non-zero for setting it HIGH otherise it will set to LOW.
 */
static void _gpio_set_open_drain_value(struct gpio_desc *desc, bool value)
{
	int err = 0;
	struct gpio_chip *chip = desc->chip;
	int offset = gpio_chip_hwgpio(desc);

	if (value) {
		err = chip->direction_input(chip, offset);
		if (!err)
			clear_bit(FLAG_IS_OUT, &desc->flags);
	} else {
		err = chip->direction_output(chip, offset, 0);
		if (!err)
			set_bit(FLAG_IS_OUT, &desc->flags);
	}
	trace_gpio_direction(desc_to_gpio(desc), value, err);
	if (err < 0)
		gpiod_err(desc,
			  "%s: Error in set_value for open drain err %d\n",
			  __func__, err);
}

/*
 *  _gpio_set_open_source_value() - Set the open source gpio's value.
 * @desc: gpio descriptor whose state need to be set.
 * @value: Non-zero for setting it HIGH otherise it will set to LOW.
 */
static void _gpio_set_open_source_value(struct gpio_desc *desc, bool value)
{
	int err = 0;
	struct gpio_chip *chip = desc->chip;
	int offset = gpio_chip_hwgpio(desc);

	if (value) {
		err = chip->direction_output(chip, offset, 1);
		if (!err)
			set_bit(FLAG_IS_OUT, &desc->flags);
	} else {
		err = chip->direction_input(chip, offset);
		if (!err)
			clear_bit(FLAG_IS_OUT, &desc->flags);
	}
	trace_gpio_direction(desc_to_gpio(desc), !value, err);
	if (err < 0)
		gpiod_err(desc,
			  "%s: Error in set_value for open source err %d\n",
			  __func__, err);
}

static void _gpiod_set_raw_value(struct gpio_desc *desc, bool value)
{
	struct gpio_chip	*chip;

	chip = desc->chip;
	trace_gpio_value(desc_to_gpio(desc), 0, value);
	if (test_bit(FLAG_OPEN_DRAIN, &desc->flags))
		_gpio_set_open_drain_value(desc, value);
	else if (test_bit(FLAG_OPEN_SOURCE, &desc->flags))
		_gpio_set_open_source_value(desc, value);
	else
		chip->set(chip, gpio_chip_hwgpio(desc), value);
}

/**
 * gpiod_set_raw_value() - assign a gpio's raw value
 * @desc: gpio whose value will be assigned
 * @value: value to assign
 *
 * Set the raw value of the GPIO, i.e. the value of its physical line without
 * regard for its ACTIVE_LOW status.
 *
 * This function should be called from contexts where we cannot sleep, and will
 * complain if the GPIO chip functions potentially sleep.
 */
void gpiod_set_raw_value(struct gpio_desc *desc, int value)
{
	if (!desc)
		return;
	/* Should be using gpio_set_value_cansleep() */
	WARN_ON(desc->chip->can_sleep);
	_gpiod_set_raw_value(desc, value);
}
EXPORT_SYMBOL_GPL(gpiod_set_raw_value);

/**
 * gpiod_set_value() - assign a gpio's value
 * @desc: gpio whose value will be assigned
 * @value: value to assign
 *
 * Set the logical value of the GPIO, i.e. taking its ACTIVE_LOW status into
 * account
 *
 * This function should be called from contexts where we cannot sleep, and will
 * complain if the GPIO chip functions potentially sleep.
 */
void gpiod_set_value(struct gpio_desc *desc, int value)
{
	if (!desc)
		return;
	/* Should be using gpio_set_value_cansleep() */
	WARN_ON(desc->chip->can_sleep);
	if (test_bit(FLAG_ACTIVE_LOW, &desc->flags))
		value = !value;
	_gpiod_set_raw_value(desc, value);
}
EXPORT_SYMBOL_GPL(gpiod_set_value);

/**
 * gpiod_cansleep() - report whether gpio value access may sleep
 * @desc: gpio to check
 *
 */
int gpiod_cansleep(const struct gpio_desc *desc)
{
	if (!desc)
		return 0;
	return desc->chip->can_sleep;
}
EXPORT_SYMBOL_GPL(gpiod_cansleep);

/**
 * gpiod_to_irq() - return the IRQ corresponding to a GPIO
 * @desc: gpio whose IRQ will be returned (already requested)
 *
 * Return the IRQ corresponding to the passed GPIO, or an error code in case of
 * error.
 */
int gpiod_to_irq(const struct gpio_desc *desc)
{
	struct gpio_chip	*chip;
	int			offset;

	if (!desc)
		return -EINVAL;
	chip = desc->chip;
	offset = gpio_chip_hwgpio(desc);
	return chip->to_irq ? chip->to_irq(chip, offset) : -ENXIO;
}
EXPORT_SYMBOL_GPL(gpiod_to_irq);

/**
 * gpiod_lock_as_irq() - lock a GPIO to be used as IRQ
 * @gpio: the GPIO line to lock as used for IRQ
 *
 * This is used directly by GPIO drivers that want to lock down
 * a certain GPIO line to be used for IRQs.
 */
int gpiod_lock_as_irq(struct gpio_desc *desc)
{
	if (!desc)
		return -EINVAL;

	if (test_bit(FLAG_IS_OUT, &desc->flags)) {
		gpiod_err(desc,
			  "%s: tried to flag a GPIO set as output for IRQ\n",
			  __func__);
		return -EIO;
	}

	set_bit(FLAG_USED_AS_IRQ, &desc->flags);
	return 0;
}
EXPORT_SYMBOL_GPL(gpiod_lock_as_irq);

int gpio_lock_as_irq(struct gpio_chip *chip, unsigned int offset)
{
	return gpiod_lock_as_irq(gpiochip_get_desc(chip, offset));
}
EXPORT_SYMBOL_GPL(gpio_lock_as_irq);

/**
 * gpiod_unlock_as_irq() - unlock a GPIO used as IRQ
 * @gpio: the GPIO line to unlock from IRQ usage
 *
 * This is used directly by GPIO drivers that want to indicate
 * that a certain GPIO is no longer used exclusively for IRQ.
 */
void gpiod_unlock_as_irq(struct gpio_desc *desc)
{
	if (!desc)
		return;

	clear_bit(FLAG_USED_AS_IRQ, &desc->flags);
}
EXPORT_SYMBOL_GPL(gpiod_unlock_as_irq);

void gpio_unlock_as_irq(struct gpio_chip *chip, unsigned int offset)
{
	return gpiod_unlock_as_irq(gpiochip_get_desc(chip, offset));
}
EXPORT_SYMBOL_GPL(gpio_unlock_as_irq);

/**
 * gpiod_get_raw_value_cansleep() - return a gpio's raw value
 * @desc: gpio whose value will be returned
 *
 * Return the GPIO's raw value, i.e. the value of the physical line disregarding
 * its ACTIVE_LOW status.
 *
 * This function is to be called from contexts that can sleep.
 */
int gpiod_get_raw_value_cansleep(const struct gpio_desc *desc)
{
	might_sleep_if(extra_checks);
	if (!desc)
		return 0;
	return _gpiod_get_raw_value(desc);
}
EXPORT_SYMBOL_GPL(gpiod_get_raw_value_cansleep);

/**
 * gpiod_get_value_cansleep() - return a gpio's value
 * @desc: gpio whose value will be returned
 *
 * Return the GPIO's logical value, i.e. taking the ACTIVE_LOW status into
 * account.
 *
 * This function is to be called from contexts that can sleep.
 */
int gpiod_get_value_cansleep(const struct gpio_desc *desc)
{
	int value;

	might_sleep_if(extra_checks);
	if (!desc)
		return 0;

	value = _gpiod_get_raw_value(desc);
	if (test_bit(FLAG_ACTIVE_LOW, &desc->flags))
		value = !value;

	return value;
}
EXPORT_SYMBOL_GPL(gpiod_get_value_cansleep);

/**
 * gpiod_set_raw_value_cansleep() - assign a gpio's raw value
 * @desc: gpio whose value will be assigned
 * @value: value to assign
 *
 * Set the raw value of the GPIO, i.e. the value of its physical line without
 * regard for its ACTIVE_LOW status.
 *
 * This function is to be called from contexts that can sleep.
 */
void gpiod_set_raw_value_cansleep(struct gpio_desc *desc, int value)
{
	might_sleep_if(extra_checks);
	if (!desc)
		return;
	_gpiod_set_raw_value(desc, value);
}
EXPORT_SYMBOL_GPL(gpiod_set_raw_value_cansleep);

/**
 * gpiod_set_value_cansleep() - assign a gpio's value
 * @desc: gpio whose value will be assigned
 * @value: value to assign
 *
 * Set the logical value of the GPIO, i.e. taking its ACTIVE_LOW status into
 * account
 *
 * This function is to be called from contexts that can sleep.
 */
void gpiod_set_value_cansleep(struct gpio_desc *desc, int value)
{
	might_sleep_if(extra_checks);
	if (!desc)
		return;

	if (test_bit(FLAG_ACTIVE_LOW, &desc->flags))
		value = !value;
	_gpiod_set_raw_value(desc, value);
}
EXPORT_SYMBOL_GPL(gpiod_set_value_cansleep);

/**
 * gpiod_add_lookup_table() - register GPIO device consumers
 * @table: table of consumers to register
 */
void gpiod_add_lookup_table(struct gpiod_lookup_table *table)
{
	mutex_lock(&gpio_lookup_lock);

	list_add_tail(&table->list, &gpio_lookup_list);

	mutex_unlock(&gpio_lookup_lock);
}

static struct gpio_desc *of_find_gpio(struct device *dev, const char *con_id,
				      unsigned int idx,
				      enum gpio_lookup_flags *flags)
{
	char prop_name[32]; /* 32 is max size of property name */
	enum of_gpio_flags of_flags;
	struct gpio_desc *desc;

	if (con_id)
		snprintf(prop_name, 32, "%s-gpios", con_id);
	else
		snprintf(prop_name, 32, "gpios");

	desc = of_get_named_gpiod_flags(dev->of_node, prop_name, idx,
					&of_flags);

	if (IS_ERR(desc))
		return desc;

	if (of_flags & OF_GPIO_ACTIVE_LOW)
		*flags |= GPIO_ACTIVE_LOW;

	return desc;
}

static struct gpio_desc *acpi_find_gpio(struct device *dev, const char *con_id,
					unsigned int idx,
					enum gpio_lookup_flags *flags)
{
	struct acpi_gpio_info info;
	struct gpio_desc *desc;

	desc = acpi_get_gpiod_by_index(dev, idx, &info);
	if (IS_ERR(desc))
		return desc;

	if (info.gpioint && info.active_low)
		*flags |= GPIO_ACTIVE_LOW;

	return desc;
}

static struct gpiod_lookup_table *gpiod_find_lookup_table(struct device *dev)
{
	const char *dev_id = dev ? dev_name(dev) : NULL;
	struct gpiod_lookup_table *table;

	mutex_lock(&gpio_lookup_lock);

	list_for_each_entry(table, &gpio_lookup_list, list) {
		if (table->dev_id && dev_id) {
			/*
			 * Valid strings on both ends, must be identical to have
			 * a match
			 */
			if (!strcmp(table->dev_id, dev_id))
				goto found;
		} else {
			/*
			 * One of the pointers is NULL, so both must be to have
			 * a match
			 */
			if (dev_id == table->dev_id)
				goto found;
		}
	}
	table = NULL;

found:
	mutex_unlock(&gpio_lookup_lock);
	return table;
}

static struct gpio_desc *gpiod_find(struct device *dev, const char *con_id,
				    unsigned int idx,
				    enum gpio_lookup_flags *flags)
{
	struct gpio_desc *desc = ERR_PTR(-ENOENT);
	struct gpiod_lookup_table *table;
	struct gpiod_lookup *p;

	table = gpiod_find_lookup_table(dev);
	if (!table)
		return desc;

	for (p = &table->table[0]; p->chip_label; p++) {
		struct gpio_chip *chip;

		/* idx must always match exactly */
		if (p->idx != idx)
			continue;

		/* If the lookup entry has a con_id, require exact match */
		if (p->con_id && (!con_id || strcmp(p->con_id, con_id)))
			continue;

		chip = find_chip_by_name(p->chip_label);

		if (!chip) {
			dev_err(dev, "cannot find GPIO chip %s\n",
				p->chip_label);
			return ERR_PTR(-ENODEV);
		}

		if (chip->ngpio <= p->chip_hwnum) {
			dev_err(dev,
				"requested GPIO %d is out of range [0..%d] for chip %s\n",
				idx, chip->ngpio, chip->label);
			return ERR_PTR(-EINVAL);
		}

		desc = gpiochip_get_desc(chip, p->chip_hwnum);
		*flags = p->flags;

		return desc;
	}

	return desc;
}

/**
 * gpio_get - obtain a GPIO for a given GPIO function
 * @dev:	GPIO consumer, can be NULL for system-global GPIOs
 * @con_id:	function within the GPIO consumer
 *
 * Return the GPIO descriptor corresponding to the function con_id of device
 * dev, -ENOENT if no GPIO has been assigned to the requested function, or
 * another IS_ERR() code if an error occured while trying to acquire the GPIO.
 */
struct gpio_desc *__must_check gpiod_get(struct device *dev, const char *con_id)
{
	return gpiod_get_index(dev, con_id, 0);
}
EXPORT_SYMBOL_GPL(gpiod_get);

/**
 * gpiod_get_index - obtain a GPIO from a multi-index GPIO function
 * @dev:	GPIO consumer, can be NULL for system-global GPIOs
 * @con_id:	function within the GPIO consumer
 * @idx:	index of the GPIO to obtain in the consumer
 *
 * This variant of gpiod_get() allows to access GPIOs other than the first
 * defined one for functions that define several GPIOs.
 *
 * Return a valid GPIO descriptor, -ENOENT if no GPIO has been assigned to the
 * requested function and/or index, or another IS_ERR() code if an error
 * occured while trying to acquire the GPIO.
 */
struct gpio_desc *__must_check gpiod_get_index(struct device *dev,
					       const char *con_id,
					       unsigned int idx)
{
	struct gpio_desc *desc = NULL;
	int status;
	enum gpio_lookup_flags flags = 0;

	dev_dbg(dev, "GPIO lookup for consumer %s\n", con_id);

	/* Using device tree? */
	if (IS_ENABLED(CONFIG_OF) && dev && dev->of_node) {
		dev_dbg(dev, "using device tree for GPIO lookup\n");
		desc = of_find_gpio(dev, con_id, idx, &flags);
	} else if (IS_ENABLED(CONFIG_ACPI) && dev && ACPI_HANDLE(dev)) {
		dev_dbg(dev, "using ACPI for GPIO lookup\n");
		desc = acpi_find_gpio(dev, con_id, idx, &flags);
	}

	/*
	 * Either we are not using DT or ACPI, or their lookup did not return
	 * a result. In that case, use platform lookup as a fallback.
	 */
	if (!desc || desc == ERR_PTR(-ENOENT)) {
		dev_dbg(dev, "using lookup tables for GPIO lookup");
		desc = gpiod_find(dev, con_id, idx, &flags);
	}

	if (IS_ERR(desc)) {
		dev_dbg(dev, "lookup for GPIO %s failed\n", con_id);
		return desc;
	}

	status = gpiod_request(desc, con_id);

	if (status < 0)
		return ERR_PTR(status);

	if (flags & GPIO_ACTIVE_LOW)
		set_bit(FLAG_ACTIVE_LOW, &desc->flags);
	if (flags & GPIO_OPEN_DRAIN)
		set_bit(FLAG_OPEN_DRAIN, &desc->flags);
	if (flags & GPIO_OPEN_SOURCE)
		set_bit(FLAG_OPEN_SOURCE, &desc->flags);

	return desc;
}
EXPORT_SYMBOL_GPL(gpiod_get_index);

/**
 * gpiod_put - dispose of a GPIO descriptor
 * @desc:	GPIO descriptor to dispose of
 *
 * No descriptor can be used after gpiod_put() has been called on it.
 */
void gpiod_put(struct gpio_desc *desc)
{
	gpiod_free(desc);
}
EXPORT_SYMBOL_GPL(gpiod_put);

#ifdef CONFIG_DEBUG_FS

static void gpiolib_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	unsigned		i;
	unsigned		gpio = chip->base;
	struct gpio_desc	*gdesc = &chip->desc[0];
	int			is_out;
	int			is_irq;

	for (i = 0; i < chip->ngpio; i++, gpio++, gdesc++) {
		if (!test_bit(FLAG_REQUESTED, &gdesc->flags))
			continue;

		gpiod_get_direction(gdesc);
		is_out = test_bit(FLAG_IS_OUT, &gdesc->flags);
		is_irq = test_bit(FLAG_USED_AS_IRQ, &gdesc->flags);
		seq_printf(s, " gpio-%-3d (%-20.20s) %s %s %s",
			gpio, gdesc->label,
			is_out ? "out" : "in ",
			chip->get
				? (chip->get(chip, i) ? "hi" : "lo")
				: "?  ",
			is_irq ? "IRQ" : "   ");
		seq_printf(s, "\n");
	}
}

static void *gpiolib_seq_start(struct seq_file *s, loff_t *pos)
{
	unsigned long flags;
	struct gpio_chip *chip = NULL;
	loff_t index = *pos;

	s->private = "";

	spin_lock_irqsave(&gpio_lock, flags);
	list_for_each_entry(chip, &gpio_chips, list)
		if (index-- == 0) {
			spin_unlock_irqrestore(&gpio_lock, flags);
			return chip;
		}
	spin_unlock_irqrestore(&gpio_lock, flags);

	return NULL;
}

static void *gpiolib_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	unsigned long flags;
	struct gpio_chip *chip = v;
	void *ret = NULL;

	spin_lock_irqsave(&gpio_lock, flags);
	if (list_is_last(&chip->list, &gpio_chips))
		ret = NULL;
	else
		ret = list_entry(chip->list.next, struct gpio_chip, list);
	spin_unlock_irqrestore(&gpio_lock, flags);

	s->private = "\n";
	++*pos;

	return ret;
}

static void gpiolib_seq_stop(struct seq_file *s, void *v)
{
}

static int gpiolib_seq_show(struct seq_file *s, void *v)
{
	struct gpio_chip *chip = v;
	struct device *dev;

	seq_printf(s, "%sGPIOs %d-%d", (char *)s->private,
			chip->base, chip->base + chip->ngpio - 1);
	dev = chip->dev;
	if (dev)
		seq_printf(s, ", %s/%s", dev->bus ? dev->bus->name : "no-bus",
			dev_name(dev));
	if (chip->label)
		seq_printf(s, ", %s", chip->label);
	if (chip->can_sleep)
		seq_printf(s, ", can sleep");
	seq_printf(s, ":\n");

	if (chip->dbg_show)
		chip->dbg_show(s, chip);
	else
		gpiolib_dbg_show(s, chip);

	return 0;
}

static const struct seq_operations gpiolib_seq_ops = {
	.start = gpiolib_seq_start,
	.next = gpiolib_seq_next,
	.stop = gpiolib_seq_stop,
	.show = gpiolib_seq_show,
};

static int gpiolib_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &gpiolib_seq_ops);
}

static const struct file_operations gpiolib_operations = {
	.owner		= THIS_MODULE,
	.open		= gpiolib_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int __init gpiolib_debugfs_init(void)
{
	/* /sys/kernel/debug/gpio */
	(void) debugfs_create_file("gpio", S_IFREG | S_IRUGO,
				NULL, NULL, &gpiolib_operations);
	return 0;
}
subsys_initcall(gpiolib_debugfs_init);

#endif	/* DEBUG_FS */
