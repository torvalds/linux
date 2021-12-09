/* r128_cce.c -- ATI Rage 128 driver -*- linux-c -*-
 * Created: Wed Apr  5 19:24:19 2000 by kevin@precisioninsight.com
 */
/*
 * Copyright 2000 Precision Insight, Inc., Cedar Park, Texas.
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
 * Authors:
 *    Gareth Hughes <gareth@valinux.com>
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_legacy.h>
#include <drm/drm_print.h>
#include <drm/r128_drm.h>

#include "r128_drv.h"

#define R128_FIFO_DEBUG		0

#define FIRMWARE_NAME		"r128/r128_cce.bin"

MODULE_FIRMWARE(FIRMWARE_NAME);

static int R128_READ_PLL(struct drm_device *dev, int addr)
{
	drm_r128_private_t *dev_priv = dev->dev_private;

	R128_WRITE8(R128_CLOCK_CNTL_INDEX, addr & 0x1f);
	return R128_READ(R128_CLOCK_CNTL_DATA);
}

#if R128_FIFO_DEBUG
static void r128_status(drm_r128_private_t *dev_priv)
{
	printk("GUI_STAT           = 0x%08x\n",
	       (unsigned int)R128_READ(R128_GUI_STAT));
	printk("PM4_STAT           = 0x%08x\n",
	       (unsigned int)R128_READ(R128_PM4_STAT));
	printk("PM4_BUFFER_DL_WPTR = 0x%08x\n",
	       (unsigned int)R128_READ(R128_PM4_BUFFER_DL_WPTR));
	printk("PM4_BUFFER_DL_RPTR = 0x%08x\n",
	       (unsigned int)R128_READ(R128_PM4_BUFFER_DL_RPTR));
	printk("PM4_MICRO_CNTL     = 0x%08x\n",
	       (unsigned int)R128_READ(R128_PM4_MICRO_CNTL));
	printk("PM4_BUFFER_CNTL    = 0x%08x\n",
	       (unsigned int)R128_READ(R128_PM4_BUFFER_CNTL));
}
#endif

/* ================================================================
 * Engine, FIFO control
 */

static int r128_do_pixcache_flush(drm_r128_private_t *dev_priv)
{
	u32 tmp;
	int i;

	tmp = R128_READ(R128_PC_NGUI_CTLSTAT) | R128_PC_FLUSH_ALL;
	R128_WRITE(R128_PC_NGUI_CTLSTAT, tmp);

	for (i = 0; i < dev_priv->usec_timeout; i++) {
		if (!(R128_READ(R128_PC_NGUI_CTLSTAT) & R128_PC_BUSY))
			return 0;
		udelay(1);
	}

#if R128_FIFO_DEBUG
	DRM_ERROR("failed!\n");
#endif
	return -EBUSY;
}

static int r128_do_wait_for_fifo(drm_r128_private_t *dev_priv, int entries)
{
	int i;

	for (i = 0; i < dev_priv->usec_timeout; i++) {
		int slots = R128_READ(R128_GUI_STAT) & R128_GUI_FIFOCNT_MASK;
		if (slots >= entries)
			return 0;
		udelay(1);
	}

#if R128_FIFO_DEBUG
	DRM_ERROR("failed!\n");
#endif
	return -EBUSY;
}

static int r128_do_wait_for_idle(drm_r128_private_t *dev_priv)
{
	int i, ret;

	ret = r128_do_wait_for_fifo(dev_priv, 64);
	if (ret)
		return ret;

	for (i = 0; i < dev_priv->usec_timeout; i++) {
		if (!(R128_READ(R128_GUI_STAT) & R128_GUI_ACTIVE)) {
			r128_do_pixcache_flush(dev_priv);
			return 0;
		}
		udelay(1);
	}

#if R128_FIFO_DEBUG
	DRM_ERROR("failed!\n");
#endif
	return -EBUSY;
}

/* ================================================================
 * CCE control, initialization
 */

/* Load the microcode for the CCE */
static int r128_cce_load_microcode(drm_r128_private_t *dev_priv)
{
	struct platform_device *pdev;
	const struct firmware *fw;
	const __be32 *fw_data;
	int rc, i;

	DRM_DEBUG("\n");

	pdev = platform_device_register_simple("r128_cce", 0, NULL, 0);
	if (IS_ERR(pdev)) {
		pr_err("r128_cce: Failed to register firmware\n");
		return PTR_ERR(pdev);
	}
	rc = request_firmware(&fw, FIRMWARE_NAME, &pdev->dev);
	platform_device_unregister(pdev);
	if (rc) {
		pr_err("r128_cce: Failed to load firmware \"%s\"\n",
		       FIRMWARE_NAME);
		return rc;
	}

	if (fw->size != 256 * 8) {
		pr_err("r128_cce: Bogus length %zu in firmware \"%s\"\n",
		       fw->size, FIRMWARE_NAME);
		rc = -EINVAL;
		goto out_release;
	}

	r128_do_wait_for_idle(dev_priv);

	fw_data = (const __be32 *)fw->data;
	R128_WRITE(R128_PM4_MICROCODE_ADDR, 0);
	for (i = 0; i < 256; i++) {
		R128_WRITE(R128_PM4_MICROCODE_DATAH,
			   be32_to_cpup(&fw_data[i * 2]));
		R128_WRITE(R128_PM4_MICROCODE_DATAL,
			   be32_to_cpup(&fw_data[i * 2 + 1]));
	}

out_release:
	release_firmware(fw);
	return rc;
}

/* Flush any pending commands to the CCE.  This should only be used just
 * prior to a wait for idle, as it informs the engine that the command
 * stream is ending.
 */
static void r128_do_cce_flush(drm_r128_private_t *dev_priv)
{
	u32 tmp;

	tmp = R128_READ(R128_PM4_BUFFER_DL_WPTR) | R128_PM4_BUFFER_DL_DONE;
	R128_WRITE(R128_PM4_BUFFER_DL_WPTR, tmp);
}

/* Wait for the CCE to go idle.
 */
int r128_do_cce_idle(drm_r128_private_t *dev_priv)
{
	int i;

	for (i = 0; i < dev_priv->usec_timeout; i++) {
		if (GET_RING_HEAD(dev_priv) == dev_priv->ring.tail) {
			int pm4stat = R128_READ(R128_PM4_STAT);
			if (((pm4stat & R128_PM4_FIFOCNT_MASK) >=
			     dev_priv->cce_fifo_size) &&
			    !(pm4stat & (R128_PM4_BUSY |
					 R128_PM4_GUI_ACTIVE))) {
				return r128_do_pixcache_flush(dev_priv);
			}
		}
		udelay(1);
	}

#if R128_FIFO_DEBUG
	DRM_ERROR("failed!\n");
	r128_status(dev_priv);
#endif
	return -EBUSY;
}

/* Start the Concurrent Command Engine.
 */
static void r128_do_cce_start(drm_r128_private_t *dev_priv)
{
	r128_do_wait_for_idle(dev_priv);

	R128_WRITE(R128_PM4_BUFFER_CNTL,
		   dev_priv->cce_mode | dev_priv->ring.size_l2qw
		   | R128_PM4_BUFFER_CNTL_NOUPDATE);
	R128_READ(R128_PM4_BUFFER_ADDR);	/* as per the sample code */
	R128_WRITE(R128_PM4_MICRO_CNTL, R128_PM4_MICRO_FREERUN);

	dev_priv->cce_running = 1;
}

/* Reset the Concurrent Command Engine.  This will not flush any pending
 * commands, so you must wait for the CCE command stream to complete
 * before calling this routine.
 */
static void r128_do_cce_reset(drm_r128_private_t *dev_priv)
{
	R128_WRITE(R128_PM4_BUFFER_DL_WPTR, 0);
	R128_WRITE(R128_PM4_BUFFER_DL_RPTR, 0);
	dev_priv->ring.tail = 0;
}

/* Stop the Concurrent Command Engine.  This will not flush any pending
 * commands, so you must flush the command stream and wait for the CCE
 * to go idle before calling this routine.
 */
static void r128_do_cce_stop(drm_r128_private_t *dev_priv)
{
	R128_WRITE(R128_PM4_MICRO_CNTL, 0);
	R128_WRITE(R128_PM4_BUFFER_CNTL,
		   R128_PM4_NONPM4 | R128_PM4_BUFFER_CNTL_NOUPDATE);

	dev_priv->cce_running = 0;
}

/* Reset the engine.  This will stop the CCE if it is running.
 */
static int r128_do_engine_reset(struct drm_device *dev)
{
	drm_r128_private_t *dev_priv = dev->dev_private;
	u32 clock_cntl_index, mclk_cntl, gen_reset_cntl;

	r128_do_pixcache_flush(dev_priv);

	clock_cntl_index = R128_READ(R128_CLOCK_CNTL_INDEX);
	mclk_cntl = R128_READ_PLL(dev, R128_MCLK_CNTL);

	R128_WRITE_PLL(R128_MCLK_CNTL,
		       mclk_cntl | R128_FORCE_GCP | R128_FORCE_PIPE3D_CP);

	gen_reset_cntl = R128_READ(R128_GEN_RESET_CNTL);

	/* Taken from the sample code - do not change */
	R128_WRITE(R128_GEN_RESET_CNTL, gen_reset_cntl | R128_SOFT_RESET_GUI);
	R128_READ(R128_GEN_RESET_CNTL);
	R128_WRITE(R128_GEN_RESET_CNTL, gen_reset_cntl & ~R128_SOFT_RESET_GUI);
	R128_READ(R128_GEN_RESET_CNTL);

	R128_WRITE_PLL(R128_MCLK_CNTL, mclk_cntl);
	R128_WRITE(R128_CLOCK_CNTL_INDEX, clock_cntl_index);
	R128_WRITE(R128_GEN_RESET_CNTL, gen_reset_cntl);

	/* Reset the CCE ring */
	r128_do_cce_reset(dev_priv);

	/* The CCE is no longer running after an engine reset */
	dev_priv->cce_running = 0;

	/* Reset any pending vertex, indirect buffers */
	r128_freelist_reset(dev);

	return 0;
}

static void r128_cce_init_ring_buffer(struct drm_device *dev,
				      drm_r128_private_t *dev_priv)
{
	u32 ring_start;
	u32 tmp;

	DRM_DEBUG("\n");

	/* The manual (p. 2) says this address is in "VM space".  This
	 * means it's an offset from the start of AGP space.
	 */
#if IS_ENABLED(CONFIG_AGP)
	if (!dev_priv->is_pci)
		ring_start = dev_priv->cce_ring->offset - dev->agp->base;
	else
#endif
		ring_start = dev_priv->cce_ring->offset -
		    (unsigned long)dev->sg->virtual;

	R128_WRITE(R128_PM4_BUFFER_OFFSET, ring_start | R128_AGP_OFFSET);

	R128_WRITE(R128_PM4_BUFFER_DL_WPTR, 0);
	R128_WRITE(R128_PM4_BUFFER_DL_RPTR, 0);

	/* Set watermark control */
	R128_WRITE(R128_PM4_BUFFER_WM_CNTL,
		   ((R128_WATERMARK_L / 4) << R128_WMA_SHIFT)
		   | ((R128_WATERMARK_M / 4) << R128_WMB_SHIFT)
		   | ((R128_WATERMARK_N / 4) << R128_WMC_SHIFT)
		   | ((R128_WATERMARK_K / 64) << R128_WB_WM_SHIFT));

	/* Force read.  Why?  Because it's in the examples... */
	R128_READ(R128_PM4_BUFFER_ADDR);

	/* Turn on bus mastering */
	tmp = R128_READ(R128_BUS_CNTL) & ~R128_BUS_MASTER_DIS;
	R128_WRITE(R128_BUS_CNTL, tmp);
}

static int r128_do_init_cce(struct drm_device *dev, drm_r128_init_t *init)
{
	drm_r128_private_t *dev_priv;
	int rc;

	DRM_DEBUG("\n");

	if (dev->dev_private) {
		DRM_DEBUG("called when already initialized\n");
		return -EINVAL;
	}

	dev_priv = kzalloc(sizeof(drm_r128_private_t), GFP_KERNEL);
	if (dev_priv == NULL)
		return -ENOMEM;

	dev_priv->is_pci = init->is_pci;

	if (dev_priv->is_pci && !dev->sg) {
		DRM_ERROR("PCI GART memory not allocated!\n");
		dev->dev_private = (void *)dev_priv;
		r128_do_cleanup_cce(dev);
		return -EINVAL;
	}

	dev_priv->usec_timeout = init->usec_timeout;
	if (dev_priv->usec_timeout < 1 ||
	    dev_priv->usec_timeout > R128_MAX_USEC_TIMEOUT) {
		DRM_DEBUG("TIMEOUT problem!\n");
		dev->dev_private = (void *)dev_priv;
		r128_do_cleanup_cce(dev);
		return -EINVAL;
	}

	dev_priv->cce_mode = init->cce_mode;

	/* GH: Simple idle check.
	 */
	atomic_set(&dev_priv->idle_count, 0);

	/* We don't support anything other than bus-mastering ring mode,
	 * but the ring can be in either AGP or PCI space for the ring
	 * read pointer.
	 */
	if ((init->cce_mode != R128_PM4_192BM) &&
	    (init->cce_mode != R128_PM4_128BM_64INDBM) &&
	    (init->cce_mode != R128_PM4_64BM_128INDBM) &&
	    (init->cce_mode != R128_PM4_64BM_64VCBM_64INDBM)) {
		DRM_DEBUG("Bad cce_mode!\n");
		dev->dev_private = (void *)dev_priv;
		r128_do_cleanup_cce(dev);
		return -EINVAL;
	}

	switch (init->cce_mode) {
	case R128_PM4_NONPM4:
		dev_priv->cce_fifo_size = 0;
		break;
	case R128_PM4_192PIO:
	case R128_PM4_192BM:
		dev_priv->cce_fifo_size = 192;
		break;
	case R128_PM4_128PIO_64INDBM:
	case R128_PM4_128BM_64INDBM:
		dev_priv->cce_fifo_size = 128;
		break;
	case R128_PM4_64PIO_128INDBM:
	case R128_PM4_64BM_128INDBM:
	case R128_PM4_64PIO_64VCBM_64INDBM:
	case R128_PM4_64BM_64VCBM_64INDBM:
	case R128_PM4_64PIO_64VCPIO_64INDPIO:
		dev_priv->cce_fifo_size = 64;
		break;
	}

	switch (init->fb_bpp) {
	case 16:
		dev_priv->color_fmt = R128_DATATYPE_RGB565;
		break;
	case 32:
	default:
		dev_priv->color_fmt = R128_DATATYPE_ARGB8888;
		break;
	}
	dev_priv->front_offset = init->front_offset;
	dev_priv->front_pitch = init->front_pitch;
	dev_priv->back_offset = init->back_offset;
	dev_priv->back_pitch = init->back_pitch;

	switch (init->depth_bpp) {
	case 16:
		dev_priv->depth_fmt = R128_DATATYPE_RGB565;
		break;
	case 24:
	case 32:
	default:
		dev_priv->depth_fmt = R128_DATATYPE_ARGB8888;
		break;
	}
	dev_priv->depth_offset = init->depth_offset;
	dev_priv->depth_pitch = init->depth_pitch;
	dev_priv->span_offset = init->span_offset;

	dev_priv->front_pitch_offset_c = (((dev_priv->front_pitch / 8) << 21) |
					  (dev_priv->front_offset >> 5));
	dev_priv->back_pitch_offset_c = (((dev_priv->back_pitch / 8) << 21) |
					 (dev_priv->back_offset >> 5));
	dev_priv->depth_pitch_offset_c = (((dev_priv->depth_pitch / 8) << 21) |
					  (dev_priv->depth_offset >> 5) |
					  R128_DST_TILE);
	dev_priv->span_pitch_offset_c = (((dev_priv->depth_pitch / 8) << 21) |
					 (dev_priv->span_offset >> 5));

	dev_priv->sarea = drm_legacy_getsarea(dev);
	if (!dev_priv->sarea) {
		DRM_ERROR("could not find sarea!\n");
		dev->dev_private = (void *)dev_priv;
		r128_do_cleanup_cce(dev);
		return -EINVAL;
	}

	dev_priv->mmio = drm_legacy_findmap(dev, init->mmio_offset);
	if (!dev_priv->mmio) {
		DRM_ERROR("could not find mmio region!\n");
		dev->dev_private = (void *)dev_priv;
		r128_do_cleanup_cce(dev);
		return -EINVAL;
	}
	dev_priv->cce_ring = drm_legacy_findmap(dev, init->ring_offset);
	if (!dev_priv->cce_ring) {
		DRM_ERROR("could not find cce ring region!\n");
		dev->dev_private = (void *)dev_priv;
		r128_do_cleanup_cce(dev);
		return -EINVAL;
	}
	dev_priv->ring_rptr = drm_legacy_findmap(dev, init->ring_rptr_offset);
	if (!dev_priv->ring_rptr) {
		DRM_ERROR("could not find ring read pointer!\n");
		dev->dev_private = (void *)dev_priv;
		r128_do_cleanup_cce(dev);
		return -EINVAL;
	}
	dev->agp_buffer_token = init->buffers_offset;
	dev->agp_buffer_map = drm_legacy_findmap(dev, init->buffers_offset);
	if (!dev->agp_buffer_map) {
		DRM_ERROR("could not find dma buffer region!\n");
		dev->dev_private = (void *)dev_priv;
		r128_do_cleanup_cce(dev);
		return -EINVAL;
	}

	if (!dev_priv->is_pci) {
		dev_priv->agp_textures =
		    drm_legacy_findmap(dev, init->agp_textures_offset);
		if (!dev_priv->agp_textures) {
			DRM_ERROR("could not find agp texture region!\n");
			dev->dev_private = (void *)dev_priv;
			r128_do_cleanup_cce(dev);
			return -EINVAL;
		}
	}

	dev_priv->sarea_priv =
	    (drm_r128_sarea_t *) ((u8 *) dev_priv->sarea->handle +
				  init->sarea_priv_offset);

#if IS_ENABLED(CONFIG_AGP)
	if (!dev_priv->is_pci) {
		drm_legacy_ioremap_wc(dev_priv->cce_ring, dev);
		drm_legacy_ioremap_wc(dev_priv->ring_rptr, dev);
		drm_legacy_ioremap_wc(dev->agp_buffer_map, dev);
		if (!dev_priv->cce_ring->handle ||
		    !dev_priv->ring_rptr->handle ||
		    !dev->agp_buffer_map->handle) {
			DRM_ERROR("Could not ioremap agp regions!\n");
			dev->dev_private = (void *)dev_priv;
			r128_do_cleanup_cce(dev);
			return -ENOMEM;
		}
	} else
#endif
	{
		dev_priv->cce_ring->handle =
			(void *)(unsigned long)dev_priv->cce_ring->offset;
		dev_priv->ring_rptr->handle =
			(void *)(unsigned long)dev_priv->ring_rptr->offset;
		dev->agp_buffer_map->handle =
			(void *)(unsigned long)dev->agp_buffer_map->offset;
	}

#if IS_ENABLED(CONFIG_AGP)
	if (!dev_priv->is_pci)
		dev_priv->cce_buffers_offset = dev->agp->base;
	else
#endif
		dev_priv->cce_buffers_offset = (unsigned long)dev->sg->virtual;

	dev_priv->ring.start = (u32 *) dev_priv->cce_ring->handle;
	dev_priv->ring.end = ((u32 *) dev_priv->cce_ring->handle
			      + init->ring_size / sizeof(u32));
	dev_priv->ring.size = init->ring_size;
	dev_priv->ring.size_l2qw = order_base_2(init->ring_size / 8);

	dev_priv->ring.tail_mask = (dev_priv->ring.size / sizeof(u32)) - 1;

	dev_priv->ring.high_mark = 128;

	dev_priv->sarea_priv->last_frame = 0;
	R128_WRITE(R128_LAST_FRAME_REG, dev_priv->sarea_priv->last_frame);

	dev_priv->sarea_priv->last_dispatch = 0;
	R128_WRITE(R128_LAST_DISPATCH_REG, dev_priv->sarea_priv->last_dispatch);

#if IS_ENABLED(CONFIG_AGP)
	if (dev_priv->is_pci) {
#endif
		dev_priv->gart_info.table_mask = DMA_BIT_MASK(32);
		dev_priv->gart_info.gart_table_location = DRM_ATI_GART_MAIN;
		dev_priv->gart_info.table_size = R128_PCIGART_TABLE_SIZE;
		dev_priv->gart_info.addr = NULL;
		dev_priv->gart_info.bus_addr = 0;
		dev_priv->gart_info.gart_reg_if = DRM_ATI_GART_PCI;
		rc = drm_ati_pcigart_init(dev, &dev_priv->gart_info);
		if (rc) {
			DRM_ERROR("failed to init PCI GART!\n");
			dev->dev_private = (void *)dev_priv;
			r128_do_cleanup_cce(dev);
			return rc;
		}
		R128_WRITE(R128_PCI_GART_PAGE, dev_priv->gart_info.bus_addr);
#if IS_ENABLED(CONFIG_AGP)
	}
#endif

	r128_cce_init_ring_buffer(dev, dev_priv);
	rc = r128_cce_load_microcode(dev_priv);

	dev->dev_private = (void *)dev_priv;

	r128_do_engine_reset(dev);

	if (rc) {
		DRM_ERROR("Failed to load firmware!\n");
		r128_do_cleanup_cce(dev);
	}

	return rc;
}

int r128_do_cleanup_cce(struct drm_device *dev)
{

	/* Make sure interrupts are disabled here because the uninstall ioctl
	 * may not have been called from userspace and after dev_private
	 * is freed, it's too late.
	 */
	if (dev->irq_enabled)
		drm_legacy_irq_uninstall(dev);

	if (dev->dev_private) {
		drm_r128_private_t *dev_priv = dev->dev_private;

#if IS_ENABLED(CONFIG_AGP)
		if (!dev_priv->is_pci) {
			if (dev_priv->cce_ring != NULL)
				drm_legacy_ioremapfree(dev_priv->cce_ring, dev);
			if (dev_priv->ring_rptr != NULL)
				drm_legacy_ioremapfree(dev_priv->ring_rptr, dev);
			if (dev->agp_buffer_map != NULL) {
				drm_legacy_ioremapfree(dev->agp_buffer_map, dev);
				dev->agp_buffer_map = NULL;
			}
		} else
#endif
		{
			if (dev_priv->gart_info.bus_addr)
				if (!drm_ati_pcigart_cleanup(dev,
							&dev_priv->gart_info))
					DRM_ERROR
					    ("failed to cleanup PCI GART!\n");
		}

		kfree(dev->dev_private);
		dev->dev_private = NULL;
	}

	return 0;
}

int r128_cce_init(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_r128_init_t *init = data;

	DRM_DEBUG("\n");

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	switch (init->func) {
	case R128_INIT_CCE:
		return r128_do_init_cce(dev, init);
	case R128_CLEANUP_CCE:
		return r128_do_cleanup_cce(dev);
	}

	return -EINVAL;
}

int r128_cce_start(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_r128_private_t *dev_priv = dev->dev_private;
	DRM_DEBUG("\n");

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	DEV_INIT_TEST_WITH_RETURN(dev_priv);

	if (dev_priv->cce_running || dev_priv->cce_mode == R128_PM4_NONPM4) {
		DRM_DEBUG("while CCE running\n");
		return 0;
	}

	r128_do_cce_start(dev_priv);

	return 0;
}

/* Stop the CCE.  The engine must have been idled before calling this
 * routine.
 */
int r128_cce_stop(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_r128_private_t *dev_priv = dev->dev_private;
	drm_r128_cce_stop_t *stop = data;
	int ret;
	DRM_DEBUG("\n");

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	DEV_INIT_TEST_WITH_RETURN(dev_priv);

	/* Flush any pending CCE commands.  This ensures any outstanding
	 * commands are exectuted by the engine before we turn it off.
	 */
	if (stop->flush)
		r128_do_cce_flush(dev_priv);

	/* If we fail to make the engine go idle, we return an error
	 * code so that the DRM ioctl wrapper can try again.
	 */
	if (stop->idle) {
		ret = r128_do_cce_idle(dev_priv);
		if (ret)
			return ret;
	}

	/* Finally, we can turn off the CCE.  If the engine isn't idle,
	 * we will get some dropped triangles as they won't be fully
	 * rendered before the CCE is shut down.
	 */
	r128_do_cce_stop(dev_priv);

	/* Reset the engine */
	r128_do_engine_reset(dev);

	return 0;
}

/* Just reset the CCE ring.  Called as part of an X Server engine reset.
 */
int r128_cce_reset(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_r128_private_t *dev_priv = dev->dev_private;
	DRM_DEBUG("\n");

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	DEV_INIT_TEST_WITH_RETURN(dev_priv);

	r128_do_cce_reset(dev_priv);

	/* The CCE is no longer running after an engine reset */
	dev_priv->cce_running = 0;

	return 0;
}

int r128_cce_idle(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_r128_private_t *dev_priv = dev->dev_private;
	DRM_DEBUG("\n");

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	DEV_INIT_TEST_WITH_RETURN(dev_priv);

	if (dev_priv->cce_running)
		r128_do_cce_flush(dev_priv);

	return r128_do_cce_idle(dev_priv);
}

int r128_engine_reset(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	DRM_DEBUG("\n");

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	DEV_INIT_TEST_WITH_RETURN(dev->dev_private);

	return r128_do_engine_reset(dev);
}

int r128_fullscreen(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	return -EINVAL;
}

/* ================================================================
 * Freelist management
 */
#define R128_BUFFER_USED	0xffffffff
#define R128_BUFFER_FREE	0

#if 0
static int r128_freelist_init(struct drm_device *dev)
{
	struct drm_device_dma *dma = dev->dma;
	drm_r128_private_t *dev_priv = dev->dev_private;
	struct drm_buf *buf;
	drm_r128_buf_priv_t *buf_priv;
	drm_r128_freelist_t *entry;
	int i;

	dev_priv->head = kzalloc(sizeof(drm_r128_freelist_t), GFP_KERNEL);
	if (dev_priv->head == NULL)
		return -ENOMEM;

	dev_priv->head->age = R128_BUFFER_USED;

	for (i = 0; i < dma->buf_count; i++) {
		buf = dma->buflist[i];
		buf_priv = buf->dev_private;

		entry = kmalloc(sizeof(drm_r128_freelist_t), GFP_KERNEL);
		if (!entry)
			return -ENOMEM;

		entry->age = R128_BUFFER_FREE;
		entry->buf = buf;
		entry->prev = dev_priv->head;
		entry->next = dev_priv->head->next;
		if (!entry->next)
			dev_priv->tail = entry;

		buf_priv->discard = 0;
		buf_priv->dispatched = 0;
		buf_priv->list_entry = entry;

		dev_priv->head->next = entry;

		if (dev_priv->head->next)
			dev_priv->head->next->prev = entry;
	}

	return 0;

}
#endif

static struct drm_buf *r128_freelist_get(struct drm_device * dev)
{
	struct drm_device_dma *dma = dev->dma;
	drm_r128_private_t *dev_priv = dev->dev_private;
	drm_r128_buf_priv_t *buf_priv;
	struct drm_buf *buf;
	int i, t;

	/* FIXME: Optimize -- use freelist code */

	for (i = 0; i < dma->buf_count; i++) {
		buf = dma->buflist[i];
		buf_priv = buf->dev_private;
		if (!buf->file_priv)
			return buf;
	}

	for (t = 0; t < dev_priv->usec_timeout; t++) {
		u32 done_age = R128_READ(R128_LAST_DISPATCH_REG);

		for (i = 0; i < dma->buf_count; i++) {
			buf = dma->buflist[i];
			buf_priv = buf->dev_private;
			if (buf->pending && buf_priv->age <= done_age) {
				/* The buffer has been processed, so it
				 * can now be used.
				 */
				buf->pending = 0;
				return buf;
			}
		}
		udelay(1);
	}

	DRM_DEBUG("returning NULL!\n");
	return NULL;
}

void r128_freelist_reset(struct drm_device *dev)
{
	struct drm_device_dma *dma = dev->dma;
	int i;

	for (i = 0; i < dma->buf_count; i++) {
		struct drm_buf *buf = dma->buflist[i];
		drm_r128_buf_priv_t *buf_priv = buf->dev_private;
		buf_priv->age = 0;
	}
}

/* ================================================================
 * CCE command submission
 */

int r128_wait_ring(drm_r128_private_t *dev_priv, int n)
{
	drm_r128_ring_buffer_t *ring = &dev_priv->ring;
	int i;

	for (i = 0; i < dev_priv->usec_timeout; i++) {
		r128_update_ring_snapshot(dev_priv);
		if (ring->space >= n)
			return 0;
		udelay(1);
	}

	/* FIXME: This is being ignored... */
	DRM_ERROR("failed!\n");
	return -EBUSY;
}

static int r128_cce_get_buffers(struct drm_device *dev,
				struct drm_file *file_priv,
				struct drm_dma *d)
{
	int i;
	struct drm_buf *buf;

	for (i = d->granted_count; i < d->request_count; i++) {
		buf = r128_freelist_get(dev);
		if (!buf)
			return -EAGAIN;

		buf->file_priv = file_priv;

		if (copy_to_user(&d->request_indices[i], &buf->idx,
				     sizeof(buf->idx)))
			return -EFAULT;
		if (copy_to_user(&d->request_sizes[i], &buf->total,
				     sizeof(buf->total)))
			return -EFAULT;

		d->granted_count++;
	}
	return 0;
}

int r128_cce_buffers(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_device_dma *dma = dev->dma;
	int ret = 0;
	struct drm_dma *d = data;

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	/* Please don't send us buffers.
	 */
	if (d->send_count != 0) {
		DRM_ERROR("Process %d trying to send %d buffers via drmDMA\n",
			  task_pid_nr(current), d->send_count);
		return -EINVAL;
	}

	/* We'll send you buffers.
	 */
	if (d->request_count < 0 || d->request_count > dma->buf_count) {
		DRM_ERROR("Process %d trying to get %d buffers (of %d max)\n",
			  task_pid_nr(current), d->request_count, dma->buf_count);
		return -EINVAL;
	}

	d->granted_count = 0;

	if (d->request_count)
		ret = r128_cce_get_buffers(dev, file_priv, d);

	return ret;
}
