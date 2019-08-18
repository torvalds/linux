// SPDX-License-Identifier: GPL-2.0+
/*
 * VFIO based AP device driver
 *
 * Copyright IBM Corp. 2018
 *
 * Author(s): Tony Krowiak <akrowiak@linux.ibm.com>
 */

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <asm/facility.h>
#include "vfio_ap_private.h"

#define VFIO_AP_ROOT_NAME "vfio_ap"
#define VFIO_AP_DEV_NAME "matrix"

MODULE_AUTHOR("IBM Corporation");
MODULE_DESCRIPTION("VFIO AP device driver, Copyright IBM Corp. 2018");
MODULE_LICENSE("GPL v2");

static struct ap_driver vfio_ap_drv;

struct ap_matrix_dev *matrix_dev;

/* Only type 10 adapters (CEX4 and later) are supported
 * by the AP matrix device driver
 */
static struct ap_device_id ap_queue_ids[] = {
	{ .dev_type = AP_DEVICE_TYPE_CEX4,
	  .match_flags = AP_DEVICE_ID_MATCH_QUEUE_TYPE },
	{ .dev_type = AP_DEVICE_TYPE_CEX5,
	  .match_flags = AP_DEVICE_ID_MATCH_QUEUE_TYPE },
	{ .dev_type = AP_DEVICE_TYPE_CEX6,
	  .match_flags = AP_DEVICE_ID_MATCH_QUEUE_TYPE },
	{ /* end of sibling */ },
};

MODULE_DEVICE_TABLE(vfio_ap, ap_queue_ids);

static int vfio_ap_queue_dev_probe(struct ap_device *apdev)
{
	return 0;
}

static void vfio_ap_queue_dev_remove(struct ap_device *apdev)
{
	/* Nothing to do yet */
}

static void vfio_ap_matrix_dev_release(struct device *dev)
{
	struct ap_matrix_dev *matrix_dev = dev_get_drvdata(dev);

	kfree(matrix_dev);
}

static int matrix_bus_match(struct device *dev, struct device_driver *drv)
{
	return 1;
}

static struct bus_type matrix_bus = {
	.name = "matrix",
	.match = &matrix_bus_match,
};

static struct device_driver matrix_driver = {
	.name = "vfio_ap",
	.bus = &matrix_bus,
	.suppress_bind_attrs = true,
};

static int vfio_ap_matrix_dev_create(void)
{
	int ret;
	struct device *root_device;

	root_device = root_device_register(VFIO_AP_ROOT_NAME);
	if (IS_ERR(root_device))
		return PTR_ERR(root_device);

	ret = bus_register(&matrix_bus);
	if (ret)
		goto bus_register_err;

	matrix_dev = kzalloc(sizeof(*matrix_dev), GFP_KERNEL);
	if (!matrix_dev) {
		ret = -ENOMEM;
		goto matrix_alloc_err;
	}

	/* Fill in config info via PQAP(QCI), if available */
	if (test_facility(12)) {
		ret = ap_qci(&matrix_dev->info);
		if (ret)
			goto matrix_alloc_err;
	}

	mutex_init(&matrix_dev->lock);
	INIT_LIST_HEAD(&matrix_dev->mdev_list);

	dev_set_name(&matrix_dev->device, "%s", VFIO_AP_DEV_NAME);
	matrix_dev->device.parent = root_device;
	matrix_dev->device.bus = &matrix_bus;
	matrix_dev->device.release = vfio_ap_matrix_dev_release;
	matrix_dev->vfio_ap_drv = &vfio_ap_drv;

	ret = device_register(&matrix_dev->device);
	if (ret)
		goto matrix_reg_err;

	ret = driver_register(&matrix_driver);
	if (ret)
		goto matrix_drv_err;

	return 0;

matrix_drv_err:
	device_unregister(&matrix_dev->device);
matrix_reg_err:
	put_device(&matrix_dev->device);
matrix_alloc_err:
	bus_unregister(&matrix_bus);
bus_register_err:
	root_device_unregister(root_device);
	return ret;
}

static void vfio_ap_matrix_dev_destroy(void)
{
	struct device *root_device = matrix_dev->device.parent;

	driver_unregister(&matrix_driver);
	device_unregister(&matrix_dev->device);
	bus_unregister(&matrix_bus);
	root_device_unregister(root_device);
}

static int __init vfio_ap_init(void)
{
	int ret;

	/* If there are no AP instructions, there is nothing to pass through. */
	if (!ap_instructions_available())
		return -ENODEV;

	ret = vfio_ap_matrix_dev_create();
	if (ret)
		return ret;

	memset(&vfio_ap_drv, 0, sizeof(vfio_ap_drv));
	vfio_ap_drv.probe = vfio_ap_queue_dev_probe;
	vfio_ap_drv.remove = vfio_ap_queue_dev_remove;
	vfio_ap_drv.ids = ap_queue_ids;

	ret = ap_driver_register(&vfio_ap_drv, THIS_MODULE, VFIO_AP_DRV_NAME);
	if (ret) {
		vfio_ap_matrix_dev_destroy();
		return ret;
	}

	ret = vfio_ap_mdev_register();
	if (ret) {
		ap_driver_unregister(&vfio_ap_drv);
		vfio_ap_matrix_dev_destroy();

		return ret;
	}

	return 0;
}

static void __exit vfio_ap_exit(void)
{
	vfio_ap_mdev_unregister();
	ap_driver_unregister(&vfio_ap_drv);
	vfio_ap_matrix_dev_destroy();
}

module_init(vfio_ap_init);
module_exit(vfio_ap_exit);
