/* sis.c -- sis driver -*- linux-c -*-
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
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#include <linux/config.h>
#include "drmP.h"
#include "sis_drm.h"
#include "sis_drv.h"

#include "drm_pciids.h"

static int postinit(struct drm_device *dev, unsigned long flags)
{
	DRM_INFO("Initialized %s %d.%d.%d %s on minor %d: %s\n",
		 DRIVER_NAME,
		 DRIVER_MAJOR,
		 DRIVER_MINOR,
		 DRIVER_PATCHLEVEL,
		 DRIVER_DATE, dev->primary.minor, pci_pretty_name(dev->pdev)
	    );
	return 0;
}

static int version(drm_version_t * version)
{
	int len;

	version->version_major = DRIVER_MAJOR;
	version->version_minor = DRIVER_MINOR;
	version->version_patchlevel = DRIVER_PATCHLEVEL;
	DRM_COPY(version->name, DRIVER_NAME);
	DRM_COPY(version->date, DRIVER_DATE);
	DRM_COPY(version->desc, DRIVER_DESC);
	return 0;
}

static struct pci_device_id pciidlist[] = {
	sisdrv_PCI_IDS
};

static struct drm_driver driver = {
	.driver_features = DRIVER_USE_AGP | DRIVER_USE_MTRR,
	.context_ctor = sis_init_context,
	.context_dtor = sis_final_context,
	.reclaim_buffers = drm_core_reclaim_buffers,
	.get_map_ofs = drm_core_get_map_ofs,
	.get_reg_ofs = drm_core_get_reg_ofs,
	.postinit = postinit,
	.version = version,
	.ioctls = sis_ioctls,
	.fops = {
		 .owner = THIS_MODULE,
		 .open = drm_open,
		 .release = drm_release,
		 .ioctl = drm_ioctl,
		 .mmap = drm_mmap,
		 .poll = drm_poll,
		 .fasync = drm_fasync,
		 },
	.pci_driver = {
		       .name = DRIVER_NAME,
		       .id_table = pciidlist,
		       }
};

static int __init sis_init(void)
{
	driver.num_ioctls = sis_max_ioctl;
	return drm_init(&driver);
}

static void __exit sis_exit(void)
{
	drm_exit(&driver);
}

module_init(sis_init);
module_exit(sis_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
