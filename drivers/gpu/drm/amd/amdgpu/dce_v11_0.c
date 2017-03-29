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
#include "drmP.h"
#include "amdgpu.h"
#include "amdgpu_pm.h"
#include "amdgpu_i2c.h"
#include "vid.h"
#include "atom.h"
#include "amdgpu_atombios.h"
#include "atombios_crtc.h"
#include "atombios_encoders.h"
#include "amdgpu_pll.h"
#include "amdgpu_connectors.h"

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"
#include "dce/dce_11_0_enum.h"
#include "oss/oss_3_0_d.h"
#include "oss/oss_3_0_sh_mask.h"
#include "gmc/gmc_8_1_d.h"
#include "gmc/gmc_8_1_sh_mask.h"

static void dce_v11_0_set_display_funcs(struct amdgpu_device *adev);
static void dce_v11_0_set_irq_funcs(struct amdgpu_device *adev);

static const u32 crtc_offsets[] =
{
	CRTC0_REGISTER_OFFSET,
	CRTC1_REGISTER_OFFSET,
	CRTC2_REGISTER_OFFSET,
	CRTC3_REGISTER_OFFSET,
	CRTC4_REGISTER_OFFSET,
	CRTC5_REGISTER_OFFSET,
	CRTC6_REGISTER_OFFSET
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
	DIG0_REGISTER_OFFSET,
	DIG1_REGISTER_OFFSET,
	DIG2_REGISTER_OFFSET,
	DIG3_REGISTER_OFFSET,
	DIG4_REGISTER_OFFSET,
	DIG5_REGISTER_OFFSET,
	DIG6_REGISTER_OFFSET,
	DIG7_REGISTER_OFFSET,
	DIG8_REGISTER_OFFSET
};

static const struct {
	uint32_t        reg;
	uint32_t        vblank;
	uint32_t        vline;
	uint32_t        hpd;

} interrupt_status_offsets[] = { {
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

static const u32 cz_golden_settings_a11[] =
{
	mmCRTC_DOUBLE_BUFFER_CONTROL, 0x00010101, 0x00010000,
	mmFBC_MISC, 0x1f311fff, 0x14300000,
};

static const u32 cz_mgcg_cgcg_init[] =
{
	mmXDMA_CLOCK_GATING_CNTL, 0xffffffff, 0x00000100,
	mmXDMA_MEM_POWER_CNTL, 0x00000101, 0x00000000,
};

static const u32 stoney_golden_settings_a11[] =
{
	mmCRTC_DOUBLE_BUFFER_CONTROL, 0x00010101, 0x00010000,
	mmFBC_MISC, 0x1f311fff, 0x14302000,
};


static void dce_v11_0_init_golden_registers(struct amdgpu_device *adev)
{
	switch (adev->asic_type) {
	case CHIP_CARRIZO:
		amdgpu_program_register_sequence(adev,
						 cz_mgcg_cgcg_init,
						 (const u32)ARRAY_SIZE(cz_mgcg_cgcg_init));
		amdgpu_program_register_sequence(adev,
						 cz_golden_settings_a11,
						 (const u32)ARRAY_SIZE(cz_golden_settings_a11));
		break;
	case CHIP_STONEY:
		amdgpu_program_register_sequence(adev,
						 stoney_golden_settings_a11,
						 (const u32)ARRAY_SIZE(stoney_golden_settings_a11));
		break;
	default:
		break;
	}
}

static u32 dce_v11_0_audio_endpt_rreg(struct amdgpu_device *adev,
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

static void dce_v11_0_audio_endpt_wreg(struct amdgpu_device *adev,
				      u32 block_offset, u32 reg, u32 v)
{
	unsigned long flags;

	spin_lock_irqsave(&adev->audio_endpt_idx_lock, flags);
	WREG32(mmAZALIA_F0_CODEC_ENDPOINT_INDEX + block_offset, reg);
	WREG32(mmAZALIA_F0_CODEC_ENDPOINT_DATA + block_offset, v);
	spin_unlock_irqrestore(&adev->audio_endpt_idx_lock, flags);
}

static bool dce_v11_0_is_in_vblank(struct amdgpu_device *adev, int crtc)
{
	if (RREG32(mmCRTC_STATUS + crtc_offsets[crtc]) &
			CRTC_V_BLANK_START_END__CRTC_V_BLANK_START_MASK)
		return true;
	else
		return false;
}

static bool dce_v11_0_is_counter_moving(struct amdgpu_device *adev, int crtc)
{
	u32 pos1, pos2;

	pos1 = RREG32(mmCRTC_STATUS_POSITION + crtc_offsets[crtc]);
	pos2 = RREG32(mmCRTC_STATUS_POSITION + crtc_offsets[crtc]);

	if (pos1 != pos2)
		return true;
	else
		return false;
}

/**
 * dce_v11_0_vblank_wait - vblank wait asic callback.
 *
 * @adev: amdgpu_device pointer
 * @crtc: crtc to wait for vblank on
 *
 * Wait for vblank on the requested crtc (evergreen+).
 */
static void dce_v11_0_vblank_wait(struct amdgpu_device *adev, int crtc)
{
	unsigned i = 0;

	if (crtc >= adev->mode_info.num_crtc)
		return;

	if (!(RREG32(mmCRTC_CONTROL + crtc_offsets[crtc]) & CRTC_CONTROL__CRTC_MASTER_EN_MASK))
		return;

	/* depending on when we hit vblank, we may be close to active; if so,
	 * wait for another frame.
	 */
	while (dce_v11_0_is_in_vblank(adev, crtc)) {
		if (i++ % 100 == 0) {
			if (!dce_v11_0_is_counter_moving(adev, crtc))
				break;
		}
	}

	while (!dce_v11_0_is_in_vblank(adev, crtc)) {
		if (i++ % 100 == 0) {
			if (!dce_v11_0_is_counter_moving(adev, crtc))
				break;
		}
	}
}

static u32 dce_v11_0_vblank_get_counter(struct amdgpu_device *adev, int crtc)
{
	if (crtc >= adev->mode_info.num_crtc)
		return 0;
	else
		return RREG32(mmCRTC_STATUS_FRAME_COUNT + crtc_offsets[crtc]);
}

static void dce_v11_0_pageflip_interrupt_init(struct amdgpu_device *adev)
{
	unsigned i;

	/* Enable pflip interrupts */
	for (i = 0; i < adev->mode_info.num_crtc; i++)
		amdgpu_irq_get(adev, &adev->pageflip_irq, i);
}

static void dce_v11_0_pageflip_interrupt_fini(struct amdgpu_device *adev)
{
	unsigned i;

	/* Disable pflip interrupts */
	for (i = 0; i < adev->mode_info.num_crtc; i++)
		amdgpu_irq_put(adev, &adev->pageflip_irq, i);
}

/**
 * dce_v11_0_page_flip - pageflip callback.
 *
 * @adev: amdgpu_device pointer
 * @crtc_id: crtc to cleanup pageflip on
 * @crtc_base: new address of the crtc (GPU MC address)
 *
 * Triggers the actual pageflip by updating the primary
 * surface base address.
 */
static void dce_v11_0_page_flip(struct amdgpu_device *adev,
			      int crtc_id, u64 crtc_base)
{
	struct amdgpu_crtc *amdgpu_crtc = adev->mode_info.crtcs[crtc_id];

	/* update the scanout addresses */
	WREG32(mmGRPH_PRIMARY_SURFACE_ADDRESS_HIGH + amdgpu_crtc->crtc_offset,
	       upper_32_bits(crtc_base));
	/* writing to the low address triggers the update */
	WREG32(mmGRPH_PRIMARY_SURFACE_ADDRESS + amdgpu_crtc->crtc_offset,
	       lower_32_bits(crtc_base));
	/* post the write */
	RREG32(mmGRPH_PRIMARY_SURFACE_ADDRESS + amdgpu_crtc->crtc_offset);
}

static int dce_v11_0_crtc_get_scanoutpos(struct amdgpu_device *adev, int crtc,
					u32 *vbl, u32 *position)
{
	if ((crtc < 0) || (crtc >= adev->mode_info.num_crtc))
		return -EINVAL;

	*vbl = RREG32(mmCRTC_V_BLANK_START_END + crtc_offsets[crtc]);
	*position = RREG32(mmCRTC_STATUS_POSITION + crtc_offsets[crtc]);

	return 0;
}

/**
 * dce_v11_0_hpd_sense - hpd sense callback.
 *
 * @adev: amdgpu_device pointer
 * @hpd: hpd (hotplug detect) pin
 *
 * Checks if a digital monitor is connected (evergreen+).
 * Returns true if connected, false if not connected.
 */
static bool dce_v11_0_hpd_sense(struct amdgpu_device *adev,
			       enum amdgpu_hpd_id hpd)
{
	int idx;
	bool connected = false;

	switch (hpd) {
	case AMDGPU_HPD_1:
		idx = 0;
		break;
	case AMDGPU_HPD_2:
		idx = 1;
		break;
	case AMDGPU_HPD_3:
		idx = 2;
		break;
	case AMDGPU_HPD_4:
		idx = 3;
		break;
	case AMDGPU_HPD_5:
		idx = 4;
		break;
	case AMDGPU_HPD_6:
		idx = 5;
		break;
	default:
		return connected;
	}

	if (RREG32(mmDC_HPD_INT_STATUS + hpd_offsets[idx]) &
	    DC_HPD_INT_STATUS__DC_HPD_SENSE_MASK)
		connected = true;

	return connected;
}

/**
 * dce_v11_0_hpd_set_polarity - hpd set polarity callback.
 *
 * @adev: amdgpu_device pointer
 * @hpd: hpd (hotplug detect) pin
 *
 * Set the polarity of the hpd pin (evergreen+).
 */
static void dce_v11_0_hpd_set_polarity(struct amdgpu_device *adev,
				      enum amdgpu_hpd_id hpd)
{
	u32 tmp;
	bool connected = dce_v11_0_hpd_sense(adev, hpd);
	int idx;

	switch (hpd) {
	case AMDGPU_HPD_1:
		idx = 0;
		break;
	case AMDGPU_HPD_2:
		idx = 1;
		break;
	case AMDGPU_HPD_3:
		idx = 2;
		break;
	case AMDGPU_HPD_4:
		idx = 3;
		break;
	case AMDGPU_HPD_5:
		idx = 4;
		break;
	case AMDGPU_HPD_6:
		idx = 5;
		break;
	default:
		return;
	}

	tmp = RREG32(mmDC_HPD_INT_CONTROL + hpd_offsets[idx]);
	if (connected)
		tmp = REG_SET_FIELD(tmp, DC_HPD_INT_CONTROL, DC_HPD_INT_POLARITY, 0);
	else
		tmp = REG_SET_FIELD(tmp, DC_HPD_INT_CONTROL, DC_HPD_INT_POLARITY, 1);
	WREG32(mmDC_HPD_INT_CONTROL + hpd_offsets[idx], tmp);
}

/**
 * dce_v11_0_hpd_init - hpd setup callback.
 *
 * @adev: amdgpu_device pointer
 *
 * Setup the hpd pins used by the card (evergreen+).
 * Enable the pin, set the polarity, and enable the hpd interrupts.
 */
static void dce_v11_0_hpd_init(struct amdgpu_device *adev)
{
	struct drm_device *dev = adev->ddev;
	struct drm_connector *connector;
	u32 tmp;
	int idx;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		struct amdgpu_connector *amdgpu_connector = to_amdgpu_connector(connector);

		switch (amdgpu_connector->hpd.hpd) {
		case AMDGPU_HPD_1:
			idx = 0;
			break;
		case AMDGPU_HPD_2:
			idx = 1;
			break;
		case AMDGPU_HPD_3:
			idx = 2;
			break;
		case AMDGPU_HPD_4:
			idx = 3;
			break;
		case AMDGPU_HPD_5:
			idx = 4;
			break;
		case AMDGPU_HPD_6:
			idx = 5;
			break;
		default:
			continue;
		}

		if (connector->connector_type == DRM_MODE_CONNECTOR_eDP ||
		    connector->connector_type == DRM_MODE_CONNECTOR_LVDS) {
			/* don't try to enable hpd on eDP or LVDS avoid breaking the
			 * aux dp channel on imac and help (but not completely fix)
			 * https://bugzilla.redhat.com/show_bug.cgi?id=726143
			 * also avoid interrupt storms during dpms.
			 */
			tmp = RREG32(mmDC_HPD_INT_CONTROL + hpd_offsets[idx]);
			tmp = REG_SET_FIELD(tmp, DC_HPD_INT_CONTROL, DC_HPD_INT_EN, 0);
			WREG32(mmDC_HPD_INT_CONTROL + hpd_offsets[idx], tmp);
			continue;
		}

		tmp = RREG32(mmDC_HPD_CONTROL + hpd_offsets[idx]);
		tmp = REG_SET_FIELD(tmp, DC_HPD_CONTROL, DC_HPD_EN, 1);
		WREG32(mmDC_HPD_CONTROL + hpd_offsets[idx], tmp);

		tmp = RREG32(mmDC_HPD_TOGGLE_FILT_CNTL + hpd_offsets[idx]);
		tmp = REG_SET_FIELD(tmp, DC_HPD_TOGGLE_FILT_CNTL,
				    DC_HPD_CONNECT_INT_DELAY,
				    AMDGPU_HPD_CONNECT_INT_DELAY_IN_MS);
		tmp = REG_SET_FIELD(tmp, DC_HPD_TOGGLE_FILT_CNTL,
				    DC_HPD_DISCONNECT_INT_DELAY,
				    AMDGPU_HPD_DISCONNECT_INT_DELAY_IN_MS);
		WREG32(mmDC_HPD_TOGGLE_FILT_CNTL + hpd_offsets[idx], tmp);

		dce_v11_0_hpd_set_polarity(adev, amdgpu_connector->hpd.hpd);
		amdgpu_irq_get(adev, &adev->hpd_irq, amdgpu_connector->hpd.hpd);
	}
}

/**
 * dce_v11_0_hpd_fini - hpd tear down callback.
 *
 * @adev: amdgpu_device pointer
 *
 * Tear down the hpd pins used by the card (evergreen+).
 * Disable the hpd interrupts.
 */
static void dce_v11_0_hpd_fini(struct amdgpu_device *adev)
{
	struct drm_device *dev = adev->ddev;
	struct drm_connector *connector;
	u32 tmp;
	int idx;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		struct amdgpu_connector *amdgpu_connector = to_amdgpu_connector(connector);

		switch (amdgpu_connector->hpd.hpd) {
		case AMDGPU_HPD_1:
			idx = 0;
			break;
		case AMDGPU_HPD_2:
			idx = 1;
			break;
		case AMDGPU_HPD_3:
			idx = 2;
			break;
		case AMDGPU_HPD_4:
			idx = 3;
			break;
		case AMDGPU_HPD_5:
			idx = 4;
			break;
		case AMDGPU_HPD_6:
			idx = 5;
			break;
		default:
			continue;
		}

		tmp = RREG32(mmDC_HPD_CONTROL + hpd_offsets[idx]);
		tmp = REG_SET_FIELD(tmp, DC_HPD_CONTROL, DC_HPD_EN, 0);
		WREG32(mmDC_HPD_CONTROL + hpd_offsets[idx], tmp);

		amdgpu_irq_put(adev, &adev->hpd_irq, amdgpu_connector->hpd.hpd);
	}
}

static u32 dce_v11_0_hpd_get_gpio_reg(struct amdgpu_device *adev)
{
	return mmDC_GPIO_HPD_A;
}

static bool dce_v11_0_is_display_hung(struct amdgpu_device *adev)
{
	u32 crtc_hung = 0;
	u32 crtc_status[6];
	u32 i, j, tmp;

	for (i = 0; i < adev->mode_info.num_crtc; i++) {
		tmp = RREG32(mmCRTC_CONTROL + crtc_offsets[i]);
		if (REG_GET_FIELD(tmp, CRTC_CONTROL, CRTC_MASTER_EN)) {
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

static void dce_v11_0_stop_mc_access(struct amdgpu_device *adev,
				     struct amdgpu_mode_mc_save *save)
{
	u32 crtc_enabled, tmp;
	int i;

	save->vga_render_control = RREG32(mmVGA_RENDER_CONTROL);
	save->vga_hdp_control = RREG32(mmVGA_HDP_CONTROL);

	/* disable VGA render */
	tmp = RREG32(mmVGA_RENDER_CONTROL);
	tmp = REG_SET_FIELD(tmp, VGA_RENDER_CONTROL, VGA_VSTATUS_CNTL, 0);
	WREG32(mmVGA_RENDER_CONTROL, tmp);

	/* blank the display controllers */
	for (i = 0; i < adev->mode_info.num_crtc; i++) {
		crtc_enabled = REG_GET_FIELD(RREG32(mmCRTC_CONTROL + crtc_offsets[i]),
					     CRTC_CONTROL, CRTC_MASTER_EN);
		if (crtc_enabled) {
#if 0
			u32 frame_count;
			int j;

			save->crtc_enabled[i] = true;
			tmp = RREG32(mmCRTC_BLANK_CONTROL + crtc_offsets[i]);
			if (REG_GET_FIELD(tmp, CRTC_BLANK_CONTROL, CRTC_BLANK_DATA_EN) == 0) {
				amdgpu_display_vblank_wait(adev, i);
				WREG32(mmCRTC_UPDATE_LOCK + crtc_offsets[i], 1);
				tmp = REG_SET_FIELD(tmp, CRTC_BLANK_CONTROL, CRTC_BLANK_DATA_EN, 1);
				WREG32(mmCRTC_BLANK_CONTROL + crtc_offsets[i], tmp);
				WREG32(mmCRTC_UPDATE_LOCK + crtc_offsets[i], 0);
			}
			/* wait for the next frame */
			frame_count = amdgpu_display_vblank_get_counter(adev, i);
			for (j = 0; j < adev->usec_timeout; j++) {
				if (amdgpu_display_vblank_get_counter(adev, i) != frame_count)
					break;
				udelay(1);
			}
			tmp = RREG32(mmGRPH_UPDATE + crtc_offsets[i]);
			if (REG_GET_FIELD(tmp, GRPH_UPDATE, GRPH_UPDATE_LOCK) == 0) {
				tmp = REG_SET_FIELD(tmp, GRPH_UPDATE, GRPH_UPDATE_LOCK, 1);
				WREG32(mmGRPH_UPDATE + crtc_offsets[i], tmp);
			}
			tmp = RREG32(mmCRTC_MASTER_UPDATE_LOCK + crtc_offsets[i]);
			if (REG_GET_FIELD(tmp, CRTC_MASTER_UPDATE_LOCK, MASTER_UPDATE_LOCK) == 0) {
				tmp = REG_SET_FIELD(tmp, CRTC_MASTER_UPDATE_LOCK, MASTER_UPDATE_LOCK, 1);
				WREG32(mmCRTC_MASTER_UPDATE_LOCK + crtc_offsets[i], tmp);
			}
#else
			/* XXX this is a hack to avoid strange behavior with EFI on certain systems */
			WREG32(mmCRTC_UPDATE_LOCK + crtc_offsets[i], 1);
			tmp = RREG32(mmCRTC_CONTROL + crtc_offsets[i]);
			tmp = REG_SET_FIELD(tmp, CRTC_CONTROL, CRTC_MASTER_EN, 0);
			WREG32(mmCRTC_CONTROL + crtc_offsets[i], tmp);
			WREG32(mmCRTC_UPDATE_LOCK + crtc_offsets[i], 0);
			save->crtc_enabled[i] = false;
			/* ***** */
#endif
		} else {
			save->crtc_enabled[i] = false;
		}
	}
}

static void dce_v11_0_resume_mc_access(struct amdgpu_device *adev,
				       struct amdgpu_mode_mc_save *save)
{
	u32 tmp, frame_count;
	int i, j;

	/* update crtc base addresses */
	for (i = 0; i < adev->mode_info.num_crtc; i++) {
		WREG32(mmGRPH_PRIMARY_SURFACE_ADDRESS_HIGH + crtc_offsets[i],
		       upper_32_bits(adev->mc.vram_start));
		WREG32(mmGRPH_SECONDARY_SURFACE_ADDRESS_HIGH + crtc_offsets[i],
		       upper_32_bits(adev->mc.vram_start));
		WREG32(mmGRPH_PRIMARY_SURFACE_ADDRESS + crtc_offsets[i],
		       (u32)adev->mc.vram_start);
		WREG32(mmGRPH_SECONDARY_SURFACE_ADDRESS + crtc_offsets[i],
		       (u32)adev->mc.vram_start);

		if (save->crtc_enabled[i]) {
			tmp = RREG32(mmCRTC_MASTER_UPDATE_MODE + crtc_offsets[i]);
			if (REG_GET_FIELD(tmp, CRTC_MASTER_UPDATE_MODE, MASTER_UPDATE_MODE) != 3) {
				tmp = REG_SET_FIELD(tmp, CRTC_MASTER_UPDATE_MODE, MASTER_UPDATE_MODE, 3);
				WREG32(mmCRTC_MASTER_UPDATE_MODE + crtc_offsets[i], tmp);
			}
			tmp = RREG32(mmGRPH_UPDATE + crtc_offsets[i]);
			if (REG_GET_FIELD(tmp, GRPH_UPDATE, GRPH_UPDATE_LOCK)) {
				tmp = REG_SET_FIELD(tmp, GRPH_UPDATE, GRPH_UPDATE_LOCK, 0);
				WREG32(mmGRPH_UPDATE + crtc_offsets[i], tmp);
			}
			tmp = RREG32(mmCRTC_MASTER_UPDATE_LOCK + crtc_offsets[i]);
			if (REG_GET_FIELD(tmp, CRTC_MASTER_UPDATE_LOCK, MASTER_UPDATE_LOCK)) {
				tmp = REG_SET_FIELD(tmp, CRTC_MASTER_UPDATE_LOCK, MASTER_UPDATE_LOCK, 0);
				WREG32(mmCRTC_MASTER_UPDATE_LOCK + crtc_offsets[i], tmp);
			}
			for (j = 0; j < adev->usec_timeout; j++) {
				tmp = RREG32(mmGRPH_UPDATE + crtc_offsets[i]);
				if (REG_GET_FIELD(tmp, GRPH_UPDATE, GRPH_SURFACE_UPDATE_PENDING) == 0)
					break;
				udelay(1);
			}
			tmp = RREG32(mmCRTC_BLANK_CONTROL + crtc_offsets[i]);
			tmp = REG_SET_FIELD(tmp, CRTC_BLANK_CONTROL, CRTC_BLANK_DATA_EN, 0);
			WREG32(mmCRTC_UPDATE_LOCK + crtc_offsets[i], 1);
			WREG32(mmCRTC_BLANK_CONTROL + crtc_offsets[i], tmp);
			WREG32(mmCRTC_UPDATE_LOCK + crtc_offsets[i], 0);
			/* wait for the next frame */
			frame_count = amdgpu_display_vblank_get_counter(adev, i);
			for (j = 0; j < adev->usec_timeout; j++) {
				if (amdgpu_display_vblank_get_counter(adev, i) != frame_count)
					break;
				udelay(1);
			}
		}
	}

	WREG32(mmVGA_MEMORY_BASE_ADDRESS_HIGH, upper_32_bits(adev->mc.vram_start));
	WREG32(mmVGA_MEMORY_BASE_ADDRESS, lower_32_bits(adev->mc.vram_start));

	/* Unlock vga access */
	WREG32(mmVGA_HDP_CONTROL, save->vga_hdp_control);
	mdelay(1);
	WREG32(mmVGA_RENDER_CONTROL, save->vga_render_control);
}

static void dce_v11_0_set_vga_render_state(struct amdgpu_device *adev,
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

static void dce_v11_0_program_fmt(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_device *adev = dev->dev_private;
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
		if (dither == AMDGPU_FMT_DITHER_ENABLE) {
			/* XXX sort out optimal dither settings */
			tmp = REG_SET_FIELD(tmp, FMT_BIT_DEPTH_CONTROL, FMT_FRAME_RANDOM_ENABLE, 1);
			tmp = REG_SET_FIELD(tmp, FMT_BIT_DEPTH_CONTROL, FMT_HIGHPASS_RANDOM_ENABLE, 1);
			tmp = REG_SET_FIELD(tmp, FMT_BIT_DEPTH_CONTROL, FMT_SPATIAL_DITHER_EN, 1);
			tmp = REG_SET_FIELD(tmp, FMT_BIT_DEPTH_CONTROL, FMT_SPATIAL_DITHER_DEPTH, 0);
		} else {
			tmp = REG_SET_FIELD(tmp, FMT_BIT_DEPTH_CONTROL, FMT_TRUNCATE_EN, 1);
			tmp = REG_SET_FIELD(tmp, FMT_BIT_DEPTH_CONTROL, FMT_TRUNCATE_DEPTH, 0);
		}
		break;
	case 8:
		if (dither == AMDGPU_FMT_DITHER_ENABLE) {
			/* XXX sort out optimal dither settings */
			tmp = REG_SET_FIELD(tmp, FMT_BIT_DEPTH_CONTROL, FMT_FRAME_RANDOM_ENABLE, 1);
			tmp = REG_SET_FIELD(tmp, FMT_BIT_DEPTH_CONTROL, FMT_HIGHPASS_RANDOM_ENABLE, 1);
			tmp = REG_SET_FIELD(tmp, FMT_BIT_DEPTH_CONTROL, FMT_RGB_RANDOM_ENABLE, 1);
			tmp = REG_SET_FIELD(tmp, FMT_BIT_DEPTH_CONTROL, FMT_SPATIAL_DITHER_EN, 1);
			tmp = REG_SET_FIELD(tmp, FMT_BIT_DEPTH_CONTROL, FMT_SPATIAL_DITHER_DEPTH, 1);
		} else {
			tmp = REG_SET_FIELD(tmp, FMT_BIT_DEPTH_CONTROL, FMT_TRUNCATE_EN, 1);
			tmp = REG_SET_FIELD(tmp, FMT_BIT_DEPTH_CONTROL, FMT_TRUNCATE_DEPTH, 1);
		}
		break;
	case 10:
		if (dither == AMDGPU_FMT_DITHER_ENABLE) {
			/* XXX sort out optimal dither settings */
			tmp = REG_SET_FIELD(tmp, FMT_BIT_DEPTH_CONTROL, FMT_FRAME_RANDOM_ENABLE, 1);
			tmp = REG_SET_FIELD(tmp, FMT_BIT_DEPTH_CONTROL, FMT_HIGHPASS_RANDOM_ENABLE, 1);
			tmp = REG_SET_FIELD(tmp, FMT_BIT_DEPTH_CONTROL, FMT_RGB_RANDOM_ENABLE, 1);
			tmp = REG_SET_FIELD(tmp, FMT_BIT_DEPTH_CONTROL, FMT_SPATIAL_DITHER_EN, 1);
			tmp = REG_SET_FIELD(tmp, FMT_BIT_DEPTH_CONTROL, FMT_SPATIAL_DITHER_DEPTH, 2);
		} else {
			tmp = REG_SET_FIELD(tmp, FMT_BIT_DEPTH_CONTROL, FMT_TRUNCATE_EN, 1);
			tmp = REG_SET_FIELD(tmp, FMT_BIT_DEPTH_CONTROL, FMT_TRUNCATE_DEPTH, 2);
		}
		break;
	default:
		/* not needed */
		break;
	}

	WREG32(mmFMT_BIT_DEPTH_CONTROL + amdgpu_crtc->crtc_offset, tmp);
}


/* display watermark setup */
/**
 * dce_v11_0_line_buffer_adjust - Set up the line buffer
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
static u32 dce_v11_0_line_buffer_adjust(struct amdgpu_device *adev,
				       struct amdgpu_crtc *amdgpu_crtc,
				       struct drm_display_mode *mode)
{
	u32 tmp, buffer_alloc, i, mem_cfg;
	u32 pipe_offset = amdgpu_crtc->crtc_id;
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
			mem_cfg = 1;
			buffer_alloc = 2;
		} else if (mode->crtc_hdisplay < 2560) {
			mem_cfg = 2;
			buffer_alloc = 2;
		} else if (mode->crtc_hdisplay < 4096) {
			mem_cfg = 0;
			buffer_alloc = (adev->flags & AMD_IS_APU) ? 2 : 4;
		} else {
			DRM_DEBUG_KMS("Mode too big for LB!\n");
			mem_cfg = 0;
			buffer_alloc = (adev->flags & AMD_IS_APU) ? 2 : 4;
		}
	} else {
		mem_cfg = 1;
		buffer_alloc = 0;
	}

	tmp = RREG32(mmLB_MEMORY_CTRL + amdgpu_crtc->crtc_offset);
	tmp = REG_SET_FIELD(tmp, LB_MEMORY_CTRL, LB_MEMORY_CONFIG, mem_cfg);
	WREG32(mmLB_MEMORY_CTRL + amdgpu_crtc->crtc_offset, tmp);

	tmp = RREG32(mmPIPE0_DMIF_BUFFER_CONTROL + pipe_offset);
	tmp = REG_SET_FIELD(tmp, PIPE0_DMIF_BUFFER_CONTROL, DMIF_BUFFERS_ALLOCATED, buffer_alloc);
	WREG32(mmPIPE0_DMIF_BUFFER_CONTROL + pipe_offset, tmp);

	for (i = 0; i < adev->usec_timeout; i++) {
		tmp = RREG32(mmPIPE0_DMIF_BUFFER_CONTROL + pipe_offset);
		if (REG_GET_FIELD(tmp, PIPE0_DMIF_BUFFER_CONTROL, DMIF_BUFFERS_ALLOCATION_COMPLETED))
			break;
		udelay(1);
	}

	if (amdgpu_crtc->base.enabled && mode) {
		switch (mem_cfg) {
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

	switch (REG_GET_FIELD(tmp, MC_SHARED_CHMAP, NOOFCHAN)) {
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

struct dce10_wm_params {
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
 * dce_v11_0_dram_bandwidth - get the dram bandwidth
 *
 * @wm: watermark calculation data
 *
 * Calculate the raw dram bandwidth (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the dram bandwidth in MBytes/s
 */
static u32 dce_v11_0_dram_bandwidth(struct dce10_wm_params *wm)
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
 * dce_v11_0_dram_bandwidth_for_display - get the dram bandwidth for display
 *
 * @wm: watermark calculation data
 *
 * Calculate the dram bandwidth used for display (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the dram bandwidth for display in MBytes/s
 */
static u32 dce_v11_0_dram_bandwidth_for_display(struct dce10_wm_params *wm)
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
 * dce_v11_0_data_return_bandwidth - get the data return bandwidth
 *
 * @wm: watermark calculation data
 *
 * Calculate the data return bandwidth used for display (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the data return bandwidth in MBytes/s
 */
static u32 dce_v11_0_data_return_bandwidth(struct dce10_wm_params *wm)
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
 * dce_v11_0_dmif_request_bandwidth - get the dmif bandwidth
 *
 * @wm: watermark calculation data
 *
 * Calculate the dmif bandwidth used for display (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the dmif bandwidth in MBytes/s
 */
static u32 dce_v11_0_dmif_request_bandwidth(struct dce10_wm_params *wm)
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
 * dce_v11_0_available_bandwidth - get the min available bandwidth
 *
 * @wm: watermark calculation data
 *
 * Calculate the min available bandwidth used for display (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the min available bandwidth in MBytes/s
 */
static u32 dce_v11_0_available_bandwidth(struct dce10_wm_params *wm)
{
	/* Calculate the Available bandwidth. Display can use this temporarily but not in average. */
	u32 dram_bandwidth = dce_v11_0_dram_bandwidth(wm);
	u32 data_return_bandwidth = dce_v11_0_data_return_bandwidth(wm);
	u32 dmif_req_bandwidth = dce_v11_0_dmif_request_bandwidth(wm);

	return min(dram_bandwidth, min(data_return_bandwidth, dmif_req_bandwidth));
}

/**
 * dce_v11_0_average_bandwidth - get the average available bandwidth
 *
 * @wm: watermark calculation data
 *
 * Calculate the average available bandwidth used for display (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the average available bandwidth in MBytes/s
 */
static u32 dce_v11_0_average_bandwidth(struct dce10_wm_params *wm)
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
 * dce_v11_0_latency_watermark - get the latency watermark
 *
 * @wm: watermark calculation data
 *
 * Calculate the latency watermark (CIK).
 * Used for display watermark bandwidth calculations
 * Returns the latency watermark in ns
 */
static u32 dce_v11_0_latency_watermark(struct dce10_wm_params *wm)
{
	/* First calculate the latency in ns */
	u32 mc_latency = 2000; /* 2000 ns. */
	u32 available_bandwidth = dce_v11_0_available_bandwidth(wm);
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
 * dce_v11_0_average_bandwidth_vs_dram_bandwidth_for_display - check
 * average and available dram bandwidth
 *
 * @wm: watermark calculation data
 *
 * Check if the display average bandwidth fits in the display
 * dram bandwidth (CIK).
 * Used for display watermark bandwidth calculations
 * Returns true if the display fits, false if not.
 */
static bool dce_v11_0_average_bandwidth_vs_dram_bandwidth_for_display(struct dce10_wm_params *wm)
{
	if (dce_v11_0_average_bandwidth(wm) <=
	    (dce_v11_0_dram_bandwidth_for_display(wm) / wm->num_heads))
		return true;
	else
		return false;
}

/**
 * dce_v11_0_average_bandwidth_vs_available_bandwidth - check
 * average and available bandwidth
 *
 * @wm: watermark calculation data
 *
 * Check if the display average bandwidth fits in the display
 * available bandwidth (CIK).
 * Used for display watermark bandwidth calculations
 * Returns true if the display fits, false if not.
 */
static bool dce_v11_0_average_bandwidth_vs_available_bandwidth(struct dce10_wm_params *wm)
{
	if (dce_v11_0_average_bandwidth(wm) <=
	    (dce_v11_0_available_bandwidth(wm) / wm->num_heads))
		return true;
	else
		return false;
}

/**
 * dce_v11_0_check_latency_hiding - check latency hiding
 *
 * @wm: watermark calculation data
 *
 * Check latency hiding (CIK).
 * Used for display watermark bandwidth calculations
 * Returns true if the display fits, false if not.
 */
static bool dce_v11_0_check_latency_hiding(struct dce10_wm_params *wm)
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

	if (dce_v11_0_latency_watermark(wm) <= latency_hiding)
		return true;
	else
		return false;
}

/**
 * dce_v11_0_program_watermarks - program display watermarks
 *
 * @adev: amdgpu_device pointer
 * @amdgpu_crtc: the selected display controller
 * @lb_size: line buffer size
 * @num_heads: number of display controllers in use
 *
 * Calculate and program the display watermarks for the
 * selected display controller (CIK).
 */
static void dce_v11_0_program_watermarks(struct amdgpu_device *adev,
					struct amdgpu_crtc *amdgpu_crtc,
					u32 lb_size, u32 num_heads)
{
	struct drm_display_mode *mode = &amdgpu_crtc->base.mode;
	struct dce10_wm_params wm_low, wm_high;
	u32 pixel_period;
	u32 line_time = 0;
	u32 latency_watermark_a = 0, latency_watermark_b = 0;
	u32 tmp, wm_mask, lb_vblank_lead_lines = 0;

	if (amdgpu_crtc->base.enabled && num_heads && mode) {
		pixel_period = 1000000 / (u32)mode->clock;
		line_time = min((u32)mode->crtc_htotal * pixel_period, (u32)65535);

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
		wm_high.active_time = mode->crtc_hdisplay * pixel_period;
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
		latency_watermark_a = min(dce_v11_0_latency_watermark(&wm_high), (u32)65535);

		/* possibly force display priority to high */
		/* should really do this at mode validation time... */
		if (!dce_v11_0_average_bandwidth_vs_dram_bandwidth_for_display(&wm_high) ||
		    !dce_v11_0_average_bandwidth_vs_available_bandwidth(&wm_high) ||
		    !dce_v11_0_check_latency_hiding(&wm_high) ||
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
		wm_low.active_time = mode->crtc_hdisplay * pixel_period;
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
		latency_watermark_b = min(dce_v11_0_latency_watermark(&wm_low), (u32)65535);

		/* possibly force display priority to high */
		/* should really do this at mode validation time... */
		if (!dce_v11_0_average_bandwidth_vs_dram_bandwidth_for_display(&wm_low) ||
		    !dce_v11_0_average_bandwidth_vs_available_bandwidth(&wm_low) ||
		    !dce_v11_0_check_latency_hiding(&wm_low) ||
		    (adev->mode_info.disp_priority == 2)) {
			DRM_DEBUG_KMS("force priority to high\n");
		}
		lb_vblank_lead_lines = DIV_ROUND_UP(lb_size, mode->crtc_hdisplay);
	}

	/* select wm A */
	wm_mask = RREG32(mmDPG_WATERMARK_MASK_CONTROL + amdgpu_crtc->crtc_offset);
	tmp = REG_SET_FIELD(wm_mask, DPG_WATERMARK_MASK_CONTROL, URGENCY_WATERMARK_MASK, 1);
	WREG32(mmDPG_WATERMARK_MASK_CONTROL + amdgpu_crtc->crtc_offset, tmp);
	tmp = RREG32(mmDPG_PIPE_URGENCY_CONTROL + amdgpu_crtc->crtc_offset);
	tmp = REG_SET_FIELD(tmp, DPG_PIPE_URGENCY_CONTROL, URGENCY_LOW_WATERMARK, latency_watermark_a);
	tmp = REG_SET_FIELD(tmp, DPG_PIPE_URGENCY_CONTROL, URGENCY_HIGH_WATERMARK, line_time);
	WREG32(mmDPG_PIPE_URGENCY_CONTROL + amdgpu_crtc->crtc_offset, tmp);
	/* select wm B */
	tmp = REG_SET_FIELD(wm_mask, DPG_WATERMARK_MASK_CONTROL, URGENCY_WATERMARK_MASK, 2);
	WREG32(mmDPG_WATERMARK_MASK_CONTROL + amdgpu_crtc->crtc_offset, tmp);
	tmp = RREG32(mmDPG_PIPE_URGENCY_CONTROL + amdgpu_crtc->crtc_offset);
	tmp = REG_SET_FIELD(tmp, DPG_PIPE_URGENCY_CONTROL, URGENCY_LOW_WATERMARK, latency_watermark_b);
	tmp = REG_SET_FIELD(tmp, DPG_PIPE_URGENCY_CONTROL, URGENCY_HIGH_WATERMARK, line_time);
	WREG32(mmDPG_PIPE_URGENCY_CONTROL + amdgpu_crtc->crtc_offset, tmp);
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
 * dce_v11_0_bandwidth_update - program display watermarks
 *
 * @adev: amdgpu_device pointer
 *
 * Calculate and program the display watermarks and line
 * buffer allocation (CIK).
 */
static void dce_v11_0_bandwidth_update(struct amdgpu_device *adev)
{
	struct drm_display_mode *mode = NULL;
	u32 num_heads = 0, lb_size;
	int i;

	amdgpu_update_display_priority(adev);

	for (i = 0; i < adev->mode_info.num_crtc; i++) {
		if (adev->mode_info.crtcs[i]->base.enabled)
			num_heads++;
	}
	for (i = 0; i < adev->mode_info.num_crtc; i++) {
		mode = &adev->mode_info.crtcs[i]->base.mode;
		lb_size = dce_v11_0_line_buffer_adjust(adev, adev->mode_info.crtcs[i], mode);
		dce_v11_0_program_watermarks(adev, adev->mode_info.crtcs[i],
					    lb_size, num_heads);
	}
}

static void dce_v11_0_audio_get_connected_pins(struct amdgpu_device *adev)
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

static struct amdgpu_audio_pin *dce_v11_0_audio_get_pin(struct amdgpu_device *adev)
{
	int i;

	dce_v11_0_audio_get_connected_pins(adev);

	for (i = 0; i < adev->mode_info.audio.num_pins; i++) {
		if (adev->mode_info.audio.pin[i].connected)
			return &adev->mode_info.audio.pin[i];
	}
	DRM_ERROR("No connected audio pins found!\n");
	return NULL;
}

static void dce_v11_0_afmt_audio_select_pin(struct drm_encoder *encoder)
{
	struct amdgpu_device *adev = encoder->dev->dev_private;
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct amdgpu_encoder_atom_dig *dig = amdgpu_encoder->enc_priv;
	u32 tmp;

	if (!dig || !dig->afmt || !dig->afmt->pin)
		return;

	tmp = RREG32(mmAFMT_AUDIO_SRC_CONTROL + dig->afmt->offset);
	tmp = REG_SET_FIELD(tmp, AFMT_AUDIO_SRC_CONTROL, AFMT_AUDIO_SRC_SELECT, dig->afmt->pin->id);
	WREG32(mmAFMT_AUDIO_SRC_CONTROL + dig->afmt->offset, tmp);
}

static void dce_v11_0_audio_write_latency_fields(struct drm_encoder *encoder,
						struct drm_display_mode *mode)
{
	struct amdgpu_device *adev = encoder->dev->dev_private;
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct amdgpu_encoder_atom_dig *dig = amdgpu_encoder->enc_priv;
	struct drm_connector *connector;
	struct amdgpu_connector *amdgpu_connector = NULL;
	u32 tmp;
	int interlace = 0;

	if (!dig || !dig->afmt || !dig->afmt->pin)
		return;

	list_for_each_entry(connector, &encoder->dev->mode_config.connector_list, head) {
		if (connector->encoder == encoder) {
			amdgpu_connector = to_amdgpu_connector(connector);
			break;
		}
	}

	if (!amdgpu_connector) {
		DRM_ERROR("Couldn't find encoder's connector\n");
		return;
	}

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		interlace = 1;
	if (connector->latency_present[interlace]) {
		tmp = REG_SET_FIELD(0, AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_LIPSYNC,
				    VIDEO_LIPSYNC, connector->video_latency[interlace]);
		tmp = REG_SET_FIELD(0, AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_LIPSYNC,
				    AUDIO_LIPSYNC, connector->audio_latency[interlace]);
	} else {
		tmp = REG_SET_FIELD(0, AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_LIPSYNC,
				    VIDEO_LIPSYNC, 0);
		tmp = REG_SET_FIELD(0, AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_LIPSYNC,
				    AUDIO_LIPSYNC, 0);
	}
	WREG32_AUDIO_ENDPT(dig->afmt->pin->offset,
			   ixAZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_LIPSYNC, tmp);
}

static void dce_v11_0_audio_write_speaker_allocation(struct drm_encoder *encoder)
{
	struct amdgpu_device *adev = encoder->dev->dev_private;
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct amdgpu_encoder_atom_dig *dig = amdgpu_encoder->enc_priv;
	struct drm_connector *connector;
	struct amdgpu_connector *amdgpu_connector = NULL;
	u32 tmp;
	u8 *sadb = NULL;
	int sad_count;

	if (!dig || !dig->afmt || !dig->afmt->pin)
		return;

	list_for_each_entry(connector, &encoder->dev->mode_config.connector_list, head) {
		if (connector->encoder == encoder) {
			amdgpu_connector = to_amdgpu_connector(connector);
			break;
		}
	}

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
	tmp = RREG32_AUDIO_ENDPT(dig->afmt->pin->offset,
				 ixAZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER);
	tmp = REG_SET_FIELD(tmp, AZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER,
			    DP_CONNECTION, 0);
	/* set HDMI mode */
	tmp = REG_SET_FIELD(tmp, AZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER,
			    HDMI_CONNECTION, 1);
	if (sad_count)
		tmp = REG_SET_FIELD(tmp, AZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER,
				    SPEAKER_ALLOCATION, sadb[0]);
	else
		tmp = REG_SET_FIELD(tmp, AZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER,
				    SPEAKER_ALLOCATION, 5); /* stereo */
	WREG32_AUDIO_ENDPT(dig->afmt->pin->offset,
			   ixAZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER, tmp);

	kfree(sadb);
}

static void dce_v11_0_audio_write_sad_regs(struct drm_encoder *encoder)
{
	struct amdgpu_device *adev = encoder->dev->dev_private;
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct amdgpu_encoder_atom_dig *dig = amdgpu_encoder->enc_priv;
	struct drm_connector *connector;
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

	list_for_each_entry(connector, &encoder->dev->mode_config.connector_list, head) {
		if (connector->encoder == encoder) {
			amdgpu_connector = to_amdgpu_connector(connector);
			break;
		}
	}

	if (!amdgpu_connector) {
		DRM_ERROR("Couldn't find encoder's connector\n");
		return;
	}

	sad_count = drm_edid_to_sad(amdgpu_connector_edid(connector), &sads);
	if (sad_count <= 0) {
		DRM_ERROR("Couldn't read SADs: %d\n", sad_count);
		return;
	}
	BUG_ON(!sads);

	for (i = 0; i < ARRAY_SIZE(eld_reg_to_type); i++) {
		u32 tmp = 0;
		u8 stereo_freqs = 0;
		int max_channels = -1;
		int j;

		for (j = 0; j < sad_count; j++) {
			struct cea_sad *sad = &sads[j];

			if (sad->format == eld_reg_to_type[i][1]) {
				if (sad->channels > max_channels) {
					tmp = REG_SET_FIELD(tmp, AZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR0,
							    MAX_CHANNELS, sad->channels);
					tmp = REG_SET_FIELD(tmp, AZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR0,
							    DESCRIPTOR_BYTE_2, sad->byte2);
					tmp = REG_SET_FIELD(tmp, AZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR0,
							    SUPPORTED_FREQUENCIES, sad->freq);
					max_channels = sad->channels;
				}

				if (sad->format == HDMI_AUDIO_CODING_TYPE_PCM)
					stereo_freqs |= sad->freq;
				else
					break;
			}
		}

		tmp = REG_SET_FIELD(tmp, AZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR0,
				    SUPPORTED_FREQUENCIES_STEREO, stereo_freqs);
		WREG32_AUDIO_ENDPT(dig->afmt->pin->offset, eld_reg_to_type[i][0], tmp);
	}

	kfree(sads);
}

static void dce_v11_0_audio_enable(struct amdgpu_device *adev,
				  struct amdgpu_audio_pin *pin,
				  bool enable)
{
	if (!pin)
		return;

	WREG32_AUDIO_ENDPT(pin->offset, ixAZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL,
			   enable ? AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL__AUDIO_ENABLED_MASK : 0);
}

static const u32 pin_offsets[] =
{
	AUD0_REGISTER_OFFSET,
	AUD1_REGISTER_OFFSET,
	AUD2_REGISTER_OFFSET,
	AUD3_REGISTER_OFFSET,
	AUD4_REGISTER_OFFSET,
	AUD5_REGISTER_OFFSET,
	AUD6_REGISTER_OFFSET,
};

static int dce_v11_0_audio_init(struct amdgpu_device *adev)
{
	int i;

	if (!amdgpu_audio)
		return 0;

	adev->mode_info.audio.enabled = true;

	adev->mode_info.audio.num_pins = 7;

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
		dce_v11_0_audio_enable(adev, &adev->mode_info.audio.pin[i], false);
	}

	return 0;
}

static void dce_v11_0_audio_fini(struct amdgpu_device *adev)
{
	int i;

	if (!adev->mode_info.audio.enabled)
		return;

	for (i = 0; i < adev->mode_info.audio.num_pins; i++)
		dce_v11_0_audio_enable(adev, &adev->mode_info.audio.pin[i], false);

	adev->mode_info.audio.enabled = false;
}

/*
 * update the N and CTS parameters for a given pixel clock rate
 */
static void dce_v11_0_afmt_update_ACR(struct drm_encoder *encoder, uint32_t clock)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_afmt_acr acr = amdgpu_afmt_acr(clock);
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct amdgpu_encoder_atom_dig *dig = amdgpu_encoder->enc_priv;
	u32 tmp;

	tmp = RREG32(mmHDMI_ACR_32_0 + dig->afmt->offset);
	tmp = REG_SET_FIELD(tmp, HDMI_ACR_32_0, HDMI_ACR_CTS_32, acr.cts_32khz);
	WREG32(mmHDMI_ACR_32_0 + dig->afmt->offset, tmp);
	tmp = RREG32(mmHDMI_ACR_32_1 + dig->afmt->offset);
	tmp = REG_SET_FIELD(tmp, HDMI_ACR_32_1, HDMI_ACR_N_32, acr.n_32khz);
	WREG32(mmHDMI_ACR_32_1 + dig->afmt->offset, tmp);

	tmp = RREG32(mmHDMI_ACR_44_0 + dig->afmt->offset);
	tmp = REG_SET_FIELD(tmp, HDMI_ACR_44_0, HDMI_ACR_CTS_44, acr.cts_44_1khz);
	WREG32(mmHDMI_ACR_44_0 + dig->afmt->offset, tmp);
	tmp = RREG32(mmHDMI_ACR_44_1 + dig->afmt->offset);
	tmp = REG_SET_FIELD(tmp, HDMI_ACR_44_1, HDMI_ACR_N_44, acr.n_44_1khz);
	WREG32(mmHDMI_ACR_44_1 + dig->afmt->offset, tmp);

	tmp = RREG32(mmHDMI_ACR_48_0 + dig->afmt->offset);
	tmp = REG_SET_FIELD(tmp, HDMI_ACR_48_0, HDMI_ACR_CTS_48, acr.cts_48khz);
	WREG32(mmHDMI_ACR_48_0 + dig->afmt->offset, tmp);
	tmp = RREG32(mmHDMI_ACR_48_1 + dig->afmt->offset);
	tmp = REG_SET_FIELD(tmp, HDMI_ACR_48_1, HDMI_ACR_N_48, acr.n_48khz);
	WREG32(mmHDMI_ACR_48_1 + dig->afmt->offset, tmp);

}

/*
 * build a HDMI Video Info Frame
 */
static void dce_v11_0_afmt_update_avi_infoframe(struct drm_encoder *encoder,
					       void *buffer, size_t size)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct amdgpu_encoder_atom_dig *dig = amdgpu_encoder->enc_priv;
	uint8_t *frame = buffer + 3;
	uint8_t *header = buffer;

	WREG32(mmAFMT_AVI_INFO0 + dig->afmt->offset,
		frame[0x0] | (frame[0x1] << 8) | (frame[0x2] << 16) | (frame[0x3] << 24));
	WREG32(mmAFMT_AVI_INFO1 + dig->afmt->offset,
		frame[0x4] | (frame[0x5] << 8) | (frame[0x6] << 16) | (frame[0x7] << 24));
	WREG32(mmAFMT_AVI_INFO2 + dig->afmt->offset,
		frame[0x8] | (frame[0x9] << 8) | (frame[0xA] << 16) | (frame[0xB] << 24));
	WREG32(mmAFMT_AVI_INFO3 + dig->afmt->offset,
		frame[0xC] | (frame[0xD] << 8) | (header[1] << 24));
}

static void dce_v11_0_audio_set_dto(struct drm_encoder *encoder, u32 clock)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct amdgpu_encoder_atom_dig *dig = amdgpu_encoder->enc_priv;
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(encoder->crtc);
	u32 dto_phase = 24 * 1000;
	u32 dto_modulo = clock;
	u32 tmp;

	if (!dig || !dig->afmt)
		return;

	/* XXX two dtos; generally use dto0 for hdmi */
	/* Express [24MHz / target pixel clock] as an exact rational
	 * number (coefficient of two integer numbers.  DCCG_AUDIO_DTOx_PHASE
	 * is the numerator, DCCG_AUDIO_DTOx_MODULE is the denominator
	 */
	tmp = RREG32(mmDCCG_AUDIO_DTO_SOURCE);
	tmp = REG_SET_FIELD(tmp, DCCG_AUDIO_DTO_SOURCE, DCCG_AUDIO_DTO0_SOURCE_SEL,
			    amdgpu_crtc->crtc_id);
	WREG32(mmDCCG_AUDIO_DTO_SOURCE, tmp);
	WREG32(mmDCCG_AUDIO_DTO0_PHASE, dto_phase);
	WREG32(mmDCCG_AUDIO_DTO0_MODULE, dto_modulo);
}

/*
 * update the info frames with the data from the current display mode
 */
static void dce_v11_0_afmt_setmode(struct drm_encoder *encoder,
				  struct drm_display_mode *mode)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct amdgpu_encoder_atom_dig *dig = amdgpu_encoder->enc_priv;
	struct drm_connector *connector = amdgpu_get_connector_for_encoder(encoder);
	u8 buffer[HDMI_INFOFRAME_HEADER_SIZE + HDMI_AVI_INFOFRAME_SIZE];
	struct hdmi_avi_infoframe frame;
	ssize_t err;
	u32 tmp;
	int bpc = 8;

	if (!dig || !dig->afmt)
		return;

	/* Silent, r600_hdmi_enable will raise WARN for us */
	if (!dig->afmt->enabled)
		return;

	/* hdmi deep color mode general control packets setup, if bpc > 8 */
	if (encoder->crtc) {
		struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(encoder->crtc);
		bpc = amdgpu_crtc->bpc;
	}

	/* disable audio prior to setting up hw */
	dig->afmt->pin = dce_v11_0_audio_get_pin(adev);
	dce_v11_0_audio_enable(adev, dig->afmt->pin, false);

	dce_v11_0_audio_set_dto(encoder, mode->clock);

	tmp = RREG32(mmHDMI_VBI_PACKET_CONTROL + dig->afmt->offset);
	tmp = REG_SET_FIELD(tmp, HDMI_VBI_PACKET_CONTROL, HDMI_NULL_SEND, 1);
	WREG32(mmHDMI_VBI_PACKET_CONTROL + dig->afmt->offset, tmp); /* send null packets when required */

	WREG32(mmAFMT_AUDIO_CRC_CONTROL + dig->afmt->offset, 0x1000);

	tmp = RREG32(mmHDMI_CONTROL + dig->afmt->offset);
	switch (bpc) {
	case 0:
	case 6:
	case 8:
	case 16:
	default:
		tmp = REG_SET_FIELD(tmp, HDMI_CONTROL, HDMI_DEEP_COLOR_ENABLE, 0);
		tmp = REG_SET_FIELD(tmp, HDMI_CONTROL, HDMI_DEEP_COLOR_DEPTH, 0);
		DRM_DEBUG("%s: Disabling hdmi deep color for %d bpc.\n",
			  connector->name, bpc);
		break;
	case 10:
		tmp = REG_SET_FIELD(tmp, HDMI_CONTROL, HDMI_DEEP_COLOR_ENABLE, 1);
		tmp = REG_SET_FIELD(tmp, HDMI_CONTROL, HDMI_DEEP_COLOR_DEPTH, 1);
		DRM_DEBUG("%s: Enabling hdmi deep color 30 for 10 bpc.\n",
			  connector->name);
		break;
	case 12:
		tmp = REG_SET_FIELD(tmp, HDMI_CONTROL, HDMI_DEEP_COLOR_ENABLE, 1);
		tmp = REG_SET_FIELD(tmp, HDMI_CONTROL, HDMI_DEEP_COLOR_DEPTH, 2);
		DRM_DEBUG("%s: Enabling hdmi deep color 36 for 12 bpc.\n",
			  connector->name);
		break;
	}
	WREG32(mmHDMI_CONTROL + dig->afmt->offset, tmp);

	tmp = RREG32(mmHDMI_VBI_PACKET_CONTROL + dig->afmt->offset);
	tmp = REG_SET_FIELD(tmp, HDMI_VBI_PACKET_CONTROL, HDMI_NULL_SEND, 1); /* send null packets when required */
	tmp = REG_SET_FIELD(tmp, HDMI_VBI_PACKET_CONTROL, HDMI_GC_SEND, 1); /* send general control packets */
	tmp = REG_SET_FIELD(tmp, HDMI_VBI_PACKET_CONTROL, HDMI_GC_CONT, 1); /* send general control packets every frame */
	WREG32(mmHDMI_VBI_PACKET_CONTROL + dig->afmt->offset, tmp);

	tmp = RREG32(mmHDMI_INFOFRAME_CONTROL0 + dig->afmt->offset);
	/* enable audio info frames (frames won't be set until audio is enabled) */
	tmp = REG_SET_FIELD(tmp, HDMI_INFOFRAME_CONTROL0, HDMI_AUDIO_INFO_SEND, 1);
	/* required for audio info values to be updated */
	tmp = REG_SET_FIELD(tmp, HDMI_INFOFRAME_CONTROL0, HDMI_AUDIO_INFO_CONT, 1);
	WREG32(mmHDMI_INFOFRAME_CONTROL0 + dig->afmt->offset, tmp);

	tmp = RREG32(mmAFMT_INFOFRAME_CONTROL0 + dig->afmt->offset);
	/* required for audio info values to be updated */
	tmp = REG_SET_FIELD(tmp, AFMT_INFOFRAME_CONTROL0, AFMT_AUDIO_INFO_UPDATE, 1);
	WREG32(mmAFMT_INFOFRAME_CONTROL0 + dig->afmt->offset, tmp);

	tmp = RREG32(mmHDMI_INFOFRAME_CONTROL1 + dig->afmt->offset);
	/* anything other than 0 */
	tmp = REG_SET_FIELD(tmp, HDMI_INFOFRAME_CONTROL1, HDMI_AUDIO_INFO_LINE, 2);
	WREG32(mmHDMI_INFOFRAME_CONTROL1 + dig->afmt->offset, tmp);

	WREG32(mmHDMI_GC + dig->afmt->offset, 0); /* unset HDMI_GC_AVMUTE */

	tmp = RREG32(mmHDMI_AUDIO_PACKET_CONTROL + dig->afmt->offset);
	/* set the default audio delay */
	tmp = REG_SET_FIELD(tmp, HDMI_AUDIO_PACKET_CONTROL, HDMI_AUDIO_DELAY_EN, 1);
	/* should be suffient for all audio modes and small enough for all hblanks */
	tmp = REG_SET_FIELD(tmp, HDMI_AUDIO_PACKET_CONTROL, HDMI_AUDIO_PACKETS_PER_LINE, 3);
	WREG32(mmHDMI_AUDIO_PACKET_CONTROL + dig->afmt->offset, tmp);

	tmp = RREG32(mmAFMT_AUDIO_PACKET_CONTROL + dig->afmt->offset);
	/* allow 60958 channel status fields to be updated */
	tmp = REG_SET_FIELD(tmp, AFMT_AUDIO_PACKET_CONTROL, AFMT_60958_CS_UPDATE, 1);
	WREG32(mmAFMT_AUDIO_PACKET_CONTROL + dig->afmt->offset, tmp);

	tmp = RREG32(mmHDMI_ACR_PACKET_CONTROL + dig->afmt->offset);
	if (bpc > 8)
		/* clear SW CTS value */
		tmp = REG_SET_FIELD(tmp, HDMI_ACR_PACKET_CONTROL, HDMI_ACR_SOURCE, 0);
	else
		/* select SW CTS value */
		tmp = REG_SET_FIELD(tmp, HDMI_ACR_PACKET_CONTROL, HDMI_ACR_SOURCE, 1);
	/* allow hw to sent ACR packets when required */
	tmp = REG_SET_FIELD(tmp, HDMI_ACR_PACKET_CONTROL, HDMI_ACR_AUTO_SEND, 1);
	WREG32(mmHDMI_ACR_PACKET_CONTROL + dig->afmt->offset, tmp);

	dce_v11_0_afmt_update_ACR(encoder, mode->clock);

	tmp = RREG32(mmAFMT_60958_0 + dig->afmt->offset);
	tmp = REG_SET_FIELD(tmp, AFMT_60958_0, AFMT_60958_CS_CHANNEL_NUMBER_L, 1);
	WREG32(mmAFMT_60958_0 + dig->afmt->offset, tmp);

	tmp = RREG32(mmAFMT_60958_1 + dig->afmt->offset);
	tmp = REG_SET_FIELD(tmp, AFMT_60958_1, AFMT_60958_CS_CHANNEL_NUMBER_R, 2);
	WREG32(mmAFMT_60958_1 + dig->afmt->offset, tmp);

	tmp = RREG32(mmAFMT_60958_2 + dig->afmt->offset);
	tmp = REG_SET_FIELD(tmp, AFMT_60958_2, AFMT_60958_CS_CHANNEL_NUMBER_2, 3);
	tmp = REG_SET_FIELD(tmp, AFMT_60958_2, AFMT_60958_CS_CHANNEL_NUMBER_3, 4);
	tmp = REG_SET_FIELD(tmp, AFMT_60958_2, AFMT_60958_CS_CHANNEL_NUMBER_4, 5);
	tmp = REG_SET_FIELD(tmp, AFMT_60958_2, AFMT_60958_CS_CHANNEL_NUMBER_5, 6);
	tmp = REG_SET_FIELD(tmp, AFMT_60958_2, AFMT_60958_CS_CHANNEL_NUMBER_6, 7);
	tmp = REG_SET_FIELD(tmp, AFMT_60958_2, AFMT_60958_CS_CHANNEL_NUMBER_7, 8);
	WREG32(mmAFMT_60958_2 + dig->afmt->offset, tmp);

	dce_v11_0_audio_write_speaker_allocation(encoder);

	WREG32(mmAFMT_AUDIO_PACKET_CONTROL2 + dig->afmt->offset,
	       (0xff << AFMT_AUDIO_PACKET_CONTROL2__AFMT_AUDIO_CHANNEL_ENABLE__SHIFT));

	dce_v11_0_afmt_audio_select_pin(encoder);
	dce_v11_0_audio_write_sad_regs(encoder);
	dce_v11_0_audio_write_latency_fields(encoder, mode);

	err = drm_hdmi_avi_infoframe_from_display_mode(&frame, mode);
	if (err < 0) {
		DRM_ERROR("failed to setup AVI infoframe: %zd\n", err);
		return;
	}

	err = hdmi_avi_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (err < 0) {
		DRM_ERROR("failed to pack AVI infoframe: %zd\n", err);
		return;
	}

	dce_v11_0_afmt_update_avi_infoframe(encoder, buffer, sizeof(buffer));

	tmp = RREG32(mmHDMI_INFOFRAME_CONTROL0 + dig->afmt->offset);
	/* enable AVI info frames */
	tmp = REG_SET_FIELD(tmp, HDMI_INFOFRAME_CONTROL0, HDMI_AVI_INFO_SEND, 1);
	/* required for audio info values to be updated */
	tmp = REG_SET_FIELD(tmp, HDMI_INFOFRAME_CONTROL0, HDMI_AVI_INFO_CONT, 1);
	WREG32(mmHDMI_INFOFRAME_CONTROL0 + dig->afmt->offset, tmp);

	tmp = RREG32(mmHDMI_INFOFRAME_CONTROL1 + dig->afmt->offset);
	tmp = REG_SET_FIELD(tmp, HDMI_INFOFRAME_CONTROL1, HDMI_AVI_INFO_LINE, 2);
	WREG32(mmHDMI_INFOFRAME_CONTROL1 + dig->afmt->offset, tmp);

	tmp = RREG32(mmAFMT_AUDIO_PACKET_CONTROL + dig->afmt->offset);
	/* send audio packets */
	tmp = REG_SET_FIELD(tmp, AFMT_AUDIO_PACKET_CONTROL, AFMT_AUDIO_SAMPLE_SEND, 1);
	WREG32(mmAFMT_AUDIO_PACKET_CONTROL + dig->afmt->offset, tmp);

	WREG32(mmAFMT_RAMP_CONTROL0 + dig->afmt->offset, 0x00FFFFFF);
	WREG32(mmAFMT_RAMP_CONTROL1 + dig->afmt->offset, 0x007FFFFF);
	WREG32(mmAFMT_RAMP_CONTROL2 + dig->afmt->offset, 0x00000001);
	WREG32(mmAFMT_RAMP_CONTROL3 + dig->afmt->offset, 0x00000001);

	/* enable audio after to setting up hw */
	dce_v11_0_audio_enable(adev, dig->afmt->pin, true);
}

static void dce_v11_0_afmt_enable(struct drm_encoder *encoder, bool enable)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_device *adev = dev->dev_private;
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
		dce_v11_0_audio_enable(adev, dig->afmt->pin, false);
		dig->afmt->pin = NULL;
	}

	dig->afmt->enabled = enable;

	DRM_DEBUG("%sabling AFMT interface @ 0x%04X for encoder 0x%x\n",
		  enable ? "En" : "Dis", dig->afmt->offset, amdgpu_encoder->encoder_id);
}

static void dce_v11_0_afmt_init(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->mode_info.num_dig; i++)
		adev->mode_info.afmt[i] = NULL;

	/* DCE11 has audio blocks tied to DIG encoders */
	for (i = 0; i < adev->mode_info.num_dig; i++) {
		adev->mode_info.afmt[i] = kzalloc(sizeof(struct amdgpu_afmt), GFP_KERNEL);
		if (adev->mode_info.afmt[i]) {
			adev->mode_info.afmt[i]->offset = dig_offsets[i];
			adev->mode_info.afmt[i]->id = i;
		}
	}
}

static void dce_v11_0_afmt_fini(struct amdgpu_device *adev)
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

static void dce_v11_0_vga_enable(struct drm_crtc *crtc, bool enable)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct amdgpu_device *adev = dev->dev_private;
	u32 vga_control;

	vga_control = RREG32(vga_control_regs[amdgpu_crtc->crtc_id]) & ~1;
	if (enable)
		WREG32(vga_control_regs[amdgpu_crtc->crtc_id], vga_control | 1);
	else
		WREG32(vga_control_regs[amdgpu_crtc->crtc_id], vga_control);
}

static void dce_v11_0_grph_enable(struct drm_crtc *crtc, bool enable)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct amdgpu_device *adev = dev->dev_private;

	if (enable)
		WREG32(mmGRPH_ENABLE + amdgpu_crtc->crtc_offset, 1);
	else
		WREG32(mmGRPH_ENABLE + amdgpu_crtc->crtc_offset, 0);
}

static int dce_v11_0_crtc_do_set_base(struct drm_crtc *crtc,
				     struct drm_framebuffer *fb,
				     int x, int y, int atomic)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_framebuffer *amdgpu_fb;
	struct drm_framebuffer *target_fb;
	struct drm_gem_object *obj;
	struct amdgpu_bo *rbo;
	uint64_t fb_location, tiling_flags;
	uint32_t fb_format, fb_pitch_pixels;
	u32 fb_swap = REG_SET_FIELD(0, GRPH_SWAP_CNTL, GRPH_ENDIAN_SWAP, ENDIAN_NONE);
	u32 pipe_config;
	u32 tmp, viewport_w, viewport_h;
	int r;
	bool bypass_lut = false;

	/* no fb bound */
	if (!atomic && !crtc->primary->fb) {
		DRM_DEBUG_KMS("No FB bound\n");
		return 0;
	}

	if (atomic) {
		amdgpu_fb = to_amdgpu_framebuffer(fb);
		target_fb = fb;
	}
	else {
		amdgpu_fb = to_amdgpu_framebuffer(crtc->primary->fb);
		target_fb = crtc->primary->fb;
	}

	/* If atomic, assume fb object is pinned & idle & fenced and
	 * just update base pointers
	 */
	obj = amdgpu_fb->obj;
	rbo = gem_to_amdgpu_bo(obj);
	r = amdgpu_bo_reserve(rbo, false);
	if (unlikely(r != 0))
		return r;

	if (atomic)
		fb_location = amdgpu_bo_gpu_offset(rbo);
	else {
		r = amdgpu_bo_pin(rbo, AMDGPU_GEM_DOMAIN_VRAM, &fb_location);
		if (unlikely(r != 0)) {
			amdgpu_bo_unreserve(rbo);
			return -EINVAL;
		}
	}

	amdgpu_bo_get_tiling_flags(rbo, &tiling_flags);
	amdgpu_bo_unreserve(rbo);

	pipe_config = AMDGPU_TILING_GET(tiling_flags, PIPE_CONFIG);

	switch (target_fb->pixel_format) {
	case DRM_FORMAT_C8:
		fb_format = REG_SET_FIELD(0, GRPH_CONTROL, GRPH_DEPTH, 0);
		fb_format = REG_SET_FIELD(fb_format, GRPH_CONTROL, GRPH_FORMAT, 0);
		break;
	case DRM_FORMAT_XRGB4444:
	case DRM_FORMAT_ARGB4444:
		fb_format = REG_SET_FIELD(0, GRPH_CONTROL, GRPH_DEPTH, 1);
		fb_format = REG_SET_FIELD(fb_format, GRPH_CONTROL, GRPH_FORMAT, 2);
#ifdef __BIG_ENDIAN
		fb_swap = REG_SET_FIELD(fb_swap, GRPH_SWAP_CNTL, GRPH_ENDIAN_SWAP,
					ENDIAN_8IN16);
#endif
		break;
	case DRM_FORMAT_XRGB1555:
	case DRM_FORMAT_ARGB1555:
		fb_format = REG_SET_FIELD(0, GRPH_CONTROL, GRPH_DEPTH, 1);
		fb_format = REG_SET_FIELD(fb_format, GRPH_CONTROL, GRPH_FORMAT, 0);
#ifdef __BIG_ENDIAN
		fb_swap = REG_SET_FIELD(fb_swap, GRPH_SWAP_CNTL, GRPH_ENDIAN_SWAP,
					ENDIAN_8IN16);
#endif
		break;
	case DRM_FORMAT_BGRX5551:
	case DRM_FORMAT_BGRA5551:
		fb_format = REG_SET_FIELD(0, GRPH_CONTROL, GRPH_DEPTH, 1);
		fb_format = REG_SET_FIELD(fb_format, GRPH_CONTROL, GRPH_FORMAT, 5);
#ifdef __BIG_ENDIAN
		fb_swap = REG_SET_FIELD(fb_swap, GRPH_SWAP_CNTL, GRPH_ENDIAN_SWAP,
					ENDIAN_8IN16);
#endif
		break;
	case DRM_FORMAT_RGB565:
		fb_format = REG_SET_FIELD(0, GRPH_CONTROL, GRPH_DEPTH, 1);
		fb_format = REG_SET_FIELD(fb_format, GRPH_CONTROL, GRPH_FORMAT, 1);
#ifdef __BIG_ENDIAN
		fb_swap = REG_SET_FIELD(fb_swap, GRPH_SWAP_CNTL, GRPH_ENDIAN_SWAP,
					ENDIAN_8IN16);
#endif
		break;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		fb_format = REG_SET_FIELD(0, GRPH_CONTROL, GRPH_DEPTH, 2);
		fb_format = REG_SET_FIELD(fb_format, GRPH_CONTROL, GRPH_FORMAT, 0);
#ifdef __BIG_ENDIAN
		fb_swap = REG_SET_FIELD(fb_swap, GRPH_SWAP_CNTL, GRPH_ENDIAN_SWAP,
					ENDIAN_8IN32);
#endif
		break;
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_ARGB2101010:
		fb_format = REG_SET_FIELD(0, GRPH_CONTROL, GRPH_DEPTH, 2);
		fb_format = REG_SET_FIELD(fb_format, GRPH_CONTROL, GRPH_FORMAT, 1);
#ifdef __BIG_ENDIAN
		fb_swap = REG_SET_FIELD(fb_swap, GRPH_SWAP_CNTL, GRPH_ENDIAN_SWAP,
					ENDIAN_8IN32);
#endif
		/* Greater 8 bpc fb needs to bypass hw-lut to retain precision */
		bypass_lut = true;
		break;
	case DRM_FORMAT_BGRX1010102:
	case DRM_FORMAT_BGRA1010102:
		fb_format = REG_SET_FIELD(0, GRPH_CONTROL, GRPH_DEPTH, 2);
		fb_format = REG_SET_FIELD(fb_format, GRPH_CONTROL, GRPH_FORMAT, 4);
#ifdef __BIG_ENDIAN
		fb_swap = REG_SET_FIELD(fb_swap, GRPH_SWAP_CNTL, GRPH_ENDIAN_SWAP,
					ENDIAN_8IN32);
#endif
		/* Greater 8 bpc fb needs to bypass hw-lut to retain precision */
		bypass_lut = true;
		break;
	default:
		DRM_ERROR("Unsupported screen format %s\n",
			drm_get_format_name(target_fb->pixel_format));
		return -EINVAL;
	}

	if (AMDGPU_TILING_GET(tiling_flags, ARRAY_MODE) == ARRAY_2D_TILED_THIN1) {
		unsigned bankw, bankh, mtaspect, tile_split, num_banks;

		bankw = AMDGPU_TILING_GET(tiling_flags, BANK_WIDTH);
		bankh = AMDGPU_TILING_GET(tiling_flags, BANK_HEIGHT);
		mtaspect = AMDGPU_TILING_GET(tiling_flags, MACRO_TILE_ASPECT);
		tile_split = AMDGPU_TILING_GET(tiling_flags, TILE_SPLIT);
		num_banks = AMDGPU_TILING_GET(tiling_flags, NUM_BANKS);

		fb_format = REG_SET_FIELD(fb_format, GRPH_CONTROL, GRPH_NUM_BANKS, num_banks);
		fb_format = REG_SET_FIELD(fb_format, GRPH_CONTROL, GRPH_ARRAY_MODE,
					  ARRAY_2D_TILED_THIN1);
		fb_format = REG_SET_FIELD(fb_format, GRPH_CONTROL, GRPH_TILE_SPLIT,
					  tile_split);
		fb_format = REG_SET_FIELD(fb_format, GRPH_CONTROL, GRPH_BANK_WIDTH, bankw);
		fb_format = REG_SET_FIELD(fb_format, GRPH_CONTROL, GRPH_BANK_HEIGHT, bankh);
		fb_format = REG_SET_FIELD(fb_format, GRPH_CONTROL, GRPH_MACRO_TILE_ASPECT,
					  mtaspect);
		fb_format = REG_SET_FIELD(fb_format, GRPH_CONTROL, GRPH_MICRO_TILE_MODE,
					  ADDR_SURF_MICRO_TILING_DISPLAY);
	} else if (AMDGPU_TILING_GET(tiling_flags, ARRAY_MODE) == ARRAY_1D_TILED_THIN1) {
		fb_format = REG_SET_FIELD(fb_format, GRPH_CONTROL, GRPH_ARRAY_MODE,
					  ARRAY_1D_TILED_THIN1);
	}

	fb_format = REG_SET_FIELD(fb_format, GRPH_CONTROL, GRPH_PIPE_CONFIG,
				  pipe_config);

	dce_v11_0_vga_enable(crtc, false);

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
	tmp = RREG32(mmGRPH_LUT_10BIT_BYPASS + amdgpu_crtc->crtc_offset);
	if (bypass_lut)
		tmp = REG_SET_FIELD(tmp, GRPH_LUT_10BIT_BYPASS, GRPH_LUT_10BIT_BYPASS_EN, 1);
	else
		tmp = REG_SET_FIELD(tmp, GRPH_LUT_10BIT_BYPASS, GRPH_LUT_10BIT_BYPASS_EN, 0);
	WREG32(mmGRPH_LUT_10BIT_BYPASS + amdgpu_crtc->crtc_offset, tmp);

	if (bypass_lut)
		DRM_DEBUG_KMS("Bypassing hardware LUT due to 10 bit fb scanout.\n");

	WREG32(mmGRPH_SURFACE_OFFSET_X + amdgpu_crtc->crtc_offset, 0);
	WREG32(mmGRPH_SURFACE_OFFSET_Y + amdgpu_crtc->crtc_offset, 0);
	WREG32(mmGRPH_X_START + amdgpu_crtc->crtc_offset, 0);
	WREG32(mmGRPH_Y_START + amdgpu_crtc->crtc_offset, 0);
	WREG32(mmGRPH_X_END + amdgpu_crtc->crtc_offset, target_fb->width);
	WREG32(mmGRPH_Y_END + amdgpu_crtc->crtc_offset, target_fb->height);

	fb_pitch_pixels = target_fb->pitches[0] / (target_fb->bits_per_pixel / 8);
	WREG32(mmGRPH_PITCH + amdgpu_crtc->crtc_offset, fb_pitch_pixels);

	dce_v11_0_grph_enable(crtc, true);

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

	/* pageflip setup */
	/* make sure flip is at vb rather than hb */
	tmp = RREG32(mmGRPH_FLIP_CONTROL + amdgpu_crtc->crtc_offset);
	tmp = REG_SET_FIELD(tmp, GRPH_FLIP_CONTROL,
			    GRPH_SURFACE_UPDATE_H_RETRACE_EN, 0);
	WREG32(mmGRPH_FLIP_CONTROL + amdgpu_crtc->crtc_offset, tmp);

	/* set pageflip to happen only at start of vblank interval (front porch) */
	WREG32(mmCRTC_MASTER_UPDATE_MODE + amdgpu_crtc->crtc_offset, 3);

	if (!atomic && fb && fb != crtc->primary->fb) {
		amdgpu_fb = to_amdgpu_framebuffer(fb);
		rbo = gem_to_amdgpu_bo(amdgpu_fb->obj);
		r = amdgpu_bo_reserve(rbo, false);
		if (unlikely(r != 0))
			return r;
		amdgpu_bo_unpin(rbo);
		amdgpu_bo_unreserve(rbo);
	}

	/* Bytes per pixel may have changed */
	dce_v11_0_bandwidth_update(adev);

	return 0;
}

static void dce_v11_0_set_interleave(struct drm_crtc *crtc,
				     struct drm_display_mode *mode)
{
	struct drm_device *dev = crtc->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	u32 tmp;

	tmp = RREG32(mmLB_DATA_FORMAT + amdgpu_crtc->crtc_offset);
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		tmp = REG_SET_FIELD(tmp, LB_DATA_FORMAT, INTERLEAVE_EN, 1);
	else
		tmp = REG_SET_FIELD(tmp, LB_DATA_FORMAT, INTERLEAVE_EN, 0);
	WREG32(mmLB_DATA_FORMAT + amdgpu_crtc->crtc_offset, tmp);
}

static void dce_v11_0_crtc_load_lut(struct drm_crtc *crtc)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct amdgpu_device *adev = dev->dev_private;
	int i;
	u32 tmp;

	DRM_DEBUG_KMS("%d\n", amdgpu_crtc->crtc_id);

	tmp = RREG32(mmINPUT_CSC_CONTROL + amdgpu_crtc->crtc_offset);
	tmp = REG_SET_FIELD(tmp, INPUT_CSC_CONTROL, INPUT_CSC_GRPH_MODE, 0);
	WREG32(mmINPUT_CSC_CONTROL + amdgpu_crtc->crtc_offset, tmp);

	tmp = RREG32(mmPRESCALE_GRPH_CONTROL + amdgpu_crtc->crtc_offset);
	tmp = REG_SET_FIELD(tmp, PRESCALE_GRPH_CONTROL, GRPH_PRESCALE_BYPASS, 1);
	WREG32(mmPRESCALE_GRPH_CONTROL + amdgpu_crtc->crtc_offset, tmp);

	tmp = RREG32(mmINPUT_GAMMA_CONTROL + amdgpu_crtc->crtc_offset);
	tmp = REG_SET_FIELD(tmp, INPUT_GAMMA_CONTROL, GRPH_INPUT_GAMMA_MODE, 0);
	WREG32(mmINPUT_GAMMA_CONTROL + amdgpu_crtc->crtc_offset, tmp);

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
	for (i = 0; i < 256; i++) {
		WREG32(mmDC_LUT_30_COLOR + amdgpu_crtc->crtc_offset,
		       (amdgpu_crtc->lut_r[i] << 20) |
		       (amdgpu_crtc->lut_g[i] << 10) |
		       (amdgpu_crtc->lut_b[i] << 0));
	}

	tmp = RREG32(mmDEGAMMA_CONTROL + amdgpu_crtc->crtc_offset);
	tmp = REG_SET_FIELD(tmp, DEGAMMA_CONTROL, GRPH_DEGAMMA_MODE, 0);
	tmp = REG_SET_FIELD(tmp, DEGAMMA_CONTROL, CURSOR_DEGAMMA_MODE, 0);
	tmp = REG_SET_FIELD(tmp, DEGAMMA_CONTROL, CURSOR2_DEGAMMA_MODE, 0);
	WREG32(mmDEGAMMA_CONTROL + amdgpu_crtc->crtc_offset, tmp);

	tmp = RREG32(mmGAMUT_REMAP_CONTROL + amdgpu_crtc->crtc_offset);
	tmp = REG_SET_FIELD(tmp, GAMUT_REMAP_CONTROL, GRPH_GAMUT_REMAP_MODE, 0);
	WREG32(mmGAMUT_REMAP_CONTROL + amdgpu_crtc->crtc_offset, tmp);

	tmp = RREG32(mmREGAMMA_CONTROL + amdgpu_crtc->crtc_offset);
	tmp = REG_SET_FIELD(tmp, REGAMMA_CONTROL, GRPH_REGAMMA_MODE, 0);
	WREG32(mmREGAMMA_CONTROL + amdgpu_crtc->crtc_offset, tmp);

	tmp = RREG32(mmOUTPUT_CSC_CONTROL + amdgpu_crtc->crtc_offset);
	tmp = REG_SET_FIELD(tmp, OUTPUT_CSC_CONTROL, OUTPUT_CSC_GRPH_MODE, 0);
	WREG32(mmOUTPUT_CSC_CONTROL + amdgpu_crtc->crtc_offset, tmp);

	/* XXX match this to the depth of the crtc fmt block, move to modeset? */
	WREG32(mmDENORM_CONTROL + amdgpu_crtc->crtc_offset, 0);
	/* XXX this only needs to be programmed once per crtc at startup,
	 * not sure where the best place for it is
	 */
	tmp = RREG32(mmALPHA_CONTROL + amdgpu_crtc->crtc_offset);
	tmp = REG_SET_FIELD(tmp, ALPHA_CONTROL, CURSOR_ALPHA_BLND_ENA, 1);
	WREG32(mmALPHA_CONTROL + amdgpu_crtc->crtc_offset, tmp);
}

static int dce_v11_0_pick_dig_encoder(struct drm_encoder *encoder)
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
 * dce_v11_0_pick_pll - Allocate a PPLL for use by the crtc.
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
 * DCE 10.x
 * Tonga
 * - PPLL1, PPLL2 are available for all UNIPHY (both DP and non-DP)
 * CI
 * - PPLL0, PPLL1, PPLL2 are available for all UNIPHY (both DP and non-DP) and DAC
 *
 */
static u32 dce_v11_0_pick_pll(struct drm_crtc *crtc)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct amdgpu_device *adev = dev->dev_private;
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

	/* XXX need to determine what plls are available on each DCE11 part */
	pll_in_use = amdgpu_pll_get_use_mask(crtc);
	if (adev->asic_type == CHIP_CARRIZO || adev->asic_type == CHIP_STONEY) {
		if (!(pll_in_use & (1 << ATOM_PPLL1)))
			return ATOM_PPLL1;
		if (!(pll_in_use & (1 << ATOM_PPLL0)))
			return ATOM_PPLL0;
		DRM_ERROR("unable to allocate a PPLL\n");
		return ATOM_PPLL_INVALID;
	} else {
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

static void dce_v11_0_lock_cursor(struct drm_crtc *crtc, bool lock)
{
	struct amdgpu_device *adev = crtc->dev->dev_private;
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	uint32_t cur_lock;

	cur_lock = RREG32(mmCUR_UPDATE + amdgpu_crtc->crtc_offset);
	if (lock)
		cur_lock = REG_SET_FIELD(cur_lock, CUR_UPDATE, CURSOR_UPDATE_LOCK, 1);
	else
		cur_lock = REG_SET_FIELD(cur_lock, CUR_UPDATE, CURSOR_UPDATE_LOCK, 0);
	WREG32(mmCUR_UPDATE + amdgpu_crtc->crtc_offset, cur_lock);
}

static void dce_v11_0_hide_cursor(struct drm_crtc *crtc)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct amdgpu_device *adev = crtc->dev->dev_private;
	u32 tmp;

	tmp = RREG32_IDX(mmCUR_CONTROL + amdgpu_crtc->crtc_offset);
	tmp = REG_SET_FIELD(tmp, CUR_CONTROL, CURSOR_EN, 0);
	WREG32_IDX(mmCUR_CONTROL + amdgpu_crtc->crtc_offset, tmp);
}

static void dce_v11_0_show_cursor(struct drm_crtc *crtc)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct amdgpu_device *adev = crtc->dev->dev_private;
	u32 tmp;

	WREG32(mmCUR_SURFACE_ADDRESS_HIGH + amdgpu_crtc->crtc_offset,
	       upper_32_bits(amdgpu_crtc->cursor_addr));
	WREG32(mmCUR_SURFACE_ADDRESS + amdgpu_crtc->crtc_offset,
	       lower_32_bits(amdgpu_crtc->cursor_addr));

	tmp = RREG32_IDX(mmCUR_CONTROL + amdgpu_crtc->crtc_offset);
	tmp = REG_SET_FIELD(tmp, CUR_CONTROL, CURSOR_EN, 1);
	tmp = REG_SET_FIELD(tmp, CUR_CONTROL, CURSOR_MODE, 2);
	WREG32_IDX(mmCUR_CONTROL + amdgpu_crtc->crtc_offset, tmp);
}

static int dce_v11_0_cursor_move_locked(struct drm_crtc *crtc,
					int x, int y)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct amdgpu_device *adev = crtc->dev->dev_private;
	int xorigin = 0, yorigin = 0;

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

	amdgpu_crtc->cursor_x = x;
	amdgpu_crtc->cursor_y = y;

	return 0;
}

static int dce_v11_0_crtc_cursor_move(struct drm_crtc *crtc,
				      int x, int y)
{
	int ret;

	dce_v11_0_lock_cursor(crtc, true);
	ret = dce_v11_0_cursor_move_locked(crtc, x, y);
	dce_v11_0_lock_cursor(crtc, false);

	return ret;
}

static int dce_v11_0_crtc_cursor_set2(struct drm_crtc *crtc,
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
		dce_v11_0_hide_cursor(crtc);
		obj = NULL;
		goto unpin;
	}

	if ((width > amdgpu_crtc->max_cursor_width) ||
	    (height > amdgpu_crtc->max_cursor_height)) {
		DRM_ERROR("bad cursor width or height %d x %d\n", width, height);
		return -EINVAL;
	}

	obj = drm_gem_object_lookup(crtc->dev, file_priv, handle);
	if (!obj) {
		DRM_ERROR("Cannot find cursor object %x for crtc %d\n", handle, amdgpu_crtc->crtc_id);
		return -ENOENT;
	}

	aobj = gem_to_amdgpu_bo(obj);
	ret = amdgpu_bo_reserve(aobj, false);
	if (ret != 0) {
		drm_gem_object_unreference_unlocked(obj);
		return ret;
	}

	ret = amdgpu_bo_pin(aobj, AMDGPU_GEM_DOMAIN_VRAM, &amdgpu_crtc->cursor_addr);
	amdgpu_bo_unreserve(aobj);
	if (ret) {
		DRM_ERROR("Failed to pin new cursor BO (%d)\n", ret);
		drm_gem_object_unreference_unlocked(obj);
		return ret;
	}

	amdgpu_crtc->cursor_width = width;
	amdgpu_crtc->cursor_height = height;

	dce_v11_0_lock_cursor(crtc, true);

	if (hot_x != amdgpu_crtc->cursor_hot_x ||
	    hot_y != amdgpu_crtc->cursor_hot_y) {
		int x, y;

		x = amdgpu_crtc->cursor_x + amdgpu_crtc->cursor_hot_x - hot_x;
		y = amdgpu_crtc->cursor_y + amdgpu_crtc->cursor_hot_y - hot_y;

		dce_v11_0_cursor_move_locked(crtc, x, y);

		amdgpu_crtc->cursor_hot_x = hot_x;
		amdgpu_crtc->cursor_hot_y = hot_y;
	}

	dce_v11_0_show_cursor(crtc);
	dce_v11_0_lock_cursor(crtc, false);

unpin:
	if (amdgpu_crtc->cursor_bo) {
		struct amdgpu_bo *aobj = gem_to_amdgpu_bo(amdgpu_crtc->cursor_bo);
		ret = amdgpu_bo_reserve(aobj, false);
		if (likely(ret == 0)) {
			amdgpu_bo_unpin(aobj);
			amdgpu_bo_unreserve(aobj);
		}
		drm_gem_object_unreference_unlocked(amdgpu_crtc->cursor_bo);
	}

	amdgpu_crtc->cursor_bo = obj;
	return 0;
}

static void dce_v11_0_cursor_reset(struct drm_crtc *crtc)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);

	if (amdgpu_crtc->cursor_bo) {
		dce_v11_0_lock_cursor(crtc, true);

		dce_v11_0_cursor_move_locked(crtc, amdgpu_crtc->cursor_x,
					     amdgpu_crtc->cursor_y);

		dce_v11_0_show_cursor(crtc);

		dce_v11_0_lock_cursor(crtc, false);
	}
}

static void dce_v11_0_crtc_gamma_set(struct drm_crtc *crtc, u16 *red, u16 *green,
				    u16 *blue, uint32_t start, uint32_t size)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	int end = (start + size > 256) ? 256 : start + size, i;

	/* userspace palettes are always correct as is */
	for (i = start; i < end; i++) {
		amdgpu_crtc->lut_r[i] = red[i] >> 6;
		amdgpu_crtc->lut_g[i] = green[i] >> 6;
		amdgpu_crtc->lut_b[i] = blue[i] >> 6;
	}
	dce_v11_0_crtc_load_lut(crtc);
}

static void dce_v11_0_crtc_destroy(struct drm_crtc *crtc)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);

	drm_crtc_cleanup(crtc);
	destroy_workqueue(amdgpu_crtc->pflip_queue);
	kfree(amdgpu_crtc);
}

static const struct drm_crtc_funcs dce_v11_0_crtc_funcs = {
	.cursor_set2 = dce_v11_0_crtc_cursor_set2,
	.cursor_move = dce_v11_0_crtc_cursor_move,
	.gamma_set = dce_v11_0_crtc_gamma_set,
	.set_config = amdgpu_crtc_set_config,
	.destroy = dce_v11_0_crtc_destroy,
	.page_flip = amdgpu_crtc_page_flip,
};

static void dce_v11_0_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct drm_device *dev = crtc->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	unsigned type;

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		amdgpu_crtc->enabled = true;
		amdgpu_atombios_crtc_enable(crtc, ATOM_ENABLE);
		dce_v11_0_vga_enable(crtc, true);
		amdgpu_atombios_crtc_blank(crtc, ATOM_DISABLE);
		dce_v11_0_vga_enable(crtc, false);
		/* Make sure VBLANK and PFLIP interrupts are still enabled */
		type = amdgpu_crtc_idx_to_irq_type(adev, amdgpu_crtc->crtc_id);
		amdgpu_irq_update(adev, &adev->crtc_irq, type);
		amdgpu_irq_update(adev, &adev->pageflip_irq, type);
		drm_vblank_post_modeset(dev, amdgpu_crtc->crtc_id);
		dce_v11_0_crtc_load_lut(crtc);
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		drm_vblank_pre_modeset(dev, amdgpu_crtc->crtc_id);
		if (amdgpu_crtc->enabled) {
			dce_v11_0_vga_enable(crtc, true);
			amdgpu_atombios_crtc_blank(crtc, ATOM_ENABLE);
			dce_v11_0_vga_enable(crtc, false);
		}
		amdgpu_atombios_crtc_enable(crtc, ATOM_DISABLE);
		amdgpu_crtc->enabled = false;
		break;
	}
	/* adjust pm to dpms */
	amdgpu_pm_compute_clocks(adev);
}

static void dce_v11_0_crtc_prepare(struct drm_crtc *crtc)
{
	/* disable crtc pair power gating before programming */
	amdgpu_atombios_crtc_powergate(crtc, ATOM_DISABLE);
	amdgpu_atombios_crtc_lock(crtc, ATOM_ENABLE);
	dce_v11_0_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
}

static void dce_v11_0_crtc_commit(struct drm_crtc *crtc)
{
	dce_v11_0_crtc_dpms(crtc, DRM_MODE_DPMS_ON);
	amdgpu_atombios_crtc_lock(crtc, ATOM_DISABLE);
}

static void dce_v11_0_crtc_disable(struct drm_crtc *crtc)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_atom_ss ss;
	int i;

	dce_v11_0_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
	if (crtc->primary->fb) {
		int r;
		struct amdgpu_framebuffer *amdgpu_fb;
		struct amdgpu_bo *rbo;

		amdgpu_fb = to_amdgpu_framebuffer(crtc->primary->fb);
		rbo = gem_to_amdgpu_bo(amdgpu_fb->obj);
		r = amdgpu_bo_reserve(rbo, false);
		if (unlikely(r))
			DRM_ERROR("failed to reserve rbo before unpin\n");
		else {
			amdgpu_bo_unpin(rbo);
			amdgpu_bo_unreserve(rbo);
		}
	}
	/* disable the GRPH */
	dce_v11_0_grph_enable(crtc, false);

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
	case ATOM_PPLL0:
	case ATOM_PPLL1:
	case ATOM_PPLL2:
		/* disable the ppll */
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

static int dce_v11_0_crtc_mode_set(struct drm_crtc *crtc,
				  struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode,
				  int x, int y, struct drm_framebuffer *old_fb)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);

	if (!amdgpu_crtc->adjusted_clock)
		return -EINVAL;

	amdgpu_atombios_crtc_set_pll(crtc, adjusted_mode);
	amdgpu_atombios_crtc_set_dtd_timing(crtc, adjusted_mode);
	dce_v11_0_crtc_do_set_base(crtc, old_fb, x, y, 0);
	amdgpu_atombios_crtc_overscan_setup(crtc, mode, adjusted_mode);
	amdgpu_atombios_crtc_scaler_setup(crtc);
	dce_v11_0_cursor_reset(crtc);
	/* update the hw version fpr dpm */
	amdgpu_crtc->hw_mode = *adjusted_mode;

	return 0;
}

static bool dce_v11_0_crtc_mode_fixup(struct drm_crtc *crtc,
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
	if (!amdgpu_crtc_scaling_mode_fixup(crtc, mode, adjusted_mode))
		return false;
	if (amdgpu_atombios_crtc_prepare_pll(crtc, adjusted_mode))
		return false;
	/* pick pll */
	amdgpu_crtc->pll_id = dce_v11_0_pick_pll(crtc);
	/* if we can't get a PPLL for a non-DP encoder, fail */
	if ((amdgpu_crtc->pll_id == ATOM_PPLL_INVALID) &&
	    !ENCODER_MODE_IS_DP(amdgpu_atombios_encoder_get_encoder_mode(amdgpu_crtc->encoder)))
		return false;

	return true;
}

static int dce_v11_0_crtc_set_base(struct drm_crtc *crtc, int x, int y,
				  struct drm_framebuffer *old_fb)
{
	return dce_v11_0_crtc_do_set_base(crtc, old_fb, x, y, 0);
}

static int dce_v11_0_crtc_set_base_atomic(struct drm_crtc *crtc,
					 struct drm_framebuffer *fb,
					 int x, int y, enum mode_set_atomic state)
{
       return dce_v11_0_crtc_do_set_base(crtc, fb, x, y, 1);
}

static const struct drm_crtc_helper_funcs dce_v11_0_crtc_helper_funcs = {
	.dpms = dce_v11_0_crtc_dpms,
	.mode_fixup = dce_v11_0_crtc_mode_fixup,
	.mode_set = dce_v11_0_crtc_mode_set,
	.mode_set_base = dce_v11_0_crtc_set_base,
	.mode_set_base_atomic = dce_v11_0_crtc_set_base_atomic,
	.prepare = dce_v11_0_crtc_prepare,
	.commit = dce_v11_0_crtc_commit,
	.load_lut = dce_v11_0_crtc_load_lut,
	.disable = dce_v11_0_crtc_disable,
};

static int dce_v11_0_crtc_init(struct amdgpu_device *adev, int index)
{
	struct amdgpu_crtc *amdgpu_crtc;
	int i;

	amdgpu_crtc = kzalloc(sizeof(struct amdgpu_crtc) +
			      (AMDGPUFB_CONN_LIMIT * sizeof(struct drm_connector *)), GFP_KERNEL);
	if (amdgpu_crtc == NULL)
		return -ENOMEM;

	drm_crtc_init(adev->ddev, &amdgpu_crtc->base, &dce_v11_0_crtc_funcs);

	drm_mode_crtc_set_gamma_size(&amdgpu_crtc->base, 256);
	amdgpu_crtc->crtc_id = index;
	amdgpu_crtc->pflip_queue = create_singlethread_workqueue("amdgpu-pageflip-queue");
	adev->mode_info.crtcs[index] = amdgpu_crtc;

	amdgpu_crtc->max_cursor_width = 128;
	amdgpu_crtc->max_cursor_height = 128;
	adev->ddev->mode_config.cursor_width = amdgpu_crtc->max_cursor_width;
	adev->ddev->mode_config.cursor_height = amdgpu_crtc->max_cursor_height;

	for (i = 0; i < 256; i++) {
		amdgpu_crtc->lut_r[i] = i << 2;
		amdgpu_crtc->lut_g[i] = i << 2;
		amdgpu_crtc->lut_b[i] = i << 2;
	}

	switch (amdgpu_crtc->crtc_id) {
	case 0:
	default:
		amdgpu_crtc->crtc_offset = CRTC0_REGISTER_OFFSET;
		break;
	case 1:
		amdgpu_crtc->crtc_offset = CRTC1_REGISTER_OFFSET;
		break;
	case 2:
		amdgpu_crtc->crtc_offset = CRTC2_REGISTER_OFFSET;
		break;
	case 3:
		amdgpu_crtc->crtc_offset = CRTC3_REGISTER_OFFSET;
		break;
	case 4:
		amdgpu_crtc->crtc_offset = CRTC4_REGISTER_OFFSET;
		break;
	case 5:
		amdgpu_crtc->crtc_offset = CRTC5_REGISTER_OFFSET;
		break;
	}

	amdgpu_crtc->pll_id = ATOM_PPLL_INVALID;
	amdgpu_crtc->adjusted_clock = 0;
	amdgpu_crtc->encoder = NULL;
	amdgpu_crtc->connector = NULL;
	drm_crtc_helper_add(&amdgpu_crtc->base, &dce_v11_0_crtc_helper_funcs);

	return 0;
}

static int dce_v11_0_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->audio_endpt_rreg = &dce_v11_0_audio_endpt_rreg;
	adev->audio_endpt_wreg = &dce_v11_0_audio_endpt_wreg;

	dce_v11_0_set_display_funcs(adev);
	dce_v11_0_set_irq_funcs(adev);

	switch (adev->asic_type) {
	case CHIP_CARRIZO:
		adev->mode_info.num_crtc = 3;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 9;
		break;
	case CHIP_STONEY:
		adev->mode_info.num_crtc = 2;
		adev->mode_info.num_hpd = 6;
		adev->mode_info.num_dig = 9;
		break;
	default:
		/* FIXME: not supported yet */
		return -EINVAL;
	}

	return 0;
}

static int dce_v11_0_sw_init(void *handle)
{
	int r, i;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	for (i = 0; i < adev->mode_info.num_crtc; i++) {
		r = amdgpu_irq_add_id(adev, i + 1, &adev->crtc_irq);
		if (r)
		return r;
	}

	for (i = 8; i < 20; i += 2) {
		r = amdgpu_irq_add_id(adev, i, &adev->pageflip_irq);
		if (r)
			return r;
	}

	/* HPD hotplug */
	r = amdgpu_irq_add_id(adev, 42, &adev->hpd_irq);
	if (r)
	return r;

	adev->mode_info.mode_config_initialized = true;

	adev->ddev->mode_config.funcs = &amdgpu_mode_funcs;

	adev->ddev->mode_config.max_width = 16384;
	adev->ddev->mode_config.max_height = 16384;

	adev->ddev->mode_config.preferred_depth = 24;
	adev->ddev->mode_config.prefer_shadow = 1;

	adev->ddev->mode_config.fb_base = adev->mc.aper_base;

	r = amdgpu_modeset_create_props(adev);
	if (r)
		return r;

	adev->ddev->mode_config.max_width = 16384;
	adev->ddev->mode_config.max_height = 16384;

	/* allocate crtcs */
	for (i = 0; i < adev->mode_info.num_crtc; i++) {
		r = dce_v11_0_crtc_init(adev, i);
		if (r)
			return r;
	}

	if (amdgpu_atombios_get_connector_info_from_object_table(adev))
		amdgpu_print_display_setup(adev->ddev);
	else
		return -EINVAL;

	/* setup afmt */
	dce_v11_0_afmt_init(adev);

	r = dce_v11_0_audio_init(adev);
	if (r)
		return r;

	drm_kms_helper_poll_init(adev->ddev);

	return r;
}

static int dce_v11_0_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	kfree(adev->mode_info.bios_hardcoded_edid);

	drm_kms_helper_poll_fini(adev->ddev);

	dce_v11_0_audio_fini(adev);

	dce_v11_0_afmt_fini(adev);

	drm_mode_config_cleanup(adev->ddev);
	adev->mode_info.mode_config_initialized = false;

	return 0;
}

static int dce_v11_0_hw_init(void *handle)
{
	int i;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	dce_v11_0_init_golden_registers(adev);

	/* init dig PHYs, disp eng pll */
	amdgpu_atombios_crtc_powergate_init(adev);
	amdgpu_atombios_encoder_init_dig(adev);
	amdgpu_atombios_crtc_set_disp_eng_pll(adev, adev->clock.default_dispclk);

	/* initialize hpd */
	dce_v11_0_hpd_init(adev);

	for (i = 0; i < adev->mode_info.audio.num_pins; i++) {
		dce_v11_0_audio_enable(adev, &adev->mode_info.audio.pin[i], false);
	}

	dce_v11_0_pageflip_interrupt_init(adev);

	return 0;
}

static int dce_v11_0_hw_fini(void *handle)
{
	int i;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	dce_v11_0_hpd_fini(adev);

	for (i = 0; i < adev->mode_info.audio.num_pins; i++) {
		dce_v11_0_audio_enable(adev, &adev->mode_info.audio.pin[i], false);
	}

	dce_v11_0_pageflip_interrupt_fini(adev);

	return 0;
}

static int dce_v11_0_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_atombios_scratch_regs_save(adev);

	return dce_v11_0_hw_fini(handle);
}

static int dce_v11_0_resume(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int ret;

	ret = dce_v11_0_hw_init(handle);

	amdgpu_atombios_scratch_regs_restore(adev);

	/* turn on the BL */
	if (adev->mode_info.bl_encoder) {
		u8 bl_level = amdgpu_display_backlight_get_level(adev,
								  adev->mode_info.bl_encoder);
		amdgpu_display_backlight_set_level(adev, adev->mode_info.bl_encoder,
						    bl_level);
	}

	return ret;
}

static bool dce_v11_0_is_idle(void *handle)
{
	return true;
}

static int dce_v11_0_wait_for_idle(void *handle)
{
	return 0;
}

static void dce_v11_0_print_status(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	dev_info(adev->dev, "DCE 10.x registers\n");
	/* XXX todo */
}

static int dce_v11_0_soft_reset(void *handle)
{
	u32 srbm_soft_reset = 0, tmp;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (dce_v11_0_is_display_hung(adev))
		srbm_soft_reset |= SRBM_SOFT_RESET__SOFT_RESET_DC_MASK;

	if (srbm_soft_reset) {
		dce_v11_0_print_status((void *)adev);

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
		dce_v11_0_print_status((void *)adev);
	}
	return 0;
}

static void dce_v11_0_set_crtc_vblank_interrupt_state(struct amdgpu_device *adev,
						     int crtc,
						     enum amdgpu_interrupt_state state)
{
	u32 lb_interrupt_mask;

	if (crtc >= adev->mode_info.num_crtc) {
		DRM_DEBUG("invalid crtc %d\n", crtc);
		return;
	}

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		lb_interrupt_mask = RREG32(mmLB_INTERRUPT_MASK + crtc_offsets[crtc]);
		lb_interrupt_mask = REG_SET_FIELD(lb_interrupt_mask, LB_INTERRUPT_MASK,
						  VBLANK_INTERRUPT_MASK, 0);
		WREG32(mmLB_INTERRUPT_MASK + crtc_offsets[crtc], lb_interrupt_mask);
		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		lb_interrupt_mask = RREG32(mmLB_INTERRUPT_MASK + crtc_offsets[crtc]);
		lb_interrupt_mask = REG_SET_FIELD(lb_interrupt_mask, LB_INTERRUPT_MASK,
						  VBLANK_INTERRUPT_MASK, 1);
		WREG32(mmLB_INTERRUPT_MASK + crtc_offsets[crtc], lb_interrupt_mask);
		break;
	default:
		break;
	}
}

static void dce_v11_0_set_crtc_vline_interrupt_state(struct amdgpu_device *adev,
						    int crtc,
						    enum amdgpu_interrupt_state state)
{
	u32 lb_interrupt_mask;

	if (crtc >= adev->mode_info.num_crtc) {
		DRM_DEBUG("invalid crtc %d\n", crtc);
		return;
	}

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		lb_interrupt_mask = RREG32(mmLB_INTERRUPT_MASK + crtc_offsets[crtc]);
		lb_interrupt_mask = REG_SET_FIELD(lb_interrupt_mask, LB_INTERRUPT_MASK,
						  VLINE_INTERRUPT_MASK, 0);
		WREG32(mmLB_INTERRUPT_MASK + crtc_offsets[crtc], lb_interrupt_mask);
		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		lb_interrupt_mask = RREG32(mmLB_INTERRUPT_MASK + crtc_offsets[crtc]);
		lb_interrupt_mask = REG_SET_FIELD(lb_interrupt_mask, LB_INTERRUPT_MASK,
						  VLINE_INTERRUPT_MASK, 1);
		WREG32(mmLB_INTERRUPT_MASK + crtc_offsets[crtc], lb_interrupt_mask);
		break;
	default:
		break;
	}
}

static int dce_v11_0_set_hpd_irq_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned hpd,
					enum amdgpu_interrupt_state state)
{
	u32 tmp;

	if (hpd >= adev->mode_info.num_hpd) {
		DRM_DEBUG("invalid hdp %d\n", hpd);
		return 0;
	}

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		tmp = RREG32(mmDC_HPD_INT_CONTROL + hpd_offsets[hpd]);
		tmp = REG_SET_FIELD(tmp, DC_HPD_INT_CONTROL, DC_HPD_INT_EN, 0);
		WREG32(mmDC_HPD_INT_CONTROL + hpd_offsets[hpd], tmp);
		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		tmp = RREG32(mmDC_HPD_INT_CONTROL + hpd_offsets[hpd]);
		tmp = REG_SET_FIELD(tmp, DC_HPD_INT_CONTROL, DC_HPD_INT_EN, 1);
		WREG32(mmDC_HPD_INT_CONTROL + hpd_offsets[hpd], tmp);
		break;
	default:
		break;
	}

	return 0;
}

static int dce_v11_0_set_crtc_irq_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	switch (type) {
	case AMDGPU_CRTC_IRQ_VBLANK1:
		dce_v11_0_set_crtc_vblank_interrupt_state(adev, 0, state);
		break;
	case AMDGPU_CRTC_IRQ_VBLANK2:
		dce_v11_0_set_crtc_vblank_interrupt_state(adev, 1, state);
		break;
	case AMDGPU_CRTC_IRQ_VBLANK3:
		dce_v11_0_set_crtc_vblank_interrupt_state(adev, 2, state);
		break;
	case AMDGPU_CRTC_IRQ_VBLANK4:
		dce_v11_0_set_crtc_vblank_interrupt_state(adev, 3, state);
		break;
	case AMDGPU_CRTC_IRQ_VBLANK5:
		dce_v11_0_set_crtc_vblank_interrupt_state(adev, 4, state);
		break;
	case AMDGPU_CRTC_IRQ_VBLANK6:
		dce_v11_0_set_crtc_vblank_interrupt_state(adev, 5, state);
		break;
	case AMDGPU_CRTC_IRQ_VLINE1:
		dce_v11_0_set_crtc_vline_interrupt_state(adev, 0, state);
		break;
	case AMDGPU_CRTC_IRQ_VLINE2:
		dce_v11_0_set_crtc_vline_interrupt_state(adev, 1, state);
		break;
	case AMDGPU_CRTC_IRQ_VLINE3:
		dce_v11_0_set_crtc_vline_interrupt_state(adev, 2, state);
		break;
	case AMDGPU_CRTC_IRQ_VLINE4:
		dce_v11_0_set_crtc_vline_interrupt_state(adev, 3, state);
		break;
	case AMDGPU_CRTC_IRQ_VLINE5:
		dce_v11_0_set_crtc_vline_interrupt_state(adev, 4, state);
		break;
	 case AMDGPU_CRTC_IRQ_VLINE6:
		dce_v11_0_set_crtc_vline_interrupt_state(adev, 5, state);
		break;
	default:
		break;
	}
	return 0;
}

static int dce_v11_0_set_pageflip_irq_state(struct amdgpu_device *adev,
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

static int dce_v11_0_pageflip_irq(struct amdgpu_device *adev,
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
	if(amdgpu_crtc == NULL)
		return 0;

	spin_lock_irqsave(&adev->ddev->event_lock, flags);
	works = amdgpu_crtc->pflip_works;
	if (amdgpu_crtc->pflip_status != AMDGPU_FLIP_SUBMITTED){
		DRM_DEBUG_DRIVER("amdgpu_crtc->pflip_status = %d != "
						 "AMDGPU_FLIP_SUBMITTED(%d)\n",
						 amdgpu_crtc->pflip_status,
						 AMDGPU_FLIP_SUBMITTED);
		spin_unlock_irqrestore(&adev->ddev->event_lock, flags);
		return 0;
	}

	/* page flip completed. clean up */
	amdgpu_crtc->pflip_status = AMDGPU_FLIP_NONE;
	amdgpu_crtc->pflip_works = NULL;

	/* wakeup usersapce */
	if(works->event)
		drm_send_vblank_event(adev->ddev, crtc_id, works->event);

	spin_unlock_irqrestore(&adev->ddev->event_lock, flags);

	drm_vblank_put(adev->ddev, amdgpu_crtc->crtc_id);
	queue_work(amdgpu_crtc->pflip_queue, &works->unpin_work);

	return 0;
}

static void dce_v11_0_hpd_int_ack(struct amdgpu_device *adev,
				  int hpd)
{
	u32 tmp;

	if (hpd >= adev->mode_info.num_hpd) {
		DRM_DEBUG("invalid hdp %d\n", hpd);
		return;
	}

	tmp = RREG32(mmDC_HPD_INT_CONTROL + hpd_offsets[hpd]);
	tmp = REG_SET_FIELD(tmp, DC_HPD_INT_CONTROL, DC_HPD_INT_ACK, 1);
	WREG32(mmDC_HPD_INT_CONTROL + hpd_offsets[hpd], tmp);
}

static void dce_v11_0_crtc_vblank_int_ack(struct amdgpu_device *adev,
					  int crtc)
{
	u32 tmp;

	if (crtc >= adev->mode_info.num_crtc) {
		DRM_DEBUG("invalid crtc %d\n", crtc);
		return;
	}

	tmp = RREG32(mmLB_VBLANK_STATUS + crtc_offsets[crtc]);
	tmp = REG_SET_FIELD(tmp, LB_VBLANK_STATUS, VBLANK_ACK, 1);
	WREG32(mmLB_VBLANK_STATUS + crtc_offsets[crtc], tmp);
}

static void dce_v11_0_crtc_vline_int_ack(struct amdgpu_device *adev,
					 int crtc)
{
	u32 tmp;

	if (crtc >= adev->mode_info.num_crtc) {
		DRM_DEBUG("invalid crtc %d\n", crtc);
		return;
	}

	tmp = RREG32(mmLB_VLINE_STATUS + crtc_offsets[crtc]);
	tmp = REG_SET_FIELD(tmp, LB_VLINE_STATUS, VLINE_ACK, 1);
	WREG32(mmLB_VLINE_STATUS + crtc_offsets[crtc], tmp);
}

static int dce_v11_0_crtc_irq(struct amdgpu_device *adev,
				struct amdgpu_irq_src *source,
				struct amdgpu_iv_entry *entry)
{
	unsigned crtc = entry->src_id - 1;
	uint32_t disp_int = RREG32(interrupt_status_offsets[crtc].reg);
	unsigned irq_type = amdgpu_crtc_idx_to_irq_type(adev, crtc);

	switch (entry->src_data) {
	case 0: /* vblank */
		if (disp_int & interrupt_status_offsets[crtc].vblank)
			dce_v11_0_crtc_vblank_int_ack(adev, crtc);
		else
			DRM_DEBUG("IH: IH event w/o asserted irq bit?\n");

		if (amdgpu_irq_enabled(adev, source, irq_type)) {
			drm_handle_vblank(adev->ddev, crtc);
		}
		DRM_DEBUG("IH: D%d vblank\n", crtc + 1);

		break;
	case 1: /* vline */
		if (disp_int & interrupt_status_offsets[crtc].vline)
			dce_v11_0_crtc_vline_int_ack(adev, crtc);
		else
			DRM_DEBUG("IH: IH event w/o asserted irq bit?\n");

		DRM_DEBUG("IH: D%d vline\n", crtc + 1);

		break;
	default:
		DRM_DEBUG("Unhandled interrupt: %d %d\n", entry->src_id, entry->src_data);
		break;
	}

	return 0;
}

static int dce_v11_0_hpd_irq(struct amdgpu_device *adev,
			     struct amdgpu_irq_src *source,
			     struct amdgpu_iv_entry *entry)
{
	uint32_t disp_int, mask;
	unsigned hpd;

	if (entry->src_data >= adev->mode_info.num_hpd) {
		DRM_DEBUG("Unhandled interrupt: %d %d\n", entry->src_id, entry->src_data);
		return 0;
	}

	hpd = entry->src_data;
	disp_int = RREG32(interrupt_status_offsets[hpd].reg);
	mask = interrupt_status_offsets[hpd].hpd;

	if (disp_int & mask) {
		dce_v11_0_hpd_int_ack(adev, hpd);
		schedule_work(&adev->hotplug_work);
		DRM_DEBUG("IH: HPD%d\n", hpd + 1);
	}

	return 0;
}

static int dce_v11_0_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	return 0;
}

static int dce_v11_0_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	return 0;
}

const struct amd_ip_funcs dce_v11_0_ip_funcs = {
	.early_init = dce_v11_0_early_init,
	.late_init = NULL,
	.sw_init = dce_v11_0_sw_init,
	.sw_fini = dce_v11_0_sw_fini,
	.hw_init = dce_v11_0_hw_init,
	.hw_fini = dce_v11_0_hw_fini,
	.suspend = dce_v11_0_suspend,
	.resume = dce_v11_0_resume,
	.is_idle = dce_v11_0_is_idle,
	.wait_for_idle = dce_v11_0_wait_for_idle,
	.soft_reset = dce_v11_0_soft_reset,
	.print_status = dce_v11_0_print_status,
	.set_clockgating_state = dce_v11_0_set_clockgating_state,
	.set_powergating_state = dce_v11_0_set_powergating_state,
};

static void
dce_v11_0_encoder_mode_set(struct drm_encoder *encoder,
			  struct drm_display_mode *mode,
			  struct drm_display_mode *adjusted_mode)
{
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);

	amdgpu_encoder->pixel_clock = adjusted_mode->clock;

	/* need to call this here rather than in prepare() since we need some crtc info */
	amdgpu_atombios_encoder_dpms(encoder, DRM_MODE_DPMS_OFF);

	/* set scaler clears this on some chips */
	dce_v11_0_set_interleave(encoder->crtc, mode);

	if (amdgpu_atombios_encoder_get_encoder_mode(encoder) == ATOM_ENCODER_MODE_HDMI) {
		dce_v11_0_afmt_enable(encoder, true);
		dce_v11_0_afmt_setmode(encoder, adjusted_mode);
	}
}

static void dce_v11_0_encoder_prepare(struct drm_encoder *encoder)
{
	struct amdgpu_device *adev = encoder->dev->dev_private;
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct drm_connector *connector = amdgpu_get_connector_for_encoder(encoder);

	if ((amdgpu_encoder->active_device &
	     (ATOM_DEVICE_DFP_SUPPORT | ATOM_DEVICE_LCD_SUPPORT)) ||
	    (amdgpu_encoder_get_dp_bridge_encoder_id(encoder) !=
	     ENCODER_OBJECT_ID_NONE)) {
		struct amdgpu_encoder_atom_dig *dig = amdgpu_encoder->enc_priv;
		if (dig) {
			dig->dig_encoder = dce_v11_0_pick_dig_encoder(encoder);
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
	dce_v11_0_program_fmt(encoder);
}

static void dce_v11_0_encoder_commit(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_device *adev = dev->dev_private;

	/* need to call this here as we need the crtc set up */
	amdgpu_atombios_encoder_dpms(encoder, DRM_MODE_DPMS_ON);
	amdgpu_atombios_scratch_regs_lock(adev, false);
}

static void dce_v11_0_encoder_disable(struct drm_encoder *encoder)
{
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct amdgpu_encoder_atom_dig *dig;

	amdgpu_atombios_encoder_dpms(encoder, DRM_MODE_DPMS_OFF);

	if (amdgpu_atombios_encoder_is_digital(encoder)) {
		if (amdgpu_atombios_encoder_get_encoder_mode(encoder) == ATOM_ENCODER_MODE_HDMI)
			dce_v11_0_afmt_enable(encoder, false);
		dig = amdgpu_encoder->enc_priv;
		dig->dig_encoder = -1;
	}
	amdgpu_encoder->active_device = 0;
}

/* these are handled by the primary encoders */
static void dce_v11_0_ext_prepare(struct drm_encoder *encoder)
{

}

static void dce_v11_0_ext_commit(struct drm_encoder *encoder)
{

}

static void
dce_v11_0_ext_mode_set(struct drm_encoder *encoder,
		      struct drm_display_mode *mode,
		      struct drm_display_mode *adjusted_mode)
{

}

static void dce_v11_0_ext_disable(struct drm_encoder *encoder)
{

}

static void
dce_v11_0_ext_dpms(struct drm_encoder *encoder, int mode)
{

}

static bool dce_v11_0_ext_mode_fixup(struct drm_encoder *encoder,
				    const struct drm_display_mode *mode,
				    struct drm_display_mode *adjusted_mode)
{
	return true;
}

static const struct drm_encoder_helper_funcs dce_v11_0_ext_helper_funcs = {
	.dpms = dce_v11_0_ext_dpms,
	.mode_fixup = dce_v11_0_ext_mode_fixup,
	.prepare = dce_v11_0_ext_prepare,
	.mode_set = dce_v11_0_ext_mode_set,
	.commit = dce_v11_0_ext_commit,
	.disable = dce_v11_0_ext_disable,
	/* no detect for TMDS/LVDS yet */
};

static const struct drm_encoder_helper_funcs dce_v11_0_dig_helper_funcs = {
	.dpms = amdgpu_atombios_encoder_dpms,
	.mode_fixup = amdgpu_atombios_encoder_mode_fixup,
	.prepare = dce_v11_0_encoder_prepare,
	.mode_set = dce_v11_0_encoder_mode_set,
	.commit = dce_v11_0_encoder_commit,
	.disable = dce_v11_0_encoder_disable,
	.detect = amdgpu_atombios_encoder_dig_detect,
};

static const struct drm_encoder_helper_funcs dce_v11_0_dac_helper_funcs = {
	.dpms = amdgpu_atombios_encoder_dpms,
	.mode_fixup = amdgpu_atombios_encoder_mode_fixup,
	.prepare = dce_v11_0_encoder_prepare,
	.mode_set = dce_v11_0_encoder_mode_set,
	.commit = dce_v11_0_encoder_commit,
	.detect = amdgpu_atombios_encoder_dac_detect,
};

static void dce_v11_0_encoder_destroy(struct drm_encoder *encoder)
{
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	if (amdgpu_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT))
		amdgpu_atombios_encoder_fini_backlight(amdgpu_encoder);
	kfree(amdgpu_encoder->enc_priv);
	drm_encoder_cleanup(encoder);
	kfree(amdgpu_encoder);
}

static const struct drm_encoder_funcs dce_v11_0_encoder_funcs = {
	.destroy = dce_v11_0_encoder_destroy,
};

static void dce_v11_0_encoder_add(struct amdgpu_device *adev,
				 uint32_t encoder_enum,
				 uint32_t supported_device,
				 u16 caps)
{
	struct drm_device *dev = adev->ddev;
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
	case 3:
		encoder->possible_crtcs = 0x7;
		break;
	case 4:
		encoder->possible_crtcs = 0xf;
		break;
	case 5:
		encoder->possible_crtcs = 0x1f;
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
		drm_encoder_init(dev, encoder, &dce_v11_0_encoder_funcs,
				 DRM_MODE_ENCODER_DAC);
		drm_encoder_helper_add(encoder, &dce_v11_0_dac_helper_funcs);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY1:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY2:
	case ENCODER_OBJECT_ID_INTERNAL_UNIPHY3:
		if (amdgpu_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT)) {
			amdgpu_encoder->rmx_type = RMX_FULL;
			drm_encoder_init(dev, encoder, &dce_v11_0_encoder_funcs,
					 DRM_MODE_ENCODER_LVDS);
			amdgpu_encoder->enc_priv = amdgpu_atombios_encoder_get_lcd_info(amdgpu_encoder);
		} else if (amdgpu_encoder->devices & (ATOM_DEVICE_CRT_SUPPORT)) {
			drm_encoder_init(dev, encoder, &dce_v11_0_encoder_funcs,
					 DRM_MODE_ENCODER_DAC);
			amdgpu_encoder->enc_priv = amdgpu_atombios_encoder_get_dig_info(amdgpu_encoder);
		} else {
			drm_encoder_init(dev, encoder, &dce_v11_0_encoder_funcs,
					 DRM_MODE_ENCODER_TMDS);
			amdgpu_encoder->enc_priv = amdgpu_atombios_encoder_get_dig_info(amdgpu_encoder);
		}
		drm_encoder_helper_add(encoder, &dce_v11_0_dig_helper_funcs);
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
			drm_encoder_init(dev, encoder, &dce_v11_0_encoder_funcs,
					 DRM_MODE_ENCODER_LVDS);
		else if (amdgpu_encoder->devices & (ATOM_DEVICE_CRT_SUPPORT))
			drm_encoder_init(dev, encoder, &dce_v11_0_encoder_funcs,
					 DRM_MODE_ENCODER_DAC);
		else
			drm_encoder_init(dev, encoder, &dce_v11_0_encoder_funcs,
					 DRM_MODE_ENCODER_TMDS);
		drm_encoder_helper_add(encoder, &dce_v11_0_ext_helper_funcs);
		break;
	}
}

static const struct amdgpu_display_funcs dce_v11_0_display_funcs = {
	.set_vga_render_state = &dce_v11_0_set_vga_render_state,
	.bandwidth_update = &dce_v11_0_bandwidth_update,
	.vblank_get_counter = &dce_v11_0_vblank_get_counter,
	.vblank_wait = &dce_v11_0_vblank_wait,
	.is_display_hung = &dce_v11_0_is_display_hung,
	.backlight_set_level = &amdgpu_atombios_encoder_set_backlight_level,
	.backlight_get_level = &amdgpu_atombios_encoder_get_backlight_level,
	.hpd_sense = &dce_v11_0_hpd_sense,
	.hpd_set_polarity = &dce_v11_0_hpd_set_polarity,
	.hpd_get_gpio_reg = &dce_v11_0_hpd_get_gpio_reg,
	.page_flip = &dce_v11_0_page_flip,
	.page_flip_get_scanoutpos = &dce_v11_0_crtc_get_scanoutpos,
	.add_encoder = &dce_v11_0_encoder_add,
	.add_connector = &amdgpu_connector_add,
	.stop_mc_access = &dce_v11_0_stop_mc_access,
	.resume_mc_access = &dce_v11_0_resume_mc_access,
};

static void dce_v11_0_set_display_funcs(struct amdgpu_device *adev)
{
	if (adev->mode_info.funcs == NULL)
		adev->mode_info.funcs = &dce_v11_0_display_funcs;
}

static const struct amdgpu_irq_src_funcs dce_v11_0_crtc_irq_funcs = {
	.set = dce_v11_0_set_crtc_irq_state,
	.process = dce_v11_0_crtc_irq,
};

static const struct amdgpu_irq_src_funcs dce_v11_0_pageflip_irq_funcs = {
	.set = dce_v11_0_set_pageflip_irq_state,
	.process = dce_v11_0_pageflip_irq,
};

static const struct amdgpu_irq_src_funcs dce_v11_0_hpd_irq_funcs = {
	.set = dce_v11_0_set_hpd_irq_state,
	.process = dce_v11_0_hpd_irq,
};

static void dce_v11_0_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->crtc_irq.num_types = AMDGPU_CRTC_IRQ_LAST;
	adev->crtc_irq.funcs = &dce_v11_0_crtc_irq_funcs;

	adev->pageflip_irq.num_types = AMDGPU_PAGEFLIP_IRQ_LAST;
	adev->pageflip_irq.funcs = &dce_v11_0_pageflip_irq_funcs;

	adev->hpd_irq.num_types = AMDGPU_HPD_LAST;
	adev->hpd_irq.funcs = &dce_v11_0_hpd_irq_funcs;
}
