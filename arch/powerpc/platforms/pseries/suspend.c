/*
  * Copyright (C) 2010 Brian King IBM Corporation
  *
  * This program is free software; you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation; either version 2 of the License, or
  * (at your option) any later version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program; if not, write to the Free Software
  * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
  */

#include <linux/delay.h>
#include <linux/suspend.h>
#include <linux/stat.h>
#include <asm/firmware.h>
#include <asm/hvcall.h>
#include <asm/machdep.h>
#include <asm/mmu.h>
#include <asm/rtas.h>

static u64 stream_id;
static struct sys_device suspend_sysdev;
static DECLARE_COMPLETION(suspend_work);
static struct rtas_suspend_me_data suspend_data;
static atomic_t suspending;

/**
 * pseries_suspend_begin - First phase of hibernation
 *
 * Check to ensure we are in a valid state to hibernate
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int pseries_suspend_begin(suspend_state_t state)
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
 * pseries_suspend_cpu - Suspend a single CPU
 *
 * Makes the H_JOIN call to suspend the CPU
 *
 **/
static int pseries_suspend_cpu(void)
{
	if (atomic_read(&suspending))
		return rtas_suspend_cpu(&suspend_data);
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
	int rc = rtas_suspend_last_cpu(&suspend_data);

	atomic_set(&suspending, 0);
	atomic_set(&suspend_data.done, 1);
	return rc;
}

/**
 * pseries_prepare_late - Prepare to suspend all other CPUs
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int pseries_prepare_late(void)
{
	atomic_set(&suspending, 1);
	atomic_set(&suspend_data.working, 0);
	atomic_set(&suspend_data.done, 0);
	atomic_set(&suspend_data.error, 0);
	suspend_data.complete = &suspend_work;
	INIT_COMPLETION(suspend_work);
	return 0;
}

/**
 * store_hibernate - Initiate partition hibernation
 * @classdev:	sysdev class struct
 * @attr:		class device attribute struct
 * @buf:		buffer
 * @count:		buffer size
 *
 * Write the stream ID received from the HMC to this file
 * to trigger hibernating the partition
 *
 * Return value:
 * 	number of bytes printed to buffer / other on failure
 **/
static ssize_t store_hibernate(struct sysdev_class *classdev,
			       struct sysdev_class_attribute *attr,
			       const char *buf, size_t count)
{
	int rc;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	stream_id = simple_strtoul(buf, NULL, 16);

	do {
		rc = pseries_suspend_begin(PM_SUSPEND_MEM);
		if (rc == -EAGAIN)
			ssleep(1);
	} while (rc == -EAGAIN);

	if (!rc)
		rc = pm_suspend(PM_SUSPEND_MEM);

	stream_id = 0;

	if (!rc)
		rc = count;
	return rc;
}

static SYSDEV_CLASS_ATTR(hibernate, S_IWUSR, NULL, store_hibernate);

static struct sysdev_class suspend_sysdev_class = {
	.name = "power",
};

static const struct platform_suspend_ops pseries_suspend_ops = {
	.valid		= suspend_valid_only_mem,
	.begin		= pseries_suspend_begin,
	.prepare_late	= pseries_prepare_late,
	.enter		= pseries_suspend_enter,
};

/**
 * pseries_suspend_sysfs_register - Register with sysfs
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int pseries_suspend_sysfs_register(struct sys_device *sysdev)
{
	int rc;

	if ((rc = sysdev_class_register(&suspend_sysdev_class)))
		return rc;

	sysdev->id = 0;
	sysdev->cls = &suspend_sysdev_class;

	if ((rc = sysdev_class_create_file(&suspend_sysdev_class, &attr_hibernate)))
		goto class_unregister;

	return 0;

class_unregister:
	sysdev_class_unregister(&suspend_sysdev_class);
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

	if (!machine_is(pseries) || !firmware_has_feature(FW_FEATURE_LPAR))
		return 0;

	suspend_data.token = rtas_token("ibm,suspend-me");
	if (suspend_data.token == RTAS_UNKNOWN_SERVICE)
		return 0;

	if ((rc = pseries_suspend_sysfs_register(&suspend_sysdev)))
		return rc;

	ppc_md.suspend_disable_cpu = pseries_suspend_cpu;
	suspend_set_ops(&pseries_suspend_ops);
	return 0;
}

__initcall(pseries_suspend_init);
