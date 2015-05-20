/*************************************************************************/ /*!
@File           adf_pdp.c
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
/* vi: set ts=8: */

/*
 * This is an example ADF display driver for the testchip's PDP output
 */

/* #define SUPPORT_ADF_PDP_FBDEV */

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

#ifdef SUPPORT_ADF_PDP_FBDEV
#include <video/adf_fbdev.h>
#endif

#include PVR_ANDROID_ION_HEADER

/* for sync_fence_put */
#include PVR_ANDROID_SYNC_HEADER

#include "apollo_drv.h"
#include "adf_common.h"

#include "pdp_regs.h"
#include "tcf_rgbpdp_regs.h"
#include "tcf_pll.h"

#include "pvrmodule.h"

#define DRV_NAME APOLLO_DEVICE_NAME_PDP

MODULE_DESCRIPTION("APOLLO PDP display driver");

static int pdp_display_width = ADF_PDP_WIDTH;
static int pdp_display_height = ADF_PDP_HEIGHT;
module_param(pdp_display_width, int, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(pdp_display_width, "PDP display width");
module_param(pdp_display_height, int, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(pdp_display_height, "PDP display height");

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
	u32 clock_freq;
};

static const struct pdp_timing_data pdp_supported_modes[] = {
	{
		.h_display		=	640,
		.h_back_porch		=	64,
		.h_total		=	800,
		.h_active_start		=	144,
		.h_left_border		=	144,
		.h_right_border		=	784,
		.h_front_porch		=	784,

		.v_display		=	480,
		.v_back_porch		=	7,
		.v_total		=	497,
		.v_active_start		=	16,
		.v_top_border		=	16,
		.v_bottom_border	=	496,
		.v_front_porch		=	496,

		.v_refresh		=	60,
		.clock_freq		=	23856000,
	},
	{
		.h_display		=	800,
		.h_back_porch		=	80,
		.h_total		=	1024,
		.h_active_start		=	192,
		.h_left_border		=	192,
		.h_right_border		=	992,
		.h_front_porch		=	992,

		.v_display		=	600,
		.v_back_porch		=	7,
		.v_total		=	621,
		.v_active_start		=	20,
		.v_top_border		=	20,
		.v_bottom_border	=	620,
		.v_front_porch		=	620,

		.v_refresh		=	60,
		.clock_freq		=	38154000,
	},
	{
		.h_display		=	1024,
		.h_back_porch		=	104,
		.h_total		=	1344,
		.h_active_start		=	264,
		.h_left_border		=	264,
		.h_right_border		=	1288,
		.h_front_porch		=	1288,

		.v_display		=	768,
		.v_back_porch		=	7,
		.v_total		=	795,
		.v_active_start		=	26,
		.v_top_border		=	26,
		.v_bottom_border	=	794,
		.v_front_porch		=	794,

		.v_refresh		=	59,
		.clock_freq		=	64108000,
	},
	{
		.h_display		=	1280,
		.h_back_porch		=	136,
		.h_total		=	1664,
		.h_active_start		=	328,
		.h_left_border		=	328,
		.h_right_border		=	1608,
		.h_front_porch		=	1608,

		.v_display		=	720,
		.v_back_porch		=	7,
		.v_total		=	745,
		.v_active_start		=	24,
		.v_top_border		=	24,
		.v_bottom_border	=	744,
		.v_front_porch		=	744,

		.v_refresh		=	59,
		.clock_freq		=	74380000,
	},
	{
		.h_display		=	1280,
		.h_back_porch		=	136,
		.h_total		=	1680,
		.h_active_start		=	336,
		.h_left_border		=	336,
		.h_right_border		=	1616,
		.h_front_porch		=	1616,

		.v_display		=	768,
		.v_back_porch		=	7,
		.v_total		=	795,
		.v_active_start		=	26,
		.v_top_border		=	26,
		.v_bottom_border	=	794,
		.v_front_porch		=	794,

		.v_refresh		=	59,
		.clock_freq		=	80136000,
	},
	{
		.h_display		=	1280,
		.h_back_porch		=	136,
		.h_total		=	1680,
		.h_active_start		=	336,
		.h_left_border		=	336,
		.h_right_border		=	1616,
		.h_front_porch		=	1616,

		.v_display		=	800,
		.v_back_porch		=	7,
		.v_total		=	828,
		.v_active_start		=	27,
		.v_top_border		=	27,
		.v_bottom_border	=	827,
		.v_front_porch		=	827,

		.v_refresh		=	59,
		.clock_freq		=	83462000,
	},
	{
		.h_display		=	1280,
		.h_back_porch		=	136,
		.h_total		=	1712,
		.h_active_start		=	352,
		.h_left_border		=	352,
		.h_right_border		=	1632,
		.h_front_porch		=	1632,

		.v_display		=	1024,
		.v_back_porch		=	7,
		.v_total		=	1059,
		.v_active_start		=	34,
		.v_top_border		=	34,
		.v_bottom_border	=	1058,
		.v_front_porch		=	1058,

		.v_refresh		=	60,
		.clock_freq		=	108780000,
	},
	{}
};


struct adf_pdp_device {
	struct ion_client *ion_client;

	struct adf_device adf_device;
	struct adf_interface adf_interface;
	struct adf_overlay_engine adf_overlay;
#ifdef SUPPORT_ADF_PDP_FBDEV
	struct adf_fbdev adf_fbdev;
#endif

	struct platform_device *pdev;

	struct apollo_pdp_platform_data *pdata;

	void __iomem *regs;
	resource_size_t regs_size;
	resource_size_t regs_start;

	void __iomem *pll_regs;
	resource_size_t pll_regs_size;
	resource_size_t pll_regs_start;

	struct drm_mode_modeinfo *supported_modes;
	int num_supported_modes;

	const struct pdp_timing_data *current_timings;

	atomic_t refcount;

	atomic_t num_validates;
	int num_posts;

	atomic_t vsync_triggered;
	wait_queue_head_t vsync_wait_queue;
	atomic_t requested_vsync_state;
	atomic_t vsync_state;
};

static const u32 pdp_supported_formats[] = {
	DRM_FORMAT_BGRA8888,
};
#define NUM_SUPPORTED_FORMATS 1

static const struct {
	u32 drm_format;
	u32 bytes_per_pixel;
	u32 pixfmt_word;
} pdp_format_table[] = {
	{ DRM_FORMAT_BGRA8888, 4, DCPDP_STR1SURF_FORMAT_ARGB8888 },
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
	const struct pdp_timing_data *pdp_mode = pdp_timing_data(pdp, mode_id);

	BUG_ON(pdp_mode == NULL);
	memset(drm_mode, 0, sizeof(*drm_mode));

	drm_mode->hdisplay = pdp_mode->h_display;
	drm_mode->vdisplay = pdp_mode->v_display;
	drm_mode->vrefresh = pdp_mode->v_refresh;

#if 0
	/* We only currently set the bare-minimum of the drm_mode required
	 * by adf. We internally manage the timings etc. keyed on the 
	 * height/width/refresh of the mode, so we don't need to set
	 * the following. */
	drm_mode->hsync_start = pdp_mode->h_front_porch;
	drm_mode->hsync_end = pdp_mode->h_total;
	drm_mode->htotal = pdp_mode->h_total;

	drm_mode->vsync_start = pdp_mode->v_bottom_border;
	drm_mode->vsync_end = pdp_mode->v_total;
	/* Clock speed converted from hz to khz */
	drm_mode->clock = (pdp_mode->clock_freq + 500) / 1000;
#endif
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

static void pll_write_reg(struct adf_pdp_device *pdp,
	resource_size_t reg_offset, u32 reg_value)
{
	BUG_ON(reg_offset > pdp->pll_regs_size-4);
	iowrite32(reg_value, pdp->pll_regs + reg_offset);
}

static void pdp_devres_release(struct device *dev, void *res)
{
	/* No extra cleanup needed */
}

static int request_pci_io_addr(struct pci_dev *dev, u32 index,
	resource_size_t offset, resource_size_t length)
{
	resource_size_t start, end;
	start = pci_resource_start(dev, index);
	end = pci_resource_end(dev, index);

	if ((start + offset + length - 1) > end)
		return -EIO;
	if (pci_resource_flags(dev, index) & IORESOURCE_IO) {
		if (request_region(start + offset, length, DRV_NAME) == NULL)
			return -EIO;
	} else {
		if (request_mem_region(start + offset, length, DRV_NAME)
			== NULL)
			return -EIO;
	}
	return 0;
}

static void release_pci_io_addr(struct pci_dev *dev, u32 index,
	resource_size_t start, resource_size_t length)
{
	if (pci_resource_flags(dev, index) & IORESOURCE_IO)
		release_region(start, length);
	else
		release_mem_region(start, length);
}


#ifdef SUPPORT_ADF_PDP_FBDEV
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
#endif /* SUPPORT_ADF_PDP_FBDDEV */

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

static void pdp_enable_scanout(struct adf_pdp_device *pdp)
{
	u32 reg_value;
	/* Turn on scanout */
	reg_value = pdp_read_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_STR1ADDRCTRL);
	reg_value &= ~(STR1STREN_MASK);
	reg_value |= 0x1 << STR1STREN_SHIFT;
	pdp_write_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_STR1ADDRCTRL, reg_value);
}

static void pdp_disable_scanout(struct adf_pdp_device *pdp)
{
	pdp_write_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_STR1ADDRCTRL, 0);
}

static bool pdp_vsync_triggered(struct adf_pdp_device *pdp)
{
	return atomic_read(&pdp->vsync_triggered) == 1;
}

static void pdp_post(struct adf_device *adf_dev, struct adf_post *cfg,
	void *driver_state)
{
	int num_validates_snapshot = *(int *)driver_state;
	dma_addr_t buf_addr;
	u32 reg_value = 0;

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

	WARN_ON(cfg->n_bufs != 1);
	WARN_ON(cfg->mappings->sg_tables[0]->nents != 1);

	buf_addr = sg_phys(cfg->mappings->sg_tables[0]->sgl);
	/* Convert the cpu address to a device address */
	buf_addr -= pdp->pdata->apollo_memory_base;

	/* Set surface register w/height, width & format */
	reg_value  = (cfg->bufs[0].w-1) << STR1WIDTH_SHIFT;
	reg_value |= (cfg->bufs[0].h-1) << STR1HEIGHT_SHIFT;
	reg_value |= pdp_format(cfg->bufs[0].format) << STR1PIXFMT_SHIFT;
	pdp_write_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_STR1SURF, reg_value);

	/* Set stride register */
	reg_value = (cfg->bufs[0].pitch[0] >> DCPDP_STR1POSN_STRIDE_SHIFT)-1;
	pdp_write_reg(pdp, TCF_RGBPDP_PVR_PDP_STR1POSN, reg_value);

	/* Set surface address without resetting any other bits in the
	 * register */
	reg_value  = pdp_read_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_STR1ADDRCTRL);
	reg_value &= ~(STR1BASE_MASK);
	reg_value |= (buf_addr >> DCPDP_STR1ADDRCTRL_BASE_ADDR_SHIFT)
		& STR1BASE_MASK;

	pdp_write_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_STR1ADDRCTRL, reg_value);
	pdp_enable_scanout(pdp);
	atomic_set(&pdp->vsync_triggered, 0);

	pdp->num_posts = num_validates_snapshot;

	/* Wait until the buffer is on-screen, so we know the previous buffer
	 * has been retired and off-screen.
	 *
	 * If vsync was already off when this post was serviced, we don't need
	 * to wait for it (note: this will cause tearing if done when the
	 * display is not blanked).
	 */
	if (atomic_read(&pdp->vsync_state)) {
		if (wait_event_timeout(pdp->vsync_wait_queue,
			pdp_vsync_triggered(pdp), timeout) == 0) {
			/* Timeout - continue as if vsync was triggered, as
			 * possible tearing is better than wedging */
			dev_err(&pdp->pdev->dev, "Post VSync wait timeout");
		}
	}
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

static void pdp_disable_vsync(struct adf_pdp_device *pdp)
{
	int err = 0;
	u32 reg_value = pdp_read_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_INTENAB);
	reg_value &= ~(0x1 << INTEN_VBLNK1_SHIFT);
	pdp_write_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_INTENAB, reg_value);

	err = apollo_disable_interrupt(&pdp->pdata->pdev->dev,
		APOLLO_INTERRUPT_PDP);
	if (err) {
		dev_err(&pdp->pdev->dev,
			"apollo_disable_interrupt failed (%d)\n", err);
	}
}

static void pdp_irq_handler(void *data)
{
	struct adf_pdp_device *pdp = data;
	u32 int_status = pdp_read_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_INTSTAT);

	if (int_status & INTS_VBLNK1_MASK)
		pdp_write_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_INTCLEAR,
			(0x1 << INTCLR_VBLNK1_SHIFT));

	/* If we're idle, and a vsync disable was requested, do it now.
	 * This code assumes that the HWC will always re-enable vsync
	 * explicitly before posting new configurations.
	 */
	if (atomic_read(&pdp->num_validates) == pdp->num_posts) {
		if (!atomic_read(&pdp->requested_vsync_state)) {
			pdp_disable_vsync(pdp);
			atomic_set(&pdp->vsync_state, 0);
		}
	}

	if (int_status & INTS_VBLNK1_MASK) {
		adf_vsync_notify(&pdp->adf_interface, ktime_get());
		atomic_set(&pdp->vsync_triggered, 1);
		wake_up(&pdp->vsync_wait_queue);
	}
}

static void pdp_enable_vsync(struct adf_pdp_device *pdp)
{
	int err = 0;
	u32 reg_value = pdp_read_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_INTENAB);
	reg_value |= (0x1 << INTEN_VBLNK1_SHIFT);
	pdp_write_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_INTENAB, reg_value);

	err = apollo_enable_interrupt(&pdp->pdata->pdev->dev,
		APOLLO_INTERRUPT_PDP);
	if (err) {
		dev_err(&pdp->pdev->dev,
			"apollo_enable_interrupt failed (%d)\n", err);
	}
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
				pdp_enable_vsync(pdp);
		}
		break;
	}
	default:
		BUG();
	}
}

static void pdp_set_clocks(struct adf_pdp_device *pdp, u32 clock_freq_hz)
{
	u32 clock_freq_mhz = (clock_freq_hz + 500000) / 1000000;

	pll_write_reg(pdp, TCF_PLL_PLL_PDP_CLK0, clock_freq_mhz);
	if (clock_freq_mhz >= 50)
		pll_write_reg(pdp, TCF_PLL_PLL_PDP_CLK1TO5, 0);
	else
		pll_write_reg(pdp, TCF_PLL_PLL_PDP_CLK1TO5, 0x3);

	pll_write_reg(pdp, TCF_PLL_PLL_PDP_DRP_GO, 1);
	udelay(1000);
	pll_write_reg(pdp, TCF_PLL_PLL_PDP_DRP_GO, 0);
}

static int pdp_modeset(struct adf_interface *intf,
	struct drm_mode_modeinfo *mode)
{
	u32 reg_value = 0;
	int err = 0;
	struct adf_pdp_device *pdp = devres_find(intf->base.parent->dev,
		pdp_devres_release, NULL, NULL);
	int mode_id = pdp_mode_id(pdp, mode->vdisplay, mode->hdisplay);
	const struct pdp_timing_data *tdata = pdp_timing_data(pdp, mode_id);


	if (!tdata) {
		dev_err(&pdp->pdev->dev, "Failed to find mode for %ux%u\n",
			mode->hdisplay, mode->vdisplay);
		err = -ENXIO;
		goto err_out;
	}
	/* Disable scanout */
	pdp_disable_scanout(pdp);
	/* Disable sync gen */
	reg_value = pdp_read_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_SYNCCTRL);
	reg_value &= ~(SYNCACTIVE_MASK);
	pdp_write_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_SYNCCTRL, reg_value);

	pdp_set_clocks(pdp, tdata->clock_freq);

	if (pdp_read_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_STRCTRL)
		!= 0x0000C010) {
		/* Buffer request threshold */
		pdp_write_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_STRCTRL,
			0x00001C10);
	}

	/* Border colour */
	pdp_write_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_BORDCOL, 0x00005544);

	/* Update control */
	pdp_write_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_UPDCTRL, 0);

	/* Set hsync */
	reg_value  = tdata->h_back_porch << HBPS_SHIFT;
	reg_value |= tdata->h_total << HT_SHIFT;
	pdp_write_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_HSYNC1, reg_value);

	reg_value  = tdata->h_active_start << HAS_SHIFT;
	reg_value |= tdata->h_left_border << HLBS_SHIFT;
	pdp_write_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_HSYNC2, reg_value);

	reg_value  = tdata->h_front_porch << HFPS_SHIFT;
	reg_value |= tdata->h_right_border << HRBS_SHIFT;
	pdp_write_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_HSYNC3, reg_value);

	/* Set vsync */
	reg_value  = tdata->v_back_porch << VBPS_SHIFT;
	reg_value |= tdata->v_total << VT_SHIFT;
	pdp_write_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_VSYNC1, reg_value);

	reg_value  = tdata->v_active_start << VAS_SHIFT;
	reg_value |= tdata->v_top_border << VTBS_SHIFT;
	pdp_write_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_VSYNC2, reg_value);

	reg_value  = tdata->v_front_porch << VFPS_SHIFT;
	reg_value |= tdata->v_bottom_border << VBBS_SHIFT;
	pdp_write_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_VSYNC3, reg_value);

	/* Horizontal data enable */
	reg_value  = tdata->h_active_start << HDES_SHIFT;
	reg_value |= tdata->h_front_porch << HDEF_SHIFT;
	pdp_write_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_HDECTRL, reg_value);

	/* Vertical data enable */
	reg_value  = tdata->v_active_start << VDES_SHIFT;
	reg_value |= tdata->v_front_porch << VDEF_SHIFT;
	pdp_write_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_VDECTRL, reg_value);

	/* Vertical event start and vertical fetch start */
	reg_value  = tdata->v_back_porch << VFETCH_SHIFT;
	reg_value |= tdata->v_front_porch << VEVENT_SHIFT;
	pdp_write_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_VEVENT, reg_value);

	/* Enable sync gen last and set up polarities of sync/blank */
	reg_value  = 0x1 << SYNCACTIVE_SHIFT;
	reg_value |= 0x1 << FIELDPOL_SHIFT;
	reg_value |= 0x1 << BLNKPOL_SHIFT;
	reg_value |= 0x1 << VSPOL_SHIFT;
	reg_value |= 0x1 << HSPOL_SHIFT;
	pdp_write_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_SYNCCTRL, reg_value);

	intf->current_mode = *mode;
	pdp->current_timings = tdata;

err_out:
	return err;
}

static int pdp_blank(struct adf_interface *intf,
	u8 state)
{
	u32 reg_value;
	struct adf_pdp_device *pdp = devres_find(intf->base.parent->dev,
		pdp_devres_release, NULL, NULL);

	if (state != DRM_MODE_DPMS_OFF &&
		state != DRM_MODE_DPMS_ON)
		return -EINVAL;

	reg_value = pdp_read_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_SYNCCTRL);
	switch (state) {
	case DRM_MODE_DPMS_OFF:
		reg_value |= 0x1 << POWERDN_SHIFT;
		break;
	case DRM_MODE_DPMS_ON:
		reg_value &= ~(POWERDN_MASK);
		break;
	}
	pdp_write_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_SYNCCTRL, reg_value);

	return 0;
}

#ifdef SUPPORT_ADF_PDP_FBDEV

static int pdp_alloc_simple_buffer(struct adf_interface *intf, u16 w, u16 h,
	u32 format, struct dma_buf **dma_buf, u32 *offset, u32 *pitch)
{
	struct adf_pdp_device *pdp = devres_find(intf->base.parent->dev,
		pdp_devres_release, NULL, NULL);
	int err = 0;
	u32 size = w * h * pdp_format_bpp(format);
	struct ion_handle *hdl = ion_alloc(pdp->ion_client, size, 0,
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
	/* No data required for simple post? */
	*size = 0;
	return 0;
}

#endif /* SUPPORT_ADF_PDP_FBDEV */

static int
adf_pdp_open(struct adf_obj *obj, struct inode *inode, struct file *file)
{
	struct adf_device *dev =
		(struct adf_device *)obj->parent;
	struct adf_pdp_device *pdp = devres_find(dev->dev,
		pdp_devres_release, NULL, NULL);
	atomic_inc(&pdp->refcount);
	return 0;
}

static void
adf_pdp_release(struct adf_obj *obj, struct inode *inode, struct file *file)
{
	struct adf_device *dev =
		(struct adf_device *)obj->parent;
	struct adf_pdp_device *pdp = devres_find(dev->dev,
		pdp_devres_release, NULL, NULL);
	struct sync_fence *release_fence;

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

static int pdp_validate(struct adf_device *dev, struct adf_post *cfg,
	void **driver_state)
{
	struct adf_pdp_device *pdp = devres_find(dev->dev,
		pdp_devres_release, NULL, NULL);
	int err = adf_img_validate_simple(dev, cfg, driver_state);
	if (err == 0 && cfg->mappings) {
		/* We store a snapshot of num_validates in driver_state at the
		 * time validate was called, which will be passed to the post
		 * function. This snapshot is copied into (i.e. overwrites)
		 * num_posts, rather then simply incrementing num_posts, to
		 * handle cases e.g. during fence timeouts where validates
		 * are called without corresponding posts.
		 */
		int *validates = kmalloc(sizeof(int), GFP_KERNEL);
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
	.state_free = pdp_state_free,
	.validate = pdp_validate,
	.post = pdp_post,
};

static struct adf_interface_ops adf_pdp_interface_ops = {
	.base = {
		.supports_event = pdp_supports_event,
		.set_event = pdp_set_event,
	},
	.modeset = pdp_modeset,
	.blank = pdp_blank,
#ifdef SUPPORT_ADF_PDP_FBDEV
	.alloc_simple_buffer = pdp_alloc_simple_buffer,
	.describe_simple_post = pdp_describe_simple_post,
#endif
};

static struct adf_overlay_engine_ops adf_pdp_overlay_ops = {
	.supported_formats = &pdp_supported_formats[0],
	.n_supported_formats = NUM_SUPPORTED_FORMATS,
};

#ifdef SUPPORT_ADF_PDP_FBDEV
static struct fb_ops adf_pdp_fb_ops = {
	.owner = THIS_MODULE,
	.fb_open = adf_fbdev_open,
	.fb_release = adf_fbdev_release,
	.fb_check_var = adf_fbdev_check_var,
	.fb_set_par = adf_fbdev_set_par,
	.fb_blank = adf_fbdev_blank,
	.fb_pan_display = adf_fbdev_pan_display,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
	.fb_mmap = adf_fbdev_mmap,
};
#endif

static int adf_pdp_probe_device(struct platform_device *pdev)
{
	struct adf_pdp_device *pdp;
	int err = 0;
	int i, default_mode_id;
	struct apollo_pdp_platform_data *pdata = pdev->dev.platform_data;
	pdp = devres_alloc(pdp_devres_release, sizeof(struct adf_pdp_device),
		GFP_KERNEL);
	if (!pdp) {
		err = -ENOMEM;
		goto err_out;
	}
	devres_add(&pdev->dev, pdp);

	pdp->pdata = pdata;
	pdp->pdev = pdev;

	err = pci_enable_device(pdata->pdev);
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

	err = request_pci_io_addr(pdata->pdev, DCPDP_REG_PCI_BASENUM,
		DCPDP_PCI_PDP_REG_OFFSET, DCPDP_PCI_PDP_REG_SIZE);
	if (err) {
		dev_err(&pdev->dev,
			"Failed to request PDP registers (%d)\n", err);
		goto err_destroy_ion_client;
	}

	pdp->regs_size = DCPDP_PCI_PDP_REG_SIZE;
	pdp->regs_start =
		pci_resource_start(pdata->pdev, DCPDP_REG_PCI_BASENUM)
		+ DCPDP_PCI_PDP_REG_OFFSET;

	pdp->regs = ioremap_nocache(pdp->regs_start, pdp->regs_size);
	if (!pdp->regs) {
		dev_err(&pdev->dev, "Failed to map PDP registers\n");
		err = -EIO;
		goto err_release_registers;
	}

	err = request_pci_io_addr(pdata->pdev, DCPDP_REG_PCI_BASENUM,
		DCPDP_PCI_PLL_REG_OFFSET, DCPDP_PCI_PLL_REG_SIZE);
	if (err) {
		dev_err(&pdev->dev,
			"Failed to request PLL registers (%d)\n", err);
		goto err_unmap_registers;
	}
	pdp->pll_regs_size = DCPDP_PCI_PLL_REG_SIZE;
	pdp->pll_regs_start =
		pci_resource_start(pdata->pdev, DCPDP_REG_PCI_BASENUM)
		+ DCPDP_PCI_PLL_REG_OFFSET;

	pdp->pll_regs = ioremap_nocache(pdp->pll_regs_start,
		pdp->pll_regs_size);
	if (!pdp->pll_regs) {
		dev_err(&pdev->dev, "Failed to map PLL registers\n");
		err = -EIO;
		goto err_release_pll_registers;
	}

	err = adf_device_init(&pdp->adf_device, &pdp->pdev->dev,
		&adf_pdp_device_ops, "pdp_device");
	if (err) {
		dev_err(&pdev->dev, "Failed to init ADF device (%d)\n", err);
		goto err_unmap_pll_registers;
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
	pdp->supported_modes = kzalloc(sizeof(struct drm_mode_modeinfo)
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
	err = apollo_set_interrupt_handler(&pdp->pdata->pdev->dev,
					   APOLLO_INTERRUPT_PDP,
					   pdp_irq_handler, pdp);
	if (err) {
		dev_err(&pdev->dev, "Failed to set interrupt handler (%d)\n",
			err);
		goto err_destroy_modelist;
	}
#ifdef SUPPORT_ADF_PDP_FBDEV
	err = adf_fbdev_init(&pdp->adf_fbdev, &pdp->adf_interface,
		&pdp->adf_overlay, pdp_display_width,
		pdp_display_height, DRM_FORMAT_BGRA8888,
		&adf_pdp_fb_ops, "adf_pdp_fb");
	if (err) {
		dev_err(&pdev->dev, "Failed to init ADF fbdev (%d)\n", err);
		goto err_destroy_modelist;
	}
#endif

	init_waitqueue_head(&pdp->vsync_wait_queue);
	atomic_set(&pdp->requested_vsync_state, 0);
	atomic_set(&pdp->vsync_state, 0);

	return err;
err_destroy_modelist:
	kfree(pdp->supported_modes);
err_destroy_adf_overlay:
	adf_overlay_engine_destroy(&pdp->adf_overlay);
err_destroy_adf_interface:
	adf_interface_destroy(&pdp->adf_interface);
err_destroy_adf_device:
	adf_device_destroy(&pdp->adf_device);
err_unmap_pll_registers:
	iounmap(pdp->pll_regs);
err_release_pll_registers:
	release_pci_io_addr(pdp->pdata->pdev, DCPDP_REG_PCI_BASENUM,
		pdp->pll_regs_start, pdp->pll_regs_size);
err_unmap_registers:
	iounmap(pdp->regs);
err_release_registers:
	release_pci_io_addr(pdp->pdata->pdev, DCPDP_REG_PCI_BASENUM,
		pdp->regs_start, pdp->regs_size);
err_destroy_ion_client:
	ion_client_destroy(pdp->ion_client);
err_disable_pci:
	pci_disable_device(pdata->pdev);
err_out:
	dev_err(&pdev->dev, "Failed to initialise PDP device\n");
	return err;
}

static int adf_pdp_remove_device(struct platform_device *pdev)
{
	int err = 0;
	struct adf_pdp_device *pdp = devres_find(&pdev->dev, pdp_devres_release,
		NULL, NULL);

	pdp_disable_scanout(pdp);

	pdp_disable_vsync(pdp);
	apollo_set_interrupt_handler(&pdp->pdata->pdev->dev,
				     APOLLO_INTERRUPT_PDP,
				     NULL, NULL);
	/* Disable scanout */
	pdp_write_reg(pdp, TCF_RGBPDP_PVR_TCF_RGBPDP_STR1ADDRCTRL, 0);
	kfree(pdp->supported_modes);
#ifdef SUPPORT_ADF_PDP_FBDEV
	adf_fbdev_destroy(&pdp->adf_fbdev);
#endif
	adf_overlay_engine_destroy(&pdp->adf_overlay);
	adf_interface_destroy(&pdp->adf_interface);
	adf_device_destroy(&pdp->adf_device);
	iounmap(pdp->pll_regs);
	release_pci_io_addr(pdp->pdata->pdev, DCPDP_REG_PCI_BASENUM,
		pdp->pll_regs_start, pdp->pll_regs_size);
	iounmap(pdp->regs);
	release_pci_io_addr(pdp->pdata->pdev, DCPDP_REG_PCI_BASENUM,
		pdp->regs_start, pdp->regs_size);
	ion_client_destroy(pdp->ion_client);
	pci_disable_device(pdp->pdata->pdev);
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
