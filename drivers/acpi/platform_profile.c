// SPDX-License-Identifier: GPL-2.0-or-later

/* Platform profile sysfs interface */

#include <linux/acpi.h>
#include <linux/bits.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/platform_profile.h>
#include <linux/sysfs.h>

#define to_pprof_handler(d)	(container_of(d, struct platform_profile_handler, class_dev))

static DEFINE_MUTEX(profile_lock);

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
	if (!test_bit(*bit, handler->choices))
		return -EOPNOTSUPP;

	return handler->profile_set(dev, *bit);
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
	sysfs_notify(&handler->class_dev.kobj, NULL, "profile");
	kobject_uevent(&handler->class_dev.kobj, KOBJ_CHANGE);

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
	err = handler->profile_get(dev, &val);
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

static const struct class platform_profile_class = {
	.name = "platform-profile",
	.dev_groups = profile_groups,
};

/**
 * _aggregate_choices - Aggregate the available profile choices
 * @dev: The device
 * @data: The available profile choices
 *
 * Return: 0 on success, -errno on failure
 */
static int _aggregate_choices(struct device *dev, void *data)
{
	struct platform_profile_handler *handler;
	unsigned long *aggregate = data;

	lockdep_assert_held(&profile_lock);
	handler = to_pprof_handler(dev);
	if (test_bit(PLATFORM_PROFILE_LAST, aggregate))
		bitmap_copy(aggregate, handler->choices, PLATFORM_PROFILE_LAST);
	else
		bitmap_and(aggregate, handler->choices, aggregate, PLATFORM_PROFILE_LAST);

	return 0;
}

/**
 * platform_profile_choices_show - Show the available profile choices for legacy sysfs interface
 * @dev: The device
 * @attr: The attribute
 * @buf: The buffer to write to
 *
 * Return: The number of bytes written
 */
static ssize_t platform_profile_choices_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	unsigned long aggregate[BITS_TO_LONGS(PLATFORM_PROFILE_LAST)];
	int err;

	set_bit(PLATFORM_PROFILE_LAST, aggregate);
	scoped_cond_guard(mutex_intr, return -ERESTARTSYS, &profile_lock) {
		err = class_for_each_device(&platform_profile_class, NULL,
					    aggregate, _aggregate_choices);
		if (err)
			return err;
	}

	/* no profile handler registered any more */
	if (bitmap_empty(aggregate, PLATFORM_PROFILE_LAST))
		return -EINVAL;

	return _commmon_choices_show(aggregate, buf);
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
 * @dev: The device
 * @attr: The attribute
 * @buf: The buffer to write to
 *
 * Return: The number of bytes written
 */
static ssize_t platform_profile_show(struct device *dev,
				     struct device_attribute *attr,
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
 * @dev: The device
 * @attr: The attribute
 * @buf: The buffer to read from
 * @count: The number of bytes to read
 *
 * Return: The number of bytes read
 */
static ssize_t platform_profile_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	unsigned long choices[BITS_TO_LONGS(PLATFORM_PROFILE_LAST)];
	int ret;
	int i;

	/* Scan for a matching profile */
	i = sysfs_match_string(profile_names, buf);
	if (i < 0 || i == PLATFORM_PROFILE_CUSTOM)
		return -EINVAL;
	set_bit(PLATFORM_PROFILE_LAST, choices);
	scoped_cond_guard(mutex_intr, return -ERESTARTSYS, &profile_lock) {
		ret = class_for_each_device(&platform_profile_class, NULL,
					    choices, _aggregate_choices);
		if (ret)
			return ret;
		if (!test_bit(i, choices))
			return -EOPNOTSUPP;

		ret = class_for_each_device(&platform_profile_class, NULL, &i,
					    _store_and_notify);
		if (ret)
			return ret;
	}

	sysfs_notify(acpi_kobj, NULL, "platform_profile");

	return count;
}

static DEVICE_ATTR_RO(platform_profile_choices);
static DEVICE_ATTR_RW(platform_profile);

static struct attribute *platform_profile_attrs[] = {
	&dev_attr_platform_profile_choices.attr,
	&dev_attr_platform_profile.attr,
	NULL
};

static int profile_class_registered(struct device *dev, const void *data)
{
	return 1;
}

static umode_t profile_class_is_visible(struct kobject *kobj, struct attribute *attr, int idx)
{
	if (!class_find_device(&platform_profile_class, NULL, NULL, profile_class_registered))
		return 0;
	return attr->mode;
}

static const struct attribute_group platform_profile_group = {
	.attrs = platform_profile_attrs,
	.is_visible = profile_class_is_visible,
};

void platform_profile_notify(struct platform_profile_handler *pprof)
{
	scoped_cond_guard(mutex_intr, return, &profile_lock) {
		_notify_class_profile(&pprof->class_dev, NULL);
	}
	sysfs_notify(acpi_kobj, NULL, "platform_profile");
}
EXPORT_SYMBOL_GPL(platform_profile_notify);

int platform_profile_cycle(void)
{
	enum platform_profile_option next = PLATFORM_PROFILE_LAST;
	enum platform_profile_option profile = PLATFORM_PROFILE_LAST;
	unsigned long choices[BITS_TO_LONGS(PLATFORM_PROFILE_LAST)];
	int err;

	set_bit(PLATFORM_PROFILE_LAST, choices);
	scoped_cond_guard(mutex_intr, return -ERESTARTSYS, &profile_lock) {
		err = class_for_each_device(&platform_profile_class, NULL,
					    &profile, _aggregate_profiles);
		if (err)
			return err;

		if (profile == PLATFORM_PROFILE_CUSTOM ||
		    profile == PLATFORM_PROFILE_LAST)
			return -EINVAL;

		err = class_for_each_device(&platform_profile_class, NULL,
					    choices, _aggregate_choices);
		if (err)
			return err;

		/* never iterate into a custom if all drivers supported it */
		clear_bit(PLATFORM_PROFILE_CUSTOM, choices);

		next = find_next_bit_wrap(choices,
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

int platform_profile_register(struct platform_profile_handler *pprof, void *drvdata)
{
	int err;

	/* Sanity check the profile handler */
	if (!pprof || bitmap_empty(pprof->choices, PLATFORM_PROFILE_LAST) ||
	    !pprof->profile_set || !pprof->profile_get) {
		pr_err("platform_profile: handler is invalid\n");
		return -EINVAL;
	}

	guard(mutex)(&profile_lock);

	/* create class interface for individual handler */
	pprof->minor = ida_alloc(&platform_profile_ida, GFP_KERNEL);
	if (pprof->minor < 0)
		return pprof->minor;

	pprof->class_dev.class = &platform_profile_class;
	pprof->class_dev.parent = pprof->dev;
	dev_set_drvdata(&pprof->class_dev, drvdata);
	dev_set_name(&pprof->class_dev, "platform-profile-%d", pprof->minor);
	err = device_register(&pprof->class_dev);
	if (err) {
		put_device(&pprof->class_dev);
		goto cleanup_ida;
	}

	sysfs_notify(acpi_kobj, NULL, "platform_profile");

	err = sysfs_update_group(acpi_kobj, &platform_profile_group);
	if (err)
		goto cleanup_cur;

	return 0;

cleanup_cur:
	device_unregister(&pprof->class_dev);

cleanup_ida:
	ida_free(&platform_profile_ida, pprof->minor);

	return err;
}
EXPORT_SYMBOL_GPL(platform_profile_register);

int platform_profile_remove(struct platform_profile_handler *pprof)
{
	int id;
	guard(mutex)(&profile_lock);

	id = pprof->minor;
	device_unregister(&pprof->class_dev);
	ida_free(&platform_profile_ida, id);

	sysfs_notify(acpi_kobj, NULL, "platform_profile");

	sysfs_update_group(acpi_kobj, &platform_profile_group);

	return 0;
}
EXPORT_SYMBOL_GPL(platform_profile_remove);

static void devm_platform_profile_release(struct device *dev, void *res)
{
	struct platform_profile_handler **pprof = res;

	platform_profile_remove(*pprof);
}

int devm_platform_profile_register(struct platform_profile_handler *pprof, void *drvdata)
{
	struct platform_profile_handler **dr;
	int ret;

	dr = devres_alloc(devm_platform_profile_release, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	ret = platform_profile_register(pprof, drvdata);
	if (ret) {
		devres_free(dr);
		return ret;
	}

	*dr = pprof;
	devres_add(pprof->dev, dr);

	return 0;
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
