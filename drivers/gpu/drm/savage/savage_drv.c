/* savage_drv.c -- Savage driver for Linux
 *
 * Copyright 2004  Felix Kuehling
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT. IN NO EVENT SHALL FELIX KUEHLING BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/module.h>

#include <drm/drmP.h>
#include <drm/savage_drm.h>
#include "savage_drv.h"

#include <drm/drm_pciids.h>

static struct pci_device_id pciidlist[] = {
	savage_PCI_IDS
};

static const struct file_operations savage_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.mmap = drm_legacy_mmap,
	.poll = drm_poll,
#ifdef CONFIG_COMPAT
	.compat_ioctl = drm_compat_ioctl,
#endif
	.llseek = noop_llseek,
};

static struct drm_driver driver = {
	.driver_features =
	    DRIVER_USE_AGP | DRIVER_HAVE_DMA | DRIVER_PCI_DMA | DRIVER_LEGACY,
	.dev_priv_size = sizeof(drm_savage_buf_priv_t),
	.load = savage_driver_load,
	.firstopen = savage_driver_firstopen,
	.preclose = savage_reclaim_buffers,
	.lastclose = savage_driver_lastclose,
	.unload = savage_driver_unload,
	.set_busid = drm_pci_set_busid,
	.ioctls = savage_ioctls,
	.dma_ioctl = savage_bci_buffers,
	.fops = &savage_driver_fops,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

static struct pci_driver savage_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = pciidlist,
};

static int __init savage_init(void)
{
	driver.num_ioctls = savage_max_ioctl;
	return drm_pci_init(&driver, &savage_pci_driver);
}

static void __exit savage_exit(void)
{
	drm_pci_exit(&driver, &savage_pci_driver);
}

module_init(savage_init);
module_exit(savage_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
