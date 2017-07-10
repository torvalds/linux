/* -*- mode: c; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* vi: set ts=8 sw=8 sts=8: */
/*************************************************************************/ /*!
@File           adf_pdp.c
@Codingstyle    LinuxKernel
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

/*
 * This is an example ADF display driver for the testchip's 5 PDP with fbdc
 * support
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/wait.h>

#include <drm/drm_fourcc.h>

#include <video/adf.h>
#include <video/adf_client.h>

#include PVR_ANDROID_ION_HEADER

/* for sync_fence_put */
#include PVR_ANDROID_SYNC_HEADER

#include "apollo_drv.h"
#include "adf_common.h"
#include "debugfs_dma_buf.h"

#include "pvrmodule.h"

#include "pdp_tc5_regs.h"
#include "pdp_tc5_fbdc_regs.h"

#define DRV_NAME APOLLO_DEVICE_NAME_PDP

#ifndef ADF_PDP_WIDTH
#define ADF_PDP_WIDTH 1280
#endif

#ifndef ADF_PDP_HEIGHT
#define ADF_PDP_HEIGHT 720
#endif

#define DRM_FORMAT_BGRA8888_DIRECT_16x4 fourcc_code('I', 'M', 'G', '0')

MODULE_DESCRIPTION("APOLLO TC5 PDP display driver");

static int pdp_display_width = ADF_PDP_WIDTH;
static int pdp_display_height = ADF_PDP_HEIGHT;
module_param(pdp_display_width, int, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(pdp_display_width, "PDP display width");
module_param(pdp_display_height, int, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(pdp_display_height, "PDP display height");

static DEFINE_SPINLOCK(gFlipLock);

struct pdp_timing_data {
	u32 h_display;
	u32 h_back_porch;
	u32 h_total;
	u32 h_active_start;
	u32 h_left_border;
	u32 h_right_border;
	u32 h_front_porch;

	u32 v_display;
	u32 v_back_porch;
	u32 v_total;
	u32 v_active_start;
	u32 v_top_border;
	u32 v_bottom_border;
	u32 v_front_porch;
	u32 v_refresh;
};

static const struct pdp_timing_data pdp_supported_modes[] = {
	{
		.h_display		=	1280,
		.h_back_porch		=	40,
		.h_total		=	1650,
		.h_active_start		=	260,
		.h_left_border		=	260,
		.h_right_border		=	1540,
		.h_front_porch		=	1540,

		.v_display		=	720,
		.v_back_porch		=	5,
		.v_total		=	750,
		.v_active_start		=	25,
		.v_top_border		=	25,
		.v_bottom_border	=	745,
		.v_front_porch		=	745,

		.v_refresh		=	60,
	},
	{}
};

struct adf_pdp_device {
	struct ion_client *ion_client;

	struct adf_device adf_device;
	struct adf_interface adf_interface;
	struct adf_overlay_engine adf_overlay;

	struct platform_device *pdev;

	struct apollo_pdp_platform_data *pdata;

	void __iomem *regs;
	resource_size_t regs_size;

	void __iomem *fbdc_regs;
	resource_size_t fbdc_regs_size;

	void __iomem *i2c_regs;
	resource_size_t i2c_regs_size;

	struct drm_mode_modeinfo *supported_modes;
	int num_supported_modes;

	atomic_t refcount;

	atomic_t num_validates;
	int num_posts;

	atomic_t vsync_triggered;
	wait_queue_head_t vsync_wait_queue;
	atomic_t requested_vsync_state;
	atomic_t vsync_state;

	const struct pdp_timing_data *current_timings;
	u32 current_drm_format;

	u32 baseaddr;
};

static const u32 pdp_supported_formats[] = {
	DRM_FORMAT_BGRA8888_DIRECT_16x4,
};
#define NUM_SUPPORTED_FORMATS 1

static const struct {
	u32 drm_format;
	u32 bytes_per_pixel;
	u32 pixfmt_word;
} pdp_format_table[] = {
	/* 01000b / 8h 8-bit alpha + 24-bit rgb888 [RGBA] */
	{ DRM_FORMAT_BGRA8888_DIRECT_16x4, 4, 0x8 },
	{},
};

static int pdp_mode_count(struct adf_pdp_device *pdp)
{
	int i = 0;

	while (pdp_supported_modes[i].h_display)
		i++;
	return i;
}

static int pdp_mode_id(struct adf_pdp_device *pdp, u32 height, u32 width)
{
	int i;

	for (i = 0; pdp_supported_modes[i].h_display; i++) {
		const struct pdp_timing_data *tdata = &pdp_supported_modes[i];

		if (tdata->h_display == width && tdata->v_display == height)
			return i;
	}
	dev_err(&pdp->pdev->dev, "Failed to find matching mode for %dx%d\n",
		width, height);
	return -1;
}

static const struct pdp_timing_data *pdp_timing_data(
	struct adf_pdp_device *pdp, int mode_id)
{
	if (mode_id >= pdp_mode_count(pdp) || mode_id < 0)
		return NULL;
	return &pdp_supported_modes[mode_id];
}

static void pdp_mode_to_drm_mode(struct adf_pdp_device *pdp, int mode_id,
	struct drm_mode_modeinfo *drm_mode)
{
	const struct pdp_timing_data *pdp_mode;

	pdp_mode = pdp_timing_data(pdp, mode_id);
	BUG_ON(pdp_mode == NULL);

	memset(drm_mode, 0, sizeof(*drm_mode));

	drm_mode->hdisplay = pdp_mode->h_display;
	drm_mode->vdisplay = pdp_mode->v_display;
	drm_mode->vrefresh = pdp_mode->v_refresh;

	adf_modeinfo_set_name(drm_mode);
}

static u32 pdp_read_reg(struct adf_pdp_device *pdp, resource_size_t reg_offset)
{
	BUG_ON(reg_offset > pdp->regs_size-4);
	return ioread32(pdp->regs + reg_offset);
}

static void pdp_write_reg(struct adf_pdp_device *pdp,
	resource_size_t reg_offset, u32 reg_value)
{
	BUG_ON(reg_offset > pdp->regs_size-4);
	iowrite32(reg_value, pdp->regs + reg_offset);
}

static void pdp_write_fbdc_reg(struct adf_pdp_device *pdp,
	resource_size_t reg_offset, u32 reg_value)
{
	BUG_ON(reg_offset > pdp->fbdc_regs_size-4);
	iowrite32(reg_value, pdp->fbdc_regs + reg_offset);
}

#define I2C_TIMEOUT 10000

static void pdp_write_i2c(struct adf_pdp_device *pdp, u32 reg_addr, u32 data)
{
	int i;

	iowrite32(0x7a, pdp->i2c_regs + 0x04);
	iowrite32(reg_addr, pdp->i2c_regs + 0x08);
	iowrite32(data, pdp->i2c_regs + 0x0c);
	iowrite32(0x1, pdp->i2c_regs + 0x14);

	for (i = 0; i < I2C_TIMEOUT; i++) {
		if (ioread32(pdp->i2c_regs + 0x18) == 0)
			break;
	}

	if (i == I2C_TIMEOUT)
		dev_err(&pdp->pdev->dev, "i2c write timeout\n");
}

static u32 pdp_read_i2c(struct adf_pdp_device *pdp, u32 reg_addr)
{
	int i;

	iowrite32(0x7b, pdp->i2c_regs + 0x04);
	iowrite32(reg_addr, pdp->i2c_regs + 0x08);
	iowrite32(0x1, pdp->i2c_regs + 0x14);

	for (i = 0; i < I2C_TIMEOUT; i++) {
		if (ioread32(pdp->i2c_regs + 0x18) == 0)
			break;
	}

	if (i == I2C_TIMEOUT) {
		dev_err(&pdp->pdev->dev, "i2c read timeout\n");
		return 0;
	}
	return ioread32(pdp->i2c_regs + 0x10);
}

static void pdp_devres_release(struct device *dev, void *res)
{
	/* No extra cleanup needed */
}

static u32 pdp_format_bpp(u32 drm_format)
{
	int i;

	for (i = 0; pdp_format_table[i].drm_format != 0; i++) {
		if (pdp_format_table[i].drm_format == drm_format)
			return pdp_format_table[i].bytes_per_pixel;
	}
	WARN(1, "Unsupported drm format");
	return 0;
}

static u32 pdp_format(u32 drm_format)
{
	int i;

	for (i = 0; pdp_format_table[i].drm_format != 0; i++) {
		if (pdp_format_table[i].drm_format == drm_format)
			return pdp_format_table[i].pixfmt_word;
	}
	WARN(1, "Unsupported drm format");
	return 0;
}

static void pdp_enable_scanout(struct adf_pdp_device *pdp, u32 base_addr)
{
	u32 reg_value;

	/* Set the base address to the fbdc module */
	pdp_write_fbdc_reg(pdp, PVR5__PDP_FBDC_INTRFC_BASE_ADDRESS,
					   base_addr);
	/* Turn on scanout */
	reg_value = pdp_read_reg(pdp, PVR5__PDP_PVR_PDP_GRPH1CTRL);
	reg_value &= ~(PVR5__GRPH1STREN_MASK);
	reg_value |= 0x1 << PVR5__GRPH1STREN_SHIFT;
	pdp_write_reg(pdp, PVR5__PDP_PVR_PDP_GRPH1CTRL, reg_value);
}

static void pdp_disable_scanout(struct adf_pdp_device *pdp)
{
	u32 reg_value;

	/* Turn off scanout */
	reg_value = pdp_read_reg(pdp, PVR5__PDP_PVR_PDP_GRPH1CTRL);
	reg_value &= ~(PVR5__GRPH1STREN_MASK);
	pdp_write_reg(pdp, PVR5__PDP_PVR_PDP_GRPH1CTRL, reg_value);
	/* Reset the base address in the fbdc module */
	pdp_write_fbdc_reg(pdp, PVR5__PDP_FBDC_INTRFC_BASE_ADDRESS,
					   0);
}

static bool pdp_vsync_triggered(struct adf_pdp_device *pdp)
{
	return atomic_read(&pdp->vsync_triggered) == 1;
}

static void pdp_enable_ints(struct adf_pdp_device *pdp)
{
	int err = 0;
	u32 reg_value;

	reg_value = pdp_read_reg(pdp, PVR5__PDP_PVR_PDP_INTENAB);
	reg_value &= ~(PVR5__INTEN_VBLNK0_MASK);
	reg_value |= 0x1 << PVR5__INTEN_VBLNK0_SHIFT;
	pdp_write_reg(pdp, PVR5__PDP_PVR_PDP_INTENAB, reg_value);

	err = apollo_enable_interrupt(pdp->pdev->dev.parent,
		APOLLO_INTERRUPT_TC5_PDP);
	if (err) {
		dev_err(&pdp->pdev->dev,
			"apollo_enable_interrupt failed (%d)\n", err);
	}
}

static void pdp_disable_ints(struct adf_pdp_device *pdp)
{
	int err = 0;
	u32 reg_value;

	reg_value = pdp_read_reg(pdp, PVR5__PDP_PVR_PDP_INTENAB);
	reg_value &= ~(PVR5__INTEN_VBLNK0_MASK);
	pdp_write_reg(pdp, PVR5__PDP_PVR_PDP_INTENAB, reg_value);

	err = apollo_disable_interrupt(pdp->pdev->dev.parent,
		APOLLO_INTERRUPT_TC5_PDP);
	if (err) {
		dev_err(&pdp->pdev->dev,
			"apollo_disable_interrupt failed (%d)\n", err);
	}
}

static void pdp_post(struct adf_device *adf_dev, struct adf_post *cfg,
	void *driver_state)
{
	int num_validates_snapshot = *(int *)driver_state;
	unsigned long flags;

	/* Set vsync wait timeout to 4x expected vsync */
	struct adf_pdp_device *pdp = devres_find(adf_dev->dev,
		pdp_devres_release, NULL, NULL);
	long timeout =
		msecs_to_jiffies((1000 / pdp->current_timings->v_refresh) * 4);

	/* Null-flip handling, used to push buffers off screen during an error
	 * state to stop them blocking subsequent rendering */
	if (cfg->n_bufs == 0) {
		pdp_disable_scanout(pdp);
		return;
	}

	/* We don't support changing the configuration on the fly */
	if (pdp->current_timings->h_display != cfg->bufs[0].w ||
		pdp->current_timings->v_display != cfg->bufs[0].h ||
		pdp->current_drm_format != cfg->bufs[0].format) {
		dev_err(&pdp->pdev->dev, "Unsupported configuration on post\n");
		return;
	}

	WARN_ON(cfg->n_bufs != 1);
	WARN_ON(cfg->mappings->sg_tables[0]->nents != 1);

	spin_lock_irqsave(&gFlipLock, flags);

	debugfs_dma_buf_set(cfg->bufs[0].dma_bufs[0]);

	/* Set surface address and enable the scanouts */
	pdp_enable_scanout(pdp, sg_phys(cfg->mappings->sg_tables[0]->sgl) -
				pdp->pdata->memory_base);

	atomic_set(&pdp->vsync_triggered, 0);

	spin_unlock_irqrestore(&gFlipLock, flags);

	/* Wait until the buffer is on-screen, so we know the previous buffer
	 * has been retired and off-screen.
	 *
	 * If vsync was already off when this post was serviced, we need to
	 * enable the vsync again briefly so the register updates we shadowed
	 * above get applied and we don't signal the fence prematurely. One
	 * vsync afterwards, we'll disable the vsync again.
	 */
	if (!atomic_xchg(&pdp->vsync_state, 1))
		pdp_enable_ints(pdp);

	if (wait_event_timeout(pdp->vsync_wait_queue,
		pdp_vsync_triggered(pdp), timeout) == 0) {
		dev_err(&pdp->pdev->dev, "Post VSync wait timeout");
		/* Undefined behaviour if this times out */
	}

	pdp->num_posts = num_validates_snapshot;
}

static bool pdp_supports_event(struct adf_obj *obj, enum adf_event_type type)
{
	switch (obj->type) {
	case ADF_OBJ_INTERFACE:
	{
		switch (type) {
		case ADF_EVENT_VSYNC:
			return true;
		default:
			return false;
		}
	}
	default:
		return false;
	}
}

static void pdp_irq_handler(void *data)
{
	struct adf_pdp_device *pdp = data;
	unsigned long flags;
	u32 int_status;

	int_status = pdp_read_reg(pdp, PVR5__PDP_PVR_PDP_INTSTAT);

	spin_lock_irqsave(&gFlipLock, flags);

	/* If we're idle, and a vsync disable was requested, do it now.
	 * This code assumes that the HWC will always re-enable vsync
	 * explicitly before posting new configurations.
	 */
	if (atomic_read(&pdp->num_validates) == pdp->num_posts) {
		if (!atomic_read(&pdp->requested_vsync_state)) {
			pdp_disable_ints(pdp);
			atomic_set(&pdp->vsync_state, 0);
		}
	}

	if ((int_status & PVR5__INTS_VBLNK0_MASK)) {
		/* Notify the framework of the just occurred vblank */
		adf_vsync_notify(&pdp->adf_interface, ktime_get());
		atomic_set(&pdp->vsync_triggered, 1);
		wake_up(&pdp->vsync_wait_queue);
	}

	spin_unlock_irqrestore(&gFlipLock, flags);
}

static void pdp_set_event(struct adf_obj *obj, enum adf_event_type type,
	bool enabled)
{
	struct adf_pdp_device *pdp;
	bool old;

	switch (type) {
	case ADF_EVENT_VSYNC:
	{
		pdp = devres_find(obj->parent->dev, pdp_devres_release,
				  NULL, NULL);
		atomic_set(&pdp->requested_vsync_state, enabled);
		if (enabled) {
			old = atomic_xchg(&pdp->vsync_state, enabled);
			if (!old)
				pdp_enable_ints(pdp);
		}
		break;
	}
	default:
		BUG();
	}
}

static int pdp_unblank_hdmi(struct adf_pdp_device *pdp)
{
	int err = 0, i;
	u32 reg_value;

	/* Powering up the ADV7511 sometimes doesn't come up immediately, so
	 * give multiple power ons.
	 */
	for (i = 0; i < 6; i++) {
		pdp_write_i2c(pdp, 0x41, 0x10);
		msleep(500);
	}
	msleep(1000);
	reg_value = pdp_read_i2c(pdp, 0x41);
	if (reg_value == 0x10) {
		dev_err(&pdp->pdev->dev, "i2c: ADV7511 powered up\n");
	} else {
		dev_err(&pdp->pdev->dev, "i2c: Failed to power up ADV7511\n");
		err = -EFAULT;
	}

	return err;
}

static void pdp_blank_hdmi(struct adf_pdp_device *pdp)
{
	pdp_write_i2c(pdp, 0x41, 0x50);
}

static void pdp_enable_hdmi(struct adf_pdp_device *pdp)
{
	u32 reg_value = 0;
	int i;

	/* Set scl clock.
	   Assuming i2c_master clock is at 50 MHz */
	iowrite32(0x18, pdp->i2c_regs);

	reg_value = pdp_read_i2c(pdp, 0xf5);
	if (reg_value != 0x75) {
		dev_err(&pdp->pdev->dev, "i2c: 1st register read failed: %x\n",
			reg_value);
		goto err_out;
	}

	reg_value = pdp_read_i2c(pdp, 0xf6);
	if (reg_value != 0x11) {
		dev_err(&pdp->pdev->dev, "i2c: 2nd register read failed: %x\n",
			reg_value);
		goto err_out;
	}

	/* Check the HPD and Monitor Sense */
	for (i = 0; i < 50; i++) {
		reg_value = pdp_read_i2c(pdp, 0x42);
		if (reg_value == 0x70) {
			dev_err(&pdp->pdev->dev, "i2c: Hot Plug and Monitor Sense detected ...\n");
			break;
		} else if (reg_value == 0x50) {
			dev_err(&pdp->pdev->dev, "i2c: Only Hot Plug detected ...\n");
		} else if (reg_value == 0x03) {
			dev_err(&pdp->pdev->dev, "i2c: Only Monitor Sense detected ...\n");
		}
	}

	if (pdp_unblank_hdmi(pdp))
		goto err_out;

	/* Writing the fixed registers */
	pdp_write_i2c(pdp, 0x98, 0x03);
	pdp_write_i2c(pdp, 0x9a, 0xe0);
	pdp_write_i2c(pdp, 0x9c, 0x30);
	pdp_write_i2c(pdp, 0x9d, 0x61);
	pdp_write_i2c(pdp, 0xa2, 0xa4);
	pdp_write_i2c(pdp, 0xa3, 0xa4);
	pdp_write_i2c(pdp, 0xe0, 0xd0);
	pdp_write_i2c(pdp, 0xf9, 0x00);

	/* Starting video input */
	/* Disable I2S */
	pdp_write_i2c(pdp, 0x0c, 0x80);

	/* Select input video format */
	pdp_write_i2c(pdp, 0x15, 0x10);

	/* Select Colour Depth and output format */
	pdp_write_i2c(pdp, 0x16, 0x30);

	/* Select Aspect Ratio */
	pdp_write_i2c(pdp, 0x17, 0x02);

	/* Other settings */
	pdp_write_i2c(pdp, 0x48, 0x00);
	pdp_write_i2c(pdp, 0x55, 0x12);

	/* Select Picture Aspect Ratio */
	pdp_write_i2c(pdp, 0x56, 0x28);

	/* GC enable */
	pdp_write_i2c(pdp, 0x40, 0x80);

	/* 24 bits/pixel */
	pdp_write_i2c(pdp, 0x4c, 0x04);

	/* Select HDMI Mode */
	pdp_write_i2c(pdp, 0xaf, 0x16);

	/* Set VIC to Receiver */
	pdp_write_i2c(pdp, 0x3d, 0x04);

	for (i = 0; i < 50; i++) {
		reg_value = pdp_read_i2c(pdp, 0x3e);
		if (reg_value == 0x10) {
			dev_err(&pdp->pdev->dev, "i2c: VIC detected as 720P, 60 Hz, 16:9...\n");
			break;
		}
	}

	if (i == 50)
		dev_err(&pdp->pdev->dev, "i2c: Desired VIC not detected\n");

	/* Write to PD register again */
	pdp_write_i2c(pdp, 0x41, 0x10);

err_out:
	return;
}

static int pdp_modeset(struct adf_interface *intf,
	struct drm_mode_modeinfo *mode)
{
	const struct pdp_timing_data *tdata;
	struct adf_pdp_device *pdp;
	int mode_id, err = 0;
	u32 reg_value = 0;

	pdp = devres_find(intf->base.parent->dev, pdp_devres_release,
			  NULL, NULL);
	mode_id = pdp_mode_id(pdp, mode->vdisplay, mode->hdisplay);
	tdata = pdp_timing_data(pdp, mode_id);

	if (!tdata) {
		dev_err(&pdp->pdev->dev, "Failed to find mode for %ux%u\n",
			mode->hdisplay, mode->vdisplay);
		err = -ENXIO;
		goto err_out;
	}

	/* Make sure all the following register writes are applied instantly */
	reg_value  = 0x1 << PVR5__BYPASS_DOUBLE_BUFFERING_SHIFT;
	reg_value |= 0x1 << PVR5__REGISTERS_VALID_SHIFT;
	pdp_write_reg(pdp, PVR5__PDP_PVR_PDP_REGISTER_UPDATE_CTRL, reg_value);

	/* Power down mode */
	reg_value = 0x1 << PVR5__POWERDN_SHIFT;
	pdp_write_reg(pdp, PVR5__PDP_PVR_PDP_SYNCCTRL, reg_value);

	/* Background color (green) */
	reg_value = 0x0099FF66;
	pdp_write_reg(pdp, PVR5__PDP_PVR_PDP_BGNDCOL, reg_value);

	/* Set alpha blend mode to global alpha blending (10b / 2h) and
	 * disable everything else.
	 */
	reg_value = 0x2 << PVR5__GRPH1BLEND_SHIFT;
	pdp_write_reg(pdp, PVR5__PDP_PVR_PDP_GRPH1CTRL, reg_value);

	/* Global alpha */
	reg_value = 0xff << PVR5__GRPH1GALPHA_SHIFT;
	pdp_write_reg(pdp, PVR5__PDP_PVR_PDP_GRPH1BLND, reg_value);

	/* Reset base addr of the non-FBCDC part. This is not used. */
	pdp_write_reg(pdp, PVR5__PDP_PVR_PDP_GRPH1_BASEADDR, 0);

	/* Graphics video pixel format:
	 * 01000b / 8h 8-bit alpha + 24-bit rgb888 [RGBA].
	 */
	pdp->current_drm_format = DRM_FORMAT_BGRA8888_DIRECT_16x4;
	reg_value = pdp_format(pdp->current_drm_format)
			<< PVR5__GRPH1PIXFMT_SHIFT;
	pdp_write_reg(pdp, PVR5__PDP_PVR_PDP_GRPH1SURF, reg_value);

	/* Reset position of the plane */
	reg_value  = 0 << PVR5__GRPH1XSTART_SHIFT;
	reg_value |= 0 << PVR5__GRPH1YSTART_SHIFT;
	pdp_write_reg(pdp, PVR5__PDP_PVR_PDP_GRPH1POSN, reg_value);

	/* Stride of surface in 16byte words - 1 */
	reg_value = (tdata->h_display * 4 / 16 - 1) << PVR5__GRPH1STRIDE_SHIFT;
	pdp_write_reg(pdp, PVR5__PDP_PVR_PDP_GRPH1STRIDE, reg_value);

	/* Size:
	 * Width of surface in pixels - 1
	 * Height of surface in lines - 1 */
	reg_value  = (tdata->h_display - 1) << PVR5__GRPH1WIDTH_SHIFT;
	reg_value |= (tdata->v_display - 1) << PVR5__GRPH1HEIGHT_SHIFT;
	pdp_write_reg(pdp, PVR5__PDP_PVR_PDP_GRPH1SIZE, reg_value);

	/* H-time */
	reg_value  = tdata->h_back_porch << PVR5__HBPS_SHIFT;
	reg_value |= tdata->h_total << PVR5__HT_SHIFT;
	pdp_write_reg(pdp, PVR5__PDP_PVR_PDP_HSYNC1, reg_value);
	reg_value  = tdata->h_active_start << PVR5__HAS_SHIFT;
	reg_value |= tdata->h_left_border << PVR5__HLBS_SHIFT;
	pdp_write_reg(pdp, PVR5__PDP_PVR_PDP_HSYNC2, reg_value);
	reg_value  = tdata->h_front_porch << PVR5__HFPS_SHIFT;
	reg_value |= tdata->h_right_border << PVR5__HRBS_SHIFT;
	pdp_write_reg(pdp, PVR5__PDP_PVR_PDP_HSYNC3, reg_value);

	/* V-time */
	reg_value  = tdata->v_back_porch << PVR5__VBPS_SHIFT;
	reg_value |= tdata->v_total << PVR5__VT_SHIFT;
	pdp_write_reg(pdp, PVR5__PDP_PVR_PDP_VSYNC1, reg_value);
	reg_value  = tdata->v_active_start << PVR5__VAS_SHIFT;
	reg_value |= tdata->v_top_border << PVR5__VTBS_SHIFT;
	pdp_write_reg(pdp, PVR5__PDP_PVR_PDP_VSYNC2, reg_value);
	reg_value  = tdata->v_front_porch << PVR5__VFPS_SHIFT;
	reg_value |= tdata->v_bottom_border << PVR5__VBBS_SHIFT;
	pdp_write_reg(pdp, PVR5__PDP_PVR_PDP_VSYNC3, reg_value);

	/* Horizontal data enable */
	reg_value  = tdata->h_left_border << PVR5__HDES_SHIFT;
	reg_value |= tdata->h_front_porch << PVR5__HDEF_SHIFT;
	pdp_write_reg(pdp, PVR5__PDP_PVR_PDP_HDECTRL, reg_value);

	/* Vertical data enable */
	reg_value  = tdata->v_top_border << PVR5__VDES_SHIFT;
	reg_value |= tdata->v_front_porch << PVR5__VDEF_SHIFT;
	pdp_write_reg(pdp, PVR5__PDP_PVR_PDP_VDECTRL, reg_value);

	/* Vertical event start and vertical fetch start */
	reg_value  = tdata->v_back_porch << PVR5__VFETCH_SHIFT;
	reg_value |= tdata->v_bottom_border << PVR5__VEVENT_SHIFT;
	pdp_write_reg(pdp, PVR5__PDP_PVR_PDP_VEVENT, reg_value);

	/* Now enable the fbdc module (direct_16x4) */
	/* Set the number of tiles per plane */
	pdp_write_fbdc_reg(pdp, PVR5__PDP_FBDC_INTRFC_NUM_TILES,
		(tdata->h_display * tdata->v_display) / (16 * 4));
	/* Set the number of the tile per line */
	pdp_write_fbdc_reg(pdp, PVR5__PDP_FBDC_INTRFC_PER_LINE,
					   tdata->h_display / 16);
	/* Set the color format */
	pdp_write_fbdc_reg(pdp, PVR5__PDP_FBDC_INTRFC_PIXEL_FORMAT, 0xc);
	/* Reset base address */
	pdp_write_fbdc_reg(pdp, PVR5__PDP_FBDC_INTRFC_BASE_ADDRESS, 0x0);
	/* Set invalidate request */
	reg_value = pdp_read_reg(pdp, PVR5__PDP_PVR_PDP_SYNCCTRL);
	if ((reg_value & PVR5__VSPOL_MASK) >> PVR5__VSPOL_SHIFT == 0x1) {
		pdp_write_fbdc_reg(pdp,
			PVR5__PDP_FBDC_INTRFC_INVALIDATE_REQUEST, 0x1);
	} else {
		pdp_write_fbdc_reg(pdp,
			PVR5__PDP_FBDC_INTRFC_INVALIDATE_REQUEST, 0x0);
	}

	/* Enable vsync again */
	reg_value = pdp_read_reg(pdp, PVR5__PDP_PVR_PDP_SYNCCTRL);
	reg_value &= ~(PVR5__SYNCACTIVE_MASK);
	reg_value |= 0x1 << PVR5__SYNCACTIVE_SHIFT;
	reg_value &= ~(PVR5__BLNKPOL_MASK);
	reg_value |= 0x1 << PVR5__BLNKPOL_SHIFT;
	pdp_write_reg(pdp, PVR5__PDP_PVR_PDP_SYNCCTRL, reg_value);

	/* Update control */
	reg_value  = 0x1 << PVR5__USE_VBLANK_SHIFT;
	reg_value |= 0x1 << PVR5__REGISTERS_VALID_SHIFT;
	pdp_write_reg(pdp, PVR5__PDP_PVR_PDP_REGISTER_UPDATE_CTRL, reg_value);

	intf->current_mode = *mode;
	pdp->current_timings = tdata;

	pdp_enable_hdmi(pdp);

err_out:
	return err;
}

static int pdp_blank(struct adf_interface *intf,
	u8 state)
{
	struct adf_pdp_device *pdp;
	u32 reg_value;

	pdp = devres_find(intf->base.parent->dev, pdp_devres_release,
			  NULL, NULL);

	if (state != DRM_MODE_DPMS_OFF && state != DRM_MODE_DPMS_ON)
		return -EINVAL;

	reg_value = pdp_read_reg(pdp, PVR5__PDP_PVR_PDP_SYNCCTRL);
	switch (state) {
	case DRM_MODE_DPMS_OFF:
		reg_value &= ~(PVR5__POWERDN_MASK);
		reg_value |= 0x1 << PVR5__POWERDN_SHIFT;
/*		pdp_blank_hdmi(pdp);*/
		break;
	case DRM_MODE_DPMS_ON:
		reg_value &= ~(PVR5__POWERDN_MASK);
/*		pdp_unblank_hdmi(pdp);*/
		break;
	}
	pdp_write_reg(pdp, PVR5__PDP_PVR_PDP_SYNCCTRL, reg_value);

	return 0;
}

static int pdp_alloc_simple_buffer(struct adf_interface *intf, u16 w, u16 h,
	u32 format, struct dma_buf **dma_buf, u32 *offset, u32 *pitch)
{
	u32 size = w * h * pdp_format_bpp(format);
	struct adf_pdp_device *pdp;
	struct ion_handle *hdl;
	int err = 0;

	pdp = devres_find(intf->base.parent->dev, pdp_devres_release,
			  NULL, NULL);
	hdl = ion_alloc(pdp->ion_client, size, 0,
		(1 << pdp->pdata->ion_heap_id), 0);
	if (IS_ERR(hdl)) {
		err = PTR_ERR(hdl);
		dev_err(&pdp->pdev->dev, "ion_alloc failed (%d)\n", err);
		goto err_out;
	}
	*dma_buf = ion_share_dma_buf(pdp->ion_client, hdl);
	if (IS_ERR(*dma_buf)) {
		err = PTR_ERR(hdl);
		dev_err(&pdp->pdev->dev,
			"ion_share_dma_buf failed (%d)\n", err);
		goto err_free_buffer;
	}
	*pitch = w * pdp_format_bpp(format);
	*offset = 0;
err_free_buffer:
	ion_free(pdp->ion_client, hdl);
err_out:
	return err;
}

static int pdp_describe_simple_post(struct adf_interface *intf,
	struct adf_buffer *fb, void *data, size_t *size)
{
	struct adf_post_ext *post_ext = data;
	static int post_id;

	struct drm_clip_rect full_screen = {
		.x2 = ADF_PDP_WIDTH,
		.y2 = ADF_PDP_HEIGHT,
	};

	/* NOTE: an upstream ADF bug means we can't test *size instead */
	BUG_ON(ADF_MAX_CUSTOM_DATA_SIZE < sizeof(struct adf_post_ext) +
				1 * sizeof(struct adf_buffer_config_ext));

	*size = sizeof(struct adf_post_ext) +
		1 * sizeof(struct adf_buffer_config_ext);

	post_ext->post_id = ++post_id;

	post_ext->bufs_ext[0].crop        = full_screen;
	post_ext->bufs_ext[0].display     = full_screen;
	post_ext->bufs_ext[0].transform   = ADF_BUFFER_TRANSFORM_NONE_EXT;
	post_ext->bufs_ext[0].blend_type  = ADF_BUFFER_BLENDING_PREMULT_EXT;
	post_ext->bufs_ext[0].plane_alpha = 0xff;

	return 0;
}

static int
adf_pdp_open(struct adf_obj *obj, struct inode *inode, struct file *file)
{
	struct adf_device *dev = (struct adf_device *)obj->parent;
	struct adf_pdp_device *pdp;

	pdp = devres_find(dev->dev, pdp_devres_release, NULL, NULL);

	atomic_inc(&pdp->refcount);
	return 0;
}

static void
adf_pdp_release(struct adf_obj *obj, struct inode *inode, struct file *file)
{
	struct adf_device *dev = (struct adf_device *)obj->parent;
	struct sync_fence *release_fence;
	struct adf_pdp_device *pdp;

	pdp = devres_find(dev->dev, pdp_devres_release, NULL, NULL);

	if (atomic_dec_return(&pdp->refcount))
		return;

	/* Make sure we have no outstanding posts waiting */
	atomic_set(&pdp->vsync_triggered, 1);
	wake_up_all(&pdp->vsync_wait_queue);
	/* This special "null" flip works around a problem with ADF
	 * which leaves buffers pinned by the display engine even
	 * after all ADF clients have closed.
	 *
	 * The "null" flip is pipelined like any other. The user won't
	 * be able to unload this module until it has been posted.
	 */
	release_fence = adf_device_post(dev, NULL, 0, NULL, 0, NULL, 0);
	if (IS_ERR_OR_NULL(release_fence)) {
		dev_err(dev->dev,
			"Failed to queue null flip command (err=%d).\n",
			(int)PTR_ERR(release_fence));
		return;
	}

	sync_fence_put(release_fence);
}

static int adf_img_validate_custom_format(struct adf_device *dev,
	struct adf_buffer *buf)
{
	int i;

	for (i = 0; pdp_format_table[i].drm_format != 0; i++) {
		if (pdp_format_table[i].drm_format == buf->format)
			return 1;
	}
	return 0;
}

static int pdp_validate(struct adf_device *dev, struct adf_post *cfg,
	void **driver_state)
{
	struct adf_pdp_device *pdp;
	int err;

	pdp = devres_find(dev->dev, pdp_devres_release, NULL, NULL);

	err = adf_img_validate_simple(dev, cfg, driver_state);
	if (err == 0 && cfg->mappings) {
		/* We store a snapshot of num_validates in driver_state at the
		 * time validate was called, which will be passed to the post
		 * function. This snapshot is copied into (i.e. overwrites)
		 * num_posts, rather then simply incrementing num_posts, to
		 * handle cases e.g. during fence timeouts where validates
		 * are called without corresponding posts.
		 */
		int *validates = kmalloc(sizeof(*validates), GFP_KERNEL);
		*validates = atomic_inc_return(&pdp->num_validates);
		*driver_state = validates;
	} else {
		*driver_state = NULL;
	}
	return err;
}

static void pdp_state_free(struct adf_device *dev, void *driver_state)
{
	kfree(driver_state);
}

static struct adf_device_ops adf_pdp_device_ops = {
	.owner = THIS_MODULE,
	.base = {
		.open = adf_pdp_open,
		.release = adf_pdp_release,
		.ioctl = adf_img_ioctl,
	},
	.validate_custom_format = adf_img_validate_custom_format,
	.validate = pdp_validate,
	.post = pdp_post,
	.state_free = pdp_state_free,
};

static struct adf_interface_ops adf_pdp_interface_ops = {
	.base = {
		.supports_event = pdp_supports_event,
		.set_event = pdp_set_event,
	},
	.modeset = pdp_modeset,
	.blank = pdp_blank,
	.alloc_simple_buffer = pdp_alloc_simple_buffer,
	.describe_simple_post = pdp_describe_simple_post,
};

static struct adf_overlay_engine_ops adf_pdp_overlay_ops = {
	.supported_formats = &pdp_supported_formats[0],
	.n_supported_formats = NUM_SUPPORTED_FORMATS,
};

static int adf_pdp_probe_device(struct platform_device *pdev)
{
	struct apollo_pdp_platform_data *pdata = pdev->dev.platform_data;
	struct pci_dev *pci_dev = to_pci_dev(pdev->dev.parent);
	int err = 0, i, default_mode_id;
	struct adf_pdp_device *pdp;
	struct resource *registers;
	u32 core_id, core_rev;

	pdp = devres_alloc(pdp_devres_release, sizeof(*pdp), GFP_KERNEL);
	if (!pdp) {
		err = -ENOMEM;
		goto err_out;
	}
	devres_add(&pdev->dev, pdp);

	pdp->pdata = pdata;
	pdp->pdev = pdev;

	err = pci_enable_device(pci_dev);
	if (err) {
		dev_err(&pdev->dev,
			"Failed to enable PDP pci device (%d)\n", err);
		goto err_out;
	}

	atomic_set(&pdp->refcount, 0);
	atomic_set(&pdp->num_validates, 0);
	pdp->num_posts = 0;

	pdp->ion_client = ion_client_create(pdata->ion_device, "adf_pdp");
	if (IS_ERR(pdp->ion_client)) {
		err = PTR_ERR(pdp->ion_client);
		dev_err(&pdev->dev,
			"Failed to create PDP ION client (%d)\n", err);
		goto err_disable_pci;
	}

	registers = platform_get_resource_byname(pdev,
						 IORESOURCE_MEM,
						 "tc5-pdp2-regs");
	pdp->regs = devm_ioremap_resource(&pdev->dev, registers);
	if (IS_ERR(pdp->regs)) {
		err = PTR_ERR(pdp->regs);
		dev_err(&pdev->dev, "Failed to map PDP registers (%d)\n", err);
		goto err_destroy_ion_client;
	}
	pdp->regs_size = resource_size(registers);

	registers = platform_get_resource_byname(pdev,
						 IORESOURCE_MEM,
						 "tc5-pdp2-fbdc-regs");
	pdp->fbdc_regs = devm_ioremap_resource(&pdev->dev, registers);
	if (IS_ERR(pdp->fbdc_regs)) {
		err = PTR_ERR(pdp->fbdc_regs);
		dev_err(&pdev->dev, "Failed to map PDP fbdc registers (%d)\n",
			err);
		goto err_destroy_ion_client;
	}
	pdp->fbdc_regs_size = resource_size(registers);

	registers = platform_get_resource_byname(pdev,
						 IORESOURCE_MEM,
						 "tc5-adv5711-regs");
	pdp->i2c_regs = devm_ioremap_resource(&pdev->dev, registers);
	if (IS_ERR(pdp->i2c_regs)) {
		err = PTR_ERR(pdp->i2c_regs);
		dev_err(&pdev->dev, "Failed to map ADV5711 i2c registers (%d)\n",
			err);
		goto err_destroy_ion_client;
	}
	pdp->i2c_regs_size = resource_size(registers);

	core_id = pdp_read_reg(pdp, PVR5__PDP_PVR_PDP_CORE_ID);
	core_rev = pdp_read_reg(pdp, PVR5__PDP_PVR_PDP_CORE_REV);

	dev_err(&pdev->dev, "pdp2 core id/rev: %d.%d.%d/%d.%d.%d\n",
		(core_id & PVR5__GROUP_ID_MASK) >> PVR5__GROUP_ID_SHIFT,
		(core_id & PVR5__CORE_ID_MASK) >> PVR5__CORE_ID_SHIFT,
		(core_id & PVR5__CONFIG_ID_MASK) >> PVR5__CONFIG_ID_SHIFT,
		(core_rev & PVR5__MAJOR_REV_MASK) >> PVR5__MAJOR_REV_SHIFT,
		(core_rev & PVR5__MINOR_REV_MASK) >> PVR5__MINOR_REV_SHIFT,
		(core_rev & PVR5__MAINT_REV_MASK) >> PVR5__MAINT_REV_SHIFT);


	err = adf_device_init(&pdp->adf_device, &pdp->pdev->dev,
		&adf_pdp_device_ops, "pdp_device");
	if (err) {
		dev_err(&pdev->dev, "Failed to init ADF device (%d)\n", err);
		goto err_destroy_ion_client;
	}

	err = adf_interface_init(&pdp->adf_interface, &pdp->adf_device,
		ADF_INTF_DVI, 0, ADF_INTF_FLAG_PRIMARY, &adf_pdp_interface_ops,
		"pdp_interface");
	if (err) {
		dev_err(&pdev->dev, "Failed to init ADF interface (%d)\n", err);
		goto err_destroy_adf_device;
	}

	err = adf_overlay_engine_init(&pdp->adf_overlay, &pdp->adf_device,
		&adf_pdp_overlay_ops, "pdp_overlay");
	if (err) {
		dev_err(&pdev->dev, "Failed to init ADF overlay (%d)\n", err);
		goto err_destroy_adf_interface;
	}

	err = adf_attachment_allow(&pdp->adf_device, &pdp->adf_overlay,
		&pdp->adf_interface);
	if (err) {
		dev_err(&pdev->dev, "Failed to attach overlay (%d)\n", err);
		goto err_destroy_adf_overlay;
	}

	pdp->num_supported_modes = pdp_mode_count(pdp);
	pdp->supported_modes = kzalloc(sizeof(*pdp->supported_modes)
		* pdp->num_supported_modes, GFP_KERNEL);

	if (!pdp->supported_modes) {
		dev_err(&pdev->dev, "Failed to allocate supported modeinfo structs\n");
		err = -ENOMEM;
		goto err_destroy_adf_overlay;
	}

	for (i = 0; i < pdp->num_supported_modes; i++)
		pdp_mode_to_drm_mode(pdp, i, &pdp->supported_modes[i]);

	default_mode_id = pdp_mode_id(pdp, pdp_display_height,
		pdp_display_width);
	if (default_mode_id == -1) {
		default_mode_id = 0;
		dev_err(&pdev->dev, "No modeline found for requested display size (%dx%d)\n",
			pdp_display_width, pdp_display_height);
	}

	/* Initial modeset... */
	err = pdp_modeset(&pdp->adf_interface,
		&pdp->supported_modes[default_mode_id]);
	if (err) {
		dev_err(&pdev->dev, "Initial modeset failed (%d)\n", err);
		goto err_destroy_modelist;
	}

	err = adf_hotplug_notify_connected(&pdp->adf_interface,
		pdp->supported_modes, pdp->num_supported_modes);
	if (err) {
		dev_err(&pdev->dev, "Initial hotplug notify failed (%d)\n",
			err);
		goto err_destroy_modelist;
	}
	err = apollo_set_interrupt_handler(pdp->pdev->dev.parent,
					   APOLLO_INTERRUPT_TC5_PDP,
					   pdp_irq_handler, pdp);
	if (err) {
		dev_err(&pdev->dev, "Failed to set interrupt handler (%d)\n",
			err);
		goto err_destroy_modelist;
	}

	init_waitqueue_head(&pdp->vsync_wait_queue);
	atomic_set(&pdp->requested_vsync_state, 0);
	atomic_set(&pdp->vsync_state, 0);

	if (debugfs_dma_buf_init("pdp_raw"))
		dev_err(&pdev->dev, "Failed to create debug fs file for raw access\n");

	return err;
err_destroy_modelist:
	kfree(pdp->supported_modes);
err_destroy_adf_overlay:
	adf_overlay_engine_destroy(&pdp->adf_overlay);
err_destroy_adf_interface:
	adf_interface_destroy(&pdp->adf_interface);
err_destroy_adf_device:
	adf_device_destroy(&pdp->adf_device);
err_destroy_ion_client:
	ion_client_destroy(pdp->ion_client);
err_disable_pci:
	pci_disable_device(pci_dev);
err_out:
	dev_err(&pdev->dev, "Failed to initialise PDP device\n");
	return err;
}

static int adf_pdp_remove_device(struct platform_device *pdev)
{
	struct pci_dev *pci_dev = to_pci_dev(pdev->dev.parent);
	struct adf_pdp_device *pdp;
	int err = 0;

	pdp = devres_find(&pdev->dev, pdp_devres_release, NULL, NULL);

	debugfs_dma_buf_deinit();

	/* Disable scanout */
	pdp_disable_scanout(pdp);
	pdp_disable_ints(pdp);
	apollo_set_interrupt_handler(pdp->pdev->dev.parent,
				     APOLLO_INTERRUPT_TC5_PDP,
				     NULL, NULL);
	/* Disable hdmi */
	pdp_blank_hdmi(pdp);
	kfree(pdp->supported_modes);
	adf_overlay_engine_destroy(&pdp->adf_overlay);
	adf_interface_destroy(&pdp->adf_interface);
	adf_device_destroy(&pdp->adf_device);
	ion_client_destroy(pdp->ion_client);
	pci_disable_device(pci_dev);
	return err;
}

static void adf_pdp_shutdown_device(struct platform_device *pdev)
{
	/* No cleanup needed, all done in remove_device */
}

static struct platform_device_id pdp_platform_device_id_table[] = {
	{ .name = APOLLO_DEVICE_NAME_PDP, .driver_data = 0 },
	{ },
};

static struct platform_driver pdp_platform_driver = {
	.probe = adf_pdp_probe_device,
	.remove = adf_pdp_remove_device,
	.shutdown = adf_pdp_shutdown_device,
	.driver = {
		.name = DRV_NAME,
	},
	.id_table = pdp_platform_device_id_table,
};

static int __init adf_pdp_init(void)
{
	return platform_driver_register(&pdp_platform_driver);
}

static void __exit adf_pdp_exit(void)
{
	platform_driver_unregister(&pdp_platform_driver);
}

module_init(adf_pdp_init);
module_exit(adf_pdp_exit);
