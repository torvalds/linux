// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2021 Intel Corporation. All rights reserved. */
#include <linux/libnvdimm.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/ndctl.h>
#include <linux/async.h>
#include <linux/slab.h>
#include "mem.h"
#include "cxl.h"

/*
 * Ordered workqueue for cxl nvdimm device arrival and departure
 * to coordinate bus rescans when a bridge arrives and trigger remove
 * operations when the bridge is removed.
 */
static struct workqueue_struct *cxl_pmem_wq;

static void unregister_nvdimm(void *nvdimm)
{
	nvdimm_delete(nvdimm);
}

static int match_nvdimm_bridge(struct device *dev, const void *data)
{
	return strcmp(dev_name(dev), "nvdimm-bridge") == 0;
}

static struct cxl_nvdimm_bridge *cxl_find_nvdimm_bridge(void)
{
	struct device *dev;

	dev = bus_find_device(&cxl_bus_type, NULL, NULL, match_nvdimm_bridge);
	if (!dev)
		return NULL;
	return to_cxl_nvdimm_bridge(dev);
}

static int cxl_nvdimm_probe(struct device *dev)
{
	struct cxl_nvdimm *cxl_nvd = to_cxl_nvdimm(dev);
	struct cxl_nvdimm_bridge *cxl_nvb;
	unsigned long flags = 0;
	struct nvdimm *nvdimm;
	int rc = -ENXIO;

	cxl_nvb = cxl_find_nvdimm_bridge();
	if (!cxl_nvb)
		return -ENXIO;

	device_lock(&cxl_nvb->dev);
	if (!cxl_nvb->nvdimm_bus)
		goto out;

	set_bit(NDD_LABELING, &flags);
	nvdimm = nvdimm_create(cxl_nvb->nvdimm_bus, cxl_nvd, NULL, flags, 0, 0,
			       NULL);
	if (!nvdimm)
		goto out;

	rc = devm_add_action_or_reset(dev, unregister_nvdimm, nvdimm);
out:
	device_unlock(&cxl_nvb->dev);
	put_device(&cxl_nvb->dev);

	return rc;
}

static struct cxl_driver cxl_nvdimm_driver = {
	.name = "cxl_nvdimm",
	.probe = cxl_nvdimm_probe,
	.id = CXL_DEVICE_NVDIMM,
};

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

static int cxl_nvdimm_release_driver(struct device *dev, void *data)
{
	if (!is_cxl_nvdimm(dev))
		return 0;
	device_release_driver(dev);
	return 0;
}

static void offline_nvdimm_bus(struct nvdimm_bus *nvdimm_bus)
{
	if (!nvdimm_bus)
		return;

	/*
	 * Set the state of cxl_nvdimm devices to unbound / idle before
	 * nvdimm_bus_unregister() rips the nvdimm objects out from
	 * underneath them.
	 */
	bus_for_each_dev(&cxl_bus_type, NULL, NULL, cxl_nvdimm_release_driver);
	nvdimm_bus_unregister(nvdimm_bus);
}

static void cxl_nvb_update_state(struct work_struct *work)
{
	struct cxl_nvdimm_bridge *cxl_nvb =
		container_of(work, typeof(*cxl_nvb), state_work);
	struct nvdimm_bus *victim_bus = NULL;
	bool release = false, rescan = false;

	device_lock(&cxl_nvb->dev);
	switch (cxl_nvb->state) {
	case CXL_NVB_ONLINE:
		if (!online_nvdimm_bus(cxl_nvb)) {
			dev_err(&cxl_nvb->dev,
				"failed to establish nvdimm bus\n");
			release = true;
		} else
			rescan = true;
		break;
	case CXL_NVB_OFFLINE:
	case CXL_NVB_DEAD:
		victim_bus = cxl_nvb->nvdimm_bus;
		cxl_nvb->nvdimm_bus = NULL;
		break;
	default:
		break;
	}
	device_unlock(&cxl_nvb->dev);

	if (release)
		device_release_driver(&cxl_nvb->dev);
	if (rescan) {
		int rc = bus_rescan_devices(&cxl_bus_type);

		dev_dbg(&cxl_nvb->dev, "rescan: %d\n", rc);
	}
	offline_nvdimm_bus(victim_bus);

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
		goto err_bridge;

	rc = cxl_driver_register(&cxl_nvdimm_driver);
	if (rc)
		goto err_nvdimm;

	return 0;

err_nvdimm:
	cxl_driver_unregister(&cxl_nvdimm_bridge_driver);
err_bridge:
	destroy_workqueue(cxl_pmem_wq);
	return rc;
}

static __exit void cxl_pmem_exit(void)
{
	cxl_driver_unregister(&cxl_nvdimm_driver);
	cxl_driver_unregister(&cxl_nvdimm_bridge_driver);
	destroy_workqueue(cxl_pmem_wq);
}

MODULE_LICENSE("GPL v2");
module_init(cxl_pmem_init);
module_exit(cxl_pmem_exit);
MODULE_IMPORT_NS(CXL);
MODULE_ALIAS_CXL(CXL_DEVICE_NVDIMM_BRIDGE);
MODULE_ALIAS_CXL(CXL_DEVICE_NVDIMM);
