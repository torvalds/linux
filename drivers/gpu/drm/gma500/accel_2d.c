/**************************************************************************
 * Copyright (c) 2007-2011, Intel Corporation.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Intel funded Tungsten Graphics (http://www.tungstengraphics.com) to
 * develop this driver.
 *
 **************************************************************************/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/console.h>

#include <drm/drmP.h>
#include <drm/drm.h>
#include <drm/drm_crtc.h>

#include "psb_drv.h"
#include "psb_reg.h"
#include "framebuffer.h"

/**
 *	psb_spank		-	reset the 2D engine
 *	@dev_priv: our PSB DRM device
 *
 *	Soft reset the graphics engine and then reload the necessary registers.
 *	We use this at initialisation time but it will become relevant for
 *	accelerated X later
 */
void psb_spank(struct drm_psb_private *dev_priv)
{
	PSB_WSGX32(_PSB_CS_RESET_BIF_RESET | _PSB_CS_RESET_DPM_RESET |
		_PSB_CS_RESET_TA_RESET | _PSB_CS_RESET_USE_RESET |
		_PSB_CS_RESET_ISP_RESET | _PSB_CS_RESET_TSP_RESET |
		_PSB_CS_RESET_TWOD_RESET, PSB_CR_SOFT_RESET);
	PSB_RSGX32(PSB_CR_SOFT_RESET);

	msleep(1);

	PSB_WSGX32(0, PSB_CR_SOFT_RESET);
	wmb();
	PSB_WSGX32(PSB_RSGX32(PSB_CR_BIF_CTRL) | _PSB_CB_CTRL_CLEAR_FAULT,
		   PSB_CR_BIF_CTRL);
	wmb();
	(void) PSB_RSGX32(PSB_CR_BIF_CTRL);

	msleep(1);
	PSB_WSGX32(PSB_RSGX32(PSB_CR_BIF_CTRL) & ~_PSB_CB_CTRL_CLEAR_FAULT,
		   PSB_CR_BIF_CTRL);
	(void) PSB_RSGX32(PSB_CR_BIF_CTRL);
	PSB_WSGX32(dev_priv->gtt.gatt_start, PSB_CR_BIF_TWOD_REQ_BASE);
}

/**
 *	psb2_2d_wait_available	-	wait for FIFO room
 *	@dev_priv: our DRM device
 *	@size: size (in dwords) of the command we want to issue
 *
 *	Wait until there is room to load the FIFO with our data. If the
 *	device is not responding then reset it
 */
static int psb_2d_wait_available(struct drm_psb_private *dev_priv,
			  unsigned size)
{
	uint32_t avail = PSB_RSGX32(PSB_CR_2D_SOCIF);
	unsigned long t = jiffies + HZ;

	while (avail < size) {
		avail = PSB_RSGX32(PSB_CR_2D_SOCIF);
		if (time_after(jiffies, t)) {
			psb_spank(dev_priv);
			return -EIO;
		}
	}
	return 0;
}

/**
 *	psb_2d_submit		-	submit a 2D command
 *	@dev_priv: our DRM device
 *	@cmdbuf: command to issue
 *	@size: length (in dwords)
 *
 *	Issue one or more 2D commands to the accelerator. This needs to be
 *	serialized later when we add the GEM interfaces for acceleration
 */
static int psbfb_2d_submit(struct drm_psb_private *dev_priv, uint32_t *cmdbuf,
								unsigned size)
{
	int ret = 0;
	int i;
	unsigned submit_size;
	unsigned long flags;

	spin_lock_irqsave(&dev_priv->lock_2d, flags);
	while (size > 0) {
		submit_size = (size < 0x60) ? size : 0x60;
		size -= submit_size;
		ret = psb_2d_wait_available(dev_priv, submit_size);
		if (ret)
			break;

		submit_size <<= 2;

		for (i = 0; i < submit_size; i += 4)
			PSB_WSGX32(*cmdbuf++, PSB_SGX_2D_SLAVE_PORT + i);

		(void)PSB_RSGX32(PSB_SGX_2D_SLAVE_PORT + i - 4);
	}
	spin_unlock_irqrestore(&dev_priv->lock_2d, flags);
	return ret;
}


/**
 *	psb_accel_2d_copy_direction	-	compute blit order
 *	@xdir: X direction of move
 *	@ydir: Y direction of move
 *
 *	Compute the correct order setings to ensure that an overlapping blit
 *	correctly copies all the pixels.
 */
static u32 psb_accel_2d_copy_direction(int xdir, int ydir)
{
	if (xdir < 0)
		return (ydir < 0) ? PSB_2D_COPYORDER_BR2TL :
						PSB_2D_COPYORDER_TR2BL;
	else
		return (ydir < 0) ? PSB_2D_COPYORDER_BL2TR :
						PSB_2D_COPYORDER_TL2BR;
}

/**
 *	psb_accel_2d_copy		-	accelerated 2D copy
 *	@dev_priv: our DRM device
 *	@src_offset in bytes
 *	@src_stride in bytes
 *	@src_format psb 2D format defines
 *	@dst_offset in bytes
 *	@dst_stride in bytes
 *	@dst_format psb 2D format defines
 *	@src_x offset in pixels
 *	@src_y offset in pixels
 *	@dst_x offset in pixels
 *	@dst_y offset in pixels
 *	@size_x of the copied area
 *	@size_y of the copied area
 *
 *	Format and issue a 2D accelerated copy command.
 */
static int psb_accel_2d_copy(struct drm_psb_private *dev_priv,
			     uint32_t src_offset, uint32_t src_stride,
			     uint32_t src_format, uint32_t dst_offset,
			     uint32_t dst_stride, uint32_t dst_format,
			     uint16_t src_x, uint16_t src_y,
			     uint16_t dst_x, uint16_t dst_y,
			     uint16_t size_x, uint16_t size_y)
{
	uint32_t blit_cmd;
	uint32_t buffer[10];
	uint32_t *buf;
	uint32_t direction;

	buf = buffer;

	direction =
	    psb_accel_2d_copy_direction(src_x - dst_x, src_y - dst_y);

	if (direction == PSB_2D_COPYORDER_BR2TL ||
	    direction == PSB_2D_COPYORDER_TR2BL) {
		src_x += size_x - 1;
		dst_x += size_x - 1;
	}
	if (direction == PSB_2D_COPYORDER_BR2TL ||
	    direction == PSB_2D_COPYORDER_BL2TR) {
		src_y += size_y - 1;
		dst_y += size_y - 1;
	}

	blit_cmd =
	    PSB_2D_BLIT_BH |
	    PSB_2D_ROT_NONE |
	    PSB_2D_DSTCK_DISABLE |
	    PSB_2D_SRCCK_DISABLE |
	    PSB_2D_USE_PAT | PSB_2D_ROP3_SRCCOPY | direction;

	*buf++ = PSB_2D_FENCE_BH;
	*buf++ =
	    PSB_2D_DST_SURF_BH | dst_format | (dst_stride <<
					       PSB_2D_DST_STRIDE_SHIFT);
	*buf++ = dst_offset;
	*buf++ =
	    PSB_2D_SRC_SURF_BH | src_format | (src_stride <<
					       PSB_2D_SRC_STRIDE_SHIFT);
	*buf++ = src_offset;
	*buf++ =
	    PSB_2D_SRC_OFF_BH | (src_x << PSB_2D_SRCOFF_XSTART_SHIFT) |
	    (src_y << PSB_2D_SRCOFF_YSTART_SHIFT);
	*buf++ = blit_cmd;
	*buf++ =
	    (dst_x << PSB_2D_DST_XSTART_SHIFT) | (dst_y <<
						  PSB_2D_DST_YSTART_SHIFT);
	*buf++ =
	    (size_x << PSB_2D_DST_XSIZE_SHIFT) | (size_y <<
						  PSB_2D_DST_YSIZE_SHIFT);
	*buf++ = PSB_2D_FLUSH_BH;

	return psbfb_2d_submit(dev_priv, buffer, buf - buffer);
}

/**
 *	psbfb_copyarea_accel	-	copyarea acceleration for /dev/fb
 *	@info: our framebuffer
 *	@a: copyarea parameters from the framebuffer core
 *
 *	Perform a 2D copy via the accelerator
 */
static void psbfb_copyarea_accel(struct fb_info *info,
				 const struct fb_copyarea *a)
{
	struct psb_fbdev *fbdev = info->par;
	struct psb_framebuffer *psbfb = &fbdev->pfb;
	struct drm_device *dev = psbfb->base.dev;
	struct drm_framebuffer *fb = fbdev->psb_fb_helper.fb;
	struct drm_psb_private *dev_priv = dev->dev_private;
	uint32_t offset;
	uint32_t stride;
	uint32_t src_format;
	uint32_t dst_format;

	if (!fb)
		return;

	offset = psbfb->gtt->offset;
	stride = fb->pitches[0];

	switch (fb->depth) {
	case 8:
		src_format = PSB_2D_SRC_332RGB;
		dst_format = PSB_2D_DST_332RGB;
		break;
	case 15:
		src_format = PSB_2D_SRC_555RGB;
		dst_format = PSB_2D_DST_555RGB;
		break;
	case 16:
		src_format = PSB_2D_SRC_565RGB;
		dst_format = PSB_2D_DST_565RGB;
		break;
	case 24:
	case 32:
		/* this is wrong but since we don't do blending its okay */
		src_format = PSB_2D_SRC_8888ARGB;
		dst_format = PSB_2D_DST_8888ARGB;
		break;
	default:
		/* software fallback */
		drm_fb_helper_cfb_copyarea(info, a);
		return;
	}

	if (!gma_power_begin(dev, false)) {
		drm_fb_helper_cfb_copyarea(info, a);
		return;
	}
	psb_accel_2d_copy(dev_priv,
			  offset, stride, src_format,
			  offset, stride, dst_format,
			  a->sx, a->sy, a->dx, a->dy, a->width, a->height);
	gma_power_end(dev);
}

/**
 *	psbfb_copyarea	-	2D copy interface
 *	@info: our framebuffer
 *	@region: region to copy
 *
 *	Copy an area of the framebuffer console either by the accelerator
 *	or directly using the cfb helpers according to the request
 */
void psbfb_copyarea(struct fb_info *info,
			   const struct fb_copyarea *region)
{
	if (unlikely(info->state != FBINFO_STATE_RUNNING))
		return;

	/* Avoid the 8 pixel erratum */
	if (region->width == 8 || region->height == 8 ||
		(info->flags & FBINFO_HWACCEL_DISABLED))
		return drm_fb_helper_cfb_copyarea(info, region);

	psbfb_copyarea_accel(info, region);
}

/**
 *	psbfb_sync	-	synchronize 2D
 *	@info: our framebuffer
 *
 *	Wait for the 2D engine to quiesce so that we can do CPU
 *	access to the framebuffer again
 */
int psbfb_sync(struct fb_info *info)
{
	struct psb_fbdev *fbdev = info->par;
	struct psb_framebuffer *psbfb = &fbdev->pfb;
	struct drm_device *dev = psbfb->base.dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	unsigned long _end = jiffies + HZ;
	int busy = 0;
	unsigned long flags;

	spin_lock_irqsave(&dev_priv->lock_2d, flags);
	/*
	 * First idle the 2D engine.
	 */

	if ((PSB_RSGX32(PSB_CR_2D_SOCIF) == _PSB_C2_SOCIF_EMPTY) &&
	    ((PSB_RSGX32(PSB_CR_2D_BLIT_STATUS) & _PSB_C2B_STATUS_BUSY) == 0))
		goto out;

	do {
		busy = (PSB_RSGX32(PSB_CR_2D_SOCIF) != _PSB_C2_SOCIF_EMPTY);
		cpu_relax();
	} while (busy && !time_after_eq(jiffies, _end));

	if (busy)
		busy = (PSB_RSGX32(PSB_CR_2D_SOCIF) != _PSB_C2_SOCIF_EMPTY);
	if (busy)
		goto out;

	do {
		busy = ((PSB_RSGX32(PSB_CR_2D_BLIT_STATUS) &
						_PSB_C2B_STATUS_BUSY) != 0);
		cpu_relax();
	} while (busy && !time_after_eq(jiffies, _end));
	if (busy)
		busy = ((PSB_RSGX32(PSB_CR_2D_BLIT_STATUS) &
					_PSB_C2B_STATUS_BUSY) != 0);

out:
	spin_unlock_irqrestore(&dev_priv->lock_2d, flags);
	return (busy) ? -EBUSY : 0;
}
