/*
 * Copyright 2012 Red Hat <mjg@redhat.com>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License version 2. See the file COPYING in the main
 * directory of this archive for more details.
 *
 * Authors: Matthew Garrett
 *          Dave Airlie
 */
#include <linux/module.h>
#include <linux/console.h>
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>

#include "cirrus_drv.h"

int cirrus_modeset = -1;
int cirrus_bpp = 24;

MODULE_PARM_DESC(modeset, "Disable/Enable modesetting");
module_param_named(modeset, cirrus_modeset, int, 0400);
MODULE_PARM_DESC(bpp, "Max bits-per-pixel (default:24)");
module_param_named(bpp, cirrus_bpp, int, 0400);

/*
 * This is the generic driver code. This binds the driver to the drm core,
 * which then performs further device association and calls our graphics init
 * functions
 */

static struct drm_driver driver;

/* only bind to the cirrus chip in qemu */
static const struct pci_device_id pciidlist[] = {
	{ PCI_VENDOR_ID_CIRRUS, PCI_DEVICE_ID_CIRRUS_5446,
	  PCI_SUBVENDOR_ID_REDHAT_QUMRANET, PCI_SUBDEVICE_ID_QEMU,
	  0, 0, 0 },
	{ PCI_VENDOR_ID_CIRRUS, PCI_DEVICE_ID_CIRRUS_5446, PCI_VENDOR_ID_XEN,
	  0x0001, 0, 0, 0 },
	{0,}
};


static int cirrus_kick_out_firmware_fb(struct pci_dev *pdev)
{
	struct apertures_struct *ap;
	bool primary = false;

	ap = alloc_apertures(1);
	if (!ap)
		return -ENOMEM;

	ap->ranges[0].base = pci_resource_start(pdev, 0);
	ap->ranges[0].size = pci_resource_len(pdev, 0);

#ifdef CONFIG_X86
	primary = pdev->resource[PCI_ROM_RESOURCE].flags & IORESOURCE_ROM_SHADOW;
#endif
	drm_fb_helper_remove_conflicting_framebuffers(ap, "cirrusdrmfb", primary);
	kfree(ap);

	return 0;
}

static int cirrus_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *ent)
{
	int ret;

	ret = cirrus_kick_out_firmware_fb(pdev);
	if (ret)
		return ret;

	return drm_get_pci_dev(pdev, ent, &driver);
}

static void cirrus_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	drm_put_dev(dev);
}

#ifdef CONFIG_PM_SLEEP
static int cirrus_pm_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	struct cirrus_device *cdev = drm_dev->dev_private;

	drm_kms_helper_poll_disable(drm_dev);

	if (cdev->mode_info.gfbdev) {
		console_lock();
		drm_fb_helper_set_suspend(&cdev->mode_info.gfbdev->helper, 1);
		console_unlock();
	}

	return 0;
}

static int cirrus_pm_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	struct cirrus_device *cdev = drm_dev->dev_private;

	drm_helper_resume_force_mode(drm_dev);

	if (cdev->mode_info.gfbdev) {
		console_lock();
		drm_fb_helper_set_suspend(&cdev->mode_info.gfbdev->helper, 0);
		console_unlock();
	}

	drm_kms_helper_poll_enable(drm_dev);
	return 0;
}
#endif

static const struct file_operations cirrus_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.mmap = cirrus_mmap,
	.poll = drm_poll,
	.compat_ioctl = drm_compat_ioctl,
};
static struct drm_driver driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM,
	.load = cirrus_driver_load,
	.unload = cirrus_driver_unload,
	.fops = &cirrus_driver_fops,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
	.gem_free_object_unlocked = cirrus_gem_free_object,
	.dumb_create = cirrus_dumb_create,
	.dumb_map_offset = cirrus_dumb_mmap_offset,
};

static const struct dev_pm_ops cirrus_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(cirrus_pm_suspend,
				cirrus_pm_resume)
};

static struct pci_driver cirrus_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = pciidlist,
	.probe = cirrus_pci_probe,
	.remove = cirrus_pci_remove,
	.driver.pm = &cirrus_pm_ops,
};

static int __init cirrus_init(void)
{
	if (vgacon_text_force() && cirrus_modeset == -1)
		return -EINVAL;

	if (cirrus_modeset == 0)
		return -EINVAL;
	return pci_register_driver(&cirrus_pci_driver);
}

static void __exit cirrus_exit(void)
{
	pci_unregister_driver(&cirrus_pci_driver);
}

module_init(cirrus_init);
module_exit(cirrus_exit);

MODULE_DEVICE_TABLE(pci, pciidlist);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
