// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2012 Red Hat
 *
 * Authors: Matthew Garrett
 *          Dave Airlie
 */

#include <linux/console.h>
#include <linux/module.h>
#include <linux/pci.h>

#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_ioctl.h>
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

int mgag200_hw_bug_no_startadd = -1;
MODULE_PARM_DESC(modeset, "HW does not interpret scanout-buffer start address correctly");
module_param_named(hw_bug_no_startadd, mgag200_hw_bug_no_startadd, int, 0400);

static struct drm_driver driver;

static const struct pci_device_id pciidlist[] = {
	{ PCI_VENDOR_ID_MATROX, 0x522, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		G200_SE_A | MGAG200_FLAG_HW_BUG_NO_STARTADD},
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
	struct drm_device *dev;
	int ret;

	drm_fb_helper_remove_conflicting_pci_framebuffers(pdev, "mgag200drmfb");

	ret = pci_enable_device(pdev);
	if (ret)
		return ret;

	dev = drm_dev_alloc(&driver, &pdev->dev);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto err_pci_disable_device;
	}

	dev->pdev = pdev;
	pci_set_drvdata(pdev, dev);

	ret = mgag200_driver_load(dev, ent->driver_data);
	if (ret)
		goto err_drm_dev_put;

	ret = drm_dev_register(dev, ent->driver_data);
	if (ret)
		goto err_mgag200_driver_unload;

	return 0;

err_mgag200_driver_unload:
	mgag200_driver_unload(dev);
err_drm_dev_put:
	drm_dev_put(dev);
err_pci_disable_device:
	pci_disable_device(pdev);
	return ret;
}

static void mga_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	drm_dev_unregister(dev);
	mgag200_driver_unload(dev);
	drm_dev_put(dev);
}

DEFINE_DRM_GEM_FOPS(mgag200_driver_fops);

static bool mgag200_pin_bo_at_0(const struct mga_device *mdev)
{
	if (mgag200_hw_bug_no_startadd > 0) {
		DRM_WARN_ONCE("Option hw_bug_no_startradd is enabled. Please "
			      "report the output of 'lspci -vvnn' to "
			      "<dri-devel@lists.freedesktop.org> if this "
			      "option is required to make mgag200 work "
			      "correctly on your system.\n");
		return true;
	} else if (!mgag200_hw_bug_no_startadd) {
		return false;
	}
	return mdev->flags & MGAG200_FLAG_HW_BUG_NO_STARTADD;
}

int mgag200_driver_dumb_create(struct drm_file *file,
			       struct drm_device *dev,
			       struct drm_mode_create_dumb *args)
{
	struct mga_device *mdev = dev->dev_private;
	unsigned long pg_align;

	if (WARN_ONCE(!dev->vram_mm, "VRAM MM not initialized"))
		return -EINVAL;

	pg_align = 0ul;

	/*
	 * Aligning scanout buffers to the size of the video ram forces
	 * placement at offset 0. Works around a bug where HW does not
	 * respect 'startadd' field.
	 */
	if (mgag200_pin_bo_at_0(mdev))
		pg_align = PFN_UP(mdev->mc.vram_size);

	return drm_gem_vram_fill_create_dumb(file, dev, pg_align, 0, args);
}

static struct drm_driver driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET,
	.fops = &mgag200_driver_fops,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
	.debugfs_init = drm_vram_mm_debugfs_init,
	.dumb_create = mgag200_driver_dumb_create,
	.dumb_map_offset = drm_gem_vram_driver_dumb_mmap_offset,
	.gem_prime_mmap = drm_gem_prime_mmap,
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
