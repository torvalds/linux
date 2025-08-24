// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#include <linux/vfio_pci_core.h>
#include "egm_dev.h"

/*
 * Determine if the EGM feature is enabled. If disabled, there
 * will be no EGM properties populated in the ACPI tables and this
 * fetch would fail.
 */
int nvgrace_gpu_has_egm_property(struct pci_dev *pdev, u64 *pegmpxm)
{
	return device_property_read_u64(&pdev->dev, "nvidia,egm-pxm",
					pegmpxm);
}

int nvgrace_gpu_fetch_egm_property(struct pci_dev *pdev, u64 *pegmphys,
				   u64 *pegmlength)
{
	int ret;

	/*
	 * The memory information is present in the system ACPI tables as DSD
	 * properties nvidia,egm-base-pa and nvidia,egm-size.
	 */
	ret = device_property_read_u64(&pdev->dev, "nvidia,egm-size",
				       pegmlength);
	if (ret)
		return ret;

	ret = device_property_read_u64(&pdev->dev, "nvidia,egm-base-pa",
				       pegmphys);

	return ret;
}

int add_gpu(struct nvgrace_egm_dev *egm_dev, struct pci_dev *pdev)
{
	struct gpu_node *node;

	node = kvzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	node->pdev = pdev;

	list_add_tail(&node->list, &egm_dev->gpus);

	return 0;
}

void remove_gpu(struct nvgrace_egm_dev *egm_dev, struct pci_dev *pdev)
{
	struct gpu_node *node, *tmp;

	list_for_each_entry_safe(node, tmp, &egm_dev->gpus, list) {
		if (node->pdev == pdev) {
			list_del(&node->list);
			kvfree(node);
		}
	}
}

static void nvgrace_gpu_release_aux_device(struct device *device)
{
	struct auxiliary_device *aux_dev = container_of(device, struct auxiliary_device, dev);
	struct nvgrace_egm_dev *egm_dev = container_of(aux_dev, struct nvgrace_egm_dev, aux_dev);

	kvfree(egm_dev);
}

struct nvgrace_egm_dev *
nvgrace_gpu_create_aux_device(struct pci_dev *pdev, const char *name,
			      u64 egmphys, u64 egmlength, u64 egmpxm)
{
	struct nvgrace_egm_dev *egm_dev;
	int ret;

	egm_dev = kvzalloc(sizeof(*egm_dev), GFP_KERNEL);
	if (!egm_dev)
		goto create_err;

	egm_dev->egmpxm = egmpxm;
	egm_dev->egmphys = egmphys;
	egm_dev->egmlength = egmlength;
	INIT_LIST_HEAD(&egm_dev->gpus);

	egm_dev->aux_dev.id = egmpxm;
	egm_dev->aux_dev.name = name;
	egm_dev->aux_dev.dev.release = nvgrace_gpu_release_aux_device;
	egm_dev->aux_dev.dev.parent = &pdev->dev;

	ret = auxiliary_device_init(&egm_dev->aux_dev);
	if (ret)
		goto free_dev;

	ret = auxiliary_device_add(&egm_dev->aux_dev);
	if (ret) {
		auxiliary_device_uninit(&egm_dev->aux_dev);
		goto free_dev;
	}

	return egm_dev;

free_dev:
	kvfree(egm_dev);
create_err:
	return NULL;
}
