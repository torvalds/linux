// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2021 Intel Corporation. All rights reserved. */
#include <linux/libnvdimm.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include "cxl.h"

/*
 * Ordered workqueue for cxl nvdimm device arrival and departure
 * to coordinate bus rescans when a bridge arrives and trigger remove
 * operations when the bridge is removed.
 */
static struct workqueue_struct *cxl_pmem_wq;

static int cxl_pmem_ctl(struct nvdimm_bus_descriptor *nd_desc,
			struct nvdimm *nvdimm, unsigned int cmd, void *buf,
			unsigned int buf_len, int *cmd_rc)
{
	return -ENOTTY;
}

static bool online_nvdimm_bus(struct cxl_nvdimm_bridge *cxl_nvb)
{
	if (cxl_nvb->nvdimm_bus)
		return true;
	cxl_nvb->nvdimm_bus =
		nvdimm_bus_register(&cxl_nvb->dev, &cxl_nvb->nd_desc);
	return cxl_nvb->nvdimm_bus != NULL;
}

static void offline_nvdimm_bus(struct cxl_nvdimm_bridge *cxl_nvb)
{
	if (!cxl_nvb->nvdimm_bus)
		return;
	nvdimm_bus_unregister(cxl_nvb->nvdimm_bus);
	cxl_nvb->nvdimm_bus = NULL;
}

static void cxl_nvb_update_state(struct work_struct *work)
{
	struct cxl_nvdimm_bridge *cxl_nvb =
		container_of(work, typeof(*cxl_nvb), state_work);
	bool release = false;

	device_lock(&cxl_nvb->dev);
	switch (cxl_nvb->state) {
	case CXL_NVB_ONLINE:
		if (!online_nvdimm_bus(cxl_nvb)) {
			dev_err(&cxl_nvb->dev,
				"failed to establish nvdimm bus\n");
			release = true;
		}
		break;
	case CXL_NVB_OFFLINE:
	case CXL_NVB_DEAD:
		offline_nvdimm_bus(cxl_nvb);
		break;
	default:
		break;
	}
	device_unlock(&cxl_nvb->dev);

	if (release)
		device_release_driver(&cxl_nvb->dev);

	put_device(&cxl_nvb->dev);
}

static void cxl_nvdimm_bridge_remove(struct device *dev)
{
	struct cxl_nvdimm_bridge *cxl_nvb = to_cxl_nvdimm_bridge(dev);

	if (cxl_nvb->state == CXL_NVB_ONLINE)
		cxl_nvb->state = CXL_NVB_OFFLINE;
	if (queue_work(cxl_pmem_wq, &cxl_nvb->state_work))
		get_device(&cxl_nvb->dev);
}

static int cxl_nvdimm_bridge_probe(struct device *dev)
{
	struct cxl_nvdimm_bridge *cxl_nvb = to_cxl_nvdimm_bridge(dev);

	if (cxl_nvb->state == CXL_NVB_DEAD)
		return -ENXIO;

	if (cxl_nvb->state == CXL_NVB_NEW) {
		cxl_nvb->nd_desc = (struct nvdimm_bus_descriptor) {
			.provider_name = "CXL",
			.module = THIS_MODULE,
			.ndctl = cxl_pmem_ctl,
		};

		INIT_WORK(&cxl_nvb->state_work, cxl_nvb_update_state);
	}

	cxl_nvb->state = CXL_NVB_ONLINE;
	if (queue_work(cxl_pmem_wq, &cxl_nvb->state_work))
		get_device(&cxl_nvb->dev);

	return 0;
}

static struct cxl_driver cxl_nvdimm_bridge_driver = {
	.name = "cxl_nvdimm_bridge",
	.probe = cxl_nvdimm_bridge_probe,
	.remove = cxl_nvdimm_bridge_remove,
	.id = CXL_DEVICE_NVDIMM_BRIDGE,
};

static __init int cxl_pmem_init(void)
{
	int rc;

	cxl_pmem_wq = alloc_ordered_workqueue("cxl_pmem", 0);
	if (!cxl_pmem_wq)
		return -ENXIO;

	rc = cxl_driver_register(&cxl_nvdimm_bridge_driver);
	if (rc)
		goto err;

	return 0;

err:
	destroy_workqueue(cxl_pmem_wq);
	return rc;
}

static __exit void cxl_pmem_exit(void)
{
	cxl_driver_unregister(&cxl_nvdimm_bridge_driver);
	destroy_workqueue(cxl_pmem_wq);
}

MODULE_LICENSE("GPL v2");
module_init(cxl_pmem_init);
module_exit(cxl_pmem_exit);
MODULE_IMPORT_NS(CXL);
MODULE_ALIAS_CXL(CXL_DEVICE_NVDIMM_BRIDGE);
