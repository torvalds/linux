// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2012 Red Hat
 *
 * Authors: Matthew Garrett
 *          Dave Airlie
 */

#include <linux/module.h>
#include <linux/console.h>

#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_pci.h>
#include <drm/drm_pciids.h>

#include "mgag200_drv.h"

/*
 * This is the generic driver code. This binds the driver to the drm core,
 * which then performs further device association and calls our graphics init
 * functions
 */
int mgag200_modeset = -1;

MODULE_PARM_DESC(modeset, "Disable/Enable modesetting");
module_param_named(modeset, mgag200_modeset, int, 0400);

static struct drm_driver driver;

static const struct pci_device_id pciidlist[] = {
	{ PCI_VENDOR_ID_MATROX, 0x522, PCI_ANY_ID, PCI_ANY_ID, 0, 0, G200_SE_A },
	{ PCI_VENDOR_ID_MATROX, 0x524, PCI_ANY_ID, PCI_ANY_ID, 0, 0, G200_SE_B },
	{ PCI_VENDOR_ID_MATROX, 0x530, PCI_ANY_ID, PCI_ANY_ID, 0, 0, G200_EV },
	{ PCI_VENDOR_ID_MATROX, 0x532, PCI_ANY_ID, PCI_ANY_ID, 0, 0, G200_WB },
	{ PCI_VENDOR_ID_MATROX, 0x533, PCI_ANY_ID, PCI_ANY_ID, 0, 0, G200_EH },
	{ PCI_VENDOR_ID_MATROX, 0x534, PCI_ANY_ID, PCI_ANY_ID, 0, 0, G200_ER },
	{ PCI_VENDOR_ID_MATROX, 0x536, PCI_ANY_ID, PCI_ANY_ID, 0, 0, G200_EW3 },
	{ PCI_VENDOR_ID_MATROX, 0x538, PCI_ANY_ID, PCI_ANY_ID, 0, 0, G200_EH3 },
	{0,}
};

MODULE_DEVICE_TABLE(pci, pciidlist);


static int mga_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	drm_fb_helper_remove_conflicting_pci_framebuffers(pdev, "mgag200drmfb");

	return drm_get_pci_dev(pdev, ent, &driver);
}

static void mga_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	drm_put_dev(dev);
}

static const struct file_operations mgag200_driver_fops = {
	.owner = THIS_MODULE,
	DRM_VRAM_MM_FILE_OPERATIONS
};

static struct drm_driver driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET,
	.load = mgag200_driver_load,
	.unload = mgag200_driver_unload,
	.fops = &mgag200_driver_fops,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
	DRM_GEM_VRAM_DRIVER
};

static struct pci_driver mgag200_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = pciidlist,
	.probe = mga_pci_probe,
	.remove = mga_pci_remove,
};

static int __init mgag200_init(void)
{
	if (vgacon_text_force() && mgag200_modeset == -1)
		return -EINVAL;

	if (mgag200_modeset == 0)
		return -EINVAL;

	return pci_register_driver(&mgag200_pci_driver);
}

static void __exit mgag200_exit(void)
{
	pci_unregister_driver(&mgag200_pci_driver);
}

module_init(mgag200_init);
module_exit(mgag200_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
