/*
 * Copyright 2012 Red Hat
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

#include "mgag200_drv.h"

#include <drm/drm_pciids.h>

/*
 * This is the generic driver code. This binds the driver to the drm core,
 * which then performs further device association and calls our graphics init
 * functions
 */
int mgag200_modeset = -1;

MODULE_PARM_DESC(modeset, "Disable/Enable modesetting");
module_param_named(modeset, mgag200_modeset, int, 0400);

static struct drm_driver driver;

static DEFINE_PCI_DEVICE_TABLE(pciidlist) = {
	{ PCI_VENDOR_ID_MATROX, 0x522, PCI_ANY_ID, PCI_ANY_ID, 0, 0, G200_SE_A },
	{ PCI_VENDOR_ID_MATROX, 0x524, PCI_ANY_ID, PCI_ANY_ID, 0, 0, G200_SE_B },
	{ PCI_VENDOR_ID_MATROX, 0x530, PCI_ANY_ID, PCI_ANY_ID, 0, 0, G200_EV },
	{ PCI_VENDOR_ID_MATROX, 0x532, PCI_ANY_ID, PCI_ANY_ID, 0, 0, G200_WB },
	{ PCI_VENDOR_ID_MATROX, 0x533, PCI_ANY_ID, PCI_ANY_ID, 0, 0, G200_EH },
	{ PCI_VENDOR_ID_MATROX, 0x534, PCI_ANY_ID, PCI_ANY_ID, 0, 0, G200_ER },
	{0,}
};

MODULE_DEVICE_TABLE(pci, pciidlist);

static void mgag200_kick_out_firmware_fb(struct pci_dev *pdev)
{
	struct apertures_struct *ap;
	bool primary = false;

	ap = alloc_apertures(1);
	if (!ap)
		return;

	ap->ranges[0].base = pci_resource_start(pdev, 0);
	ap->ranges[0].size = pci_resource_len(pdev, 0);

#ifdef CONFIG_X86
	primary = pdev->resource[PCI_ROM_RESOURCE].flags & IORESOURCE_ROM_SHADOW;
#endif
	remove_conflicting_framebuffers(ap, "mgag200drmfb", primary);
	kfree(ap);
}


static int mga_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	mgag200_kick_out_firmware_fb(pdev);

	return drm_get_pci_dev(pdev, ent, &driver);
}

static void mga_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	drm_put_dev(dev);
}

static const struct file_operations mgag200_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.mmap = mgag200_mmap,
	.poll = drm_poll,
	.fasync = drm_fasync,
#ifdef CONFIG_COMPAT
	.compat_ioctl = drm_compat_ioctl,
#endif
	.read = drm_read,
};

static struct drm_driver driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_USE_MTRR,
	.load = mgag200_driver_load,
	.unload = mgag200_driver_unload,
	.fops = &mgag200_driver_fops,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,

	.gem_init_object = mgag200_gem_init_object,
	.gem_free_object = mgag200_gem_free_object,
	.dumb_create = mgag200_dumb_create,
	.dumb_map_offset = mgag200_dumb_mmap_offset,
	.dumb_destroy = drm_gem_dumb_destroy,
};

static struct pci_driver mgag200_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = pciidlist,
	.probe = mga_pci_probe,
	.remove = mga_pci_remove,
};

static int __init mgag200_init(void)
{
#ifdef CONFIG_VGA_CONSOLE
	if (vgacon_text_force() && mgag200_modeset == -1)
		return -EINVAL;
#endif

	if (mgag200_modeset == 0)
		return -EINVAL;
	return drm_pci_init(&driver, &mgag200_pci_driver);
}

static void __exit mgag200_exit(void)
{
	drm_pci_exit(&driver, &mgag200_pci_driver);
}

module_init(mgag200_init);
module_exit(mgag200_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
