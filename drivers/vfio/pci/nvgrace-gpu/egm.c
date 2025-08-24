// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#include <linux/vfio_pci_core.h>
#include <linux/nvgrace-egm.h>

#define MAX_EGM_NODES 4

static dev_t dev;
static struct class *class;

static int egm_driver_probe(struct auxiliary_device *aux_dev,
			    const struct auxiliary_device_id *id)
{
	return 0;
}

static void egm_driver_remove(struct auxiliary_device *aux_dev)
{
}

static const struct auxiliary_device_id egm_id_table[] = {
	{ .name = "nvgrace_gpu_vfio_pci.egm" },
	{ .name = "nvidia_vgpu_vfio.egm" },
	{ },
};
MODULE_DEVICE_TABLE(auxiliary, egm_id_table);

static struct auxiliary_driver egm_driver = {
	.name = KBUILD_MODNAME,
	.id_table = egm_id_table,
	.probe = egm_driver_probe,
	.remove = egm_driver_remove,
};

static char *egm_devnode(const struct device *device, umode_t *mode)
{
	if (mode)
		*mode = 0600;

	return NULL;
}

static int __init nvgrace_egm_init(void)
{
	int ret;

	/*
	 * Each EGM region on a system is represented with a unique
	 * char device with a different minor number. Allow a range
	 * of char device creation.
	 */
	ret = alloc_chrdev_region(&dev, 0, MAX_EGM_NODES,
				  NVGRACE_EGM_DEV_NAME);
	if (ret < 0)
		return ret;

	class = class_create(NVGRACE_EGM_DEV_NAME);
	if (IS_ERR(class)) {
		ret = PTR_ERR(class);
		goto unregister_chrdev;
	}

	class->devnode = egm_devnode;

	ret = auxiliary_driver_register(&egm_driver);
	if (!ret)
		goto fn_exit;

	class_destroy(class);
unregister_chrdev:
	unregister_chrdev_region(dev, MAX_EGM_NODES);
fn_exit:
	return ret;
}

static void __exit nvgrace_egm_cleanup(void)
{
	class_destroy(class);
	unregister_chrdev_region(dev, MAX_EGM_NODES);
	auxiliary_driver_unregister(&egm_driver);
}

module_init(nvgrace_egm_init);
module_exit(nvgrace_egm_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ankit Agrawal <ankita@nvidia.com>");
MODULE_DESCRIPTION("NVGRACE EGM - Module to support Extended GPU Memory on NVIDIA Grace Based systems");
