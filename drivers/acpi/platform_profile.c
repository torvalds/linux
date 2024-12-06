// SPDX-License-Identifier: GPL-2.0-or-later

/* Platform profile sysfs interface */

#include <linux/acpi.h>
#include <linux/bits.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/platform_profile.h>
#include <linux/sysfs.h>

static struct platform_profile_handler *cur_profile;
static DEFINE_MUTEX(profile_lock);

static const char * const profile_names[] = {
	[PLATFORM_PROFILE_LOW_POWER] = "low-power",
	[PLATFORM_PROFILE_COOL] = "cool",
	[PLATFORM_PROFILE_QUIET] = "quiet",
	[PLATFORM_PROFILE_BALANCED] = "balanced",
	[PLATFORM_PROFILE_BALANCED_PERFORMANCE] = "balanced-performance",
	[PLATFORM_PROFILE_PERFORMANCE] = "performance",
};
static_assert(ARRAY_SIZE(profile_names) == PLATFORM_PROFILE_LAST);

static ssize_t platform_profile_choices_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int len = 0;
	int i;

	scoped_cond_guard(mutex_intr, return -ERESTARTSYS, &profile_lock) {
		if (!cur_profile)
			return -ENODEV;
		for_each_set_bit(i, cur_profile->choices, PLATFORM_PROFILE_LAST)
			len += sysfs_emit_at(buf, len, len ? " %s": "%s", profile_names[i]);
	}
	len += sysfs_emit_at(buf, len, "\n");

	return len;
}

static ssize_t platform_profile_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	enum platform_profile_option profile = PLATFORM_PROFILE_BALANCED;
	int err;

	scoped_cond_guard(mutex_intr, return -ERESTARTSYS, &profile_lock) {
		if (!cur_profile)
			return -ENODEV;

		err = cur_profile->profile_get(cur_profile, &profile);
		if (err)
			return err;
	}

	/* Check that profile is valid index */
	if (WARN_ON((profile < 0) || (profile >= ARRAY_SIZE(profile_names))))
		return -EIO;

	return sysfs_emit(buf, "%s\n", profile_names[profile]);
}

static ssize_t platform_profile_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	int err, i;

	/* Scan for a matching profile */
	i = sysfs_match_string(profile_names, buf);
	if (i < 0)
		return -EINVAL;

	scoped_cond_guard(mutex_intr, return -ERESTARTSYS, &profile_lock) {
		if (!cur_profile)
			return -ENODEV;

		/* Check that platform supports this profile choice */
		if (!test_bit(i, cur_profile->choices))
			return -EOPNOTSUPP;

		err = cur_profile->profile_set(cur_profile, i);
		if (err)
			return err;
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

static const struct attribute_group platform_profile_group = {
	.attrs = platform_profile_attrs
};

void platform_profile_notify(struct platform_profile_handler *pprof)
{
	if (!cur_profile)
		return;
	sysfs_notify(acpi_kobj, NULL, "platform_profile");
}
EXPORT_SYMBOL_GPL(platform_profile_notify);

int platform_profile_cycle(void)
{
	enum platform_profile_option profile;
	enum platform_profile_option next;
	int err;

	scoped_cond_guard(mutex_intr, return -ERESTARTSYS, &profile_lock) {
		if (!cur_profile)
			return -ENODEV;

		err = cur_profile->profile_get(cur_profile, &profile);
		if (err)
			return err;

		next = find_next_bit_wrap(cur_profile->choices, PLATFORM_PROFILE_LAST,
					  profile + 1);

		if (WARN_ON(next == PLATFORM_PROFILE_LAST))
			return -EINVAL;

		err = cur_profile->profile_set(cur_profile, next);
		if (err)
			return err;
	}

	sysfs_notify(acpi_kobj, NULL, "platform_profile");

	return 0;
}
EXPORT_SYMBOL_GPL(platform_profile_cycle);

int platform_profile_register(struct platform_profile_handler *pprof)
{
	int err;

	/* Sanity check the profile handler */
	if (!pprof || bitmap_empty(pprof->choices, PLATFORM_PROFILE_LAST) ||
	    !pprof->profile_set || !pprof->profile_get) {
		pr_err("platform_profile: handler is invalid\n");
		return -EINVAL;
	}

	guard(mutex)(&profile_lock);
	/* We can only have one active profile */
	if (cur_profile)
		return -EEXIST;

	err = sysfs_create_group(acpi_kobj, &platform_profile_group);
	if (err)
		return err;

	cur_profile = pprof;
	return 0;
}
EXPORT_SYMBOL_GPL(platform_profile_register);

int platform_profile_remove(struct platform_profile_handler *pprof)
{
	guard(mutex)(&profile_lock);

	sysfs_remove_group(acpi_kobj, &platform_profile_group);
	cur_profile = NULL;
	return 0;
}
EXPORT_SYMBOL_GPL(platform_profile_remove);

MODULE_AUTHOR("Mark Pearson <markpearson@lenovo.com>");
MODULE_DESCRIPTION("ACPI platform profile sysfs interface");
MODULE_LICENSE("GPL");
