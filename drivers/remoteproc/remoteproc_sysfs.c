// SPDX-License-Identifier: GPL-2.0-only
/*
 * Remote Processor Framework
 */

#include <linux/remoteproc.h>
#include <linux/slab.h>

#include "remoteproc_internal.h"

#define to_rproc(d) container_of(d, struct rproc, dev)

static ssize_t recovery_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct rproc *rproc = to_rproc(dev);

	return sysfs_emit(buf, "%s", rproc->recovery_disabled ? "disabled\n" : "enabled\n");
}

/*
 * By writing to the 'recovery' sysfs entry, we control the behavior of the
 * recovery mechanism dynamically. The default value of this entry is "enabled".
 *
 * The 'recovery' sysfs entry supports these commands:
 *
 * enabled:	When enabled, the remote processor will be automatically
 *		recovered whenever it crashes. Moreover, if the remote
 *		processor crashes while recovery is disabled, it will
 *		be automatically recovered too as soon as recovery is enabled.
 *
 * disabled:	When disabled, a remote processor will remain in a crashed
 *		state if it crashes. This is useful for debugging purposes;
 *		without it, debugging a crash is substantially harder.
 *
 * recover:	This function will trigger an immediate recovery if the
 *		remote processor is in a crashed state, without changing
 *		or checking the recovery state (enabled/disabled).
 *		This is useful during debugging sessions, when one expects
 *		additional crashes to happen after enabling recovery. In this
 *		case, enabling recovery will make it hard to debug subsequent
 *		crashes, so it's recommended to keep recovery disabled, and
 *		instead use the "recover" command as needed.
 */
static ssize_t recovery_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct rproc *rproc = to_rproc(dev);

	if (sysfs_streq(buf, "enabled")) {
		/* change the flag and begin the recovery process if needed */
		rproc->recovery_disabled = false;
		rproc_trigger_recovery(rproc);
	} else if (sysfs_streq(buf, "disabled")) {
		rproc->recovery_disabled = true;
	} else if (sysfs_streq(buf, "recover")) {
		/* begin the recovery process without changing the flag */
		rproc_trigger_recovery(rproc);
	} else {
		return -EINVAL;
	}

	return count;
}
static DEVICE_ATTR_RW(recovery);

/*
 * A coredump-configuration-to-string lookup table, for exposing a
 * human readable configuration via sysfs. Always keep in sync with
 * enum rproc_coredump_mechanism
 */
static const char * const rproc_coredump_str[] = {
	[RPROC_COREDUMP_DISABLED]	= "disabled",
	[RPROC_COREDUMP_ENABLED]	= "enabled",
	[RPROC_COREDUMP_INLINE]		= "inline",
};

/* Expose the current coredump configuration via debugfs */
static ssize_t coredump_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct rproc *rproc = to_rproc(dev);

	return sysfs_emit(buf, "%s\n", rproc_coredump_str[rproc->dump_conf]);
}

/*
 * By writing to the 'coredump' sysfs entry, we control the behavior of the
 * coredump mechanism dynamically. The default value of this entry is "default".
 *
 * The 'coredump' sysfs entry supports these commands:
 *
 * disabled:	This is the default coredump mechanism. Recovery will proceed
 *		without collecting any dump.
 *
 * default:	When the remoteproc crashes the entire coredump will be
 *		copied to a separate buffer and exposed to userspace.
 *
 * inline:	The coredump will not be copied to a separate buffer and the
 *		recovery process will have to wait until data is read by
 *		userspace. But this avoid usage of extra memory.
 */
static ssize_t coredump_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct rproc *rproc = to_rproc(dev);

	if (rproc->state == RPROC_CRASHED) {
		dev_err(&rproc->dev, "can't change coredump configuration\n");
		return -EBUSY;
	}

	if (sysfs_streq(buf, "disabled")) {
		rproc->dump_conf = RPROC_COREDUMP_DISABLED;
	} else if (sysfs_streq(buf, "enabled")) {
		rproc->dump_conf = RPROC_COREDUMP_ENABLED;
	} else if (sysfs_streq(buf, "inline")) {
		rproc->dump_conf = RPROC_COREDUMP_INLINE;
	} else {
		dev_err(&rproc->dev, "Invalid coredump configuration\n");
		return -EINVAL;
	}

	return count;
}
static DEVICE_ATTR_RW(coredump);

/* Expose the loaded / running firmware name via sysfs */
static ssize_t firmware_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct rproc *rproc = to_rproc(dev);
	const char *firmware = rproc->firmware;

	/*
	 * If the remote processor has been started by an external
	 * entity we have no idea of what image it is running.  As such
	 * simply display a generic string rather then rproc->firmware.
	 */
	if (rproc->state == RPROC_ATTACHED)
		firmware = "unknown";

	return sprintf(buf, "%s\n", firmware);
}

/* Change firmware name via sysfs */
static ssize_t firmware_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct rproc *rproc = to_rproc(dev);
	int err;

	err = rproc_set_firmware(rproc, buf);

	return err ? err : count;
}
static DEVICE_ATTR_RW(firmware);

/*
 * A state-to-string lookup table, for exposing a human readable state
 * via sysfs. Always keep in sync with enum rproc_state
 */
static const char * const rproc_state_string[] = {
	[RPROC_OFFLINE]		= "offline",
	[RPROC_SUSPENDED]	= "suspended",
	[RPROC_RUNNING]		= "running",
	[RPROC_CRASHED]		= "crashed",
	[RPROC_DELETED]		= "deleted",
	[RPROC_ATTACHED]	= "attached",
	[RPROC_DETACHED]	= "detached",
	[RPROC_LAST]		= "invalid",
};

/* Expose the state of the remote processor via sysfs */
static ssize_t state_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct rproc *rproc = to_rproc(dev);
	unsigned int state;

	state = rproc->state > RPROC_LAST ? RPROC_LAST : rproc->state;
	return sprintf(buf, "%s\n", rproc_state_string[state]);
}

/* Change remote processor state via sysfs */
static ssize_t state_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct rproc *rproc = to_rproc(dev);
	int ret = 0;

	if (sysfs_streq(buf, "start")) {
		ret = rproc_boot(rproc);
		if (ret)
			dev_err(&rproc->dev, "Boot failed: %d\n", ret);
	} else if (sysfs_streq(buf, "stop")) {
		ret = rproc_shutdown(rproc);
	} else if (sysfs_streq(buf, "detach")) {
		ret = rproc_detach(rproc);
	} else {
		dev_err(&rproc->dev, "Unrecognised option: %s\n", buf);
		ret = -EINVAL;
	}
	return ret ? ret : count;
}
static DEVICE_ATTR_RW(state);

/* Expose the name of the remote processor via sysfs */
static ssize_t name_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct rproc *rproc = to_rproc(dev);

	return sprintf(buf, "%s\n", rproc->name);
}
static DEVICE_ATTR_RO(name);

static umode_t rproc_is_visible(struct kobject *kobj, struct attribute *attr,
				int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct rproc *rproc = to_rproc(dev);
	umode_t mode = attr->mode;

	if (rproc->sysfs_read_only && (attr == &dev_attr_recovery.attr ||
				       attr == &dev_attr_firmware.attr ||
				       attr == &dev_attr_state.attr ||
				       attr == &dev_attr_coredump.attr))
		mode = 0444;

	return mode;
}

static struct attribute *rproc_attrs[] = {
	&dev_attr_coredump.attr,
	&dev_attr_recovery.attr,
	&dev_attr_firmware.attr,
	&dev_attr_state.attr,
	&dev_attr_name.attr,
	NULL
};

static const struct attribute_group rproc_devgroup = {
	.attrs = rproc_attrs,
	.is_visible = rproc_is_visible,
};

static const struct attribute_group *rproc_devgroups[] = {
	&rproc_devgroup,
	NULL
};

struct class rproc_class = {
	.name		= "remoteproc",
	.dev_groups	= rproc_devgroups,
};

int __init rproc_init_sysfs(void)
{
	/* create remoteproc device class for sysfs */
	int err = class_register(&rproc_class);

	if (err)
		pr_err("remoteproc: unable to register class\n");
	return err;
}

void __exit rproc_exit_sysfs(void)
{
	class_unregister(&rproc_class);
}
