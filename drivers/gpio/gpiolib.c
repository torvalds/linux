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

#define CREATE_TRACE_POINTS
#include <trace/events/gpio.h>

/* Optional implementation infrastructure for GPIO interfaces.
 *
 * Platforms may want to use this if they tend to use very many GPIOs
 * that aren't part of a System-On-Chip core; or across I2C/SPI/etc.
 *
 * When kernel footprint or instruction count is an issue, simpler
 * implementations may be preferred.  The GPIO programming interface
 * allows for inlining speed-critical get/set operations for common
 * cases, so that access to SOC-integrated GPIOs can sometimes cost
 * only an instruction or two per bit.
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
#define FLAG_ACTIVE_LOW	6	/* sysfs value has active low */
#define FLAG_OPEN_DRAIN	7	/* Gpio is open drain type */
#define FLAG_OPEN_SOURCE 8	/* Gpio is open source type */

#define ID_SHIFT	16	/* add new flags before this one */

#define GPIO_FLAGS_MASK		((1 << ID_SHIFT) - 1)
#define GPIO_TRIGGER_MASK	(BIT(FLAG_TRIG_FALL) | BIT(FLAG_TRIG_RISE))

#ifdef CONFIG_DEBUG_FS
	const char		*label;
#endif
};
static struct gpio_desc gpio_desc[ARCH_NR_GPIOS];

#define GPIO_OFFSET_VALID(chip, offset) (offset >= 0 && offset < chip->ngpio)

static LIST_HEAD(gpio_chips);

#ifdef CONFIG_GPIO_SYSFS
static DEFINE_IDR(dirent_idr);
#endif

/*
 * Internal gpiod_* API using descriptors instead of the integer namespace.
 * Most of this should eventually go public.
 */
static int gpiod_request(struct gpio_desc *desc, const char *label);
static void gpiod_free(struct gpio_desc *desc);
static int gpiod_direction_input(struct gpio_desc *desc);
static int gpiod_direction_output(struct gpio_desc *desc, int value);
static int gpiod_get_direction(const struct gpio_desc *desc);
static int gpiod_set_debounce(struct gpio_desc *desc, unsigned debounce);
static int gpiod_get_value_cansleep(const struct gpio_desc *desc);
static void gpiod_set_value_cansleep(struct gpio_desc *desc, int value);
static int gpiod_get_value(const struct gpio_desc *desc);
static void gpiod_set_value(struct gpio_desc *desc, int value);
static int gpiod_cansleep(const struct gpio_desc *desc);
static int gpiod_to_irq(const struct gpio_desc *desc);
static int gpiod_export(struct gpio_desc *desc, bool direction_may_change);
static int gpiod_export_link(struct device *dev, const char *name,
			     struct gpio_desc *desc);
static int gpiod_sysfs_set_active_low(struct gpio_desc *desc, int value);
static void gpiod_unexport(struct gpio_desc *desc);


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
static struct gpio_desc *gpio_to_desc(unsigned gpio)
{
	if (WARN(!gpio_is_valid(gpio), "invalid GPIO %d\n", gpio))
		return NULL;
	else
		return &gpio_desc[gpio];
}

/**
 * Convert a GPIO descriptor to the integer namespace.
 * This should disappear in the future but is needed since we still
 * use GPIO numbers for error messages and sysfs nodes
 */
static int desc_to_gpio(const struct gpio_desc *desc)
{
	return desc - &gpio_desc[0];
}


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
			pr_err("GPIO-%d: module can't be gotten \n", gpio);
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

static struct gpio_chip *gpiod_to_chip(const struct gpio_desc *desc)
{
	return desc ? desc->chip : NULL;
}

/* caller holds gpio_lock *OR* gpio is marked as requested */
struct gpio_chip *gpio_to_chip(unsigned gpio)
{
	return gpiod_to_chip(gpio_to_desc(gpio));
}

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

/* caller ensures gpio is valid and requested, chip->get_direction may sleep  */
static int gpiod_get_direction(const struct gpio_desc *desc)
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
		status = gpiod_direction_output(desc, 1);
	else if (sysfs_streq(buf, "out") || sysfs_streq(buf, "low"))
		status = gpiod_direction_output(desc, 0);
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

	if (!test_bit(FLAG_EXPORT, &desc->flags)) {
		status = -EIO;
	} else {
		int value;

		value = !!gpiod_get_value_cansleep(desc);
		if (test_bit(FLAG_ACTIVE_LOW, &desc->flags))
			value = !value;

		status = sprintf(buf, "%d\n", value);
	}

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

		status = strict_strtol(buf, 0, &value);
		if (status == 0) {
			if (test_bit(FLAG_ACTIVE_LOW, &desc->flags))
				value = !value;
			gpiod_set_value_cansleep(desc, value != 0);
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
	struct sysfs_dirent	*value_sd = priv;

	sysfs_notify_dirent(value_sd);
	return IRQ_HANDLED;
}

static int gpio_setup_irq(struct gpio_desc *desc, struct device *dev,
		unsigned long gpio_flags)
{
	struct sysfs_dirent	*value_sd;
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
		value_sd = sysfs_get_dirent(dev->kobj.sd, NULL, "value");
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

		status = strict_strtol(buf, 0, &value);
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

	status = strict_strtol(buf, 0, &gpio);
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

	status = strict_strtol(buf, 0, &gpio);
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
 * gpio_export - export a GPIO through sysfs
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
static int gpiod_export(struct gpio_desc *desc, bool direction_may_change)
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
		pr_debug("%s: gpio %d unavailable (requested=%d, exported=%d)\n",
				__func__, desc_to_gpio(desc),
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
	pr_debug("%s: gpio%d status %d\n", __func__, desc_to_gpio(desc),
		 status);
	return status;
}

int gpio_export(unsigned gpio, bool direction_may_change)
{
	return gpiod_export(gpio_to_desc(gpio), direction_may_change);
}
EXPORT_SYMBOL_GPL(gpio_export);

static int match_export(struct device *dev, const void *data)
{
	return dev_get_drvdata(dev) == data;
}

/**
 * gpio_export_link - create a sysfs link to an exported GPIO node
 * @dev: device under which to create symlink
 * @name: name of the symlink
 * @gpio: gpio to create symlink to, already exported
 *
 * Set up a symlink from /sys/.../dev/name to /sys/class/gpio/gpioN
 * node. Caller is responsible for unlinking.
 *
 * Returns zero on success, else an error.
 */
static int gpiod_export_link(struct device *dev, const char *name,
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
		pr_debug("%s: gpio%d status %d\n", __func__, desc_to_gpio(desc),
			 status);

	return status;
}

int gpio_export_link(struct device *dev, const char *name, unsigned gpio)
{
	return gpiod_export_link(dev, name, gpio_to_desc(gpio));
}
EXPORT_SYMBOL_GPL(gpio_export_link);

/**
 * gpio_sysfs_set_active_low - set the polarity of gpio sysfs value
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
static int gpiod_sysfs_set_active_low(struct gpio_desc *desc, int value)
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
		pr_debug("%s: gpio%d status %d\n", __func__, desc_to_gpio(desc),
			 status);

	return status;
}

int gpio_sysfs_set_active_low(unsigned gpio, int value)
{
	return gpiod_sysfs_set_active_low(gpio_to_desc(gpio), value);
}
EXPORT_SYMBOL_GPL(gpio_sysfs_set_active_low);

/**
 * gpio_unexport - reverse effect of gpio_export()
 * @gpio: gpio to make unavailable
 *
 * This is implicit on gpio_free().
 */
static void gpiod_unexport(struct gpio_desc *desc)
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
		pr_debug("%s: gpio%d status %d\n", __func__, desc_to_gpio(desc),
			 status);
}

void gpio_unexport(unsigned gpio)
{
	gpiod_unexport(gpio_to_desc(gpio));
}
EXPORT_SYMBOL_GPL(gpio_unexport);

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

		pr_debug("%s: chip %s status %d\n", __func__,
				chip->label, status);
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
		chip->exported = 0;
		status = 0;
	} else
		status = -ENODEV;
	mutex_unlock(&sysfs_lock);

	if (status)
		pr_debug("%s: chip %s status %d\n", __func__,
				chip->label, status);
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

static inline int gpiod_export(struct gpio_desc *desc,
			       bool direction_may_change)
{
	return -ENOSYS;
}

static inline int gpiod_export_link(struct device *dev, const char *name,
				    struct gpio_desc *desc)
{
	return -ENOSYS;
}

static inline int gpiod_sysfs_set_active_low(struct gpio_desc *desc, int value)
{
	return -ENOSYS;
}

static inline void gpiod_unexport(struct gpio_desc *desc)
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

	if (status)
		goto fail;

	status = gpiochip_export(chip);
	if (status)
		goto fail;

	pr_debug("gpiochip_add: registered GPIOs %d to %d on device: %s\n",
		chip->base, chip->base + chip->ngpio - 1,
		chip->label ? : "generic");

	return 0;

unlock:
	spin_unlock_irqrestore(&gpio_lock, flags);
fail:
	/* failures here can mean systems won't boot... */
	pr_err("gpiochip_add: gpios %d..%d (%s) failed to register\n",
		chip->base, chip->base + chip->ngpio - 1,
		chip->label ? : "generic");
	return status;
}
EXPORT_SYMBOL_GPL(gpiochip_add);

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

	spin_lock_irqsave(&gpio_lock, flags);

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

#ifdef CONFIG_PINCTRL

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
		pr_err("%s: GPIO chip: failed to allocate pin ranges\n",
				chip->label);
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
		pr_err("%s: GPIO chip: could not create pin range\n",
		       chip->label);
		kfree(pin_range);
		return ret;
	}
	pr_debug("GPIO chip %s: created GPIO range %d->%d ==> %s PIN %d->%d\n",
		 chip->label, gpio_offset, gpio_offset + npins - 1,
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
static int gpiod_request(struct gpio_desc *desc, const char *label)
{
	struct gpio_chip	*chip;
	int			status = -EPROBE_DEFER;
	unsigned long		flags;

	if (!desc) {
		pr_warn("%s: invalid GPIO\n", __func__);
		return -EINVAL;
	}

	spin_lock_irqsave(&gpio_lock, flags);

	chip = desc->chip;
	if (chip == NULL)
		goto done;

	if (!try_module_get(chip->owner))
		goto done;

	/* NOTE:  gpio_request() can be called in early boot,
	 * before IRQs are enabled, for non-sleeping (SOC) GPIOs.
	 */

	if (test_and_set_bit(FLAG_REQUESTED, &desc->flags) == 0) {
		desc_set_label(desc, label ? : "?");
		status = 0;
	} else {
		status = -EBUSY;
		module_put(chip->owner);
		goto done;
	}

	if (chip->request) {
		/* chip->request may sleep */
		spin_unlock_irqrestore(&gpio_lock, flags);
		status = chip->request(chip, gpio_chip_hwgpio(desc));
		spin_lock_irqsave(&gpio_lock, flags);

		if (status < 0) {
			desc_set_label(desc, NULL);
			module_put(chip->owner);
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
	if (status)
		pr_debug("_gpio_request: gpio-%d (%s) status %d\n",
			 desc_to_gpio(desc), label ? : "?", status);
	spin_unlock_irqrestore(&gpio_lock, flags);
	return status;
}

int gpio_request(unsigned gpio, const char *label)
{
	return gpiod_request(gpio_to_desc(gpio), label);
}
EXPORT_SYMBOL_GPL(gpio_request);

static void gpiod_free(struct gpio_desc *desc)
{
	unsigned long		flags;
	struct gpio_chip	*chip;

	might_sleep();

	if (!desc) {
		WARN_ON(extra_checks);
		return;
	}

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
		module_put(desc->chip->owner);
		clear_bit(FLAG_ACTIVE_LOW, &desc->flags);
		clear_bit(FLAG_REQUESTED, &desc->flags);
		clear_bit(FLAG_OPEN_DRAIN, &desc->flags);
		clear_bit(FLAG_OPEN_SOURCE, &desc->flags);
	} else
		WARN_ON(extra_checks);

	spin_unlock_irqrestore(&gpio_lock, flags);
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
		err = gpiod_direction_output(desc,
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


/* Drivers MUST set GPIO direction before making get/set calls.  In
 * some cases this is done in early boot, before IRQs are enabled.
 *
 * As a rule these aren't called more than once (except for drivers
 * using the open-drain emulation idiom) so these are natural places
 * to accumulate extra debugging checks.  Note that we can't (yet)
 * rely on gpio_request() having been called beforehand.
 */

static int gpiod_direction_input(struct gpio_desc *desc)
{
	unsigned long		flags;
	struct gpio_chip	*chip;
	int			status = -EINVAL;
	int			offset;

	if (!desc) {
		pr_warn("%s: invalid GPIO\n", __func__);
		return -EINVAL;
	}

	spin_lock_irqsave(&gpio_lock, flags);

	chip = desc->chip;
	if (!chip || !chip->get || !chip->direction_input)
		goto fail;
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
			pr_debug("GPIO-%d: chip request fail, %d\n",
				desc_to_gpio(desc), status);
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
		pr_debug("%s: gpio-%d status %d\n", __func__,
			 desc_to_gpio(desc), status);
	return status;
}

int gpio_direction_input(unsigned gpio)
{
	return gpiod_direction_input(gpio_to_desc(gpio));
}
EXPORT_SYMBOL_GPL(gpio_direction_input);

static int gpiod_direction_output(struct gpio_desc *desc, int value)
{
	unsigned long		flags;
	struct gpio_chip	*chip;
	int			status = -EINVAL;
	int offset;

	if (!desc) {
		pr_warn("%s: invalid GPIO\n", __func__);
		return -EINVAL;
	}

	/* Open drain pin should not be driven to 1 */
	if (value && test_bit(FLAG_OPEN_DRAIN,  &desc->flags))
		return gpiod_direction_input(desc);

	/* Open source pin should not be driven to 0 */
	if (!value && test_bit(FLAG_OPEN_SOURCE,  &desc->flags))
		return gpiod_direction_input(desc);

	spin_lock_irqsave(&gpio_lock, flags);

	chip = desc->chip;
	if (!chip || !chip->set || !chip->direction_output)
		goto fail;
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
			pr_debug("GPIO-%d: chip request fail, %d\n",
				desc_to_gpio(desc), status);
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
		pr_debug("%s: gpio-%d status %d\n", __func__,
			 desc_to_gpio(desc), status);
	return status;
}

int gpio_direction_output(unsigned gpio, int value)
{
	return gpiod_direction_output(gpio_to_desc(gpio), value);
}
EXPORT_SYMBOL_GPL(gpio_direction_output);

/**
 * gpio_set_debounce - sets @debounce time for a @gpio
 * @gpio: the gpio to set debounce time
 * @debounce: debounce time is microseconds
 */
static int gpiod_set_debounce(struct gpio_desc *desc, unsigned debounce)
{
	unsigned long		flags;
	struct gpio_chip	*chip;
	int			status = -EINVAL;
	int			offset;

	if (!desc) {
		pr_warn("%s: invalid GPIO\n", __func__);
		return -EINVAL;
	}

	spin_lock_irqsave(&gpio_lock, flags);

	chip = desc->chip;
	if (!chip || !chip->set || !chip->set_debounce)
		goto fail;

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
		pr_debug("%s: gpio-%d status %d\n", __func__,
			 desc_to_gpio(desc), status);

	return status;
}

int gpio_set_debounce(unsigned gpio, unsigned debounce)
{
	return gpiod_set_debounce(gpio_to_desc(gpio), debounce);
}
EXPORT_SYMBOL_GPL(gpio_set_debounce);

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

/**
 * __gpio_get_value() - return a gpio's value
 * @gpio: gpio whose value will be returned
 * Context: any
 *
 * This is used directly or indirectly to implement gpio_get_value().
 * It returns the zero or nonzero value provided by the associated
 * gpio_chip.get() method; or zero if no such method is provided.
 */
static int gpiod_get_value(const struct gpio_desc *desc)
{
	struct gpio_chip	*chip;
	int value;
	int offset;

	if (!desc)
		return 0;
	chip = desc->chip;
	offset = gpio_chip_hwgpio(desc);
	/* Should be using gpio_get_value_cansleep() */
	WARN_ON(chip->can_sleep);
	value = chip->get ? chip->get(chip, offset) : 0;
	trace_gpio_value(desc_to_gpio(desc), 1, value);
	return value;
}

int __gpio_get_value(unsigned gpio)
{
	return gpiod_get_value(gpio_to_desc(gpio));
}
EXPORT_SYMBOL_GPL(__gpio_get_value);

/*
 *  _gpio_set_open_drain_value() - Set the open drain gpio's value.
 * @gpio: Gpio whose state need to be set.
 * @chip: Gpio chip.
 * @value: Non-zero for setting it HIGH otherise it will set to LOW.
 */
static void _gpio_set_open_drain_value(struct gpio_desc *desc, int value)
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
		pr_err("%s: Error in set_value for open drain gpio%d err %d\n",
					__func__, desc_to_gpio(desc), err);
}

/*
 *  _gpio_set_open_source() - Set the open source gpio's value.
 * @gpio: Gpio whose state need to be set.
 * @chip: Gpio chip.
 * @value: Non-zero for setting it HIGH otherise it will set to LOW.
 */
static void _gpio_set_open_source_value(struct gpio_desc *desc, int value)
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
		pr_err("%s: Error in set_value for open source gpio%d err %d\n",
					__func__, desc_to_gpio(desc), err);
}

/**
 * __gpio_set_value() - assign a gpio's value
 * @gpio: gpio whose value will be assigned
 * @value: value to assign
 * Context: any
 *
 * This is used directly or indirectly to implement gpio_set_value().
 * It invokes the associated gpio_chip.set() method.
 */
static void gpiod_set_value(struct gpio_desc *desc, int value)
{
	struct gpio_chip	*chip;

	if (!desc)
		return;
	chip = desc->chip;
	/* Should be using gpio_set_value_cansleep() */
	WARN_ON(chip->can_sleep);
	trace_gpio_value(desc_to_gpio(desc), 0, value);
	if (test_bit(FLAG_OPEN_DRAIN, &desc->flags))
		_gpio_set_open_drain_value(desc, value);
	else if (test_bit(FLAG_OPEN_SOURCE, &desc->flags))
		_gpio_set_open_source_value(desc, value);
	else
		chip->set(chip, gpio_chip_hwgpio(desc), value);
}

void __gpio_set_value(unsigned gpio, int value)
{
	return gpiod_set_value(gpio_to_desc(gpio), value);
}
EXPORT_SYMBOL_GPL(__gpio_set_value);

/**
 * __gpio_cansleep() - report whether gpio value access will sleep
 * @gpio: gpio in question
 * Context: any
 *
 * This is used directly or indirectly to implement gpio_cansleep().  It
 * returns nonzero if access reading or writing the GPIO value can sleep.
 */
static int gpiod_cansleep(const struct gpio_desc *desc)
{
	if (!desc)
		return 0;
	/* only call this on GPIOs that are valid! */
	return desc->chip->can_sleep;
}

int __gpio_cansleep(unsigned gpio)
{
	return gpiod_cansleep(gpio_to_desc(gpio));
}
EXPORT_SYMBOL_GPL(__gpio_cansleep);

/**
 * __gpio_to_irq() - return the IRQ corresponding to a GPIO
 * @gpio: gpio whose IRQ will be returned (already requested)
 * Context: any
 *
 * This is used directly or indirectly to implement gpio_to_irq().
 * It returns the number of the IRQ signaled by this (input) GPIO,
 * or a negative errno.
 */
static int gpiod_to_irq(const struct gpio_desc *desc)
{
	struct gpio_chip	*chip;
	int			offset;

	if (!desc)
		return -EINVAL;
	chip = desc->chip;
	offset = gpio_chip_hwgpio(desc);
	return chip->to_irq ? chip->to_irq(chip, offset) : -ENXIO;
}

int __gpio_to_irq(unsigned gpio)
{
	return gpiod_to_irq(gpio_to_desc(gpio));
}
EXPORT_SYMBOL_GPL(__gpio_to_irq);


/* There's no value in making it easy to inline GPIO calls that may sleep.
 * Common examples include ones connected to I2C or SPI chips.
 */

static int gpiod_get_value_cansleep(const struct gpio_desc *desc)
{
	struct gpio_chip	*chip;
	int value;
	int offset;

	might_sleep_if(extra_checks);
	if (!desc)
		return 0;
	chip = desc->chip;
	offset = gpio_chip_hwgpio(desc);
	value = chip->get ? chip->get(chip, offset) : 0;
	trace_gpio_value(desc_to_gpio(desc), 1, value);
	return value;
}

int gpio_get_value_cansleep(unsigned gpio)
{
	return gpiod_get_value_cansleep(gpio_to_desc(gpio));
}
EXPORT_SYMBOL_GPL(gpio_get_value_cansleep);

static void gpiod_set_value_cansleep(struct gpio_desc *desc, int value)
{
	struct gpio_chip	*chip;

	might_sleep_if(extra_checks);
	if (!desc)
		return;
	chip = desc->chip;
	trace_gpio_value(desc_to_gpio(desc), 0, value);
	if (test_bit(FLAG_OPEN_DRAIN,  &desc->flags))
		_gpio_set_open_drain_value(desc, value);
	else if (test_bit(FLAG_OPEN_SOURCE,  &desc->flags))
		_gpio_set_open_source_value(desc, value);
	else
		chip->set(chip, gpio_chip_hwgpio(desc), value);
}

void gpio_set_value_cansleep(unsigned gpio, int value)
{
	return gpiod_set_value_cansleep(gpio_to_desc(gpio), value);
}
EXPORT_SYMBOL_GPL(gpio_set_value_cansleep);

#ifdef CONFIG_DEBUG_FS

static void gpiolib_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	unsigned		i;
	unsigned		gpio = chip->base;
	struct gpio_desc	*gdesc = &chip->desc[0];
	int			is_out;

	for (i = 0; i < chip->ngpio; i++, gpio++, gdesc++) {
		if (!test_bit(FLAG_REQUESTED, &gdesc->flags))
			continue;

		gpiod_get_direction(gdesc);
		is_out = test_bit(FLAG_IS_OUT, &gdesc->flags);
		seq_printf(s, " gpio-%-3d (%-20.20s) %s %s",
			gpio, gdesc->label,
			is_out ? "out" : "in ",
			chip->get
				? (chip->get(chip, i) ? "hi" : "lo")
				: "?  ");
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
