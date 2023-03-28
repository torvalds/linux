// SPDX-License-Identifier: GPL-2.0+
/*
 * VFIO based AP device driver
 *
 * Copyright IBM Corp. 2018
 *
 * Author(s): Tony Krowiak <akrowiak@linux.ibm.com>
 *	      Pierre Morel <pmorel@linux.ibm.com>
 */

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <asm/facility.h>
#include "vfio_ap_private.h"
#include "vfio_ap_debug.h"

#define VFIO_AP_ROOT_NAME "vfio_ap"
#define VFIO_AP_DEV_NAME "matrix"

MODULE_AUTHOR("IBM Corporation");
MODULE_DESCRIPTION("VFIO AP device driver, Copyright IBM Corp. 2018");
MODULE_LICENSE("GPL v2");

struct ap_matrix_dev *matrix_dev;
debug_info_t *vfio_ap_dbf_info;

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
	{ .dev_type = AP_DEVICE_TYPE_CEX7,
	  .match_flags = AP_DEVICE_ID_MATCH_QUEUE_TYPE },
	{ .dev_type = AP_DEVICE_TYPE_CEX8,
	  .match_flags = AP_DEVICE_ID_MATCH_QUEUE_TYPE },
	{ /* end of sibling */ },
};

static struct ap_driver vfio_ap_drv = {
	.probe = vfio_ap_mdev_probe_queue,
	.remove = vfio_ap_mdev_remove_queue,
	.in_use = vfio_ap_mdev_resource_in_use,
	.on_config_changed = vfio_ap_on_cfg_changed,
	.on_scan_complete = vfio_ap_on_scan_complete,
	.ids = ap_queue_ids,
};

static void vfio_ap_matrix_dev_release(struct device *dev)
{
	struct ap_matrix_dev *matrix_dev;

	matrix_dev = container_of(dev, struct ap_matrix_dev, device);
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

	mutex_init(&matrix_dev->mdevs_lock);
	INIT_LIST_HEAD(&matrix_dev->mdev_list);
	mutex_init(&matrix_dev->guests_lock);

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
	device_del(&matrix_dev->device);
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

static int __init vfio_ap_dbf_info_init(void)
{
	vfio_ap_dbf_info = debug_register("vfio_ap", 1, 1,
					  DBF_MAX_SPRINTF_ARGS * sizeof(long));

	if (!vfio_ap_dbf_info)
		return -ENOENT;

	debug_register_view(vfio_ap_dbf_info, &debug_sprintf_view);
	debug_set_level(vfio_ap_dbf_info, DBF_WARN);

	return 0;
}

static int __init vfio_ap_init(void)
{
	int ret;

	ret = vfio_ap_dbf_info_init();
	if (ret)
		return ret;

	/* If there are no AP instructions, there is nothing to pass through. */
	if (!ap_instructions_available())
		return -ENODEV;

	ret = vfio_ap_matrix_dev_create();
	if (ret)
		return ret;

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
	debug_unregister(vfio_ap_dbf_info);
}

module_init(vfio_ap_init);
module_exit(vfio_ap_exit);
