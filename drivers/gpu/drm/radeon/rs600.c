/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
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
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 */
/* RS600 / Radeon X1250/X1270 integrated GPU
 *
 * This file gather function specific to RS600 which is the IGP of
 * the X1250/X1270 family supporting intel CPU (while RS690/RS740
 * is the X1250/X1270 supporting AMD CPU). The display engine are
 * the avivo one, bios is an atombios, 3D block are the one of the
 * R4XX family. The GART is different from the RS400 one and is very
 * close to the one of the R600 family (R600 likely being an evolution
 * of the RS600 GART block).
 */
#include <drm/drmP.h>
#include "radeon.h"
#include "radeon_asic.h"
#include "atom.h"
#include "rs600d.h"

#include "rs600_reg_safe.h"

static void rs600_gpu_init(struct radeon_device *rdev);
int rs600_mc_wait_for_idle(struct radeon_device *rdev);

static const u32 crtc_offsets[2] =
{
	0,
	AVIVO_D2CRTC_H_TOTAL - AVIVO_D1CRTC_H_TOTAL
};

static bool avivo_is_in_vblank(struct radeon_device *rdev, int crtc)
{
	if (RREG32(AVIVO_D1CRTC_STATUS + crtc_offsets[crtc]) & AVIVO_D1CRTC_V_BLANK)
		return true;
	else
		return false;
}

static bool avivo_is_counter_moving(struct radeon_device *rdev, int crtc)
{
	u32 pos1, pos2;

	pos1 = RREG32(AVIVO_D1CRTC_STATUS_POSITION + crtc_offsets[crtc]);
	pos2 = RREG32(AVIVO_D1CRTC_STATUS_POSITION + crtc_offsets[crtc]);

	if (pos1 != pos2)
		return true;
	else
		return false;
}

/**
 * avivo_wait_for_vblank - vblank wait asic callback.
 *
 * @rdev: radeon_device pointer
 * @crtc: crtc to wait for vblank on
 *
 * Wait for vblank on the requested crtc (r5xx-r7xx).
 */
void avivo_wait_for_vblank(struct radeon_device *rdev, int crtc)
{
	unsigned i = 0;

	if (crtc >= rdev->num_crtc)
		return;

	if (!(RREG32(AVIVO_D1CRTC_CONTROL + crtc_offsets[crtc]) & AVIVO_CRTC_EN))
		return;

	/* depending on when we hit vblank, we may be close to active; if so,
	 * wait for another frame.
	 */
	while (avivo_is_in_vblank(rdev, crtc)) {
		if (i++ % 100 == 0) {
			if (!avivo_is_counter_moving(rdev, crtc))
				break;
		}
	}

	while (!avivo_is_in_vblank(rdev, crtc)) {
		if (i++ % 100 == 0) {
			if (!avivo_is_counter_moving(rdev, crtc))
				break;
		}
	}
}

void rs600_pre_page_flip(struct radeon_device *rdev, int crtc)
{
	/* enable the pflip int */
	radeon_irq_kms_pflip_irq_get(rdev, crtc);
}

void rs600_post_page_flip(struct radeon_device *rdev, int crtc)
{
	/* disable the pflip int */
	radeon_irq_kms_pflip_irq_put(rdev, crtc);
}

u32 rs600_page_flip(struct radeon_device *rdev, int crtc_id, u64 crtc_base)
{
	struct radeon_crtc *radeon_crtc = rdev->mode_info.crtcs[crtc_id];
	u32 tmp = RREG32(AVIVO_D1GRPH_UPDATE + radeon_crtc->crtc_offset);
	int i;

	/* Lock the graphics update lock */
	tmp |= AVIVO_D1GRPH_UPDATE_LOCK;
	WREG32(AVIVO_D1GRPH_UPDATE + radeon_crtc->crtc_offset, tmp);

	/* update the scanout addresses */
	WREG32(AVIVO_D1GRPH_SECONDARY_SURFACE_ADDRESS + radeon_crtc->crtc_offset,
	       (u32)crtc_base);
	WREG32(AVIVO_D1GRPH_PRIMARY_SURFACE_ADDRESS + radeon_crtc->crtc_offset,
	       (u32)crtc_base);

	/* Wait for update_pending to go high. */
	for (i = 0; i < rdev->usec_timeout; i++) {
		if (RREG32(AVIVO_D1GRPH_UPDATE + radeon_crtc->crtc_offset) & AVIVO_D1GRPH_SURFACE_UPDATE_PENDING)
			break;
		udelay(1);
	}
	DRM_DEBUG("Update pending now high. Unlocking vupdate_lock.\n");

	/* Unlock the lock, so double-buffering can take place inside vblank */
	tmp &= ~AVIVO_D1GRPH_UPDATE_LOCK;
	WREG32(AVIVO_D1GRPH_UPDATE + radeon_crtc->crtc_offset, tmp);

	/* Return current update_pending status: */
	return RREG32(AVIVO_D1GRPH_UPDATE + radeon_crtc->crtc_offset) & AVIVO_D1GRPH_SURFACE_UPDATE_PENDING;
}

void avivo_program_fmt(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct drm_connector *connector = radeon_get_connector_for_encoder(encoder);
	int bpc = 0;
	u32 tmp = 0;
	enum radeon_connector_dither dither = RADEON_FMT_DITHER_DISABLE;

	if (connector) {
		struct radeon_connector *radeon_connector = to_radeon_connector(connector);
		bpc = radeon_get_monitor_bpc(connector);
		dither = radeon_connector->dither;
	}

	/* LVDS FMT is set up by atom */
	if (radeon_encoder->devices & ATOM_DEVICE_LCD_SUPPORT)
		return;

	if (bpc == 0)
		return;

	switch (bpc) {
	case 6:
		if (dither == RADEON_FMT_DITHER_ENABLE)
			/* XXX sort out optimal dither settings */
			tmp |= AVIVO_TMDS_BIT_DEPTH_CONTROL_SPATIAL_DITHER_EN;
		else
			tmp |= AVIVO_TMDS_BIT_DEPTH_CONTROL_TRUNCATE_EN;
		break;
	case 8:
		if (dither == RADEON_FMT_DITHER_ENABLE)
			/* XXX sort out optimal dither settings */
			tmp |= (AVIVO_TMDS_BIT_DEPTH_CONTROL_SPATIAL_DITHER_EN |
				AVIVO_TMDS_BIT_DEPTH_CONTROL_SPATIAL_DITHER_DEPTH);
		else
			tmp |= (AVIVO_TMDS_BIT_DEPTH_CONTROL_TRUNCATE_EN |
				AVIVO_TMDS_BIT_DEPTH_CONTROL_TRUNCATE_DEPTH);
		break;
	case 10:
	default:
		/* not needed */
		break;
	}

	switch (radeon_encoder->encoder_id) {
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_TMDS1:
		WREG32(AVIVO_TMDSA_BIT_DEPTH_CONTROL, tmp);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_LVTM1:
		WREG32(AVIVO_LVTMA_BIT_DEPTH_CONTROL, tmp);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1:
		WREG32(AVIVO_DVOA_BIT_DEPTH_CONTROL, tmp);
		break;
	case ENCODER_OBJECT_ID_INTERNAL_DDI:
		WREG32(AVIVO_DDIA_BIT_DEPTH_CONTROL, tmp);
		break;
	default:
		break;
	}
}

void rs600_pm_misc(struct radeon_device *rdev)
{
	int requested_index = rdev->pm.requested_power_state_index;
	struct radeon_power_state *ps = &rdev->pm.power_state[requested_index];
	struct radeon_voltage *voltage = &ps->clock_info[0].voltage;
	u32 tmp, dyn_pwrmgt_sclk_length, dyn_sclk_vol_cntl;
	u32 hdp_dyn_cntl, /*mc_host_dyn_cntl,*/ dyn_backbias_cntl;

	if ((voltage->type == VOLTAGE_GPIO) && (voltage->gpio.valid)) {
		if (ps->misc & ATOM_PM_MISCINFO_VOLTAGE_DROP_SUPPORT) {
			tmp = RREG32(voltage->gpio.reg);
			if (voltage->active_high)
				tmp |= voltage->gpio.mask;
			else
				tmp &= ~(voltage->gpio.mask);
			WREG32(voltage->gpio.reg, tmp);
			if (voltage->delay)
				udelay(voltage->delay);
		} else {
			tmp = RREG32(voltage->gpio.reg);
			if (voltage->active_high)
				tmp &= ~voltage->gpio.mask;
			else
				tmp |= voltage->gpio.mask;
			WREG32(voltage->gpio.reg, tmp);
			if (voltage->delay)
				udelay(voltage->delay);
		}
	} else if (voltage->type == VOLTAGE_VDDC)
		radeon_atom_set_voltage(rdev, voltage->vddc_id, SET_VOLTAGE_TYPE_ASIC_VDDC);

	dyn_pwrmgt_sclk_length = RREG32_PLL(DYN_PWRMGT_SCLK_LENGTH);
	dyn_pwrmgt_sclk_length &= ~REDUCED_POWER_SCLK_HILEN(0xf);
	dyn_pwrmgt_sclk_length &= ~REDUCED_POWER_SCLK_LOLEN(0xf);
	if (ps->misc & ATOM_PM_MISCINFO_ASIC_REDUCED_SPEED_SCLK_EN) {
		if (ps->misc & ATOM_PM_MISCINFO_DYNAMIC_CLOCK_DIVIDER_BY_2) {
			dyn_pwrmgt_sclk_length |= REDUCED_POWER_SCLK_HILEN(2);
			dyn_pwrmgt_sclk_length |= REDUCED_POWER_SCLK_LOLEN(2);
		} else if (ps->misc & ATOM_PM_MISCINFO_DYNAMIC_CLOCK_DIVIDER_BY_4) {
			dyn_pwrmgt_sclk_length |= REDUCED_POWER_SCLK_HILEN(4);
			dyn_pwrmgt_sclk_length |= REDUCED_POWER_SCLK_LOLEN(4);
		}
	} else {
		dyn_pwrmgt_sclk_length |= REDUCED_POWER_SCLK_HILEN(1);
		dyn_pwrmgt_sclk_length |= REDUCED_POWER_SCLK_LOLEN(1);
	}
	WREG32_PLL(DYN_PWRMGT_SCLK_LENGTH, dyn_pwrmgt_sclk_length);

	dyn_sclk_vol_cntl = RREG32_PLL(DYN_SCLK_VOL_CNTL);
	if (ps->misc & ATOM_PM_MISCINFO_ASIC_DYNAMIC_VOLTAGE_EN) {
		dyn_sclk_vol_cntl |= IO_CG_VOLTAGE_DROP;
		if (voltage->delay) {
			dyn_sclk_vol_cntl |= VOLTAGE_DROP_SYNC;
			dyn_sclk_vol_cntl |= VOLTAGE_DELAY_SEL(voltage->delay);
		} else
			dyn_sclk_vol_cntl &= ~VOLTAGE_DROP_SYNC;
	} else
		dyn_sclk_vol_cntl &= ~IO_CG_VOLTAGE_DROP;
	WREG32_PLL(DYN_SCLK_VOL_CNTL, dyn_sclk_vol_cntl);

	hdp_dyn_cntl = RREG32_PLL(HDP_DYN_CNTL);
	if (ps->misc & ATOM_PM_MISCINFO_DYNAMIC_HDP_BLOCK_EN)
		hdp_dyn_cntl &= ~HDP_FORCEON;
	else
		hdp_dyn_cntl |= HDP_FORCEON;
	WREG32_PLL(HDP_DYN_CNTL, hdp_dyn_cntl);
#if 0
	/* mc_host_dyn seems to cause hangs from time to time */
	mc_host_dyn_cntl = RREG32_PLL(MC_HOST_DYN_CNTL);
	if (ps->misc & ATOM_PM_MISCINFO_DYNAMIC_MC_HOST_BLOCK_EN)
		mc_host_dyn_cntl &= ~MC_HOST_FORCEON;
	else
		mc_host_dyn_cntl |= MC_HOST_FORCEON;
	WREG32_PLL(MC_HOST_DYN_CNTL, mc_host_dyn_cntl);
#endif
	dyn_backbias_cntl = RREG32_PLL(DYN_BACKBIAS_CNTL);
	if (ps->misc & ATOM_PM_MISCINFO2_DYNAMIC_BACK_BIAS_EN)
		dyn_backbias_cntl |= IO_CG_BACKBIAS_EN;
	else
		dyn_backbias_cntl &= ~IO_CG_BACKBIAS_EN;
	WREG32_PLL(DYN_BACKBIAS_CNTL, dyn_backbias_cntl);

	/* set pcie lanes */
	if ((rdev->flags & RADEON_IS_PCIE) &&
	    !(rdev->flags & RADEON_IS_IGP) &&
	    rdev->asic->pm.set_pcie_lanes &&
	    (ps->pcie_lanes !=
	     rdev->pm.power_state[rdev->pm.current_power_state_index].pcie_lanes)) {
		radeon_set_pcie_lanes(rdev,
				      ps->pcie_lanes);
		DRM_DEBUG("Setting: p: %d\n", ps->pcie_lanes);
	}
}

void rs600_pm_prepare(struct radeon_device *rdev)
{
	struct drm_device *ddev = rdev->ddev;
	struct drm_crtc *crtc;
	struct radeon_crtc *radeon_crtc;
	u32 tmp;

	/* disable any active CRTCs */
	list_for_each_entry(crtc, &ddev->mode_config.crtc_list, head) {
		radeon_crtc = to_radeon_crtc(crtc);
		if (radeon_crtc->enabled) {
			tmp = RREG32(AVIVO_D1CRTC_CONTROL + radeon_crtc->crtc_offset);
			tmp |= AVIVO_CRTC_DISP_READ_REQUEST_DISABLE;
			WREG32(AVIVO_D1CRTC_CONTROL + radeon_crtc->crtc_offset, tmp);
		}
	}
}

void rs600_pm_finish(struct radeon_device *rdev)
{
	struct drm_device *ddev = rdev->ddev;
	struct drm_crtc *crtc;
	struct radeon_crtc *radeon_crtc;
	u32 tmp;

	/* enable any active CRTCs */
	list_for_each_entry(crtc, &ddev->mode_config.crtc_list, head) {
		radeon_crtc = to_radeon_crtc(crtc);
		if (radeon_crtc->enabled) {
			tmp = RREG32(AVIVO_D1CRTC_CONTROL + radeon_crtc->crtc_offset);
			tmp &= ~AVIVO_CRTC_DISP_READ_REQUEST_DISABLE;
			WREG32(AVIVO_D1CRTC_CONTROL + radeon_crtc->crtc_offset, tmp);
		}
	}
}

/* hpd for digital panel detect/disconnect */
bool rs600_hpd_sense(struct radeon_device *rdev, enum radeon_hpd_id hpd)
{
	u32 tmp;
	bool connected = false;

	switch (hpd) {
	case RADEON_HPD_1:
		tmp = RREG32(R_007D04_DC_HOT_PLUG_DETECT1_INT_STATUS);
		if (G_007D04_DC_HOT_PLUG_DETECT1_SENSE(tmp))
			connected = true;
		break;
	case RADEON_HPD_2:
		tmp = RREG32(R_007D14_DC_HOT_PLUG_DETECT2_INT_STATUS);
		if (G_007D14_DC_HOT_PLUG_DETECT2_SENSE(tmp))
			connected = true;
		break;
	default:
		break;
	}
	return connected;
}

void rs600_hpd_set_polarity(struct radeon_device *rdev,
			    enum radeon_hpd_id hpd)
{
	u32 tmp;
	bool connected = rs600_hpd_sense(rdev, hpd);

	switch (hpd) {
	case RADEON_HPD_1:
		tmp = RREG32(R_007D08_DC_HOT_PLUG_DETECT1_INT_CONTROL);
		if (connected)
			tmp &= ~S_007D08_DC_HOT_PLUG_DETECT1_INT_POLARITY(1);
		else
			tmp |= S_007D08_DC_HOT_PLUG_DETECT1_INT_POLARITY(1);
		WREG32(R_007D08_DC_HOT_PLUG_DETECT1_INT_CONTROL, tmp);
		break;
	case RADEON_HPD_2:
		tmp = RREG32(R_007D18_DC_HOT_PLUG_DETECT2_INT_CONTROL);
		if (connected)
			tmp &= ~S_007D18_DC_HOT_PLUG_DETECT2_INT_POLARITY(1);
		else
			tmp |= S_007D18_DC_HOT_PLUG_DETECT2_INT_POLARITY(1);
		WREG32(R_007D18_DC_HOT_PLUG_DETECT2_INT_CONTROL, tmp);
		break;
	default:
		break;
	}
}

void rs600_hpd_init(struct radeon_device *rdev)
{
	struct drm_device *dev = rdev->ddev;
	struct drm_connector *connector;
	unsigned enable = 0;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		struct radeon_connector *radeon_connector = to_radeon_connector(connector);
		switch (radeon_connector->hpd.hpd) {
		case RADEON_HPD_1:
			WREG32(R_007D00_DC_HOT_PLUG_DETECT1_CONTROL,
			       S_007D00_DC_HOT_PLUG_DETECT1_EN(1));
			break;
		case RADEON_HPD_2:
			WREG32(R_007D10_DC_HOT_PLUG_DETECT2_CONTROL,
			       S_007D10_DC_HOT_PLUG_DETECT2_EN(1));
			break;
		default:
			break;
		}
		enable |= 1 << radeon_connector->hpd.hpd;
		radeon_hpd_set_polarity(rdev, radeon_connector->hpd.hpd);
	}
	radeon_irq_kms_enable_hpd(rdev, enable);
}

void rs600_hpd_fini(struct radeon_device *rdev)
{
	struct drm_device *dev = rdev->ddev;
	struct drm_connector *connector;
	unsigned disable = 0;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		struct radeon_connector *radeon_connector = to_radeon_connector(connector);
		switch (radeon_connector->hpd.hpd) {
		case RADEON_HPD_1:
			WREG32(R_007D00_DC_HOT_PLUG_DETECT1_CONTROL,
			       S_007D00_DC_HOT_PLUG_DETECT1_EN(0));
			break;
		case RADEON_HPD_2:
			WREG32(R_007D10_DC_HOT_PLUG_DETECT2_CONTROL,
			       S_007D10_DC_HOT_PLUG_DETECT2_EN(0));
			break;
		default:
			break;
		}
		disable |= 1 << radeon_connector->hpd.hpd;
	}
	radeon_irq_kms_disable_hpd(rdev, disable);
}

int rs600_asic_reset(struct radeon_device *rdev)
{
	struct rv515_mc_save save;
	u32 status, tmp;
	int ret = 0;

	status = RREG32(R_000E40_RBBM_STATUS);
	if (!G_000E40_GUI_ACTIVE(status)) {
		return 0;
	}
	/* Stops all mc clients */
	rv515_mc_stop(rdev, &save);
	status = RREG32(R_000E40_RBBM_STATUS);
	dev_info(rdev->dev, "(%s:%d) RBBM_STATUS=0x%08X\n", __func__, __LINE__, status);
	/* stop CP */
	WREG32(RADEON_CP_CSQ_CNTL, 0);
	tmp = RREG32(RADEON_CP_RB_CNTL);
	WREG32(RADEON_CP_RB_CNTL, tmp | RADEON_RB_RPTR_WR_ENA);
	WREG32(RADEON_CP_RB_RPTR_WR, 0);
	WREG32(RADEON_CP_RB_WPTR, 0);
	WREG32(RADEON_CP_RB_CNTL, tmp);
	pci_save_state(rdev->pdev);
	/* disable bus mastering */
	pci_clear_master(rdev->pdev);
	mdelay(1);
	/* reset GA+VAP */
	WREG32(R_0000F0_RBBM_SOFT_RESET, S_0000F0_SOFT_RESET_VAP(1) |
					S_0000F0_SOFT_RESET_GA(1));
	RREG32(R_0000F0_RBBM_SOFT_RESET);
	mdelay(500);
	WREG32(R_0000F0_RBBM_SOFT_RESET, 0);
	mdelay(1);
	status = RREG32(R_000E40_RBBM_STATUS);
	dev_info(rdev->dev, "(%s:%d) RBBM_STATUS=0x%08X\n", __func__, __LINE__, status);
	/* reset CP */
	WREG32(R_0000F0_RBBM_SOFT_RESET, S_0000F0_SOFT_RESET_CP(1));
	RREG32(R_0000F0_RBBM_SOFT_RESET);
	mdelay(500);
	WREG32(R_0000F0_RBBM_SOFT_RESET, 0);
	mdelay(1);
	status = RREG32(R_000E40_RBBM_STATUS);
	dev_info(rdev->dev, "(%s:%d) RBBM_STATUS=0x%08X\n", __func__, __LINE__, status);
	/* reset MC */
	WREG32(R_0000F0_RBBM_SOFT_RESET, S_0000F0_SOFT_RESET_MC(1));
	RREG32(R_0000F0_RBBM_SOFT_RESET);
	mdelay(500);
	WREG32(R_0000F0_RBBM_SOFT_RESET, 0);
	mdelay(1);
	status = RREG32(R_000E40_RBBM_STATUS);
	dev_info(rdev->dev, "(%s:%d) RBBM_STATUS=0x%08X\n", __func__, __LINE__, status);
	/* restore PCI & busmastering */
	pci_restore_state(rdev->pdev);
	/* Check if GPU is idle */
	if (G_000E40_GA_BUSY(status) || G_000E40_VAP_BUSY(status)) {
		dev_err(rdev->dev, "failed to reset GPU\n");
		ret = -1;
	} else
		dev_info(rdev->dev, "GPU reset succeed\n");
	rv515_mc_resume(rdev, &save);
	return ret;
}

/*
 * GART.
 */
void rs600_gart_tlb_flush(struct radeon_device *rdev)
{
	uint32_t tmp;

	tmp = RREG32_MC(R_000100_MC_PT0_CNTL);
	tmp &= C_000100_INVALIDATE_ALL_L1_TLBS & C_000100_INVALIDATE_L2_CACHE;
	WREG32_MC(R_000100_MC_PT0_CNTL, tmp);

	tmp = RREG32_MC(R_000100_MC_PT0_CNTL);
	tmp |= S_000100_INVALIDATE_ALL_L1_TLBS(1) | S_000100_INVALIDATE_L2_CACHE(1);
	WREG32_MC(R_000100_MC_PT0_CNTL, tmp);

	tmp = RREG32_MC(R_000100_MC_PT0_CNTL);
	tmp &= C_000100_INVALIDATE_ALL_L1_TLBS & C_000100_INVALIDATE_L2_CACHE;
	WREG32_MC(R_000100_MC_PT0_CNTL, tmp);
	tmp = RREG32_MC(R_000100_MC_PT0_CNTL);
}

static int rs600_gart_init(struct radeon_device *rdev)
{
	int r;

	if (rdev->gart.robj) {
		WARN(1, "RS600 GART already initialized\n");
		return 0;
	}
	/* Initialize common gart structure */
	r = radeon_gart_init(rdev);
	if (r) {
		return r;
	}
	rdev->gart.table_size = rdev->gart.num_gpu_pages * 8;
	return radeon_gart_table_vram_alloc(rdev);
}

static int rs600_gart_enable(struct radeon_device *rdev)
{
	u32 tmp;
	int r, i;

	if (rdev->gart.robj == NULL) {
		dev_err(rdev->dev, "No VRAM object for PCIE GART.\n");
		return -EINVAL;
	}
	r = radeon_gart_table_vram_pin(rdev);
	if (r)
		return r;
	radeon_gart_restore(rdev);
	/* Enable bus master */
	tmp = RREG32(RADEON_BUS_CNTL) & ~RS600_BUS_MASTER_DIS;
	WREG32(RADEON_BUS_CNTL, tmp);
	/* FIXME: setup default page */
	WREG32_MC(R_000100_MC_PT0_CNTL,
		  (S_000100_EFFECTIVE_L2_CACHE_SIZE(6) |
		   S_000100_EFFECTIVE_L2_QUEUE_SIZE(6)));

	for (i = 0; i < 19; i++) {
		WREG32_MC(R_00016C_MC_PT0_CLIENT0_CNTL + i,
			  S_00016C_ENABLE_TRANSLATION_MODE_OVERRIDE(1) |
			  S_00016C_SYSTEM_ACCESS_MODE_MASK(
				  V_00016C_SYSTEM_ACCESS_MODE_NOT_IN_SYS) |
			  S_00016C_SYSTEM_APERTURE_UNMAPPED_ACCESS(
				  V_00016C_SYSTEM_APERTURE_UNMAPPED_PASSTHROUGH) |
			  S_00016C_EFFECTIVE_L1_CACHE_SIZE(3) |
			  S_00016C_ENABLE_FRAGMENT_PROCESSING(1) |
			  S_00016C_EFFECTIVE_L1_QUEUE_SIZE(3));
	}
	/* enable first context */
	WREG32_MC(R_000102_MC_PT0_CONTEXT0_CNTL,
		  S_000102_ENABLE_PAGE_TABLE(1) |
		  S_000102_PAGE_TABLE_DEPTH(V_000102_PAGE_TABLE_FLAT));

	/* disable all other contexts */
	for (i = 1; i < 8; i++)
		WREG32_MC(R_000102_MC_PT0_CONTEXT0_CNTL + i, 0);

	/* setup the page table */
	WREG32_MC(R_00012C_MC_PT0_CONTEXT0_FLAT_BASE_ADDR,
		  rdev->gart.table_addr);
	WREG32_MC(R_00013C_MC_PT0_CONTEXT0_FLAT_START_ADDR, rdev->mc.gtt_start);
	WREG32_MC(R_00014C_MC_PT0_CONTEXT0_FLAT_END_ADDR, rdev->mc.gtt_end);
	WREG32_MC(R_00011C_MC_PT0_CONTEXT0_DEFAULT_READ_ADDR, 0);

	/* System context maps to VRAM space */
	WREG32_MC(R_000112_MC_PT0_SYSTEM_APERTURE_LOW_ADDR, rdev->mc.vram_start);
	WREG32_MC(R_000114_MC_PT0_SYSTEM_APERTURE_HIGH_ADDR, rdev->mc.vram_end);

	/* enable page tables */
	tmp = RREG32_MC(R_000100_MC_PT0_CNTL);
	WREG32_MC(R_000100_MC_PT0_CNTL, (tmp | S_000100_ENABLE_PT(1)));
	tmp = RREG32_MC(R_000009_MC_CNTL1);
	WREG32_MC(R_000009_MC_CNTL1, (tmp | S_000009_ENABLE_PAGE_TABLES(1)));
	rs600_gart_tlb_flush(rdev);
	DRM_INFO("PCIE GART of %uM enabled (table at 0x%016llX).\n",
		 (unsigned)(rdev->mc.gtt_size >> 20),
		 (unsigned long long)rdev->gart.table_addr);
	rdev->gart.ready = true;
	return 0;
}

static void rs600_gart_disable(struct radeon_device *rdev)
{
	u32 tmp;

	/* FIXME: disable out of gart access */
	WREG32_MC(R_000100_MC_PT0_CNTL, 0);
	tmp = RREG32_MC(R_000009_MC_CNTL1);
	WREG32_MC(R_000009_MC_CNTL1, tmp & C_000009_ENABLE_PAGE_TABLES);
	radeon_gart_table_vram_unpin(rdev);
}

static void rs600_gart_fini(struct radeon_device *rdev)
{
	radeon_gart_fini(rdev);
	rs600_gart_disable(rdev);
	radeon_gart_table_vram_free(rdev);
}

#define R600_PTE_VALID     (1 << 0)
#define R600_PTE_SYSTEM    (1 << 1)
#define R600_PTE_SNOOPED   (1 << 2)
#define R600_PTE_READABLE  (1 << 5)
#define R600_PTE_WRITEABLE (1 << 6)

int rs600_gart_set_page(struct radeon_device *rdev, int i, uint64_t addr)
{
	void __iomem *ptr = (void *)rdev->gart.ptr;

	if (i < 0 || i > rdev->gart.num_gpu_pages) {
		return -EINVAL;
	}
	addr = addr & 0xFFFFFFFFFFFFF000ULL;
	addr |= R600_PTE_VALID | R600_PTE_SYSTEM | R600_PTE_SNOOPED;
	addr |= R600_PTE_READABLE | R600_PTE_WRITEABLE;
	writeq(addr, ptr + (i * 8));
	return 0;
}

int rs600_irq_set(struct radeon_device *rdev)
{
	uint32_t tmp = 0;
	uint32_t mode_int = 0;
	u32 hpd1 = RREG32(R_007D08_DC_HOT_PLUG_DETECT1_INT_CONTROL) &
		~S_007D08_DC_HOT_PLUG_DETECT1_INT_EN(1);
	u32 hpd2 = RREG32(R_007D18_DC_HOT_PLUG_DETECT2_INT_CONTROL) &
		~S_007D18_DC_HOT_PLUG_DETECT2_INT_EN(1);
	u32 hdmi0;
	if (ASIC_IS_DCE2(rdev))
		hdmi0 = RREG32(R_007408_HDMI0_AUDIO_PACKET_CONTROL) &
			~S_007408_HDMI0_AZ_FORMAT_WTRIG_MASK(1);
	else
		hdmi0 = 0;

	if (!rdev->irq.installed) {
		WARN(1, "Can't enable IRQ/MSI because no handler is installed\n");
		WREG32(R_000040_GEN_INT_CNTL, 0);
		return -EINVAL;
	}
	if (atomic_read(&rdev->irq.ring_int[RADEON_RING_TYPE_GFX_INDEX])) {
		tmp |= S_000040_SW_INT_EN(1);
	}
	if (rdev->irq.crtc_vblank_int[0] ||
	    atomic_read(&rdev->irq.pflip[0])) {
		mode_int |= S_006540_D1MODE_VBLANK_INT_MASK(1);
	}
	if (rdev->irq.crtc_vblank_int[1] ||
	    atomic_read(&rdev->irq.pflip[1])) {
		mode_int |= S_006540_D2MODE_VBLANK_INT_MASK(1);
	}
	if (rdev->irq.hpd[0]) {
		hpd1 |= S_007D08_DC_HOT_PLUG_DETECT1_INT_EN(1);
	}
	if (rdev->irq.hpd[1]) {
		hpd2 |= S_007D18_DC_HOT_PLUG_DETECT2_INT_EN(1);
	}
	if (rdev->irq.afmt[0]) {
		hdmi0 |= S_007408_HDMI0_AZ_FORMAT_WTRIG_MASK(1);
	}
	WREG32(R_000040_GEN_INT_CNTL, tmp);
	WREG32(R_006540_DxMODE_INT_MASK, mode_int);
	WREG32(R_007D08_DC_HOT_PLUG_DETECT1_INT_CONTROL, hpd1);
	WREG32(R_007D18_DC_HOT_PLUG_DETECT2_INT_CONTROL, hpd2);
	if (ASIC_IS_DCE2(rdev))
		WREG32(R_007408_HDMI0_AUDIO_PACKET_CONTROL, hdmi0);
	return 0;
}

static inline u32 rs600_irq_ack(struct radeon_device *rdev)
{
	uint32_t irqs = RREG32(R_000044_GEN_INT_STATUS);
	uint32_t irq_mask = S_000044_SW_INT(1);
	u32 tmp;

	if (G_000044_DISPLAY_INT_STAT(irqs)) {
		rdev->irq.stat_regs.r500.disp_int = RREG32(R_007EDC_DISP_INTERRUPT_STATUS);
		if (G_007EDC_LB_D1_VBLANK_INTERRUPT(rdev->irq.stat_regs.r500.disp_int)) {
			WREG32(R_006534_D1MODE_VBLANK_STATUS,
				S_006534_D1MODE_VBLANK_ACK(1));
		}
		if (G_007EDC_LB_D2_VBLANK_INTERRUPT(rdev->irq.stat_regs.r500.disp_int)) {
			WREG32(R_006D34_D2MODE_VBLANK_STATUS,
				S_006D34_D2MODE_VBLANK_ACK(1));
		}
		if (G_007EDC_DC_HOT_PLUG_DETECT1_INTERRUPT(rdev->irq.stat_regs.r500.disp_int)) {
			tmp = RREG32(R_007D08_DC_HOT_PLUG_DETECT1_INT_CONTROL);
			tmp |= S_007D08_DC_HOT_PLUG_DETECT1_INT_ACK(1);
			WREG32(R_007D08_DC_HOT_PLUG_DETECT1_INT_CONTROL, tmp);
		}
		if (G_007EDC_DC_HOT_PLUG_DETECT2_INTERRUPT(rdev->irq.stat_regs.r500.disp_int)) {
			tmp = RREG32(R_007D18_DC_HOT_PLUG_DETECT2_INT_CONTROL);
			tmp |= S_007D18_DC_HOT_PLUG_DETECT2_INT_ACK(1);
			WREG32(R_007D18_DC_HOT_PLUG_DETECT2_INT_CONTROL, tmp);
		}
	} else {
		rdev->irq.stat_regs.r500.disp_int = 0;
	}

	if (ASIC_IS_DCE2(rdev)) {
		rdev->irq.stat_regs.r500.hdmi0_status = RREG32(R_007404_HDMI0_STATUS) &
			S_007404_HDMI0_AZ_FORMAT_WTRIG(1);
		if (G_007404_HDMI0_AZ_FORMAT_WTRIG(rdev->irq.stat_regs.r500.hdmi0_status)) {
			tmp = RREG32(R_007408_HDMI0_AUDIO_PACKET_CONTROL);
			tmp |= S_007408_HDMI0_AZ_FORMAT_WTRIG_ACK(1);
			WREG32(R_007408_HDMI0_AUDIO_PACKET_CONTROL, tmp);
		}
	} else
		rdev->irq.stat_regs.r500.hdmi0_status = 0;

	if (irqs) {
		WREG32(R_000044_GEN_INT_STATUS, irqs);
	}
	return irqs & irq_mask;
}

void rs600_irq_disable(struct radeon_device *rdev)
{
	u32 hdmi0 = RREG32(R_007408_HDMI0_AUDIO_PACKET_CONTROL) &
		~S_007408_HDMI0_AZ_FORMAT_WTRIG_MASK(1);
	WREG32(R_007408_HDMI0_AUDIO_PACKET_CONTROL, hdmi0);
	WREG32(R_000040_GEN_INT_CNTL, 0);
	WREG32(R_006540_DxMODE_INT_MASK, 0);
	/* Wait and acknowledge irq */
	mdelay(1);
	rs600_irq_ack(rdev);
}

int rs600_irq_process(struct radeon_device *rdev)
{
	u32 status, msi_rearm;
	bool queue_hotplug = false;
	bool queue_hdmi = false;

	status = rs600_irq_ack(rdev);
	if (!status &&
	    !rdev->irq.stat_regs.r500.disp_int &&
	    !rdev->irq.stat_regs.r500.hdmi0_status) {
		return IRQ_NONE;
	}
	while (status ||
	       rdev->irq.stat_regs.r500.disp_int ||
	       rdev->irq.stat_regs.r500.hdmi0_status) {
		/* SW interrupt */
		if (G_000044_SW_INT(status)) {
			radeon_fence_process(rdev, RADEON_RING_TYPE_GFX_INDEX);
		}
		/* Vertical blank interrupts */
		if (G_007EDC_LB_D1_VBLANK_INTERRUPT(rdev->irq.stat_regs.r500.disp_int)) {
			if (rdev->irq.crtc_vblank_int[0]) {
				drm_handle_vblank(rdev->ddev, 0);
				rdev->pm.vblank_sync = true;
				wake_up(&rdev->irq.vblank_queue);
			}
			if (atomic_read(&rdev->irq.pflip[0]))
				radeon_crtc_handle_flip(rdev, 0);
		}
		if (G_007EDC_LB_D2_VBLANK_INTERRUPT(rdev->irq.stat_regs.r500.disp_int)) {
			if (rdev->irq.crtc_vblank_int[1]) {
				drm_handle_vblank(rdev->ddev, 1);
				rdev->pm.vblank_sync = true;
				wake_up(&rdev->irq.vblank_queue);
			}
			if (atomic_read(&rdev->irq.pflip[1]))
				radeon_crtc_handle_flip(rdev, 1);
		}
		if (G_007EDC_DC_HOT_PLUG_DETECT1_INTERRUPT(rdev->irq.stat_regs.r500.disp_int)) {
			queue_hotplug = true;
			DRM_DEBUG("HPD1\n");
		}
		if (G_007EDC_DC_HOT_PLUG_DETECT2_INTERRUPT(rdev->irq.stat_regs.r500.disp_int)) {
			queue_hotplug = true;
			DRM_DEBUG("HPD2\n");
		}
		if (G_007404_HDMI0_AZ_FORMAT_WTRIG(rdev->irq.stat_regs.r500.hdmi0_status)) {
			queue_hdmi = true;
			DRM_DEBUG("HDMI0\n");
		}
		status = rs600_irq_ack(rdev);
	}
	if (queue_hotplug)
		schedule_work(&rdev->hotplug_work);
	if (queue_hdmi)
		schedule_work(&rdev->audio_work);
	if (rdev->msi_enabled) {
		switch (rdev->family) {
		case CHIP_RS600:
		case CHIP_RS690:
		case CHIP_RS740:
			msi_rearm = RREG32(RADEON_BUS_CNTL) & ~RS600_MSI_REARM;
			WREG32(RADEON_BUS_CNTL, msi_rearm);
			WREG32(RADEON_BUS_CNTL, msi_rearm | RS600_MSI_REARM);
			break;
		default:
			WREG32(RADEON_MSI_REARM_EN, RV370_MSI_REARM_EN);
			break;
		}
	}
	return IRQ_HANDLED;
}

u32 rs600_get_vblank_counter(struct radeon_device *rdev, int crtc)
{
	if (crtc == 0)
		return RREG32(R_0060A4_D1CRTC_STATUS_FRAME_COUNT);
	else
		return RREG32(R_0068A4_D2CRTC_STATUS_FRAME_COUNT);
}

int rs600_mc_wait_for_idle(struct radeon_device *rdev)
{
	unsigned i;

	for (i = 0; i < rdev->usec_timeout; i++) {
		if (G_000000_MC_IDLE(RREG32_MC(R_000000_MC_STATUS)))
			return 0;
		udelay(1);
	}
	return -1;
}

static void rs600_gpu_init(struct radeon_device *rdev)
{
	r420_pipes_init(rdev);
	/* Wait for mc idle */
	if (rs600_mc_wait_for_idle(rdev))
		dev_warn(rdev->dev, "Wait MC idle timeout before updating MC.\n");
}

static void rs600_mc_init(struct radeon_device *rdev)
{
	u64 base;

	rdev->mc.aper_base = pci_resource_start(rdev->pdev, 0);
	rdev->mc.aper_size = pci_resource_len(rdev->pdev, 0);
	rdev->mc.vram_is_ddr = true;
	rdev->mc.vram_width = 128;
	rdev->mc.real_vram_size = RREG32(RADEON_CONFIG_MEMSIZE);
	rdev->mc.mc_vram_size = rdev->mc.real_vram_size;
	rdev->mc.visible_vram_size = rdev->mc.aper_size;
	rdev->mc.igp_sideport_enabled = radeon_atombios_sideport_present(rdev);
	base = RREG32_MC(R_000004_MC_FB_LOCATION);
	base = G_000004_MC_FB_START(base) << 16;
	radeon_vram_location(rdev, &rdev->mc, base);
	rdev->mc.gtt_base_align = 0;
	radeon_gtt_location(rdev, &rdev->mc);
	radeon_update_bandwidth_info(rdev);
}

void rs600_bandwidth_update(struct radeon_device *rdev)
{
	struct drm_display_mode *mode0 = NULL;
	struct drm_display_mode *mode1 = NULL;
	u32 d1mode_priority_a_cnt, d2mode_priority_a_cnt;
	/* FIXME: implement full support */

	radeon_update_display_priority(rdev);

	if (rdev->mode_info.crtcs[0]->base.enabled)
		mode0 = &rdev->mode_info.crtcs[0]->base.mode;
	if (rdev->mode_info.crtcs[1]->base.enabled)
		mode1 = &rdev->mode_info.crtcs[1]->base.mode;

	rs690_line_buffer_adjust(rdev, mode0, mode1);

	if (rdev->disp_priority == 2) {
		d1mode_priority_a_cnt = RREG32(R_006548_D1MODE_PRIORITY_A_CNT);
		d2mode_priority_a_cnt = RREG32(R_006D48_D2MODE_PRIORITY_A_CNT);
		d1mode_priority_a_cnt |= S_006548_D1MODE_PRIORITY_A_ALWAYS_ON(1);
		d2mode_priority_a_cnt |= S_006D48_D2MODE_PRIORITY_A_ALWAYS_ON(1);
		WREG32(R_006548_D1MODE_PRIORITY_A_CNT, d1mode_priority_a_cnt);
		WREG32(R_00654C_D1MODE_PRIORITY_B_CNT, d1mode_priority_a_cnt);
		WREG32(R_006D48_D2MODE_PRIORITY_A_CNT, d2mode_priority_a_cnt);
		WREG32(R_006D4C_D2MODE_PRIORITY_B_CNT, d2mode_priority_a_cnt);
	}
}

uint32_t rs600_mc_rreg(struct radeon_device *rdev, uint32_t reg)
{
	unsigned long flags;
	u32 r;

	spin_lock_irqsave(&rdev->mc_idx_lock, flags);
	WREG32(R_000070_MC_IND_INDEX, S_000070_MC_IND_ADDR(reg) |
		S_000070_MC_IND_CITF_ARB0(1));
	r = RREG32(R_000074_MC_IND_DATA);
	spin_unlock_irqrestore(&rdev->mc_idx_lock, flags);
	return r;
}

void rs600_mc_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v)
{
	unsigned long flags;

	spin_lock_irqsave(&rdev->mc_idx_lock, flags);
	WREG32(R_000070_MC_IND_INDEX, S_000070_MC_IND_ADDR(reg) |
		S_000070_MC_IND_CITF_ARB0(1) | S_000070_MC_IND_WR_EN(1));
	WREG32(R_000074_MC_IND_DATA, v);
	spin_unlock_irqrestore(&rdev->mc_idx_lock, flags);
}

static void rs600_debugfs(struct radeon_device *rdev)
{
	if (r100_debugfs_rbbm_init(rdev))
		DRM_ERROR("Failed to register debugfs file for RBBM !\n");
}

void rs600_set_safe_registers(struct radeon_device *rdev)
{
	rdev->config.r300.reg_safe_bm = rs600_reg_safe_bm;
	rdev->config.r300.reg_safe_bm_size = ARRAY_SIZE(rs600_reg_safe_bm);
}

static void rs600_mc_program(struct radeon_device *rdev)
{
	struct rv515_mc_save save;

	/* Stops all mc clients */
	rv515_mc_stop(rdev, &save);

	/* Wait for mc idle */
	if (rs600_mc_wait_for_idle(rdev))
		dev_warn(rdev->dev, "Wait MC idle timeout before updating MC.\n");

	/* FIXME: What does AGP means for such chipset ? */
	WREG32_MC(R_000005_MC_AGP_LOCATION, 0x0FFFFFFF);
	WREG32_MC(R_000006_AGP_BASE, 0);
	WREG32_MC(R_000007_AGP_BASE_2, 0);
	/* Program MC */
	WREG32_MC(R_000004_MC_FB_LOCATION,
			S_000004_MC_FB_START(rdev->mc.vram_start >> 16) |
			S_000004_MC_FB_TOP(rdev->mc.vram_end >> 16));
	WREG32(R_000134_HDP_FB_LOCATION,
		S_000134_HDP_FB_START(rdev->mc.vram_start >> 16));

	rv515_mc_resume(rdev, &save);
}

static int rs600_startup(struct radeon_device *rdev)
{
	int r;

	rs600_mc_program(rdev);
	/* Resume clock */
	rv515_clock_startup(rdev);
	/* Initialize GPU configuration (# pipes, ...) */
	rs600_gpu_init(rdev);
	/* Initialize GART (initialize after TTM so we can allocate
	 * memory through TTM but finalize after TTM) */
	r = rs600_gart_enable(rdev);
	if (r)
		return r;

	/* allocate wb buffer */
	r = radeon_wb_init(rdev);
	if (r)
		return r;

	r = radeon_fence_driver_start_ring(rdev, RADEON_RING_TYPE_GFX_INDEX);
	if (r) {
		dev_err(rdev->dev, "failed initializing CP fences (%d).\n", r);
		return r;
	}

	/* Enable IRQ */
	if (!rdev->irq.installed) {
		r = radeon_irq_kms_init(rdev);
		if (r)
			return r;
	}

	rs600_irq_set(rdev);
	rdev->config.r300.hdp_cntl = RREG32(RADEON_HOST_PATH_CNTL);
	/* 1M ring buffer */
	r = r100_cp_init(rdev, 1024 * 1024);
	if (r) {
		dev_err(rdev->dev, "failed initializing CP (%d).\n", r);
		return r;
	}

	r = radeon_ib_pool_init(rdev);
	if (r) {
		dev_err(rdev->dev, "IB initialization failed (%d).\n", r);
		return r;
	}

	r = r600_audio_init(rdev);
	if (r) {
		dev_err(rdev->dev, "failed initializing audio\n");
		return r;
	}

	return 0;
}

int rs600_resume(struct radeon_device *rdev)
{
	int r;

	/* Make sur GART are not working */
	rs600_gart_disable(rdev);
	/* Resume clock before doing reset */
	rv515_clock_startup(rdev);
	/* Reset gpu before posting otherwise ATOM will enter infinite loop */
	if (radeon_asic_reset(rdev)) {
		dev_warn(rdev->dev, "GPU reset failed ! (0xE40=0x%08X, 0x7C0=0x%08X)\n",
			RREG32(R_000E40_RBBM_STATUS),
			RREG32(R_0007C0_CP_STAT));
	}
	/* post */
	atom_asic_init(rdev->mode_info.atom_context);
	/* Resume clock after posting */
	rv515_clock_startup(rdev);
	/* Initialize surface registers */
	radeon_surface_init(rdev);

	rdev->accel_working = true;
	r = rs600_startup(rdev);
	if (r) {
		rdev->accel_working = false;
	}
	return r;
}

int rs600_suspend(struct radeon_device *rdev)
{
	r600_audio_fini(rdev);
	r100_cp_disable(rdev);
	radeon_wb_disable(rdev);
	rs600_irq_disable(rdev);
	rs600_gart_disable(rdev);
	return 0;
}

void rs600_fini(struct radeon_device *rdev)
{
	r600_audio_fini(rdev);
	r100_cp_fini(rdev);
	radeon_wb_fini(rdev);
	radeon_ib_pool_fini(rdev);
	radeon_gem_fini(rdev);
	rs600_gart_fini(rdev);
	radeon_irq_kms_fini(rdev);
	radeon_fence_driver_fini(rdev);
	radeon_bo_fini(rdev);
	radeon_atombios_fini(rdev);
	kfree(rdev->bios);
	rdev->bios = NULL;
}

int rs600_init(struct radeon_device *rdev)
{
	int r;

	/* Disable VGA */
	rv515_vga_render_disable(rdev);
	/* Initialize scratch registers */
	radeon_scratch_init(rdev);
	/* Initialize surface registers */
	radeon_surface_init(rdev);
	/* restore some register to sane defaults */
	r100_restore_sanity(rdev);
	/* BIOS */
	if (!radeon_get_bios(rdev)) {
		if (ASIC_IS_AVIVO(rdev))
			return -EINVAL;
	}
	if (rdev->is_atom_bios) {
		r = radeon_atombios_init(rdev);
		if (r)
			return r;
	} else {
		dev_err(rdev->dev, "Expecting atombios for RS600 GPU\n");
		return -EINVAL;
	}
	/* Reset gpu before posting otherwise ATOM will enter infinite loop */
	if (radeon_asic_reset(rdev)) {
		dev_warn(rdev->dev,
			"GPU reset failed ! (0xE40=0x%08X, 0x7C0=0x%08X)\n",
			RREG32(R_000E40_RBBM_STATUS),
			RREG32(R_0007C0_CP_STAT));
	}
	/* check if cards are posted or not */
	if (radeon_boot_test_post_card(rdev) == false)
		return -EINVAL;

	/* Initialize clocks */
	radeon_get_clock_info(rdev->ddev);
	/* initialize memory controller */
	rs600_mc_init(rdev);
	rs600_debugfs(rdev);
	/* Fence driver */
	r = radeon_fence_driver_init(rdev);
	if (r)
		return r;
	/* Memory manager */
	r = radeon_bo_init(rdev);
	if (r)
		return r;
	r = rs600_gart_init(rdev);
	if (r)
		return r;
	rs600_set_safe_registers(rdev);

	rdev->accel_working = true;
	r = rs600_startup(rdev);
	if (r) {
		/* Somethings want wront with the accel init stop accel */
		dev_err(rdev->dev, "Disabling GPU acceleration\n");
		r100_cp_fini(rdev);
		radeon_wb_fini(rdev);
		radeon_ib_pool_fini(rdev);
		rs600_gart_fini(rdev);
		radeon_irq_kms_fini(rdev);
		rdev->accel_working = false;
	}
	return 0;
}
