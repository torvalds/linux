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
				   u64 *pegmlength, u64 *pretiredpagesphys)
{
	int ret;

	/*
	 * The EGM memory information is present in the system ACPI tables
	 * as DSD properties nvidia,egm-base-pa and nvidia,egm-size.
	 */
	ret = device_property_read_u64(&pdev->dev, "nvidia,egm-size",
				       pegmlength);
	if (ret)
		goto error_exit;

	ret = device_property_read_u64(&pdev->dev, "nvidia,egm-base-pa",
				       pegmphys);
	if (ret)
		goto error_exit;

	/*
	 * SBIOS puts the list of retired pages on a region. The region
	 * SPA is exposed as "nvidia,egm-retired-pages-data-base".
	 */
	ret = device_property_read_u64(&pdev->dev,
				       "nvidia,egm-retired-pages-data-base",
				       pretiredpagesphys);
	if (ret)
		goto error_exit;

	/* Catch firmware bug and avoid a crash */
	if (*pretiredpagesphys == 0) {
		dev_err(&pdev->dev, "Retired pages region is not setup\n");
		ret = -EINVAL;
	}

error_exit:
	return ret;
}

static int create_egm_symlinks(struct nvgrace_egm_dev *egm_dev,
			       struct pci_dev *pdev)
{
	int ret;

	ret = sysfs_create_link_nowarn(&pdev->dev.kobj,
				       &egm_dev->aux_dev.dev.kobj,
				       dev_name(&egm_dev->aux_dev.dev));

	/*
	 * Allow if Link already exists - created since GPU is the auxiliary
	 * device's parent; flag the error otherwise.
	 */
	if (ret && ret != -EEXIST)
		return ret;

	return sysfs_create_link(&egm_dev->aux_dev.dev.kobj,
				 &pdev->dev.kobj,
				 dev_name(&pdev->dev));
}

int add_gpu(struct nvgrace_egm_dev *egm_dev, struct pci_dev *pdev)
{
	struct gpu_node *node;

	node = kvzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	node->pdev = pdev;

	list_add_tail(&node->list, &egm_dev->gpus);

	return create_egm_symlinks(egm_dev, pdev);
}

static void remove_egm_symlinks(struct nvgrace_egm_dev *egm_dev,
				struct pci_dev *pdev)
{
	sysfs_remove_link(&pdev->dev.kobj,
			  dev_name(&egm_dev->aux_dev.dev));
	sysfs_remove_link(&egm_dev->aux_dev.dev.kobj,
			  dev_name(&pdev->dev));
}

void remove_gpu(struct nvgrace_egm_dev *egm_dev, struct pci_dev *pdev)
{
	struct gpu_node *node, *tmp;

	list_for_each_entry_safe(node, tmp, &egm_dev->gpus, list) {
		if (node->pdev == pdev) {
			remove_egm_symlinks(egm_dev, pdev);
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
			      u64 egmphys, u64 egmlength, u64 egmpxm,
			      u64 retiredpagesphys)
{
	struct nvgrace_egm_dev *egm_dev;
	int ret;

	egm_dev = kvzalloc(sizeof(*egm_dev), GFP_KERNEL);
	if (!egm_dev)
		goto create_err;

	egm_dev->egmpxm = egmpxm;
	egm_dev->egmphys = egmphys;
	egm_dev->egmlength = egmlength;
	egm_dev->retiredpagesphys = retiredpagesphys;

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
