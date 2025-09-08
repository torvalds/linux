// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 - 2025 Intel Corporation
 */

#include <linux/auxiliary_bus.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include "ipu7.h"
#include "ipu7-bus.h"
#include "ipu7-boot.h"
#include "ipu7-dma.h"

static int bus_pm_runtime_suspend(struct device *dev)
{
	struct ipu7_bus_device *adev = to_ipu7_bus_device(dev);
	int ret;

	ret = pm_generic_runtime_suspend(dev);
	if (ret)
		return ret;

	ret = ipu_buttress_powerdown(dev, adev->ctrl);
	if (!ret)
		return 0;

	dev_err(dev, "power down failed!\n");

	/* Powering down failed, attempt to resume device now */
	ret = pm_generic_runtime_resume(dev);
	if (!ret)
		return -EBUSY;

	return -EIO;
}

static int bus_pm_runtime_resume(struct device *dev)
{
	struct ipu7_bus_device *adev = to_ipu7_bus_device(dev);
	int ret;

	ret = ipu_buttress_powerup(dev, adev->ctrl);
	if (ret)
		return ret;

	ret = pm_generic_runtime_resume(dev);
	if (ret)
		goto out_err;

	return 0;

out_err:
	ipu_buttress_powerdown(dev, adev->ctrl);

	return -EBUSY;
}

static struct dev_pm_domain ipu7_bus_pm_domain = {
	.ops = {
		.runtime_suspend = bus_pm_runtime_suspend,
		.runtime_resume = bus_pm_runtime_resume,
	},
};

static DEFINE_MUTEX(ipu7_bus_mutex);
static void ipu7_bus_release(struct device *dev)
{
	struct ipu7_bus_device *adev = to_ipu7_bus_device(dev);

	kfree(adev->pdata);
	kfree(adev);
}

struct ipu7_bus_device *
ipu7_bus_initialize_device(struct pci_dev *pdev, struct device *parent,
			   void *pdata, const struct ipu_buttress_ctrl *ctrl,
			   const char *name)
{
	struct auxiliary_device *auxdev;
	struct ipu7_bus_device *adev;
	struct ipu7_device *isp = pci_get_drvdata(pdev);
	int ret;

	adev = kzalloc(sizeof(*adev), GFP_KERNEL);
	if (!adev)
		return ERR_PTR(-ENOMEM);

	adev->isp = isp;
	adev->ctrl = ctrl;
	adev->pdata = pdata;
	auxdev = &adev->auxdev;
	auxdev->name = name;
	auxdev->id = (pci_domain_nr(pdev->bus) << 16) |
		PCI_DEVID(pdev->bus->number, pdev->devfn);

	auxdev->dev.parent = parent;
	auxdev->dev.release = ipu7_bus_release;

	ret = auxiliary_device_init(auxdev);
	if (ret < 0) {
		dev_err(&isp->pdev->dev, "auxiliary device init failed (%d)\n",
			ret);
		kfree(adev);
		return ERR_PTR(ret);
	}

	dev_pm_domain_set(&auxdev->dev, &ipu7_bus_pm_domain);

	pm_runtime_forbid(&adev->auxdev.dev);
	pm_runtime_enable(&adev->auxdev.dev);

	return adev;
}

int ipu7_bus_add_device(struct ipu7_bus_device *adev)
{
	struct auxiliary_device *auxdev = &adev->auxdev;
	int ret;

	ret = auxiliary_device_add(auxdev);
	if (ret) {
		auxiliary_device_uninit(auxdev);
		return ret;
	}

	mutex_lock(&ipu7_bus_mutex);
	list_add(&adev->list, &adev->isp->devices);
	mutex_unlock(&ipu7_bus_mutex);

	pm_runtime_allow(&auxdev->dev);

	return 0;
}

void ipu7_bus_del_devices(struct pci_dev *pdev)
{
	struct ipu7_device *isp = pci_get_drvdata(pdev);
	struct ipu7_bus_device *adev, *save;

	mutex_lock(&ipu7_bus_mutex);

	list_for_each_entry_safe(adev, save, &isp->devices, list) {
		pm_runtime_disable(&adev->auxdev.dev);
		list_del(&adev->list);
		auxiliary_device_delete(&adev->auxdev);
		auxiliary_device_uninit(&adev->auxdev);
	}

	mutex_unlock(&ipu7_bus_mutex);
}
