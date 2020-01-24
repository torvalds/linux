// SPDX-License-Identifier: GPL-2.0-only
/*
 * Remote Processor Framework
 */

#include <linux/remoteproc.h>

#include "remoteproc_internal.h"

#define to_rproc(d) container_of(d, struct rproc, dev)

/* Expose the loaded / running firmware name via sysfs */
static ssize_t firmware_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct rproc *rproc = to_rproc(dev);

	return sprintf(buf, "%s\n", rproc->firmware);
}

/* Change firmware name via sysfs */
static ssize_t firmware_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct rproc *rproc = to_rproc(dev);
	char *p;
	int err, len = count;

	err = mutex_lock_interruptible(&rproc->lock);
	if (err) {
		dev_err(dev, "can't lock rproc %s: %d\n", rproc->name, err);
		return -EINVAL;
	}

	if (rproc->state != RPROC_OFFLINE) {
		dev_err(dev, "can't change firmware while running\n");
		err = -EBUSY;
		goto out;
	}

	len = strcspn(buf, "\n");
	if (!len) {
		dev_err(dev, "can't provide a NULL firmware\n");
		err = -EINVAL;
		goto out;
	}

	p = kstrndup(buf, len, GFP_KERNEL);
	if (!p) {
		err = -ENOMEM;
		goto out;
	}

	kfree(rproc->firmware);
	rproc->firmware = p;
out:
	mutex_unlock(&rproc->lock);

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
		if (rproc->state == RPROC_RUNNING)
			return -EBUSY;

		ret = rproc_boot(rproc);
		if (ret)
			dev_err(&rproc->dev, "Boot failed: %d\n", ret);
	} else if (sysfs_streq(buf, "stop")) {
		if (rproc->state != RPROC_RUNNING)
			return -EINVAL;

		rproc_shutdown(rproc);
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

static struct attribute *rproc_attrs[] = {
	&dev_attr_firmware.attr,
	&dev_attr_state.attr,
	&dev_attr_name.attr,
	NULL
};

static const struct attribute_group rproc_devgroup = {
	.attrs = rproc_attrs
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
