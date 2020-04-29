/* mga_drv.c -- Matrox G200/G400 driver -*- linux-c -*-
 * Created: Mon Dec 13 01:56:22 1999 by jhartmann@precisioninsight.com
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

#include <drm/drm_drv.h>
#include <drm/drm_pciids.h>

#include "mga_drv.h"

static struct pci_device_id pciidlist[] = {
	mga_PCI_IDS
};

static const struct file_operations mga_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.mmap = drm_legacy_mmap,
	.poll = drm_poll,
#ifdef CONFIG_COMPAT
	.compat_ioctl = mga_compat_ioctl,
#endif
	.llseek = noop_llseek,
};

static struct drm_driver driver = {
	.driver_features =
	    DRIVER_USE_AGP | DRIVER_PCI_DMA | DRIVER_LEGACY |
	    DRIVER_HAVE_DMA | DRIVER_HAVE_IRQ,
	.dev_priv_size = sizeof(drm_mga_buf_priv_t),
	.load = mga_driver_load,
	.unload = mga_driver_unload,
	.lastclose = mga_driver_lastclose,
	.dma_quiescent = mga_driver_dma_quiescent,
	.get_vblank_counter = mga_get_vblank_counter,
	.enable_vblank = mga_enable_vblank,
	.disable_vblank = mga_disable_vblank,
	.irq_preinstall = mga_driver_irq_preinstall,
	.irq_postinstall = mga_driver_irq_postinstall,
	.irq_uninstall = mga_driver_irq_uninstall,
	.irq_handler = mga_driver_irq_handler,
	.ioctls = mga_ioctls,
	.dma_ioctl = mga_dma_buffers,
	.fops = &mga_driver_fops,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

static struct pci_driver mga_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = pciidlist,
};

static int __init mga_init(void)
{
	driver.num_ioctls = mga_max_ioctl;
	return drm_legacy_pci_init(&driver, &mga_pci_driver);
}

static void __exit mga_exit(void)
{
	drm_legacy_pci_exit(&driver, &mga_pci_driver);
}

module_init(mga_init);
module_exit(mga_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
