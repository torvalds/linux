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
#include "drmP.h"
#include "drm.h"

#include "cirrus_drv.h"

int cirrus_modeset = -1;

MODULE_PARM_DESC(modeset, "Disable/Enable modesetting");
module_param_named(modeset, cirrus_modeset, int, 0400);

/*
 * This is the generic driver code. This binds the driver to the drm core,
 * which then performs further device association and calls our graphics init
 * functions
 */

static struct drm_driver driver;

/* only bind to the cirrus chip in qemu */
static DEFINE_PCI_DEVICE_TABLE(pciidlist) = {
	{ PCI_VENDOR_ID_CIRRUS, PCI_DEVICE_ID_CIRRUS_5446, 0x1af4, 0x1100, 0,
	  0, 0 },
	{0,}
};

static int __devinit
cirrus_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	return drm_get_pci_dev(pdev, ent, &driver);
}

static void cirrus_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	drm_put_dev(dev);
}

static const struct file_operations cirrus_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.mmap = cirrus_mmap,
	.poll = drm_poll,
	.fasync = drm_fasync,
};
static struct drm_driver driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_USE_MTRR,
	.load = cirrus_driver_load,
	.unload = cirrus_driver_unload,
	.fops = &cirrus_driver_fops,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
	.gem_init_object = cirrus_gem_init_object,
	.gem_free_object = cirrus_gem_free_object,
	.dumb_create = cirrus_dumb_create,
	.dumb_map_offset = cirrus_dumb_mmap_offset,
	.dumb_destroy = cirrus_dumb_destroy,
};

static struct pci_driver cirrus_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = pciidlist,
	.probe = cirrus_pci_probe,
	.remove = cirrus_pci_remove,
};

static int __init cirrus_init(void)
{
	if (vgacon_text_force() && cirrus_modeset == -1)
		return -EINVAL;

	if (cirrus_modeset == 0)
		return -EINVAL;
	return drm_pci_init(&driver, &cirrus_pci_driver);
}

static void __exit cirrus_exit(void)
{
	drm_pci_exit(&driver, &cirrus_pci_driver);
}

module_init(cirrus_init);
module_exit(cirrus_exit);

MODULE_DEVICE_TABLE(pci, pciidlist);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
