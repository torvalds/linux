// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2021 Microsoft
 */

#include <linux/efi.h>
#include <linux/hyperv.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/screen_info.h>

#include <drm/drm_aperture.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_generic.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include "hyperv_drm.h"

#define DRIVER_NAME "hyperv_drm"
#define DRIVER_DESC "DRM driver for Hyper-V synthetic video device"
#define DRIVER_DATE "2020"
#define DRIVER_MAJOR 1
#define DRIVER_MINOR 0

DEFINE_DRM_GEM_FOPS(hv_fops);

static struct drm_driver hyperv_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,

	.name		 = DRIVER_NAME,
	.desc		 = DRIVER_DESC,
	.date		 = DRIVER_DATE,
	.major		 = DRIVER_MAJOR,
	.minor		 = DRIVER_MINOR,

	.fops		 = &hv_fops,
	DRM_GEM_SHMEM_DRIVER_OPS,
};

static int hyperv_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *ent)
{
	return 0;
}

static void hyperv_pci_remove(struct pci_dev *pdev)
{
}

static const struct pci_device_id hyperv_pci_tbl[] = {
	{
		.vendor = PCI_VENDOR_ID_MICROSOFT,
		.device = PCI_DEVICE_ID_HYPERV_VIDEO,
	},
	{ /* end of list */ }
};

/*
 * PCI stub to support gen1 VM.
 */
static struct pci_driver hyperv_pci_driver = {
	.name =		KBUILD_MODNAME,
	.id_table =	hyperv_pci_tbl,
	.probe =	hyperv_pci_probe,
	.remove =	hyperv_pci_remove,
};

static int hyperv_setup_vram(struct hyperv_drm_device *hv,
			     struct hv_device *hdev)
{
	struct drm_device *dev = &hv->dev;
	int ret;

	drm_aperture_remove_conflicting_framebuffers(screen_info.lfb_base,
						     screen_info.lfb_size,
						     &hyperv_driver);

	hv->fb_size = (unsigned long)hv->mmio_megabytes * 1024 * 1024;

	ret = vmbus_allocate_mmio(&hv->mem, hdev, 0, -1, hv->fb_size, 0x100000,
				  true);
	if (ret) {
		drm_err(dev, "Failed to allocate mmio\n");
		return -ENOMEM;
	}

	/*
	 * Map the VRAM cacheable for performance. This is also required for VM
	 * connect to display properly for ARM64 Linux VM, as the host also maps
	 * the VRAM cacheable.
	 */
	hv->vram = ioremap_cache(hv->mem->start, hv->fb_size);
	if (!hv->vram) {
		drm_err(dev, "Failed to map vram\n");
		ret = -ENOMEM;
		goto error;
	}

	hv->fb_base = hv->mem->start;
	return 0;

error:
	vmbus_free_mmio(hv->mem->start, hv->fb_size);
	return ret;
}

static int hyperv_vmbus_probe(struct hv_device *hdev,
			      const struct hv_vmbus_device_id *dev_id)
{
	struct hyperv_drm_device *hv;
	struct drm_device *dev;
	int ret;

	hv = devm_drm_dev_alloc(&hdev->device, &hyperv_driver,
				struct hyperv_drm_device, dev);
	if (IS_ERR(hv))
		return PTR_ERR(hv);

	dev = &hv->dev;
	init_completion(&hv->wait);
	hv_set_drvdata(hdev, hv);
	hv->hdev = hdev;

	ret = hyperv_connect_vsp(hdev);
	if (ret) {
		drm_err(dev, "Failed to connect to vmbus.\n");
		goto err_hv_set_drv_data;
	}

	ret = hyperv_setup_vram(hv, hdev);
	if (ret)
		goto err_vmbus_close;

	/*
	 * Should be done only once during init and resume. Failing to update
	 * vram location is not fatal. Device will update dirty area till
	 * preferred resolution only.
	 */
	ret = hyperv_update_vram_location(hdev, hv->fb_base);
	if (ret)
		drm_warn(dev, "Failed to update vram location.\n");

	ret = hyperv_mode_config_init(hv);
	if (ret)
		goto err_free_mmio;

	ret = drm_dev_register(dev, 0);
	if (ret) {
		drm_err(dev, "Failed to register drm driver.\n");
		goto err_free_mmio;
	}

	drm_fbdev_generic_setup(dev, 0);

	return 0;

err_free_mmio:
	vmbus_free_mmio(hv->mem->start, hv->fb_size);
err_vmbus_close:
	vmbus_close(hdev->channel);
err_hv_set_drv_data:
	hv_set_drvdata(hdev, NULL);
	return ret;
}

static void hyperv_vmbus_remove(struct hv_device *hdev)
{
	struct drm_device *dev = hv_get_drvdata(hdev);
	struct hyperv_drm_device *hv = to_hv(dev);

	drm_dev_unplug(dev);
	drm_atomic_helper_shutdown(dev);
	vmbus_close(hdev->channel);
	hv_set_drvdata(hdev, NULL);

	vmbus_free_mmio(hv->mem->start, hv->fb_size);
}

static void hyperv_vmbus_shutdown(struct hv_device *hdev)
{
	drm_atomic_helper_shutdown(hv_get_drvdata(hdev));
}

static int hyperv_vmbus_suspend(struct hv_device *hdev)
{
	struct drm_device *dev = hv_get_drvdata(hdev);
	int ret;

	ret = drm_mode_config_helper_suspend(dev);
	if (ret)
		return ret;

	vmbus_close(hdev->channel);

	return 0;
}

static int hyperv_vmbus_resume(struct hv_device *hdev)
{
	struct drm_device *dev = hv_get_drvdata(hdev);
	struct hyperv_drm_device *hv = to_hv(dev);
	int ret;

	ret = hyperv_connect_vsp(hdev);
	if (ret)
		return ret;

	ret = hyperv_update_vram_location(hdev, hv->fb_base);
	if (ret)
		return ret;

	return drm_mode_config_helper_resume(dev);
}

static const struct hv_vmbus_device_id hyperv_vmbus_tbl[] = {
	/* Synthetic Video Device GUID */
	{HV_SYNTHVID_GUID},
	{}
};

static struct hv_driver hyperv_hv_driver = {
	.name = KBUILD_MODNAME,
	.id_table = hyperv_vmbus_tbl,
	.probe = hyperv_vmbus_probe,
	.remove = hyperv_vmbus_remove,
	.shutdown = hyperv_vmbus_shutdown,
	.suspend = hyperv_vmbus_suspend,
	.resume = hyperv_vmbus_resume,
	.driver = {
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};

static int __init hyperv_init(void)
{
	int ret;

	if (drm_firmware_drivers_only())
		return -ENODEV;

	ret = pci_register_driver(&hyperv_pci_driver);
	if (ret != 0)
		return ret;

	return vmbus_driver_register(&hyperv_hv_driver);
}

static void __exit hyperv_exit(void)
{
	vmbus_driver_unregister(&hyperv_hv_driver);
	pci_unregister_driver(&hyperv_pci_driver);
}

module_init(hyperv_init);
module_exit(hyperv_exit);

MODULE_DEVICE_TABLE(pci, hyperv_pci_tbl);
MODULE_DEVICE_TABLE(vmbus, hyperv_vmbus_tbl);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Deepak Rawat <drawat.floss@gmail.com>");
MODULE_DESCRIPTION("DRM driver for Hyper-V synthetic video device");
