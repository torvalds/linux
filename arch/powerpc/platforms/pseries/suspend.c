// SPDX-License-Identifier: GPL-2.0-or-later
/*
  * Copyright (C) 2010 Brian King IBM Corporation
  */

#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/suspend.h>
#include <linux/stat.h>
#include <asm/firmware.h>
#include <asm/hvcall.h>
#include <asm/machdep.h>
#include <asm/mmu.h>
#include <asm/rtas.h>
#include <asm/topology.h>

static struct device suspend_dev;

/**
 * pseries_suspend_begin - First phase of hibernation
 *
 * Check to ensure we are in a valid state to hibernate
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int pseries_suspend_begin(u64 stream_id)
{
	long vasi_state, rc;
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];

	/* Make sure the state is valid */
	rc = plpar_hcall(H_VASI_STATE, retbuf, stream_id);

	vasi_state = retbuf[0];

	if (rc) {
		pr_err("pseries_suspend_begin: vasi_state returned %ld\n",rc);
		return rc;
	} else if (vasi_state == H_VASI_ENABLED) {
		return -EAGAIN;
	} else if (vasi_state != H_VASI_SUSPENDING) {
		pr_err("pseries_suspend_begin: vasi_state returned state %ld\n",
		       vasi_state);
		return -EIO;
	}
	return 0;
}

/**
 * pseries_suspend_enter - Final phase of hibernation
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int pseries_suspend_enter(suspend_state_t state)
{
	return rtas_ibm_suspend_me(NULL);
}

/**
 * store_hibernate - Initiate partition hibernation
 * @dev:		subsys root device
 * @attr:		device attribute struct
 * @buf:		buffer
 * @count:		buffer size
 *
 * Write the stream ID received from the HMC to this file
 * to trigger hibernating the partition
 *
 * Return value:
 * 	number of bytes printed to buffer / other on failure
 **/
static ssize_t store_hibernate(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	u64 stream_id;
	int rc;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	stream_id = simple_strtoul(buf, NULL, 16);

	do {
		rc = pseries_suspend_begin(stream_id);
		if (rc == -EAGAIN)
			ssleep(1);
	} while (rc == -EAGAIN);

	if (!rc)
		rc = pm_suspend(PM_SUSPEND_MEM);

	if (!rc) {
		rc = count;
		post_mobility_fixup();
	}


	return rc;
}

#define USER_DT_UPDATE	0
#define KERN_DT_UPDATE	1

/**
 * show_hibernate - Report device tree update responsibilty
 * @dev:		subsys root device
 * @attr:		device attribute struct
 * @buf:		buffer
 *
 * Report whether a device tree update is performed by the kernel after a
 * resume, or if drmgr must coordinate the update from user space.
 *
 * Return value:
 *	0 if drmgr is to initiate update, and 1 otherwise
 **/
static ssize_t show_hibernate(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	return sprintf(buf, "%d\n", KERN_DT_UPDATE);
}

static DEVICE_ATTR(hibernate, 0644, show_hibernate, store_hibernate);

static struct bus_type suspend_subsys = {
	.name = "power",
	.dev_name = "power",
};

static const struct platform_suspend_ops pseries_suspend_ops = {
	.valid		= suspend_valid_only_mem,
	.enter		= pseries_suspend_enter,
};

/**
 * pseries_suspend_sysfs_register - Register with sysfs
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int pseries_suspend_sysfs_register(struct device *dev)
{
	struct device *dev_root;
	int rc;

	if ((rc = subsys_system_register(&suspend_subsys, NULL)))
		return rc;

	dev->id = 0;
	dev->bus = &suspend_subsys;

	dev_root = bus_get_dev_root(&suspend_subsys);
	if (dev_root) {
		rc = device_create_file(dev_root, &dev_attr_hibernate);
		put_device(dev_root);
		if (rc)
			goto subsys_unregister;
	}

	return 0;

subsys_unregister:
	bus_unregister(&suspend_subsys);
	return rc;
}

/**
 * pseries_suspend_init - initcall for pSeries suspend
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int __init pseries_suspend_init(void)
{
	int rc;

	if (!firmware_has_feature(FW_FEATURE_LPAR))
		return 0;

	if ((rc = pseries_suspend_sysfs_register(&suspend_dev)))
		return rc;

	suspend_set_ops(&pseries_suspend_ops);
	return 0;
}
machine_device_initcall(pseries, pseries_suspend_init);
