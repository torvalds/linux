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
#include "drm_crtc_helper.h"
#include "qxl_drv.h"
#include "qxl_object.h"

extern int qxl_max_ioctls;
static const struct pci_device_id pciidlist[] = {
	{ 0x1b36, 0x100, PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_DISPLAY_VGA << 8,
	  0xffff00, 0 },
	{ 0x1b36, 0x100, PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_DISPLAY_OTHER << 8,
	  0xffff00, 0 },
	{ 0, 0, 0 },
};
MODULE_DEVICE_TABLE(pci, pciidlist);

static int qxl_modeset = -1;
int qxl_num_crtc = 4;

MODULE_PARM_DESC(modeset, "Disable/Enable modesetting");
module_param_named(modeset, qxl_modeset, int, 0400);

MODULE_PARM_DESC(num_heads, "Number of virtual crtcs to expose (default 4)");
module_param_named(num_heads, qxl_num_crtc, int, 0400);

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

static const struct file_operations qxl_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.poll = drm_poll,
	.read = drm_read,
	.mmap = qxl_mmap,
};

static int qxl_drm_freeze(struct drm_device *dev)
{
	struct pci_dev *pdev = dev->pdev;
	struct qxl_device *qdev = dev->dev_private;
	struct drm_crtc *crtc;

	drm_kms_helper_poll_disable(dev);

	console_lock();
	qxl_fbdev_set_suspend(qdev, 1);
	console_unlock();

	/* unpin the front buffers */
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		const struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;
		if (crtc->enabled)
			(*crtc_funcs->disable)(crtc);
	}

	qxl_destroy_monitors_object(qdev);
	qxl_surf_evict(qdev);
	qxl_vram_evict(qdev);

	while (!qxl_check_idle(qdev->command_ring));
	while (!qxl_check_idle(qdev->release_ring))
		qxl_queue_garbage_collect(qdev, 1);

	pci_save_state(pdev);

	return 0;
}

static int qxl_drm_resume(struct drm_device *dev, bool thaw)
{
	struct qxl_device *qdev = dev->dev_private;

	qdev->ram_header->int_mask = QXL_INTERRUPT_MASK;
	if (!thaw) {
		qxl_reinit_memslots(qdev);
		qxl_ring_init_hdr(qdev->release_ring);
	}

	qxl_create_monitors_object(qdev);
	drm_helper_resume_force_mode(dev);

	console_lock();
	qxl_fbdev_set_suspend(qdev, 0);
	console_unlock();

	drm_kms_helper_poll_enable(dev);
	return 0;
}

static int qxl_pm_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	int error;

	error = qxl_drm_freeze(drm_dev);
	if (error)
		return error;

	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3hot);
	return 0;
}

static int qxl_pm_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	if (pci_enable_device(pdev)) {
		return -EIO;
	}

	return qxl_drm_resume(drm_dev, false);
}

static int qxl_pm_thaw(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);

	return qxl_drm_resume(drm_dev, true);
}

static int qxl_pm_freeze(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);

	return qxl_drm_freeze(drm_dev);
}

static int qxl_pm_restore(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	struct qxl_device *qdev = drm_dev->dev_private;

	qxl_io_reset(qdev);
	return qxl_drm_resume(drm_dev, false);
}

static u32 qxl_noop_get_vblank_counter(struct drm_device *dev,
				       unsigned int pipe)
{
	return 0;
}

static int qxl_noop_enable_vblank(struct drm_device *dev, unsigned int pipe)
{
	return 0;
}

static void qxl_noop_disable_vblank(struct drm_device *dev, unsigned int pipe)
{
}

static const struct dev_pm_ops qxl_pm_ops = {
	.suspend = qxl_pm_suspend,
	.resume = qxl_pm_resume,
	.freeze = qxl_pm_freeze,
	.thaw = qxl_pm_thaw,
	.poweroff = qxl_pm_freeze,
	.restore = qxl_pm_restore,
};
static struct pci_driver qxl_pci_driver = {
	 .name = DRIVER_NAME,
	 .id_table = pciidlist,
	 .probe = qxl_pci_probe,
	 .remove = qxl_pci_remove,
	 .driver.pm = &qxl_pm_ops,
};

static struct drm_driver qxl_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_PRIME |
			   DRIVER_HAVE_IRQ | DRIVER_IRQ_SHARED,
	.load = qxl_driver_load,
	.unload = qxl_driver_unload,
	.get_vblank_counter = qxl_noop_get_vblank_counter,
	.enable_vblank = qxl_noop_enable_vblank,
	.disable_vblank = qxl_noop_disable_vblank,

	.set_busid = drm_pci_set_busid,

	.dumb_create = qxl_mode_dumb_create,
	.dumb_map_offset = qxl_mode_dumb_mmap,
	.dumb_destroy = drm_gem_dumb_destroy,
#if defined(CONFIG_DEBUG_FS)
	.debugfs_init = qxl_debugfs_init,
	.debugfs_cleanup = qxl_debugfs_takedown,
#endif
	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_export = drm_gem_prime_export,
	.gem_prime_import = drm_gem_prime_import,
	.gem_prime_pin = qxl_gem_prime_pin,
	.gem_prime_unpin = qxl_gem_prime_unpin,
	.gem_prime_get_sg_table = qxl_gem_prime_get_sg_table,
	.gem_prime_import_sg_table = qxl_gem_prime_import_sg_table,
	.gem_prime_vmap = qxl_gem_prime_vmap,
	.gem_prime_vunmap = qxl_gem_prime_vunmap,
	.gem_prime_mmap = qxl_gem_prime_mmap,
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
