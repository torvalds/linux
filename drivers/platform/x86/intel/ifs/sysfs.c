// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 Intel Corporation. */

#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/semaphore.h>
#include <linux/slab.h>

#include "ifs.h"

/*
 * Protects against simultaneous tests on multiple cores, or
 * reloading can file while a test is in progress
 */
static DEFINE_SEMAPHORE(ifs_sem);

/*
 * The sysfs interface to check additional details of last test
 * cat /sys/devices/system/platform/ifs/details
 */
static ssize_t details_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct ifs_data *ifsd = ifs_get_data(dev);

	return sysfs_emit(buf, "%#llx\n", ifsd->scan_details);
}

static DEVICE_ATTR_RO(details);

static const char * const status_msg[] = {
	[SCAN_NOT_TESTED] = "untested",
	[SCAN_TEST_PASS] = "pass",
	[SCAN_TEST_FAIL] = "fail"
};

/*
 * The sysfs interface to check the test status:
 * To check the status of last test
 * cat /sys/devices/platform/ifs/status
 */
static ssize_t status_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct ifs_data *ifsd = ifs_get_data(dev);

	return sysfs_emit(buf, "%s\n", status_msg[ifsd->status]);
}

static DEVICE_ATTR_RO(status);

/*
 * The sysfs interface for single core testing
 * To start test, for example, cpu5
 * echo 5 > /sys/devices/platform/ifs/run_test
 * To check the result:
 * cat /sys/devices/platform/ifs/result
 * The sibling core gets tested at the same time.
 */
static ssize_t run_test_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct ifs_data *ifsd = ifs_get_data(dev);
	unsigned int cpu;
	int rc;

	rc = kstrtouint(buf, 0, &cpu);
	if (rc < 0 || cpu >= nr_cpu_ids)
		return -EINVAL;

	if (down_interruptible(&ifs_sem))
		return -EINTR;

	if (!ifsd->loaded)
		rc = -EPERM;
	else
		rc = do_core_test(cpu, dev);

	up(&ifs_sem);

	return rc ? rc : count;
}

static DEVICE_ATTR_WO(run_test);

/*
 * Display currently loaded IFS image version.
 */
static ssize_t image_version_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct ifs_data *ifsd = ifs_get_data(dev);

	if (!ifsd->loaded)
		return sysfs_emit(buf, "%s\n", "none");
	else
		return sysfs_emit(buf, "%#x\n", ifsd->loaded_version);
}

static DEVICE_ATTR_RO(image_version);

/* global scan sysfs attributes */
static struct attribute *plat_ifs_attrs[] = {
	&dev_attr_details.attr,
	&dev_attr_status.attr,
	&dev_attr_run_test.attr,
	&dev_attr_image_version.attr,
	NULL
};

ATTRIBUTE_GROUPS(plat_ifs);

const struct attribute_group **ifs_get_groups(void)
{
	return plat_ifs_groups;
}
