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

#include <linux/sched.h>
#include <linux/device.h>
#include "kfd_priv.h"
#include "amdgpu_amdkfd.h"

static int kfd_init(void)
{
	int err;

	/* Verify module parameters */
	if ((sched_policy < KFD_SCHED_POLICY_HWS) ||
		(sched_policy > KFD_SCHED_POLICY_NO_HWS)) {
		pr_err("sched_policy has invalid value\n");
		return -EINVAL;
	}

	/* Verify module parameters */
	if ((max_num_of_queues_per_device < 1) ||
		(max_num_of_queues_per_device >
			KFD_MAX_NUM_OF_QUEUES_PER_DEVICE)) {
		pr_err("max_num_of_queues_per_device must be between 1 to KFD_MAX_NUM_OF_QUEUES_PER_DEVICE\n");
		return -EINVAL;
	}

	err = kfd_chardev_init();
	if (err < 0)
		goto err_ioctl;

	err = kfd_topology_init();
	if (err < 0)
		goto err_topology;

	err = kfd_process_create_wq();
	if (err < 0)
		goto err_create_wq;

	/* Ignore the return value, so that we can continue
	 * to init the KFD, even if procfs isn't craated
	 */
	kfd_procfs_init();

	kfd_debugfs_init();

	return 0;

err_create_wq:
	kfd_topology_shutdown();
err_topology:
	kfd_chardev_exit();
err_ioctl:
	return err;
}

static void kfd_exit(void)
{
	kfd_debugfs_fini();
	kfd_process_destroy_wq();
	kfd_procfs_shutdown();
	kfd_topology_shutdown();
	kfd_chardev_exit();
}

int kgd2kfd_init()
{
	return kfd_init();
}

void kgd2kfd_exit(void)
{
	kfd_exit();
}
