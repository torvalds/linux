/*
 * Copyright 1998-2003 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2003 S3 Graphics, Inc. All Rights Reserved.
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * VIA, S3 GRAPHICS, AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <linux/config.h>
#include "drmP.h"
#include "via_drm.h"
#include "via_drv.h"

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
	viadrv_PCI_IDS
};

static drm_ioctl_desc_t ioctls[] = {
	[DRM_IOCTL_NR(DRM_VIA_ALLOCMEM)] = {via_mem_alloc, 1, 0},
	[DRM_IOCTL_NR(DRM_VIA_FREEMEM)] = {via_mem_free, 1, 0},
	[DRM_IOCTL_NR(DRM_VIA_AGP_INIT)] = {via_agp_init, 1, 0},
	[DRM_IOCTL_NR(DRM_VIA_FB_INIT)] = {via_fb_init, 1, 0},
	[DRM_IOCTL_NR(DRM_VIA_MAP_INIT)] = {via_map_init, 1, 0},
	[DRM_IOCTL_NR(DRM_VIA_DEC_FUTEX)] = {via_decoder_futex, 1, 0},
	[DRM_IOCTL_NR(DRM_VIA_DMA_INIT)] = {via_dma_init, 1, 0},
	[DRM_IOCTL_NR(DRM_VIA_CMDBUFFER)] = {via_cmdbuffer, 1, 0},
	[DRM_IOCTL_NR(DRM_VIA_FLUSH)] = {via_flush_ioctl, 1, 0},
	[DRM_IOCTL_NR(DRM_VIA_PCICMD)] = {via_pci_cmdbuffer, 1, 0},
	[DRM_IOCTL_NR(DRM_VIA_CMDBUF_SIZE)] = {via_cmdbuf_size, 1, 0},
	[DRM_IOCTL_NR(DRM_VIA_WAIT_IRQ)] = {via_wait_irq, 1, 0}
};

static struct drm_driver driver = {
	.driver_features =
	    DRIVER_USE_AGP | DRIVER_USE_MTRR | DRIVER_HAVE_IRQ |
	    DRIVER_IRQ_SHARED | DRIVER_IRQ_VBL,
	.context_ctor = via_init_context,
	.context_dtor = via_final_context,
	.vblank_wait = via_driver_vblank_wait,
	.irq_preinstall = via_driver_irq_preinstall,
	.irq_postinstall = via_driver_irq_postinstall,
	.irq_uninstall = via_driver_irq_uninstall,
	.irq_handler = via_driver_irq_handler,
	.dma_quiescent = via_driver_dma_quiescent,
	.reclaim_buffers = drm_core_reclaim_buffers,
	.get_map_ofs = drm_core_get_map_ofs,
	.get_reg_ofs = drm_core_get_reg_ofs,
	.postinit = postinit,
	.version = version,
	.ioctls = ioctls,
	.num_ioctls = DRM_ARRAY_SIZE(ioctls),
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

static int __init via_init(void)
{
	via_init_command_verifier();
	return drm_init(&driver);
}

static void __exit via_exit(void)
{
	drm_exit(&driver);
}

module_init(via_init);
module_exit(via_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
