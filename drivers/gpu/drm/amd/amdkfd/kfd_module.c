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
#include <linux/notifier.h>
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

	err = kfd_chardev_init();
	if (err < 0)
		goto err_ioctl;

	err = kfd_topology_init();
	if (err < 0)
		goto err_topology;

	dev_info(kfd_device, "Initialized module\n");

	return 0;

err_topology:
	kfd_chardev_exit();
err_ioctl:
	return err;
}

static void __exit kfd_module_exit(void)
{
	kfd_topology_shutdown();
	kfd_chardev_exit();
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
