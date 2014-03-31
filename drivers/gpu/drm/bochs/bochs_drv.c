/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "bochs.h"

static bool enable_fbdev = true;
module_param_named(fbdev, enable_fbdev, bool, 0444);
MODULE_PARM_DESC(fbdev, "register fbdev device");

/* ---------------------------------------------------------------------- */
/* drm interface                                                          */

static int bochs_unload(struct drm_device *dev)
{
	struct bochs_device *bochs = dev->dev_private;

	bochs_fbdev_fini(bochs);
	bochs_kms_fini(bochs);
	bochs_mm_fini(bochs);
	bochs_hw_fini(dev);
	kfree(bochs);
	dev->dev_private = NULL;
	return 0;
}

static int bochs_load(struct drm_device *dev, unsigned long flags)
{
	struct bochs_device *bochs;
	int ret;

	bochs = kzalloc(sizeof(*bochs), GFP_KERNEL);
	if (bochs == NULL)
		return -ENOMEM;
	dev->dev_private = bochs;
	bochs->dev = dev;

	ret = bochs_hw_init(dev, flags);
	if (ret)
		goto err;

	ret = bochs_mm_init(bochs);
	if (ret)
		goto err;

	ret = bochs_kms_init(bochs);
	if (ret)
		goto err;

	if (enable_fbdev)
		bochs_fbdev_init(bochs);

	return 0;

err:
	bochs_unload(dev);
	return ret;
}

static const struct file_operations bochs_fops = {
	.owner		= THIS_MODULE,
	.open		= drm_open,
	.release	= drm_release,
	.unlocked_ioctl	= drm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= drm_compat_ioctl,
#endif
	.poll		= drm_poll,
	.read		= drm_read,
	.llseek		= no_llseek,
	.mmap           = bochs_mmap,
};

static struct drm_driver bochs_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET,
	.load			= bochs_load,
	.unload			= bochs_unload,
	.fops			= &bochs_fops,
	.name			= "bochs-drm",
	.desc			= "bochs dispi vga interface (qemu stdvga)",
	.date			= "20130925",
	.major			= 1,
	.minor			= 0,
	.gem_free_object        = bochs_gem_free_object,
	.dumb_create            = bochs_dumb_create,
	.dumb_map_offset        = bochs_dumb_mmap_offset,
	.dumb_destroy           = drm_gem_dumb_destroy,
};

/* ---------------------------------------------------------------------- */
/* pci interface                                                          */

static int bochs_kick_out_firmware_fb(struct pci_dev *pdev)
{
	struct apertures_struct *ap;

	ap = alloc_apertures(1);
	if (!ap)
		return -ENOMEM;

	ap->ranges[0].base = pci_resource_start(pdev, 0);
	ap->ranges[0].size = pci_resource_len(pdev, 0);
	remove_conflicting_framebuffers(ap, "bochsdrmfb", false);
	kfree(ap);

	return 0;
}

static int bochs_pci_probe(struct pci_dev *pdev,
			   const struct pci_device_id *ent)
{
	int ret;

	ret = bochs_kick_out_firmware_fb(pdev);
	if (ret)
		return ret;

	return drm_get_pci_dev(pdev, ent, &bochs_driver);
}

static void bochs_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	drm_put_dev(dev);
}

static DEFINE_PCI_DEVICE_TABLE(bochs_pci_tbl) = {
	{
		.vendor      = 0x1234,
		.device      = 0x1111,
		.subvendor   = 0x1af4,
		.subdevice   = 0x1100,
		.driver_data = BOCHS_QEMU_STDVGA,
	},
	{
		.vendor      = 0x1234,
		.device      = 0x1111,
		.subvendor   = PCI_ANY_ID,
		.subdevice   = PCI_ANY_ID,
		.driver_data = BOCHS_UNKNOWN,
	},
	{ /* end of list */ }
};

static struct pci_driver bochs_pci_driver = {
	.name =		"bochs-drm",
	.id_table =	bochs_pci_tbl,
	.probe =	bochs_pci_probe,
	.remove =	bochs_pci_remove,
};

/* ---------------------------------------------------------------------- */
/* module init/exit                                                       */

static int __init bochs_init(void)
{
	return drm_pci_init(&bochs_driver, &bochs_pci_driver);
}

static void __exit bochs_exit(void)
{
	drm_pci_exit(&bochs_driver, &bochs_pci_driver);
}

module_init(bochs_init);
module_exit(bochs_exit);

MODULE_DEVICE_TABLE(pci, bochs_pci_tbl);
MODULE_AUTHOR("Gerd Hoffmann <kraxel@redhat.com>");
MODULE_LICENSE("GPL");
