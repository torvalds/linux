/* r128_drv.c -- ATI Rage 128 driver -*- linux-c -*-
 * Created: Mon Dec 13 09:47:27 1999 by faith@precisioninsight.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
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
 *    Rickard E. (Rik) Faith <faith@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 */

#include <linux/module.h>

#include <drm/drmP.h>
#include <drm/r128_drm.h>
#include "r128_drv.h"

#include <drm/drm_pciids.h>

static struct pci_device_id pciidlist[] = {
	r128_PCI_IDS
};

static const struct file_operations r128_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.mmap = drm_mmap,
	.poll = drm_poll,
#ifdef CONFIG_COMPAT
	.compat_ioctl = r128_compat_ioctl,
#endif
	.llseek = noop_llseek,
};

static struct drm_driver driver = {
	.driver_features =
	    DRIVER_USE_AGP | DRIVER_PCI_DMA | DRIVER_SG |
	    DRIVER_HAVE_DMA | DRIVER_HAVE_IRQ | DRIVER_IRQ_SHARED,
	.dev_priv_size = sizeof(drm_r128_buf_priv_t),
	.load = r128_driver_load,
	.preclose = r128_driver_preclose,
	.lastclose = r128_driver_lastclose,
	.get_vblank_counter = r128_get_vblank_counter,
	.enable_vblank = r128_enable_vblank,
	.disable_vblank = r128_disable_vblank,
	.irq_preinstall = r128_driver_irq_preinstall,
	.irq_postinstall = r128_driver_irq_postinstall,
	.irq_uninstall = r128_driver_irq_uninstall,
	.irq_handler = r128_driver_irq_handler,
	.ioctls = r128_ioctls,
	.dma_ioctl = r128_cce_buffers,
	.fops = &r128_driver_fops,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

int r128_driver_load(struct drm_device *dev, unsigned long flags)
{
	pci_set_master(dev->pdev);
	return drm_vblank_init(dev, 1);
}

static struct pci_driver r128_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = pciidlist,
};

static int __init r128_init(void)
{
	driver.num_ioctls = r128_max_ioctl;

	return drm_pci_init(&driver, &r128_pci_driver);
}

static void __exit r128_exit(void)
{
	drm_pci_exit(&driver, &r128_pci_driver);
}

module_init(r128_init);
module_exit(r128_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
