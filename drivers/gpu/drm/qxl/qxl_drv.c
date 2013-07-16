/* vim: set ts=8 sw=8 tw=78 ai noexpandtab */
/* qxl_drv.c -- QXL driver -*- linux-c -*-
 *
 * Copyright 2011 Red Hat, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Dave Airlie <airlie@redhat.com>
 *    Alon Levy <alevy@redhat.com>
 */

#include <linux/module.h>
#include <linux/console.h>

#include "drmP.h"
#include "drm/drm.h"

#include "qxl_drv.h"

extern int qxl_max_ioctls;
static DEFINE_PCI_DEVICE_TABLE(pciidlist) = {
	{ 0x1b36, 0x100, PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_DISPLAY_VGA << 8,
	  0xffff00, 0 },
	{ 0x1b36, 0x100, PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_DISPLAY_OTHER << 8,
	  0xffff00, 0 },
	{ 0, 0, 0 },
};
MODULE_DEVICE_TABLE(pci, pciidlist);

static int qxl_modeset = -1;

MODULE_PARM_DESC(modeset, "Disable/Enable modesetting");
module_param_named(modeset, qxl_modeset, int, 0400);

static struct drm_driver qxl_driver;
static struct pci_driver qxl_pci_driver;

static int
qxl_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	if (pdev->revision < 4) {
		DRM_ERROR("qxl too old, doesn't support client_monitors_config,"
			  " use xf86-video-qxl in user mode");
		return -EINVAL; /* TODO: ENODEV ? */
	}
	return drm_get_pci_dev(pdev, ent, &qxl_driver);
}

static void
qxl_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	drm_put_dev(dev);
}

static struct pci_driver qxl_pci_driver = {
	 .name = DRIVER_NAME,
	 .id_table = pciidlist,
	 .probe = qxl_pci_probe,
	 .remove = qxl_pci_remove,
};

static const struct file_operations qxl_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.poll = drm_poll,
	.fasync = drm_fasync,
	.mmap = qxl_mmap,
};

static struct drm_driver qxl_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET |
			   DRIVER_HAVE_IRQ | DRIVER_IRQ_SHARED,
	.dev_priv_size = 0,
	.load = qxl_driver_load,
	.unload = qxl_driver_unload,

	.dumb_create = qxl_mode_dumb_create,
	.dumb_map_offset = qxl_mode_dumb_mmap,
	.dumb_destroy = drm_gem_dumb_destroy,
#if defined(CONFIG_DEBUG_FS)
	.debugfs_init = qxl_debugfs_init,
	.debugfs_cleanup = qxl_debugfs_takedown,
#endif
	.gem_init_object = qxl_gem_object_init,
	.gem_free_object = qxl_gem_object_free,
	.gem_open_object = qxl_gem_object_open,
	.gem_close_object = qxl_gem_object_close,
	.fops = &qxl_fops,
	.ioctls = qxl_ioctls,
	.irq_handler = qxl_irq_handler,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = 0,
	.minor = 1,
	.patchlevel = 0,
};

static int __init qxl_init(void)
{
#ifdef CONFIG_VGA_CONSOLE
	if (vgacon_text_force() && qxl_modeset == -1)
		return -EINVAL;
#endif

	if (qxl_modeset == 0)
		return -EINVAL;
	qxl_driver.num_ioctls = qxl_max_ioctls;
	return drm_pci_init(&qxl_driver, &qxl_pci_driver);
}

static void __exit qxl_exit(void)
{
	drm_pci_exit(&qxl_driver, &qxl_pci_driver);
}

module_init(qxl_init);
module_exit(qxl_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
