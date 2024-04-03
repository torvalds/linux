// SPDX-License-Identifier: GPL-2.0

#include <linux/acpi.h>
#include <linux/bitmap.h>
#include <linux/cleanup.h>
#include <linux/compat.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/srcu.h>
#include <linux/string.h>

#include <linux/gpio.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/machine.h>

#include <uapi/linux/gpio.h>

#include "gpiolib-acpi.h"
#include "gpiolib-cdev.h"
#include "gpiolib-of.h"
#include "gpiolib-swnode.h"
#include "gpiolib-sysfs.h"
#include "gpiolib.h"

#define CREATE_TRACE_POINTS
#include <trace/events/gpio.h>

/* Implementation infrastructure for GPIO interfaces.
 *
 * The GPIO programming interface allows for inlining speed-critical
 * get/set operations for common cases, so that access to SOC-integrated
 * GPIOs can sometimes cost only an instruction or two per bit.
 */

/* Device and char device-related information */
static DEFINE_IDA(gpio_ida);
static dev_t gpio_devt;
#define GPIO_DEV_MAX 256 /* 256 GPIO chip devices supported */

static int gpio_bus_match(struct device *dev, struct device_driver *drv)
{
	struct fwnode_handle *fwnode = dev_fwnode(dev);

	/*
	 * Only match if the fwnode doesn't already have a proper struct device
	 * created for it.
	 */
	if (fwnode && fwnode->dev != dev)
		return 0;
	return 1;
}

static const struct bus_type gpio_bus_type = {
	.name = "gpio",
	.match = gpio_bus_match,
};

/*
 * Number of GPIOs to use for the fast path in set array
 */
#define FASTPATH_NGPIO CONFIG_GPIOLIB_FASTPATH_LIMIT

static DEFINE_MUTEX(gpio_lookup_lock);
static LIST_HEAD(gpio_lookup_list);

static LIST_HEAD(gpio_devices);
/* Protects the GPIO device list against concurrent modifications. */
static DEFINE_MUTEX(gpio_devices_lock);
/* Ensures coherence during read-only accesses to the list of GPIO devices. */
DEFINE_STATIC_SRCU(gpio_devices_srcu);

static DEFINE_MUTEX(gpio_machine_hogs_mutex);
static LIST_HEAD(gpio_machine_hogs);

static void gpiochip_free_hogs(struct gpio_chip *gc);
static int gpiochip_add_irqchip(struct gpio_chip *gc,
				struct lock_class_key *lock_key,
				struct lock_class_key *request_key);
static void gpiochip_irqchip_remove(struct gpio_chip *gc);
static int gpiochip_irqchip_init_hw(struct gpio_chip *gc);
static int gpiochip_irqchip_init_valid_mask(struct gpio_chip *gc);
static void gpiochip_irqchip_free_valid_mask(struct gpio_chip *gc);

static bool gpiolib_initialized;

const char *gpiod_get_label(struct gpio_desc *desc)
{
	unsigned long flags;

	flags = READ_ONCE(desc->flags);
	if (test_bit(FLAG_USED_AS_IRQ, &flags) &&
	    !test_bit(FLAG_REQUESTED, &flags))
		return "interrupt";

	return test_bit(FLAG_REQUESTED, &flags) ?
			srcu_dereference(desc->label, &desc->srcu) : NULL;
}

static int desc_set_label(struct gpio_desc *desc, const char *label)
{
	const char *new = NULL, *old;

	if (label) {
		new = kstrdup_const(label, GFP_KERNEL);
		if (!new)
			return -ENOMEM;
	}

	old = rcu_replace_pointer(desc->label, new, 1);
	synchronize_srcu(&desc->srcu);
	kfree_const(old);

	return 0;
}

/**
 * gpio_to_desc - Convert a GPIO number to its descriptor
 * @gpio: global GPIO number
 *
 * Returns:
 * The GPIO descriptor associated with the given GPIO, or %NULL if no GPIO
 * with the given number exists in the system.
 */
struct gpio_desc *gpio_to_desc(unsigned gpio)
{
	struct gpio_device *gdev;

	scoped_guard(srcu, &gpio_devices_srcu) {
		list_for_each_entry_srcu(gdev, &gpio_devices, list,
				srcu_read_lock_held(&gpio_devices_srcu)) {
			if (gdev->base <= gpio &&
			    gdev->base + gdev->ngpio > gpio)
				return &gdev->descs[gpio - gdev->base];
		}
	}

	if (!gpio_is_valid(gpio))
		pr_warn("invalid GPIO %d\n", gpio);

	return NULL;
}
EXPORT_SYMBOL_GPL(gpio_to_desc);

/* This function is deprecated and will be removed soon, don't use. */
struct gpio_desc *gpiochip_get_desc(struct gpio_chip *gc,
				    unsigned int hwnum)
{
	return gpio_device_get_desc(gc->gpiodev, hwnum);
}
EXPORT_SYMBOL_GPL(gpiochip_get_desc);

/**
 * gpio_device_get_desc() - get the GPIO descriptor corresponding to the given
 *                          hardware number for this GPIO device
 * @gdev: GPIO device to get the descriptor from
 * @hwnum: hardware number of the GPIO for this chip
 *
 * Returns:
 * A pointer to the GPIO descriptor or %EINVAL if no GPIO exists in the given
 * chip for the specified hardware number or %ENODEV if the underlying chip
 * already vanished.
 *
 * The reference count of struct gpio_device is *NOT* increased like when the
 * GPIO is being requested for exclusive usage. It's up to the caller to make
 * sure the GPIO device will stay alive together with the descriptor returned
 * by this function.
 */
struct gpio_desc *
gpio_device_get_desc(struct gpio_device *gdev, unsigned int hwnum)
{
	if (hwnum >= gdev->ngpio)
		return ERR_PTR(-EINVAL);

	return &gdev->descs[hwnum];
}
EXPORT_SYMBOL_GPL(gpio_device_get_desc);

/**
 * desc_to_gpio - convert a GPIO descriptor to the integer namespace
 * @desc: GPIO descriptor
 *
 * This should disappear in the future but is needed since we still
 * use GPIO numbers for error messages and sysfs nodes.
 *
 * Returns:
 * The global GPIO number for the GPIO specified by its descriptor.
 */
int desc_to_gpio(const struct gpio_desc *desc)
{
	return desc->gdev->base + (desc - &desc->gdev->descs[0]);
}
EXPORT_SYMBOL_GPL(desc_to_gpio);


/**
 * gpiod_to_chip - Return the GPIO chip to which a GPIO descriptor belongs
 * @desc:	descriptor to return the chip of
 *
 * *DEPRECATED*
 * This function is unsafe and should not be used. Using the chip address
 * without taking the SRCU read lock may result in dereferencing a dangling
 * pointer.
 */
struct gpio_chip *gpiod_to_chip(const struct gpio_desc *desc)
{
	if (!desc)
		return NULL;

	return gpio_device_get_chip(desc->gdev);
}
EXPORT_SYMBOL_GPL(gpiod_to_chip);

/**
 * gpiod_to_gpio_device() - Return the GPIO device to which this descriptor
 *                          belongs.
 * @desc: Descriptor for which to return the GPIO device.
 *
 * This *DOES NOT* increase the reference count of the GPIO device as it's
 * expected that the descriptor is requested and the users already holds a
 * reference to the device.
 *
 * Returns:
 * Address of the GPIO device owning this descriptor.
 */
struct gpio_device *gpiod_to_gpio_device(struct gpio_desc *desc)
{
	if (!desc)
		return NULL;

	return desc->gdev;
}
EXPORT_SYMBOL_GPL(gpiod_to_gpio_device);

/**
 * gpio_device_get_base() - Get the base GPIO number allocated by this device
 * @gdev: GPIO device
 *
 * Returns:
 * First GPIO number in the global GPIO numberspace for this device.
 */
int gpio_device_get_base(struct gpio_device *gdev)
{
	return gdev->base;
}
EXPORT_SYMBOL_GPL(gpio_device_get_base);

/**
 * gpio_device_get_label() - Get the label of this GPIO device
 * @gdev: GPIO device
 *
 * Returns:
 * Pointer to the string containing the GPIO device label. The string's
 * lifetime is tied to that of the underlying GPIO device.
 */
const char *gpio_device_get_label(struct gpio_device *gdev)
{
	return gdev->label;
}
EXPORT_SYMBOL(gpio_device_get_label);

/**
 * gpio_device_get_chip() - Get the gpio_chip implementation of this GPIO device
 * @gdev: GPIO device
 *
 * Returns:
 * Address of the GPIO chip backing this device.
 *
 * *DEPRECATED*
 * Until we can get rid of all non-driver users of struct gpio_chip, we must
 * provide a way of retrieving the pointer to it from struct gpio_device. This
 * is *NOT* safe as the GPIO API is considered to be hot-unpluggable and the
 * chip can dissapear at any moment (unlike reference-counted struct
 * gpio_device).
 *
 * Use at your own risk.
 */
struct gpio_chip *gpio_device_get_chip(struct gpio_device *gdev)
{
	return rcu_dereference_check(gdev->chip, 1);
}
EXPORT_SYMBOL_GPL(gpio_device_get_chip);

/* dynamic allocation of GPIOs, e.g. on a hotplugged device */
static int gpiochip_find_base_unlocked(int ngpio)
{
	struct gpio_device *gdev;
	int base = GPIO_DYNAMIC_BASE;

	list_for_each_entry_srcu(gdev, &gpio_devices, list,
				 lockdep_is_held(&gpio_devices_lock)) {
		/* found a free space? */
		if (gdev->base >= base + ngpio)
			break;
		/* nope, check the space right after the chip */
		base = gdev->base + gdev->ngpio;
		if (base < GPIO_DYNAMIC_BASE)
			base = GPIO_DYNAMIC_BASE;
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
 * Returns 0 for output, 1 for input, or an error code in case of error.
 *
 * This function may sleep if gpiod_cansleep() is true.
 */
int gpiod_get_direction(struct gpio_desc *desc)
{
	unsigned long flags;
	unsigned int offset;
	int ret;

	/*
	 * We cannot use VALIDATE_DESC() as we must not return 0 for a NULL
	 * descriptor like we usually do.
	 */
	if (!desc || IS_ERR(desc))
		return -EINVAL;

	CLASS(gpio_chip_guard, guard)(desc);
	if (!guard.gc)
		return -ENODEV;

	offset = gpio_chip_hwgpio(desc);
	flags = READ_ONCE(desc->flags);

	/*
	 * Open drain emulation using input mode may incorrectly report
	 * input here, fix that up.
	 */
	if (test_bit(FLAG_OPEN_DRAIN, &flags) &&
	    test_bit(FLAG_IS_OUT, &flags))
		return 0;

	if (!guard.gc->get_direction)
		return -ENOTSUPP;

	ret = guard.gc->get_direction(guard.gc, offset);
	if (ret < 0)
		return ret;

	/* GPIOF_DIR_IN or other positive, otherwise GPIOF_DIR_OUT */
	if (ret > 0)
		ret = 1;

	assign_bit(FLAG_IS_OUT, &flags, !ret);
	WRITE_ONCE(desc->flags, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(gpiod_get_direction);

/*
 * Add a new chip to the global chips list, keeping the list of chips sorted
 * by range(means [base, base + ngpio - 1]) order.
 *
 * Return -EBUSY if the new chip overlaps with some other chip's integer
 * space.
 */
static int gpiodev_add_to_list_unlocked(struct gpio_device *gdev)
{
	struct gpio_device *prev, *next;

	lockdep_assert_held(&gpio_devices_lock);

	if (list_empty(&gpio_devices)) {
		/* initial entry in list */
		list_add_tail_rcu(&gdev->list, &gpio_devices);
		return 0;
	}

	next = list_first_entry(&gpio_devices, struct gpio_device, list);
	if (gdev->base + gdev->ngpio <= next->base) {
		/* add before first entry */
		list_add_rcu(&gdev->list, &gpio_devices);
		return 0;
	}

	prev = list_last_entry(&gpio_devices, struct gpio_device, list);
	if (prev->base + prev->ngpio <= gdev->base) {
		/* add behind last entry */
		list_add_tail_rcu(&gdev->list, &gpio_devices);
		return 0;
	}

	list_for_each_entry_safe(prev, next, &gpio_devices, list) {
		/* at the end of the list */
		if (&next->list == &gpio_devices)
			break;

		/* add between prev and next */
		if (prev->base + prev->ngpio <= gdev->base
				&& gdev->base + gdev->ngpio <= next->base) {
			list_add_rcu(&gdev->list, &prev->list);
			return 0;
		}
	}

	synchronize_srcu(&gpio_devices_srcu);

	return -EBUSY;
}

/*
 * Convert a GPIO name to its descriptor
 * Note that there is no guarantee that GPIO names are globally unique!
 * Hence this function will return, if it exists, a reference to the first GPIO
 * line found that matches the given name.
 */
static struct gpio_desc *gpio_name_to_desc(const char * const name)
{
	struct gpio_device *gdev;
	struct gpio_desc *desc;
	struct gpio_chip *gc;

	if (!name)
		return NULL;

	guard(srcu)(&gpio_devices_srcu);

	list_for_each_entry_srcu(gdev, &gpio_devices, list,
				 srcu_read_lock_held(&gpio_devices_srcu)) {
		guard(srcu)(&gdev->srcu);

		gc = srcu_dereference(gdev->chip, &gdev->srcu);
		if (!gc)
			continue;

		for_each_gpio_desc(gc, desc) {
			if (desc->name && !strcmp(desc->name, name))
				return desc;
		}
	}

	return NULL;
}

/*
 * Take the names from gc->names and assign them to their GPIO descriptors.
 * Warn if a name is already used for a GPIO line on a different GPIO chip.
 *
 * Note that:
 *   1. Non-unique names are still accepted,
 *   2. Name collisions within the same GPIO chip are not reported.
 */
static int gpiochip_set_desc_names(struct gpio_chip *gc)
{
	struct gpio_device *gdev = gc->gpiodev;
	int i;

	/* First check all names if they are unique */
	for (i = 0; i != gc->ngpio; ++i) {
		struct gpio_desc *gpio;

		gpio = gpio_name_to_desc(gc->names[i]);
		if (gpio)
			dev_warn(&gdev->dev,
				 "Detected name collision for GPIO name '%s'\n",
				 gc->names[i]);
	}

	/* Then add all names to the GPIO descriptors */
	for (i = 0; i != gc->ngpio; ++i)
		gdev->descs[i].name = gc->names[i];

	return 0;
}

/*
 * gpiochip_set_names - Set GPIO line names using device properties
 * @chip: GPIO chip whose lines should be named, if possible
 *
 * Looks for device property "gpio-line-names" and if it exists assigns
 * GPIO line names for the chip. The memory allocated for the assigned
 * names belong to the underlying firmware node and should not be released
 * by the caller.
 */
static int gpiochip_set_names(struct gpio_chip *chip)
{
	struct gpio_device *gdev = chip->gpiodev;
	struct device *dev = &gdev->dev;
	const char **names;
	int ret, i;
	int count;

	count = device_property_string_array_count(dev, "gpio-line-names");
	if (count < 0)
		return 0;

	/*
	 * When offset is set in the driver side we assume the driver internally
	 * is using more than one gpiochip per the same device. We have to stop
	 * setting friendly names if the specified ones with 'gpio-line-names'
	 * are less than the offset in the device itself. This means all the
	 * lines are not present for every single pin within all the internal
	 * gpiochips.
	 */
	if (count <= chip->offset) {
		dev_warn(dev, "gpio-line-names too short (length %d), cannot map names for the gpiochip at offset %u\n",
			 count, chip->offset);
		return 0;
	}

	names = kcalloc(count, sizeof(*names), GFP_KERNEL);
	if (!names)
		return -ENOMEM;

	ret = device_property_read_string_array(dev, "gpio-line-names",
						names, count);
	if (ret < 0) {
		dev_warn(dev, "failed to read GPIO line names\n");
		kfree(names);
		return ret;
	}

	/*
	 * When more that one gpiochip per device is used, 'count' can
	 * contain at most number gpiochips x chip->ngpio. We have to
	 * correctly distribute all defined lines taking into account
	 * chip->offset as starting point from where we will assign
	 * the names to pins from the 'names' array. Since property
	 * 'gpio-line-names' cannot contains gaps, we have to be sure
	 * we only assign those pins that really exists since chip->ngpio
	 * can be different of the chip->offset.
	 */
	count = (count > chip->offset) ? count - chip->offset : count;
	if (count > chip->ngpio)
		count = chip->ngpio;

	for (i = 0; i < count; i++) {
		/*
		 * Allow overriding "fixed" names provided by the GPIO
		 * provider. The "fixed" names are more often than not
		 * generic and less informative than the names given in
		 * device properties.
		 */
		if (names[chip->offset + i] && names[chip->offset + i][0])
			gdev->descs[i].name = names[chip->offset + i];
	}

	kfree(names);

	return 0;
}

static unsigned long *gpiochip_allocate_mask(struct gpio_chip *gc)
{
	unsigned long *p;

	p = bitmap_alloc(gc->ngpio, GFP_KERNEL);
	if (!p)
		return NULL;

	/* Assume by default all GPIOs are valid */
	bitmap_fill(p, gc->ngpio);

	return p;
}

static void gpiochip_free_mask(unsigned long **p)
{
	bitmap_free(*p);
	*p = NULL;
}

static unsigned int gpiochip_count_reserved_ranges(struct gpio_chip *gc)
{
	struct device *dev = &gc->gpiodev->dev;
	int size;

	/* Format is "start, count, ..." */
	size = device_property_count_u32(dev, "gpio-reserved-ranges");
	if (size > 0 && size % 2 == 0)
		return size;

	return 0;
}

static int gpiochip_apply_reserved_ranges(struct gpio_chip *gc)
{
	struct device *dev = &gc->gpiodev->dev;
	unsigned int size;
	u32 *ranges;
	int ret;

	size = gpiochip_count_reserved_ranges(gc);
	if (size == 0)
		return 0;

	ranges = kmalloc_array(size, sizeof(*ranges), GFP_KERNEL);
	if (!ranges)
		return -ENOMEM;

	ret = device_property_read_u32_array(dev, "gpio-reserved-ranges",
					     ranges, size);
	if (ret) {
		kfree(ranges);
		return ret;
	}

	while (size) {
		u32 count = ranges[--size];
		u32 start = ranges[--size];

		if (start >= gc->ngpio || start + count > gc->ngpio)
			continue;

		bitmap_clear(gc->valid_mask, start, count);
	}

	kfree(ranges);
	return 0;
}

static int gpiochip_init_valid_mask(struct gpio_chip *gc)
{
	int ret;

	if (!(gpiochip_count_reserved_ranges(gc) || gc->init_valid_mask))
		return 0;

	gc->valid_mask = gpiochip_allocate_mask(gc);
	if (!gc->valid_mask)
		return -ENOMEM;

	ret = gpiochip_apply_reserved_ranges(gc);
	if (ret)
		return ret;

	if (gc->init_valid_mask)
		return gc->init_valid_mask(gc,
					   gc->valid_mask,
					   gc->ngpio);

	return 0;
}

static void gpiochip_free_valid_mask(struct gpio_chip *gc)
{
	gpiochip_free_mask(&gc->valid_mask);
}

static int gpiochip_add_pin_ranges(struct gpio_chip *gc)
{
	/*
	 * Device Tree platforms are supposed to use "gpio-ranges"
	 * property. This check ensures that the ->add_pin_ranges()
	 * won't be called for them.
	 */
	if (device_property_present(&gc->gpiodev->dev, "gpio-ranges"))
		return 0;

	if (gc->add_pin_ranges)
		return gc->add_pin_ranges(gc);

	return 0;
}

bool gpiochip_line_is_valid(const struct gpio_chip *gc,
				unsigned int offset)
{
	/* No mask means all valid */
	if (likely(!gc->valid_mask))
		return true;
	return test_bit(offset, gc->valid_mask);
}
EXPORT_SYMBOL_GPL(gpiochip_line_is_valid);

static void gpiodev_release(struct device *dev)
{
	struct gpio_device *gdev = to_gpio_device(dev);
	unsigned int i;

	for (i = 0; i < gdev->ngpio; i++)
		cleanup_srcu_struct(&gdev->descs[i].srcu);

	ida_free(&gpio_ida, gdev->id);
	kfree_const(gdev->label);
	kfree(gdev->descs);
	cleanup_srcu_struct(&gdev->srcu);
	kfree(gdev);
}

static const struct device_type gpio_dev_type = {
	.name = "gpio_chip",
	.release = gpiodev_release,
};

#ifdef CONFIG_GPIO_CDEV
#define gcdev_register(gdev, devt)	gpiolib_cdev_register((gdev), (devt))
#define gcdev_unregister(gdev)		gpiolib_cdev_unregister((gdev))
#else
/*
 * gpiolib_cdev_register() indirectly calls device_add(), which is still
 * required even when cdev is not selected.
 */
#define gcdev_register(gdev, devt)	device_add(&(gdev)->dev)
#define gcdev_unregister(gdev)		device_del(&(gdev)->dev)
#endif

static int gpiochip_setup_dev(struct gpio_device *gdev)
{
	struct fwnode_handle *fwnode = dev_fwnode(&gdev->dev);
	int ret;

	device_initialize(&gdev->dev);

	/*
	 * If fwnode doesn't belong to another device, it's safe to clear its
	 * initialized flag.
	 */
	if (fwnode && !fwnode->dev)
		fwnode_dev_initialized(fwnode, false);

	ret = gcdev_register(gdev, gpio_devt);
	if (ret)
		return ret;

	ret = gpiochip_sysfs_register(gdev);
	if (ret)
		goto err_remove_device;

	dev_dbg(&gdev->dev, "registered GPIOs %d to %d on %s\n", gdev->base,
		gdev->base + gdev->ngpio - 1, gdev->label);

	return 0;

err_remove_device:
	gcdev_unregister(gdev);
	return ret;
}

static void gpiochip_machine_hog(struct gpio_chip *gc, struct gpiod_hog *hog)
{
	struct gpio_desc *desc;
	int rv;

	desc = gpiochip_get_desc(gc, hog->chip_hwnum);
	if (IS_ERR(desc)) {
		chip_err(gc, "%s: unable to get GPIO desc: %ld\n", __func__,
			 PTR_ERR(desc));
		return;
	}

	rv = gpiod_hog(desc, hog->line_name, hog->lflags, hog->dflags);
	if (rv)
		gpiod_err(desc, "%s: unable to hog GPIO line (%s:%u): %d\n",
			  __func__, gc->label, hog->chip_hwnum, rv);
}

static void machine_gpiochip_add(struct gpio_chip *gc)
{
	struct gpiod_hog *hog;

	mutex_lock(&gpio_machine_hogs_mutex);

	list_for_each_entry(hog, &gpio_machine_hogs, list) {
		if (!strcmp(gc->label, hog->chip_label))
			gpiochip_machine_hog(gc, hog);
	}

	mutex_unlock(&gpio_machine_hogs_mutex);
}

static void gpiochip_setup_devs(void)
{
	struct gpio_device *gdev;
	int ret;

	guard(srcu)(&gpio_devices_srcu);

	list_for_each_entry_srcu(gdev, &gpio_devices, list,
				 srcu_read_lock_held(&gpio_devices_srcu)) {
		ret = gpiochip_setup_dev(gdev);
		if (ret)
			dev_err(&gdev->dev,
				"Failed to initialize gpio device (%d)\n", ret);
	}
}

static void gpiochip_set_data(struct gpio_chip *gc, void *data)
{
	gc->gpiodev->data = data;
}

/**
 * gpiochip_get_data() - get per-subdriver data for the chip
 * @gc: GPIO chip
 *
 * Returns:
 * The per-subdriver data for the chip.
 */
void *gpiochip_get_data(struct gpio_chip *gc)
{
	return gc->gpiodev->data;
}
EXPORT_SYMBOL_GPL(gpiochip_get_data);

int gpiochip_get_ngpios(struct gpio_chip *gc, struct device *dev)
{
	u32 ngpios = gc->ngpio;
	int ret;

	if (ngpios == 0) {
		ret = device_property_read_u32(dev, "ngpios", &ngpios);
		if (ret == -ENODATA)
			/*
			 * -ENODATA means that there is no property found and
			 * we want to issue the error message to the user.
			 * Besides that, we want to return different error code
			 * to state that supplied value is not valid.
			 */
			ngpios = 0;
		else if (ret)
			return ret;

		gc->ngpio = ngpios;
	}

	if (gc->ngpio == 0) {
		chip_err(gc, "tried to insert a GPIO chip with zero lines\n");
		return -EINVAL;
	}

	if (gc->ngpio > FASTPATH_NGPIO)
		chip_warn(gc, "line cnt %u is greater than fast path cnt %u\n",
			gc->ngpio, FASTPATH_NGPIO);

	return 0;
}
EXPORT_SYMBOL_GPL(gpiochip_get_ngpios);

int gpiochip_add_data_with_key(struct gpio_chip *gc, void *data,
			       struct lock_class_key *lock_key,
			       struct lock_class_key *request_key)
{
	struct gpio_device *gdev;
	unsigned int desc_index;
	int base = 0;
	int ret = 0;

	/*
	 * First: allocate and populate the internal stat container, and
	 * set up the struct device.
	 */
	gdev = kzalloc(sizeof(*gdev), GFP_KERNEL);
	if (!gdev)
		return -ENOMEM;

	gdev->dev.type = &gpio_dev_type;
	gdev->dev.bus = &gpio_bus_type;
	gdev->dev.parent = gc->parent;
	rcu_assign_pointer(gdev->chip, gc);

	gc->gpiodev = gdev;
	gpiochip_set_data(gc, data);

	/*
	 * If the calling driver did not initialize firmware node,
	 * do it here using the parent device, if any.
	 */
	if (gc->fwnode)
		device_set_node(&gdev->dev, gc->fwnode);
	else if (gc->parent)
		device_set_node(&gdev->dev, dev_fwnode(gc->parent));

	gdev->id = ida_alloc(&gpio_ida, GFP_KERNEL);
	if (gdev->id < 0) {
		ret = gdev->id;
		goto err_free_gdev;
	}

	ret = dev_set_name(&gdev->dev, GPIOCHIP_NAME "%d", gdev->id);
	if (ret)
		goto err_free_ida;

	if (gc->parent && gc->parent->driver)
		gdev->owner = gc->parent->driver->owner;
	else if (gc->owner)
		/* TODO: remove chip->owner */
		gdev->owner = gc->owner;
	else
		gdev->owner = THIS_MODULE;

	ret = gpiochip_get_ngpios(gc, &gdev->dev);
	if (ret)
		goto err_free_dev_name;

	gdev->descs = kcalloc(gc->ngpio, sizeof(*gdev->descs), GFP_KERNEL);
	if (!gdev->descs) {
		ret = -ENOMEM;
		goto err_free_dev_name;
	}

	gdev->label = kstrdup_const(gc->label ?: "unknown", GFP_KERNEL);
	if (!gdev->label) {
		ret = -ENOMEM;
		goto err_free_descs;
	}

	gdev->ngpio = gc->ngpio;
	gdev->can_sleep = gc->can_sleep;

	scoped_guard(mutex, &gpio_devices_lock) {
		/*
		 * TODO: this allocates a Linux GPIO number base in the global
		 * GPIO numberspace for this chip. In the long run we want to
		 * get *rid* of this numberspace and use only descriptors, but
		 * it may be a pipe dream. It will not happen before we get rid
		 * of the sysfs interface anyways.
		 */
		base = gc->base;
		if (base < 0) {
			base = gpiochip_find_base_unlocked(gc->ngpio);
			if (base < 0) {
				ret = base;
				base = 0;
				goto err_free_label;
			}

			/*
			 * TODO: it should not be necessary to reflect the
			 * assigned base outside of the GPIO subsystem. Go over
			 * drivers and see if anyone makes use of this, else
			 * drop this and assign a poison instead.
			 */
			gc->base = base;
		} else {
			dev_warn(&gdev->dev,
				 "Static allocation of GPIO base is deprecated, use dynamic allocation.\n");
		}

		gdev->base = base;

		ret = gpiodev_add_to_list_unlocked(gdev);
		if (ret) {
			chip_err(gc, "GPIO integer space overlap, cannot add chip\n");
			goto err_free_label;
		}
	}

	for (desc_index = 0; desc_index < gc->ngpio; desc_index++)
		gdev->descs[desc_index].gdev = gdev;

	BLOCKING_INIT_NOTIFIER_HEAD(&gdev->line_state_notifier);
	BLOCKING_INIT_NOTIFIER_HEAD(&gdev->device_notifier);

	ret = init_srcu_struct(&gdev->srcu);
	if (ret)
		goto err_remove_from_list;

#ifdef CONFIG_PINCTRL
	INIT_LIST_HEAD(&gdev->pin_ranges);
#endif

	if (gc->names) {
		ret = gpiochip_set_desc_names(gc);
		if (ret)
			goto err_cleanup_gdev_srcu;
	}
	ret = gpiochip_set_names(gc);
	if (ret)
		goto err_cleanup_gdev_srcu;

	ret = gpiochip_init_valid_mask(gc);
	if (ret)
		goto err_cleanup_gdev_srcu;

	for (desc_index = 0; desc_index < gc->ngpio; desc_index++) {
		struct gpio_desc *desc = &gdev->descs[desc_index];

		ret = init_srcu_struct(&desc->srcu);
		if (ret)
			goto err_cleanup_desc_srcu;

		if (gc->get_direction && gpiochip_line_is_valid(gc, desc_index)) {
			assign_bit(FLAG_IS_OUT,
				   &desc->flags, !gc->get_direction(gc, desc_index));
		} else {
			assign_bit(FLAG_IS_OUT,
				   &desc->flags, !gc->direction_input);
		}
	}

	ret = of_gpiochip_add(gc);
	if (ret)
		goto err_cleanup_desc_srcu;

	ret = gpiochip_add_pin_ranges(gc);
	if (ret)
		goto err_remove_of_chip;

	acpi_gpiochip_add(gc);

	machine_gpiochip_add(gc);

	ret = gpiochip_irqchip_init_valid_mask(gc);
	if (ret)
		goto err_free_hogs;

	ret = gpiochip_irqchip_init_hw(gc);
	if (ret)
		goto err_remove_irqchip_mask;

	ret = gpiochip_add_irqchip(gc, lock_key, request_key);
	if (ret)
		goto err_remove_irqchip_mask;

	/*
	 * By first adding the chardev, and then adding the device,
	 * we get a device node entry in sysfs under
	 * /sys/bus/gpio/devices/gpiochipN/dev that can be used for
	 * coldplug of device nodes and other udev business.
	 * We can do this only if gpiolib has been initialized.
	 * Otherwise, defer until later.
	 */
	if (gpiolib_initialized) {
		ret = gpiochip_setup_dev(gdev);
		if (ret)
			goto err_remove_irqchip;
	}
	return 0;

err_remove_irqchip:
	gpiochip_irqchip_remove(gc);
err_remove_irqchip_mask:
	gpiochip_irqchip_free_valid_mask(gc);
err_free_hogs:
	gpiochip_free_hogs(gc);
	acpi_gpiochip_remove(gc);
	gpiochip_remove_pin_ranges(gc);
err_remove_of_chip:
	of_gpiochip_remove(gc);
err_cleanup_desc_srcu:
	while (desc_index--)
		cleanup_srcu_struct(&gdev->descs[desc_index].srcu);
	gpiochip_free_valid_mask(gc);
err_cleanup_gdev_srcu:
	cleanup_srcu_struct(&gdev->srcu);
err_remove_from_list:
	scoped_guard(mutex, &gpio_devices_lock)
		list_del_rcu(&gdev->list);
	synchronize_srcu(&gpio_devices_srcu);
	if (gdev->dev.release) {
		/* release() has been registered by gpiochip_setup_dev() */
		gpio_device_put(gdev);
		goto err_print_message;
	}
err_free_label:
	kfree_const(gdev->label);
err_free_descs:
	kfree(gdev->descs);
err_free_dev_name:
	kfree(dev_name(&gdev->dev));
err_free_ida:
	ida_free(&gpio_ida, gdev->id);
err_free_gdev:
	kfree(gdev);
err_print_message:
	/* failures here can mean systems won't boot... */
	if (ret != -EPROBE_DEFER) {
		pr_err("%s: GPIOs %d..%d (%s) failed to register, %d\n", __func__,
		       base, base + (int)gc->ngpio - 1,
		       gc->label ? : "generic", ret);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(gpiochip_add_data_with_key);

/**
 * gpiochip_remove() - unregister a gpio_chip
 * @gc: the chip to unregister
 *
 * A gpio_chip with any GPIOs still requested may not be removed.
 */
void gpiochip_remove(struct gpio_chip *gc)
{
	struct gpio_device *gdev = gc->gpiodev;

	/* FIXME: should the legacy sysfs handling be moved to gpio_device? */
	gpiochip_sysfs_unregister(gdev);
	gpiochip_free_hogs(gc);

	scoped_guard(mutex, &gpio_devices_lock)
		list_del_rcu(&gdev->list);
	synchronize_srcu(&gpio_devices_srcu);

	/* Numb the device, cancelling all outstanding operations */
	rcu_assign_pointer(gdev->chip, NULL);
	synchronize_srcu(&gdev->srcu);
	gpiochip_irqchip_remove(gc);
	acpi_gpiochip_remove(gc);
	of_gpiochip_remove(gc);
	gpiochip_remove_pin_ranges(gc);
	gpiochip_free_valid_mask(gc);
	/*
	 * We accept no more calls into the driver from this point, so
	 * NULL the driver data pointer.
	 */
	gpiochip_set_data(gc, NULL);

	/*
	 * The gpiochip side puts its use of the device to rest here:
	 * if there are no userspace clients, the chardev and device will
	 * be removed, else it will be dangling until the last user is
	 * gone.
	 */
	gcdev_unregister(gdev);
	gpio_device_put(gdev);
}
EXPORT_SYMBOL_GPL(gpiochip_remove);

/**
 * gpio_device_find() - find a specific GPIO device
 * @data: data to pass to match function
 * @match: Callback function to check gpio_chip
 *
 * Returns:
 * New reference to struct gpio_device.
 *
 * Similar to bus_find_device(). It returns a reference to a gpio_device as
 * determined by a user supplied @match callback. The callback should return
 * 0 if the device doesn't match and non-zero if it does. If the callback
 * returns non-zero, this function will return to the caller and not iterate
 * over any more gpio_devices.
 *
 * The callback takes the GPIO chip structure as argument. During the execution
 * of the callback function the chip is protected from being freed. TODO: This
 * actually has yet to be implemented.
 *
 * If the function returns non-NULL, the returned reference must be freed by
 * the caller using gpio_device_put().
 */
struct gpio_device *gpio_device_find(const void *data,
				     int (*match)(struct gpio_chip *gc,
						  const void *data))
{
	struct gpio_device *gdev;
	struct gpio_chip *gc;

	/*
	 * Not yet but in the future the spinlock below will become a mutex.
	 * Annotate this function before anyone tries to use it in interrupt
	 * context like it happened with gpiochip_find().
	 */
	might_sleep();

	guard(srcu)(&gpio_devices_srcu);

	list_for_each_entry_srcu(gdev, &gpio_devices, list,
				 srcu_read_lock_held(&gpio_devices_srcu)) {
		guard(srcu)(&gdev->srcu);

		gc = srcu_dereference(gdev->chip, &gdev->srcu);

		if (gc && match(gc, data))
			return gpio_device_get(gdev);
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(gpio_device_find);

static int gpio_chip_match_by_label(struct gpio_chip *gc, const void *label)
{
	return gc->label && !strcmp(gc->label, label);
}

/**
 * gpio_device_find_by_label() - wrapper around gpio_device_find() finding the
 *                               GPIO device by its backing chip's label
 * @label: Label to lookup
 *
 * Returns:
 * Reference to the GPIO device or NULL. Reference must be released with
 * gpio_device_put().
 */
struct gpio_device *gpio_device_find_by_label(const char *label)
{
	return gpio_device_find((void *)label, gpio_chip_match_by_label);
}
EXPORT_SYMBOL_GPL(gpio_device_find_by_label);

static int gpio_chip_match_by_fwnode(struct gpio_chip *gc, const void *fwnode)
{
	return device_match_fwnode(&gc->gpiodev->dev, fwnode);
}

/**
 * gpio_device_find_by_fwnode() - wrapper around gpio_device_find() finding
 *                                the GPIO device by its fwnode
 * @fwnode: Firmware node to lookup
 *
 * Returns:
 * Reference to the GPIO device or NULL. Reference must be released with
 * gpio_device_put().
 */
struct gpio_device *gpio_device_find_by_fwnode(const struct fwnode_handle *fwnode)
{
	return gpio_device_find((void *)fwnode, gpio_chip_match_by_fwnode);
}
EXPORT_SYMBOL_GPL(gpio_device_find_by_fwnode);

/**
 * gpio_device_get() - Increase the reference count of this GPIO device
 * @gdev: GPIO device to increase the refcount for
 *
 * Returns:
 * Pointer to @gdev.
 */
struct gpio_device *gpio_device_get(struct gpio_device *gdev)
{
	return to_gpio_device(get_device(&gdev->dev));
}
EXPORT_SYMBOL_GPL(gpio_device_get);

/**
 * gpio_device_put() - Decrease the reference count of this GPIO device and
 *                     possibly free all resources associated with it.
 * @gdev: GPIO device to decrease the reference count for
 */
void gpio_device_put(struct gpio_device *gdev)
{
	put_device(&gdev->dev);
}
EXPORT_SYMBOL_GPL(gpio_device_put);

/**
 * gpio_device_to_device() - Retrieve the address of the underlying struct
 *                           device.
 * @gdev: GPIO device for which to return the address.
 *
 * This does not increase the reference count of the GPIO device nor the
 * underlying struct device.
 *
 * Returns:
 * Address of struct device backing this GPIO device.
 */
struct device *gpio_device_to_device(struct gpio_device *gdev)
{
	return &gdev->dev;
}
EXPORT_SYMBOL_GPL(gpio_device_to_device);

#ifdef CONFIG_GPIOLIB_IRQCHIP

/*
 * The following is irqchip helper code for gpiochips.
 */

static int gpiochip_irqchip_init_hw(struct gpio_chip *gc)
{
	struct gpio_irq_chip *girq = &gc->irq;

	if (!girq->init_hw)
		return 0;

	return girq->init_hw(gc);
}

static int gpiochip_irqchip_init_valid_mask(struct gpio_chip *gc)
{
	struct gpio_irq_chip *girq = &gc->irq;

	if (!girq->init_valid_mask)
		return 0;

	girq->valid_mask = gpiochip_allocate_mask(gc);
	if (!girq->valid_mask)
		return -ENOMEM;

	girq->init_valid_mask(gc, girq->valid_mask, gc->ngpio);

	return 0;
}

static void gpiochip_irqchip_free_valid_mask(struct gpio_chip *gc)
{
	gpiochip_free_mask(&gc->irq.valid_mask);
}

static bool gpiochip_irqchip_irq_valid(const struct gpio_chip *gc,
				       unsigned int offset)
{
	if (!gpiochip_line_is_valid(gc, offset))
		return false;
	/* No mask means all valid */
	if (likely(!gc->irq.valid_mask))
		return true;
	return test_bit(offset, gc->irq.valid_mask);
}

#ifdef CONFIG_IRQ_DOMAIN_HIERARCHY

/**
 * gpiochip_set_hierarchical_irqchip() - connects a hierarchical irqchip
 * to a gpiochip
 * @gc: the gpiochip to set the irqchip hierarchical handler to
 * @irqchip: the irqchip to handle this level of the hierarchy, the interrupt
 * will then percolate up to the parent
 */
static void gpiochip_set_hierarchical_irqchip(struct gpio_chip *gc,
					      struct irq_chip *irqchip)
{
	/* DT will deal with mapping each IRQ as we go along */
	if (is_of_node(gc->irq.fwnode))
		return;

	/*
	 * This is for legacy and boardfile "irqchip" fwnodes: allocate
	 * irqs upfront instead of dynamically since we don't have the
	 * dynamic type of allocation that hardware description languages
	 * provide. Once all GPIO drivers using board files are gone from
	 * the kernel we can delete this code, but for a transitional period
	 * it is necessary to keep this around.
	 */
	if (is_fwnode_irqchip(gc->irq.fwnode)) {
		int i;
		int ret;

		for (i = 0; i < gc->ngpio; i++) {
			struct irq_fwspec fwspec;
			unsigned int parent_hwirq;
			unsigned int parent_type;
			struct gpio_irq_chip *girq = &gc->irq;

			/*
			 * We call the child to parent translation function
			 * only to check if the child IRQ is valid or not.
			 * Just pick the rising edge type here as that is what
			 * we likely need to support.
			 */
			ret = girq->child_to_parent_hwirq(gc, i,
							  IRQ_TYPE_EDGE_RISING,
							  &parent_hwirq,
							  &parent_type);
			if (ret) {
				chip_err(gc, "skip set-up on hwirq %d\n",
					 i);
				continue;
			}

			fwspec.fwnode = gc->irq.fwnode;
			/* This is the hwirq for the GPIO line side of things */
			fwspec.param[0] = girq->child_offset_to_irq(gc, i);
			/* Just pick something */
			fwspec.param[1] = IRQ_TYPE_EDGE_RISING;
			fwspec.param_count = 2;
			ret = irq_domain_alloc_irqs(gc->irq.domain, 1,
						    NUMA_NO_NODE, &fwspec);
			if (ret < 0) {
				chip_err(gc,
					 "can not allocate irq for GPIO line %d parent hwirq %d in hierarchy domain: %d\n",
					 i, parent_hwirq,
					 ret);
			}
		}
	}

	chip_err(gc, "%s unknown fwnode type proceed anyway\n", __func__);

	return;
}

static int gpiochip_hierarchy_irq_domain_translate(struct irq_domain *d,
						   struct irq_fwspec *fwspec,
						   unsigned long *hwirq,
						   unsigned int *type)
{
	/* We support standard DT translation */
	if (is_of_node(fwspec->fwnode) && fwspec->param_count == 2) {
		return irq_domain_translate_twocell(d, fwspec, hwirq, type);
	}

	/* This is for board files and others not using DT */
	if (is_fwnode_irqchip(fwspec->fwnode)) {
		int ret;

		ret = irq_domain_translate_twocell(d, fwspec, hwirq, type);
		if (ret)
			return ret;
		WARN_ON(*type == IRQ_TYPE_NONE);
		return 0;
	}
	return -EINVAL;
}

static int gpiochip_hierarchy_irq_domain_alloc(struct irq_domain *d,
					       unsigned int irq,
					       unsigned int nr_irqs,
					       void *data)
{
	struct gpio_chip *gc = d->host_data;
	irq_hw_number_t hwirq;
	unsigned int type = IRQ_TYPE_NONE;
	struct irq_fwspec *fwspec = data;
	union gpio_irq_fwspec gpio_parent_fwspec = {};
	unsigned int parent_hwirq;
	unsigned int parent_type;
	struct gpio_irq_chip *girq = &gc->irq;
	int ret;

	/*
	 * The nr_irqs parameter is always one except for PCI multi-MSI
	 * so this should not happen.
	 */
	WARN_ON(nr_irqs != 1);

	ret = gc->irq.child_irq_domain_ops.translate(d, fwspec, &hwirq, &type);
	if (ret)
		return ret;

	chip_dbg(gc, "allocate IRQ %d, hwirq %lu\n", irq, hwirq);

	ret = girq->child_to_parent_hwirq(gc, hwirq, type,
					  &parent_hwirq, &parent_type);
	if (ret) {
		chip_err(gc, "can't look up hwirq %lu\n", hwirq);
		return ret;
	}
	chip_dbg(gc, "found parent hwirq %u\n", parent_hwirq);

	/*
	 * We set handle_bad_irq because the .set_type() should
	 * always be invoked and set the right type of handler.
	 */
	irq_domain_set_info(d,
			    irq,
			    hwirq,
			    gc->irq.chip,
			    gc,
			    girq->handler,
			    NULL, NULL);
	irq_set_probe(irq);

	/* This parent only handles asserted level IRQs */
	ret = girq->populate_parent_alloc_arg(gc, &gpio_parent_fwspec,
					      parent_hwirq, parent_type);
	if (ret)
		return ret;

	chip_dbg(gc, "alloc_irqs_parent for %d parent hwirq %d\n",
		  irq, parent_hwirq);
	irq_set_lockdep_class(irq, gc->irq.lock_key, gc->irq.request_key);
	ret = irq_domain_alloc_irqs_parent(d, irq, 1, &gpio_parent_fwspec);
	/*
	 * If the parent irqdomain is msi, the interrupts have already
	 * been allocated, so the EEXIST is good.
	 */
	if (irq_domain_is_msi(d->parent) && (ret == -EEXIST))
		ret = 0;
	if (ret)
		chip_err(gc,
			 "failed to allocate parent hwirq %d for hwirq %lu\n",
			 parent_hwirq, hwirq);

	return ret;
}

static unsigned int gpiochip_child_offset_to_irq_noop(struct gpio_chip *gc,
						      unsigned int offset)
{
	return offset;
}

/**
 * gpiochip_irq_domain_activate() - Lock a GPIO to be used as an IRQ
 * @domain: The IRQ domain used by this IRQ chip
 * @data: Outermost irq_data associated with the IRQ
 * @reserve: If set, only reserve an interrupt vector instead of assigning one
 *
 * This function is a wrapper that calls gpiochip_lock_as_irq() and is to be
 * used as the activate function for the &struct irq_domain_ops. The host_data
 * for the IRQ domain must be the &struct gpio_chip.
 */
static int gpiochip_irq_domain_activate(struct irq_domain *domain,
					struct irq_data *data, bool reserve)
{
	struct gpio_chip *gc = domain->host_data;
	unsigned int hwirq = irqd_to_hwirq(data);

	return gpiochip_lock_as_irq(gc, hwirq);
}

/**
 * gpiochip_irq_domain_deactivate() - Unlock a GPIO used as an IRQ
 * @domain: The IRQ domain used by this IRQ chip
 * @data: Outermost irq_data associated with the IRQ
 *
 * This function is a wrapper that will call gpiochip_unlock_as_irq() and is to
 * be used as the deactivate function for the &struct irq_domain_ops. The
 * host_data for the IRQ domain must be the &struct gpio_chip.
 */
static void gpiochip_irq_domain_deactivate(struct irq_domain *domain,
					   struct irq_data *data)
{
	struct gpio_chip *gc = domain->host_data;
	unsigned int hwirq = irqd_to_hwirq(data);

	return gpiochip_unlock_as_irq(gc, hwirq);
}

static void gpiochip_hierarchy_setup_domain_ops(struct irq_domain_ops *ops)
{
	ops->activate = gpiochip_irq_domain_activate;
	ops->deactivate = gpiochip_irq_domain_deactivate;
	ops->alloc = gpiochip_hierarchy_irq_domain_alloc;

	/*
	 * We only allow overriding the translate() and free() functions for
	 * hierarchical chips, and this should only be done if the user
	 * really need something other than 1:1 translation for translate()
	 * callback and free if user wants to free up any resources which
	 * were allocated during callbacks, for example populate_parent_alloc_arg.
	 */
	if (!ops->translate)
		ops->translate = gpiochip_hierarchy_irq_domain_translate;
	if (!ops->free)
		ops->free = irq_domain_free_irqs_common;
}

static struct irq_domain *gpiochip_hierarchy_create_domain(struct gpio_chip *gc)
{
	struct irq_domain *domain;

	if (!gc->irq.child_to_parent_hwirq ||
	    !gc->irq.fwnode) {
		chip_err(gc, "missing irqdomain vital data\n");
		return ERR_PTR(-EINVAL);
	}

	if (!gc->irq.child_offset_to_irq)
		gc->irq.child_offset_to_irq = gpiochip_child_offset_to_irq_noop;

	if (!gc->irq.populate_parent_alloc_arg)
		gc->irq.populate_parent_alloc_arg =
			gpiochip_populate_parent_fwspec_twocell;

	gpiochip_hierarchy_setup_domain_ops(&gc->irq.child_irq_domain_ops);

	domain = irq_domain_create_hierarchy(
		gc->irq.parent_domain,
		0,
		gc->ngpio,
		gc->irq.fwnode,
		&gc->irq.child_irq_domain_ops,
		gc);

	if (!domain)
		return ERR_PTR(-ENOMEM);

	gpiochip_set_hierarchical_irqchip(gc, gc->irq.chip);

	return domain;
}

static bool gpiochip_hierarchy_is_hierarchical(struct gpio_chip *gc)
{
	return !!gc->irq.parent_domain;
}

int gpiochip_populate_parent_fwspec_twocell(struct gpio_chip *gc,
					    union gpio_irq_fwspec *gfwspec,
					    unsigned int parent_hwirq,
					    unsigned int parent_type)
{
	struct irq_fwspec *fwspec = &gfwspec->fwspec;

	fwspec->fwnode = gc->irq.parent_domain->fwnode;
	fwspec->param_count = 2;
	fwspec->param[0] = parent_hwirq;
	fwspec->param[1] = parent_type;

	return 0;
}
EXPORT_SYMBOL_GPL(gpiochip_populate_parent_fwspec_twocell);

int gpiochip_populate_parent_fwspec_fourcell(struct gpio_chip *gc,
					     union gpio_irq_fwspec *gfwspec,
					     unsigned int parent_hwirq,
					     unsigned int parent_type)
{
	struct irq_fwspec *fwspec = &gfwspec->fwspec;

	fwspec->fwnode = gc->irq.parent_domain->fwnode;
	fwspec->param_count = 4;
	fwspec->param[0] = 0;
	fwspec->param[1] = parent_hwirq;
	fwspec->param[2] = 0;
	fwspec->param[3] = parent_type;

	return 0;
}
EXPORT_SYMBOL_GPL(gpiochip_populate_parent_fwspec_fourcell);

#else

static struct irq_domain *gpiochip_hierarchy_create_domain(struct gpio_chip *gc)
{
	return ERR_PTR(-EINVAL);
}

static bool gpiochip_hierarchy_is_hierarchical(struct gpio_chip *gc)
{
	return false;
}

#endif /* CONFIG_IRQ_DOMAIN_HIERARCHY */

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
	struct gpio_chip *gc = d->host_data;
	int ret = 0;

	if (!gpiochip_irqchip_irq_valid(gc, hwirq))
		return -ENXIO;

	irq_set_chip_data(irq, gc);
	/*
	 * This lock class tells lockdep that GPIO irqs are in a different
	 * category than their parents, so it won't report false recursion.
	 */
	irq_set_lockdep_class(irq, gc->irq.lock_key, gc->irq.request_key);
	irq_set_chip_and_handler(irq, gc->irq.chip, gc->irq.handler);
	/* Chips that use nested thread handlers have them marked */
	if (gc->irq.threaded)
		irq_set_nested_thread(irq, 1);
	irq_set_noprobe(irq);

	if (gc->irq.num_parents == 1)
		ret = irq_set_parent(irq, gc->irq.parents[0]);
	else if (gc->irq.map)
		ret = irq_set_parent(irq, gc->irq.map[hwirq]);

	if (ret < 0)
		return ret;

	/*
	 * No set-up of the hardware will happen if IRQ_TYPE_NONE
	 * is passed as default type.
	 */
	if (gc->irq.default_type != IRQ_TYPE_NONE)
		irq_set_irq_type(irq, gc->irq.default_type);

	return 0;
}

static void gpiochip_irq_unmap(struct irq_domain *d, unsigned int irq)
{
	struct gpio_chip *gc = d->host_data;

	if (gc->irq.threaded)
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

static struct irq_domain *gpiochip_simple_create_domain(struct gpio_chip *gc)
{
	struct fwnode_handle *fwnode = dev_fwnode(&gc->gpiodev->dev);
	struct irq_domain *domain;

	domain = irq_domain_create_simple(fwnode, gc->ngpio, gc->irq.first,
					  &gpiochip_domain_ops, gc);
	if (!domain)
		return ERR_PTR(-EINVAL);

	return domain;
}

static int gpiochip_to_irq(struct gpio_chip *gc, unsigned int offset)
{
	struct irq_domain *domain = gc->irq.domain;

#ifdef CONFIG_GPIOLIB_IRQCHIP
	/*
	 * Avoid race condition with other code, which tries to lookup
	 * an IRQ before the irqchip has been properly registered,
	 * i.e. while gpiochip is still being brought up.
	 */
	if (!gc->irq.initialized)
		return -EPROBE_DEFER;
#endif

	if (!gpiochip_irqchip_irq_valid(gc, offset))
		return -ENXIO;

#ifdef CONFIG_IRQ_DOMAIN_HIERARCHY
	if (irq_domain_is_hierarchy(domain)) {
		struct irq_fwspec spec;

		spec.fwnode = domain->fwnode;
		spec.param_count = 2;
		spec.param[0] = gc->irq.child_offset_to_irq(gc, offset);
		spec.param[1] = IRQ_TYPE_NONE;

		return irq_create_fwspec_mapping(&spec);
	}
#endif

	return irq_create_mapping(domain, offset);
}

int gpiochip_irq_reqres(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	unsigned int hwirq = irqd_to_hwirq(d);

	return gpiochip_reqres_irq(gc, hwirq);
}
EXPORT_SYMBOL(gpiochip_irq_reqres);

void gpiochip_irq_relres(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	unsigned int hwirq = irqd_to_hwirq(d);

	gpiochip_relres_irq(gc, hwirq);
}
EXPORT_SYMBOL(gpiochip_irq_relres);

static void gpiochip_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	unsigned int hwirq = irqd_to_hwirq(d);

	if (gc->irq.irq_mask)
		gc->irq.irq_mask(d);
	gpiochip_disable_irq(gc, hwirq);
}

static void gpiochip_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	unsigned int hwirq = irqd_to_hwirq(d);

	gpiochip_enable_irq(gc, hwirq);
	if (gc->irq.irq_unmask)
		gc->irq.irq_unmask(d);
}

static void gpiochip_irq_enable(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	unsigned int hwirq = irqd_to_hwirq(d);

	gpiochip_enable_irq(gc, hwirq);
	gc->irq.irq_enable(d);
}

static void gpiochip_irq_disable(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	unsigned int hwirq = irqd_to_hwirq(d);

	gc->irq.irq_disable(d);
	gpiochip_disable_irq(gc, hwirq);
}

static void gpiochip_set_irq_hooks(struct gpio_chip *gc)
{
	struct irq_chip *irqchip = gc->irq.chip;

	if (irqchip->flags & IRQCHIP_IMMUTABLE)
		return;

	chip_warn(gc, "not an immutable chip, please consider fixing it!\n");

	if (!irqchip->irq_request_resources &&
	    !irqchip->irq_release_resources) {
		irqchip->irq_request_resources = gpiochip_irq_reqres;
		irqchip->irq_release_resources = gpiochip_irq_relres;
	}
	if (WARN_ON(gc->irq.irq_enable))
		return;
	/* Check if the irqchip already has this hook... */
	if (irqchip->irq_enable == gpiochip_irq_enable ||
		irqchip->irq_mask == gpiochip_irq_mask) {
		/*
		 * ...and if so, give a gentle warning that this is bad
		 * practice.
		 */
		chip_info(gc,
			  "detected irqchip that is shared with multiple gpiochips: please fix the driver.\n");
		return;
	}

	if (irqchip->irq_disable) {
		gc->irq.irq_disable = irqchip->irq_disable;
		irqchip->irq_disable = gpiochip_irq_disable;
	} else {
		gc->irq.irq_mask = irqchip->irq_mask;
		irqchip->irq_mask = gpiochip_irq_mask;
	}

	if (irqchip->irq_enable) {
		gc->irq.irq_enable = irqchip->irq_enable;
		irqchip->irq_enable = gpiochip_irq_enable;
	} else {
		gc->irq.irq_unmask = irqchip->irq_unmask;
		irqchip->irq_unmask = gpiochip_irq_unmask;
	}
}

static int gpiochip_irqchip_add_allocated_domain(struct gpio_chip *gc,
						 struct irq_domain *domain,
						 bool allocated_externally)
{
	if (!domain)
		return -EINVAL;

	if (gc->to_irq)
		chip_warn(gc, "to_irq is redefined in %s and you shouldn't rely on it\n", __func__);

	gc->to_irq = gpiochip_to_irq;
	gc->irq.domain = domain;
	gc->irq.domain_is_allocated_externally = allocated_externally;

	/*
	 * Using barrier() here to prevent compiler from reordering
	 * gc->irq.initialized before adding irqdomain.
	 */
	barrier();

	gc->irq.initialized = true;

	return 0;
}

/**
 * gpiochip_add_irqchip() - adds an IRQ chip to a GPIO chip
 * @gc: the GPIO chip to add the IRQ chip to
 * @lock_key: lockdep class for IRQ lock
 * @request_key: lockdep class for IRQ request
 */
static int gpiochip_add_irqchip(struct gpio_chip *gc,
				struct lock_class_key *lock_key,
				struct lock_class_key *request_key)
{
	struct fwnode_handle *fwnode = dev_fwnode(&gc->gpiodev->dev);
	struct irq_chip *irqchip = gc->irq.chip;
	struct irq_domain *domain;
	unsigned int type;
	unsigned int i;
	int ret;

	if (!irqchip)
		return 0;

	if (gc->irq.parent_handler && gc->can_sleep) {
		chip_err(gc, "you cannot have chained interrupts on a chip that may sleep\n");
		return -EINVAL;
	}

	type = gc->irq.default_type;

	/*
	 * Specifying a default trigger is a terrible idea if DT or ACPI is
	 * used to configure the interrupts, as you may end up with
	 * conflicting triggers. Tell the user, and reset to NONE.
	 */
	if (WARN(fwnode && type != IRQ_TYPE_NONE,
		 "%pfw: Ignoring %u default trigger\n", fwnode, type))
		type = IRQ_TYPE_NONE;

	gc->irq.default_type = type;
	gc->irq.lock_key = lock_key;
	gc->irq.request_key = request_key;

	/* If a parent irqdomain is provided, let's build a hierarchy */
	if (gpiochip_hierarchy_is_hierarchical(gc)) {
		domain = gpiochip_hierarchy_create_domain(gc);
	} else {
		domain = gpiochip_simple_create_domain(gc);
	}
	if (IS_ERR(domain))
		return PTR_ERR(domain);

	if (gc->irq.parent_handler) {
		for (i = 0; i < gc->irq.num_parents; i++) {
			void *data;

			if (gc->irq.per_parent_data)
				data = gc->irq.parent_handler_data_array[i];
			else
				data = gc->irq.parent_handler_data ?: gc;

			/*
			 * The parent IRQ chip is already using the chip_data
			 * for this IRQ chip, so our callbacks simply use the
			 * handler_data.
			 */
			irq_set_chained_handler_and_data(gc->irq.parents[i],
							 gc->irq.parent_handler,
							 data);
		}
	}

	gpiochip_set_irq_hooks(gc);

	ret = gpiochip_irqchip_add_allocated_domain(gc, domain, false);
	if (ret)
		return ret;

	acpi_gpiochip_request_interrupts(gc);

	return 0;
}

/**
 * gpiochip_irqchip_remove() - removes an irqchip added to a gpiochip
 * @gc: the gpiochip to remove the irqchip from
 *
 * This is called only from gpiochip_remove()
 */
static void gpiochip_irqchip_remove(struct gpio_chip *gc)
{
	struct irq_chip *irqchip = gc->irq.chip;
	unsigned int offset;

	acpi_gpiochip_free_interrupts(gc);

	if (irqchip && gc->irq.parent_handler) {
		struct gpio_irq_chip *irq = &gc->irq;
		unsigned int i;

		for (i = 0; i < irq->num_parents; i++)
			irq_set_chained_handler_and_data(irq->parents[i],
							 NULL, NULL);
	}

	/* Remove all IRQ mappings and delete the domain */
	if (!gc->irq.domain_is_allocated_externally && gc->irq.domain) {
		unsigned int irq;

		for (offset = 0; offset < gc->ngpio; offset++) {
			if (!gpiochip_irqchip_irq_valid(gc, offset))
				continue;

			irq = irq_find_mapping(gc->irq.domain, offset);
			irq_dispose_mapping(irq);
		}

		irq_domain_remove(gc->irq.domain);
	}

	if (irqchip && !(irqchip->flags & IRQCHIP_IMMUTABLE)) {
		if (irqchip->irq_request_resources == gpiochip_irq_reqres) {
			irqchip->irq_request_resources = NULL;
			irqchip->irq_release_resources = NULL;
		}
		if (irqchip->irq_enable == gpiochip_irq_enable) {
			irqchip->irq_enable = gc->irq.irq_enable;
			irqchip->irq_disable = gc->irq.irq_disable;
		}
	}
	gc->irq.irq_enable = NULL;
	gc->irq.irq_disable = NULL;
	gc->irq.chip = NULL;

	gpiochip_irqchip_free_valid_mask(gc);
}

/**
 * gpiochip_irqchip_add_domain() - adds an irqdomain to a gpiochip
 * @gc: the gpiochip to add the irqchip to
 * @domain: the irqdomain to add to the gpiochip
 *
 * This function adds an IRQ domain to the gpiochip.
 */
int gpiochip_irqchip_add_domain(struct gpio_chip *gc,
				struct irq_domain *domain)
{
	return gpiochip_irqchip_add_allocated_domain(gc, domain, true);
}
EXPORT_SYMBOL_GPL(gpiochip_irqchip_add_domain);

#else /* CONFIG_GPIOLIB_IRQCHIP */

static inline int gpiochip_add_irqchip(struct gpio_chip *gc,
				       struct lock_class_key *lock_key,
				       struct lock_class_key *request_key)
{
	return 0;
}
static void gpiochip_irqchip_remove(struct gpio_chip *gc) {}

static inline int gpiochip_irqchip_init_hw(struct gpio_chip *gc)
{
	return 0;
}

static inline int gpiochip_irqchip_init_valid_mask(struct gpio_chip *gc)
{
	return 0;
}
static inline void gpiochip_irqchip_free_valid_mask(struct gpio_chip *gc)
{ }

#endif /* CONFIG_GPIOLIB_IRQCHIP */

/**
 * gpiochip_generic_request() - request the gpio function for a pin
 * @gc: the gpiochip owning the GPIO
 * @offset: the offset of the GPIO to request for GPIO function
 */
int gpiochip_generic_request(struct gpio_chip *gc, unsigned int offset)
{
#ifdef CONFIG_PINCTRL
	if (list_empty(&gc->gpiodev->pin_ranges))
		return 0;
#endif

	return pinctrl_gpio_request(gc, offset);
}
EXPORT_SYMBOL_GPL(gpiochip_generic_request);

/**
 * gpiochip_generic_free() - free the gpio function from a pin
 * @gc: the gpiochip to request the gpio function for
 * @offset: the offset of the GPIO to free from GPIO function
 */
void gpiochip_generic_free(struct gpio_chip *gc, unsigned int offset)
{
#ifdef CONFIG_PINCTRL
	if (list_empty(&gc->gpiodev->pin_ranges))
		return;
#endif

	pinctrl_gpio_free(gc, offset);
}
EXPORT_SYMBOL_GPL(gpiochip_generic_free);

/**
 * gpiochip_generic_config() - apply configuration for a pin
 * @gc: the gpiochip owning the GPIO
 * @offset: the offset of the GPIO to apply the configuration
 * @config: the configuration to be applied
 */
int gpiochip_generic_config(struct gpio_chip *gc, unsigned int offset,
			    unsigned long config)
{
#ifdef CONFIG_PINCTRL
	if (list_empty(&gc->gpiodev->pin_ranges))
		return -ENOTSUPP;
#endif

	return pinctrl_gpio_set_config(gc, offset, config);
}
EXPORT_SYMBOL_GPL(gpiochip_generic_config);

#ifdef CONFIG_PINCTRL

/**
 * gpiochip_add_pingroup_range() - add a range for GPIO <-> pin mapping
 * @gc: the gpiochip to add the range for
 * @pctldev: the pin controller to map to
 * @gpio_offset: the start offset in the current gpio_chip number space
 * @pin_group: name of the pin group inside the pin controller
 *
 * Calling this function directly from a DeviceTree-supported
 * pinctrl driver is DEPRECATED. Please see Section 2.1 of
 * Documentation/devicetree/bindings/gpio/gpio.txt on how to
 * bind pinctrl and gpio drivers via the "gpio-ranges" property.
 */
int gpiochip_add_pingroup_range(struct gpio_chip *gc,
			struct pinctrl_dev *pctldev,
			unsigned int gpio_offset, const char *pin_group)
{
	struct gpio_pin_range *pin_range;
	struct gpio_device *gdev = gc->gpiodev;
	int ret;

	pin_range = kzalloc(sizeof(*pin_range), GFP_KERNEL);
	if (!pin_range) {
		chip_err(gc, "failed to allocate pin ranges\n");
		return -ENOMEM;
	}

	/* Use local offset as range ID */
	pin_range->range.id = gpio_offset;
	pin_range->range.gc = gc;
	pin_range->range.name = gc->label;
	pin_range->range.base = gdev->base + gpio_offset;
	pin_range->pctldev = pctldev;

	ret = pinctrl_get_group_pins(pctldev, pin_group,
					&pin_range->range.pins,
					&pin_range->range.npins);
	if (ret < 0) {
		kfree(pin_range);
		return ret;
	}

	pinctrl_add_gpio_range(pctldev, &pin_range->range);

	chip_dbg(gc, "created GPIO range %d->%d ==> %s PINGRP %s\n",
		 gpio_offset, gpio_offset + pin_range->range.npins - 1,
		 pinctrl_dev_get_devname(pctldev), pin_group);

	list_add_tail(&pin_range->node, &gdev->pin_ranges);

	return 0;
}
EXPORT_SYMBOL_GPL(gpiochip_add_pingroup_range);

/**
 * gpiochip_add_pin_range() - add a range for GPIO <-> pin mapping
 * @gc: the gpiochip to add the range for
 * @pinctl_name: the dev_name() of the pin controller to map to
 * @gpio_offset: the start offset in the current gpio_chip number space
 * @pin_offset: the start offset in the pin controller number space
 * @npins: the number of pins from the offset of each pin space (GPIO and
 *	pin controller) to accumulate in this range
 *
 * Returns:
 * 0 on success, or a negative error-code on failure.
 *
 * Calling this function directly from a DeviceTree-supported
 * pinctrl driver is DEPRECATED. Please see Section 2.1 of
 * Documentation/devicetree/bindings/gpio/gpio.txt on how to
 * bind pinctrl and gpio drivers via the "gpio-ranges" property.
 */
int gpiochip_add_pin_range(struct gpio_chip *gc, const char *pinctl_name,
			   unsigned int gpio_offset, unsigned int pin_offset,
			   unsigned int npins)
{
	struct gpio_pin_range *pin_range;
	struct gpio_device *gdev = gc->gpiodev;
	int ret;

	pin_range = kzalloc(sizeof(*pin_range), GFP_KERNEL);
	if (!pin_range) {
		chip_err(gc, "failed to allocate pin ranges\n");
		return -ENOMEM;
	}

	/* Use local offset as range ID */
	pin_range->range.id = gpio_offset;
	pin_range->range.gc = gc;
	pin_range->range.name = gc->label;
	pin_range->range.base = gdev->base + gpio_offset;
	pin_range->range.pin_base = pin_offset;
	pin_range->range.npins = npins;
	pin_range->pctldev = pinctrl_find_and_add_gpio_range(pinctl_name,
			&pin_range->range);
	if (IS_ERR(pin_range->pctldev)) {
		ret = PTR_ERR(pin_range->pctldev);
		chip_err(gc, "could not create pin range\n");
		kfree(pin_range);
		return ret;
	}
	chip_dbg(gc, "created GPIO range %d->%d ==> %s PIN %d->%d\n",
		 gpio_offset, gpio_offset + npins - 1,
		 pinctl_name,
		 pin_offset, pin_offset + npins - 1);

	list_add_tail(&pin_range->node, &gdev->pin_ranges);

	return 0;
}
EXPORT_SYMBOL_GPL(gpiochip_add_pin_range);

/**
 * gpiochip_remove_pin_ranges() - remove all the GPIO <-> pin mappings
 * @gc: the chip to remove all the mappings for
 */
void gpiochip_remove_pin_ranges(struct gpio_chip *gc)
{
	struct gpio_pin_range *pin_range, *tmp;
	struct gpio_device *gdev = gc->gpiodev;

	list_for_each_entry_safe(pin_range, tmp, &gdev->pin_ranges, node) {
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
static int gpiod_request_commit(struct gpio_desc *desc, const char *label)
{
	unsigned int offset;
	int ret;

	CLASS(gpio_chip_guard, guard)(desc);
	if (!guard.gc)
		return -ENODEV;

	if (test_and_set_bit(FLAG_REQUESTED, &desc->flags))
		return -EBUSY;

	/* NOTE:  gpio_request() can be called in early boot,
	 * before IRQs are enabled, for non-sleeping (SOC) GPIOs.
	 */

	if (guard.gc->request) {
		offset = gpio_chip_hwgpio(desc);
		if (gpiochip_line_is_valid(guard.gc, offset))
			ret = guard.gc->request(guard.gc, offset);
		else
			ret = -EINVAL;
		if (ret)
			goto out_clear_bit;
	}

	if (guard.gc->get_direction)
		gpiod_get_direction(desc);

	ret = desc_set_label(desc, label ? : "?");
	if (ret)
		goto out_clear_bit;

	return 0;

out_clear_bit:
	clear_bit(FLAG_REQUESTED, &desc->flags);
	return ret;
}

/*
 * This descriptor validation needs to be inserted verbatim into each
 * function taking a descriptor, so we need to use a preprocessor
 * macro to avoid endless duplication. If the desc is NULL it is an
 * optional GPIO and calls should just bail out.
 */
static int validate_desc(const struct gpio_desc *desc, const char *func)
{
	if (!desc)
		return 0;

	if (IS_ERR(desc)) {
		pr_warn("%s: invalid GPIO (errorpointer)\n", func);
		return PTR_ERR(desc);
	}

	return 1;
}

#define VALIDATE_DESC(desc) do { \
	int __valid = validate_desc(desc, __func__); \
	if (__valid <= 0) \
		return __valid; \
	} while (0)

#define VALIDATE_DESC_VOID(desc) do { \
	int __valid = validate_desc(desc, __func__); \
	if (__valid <= 0) \
		return; \
	} while (0)

int gpiod_request(struct gpio_desc *desc, const char *label)
{
	int ret = -EPROBE_DEFER;

	VALIDATE_DESC(desc);

	if (try_module_get(desc->gdev->owner)) {
		ret = gpiod_request_commit(desc, label);
		if (ret)
			module_put(desc->gdev->owner);
		else
			gpio_device_get(desc->gdev);
	}

	if (ret)
		gpiod_dbg(desc, "%s: status %d\n", __func__, ret);

	return ret;
}

static void gpiod_free_commit(struct gpio_desc *desc)
{
	unsigned long flags;

	might_sleep();

	CLASS(gpio_chip_guard, guard)(desc);

	flags = READ_ONCE(desc->flags);

	if (guard.gc && test_bit(FLAG_REQUESTED, &flags)) {
		if (guard.gc->free)
			guard.gc->free(guard.gc, gpio_chip_hwgpio(desc));

		clear_bit(FLAG_ACTIVE_LOW, &flags);
		clear_bit(FLAG_REQUESTED, &flags);
		clear_bit(FLAG_OPEN_DRAIN, &flags);
		clear_bit(FLAG_OPEN_SOURCE, &flags);
		clear_bit(FLAG_PULL_UP, &flags);
		clear_bit(FLAG_PULL_DOWN, &flags);
		clear_bit(FLAG_BIAS_DISABLE, &flags);
		clear_bit(FLAG_EDGE_RISING, &flags);
		clear_bit(FLAG_EDGE_FALLING, &flags);
		clear_bit(FLAG_IS_HOGGED, &flags);
#ifdef CONFIG_OF_DYNAMIC
		WRITE_ONCE(desc->hog, NULL);
#endif
		desc_set_label(desc, NULL);
		WRITE_ONCE(desc->flags, flags);

		gpiod_line_state_notify(desc, GPIOLINE_CHANGED_RELEASED);
	}
}

void gpiod_free(struct gpio_desc *desc)
{
	VALIDATE_DESC_VOID(desc);

	gpiod_free_commit(desc);
	module_put(desc->gdev->owner);
	gpio_device_put(desc->gdev);
}

/**
 * gpiochip_dup_line_label - Get a copy of the consumer label.
 * @gc: GPIO chip controlling this line.
 * @offset: Hardware offset of the line.
 *
 * Returns:
 * Pointer to a copy of the consumer label if the line is requested or NULL
 * if it's not. If a valid pointer was returned, it must be freed using
 * kfree(). In case of a memory allocation error, the function returns %ENOMEM.
 *
 * Must not be called from atomic context.
 */
char *gpiochip_dup_line_label(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_desc *desc;
	char *label;

	desc = gpiochip_get_desc(gc, offset);
	if (IS_ERR(desc))
		return NULL;

	if (!test_bit(FLAG_REQUESTED, &desc->flags))
		return NULL;

	guard(srcu)(&desc->srcu);

	label = kstrdup(gpiod_get_label(desc), GFP_KERNEL);
	if (!label)
		return ERR_PTR(-ENOMEM);

	return label;
}
EXPORT_SYMBOL_GPL(gpiochip_dup_line_label);

static inline const char *function_name_or_default(const char *con_id)
{
	return con_id ?: "(default)";
}

/**
 * gpiochip_request_own_desc - Allow GPIO chip to request its own descriptor
 * @gc: GPIO chip
 * @hwnum: hardware number of the GPIO for which to request the descriptor
 * @label: label for the GPIO
 * @lflags: lookup flags for this GPIO or 0 if default, this can be used to
 * specify things like line inversion semantics with the machine flags
 * such as GPIO_OUT_LOW
 * @dflags: descriptor request flags for this GPIO or 0 if default, this
 * can be used to specify consumer semantics such as open drain
 *
 * Function allows GPIO chip drivers to request and use their own GPIO
 * descriptors via gpiolib API. Difference to gpiod_request() is that this
 * function will not increase reference count of the GPIO chip module. This
 * allows the GPIO chip module to be unloaded as needed (we assume that the
 * GPIO chip driver handles freeing the GPIOs it has requested).
 *
 * Returns:
 * A pointer to the GPIO descriptor, or an ERR_PTR()-encoded negative error
 * code on failure.
 */
struct gpio_desc *gpiochip_request_own_desc(struct gpio_chip *gc,
					    unsigned int hwnum,
					    const char *label,
					    enum gpio_lookup_flags lflags,
					    enum gpiod_flags dflags)
{
	struct gpio_desc *desc = gpiochip_get_desc(gc, hwnum);
	const char *name = function_name_or_default(label);
	int ret;

	if (IS_ERR(desc)) {
		chip_err(gc, "failed to get GPIO %s descriptor\n", name);
		return desc;
	}

	ret = gpiod_request_commit(desc, label);
	if (ret < 0)
		return ERR_PTR(ret);

	ret = gpiod_configure_flags(desc, label, lflags, dflags);
	if (ret) {
		gpiod_free_commit(desc);
		chip_err(gc, "setup of own GPIO %s failed\n", name);
		return ERR_PTR(ret);
	}

	return desc;
}
EXPORT_SYMBOL_GPL(gpiochip_request_own_desc);

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
		gpiod_free_commit(desc);
}
EXPORT_SYMBOL_GPL(gpiochip_free_own_desc);

/*
 * Drivers MUST set GPIO direction before making get/set calls.  In
 * some cases this is done in early boot, before IRQs are enabled.
 *
 * As a rule these aren't called more than once (except for drivers
 * using the open-drain emulation idiom) so these are natural places
 * to accumulate extra debugging checks.  Note that we can't (yet)
 * rely on gpio_request() having been called beforehand.
 */

static int gpio_do_set_config(struct gpio_chip *gc, unsigned int offset,
			      unsigned long config)
{
	if (!gc->set_config)
		return -ENOTSUPP;

	return gc->set_config(gc, offset, config);
}

static int gpio_set_config_with_argument(struct gpio_desc *desc,
					 enum pin_config_param mode,
					 u32 argument)
{
	unsigned long config;

	CLASS(gpio_chip_guard, guard)(desc);
	if (!guard.gc)
		return -ENODEV;

	config = pinconf_to_config_packed(mode, argument);
	return gpio_do_set_config(guard.gc, gpio_chip_hwgpio(desc), config);
}

static int gpio_set_config_with_argument_optional(struct gpio_desc *desc,
						  enum pin_config_param mode,
						  u32 argument)
{
	struct device *dev = &desc->gdev->dev;
	int gpio = gpio_chip_hwgpio(desc);
	int ret;

	ret = gpio_set_config_with_argument(desc, mode, argument);
	if (ret != -ENOTSUPP)
		return ret;

	switch (mode) {
	case PIN_CONFIG_PERSIST_STATE:
		dev_dbg(dev, "Persistence not supported for GPIO %d\n", gpio);
		break;
	default:
		break;
	}

	return 0;
}

static int gpio_set_config(struct gpio_desc *desc, enum pin_config_param mode)
{
	return gpio_set_config_with_argument(desc, mode, 0);
}

static int gpio_set_bias(struct gpio_desc *desc)
{
	enum pin_config_param bias;
	unsigned long flags;
	unsigned int arg;

	flags = READ_ONCE(desc->flags);

	if (test_bit(FLAG_BIAS_DISABLE, &flags))
		bias = PIN_CONFIG_BIAS_DISABLE;
	else if (test_bit(FLAG_PULL_UP, &flags))
		bias = PIN_CONFIG_BIAS_PULL_UP;
	else if (test_bit(FLAG_PULL_DOWN, &flags))
		bias = PIN_CONFIG_BIAS_PULL_DOWN;
	else
		return 0;

	switch (bias) {
	case PIN_CONFIG_BIAS_PULL_DOWN:
	case PIN_CONFIG_BIAS_PULL_UP:
		arg = 1;
		break;

	default:
		arg = 0;
		break;
	}

	return gpio_set_config_with_argument_optional(desc, bias, arg);
}

/**
 * gpio_set_debounce_timeout() - Set debounce timeout
 * @desc:	GPIO descriptor to set the debounce timeout
 * @debounce:	Debounce timeout in microseconds
 *
 * The function calls the certain GPIO driver to set debounce timeout
 * in the hardware.
 *
 * Returns 0 on success, or negative error code otherwise.
 */
int gpio_set_debounce_timeout(struct gpio_desc *desc, unsigned int debounce)
{
	return gpio_set_config_with_argument_optional(desc,
						      PIN_CONFIG_INPUT_DEBOUNCE,
						      debounce);
}

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
	int ret = 0;

	VALIDATE_DESC(desc);

	CLASS(gpio_chip_guard, guard)(desc);
	if (!guard.gc)
		return -ENODEV;

	/*
	 * It is legal to have no .get() and .direction_input() specified if
	 * the chip is output-only, but you can't specify .direction_input()
	 * and not support the .get() operation, that doesn't make sense.
	 */
	if (!guard.gc->get && guard.gc->direction_input) {
		gpiod_warn(desc,
			   "%s: missing get() but have direction_input()\n",
			   __func__);
		return -EIO;
	}

	/*
	 * If we have a .direction_input() callback, things are simple,
	 * just call it. Else we are some input-only chip so try to check the
	 * direction (if .get_direction() is supported) else we silently
	 * assume we are in input mode after this.
	 */
	if (guard.gc->direction_input) {
		ret = guard.gc->direction_input(guard.gc,
						gpio_chip_hwgpio(desc));
	} else if (guard.gc->get_direction &&
		  (guard.gc->get_direction(guard.gc,
					   gpio_chip_hwgpio(desc)) != 1)) {
		gpiod_warn(desc,
			   "%s: missing direction_input() operation and line is output\n",
			   __func__);
		return -EIO;
	}
	if (ret == 0) {
		clear_bit(FLAG_IS_OUT, &desc->flags);
		ret = gpio_set_bias(desc);
	}

	trace_gpio_direction(desc_to_gpio(desc), 1, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(gpiod_direction_input);

static int gpiod_direction_output_raw_commit(struct gpio_desc *desc, int value)
{
	int val = !!value, ret = 0;

	CLASS(gpio_chip_guard, guard)(desc);
	if (!guard.gc)
		return -ENODEV;

	/*
	 * It's OK not to specify .direction_output() if the gpiochip is
	 * output-only, but if there is then not even a .set() operation it
	 * is pretty tricky to drive the output line.
	 */
	if (!guard.gc->set && !guard.gc->direction_output) {
		gpiod_warn(desc,
			   "%s: missing set() and direction_output() operations\n",
			   __func__);
		return -EIO;
	}

	if (guard.gc->direction_output) {
		ret = guard.gc->direction_output(guard.gc,
						 gpio_chip_hwgpio(desc), val);
	} else {
		/* Check that we are in output mode if we can */
		if (guard.gc->get_direction &&
		    guard.gc->get_direction(guard.gc, gpio_chip_hwgpio(desc))) {
			gpiod_warn(desc,
				"%s: missing direction_output() operation\n",
				__func__);
			return -EIO;
		}
		/*
		 * If we can't actively set the direction, we are some
		 * output-only chip, so just drive the output as desired.
		 */
		guard.gc->set(guard.gc, gpio_chip_hwgpio(desc), val);
	}

	if (!ret)
		set_bit(FLAG_IS_OUT, &desc->flags);
	trace_gpio_value(desc_to_gpio(desc), 0, val);
	trace_gpio_direction(desc_to_gpio(desc), 0, ret);
	return ret;
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
	VALIDATE_DESC(desc);
	return gpiod_direction_output_raw_commit(desc, value);
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
	unsigned long flags;
	int ret;

	VALIDATE_DESC(desc);

	flags = READ_ONCE(desc->flags);

	if (test_bit(FLAG_ACTIVE_LOW, &flags))
		value = !value;
	else
		value = !!value;

	/* GPIOs used for enabled IRQs shall not be set as output */
	if (test_bit(FLAG_USED_AS_IRQ, &flags) &&
	    test_bit(FLAG_IRQ_IS_ENABLED, &flags)) {
		gpiod_err(desc,
			  "%s: tried to set a GPIO tied to an IRQ as output\n",
			  __func__);
		return -EIO;
	}

	if (test_bit(FLAG_OPEN_DRAIN, &flags)) {
		/* First see if we can enable open drain in hardware */
		ret = gpio_set_config(desc, PIN_CONFIG_DRIVE_OPEN_DRAIN);
		if (!ret)
			goto set_output_value;
		/* Emulate open drain by not actively driving the line high */
		if (value) {
			ret = gpiod_direction_input(desc);
			goto set_output_flag;
		}
	} else if (test_bit(FLAG_OPEN_SOURCE, &flags)) {
		ret = gpio_set_config(desc, PIN_CONFIG_DRIVE_OPEN_SOURCE);
		if (!ret)
			goto set_output_value;
		/* Emulate open source by not actively driving the line low */
		if (!value) {
			ret = gpiod_direction_input(desc);
			goto set_output_flag;
		}
	} else {
		gpio_set_config(desc, PIN_CONFIG_DRIVE_PUSH_PULL);
	}

set_output_value:
	ret = gpio_set_bias(desc);
	if (ret)
		return ret;
	return gpiod_direction_output_raw_commit(desc, value);

set_output_flag:
	/*
	 * When emulating open-source or open-drain functionalities by not
	 * actively driving the line (setting mode to input) we still need to
	 * set the IS_OUT flag or otherwise we won't be able to set the line
	 * value anymore.
	 */
	if (ret == 0)
		set_bit(FLAG_IS_OUT, &desc->flags);
	return ret;
}
EXPORT_SYMBOL_GPL(gpiod_direction_output);

/**
 * gpiod_enable_hw_timestamp_ns - Enable hardware timestamp in nanoseconds.
 *
 * @desc: GPIO to enable.
 * @flags: Flags related to GPIO edge.
 *
 * Return 0 in case of success, else negative error code.
 */
int gpiod_enable_hw_timestamp_ns(struct gpio_desc *desc, unsigned long flags)
{
	int ret = 0;

	VALIDATE_DESC(desc);

	CLASS(gpio_chip_guard, guard)(desc);
	if (!guard.gc)
		return -ENODEV;

	if (!guard.gc->en_hw_timestamp) {
		gpiod_warn(desc, "%s: hw ts not supported\n", __func__);
		return -ENOTSUPP;
	}

	ret = guard.gc->en_hw_timestamp(guard.gc,
					gpio_chip_hwgpio(desc), flags);
	if (ret)
		gpiod_warn(desc, "%s: hw ts request failed\n", __func__);

	return ret;
}
EXPORT_SYMBOL_GPL(gpiod_enable_hw_timestamp_ns);

/**
 * gpiod_disable_hw_timestamp_ns - Disable hardware timestamp.
 *
 * @desc: GPIO to disable.
 * @flags: Flags related to GPIO edge, same value as used during enable call.
 *
 * Return 0 in case of success, else negative error code.
 */
int gpiod_disable_hw_timestamp_ns(struct gpio_desc *desc, unsigned long flags)
{
	int ret = 0;

	VALIDATE_DESC(desc);

	CLASS(gpio_chip_guard, guard)(desc);
	if (!guard.gc)
		return -ENODEV;

	if (!guard.gc->dis_hw_timestamp) {
		gpiod_warn(desc, "%s: hw ts not supported\n", __func__);
		return -ENOTSUPP;
	}

	ret = guard.gc->dis_hw_timestamp(guard.gc, gpio_chip_hwgpio(desc),
					 flags);
	if (ret)
		gpiod_warn(desc, "%s: hw ts release failed\n", __func__);

	return ret;
}
EXPORT_SYMBOL_GPL(gpiod_disable_hw_timestamp_ns);

/**
 * gpiod_set_config - sets @config for a GPIO
 * @desc: descriptor of the GPIO for which to set the configuration
 * @config: Same packed config format as generic pinconf
 *
 * Returns:
 * 0 on success, %-ENOTSUPP if the controller doesn't support setting the
 * configuration.
 */
int gpiod_set_config(struct gpio_desc *desc, unsigned long config)
{
	VALIDATE_DESC(desc);

	CLASS(gpio_chip_guard, guard)(desc);
	if (!guard.gc)
		return -ENODEV;

	return gpio_do_set_config(guard.gc, gpio_chip_hwgpio(desc), config);
}
EXPORT_SYMBOL_GPL(gpiod_set_config);

/**
 * gpiod_set_debounce - sets @debounce time for a GPIO
 * @desc: descriptor of the GPIO for which to set debounce time
 * @debounce: debounce time in microseconds
 *
 * Returns:
 * 0 on success, %-ENOTSUPP if the controller doesn't support setting the
 * debounce time.
 */
int gpiod_set_debounce(struct gpio_desc *desc, unsigned int debounce)
{
	unsigned long config;

	config = pinconf_to_config_packed(PIN_CONFIG_INPUT_DEBOUNCE, debounce);
	return gpiod_set_config(desc, config);
}
EXPORT_SYMBOL_GPL(gpiod_set_debounce);

/**
 * gpiod_set_transitory - Lose or retain GPIO state on suspend or reset
 * @desc: descriptor of the GPIO for which to configure persistence
 * @transitory: True to lose state on suspend or reset, false for persistence
 *
 * Returns:
 * 0 on success, otherwise a negative error code.
 */
int gpiod_set_transitory(struct gpio_desc *desc, bool transitory)
{
	VALIDATE_DESC(desc);
	/*
	 * Handle FLAG_TRANSITORY first, enabling queries to gpiolib for
	 * persistence state.
	 */
	assign_bit(FLAG_TRANSITORY, &desc->flags, transitory);

	/* If the driver supports it, set the persistence state now */
	return gpio_set_config_with_argument_optional(desc,
						      PIN_CONFIG_PERSIST_STATE,
						      !transitory);
}

/**
 * gpiod_is_active_low - test whether a GPIO is active-low or not
 * @desc: the gpio descriptor to test
 *
 * Returns 1 if the GPIO is active-low, 0 otherwise.
 */
int gpiod_is_active_low(const struct gpio_desc *desc)
{
	VALIDATE_DESC(desc);
	return test_bit(FLAG_ACTIVE_LOW, &desc->flags);
}
EXPORT_SYMBOL_GPL(gpiod_is_active_low);

/**
 * gpiod_toggle_active_low - toggle whether a GPIO is active-low or not
 * @desc: the gpio descriptor to change
 */
void gpiod_toggle_active_low(struct gpio_desc *desc)
{
	VALIDATE_DESC_VOID(desc);
	change_bit(FLAG_ACTIVE_LOW, &desc->flags);
}
EXPORT_SYMBOL_GPL(gpiod_toggle_active_low);

static int gpio_chip_get_value(struct gpio_chip *gc, const struct gpio_desc *desc)
{
	return gc->get ? gc->get(gc, gpio_chip_hwgpio(desc)) : -EIO;
}

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

static int gpiod_get_raw_value_commit(const struct gpio_desc *desc)
{
	struct gpio_device *gdev;
	struct gpio_chip *gc;
	int value;

	/* FIXME Unable to use gpio_chip_guard due to const desc. */
	gdev = desc->gdev;

	guard(srcu)(&gdev->srcu);

	gc = srcu_dereference(gdev->chip, &gdev->srcu);
	if (!gc)
		return -ENODEV;

	value = gpio_chip_get_value(gc, desc);
	value = value < 0 ? value : !!value;
	trace_gpio_value(desc_to_gpio(desc), 1, value);
	return value;
}

static int gpio_chip_get_multiple(struct gpio_chip *gc,
				  unsigned long *mask, unsigned long *bits)
{
	if (gc->get_multiple)
		return gc->get_multiple(gc, mask, bits);
	if (gc->get) {
		int i, value;

		for_each_set_bit(i, mask, gc->ngpio) {
			value = gc->get(gc, i);
			if (value < 0)
				return value;
			__assign_bit(i, bits, value);
		}
		return 0;
	}
	return -EIO;
}

/* The 'other' chip must be protected with its GPIO device's SRCU. */
static bool gpio_device_chip_cmp(struct gpio_device *gdev, struct gpio_chip *gc)
{
	guard(srcu)(&gdev->srcu);

	return gc == srcu_dereference(gdev->chip, &gdev->srcu);
}

int gpiod_get_array_value_complex(bool raw, bool can_sleep,
				  unsigned int array_size,
				  struct gpio_desc **desc_array,
				  struct gpio_array *array_info,
				  unsigned long *value_bitmap)
{
	int ret, i = 0;

	/*
	 * Validate array_info against desc_array and its size.
	 * It should immediately follow desc_array if both
	 * have been obtained from the same gpiod_get_array() call.
	 */
	if (array_info && array_info->desc == desc_array &&
	    array_size <= array_info->size &&
	    (void *)array_info == desc_array + array_info->size) {
		if (!can_sleep)
			WARN_ON(array_info->chip->can_sleep);

		ret = gpio_chip_get_multiple(array_info->chip,
					     array_info->get_mask,
					     value_bitmap);
		if (ret)
			return ret;

		if (!raw && !bitmap_empty(array_info->invert_mask, array_size))
			bitmap_xor(value_bitmap, value_bitmap,
				   array_info->invert_mask, array_size);

		i = find_first_zero_bit(array_info->get_mask, array_size);
		if (i == array_size)
			return 0;
	} else {
		array_info = NULL;
	}

	while (i < array_size) {
		DECLARE_BITMAP(fastpath_mask, FASTPATH_NGPIO);
		DECLARE_BITMAP(fastpath_bits, FASTPATH_NGPIO);
		unsigned long *mask, *bits;
		int first, j;

		CLASS(gpio_chip_guard, guard)(desc_array[i]);
		if (!guard.gc)
			return -ENODEV;

		if (likely(guard.gc->ngpio <= FASTPATH_NGPIO)) {
			mask = fastpath_mask;
			bits = fastpath_bits;
		} else {
			gfp_t flags = can_sleep ? GFP_KERNEL : GFP_ATOMIC;

			mask = bitmap_alloc(guard.gc->ngpio, flags);
			if (!mask)
				return -ENOMEM;

			bits = bitmap_alloc(guard.gc->ngpio, flags);
			if (!bits) {
				bitmap_free(mask);
				return -ENOMEM;
			}
		}

		bitmap_zero(mask, guard.gc->ngpio);

		if (!can_sleep)
			WARN_ON(guard.gc->can_sleep);

		/* collect all inputs belonging to the same chip */
		first = i;
		do {
			const struct gpio_desc *desc = desc_array[i];
			int hwgpio = gpio_chip_hwgpio(desc);

			__set_bit(hwgpio, mask);
			i++;

			if (array_info)
				i = find_next_zero_bit(array_info->get_mask,
						       array_size, i);
		} while ((i < array_size) &&
			 gpio_device_chip_cmp(desc_array[i]->gdev, guard.gc));

		ret = gpio_chip_get_multiple(guard.gc, mask, bits);
		if (ret) {
			if (mask != fastpath_mask)
				bitmap_free(mask);
			if (bits != fastpath_bits)
				bitmap_free(bits);
			return ret;
		}

		for (j = first; j < i; ) {
			const struct gpio_desc *desc = desc_array[j];
			int hwgpio = gpio_chip_hwgpio(desc);
			int value = test_bit(hwgpio, bits);

			if (!raw && test_bit(FLAG_ACTIVE_LOW, &desc->flags))
				value = !value;
			__assign_bit(j, value_bitmap, value);
			trace_gpio_value(desc_to_gpio(desc), 1, value);
			j++;

			if (array_info)
				j = find_next_zero_bit(array_info->get_mask, i,
						       j);
		}

		if (mask != fastpath_mask)
			bitmap_free(mask);
		if (bits != fastpath_bits)
			bitmap_free(bits);
	}
	return 0;
}

/**
 * gpiod_get_raw_value() - return a gpio's raw value
 * @desc: gpio whose value will be returned
 *
 * Return the GPIO's raw value, i.e. the value of the physical line disregarding
 * its ACTIVE_LOW status, or negative errno on failure.
 *
 * This function can be called from contexts where we cannot sleep, and will
 * complain if the GPIO chip functions potentially sleep.
 */
int gpiod_get_raw_value(const struct gpio_desc *desc)
{
	VALIDATE_DESC(desc);
	/* Should be using gpiod_get_raw_value_cansleep() */
	WARN_ON(desc->gdev->can_sleep);
	return gpiod_get_raw_value_commit(desc);
}
EXPORT_SYMBOL_GPL(gpiod_get_raw_value);

/**
 * gpiod_get_value() - return a gpio's value
 * @desc: gpio whose value will be returned
 *
 * Return the GPIO's logical value, i.e. taking the ACTIVE_LOW status into
 * account, or negative errno on failure.
 *
 * This function can be called from contexts where we cannot sleep, and will
 * complain if the GPIO chip functions potentially sleep.
 */
int gpiod_get_value(const struct gpio_desc *desc)
{
	int value;

	VALIDATE_DESC(desc);
	/* Should be using gpiod_get_value_cansleep() */
	WARN_ON(desc->gdev->can_sleep);

	value = gpiod_get_raw_value_commit(desc);
	if (value < 0)
		return value;

	if (test_bit(FLAG_ACTIVE_LOW, &desc->flags))
		value = !value;

	return value;
}
EXPORT_SYMBOL_GPL(gpiod_get_value);

/**
 * gpiod_get_raw_array_value() - read raw values from an array of GPIOs
 * @array_size: number of elements in the descriptor array / value bitmap
 * @desc_array: array of GPIO descriptors whose values will be read
 * @array_info: information on applicability of fast bitmap processing path
 * @value_bitmap: bitmap to store the read values
 *
 * Read the raw values of the GPIOs, i.e. the values of the physical lines
 * without regard for their ACTIVE_LOW status.  Return 0 in case of success,
 * else an error code.
 *
 * This function can be called from contexts where we cannot sleep,
 * and it will complain if the GPIO chip functions potentially sleep.
 */
int gpiod_get_raw_array_value(unsigned int array_size,
			      struct gpio_desc **desc_array,
			      struct gpio_array *array_info,
			      unsigned long *value_bitmap)
{
	if (!desc_array)
		return -EINVAL;
	return gpiod_get_array_value_complex(true, false, array_size,
					     desc_array, array_info,
					     value_bitmap);
}
EXPORT_SYMBOL_GPL(gpiod_get_raw_array_value);

/**
 * gpiod_get_array_value() - read values from an array of GPIOs
 * @array_size: number of elements in the descriptor array / value bitmap
 * @desc_array: array of GPIO descriptors whose values will be read
 * @array_info: information on applicability of fast bitmap processing path
 * @value_bitmap: bitmap to store the read values
 *
 * Read the logical values of the GPIOs, i.e. taking their ACTIVE_LOW status
 * into account.  Return 0 in case of success, else an error code.
 *
 * This function can be called from contexts where we cannot sleep,
 * and it will complain if the GPIO chip functions potentially sleep.
 */
int gpiod_get_array_value(unsigned int array_size,
			  struct gpio_desc **desc_array,
			  struct gpio_array *array_info,
			  unsigned long *value_bitmap)
{
	if (!desc_array)
		return -EINVAL;
	return gpiod_get_array_value_complex(false, false, array_size,
					     desc_array, array_info,
					     value_bitmap);
}
EXPORT_SYMBOL_GPL(gpiod_get_array_value);

/*
 *  gpio_set_open_drain_value_commit() - Set the open drain gpio's value.
 * @desc: gpio descriptor whose state need to be set.
 * @value: Non-zero for setting it HIGH otherwise it will set to LOW.
 */
static void gpio_set_open_drain_value_commit(struct gpio_desc *desc, bool value)
{
	int ret = 0, offset = gpio_chip_hwgpio(desc);

	CLASS(gpio_chip_guard, guard)(desc);
	if (!guard.gc)
		return;

	if (value) {
		ret = guard.gc->direction_input(guard.gc, offset);
	} else {
		ret = guard.gc->direction_output(guard.gc, offset, 0);
		if (!ret)
			set_bit(FLAG_IS_OUT, &desc->flags);
	}
	trace_gpio_direction(desc_to_gpio(desc), value, ret);
	if (ret < 0)
		gpiod_err(desc,
			  "%s: Error in set_value for open drain err %d\n",
			  __func__, ret);
}

/*
 *  _gpio_set_open_source_value() - Set the open source gpio's value.
 * @desc: gpio descriptor whose state need to be set.
 * @value: Non-zero for setting it HIGH otherwise it will set to LOW.
 */
static void gpio_set_open_source_value_commit(struct gpio_desc *desc, bool value)
{
	int ret = 0, offset = gpio_chip_hwgpio(desc);

	CLASS(gpio_chip_guard, guard)(desc);
	if (!guard.gc)
		return;

	if (value) {
		ret = guard.gc->direction_output(guard.gc, offset, 1);
		if (!ret)
			set_bit(FLAG_IS_OUT, &desc->flags);
	} else {
		ret = guard.gc->direction_input(guard.gc, offset);
	}
	trace_gpio_direction(desc_to_gpio(desc), !value, ret);
	if (ret < 0)
		gpiod_err(desc,
			  "%s: Error in set_value for open source err %d\n",
			  __func__, ret);
}

static void gpiod_set_raw_value_commit(struct gpio_desc *desc, bool value)
{
	CLASS(gpio_chip_guard, guard)(desc);
	if (!guard.gc)
		return;

	trace_gpio_value(desc_to_gpio(desc), 0, value);
	guard.gc->set(guard.gc, gpio_chip_hwgpio(desc), value);
}

/*
 * set multiple outputs on the same chip;
 * use the chip's set_multiple function if available;
 * otherwise set the outputs sequentially;
 * @chip: the GPIO chip we operate on
 * @mask: bit mask array; one bit per output; BITS_PER_LONG bits per word
 *        defines which outputs are to be changed
 * @bits: bit value array; one bit per output; BITS_PER_LONG bits per word
 *        defines the values the outputs specified by mask are to be set to
 */
static void gpio_chip_set_multiple(struct gpio_chip *gc,
				   unsigned long *mask, unsigned long *bits)
{
	if (gc->set_multiple) {
		gc->set_multiple(gc, mask, bits);
	} else {
		unsigned int i;

		/* set outputs if the corresponding mask bit is set */
		for_each_set_bit(i, mask, gc->ngpio)
			gc->set(gc, i, test_bit(i, bits));
	}
}

int gpiod_set_array_value_complex(bool raw, bool can_sleep,
				  unsigned int array_size,
				  struct gpio_desc **desc_array,
				  struct gpio_array *array_info,
				  unsigned long *value_bitmap)
{
	int i = 0;

	/*
	 * Validate array_info against desc_array and its size.
	 * It should immediately follow desc_array if both
	 * have been obtained from the same gpiod_get_array() call.
	 */
	if (array_info && array_info->desc == desc_array &&
	    array_size <= array_info->size &&
	    (void *)array_info == desc_array + array_info->size) {
		if (!can_sleep)
			WARN_ON(array_info->chip->can_sleep);

		if (!raw && !bitmap_empty(array_info->invert_mask, array_size))
			bitmap_xor(value_bitmap, value_bitmap,
				   array_info->invert_mask, array_size);

		gpio_chip_set_multiple(array_info->chip, array_info->set_mask,
				       value_bitmap);

		i = find_first_zero_bit(array_info->set_mask, array_size);
		if (i == array_size)
			return 0;
	} else {
		array_info = NULL;
	}

	while (i < array_size) {
		DECLARE_BITMAP(fastpath_mask, FASTPATH_NGPIO);
		DECLARE_BITMAP(fastpath_bits, FASTPATH_NGPIO);
		unsigned long *mask, *bits;
		int count = 0;

		CLASS(gpio_chip_guard, guard)(desc_array[i]);
		if (!guard.gc)
			return -ENODEV;

		if (likely(guard.gc->ngpio <= FASTPATH_NGPIO)) {
			mask = fastpath_mask;
			bits = fastpath_bits;
		} else {
			gfp_t flags = can_sleep ? GFP_KERNEL : GFP_ATOMIC;

			mask = bitmap_alloc(guard.gc->ngpio, flags);
			if (!mask)
				return -ENOMEM;

			bits = bitmap_alloc(guard.gc->ngpio, flags);
			if (!bits) {
				bitmap_free(mask);
				return -ENOMEM;
			}
		}

		bitmap_zero(mask, guard.gc->ngpio);

		if (!can_sleep)
			WARN_ON(guard.gc->can_sleep);

		do {
			struct gpio_desc *desc = desc_array[i];
			int hwgpio = gpio_chip_hwgpio(desc);
			int value = test_bit(i, value_bitmap);

			/*
			 * Pins applicable for fast input but not for
			 * fast output processing may have been already
			 * inverted inside the fast path, skip them.
			 */
			if (!raw && !(array_info &&
			    test_bit(i, array_info->invert_mask)) &&
			    test_bit(FLAG_ACTIVE_LOW, &desc->flags))
				value = !value;
			trace_gpio_value(desc_to_gpio(desc), 0, value);
			/*
			 * collect all normal outputs belonging to the same chip
			 * open drain and open source outputs are set individually
			 */
			if (test_bit(FLAG_OPEN_DRAIN, &desc->flags) && !raw) {
				gpio_set_open_drain_value_commit(desc, value);
			} else if (test_bit(FLAG_OPEN_SOURCE, &desc->flags) && !raw) {
				gpio_set_open_source_value_commit(desc, value);
			} else {
				__set_bit(hwgpio, mask);
				__assign_bit(hwgpio, bits, value);
				count++;
			}
			i++;

			if (array_info)
				i = find_next_zero_bit(array_info->set_mask,
						       array_size, i);
		} while ((i < array_size) &&
			 gpio_device_chip_cmp(desc_array[i]->gdev, guard.gc));
		/* push collected bits to outputs */
		if (count != 0)
			gpio_chip_set_multiple(guard.gc, mask, bits);

		if (mask != fastpath_mask)
			bitmap_free(mask);
		if (bits != fastpath_bits)
			bitmap_free(bits);
	}
	return 0;
}

/**
 * gpiod_set_raw_value() - assign a gpio's raw value
 * @desc: gpio whose value will be assigned
 * @value: value to assign
 *
 * Set the raw value of the GPIO, i.e. the value of its physical line without
 * regard for its ACTIVE_LOW status.
 *
 * This function can be called from contexts where we cannot sleep, and will
 * complain if the GPIO chip functions potentially sleep.
 */
void gpiod_set_raw_value(struct gpio_desc *desc, int value)
{
	VALIDATE_DESC_VOID(desc);
	/* Should be using gpiod_set_raw_value_cansleep() */
	WARN_ON(desc->gdev->can_sleep);
	gpiod_set_raw_value_commit(desc, value);
}
EXPORT_SYMBOL_GPL(gpiod_set_raw_value);

/**
 * gpiod_set_value_nocheck() - set a GPIO line value without checking
 * @desc: the descriptor to set the value on
 * @value: value to set
 *
 * This sets the value of a GPIO line backing a descriptor, applying
 * different semantic quirks like active low and open drain/source
 * handling.
 */
static void gpiod_set_value_nocheck(struct gpio_desc *desc, int value)
{
	if (test_bit(FLAG_ACTIVE_LOW, &desc->flags))
		value = !value;
	if (test_bit(FLAG_OPEN_DRAIN, &desc->flags))
		gpio_set_open_drain_value_commit(desc, value);
	else if (test_bit(FLAG_OPEN_SOURCE, &desc->flags))
		gpio_set_open_source_value_commit(desc, value);
	else
		gpiod_set_raw_value_commit(desc, value);
}

/**
 * gpiod_set_value() - assign a gpio's value
 * @desc: gpio whose value will be assigned
 * @value: value to assign
 *
 * Set the logical value of the GPIO, i.e. taking its ACTIVE_LOW,
 * OPEN_DRAIN and OPEN_SOURCE flags into account.
 *
 * This function can be called from contexts where we cannot sleep, and will
 * complain if the GPIO chip functions potentially sleep.
 */
void gpiod_set_value(struct gpio_desc *desc, int value)
{
	VALIDATE_DESC_VOID(desc);
	/* Should be using gpiod_set_value_cansleep() */
	WARN_ON(desc->gdev->can_sleep);
	gpiod_set_value_nocheck(desc, value);
}
EXPORT_SYMBOL_GPL(gpiod_set_value);

/**
 * gpiod_set_raw_array_value() - assign values to an array of GPIOs
 * @array_size: number of elements in the descriptor array / value bitmap
 * @desc_array: array of GPIO descriptors whose values will be assigned
 * @array_info: information on applicability of fast bitmap processing path
 * @value_bitmap: bitmap of values to assign
 *
 * Set the raw values of the GPIOs, i.e. the values of the physical lines
 * without regard for their ACTIVE_LOW status.
 *
 * This function can be called from contexts where we cannot sleep, and will
 * complain if the GPIO chip functions potentially sleep.
 */
int gpiod_set_raw_array_value(unsigned int array_size,
			      struct gpio_desc **desc_array,
			      struct gpio_array *array_info,
			      unsigned long *value_bitmap)
{
	if (!desc_array)
		return -EINVAL;
	return gpiod_set_array_value_complex(true, false, array_size,
					desc_array, array_info, value_bitmap);
}
EXPORT_SYMBOL_GPL(gpiod_set_raw_array_value);

/**
 * gpiod_set_array_value() - assign values to an array of GPIOs
 * @array_size: number of elements in the descriptor array / value bitmap
 * @desc_array: array of GPIO descriptors whose values will be assigned
 * @array_info: information on applicability of fast bitmap processing path
 * @value_bitmap: bitmap of values to assign
 *
 * Set the logical values of the GPIOs, i.e. taking their ACTIVE_LOW status
 * into account.
 *
 * This function can be called from contexts where we cannot sleep, and will
 * complain if the GPIO chip functions potentially sleep.
 */
int gpiod_set_array_value(unsigned int array_size,
			  struct gpio_desc **desc_array,
			  struct gpio_array *array_info,
			  unsigned long *value_bitmap)
{
	if (!desc_array)
		return -EINVAL;
	return gpiod_set_array_value_complex(false, false, array_size,
					     desc_array, array_info,
					     value_bitmap);
}
EXPORT_SYMBOL_GPL(gpiod_set_array_value);

/**
 * gpiod_cansleep() - report whether gpio value access may sleep
 * @desc: gpio to check
 *
 */
int gpiod_cansleep(const struct gpio_desc *desc)
{
	VALIDATE_DESC(desc);
	return desc->gdev->can_sleep;
}
EXPORT_SYMBOL_GPL(gpiod_cansleep);

/**
 * gpiod_set_consumer_name() - set the consumer name for the descriptor
 * @desc: gpio to set the consumer name on
 * @name: the new consumer name
 */
int gpiod_set_consumer_name(struct gpio_desc *desc, const char *name)
{
	VALIDATE_DESC(desc);

	return desc_set_label(desc, name);
}
EXPORT_SYMBOL_GPL(gpiod_set_consumer_name);

/**
 * gpiod_to_irq() - return the IRQ corresponding to a GPIO
 * @desc: gpio whose IRQ will be returned (already requested)
 *
 * Return the IRQ corresponding to the passed GPIO, or an error code in case of
 * error.
 */
int gpiod_to_irq(const struct gpio_desc *desc)
{
	struct gpio_device *gdev;
	struct gpio_chip *gc;
	int offset;

	/*
	 * Cannot VALIDATE_DESC() here as gpiod_to_irq() consumer semantics
	 * requires this function to not return zero on an invalid descriptor
	 * but rather a negative error number.
	 */
	if (!desc || IS_ERR(desc))
		return -EINVAL;

	gdev = desc->gdev;
	/* FIXME Cannot use gpio_chip_guard due to const desc. */
	guard(srcu)(&gdev->srcu);
	gc = srcu_dereference(gdev->chip, &gdev->srcu);
	if (!gc)
		return -ENODEV;

	offset = gpio_chip_hwgpio(desc);
	if (gc->to_irq) {
		int retirq = gc->to_irq(gc, offset);

		/* Zero means NO_IRQ */
		if (!retirq)
			return -ENXIO;

		return retirq;
	}
#ifdef CONFIG_GPIOLIB_IRQCHIP
	if (gc->irq.chip) {
		/*
		 * Avoid race condition with other code, which tries to lookup
		 * an IRQ before the irqchip has been properly registered,
		 * i.e. while gpiochip is still being brought up.
		 */
		return -EPROBE_DEFER;
	}
#endif
	return -ENXIO;
}
EXPORT_SYMBOL_GPL(gpiod_to_irq);

/**
 * gpiochip_lock_as_irq() - lock a GPIO to be used as IRQ
 * @gc: the chip the GPIO to lock belongs to
 * @offset: the offset of the GPIO to lock as IRQ
 *
 * This is used directly by GPIO drivers that want to lock down
 * a certain GPIO line to be used for IRQs.
 */
int gpiochip_lock_as_irq(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_desc *desc;

	desc = gpiochip_get_desc(gc, offset);
	if (IS_ERR(desc))
		return PTR_ERR(desc);

	/*
	 * If it's fast: flush the direction setting if something changed
	 * behind our back
	 */
	if (!gc->can_sleep && gc->get_direction) {
		int dir = gpiod_get_direction(desc);

		if (dir < 0) {
			chip_err(gc, "%s: cannot get GPIO direction\n",
				 __func__);
			return dir;
		}
	}

	/* To be valid for IRQ the line needs to be input or open drain */
	if (test_bit(FLAG_IS_OUT, &desc->flags) &&
	    !test_bit(FLAG_OPEN_DRAIN, &desc->flags)) {
		chip_err(gc,
			 "%s: tried to flag a GPIO set as output for IRQ\n",
			 __func__);
		return -EIO;
	}

	set_bit(FLAG_USED_AS_IRQ, &desc->flags);
	set_bit(FLAG_IRQ_IS_ENABLED, &desc->flags);

	return 0;
}
EXPORT_SYMBOL_GPL(gpiochip_lock_as_irq);

/**
 * gpiochip_unlock_as_irq() - unlock a GPIO used as IRQ
 * @gc: the chip the GPIO to lock belongs to
 * @offset: the offset of the GPIO to lock as IRQ
 *
 * This is used directly by GPIO drivers that want to indicate
 * that a certain GPIO is no longer used exclusively for IRQ.
 */
void gpiochip_unlock_as_irq(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_desc *desc;

	desc = gpiochip_get_desc(gc, offset);
	if (IS_ERR(desc))
		return;

	clear_bit(FLAG_USED_AS_IRQ, &desc->flags);
	clear_bit(FLAG_IRQ_IS_ENABLED, &desc->flags);
}
EXPORT_SYMBOL_GPL(gpiochip_unlock_as_irq);

void gpiochip_disable_irq(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_desc *desc = gpiochip_get_desc(gc, offset);

	if (!IS_ERR(desc) &&
	    !WARN_ON(!test_bit(FLAG_USED_AS_IRQ, &desc->flags)))
		clear_bit(FLAG_IRQ_IS_ENABLED, &desc->flags);
}
EXPORT_SYMBOL_GPL(gpiochip_disable_irq);

void gpiochip_enable_irq(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_desc *desc = gpiochip_get_desc(gc, offset);

	if (!IS_ERR(desc) &&
	    !WARN_ON(!test_bit(FLAG_USED_AS_IRQ, &desc->flags))) {
		/*
		 * We must not be output when using IRQ UNLESS we are
		 * open drain.
		 */
		WARN_ON(test_bit(FLAG_IS_OUT, &desc->flags) &&
			!test_bit(FLAG_OPEN_DRAIN, &desc->flags));
		set_bit(FLAG_IRQ_IS_ENABLED, &desc->flags);
	}
}
EXPORT_SYMBOL_GPL(gpiochip_enable_irq);

bool gpiochip_line_is_irq(struct gpio_chip *gc, unsigned int offset)
{
	if (offset >= gc->ngpio)
		return false;

	return test_bit(FLAG_USED_AS_IRQ, &gc->gpiodev->descs[offset].flags);
}
EXPORT_SYMBOL_GPL(gpiochip_line_is_irq);

int gpiochip_reqres_irq(struct gpio_chip *gc, unsigned int offset)
{
	int ret;

	if (!try_module_get(gc->gpiodev->owner))
		return -ENODEV;

	ret = gpiochip_lock_as_irq(gc, offset);
	if (ret) {
		chip_err(gc, "unable to lock HW IRQ %u for IRQ\n", offset);
		module_put(gc->gpiodev->owner);
		return ret;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(gpiochip_reqres_irq);

void gpiochip_relres_irq(struct gpio_chip *gc, unsigned int offset)
{
	gpiochip_unlock_as_irq(gc, offset);
	module_put(gc->gpiodev->owner);
}
EXPORT_SYMBOL_GPL(gpiochip_relres_irq);

bool gpiochip_line_is_open_drain(struct gpio_chip *gc, unsigned int offset)
{
	if (offset >= gc->ngpio)
		return false;

	return test_bit(FLAG_OPEN_DRAIN, &gc->gpiodev->descs[offset].flags);
}
EXPORT_SYMBOL_GPL(gpiochip_line_is_open_drain);

bool gpiochip_line_is_open_source(struct gpio_chip *gc, unsigned int offset)
{
	if (offset >= gc->ngpio)
		return false;

	return test_bit(FLAG_OPEN_SOURCE, &gc->gpiodev->descs[offset].flags);
}
EXPORT_SYMBOL_GPL(gpiochip_line_is_open_source);

bool gpiochip_line_is_persistent(struct gpio_chip *gc, unsigned int offset)
{
	if (offset >= gc->ngpio)
		return false;

	return !test_bit(FLAG_TRANSITORY, &gc->gpiodev->descs[offset].flags);
}
EXPORT_SYMBOL_GPL(gpiochip_line_is_persistent);

/**
 * gpiod_get_raw_value_cansleep() - return a gpio's raw value
 * @desc: gpio whose value will be returned
 *
 * Return the GPIO's raw value, i.e. the value of the physical line disregarding
 * its ACTIVE_LOW status, or negative errno on failure.
 *
 * This function is to be called from contexts that can sleep.
 */
int gpiod_get_raw_value_cansleep(const struct gpio_desc *desc)
{
	might_sleep();
	VALIDATE_DESC(desc);
	return gpiod_get_raw_value_commit(desc);
}
EXPORT_SYMBOL_GPL(gpiod_get_raw_value_cansleep);

/**
 * gpiod_get_value_cansleep() - return a gpio's value
 * @desc: gpio whose value will be returned
 *
 * Return the GPIO's logical value, i.e. taking the ACTIVE_LOW status into
 * account, or negative errno on failure.
 *
 * This function is to be called from contexts that can sleep.
 */
int gpiod_get_value_cansleep(const struct gpio_desc *desc)
{
	int value;

	might_sleep();
	VALIDATE_DESC(desc);
	value = gpiod_get_raw_value_commit(desc);
	if (value < 0)
		return value;

	if (test_bit(FLAG_ACTIVE_LOW, &desc->flags))
		value = !value;

	return value;
}
EXPORT_SYMBOL_GPL(gpiod_get_value_cansleep);

/**
 * gpiod_get_raw_array_value_cansleep() - read raw values from an array of GPIOs
 * @array_size: number of elements in the descriptor array / value bitmap
 * @desc_array: array of GPIO descriptors whose values will be read
 * @array_info: information on applicability of fast bitmap processing path
 * @value_bitmap: bitmap to store the read values
 *
 * Read the raw values of the GPIOs, i.e. the values of the physical lines
 * without regard for their ACTIVE_LOW status.  Return 0 in case of success,
 * else an error code.
 *
 * This function is to be called from contexts that can sleep.
 */
int gpiod_get_raw_array_value_cansleep(unsigned int array_size,
				       struct gpio_desc **desc_array,
				       struct gpio_array *array_info,
				       unsigned long *value_bitmap)
{
	might_sleep();
	if (!desc_array)
		return -EINVAL;
	return gpiod_get_array_value_complex(true, true, array_size,
					     desc_array, array_info,
					     value_bitmap);
}
EXPORT_SYMBOL_GPL(gpiod_get_raw_array_value_cansleep);

/**
 * gpiod_get_array_value_cansleep() - read values from an array of GPIOs
 * @array_size: number of elements in the descriptor array / value bitmap
 * @desc_array: array of GPIO descriptors whose values will be read
 * @array_info: information on applicability of fast bitmap processing path
 * @value_bitmap: bitmap to store the read values
 *
 * Read the logical values of the GPIOs, i.e. taking their ACTIVE_LOW status
 * into account.  Return 0 in case of success, else an error code.
 *
 * This function is to be called from contexts that can sleep.
 */
int gpiod_get_array_value_cansleep(unsigned int array_size,
				   struct gpio_desc **desc_array,
				   struct gpio_array *array_info,
				   unsigned long *value_bitmap)
{
	might_sleep();
	if (!desc_array)
		return -EINVAL;
	return gpiod_get_array_value_complex(false, true, array_size,
					     desc_array, array_info,
					     value_bitmap);
}
EXPORT_SYMBOL_GPL(gpiod_get_array_value_cansleep);

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
	might_sleep();
	VALIDATE_DESC_VOID(desc);
	gpiod_set_raw_value_commit(desc, value);
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
	might_sleep();
	VALIDATE_DESC_VOID(desc);
	gpiod_set_value_nocheck(desc, value);
}
EXPORT_SYMBOL_GPL(gpiod_set_value_cansleep);

/**
 * gpiod_set_raw_array_value_cansleep() - assign values to an array of GPIOs
 * @array_size: number of elements in the descriptor array / value bitmap
 * @desc_array: array of GPIO descriptors whose values will be assigned
 * @array_info: information on applicability of fast bitmap processing path
 * @value_bitmap: bitmap of values to assign
 *
 * Set the raw values of the GPIOs, i.e. the values of the physical lines
 * without regard for their ACTIVE_LOW status.
 *
 * This function is to be called from contexts that can sleep.
 */
int gpiod_set_raw_array_value_cansleep(unsigned int array_size,
				       struct gpio_desc **desc_array,
				       struct gpio_array *array_info,
				       unsigned long *value_bitmap)
{
	might_sleep();
	if (!desc_array)
		return -EINVAL;
	return gpiod_set_array_value_complex(true, true, array_size, desc_array,
				      array_info, value_bitmap);
}
EXPORT_SYMBOL_GPL(gpiod_set_raw_array_value_cansleep);

/**
 * gpiod_add_lookup_tables() - register GPIO device consumers
 * @tables: list of tables of consumers to register
 * @n: number of tables in the list
 */
void gpiod_add_lookup_tables(struct gpiod_lookup_table **tables, size_t n)
{
	unsigned int i;

	mutex_lock(&gpio_lookup_lock);

	for (i = 0; i < n; i++)
		list_add_tail(&tables[i]->list, &gpio_lookup_list);

	mutex_unlock(&gpio_lookup_lock);
}

/**
 * gpiod_set_array_value_cansleep() - assign values to an array of GPIOs
 * @array_size: number of elements in the descriptor array / value bitmap
 * @desc_array: array of GPIO descriptors whose values will be assigned
 * @array_info: information on applicability of fast bitmap processing path
 * @value_bitmap: bitmap of values to assign
 *
 * Set the logical values of the GPIOs, i.e. taking their ACTIVE_LOW status
 * into account.
 *
 * This function is to be called from contexts that can sleep.
 */
int gpiod_set_array_value_cansleep(unsigned int array_size,
				   struct gpio_desc **desc_array,
				   struct gpio_array *array_info,
				   unsigned long *value_bitmap)
{
	might_sleep();
	if (!desc_array)
		return -EINVAL;
	return gpiod_set_array_value_complex(false, true, array_size,
					     desc_array, array_info,
					     value_bitmap);
}
EXPORT_SYMBOL_GPL(gpiod_set_array_value_cansleep);

void gpiod_line_state_notify(struct gpio_desc *desc, unsigned long action)
{
	blocking_notifier_call_chain(&desc->gdev->line_state_notifier,
				     action, desc);
}

/**
 * gpiod_add_lookup_table() - register GPIO device consumers
 * @table: table of consumers to register
 */
void gpiod_add_lookup_table(struct gpiod_lookup_table *table)
{
	gpiod_add_lookup_tables(&table, 1);
}
EXPORT_SYMBOL_GPL(gpiod_add_lookup_table);

/**
 * gpiod_remove_lookup_table() - unregister GPIO device consumers
 * @table: table of consumers to unregister
 */
void gpiod_remove_lookup_table(struct gpiod_lookup_table *table)
{
	/* Nothing to remove */
	if (!table)
		return;

	mutex_lock(&gpio_lookup_lock);

	list_del(&table->list);

	mutex_unlock(&gpio_lookup_lock);
}
EXPORT_SYMBOL_GPL(gpiod_remove_lookup_table);

/**
 * gpiod_add_hogs() - register a set of GPIO hogs from machine code
 * @hogs: table of gpio hog entries with a zeroed sentinel at the end
 */
void gpiod_add_hogs(struct gpiod_hog *hogs)
{
	struct gpiod_hog *hog;

	mutex_lock(&gpio_machine_hogs_mutex);

	for (hog = &hogs[0]; hog->chip_label; hog++) {
		list_add_tail(&hog->list, &gpio_machine_hogs);

		/*
		 * The chip may have been registered earlier, so check if it
		 * exists and, if so, try to hog the line now.
		 */
		struct gpio_device *gdev __free(gpio_device_put) =
				gpio_device_find_by_label(hog->chip_label);
		if (gdev)
			gpiochip_machine_hog(gpio_device_get_chip(gdev), hog);
	}

	mutex_unlock(&gpio_machine_hogs_mutex);
}
EXPORT_SYMBOL_GPL(gpiod_add_hogs);

void gpiod_remove_hogs(struct gpiod_hog *hogs)
{
	struct gpiod_hog *hog;

	mutex_lock(&gpio_machine_hogs_mutex);
	for (hog = &hogs[0]; hog->chip_label; hog++)
		list_del(&hog->list);
	mutex_unlock(&gpio_machine_hogs_mutex);
}
EXPORT_SYMBOL_GPL(gpiod_remove_hogs);

static struct gpiod_lookup_table *gpiod_find_lookup_table(struct device *dev)
{
	const char *dev_id = dev ? dev_name(dev) : NULL;
	struct gpiod_lookup_table *table;

	list_for_each_entry(table, &gpio_lookup_list, list) {
		if (table->dev_id && dev_id) {
			/*
			 * Valid strings on both ends, must be identical to have
			 * a match
			 */
			if (!strcmp(table->dev_id, dev_id))
				return table;
		} else {
			/*
			 * One of the pointers is NULL, so both must be to have
			 * a match
			 */
			if (dev_id == table->dev_id)
				return table;
		}
	}

	return NULL;
}

static struct gpio_desc *gpiod_find(struct device *dev, const char *con_id,
				    unsigned int idx, unsigned long *flags)
{
	struct gpio_desc *desc = ERR_PTR(-ENOENT);
	struct gpiod_lookup_table *table;
	struct gpiod_lookup *p;
	struct gpio_chip *gc;

	guard(mutex)(&gpio_lookup_lock);

	table = gpiod_find_lookup_table(dev);
	if (!table)
		return desc;

	for (p = &table->table[0]; p->key; p++) {
		/* idx must always match exactly */
		if (p->idx != idx)
			continue;

		/* If the lookup entry has a con_id, require exact match */
		if (p->con_id && (!con_id || strcmp(p->con_id, con_id)))
			continue;

		if (p->chip_hwnum == U16_MAX) {
			desc = gpio_name_to_desc(p->key);
			if (desc) {
				*flags = p->flags;
				return desc;
			}

			dev_warn(dev, "cannot find GPIO line %s, deferring\n",
				 p->key);
			return ERR_PTR(-EPROBE_DEFER);
		}

		struct gpio_device *gdev __free(gpio_device_put) =
					gpio_device_find_by_label(p->key);
		if (!gdev) {
			/*
			 * As the lookup table indicates a chip with
			 * p->key should exist, assume it may
			 * still appear later and let the interested
			 * consumer be probed again or let the Deferred
			 * Probe infrastructure handle the error.
			 */
			dev_warn(dev, "cannot find GPIO chip %s, deferring\n",
				 p->key);
			return ERR_PTR(-EPROBE_DEFER);
		}

		gc = gpio_device_get_chip(gdev);

		if (gc->ngpio <= p->chip_hwnum) {
			dev_err(dev,
				"requested GPIO %u (%u) is out of range [0..%u] for chip %s\n",
				idx, p->chip_hwnum, gc->ngpio - 1,
				gc->label);
			return ERR_PTR(-EINVAL);
		}

		desc = gpio_device_get_desc(gdev, p->chip_hwnum);
		*flags = p->flags;

		return desc;
	}

	return desc;
}

static int platform_gpio_count(struct device *dev, const char *con_id)
{
	struct gpiod_lookup_table *table;
	struct gpiod_lookup *p;
	unsigned int count = 0;

	scoped_guard(mutex, &gpio_lookup_lock) {
		table = gpiod_find_lookup_table(dev);
		if (!table)
			return -ENOENT;

		for (p = &table->table[0]; p->key; p++) {
			if ((con_id && p->con_id && !strcmp(con_id, p->con_id)) ||
			    (!con_id && !p->con_id))
				count++;
		}
	}

	if (!count)
		return -ENOENT;

	return count;
}

static struct gpio_desc *gpiod_find_by_fwnode(struct fwnode_handle *fwnode,
					      struct device *consumer,
					      const char *con_id,
					      unsigned int idx,
					      enum gpiod_flags *flags,
					      unsigned long *lookupflags)
{
	const char *name = function_name_or_default(con_id);
	struct gpio_desc *desc = ERR_PTR(-ENOENT);

	if (is_of_node(fwnode)) {
		dev_dbg(consumer, "using DT '%pfw' for '%s' GPIO lookup\n", fwnode, name);
		desc = of_find_gpio(to_of_node(fwnode), con_id, idx, lookupflags);
	} else if (is_acpi_node(fwnode)) {
		dev_dbg(consumer, "using ACPI '%pfw' for '%s' GPIO lookup\n", fwnode, name);
		desc = acpi_find_gpio(fwnode, con_id, idx, flags, lookupflags);
	} else if (is_software_node(fwnode)) {
		dev_dbg(consumer, "using swnode '%pfw' for '%s' GPIO lookup\n", fwnode, name);
		desc = swnode_find_gpio(fwnode, con_id, idx, lookupflags);
	}

	return desc;
}

struct gpio_desc *gpiod_find_and_request(struct device *consumer,
					 struct fwnode_handle *fwnode,
					 const char *con_id,
					 unsigned int idx,
					 enum gpiod_flags flags,
					 const char *label,
					 bool platform_lookup_allowed)
{
	unsigned long lookupflags = GPIO_LOOKUP_FLAGS_DEFAULT;
	const char *name = function_name_or_default(con_id);
	/*
	 * scoped_guard() is implemented as a for loop, meaning static
	 * analyzers will complain about these two not being initialized.
	 */
	struct gpio_desc *desc = NULL;
	int ret = 0;

	scoped_guard(srcu, &gpio_devices_srcu) {
		desc = gpiod_find_by_fwnode(fwnode, consumer, con_id, idx,
					    &flags, &lookupflags);
		if (gpiod_not_found(desc) && platform_lookup_allowed) {
			/*
			 * Either we are not using DT or ACPI, or their lookup
			 * did not return a result. In that case, use platform
			 * lookup as a fallback.
			 */
			dev_dbg(consumer,
				"using lookup tables for GPIO lookup\n");
			desc = gpiod_find(consumer, con_id, idx, &lookupflags);
		}

		if (IS_ERR(desc)) {
			dev_dbg(consumer, "No GPIO consumer %s found\n", name);
			return desc;
		}

		/*
		 * If a connection label was passed use that, else attempt to use
		 * the device name as label
		 */
		ret = gpiod_request(desc, label);
	}
	if (ret) {
		if (!(ret == -EBUSY && flags & GPIOD_FLAGS_BIT_NONEXCLUSIVE))
			return ERR_PTR(ret);

		/*
		 * This happens when there are several consumers for
		 * the same GPIO line: we just return here without
		 * further initialization. It is a bit of a hack.
		 * This is necessary to support fixed regulators.
		 *
		 * FIXME: Make this more sane and safe.
		 */
		dev_info(consumer, "nonexclusive access to GPIO for %s\n", name);
		return desc;
	}

	ret = gpiod_configure_flags(desc, con_id, lookupflags, flags);
	if (ret < 0) {
		gpiod_put(desc);
		dev_dbg(consumer, "setup of GPIO %s failed\n", name);
		return ERR_PTR(ret);
	}

	gpiod_line_state_notify(desc, GPIOLINE_CHANGED_REQUESTED);

	return desc;
}

/**
 * fwnode_gpiod_get_index - obtain a GPIO from firmware node
 * @fwnode:	handle of the firmware node
 * @con_id:	function within the GPIO consumer
 * @index:	index of the GPIO to obtain for the consumer
 * @flags:	GPIO initialization flags
 * @label:	label to attach to the requested GPIO
 *
 * This function can be used for drivers that get their configuration
 * from opaque firmware.
 *
 * The function properly finds the corresponding GPIO using whatever is the
 * underlying firmware interface and then makes sure that the GPIO
 * descriptor is requested before it is returned to the caller.
 *
 * Returns:
 * On successful request the GPIO pin is configured in accordance with
 * provided @flags.
 *
 * In case of error an ERR_PTR() is returned.
 */
struct gpio_desc *fwnode_gpiod_get_index(struct fwnode_handle *fwnode,
					 const char *con_id,
					 int index,
					 enum gpiod_flags flags,
					 const char *label)
{
	return gpiod_find_and_request(NULL, fwnode, con_id, index, flags, label, false);
}
EXPORT_SYMBOL_GPL(fwnode_gpiod_get_index);

/**
 * gpiod_count - return the number of GPIOs associated with a device / function
 *		or -ENOENT if no GPIO has been assigned to the requested function
 * @dev:	GPIO consumer, can be NULL for system-global GPIOs
 * @con_id:	function within the GPIO consumer
 */
int gpiod_count(struct device *dev, const char *con_id)
{
	const struct fwnode_handle *fwnode = dev ? dev_fwnode(dev) : NULL;
	int count = -ENOENT;

	if (is_of_node(fwnode))
		count = of_gpio_count(fwnode, con_id);
	else if (is_acpi_node(fwnode))
		count = acpi_gpio_count(fwnode, con_id);
	else if (is_software_node(fwnode))
		count = swnode_gpio_count(fwnode, con_id);

	if (count < 0)
		count = platform_gpio_count(dev, con_id);

	return count;
}
EXPORT_SYMBOL_GPL(gpiod_count);

/**
 * gpiod_get - obtain a GPIO for a given GPIO function
 * @dev:	GPIO consumer, can be NULL for system-global GPIOs
 * @con_id:	function within the GPIO consumer
 * @flags:	optional GPIO initialization flags
 *
 * Return the GPIO descriptor corresponding to the function con_id of device
 * dev, -ENOENT if no GPIO has been assigned to the requested function, or
 * another IS_ERR() code if an error occurred while trying to acquire the GPIO.
 */
struct gpio_desc *__must_check gpiod_get(struct device *dev, const char *con_id,
					 enum gpiod_flags flags)
{
	return gpiod_get_index(dev, con_id, 0, flags);
}
EXPORT_SYMBOL_GPL(gpiod_get);

/**
 * gpiod_get_optional - obtain an optional GPIO for a given GPIO function
 * @dev: GPIO consumer, can be NULL for system-global GPIOs
 * @con_id: function within the GPIO consumer
 * @flags: optional GPIO initialization flags
 *
 * This is equivalent to gpiod_get(), except that when no GPIO was assigned to
 * the requested function it will return NULL. This is convenient for drivers
 * that need to handle optional GPIOs.
 */
struct gpio_desc *__must_check gpiod_get_optional(struct device *dev,
						  const char *con_id,
						  enum gpiod_flags flags)
{
	return gpiod_get_index_optional(dev, con_id, 0, flags);
}
EXPORT_SYMBOL_GPL(gpiod_get_optional);


/**
 * gpiod_configure_flags - helper function to configure a given GPIO
 * @desc:	gpio whose value will be assigned
 * @con_id:	function within the GPIO consumer
 * @lflags:	bitmask of gpio_lookup_flags GPIO_* values - returned from
 *		of_find_gpio() or of_get_gpio_hog()
 * @dflags:	gpiod_flags - optional GPIO initialization flags
 *
 * Return 0 on success, -ENOENT if no GPIO has been assigned to the
 * requested function and/or index, or another IS_ERR() code if an error
 * occurred while trying to acquire the GPIO.
 */
int gpiod_configure_flags(struct gpio_desc *desc, const char *con_id,
		unsigned long lflags, enum gpiod_flags dflags)
{
	const char *name = function_name_or_default(con_id);
	int ret;

	if (lflags & GPIO_ACTIVE_LOW)
		set_bit(FLAG_ACTIVE_LOW, &desc->flags);

	if (lflags & GPIO_OPEN_DRAIN)
		set_bit(FLAG_OPEN_DRAIN, &desc->flags);
	else if (dflags & GPIOD_FLAGS_BIT_OPEN_DRAIN) {
		/*
		 * This enforces open drain mode from the consumer side.
		 * This is necessary for some busses like I2C, but the lookup
		 * should *REALLY* have specified them as open drain in the
		 * first place, so print a little warning here.
		 */
		set_bit(FLAG_OPEN_DRAIN, &desc->flags);
		gpiod_warn(desc,
			   "enforced open drain please flag it properly in DT/ACPI DSDT/board file\n");
	}

	if (lflags & GPIO_OPEN_SOURCE)
		set_bit(FLAG_OPEN_SOURCE, &desc->flags);

	if (((lflags & GPIO_PULL_UP) && (lflags & GPIO_PULL_DOWN)) ||
	    ((lflags & GPIO_PULL_UP) && (lflags & GPIO_PULL_DISABLE)) ||
	    ((lflags & GPIO_PULL_DOWN) && (lflags & GPIO_PULL_DISABLE))) {
		gpiod_err(desc,
			  "multiple pull-up, pull-down or pull-disable enabled, invalid configuration\n");
		return -EINVAL;
	}

	if (lflags & GPIO_PULL_UP)
		set_bit(FLAG_PULL_UP, &desc->flags);
	else if (lflags & GPIO_PULL_DOWN)
		set_bit(FLAG_PULL_DOWN, &desc->flags);
	else if (lflags & GPIO_PULL_DISABLE)
		set_bit(FLAG_BIAS_DISABLE, &desc->flags);

	ret = gpiod_set_transitory(desc, (lflags & GPIO_TRANSITORY));
	if (ret < 0)
		return ret;

	/* No particular flag request, return here... */
	if (!(dflags & GPIOD_FLAGS_BIT_DIR_SET)) {
		gpiod_dbg(desc, "no flags found for GPIO %s\n", name);
		return 0;
	}

	/* Process flags */
	if (dflags & GPIOD_FLAGS_BIT_DIR_OUT)
		ret = gpiod_direction_output(desc,
				!!(dflags & GPIOD_FLAGS_BIT_DIR_VAL));
	else
		ret = gpiod_direction_input(desc);

	return ret;
}

/**
 * gpiod_get_index - obtain a GPIO from a multi-index GPIO function
 * @dev:	GPIO consumer, can be NULL for system-global GPIOs
 * @con_id:	function within the GPIO consumer
 * @idx:	index of the GPIO to obtain in the consumer
 * @flags:	optional GPIO initialization flags
 *
 * This variant of gpiod_get() allows to access GPIOs other than the first
 * defined one for functions that define several GPIOs.
 *
 * Return a valid GPIO descriptor, -ENOENT if no GPIO has been assigned to the
 * requested function and/or index, or another IS_ERR() code if an error
 * occurred while trying to acquire the GPIO.
 */
struct gpio_desc *__must_check gpiod_get_index(struct device *dev,
					       const char *con_id,
					       unsigned int idx,
					       enum gpiod_flags flags)
{
	struct fwnode_handle *fwnode = dev ? dev_fwnode(dev) : NULL;
	const char *devname = dev ? dev_name(dev) : "?";
	const char *label = con_id ?: devname;

	return gpiod_find_and_request(dev, fwnode, con_id, idx, flags, label, true);
}
EXPORT_SYMBOL_GPL(gpiod_get_index);

/**
 * gpiod_get_index_optional - obtain an optional GPIO from a multi-index GPIO
 *                            function
 * @dev: GPIO consumer, can be NULL for system-global GPIOs
 * @con_id: function within the GPIO consumer
 * @index: index of the GPIO to obtain in the consumer
 * @flags: optional GPIO initialization flags
 *
 * This is equivalent to gpiod_get_index(), except that when no GPIO with the
 * specified index was assigned to the requested function it will return NULL.
 * This is convenient for drivers that need to handle optional GPIOs.
 */
struct gpio_desc *__must_check gpiod_get_index_optional(struct device *dev,
							const char *con_id,
							unsigned int index,
							enum gpiod_flags flags)
{
	struct gpio_desc *desc;

	desc = gpiod_get_index(dev, con_id, index, flags);
	if (gpiod_not_found(desc))
		return NULL;

	return desc;
}
EXPORT_SYMBOL_GPL(gpiod_get_index_optional);

/**
 * gpiod_hog - Hog the specified GPIO desc given the provided flags
 * @desc:	gpio whose value will be assigned
 * @name:	gpio line name
 * @lflags:	bitmask of gpio_lookup_flags GPIO_* values - returned from
 *		of_find_gpio() or of_get_gpio_hog()
 * @dflags:	gpiod_flags - optional GPIO initialization flags
 */
int gpiod_hog(struct gpio_desc *desc, const char *name,
	      unsigned long lflags, enum gpiod_flags dflags)
{
	struct gpio_device *gdev = desc->gdev;
	struct gpio_desc *local_desc;
	int hwnum;
	int ret;

	CLASS(gpio_chip_guard, guard)(desc);
	if (!guard.gc)
		return -ENODEV;

	if (test_and_set_bit(FLAG_IS_HOGGED, &desc->flags))
		return 0;

	hwnum = gpio_chip_hwgpio(desc);

	local_desc = gpiochip_request_own_desc(guard.gc, hwnum, name,
					       lflags, dflags);
	if (IS_ERR(local_desc)) {
		clear_bit(FLAG_IS_HOGGED, &desc->flags);
		ret = PTR_ERR(local_desc);
		pr_err("requesting hog GPIO %s (chip %s, offset %d) failed, %d\n",
		       name, gdev->label, hwnum, ret);
		return ret;
	}

	gpiod_dbg(desc, "hogged as %s%s\n",
		(dflags & GPIOD_FLAGS_BIT_DIR_OUT) ? "output" : "input",
		(dflags & GPIOD_FLAGS_BIT_DIR_OUT) ?
		  (dflags & GPIOD_FLAGS_BIT_DIR_VAL) ? "/high" : "/low" : "");

	return 0;
}

/**
 * gpiochip_free_hogs - Scan gpio-controller chip and release GPIO hog
 * @gc:	gpio chip to act on
 */
static void gpiochip_free_hogs(struct gpio_chip *gc)
{
	struct gpio_desc *desc;

	for_each_gpio_desc_with_flag(gc, desc, FLAG_IS_HOGGED)
		gpiochip_free_own_desc(desc);
}

/**
 * gpiod_get_array - obtain multiple GPIOs from a multi-index GPIO function
 * @dev:	GPIO consumer, can be NULL for system-global GPIOs
 * @con_id:	function within the GPIO consumer
 * @flags:	optional GPIO initialization flags
 *
 * This function acquires all the GPIOs defined under a given function.
 *
 * Return a struct gpio_descs containing an array of descriptors, -ENOENT if
 * no GPIO has been assigned to the requested function, or another IS_ERR()
 * code if an error occurred while trying to acquire the GPIOs.
 */
struct gpio_descs *__must_check gpiod_get_array(struct device *dev,
						const char *con_id,
						enum gpiod_flags flags)
{
	struct gpio_desc *desc;
	struct gpio_descs *descs;
	struct gpio_array *array_info = NULL;
	struct gpio_chip *gc;
	int count, bitmap_size;
	size_t descs_size;

	count = gpiod_count(dev, con_id);
	if (count < 0)
		return ERR_PTR(count);

	descs_size = struct_size(descs, desc, count);
	descs = kzalloc(descs_size, GFP_KERNEL);
	if (!descs)
		return ERR_PTR(-ENOMEM);

	for (descs->ndescs = 0; descs->ndescs < count; descs->ndescs++) {
		desc = gpiod_get_index(dev, con_id, descs->ndescs, flags);
		if (IS_ERR(desc)) {
			gpiod_put_array(descs);
			return ERR_CAST(desc);
		}

		descs->desc[descs->ndescs] = desc;

		gc = gpiod_to_chip(desc);
		/*
		 * If pin hardware number of array member 0 is also 0, select
		 * its chip as a candidate for fast bitmap processing path.
		 */
		if (descs->ndescs == 0 && gpio_chip_hwgpio(desc) == 0) {
			struct gpio_descs *array;

			bitmap_size = BITS_TO_LONGS(gc->ngpio > count ?
						    gc->ngpio : count);

			array = krealloc(descs, descs_size +
					 struct_size(array_info, invert_mask, 3 * bitmap_size),
					 GFP_KERNEL | __GFP_ZERO);
			if (!array) {
				gpiod_put_array(descs);
				return ERR_PTR(-ENOMEM);
			}

			descs = array;

			array_info = (void *)descs + descs_size;
			array_info->get_mask = array_info->invert_mask +
						  bitmap_size;
			array_info->set_mask = array_info->get_mask +
						  bitmap_size;

			array_info->desc = descs->desc;
			array_info->size = count;
			array_info->chip = gc;
			bitmap_set(array_info->get_mask, descs->ndescs,
				   count - descs->ndescs);
			bitmap_set(array_info->set_mask, descs->ndescs,
				   count - descs->ndescs);
			descs->info = array_info;
		}

		/* If there is no cache for fast bitmap processing path, continue */
		if (!array_info)
			continue;

		/* Unmark array members which don't belong to the 'fast' chip */
		if (array_info->chip != gc) {
			__clear_bit(descs->ndescs, array_info->get_mask);
			__clear_bit(descs->ndescs, array_info->set_mask);
		}
		/*
		 * Detect array members which belong to the 'fast' chip
		 * but their pins are not in hardware order.
		 */
		else if (gpio_chip_hwgpio(desc) != descs->ndescs) {
			/*
			 * Don't use fast path if all array members processed so
			 * far belong to the same chip as this one but its pin
			 * hardware number is different from its array index.
			 */
			if (bitmap_full(array_info->get_mask, descs->ndescs)) {
				array_info = NULL;
			} else {
				__clear_bit(descs->ndescs,
					    array_info->get_mask);
				__clear_bit(descs->ndescs,
					    array_info->set_mask);
			}
		} else {
			/* Exclude open drain or open source from fast output */
			if (gpiochip_line_is_open_drain(gc, descs->ndescs) ||
			    gpiochip_line_is_open_source(gc, descs->ndescs))
				__clear_bit(descs->ndescs,
					    array_info->set_mask);
			/* Identify 'fast' pins which require invertion */
			if (gpiod_is_active_low(desc))
				__set_bit(descs->ndescs,
					  array_info->invert_mask);
		}
	}
	if (array_info)
		dev_dbg(dev,
			"GPIO array info: chip=%s, size=%d, get_mask=%lx, set_mask=%lx, invert_mask=%lx\n",
			array_info->chip->label, array_info->size,
			*array_info->get_mask, *array_info->set_mask,
			*array_info->invert_mask);
	return descs;
}
EXPORT_SYMBOL_GPL(gpiod_get_array);

/**
 * gpiod_get_array_optional - obtain multiple GPIOs from a multi-index GPIO
 *                            function
 * @dev:	GPIO consumer, can be NULL for system-global GPIOs
 * @con_id:	function within the GPIO consumer
 * @flags:	optional GPIO initialization flags
 *
 * This is equivalent to gpiod_get_array(), except that when no GPIO was
 * assigned to the requested function it will return NULL.
 */
struct gpio_descs *__must_check gpiod_get_array_optional(struct device *dev,
							const char *con_id,
							enum gpiod_flags flags)
{
	struct gpio_descs *descs;

	descs = gpiod_get_array(dev, con_id, flags);
	if (gpiod_not_found(descs))
		return NULL;

	return descs;
}
EXPORT_SYMBOL_GPL(gpiod_get_array_optional);

/**
 * gpiod_put - dispose of a GPIO descriptor
 * @desc:	GPIO descriptor to dispose of
 *
 * No descriptor can be used after gpiod_put() has been called on it.
 */
void gpiod_put(struct gpio_desc *desc)
{
	if (desc)
		gpiod_free(desc);
}
EXPORT_SYMBOL_GPL(gpiod_put);

/**
 * gpiod_put_array - dispose of multiple GPIO descriptors
 * @descs:	struct gpio_descs containing an array of descriptors
 */
void gpiod_put_array(struct gpio_descs *descs)
{
	unsigned int i;

	for (i = 0; i < descs->ndescs; i++)
		gpiod_put(descs->desc[i]);

	kfree(descs);
}
EXPORT_SYMBOL_GPL(gpiod_put_array);

static int gpio_stub_drv_probe(struct device *dev)
{
	/*
	 * The DT node of some GPIO chips have a "compatible" property, but
	 * never have a struct device added and probed by a driver to register
	 * the GPIO chip with gpiolib. In such cases, fw_devlink=on will cause
	 * the consumers of the GPIO chip to get probe deferred forever because
	 * they will be waiting for a device associated with the GPIO chip
	 * firmware node to get added and bound to a driver.
	 *
	 * To allow these consumers to probe, we associate the struct
	 * gpio_device of the GPIO chip with the firmware node and then simply
	 * bind it to this stub driver.
	 */
	return 0;
}

static struct device_driver gpio_stub_drv = {
	.name = "gpio_stub_drv",
	.bus = &gpio_bus_type,
	.probe = gpio_stub_drv_probe,
};

static int __init gpiolib_dev_init(void)
{
	int ret;

	/* Register GPIO sysfs bus */
	ret = bus_register(&gpio_bus_type);
	if (ret < 0) {
		pr_err("gpiolib: could not register GPIO bus type\n");
		return ret;
	}

	ret = driver_register(&gpio_stub_drv);
	if (ret < 0) {
		pr_err("gpiolib: could not register GPIO stub driver\n");
		bus_unregister(&gpio_bus_type);
		return ret;
	}

	ret = alloc_chrdev_region(&gpio_devt, 0, GPIO_DEV_MAX, GPIOCHIP_NAME);
	if (ret < 0) {
		pr_err("gpiolib: failed to allocate char dev region\n");
		driver_unregister(&gpio_stub_drv);
		bus_unregister(&gpio_bus_type);
		return ret;
	}

	gpiolib_initialized = true;
	gpiochip_setup_devs();

#if IS_ENABLED(CONFIG_OF_DYNAMIC) && IS_ENABLED(CONFIG_OF_GPIO)
	WARN_ON(of_reconfig_notifier_register(&gpio_of_notifier));
#endif /* CONFIG_OF_DYNAMIC && CONFIG_OF_GPIO */

	return ret;
}
core_initcall(gpiolib_dev_init);

#ifdef CONFIG_DEBUG_FS

static void gpiolib_dbg_show(struct seq_file *s, struct gpio_device *gdev)
{
	bool active_low, is_irq, is_out;
	unsigned int gpio = gdev->base;
	struct gpio_desc *desc;
	struct gpio_chip *gc;
	int value;

	guard(srcu)(&gdev->srcu);

	gc = srcu_dereference(gdev->chip, &gdev->srcu);
	if (!gc) {
		seq_puts(s, "Underlying GPIO chip is gone\n");
		return;
	}

	for_each_gpio_desc(gc, desc) {
		guard(srcu)(&desc->srcu);
		if (test_bit(FLAG_REQUESTED, &desc->flags)) {
			gpiod_get_direction(desc);
			is_out = test_bit(FLAG_IS_OUT, &desc->flags);
			value = gpio_chip_get_value(gc, desc);
			is_irq = test_bit(FLAG_USED_AS_IRQ, &desc->flags);
			active_low = test_bit(FLAG_ACTIVE_LOW, &desc->flags);
			seq_printf(s, " gpio-%-3d (%-20.20s|%-20.20s) %s %s %s%s\n",
				   gpio, desc->name ?: "", gpiod_get_label(desc),
				   is_out ? "out" : "in ",
				   value >= 0 ? (value ? "hi" : "lo") : "?  ",
				   is_irq ? "IRQ " : "",
				   active_low ? "ACTIVE LOW" : "");
		} else if (desc->name) {
			seq_printf(s, " gpio-%-3d (%-20.20s)\n", gpio, desc->name);
		}

		gpio++;
	}
}

struct gpiolib_seq_priv {
	bool newline;
	int idx;
};

static void *gpiolib_seq_start(struct seq_file *s, loff_t *pos)
{
	struct gpiolib_seq_priv *priv;
	struct gpio_device *gdev;
	loff_t index = *pos;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return NULL;

	s->private = priv;
	priv->idx = srcu_read_lock(&gpio_devices_srcu);

	list_for_each_entry_srcu(gdev, &gpio_devices, list,
				 srcu_read_lock_held(&gpio_devices_srcu)) {
		if (index-- == 0)
			return gdev;
	}

	return NULL;
}

static void *gpiolib_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct gpiolib_seq_priv *priv = s->private;
	struct gpio_device *gdev = v, *next;

	next = list_entry_rcu(gdev->list.next, struct gpio_device, list);
	gdev = &next->list == &gpio_devices ? NULL : next;
	priv->newline = true;
	++*pos;

	return gdev;
}

static void gpiolib_seq_stop(struct seq_file *s, void *v)
{
	struct gpiolib_seq_priv *priv = s->private;

	srcu_read_unlock(&gpio_devices_srcu, priv->idx);
	kfree(priv);
}

static int gpiolib_seq_show(struct seq_file *s, void *v)
{
	struct gpiolib_seq_priv *priv = s->private;
	struct gpio_device *gdev = v;
	struct gpio_chip *gc;
	struct device *parent;

	guard(srcu)(&gdev->srcu);

	gc = srcu_dereference(gdev->chip, &gdev->srcu);
	if (!gc) {
		seq_printf(s, "%s%s: (dangling chip)",
			   priv->newline ? "\n" : "",
			   dev_name(&gdev->dev));
		return 0;
	}

	seq_printf(s, "%s%s: GPIOs %d-%d", priv->newline ? "\n" : "",
		   dev_name(&gdev->dev),
		   gdev->base, gdev->base + gdev->ngpio - 1);
	parent = gc->parent;
	if (parent)
		seq_printf(s, ", parent: %s/%s",
			   parent->bus ? parent->bus->name : "no-bus",
			   dev_name(parent));
	if (gc->label)
		seq_printf(s, ", %s", gc->label);
	if (gc->can_sleep)
		seq_printf(s, ", can sleep");
	seq_printf(s, ":\n");

	if (gc->dbg_show)
		gc->dbg_show(s, gc);
	else
		gpiolib_dbg_show(s, gdev);

	return 0;
}

static const struct seq_operations gpiolib_sops = {
	.start = gpiolib_seq_start,
	.next = gpiolib_seq_next,
	.stop = gpiolib_seq_stop,
	.show = gpiolib_seq_show,
};
DEFINE_SEQ_ATTRIBUTE(gpiolib);

static int __init gpiolib_debugfs_init(void)
{
	/* /sys/kernel/debug/gpio */
	debugfs_create_file("gpio", 0444, NULL, NULL, &gpiolib_fops);
	return 0;
}
subsys_initcall(gpiolib_debugfs_init);

#endif	/* DEBUG_FS */
