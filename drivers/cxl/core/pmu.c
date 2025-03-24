// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Huawei. All rights reserved. */

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <cxlmem.h>
#include <pmu.h>
#include <cxl.h>
#include "core.h"

static void cxl_pmu_release(struct device *dev)
{
	struct cxl_pmu *pmu = to_cxl_pmu(dev);

	kfree(pmu);
}

const struct device_type cxl_pmu_type = {
	.name = "cxl_pmu",
	.release = cxl_pmu_release,
};

static void remove_dev(void *dev)
{
	device_unregister(dev);
}

int devm_cxl_pmu_add(struct device *parent, struct cxl_pmu_regs *regs,
		     int assoc_id, int index, enum cxl_pmu_type type)
{
	struct cxl_pmu *pmu;
	struct device *dev;
	int rc;

	pmu = kzalloc(sizeof(*pmu), GFP_KERNEL);
	if (!pmu)
		return -ENOMEM;

	pmu->assoc_id = assoc_id;
	pmu->index = index;
	pmu->type = type;
	pmu->base = regs->pmu;
	dev = &pmu->dev;
	device_initialize(dev);
	device_set_pm_not_required(dev);
	dev->parent = parent;
	dev->bus = &cxl_bus_type;
	dev->type = &cxl_pmu_type;
	switch (pmu->type) {
	case CXL_PMU_MEMDEV:
		rc = dev_set_name(dev, "pmu_mem%d.%d", assoc_id, index);
		break;
	}
	if (rc)
		goto err;

	rc = device_add(dev);
	if (rc)
		goto err;

	return devm_add_action_or_reset(parent, remove_dev, dev);

err:
	put_device(&pmu->dev);
	return rc;
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_pmu_add, "CXL");
