/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include "kfd_priv.h"

#define KFD_DRIVER_AUTHOR	"AMD Inc. and others"

#define KFD_DRIVER_DESC		"Standalone HSA driver for AMD's GPUs"
#define KFD_DRIVER_DATE		"20141113"
#define KFD_DRIVER_MAJOR	0
#define KFD_DRIVER_MINOR	7
#define KFD_DRIVER_PATCHLEVEL	0

const struct kfd2kgd_calls *kfd2kgd;
static const struct kgd2kfd_calls kgd2kfd = {
	.exit		= kgd2kfd_exit,
	.probe		= kgd2kfd_probe,
	.device_init	= kgd2kfd_device_init,
	.device_exit	= kgd2kfd_device_exit,
	.interrupt	= kgd2kfd_interrupt,
	.suspend	= kgd2kfd_suspend,
	.resume		= kgd2kfd_resume,
};

int sched_policy = KFD_SCHED_POLICY_HWS;
module_param(sched_policy, int, 0444);
MODULE_PARM_DESC(sched_policy,
	"Kernel cmdline parameter that defines the amdkfd scheduling policy");

int max_num_of_processes = KFD_MAX_NUM_OF_PROCESSES_DEFAULT;
module_param(max_num_of_processes, int, 0444);
MODULE_PARM_DESC(max_num_of_processes,
	"Kernel cmdline parameter that defines the amdkfd maximum number of supported processes");

int max_num_of_queues_per_process = KFD_MAX_NUM_OF_QUEUES_PER_PROCESS_DEFAULT;
module_param(max_num_of_queues_per_process, int, 0444);
MODULE_PARM_DESC(max_num_of_queues_per_process,
	"Kernel cmdline parameter that defines the amdkfd maximum number of supported queues per process");

bool kgd2kfd_init(unsigned interface_version,
		  const struct kfd2kgd_calls *f2g,
		  const struct kgd2kfd_calls **g2f)
{
	/*
	 * Only one interface version is supported,
	 * no kfd/kgd version skew allowed.
	 */
	if (interface_version != KFD_INTERFACE_VERSION)
		return false;

	/* Protection against multiple amd kgd loads */
	if (kfd2kgd)
		return true;

	kfd2kgd = f2g;
	*g2f = &kgd2kfd;

	return true;
}
EXPORT_SYMBOL(kgd2kfd_init);

void kgd2kfd_exit(void)
{
}

static int __init kfd_module_init(void)
{
	int err;

	kfd2kgd = NULL;

	/* Verify module parameters */
	if ((sched_policy < KFD_SCHED_POLICY_HWS) ||
		(sched_policy > KFD_SCHED_POLICY_NO_HWS)) {
		pr_err("kfd: sched_policy has invalid value\n");
		return -1;
	}

	/* Verify module parameters */
	if ((max_num_of_processes < 0) ||
		(max_num_of_processes > KFD_MAX_NUM_OF_PROCESSES)) {
		pr_err("kfd: max_num_of_processes must be between 0 to KFD_MAX_NUM_OF_PROCESSES\n");
		return -1;
	}

	if ((max_num_of_queues_per_process < 0) ||
		(max_num_of_queues_per_process >
			KFD_MAX_NUM_OF_QUEUES_PER_PROCESS)) {
		pr_err("kfd: max_num_of_queues_per_process must be between 0 to KFD_MAX_NUM_OF_QUEUES_PER_PROCESS\n");
		return -1;
	}

	err = kfd_pasid_init();
	if (err < 0)
		goto err_pasid;

	err = kfd_chardev_init();
	if (err < 0)
		goto err_ioctl;

	err = kfd_topology_init();
	if (err < 0)
		goto err_topology;

	kfd_process_create_wq();

	dev_info(kfd_device, "Initialized module\n");

	return 0;

err_topology:
	kfd_chardev_exit();
err_ioctl:
	kfd_pasid_exit();
err_pasid:
	return err;
}

static void __exit kfd_module_exit(void)
{
	kfd_process_destroy_wq();
	kfd_topology_shutdown();
	kfd_chardev_exit();
	kfd_pasid_exit();
	dev_info(kfd_device, "Removed module\n");
}

module_init(kfd_module_init);
module_exit(kfd_module_exit);

MODULE_AUTHOR(KFD_DRIVER_AUTHOR);
MODULE_DESCRIPTION(KFD_DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
MODULE_VERSION(__stringify(KFD_DRIVER_MAJOR) "."
	       __stringify(KFD_DRIVER_MINOR) "."
	       __stringify(KFD_DRIVER_PATCHLEVEL));
