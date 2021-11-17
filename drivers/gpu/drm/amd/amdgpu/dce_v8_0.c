/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <drm/drm_fourcc.h>
#include <drm/drm_vblank.h>

#include "amdgpu.h"
#include "amdgpu_pm.h"
#include "amdgpu_i2c.h"
#include "cikd.h"
#include "atom.h"
#include "amdgpu_atombios.h"
#include "atombios_crtc.h"
#include "atombios_encoders.h"
#include "amdgpu_pll.h"
#include "amdgpu_connectors.h"
#include "amdgpu_display.h"
#include "dce_v8_0.h"

#include "dce/dce_8_0_d.h"
#include "dce/dce_8_0_sh_mask.h"

#include "gca/gfx_7_2_enum.h"

#include "gmc/gmc_7_1_d.h"
#include "gmc/gmc_7_1_sh_mask.h"

#include "oss/oss_2_0_d.h"
#include "oss/oss_2_0_sh_mask.h"

static void dce_v8_0_set_display_funcs(struct amdgpu_device *adev);
static void dce_v8_0_set_irq_funcs(struct amdgpu_device *adev);

static const u32 crtc_offsets[6] =
{
	CRTC0_REGISTER_OFFSET,
	CRTC1_REGISTER_OFFSET,
	CRTC2_REGISTER_OFFSET,
	CRTC3_REGISTER_OFFSET,
	CRTC4_REGISTER_OFFSET,
	CRTC5_REGISTER_OFFSET
};

static const u32 hpd_offsets[] =
{
	HPD0_REGISTER_OFFSET,
	HPD1_REGISTER_OFFSET,
	HPD2_REGISTER_OFFSET,
	HPD3_REGISTER_OFFSET,
	HPD4_REGISTER_OFFSET,
	HPD5_REGISTER_OFFSET
};

static const uint32_t dig_offsets[] = {
	CRTC0_REGISTER_OFFSET,
	CRTC1_REGISTER_OFFSET,
	CRTC2_REGISTER_OFFSET,
	CRTC3_REGISTER_OFFSET,
	CRTC4_REGISTER_OFFSET,
	CRTC5_REGISTER_OFFSET,
	(0x13830 - 0x7030) >> 2,
};

static const struct {
	uint32_t	reg;
	uint32_t	vblank;
	uint32_t	vline;
	uint32_t	hpd;

} interrupt_status_offsets[6] = { {
	.reg = mmDISP_INTERRUPT_STATUS,
	.vblank = DISP_INTERRUPT_STATUS__LB_D1_VBLANK_INTERRUPT_MASK,
	.vline = DISP_INTERRUPT_STATUS__LB_D1_VLINE_INTERRUPT_MASK,
	.hpd = DISP_INTERRUPT_STATUS__DC_HPD1_INTERRUPT_MASK
}, {
	.reg = mmDISP_INTERRUPT_STATUS_CONTINUE,
	.vblank = DISP_INTERRUPT_STATUS_CONTINUE__LB_D2_VBLANK_INTERRUPT_MASK,
	.vline = DISP_INTERRUPT_STATUS_CONTINUE__LB_D2_VLINE_INTERRUPT_MASK,
	.hpd = DISP_INTERRUPT_STATUS_CONTINUE__DC_HPD2_INTERRUPT_MASK
}, {
	.reg = mmDISP_INTERRUPT_STATUS_CONTINUE2,
	.vblank = DISP_INTERRUPT_STATUS_CONTINUE2__LB_D3_VBLANK_INTERRUPT_MASK,
	.vline = DISP_INTERRUPT_STATUS_CONTINUE2__LB_D3_VLINE_INTERRUPT_MASK,
	.hpd = DISP_INTERRUPT_STATUS_CONTINUE2__DC_HPD3_INTERRUPT_MASK
}, {
	.reg = mmDISP_INTERRUPT_STATUS_CONTINUE3,
	.vblank = DISP_INTERRUPT_STATUS_CONTINUE3__LB_D4_VBLANK_INTERRUPT_MASK,
	.vline = DISP_INTERRUPT_STATUS_CONTINUE3__LB_D4_VLINE_INTERRUPT_MASK,
	.hpd = DISP_INTERRUPT_STATUS_CONTINUE3__DC_HPD4_INTERRUPT_MASK
}, {
	.reg = mmDISP_INTERRUPT_STATUS_CONTINUE4,
	.vblank = DISP_INTERRUPT_STATUS_CONTINUE4__LB_D5_VBLANK_INTERRUPT_MASK,
	.vline = DISP_INTERRUPT_STATUS_CONTINUE4__LB_D5_VLINE_INTERRUPT_MASK,
	.hpd = DISP_INTERRUPT_STATUS_CONTINUE4__DC_HPD5_INTERRUPT_MASK
}, {
	.reg = mmDISP_INTERRUPT_STATUS_CONTINUE5,
	.vblank = DISP_INTERRUPT_STATUS_CONTINUE5__LB_D6_VBLANK_INTERRUPT_MASK,
	.vline = DISP_INTERRUPT_STATUS_CONTINUE5__LB_D6_VLINE_INTERRUPT_MASK,
	.hpd = DISP_INTERRUPT_STATUS_CONTINUE5__DC_HPD6_INTERRUPT_MASK
} };

static u32 dce_v8_0_audio_endpt_rreg(struct amdgpu_device *adev,
				     u32 block_offset, u32 reg)
{
	unsigned long flags;
	u32 r;

	spin_lock_irqsave(&adev->audio_endpt_idx_lock, flags);
	WREG32(mmAZALIA_F0_CODEC_ENDPOINT_INDEX + block_offset, reg);
	r = RREG32(mmAZALIA_F0_CODEC_ENDPOINT_DATA + block_offset);
	spin_unlock_irqrestore(&adev->audio_endpt_idx_lock, flags);

	return r;
}

static void dce_v8_0_audio_endpt_wreg(struct amdgpu_device *adev,
				      u32 block_offset, u32 reg, u32 v)
{
	unsigned long flags;

	spin_lock_irqsave(&adev->audio_endpt_idx_lock, flags);
	WREG32(mmAZALIA_F0_CODEC_ENDPOINT_INDEX + block_offset, reg);
	WREG32(mmAZALIA_F0_CODEC_ENDPOINT_DATA + block_offset, v);
	spin_unlock_irqrestore(&adev->audio_endpt_idx_lock, flags);
}

static u32 dce_v8_0_vblank_get_counter(struct amdgpu_device *adev, int crtc)
{
	if (crtc >= adev->mode_info.num_crtc)
		return 0;
	else
		return RREG32(mmCRTC_STATUS_FRAME_COUNT + crtc_offsets[crtc]);
}

static void dce_v8_0_pageflip_interrupt_init(struct amdgpu_device *adev)
{
	unsigned i;

	/* Enable pflip interrupts */
	for (i = 0; i < adev->mode_info.num_crtc; i++)
		amdgpu_irq_get(adev, &adev->pageflip_irq, i);
}

static void dce_v8_0_pageflip_interrupt_fini(struct amdgpu_device *adev)
{
	unsigned i;

	/* Disable pflip interrupts */
	for (i = 0; i < adev->mode_info.num_crtc; i++)
		amdgpu_irq_put(adev, &adev->pageflip_irq, i);
}

/**
 * dce_v8_0_page_flip - pageflip callback.
 *
 * @adev: amdgpu_device pointer
 * @crtc_id: crtc to cleanup pageflip on
 * @crtc_base: new address of the crtc (GPU MC address)
 * @async: asynchronous flip
 *
 * Triggers the actual pageflip by updating the primary
 * surface base address.
 */
static void dce_v8_0_page_flip(struct amdgpu_device *adev,
			       int crtc_id, u64 crtc_base, bool async)
{
	struct amdgpu_crtc *amdgpu_crtc = adev->mode_info.crtcs[crtc_id];
	struct drm_framebuffer *fb = amdgpu_crtc->base.primary->fb;

	/* flip at hsync for async, default is vsync */
	WREG32(mmGRPH_FLIP_CONTROL + amdgpu_crtc->crtc_offset, async ?
	       GRPH_FLIP_CONTROL__GRPH_SURFACE_UPDATE_H_RETRACE_EN_MASK : 0);
	/* update pitch */
	WREG32(mmGRPH_PITCH + amdgpu_crtc->crtc_offset,
	       fb->pitches[0] / fb->format->cpp[0]);
	/* update the primary scanout addresses */
	WREG32(mmGRPH_PRIMARY_SURFACE_ADDRESS_HIGH + amdgpu_crtc->crtc_offset,
	       upper_32_bits(crtc_base));
	/* writing to the low address triggers the update */
	WREG32(mmGRPH_PRIMARY_SURFACE_ADDRESS + amdgpu_crtc->crtc_offset,
	       lower_32_bits(crtc_base));
	/* post the write */
	RREG32(mmGRPH_PRIMARY_SURFACE_ADDRESS + amdgpu_crtc->crtc_offset);
}

static int dce_v8_0_crtc_get_scanoutpos(struct amdgpu_device *adev, int crtc,
					u32 *vbl, u32 *position)
{
	if ((crtc < 0) || (crtc >= adev->mode_info.num_crtc))
		return -EINVAL;

	*vbl = RREG32(mmCRTC_V_BLANK_START_END + crtc_offsets[crtc]);
	*position = RREG32(mmCRTC_STATUS_POSITION + crtc_offsets[crtc]);

	return 0;
}

/**
 * dce_v8_0_hpd_sense - hpd sense callback.
 *
 * @adev: amdgpu_device pointer
 * @hpd: hpd (hotplug detect) pin
 *
 * Checks if a digital monitor is connected (evergreen+).
 * Returns true if connected, false if not connected.
 */
static bool dce_v8_0_hpd_sense(struct amdgpu_device *adev,
			       enum amdgpu_hpd_id hpd)
{
	bool connected = false;

	if (hpd >= adev->mode_info.num_hpd)
		return connected;

	if (RREG32(mmDC_HPD1_INT_STATUS + hpd_offsets[hpd]) &
	    DC_HPD1_INT_STATUS__DC_HPD1_SENSE_MASK)
		connected = true;

	return connected;
}

/**
 * dce_v8_0_hpd_set_polarity - hpd set polarity callback.
 *
 * @adev: amdgpu_device pointer
 * @hpd: hpd (hotplug detect) pin
 *
 * Set the polarity of the hpd pin (evergreen+).
 */
static void dce_v8_0_hpd_set_polarity(struct amdgpu_device *adev,
				      enum amdgpu_hpd_id hpd)
{
	u32 tmp;
	bool connected = dce_v8_0_hpd_sense(adev, hpd);

	if (hpd >= adev->mode_info.num_hpd)
		return;

	tmp = RREG32(mmDC_HPD1_INT_CONTROL + hpd_offsets[hpd]);
	if (connected)
		tmp &= ~DC_HPD1_INT_CONTROL__DC_HPD1_INT_POLARITY_MASK;
	else
		tmp |= DC_HPD1_INT_CONTROL__DC_HPD1_INT_POLARITY_MASK;
	WREG32(mmDC_HPD1_INT_CONTROL + hpd_offsets[hpd], tmp);
}

/**
 * dce_v8_0_hpd_init - hpd setup callback.
 *
 * @adev: amdgpu_device pointer
 *
 * Setup the hpd pins used by the card (evergreen+).
 * Enable the pin, set the polarity, and enable the hpd interrupts.
 */
static void dce_v8_0_hpd_init(struct amdgpu_device *adev)
{
	struct drm_device *dev = adev_to_drm(adev);
	struct drm_connector *connector;
	struct drm_connector_list_iter iter;
	u32 tmp;

	drm_connector_list_iter_begin(dev, &iter);
	drm_for_each_connector_iter(connector, &iter) {
		struct amdgpu_connector *amdgpu_connector = to_amdgpu_connector(connector);

		if (amdgpu_connector->hpd.hpd >= adev->mode_info.num_hpd)
			continue;

		tmp = RREG32(mmDC_HPD1_CONTROL + hpd_offsets[amdgpu_connector->hpd.hpd]);
		tmp |= DC_HPD1_CONTROL__DC_HPD1_EN_MASK;
		WREG32(mmDC_HPD1_CONTROL + hpd_offsets[amdgpu_connector->hpd.hpd], tmp);

		if (connector->connector_type == DRM_MODE_CONNECTOR_eDP ||
		    connector->connector_type == DRM_MODE_CONNECTOR_LVDS) {
			/* don't try to enable hpd on eDP or LVDS avoid breaking the
			 * aux dp channel on imac and help (but not completely fix)
			 * https://bugzilla.redhat.com/show_bug.cgi?id=726143
			 * also avoid interrupt storms during dpms.
			 */
			tmp = RREG32(mmDC_HPD1_INT_CONTROL + hpd_offsets[amdgpu_connector->hpd.hpd]);
			tmp &= ~DC_HPD1_INT_CONTROL__DC_HPD1_INT_EN_MASK;
			WREG32(mmDC_HPD1_INT_CONTROL + hpd_offsets[amdgpu_connector->hpd.hpd], tmp);
			continue;
		}

		dce_v8_0_hpd_set_polarity(adev, amdgpu_connector->hpd.hpd);
		amdgpu_irq_get(adev, &adev->hpd_irq, amdgpu_connector->hpd.hpd);
	}
	drm_connector_list_iter_end(&iter);
}

/**
 * dce_v8_0_hpd_fini - hpd tear down callback.
 *
 * @adev: amdgpu_device pointer
 *
 * Tear down the hpd pins used by the card (evergreen+).
 * Disable the hpd interrupts.
 */
static void dce_v8_0_hpd_fini(struct amdgpu_device *adev)
{
	struct drm_device *dev = adev_to_drm(adev);
	struct drm_connector *connector;
	struct drm_connector_list_iter iter;
	u32 tmp;

	drm_connector_list_iter_begin(dev, &iter);
	drm_for_each_connector_iter(connector, &iter) {
		struct amdgpu_connector *amdgpu_connector = to_amdgpu_connector(connector);

		if (amdgpu_connector->hpd.hpd >= adev->mode_info.num_hpd)
			continue;

		tmp = RREG32(mmDC_HPD1_CONTROL + hpd_offsets[amdgpu_connector->hpd.hpd]);
		tmp &= ~DC_HPD1_CONTROL__DC_HPD1_EN_MASK;
		WREG32(mmDC_HPD1_CONTROL + hpd_offsets[amdgpu_connector->hpd.hpd], 0);

		amdgpu_irq_put(adev, &adev->hpd_irq, amdgpu_connector->hpd.hpd);
	}
	drm_connector_list_iter_end(&iter);
}

static u32 dce_v8_0_hpd_get_gpio_reg(struct amdgpu_device *adev)
{
	return mmDC_GPIO_HPD_A;
}

static bool dce_v8_0_is_display_hung(struct amdgpu_device *adev)
{
	u32 crtc_hung = 0;
	u32 crtc_status[6];
	u32 i, j, tmp;

	for (i = 0; i < adev->mode_info.num_crtc; i++) {
		if (RREG32(mmCRTC_CONTROL + crtc_offsets[i]) & CRTC_CONTROL__CRTC_MASTER_EN_MASK) {
			crtc_status[i] = RREG32(mmCRTC_STATUS_HV_COUNT + crtc_offsets[i]);
			crtc_hung |= (1 << i);
		}
	}

	for (j = 0; j < 10; j++) {
		for (i = 0; i < adev->mode_info.num_crtc; i++) {
			if (crtc_hung & (1 << i)) {
				tmp = RREG32(mmCRTC_STATUS_HV_COUNT + crtc_offsets[i]);
				if (tmp != crtc_status[i])
					crtc_hung &= ~(1 << i);
			}
		}
		if (crtc_hung == 0)
			return false;
		udelay(100);
	}

	return true;
}

static void dce_v8_0_set_vga_render_state(struct amdgpu_device *adev,
					  bool render)
{
	u32 tmp;

	/* Lockout access through VGA aperture*/
	tmp = RREG32(mmVGA_HDP_CONTROL);
	if (render)
		tmp = REG_SET_FIELD(tmp, VGA_HDP_CONTROL, VGA_MEMORY_DISABLE, 0);
	else
		tmp = REG_SET_FIELD(tmp, VGA_HDP_CONTROL, VGA_MEMORY_DISABLE, 1);
	WREG32(mmVGA_HDP_CONTROL, tmp);

	/* disable VGA render */
	tmp = RREG32(mmVGA_RENDER_CONTROL);
	if (render)
		tmp = REG_SET_FIELD(tmp, VGA_RENDER_CONTROL, VGA_VSTATUS_CNTL, 1);
	else
		tmp = REG_SET_FIELD(tmp, VGA_RENDER_CONTROL, VGA_VSTATUS_CNTL, 0);
	WREG32(mmVGA_RENDER_CONTROL, tmp);
}

static int dce_v8_0_get_num_crtc(struct amdgpu_device *adev)
{
	int num_crtc = 0;

	switch (adev->asic_type) {
	case CHIP_BONAIRE:
	case CHIP_HAWAII:
		num_crtc = 6;
		break;
	case CHIP_KAVERI:
		num_crtc = 4;
		break;
	case CHIP_KABINI:
	case CHIP_MULLINS:
		num_crtc = 2;
		break;
	default:
		num_crtc = 0;
	}
	return num_crtc;
}

void dce_v8_0_disable_dce(struct amdgpu_device *adev)
{
	/*Disable VGA render and enabled crtc, if has DCE engine*/
	if (amdgpu_atombios_has_dce_engine_info(adev)) {
		u32 tmp;
		int crtc_enabled, i;

		dce_v8_0_set_vga_render_state(adev, false);

		/*Disable crtc*/
		for (i = 0; i < dce_v8_0_get_num_crtc(adev); i++) {
			crtc_enabled = REG_GET_FIELD(RREG32(mmCRTC_CONTROL + crtc_offsets[i]),
									 CRTC_CONTROL, CRTC_MASTER_EN);
			if (crtc_enabled) {
				WREG32(mmCRTC_UPDATE_LOCK + crtc_offsets[i], 1);
				tmp = RREG32(mmCRTC_CONTROL + crtc_offsets[i]);
				tmp = REG_SET_FIELD(tmp, CRTC_CONTROL, CRTC_MASTER_EN, 0);
				WREG32(mmCRTC_CONTROL + crtc_offsets[i], tmp);
				WREG32(mmCRTC_UPDATE_LOCK + crtc_offsets[i], 0);
			}
		}
	}
}

static void dce_v8_0_program_fmt(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(encoder->crtc);
	struct drm_connector *connector = amdgpu_get_connector_for_encoder(encoder);
	int bpc = 0;
	u32 tmp = 0;
	enum amdgpu_connector_dither dither = AMDGPU_FMT_DITHER_DISABLE;

	if (connector) {
		struct amdgpu_connector *amdgpu_connector = to_amdgpu_connector(connector);
		bpc = amdgpu_connector_get_monitor_bpc(connector);
		dither = amdgpu_connector->dither;
	}

	/* LVDS/eDP FMT is set up by atom */
	if (amdgpu_encoder->devices & ATOM_DEVICE_LCD_SUPPORT)
		return;

	/* not needed for analog */
	if ((amdgpu_encoder->encoder_id == ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC1) ||
	    (amdgpu_encoder->encoder_id == ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC2))
		return;

	if (bpc == 0)
		return;

	switch (bpc) {
	case 6:
		if (dither == AMDGPU_FMT_DITHER_ENABLE)
			/* XXX sort out optimal dither settings */
			tmp |= (FMT_BIT_DEPTH_CONTROL__FMT_FRAME_RANDOM_ENABLE_MASK |
				FMT_BIT_DEPTH_CONTROL__FMT_HIGHPASS_RANDOM_ENABLE_MASK |
				FMT_BIT_DEPTH_CONTROL__FMT_SPATIAL_DITHER_EN_MASK |
				(0 << FMT_BIT_DEPTH_CONTROL__FMT_SPATIAL_DITHER_DEPTH__SHIFT));
		else
			tmp |= (FMT_BIT_DEPTH_CONTROL__FMT_TRUNCATE_EN_MASK |
			(0 << FMT_BIT_DEPTH_CONTROL__FMT_TRUNCATE_DEPTH__SHIFT));
		break;
	case 8:
		if (dither == AMDGPU_FMT_DITHER_ENABLE)
			/* XXX sort out optimal dither settings */
			tmp |= (FMT_BIT_DEPTH_CONTROL__FMT_FRAME_RANDOM_ENABLE_MASK |
				FMT_BIT_DEPTH_CONTROL__FMT_HIGHPASS_RANDOM_ENABLE_MASK |
				FMT_BIT_DEPTH_CONTROL__FMT_RGB_RANDOM_ENABLE_MASK |
				FMT_BIT_DEPTH_CONTROL__FMT_SPATIAL_DITHER_EN_MASK |
				(1 << FMT_BIT_DEPTH_CONTROL__FMT_SPATIAL_DITHER_DEPTH__SHIFT));
		else
			tmp |= (FMT_BIT_DEPTH_CONTROL__FMT_TRUNCATE_EN_MASK |
			(1 << FMT_BIT_DEPTH_CONTROL__FMT_TRUNCATE_DEPTH__SHIFT));
		break;
	case 10:
		if (dither == AMDGPU_FMT_DITHER_ENABLE)
			/* XXX sort out optimal dither settings */
			tmp |= (FMT_BIT_DEPTH_CONTROL__FMT_FRAME_RANDOM_ENABLE_MASK |
				FMT_BIT_DEPTH_CONTROL__FMT_HIGHPASS_RANDOM_ENABLE_MASK |
				FMT_BIT_DEPTH_CONTROL__FMT_RGB_RANDOM_ENABLE_MASK |
				FMT_BIT_DEPTH_CONTROL__FMT_SPATIAL_DITHER_EN_MASK |
				(2 << FMT_BIT_DEPTH_CONTROL__FMT_SPATIAL_DITHER_DEPTH__SHIFT));
		else
			tmp |= (FMT_BIT_DEPTH_CONTROL__FMT_TRUNCATE_EN_MASK |
			(2 << FMT_BIT_DEPTH_CONTROL__FMT_TRUNCATE_DEPTH__SHIFT));
		break;
	default:
		/* not needed */
		break;
	}

	WREG32(mmFMT_BIT_DEPTH_CONTROL + amdgpu_crtc->crtc_offset, tmp);
}


/* display watermark setup */
/**
 * dce_v8_0_line_buffer_adjust - Set up the line buffer
 *
 * @adev: amdgpu_device pointer
 * @amdgpu_crtc: the selected display controller
 * @mode: the current display mode on the selected display
 * controller
 *
 * Setup up the line buffer allocation for
 * the selected display controller (CIK).
 * Returns the line buffer size in pixels.
 */
static u32 dce_v8_0_line_buffer_adjust(struct amdgpu_device *adev,
				       struct amdgpu_crtc *amdgpu_crtc,
				       struct drm_display_mode *mode)
{
	u32 tmp, buffer_alloc, i;
	u32 pipe_offset = amdgpu_crtc->crtc_id * 0x8;
	/*
	 * Line Buffer Setup
	 * There are 6 line buffers, one for each display controllers.
	 * There are 3 partitions per LB. Select the number of partitions
	 * to enable based on the display width.  For display widths larger
	 * than 4096, you need use to use 2 display controllers and combine
	 * them using the stereo blender.
	 */
	if (amdgpu_crtc->base.enabled && mode) {
		if (mode->crtc_hdisplay < 1920) {
			tmp = 1;
			buffer_alloc = 2;
		} else if (mode->crtc_hdisplay < 2560) {
			tmp = 2;
			buffer_alloc = 2;
		} else if (mode->crtc_hdisplay < 4096) {
			tmp = 0;
			buffer_alloc = (adev->flags & AMD_IS_APU) ? 2 : 4;
		} else {
			DRM_DEBUG_KMS("Mode too big for LB!\n");
			tmp = 0;
			buffer_alloc = (adev->flags & AMD_IS_APU) ? 2 : 4;
		}
	} else {
		tmp = 1;
		buffer_alloc = 0;
	}

	WREG32(mmLB_MEMORY_CTRL + amdgpu_crtc->crtc_offset,
	      (tmp << LB_MEMORY_CTRL__LB_MEMORY_CONFIG__SHIFT) |
	      (0x6B0 << LB_MEMORY_CTRL__LB_MEMORY_SIZE__SHIFT));

	WREG32(mmPIPE0_DMIF_BUFFER_CONTROL + pipe_offset,
	       (buffer_alloc << PIPE0_DMIF_BUFFER_CONTROL__DMIF_BUFFERS_ALLOCATED__SHIFT));
	for (i = 0; i < adev->usec_timeout; i++) {
		if (RREG32(mmPIPE0_DMIF_BUFFER_CONTROL + pipe_offset) &
		    PIPE0_DMIF_BUFFER_CONTROL__DMIF_BUFFERS_ALLOCATION_COMPLETED_MASK)
			break;
		udelay(1);
	}

	if (amdgpu_crtc->base.enabled && mode) {
		switch (tmp) {
		case 0:
		default:
			return 4096 * 2;
		case 1:
			return 1920 * 2;
		case 2:
			return 2560 * 2;
		}
	}

	/* controller not enabled, so no lb used */
	return 0;
}

/**
 * cik_get_number_of_dram_channels - get the number of dram channels
 *
 * @adev: amdgpu_device pointer
 *
 * Look up the number of video ram channels (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the number of dram channels
 */
static u32 cik_get_number_of_dram_channels(struct amdgpu_device *adev)
{
	u32 tmp = RREG32(mmMC_SHARED_CHMAP);

	switch ((tmp & MC_SHARED_CHMAP__NOOFCHAN_MASK) >> MC_SHARED_CHMAP__NOOFCHAN__SHIFT) {
	case 0:
	default:
		return 1;
	case 1:
		return 2;
	case 2:
		return 4;
	case 3:
		return 8;
	case 4:
		return 3;
	case 5:
		return 6;
	case 6:
		return 10;
	case 7:
		return 12;
	case 8:
		return 16;
	}
}

struct dce8_wm_params {
	u32 dram_channels; /* number of dram channels */
	u32 yclk;          /* bandwidth per dram data pin in kHz */
	u32 sclk;          /* engine clock in kHz */
	u32 disp_clk;      /* display clock in kHz */
	u32 src_width;     /* viewport width */
	u32 active_time;   /* active display time in ns */
	u32 blank_time;    /* blank time in ns */
	bool interlaced;    /* mode is interlaced */
	fixed20_12 vsc;    /* vertical scale ratio */
	u32 num_heads;     /* number of active crtcs */
	u32 bytes_per_pixel; /* bytes per pixel display + overlay */
	u32 lb_size;       /* line buffer allocated to pipe */
	u32 vtaps;         /* vertical scaler taps */
};

/**
 * dce_v8_0_dram_bandwidth - get the dram bandwidth
 *
 * @wm: watermark calculation data
 *
 * Calculate the raw dram bandwidth (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the dram bandwidth in MBytes/s
 */
static u32 dce_v8_0_dram_bandwidth(struct dce8_wm_params *wm)
{
	/* Calculate raw DRAM Bandwidth */
	fixed20_12 dram_efficiency; /* 0.7 */
	fixed20_12 yclk, dram_channels, bandwidth;
	fixed20_12 a;

	a.full = dfixed_const(1000);
	yclk.full = dfixed_const(wm->yclk);
	yclk.full = dfixed_div(yclk, a);
	dram_channels.full = dfixed_const(wm->dram_channels * 4);
	a.full = dfixed_const(10);
	dram_efficiency.full = dfixed_const(7);
	dram_efficiency.full = dfixed_div(dram_efficiency, a);
	bandwidth.full = dfixed_mul(dram_channels, yclk);
	bandwidth.full = dfixed_mul(bandwidth, dram_efficiency);

	return dfixed_trunc(bandwidth);
}

/**
 * dce_v8_0_dram_bandwidth_for_display - get the dram bandwidth for display
 *
 * @wm: watermark calculation data
 *
 * Calculate the dram bandwidth used for display (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the dram bandwidth for display in MBytes/s
 */
static u32 dce_v8_0_dram_bandwidth_for_display(struct dce8_wm_params *wm)
{
	/* Calculate DRAM Bandwidth and the part allocated to display. */
	fixed20_12 disp_dram_allocation; /* 0.3 to 0.7 */
	fixed20_12 yclk, dram_channels, bandwidth;
	fixed20_12 a;

	a.full = dfixed_const(1000);
	yclk.full = dfixed_const(wm->yclk);
	yclk.full = dfixed_div(yclk, a);
	dram_channels.full = dfixed_const(wm->dram_channels * 4);
	a.full = dfixed_const(10);
	disp_dram_allocation.full = dfixed_const(3); /* XXX worse case value 0.3 */
	disp_dram_allocation.full = dfixed_div(disp_dram_allocation, a);
	bandwidth.full = dfixed_mul(dram_channels, yclk);
	bandwidth.full = dfixed_mul(bandwidth, disp_dram_allocation);

	return dfixed_trunc(bandwidth);
}

/**
 * dce_v8_0_data_return_bandwidth - get the data return bandwidth
 *
 * @wm: watermark calculation data
 *
 * Calculate the data return bandwidth used for display (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the data return bandwidth in MBytes/s
 */
static u32 dce_v8_0_data_return_bandwidth(struct dce8_wm_params *wm)
{
	/* Calculate the display Data return Bandwidth */
	fixed20_12 return_efficiency; /* 0.8 */
	fixed20_12 sclk, bandwidth;
	fixed20_12 a;

	a.full = dfixed_const(1000);
	sclk.full = dfixed_const(wm->sclk);
	sclk.full = dfixed_div(sclk, a);
	a.full = dfixed_const(10);
	return_efficiency.full = dfixed_const(8);
	return_efficiency.full = dfixed_div(return_efficiency, a);
	a.full = dfixed_const(32);
	bandwidth.full = dfixed_mul(a, sclk);
	bandwidth.full = dfixed_mul(bandwidth, return_efficiency);

	return dfixed_trunc(bandwidth);
}

/**
 * dce_v8_0_dmif_request_bandwidth - get the dmif bandwidth
 *
 * @wm: watermark calculation data
 *
 * Calculate the dmif bandwidth used for display (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the dmif bandwidth in MBytes/s
 */
static u32 dce_v8_0_dmif_request_bandwidth(struct dce8_wm_params *wm)
{
	/* Calculate the DMIF Request Bandwidth */
	fixed20_12 disp_clk_request_efficiency; /* 0.8 */
	fixed20_12 disp_clk, bandwidth;
	fixed20_12 a, b;

	a.full = dfixed_const(1000);
	disp_clk.full = dfixed_const(wm->disp_clk);
	disp_clk.full = dfixed_div(disp_clk, a);
	a.full = dfixed_const(32);
	b.full = dfixed_mul(a, disp_clk);

	a.full = dfixed_const(10);
	disp_clk_request_efficiency.full = dfixed_const(8);
	disp_clk_request_efficiency.full = dfixed_div(disp_clk_request_efficiency, a);

	bandwidth.full = dfixed_mul(b, disp_clk_request_efficiency);

	return dfixed_trunc(bandwidth);
}

/**
 * dce_v8_0_available_bandwidth - get the min available bandwidth
 *
 * @wm: watermark calculation data
 *
 * Calculate the min available bandwidth used for display (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the min available bandwidth in MBytes/s
 */
static u32 dce_v8_0_available_bandwidth(struct dce8_wm_params *wm)
{
	/* Calculate the Available bandwidth. Display can use this temporarily but not in average. */
	u32 dram_bandwidth = dce_v8_0_dram_bandwidth(wm);
	u32 data_return_bandwidth = dce_v8_0_data_return_bandwidth(wm);
	u32 dmif_req_bandwidth = dce_v8_0_dmif_request_bandwidth(wm);

	return min(dram_bandwidth, min(data_return_bandwidth, dmif_req_bandwidth));
}

/**
 * dce_v8_0_average_bandwidth - get the average available bandwidth
 *
 * @wm: watermark calculation data
 *
 * Calculate the average available bandwidth used for display (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the average available bandwidth in MBytes/s
 */
static u32 dce_v8_0_average_bandwidth(struct dce8_wm_params *wm)
{
	/* Calculate the display mode Average Bandwidth
	 * DisplayMode should contain the source and destination dimensions,
	 * timing, etc.
	 */
	fixed20_12 bpp;
	fixed20_12 line_time;
	fixed20_12 src_width;
	fixed20_12 bandwidth;
	fixed20_12 a;

	a.full = dfixed_const(1000);
	line_time.full = dfixed_const(wm->active_time + wm->blank_time);
	line_time.full = dfixed_div(line_time, a);
	bpp.full = dfixed_const(wm->bytes_per_pixel);
	src_width.full = dfixed_const(wm->src_width);
	bandwidth.full = dfixed_mul(src_width, bpp);
	bandwidth.full = dfixed_mul(bandwidth, wm->vsc);
	bandwidth.full = dfixed_div(bandwidth, line_time);

	return dfixed_trunc(bandwidth);
}

/**
 * dce_v8_0_latency_watermark - get the latency watermark
 *
 * @wm: watermark calculation data
 *
 * Calculate the latency watermark (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the latency watermark in ns
 */
static u32 dce_v8_0_latency_watermark(struct dce8_wm_params *wm)
{
	/* First calculate the latency in ns */
	u32 mc_latency = 2000; /* 2000 ns. */
	u32 available_bandwidth = dce_v8_0_available_bandwidth(wm);
	u32 worst_chunk_return_time = (512 * 8 * 1000) / available_bandwidth;
	u32 cursor_line_pair_return_time = (128 * 4 * 1000) / available_bandwidth;
	u32 dc_latency = 40000000 / wm->disp_clk; /* dc pipe latency */
	u32 other_heads_data_return_time = ((wm->num_heads + 1) * worst_chunk_return_time) +
		(wm->num_heads * cursor_line_pair_return_time);
	u32 latency = mc_latency + other_heads_data_return_time + dc_latency;
	u32 max_src_lines_per_dst_line, lb_fill_bw, line_fill_time;
	u32 tmp, dmif_size = 12288;
	fixed20_12 a, b, c;

	if (wm->num_heads == 0)
		return 0;

	a.full = dfixed_const(2);
	b.full = dfixed_const(1);
	if ((wm->vsc.full > a.full) ||
	    ((wm->vsc.full > b.full) && (wm->vtaps >= 3)) ||
	    (wm->vtaps >= 5) ||
	    ((wm->vsc.full >= a.full) && wm->interlaced))
		max_src_lines_per_dst_line = 4;
	else
		max_src_lines_per_dst_line = 2;

	a.full = dfixed_const(available_bandwidth);
	b.full = dfixed_const(wm->num_heads);
	a.full = dfixed_div(a, b);
	tmp = div_u64((u64) dmif_size * (u64) wm->disp_clk, mc_latency + 512);
	tmp = min(dfixed_trunc(a), tmp);

	lb_fill_bw = min(tmp, wm->disp_clk * wm->bytes_per_pixel / 1000);

	a.full = dfixed_const(max_src_lines_per_dst_line * wm->src_width * wm->bytes_per_pixel);
	b.full = dfixed_const(1000);
	c.full = dfixed_const(lb_fill_bw);
	b.full = dfixed_div(c, b);
	a.full = dfixed_div(a, b);
	line_fill_time = dfixed_trunc(a);

	if (line_fill_time < wm->active_time)
		return latency;
	else
		return latency + (line_fill_time - wm->active_time);

}

/**
 * dce_v8_0_average_bandwidth_vs_dram_bandwidth_for_display - check
 * average and available dram bandwidth
 *
 * @wm: watermark calculation data
 *
 * Check if the display average bandwidth fits in the display
 * dram bandwidth (CIK).
 * Used for display watermark bandwidth calculations
 * Returns true if the display fits, false if not.
 */
static bool dce_v8_0_average_bandwidth_vs_dram_bandwidth_for_display(struct dce8_wm_params *wm)
{
	if (dce_v8_0_average_bandwidth(wm) <=
	    (dce_v8_0_dram_bandwidth_for_display(wm) / wm->num_heads))
		return true;
	else
		return false;
}

/**
 * dce_v8_0_average_bandwidth_vs_available_bandwidth - check
 * average and available bandwidth
 *
 * @wm: watermark calculation data
 *
 * Check if the display average bandwidth fits in the display
 * available bandwidth (CIK).
 * Used for display watermark bandwidth calculations
 * Returns true if the display fits, false if not.
 */
static bool dce_v8_0_average_bandwidth_vs_available_bandwidth(struct dce8_wm_params *wm)
{
	if (dce_v8_0_average_bandwidth(wm) <=
	    (dce_v8_0_available_bandwidth(wm) / wm->num_heads))
		return true;
	else
		return false;
}

/**
 * dce_v8_0_check_latency_hiding - check latency hiding
 *
 * @wm: watermark calculation data
 *
 * Check latency hiding (CIK).
 * Used for display watermark bandwidth calculations
 * Returns true if the display fits, false if not.
 */
static bool dce_v8_0_check_latency_hiding(struct dce8_wm_params *wm)
{
	u32 lb_partitions = wm->lb_size / wm->src_width;
	u32 line_time = wm->active_time + wm->blank_time;
	u32 latency_tolerant_lines;
	u32 latency_hiding;
	fixed20_12 a;

	a.full = dfixed_const(1);
	if (wm->vsc.full > a.full)
		latency_tolerant_lines = 1;
	else {
		if (lb_partitions <= (wm->vtaps + 1))
			latency_tolerant_lines = 1;
		else
			latency_tolerant_lines = 2;
	}

	latency_hiding = (latency_tolerant_lines * line_time + wm->blank_time);

	if (dce_v8_0_latency_watermark(wm) <= latency_hiding)
		return true;
	else
		return false;
}

/**
 * dce_v8_0_program_watermarks - program display watermarks
 *
 * @adev: amdgpu_device pointer
 * @amdgpu_crtc: the selected display controller
 * @lb_size: line buffer size
 * @num_heads: number of display controllers in use
 *
 * Calculate and program the display watermarks for the
 * selected display controller (CIK).
 */
static void dce_v8_0_program_watermarks(struct amdgpu_device *adev,
					struct amdgpu_crtc *amdgpu_crtc,
					u32 lb_size, u32 num_heads)
{
	struct drm_display_mode *mode = &amdgpu_crtc->base.mode;
	struct dce8_wm_params wm_low, wm_high;
	u32 active_time;
	u32 line_time = 0;
	u32 latency_watermark_a = 0, latency_watermark_b = 0;
	u32 tmp, wm_mask, lb_vblank_lead_lines = 0;

	if (amdgpu_crtc->base.enabled && num_heads && mode) {
		active_time = (u32) div_u64((u64)mode->crtc_hdisplay * 1000000,
					    (u32)mode->clock);
		line_time = (u32) div_u64((u64)mode->crtc_htotal * 1000000,
					  (u32)mode->clock);
		line_time = min(line_time, (u32)65535);

		/* watermark for high clocks */
		if (adev->pm.dpm_enabled) {
			wm_high.yclk =
				amdgpu_dpm_get_mclk(adev, false) * 10;
			wm_high.sclk =
				amdgpu_dpm_get_sclk(adev, false) * 10;
		} else {
			wm_high.yclk = adev->pm.current_mclk * 10;
			wm_high.sclk = adev->pm.current_sclk * 10;
		}

		wm_high.disp_clk = mode->clock;
		wm_high.src_width = mode->crtc_hdisplay;
		wm_high.active_time = active_time;
		wm_high.blank_time = line_time - wm_high.active_time;
		wm_high.interlaced = false;
		if (mode->flags & DRM_MODE_FLAG_INTERLACE)
			wm_high.interlaced = true;
		wm_high.vsc = amdgpu_crtc->vsc;
		wm_high.vtaps = 1;
		if (amdgpu_crtc->rmx_type != RMX_OFF)
			wm_high.vtaps = 2;
		wm_high.bytes_per_pixel = 4; /* XXX: get this from fb config */
		wm_high.lb_size = lb_size;
		wm_high.dram_channels = cik_get_number_of_dram_channels(adev);
		wm_high.num_heads = num_heads;

		/* set for high clocks */
		latency_watermark_a = min(dce_v8_0_latency_watermark(&wm_high), (u32)65535);

		/* possibly force display priority to high */
		/* should really do this at mode validation time... */
		if (!dce_v8_0_average_bandwidth_vs_dram_bandwidth_for_display(&wm_high) ||
		    !dce_v8_0_average_bandwidth_vs_available_bandwidth(&wm_high) ||
		    !dce_v8_0_check_latency_hiding(&wm_high) ||
		    (adev->mode_info.disp_priority == 2)) {
			DRM_DEBUG_KMS("force priority to high\n");
		}

		/* watermark for low clocks */
		if (adev->pm.dpm_enabled) {
			wm_low.yclk =
				amdgpu_dpm_get_mclk(adev, true) * 10;
			wm_low.sclk =
				amdgpu_dpm_get_sclk(adev, true) * 10;
		} else {
			wm_low.yclk = adev->pm.current_mclk * 10;
			wm_low.sclk = adev->pm.current_sclk * 10;
		}

		wm_low.disp_clk = mode->clock;
		wm_low.src_width = mode->crtc_hdisplay;
		wm_low.active_time = active_time;
		wm_low.blank_time = line_time - wm_low.active_time;
		wm_low.interlaced = false;
		if (mode->flags & DRM_MODE_FLAG_INTERLACE)
			wm_low.interlaced = true;
		wm_low.vsc = amdgpu_crtc->vsc;
		wm_low.vtaps = 1;
		if (amdgpu_crtc->rmx_type != RMX_OFF)
			wm_low.vtaps = 2;
		wm_low.bytes_per_pixel = 4; /* XXX: get this from fb config */
		wm_low.lb_size = lb_size;
		wm_low.dram_channels = cik_get_number_of_dram_channels(adev);
		wm_low.num_heads = num_heads;

		/* set for low clocks */
		latency_watermark_b = min(dce_v8_0_latency_watermark(&wm_low), (u32)65535);

		/* possibly force display priority to high */
		/* should really do this at mode validation time... */
		if (!dce_v8_0_average_bandwidth_vs_dram_bandwidth_for_display(&wm_low) ||
		    !dce_v8_0_average_bandwidth_vs_available_bandwidth(&wm_low) ||
		    !dce_v8_0_check_latency_hiding(&wm_low) ||
		    (adev->mode_info.disp_priority == 2)) {
			DRM_DEBUG_KMS("force priority to high\n");
		}
		lb_vblank_lead_lines = DIV_ROUND_UP(lb_size, mode->crtc_hdisplay);
	}

	/* select wm A */
	wm_mask = RREG32(mmDPG_WATERMARK_MASK_CONTROL + amdgpu_crtc->crtc_offset);
	tmp = wm_mask;
	tmp &= ~(3 << DPG_WATERMARK_MASK_CONTROL__URGENCY_WATERMARK_MASK__SHIFT);
	tmp |= (1 << DPG_WATERMARK_MASK_CONTROL__URGENCY_WATERMARK_MASK__SHIFT);
	WREG32(mmDPG_WATERMARK_MASK_CONTROL + amdgpu_crtc->crtc_offset, tmp);
	WREG32(mmDPG_PIPE_URGENCY_CONTROL + amdgpu_crtc->crtc_offset,
	       ((latency_watermark_a << DPG_PIPE_URGENCY_CONTROL__URGENCY_LOW_WATERMARK__SHIFT) |
		(line_time << DPG_PIPE_URGENCY_CONTROL__URGENCY_HIGH_WATERMARK__SHIFT)));
	/* select wm B */
	tmp = RREG32(mmDPG_WATERMARK_MASK_CONTROL + amdgpu_crtc->crtc_offset);
	tmp &= ~(3 << DPG_WATERMARK_MASK_CONTROL__URGENCY_WATERMARK_MASK__SHIFT);
	tmp |= (2 << DPG_WATERMARK_MASK_CONTROL__URGENCY_WATERMARK_MASK__SHIFT);
	WREG32(mmDPG_WATERMARK_MASK_CONTROL + amdgpu_crtc->crtc_offset, tmp);
	WREG32(mmDPG_PIPE_URGENCY_CONTROL + amdgpu_crtc->crtc_offset,
	       ((latency_watermark_b << DPG_PIPE_URGENCY_CONTROL__URGENCY_LOW_WATERMARK__SHIFT) |
		(line_time << DPG_PIPE_URGENCY_CONTROL__URGENCY_HIGH_WATERMARK__SHIFT)));
	/* restore original selection */
	WREG32(mmDPG_WATERMARK_MASK_CONTROL + amdgpu_crtc->crtc_offset, wm_mask);

	/* save values for DPM */
	amdgpu_crtc->line_time = line_time;
	amdgpu_crtc->wm_high = latency_watermark_a;
	amdgpu_crtc->wm_low = latency_watermark_b;
	/* Save number of lines the linebuffer leads before the scanout */
	amdgpu_crtc->lb_vblank_lead_lines = lb_vblank_lead_lines;
}

/**
 * dce_v8_0_bandwidth_update - program display watermarks
 *
 * @adev: amdgpu_device pointer
 *
 * Calculate and program the display watermarks and line
 * buffer allocation (CIK).
 */
static void dce_v8_0_bandwidth_update(struct amdgpu_device *adev)
{
	struct drm_display_mode *mode = NULL;
	u32 num_heads = 0, lb_size;
	int i;

	amdgpu_display_update_priority(adev);

	for (i = 0; i < adev->mode_info.num_crtc; i++) {
		if (adev->mode_info.crtcs[i]->base.enabled)
			num_heads++;
	}
	for (i = 0; i < adev->mode_info.num_crtc; i++) {
		mode = &adev->mode_info.crtcs[i]->base.mode;
		lb_size = dce_v8_0_line_buffer_adjust(adev, adev->mode_info.crtcs[i], mode);
		dce_v8_0_program_watermarks(adev, adev->mode_info.crtcs[i],
					    lb_size, num_heads);
	}
}

static void dce_v8_0_audio_get_connected_pins(struct amdgpu_device *adev)
{
	int i;
	u32 offset, tmp;

	for (i = 0; i < adev->mode_info.audio.num_pins; i++) {
		offset = adev->mode_info.audio.pin[i].offset;
		tmp = RREG32_AUDIO_ENDPT(offset,
					 ixAZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_CONFIGURATION_DEFAULT);
		if (((tmp &
		AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_CONFIGURATION_DEFAULT__PORT_CONNECTIVITY_MASK) >>
		AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_CONFIGURATION_DEFAULT__PORT_CONNECTIVITY__SHIFT) == 1)
			adev->mode_info.audio.pin[i].connected = false;
		else
			adev->mode_info.audio.pin[i].connected = true;
	}
}

static struct amdgpu_audio_pin *dce_v8_0_audio_get_pin(struct amdgpu_device *adev)
{
	int i;

	dce_v8_0_audio_get_connected_pins(adev);

	for (i = 0; i < adev->mode_info.audio.num_pins; i++) {
		if (adev->mode_info.audio.pin[i].connected)
			return &adev->mode_info.audio.pin[i];
	}
	DRM_ERROR("No connected audio pins found!\n");
	return NULL;
}

static void dce_v8_0_afmt_audio_select_pin(struct drm_encoder *encoder)
{
	struct amdgpu_device *adev = drm_to_adev(encoder->dev);
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct amdgpu_encoder_atom_dig *dig = amdgpu_encoder->enc_priv;
	u32 offset;

	if (!dig || !dig->afmt || !dig->afmt->pin)
		return;

	offset = dig->afmt->offset;

	WREG32(mmAFMT_AUDIO_SRC_CONTROL + offset,
	       (dig->afmt->pin->id << AFMT_AUDIO_SRC_CONTROL__AFMT_AUDIO_SRC_SELECT__SHIFT));
}

static void dce_v8_0_audio_write_latency_fields(struct drm_encoder *encoder,
						struct drm_display_mode *mode)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct amdgpu_encoder_atom_dig *dig = amdgpu_encoder->enc_priv;
	struct drm_connector *connector;
	struct drm_connector_list_iter iter;
	struct amdgpu_connector *amdgpu_connector = NULL;
	u32 tmp = 0, offset;

	if (!dig || !dig->afmt || !dig->afmt->pin)
		return;

	offset = dig->afmt->pin->offset;

	drm_connector_list_iter_begin(dev, &iter);
	drm_for_each_connector_iter(connector, &iter) {
		if (connector->encoder == encoder) {
			amdgpu_connector = to_amdgpu_connector(connector);
			break;
		}
	}
	drm_connector_list_iter_end(&iter);

	if (!amdgpu_connector) {
		DRM_ERROR("Couldn't find encoder's connector\n");
		return;
	}

	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		if (connector->latency_present[1])
			tmp =
			(connector->video_latency[1] <<
			 AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_LIPSYNC__VIDEO_LIPSYNC__SHIFT) |
			(connector->audio_latency[1] <<
			 AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_LIPSYNC__AUDIO_LIPSYNC__SHIFT);
		else
			tmp =
			(0 <<
			 AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_LIPSYNC__VIDEO_LIPSYNC__SHIFT) |
			(0 <<
			 AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_LIPSYNC__AUDIO_LIPSYNC__SHIFT);
	} else {
		if (connector->latency_present[0])
			tmp =
			(connector->video_latency[0] <<
			 AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_LIPSYNC__VIDEO_LIPSYNC__SHIFT) |
			(connector->audio_latency[0] <<
			 AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_LIPSYNC__AUDIO_LIPSYNC__SHIFT);
		else
			tmp =
			(0 <<
			 AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_LIPSYNC__VIDEO_LIPSYNC__SHIFT) |
			(0 <<
			 AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_LIPSYNC__AUDIO_LIPSYNC__SHIFT);

	}
	WREG32_AUDIO_ENDPT(offset, ixAZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_LIPSYNC, tmp);
}

static void dce_v8_0_audio_write_speaker_allocation(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct amdgpu_encoder_atom_dig *dig = amdgpu_encoder->enc_priv;
	struct drm_connector *connector;
	struct drm_connector_list_iter iter;
	struct amdgpu_connector *amdgpu_connector = NULL;
	u32 offset, tmp;
	u8 *sadb = NULL;
	int sad_count;

	if (!dig || !dig->afmt || !dig->afmt->pin)
		return;

	offset = dig->afmt->pin->offset;

	drm_connector_list_iter_begin(dev, &iter);
	drm_for_each_connector_iter(connector, &iter) {
		if (connector->encoder == encoder) {
			amdgpu_connector = to_amdgpu_connector(connector);
			break;
		}
	}
	drm_connector_list_iter_end(&iter);

	if (!amdgpu_connector) {
		DRM_ERROR("Couldn't find encoder's connector\n");
		return;
	}

	sad_count = drm_edid_to_speaker_allocation(amdgpu_connector_edid(connector), &sadb);
	if (sad_count < 0) {
		DRM_ERROR("Couldn't read Speaker Allocation Data Block: %d\n", sad_count);
		sad_count = 0;
	}

	/* program the speaker allocation */
	tmp = RREG32_AUDIO_ENDPT(offset, ixAZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER);
	tmp &= ~(AZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER__DP_CONNECTION_MASK |
		AZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER__SPEAKER_ALLOCATION_MASK);
	/* set HDMI mode */
	tmp |= AZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER__HDMI_CONNECTION_MASK;
	if (sad_count)
		tmp |= (sadb[0] << AZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER__SPEAKER_ALLOCATION__SHIFT);
	else
		tmp |= (5 << AZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER__SPEAKER_ALLOCATION__SHIFT); /* stereo */
	WREG32_AUDIO_ENDPT(offset, ixAZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER, tmp);

	kfree(sadb);
}

static void dce_v8_0_audio_write_sad_regs(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct amdgpu_encoder_atom_dig *dig = amdgpu_encoder->enc_priv;
	u32 offset;
	struct drm_connector *connector;
	struct drm_connector_list_iter iter;
	struct amdgpu_connector *amdgpu_connector = NULL;
	struct cea_sad *sads;
	int i, sad_count;

	static const u16 eld_reg_to_type[][2] = {
		{ ixAZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR0, HDMI_AUDIO_CODING_TYPE_PCM },
		{ ixAZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR1, HDMI_AUDIO_CODING_TYPE_AC3 },
		{ ixAZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR2, HDMI_AUDIO_CODING_TYPE_MPEG1 },
		{ ixAZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR3, HDMI_AUDIO_CODING_TYPE_MP3 },
		{ ixAZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR4, HDMI_AUDIO_CODING_TYPE_MPEG2 },
		{ ixAZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR5, HDMI_AUDIO_CODING_TYPE_AAC_LC },
		{ ixAZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR6, HDMI_AUDIO_CODING_TYPE_DTS },
		{ ixAZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR7, HDMI_AUDIO_CODING_TYPE_ATRAC },
		{ ixAZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR9, HDMI_AUDIO_CODING_TYPE_EAC3 },
		{ ixAZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR10, HDMI_AUDIO_CODING_TYPE_DTS_HD },
		{ ixAZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR11, HDMI_AUDIO_CODING_TYPE_MLP },
		{ ixAZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR13, HDMI_AUDIO_CODING_TYPE_WMA_PRO },
	};

	if (!dig || !dig->afmt || !dig->afmt->pin)
		return;

	offset = dig->afmt->pin->offset;

	drm_connector_list_iter_begin(dev, &iter);
	drm_for_each_connector_iter(connector, &iter) {
		if (connector->encoder == encoder) {
			amdgpu_connector = to_amdgpu_connector(connector);
			break;
		}
	}
	drm_connector_list_iter_end(&iter);

	if (!amdgpu_connector) {
		DRM_ERROR("Couldn't find encoder's connector\n");
		return;
	}

	sad_count = drm_edid_to_sad(amdgpu_connector_edid(connector), &sads);
	if (sad_count < 0)
		DRM_ERROR("Couldn't read SADs: %d\n", sad_count);
	if (sad_count <= 0)
		return;
	BUG_ON(!sads);

	for (i = 0; i < ARRAY_SIZE(eld_reg_to_type); i++) {
		u32 value = 0;
		u8 stereo_freqs = 0;
		int max_channels = -1;
		int j;

		for (j = 0; j < sad_count; j++) {
			struct cea_sad *sad = &sads[j];

			if (sad->format == eld_reg_to_type[i][1]) {
				if (sad->channels > max_channels) {
					value = (sad->channels <<
						 AZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR0__MAX_CHANNELS__SHIFT) |
					        (sad->byte2 <<
						 AZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR0__DESCRIPTOR_BYTE_2__SHIFT) |
					        (sad->freq <<
						 AZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR0__SUPPORTED_FREQUENCIES__SHIFT);
					max_channels = sad->channels;
				}

				if (sad->format == HDMI_AUDIO_CODING_TYPE_PCM)
					stereo_freqs |= sad->freq;
				else
					break;
			}
		}

		value |= (stereo_freqs <<
			AZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR0__SUPPORTED_FREQUENCIES_STEREO__SHIFT);

		WREG32_AUDIO_ENDPT(offset, eld_reg_to_type[i][0], value);
	}

	kfree(sads);
}

static void dce_v8_0_audio_enable(struct amdgpu_device *adev,
				  struct amdgpu_audio_pin *pin,
				  bool enable)
{
	if (!pin)
		return;

	WREG32_AUDIO_ENDPT(pin->offset, ixAZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL,
		enable ? AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL__AUDIO_ENABLED_MASK : 0);
}

static const u32 pin_offsets[7] =
{
	(0x1780 - 0x1780),
	(0x1786 - 0x1780),
	(0x178c - 0x1780),
	(0x1792 - 0x1780),
	(0x1798 - 0x1780),
	(0x179d - 0x1780),
	(0x17a4 - 0x1780),
};

static int dce_v8_0_audio_init(struct amdgpu_device *adev)
{
	int i;

	if (!amdgpu_audio)
		return 0;

	adev->mode_info.audio.enabled = true;

	if (adev->asic_type == CHIP_KAVERI) /* KV: 4 streams, 7 endpoints */
		adev->mode_info.audio.num_pins = 7;
	else if ((adev->asic_type == CHIP_KABINI) ||
		 (adev->asic_type == CHIP_MULLINS)) /* KB/ML: 2 streams, 3 endpoints */
		adev->mode_info.audio.num_pins = 3;
	else if ((adev->asic_type == CHIP_BONAIRE) ||
		 (adev->asic_type == CHIP_HAWAII))/* BN/HW: 6 streams, 7 endpoints */
		adev->mode_info.audio.num_pins = 7;
	else
		adev->mode_info.audio.num_pins = 3;

	for (i = 0; i < adev->mode_info.audio.num_pins; i++) {
		adev->mode_info.audio.pin[i].channels = -1;
		adev->mode_info.audio.pin[i].rate = -1;
		adev->mode_info.audio.pin[i].bits_per_sample = -1;
		adev->mode_info.audio.pin[i].status_bits = 0;
		adev->mode_info.audio.pin[i].category_code = 0;
		adev->mode_info.audio.pin[i].connected = false;
		adev->mode_info.audio.pin[i].offset = pin_offsets[i];
		adev->mode_info.audio.pin[i].id = i;
		/* disable audio.  it will be set up later */
		/* XXX remove once we switch to ip funcs */
		dce_v8_0_audio_enable(adev, &adev->mode_info.audio.pin[i], false);
	}

	return 0;
}

static void dce_v8_0_audio_fini(struct amdgpu_device *adev)
{
	int i;

	if (!amdgpu_audio)
		return;

	if (!adev->mode_info.audio.enabled)
		return;

	for (i = 0; i < adev->mode_info.audio.num_pins; i++)
		dce_v8_0_audio_enable(adev, &adev->mode_info.audio.pin[i], false);

	adev->mode_info.audio.enabled = false;
}

/*
 * update the N and CTS parameters for a given pixel clock rate
 */
static void dce_v8_0_afmt_update_ACR(struct drm_encoder *encoder, uint32_t clock)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_afmt_acr acr = amdgpu_afmt_acr(clock);
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct amdgpu_encoder_atom_dig *dig = amdgpu_encoder->enc_priv;
	uint32_t offset = dig->afmt->offset;

	WREG32(mmHDMI_ACR_32_0 + offset, (acr.cts_32khz << HDMI_ACR_32_0__HDMI_ACR_CTS_32__SHIFT));
	WREG32(mmHDMI_ACR_32_1 + offset, acr.n_32khz);

	WREG32(mmHDMI_ACR_44_0 + offset, (acr.cts_44_1khz << HDMI_ACR_44_0__HDMI_ACR_CTS_44__SHIFT));
	WREG32(mmHDMI_ACR_44_1 + offset, acr.n_44_1khz);

	WREG32(mmHDMI_ACR_48_0 + offset, (acr.cts_48khz << HDMI_ACR_48_0__HDMI_ACR_CTS_48__SHIFT));
	WREG32(mmHDMI_ACR_48_1 + offset, acr.n_48khz);
}

/*
 * build a HDMI Video Info Frame
 */
static void dce_v8_0_afmt_update_avi_infoframe(struct drm_encoder *encoder,
					       void *buffer, size_t size)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct amdgpu_encoder_atom_dig *dig = amdgpu_encoder->enc_priv;
	uint32_t offset = dig->afmt->offset;
	uint8_t *frame = buffer + 3;
	uint8_t *header = buffer;

	WREG32(mmAFMT_AVI_INFO0 + offset,
		frame[0x0] | (frame[0x1] << 8) | (frame[0x2] << 16) | (frame[0x3] << 24));
	WREG32(mmAFMT_AVI_INFO1 + offset,
		frame[0x4] | (frame[0x5] << 8) | (frame[0x6] << 16) | (frame[0x7] << 24));
	WREG32(mmAFMT_AVI_INFO2 + offset,
		frame[0x8] | (frame[0x9] << 8) | (frame[0xA] << 16) | (frame[0xB] << 24));
	WREG32(mmAFMT_AVI_INFO3 + offset,
		frame[0xC] | (frame[0xD] << 8) | (header[1] << 24));
}

static void dce_v8_0_audio_set_dto(struct drm_encoder *encoder, u32 clock)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct amdgpu_encoder_atom_dig *dig = amdgpu_encoder->enc_priv;
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(encoder->crtc);
	u32 dto_phase = 24 * 1000;
	u32 dto_modulo = clock;

	if (!dig || !dig->afmt)
		return;

	/* XXX two dtos; generally use dto0 for hdmi */
	/* Express [24MHz / target pixel clock] as an exact rational
	 * number (coefficient of two integer numbers.  DCCG_AUDIO_DTOx_PHASE
	 * is the numerator, DCCG_AUDIO_DTOx_MODULE is the denominator
	 */
	WREG32(mmDCCG_AUDIO_DTO_SOURCE, (amdgpu_crtc->crtc_id << DCCG_AUDIO_DTO_SOURCE__DCCG_AUDIO_DTO0_SOURCE_SEL__SHIFT));
	WREG32(mmDCCG_AUDIO_DTO0_PHASE, dto_phase);
	WREG32(mmDCCG_AUDIO_DTO0_MODULE, dto_modulo);
}

/*
 * update the info frames with the data from the current display mode
 */
static void dce_v8_0_afmt_setmode(struct drm_encoder *encoder,
				  struct drm_display_mode *mode)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct amdgpu_encoder_atom_dig *dig = amdgpu_encoder->enc_priv;
	struct drm_connector *connector = amdgpu_get_connector_for_encoder(encoder);
	u8 buffer[HDMI_INFOFRAME_HEADER_SIZE + HDMI_AVI_INFOFRAME_SIZE];
	struct hdmi_avi_infoframe frame;
	uint32_t offset, val;
	ssize_t err;
	int bpc = 8;

	if (!dig || !dig->afmt)
		return;

	/* Silent, r600_hdmi_enable will raise WARN for us */
	if (!dig->afmt->enabled)
		return;

	offset = dig->afmt->offset;

	/* hdmi deep color mode general control packets setup, if bpc > 8 */
	if (encoder->crtc) {
		struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(encoder->crtc);
		bpc = amdgpu_crtc->bpc;
	}

	/* disable audio prior to setting up hw */
	dig->afmt->pin = dce_v8_0_audio_get_pin(adev);
	dce_v8_0_audio_enable(adev, dig->afmt->pin, false);

	dce_v8_0_audio_set_dto(encoder, mode->clock);

	WREG32(mmHDMI_VBI_PACKET_CONTROL + offset,
	       HDMI_VBI_PACKET_CONTROL__HDMI_NULL_SEND_MASK); /* send null packets when required */

	WREG32(mmAFMT_AUDIO_CRC_CONTROL + offset, 0x1000);

	val = RREG32(mmHDMI_CONTROL + offset);
	val &= ~HDMI_CONTROL__HDMI_DEEP_COLOR_ENABLE_MASK;
	val &= ~HDMI_CONTROL__HDMI_DEEP_COLOR_DEPTH_MASK;

	switch (bpc) {
	case 0:
	case 6:
	case 8:
	case 16:
	default:
		DRM_DEBUG("%s: Disabling hdmi deep color for %d bpc.\n",
			  connector->name, bpc);
		break;
	case 10:
		val |= HDMI_CONTROL__HDMI_DEEP_COLOR_ENABLE_MASK;
		val |= 1 << HDMI_CONTROL__HDMI_DEEP_COLOR_DEPTH__SHIFT;
		DRM_DEBUG("%s: Enabling hdmi deep color 30 for 10 bpc.\n",
			  connector->name);
		break;
	case 12:
		val |= HDMI_CONTROL__HDMI_DEEP_COLOR_ENABLE_MASK;
		val |= 2 << HDMI_CONTROL__HDMI_DEEP_COLOR_DEPTH__SHIFT;
		DRM_DEBUG("%s: Enabling hdmi deep color 36 for 12 bpc.\n",
			  connector->name);
		break;
	}

	WREG32(mmHDMI_CONTROL + offset, val);

	WREG32(mmHDMI_VBI_PACKET_CONTROL + offset,
	       HDMI_VBI_PACKET_CONTROL__HDMI_NULL_SEND_MASK | /* send null packets when required */
	       HDMI_VBI_PACKET_CONTROL__HDMI_GC_SEND_MASK | /* send general control packets */
	       HDMI_VBI_PACKET_CONTROL__HDMI_GC_CONT_MASK); /* send general control packets every frame */

	WREG32(mmHDMI_INFOFRAME_CONTROL0 + offset,
	       HDMI_INFOFRAME_CONTROL0__HDMI_AUDIO_INFO_SEND_MASK | /* enable audio info frames (frames won't be set until audio is enabled) */
	       HDMI_INFOFRAME_CONTROL0__HDMI_AUDIO_INFO_CONT_MASK); /* required for audio info values to be updated */

	WREG32(mmAFMT_INFOFRAME_CONTROL0 + offset,
	       AFMT_INFOFRAME_CONTROL0__AFMT_AUDIO_INFO_UPDATE_MASK); /* required for audio info values to be updated */

	WREG32(mmHDMI_INFOFRAME_CONTROL1 + offset,
	       (2 << HDMI_INFOFRAME_CONTROL1__HDMI_AUDIO_INFO_LINE__SHIFT)); /* anything other than 0 */

	WREG32(mmHDMI_GC + offset, 0); /* unset HDMI_GC_AVMUTE */

	WREG32(mmHDMI_AUDIO_PACKET_CONTROL + offset,
	       (1 << HDMI_AUDIO_PACKET_CONTROL__HDMI_AUDIO_DELAY_EN__SHIFT) | /* set the default audio delay */
	       (3 << HDMI_AUDIO_PACKET_CONTROL__HDMI_AUDIO_PACKETS_PER_LINE__SHIFT)); /* should be suffient for all audio modes and small enough for all hblanks */

	WREG32(mmAFMT_AUDIO_PACKET_CONTROL + offset,
	       AFMT_AUDIO_PACKET_CONTROL__AFMT_60958_CS_UPDATE_MASK); /* allow 60958 channel status fields to be updated */

	/* fglrx clears sth in AFMT_AUDIO_PACKET_CONTROL2 here */

	if (bpc > 8)
		WREG32(mmHDMI_ACR_PACKET_CONTROL + offset,
		       HDMI_ACR_PACKET_CONTROL__HDMI_ACR_AUTO_SEND_MASK); /* allow hw to sent ACR packets when required */
	else
		WREG32(mmHDMI_ACR_PACKET_CONTROL + offset,
		       HDMI_ACR_PACKET_CONTROL__HDMI_ACR_SOURCE_MASK | /* select SW CTS value */
		       HDMI_ACR_PACKET_CONTROL__HDMI_ACR_AUTO_SEND_MASK); /* allow hw to sent ACR packets when required */

	dce_v8_0_afmt_update_ACR(encoder, mode->clock);

	WREG32(mmAFMT_60958_0 + offset,
	       (1 << AFMT_60958_0__AFMT_60958_CS_CHANNEL_NUMBER_L__SHIFT));

	WREG32(mmAFMT_60958_1 + offset,
	       (2 << AFMT_60958_1__AFMT_60958_CS_CHANNEL_NUMBER_R__SHIFT));

	WREG32(mmAFMT_60958_2 + offset,
	       (3 << AFMT_60958_2__AFMT_60958_CS_CHANNEL_NUMBER_2__SHIFT) |
	       (4 << AFMT_60958_2__AFMT_60958_CS_CHANNEL_NUMBER_3__SHIFT) |
	       (5 << AFMT_60958_2__AFMT_60958_CS_CHANNEL_NUMBER_4__SHIFT) |
	       (6 << AFMT_60958_2__AFMT_60958_CS_CHANNEL_NUMBER_5__SHIFT) |
	       (7 << AFMT_60958_2__AFMT_60958_CS_CHANNEL_NUMBER_6__SHIFT) |
	       (8 << AFMT_60958_2__AFMT_60958_CS_CHANNEL_NUMBER_7__SHIFT));

	dce_v8_0_audio_write_speaker_allocation(encoder);


	WREG32(mmAFMT_AUDIO_PACKET_CONTROL2 + offset,
	       (0xff << AFMT_AUDIO_PACKET_CONTROL2__AFMT_AUDIO_CHANNEL_ENABLE__SHIFT));

	dce_v8_0_afmt_audio_select_pin(encoder);
	dce_v8_0_audio_write_sad_regs(encoder);
	dce_v8_0_audio_write_latency_fields(encoder, mode);

	err = drm_hdmi_avi_infoframe_from_display_mode(&frame, connector, mode);
	if (err < 0) {
		DRM_ERROR("failed to setup AVI infoframe: %zd\n", err);
		return;
	}

	err = hdmi_avi_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (err < 0) {
		DRM_ERROR("failed to pack AVI infoframe: %zd\n", err);
		return;
	}

	dce_v8_0_afmt_update_avi_infoframe(encoder, buffer, sizeof(buffer));

	WREG32_OR(mmHDMI_INFOFRAME_CONTROL0 + offset,
		  HDMI_INFOFRAME_CONTROL0__HDMI_AVI_INFO_SEND_MASK | /* enable AVI info frames */
		  HDMI_INFOFRAME_CONTROL0__HDMI_AVI_INFO_CONT_MASK); /* required for audio info values to be updated */

	WREG32_P(mmHDMI_INFOFRAME_CONTROL1 + offset,
		 (2 << HDMI_INFOFRAME_CONTROL1__HDMI_AVI_INFO_LINE__SHIFT), /* anything other than 0 */
		 ~HDMI_INFOFRAME_CONTROL1__HDMI_AVI_INFO_LINE_MASK);

	WREG32_OR(mmAFMT_AUDIO_PACKET_CONTROL + offset,
		  AFMT_AUDIO_PACKET_CONTROL__AFMT_AUDIO_SAMPLE_SEND_MASK); /* send audio packets */

	WREG32(mmAFMT_RAMP_CONTROL0 + offset, 0x00FFFFFF);
	WREG32(mmAFMT_RAMP_CONTROL1 + offset, 0x007FFFFF);
	WREG32(mmAFMT_RAMP_CONTROL2 + offset, 0x00000001);
	WREG32(mmAFMT_RAMP_CONTROL3 + offset, 0x00000001);

	/* enable audio after setting up hw */
	dce_v8_0_audio_enable(adev, dig->afmt->pin, true);
}

static void dce_v8_0_afmt_enable(struct drm_encoder *encoder, bool enable)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct amdgpu_encoder_atom_dig *dig = amdgpu_encoder->enc_priv;

	if (!dig || !dig->afmt)
		return;

	/* Silent, r600_hdmi_enable will raise WARN for us */
	if (enable && dig->afmt->enabled)
		return;
	if (!enable && !dig->afmt->enabled)
		return;

	if (!enable && dig->afmt->pin) {
		dce_v8_0_audio_enable(adev, dig->afmt->pin, false);
		dig->afmt->pin = NULL;
	}

	dig->afmt->enabled = enable;

	DRM_DEBUG("%sabling AFMT interface @ 0x%04X for encoder 0x%x\n",
		  enable ? "En" : "Dis", dig->afmt->offset, amdgpu_encoder->encoder_id);
}

static int dce_v8_0_afmt_init(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->mode_info.num_dig; i++)
		adev->mode_info.afmt[i] = NULL;

	/* DCE8 has audio blocks tied to DIG encoders */
	for (i = 0; i < adev->mode_info.num_dig; i++) {
		adev->mode_info.afmt[i] = kzalloc(sizeof(struct amdgpu_afmt), GFP_KERNEL);
		if (adev->mode_info.afmt[i]) {
			adev->mode_info.afmt[i]->offset = dig_offsets[i];
			adev->mode_info.afmt[i]->id = i;
		} else {
			int j;
			for (j = 0; j < i; j++) {
				kfree(adev->mode_info.afmt[j]);
				adev->mode_info.afmt[j] = NULL;
			}
			return -ENOMEM;
		}
	}
	return 0;
}

static void dce_v8_0_afmt_fini(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->mode_info.num_dig; i++) {
		kfree(adev->mode_info.afmt[i]);
		adev->mode_info.afmt[i] = NULL;
	}
}

static const u32 vga_control_regs[6] =
{
	mmD1VGA_CONTROL,
	mmD2VGA_CONTROL,
	mmD3VGA_CONTROL,
	mmD4VGA_CONTROL,
	mmD5VGA_CONTROL,
	mmD6VGA_CONTROL,
};

static void dce_v8_0_vga_enable(struct drm_crtc *crtc, bool enable)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	u32 vga_control;

	vga_control = RREG32(vga_control_regs[amdgpu_crtc->crtc_id]) & ~1;
	if (enable)
		WREG32(vga_control_regs[amdgpu_crtc->crtc_id], vga_control | 1);
	else
		WREG32(vga_control_regs[amdgpu_crtc->crtc_id], vga_control);
}

static void dce_v8_0_grph_enable(struct drm_crtc *crtc, bool enable)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);

	if (enable)
		WREG32(mmGRPH_ENABLE + amdgpu_crtc->crtc_offset, 1);
	else
		WREG32(mmGRPH_ENABLE + amdgpu_crtc->crtc_offset, 0);
}

static int dce_v8_0_crtc_do_set_base(struct drm_crtc *crtc,
				     struct drm_framebuffer *fb,
				     int x, int y, int atomic)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct drm_framebuffer *target_fb;
	struct drm_gem_object *obj;
	struct amdgpu_bo *abo;
	uint64_t fb_location, tiling_flags;
	uint32_t fb_format, fb_pitch_pixels;
	u32 fb_swap = (GRPH_ENDIAN_NONE << GRPH_SWAP_CNTL__GRPH_ENDIAN_SWAP__SHIFT);
	u32 pipe_config;
	u32 viewport_w, viewport_h;
	int r;
	bool bypass_lut = false;

	/* no fb bound */
	if (!atomic && !crtc->primary->fb) {
		DRM_DEBUG_KMS("No FB bound\n");
		return 0;
	}

	if (atomic)
		target_fb = fb;
	else
		target_fb = crtc->primary->fb;

	/* If atomic, assume fb object is pinned & idle & fenced and
	 * just update base pointers
	 */
	obj = target_fb->obj[0];
	abo = gem_to_amdgpu_bo(obj);
	r = amdgpu_bo_reserve(abo, false);
	if (unlikely(r != 0))
		return r;

	if (!atomic) {
		r = amdgpu_bo_pin(abo, AMDGPU_GEM_DOMAIN_VRAM);
		if (unlikely(r != 0)) {
			amdgpu_bo_unreserve(abo);
			return -EINVAL;
		}
	}
	fb_location = amdgpu_bo_gpu_offset(abo);

	amdgpu_bo_get_tiling_flags(abo, &tiling_flags);
	amdgpu_bo_unreserve(abo);

	pipe_config = AMDGPU_TILING_GET(tiling_flags, PIPE_CONFIG);

	switch (target_fb->format->format) {
	case DRM_FORMAT_C8:
		fb_format = ((GRPH_DEPTH_8BPP << GRPH_CONTROL__GRPH_DEPTH__SHIFT) |
			     (GRPH_FORMAT_INDEXED << GRPH_CONTROL__GRPH_FORMAT__SHIFT));
		break;
	case DRM_FORMAT_XRGB4444:
	case DRM_FORMAT_ARGB4444:
		fb_format = ((GRPH_DEPTH_16BPP << GRPH_CONTROL__GRPH_DEPTH__SHIFT) |
			     (GRPH_FORMAT_ARGB4444 << GRPH_CONTROL__GRPH_FORMAT__SHIFT));
#ifdef __BIG_ENDIAN
		fb_swap = (GRPH_ENDIAN_8IN16 << GRPH_SWAP_CNTL__GRPH_ENDIAN_SWAP__SHIFT);
#endif
		break;
	case DRM_FORMAT_XRGB1555:
	case DRM_FORMAT_ARGB1555:
		fb_format = ((GRPH_DEPTH_16BPP << GRPH_CONTROL__GRPH_DEPTH__SHIFT) |
			     (GRPH_FORMAT_ARGB1555 << GRPH_CONTROL__GRPH_FORMAT__SHIFT));
#ifdef __BIG_ENDIAN
		fb_swap = (GRPH_ENDIAN_8IN16 << GRPH_SWAP_CNTL__GRPH_ENDIAN_SWAP__SHIFT);
#endif
		break;
	case DRM_FORMAT_BGRX5551:
	case DRM_FORMAT_BGRA5551:
		fb_format = ((GRPH_DEPTH_16BPP << GRPH_CONTROL__GRPH_DEPTH__SHIFT) |
			     (GRPH_FORMAT_BGRA5551 << GRPH_CONTROL__GRPH_FORMAT__SHIFT));
#ifdef __BIG_ENDIAN
		fb_swap = (GRPH_ENDIAN_8IN16 << GRPH_SWAP_CNTL__GRPH_ENDIAN_SWAP__SHIFT);
#endif
		break;
	case DRM_FORMAT_RGB565:
		fb_format = ((GRPH_DEPTH_16BPP << GRPH_CONTROL__GRPH_DEPTH__SHIFT) |
			     (GRPH_FORMAT_ARGB565 << GRPH_CONTROL__GRPH_FORMAT__SHIFT));
#ifdef __BIG_ENDIAN
		fb_swap = (GRPH_ENDIAN_8IN16 << GRPH_SWAP_CNTL__GRPH_ENDIAN_SWAP__SHIFT);
#endif
		break;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		fb_format = ((GRPH_DEPTH_32BPP << GRPH_CONTROL__GRPH_DEPTH__SHIFT) |
			     (GRPH_FORMAT_ARGB8888 << GRPH_CONTROL__GRPH_FORMAT__SHIFT));
#ifdef __BIG_ENDIAN
		fb_swap = (GRPH_ENDIAN_8IN32 << GRPH_SWAP_CNTL__GRPH_ENDIAN_SWAP__SHIFT);
#endif
		break;
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_ARGB2101010:
		fb_format = ((GRPH_DEPTH_32BPP << GRPH_CONTROL__GRPH_DEPTH__SHIFT) |
			     (GRPH_FORMAT_ARGB2101010 << GRPH_CONTROL__GRPH_FORMAT__SHIFT));
#ifdef __BIG_ENDIAN
		fb_swap = (GRPH_ENDIAN_8IN32 << GRPH_SWAP_CNTL__GRPH_ENDIAN_SWAP__SHIFT);
#endif
		/* Greater 8 bpc fb needs to bypass hw-lut to retain precision */
		bypass_lut = true;
		break;
	case DRM_FORMAT_BGRX1010102:
	case DRM_FORMAT_BGRA1010102:
		fb_format = ((GRPH_DEPTH_32BPP << GRPH_CONTROL__GRPH_DEPTH__SHIFT) |
			     (GRPH_FORMAT_BGRA1010102 << GRPH_CONTROL__GRPH_FORMAT__SHIFT));
#ifdef __BIG_ENDIAN
		fb_swap = (GRPH_ENDIAN_8IN32 << GRPH_SWAP_CNTL__GRPH_ENDIAN_SWAP__SHIFT);
#endif
		/* Greater 8 bpc fb needs to bypass hw-lut to retain precision */
		bypass_lut = true;
		break;
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
		fb_format = ((GRPH_DEPTH_32BPP << GRPH_CONTROL__GRPH_DEPTH__SHIFT) |
		             (GRPH_FORMAT_ARGB8888 << GRPH_CONTROL__GRPH_FORMAT__SHIFT));
		fb_swap = ((GRPH_RED_SEL_B << GRPH_SWAP_CNTL__GRPH_RED_CROSSBAR__SHIFT) |
		           (GRPH_BLUE_SEL_R << GRPH_SWAP_CNTL__GRPH_BLUE_CROSSBAR__SHIFT));
#ifdef __BIG_ENDIAN
		fb_swap |= (GRPH_ENDIAN_8IN32 << GRPH_SWAP_CNTL__GRPH_ENDIAN_SWAP__SHIFT);
#endif
		break;
	default:
		DRM_ERROR("Unsupported screen format %p4cc\n",
			  &target_fb->format->format);
		return -EINVAL;
	}

	if (AMDGPU_TILING_GET(tiling_flags, ARRAY_MODE) == ARRAY_2D_TILED_THIN1) {
		unsigned bankw, bankh, mtaspect, tile_split, num_banks;

		bankw = AMDGPU_TILING_GET(tiling_flags, BANK_WIDTH);
		bankh = AMDGPU_TILING_GET(tiling_flags, BANK_HEIGHT);
		mtaspect = AMDGPU_TILING_GET(tiling_flags, MACRO_TILE_ASPECT);
		tile_split = AMDGPU_TILING_GET(tiling_flags, TILE_SPLIT);
		num_banks = AMDGPU_TILING_GET(tiling_flags, NUM_BANKS);

		fb_format |= (num_banks << GRPH_CONTROL__GRPH_NUM_BANKS__SHIFT);
		fb_format |= (GRPH_ARRAY_2D_TILED_THIN1 << GRPH_CONTROL__GRPH_ARRAY_MODE__SHIFT);
		fb_format |= (tile_split << GRPH_CONTROL__GRPH_TILE_SPLIT__SHIFT);
		fb_format |= (bankw << GRPH_CONTROL__GRPH_BANK_WIDTH__SHIFT);
		fb_format |= (bankh << GRPH_CONTROL__GRPH_BANK_HEIGHT__SHIFT);
		fb_format |= (mtaspect << GRPH_CONTROL__GRPH_MACRO_TILE_ASPECT__SHIFT);
		fb_format |= (DISPLAY_MICRO_TILING << GRPH_CONTROL__GRPH_MICRO_TILE_MODE__SHIFT);
	} else if (AMDGPU_TILING_GET(tiling_flags, ARRAY_MODE) == ARRAY_1D_TILED_THIN1) {
		fb_format |= (GRPH_ARRAY_1D_TILED_THIN1 << GRPH_CONTROL__GRPH_ARRAY_MODE__SHIFT);
	}

	fb_format |= (pipe_config << GRPH_CONTROL__GRPH_PIPE_CONFIG__SHIFT);

	dce_v8_0_vga_enable(crtc, false);

	/* Make sure surface address is updated at vertical blank rather than
	 * horizontal blank
	 */
	WREG32(mmGRPH_FLIP_CONTROL + amdgpu_crtc->crtc_offset, 0);

	WREG32(mmGRPH_PRIMARY_SURFACE_ADDRESS_HIGH + amdgpu_crtc->crtc_offset,
	       upper_32_bits(fb_location));
	WREG32(mmGRPH_SECONDARY_SURFACE_ADDRESS_HIGH + amdgpu_crtc->crtc_offset,
	       upper_32_bits(fb_location));
	WREG32(mmGRPH_PRIMARY_SURFACE_ADDRESS + amdgpu_crtc->crtc_offset,
	       (u32)fb_location & GRPH_PRIMARY_SURFACE_ADDRESS__GRPH_PRIMARY_SURFACE_ADDRESS_MASK);
	WREG32(mmGRPH_SECONDARY_SURFACE_ADDRESS + amdgpu_crtc->crtc_offset,
	       (u32) fb_location & GRPH_SECONDARY_SURFACE_ADDRESS__GRPH_SECONDARY_SURFACE_ADDRESS_MASK);
	WREG32(mmGRPH_CONTROL + amdgpu_crtc->crtc_offset, fb_format);
	WREG32(mmGRPH_SWAP_CNTL + amdgpu_crtc->crtc_offset, fb_swap);

	/*
	 * The LUT only has 256 slots for indexing by a 8 bpc fb. Bypass the LUT
	 * for > 8 bpc scanout to avoid truncation of fb indices to 8 msb's, to
	 * retain the full precision throughout the pipeline.
	 */
	WREG32_P(mmGRPH_LUT_10BIT_BYPASS_CONTROL + amdgpu_crtc->crtc_offset,
		 (bypass_lut ? LUT_10BIT_BYPASS_EN : 0),
		 ~LUT_10BIT_BYPASS_EN);

	if (bypass_lut)
		DRM_DEBUG_KMS("Bypassing hardware LUT due to 10 bit fb scanout.\n");

	WREG32(mmGRPH_SURFACE_OFFSET_X + amdgpu_crtc->crtc_offset, 0);
	WREG32(mmGRPH_SURFACE_OFFSET_Y + amdgpu_crtc->crtc_offset, 0);
	WREG32(mmGRPH_X_START + amdgpu_crtc->crtc_offset, 0);
	WREG32(mmGRPH_Y_START + amdgpu_crtc->crtc_offset, 0);
	WREG32(mmGRPH_X_END + amdgpu_crtc->crtc_offset, target_fb->width);
	WREG32(mmGRPH_Y_END + amdgpu_crtc->crtc_offset, target_fb->height);

	fb_pitch_pixels = target_fb->pitches[0] / target_fb->format->cpp[0];
	WREG32(mmGRPH_PITCH + amdgpu_crtc->crtc_offset, fb_pitch_pixels);

	dce_v8_0_grph_enable(crtc, true);

	WREG32(mmLB_DESKTOP_HEIGHT + amdgpu_crtc->crtc_offset,
	       target_fb->height);

	x &= ~3;
	y &= ~1;
	WREG32(mmVIEWPORT_START + amdgpu_crtc->crtc_offset,
	       (x << 16) | y);
	viewport_w = crtc->mode.hdisplay;
	viewport_h = (crtc->mode.vdisplay + 1) & ~1;
	WREG32(mmVIEWPORT_SIZE + amdgpu_crtc->crtc_offset,
	       (viewport_w << 16) | viewport_h);

	/* set pageflip to happen anywhere in vblank interval */
	WREG32(mmMASTER_UPDATE_MODE + amdgpu_crtc->crtc_offset, 0);

	if (!atomic && fb && fb != crtc->primary->fb) {
		abo = gem_to_amdgpu_bo(fb->obj[0]);
		r = amdgpu_bo_reserve(abo, true);
		if (unlikely(r != 0))
			return r;
		amdgpu_bo_unpin(abo);
		amdgpu_bo_unreserve(abo);
	}

	/* Bytes per pixel may have changed */
	dce_v8_0_bandwidth_update(adev);

	return 0;
}

static void dce_v8_0_set_interleave(struct drm_crtc *crtc,
				    struct drm_display_mode *mode)
{
	struct drm_device *dev = crtc->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		WREG32(mmLB_DATA_FORMAT + amdgpu_crtc->crtc_offset,
		       LB_DATA_FORMAT__INTERLEAVE_EN__SHIFT);
	else
		WREG32(mmLB_DATA_FORMAT + amdgpu_crtc->crtc_offset, 0);
}

static void dce_v8_0_crtc_load_lut(struct drm_crtc *crtc)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	u16 *r, *g, *b;
	int i;

	DRM_DEBUG_KMS("%d\n", amdgpu_crtc->crtc_id);

	WREG32(mmINPUT_CSC_CONTROL + amdgpu_crtc->crtc_offset,
	       ((INPUT_CSC_BYPASS << INPUT_CSC_CONTROL__INPUT_CSC_GRPH_MODE__SHIFT) |
		(INPUT_CSC_BYPASS << INPUT_CSC_CONTROL__INPUT_CSC_OVL_MODE__SHIFT)));
	WREG32(mmPRESCALE_GRPH_CONTROL + amdgpu_crtc->crtc_offset,
	       PRESCALE_GRPH_CONTROL__GRPH_PRESCALE_BYPASS_MASK);
	WREG32(mmPRESCALE_OVL_CONTROL + amdgpu_crtc->crtc_offset,
	       PRESCALE_OVL_CONTROL__OVL_PRESCALE_BYPASS_MASK);
	WREG32(mmINPUT_GAMMA_CONTROL + amdgpu_crtc->crtc_offset,
	       ((INPUT_GAMMA_USE_LUT << INPUT_GAMMA_CONTROL__GRPH_INPUT_GAMMA_MODE__SHIFT) |
		(INPUT_GAMMA_USE_LUT << INPUT_GAMMA_CONTROL__OVL_INPUT_GAMMA_MODE__SHIFT)));

	WREG32(mmDC_LUT_CONTROL + amdgpu_crtc->crtc_offset, 0);

	WREG32(mmDC_LUT_BLACK_OFFSET_BLUE + amdgpu_crtc->crtc_offset, 0);
	WREG32(mmDC_LUT_BLACK_OFFSET_GREEN + amdgpu_crtc->crtc_offset, 0);
	WREG32(mmDC_LUT_BLACK_OFFSET_RED + amdgpu_crtc->crtc_offset, 0);

	WREG32(mmDC_LUT_WHITE_OFFSET_BLUE + amdgpu_crtc->crtc_offset, 0xffff);
	WREG32(mmDC_LUT_WHITE_OFFSET_GREEN + amdgpu_crtc->crtc_offset, 0xffff);
	WREG32(mmDC_LUT_WHITE_OFFSET_RED + amdgpu_crtc->crtc_offset, 0xffff);

	WREG32(mmDC_LUT_RW_MODE + amdgpu_crtc->crtc_offset, 0);
	WREG32(mmDC_LUT_WRITE_EN_MASK + amdgpu_crtc->crtc_offset, 0x00000007);

	WREG32(mmDC_LUT_RW_INDEX + amdgpu_crtc->crtc_offset, 0);
	r = crtc->gamma_store;
	g = r + crtc->gamma_size;
	b = g + crtc->gamma_size;
	for (i = 0; i < 256; i++) {
		WREG32(mmDC_LUT_30_COLOR + amdgpu_crtc->crtc_offset,
		       ((*r++ & 0xffc0) << 14) |
		       ((*g++ & 0xffc0) << 4) |
		       (*b++ >> 6));
	}

	WREG32(mmDEGAMMA_CONTROL + amdgpu_crtc->crtc_offset,
	       ((DEGAMMA_BYPASS << DEGAMMA_CONTROL__GRPH_DEGAMMA_MODE__SHIFT) |
		(DEGAMMA_BYPASS << DEGAMMA_CONTROL__OVL_DEGAMMA_MODE__SHIFT) |
		(DEGAMMA_BYPASS << DEGAMMA_CONTROL__CURSOR_DEGAMMA_MODE__SHIFT)));
	WREG32(mmGAMUT_REMAP_CONTROL + amdgpu_crtc->crtc_offset,
	       ((GAMUT_REMAP_BYPASS << GAMUT_REMAP_CONTROL__GRPH_GAMUT_REMAP_MODE__SHIFT) |
		(GAMUT_REMAP_BYPASS << GAMUT_REMAP_CONTROL__OVL_GAMUT_REMAP_MODE__SHIFT)));
	WREG32(mmREGAMMA_CONTROL + amdgpu_crtc->crtc_offset,
	       ((REGAMMA_BYPASS << REGAMMA_CONTROL__GRPH_REGAMMA_MODE__SHIFT) |
		(REGAMMA_BYPASS << REGAMMA_CONTROL__OVL_REGAMMA_MODE__SHIFT)));
	WREG32(mmOUTPUT_CSC_CONTROL + amdgpu_crtc->crtc_offset,
	       ((OUTPUT_CSC_BYPASS << OUTPUT_CSC_CONTROL__OUTPUT_CSC_GRPH_MODE__SHIFT) |
		(OUTPUT_CSC_BYPASS << OUTPUT_CSC_CONTROL__OUTPUT_CSC_OVL_MODE__SHIFT)));
	/* XXX match this to the depth of the crtc fmt block, move to modeset? */
	WREG32(0x1a50 + amdgpu_crtc->crtc_offset, 0);
	/* XXX this only needs to be programmed once per crtc at startup,
	 * not sure where the best place for it is
	 */
	WREG32(mmALPHA_CONTROL + amdgpu_crtc->crtc_offset,
	       ALPHA_CONTROL__CURSOR_ALPHA_BLND_ENA_MASK);
}

static int dce_v8_0_pick_dig_encoder(struct drm_encoder *encoder)
{
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct amdgpu_encoder_atom_dig *dig = amdgpu_encoder->enc_priv;

	switch (amdgpu_encoder->encoder_id) {
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
		if (dig->linkb)
			return 1;
		else
			return 0;
		break;
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
		if (dig->linkb)
			return 3;
		else
			return 2;
		break;
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
		if (dig->linkb)
			return 5;
		else
			return 4;
		break;
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY3:
		return 6;
		break;
	default:
		DRM_ERROR("invalid encoder_id: 0x%x\n", amdgpu_encoder->encoder_id);
		return 0;
	}
}

/**
 * dce_v8_0_pick_pll - Allocate a PPLL for use by the crtc.
 *
 * @crtc: drm crtc
 *
 * Returns the PPLL (Pixel PLL) to be used by the crtc.  For DP monitors
 * a single PPLL can be used for all DP crtcs/encoders.  For non-DP
 * monitors a dedicated PPLL must be used.  If a particular board has
 * an external DP PLL, return ATOM_PPLL_INVALID to skip PLL programming
 * as there is no need to program the PLL itself.  If we are not able to
 * allocate a PLL, return ATOM_PPLL_INVALID to skip PLL programming to
 * avoid messing up an existing monitor.
 *
 * Asic specific PLL information
 *
 * DCE 8.x
 * KB/KV
 * - PPLL1, PPLL2 are available for all UNIPHY (both DP and non-DP)
 * CI
 * - PPLL0, PPLL1, PPLL2 are available for all UNIPHY (both DP and non-DP) and DAC
 *
 */
static u32 dce_v8_0_pick_pll(struct drm_crtc *crtc)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	u32 pll_in_use;
	int pll;

	if (ENCODER_MODE_IS_DP(amdgpu_atombios_encoder_get_encoder_mode(amdgpu_crtc->encoder))) {
		if (adev->clock.dp_extclk)
			/* skip PPLL programming if using ext clock */
			return ATOM_PPLL_INVALID;
		else {
			/* use the same PPLL for all DP monitors */
			pll = amdgpu_pll_get_shared_dp_ppll(crtc);
			if (pll != ATOM_PPLL_INVALID)
				return pll;
		}
	} else {
		/* use the same PPLL for all monitors with the same clock */
		pll = amdgpu_pll_get_shared_nondp_ppll(crtc);
		if (pll != ATOM_PPLL_INVALID)
			return pll;
	}
	/* otherwise, pick one of the plls */
	if ((adev->asic_type == CHIP_KABINI) ||
	    (adev->asic_type == CHIP_MULLINS)) {
		/* KB/ML has PPLL1 and PPLL2 */
		pll_in_use = amdgpu_pll_get_use_mask(crtc);
		if (!(pll_in_use & (1 << ATOM_PPLL2)))
			return ATOM_PPLL2;
		if (!(pll_in_use & (1 << ATOM_PPLL1)))
			return ATOM_PPLL1;
		DRM_ERROR("unable to allocate a PPLL\n");
		return ATOM_PPLL_INVALID;
	} else {
		/* CI/KV has PPLL0, PPLL1, and PPLL2 */
		pll_in_use = amdgpu_pll_get_use_mask(crtc);
		if (!(pll_in_use & (1 << ATOM_PPLL2)))
			return ATOM_PPLL2;
		if (!(pll_in_use & (1 << ATOM_PPLL1)))
			return ATOM_PPLL1;
		if (!(pll_in_use & (1 << ATOM_PPLL0)))
			return ATOM_PPLL0;
		DRM_ERROR("unable to allocate a PPLL\n");
		return ATOM_PPLL_INVALID;
	}
	return ATOM_PPLL_INVALID;
}

static void dce_v8_0_lock_cursor(struct drm_crtc *crtc, bool lock)
{
	struct amdgpu_device *adev = drm_to_adev(crtc->dev);
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	uint32_t cur_lock;

	cur_lock = RREG32(mmCUR_UPDATE + amdgpu_crtc->crtc_offset);
	if (lock)
		cur_lock |= CUR_UPDATE__CURSOR_UPDATE_LOCK_MASK;
	else
		cur_lock &= ~CUR_UPDATE__CURSOR_UPDATE_LOCK_MASK;
	WREG32(mmCUR_UPDATE + amdgpu_crtc->crtc_offset, cur_lock);
}

static void dce_v8_0_hide_cursor(struct drm_crtc *crtc)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct amdgpu_device *adev = drm_to_adev(crtc->dev);

	WREG32(mmCUR_CONTROL + amdgpu_crtc->crtc_offset,
	       (CURSOR_24_8_PRE_MULT << CUR_CONTROL__CURSOR_MODE__SHIFT) |
	       (CURSOR_URGENT_1_2 << CUR_CONTROL__CURSOR_URGENT_CONTROL__SHIFT));
}

static void dce_v8_0_show_cursor(struct drm_crtc *crtc)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct amdgpu_device *adev = drm_to_adev(crtc->dev);

	WREG32(mmCUR_SURFACE_ADDRESS_HIGH + amdgpu_crtc->crtc_offset,
	       upper_32_bits(amdgpu_crtc->cursor_addr));
	WREG32(mmCUR_SURFACE_ADDRESS + amdgpu_crtc->crtc_offset,
	       lower_32_bits(amdgpu_crtc->cursor_addr));

	WREG32(mmCUR_CONTROL + amdgpu_crtc->crtc_offset,
	       CUR_CONTROL__CURSOR_EN_MASK |
	       (CURSOR_24_8_PRE_MULT << CUR_CONTROL__CURSOR_MODE__SHIFT) |
	       (CURSOR_URGENT_1_2 << CUR_CONTROL__CURSOR_URGENT_CONTROL__SHIFT));
}

static int dce_v8_0_cursor_move_locked(struct drm_crtc *crtc,
				       int x, int y)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct amdgpu_device *adev = drm_to_adev(crtc->dev);
	int xorigin = 0, yorigin = 0;

	amdgpu_crtc->cursor_x = x;
	amdgpu_crtc->cursor_y = y;

	/* avivo cursor are offset into the total surface */
	x += crtc->x;
	y += crtc->y;
	DRM_DEBUG("x %d y %d c->x %d c->y %d\n", x, y, crtc->x, crtc->y);

	if (x < 0) {
		xorigin = min(-x, amdgpu_crtc->max_cursor_width - 1);
		x = 0;
	}
	if (y < 0) {
		yorigin = min(-y, amdgpu_crtc->max_cursor_height - 1);
		y = 0;
	}

	WREG32(mmCUR_POSITION + amdgpu_crtc->crtc_offset, (x << 16) | y);
	WREG32(mmCUR_HOT_SPOT + amdgpu_crtc->crtc_offset, (xorigin << 16) | yorigin);
	WREG32(mmCUR_SIZE + amdgpu_crtc->crtc_offset,
	       ((amdgpu_crtc->cursor_width - 1) << 16) | (amdgpu_crtc->cursor_height - 1));

	return 0;
}

static int dce_v8_0_crtc_cursor_move(struct drm_crtc *crtc,
				     int x, int y)
{
	int ret;

	dce_v8_0_lock_cursor(crtc, true);
	ret = dce_v8_0_cursor_move_locked(crtc, x, y);
	dce_v8_0_lock_cursor(crtc, false);

	return ret;
}

static int dce_v8_0_crtc_cursor_set2(struct drm_crtc *crtc,
				     struct drm_file *file_priv,
				     uint32_t handle,
				     uint32_t width,
				     uint32_t height,
				     int32_t hot_x,
				     int32_t hot_y)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct drm_gem_object *obj;
	struct amdgpu_bo *aobj;
	int ret;

	if (!handle) {
		/* turn off cursor */
		dce_v8_0_hide_cursor(crtc);
		obj = NULL;
		goto unpin;
	}

	if ((width > amdgpu_crtc->max_cursor_width) ||
	    (height > amdgpu_crtc->max_cursor_height)) {
		DRM_ERROR("bad cursor width or height %d x %d\n", width, height);
		return -EINVAL;
	}

	obj = drm_gem_object_lookup(file_priv, handle);
	if (!obj) {
		DRM_ERROR("Cannot find cursor object %x for crtc %d\n", handle, amdgpu_crtc->crtc_id);
		return -ENOENT;
	}

	aobj = gem_to_amdgpu_bo(obj);
	ret = amdgpu_bo_reserve(aobj, false);
	if (ret != 0) {
		drm_gem_object_put(obj);
		return ret;
	}

	ret = amdgpu_bo_pin(aobj, AMDGPU_GEM_DOMAIN_VRAM);
	amdgpu_bo_unreserve(aobj);
	if (ret) {
		DRM_ERROR("Failed to pin new cursor BO (%d)\n", ret);
		drm_gem_object_put(obj);
		return ret;
	}
	amdgpu_crtc->cursor_addr = amdgpu_bo_gpu_offset(aobj);

	dce_v8_0_lock_cursor(crtc, true);

	if (width != amdgpu_crtc->cursor_width ||
	    height != amdgpu_crtc->cursor_height ||
	    hot_x != amdgpu_crtc->cursor_hot_x ||
	    hot_y != amdgpu_crtc->cursor_hot_y) {
		int x, y;

		x = amdgpu_crtc->cursor_x + amdgpu_crtc->cursor_hot_x - hot_x;
		y = amdgpu_crtc->cursor_y + amdgpu_crtc->cursor_hot_y - hot_y;

		dce_v8_0_cursor_move_locked(crtc, x, y);

		amdgpu_crtc->cursor_width = width;
		amdgpu_crtc->cursor_height = height;
		amdgpu_crtc->cursor_hot_x = hot_x;
		amdgpu_crtc->cursor_hot_y = hot_y;
	}

	dce_v8_0_show_cursor(crtc);
	dce_v8_0_lock_cursor(crtc, false);

unpin:
	if (amdgpu_crtc->cursor_bo) {
		struct amdgpu_bo *aobj = gem_to_amdgpu_bo(amdgpu_crtc->cursor_bo);
		ret = amdgpu_bo_reserve(aobj, true);
		if (likely(ret == 0)) {
			amdgpu_bo_unpin(aobj);
			amdgpu_bo_unreserve(aobj);
		}
		drm_gem_object_put(amdgpu_crtc->cursor_bo);
	}

	amdgpu_crtc->cursor_bo = obj;
	return 0;
}

static void dce_v8_0_cursor_reset(struct drm_crtc *crtc)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);

	if (amdgpu_crtc->cursor_bo) {
		dce_v8_0_lock_cursor(crtc, true);

		dce_v8_0_cursor_move_locked(crtc, amdgpu_crtc->cursor_x,
					    amdgpu_crtc->cursor_y);

		dce_v8_0_show_cursor(crtc);

		dce_v8_0_lock_cursor(crtc, false);
	}
}

static int dce_v8_0_crtc_gamma_set(struct drm_crtc *crtc, u16 *red, u16 *green,
				   u16 *blue, uint32_t size,
				   struct drm_modeset_acquire_ctx *ctx)
{
	dce_v8_0_crtc_load_lut(crtc);

	return 0;
}

static void dce_v8_0_crtc_destroy(struct drm_crtc *crtc)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);

	drm_crtc_cleanup(crtc);
	kfree(amdgpu_crtc);
}

static const struct drm_crtc_funcs dce_v8_0_crtc_funcs = {
	.cursor_set2 = dce_v8_0_crtc_cursor_set2,
	.cursor_move = dce_v8_0_crtc_cursor_move,
	.gamma_set = dce_v8_0_crtc_gamma_set,
	.set_config = amdgpu_display_crtc_set_config,
	.destroy = dce_v8_0_crtc_destroy,
	.page_flip_target = amdgpu_display_crtc_page_flip_target,
	.get_vblank_counter = amdgpu_get_vblank_counter_kms,
	.enable_vblank = amdgpu_enable_vblank_kms,
	.disable_vblank = amdgpu_disable_vblank_kms,
	.get_vblank_timestamp = drm_crtc_vblank_helper_get_vblank_timestamp,
};

static void dce_v8_0_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct drm_device *dev = crtc->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	unsigned type;

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		amdgpu_crtc->enabled = true;
		amdgpu_atombios_crtc_enable(crtc, ATOM_ENABLE);
		dce_v8_0_vga_enable(crtc, true);
		amdgpu_atombios_crtc_blank(crtc, ATOM_DISABLE);
		dce_v8_0_vga_enable(crtc, false);
		/* Make sure VBLANK and PFLIP interrupts are still enabled */
		type = amdgpu_display_crtc_idx_to_irq_type(adev,
						amdgpu_crtc->crtc_id);
		amdgpu_irq_update(adev, &adev->crtc_irq, type);
		amdgpu_irq_update(adev, &adev->pageflip_irq, type);
		drm_crtc_vblank_on(crtc);
		dce_v8_0_crtc_load_lut(crtc);
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		drm_crtc_vblank_off(crtc);
		if (amdgpu_crtc->enabled) {
			dce_v8_0_vga_enable(crtc, true);
			amdgpu_atombios_crtc_blank(crtc, ATOM_ENABLE);
			dce_v8_0_vga_enable(crtc, false);
		}
		amdgpu_atombios_crtc_enable(crtc, ATOM_DISABLE);
		amdgpu_crtc->enabled = false;
		break;
	}
	/* adjust pm to dpms */
	amdgpu_pm_compute_clocks(adev);
}

static void dce_v8_0_crtc_prepare(struct drm_crtc *crtc)
{
	/* disable crtc pair power gating before programming */
	amdgpu_atombios_crtc_powergate(crtc, ATOM_DISABLE);
	amdgpu_atombios_crtc_lock(crtc, ATOM_ENABLE);
	dce_v8_0_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
}

static void dce_v8_0_crtc_commit(struct drm_crtc *crtc)
{
	dce_v8_0_crtc_dpms(crtc, DRM_MODE_DPMS_ON);
	amdgpu_atombios_crtc_lock(crtc, ATOM_DISABLE);
}

static void dce_v8_0_crtc_disable(struct drm_crtc *crtc)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_atom_ss ss;
	int i;

	dce_v8_0_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
	if (crtc->primary->fb) {
		int r;
		struct amdgpu_bo *abo;

		abo = gem_to_amdgpu_bo(crtc->primary->fb->obj[0]);
		r = amdgpu_bo_reserve(abo, true);
		if (unlikely(r))
			DRM_ERROR("failed to reserve abo before unpin\n");
		else {
			amdgpu_bo_unpin(abo);
			amdgpu_bo_unreserve(abo);
		}
	}
	/* disable the GRPH */
	dce_v8_0_grph_enable(crtc, false);

	amdgpu_atombios_crtc_powergate(crtc, ATOM_ENABLE);

	for (i = 0; i < adev->mode_info.num_crtc; i++) {
		if (adev->mode_info.crtcs[i] &&
		    adev->mode_info.crtcs[i]->enabled &&
		    i != amdgpu_crtc->crtc_id &&
		    amdgpu_crtc->pll_id == adev->mode_info.crtcs[i]->pll_id) {
			/* one other crtc is using this pll don't turn
			 * off the pll
			 */
			goto done;
		}
	}

	switch (amdgpu_crtc->pll_id) {
	case ATOM_PPLL1:
	case ATOM_PPLL2:
		/* disable the ppll */
		amdgpu_atombios_crtc_program_pll(crtc, amdgpu_crtc->crtc_id, amdgpu_crtc->pll_id,
						 0, 0, ATOM_DISABLE, 0, 0, 0, 0, 0, false, &ss);
		break;
	case ATOM_PPLL0:
		/* disable the ppll */
		if ((adev->asic_type == CHIP_KAVERI) ||
		    (adev->asic_type == CHIP_BONAIRE) ||
		    (adev->asic_type == CHIP_HAWAII))
			amdgpu_atombios_crtc_program_pll(crtc, amdgpu_crtc->crtc_id, amdgpu_crtc->pll_id,
						  0, 0, ATOM_DISABLE, 0, 0, 0, 0, 0, false, &ss);
		break;
	default:
		break;
	}
done:
	amdgpu_crtc->pll_id = ATOM_PPLL_INVALID;
	amdgpu_crtc->adjusted_clock = 0;
	amdgpu_crtc->encoder = NULL;
	amdgpu_crtc->connector = NULL;
}

static int dce_v8_0_crtc_mode_set(struct drm_crtc *crtc,
				  struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode,
				  int x, int y, struct drm_framebuffer *old_fb)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);

	if (!amdgpu_crtc->adjusted_clock)
		return -EINVAL;

	amdgpu_atombios_crtc_set_pll(crtc, adjusted_mode);
	amdgpu_atombios_crtc_set_dtd_timing(crtc, adjusted_mode);
	dce_v8_0_crtc_do_set_base(crtc, old_fb, x, y, 0);
	amdgpu_atombios_crtc_overscan_setup(crtc, mode, adjusted_mode);
	amdgpu_atombios_crtc_scaler_setup(crtc);
	dce_v8_0_cursor_reset(crtc);
	/* update the hw version fpr dpm */
	amdgpu_crtc->hw_mode = *adjusted_mode;

	return 0;
}

static bool dce_v8_0_crtc_mode_fixup(struct drm_crtc *crtc,
				     const struct drm_display_mode *mode,
				     struct drm_display_mode *adjusted_mode)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct drm_encoder *encoder;

	/* assign the encoder to the amdgpu crtc to avoid repeated lookups later */
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		if (encoder->crtc == crtc) {
			amdgpu_crtc->encoder = encoder;
			amdgpu_crtc->connector = amdgpu_get_connector_for_encoder(encoder);
			break;
		}
	}
	if ((amdgpu_crtc->encoder == NULL) || (amdgpu_crtc->connector == NULL)) {
		amdgpu_crtc->encoder = NULL;
		amdgpu_crtc->connector = NULL;
		return false;
	}
	if (!amdgpu_display_crtc_scaling_mode_fixup(crtc, mode, adjusted_mode))
		return false;
	if (amdgpu_atombios_crtc_prepare_pll(crtc, adjusted_mode))
		return false;
	/* pick pll */
	amdgpu_crtc->pll_id = dce_v8_0_pick_pll(crtc);
	/* if we can't get a PPLL for a non-DP encoder, fail */
	if ((amdgpu_crtc->pll_id == ATOM_PPLL_INVALID) &&
	    !ENCODER_MODE_IS_DP(amdgpu_atombios_encoder_get_encoder_mode(amdgpu_crtc->encoder)))
		return false;

	return true;
}

static int dce_v8_0_crtc_set_base(struct drm_crtc *crtc, int x, int y,
				  struct drm_framebuffer *old_fb)
{
	return dce_v8_0_crtc_do_set_base(crtc, old_fb, x, y, 0);
}

static int dce_v8_0_crtc_set_base_atomic(struct drm_crtc *crtc,
					 struct drm_framebuffer *fb,
					 int x, int y, enum mode_set_atomic state)
{
	return dce_v8_0_crtc_do_set_base(crtc, fb, x, y, 1);
}

static const struct drm_crtc_helper_funcs dce_v8_0_crtc_helper_funcs = {
	.dpms = dce_v8_0_crtc_dpms,
	.mode_fixup = dce_v8_0_crtc_mode_fixup,
	.mode_set = dce_v8_0_crtc_mode_set,
	.mode_set_base = dce_v8_0_crtc_set_base,
	.mode_set_base_atomic = dce_v8_0_crtc_set_base_atomic,
	.prepare = dce_v8_0_crtc_prepare,
	.commit = dce_v8_0_crtc_commit,
	.disable = dce_v8_0_crtc_disable,
	.get_scanout_position = amdgpu_crtc_get_scanout_position,
};

static int dce_v8_0_crtc_init(struct amdgpu_device *adev, int index)
{
	struct amdgpu_crtc *amdgpu_crtc;

	amdgpu_crtc = kzalloc(sizeof(struct amdgpu_crtc) +
			      (AMDGPUFB_CONN_LIMIT * sizeof(struct drm_connector *)), GFP_KERNEL);
	if (amdgpu_crtc == NULL)
		return -ENOMEM;

	drm_crtc_init(adev_to_drm(adev), &amdgpu_crtc->base, &dce_v8_0_crtc_funcs);

	drm_mode_crtc_set_gamma_size(&amdgpu_crtc->base, 256);
	amdgpu_crtc->crtc_id = index;
	adev->mode_info.crtcs[index] = amdgpu_crtc;

	amdgpu_crtc->max_cursor_width = CIK_CURSOR_WIDTH;
	amdgpu_crtc->max_cursor_height = CIK_CURSOR_HEIGHT;
	adev_to_drm(adev)->mode_config.cursor_width = amdgpu_crtc->max_cursor_width;
	adev_to_drm(adev)->mode_config.cursor_height = amdgpu_crtc->max_cursor_height;

	amdgpu_crtc->crtc_offset = crtc_offsets[amdgpu_crtc->crtc_id];

	amdgpu_crtc->pll_id = ATOM_PPLL_INVALID;
	amdgpu_crtc->adjusted_clock = 0;
	amdgpu_crtc->encoder = NULL;
	amdgpu_crtc->connector = NULL;
	drm_crtc_helper_add(&amdgpu_crtc->base, &dce_v8_0_crtc_helper_funcs);

	return 0;
}

static int dce_v8_0_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->audio_endpt_rreg = &dce_v8_0_audio_endpt_rreg;
	adev->audio_endpt_wreg = &dce_v8_0_audio_endpt_wreg;

	dce_v8_0_set_display_funcs(adev);

	adev->mode_info.num_crtc = dce_v8_0_get_num_crtc(adev);

	switch (adev->asic_type) {
	case CHIP_BONAIRE:
	case CHIP_HAWAII:
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 6;
		break;
	case CHIP_KAVERI:
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 7;
		break;
	case CHIP_KABINI:
	case CHIP_MULLINS:
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 6; /* ? */
		break;
	default:
		/* FIXME: not supported yet */
		return -EINVAL;
	}

	dce_v8_0_set_irq_funcs(adev);

	return 0;
}

static int dce_v8_0_sw_init(void *handle)
{
	int r, i;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	for (i = 0; i < adev->mode_info.num_crtc; i++) {
		r = amdgpu_irq_add_id(adev, AMDGPU_IRQ_CLIENTID_LEGACY, i + 1, &adev->crtc_irq);
		if (r)
			return r;
	}

	for (i = 8; i < 20; i += 2) {
		r = amdgpu_irq_add_id(adev, AMDGPU_IRQ_CLIENTID_LEGACY, i, &adev->pageflip_irq);
		if (r)
			return r;
	}

	/* HPD hotplug */
	r = amdgpu_irq_add_id(adev, AMDGPU_IRQ_CLIENTID_LEGACY, 42, &adev->hpd_irq);
	if (r)
		return r;

	adev_to_drm(adev)->mode_config.funcs = &amdgpu_mode_funcs;

	adev_to_drm(adev)->mode_config.async_page_flip = true;

	adev_to_drm(adev)->mode_config.max_width = 16384;
	adev_to_drm(adev)->mode_config.max_height = 16384;

	adev_to_drm(adev)->mode_config.preferred_depth = 24;
	adev_to_drm(adev)->mode_config.prefer_shadow = 1;

	adev_to_drm(adev)->mode_config.fb_base = adev->gmc.aper_base;

	r = amdgpu_display_modeset_create_props(adev);
	if (r)
		return r;

	adev_to_drm(adev)->mode_config.max_width = 16384;
	adev_to_drm(adev)->mode_config.max_height = 16384;

	/* allocate crtcs */
	for (i = 0; i < adev->mode_info.num_crtc; i++) {
		r = dce_v8_0_crtc_init(adev, i);
		if (r)
			return r;
	}

	if (amdgpu_atombios_get_connector_info_from_object_table(adev))
		amdgpu_display_print_display_setup(adev_to_drm(adev));
	else
		return -EINVAL;

	/* setup afmt */
	r = dce_v8_0_afmt_init(adev);
	if (r)
		return r;

	r = dce_v8_0_audio_init(adev);
	if (r)
		return r;

	drm_kms_helper_poll_init(adev_to_drm(adev));

	adev->mode_info.mode_config_initialized = true;
	return 0;
}

static int dce_v8_0_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	kfree(adev->mode_info.bios_hardcoded_edid);

	drm_kms_helper_poll_fini(adev_to_drm(adev));

	dce_v8_0_audio_fini(adev);

	dce_v8_0_afmt_fini(adev);

	drm_mode_config_cleanup(adev_to_drm(adev));
	adev->mode_info.mode_config_initialized = false;

	return 0;
}

static int dce_v8_0_hw_init(void *handle)
{
	int i;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* disable vga render */
	dce_v8_0_set_vga_render_state(adev, false);
	/* init dig PHYs, disp eng pll */
	amdgpu_atombios_encoder_init_dig(adev);
	amdgpu_atombios_crtc_set_disp_eng_pll(adev, adev->clock.default_dispclk);

	/* initialize hpd */
	dce_v8_0_hpd_init(adev);

	for (i = 0; i < adev->mode_info.audio.num_pins; i++) {
		dce_v8_0_audio_enable(adev, &adev->mode_info.audio.pin[i], false);
	}

	dce_v8_0_pageflip_interrupt_init(adev);

	return 0;
}

static int dce_v8_0_hw_fini(void *handle)
{
	int i;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	dce_v8_0_hpd_fini(adev);

	for (i = 0; i < adev->mode_info.audio.num_pins; i++) {
		dce_v8_0_audio_enable(adev, &adev->mode_info.audio.pin[i], false);
	}

	dce_v8_0_pageflip_interrupt_fini(adev);

	return 0;
}

static int dce_v8_0_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int r;

	r = amdgpu_display_suspend_helper(adev);
	if (r)
		return r;

	adev->mode_info.bl_level =
		amdgpu_atombios_encoder_get_backlight_level_from_reg(adev);

	return dce_v8_0_hw_fini(handle);
}

static int dce_v8_0_resume(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int ret;

	amdgpu_atombios_encoder_set_backlight_level_to_reg(adev,
							   adev->mode_info.bl_level);

	ret = dce_v8_0_hw_init(handle);

	/* turn on the BL */
	if (adev->mode_info.bl_encoder) {
		u8 bl_level = amdgpu_display_backlight_get_level(adev,
								  adev->mode_info.bl_encoder);
		amdgpu_display_backlight_set_level(adev, adev->mode_info.bl_encoder,
						    bl_level);
	}
	if (ret)
		return ret;

	return amdgpu_display_resume_helper(adev);
}

static bool dce_v8_0_is_idle(void *handle)
{
	return true;
}

static int dce_v8_0_wait_for_idle(void *handle)
{
	return 0;
}

static int dce_v8_0_soft_reset(void *handle)
{
	u32 srbm_soft_reset = 0, tmp;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (dce_v8_0_is_display_hung(adev))
		srbm_soft_reset |= SRBM_SOFT_RESET__SOFT_RESET_DC_MASK;

	if (srbm_soft_reset) {
		tmp = RREG32(mmSRBM_SOFT_RESET);
		tmp |= srbm_soft_reset;
		dev_info(adev->dev, "SRBM_SOFT_RESET=0x%08X\n", tmp);
		WREG32(mmSRBM_SOFT_RESET, tmp);
		tmp = RREG32(mmSRBM_SOFT_RESET);

		udelay(50);

		tmp &= ~srbm_soft_reset;
		WREG32(mmSRBM_SOFT_RESET, tmp);
		tmp = RREG32(mmSRBM_SOFT_RESET);

		/* Wait a little for things to settle down */
		udelay(50);
	}
	return 0;
}

static void dce_v8_0_set_crtc_vblank_interrupt_state(struct amdgpu_device *adev,
						     int crtc,
						     enum amdgpu_interrupt_state state)
{
	u32 reg_block, lb_interrupt_mask;

	if (crtc >= adev->mode_info.num_crtc) {
		DRM_DEBUG("invalid crtc %d\n", crtc);
		return;
	}

	switch (crtc) {
	case 0:
		reg_block = CRTC0_REGISTER_OFFSET;
		break;
	case 1:
		reg_block = CRTC1_REGISTER_OFFSET;
		break;
	case 2:
		reg_block = CRTC2_REGISTER_OFFSET;
		break;
	case 3:
		reg_block = CRTC3_REGISTER_OFFSET;
		break;
	case 4:
		reg_block = CRTC4_REGISTER_OFFSET;
		break;
	case 5:
		reg_block = CRTC5_REGISTER_OFFSET;
		break;
	default:
		DRM_DEBUG("invalid crtc %d\n", crtc);
		return;
	}

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		lb_interrupt_mask = RREG32(mmLB_INTERRUPT_MASK + reg_block);
		lb_interrupt_mask &= ~LB_INTERRUPT_MASK__VBLANK_INTERRUPT_MASK_MASK;
		WREG32(mmLB_INTERRUPT_MASK + reg_block, lb_interrupt_mask);
		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		lb_interrupt_mask = RREG32(mmLB_INTERRUPT_MASK + reg_block);
		lb_interrupt_mask |= LB_INTERRUPT_MASK__VBLANK_INTERRUPT_MASK_MASK;
		WREG32(mmLB_INTERRUPT_MASK + reg_block, lb_interrupt_mask);
		break;
	default:
		break;
	}
}

static void dce_v8_0_set_crtc_vline_interrupt_state(struct amdgpu_device *adev,
						    int crtc,
						    enum amdgpu_interrupt_state state)
{
	u32 reg_block, lb_interrupt_mask;

	if (crtc >= adev->mode_info.num_crtc) {
		DRM_DEBUG("invalid crtc %d\n", crtc);
		return;
	}

	switch (crtc) {
	case 0:
		reg_block = CRTC0_REGISTER_OFFSET;
		break;
	case 1:
		reg_block = CRTC1_REGISTER_OFFSET;
		break;
	case 2:
		reg_block = CRTC2_REGISTER_OFFSET;
		break;
	case 3:
		reg_block = CRTC3_REGISTER_OFFSET;
		break;
	case 4:
		reg_block = CRTC4_REGISTER_OFFSET;
		break;
	case 5:
		reg_block = CRTC5_REGISTER_OFFSET;
		break;
	default:
		DRM_DEBUG("invalid crtc %d\n", crtc);
		return;
	}

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		lb_interrupt_mask = RREG32(mmLB_INTERRUPT_MASK + reg_block);
		lb_interrupt_mask &= ~LB_INTERRUPT_MASK__VLINE_INTERRUPT_MASK_MASK;
		WREG32(mmLB_INTERRUPT_MASK + reg_block, lb_interrupt_mask);
		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		lb_interrupt_mask = RREG32(mmLB_INTERRUPT_MASK + reg_block);
		lb_interrupt_mask |= LB_INTERRUPT_MASK__VLINE_INTERRUPT_MASK_MASK;
		WREG32(mmLB_INTERRUPT_MASK + reg_block, lb_interrupt_mask);
		break;
	default:
		break;
	}
}

static int dce_v8_0_set_hpd_interrupt_state(struct amdgpu_device *adev,
					    struct amdgpu_irq_src *src,
					    unsigned type,
					    enum amdgpu_interrupt_state state)
{
	u32 dc_hpd_int_cntl;

	if (type >= adev->mode_info.num_hpd) {
		DRM_DEBUG("invalid hdp %d\n", type);
		return 0;
	}

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		dc_hpd_int_cntl = RREG32(mmDC_HPD1_INT_CONTROL + hpd_offsets[type]);
		dc_hpd_int_cntl &= ~DC_HPD1_INT_CONTROL__DC_HPD1_INT_EN_MASK;
		WREG32(mmDC_HPD1_INT_CONTROL + hpd_offsets[type], dc_hpd_int_cntl);
		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		dc_hpd_int_cntl = RREG32(mmDC_HPD1_INT_CONTROL + hpd_offsets[type]);
		dc_hpd_int_cntl |= DC_HPD1_INT_CONTROL__DC_HPD1_INT_EN_MASK;
		WREG32(mmDC_HPD1_INT_CONTROL + hpd_offsets[type], dc_hpd_int_cntl);
		break;
	default:
		break;
	}

	return 0;
}

static int dce_v8_0_set_crtc_interrupt_state(struct amdgpu_device *adev,
					     struct amdgpu_irq_src *src,
					     unsigned type,
					     enum amdgpu_interrupt_state state)
{
	switch (type) {
	case AMDGPU_CRTC_IRQ_VBLANK1:
		dce_v8_0_set_crtc_vblank_interrupt_state(adev, 0, state);
		break;
	case AMDGPU_CRTC_IRQ_VBLANK2:
		dce_v8_0_set_crtc_vblank_interrupt_state(adev, 1, state);
		break;
	case AMDGPU_CRTC_IRQ_VBLANK3:
		dce_v8_0_set_crtc_vblank_interrupt_state(adev, 2, state);
		break;
	case AMDGPU_CRTC_IRQ_VBLANK4:
		dce_v8_0_set_crtc_vblank_interrupt_state(adev, 3, state);
		break;
	case AMDGPU_CRTC_IRQ_VBLANK5:
		dce_v8_0_set_crtc_vblank_interrupt_state(adev, 4, state);
		break;
	case AMDGPU_CRTC_IRQ_VBLANK6:
		dce_v8_0_set_crtc_vblank_interrupt_state(adev, 5, state);
		break;
	case AMDGPU_CRTC_IRQ_VLINE1:
		dce_v8_0_set_crtc_vline_interrupt_state(adev, 0, state);
		break;
	case AMDGPU_CRTC_IRQ_VLINE2:
		dce_v8_0_set_crtc_vline_interrupt_state(adev, 1, state);
		break;
	case AMDGPU_CRTC_IRQ_VLINE3:
		dce_v8_0_set_crtc_vline_interrupt_state(adev, 2, state);
		break;
	case AMDGPU_CRTC_IRQ_VLINE4:
		dce_v8_0_set_crtc_vline_interrupt_state(adev, 3, state);
		break;
	case AMDGPU_CRTC_IRQ_VLINE5:
		dce_v8_0_set_crtc_vline_interrupt_state(adev, 4, state);
		break;
	case AMDGPU_CRTC_IRQ_VLINE6:
		dce_v8_0_set_crtc_vline_interrupt_state(adev, 5, state);
		break;
	default:
		break;
	}
	return 0;
}

static int dce_v8_0_crtc_irq(struct amdgpu_device *adev,
			     struct amdgpu_irq_src *source,
			     struct amdgpu_iv_entry *entry)
{
	unsigned crtc = entry->src_id - 1;
	uint32_t disp_int = RREG32(interrupt_status_offsets[crtc].reg);
	unsigned int irq_type = amdgpu_display_crtc_idx_to_irq_type(adev,
								    crtc);

	switch (entry->src_data[0]) {
	case 0: /* vblank */
		if (disp_int & interrupt_status_offsets[crtc].vblank)
			WREG32(mmLB_VBLANK_STATUS + crtc_offsets[crtc], LB_VBLANK_STATUS__VBLANK_ACK_MASK);
		else
			DRM_DEBUG("IH: IH event w/o asserted irq bit?\n");

		if (amdgpu_irq_enabled(adev, source, irq_type)) {
			drm_handle_vblank(adev_to_drm(adev), crtc);
		}
		DRM_DEBUG("IH: D%d vblank\n", crtc + 1);
		break;
	case 1: /* vline */
		if (disp_int & interrupt_status_offsets[crtc].vline)
			WREG32(mmLB_VLINE_STATUS + crtc_offsets[crtc], LB_VLINE_STATUS__VLINE_ACK_MASK);
		else
			DRM_DEBUG("IH: IH event w/o asserted irq bit?\n");

		DRM_DEBUG("IH: D%d vline\n", crtc + 1);
		break;
	default:
		DRM_DEBUG("Unhandled interrupt: %d %d\n", entry->src_id, entry->src_data[0]);
		break;
	}

	return 0;
}

static int dce_v8_0_set_pageflip_interrupt_state(struct amdgpu_device *adev,
						 struct amdgpu_irq_src *src,
						 unsigned type,
						 enum amdgpu_interrupt_state state)
{
	u32 reg;

	if (type >= adev->mode_info.num_crtc) {
		DRM_ERROR("invalid pageflip crtc %d\n", type);
		return -EINVAL;
	}

	reg = RREG32(mmGRPH_INTERRUPT_CONTROL + crtc_offsets[type]);
	if (state == AMDGPU_IRQ_STATE_DISABLE)
		WREG32(mmGRPH_INTERRUPT_CONTROL + crtc_offsets[type],
		       reg & ~GRPH_INTERRUPT_CONTROL__GRPH_PFLIP_INT_MASK_MASK);
	else
		WREG32(mmGRPH_INTERRUPT_CONTROL + crtc_offsets[type],
		       reg | GRPH_INTERRUPT_CONTROL__GRPH_PFLIP_INT_MASK_MASK);

	return 0;
}

static int dce_v8_0_pageflip_irq(struct amdgpu_device *adev,
				struct amdgpu_irq_src *source,
				struct amdgpu_iv_entry *entry)
{
	unsigned long flags;
	unsigned crtc_id;
	struct amdgpu_crtc *amdgpu_crtc;
	struct amdgpu_flip_work *works;

	crtc_id = (entry->src_id - 8) >> 1;
	amdgpu_crtc = adev->mode_info.crtcs[crtc_id];

	if (crtc_id >= adev->mode_info.num_crtc) {
		DRM_ERROR("invalid pageflip crtc %d\n", crtc_id);
		return -EINVAL;
	}

	if (RREG32(mmGRPH_INTERRUPT_STATUS + crtc_offsets[crtc_id]) &
	    GRPH_INTERRUPT_STATUS__GRPH_PFLIP_INT_OCCURRED_MASK)
		WREG32(mmGRPH_INTERRUPT_STATUS + crtc_offsets[crtc_id],
		       GRPH_INTERRUPT_STATUS__GRPH_PFLIP_INT_CLEAR_MASK);

	/* IRQ could occur when in initial stage */
	if (amdgpu_crtc == NULL)
		return 0;

	spin_lock_irqsave(&adev_to_drm(adev)->event_lock, flags);
	works = amdgpu_crtc->pflip_works;
	if (amdgpu_crtc->pflip_status != AMDGPU_FLIP_SUBMITTED){
		DRM_DEBUG_DRIVER("amdgpu_crtc->pflip_status = %d != "
						"AMDGPU_FLIP_SUBMITTED(%d)\n",
						amdgpu_crtc->pflip_status,
						AMDGPU_FLIP_SUBMITTED);
		spin_unlock_irqrestore(&adev_to_drm(adev)->event_lock, flags);
		return 0;
	}

	/* page flip completed. clean up */
	amdgpu_crtc->pflip_status = AMDGPU_FLIP_NONE;
	amdgpu_crtc->pflip_works = NULL;

	/* wakeup usersapce */
	if (works->event)
		drm_crtc_send_vblank_event(&amdgpu_crtc->base, works->event);

	spin_unlock_irqrestore(&adev_to_drm(adev)->event_lock, flags);

	drm_crtc_vblank_put(&amdgpu_crtc->base);
	schedule_work(&works->unpin_work);

	return 0;
}

static int dce_v8_0_hpd_irq(struct amdgpu_device *adev,
			    struct amdgpu_irq_src *source,
			    struct amdgpu_iv_entry *entry)
{
	uint32_t disp_int, mask, tmp;
	unsigned hpd;

	if (entry->src_data[0] >= adev->mode_info.num_hpd) {
		DRM_DEBUG("Unhandled interrupt: %d %d\n", entry->src_id, entry->src_data[0]);
		return 0;
	}

	hpd = entry->src_data[0];
	disp_int = RREG32(interrupt_status_offsets[hpd].reg);
	mask = interrupt_status_offsets[hpd].hpd;

	if (disp_int & mask) {
		tmp = RREG32(mmDC_HPD1_INT_CONTROL + hpd_offsets[hpd]);
		tmp |= DC_HPD1_INT_CONTROL__DC_HPD1_INT_ACK_MASK;
		WREG32(mmDC_HPD1_INT_CONTROL + hpd_offsets[hpd], tmp);
		schedule_work(&adev->hotplug_work);
		DRM_DEBUG("IH: HPD%d\n", hpd + 1);
	}

	return 0;

}

static int dce_v8_0_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	return 0;
}

static int dce_v8_0_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	return 0;
}

static const struct amd_ip_funcs dce_v8_0_ip_funcs = {
	.name = "dce_v8_0",
	.early_init = dce_v8_0_early_init,
	.late_init = NULL,
	.sw_init = dce_v8_0_sw_init,
	.sw_fini = dce_v8_0_sw_fini,
	.hw_init = dce_v8_0_hw_init,
	.hw_fini = dce_v8_0_hw_fini,
	.suspend = dce_v8_0_suspend,
	.resume = dce_v8_0_resume,
	.is_idle = dce_v8_0_is_idle,
	.wait_for_idle = dce_v8_0_wait_for_idle,
	.soft_reset = dce_v8_0_soft_reset,
	.set_clockgating_state = dce_v8_0_set_clockgating_state,
	.set_powergating_state = dce_v8_0_set_powergating_state,
};

static void
dce_v8_0_encoder_mode_set(struct drm_encoder *encoder,
			  struct drm_display_mode *mode,
			  struct drm_display_mode *adjusted_mode)
{
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);

	amdgpu_encoder->pixel_clock = adjusted_mode->clock;

	/* need to call this here rather than in prepare() since we need some crtc info */
	amdgpu_atombios_encoder_dpms(encoder, DRM_MODE_DPMS_OFF);

	/* set scaler clears this on some chips */
	dce_v8_0_set_interleave(encoder->crtc, mode);

	if (amdgpu_atombios_encoder_get_encoder_mode(encoder) == ATOM_ENCODER_MODE_HDMI) {
		dce_v8_0_afmt_enable(encoder, true);
		dce_v8_0_afmt_setmode(encoder, adjusted_mode);
	}
}

static void dce_v8_0_encoder_prepare(struct drm_encoder *encoder)
{
	struct amdgpu_device *adev = drm_to_adev(encoder->dev);
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct drm_connector *connector = amdgpu_get_connector_for_encoder(encoder);

	if ((amdgpu_encoder->active_device &
	     (ATOM_DEVICE_DFP_SUPPORT | ATOM_DEVICE_LCD_SUPPORT)) ||
	    (amdgpu_encoder_get_dp_bridge_encoder_id(encoder) !=
	     ENCODER_OBJECT_ID_NONE)) {
		struct amdgpu_encoder_atom_dig *dig = amdgpu_encoder->enc_priv;
		if (dig) {
			dig->dig_encoder = dce_v8_0_pick_dig_encoder(encoder);
			if (amdgpu_encoder->active_device & ATOM_DEVICE_DFP_SUPPORT)
				dig->afmt = adev->mode_info.afmt[dig->dig_encoder];
		}
	}

	amdgpu_atombios_scratch_regs_lock(adev, true);

	if (connector) {
		struct amdgpu_connector *amdgpu_connector = to_amdgpu_connector(connector);

		/* select the clock/data port if it uses a router */
		if (amdgpu_connector->router.cd_valid)
			amdgpu_i2c_router_select_cd_port(amdgpu_connector);

		/* turn eDP panel on for mode set */
		if (connector->connector_type == DRM_MODE_CONNECTOR_eDP)
			amdgpu_atombios_encoder_set_edp_panel_power(connector,
							     ATOM_TRANSMITTER_ACTION_POWER_ON);
	}

	/* this is needed for the pll/ss setup to work correctly in some cases */
	amdgpu_atombios_encoder_set_crtc_source(encoder);
	/* set up the FMT blocks */
	dce_v8_0_program_fmt(encoder);
}

static void dce_v8_0_encoder_commit(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);

	/* need to call this here as we need the crtc set up */
	amdgpu_atombios_encoder_dpms(encoder, DRM_MODE_DPMS_ON);
	amdgpu_atombios_scratch_regs_lock(adev, false);
}

static void dce_v8_0_encoder_disable(struct drm_encoder *encoder)
{
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct amdgpu_encoder_atom_dig *dig;

	amdgpu_atombios_encoder_dpms(encoder, DRM_MODE_DPMS_OFF);

	if (amdgpu_atombios_encoder_is_digital(encoder)) {
		if (amdgpu_atombios_encoder_get_encoder_mode(encoder) == ATOM_ENCODER_MODE_HDMI)
			dce_v8_0_afmt_enable(encoder, false);
		dig = amdgpu_encoder->enc_priv;
		dig->dig_encoder = -1;
	}
	amdgpu_encoder->active_device = 0;
}

/* these are handled by the primary encoders */
static void dce_v8_0_ext_prepare(struct drm_encoder *encoder)
{

}

static void dce_v8_0_ext_commit(struct drm_encoder *encoder)
{

}

static void
dce_v8_0_ext_mode_set(struct drm_encoder *encoder,
		      struct drm_display_mode *mode,
		      struct drm_display_mode *adjusted_mode)
{

}

static void dce_v8_0_ext_disable(struct drm_encoder *encoder)
{

}

static void
dce_v8_0_ext_dpms(struct drm_encoder *encoder, int mode)
{

}

static const struct drm_encoder_helper_funcs dce_v8_0_ext_helper_funcs = {
	.dpms = dce_v8_0_ext_dpms,
	.prepare = dce_v8_0_ext_prepare,
	.mode_set = dce_v8_0_ext_mode_set,
	.commit = dce_v8_0_ext_commit,
	.disable = dce_v8_0_ext_disable,
	/* no detect for TMDS/LVDS yet */
};

static const struct drm_encoder_helper_funcs dce_v8_0_dig_helper_funcs = {
	.dpms = amdgpu_atombios_encoder_dpms,
	.mode_fixup = amdgpu_atombios_encoder_mode_fixup,
	.prepare = dce_v8_0_encoder_prepare,
	.mode_set = dce_v8_0_encoder_mode_set,
	.commit = dce_v8_0_encoder_commit,
	.disable = dce_v8_0_encoder_disable,
	.detect = amdgpu_atombios_encoder_dig_detect,
};

static const struct drm_encoder_helper_funcs dce_v8_0_dac_helper_funcs = {
	.dpms = amdgpu_atombios_encoder_dpms,
	.mode_fixup = amdgpu_atombios_encoder_mode_fixup,
	.prepare = dce_v8_0_encoder_prepare,
	.mode_set = dce_v8_0_encoder_mode_set,
	.commit = dce_v8_0_encoder_commit,
	.detect = amdgpu_atombios_encoder_dac_detect,
};

static void dce_v8_0_encoder_destroy(struct drm_encoder *encoder)
{
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	if (amdgpu_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT))
		amdgpu_atombios_encoder_fini_backlight(amdgpu_encoder);
	kfree(amdgpu_encoder->enc_priv);
	drm_encoder_cleanup(encoder);
	kfree(amdgpu_encoder);
}

static const struct drm_encoder_funcs dce_v8_0_encoder_funcs = {
	.destroy = dce_v8_0_encoder_destroy,
};

static void dce_v8_0_encoder_add(struct amdgpu_device *adev,
				 uint32_t encoder_enum,
				 uint32_t supported_device,
				 u16 caps)
{
	struct drm_device *dev = adev_to_drm(adev);
	struct drm_encoder *encoder;
	struct amdgpu_encoder *amdgpu_encoder;

	/* see if we already added it */
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		amdgpu_encoder = to_amdgpu_encoder(encoder);
		if (amdgpu_encoder->encoder_enum == encoder_enum) {
			amdgpu_encoder->devices |= supported_device;
			return;
		}

	}

	/* add a new one */
	amdgpu_encoder = kzalloc(sizeof(struct amdgpu_encoder), GFP_KERNEL);
	if (!amdgpu_encoder)
		return;

	encoder = &amdgpu_encoder->base;
	switch (adev->mode_info.num_crtc) {
	case 1:
		encoder->possible_crtcs = 0x1;
		break;
	case 2:
	default:
		encoder->possible_crtcs = 0x3;
		break;
	case 4:
		encoder->possible_crtcs = 0xf;
		break;
	case 6:
		encoder->possible_crtcs = 0x3f;
		break;
	}

	amdgpu_encoder->enc_priv = NULL;

	amdgpu_encoder->encoder_enum = encoder_enum;
	amdgpu_encoder->encoder_id = (encoder_enum & OBJECT_ID_MASK) >> OBJECT_ID_SHIFT;
	amdgpu_encoder->devices = supported_device;
	amdgpu_encoder->rmx_type = RMX_OFF;
	amdgpu_encoder->underscan_type = UNDERSCAN_OFF;
	amdgpu_encoder->is_ext_encoder = false;
	amdgpu_encoder->caps = caps;

	switch (amdgpu_encoder->encoder_id) {
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC1:
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC2:
		drm_encoder_init(dev, encoder, &dce_v8_0_encoder_funcs,
				 DRM_MODE_ENCODER_DAC, NULL);
		drm_encoder_helper_add(encoder, &dce_v8_0_dac_helper_funcs);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY3:
		if (amdgpu_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT)) {
			amdgpu_encoder->rmx_type = RMX_FULL;
			drm_encoder_init(dev, encoder, &dce_v8_0_encoder_funcs,
					 DRM_MODE_ENCODER_LVDS, NULL);
			amdgpu_encoder->enc_priv = amdgpu_atombios_encoder_get_lcd_info(amdgpu_encoder);
		} else if (amdgpu_encoder->devices & (ATOM_DEVICE_CRT_SUPPORT)) {
			drm_encoder_init(dev, encoder, &dce_v8_0_encoder_funcs,
					 DRM_MODE_ENCODER_DAC, NULL);
			amdgpu_encoder->enc_priv = amdgpu_atombios_encoder_get_dig_info(amdgpu_encoder);
		} else {
			drm_encoder_init(dev, encoder, &dce_v8_0_encoder_funcs,
					 DRM_MODE_ENCODER_TMDS, NULL);
			amdgpu_encoder->enc_priv = amdgpu_atombios_encoder_get_dig_info(amdgpu_encoder);
		}
		drm_encoder_helper_add(encoder, &dce_v8_0_dig_helper_funcs);
		break;
	case ENCODER_OBJECT_ID_SI170B:
	case ENCODER_OBJECT_ID_CH7303:
	case ENCODER_OBJECT_ID_EXTERNAL_SDVOA:
	case ENCODER_OBJECT_ID_EXTERNAL_SDVOB:
	case ENCODER_OBJECT_ID_TITFP513:
	case ENCODER_OBJECT_ID_VT1623:
	case ENCODER_OBJECT_ID_HDMI_SI1930:
	case ENCODER_OBJECT_ID_TRAVIS:
	case ENCODER_OBJECT_ID_NUTMEG:
		/* these are handled by the primary encoders */
		amdgpu_encoder->is_ext_encoder = true;
		if (amdgpu_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT))
			drm_encoder_init(dev, encoder, &dce_v8_0_encoder_funcs,
					 DRM_MODE_ENCODER_LVDS, NULL);
		else if (amdgpu_encoder->devices & (ATOM_DEVICE_CRT_SUPPORT))
			drm_encoder_init(dev, encoder, &dce_v8_0_encoder_funcs,
					 DRM_MODE_ENCODER_DAC, NULL);
		else
			drm_encoder_init(dev, encoder, &dce_v8_0_encoder_funcs,
					 DRM_MODE_ENCODER_TMDS, NULL);
		drm_encoder_helper_add(encoder, &dce_v8_0_ext_helper_funcs);
		break;
	}
}

static const struct amdgpu_display_funcs dce_v8_0_display_funcs = {
	.bandwidth_update = &dce_v8_0_bandwidth_update,
	.vblank_get_counter = &dce_v8_0_vblank_get_counter,
	.backlight_set_level = &amdgpu_atombios_encoder_set_backlight_level,
	.backlight_get_level = &amdgpu_atombios_encoder_get_backlight_level,
	.hpd_sense = &dce_v8_0_hpd_sense,
	.hpd_set_polarity = &dce_v8_0_hpd_set_polarity,
	.hpd_get_gpio_reg = &dce_v8_0_hpd_get_gpio_reg,
	.page_flip = &dce_v8_0_page_flip,
	.page_flip_get_scanoutpos = &dce_v8_0_crtc_get_scanoutpos,
	.add_encoder = &dce_v8_0_encoder_add,
	.add_connector = &amdgpu_connector_add,
};

static void dce_v8_0_set_display_funcs(struct amdgpu_device *adev)
{
	adev->mode_info.funcs = &dce_v8_0_display_funcs;
}

static const struct amdgpu_irq_src_funcs dce_v8_0_crtc_irq_funcs = {
	.set = dce_v8_0_set_crtc_interrupt_state,
	.process = dce_v8_0_crtc_irq,
};

static const struct amdgpu_irq_src_funcs dce_v8_0_pageflip_irq_funcs = {
	.set = dce_v8_0_set_pageflip_interrupt_state,
	.process = dce_v8_0_pageflip_irq,
};

static const struct amdgpu_irq_src_funcs dce_v8_0_hpd_irq_funcs = {
	.set = dce_v8_0_set_hpd_interrupt_state,
	.process = dce_v8_0_hpd_irq,
};

static void dce_v8_0_set_irq_funcs(struct amdgpu_device *adev)
{
	if (adev->mode_info.num_crtc > 0)
		adev->crtc_irq.num_types = AMDGPU_CRTC_IRQ_VLINE1 + adev->mode_info.num_crtc;
	else
		adev->crtc_irq.num_types = 0;
	adev->crtc_irq.funcs = &dce_v8_0_crtc_irq_funcs;

	adev->pageflip_irq.num_types = adev->mode_info.num_crtc;
	adev->pageflip_irq.funcs = &dce_v8_0_pageflip_irq_funcs;

	adev->hpd_irq.num_types = adev->mode_info.num_hpd;
	adev->hpd_irq.funcs = &dce_v8_0_hpd_irq_funcs;
}

const struct amdgpu_ip_block_version dce_v8_0_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_DCE,
	.major = 8,
	.minor = 0,
	.rev = 0,
	.funcs = &dce_v8_0_ip_funcs,
};

const struct amdgpu_ip_block_version dce_v8_1_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_DCE,
	.major = 8,
	.minor = 1,
	.rev = 0,
	.funcs = &dce_v8_0_ip_funcs,
};

const struct amdgpu_ip_block_version dce_v8_2_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_DCE,
	.major = 8,
	.minor = 2,
	.rev = 0,
	.funcs = &dce_v8_0_ip_funcs,
};

const struct amdgpu_ip_block_version dce_v8_3_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_DCE,
	.major = 8,
	.minor = 3,
	.rev = 0,
	.funcs = &dce_v8_0_ip_funcs,
};

const struct amdgpu_ip_block_version dce_v8_5_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_DCE,
	.major = 8,
	.minor = 5,
	.rev = 0,
	.funcs = &dce_v8_0_ip_funcs,
};
