// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Linaro Ltd.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/auxiliary_bus.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/fwnode.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/idr.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/overflow.h>
#include <linux/printk.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "gpiolib.h"
#include "gpiolib-shared.h"

/* Represents a single reference to a GPIO pin. */
struct gpio_shared_ref {
	struct list_head list;
	/* Firmware node associated with this GPIO's consumer. */
	struct fwnode_handle *fwnode;
	/* GPIO flags this consumer uses for the request. */
	enum gpiod_flags flags;
	char *con_id;
	int dev_id;
	/* Protects the auxiliary device struct and the lookup table. */
	struct mutex lock;
	struct auxiliary_device adev;
	struct gpiod_lookup_table *lookup;
};

/* Represents a single GPIO pin. */
struct gpio_shared_entry {
	struct list_head list;
	/* Firmware node associated with the GPIO controller. */
	struct fwnode_handle *fwnode;
	/* Hardware offset of the GPIO within its chip. */
	unsigned int offset;
	/* Index in the property value array. */
	size_t index;
	/* Synchronizes the modification of shared_desc. */
	struct mutex lock;
	struct gpio_shared_desc *shared_desc;
	struct kref ref;
	struct list_head refs;
};

static LIST_HEAD(gpio_shared_list);
static DEFINE_IDA(gpio_shared_ida);

#if IS_ENABLED(CONFIG_OF)
static struct gpio_shared_entry *
gpio_shared_find_entry(struct fwnode_handle *controller_node,
		       unsigned int offset)
{
	struct gpio_shared_entry *entry;

	list_for_each_entry(entry, &gpio_shared_list, list) {
		if (entry->fwnode == controller_node && entry->offset == offset)
			return entry;
	}

	return NULL;
}

/* Handle all special nodes that we should ignore. */
static bool gpio_shared_of_node_ignore(struct device_node *node)
{
	/* Ignore disabled devices. */
	if (!of_device_is_available(node))
		return true;

	/*
	 * __symbols__ is a special, internal node and should not be considered
	 * when scanning for shared GPIOs.
	 */
	if (of_node_name_eq(node, "__symbols__"))
		return true;

	/*
	 * GPIO hogs have a "gpios" property which is not a phandle and can't
	 * possibly refer to a shared GPIO.
	 */
	if (of_property_present(node, "gpio-hog"))
		return true;

	return false;
}

static int gpio_shared_of_traverse(struct device_node *curr)
{
	struct gpio_shared_entry *entry;
	size_t con_id_len, suffix_len;
	struct fwnode_handle *fwnode;
	struct of_phandle_args args;
	struct property *prop;
	unsigned int offset;
	const char *suffix;
	int ret, count, i;

	if (gpio_shared_of_node_ignore(curr))
		return 0;

	for_each_property_of_node(curr, prop) {
		/*
		 * The standard name for a GPIO property is "foo-gpios"
		 * or "foo-gpio". Some bindings also use "gpios" or "gpio".
		 * There are some legacy device-trees which have a different
		 * naming convention and for which we have rename quirks in
		 * place in gpiolib-of.c. I don't think any of them require
		 * support for shared GPIOs so for now let's just ignore
		 * them. We can always just export the quirk list and
		 * iterate over it here.
		 */
		if (!strends(prop->name, "-gpios") &&
		    !strends(prop->name, "-gpio") &&
		    strcmp(prop->name, "gpios") != 0 &&
		    strcmp(prop->name, "gpio") != 0)
			continue;

		count = of_count_phandle_with_args(curr, prop->name,
						   "#gpio-cells");
		if (count <= 0)
			continue;

		for (i = 0; i < count; i++) {
			struct device_node *np __free(device_node) = NULL;

			ret = of_parse_phandle_with_args(curr, prop->name,
							 "#gpio-cells", i,
							 &args);
			if (ret)
				continue;

			np = args.np;

			if (!of_property_present(np, "gpio-controller"))
				continue;

			/*
			 * We support 1, 2 and 3 cell GPIO bindings in the
			 * kernel currently. There's only one old MIPS dts that
			 * has a one-cell binding but there's no associated
			 * consumer so it may as well be an error. There don't
			 * seem to be any 3-cell users of non-exclusive GPIOs,
			 * so we can skip this as well. Let's occupy ourselves
			 * with the predominant 2-cell binding with the first
			 * cell indicating the hardware offset of the GPIO and
			 * the second defining the GPIO flags of the request.
			 */
			if (args.args_count != 2)
				continue;

			fwnode = of_fwnode_handle(args.np);
			offset = args.args[0];

			entry = gpio_shared_find_entry(fwnode, offset);
			if (!entry) {
				entry = kzalloc(sizeof(*entry), GFP_KERNEL);
				if (!entry)
					return -ENOMEM;

				entry->fwnode = fwnode_handle_get(fwnode);
				entry->offset = offset;
				entry->index = count;
				INIT_LIST_HEAD(&entry->refs);
				mutex_init(&entry->lock);

				list_add_tail(&entry->list, &gpio_shared_list);
			}

			struct gpio_shared_ref *ref __free(kfree) =
					kzalloc(sizeof(*ref), GFP_KERNEL);
			if (!ref)
				return -ENOMEM;

			ref->fwnode = fwnode_handle_get(of_fwnode_handle(curr));
			ref->flags = args.args[1];
			mutex_init(&ref->lock);

			if (strends(prop->name, "gpios"))
				suffix = "-gpios";
			else if (strends(prop->name, "gpio"))
				suffix = "-gpio";
			else
				suffix = NULL;
			if (!suffix)
				continue;

			/* We only set con_id if there's actually one. */
			if (strcmp(prop->name, "gpios") && strcmp(prop->name, "gpio")) {
				ref->con_id = kstrdup(prop->name, GFP_KERNEL);
				if (!ref->con_id)
					return -ENOMEM;

				con_id_len = strlen(ref->con_id);
				suffix_len = strlen(suffix);

				ref->con_id[con_id_len - suffix_len] = '\0';
			}

			ref->dev_id = ida_alloc(&gpio_shared_ida, GFP_KERNEL);
			if (ref->dev_id < 0) {
				kfree(ref->con_id);
				return -ENOMEM;
			}

			if (!list_empty(&entry->refs))
				pr_debug("GPIO %u at %s is shared by multiple firmware nodes\n",
					 entry->offset, fwnode_get_name(entry->fwnode));

			list_add_tail(&no_free_ptr(ref)->list, &entry->refs);
		}
	}

	for_each_child_of_node_scoped(curr, child) {
		ret = gpio_shared_of_traverse(child);
		if (ret)
			return ret;
	}

	return 0;
}

static int gpio_shared_of_scan(void)
{
	if (of_root)
		return gpio_shared_of_traverse(of_root);

	return 0;
}
#else
static int gpio_shared_of_scan(void)
{
	return 0;
}
#endif /* CONFIG_OF */

static void gpio_shared_adev_release(struct device *dev)
{

}

static int gpio_shared_make_adev(struct gpio_device *gdev,
				 struct gpio_shared_entry *entry,
				 struct gpio_shared_ref *ref)
{
	struct auxiliary_device *adev = &ref->adev;
	int ret;

	guard(mutex)(&ref->lock);

	memset(adev, 0, sizeof(*adev));

	adev->id = ref->dev_id;
	adev->name = "proxy";
	adev->dev.parent = gdev->dev.parent;
	adev->dev.platform_data = entry;
	adev->dev.release = gpio_shared_adev_release;

	ret = auxiliary_device_init(adev);
	if (ret)
		return ret;

	ret = auxiliary_device_add(adev);
	if (ret) {
		auxiliary_device_uninit(adev);
		return ret;
	}

	pr_debug("Created an auxiliary GPIO proxy %s for GPIO device %s\n",
		 dev_name(&adev->dev), gpio_device_get_label(gdev));

	return 0;
}

#if IS_ENABLED(CONFIG_RESET_GPIO)
/*
 * Special case: reset-gpio is an auxiliary device that's created dynamically
 * and put in between the GPIO controller and consumers of shared GPIOs
 * referred to by the "reset-gpios" property.
 *
 * If the supposed consumer of a shared GPIO didn't match any of the mappings
 * we created when scanning the firmware nodes, it's still possible that it's
 * the reset-gpio device which didn't exist at the time of the scan.
 *
 * This function verifies it an return true if it's the case.
 */
static bool gpio_shared_dev_is_reset_gpio(struct device *consumer,
					  struct gpio_shared_entry *entry,
					  struct gpio_shared_ref *ref)
{
	struct fwnode_handle *reset_fwnode = dev_fwnode(consumer);
	struct fwnode_reference_args ref_args, aux_args;
	struct device *parent = consumer->parent;
	bool match;
	int ret;

	/* The reset-gpio device must have a parent AND a firmware node. */
	if (!parent || !reset_fwnode)
		return false;

	/*
	 * FIXME: use device_is_compatible() once the reset-gpio drivers gains
	 * a compatible string which it currently does not have.
	 */
	if (!strstarts(dev_name(consumer), "reset.gpio."))
		return false;

	/*
	 * Parent of the reset-gpio auxiliary device is the GPIO chip whose
	 * fwnode we stored in the entry structure.
	 */
	if (!device_match_fwnode(parent, entry->fwnode))
		return false;

	/*
	 * The device associated with the shared reference's firmware node is
	 * the consumer of the reset control exposed by the reset-gpio device.
	 * It must have a "reset-gpios" property that's referencing the entry's
	 * firmware node.
	 *
	 * The reference args must agree between the real consumer and the
	 * auxiliary reset-gpio device.
	 */
	ret = fwnode_property_get_reference_args(ref->fwnode, "reset-gpios",
						 NULL, 2, 0, &ref_args);
	if (ret)
		return false;

	ret = fwnode_property_get_reference_args(reset_fwnode, "reset-gpios",
						 NULL, 2, 0, &aux_args);
	if (ret) {
		fwnode_handle_put(ref_args.fwnode);
		return false;
	}

	match = ((ref_args.fwnode == entry->fwnode) &&
		 (aux_args.fwnode == entry->fwnode) &&
		 (ref_args.args[0] == aux_args.args[0]));

	fwnode_handle_put(ref_args.fwnode);
	fwnode_handle_put(aux_args.fwnode);
	return match;
}
#else
static bool gpio_shared_dev_is_reset_gpio(struct device *consumer,
					  struct gpio_shared_entry *entry,
					  struct gpio_shared_ref *ref)
{
	return false;
}
#endif /* CONFIG_RESET_GPIO */

int gpio_shared_add_proxy_lookup(struct device *consumer, unsigned long lflags)
{
	const char *dev_id = dev_name(consumer);
	struct gpio_shared_entry *entry;
	struct gpio_shared_ref *ref;

	struct gpiod_lookup_table *lookup __free(kfree) =
			kzalloc(struct_size(lookup, table, 2), GFP_KERNEL);
	if (!lookup)
		return -ENOMEM;

	list_for_each_entry(entry, &gpio_shared_list, list) {
		list_for_each_entry(ref, &entry->refs, list) {
			if (!device_match_fwnode(consumer, ref->fwnode) &&
			    !gpio_shared_dev_is_reset_gpio(consumer, entry, ref))
				continue;

			guard(mutex)(&ref->lock);

			/* We've already done that on a previous request. */
			if (ref->lookup)
				return 0;

			char *key __free(kfree) =
				kasprintf(GFP_KERNEL,
					  KBUILD_MODNAME ".proxy.%u",
					  ref->adev.id);
			if (!key)
				return -ENOMEM;

			pr_debug("Adding machine lookup entry for a shared GPIO for consumer %s, with key '%s' and con_id '%s'\n",
				 dev_id, key, ref->con_id ?: "none");

			lookup->dev_id = dev_id;
			lookup->table[0] = GPIO_LOOKUP(no_free_ptr(key), 0,
						       ref->con_id, lflags);

			ref->lookup = no_free_ptr(lookup);
			gpiod_add_lookup_table(ref->lookup);

			return 0;
		}
	}

	/* We warn here because this can only happen if the programmer borked. */
	WARN_ON(1);
	return -ENOENT;
}

static void gpio_shared_remove_adev(struct auxiliary_device *adev)
{
	auxiliary_device_delete(adev);
	auxiliary_device_uninit(adev);
}

int gpio_device_setup_shared(struct gpio_device *gdev)
{
	struct gpio_shared_entry *entry;
	struct gpio_shared_ref *ref;
	unsigned long *flags;
	int ret;

	list_for_each_entry(entry, &gpio_shared_list, list) {
		list_for_each_entry(ref, &entry->refs, list) {
			if (gdev->dev.parent == &ref->adev.dev) {
				/*
				 * This is a shared GPIO proxy. Mark its
				 * descriptor as such and return here.
				 */
				__set_bit(GPIOD_FLAG_SHARED_PROXY,
					  &gdev->descs[0].flags);
				return 0;
			}
		}
	}

	/*
	 * This is not a shared GPIO proxy but it still may be the device
	 * exposing shared pins. Find them and create the proxy devices.
	 */
	list_for_each_entry(entry, &gpio_shared_list, list) {
		if (!device_match_fwnode(&gdev->dev, entry->fwnode))
			continue;

		if (list_count_nodes(&entry->refs) <= 1)
			continue;

		flags = &gdev->descs[entry->offset].flags;

		__set_bit(GPIOD_FLAG_SHARED, flags);
		/*
		 * Shared GPIOs are not requested via the normal path. Make
		 * them inaccessible to anyone even before we register the
		 * chip.
		 */
		__set_bit(GPIOD_FLAG_REQUESTED, flags);

		pr_debug("GPIO %u owned by %s is shared by multiple consumers\n",
			 entry->offset, gpio_device_get_label(gdev));

		list_for_each_entry(ref, &entry->refs, list) {
			pr_debug("Setting up a shared GPIO entry for %s\n",
				 fwnode_get_name(ref->fwnode));

			ret = gpio_shared_make_adev(gdev, entry, ref);
			if (ret)
				return ret;
		}
	}

	return 0;
}

void gpio_device_teardown_shared(struct gpio_device *gdev)
{
	struct gpio_shared_entry *entry;
	struct gpio_shared_ref *ref;

	list_for_each_entry(entry, &gpio_shared_list, list) {
		if (!device_match_fwnode(&gdev->dev, entry->fwnode))
			continue;

		/*
		 * For some reason if we call synchronize_srcu() in GPIO core,
		 * descent here and take this mutex and then recursively call
		 * synchronize_srcu() again from gpiochip_remove() (which is
		 * totally fine) called after gpio_shared_remove_adev(),
		 * lockdep prints a false positive deadlock splat. Disable
		 * lockdep here.
		 */
		lockdep_off();
		list_for_each_entry(ref, &entry->refs, list) {
			guard(mutex)(&ref->lock);

			if (ref->lookup) {
				gpiod_remove_lookup_table(ref->lookup);
				kfree(ref->lookup->table[0].key);
				kfree(ref->lookup);
				ref->lookup = NULL;
			}

			gpio_shared_remove_adev(&ref->adev);
		}
		lockdep_on();
	}
}

static void gpio_shared_release(struct kref *kref)
{
	struct gpio_shared_entry *entry =
		container_of(kref, struct gpio_shared_entry, ref);
	struct gpio_shared_desc *shared_desc;

	guard(mutex)(&entry->lock);

	shared_desc = entry->shared_desc;
	gpio_device_put(shared_desc->desc->gdev);
	if (shared_desc->can_sleep)
		mutex_destroy(&shared_desc->mutex);
	kfree(shared_desc);
	entry->shared_desc = NULL;
}

static void gpiod_shared_put(void *data)
{
	struct gpio_shared_entry *entry = data;

	kref_put(&entry->ref, gpio_shared_release);
}

static struct gpio_shared_desc *
gpiod_shared_desc_create(struct gpio_shared_entry *entry)
{
	struct gpio_shared_desc *shared_desc;
	struct gpio_device *gdev;

	lockdep_assert_held(&entry->lock);

	shared_desc = kzalloc(sizeof(*shared_desc), GFP_KERNEL);
	if (!shared_desc)
		return ERR_PTR(-ENOMEM);

	gdev = gpio_device_find_by_fwnode(entry->fwnode);
	if (!gdev) {
		kfree(shared_desc);
		return ERR_PTR(-EPROBE_DEFER);
	}

	shared_desc->desc = &gdev->descs[entry->offset];
	shared_desc->can_sleep = gpiod_cansleep(shared_desc->desc);
	if (shared_desc->can_sleep)
		mutex_init(&shared_desc->mutex);
	else
		spin_lock_init(&shared_desc->spinlock);

	return shared_desc;
}

struct gpio_shared_desc *devm_gpiod_shared_get(struct device *dev)
{
	struct gpio_shared_desc *shared_desc;
	struct gpio_shared_entry *entry;
	int ret;

	entry = dev_get_platdata(dev);
	if (WARN_ON(!entry))
		/* Programmer bug */
		return ERR_PTR(-ENOENT);

	scoped_guard(mutex, &entry->lock) {
		if (entry->shared_desc) {
			kref_get(&entry->ref);
			shared_desc = entry->shared_desc;
		} else {
			shared_desc = gpiod_shared_desc_create(entry);
			if (IS_ERR(shared_desc))
				return ERR_CAST(shared_desc);

			kref_init(&entry->ref);
			entry->shared_desc = shared_desc;
		}

		pr_debug("Device %s acquired a reference to the shared GPIO %u owned by %s\n",
			 dev_name(dev), gpiod_hwgpio(shared_desc->desc),
			 gpio_device_get_label(shared_desc->desc->gdev));
	}

	ret = devm_add_action_or_reset(dev, gpiod_shared_put, entry);
	if (ret)
		return ERR_PTR(ret);

	return shared_desc;
}
EXPORT_SYMBOL_GPL(devm_gpiod_shared_get);

static void gpio_shared_drop_ref(struct gpio_shared_ref *ref)
{
	list_del(&ref->list);
	mutex_destroy(&ref->lock);
	kfree(ref->con_id);
	ida_free(&gpio_shared_ida, ref->dev_id);
	fwnode_handle_put(ref->fwnode);
	kfree(ref);
}

static void gpio_shared_drop_entry(struct gpio_shared_entry *entry)
{
	list_del(&entry->list);
	mutex_destroy(&entry->lock);
	fwnode_handle_put(entry->fwnode);
	kfree(entry);
}

/*
 * This is only called if gpio_shared_init() fails so it's in fact __init and
 * not __exit.
 */
static void __init gpio_shared_teardown(void)
{
	struct gpio_shared_entry *entry, *epos;
	struct gpio_shared_ref *ref, *rpos;

	list_for_each_entry_safe(entry, epos, &gpio_shared_list, list) {
		list_for_each_entry_safe(ref, rpos, &entry->refs, list)
			gpio_shared_drop_ref(ref);

		gpio_shared_drop_entry(entry);
	}
}

static void gpio_shared_free_exclusive(void)
{
	struct gpio_shared_entry *entry, *epos;

	list_for_each_entry_safe(entry, epos, &gpio_shared_list, list) {
		if (list_count_nodes(&entry->refs) > 1)
			continue;

		gpio_shared_drop_ref(list_first_entry(&entry->refs,
						      struct gpio_shared_ref,
						      list));
		gpio_shared_drop_entry(entry);
	}
}

static int __init gpio_shared_init(void)
{
	int ret;

	/* Right now, we only support OF-based systems. */
	ret = gpio_shared_of_scan();
	if (ret) {
		gpio_shared_teardown();
		pr_err("Failed to scan OF nodes for shared GPIOs: %d\n", ret);
		return ret;
	}

	gpio_shared_free_exclusive();

	pr_debug("Finished scanning firmware nodes for shared GPIOs\n");
	return 0;
}
postcore_initcall(gpio_shared_init);
