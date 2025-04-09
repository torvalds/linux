// SPDX-License-Identifier: GPL-2.0-or-later

/* Platform profile sysfs interface */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/platform_profile.h>
#include <linux/sysfs.h>

#define to_pprof_handler(d)	(container_of(d, struct platform_profile_handler, dev))

static DEFINE_MUTEX(profile_lock);

struct platform_profile_handler {
	const char *name;
	struct device dev;
	int minor;
	unsigned long choices[BITS_TO_LONGS(PLATFORM_PROFILE_LAST)];
	unsigned long hidden_choices[BITS_TO_LONGS(PLATFORM_PROFILE_LAST)];
	const struct platform_profile_ops *ops;
};

struct aggregate_choices_data {
	unsigned long aggregate[BITS_TO_LONGS(PLATFORM_PROFILE_LAST)];
	int count;
};

static const char * const profile_names[] = {
	[PLATFORM_PROFILE_LOW_POWER] = "low-power",
	[PLATFORM_PROFILE_COOL] = "cool",
	[PLATFORM_PROFILE_QUIET] = "quiet",
	[PLATFORM_PROFILE_BALANCED] = "balanced",
	[PLATFORM_PROFILE_BALANCED_PERFORMANCE] = "balanced-performance",
	[PLATFORM_PROFILE_PERFORMANCE] = "performance",
	[PLATFORM_PROFILE_CUSTOM] = "custom",
};
static_assert(ARRAY_SIZE(profile_names) == PLATFORM_PROFILE_LAST);

static DEFINE_IDA(platform_profile_ida);

/**
 * _commmon_choices_show - Show the available profile choices
 * @choices: The available profile choices
 * @buf: The buffer to write to
 *
 * Return: The number of bytes written
 */
static ssize_t _commmon_choices_show(unsigned long *choices, char *buf)
{
	int i, len = 0;

	for_each_set_bit(i, choices, PLATFORM_PROFILE_LAST) {
		if (len == 0)
			len += sysfs_emit_at(buf, len, "%s", profile_names[i]);
		else
			len += sysfs_emit_at(buf, len, " %s", profile_names[i]);
	}
	len += sysfs_emit_at(buf, len, "\n");

	return len;
}

/**
 * _store_class_profile - Set the profile for a class device
 * @dev: The class device
 * @data: The profile to set
 *
 * Return: 0 on success, -errno on failure
 */
static int _store_class_profile(struct device *dev, void *data)
{
	struct platform_profile_handler *handler;
	int *bit = (int *)data;

	lockdep_assert_held(&profile_lock);
	handler = to_pprof_handler(dev);
	if (!test_bit(*bit, handler->choices) && !test_bit(*bit, handler->hidden_choices))
		return -EOPNOTSUPP;

	return handler->ops->profile_set(dev, *bit);
}

/**
 * _notify_class_profile - Notify the class device of a profile change
 * @dev: The class device
 * @data: Unused
 *
 * Return: 0 on success, -errno on failure
 */
static int _notify_class_profile(struct device *dev, void *data)
{
	struct platform_profile_handler *handler = to_pprof_handler(dev);

	lockdep_assert_held(&profile_lock);
	sysfs_notify(&handler->dev.kobj, NULL, "profile");
	kobject_uevent(&handler->dev.kobj, KOBJ_CHANGE);

	return 0;
}

/**
 * get_class_profile - Show the current profile for a class device
 * @dev: The class device
 * @profile: The profile to return
 *
 * Return: 0 on success, -errno on failure
 */
static int get_class_profile(struct device *dev,
			     enum platform_profile_option *profile)
{
	struct platform_profile_handler *handler;
	enum platform_profile_option val;
	int err;

	lockdep_assert_held(&profile_lock);
	handler = to_pprof_handler(dev);
	err = handler->ops->profile_get(dev, &val);
	if (err) {
		pr_err("Failed to get profile for handler %s\n", handler->name);
		return err;
	}

	if (WARN_ON(val >= PLATFORM_PROFILE_LAST))
		return -EINVAL;
	*profile = val;

	return 0;
}

/**
 * name_show - Show the name of the profile handler
 * @dev: The device
 * @attr: The attribute
 * @buf: The buffer to write to
 *
 * Return: The number of bytes written
 */
static ssize_t name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_profile_handler *handler = to_pprof_handler(dev);

	return sysfs_emit(buf, "%s\n", handler->name);
}
static DEVICE_ATTR_RO(name);

/**
 * choices_show - Show the available profile choices
 * @dev: The device
 * @attr: The attribute
 * @buf: The buffer to write to
 *
 * Return: The number of bytes written
 */
static ssize_t choices_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct platform_profile_handler *handler = to_pprof_handler(dev);

	return _commmon_choices_show(handler->choices, buf);
}
static DEVICE_ATTR_RO(choices);

/**
 * profile_show - Show the current profile for a class device
 * @dev: The device
 * @attr: The attribute
 * @buf: The buffer to write to
 *
 * Return: The number of bytes written
 */
static ssize_t profile_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	enum platform_profile_option profile = PLATFORM_PROFILE_LAST;
	int err;

	scoped_cond_guard(mutex_intr, return -ERESTARTSYS, &profile_lock) {
		err = get_class_profile(dev, &profile);
		if (err)
			return err;
	}

	return sysfs_emit(buf, "%s\n", profile_names[profile]);
}

/**
 * profile_store - Set the profile for a class device
 * @dev: The device
 * @attr: The attribute
 * @buf: The buffer to read from
 * @count: The number of bytes to read
 *
 * Return: The number of bytes read
 */
static ssize_t profile_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	int index, ret;

	index = sysfs_match_string(profile_names, buf);
	if (index < 0)
		return -EINVAL;

	scoped_cond_guard(mutex_intr, return -ERESTARTSYS, &profile_lock) {
		ret = _store_class_profile(dev, &index);
		if (ret)
			return ret;
	}

	sysfs_notify(acpi_kobj, NULL, "platform_profile");

	return count;
}
static DEVICE_ATTR_RW(profile);

static struct attribute *profile_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_choices.attr,
	&dev_attr_profile.attr,
	NULL
};
ATTRIBUTE_GROUPS(profile);

static void pprof_device_release(struct device *dev)
{
	struct platform_profile_handler *pprof = to_pprof_handler(dev);

	kfree(pprof);
}

static const struct class platform_profile_class = {
	.name = "platform-profile",
	.dev_groups = profile_groups,
	.dev_release = pprof_device_release,
};

/**
 * _aggregate_choices - Aggregate the available profile choices
 * @dev: The device
 * @arg: struct aggregate_choices_data, with it's aggregate member bitmap
 *	 initially filled with ones
 *
 * Return: 0 on success, -errno on failure
 */
static int _aggregate_choices(struct device *dev, void *arg)
{
	unsigned long tmp[BITS_TO_LONGS(PLATFORM_PROFILE_LAST)];
	struct aggregate_choices_data *data = arg;
	struct platform_profile_handler *handler;

	lockdep_assert_held(&profile_lock);

	handler = to_pprof_handler(dev);
	bitmap_or(tmp, handler->choices, handler->hidden_choices, PLATFORM_PROFILE_LAST);
	bitmap_and(data->aggregate, tmp, data->aggregate, PLATFORM_PROFILE_LAST);
	data->count++;

	return 0;
}

/**
 * _remove_hidden_choices - Remove hidden choices from aggregate data
 * @dev: The device
 * @arg: struct aggregate_choices_data
 *
 * Return: 0 on success, -errno on failure
 */
static int _remove_hidden_choices(struct device *dev, void *arg)
{
	struct aggregate_choices_data *data = arg;
	struct platform_profile_handler *handler;

	lockdep_assert_held(&profile_lock);
	handler = to_pprof_handler(dev);
	bitmap_andnot(data->aggregate, handler->choices,
		      handler->hidden_choices, PLATFORM_PROFILE_LAST);

	return 0;
}

/**
 * platform_profile_choices_show - Show the available profile choices for legacy sysfs interface
 * @kobj: The kobject
 * @attr: The attribute
 * @buf: The buffer to write to
 *
 * Return: The number of bytes written
 */
static ssize_t platform_profile_choices_show(struct kobject *kobj,
					     struct kobj_attribute *attr,
					     char *buf)
{
	struct aggregate_choices_data data = {
		.aggregate = { [0 ... BITS_TO_LONGS(PLATFORM_PROFILE_LAST) - 1] = ~0UL },
		.count = 0,
	};
	int err;

	scoped_cond_guard(mutex_intr, return -ERESTARTSYS, &profile_lock) {
		err = class_for_each_device(&platform_profile_class, NULL,
					    &data, _aggregate_choices);
		if (err)
			return err;
		if (data.count == 1) {
			err = class_for_each_device(&platform_profile_class, NULL,
						    &data, _remove_hidden_choices);
			if (err)
				return err;
		}
	}

	/* no profile handler registered any more */
	if (bitmap_empty(data.aggregate, PLATFORM_PROFILE_LAST))
		return -EINVAL;

	return _commmon_choices_show(data.aggregate, buf);
}

/**
 * _aggregate_profiles - Aggregate the profiles for legacy sysfs interface
 * @dev: The device
 * @data: The profile to return
 *
 * Return: 0 on success, -errno on failure
 */
static int _aggregate_profiles(struct device *dev, void *data)
{
	enum platform_profile_option *profile = data;
	enum platform_profile_option val;
	int err;

	err = get_class_profile(dev, &val);
	if (err)
		return err;

	if (*profile != PLATFORM_PROFILE_LAST && *profile != val)
		*profile = PLATFORM_PROFILE_CUSTOM;
	else
		*profile = val;

	return 0;
}

/**
 * _store_and_notify - Store and notify a class from legacy sysfs interface
 * @dev: The device
 * @data: The profile to return
 *
 * Return: 0 on success, -errno on failure
 */
static int _store_and_notify(struct device *dev, void *data)
{
	enum platform_profile_option *profile = data;
	int err;

	err = _store_class_profile(dev, profile);
	if (err)
		return err;
	return _notify_class_profile(dev, NULL);
}

/**
 * platform_profile_show - Show the current profile for legacy sysfs interface
 * @kobj: The kobject
 * @attr: The attribute
 * @buf: The buffer to write to
 *
 * Return: The number of bytes written
 */
static ssize_t platform_profile_show(struct kobject *kobj,
				     struct kobj_attribute *attr,
				     char *buf)
{
	enum platform_profile_option profile = PLATFORM_PROFILE_LAST;
	int err;

	scoped_cond_guard(mutex_intr, return -ERESTARTSYS, &profile_lock) {
		err = class_for_each_device(&platform_profile_class, NULL,
					    &profile, _aggregate_profiles);
		if (err)
			return err;
	}

	/* no profile handler registered any more */
	if (profile == PLATFORM_PROFILE_LAST)
		return -EINVAL;

	return sysfs_emit(buf, "%s\n", profile_names[profile]);
}

/**
 * platform_profile_store - Set the profile for legacy sysfs interface
 * @kobj: The kobject
 * @attr: The attribute
 * @buf: The buffer to read from
 * @count: The number of bytes to read
 *
 * Return: The number of bytes read
 */
static ssize_t platform_profile_store(struct kobject *kobj,
				      struct kobj_attribute *attr,
				      const char *buf, size_t count)
{
	struct aggregate_choices_data data = {
		.aggregate = { [0 ... BITS_TO_LONGS(PLATFORM_PROFILE_LAST) - 1] = ~0UL },
		.count = 0,
	};
	int ret;
	int i;

	/* Scan for a matching profile */
	i = sysfs_match_string(profile_names, buf);
	if (i < 0 || i == PLATFORM_PROFILE_CUSTOM)
		return -EINVAL;

	scoped_cond_guard(mutex_intr, return -ERESTARTSYS, &profile_lock) {
		ret = class_for_each_device(&platform_profile_class, NULL,
					    &data, _aggregate_choices);
		if (ret)
			return ret;
		if (!test_bit(i, data.aggregate))
			return -EOPNOTSUPP;

		ret = class_for_each_device(&platform_profile_class, NULL, &i,
					    _store_and_notify);
		if (ret)
			return ret;
	}

	sysfs_notify(acpi_kobj, NULL, "platform_profile");

	return count;
}

static struct kobj_attribute attr_platform_profile_choices = __ATTR_RO(platform_profile_choices);
static struct kobj_attribute attr_platform_profile = __ATTR_RW(platform_profile);

static struct attribute *platform_profile_attrs[] = {
	&attr_platform_profile_choices.attr,
	&attr_platform_profile.attr,
	NULL
};

static int profile_class_registered(struct device *dev, const void *data)
{
	return 1;
}

static umode_t profile_class_is_visible(struct kobject *kobj, struct attribute *attr, int idx)
{
	struct device *dev;

	dev = class_find_device(&platform_profile_class, NULL, NULL, profile_class_registered);
	if (!dev)
		return 0;

	put_device(dev);

	return attr->mode;
}

static const struct attribute_group platform_profile_group = {
	.attrs = platform_profile_attrs,
	.is_visible = profile_class_is_visible,
};

/**
 * platform_profile_notify - Notify class device and legacy sysfs interface
 * @dev: The class device
 */
void platform_profile_notify(struct device *dev)
{
	scoped_cond_guard(mutex_intr, return, &profile_lock) {
		_notify_class_profile(dev, NULL);
	}
	sysfs_notify(acpi_kobj, NULL, "platform_profile");
}
EXPORT_SYMBOL_GPL(platform_profile_notify);

/**
 * platform_profile_cycle - Cycles profiles available on all registered class devices
 *
 * Return: 0 on success, -errno on failure
 */
int platform_profile_cycle(void)
{
	struct aggregate_choices_data data = {
		.aggregate = { [0 ... BITS_TO_LONGS(PLATFORM_PROFILE_LAST) - 1] = ~0UL },
		.count = 0,
	};
	enum platform_profile_option next = PLATFORM_PROFILE_LAST;
	enum platform_profile_option profile = PLATFORM_PROFILE_LAST;
	int err;

	scoped_cond_guard(mutex_intr, return -ERESTARTSYS, &profile_lock) {
		err = class_for_each_device(&platform_profile_class, NULL,
					    &profile, _aggregate_profiles);
		if (err)
			return err;

		if (profile == PLATFORM_PROFILE_CUSTOM ||
		    profile == PLATFORM_PROFILE_LAST)
			return -EINVAL;

		err = class_for_each_device(&platform_profile_class, NULL,
					    &data, _aggregate_choices);
		if (err)
			return err;

		/* never iterate into a custom if all drivers supported it */
		clear_bit(PLATFORM_PROFILE_CUSTOM, data.aggregate);

		next = find_next_bit_wrap(data.aggregate,
					  PLATFORM_PROFILE_LAST,
					  profile + 1);

		err = class_for_each_device(&platform_profile_class, NULL, &next,
					    _store_and_notify);

		if (err)
			return err;
	}

	sysfs_notify(acpi_kobj, NULL, "platform_profile");

	return 0;
}
EXPORT_SYMBOL_GPL(platform_profile_cycle);

/**
 * platform_profile_register - Creates and registers a platform profile class device
 * @dev: Parent device
 * @name: Name of the class device
 * @drvdata: Driver data that will be attached to the class device
 * @ops: Platform profile's mandatory operations
 *
 * Return: pointer to the new class device on success, ERR_PTR on failure
 */
struct device *platform_profile_register(struct device *dev, const char *name,
					 void *drvdata,
					 const struct platform_profile_ops *ops)
{
	struct device *ppdev;
	int minor;
	int err;

	/* Sanity check */
	if (WARN_ON_ONCE(!dev || !name || !ops || !ops->profile_get ||
	    !ops->profile_set || !ops->probe))
		return ERR_PTR(-EINVAL);

	struct platform_profile_handler *pprof __free(kfree) = kzalloc(
		sizeof(*pprof), GFP_KERNEL);
	if (!pprof)
		return ERR_PTR(-ENOMEM);

	err = ops->probe(drvdata, pprof->choices);
	if (err) {
		dev_err(dev, "platform_profile probe failed\n");
		return ERR_PTR(err);
	}

	if (bitmap_empty(pprof->choices, PLATFORM_PROFILE_LAST)) {
		dev_err(dev, "Failed to register platform_profile class device with empty choices\n");
		return ERR_PTR(-EINVAL);
	}

	if (ops->hidden_choices) {
		err = ops->hidden_choices(drvdata, pprof->hidden_choices);
		if (err) {
			dev_err(dev, "platform_profile hidden_choices failed\n");
			return ERR_PTR(err);
		}
	}

	guard(mutex)(&profile_lock);

	/* create class interface for individual handler */
	minor = ida_alloc(&platform_profile_ida, GFP_KERNEL);
	if (minor < 0)
		return ERR_PTR(minor);

	pprof->name = name;
	pprof->ops = ops;
	pprof->minor = minor;
	pprof->dev.class = &platform_profile_class;
	pprof->dev.parent = dev;
	dev_set_drvdata(&pprof->dev, drvdata);
	dev_set_name(&pprof->dev, "platform-profile-%d", pprof->minor);
	/* device_register() takes ownership of pprof/ppdev */
	ppdev = &no_free_ptr(pprof)->dev;
	err = device_register(ppdev);
	if (err) {
		put_device(ppdev);
		goto cleanup_ida;
	}

	sysfs_notify(acpi_kobj, NULL, "platform_profile");

	err = sysfs_update_group(acpi_kobj, &platform_profile_group);
	if (err)
		goto cleanup_cur;

	return ppdev;

cleanup_cur:
	device_unregister(ppdev);

cleanup_ida:
	ida_free(&platform_profile_ida, minor);

	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(platform_profile_register);

/**
 * platform_profile_remove - Unregisters a platform profile class device
 * @dev: Class device
 */
void platform_profile_remove(struct device *dev)
{
	struct platform_profile_handler *pprof;

	if (IS_ERR_OR_NULL(dev))
		return;

	pprof = to_pprof_handler(dev);

	guard(mutex)(&profile_lock);

	ida_free(&platform_profile_ida, pprof->minor);
	device_unregister(&pprof->dev);

	sysfs_notify(acpi_kobj, NULL, "platform_profile");
	sysfs_update_group(acpi_kobj, &platform_profile_group);
}
EXPORT_SYMBOL_GPL(platform_profile_remove);

static void devm_platform_profile_release(struct device *dev, void *res)
{
	struct device **ppdev = res;

	platform_profile_remove(*ppdev);
}

/**
 * devm_platform_profile_register - Device managed version of platform_profile_register
 * @dev: Parent device
 * @name: Name of the class device
 * @drvdata: Driver data that will be attached to the class device
 * @ops: Platform profile's mandatory operations
 *
 * Return: pointer to the new class device on success, ERR_PTR on failure
 */
struct device *devm_platform_profile_register(struct device *dev, const char *name,
					      void *drvdata,
					      const struct platform_profile_ops *ops)
{
	struct device *ppdev;
	struct device **dr;

	dr = devres_alloc(devm_platform_profile_release, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return ERR_PTR(-ENOMEM);

	ppdev = platform_profile_register(dev, name, drvdata, ops);
	if (IS_ERR(ppdev)) {
		devres_free(dr);
		return ppdev;
	}

	*dr = ppdev;
	devres_add(dev, dr);

	return ppdev;
}
EXPORT_SYMBOL_GPL(devm_platform_profile_register);

static int __init platform_profile_init(void)
{
	int err;

	err = class_register(&platform_profile_class);
	if (err)
		return err;

	err = sysfs_create_group(acpi_kobj, &platform_profile_group);
	if (err)
		class_unregister(&platform_profile_class);

	return err;
}

static void __exit platform_profile_exit(void)
{
	sysfs_remove_group(acpi_kobj, &platform_profile_group);
	class_unregister(&platform_profile_class);
}
module_init(platform_profile_init);
module_exit(platform_profile_exit);

MODULE_AUTHOR("Mark Pearson <markpearson@lenovo.com>");
MODULE_DESCRIPTION("ACPI platform profile sysfs interface");
MODULE_LICENSE("GPL");
