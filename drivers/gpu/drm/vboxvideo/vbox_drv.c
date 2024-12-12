// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2013-2017 Oracle Corporation
 * This file is based on ast_drv.c
 * Copyright 2012 Red Hat Inc.
 * Authors: Dave Airlie <airlied@redhat.com>
 *          Michael Thayer <michael.thayer@oracle.com,
 *          Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/aperture.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/vt_kern.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_ttm.h>
#include <drm/drm_file.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_module.h>

#include "vbox_drv.h"

static int vbox_modeset = -1;

MODULE_PARM_DESC(modeset, "Disable/Enable modesetting");
module_param_named(modeset, vbox_modeset, int, 0400);

static const struct drm_driver driver;

static const struct pci_device_id pciidlist[] = {
	{ PCI_DEVICE(0x80ee, 0xbeef) },
	{ }
};
MODULE_DEVICE_TABLE(pci, pciidlist);

static int vbox_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct vbox_private *vbox;
	int ret = 0;

	if (!vbox_check_supported(VBE_DISPI_ID_HGSMI))
		return -ENODEV;

	ret = aperture_remove_conflicting_pci_devices(pdev, driver.name);
	if (ret)
		return ret;

	vbox = devm_drm_dev_alloc(&pdev->dev, &driver,
				  struct vbox_private, ddev);
	if (IS_ERR(vbox))
		return PTR_ERR(vbox);

	pci_set_drvdata(pdev, vbox);
	mutex_init(&vbox->hw_mutex);

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = vbox_hw_init(vbox);
	if (ret)
		return ret;

	ret = vbox_mm_init(vbox);
	if (ret)
		goto err_hw_fini;

	ret = vbox_mode_init(vbox);
	if (ret)
		goto err_hw_fini;

	ret = vbox_irq_init(vbox);
	if (ret)
		goto err_mode_fini;

	ret = drm_dev_register(&vbox->ddev, 0);
	if (ret)
		goto err_irq_fini;

	drm_client_setup(&vbox->ddev, NULL);

	return 0;

err_irq_fini:
	vbox_irq_fini(vbox);
err_mode_fini:
	vbox_mode_fini(vbox);
err_hw_fini:
	vbox_hw_fini(vbox);
	return ret;
}

static void vbox_pci_remove(struct pci_dev *pdev)
{
	struct vbox_private *vbox = pci_get_drvdata(pdev);

	drm_dev_unregister(&vbox->ddev);
	drm_atomic_helper_shutdown(&vbox->ddev);
	vbox_irq_fini(vbox);
	vbox_mode_fini(vbox);
	vbox_hw_fini(vbox);
}

static void vbox_pci_shutdown(struct pci_dev *pdev)
{
	struct vbox_private *vbox = pci_get_drvdata(pdev);

	drm_atomic_helper_shutdown(&vbox->ddev);
}

static int vbox_pm_suspend(struct device *dev)
{
	struct vbox_private *vbox = dev_get_drvdata(dev);
	struct pci_dev *pdev = to_pci_dev(dev);
	int error;

	error = drm_mode_config_helper_suspend(&vbox->ddev);
	if (error)
		return error;

	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3hot);

	return 0;
}

static int vbox_pm_resume(struct device *dev)
{
	struct vbox_private *vbox = dev_get_drvdata(dev);
	struct pci_dev *pdev = to_pci_dev(dev);

	if (pci_enable_device(pdev))
		return -EIO;

	return drm_mode_config_helper_resume(&vbox->ddev);
}

static int vbox_pm_freeze(struct device *dev)
{
	struct vbox_private *vbox = dev_get_drvdata(dev);

	return drm_mode_config_helper_suspend(&vbox->ddev);
}

static int vbox_pm_thaw(struct device *dev)
{
	struct vbox_private *vbox = dev_get_drvdata(dev);

	return drm_mode_config_helper_resume(&vbox->ddev);
}

static int vbox_pm_poweroff(struct device *dev)
{
	struct vbox_private *vbox = dev_get_drvdata(dev);

	return drm_mode_config_helper_suspend(&vbox->ddev);
}

static const struct dev_pm_ops vbox_pm_ops = {
	.suspend = vbox_pm_suspend,
	.resume = vbox_pm_resume,
	.freeze = vbox_pm_freeze,
	.thaw = vbox_pm_thaw,
	.poweroff = vbox_pm_poweroff,
	.restore = vbox_pm_resume,
};

static struct pci_driver vbox_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = pciidlist,
	.probe = vbox_pci_probe,
	.remove = vbox_pci_remove,
	.shutdown = vbox_pci_shutdown,
	.driver.pm = pm_sleep_ptr(&vbox_pm_ops),
};

DEFINE_DRM_GEM_FOPS(vbox_fops);

static const struct drm_driver driver = {
	.driver_features =
	    DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC | DRIVER_CURSOR_HOTSPOT,

	.fops = &vbox_fops,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,

	DRM_GEM_VRAM_DRIVER,
	DRM_FBDEV_TTM_DRIVER_OPS,
};

drm_module_pci_driver_if_modeset(vbox_pci_driver, vbox_modeset);

MODULE_AUTHOR("Oracle Corporation");
MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
