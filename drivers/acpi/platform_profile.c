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
	int err, i;

	err = mutex_lock_interruptible(&profile_lock);
	if (err)
		return err;

	if (!cur_profile) {
		mutex_unlock(&profile_lock);
		return -ENODEV;
	}

	for_each_set_bit(i, cur_profile->choices, PLATFORM_PROFILE_LAST) {
		if (len == 0)
			len += sysfs_emit_at(buf, len, "%s", profile_names[i]);
		else
			len += sysfs_emit_at(buf, len, " %s", profile_names[i]);
	}
	len += sysfs_emit_at(buf, len, "\n");
	mutex_unlock(&profile_lock);
	return len;
}

static ssize_t platform_profile_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	enum platform_profile_option profile = PLATFORM_PROFILE_BALANCED;
	int err;

	err = mutex_lock_interruptible(&profile_lock);
	if (err)
		return err;

	if (!cur_profile) {
		mutex_unlock(&profile_lock);
		return -ENODEV;
	}

	err = cur_profile->profile_get(cur_profile, &profile);
	mutex_unlock(&profile_lock);
	if (err)
		return err;

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

	err = mutex_lock_interruptible(&profile_lock);
	if (err)
		return err;

	if (!cur_profile) {
		mutex_unlock(&profile_lock);
		return -ENODEV;
	}

	/* Scan for a matching profile */
	i = sysfs_match_string(profile_names, buf);
	if (i < 0) {
		mutex_unlock(&profile_lock);
		return -EINVAL;
	}

	/* Check that platform supports this profile choice */
	if (!test_bit(i, cur_profile->choices)) {
		mutex_unlock(&profile_lock);
		return -EOPNOTSUPP;
	}

	err = cur_profile->profile_set(cur_profile, i);
	if (!err)
		sysfs_notify(acpi_kobj, NULL, "platform_profile");

	mutex_unlock(&profile_lock);
	if (err)
		return err;
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

void platform_profile_notify(void)
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

	err = mutex_lock_interruptible(&profile_lock);
	if (err)
		return err;

	if (!cur_profile) {
		mutex_unlock(&profile_lock);
		return -ENODEV;
	}

	err = cur_profile->profile_get(cur_profile, &profile);
	if (err) {
		mutex_unlock(&profile_lock);
		return err;
	}

	next = find_next_bit_wrap(cur_profile->choices, PLATFORM_PROFILE_LAST,
				  profile + 1);

	if (WARN_ON(next == PLATFORM_PROFILE_LAST)) {
		mutex_unlock(&profile_lock);
		return -EINVAL;
	}

	err = cur_profile->profile_set(cur_profile, next);
	mutex_unlock(&profile_lock);

	if (!err)
		sysfs_notify(acpi_kobj, NULL, "platform_profile");

	return err;
}
EXPORT_SYMBOL_GPL(platform_profile_cycle);

int platform_profile_register(struct platform_profile_handler *pprof)
{
	int err;

	mutex_lock(&profile_lock);
	/* We can only have one active profile */
	if (cur_profile) {
		mutex_unlock(&profile_lock);
		return -EEXIST;
	}

	/* Sanity check the profile handler field are set */
	if (!pprof || bitmap_empty(pprof->choices, PLATFORM_PROFILE_LAST) ||
		!pprof->profile_set || !pprof->profile_get) {
		mutex_unlock(&profile_lock);
		return -EINVAL;
	}

	err = sysfs_create_group(acpi_kobj, &platform_profile_group);
	if (err) {
		mutex_unlock(&profile_lock);
		return err;
	}

	cur_profile = pprof;
	mutex_unlock(&profile_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(platform_profile_register);

int platform_profile_remove(void)
{
	sysfs_remove_group(acpi_kobj, &platform_profile_group);

	mutex_lock(&profile_lock);
	cur_profile = NULL;
	mutex_unlock(&profile_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(platform_profile_remove);

MODULE_AUTHOR("Mark Pearson <markpearson@lenovo.com>");
MODULE_LICENSE("GPL");
