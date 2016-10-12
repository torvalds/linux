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
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <drm/drmP.h>
#include <drm/radeon_drm.h>
#include "radeon_reg.h"
#include "radeon.h"
#include "radeon_asic.h"
#include "r100d.h"
#include "rs100d.h"
#include "rv200d.h"
#include "rv250d.h"
#include "atom.h"

#include <linux/firmware.h>
#include <linux/module.h>

#include "r100_reg_safe.h"
#include "rn50_reg_safe.h"

/* Firmware Names */
#define FIRMWARE_R100		"radeon/R100_cp.bin"
#define FIRMWARE_R200		"radeon/R200_cp.bin"
#define FIRMWARE_R300		"radeon/R300_cp.bin"
#define FIRMWARE_R420		"radeon/R420_cp.bin"
#define FIRMWARE_RS690		"radeon/RS690_cp.bin"
#define FIRMWARE_RS600		"radeon/RS600_cp.bin"
#define FIRMWARE_R520		"radeon/R520_cp.bin"

MODULE_FIRMWARE(FIRMWARE_R100);
MODULE_FIRMWARE(FIRMWARE_R200);
MODULE_FIRMWARE(FIRMWARE_R300);
MODULE_FIRMWARE(FIRMWARE_R420);
MODULE_FIRMWARE(FIRMWARE_RS690);
MODULE_FIRMWARE(FIRMWARE_RS600);
MODULE_FIRMWARE(FIRMWARE_R520);

#include "r100_track.h"

/* This files gather functions specifics to:
 * r100,rv100,rs100,rv200,rs200,r200,rv250,rs300,rv280
 * and others in some cases.
 */

static bool r100_is_in_vblank(struct radeon_device *rdev, int crtc)
{
	if (crtc == 0) {
		if (RREG32(RADEON_CRTC_STATUS) & RADEON_CRTC_VBLANK_CUR)
			return true;
		else
			return false;
	} else {
		if (RREG32(RADEON_CRTC2_STATUS) & RADEON_CRTC2_VBLANK_CUR)
			return true;
		else
			return false;
	}
}

static bool r100_is_counter_moving(struct radeon_device *rdev, int crtc)
{
	u32 vline1, vline2;

	if (crtc == 0) {
		vline1 = (RREG32(RADEON_CRTC_VLINE_CRNT_VLINE) >> 16) & RADEON_CRTC_V_TOTAL;
		vline2 = (RREG32(RADEON_CRTC_VLINE_CRNT_VLINE) >> 16) & RADEON_CRTC_V_TOTAL;
	} else {
		vline1 = (RREG32(RADEON_CRTC2_VLINE_CRNT_VLINE) >> 16) & RADEON_CRTC_V_TOTAL;
		vline2 = (RREG32(RADEON_CRTC2_VLINE_CRNT_VLINE) >> 16) & RADEON_CRTC_V_TOTAL;
	}
	if (vline1 != vline2)
		return true;
	else
		return false;
}

/**
 * r100_wait_for_vblank - vblank wait asic callback.
 *
 * @rdev: radeon_device pointer
 * @crtc: crtc to wait for vblank on
 *
 * Wait for vblank on the requested crtc (r1xx-r4xx).
 */
void r100_wait_for_vblank(struct radeon_device *rdev, int crtc)
{
	unsigned i = 0;

	if (crtc >= rdev->num_crtc)
		return;

	if (crtc == 0) {
		if (!(RREG32(RADEON_CRTC_GEN_CNTL) & RADEON_CRTC_EN))
			return;
	} else {
		if (!(RREG32(RADEON_CRTC2_GEN_CNTL) & RADEON_CRTC2_EN))
			return;
	}

	/* depending on when we hit vblank, we may be close to active; if so,
	 * wait for another frame.
	 */
	while (r100_is_in_vblank(rdev, crtc)) {
		if (i++ % 100 == 0) {
			if (!r100_is_counter_moving(rdev, crtc))
				break;
		}
	}

	while (!r100_is_in_vblank(rdev, crtc)) {
		if (i++ % 100 == 0) {
			if (!r100_is_counter_moving(rdev, crtc))
				break;
		}
	}
}

/**
 * r100_page_flip - pageflip callback.
 *
 * @rdev: radeon_device pointer
 * @crtc_id: crtc to cleanup pageflip on
 * @crtc_base: new address of the crtc (GPU MC address)
 *
 * Does the actual pageflip (r1xx-r4xx).
 * During vblank we take the crtc lock and wait for the update_pending
 * bit to go high, when it does, we release the lock, and allow the
 * double buffered update to take place.
 */
void r100_page_flip(struct radeon_device *rdev, int crtc_id, u64 crtc_base, bool async)
{
	struct radeon_crtc *radeon_crtc = rdev->mode_info.crtcs[crtc_id];
	u32 tmp = ((u32)crtc_base) | RADEON_CRTC_OFFSET__OFFSET_LOCK;
	int i;

	/* Lock the graphics update lock */
	/* update the scanout addresses */
	WREG32(RADEON_CRTC_OFFSET + radeon_crtc->crtc_offset, tmp);

	/* Wait for update_pending to go high. */
	for (i = 0; i < rdev->usec_timeout; i++) {
		if (RREG32(RADEON_CRTC_OFFSET + radeon_crtc->crtc_offset) & RADEON_CRTC_OFFSET__GUI_TRIG_OFFSET)
			break;
		udelay(1);
	}
	DRM_DEBUG("Update pending now high. Unlocking vupdate_lock.\n");

	/* Unlock the lock, so double-buffering can take place inside vblank */
	tmp &= ~RADEON_CRTC_OFFSET__OFFSET_LOCK;
	WREG32(RADEON_CRTC_OFFSET + radeon_crtc->crtc_offset, tmp);

}

/**
 * r100_page_flip_pending - check if page flip is still pending
 *
 * @rdev: radeon_device pointer
 * @crtc_id: crtc to check
 *
 * Check if the last pagefilp is still pending (r1xx-r4xx).
 * Returns the current update pending status.
 */
bool r100_page_flip_pending(struct radeon_device *rdev, int crtc_id)
{
	struct radeon_crtc *radeon_crtc = rdev->mode_info.crtcs[crtc_id];

	/* Return current update_pending status: */
	return !!(RREG32(RADEON_CRTC_OFFSET + radeon_crtc->crtc_offset) &
		RADEON_CRTC_OFFSET__GUI_TRIG_OFFSET);
}

/**
 * r100_pm_get_dynpm_state - look up dynpm power state callback.
 *
 * @rdev: radeon_device pointer
 *
 * Look up the optimal power state based on the
 * current state of the GPU (r1xx-r5xx).
 * Used for dynpm only.
 */
void r100_pm_get_dynpm_state(struct radeon_device *rdev)
{
	int i;
	rdev->pm.dynpm_can_upclock = true;
	rdev->pm.dynpm_can_downclock = true;

	switch (rdev->pm.dynpm_planned_action) {
	case DYNPM_ACTION_MINIMUM:
		rdev->pm.requested_power_state_index = 0;
		rdev->pm.dynpm_can_downclock = false;
		break;
	case DYNPM_ACTION_DOWNCLOCK:
		if (rdev->pm.current_power_state_index == 0) {
			rdev->pm.requested_power_state_index = rdev->pm.current_power_state_index;
			rdev->pm.dynpm_can_downclock = false;
		} else {
			if (rdev->pm.active_crtc_count > 1) {
				for (i = 0; i < rdev->pm.num_power_states; i++) {
					if (rdev->pm.power_state[i].flags & RADEON_PM_STATE_SINGLE_DISPLAY_ONLY)
						continue;
					else if (i >= rdev->pm.current_power_state_index) {
						rdev->pm.requested_power_state_index = rdev->pm.current_power_state_index;
						break;
					} else {
						rdev->pm.requested_power_state_index = i;
						break;
					}
				}
			} else
				rdev->pm.requested_power_state_index =
					rdev->pm.current_power_state_index - 1;
		}
		/* don't use the power state if crtcs are active and no display flag is set */
		if ((rdev->pm.active_crtc_count > 0) &&
		    (rdev->pm.power_state[rdev->pm.requested_power_state_index].clock_info[0].flags &
		     RADEON_PM_MODE_NO_DISPLAY)) {
			rdev->pm.requested_power_state_index++;
		}
		break;
	case DYNPM_ACTION_UPCLOCK:
		if (rdev->pm.current_power_state_index == (rdev->pm.num_power_states - 1)) {
			rdev->pm.requested_power_state_index = rdev->pm.current_power_state_index;
			rdev->pm.dynpm_can_upclock = false;
		} else {
			if (rdev->pm.active_crtc_count > 1) {
				for (i = (rdev->pm.num_power_states - 1); i >= 0; i--) {
					if (rdev->pm.power_state[i].flags & RADEON_PM_STATE_SINGLE_DISPLAY_ONLY)
						continue;
					else if (i <= rdev->pm.current_power_state_index) {
						rdev->pm.requested_power_state_index = rdev->pm.current_power_state_index;
						break;
					} else {
						rdev->pm.requested_power_state_index = i;
						break;
					}
				}
			} else
				rdev->pm.requested_power_state_index =
					rdev->pm.current_power_state_index + 1;
		}
		break;
	case DYNPM_ACTION_DEFAULT:
		rdev->pm.requested_power_state_index = rdev->pm.default_power_state_index;
		rdev->pm.dynpm_can_upclock = false;
		break;
	case DYNPM_ACTION_NONE:
	default:
		DRM_ERROR("Requested mode for not defined action\n");
		return;
	}
	/* only one clock mode per power state */
	rdev->pm.requested_clock_mode_index = 0;

	DRM_DEBUG_DRIVER("Requested: e: %d m: %d p: %d\n",
		  rdev->pm.power_state[rdev->pm.requested_power_state_index].
		  clock_info[rdev->pm.requested_clock_mode_index].sclk,
		  rdev->pm.power_state[rdev->pm.requested_power_state_index].
		  clock_info[rdev->pm.requested_clock_mode_index].mclk,
		  rdev->pm.power_state[rdev->pm.requested_power_state_index].
		  pcie_lanes);
}

/**
 * r100_pm_init_profile - Initialize power profiles callback.
 *
 * @rdev: radeon_device pointer
 *
 * Initialize the power states used in profile mode
 * (r1xx-r3xx).
 * Used for profile mode only.
 */
void r100_pm_init_profile(struct radeon_device *rdev)
{
	/* default */
	rdev->pm.profiles[PM_PROFILE_DEFAULT_IDX].dpms_off_ps_idx = rdev->pm.default_power_state_index;
	rdev->pm.profiles[PM_PROFILE_DEFAULT_IDX].dpms_on_ps_idx = rdev->pm.default_power_state_index;
	rdev->pm.profiles[PM_PROFILE_DEFAULT_IDX].dpms_off_cm_idx = 0;
	rdev->pm.profiles[PM_PROFILE_DEFAULT_IDX].dpms_on_cm_idx = 0;
	/* low sh */
	rdev->pm.profiles[PM_PROFILE_LOW_SH_IDX].dpms_off_ps_idx = 0;
	rdev->pm.profiles[PM_PROFILE_LOW_SH_IDX].dpms_on_ps_idx = 0;
	rdev->pm.profiles[PM_PROFILE_LOW_SH_IDX].dpms_off_cm_idx = 0;
	rdev->pm.profiles[PM_PROFILE_LOW_SH_IDX].dpms_on_cm_idx = 0;
	/* mid sh */
	rdev->pm.profiles[PM_PROFILE_MID_SH_IDX].dpms_off_ps_idx = 0;
	rdev->pm.profiles[PM_PROFILE_MID_SH_IDX].dpms_on_ps_idx = 0;
	rdev->pm.profiles[PM_PROFILE_MID_SH_IDX].dpms_off_cm_idx = 0;
	rdev->pm.profiles[PM_PROFILE_MID_SH_IDX].dpms_on_cm_idx = 0;
	/* high sh */
	rdev->pm.profiles[PM_PROFILE_HIGH_SH_IDX].dpms_off_ps_idx = 0;
	rdev->pm.profiles[PM_PROFILE_HIGH_SH_IDX].dpms_on_ps_idx = rdev->pm.default_power_state_index;
	rdev->pm.profiles[PM_PROFILE_HIGH_SH_IDX].dpms_off_cm_idx = 0;
	rdev->pm.profiles[PM_PROFILE_HIGH_SH_IDX].dpms_on_cm_idx = 0;
	/* low mh */
	rdev->pm.profiles[PM_PROFILE_LOW_MH_IDX].dpms_off_ps_idx = 0;
	rdev->pm.profiles[PM_PROFILE_LOW_MH_IDX].dpms_on_ps_idx = rdev->pm.default_power_state_index;
	rdev->pm.profiles[PM_PROFILE_LOW_MH_IDX].dpms_off_cm_idx = 0;
	rdev->pm.profiles[PM_PROFILE_LOW_MH_IDX].dpms_on_cm_idx = 0;
	/* mid mh */
	rdev->pm.profiles[PM_PROFILE_MID_MH_IDX].dpms_off_ps_idx = 0;
	rdev->pm.profiles[PM_PROFILE_MID_MH_IDX].dpms_on_ps_idx = rdev->pm.default_power_state_index;
	rdev->pm.profiles[PM_PROFILE_MID_MH_IDX].dpms_off_cm_idx = 0;
	rdev->pm.profiles[PM_PROFILE_MID_MH_IDX].dpms_on_cm_idx = 0;
	/* high mh */
	rdev->pm.profiles[PM_PROFILE_HIGH_MH_IDX].dpms_off_ps_idx = 0;
	rdev->pm.profiles[PM_PROFILE_HIGH_MH_IDX].dpms_on_ps_idx = rdev->pm.default_power_state_index;
	rdev->pm.profiles[PM_PROFILE_HIGH_MH_IDX].dpms_off_cm_idx = 0;
	rdev->pm.profiles[PM_PROFILE_HIGH_MH_IDX].dpms_on_cm_idx = 0;
}

/**
 * r100_pm_misc - set additional pm hw parameters callback.
 *
 * @rdev: radeon_device pointer
 *
 * Set non-clock parameters associated with a power state
 * (voltage, pcie lanes, etc.) (r1xx-r4xx).
 */
void r100_pm_misc(struct radeon_device *rdev)
{
	int requested_index = rdev->pm.requested_power_state_index;
	struct radeon_power_state *ps = &rdev->pm.power_state[requested_index];
	struct radeon_voltage *voltage = &ps->clock_info[0].voltage;
	u32 tmp, sclk_cntl, sclk_cntl2, sclk_more_cntl;

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
	}

	sclk_cntl = RREG32_PLL(SCLK_CNTL);
	sclk_cntl2 = RREG32_PLL(SCLK_CNTL2);
	sclk_cntl2 &= ~REDUCED_SPEED_SCLK_SEL(3);
	sclk_more_cntl = RREG32_PLL(SCLK_MORE_CNTL);
	sclk_more_cntl &= ~VOLTAGE_DELAY_SEL(3);
	if (ps->misc & ATOM_PM_MISCINFO_ASIC_REDUCED_SPEED_SCLK_EN) {
		sclk_more_cntl |= REDUCED_SPEED_SCLK_EN;
		if (ps->misc & ATOM_PM_MISCINFO_DYN_CLK_3D_IDLE)
			sclk_cntl2 |= REDUCED_SPEED_SCLK_MODE;
		else
			sclk_cntl2 &= ~REDUCED_SPEED_SCLK_MODE;
		if (ps->misc & ATOM_PM_MISCINFO_DYNAMIC_CLOCK_DIVIDER_BY_2)
			sclk_cntl2 |= REDUCED_SPEED_SCLK_SEL(0);
		else if (ps->misc & ATOM_PM_MISCINFO_DYNAMIC_CLOCK_DIVIDER_BY_4)
			sclk_cntl2 |= REDUCED_SPEED_SCLK_SEL(2);
	} else
		sclk_more_cntl &= ~REDUCED_SPEED_SCLK_EN;

	if (ps->misc & ATOM_PM_MISCINFO_ASIC_DYNAMIC_VOLTAGE_EN) {
		sclk_more_cntl |= IO_CG_VOLTAGE_DROP;
		if (voltage->delay) {
			sclk_more_cntl |= VOLTAGE_DROP_SYNC;
			switch (voltage->delay) {
			case 33:
				sclk_more_cntl |= VOLTAGE_DELAY_SEL(0);
				break;
			case 66:
				sclk_more_cntl |= VOLTAGE_DELAY_SEL(1);
				break;
			case 99:
				sclk_more_cntl |= VOLTAGE_DELAY_SEL(2);
				break;
			case 132:
				sclk_more_cntl |= VOLTAGE_DELAY_SEL(3);
				break;
			}
		} else
			sclk_more_cntl &= ~VOLTAGE_DROP_SYNC;
	} else
		sclk_more_cntl &= ~IO_CG_VOLTAGE_DROP;

	if (ps->misc & ATOM_PM_MISCINFO_DYNAMIC_HDP_BLOCK_EN)
		sclk_cntl &= ~FORCE_HDP;
	else
		sclk_cntl |= FORCE_HDP;

	WREG32_PLL(SCLK_CNTL, sclk_cntl);
	WREG32_PLL(SCLK_CNTL2, sclk_cntl2);
	WREG32_PLL(SCLK_MORE_CNTL, sclk_more_cntl);

	/* set pcie lanes */
	if ((rdev->flags & RADEON_IS_PCIE) &&
	    !(rdev->flags & RADEON_IS_IGP) &&
	    rdev->asic->pm.set_pcie_lanes &&
	    (ps->pcie_lanes !=
	     rdev->pm.power_state[rdev->pm.current_power_state_index].pcie_lanes)) {
		radeon_set_pcie_lanes(rdev,
				      ps->pcie_lanes);
		DRM_DEBUG_DRIVER("Setting: p: %d\n", ps->pcie_lanes);
	}
}

/**
 * r100_pm_prepare - pre-power state change callback.
 *
 * @rdev: radeon_device pointer
 *
 * Prepare for a power state change (r1xx-r4xx).
 */
void r100_pm_prepare(struct radeon_device *rdev)
{
	struct drm_device *ddev = rdev->ddev;
	struct drm_crtc *crtc;
	struct radeon_crtc *radeon_crtc;
	u32 tmp;

	/* disable any active CRTCs */
	list_for_each_entry(crtc, &ddev->mode_config.crtc_list, head) {
		radeon_crtc = to_radeon_crtc(crtc);
		if (radeon_crtc->enabled) {
			if (radeon_crtc->crtc_id) {
				tmp = RREG32(RADEON_CRTC2_GEN_CNTL);
				tmp |= RADEON_CRTC2_DISP_REQ_EN_B;
				WREG32(RADEON_CRTC2_GEN_CNTL, tmp);
			} else {
				tmp = RREG32(RADEON_CRTC_GEN_CNTL);
				tmp |= RADEON_CRTC_DISP_REQ_EN_B;
				WREG32(RADEON_CRTC_GEN_CNTL, tmp);
			}
		}
	}
}

/**
 * r100_pm_finish - post-power state change callback.
 *
 * @rdev: radeon_device pointer
 *
 * Clean up after a power state change (r1xx-r4xx).
 */
void r100_pm_finish(struct radeon_device *rdev)
{
	struct drm_device *ddev = rdev->ddev;
	struct drm_crtc *crtc;
	struct radeon_crtc *radeon_crtc;
	u32 tmp;

	/* enable any active CRTCs */
	list_for_each_entry(crtc, &ddev->mode_config.crtc_list, head) {
		radeon_crtc = to_radeon_crtc(crtc);
		if (radeon_crtc->enabled) {
			if (radeon_crtc->crtc_id) {
				tmp = RREG32(RADEON_CRTC2_GEN_CNTL);
				tmp &= ~RADEON_CRTC2_DISP_REQ_EN_B;
				WREG32(RADEON_CRTC2_GEN_CNTL, tmp);
			} else {
				tmp = RREG32(RADEON_CRTC_GEN_CNTL);
				tmp &= ~RADEON_CRTC_DISP_REQ_EN_B;
				WREG32(RADEON_CRTC_GEN_CNTL, tmp);
			}
		}
	}
}

/**
 * r100_gui_idle - gui idle callback.
 *
 * @rdev: radeon_device pointer
 *
 * Check of the GUI (2D/3D engines) are idle (r1xx-r5xx).
 * Returns true if idle, false if not.
 */
bool r100_gui_idle(struct radeon_device *rdev)
{
	if (RREG32(RADEON_RBBM_STATUS) & RADEON_RBBM_ACTIVE)
		return false;
	else
		return true;
}

/* hpd for digital panel detect/disconnect */
/**
 * r100_hpd_sense - hpd sense callback.
 *
 * @rdev: radeon_device pointer
 * @hpd: hpd (hotplug detect) pin
 *
 * Checks if a digital monitor is connected (r1xx-r4xx).
 * Returns true if connected, false if not connected.
 */
bool r100_hpd_sense(struct radeon_device *rdev, enum radeon_hpd_id hpd)
{
	bool connected = false;

	switch (hpd) {
	case RADEON_HPD_1:
		if (RREG32(RADEON_FP_GEN_CNTL) & RADEON_FP_DETECT_SENSE)
			connected = true;
		break;
	case RADEON_HPD_2:
		if (RREG32(RADEON_FP2_GEN_CNTL) & RADEON_FP2_DETECT_SENSE)
			connected = true;
		break;
	default:
		break;
	}
	return connected;
}

/**
 * r100_hpd_set_polarity - hpd set polarity callback.
 *
 * @rdev: radeon_device pointer
 * @hpd: hpd (hotplug detect) pin
 *
 * Set the polarity of the hpd pin (r1xx-r4xx).
 */
void r100_hpd_set_polarity(struct radeon_device *rdev,
			   enum radeon_hpd_id hpd)
{
	u32 tmp;
	bool connected = r100_hpd_sense(rdev, hpd);

	switch (hpd) {
	case RADEON_HPD_1:
		tmp = RREG32(RADEON_FP_GEN_CNTL);
		if (connected)
			tmp &= ~RADEON_FP_DETECT_INT_POL;
		else
			tmp |= RADEON_FP_DETECT_INT_POL;
		WREG32(RADEON_FP_GEN_CNTL, tmp);
		break;
	case RADEON_HPD_2:
		tmp = RREG32(RADEON_FP2_GEN_CNTL);
		if (connected)
			tmp &= ~RADEON_FP2_DETECT_INT_POL;
		else
			tmp |= RADEON_FP2_DETECT_INT_POL;
		WREG32(RADEON_FP2_GEN_CNTL, tmp);
		break;
	default:
		break;
	}
}

/**
 * r100_hpd_init - hpd setup callback.
 *
 * @rdev: radeon_device pointer
 *
 * Setup the hpd pins used by the card (r1xx-r4xx).
 * Set the polarity, and enable the hpd interrupts.
 */
void r100_hpd_init(struct radeon_device *rdev)
{
	struct drm_device *dev = rdev->ddev;
	struct drm_connector *connector;
	unsigned enable = 0;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		struct radeon_connector *radeon_connector = to_radeon_connector(connector);
		if (radeon_connector->hpd.hpd != RADEON_HPD_NONE)
			enable |= 1 << radeon_connector->hpd.hpd;
		radeon_hpd_set_polarity(rdev, radeon_connector->hpd.hpd);
	}
	radeon_irq_kms_enable_hpd(rdev, enable);
}

/**
 * r100_hpd_fini - hpd tear down callback.
 *
 * @rdev: radeon_device pointer
 *
 * Tear down the hpd pins used by the card (r1xx-r4xx).
 * Disable the hpd interrupts.
 */
void r100_hpd_fini(struct radeon_device *rdev)
{
	struct drm_device *dev = rdev->ddev;
	struct drm_connector *connector;
	unsigned disable = 0;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		struct radeon_connector *radeon_connector = to_radeon_connector(connector);
		if (radeon_connector->hpd.hpd != RADEON_HPD_NONE)
			disable |= 1 << radeon_connector->hpd.hpd;
	}
	radeon_irq_kms_disable_hpd(rdev, disable);
}

/*
 * PCI GART
 */
void r100_pci_gart_tlb_flush(struct radeon_device *rdev)
{
	/* TODO: can we do somethings here ? */
	/* It seems hw only cache one entry so we should discard this
	 * entry otherwise if first GPU GART read hit this entry it
	 * could end up in wrong address. */
}

int r100_pci_gart_init(struct radeon_device *rdev)
{
	int r;

	if (rdev->gart.ptr) {
		WARN(1, "R100 PCI GART already initialized\n");
		return 0;
	}
	/* Initialize common gart structure */
	r = radeon_gart_init(rdev);
	if (r)
		return r;
	rdev->gart.table_size = rdev->gart.num_gpu_pages * 4;
	rdev->asic->gart.tlb_flush = &r100_pci_gart_tlb_flush;
	rdev->asic->gart.get_page_entry = &r100_pci_gart_get_page_entry;
	rdev->asic->gart.set_page = &r100_pci_gart_set_page;
	return radeon_gart_table_ram_alloc(rdev);
}

int r100_pci_gart_enable(struct radeon_device *rdev)
{
	uint32_t tmp;

	/* discard memory request outside of configured range */
	tmp = RREG32(RADEON_AIC_CNTL) | RADEON_DIS_OUT_OF_PCI_GART_ACCESS;
	WREG32(RADEON_AIC_CNTL, tmp);
	/* set address range for PCI address translate */
	WREG32(RADEON_AIC_LO_ADDR, rdev->mc.gtt_start);
	WREG32(RADEON_AIC_HI_ADDR, rdev->mc.gtt_end);
	/* set PCI GART page-table base address */
	WREG32(RADEON_AIC_PT_BASE, rdev->gart.table_addr);
	tmp = RREG32(RADEON_AIC_CNTL) | RADEON_PCIGART_TRANSLATE_EN;
	WREG32(RADEON_AIC_CNTL, tmp);
	r100_pci_gart_tlb_flush(rdev);
	DRM_INFO("PCI GART of %uM enabled (table at 0x%016llX).\n",
		 (unsigned)(rdev->mc.gtt_size >> 20),
		 (unsigned long long)rdev->gart.table_addr);
	rdev->gart.ready = true;
	return 0;
}

void r100_pci_gart_disable(struct radeon_device *rdev)
{
	uint32_t tmp;

	/* discard memory request outside of configured range */
	tmp = RREG32(RADEON_AIC_CNTL) | RADEON_DIS_OUT_OF_PCI_GART_ACCESS;
	WREG32(RADEON_AIC_CNTL, tmp & ~RADEON_PCIGART_TRANSLATE_EN);
	WREG32(RADEON_AIC_LO_ADDR, 0);
	WREG32(RADEON_AIC_HI_ADDR, 0);
}

uint64_t r100_pci_gart_get_page_entry(uint64_t addr, uint32_t flags)
{
	return addr;
}

void r100_pci_gart_set_page(struct radeon_device *rdev, unsigned i,
			    uint64_t entry)
{
	u32 *gtt = rdev->gart.ptr;
	gtt[i] = cpu_to_le32(lower_32_bits(entry));
}

void r100_pci_gart_fini(struct radeon_device *rdev)
{
	radeon_gart_fini(rdev);
	r100_pci_gart_disable(rdev);
	radeon_gart_table_ram_free(rdev);
}

int r100_irq_set(struct radeon_device *rdev)
{
	uint32_t tmp = 0;

	if (!rdev->irq.installed) {
		WARN(1, "Can't enable IRQ/MSI because no handler is installed\n");
		WREG32(R_000040_GEN_INT_CNTL, 0);
		return -EINVAL;
	}
	if (atomic_read(&rdev->irq.ring_int[RADEON_RING_TYPE_GFX_INDEX])) {
		tmp |= RADEON_SW_INT_ENABLE;
	}
	if (rdev->irq.crtc_vblank_int[0] ||
	    atomic_read(&rdev->irq.pflip[0])) {
		tmp |= RADEON_CRTC_VBLANK_MASK;
	}
	if (rdev->irq.crtc_vblank_int[1] ||
	    atomic_read(&rdev->irq.pflip[1])) {
		tmp |= RADEON_CRTC2_VBLANK_MASK;
	}
	if (rdev->irq.hpd[0]) {
		tmp |= RADEON_FP_DETECT_MASK;
	}
	if (rdev->irq.hpd[1]) {
		tmp |= RADEON_FP2_DETECT_MASK;
	}
	WREG32(RADEON_GEN_INT_CNTL, tmp);

	/* read back to post the write */
	RREG32(RADEON_GEN_INT_CNTL);

	return 0;
}

void r100_irq_disable(struct radeon_device *rdev)
{
	u32 tmp;

	WREG32(R_000040_GEN_INT_CNTL, 0);
	/* Wait and acknowledge irq */
	mdelay(1);
	tmp = RREG32(R_000044_GEN_INT_STATUS);
	WREG32(R_000044_GEN_INT_STATUS, tmp);
}

static uint32_t r100_irq_ack(struct radeon_device *rdev)
{
	uint32_t irqs = RREG32(RADEON_GEN_INT_STATUS);
	uint32_t irq_mask = RADEON_SW_INT_TEST |
		RADEON_CRTC_VBLANK_STAT | RADEON_CRTC2_VBLANK_STAT |
		RADEON_FP_DETECT_STAT | RADEON_FP2_DETECT_STAT;

	if (irqs) {
		WREG32(RADEON_GEN_INT_STATUS, irqs);
	}
	return irqs & irq_mask;
}

int r100_irq_process(struct radeon_device *rdev)
{
	uint32_t status, msi_rearm;
	bool queue_hotplug = false;

	status = r100_irq_ack(rdev);
	if (!status) {
		return IRQ_NONE;
	}
	if (rdev->shutdown) {
		return IRQ_NONE;
	}
	while (status) {
		/* SW interrupt */
		if (status & RADEON_SW_INT_TEST) {
			radeon_fence_process(rdev, RADEON_RING_TYPE_GFX_INDEX);
		}
		/* Vertical blank interrupts */
		if (status & RADEON_CRTC_VBLANK_STAT) {
			if (rdev->irq.crtc_vblank_int[0]) {
				drm_handle_vblank(rdev->ddev, 0);
				rdev->pm.vblank_sync = true;
				wake_up(&rdev->irq.vblank_queue);
			}
			if (atomic_read(&rdev->irq.pflip[0]))
				radeon_crtc_handle_vblank(rdev, 0);
		}
		if (status & RADEON_CRTC2_VBLANK_STAT) {
			if (rdev->irq.crtc_vblank_int[1]) {
				drm_handle_vblank(rdev->ddev, 1);
				rdev->pm.vblank_sync = true;
				wake_up(&rdev->irq.vblank_queue);
			}
			if (atomic_read(&rdev->irq.pflip[1]))
				radeon_crtc_handle_vblank(rdev, 1);
		}
		if (status & RADEON_FP_DETECT_STAT) {
			queue_hotplug = true;
			DRM_DEBUG("HPD1\n");
		}
		if (status & RADEON_FP2_DETECT_STAT) {
			queue_hotplug = true;
			DRM_DEBUG("HPD2\n");
		}
		status = r100_irq_ack(rdev);
	}
	if (queue_hotplug)
		schedule_delayed_work(&rdev->hotplug_work, 0);
	if (rdev->msi_enabled) {
		switch (rdev->family) {
		case CHIP_RS400:
		case CHIP_RS480:
			msi_rearm = RREG32(RADEON_AIC_CNTL) & ~RS400_MSI_REARM;
			WREG32(RADEON_AIC_CNTL, msi_rearm);
			WREG32(RADEON_AIC_CNTL, msi_rearm | RS400_MSI_REARM);
			break;
		default:
			WREG32(RADEON_MSI_REARM_EN, RV370_MSI_REARM_EN);
			break;
		}
	}
	return IRQ_HANDLED;
}

u32 r100_get_vblank_counter(struct radeon_device *rdev, int crtc)
{
	if (crtc == 0)
		return RREG32(RADEON_CRTC_CRNT_FRAME);
	else
		return RREG32(RADEON_CRTC2_CRNT_FRAME);
}

/**
 * r100_ring_hdp_flush - flush Host Data Path via the ring buffer
 * rdev: radeon device structure
 * ring: ring buffer struct for emitting packets
 */
static void r100_ring_hdp_flush(struct radeon_device *rdev, struct radeon_ring *ring)
{
	radeon_ring_write(ring, PACKET0(RADEON_HOST_PATH_CNTL, 0));
	radeon_ring_write(ring, rdev->config.r100.hdp_cntl |
				RADEON_HDP_READ_BUFFER_INVALIDATE);
	radeon_ring_write(ring, PACKET0(RADEON_HOST_PATH_CNTL, 0));
	radeon_ring_write(ring, rdev->config.r100.hdp_cntl);
}

/* Who ever call radeon_fence_emit should call ring_lock and ask
 * for enough space (today caller are ib schedule and buffer move) */
void r100_fence_ring_emit(struct radeon_device *rdev,
			  struct radeon_fence *fence)
{
	struct radeon_ring *ring = &rdev->ring[fence->ring];

	/* We have to make sure that caches are flushed before
	 * CPU might read something from VRAM. */
	radeon_ring_write(ring, PACKET0(RADEON_RB3D_DSTCACHE_CTLSTAT, 0));
	radeon_ring_write(ring, RADEON_RB3D_DC_FLUSH_ALL);
	radeon_ring_write(ring, PACKET0(RADEON_RB3D_ZCACHE_CTLSTAT, 0));
	radeon_ring_write(ring, RADEON_RB3D_ZC_FLUSH_ALL);
	/* Wait until IDLE & CLEAN */
	radeon_ring_write(ring, PACKET0(RADEON_WAIT_UNTIL, 0));
	radeon_ring_write(ring, RADEON_WAIT_2D_IDLECLEAN | RADEON_WAIT_3D_IDLECLEAN);
	r100_ring_hdp_flush(rdev, ring);
	/* Emit fence sequence & fire IRQ */
	radeon_ring_write(ring, PACKET0(rdev->fence_drv[fence->ring].scratch_reg, 0));
	radeon_ring_write(ring, fence->seq);
	radeon_ring_write(ring, PACKET0(RADEON_GEN_INT_STATUS, 0));
	radeon_ring_write(ring, RADEON_SW_INT_FIRE);
}

bool r100_semaphore_ring_emit(struct radeon_device *rdev,
			      struct radeon_ring *ring,
			      struct radeon_semaphore *semaphore,
			      bool emit_wait)
{
	/* Unused on older asics, since we don't have semaphores or multiple rings */
	BUG();
	return false;
}

struct radeon_fence *r100_copy_blit(struct radeon_device *rdev,
				    uint64_t src_offset,
				    uint64_t dst_offset,
				    unsigned num_gpu_pages,
				    struct reservation_object *resv)
{
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	struct radeon_fence *fence;
	uint32_t cur_pages;
	uint32_t stride_bytes = RADEON_GPU_PAGE_SIZE;
	uint32_t pitch;
	uint32_t stride_pixels;
	unsigned ndw;
	int num_loops;
	int r = 0;

	/* radeon limited to 16k stride */
	stride_bytes &= 0x3fff;
	/* radeon pitch is /64 */
	pitch = stride_bytes / 64;
	stride_pixels = stride_bytes / 4;
	num_loops = DIV_ROUND_UP(num_gpu_pages, 8191);

	/* Ask for enough room for blit + flush + fence */
	ndw = 64 + (10 * num_loops);
	r = radeon_ring_lock(rdev, ring, ndw);
	if (r) {
		DRM_ERROR("radeon: moving bo (%d) asking for %u dw.\n", r, ndw);
		return ERR_PTR(-EINVAL);
	}
	while (num_gpu_pages > 0) {
		cur_pages = num_gpu_pages;
		if (cur_pages > 8191) {
			cur_pages = 8191;
		}
		num_gpu_pages -= cur_pages;

		/* pages are in Y direction - height
		   page width in X direction - width */
		radeon_ring_write(ring, PACKET3(PACKET3_BITBLT_MULTI, 8));
		radeon_ring_write(ring,
				  RADEON_GMC_SRC_PITCH_OFFSET_CNTL |
				  RADEON_GMC_DST_PITCH_OFFSET_CNTL |
				  RADEON_GMC_SRC_CLIPPING |
				  RADEON_GMC_DST_CLIPPING |
				  RADEON_GMC_BRUSH_NONE |
				  (RADEON_COLOR_FORMAT_ARGB8888 << 8) |
				  RADEON_GMC_SRC_DATATYPE_COLOR |
				  RADEON_ROP3_S |
				  RADEON_DP_SRC_SOURCE_MEMORY |
				  RADEON_GMC_CLR_CMP_CNTL_DIS |
				  RADEON_GMC_WR_MSK_DIS);
		radeon_ring_write(ring, (pitch << 22) | (src_offset >> 10));
		radeon_ring_write(ring, (pitch << 22) | (dst_offset >> 10));
		radeon_ring_write(ring, (0x1fff) | (0x1fff << 16));
		radeon_ring_write(ring, 0);
		radeon_ring_write(ring, (0x1fff) | (0x1fff << 16));
		radeon_ring_write(ring, num_gpu_pages);
		radeon_ring_write(ring, num_gpu_pages);
		radeon_ring_write(ring, cur_pages | (stride_pixels << 16));
	}
	radeon_ring_write(ring, PACKET0(RADEON_DSTCACHE_CTLSTAT, 0));
	radeon_ring_write(ring, RADEON_RB2D_DC_FLUSH_ALL);
	radeon_ring_write(ring, PACKET0(RADEON_WAIT_UNTIL, 0));
	radeon_ring_write(ring,
			  RADEON_WAIT_2D_IDLECLEAN |
			  RADEON_WAIT_HOST_IDLECLEAN |
			  RADEON_WAIT_DMA_GUI_IDLE);
	r = radeon_fence_emit(rdev, &fence, RADEON_RING_TYPE_GFX_INDEX);
	if (r) {
		radeon_ring_unlock_undo(rdev, ring);
		return ERR_PTR(r);
	}
	radeon_ring_unlock_commit(rdev, ring, false);
	return fence;
}

static int r100_cp_wait_for_idle(struct radeon_device *rdev)
{
	unsigned i;
	u32 tmp;

	for (i = 0; i < rdev->usec_timeout; i++) {
		tmp = RREG32(R_000E40_RBBM_STATUS);
		if (!G_000E40_CP_CMDSTRM_BUSY(tmp)) {
			return 0;
		}
		udelay(1);
	}
	return -1;
}

void r100_ring_start(struct radeon_device *rdev, struct radeon_ring *ring)
{
	int r;

	r = radeon_ring_lock(rdev, ring, 2);
	if (r) {
		return;
	}
	radeon_ring_write(ring, PACKET0(RADEON_ISYNC_CNTL, 0));
	radeon_ring_write(ring,
			  RADEON_ISYNC_ANY2D_IDLE3D |
			  RADEON_ISYNC_ANY3D_IDLE2D |
			  RADEON_ISYNC_WAIT_IDLEGUI |
			  RADEON_ISYNC_CPSCRATCH_IDLEGUI);
	radeon_ring_unlock_commit(rdev, ring, false);
}


/* Load the microcode for the CP */
static int r100_cp_init_microcode(struct radeon_device *rdev)
{
	const char *fw_name = NULL;
	int err;

	DRM_DEBUG_KMS("\n");

	if ((rdev->family == CHIP_R100) || (rdev->family == CHIP_RV100) ||
	    (rdev->family == CHIP_RV200) || (rdev->family == CHIP_RS100) ||
	    (rdev->family == CHIP_RS200)) {
		DRM_INFO("Loading R100 Microcode\n");
		fw_name = FIRMWARE_R100;
	} else if ((rdev->family == CHIP_R200) ||
		   (rdev->family == CHIP_RV250) ||
		   (rdev->family == CHIP_RV280) ||
		   (rdev->family == CHIP_RS300)) {
		DRM_INFO("Loading R200 Microcode\n");
		fw_name = FIRMWARE_R200;
	} else if ((rdev->family == CHIP_R300) ||
		   (rdev->family == CHIP_R350) ||
		   (rdev->family == CHIP_RV350) ||
		   (rdev->family == CHIP_RV380) ||
		   (rdev->family == CHIP_RS400) ||
		   (rdev->family == CHIP_RS480)) {
		DRM_INFO("Loading R300 Microcode\n");
		fw_name = FIRMWARE_R300;
	} else if ((rdev->family == CHIP_R420) ||
		   (rdev->family == CHIP_R423) ||
		   (rdev->family == CHIP_RV410)) {
		DRM_INFO("Loading R400 Microcode\n");
		fw_name = FIRMWARE_R420;
	} else if ((rdev->family == CHIP_RS690) ||
		   (rdev->family == CHIP_RS740)) {
		DRM_INFO("Loading RS690/RS740 Microcode\n");
		fw_name = FIRMWARE_RS690;
	} else if (rdev->family == CHIP_RS600) {
		DRM_INFO("Loading RS600 Microcode\n");
		fw_name = FIRMWARE_RS600;
	} else if ((rdev->family == CHIP_RV515) ||
		   (rdev->family == CHIP_R520) ||
		   (rdev->family == CHIP_RV530) ||
		   (rdev->family == CHIP_R580) ||
		   (rdev->family == CHIP_RV560) ||
		   (rdev->family == CHIP_RV570)) {
		DRM_INFO("Loading R500 Microcode\n");
		fw_name = FIRMWARE_R520;
	}

	err = request_firmware(&rdev->me_fw, fw_name, rdev->dev);
	if (err) {
		printk(KERN_ERR "radeon_cp: Failed to load firmware \"%s\"\n",
		       fw_name);
	} else if (rdev->me_fw->size % 8) {
		printk(KERN_ERR
		       "radeon_cp: Bogus length %zu in firmware \"%s\"\n",
		       rdev->me_fw->size, fw_name);
		err = -EINVAL;
		release_firmware(rdev->me_fw);
		rdev->me_fw = NULL;
	}
	return err;
}

u32 r100_gfx_get_rptr(struct radeon_device *rdev,
		      struct radeon_ring *ring)
{
	u32 rptr;

	if (rdev->wb.enabled)
		rptr = le32_to_cpu(rdev->wb.wb[ring->rptr_offs/4]);
	else
		rptr = RREG32(RADEON_CP_RB_RPTR);

	return rptr;
}

u32 r100_gfx_get_wptr(struct radeon_device *rdev,
		      struct radeon_ring *ring)
{
	return RREG32(RADEON_CP_RB_WPTR);
}

void r100_gfx_set_wptr(struct radeon_device *rdev,
		       struct radeon_ring *ring)
{
	WREG32(RADEON_CP_RB_WPTR, ring->wptr);
	(void)RREG32(RADEON_CP_RB_WPTR);
}

static void r100_cp_load_microcode(struct radeon_device *rdev)
{
	const __be32 *fw_data;
	int i, size;

	if (r100_gui_wait_for_idle(rdev)) {
		printk(KERN_WARNING "Failed to wait GUI idle while "
		       "programming pipes. Bad things might happen.\n");
	}

	if (rdev->me_fw) {
		size = rdev->me_fw->size / 4;
		fw_data = (const __be32 *)&rdev->me_fw->data[0];
		WREG32(RADEON_CP_ME_RAM_ADDR, 0);
		for (i = 0; i < size; i += 2) {
			WREG32(RADEON_CP_ME_RAM_DATAH,
			       be32_to_cpup(&fw_data[i]));
			WREG32(RADEON_CP_ME_RAM_DATAL,
			       be32_to_cpup(&fw_data[i + 1]));
		}
	}
}

int r100_cp_init(struct radeon_device *rdev, unsigned ring_size)
{
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	unsigned rb_bufsz;
	unsigned rb_blksz;
	unsigned max_fetch;
	unsigned pre_write_timer;
	unsigned pre_write_limit;
	unsigned indirect2_start;
	unsigned indirect1_start;
	uint32_t tmp;
	int r;

	if (r100_debugfs_cp_init(rdev)) {
		DRM_ERROR("Failed to register debugfs file for CP !\n");
	}
	if (!rdev->me_fw) {
		r = r100_cp_init_microcode(rdev);
		if (r) {
			DRM_ERROR("Failed to load firmware!\n");
			return r;
		}
	}

	/* Align ring size */
	rb_bufsz = order_base_2(ring_size / 8);
	ring_size = (1 << (rb_bufsz + 1)) * 4;
	r100_cp_load_microcode(rdev);
	r = radeon_ring_init(rdev, ring, ring_size, RADEON_WB_CP_RPTR_OFFSET,
			     RADEON_CP_PACKET2);
	if (r) {
		return r;
	}
	/* Each time the cp read 1024 bytes (16 dword/quadword) update
	 * the rptr copy in system ram */
	rb_blksz = 9;
	/* cp will read 128bytes at a time (4 dwords) */
	max_fetch = 1;
	ring->align_mask = 16 - 1;
	/* Write to CP_RB_WPTR will be delayed for pre_write_timer clocks */
	pre_write_timer = 64;
	/* Force CP_RB_WPTR write if written more than one time before the
	 * delay expire
	 */
	pre_write_limit = 0;
	/* Setup the cp cache like this (cache size is 96 dwords) :
	 *	RING		0  to 15
	 *	INDIRECT1	16 to 79
	 *	INDIRECT2	80 to 95
	 * So ring cache size is 16dwords (> (2 * max_fetch = 2 * 4dwords))
	 *    indirect1 cache size is 64dwords (> (2 * max_fetch = 2 * 4dwords))
	 *    indirect2 cache size is 16dwords (> (2 * max_fetch = 2 * 4dwords))
	 * Idea being that most of the gpu cmd will be through indirect1 buffer
	 * so it gets the bigger cache.
	 */
	indirect2_start = 80;
	indirect1_start = 16;
	/* cp setup */
	WREG32(0x718, pre_write_timer | (pre_write_limit << 28));
	tmp = (REG_SET(RADEON_RB_BUFSZ, rb_bufsz) |
	       REG_SET(RADEON_RB_BLKSZ, rb_blksz) |
	       REG_SET(RADEON_MAX_FETCH, max_fetch));
#ifdef __BIG_ENDIAN
	tmp |= RADEON_BUF_SWAP_32BIT;
#endif
	WREG32(RADEON_CP_RB_CNTL, tmp | RADEON_RB_NO_UPDATE);

	/* Set ring address */
	DRM_INFO("radeon: ring at 0x%016lX\n", (unsigned long)ring->gpu_addr);
	WREG32(RADEON_CP_RB_BASE, ring->gpu_addr);
	/* Force read & write ptr to 0 */
	WREG32(RADEON_CP_RB_CNTL, tmp | RADEON_RB_RPTR_WR_ENA | RADEON_RB_NO_UPDATE);
	WREG32(RADEON_CP_RB_RPTR_WR, 0);
	ring->wptr = 0;
	WREG32(RADEON_CP_RB_WPTR, ring->wptr);

	/* set the wb address whether it's enabled or not */
	WREG32(R_00070C_CP_RB_RPTR_ADDR,
		S_00070C_RB_RPTR_ADDR((rdev->wb.gpu_addr + RADEON_WB_CP_RPTR_OFFSET) >> 2));
	WREG32(R_000774_SCRATCH_ADDR, rdev->wb.gpu_addr + RADEON_WB_SCRATCH_OFFSET);

	if (rdev->wb.enabled)
		WREG32(R_000770_SCRATCH_UMSK, 0xff);
	else {
		tmp |= RADEON_RB_NO_UPDATE;
		WREG32(R_000770_SCRATCH_UMSK, 0);
	}

	WREG32(RADEON_CP_RB_CNTL, tmp);
	udelay(10);
	/* Set cp mode to bus mastering & enable cp*/
	WREG32(RADEON_CP_CSQ_MODE,
	       REG_SET(RADEON_INDIRECT2_START, indirect2_start) |
	       REG_SET(RADEON_INDIRECT1_START, indirect1_start));
	WREG32(RADEON_CP_RB_WPTR_DELAY, 0);
	WREG32(RADEON_CP_CSQ_MODE, 0x00004D4D);
	WREG32(RADEON_CP_CSQ_CNTL, RADEON_CSQ_PRIBM_INDBM);

	/* at this point everything should be setup correctly to enable master */
	pci_set_master(rdev->pdev);

	radeon_ring_start(rdev, RADEON_RING_TYPE_GFX_INDEX, &rdev->ring[RADEON_RING_TYPE_GFX_INDEX]);
	r = radeon_ring_test(rdev, RADEON_RING_TYPE_GFX_INDEX, ring);
	if (r) {
		DRM_ERROR("radeon: cp isn't working (%d).\n", r);
		return r;
	}
	ring->ready = true;
	radeon_ttm_set_active_vram_size(rdev, rdev->mc.real_vram_size);

	if (!ring->rptr_save_reg /* not resuming from suspend */
	    && radeon_ring_supports_scratch_reg(rdev, ring)) {
		r = radeon_scratch_get(rdev, &ring->rptr_save_reg);
		if (r) {
			DRM_ERROR("failed to get scratch reg for rptr save (%d).\n", r);
			ring->rptr_save_reg = 0;
		}
	}
	return 0;
}

void r100_cp_fini(struct radeon_device *rdev)
{
	if (r100_cp_wait_for_idle(rdev)) {
		DRM_ERROR("Wait for CP idle timeout, shutting down CP.\n");
	}
	/* Disable ring */
	r100_cp_disable(rdev);
	radeon_scratch_free(rdev, rdev->ring[RADEON_RING_TYPE_GFX_INDEX].rptr_save_reg);
	radeon_ring_fini(rdev, &rdev->ring[RADEON_RING_TYPE_GFX_INDEX]);
	DRM_INFO("radeon: cp finalized\n");
}

void r100_cp_disable(struct radeon_device *rdev)
{
	/* Disable ring */
	radeon_ttm_set_active_vram_size(rdev, rdev->mc.visible_vram_size);
	rdev->ring[RADEON_RING_TYPE_GFX_INDEX].ready = false;
	WREG32(RADEON_CP_CSQ_MODE, 0);
	WREG32(RADEON_CP_CSQ_CNTL, 0);
	WREG32(R_000770_SCRATCH_UMSK, 0);
	if (r100_gui_wait_for_idle(rdev)) {
		printk(KERN_WARNING "Failed to wait GUI idle while "
		       "programming pipes. Bad things might happen.\n");
	}
}

/*
 * CS functions
 */
int r100_reloc_pitch_offset(struct radeon_cs_parser *p,
			    struct radeon_cs_packet *pkt,
			    unsigned idx,
			    unsigned reg)
{
	int r;
	u32 tile_flags = 0;
	u32 tmp;
	struct radeon_bo_list *reloc;
	u32 value;

	r = radeon_cs_packet_next_reloc(p, &reloc, 0);
	if (r) {
		DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
			  idx, reg);
		radeon_cs_dump_packet(p, pkt);
		return r;
	}

	value = radeon_get_ib_value(p, idx);
	tmp = value & 0x003fffff;
	tmp += (((u32)reloc->gpu_offset) >> 10);

	if (!(p->cs_flags & RADEON_CS_KEEP_TILING_FLAGS)) {
		if (reloc->tiling_flags & RADEON_TILING_MACRO)
			tile_flags |= RADEON_DST_TILE_MACRO;
		if (reloc->tiling_flags & RADEON_TILING_MICRO) {
			if (reg == RADEON_SRC_PITCH_OFFSET) {
				DRM_ERROR("Cannot src blit from microtiled surface\n");
				radeon_cs_dump_packet(p, pkt);
				return -EINVAL;
			}
			tile_flags |= RADEON_DST_TILE_MICRO;
		}

		tmp |= tile_flags;
		p->ib.ptr[idx] = (value & 0x3fc00000) | tmp;
	} else
		p->ib.ptr[idx] = (value & 0xffc00000) | tmp;
	return 0;
}

int r100_packet3_load_vbpntr(struct radeon_cs_parser *p,
			     struct radeon_cs_packet *pkt,
			     int idx)
{
	unsigned c, i;
	struct radeon_bo_list *reloc;
	struct r100_cs_track *track;
	int r = 0;
	volatile uint32_t *ib;
	u32 idx_value;

	ib = p->ib.ptr;
	track = (struct r100_cs_track *)p->track;
	c = radeon_get_ib_value(p, idx++) & 0x1F;
	if (c > 16) {
	    DRM_ERROR("Only 16 vertex buffers are allowed %d\n",
		      pkt->opcode);
	    radeon_cs_dump_packet(p, pkt);
	    return -EINVAL;
	}
	track->num_arrays = c;
	for (i = 0; i < (c - 1); i+=2, idx+=3) {
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("No reloc for packet3 %d\n",
				  pkt->opcode);
			radeon_cs_dump_packet(p, pkt);
			return r;
		}
		idx_value = radeon_get_ib_value(p, idx);
		ib[idx+1] = radeon_get_ib_value(p, idx + 1) + ((u32)reloc->gpu_offset);

		track->arrays[i + 0].esize = idx_value >> 8;
		track->arrays[i + 0].robj = reloc->robj;
		track->arrays[i + 0].esize &= 0x7F;
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("No reloc for packet3 %d\n",
				  pkt->opcode);
			radeon_cs_dump_packet(p, pkt);
			return r;
		}
		ib[idx+2] = radeon_get_ib_value(p, idx + 2) + ((u32)reloc->gpu_offset);
		track->arrays[i + 1].robj = reloc->robj;
		track->arrays[i + 1].esize = idx_value >> 24;
		track->arrays[i + 1].esize &= 0x7F;
	}
	if (c & 1) {
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("No reloc for packet3 %d\n",
					  pkt->opcode);
			radeon_cs_dump_packet(p, pkt);
			return r;
		}
		idx_value = radeon_get_ib_value(p, idx);
		ib[idx+1] = radeon_get_ib_value(p, idx + 1) + ((u32)reloc->gpu_offset);
		track->arrays[i + 0].robj = reloc->robj;
		track->arrays[i + 0].esize = idx_value >> 8;
		track->arrays[i + 0].esize &= 0x7F;
	}
	return r;
}

int r100_cs_parse_packet0(struct radeon_cs_parser *p,
			  struct radeon_cs_packet *pkt,
			  const unsigned *auth, unsigned n,
			  radeon_packet0_check_t check)
{
	unsigned reg;
	unsigned i, j, m;
	unsigned idx;
	int r;

	idx = pkt->idx + 1;
	reg = pkt->reg;
	/* Check that register fall into register range
	 * determined by the number of entry (n) in the
	 * safe register bitmap.
	 */
	if (pkt->one_reg_wr) {
		if ((reg >> 7) > n) {
			return -EINVAL;
		}
	} else {
		if (((reg + (pkt->count << 2)) >> 7) > n) {
			return -EINVAL;
		}
	}
	for (i = 0; i <= pkt->count; i++, idx++) {
		j = (reg >> 7);
		m = 1 << ((reg >> 2) & 31);
		if (auth[j] & m) {
			r = check(p, pkt, idx, reg);
			if (r) {
				return r;
			}
		}
		if (pkt->one_reg_wr) {
			if (!(auth[j] & m)) {
				break;
			}
		} else {
			reg += 4;
		}
	}
	return 0;
}

/**
 * r100_cs_packet_next_vline() - parse userspace VLINE packet
 * @parser:		parser structure holding parsing context.
 *
 * Userspace sends a special sequence for VLINE waits.
 * PACKET0 - VLINE_START_END + value
 * PACKET0 - WAIT_UNTIL +_value
 * RELOC (P3) - crtc_id in reloc.
 *
 * This function parses this and relocates the VLINE START END
 * and WAIT UNTIL packets to the correct crtc.
 * It also detects a switched off crtc and nulls out the
 * wait in that case.
 */
int r100_cs_packet_parse_vline(struct radeon_cs_parser *p)
{
	struct drm_crtc *crtc;
	struct radeon_crtc *radeon_crtc;
	struct radeon_cs_packet p3reloc, waitreloc;
	int crtc_id;
	int r;
	uint32_t header, h_idx, reg;
	volatile uint32_t *ib;

	ib = p->ib.ptr;

	/* parse the wait until */
	r = radeon_cs_packet_parse(p, &waitreloc, p->idx);
	if (r)
		return r;

	/* check its a wait until and only 1 count */
	if (waitreloc.reg != RADEON_WAIT_UNTIL ||
	    waitreloc.count != 0) {
		DRM_ERROR("vline wait had illegal wait until segment\n");
		return -EINVAL;
	}

	if (radeon_get_ib_value(p, waitreloc.idx + 1) != RADEON_WAIT_CRTC_VLINE) {
		DRM_ERROR("vline wait had illegal wait until\n");
		return -EINVAL;
	}

	/* jump over the NOP */
	r = radeon_cs_packet_parse(p, &p3reloc, p->idx + waitreloc.count + 2);
	if (r)
		return r;

	h_idx = p->idx - 2;
	p->idx += waitreloc.count + 2;
	p->idx += p3reloc.count + 2;

	header = radeon_get_ib_value(p, h_idx);
	crtc_id = radeon_get_ib_value(p, h_idx + 5);
	reg = R100_CP_PACKET0_GET_REG(header);
	crtc = drm_crtc_find(p->rdev->ddev, crtc_id);
	if (!crtc) {
		DRM_ERROR("cannot find crtc %d\n", crtc_id);
		return -ENOENT;
	}
	radeon_crtc = to_radeon_crtc(crtc);
	crtc_id = radeon_crtc->crtc_id;

	if (!crtc->enabled) {
		/* if the CRTC isn't enabled - we need to nop out the wait until */
		ib[h_idx + 2] = PACKET2(0);
		ib[h_idx + 3] = PACKET2(0);
	} else if (crtc_id == 1) {
		switch (reg) {
		case AVIVO_D1MODE_VLINE_START_END:
			header &= ~R300_CP_PACKET0_REG_MASK;
			header |= AVIVO_D2MODE_VLINE_START_END >> 2;
			break;
		case RADEON_CRTC_GUI_TRIG_VLINE:
			header &= ~R300_CP_PACKET0_REG_MASK;
			header |= RADEON_CRTC2_GUI_TRIG_VLINE >> 2;
			break;
		default:
			DRM_ERROR("unknown crtc reloc\n");
			return -EINVAL;
		}
		ib[h_idx] = header;
		ib[h_idx + 3] |= RADEON_ENG_DISPLAY_SELECT_CRTC1;
	}

	return 0;
}

static int r100_get_vtx_size(uint32_t vtx_fmt)
{
	int vtx_size;
	vtx_size = 2;
	/* ordered according to bits in spec */
	if (vtx_fmt & RADEON_SE_VTX_FMT_W0)
		vtx_size++;
	if (vtx_fmt & RADEON_SE_VTX_FMT_FPCOLOR)
		vtx_size += 3;
	if (vtx_fmt & RADEON_SE_VTX_FMT_FPALPHA)
		vtx_size++;
	if (vtx_fmt & RADEON_SE_VTX_FMT_PKCOLOR)
		vtx_size++;
	if (vtx_fmt & RADEON_SE_VTX_FMT_FPSPEC)
		vtx_size += 3;
	if (vtx_fmt & RADEON_SE_VTX_FMT_FPFOG)
		vtx_size++;
	if (vtx_fmt & RADEON_SE_VTX_FMT_PKSPEC)
		vtx_size++;
	if (vtx_fmt & RADEON_SE_VTX_FMT_ST0)
		vtx_size += 2;
	if (vtx_fmt & RADEON_SE_VTX_FMT_ST1)
		vtx_size += 2;
	if (vtx_fmt & RADEON_SE_VTX_FMT_Q1)
		vtx_size++;
	if (vtx_fmt & RADEON_SE_VTX_FMT_ST2)
		vtx_size += 2;
	if (vtx_fmt & RADEON_SE_VTX_FMT_Q2)
		vtx_size++;
	if (vtx_fmt & RADEON_SE_VTX_FMT_ST3)
		vtx_size += 2;
	if (vtx_fmt & RADEON_SE_VTX_FMT_Q3)
		vtx_size++;
	if (vtx_fmt & RADEON_SE_VTX_FMT_Q0)
		vtx_size++;
	/* blend weight */
	if (vtx_fmt & (0x7 << 15))
		vtx_size += (vtx_fmt >> 15) & 0x7;
	if (vtx_fmt & RADEON_SE_VTX_FMT_N0)
		vtx_size += 3;
	if (vtx_fmt & RADEON_SE_VTX_FMT_XY1)
		vtx_size += 2;
	if (vtx_fmt & RADEON_SE_VTX_FMT_Z1)
		vtx_size++;
	if (vtx_fmt & RADEON_SE_VTX_FMT_W1)
		vtx_size++;
	if (vtx_fmt & RADEON_SE_VTX_FMT_N1)
		vtx_size++;
	if (vtx_fmt & RADEON_SE_VTX_FMT_Z)
		vtx_size++;
	return vtx_size;
}

static int r100_packet0_check(struct radeon_cs_parser *p,
			      struct radeon_cs_packet *pkt,
			      unsigned idx, unsigned reg)
{
	struct radeon_bo_list *reloc;
	struct r100_cs_track *track;
	volatile uint32_t *ib;
	uint32_t tmp;
	int r;
	int i, face;
	u32 tile_flags = 0;
	u32 idx_value;

	ib = p->ib.ptr;
	track = (struct r100_cs_track *)p->track;

	idx_value = radeon_get_ib_value(p, idx);

	switch (reg) {
	case RADEON_CRTC_GUI_TRIG_VLINE:
		r = r100_cs_packet_parse_vline(p);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
				  idx, reg);
			radeon_cs_dump_packet(p, pkt);
			return r;
		}
		break;
		/* FIXME: only allow PACKET3 blit? easier to check for out of
		 * range access */
	case RADEON_DST_PITCH_OFFSET:
	case RADEON_SRC_PITCH_OFFSET:
		r = r100_reloc_pitch_offset(p, pkt, idx, reg);
		if (r)
			return r;
		break;
	case RADEON_RB3D_DEPTHOFFSET:
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
				  idx, reg);
			radeon_cs_dump_packet(p, pkt);
			return r;
		}
		track->zb.robj = reloc->robj;
		track->zb.offset = idx_value;
		track->zb_dirty = true;
		ib[idx] = idx_value + ((u32)reloc->gpu_offset);
		break;
	case RADEON_RB3D_COLOROFFSET:
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
				  idx, reg);
			radeon_cs_dump_packet(p, pkt);
			return r;
		}
		track->cb[0].robj = reloc->robj;
		track->cb[0].offset = idx_value;
		track->cb_dirty = true;
		ib[idx] = idx_value + ((u32)reloc->gpu_offset);
		break;
	case RADEON_PP_TXOFFSET_0:
	case RADEON_PP_TXOFFSET_1:
	case RADEON_PP_TXOFFSET_2:
		i = (reg - RADEON_PP_TXOFFSET_0) / 24;
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
				  idx, reg);
			radeon_cs_dump_packet(p, pkt);
			return r;
		}
		if (!(p->cs_flags & RADEON_CS_KEEP_TILING_FLAGS)) {
			if (reloc->tiling_flags & RADEON_TILING_MACRO)
				tile_flags |= RADEON_TXO_MACRO_TILE;
			if (reloc->tiling_flags & RADEON_TILING_MICRO)
				tile_flags |= RADEON_TXO_MICRO_TILE_X2;

			tmp = idx_value & ~(0x7 << 2);
			tmp |= tile_flags;
			ib[idx] = tmp + ((u32)reloc->gpu_offset);
		} else
			ib[idx] = idx_value + ((u32)reloc->gpu_offset);
		track->textures[i].robj = reloc->robj;
		track->tex_dirty = true;
		break;
	case RADEON_PP_CUBIC_OFFSET_T0_0:
	case RADEON_PP_CUBIC_OFFSET_T0_1:
	case RADEON_PP_CUBIC_OFFSET_T0_2:
	case RADEON_PP_CUBIC_OFFSET_T0_3:
	case RADEON_PP_CUBIC_OFFSET_T0_4:
		i = (reg - RADEON_PP_CUBIC_OFFSET_T0_0) / 4;
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
				  idx, reg);
			radeon_cs_dump_packet(p, pkt);
			return r;
		}
		track->textures[0].cube_info[i].offset = idx_value;
		ib[idx] = idx_value + ((u32)reloc->gpu_offset);
		track->textures[0].cube_info[i].robj = reloc->robj;
		track->tex_dirty = true;
		break;
	case RADEON_PP_CUBIC_OFFSET_T1_0:
	case RADEON_PP_CUBIC_OFFSET_T1_1:
	case RADEON_PP_CUBIC_OFFSET_T1_2:
	case RADEON_PP_CUBIC_OFFSET_T1_3:
	case RADEON_PP_CUBIC_OFFSET_T1_4:
		i = (reg - RADEON_PP_CUBIC_OFFSET_T1_0) / 4;
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
				  idx, reg);
			radeon_cs_dump_packet(p, pkt);
			return r;
		}
		track->textures[1].cube_info[i].offset = idx_value;
		ib[idx] = idx_value + ((u32)reloc->gpu_offset);
		track->textures[1].cube_info[i].robj = reloc->robj;
		track->tex_dirty = true;
		break;
	case RADEON_PP_CUBIC_OFFSET_T2_0:
	case RADEON_PP_CUBIC_OFFSET_T2_1:
	case RADEON_PP_CUBIC_OFFSET_T2_2:
	case RADEON_PP_CUBIC_OFFSET_T2_3:
	case RADEON_PP_CUBIC_OFFSET_T2_4:
		i = (reg - RADEON_PP_CUBIC_OFFSET_T2_0) / 4;
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
				  idx, reg);
			radeon_cs_dump_packet(p, pkt);
			return r;
		}
		track->textures[2].cube_info[i].offset = idx_value;
		ib[idx] = idx_value + ((u32)reloc->gpu_offset);
		track->textures[2].cube_info[i].robj = reloc->robj;
		track->tex_dirty = true;
		break;
	case RADEON_RE_WIDTH_HEIGHT:
		track->maxy = ((idx_value >> 16) & 0x7FF);
		track->cb_dirty = true;
		track->zb_dirty = true;
		break;
	case RADEON_RB3D_COLORPITCH:
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
				  idx, reg);
			radeon_cs_dump_packet(p, pkt);
			return r;
		}
		if (!(p->cs_flags & RADEON_CS_KEEP_TILING_FLAGS)) {
			if (reloc->tiling_flags & RADEON_TILING_MACRO)
				tile_flags |= RADEON_COLOR_TILE_ENABLE;
			if (reloc->tiling_flags & RADEON_TILING_MICRO)
				tile_flags |= RADEON_COLOR_MICROTILE_ENABLE;

			tmp = idx_value & ~(0x7 << 16);
			tmp |= tile_flags;
			ib[idx] = tmp;
		} else
			ib[idx] = idx_value;

		track->cb[0].pitch = idx_value & RADEON_COLORPITCH_MASK;
		track->cb_dirty = true;
		break;
	case RADEON_RB3D_DEPTHPITCH:
		track->zb.pitch = idx_value & RADEON_DEPTHPITCH_MASK;
		track->zb_dirty = true;
		break;
	case RADEON_RB3D_CNTL:
		switch ((idx_value >> RADEON_RB3D_COLOR_FORMAT_SHIFT) & 0x1f) {
		case 7:
		case 8:
		case 9:
		case 11:
		case 12:
			track->cb[0].cpp = 1;
			break;
		case 3:
		case 4:
		case 15:
			track->cb[0].cpp = 2;
			break;
		case 6:
			track->cb[0].cpp = 4;
			break;
		default:
			DRM_ERROR("Invalid color buffer format (%d) !\n",
				  ((idx_value >> RADEON_RB3D_COLOR_FORMAT_SHIFT) & 0x1f));
			return -EINVAL;
		}
		track->z_enabled = !!(idx_value & RADEON_Z_ENABLE);
		track->cb_dirty = true;
		track->zb_dirty = true;
		break;
	case RADEON_RB3D_ZSTENCILCNTL:
		switch (idx_value & 0xf) {
		case 0:
			track->zb.cpp = 2;
			break;
		case 2:
		case 3:
		case 4:
		case 5:
		case 9:
		case 11:
			track->zb.cpp = 4;
			break;
		default:
			break;
		}
		track->zb_dirty = true;
		break;
	case RADEON_RB3D_ZPASS_ADDR:
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
				  idx, reg);
			radeon_cs_dump_packet(p, pkt);
			return r;
		}
		ib[idx] = idx_value + ((u32)reloc->gpu_offset);
		break;
	case RADEON_PP_CNTL:
		{
			uint32_t temp = idx_value >> 4;
			for (i = 0; i < track->num_texture; i++)
				track->textures[i].enabled = !!(temp & (1 << i));
			track->tex_dirty = true;
		}
		break;
	case RADEON_SE_VF_CNTL:
		track->vap_vf_cntl = idx_value;
		break;
	case RADEON_SE_VTX_FMT:
		track->vtx_size = r100_get_vtx_size(idx_value);
		break;
	case RADEON_PP_TEX_SIZE_0:
	case RADEON_PP_TEX_SIZE_1:
	case RADEON_PP_TEX_SIZE_2:
		i = (reg - RADEON_PP_TEX_SIZE_0) / 8;
		track->textures[i].width = (idx_value & RADEON_TEX_USIZE_MASK) + 1;
		track->textures[i].height = ((idx_value & RADEON_TEX_VSIZE_MASK) >> RADEON_TEX_VSIZE_SHIFT) + 1;
		track->tex_dirty = true;
		break;
	case RADEON_PP_TEX_PITCH_0:
	case RADEON_PP_TEX_PITCH_1:
	case RADEON_PP_TEX_PITCH_2:
		i = (reg - RADEON_PP_TEX_PITCH_0) / 8;
		track->textures[i].pitch = idx_value + 32;
		track->tex_dirty = true;
		break;
	case RADEON_PP_TXFILTER_0:
	case RADEON_PP_TXFILTER_1:
	case RADEON_PP_TXFILTER_2:
		i = (reg - RADEON_PP_TXFILTER_0) / 24;
		track->textures[i].num_levels = ((idx_value & RADEON_MAX_MIP_LEVEL_MASK)
						 >> RADEON_MAX_MIP_LEVEL_SHIFT);
		tmp = (idx_value >> 23) & 0x7;
		if (tmp == 2 || tmp == 6)
			track->textures[i].roundup_w = false;
		tmp = (idx_value >> 27) & 0x7;
		if (tmp == 2 || tmp == 6)
			track->textures[i].roundup_h = false;
		track->tex_dirty = true;
		break;
	case RADEON_PP_TXFORMAT_0:
	case RADEON_PP_TXFORMAT_1:
	case RADEON_PP_TXFORMAT_2:
		i = (reg - RADEON_PP_TXFORMAT_0) / 24;
		if (idx_value & RADEON_TXFORMAT_NON_POWER2) {
			track->textures[i].use_pitch = 1;
		} else {
			track->textures[i].use_pitch = 0;
			track->textures[i].width = 1 << ((idx_value >> RADEON_TXFORMAT_WIDTH_SHIFT) & RADEON_TXFORMAT_WIDTH_MASK);
			track->textures[i].height = 1 << ((idx_value >> RADEON_TXFORMAT_HEIGHT_SHIFT) & RADEON_TXFORMAT_HEIGHT_MASK);
		}
		if (idx_value & RADEON_TXFORMAT_CUBIC_MAP_ENABLE)
			track->textures[i].tex_coord_type = 2;
		switch ((idx_value & RADEON_TXFORMAT_FORMAT_MASK)) {
		case RADEON_TXFORMAT_I8:
		case RADEON_TXFORMAT_RGB332:
		case RADEON_TXFORMAT_Y8:
			track->textures[i].cpp = 1;
			track->textures[i].compress_format = R100_TRACK_COMP_NONE;
			break;
		case RADEON_TXFORMAT_AI88:
		case RADEON_TXFORMAT_ARGB1555:
		case RADEON_TXFORMAT_RGB565:
		case RADEON_TXFORMAT_ARGB4444:
		case RADEON_TXFORMAT_VYUY422:
		case RADEON_TXFORMAT_YVYU422:
		case RADEON_TXFORMAT_SHADOW16:
		case RADEON_TXFORMAT_LDUDV655:
		case RADEON_TXFORMAT_DUDV88:
			track->textures[i].cpp = 2;
			track->textures[i].compress_format = R100_TRACK_COMP_NONE;
			break;
		case RADEON_TXFORMAT_ARGB8888:
		case RADEON_TXFORMAT_RGBA8888:
		case RADEON_TXFORMAT_SHADOW32:
		case RADEON_TXFORMAT_LDUDUV8888:
			track->textures[i].cpp = 4;
			track->textures[i].compress_format = R100_TRACK_COMP_NONE;
			break;
		case RADEON_TXFORMAT_DXT1:
			track->textures[i].cpp = 1;
			track->textures[i].compress_format = R100_TRACK_COMP_DXT1;
			break;
		case RADEON_TXFORMAT_DXT23:
		case RADEON_TXFORMAT_DXT45:
			track->textures[i].cpp = 1;
			track->textures[i].compress_format = R100_TRACK_COMP_DXT35;
			break;
		}
		track->textures[i].cube_info[4].width = 1 << ((idx_value >> 16) & 0xf);
		track->textures[i].cube_info[4].height = 1 << ((idx_value >> 20) & 0xf);
		track->tex_dirty = true;
		break;
	case RADEON_PP_CUBIC_FACES_0:
	case RADEON_PP_CUBIC_FACES_1:
	case RADEON_PP_CUBIC_FACES_2:
		tmp = idx_value;
		i = (reg - RADEON_PP_CUBIC_FACES_0) / 4;
		for (face = 0; face < 4; face++) {
			track->textures[i].cube_info[face].width = 1 << ((tmp >> (face * 8)) & 0xf);
			track->textures[i].cube_info[face].height = 1 << ((tmp >> ((face * 8) + 4)) & 0xf);
		}
		track->tex_dirty = true;
		break;
	default:
		printk(KERN_ERR "Forbidden register 0x%04X in cs at %d\n",
		       reg, idx);
		return -EINVAL;
	}
	return 0;
}

int r100_cs_track_check_pkt3_indx_buffer(struct radeon_cs_parser *p,
					 struct radeon_cs_packet *pkt,
					 struct radeon_bo *robj)
{
	unsigned idx;
	u32 value;
	idx = pkt->idx + 1;
	value = radeon_get_ib_value(p, idx + 2);
	if ((value + 1) > radeon_bo_size(robj)) {
		DRM_ERROR("[drm] Buffer too small for PACKET3 INDX_BUFFER "
			  "(need %u have %lu) !\n",
			  value + 1,
			  radeon_bo_size(robj));
		return -EINVAL;
	}
	return 0;
}

static int r100_packet3_check(struct radeon_cs_parser *p,
			      struct radeon_cs_packet *pkt)
{
	struct radeon_bo_list *reloc;
	struct r100_cs_track *track;
	unsigned idx;
	volatile uint32_t *ib;
	int r;

	ib = p->ib.ptr;
	idx = pkt->idx + 1;
	track = (struct r100_cs_track *)p->track;
	switch (pkt->opcode) {
	case PACKET3_3D_LOAD_VBPNTR:
		r = r100_packet3_load_vbpntr(p, pkt, idx);
		if (r)
			return r;
		break;
	case PACKET3_INDX_BUFFER:
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("No reloc for packet3 %d\n", pkt->opcode);
			radeon_cs_dump_packet(p, pkt);
			return r;
		}
		ib[idx+1] = radeon_get_ib_value(p, idx+1) + ((u32)reloc->gpu_offset);
		r = r100_cs_track_check_pkt3_indx_buffer(p, pkt, reloc->robj);
		if (r) {
			return r;
		}
		break;
	case 0x23:
		/* 3D_RNDR_GEN_INDX_PRIM on r100/r200 */
		r = radeon_cs_packet_next_reloc(p, &reloc, 0);
		if (r) {
			DRM_ERROR("No reloc for packet3 %d\n", pkt->opcode);
			radeon_cs_dump_packet(p, pkt);
			return r;
		}
		ib[idx] = radeon_get_ib_value(p, idx) + ((u32)reloc->gpu_offset);
		track->num_arrays = 1;
		track->vtx_size = r100_get_vtx_size(radeon_get_ib_value(p, idx + 2));

		track->arrays[0].robj = reloc->robj;
		track->arrays[0].esize = track->vtx_size;

		track->max_indx = radeon_get_ib_value(p, idx+1);

		track->vap_vf_cntl = radeon_get_ib_value(p, idx+3);
		track->immd_dwords = pkt->count - 1;
		r = r100_cs_track_check(p->rdev, track);
		if (r)
			return r;
		break;
	case PACKET3_3D_DRAW_IMMD:
		if (((radeon_get_ib_value(p, idx + 1) >> 4) & 0x3) != 3) {
			DRM_ERROR("PRIM_WALK must be 3 for IMMD draw\n");
			return -EINVAL;
		}
		track->vtx_size = r100_get_vtx_size(radeon_get_ib_value(p, idx + 0));
		track->vap_vf_cntl = radeon_get_ib_value(p, idx + 1);
		track->immd_dwords = pkt->count - 1;
		r = r100_cs_track_check(p->rdev, track);
		if (r)
			return r;
		break;
		/* triggers drawing using in-packet vertex data */
	case PACKET3_3D_DRAW_IMMD_2:
		if (((radeon_get_ib_value(p, idx) >> 4) & 0x3) != 3) {
			DRM_ERROR("PRIM_WALK must be 3 for IMMD draw\n");
			return -EINVAL;
		}
		track->vap_vf_cntl = radeon_get_ib_value(p, idx);
		track->immd_dwords = pkt->count;
		r = r100_cs_track_check(p->rdev, track);
		if (r)
			return r;
		break;
		/* triggers drawing using in-packet vertex data */
	case PACKET3_3D_DRAW_VBUF_2:
		track->vap_vf_cntl = radeon_get_ib_value(p, idx);
		r = r100_cs_track_check(p->rdev, track);
		if (r)
			return r;
		break;
		/* triggers drawing of vertex buffers setup elsewhere */
	case PACKET3_3D_DRAW_INDX_2:
		track->vap_vf_cntl = radeon_get_ib_value(p, idx);
		r = r100_cs_track_check(p->rdev, track);
		if (r)
			return r;
		break;
		/* triggers drawing using indices to vertex buffer */
	case PACKET3_3D_DRAW_VBUF:
		track->vap_vf_cntl = radeon_get_ib_value(p, idx + 1);
		r = r100_cs_track_check(p->rdev, track);
		if (r)
			return r;
		break;
		/* triggers drawing of vertex buffers setup elsewhere */
	case PACKET3_3D_DRAW_INDX:
		track->vap_vf_cntl = radeon_get_ib_value(p, idx + 1);
		r = r100_cs_track_check(p->rdev, track);
		if (r)
			return r;
		break;
		/* triggers drawing using indices to vertex buffer */
	case PACKET3_3D_CLEAR_HIZ:
	case PACKET3_3D_CLEAR_ZMASK:
		if (p->rdev->hyperz_filp != p->filp)
			return -EINVAL;
		break;
	case PACKET3_NOP:
		break;
	default:
		DRM_ERROR("Packet3 opcode %x not supported\n", pkt->opcode);
		return -EINVAL;
	}
	return 0;
}

int r100_cs_parse(struct radeon_cs_parser *p)
{
	struct radeon_cs_packet pkt;
	struct r100_cs_track *track;
	int r;

	track = kzalloc(sizeof(*track), GFP_KERNEL);
	if (!track)
		return -ENOMEM;
	r100_cs_track_clear(p->rdev, track);
	p->track = track;
	do {
		r = radeon_cs_packet_parse(p, &pkt, p->idx);
		if (r) {
			return r;
		}
		p->idx += pkt.count + 2;
		switch (pkt.type) {
		case RADEON_PACKET_TYPE0:
			if (p->rdev->family >= CHIP_R200)
				r = r100_cs_parse_packet0(p, &pkt,
					p->rdev->config.r100.reg_safe_bm,
					p->rdev->config.r100.reg_safe_bm_size,
					&r200_packet0_check);
			else
				r = r100_cs_parse_packet0(p, &pkt,
					p->rdev->config.r100.reg_safe_bm,
					p->rdev->config.r100.reg_safe_bm_size,
					&r100_packet0_check);
			break;
		case RADEON_PACKET_TYPE2:
			break;
		case RADEON_PACKET_TYPE3:
			r = r100_packet3_check(p, &pkt);
			break;
		default:
			DRM_ERROR("Unknown packet type %d !\n",
				  pkt.type);
			return -EINVAL;
		}
		if (r)
			return r;
	} while (p->idx < p->chunk_ib->length_dw);
	return 0;
}

static void r100_cs_track_texture_print(struct r100_cs_track_texture *t)
{
	DRM_ERROR("pitch                      %d\n", t->pitch);
	DRM_ERROR("use_pitch                  %d\n", t->use_pitch);
	DRM_ERROR("width                      %d\n", t->width);
	DRM_ERROR("width_11                   %d\n", t->width_11);
	DRM_ERROR("height                     %d\n", t->height);
	DRM_ERROR("height_11                  %d\n", t->height_11);
	DRM_ERROR("num levels                 %d\n", t->num_levels);
	DRM_ERROR("depth                      %d\n", t->txdepth);
	DRM_ERROR("bpp                        %d\n", t->cpp);
	DRM_ERROR("coordinate type            %d\n", t->tex_coord_type);
	DRM_ERROR("width round to power of 2  %d\n", t->roundup_w);
	DRM_ERROR("height round to power of 2 %d\n", t->roundup_h);
	DRM_ERROR("compress format            %d\n", t->compress_format);
}

static int r100_track_compress_size(int compress_format, int w, int h)
{
	int block_width, block_height, block_bytes;
	int wblocks, hblocks;
	int min_wblocks;
	int sz;

	block_width = 4;
	block_height = 4;

	switch (compress_format) {
	case R100_TRACK_COMP_DXT1:
		block_bytes = 8;
		min_wblocks = 4;
		break;
	default:
	case R100_TRACK_COMP_DXT35:
		block_bytes = 16;
		min_wblocks = 2;
		break;
	}

	hblocks = (h + block_height - 1) / block_height;
	wblocks = (w + block_width - 1) / block_width;
	if (wblocks < min_wblocks)
		wblocks = min_wblocks;
	sz = wblocks * hblocks * block_bytes;
	return sz;
}

static int r100_cs_track_cube(struct radeon_device *rdev,
			      struct r100_cs_track *track, unsigned idx)
{
	unsigned face, w, h;
	struct radeon_bo *cube_robj;
	unsigned long size;
	unsigned compress_format = track->textures[idx].compress_format;

	for (face = 0; face < 5; face++) {
		cube_robj = track->textures[idx].cube_info[face].robj;
		w = track->textures[idx].cube_info[face].width;
		h = track->textures[idx].cube_info[face].height;

		if (compress_format) {
			size = r100_track_compress_size(compress_format, w, h);
		} else
			size = w * h;
		size *= track->textures[idx].cpp;

		size += track->textures[idx].cube_info[face].offset;

		if (size > radeon_bo_size(cube_robj)) {
			DRM_ERROR("Cube texture offset greater than object size %lu %lu\n",
				  size, radeon_bo_size(cube_robj));
			r100_cs_track_texture_print(&track->textures[idx]);
			return -1;
		}
	}
	return 0;
}

static int r100_cs_track_texture_check(struct radeon_device *rdev,
				       struct r100_cs_track *track)
{
	struct radeon_bo *robj;
	unsigned long size;
	unsigned u, i, w, h, d;
	int ret;

	for (u = 0; u < track->num_texture; u++) {
		if (!track->textures[u].enabled)
			continue;
		if (track->textures[u].lookup_disable)
			continue;
		robj = track->textures[u].robj;
		if (robj == NULL) {
			DRM_ERROR("No texture bound to unit %u\n", u);
			return -EINVAL;
		}
		size = 0;
		for (i = 0; i <= track->textures[u].num_levels; i++) {
			if (track->textures[u].use_pitch) {
				if (rdev->family < CHIP_R300)
					w = (track->textures[u].pitch / track->textures[u].cpp) / (1 << i);
				else
					w = track->textures[u].pitch / (1 << i);
			} else {
				w = track->textures[u].width;
				if (rdev->family >= CHIP_RV515)
					w |= track->textures[u].width_11;
				w = w / (1 << i);
				if (track->textures[u].roundup_w)
					w = roundup_pow_of_two(w);
			}
			h = track->textures[u].height;
			if (rdev->family >= CHIP_RV515)
				h |= track->textures[u].height_11;
			h = h / (1 << i);
			if (track->textures[u].roundup_h)
				h = roundup_pow_of_two(h);
			if (track->textures[u].tex_coord_type == 1) {
				d = (1 << track->textures[u].txdepth) / (1 << i);
				if (!d)
					d = 1;
			} else {
				d = 1;
			}
			if (track->textures[u].compress_format) {

				size += r100_track_compress_size(track->textures[u].compress_format, w, h) * d;
				/* compressed textures are block based */
			} else
				size += w * h * d;
		}
		size *= track->textures[u].cpp;

		switch (track->textures[u].tex_coord_type) {
		case 0:
		case 1:
			break;
		case 2:
			if (track->separate_cube) {
				ret = r100_cs_track_cube(rdev, track, u);
				if (ret)
					return ret;
			} else
				size *= 6;
			break;
		default:
			DRM_ERROR("Invalid texture coordinate type %u for unit "
				  "%u\n", track->textures[u].tex_coord_type, u);
			return -EINVAL;
		}
		if (size > radeon_bo_size(robj)) {
			DRM_ERROR("Texture of unit %u needs %lu bytes but is "
				  "%lu\n", u, size, radeon_bo_size(robj));
			r100_cs_track_texture_print(&track->textures[u]);
			return -EINVAL;
		}
	}
	return 0;
}

int r100_cs_track_check(struct radeon_device *rdev, struct r100_cs_track *track)
{
	unsigned i;
	unsigned long size;
	unsigned prim_walk;
	unsigned nverts;
	unsigned num_cb = track->cb_dirty ? track->num_cb : 0;

	if (num_cb && !track->zb_cb_clear && !track->color_channel_mask &&
	    !track->blend_read_enable)
		num_cb = 0;

	for (i = 0; i < num_cb; i++) {
		if (track->cb[i].robj == NULL) {
			DRM_ERROR("[drm] No buffer for color buffer %d !\n", i);
			return -EINVAL;
		}
		size = track->cb[i].pitch * track->cb[i].cpp * track->maxy;
		size += track->cb[i].offset;
		if (size > radeon_bo_size(track->cb[i].robj)) {
			DRM_ERROR("[drm] Buffer too small for color buffer %d "
				  "(need %lu have %lu) !\n", i, size,
				  radeon_bo_size(track->cb[i].robj));
			DRM_ERROR("[drm] color buffer %d (%u %u %u %u)\n",
				  i, track->cb[i].pitch, track->cb[i].cpp,
				  track->cb[i].offset, track->maxy);
			return -EINVAL;
		}
	}
	track->cb_dirty = false;

	if (track->zb_dirty && track->z_enabled) {
		if (track->zb.robj == NULL) {
			DRM_ERROR("[drm] No buffer for z buffer !\n");
			return -EINVAL;
		}
		size = track->zb.pitch * track->zb.cpp * track->maxy;
		size += track->zb.offset;
		if (size > radeon_bo_size(track->zb.robj)) {
			DRM_ERROR("[drm] Buffer too small for z buffer "
				  "(need %lu have %lu) !\n", size,
				  radeon_bo_size(track->zb.robj));
			DRM_ERROR("[drm] zbuffer (%u %u %u %u)\n",
				  track->zb.pitch, track->zb.cpp,
				  track->zb.offset, track->maxy);
			return -EINVAL;
		}
	}
	track->zb_dirty = false;

	if (track->aa_dirty && track->aaresolve) {
		if (track->aa.robj == NULL) {
			DRM_ERROR("[drm] No buffer for AA resolve buffer %d !\n", i);
			return -EINVAL;
		}
		/* I believe the format comes from colorbuffer0. */
		size = track->aa.pitch * track->cb[0].cpp * track->maxy;
		size += track->aa.offset;
		if (size > radeon_bo_size(track->aa.robj)) {
			DRM_ERROR("[drm] Buffer too small for AA resolve buffer %d "
				  "(need %lu have %lu) !\n", i, size,
				  radeon_bo_size(track->aa.robj));
			DRM_ERROR("[drm] AA resolve buffer %d (%u %u %u %u)\n",
				  i, track->aa.pitch, track->cb[0].cpp,
				  track->aa.offset, track->maxy);
			return -EINVAL;
		}
	}
	track->aa_dirty = false;

	prim_walk = (track->vap_vf_cntl >> 4) & 0x3;
	if (track->vap_vf_cntl & (1 << 14)) {
		nverts = track->vap_alt_nverts;
	} else {
		nverts = (track->vap_vf_cntl >> 16) & 0xFFFF;
	}
	switch (prim_walk) {
	case 1:
		for (i = 0; i < track->num_arrays; i++) {
			size = track->arrays[i].esize * track->max_indx * 4;
			if (track->arrays[i].robj == NULL) {
				DRM_ERROR("(PW %u) Vertex array %u no buffer "
					  "bound\n", prim_walk, i);
				return -EINVAL;
			}
			if (size > radeon_bo_size(track->arrays[i].robj)) {
				dev_err(rdev->dev, "(PW %u) Vertex array %u "
					"need %lu dwords have %lu dwords\n",
					prim_walk, i, size >> 2,
					radeon_bo_size(track->arrays[i].robj)
					>> 2);
				DRM_ERROR("Max indices %u\n", track->max_indx);
				return -EINVAL;
			}
		}
		break;
	case 2:
		for (i = 0; i < track->num_arrays; i++) {
			size = track->arrays[i].esize * (nverts - 1) * 4;
			if (track->arrays[i].robj == NULL) {
				DRM_ERROR("(PW %u) Vertex array %u no buffer "
					  "bound\n", prim_walk, i);
				return -EINVAL;
			}
			if (size > radeon_bo_size(track->arrays[i].robj)) {
				dev_err(rdev->dev, "(PW %u) Vertex array %u "
					"need %lu dwords have %lu dwords\n",
					prim_walk, i, size >> 2,
					radeon_bo_size(track->arrays[i].robj)
					>> 2);
				return -EINVAL;
			}
		}
		break;
	case 3:
		size = track->vtx_size * nverts;
		if (size != track->immd_dwords) {
			DRM_ERROR("IMMD draw %u dwors but needs %lu dwords\n",
				  track->immd_dwords, size);
			DRM_ERROR("VAP_VF_CNTL.NUM_VERTICES %u, VTX_SIZE %u\n",
				  nverts, track->vtx_size);
			return -EINVAL;
		}
		break;
	default:
		DRM_ERROR("[drm] Invalid primitive walk %d for VAP_VF_CNTL\n",
			  prim_walk);
		return -EINVAL;
	}

	if (track->tex_dirty) {
		track->tex_dirty = false;
		return r100_cs_track_texture_check(rdev, track);
	}
	return 0;
}

void r100_cs_track_clear(struct radeon_device *rdev, struct r100_cs_track *track)
{
	unsigned i, face;

	track->cb_dirty = true;
	track->zb_dirty = true;
	track->tex_dirty = true;
	track->aa_dirty = true;

	if (rdev->family < CHIP_R300) {
		track->num_cb = 1;
		if (rdev->family <= CHIP_RS200)
			track->num_texture = 3;
		else
			track->num_texture = 6;
		track->maxy = 2048;
		track->separate_cube = 1;
	} else {
		track->num_cb = 4;
		track->num_texture = 16;
		track->maxy = 4096;
		track->separate_cube = 0;
		track->aaresolve = false;
		track->aa.robj = NULL;
	}

	for (i = 0; i < track->num_cb; i++) {
		track->cb[i].robj = NULL;
		track->cb[i].pitch = 8192;
		track->cb[i].cpp = 16;
		track->cb[i].offset = 0;
	}
	track->z_enabled = true;
	track->zb.robj = NULL;
	track->zb.pitch = 8192;
	track->zb.cpp = 4;
	track->zb.offset = 0;
	track->vtx_size = 0x7F;
	track->immd_dwords = 0xFFFFFFFFUL;
	track->num_arrays = 11;
	track->max_indx = 0x00FFFFFFUL;
	for (i = 0; i < track->num_arrays; i++) {
		track->arrays[i].robj = NULL;
		track->arrays[i].esize = 0x7F;
	}
	for (i = 0; i < track->num_texture; i++) {
		track->textures[i].compress_format = R100_TRACK_COMP_NONE;
		track->textures[i].pitch = 16536;
		track->textures[i].width = 16536;
		track->textures[i].height = 16536;
		track->textures[i].width_11 = 1 << 11;
		track->textures[i].height_11 = 1 << 11;
		track->textures[i].num_levels = 12;
		if (rdev->family <= CHIP_RS200) {
			track->textures[i].tex_coord_type = 0;
			track->textures[i].txdepth = 0;
		} else {
			track->textures[i].txdepth = 16;
			track->textures[i].tex_coord_type = 1;
		}
		track->textures[i].cpp = 64;
		track->textures[i].robj = NULL;
		/* CS IB emission code makes sure texture unit are disabled */
		track->textures[i].enabled = false;
		track->textures[i].lookup_disable = false;
		track->textures[i].roundup_w = true;
		track->textures[i].roundup_h = true;
		if (track->separate_cube)
			for (face = 0; face < 5; face++) {
				track->textures[i].cube_info[face].robj = NULL;
				track->textures[i].cube_info[face].width = 16536;
				track->textures[i].cube_info[face].height = 16536;
				track->textures[i].cube_info[face].offset = 0;
			}
	}
}

/*
 * Global GPU functions
 */
static void r100_errata(struct radeon_device *rdev)
{
	rdev->pll_errata = 0;

	if (rdev->family == CHIP_RV200 || rdev->family == CHIP_RS200) {
		rdev->pll_errata |= CHIP_ERRATA_PLL_DUMMYREADS;
	}

	if (rdev->family == CHIP_RV100 ||
	    rdev->family == CHIP_RS100 ||
	    rdev->family == CHIP_RS200) {
		rdev->pll_errata |= CHIP_ERRATA_PLL_DELAY;
	}
}

static int r100_rbbm_fifo_wait_for_entry(struct radeon_device *rdev, unsigned n)
{
	unsigned i;
	uint32_t tmp;

	for (i = 0; i < rdev->usec_timeout; i++) {
		tmp = RREG32(RADEON_RBBM_STATUS) & RADEON_RBBM_FIFOCNT_MASK;
		if (tmp >= n) {
			return 0;
		}
		DRM_UDELAY(1);
	}
	return -1;
}

int r100_gui_wait_for_idle(struct radeon_device *rdev)
{
	unsigned i;
	uint32_t tmp;

	if (r100_rbbm_fifo_wait_for_entry(rdev, 64)) {
		printk(KERN_WARNING "radeon: wait for empty RBBM fifo failed !"
		       " Bad things might happen.\n");
	}
	for (i = 0; i < rdev->usec_timeout; i++) {
		tmp = RREG32(RADEON_RBBM_STATUS);
		if (!(tmp & RADEON_RBBM_ACTIVE)) {
			return 0;
		}
		DRM_UDELAY(1);
	}
	return -1;
}

int r100_mc_wait_for_idle(struct radeon_device *rdev)
{
	unsigned i;
	uint32_t tmp;

	for (i = 0; i < rdev->usec_timeout; i++) {
		/* read MC_STATUS */
		tmp = RREG32(RADEON_MC_STATUS);
		if (tmp & RADEON_MC_IDLE) {
			return 0;
		}
		DRM_UDELAY(1);
	}
	return -1;
}

bool r100_gpu_is_lockup(struct radeon_device *rdev, struct radeon_ring *ring)
{
	u32 rbbm_status;

	rbbm_status = RREG32(R_000E40_RBBM_STATUS);
	if (!G_000E40_GUI_ACTIVE(rbbm_status)) {
		radeon_ring_lockup_update(rdev, ring);
		return false;
	}
	return radeon_ring_test_lockup(rdev, ring);
}

/* required on r1xx, r2xx, r300, r(v)350, r420/r481, rs400/rs480 */
void r100_enable_bm(struct radeon_device *rdev)
{
	uint32_t tmp;
	/* Enable bus mastering */
	tmp = RREG32(RADEON_BUS_CNTL) & ~RADEON_BUS_MASTER_DIS;
	WREG32(RADEON_BUS_CNTL, tmp);
}

void r100_bm_disable(struct radeon_device *rdev)
{
	u32 tmp;

	/* disable bus mastering */
	tmp = RREG32(R_000030_BUS_CNTL);
	WREG32(R_000030_BUS_CNTL, (tmp & 0xFFFFFFFF) | 0x00000044);
	mdelay(1);
	WREG32(R_000030_BUS_CNTL, (tmp & 0xFFFFFFFF) | 0x00000042);
	mdelay(1);
	WREG32(R_000030_BUS_CNTL, (tmp & 0xFFFFFFFF) | 0x00000040);
	tmp = RREG32(RADEON_BUS_CNTL);
	mdelay(1);
	pci_clear_master(rdev->pdev);
	mdelay(1);
}

int r100_asic_reset(struct radeon_device *rdev, bool hard)
{
	struct r100_mc_save save;
	u32 status, tmp;
	int ret = 0;

	status = RREG32(R_000E40_RBBM_STATUS);
	if (!G_000E40_GUI_ACTIVE(status)) {
		return 0;
	}
	r100_mc_stop(rdev, &save);
	status = RREG32(R_000E40_RBBM_STATUS);
	dev_info(rdev->dev, "(%s:%d) RBBM_STATUS=0x%08X\n", __func__, __LINE__, status);
	/* stop CP */
	WREG32(RADEON_CP_CSQ_CNTL, 0);
	tmp = RREG32(RADEON_CP_RB_CNTL);
	WREG32(RADEON_CP_RB_CNTL, tmp | RADEON_RB_RPTR_WR_ENA);
	WREG32(RADEON_CP_RB_RPTR_WR, 0);
	WREG32(RADEON_CP_RB_WPTR, 0);
	WREG32(RADEON_CP_RB_CNTL, tmp);
	/* save PCI state */
	pci_save_state(rdev->pdev);
	/* disable bus mastering */
	r100_bm_disable(rdev);
	WREG32(R_0000F0_RBBM_SOFT_RESET, S_0000F0_SOFT_RESET_SE(1) |
					S_0000F0_SOFT_RESET_RE(1) |
					S_0000F0_SOFT_RESET_PP(1) |
					S_0000F0_SOFT_RESET_RB(1));
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
	/* restore PCI & busmastering */
	pci_restore_state(rdev->pdev);
	r100_enable_bm(rdev);
	/* Check if GPU is idle */
	if (G_000E40_SE_BUSY(status) || G_000E40_RE_BUSY(status) ||
		G_000E40_TAM_BUSY(status) || G_000E40_PB_BUSY(status)) {
		dev_err(rdev->dev, "failed to reset GPU\n");
		ret = -1;
	} else
		dev_info(rdev->dev, "GPU reset succeed\n");
	r100_mc_resume(rdev, &save);
	return ret;
}

void r100_set_common_regs(struct radeon_device *rdev)
{
	struct drm_device *dev = rdev->ddev;
	bool force_dac2 = false;
	u32 tmp;

	/* set these so they don't interfere with anything */
	WREG32(RADEON_OV0_SCALE_CNTL, 0);
	WREG32(RADEON_SUBPIC_CNTL, 0);
	WREG32(RADEON_VIPH_CONTROL, 0);
	WREG32(RADEON_I2C_CNTL_1, 0);
	WREG32(RADEON_DVI_I2C_CNTL_1, 0);
	WREG32(RADEON_CAP0_TRIG_CNTL, 0);
	WREG32(RADEON_CAP1_TRIG_CNTL, 0);

	/* always set up dac2 on rn50 and some rv100 as lots
	 * of servers seem to wire it up to a VGA port but
	 * don't report it in the bios connector
	 * table.
	 */
	switch (dev->pdev->device) {
		/* RN50 */
	case 0x515e:
	case 0x5969:
		force_dac2 = true;
		break;
		/* RV100*/
	case 0x5159:
	case 0x515a:
		/* DELL triple head servers */
		if ((dev->pdev->subsystem_vendor == 0x1028 /* DELL */) &&
		    ((dev->pdev->subsystem_device == 0x016c) ||
		     (dev->pdev->subsystem_device == 0x016d) ||
		     (dev->pdev->subsystem_device == 0x016e) ||
		     (dev->pdev->subsystem_device == 0x016f) ||
		     (dev->pdev->subsystem_device == 0x0170) ||
		     (dev->pdev->subsystem_device == 0x017d) ||
		     (dev->pdev->subsystem_device == 0x017e) ||
		     (dev->pdev->subsystem_device == 0x0183) ||
		     (dev->pdev->subsystem_device == 0x018a) ||
		     (dev->pdev->subsystem_device == 0x019a)))
			force_dac2 = true;
		break;
	}

	if (force_dac2) {
		u32 disp_hw_debug = RREG32(RADEON_DISP_HW_DEBUG);
		u32 tv_dac_cntl = RREG32(RADEON_TV_DAC_CNTL);
		u32 dac2_cntl = RREG32(RADEON_DAC_CNTL2);

		/* For CRT on DAC2, don't turn it on if BIOS didn't
		   enable it, even it's detected.
		*/

		/* force it to crtc0 */
		dac2_cntl &= ~RADEON_DAC2_DAC_CLK_SEL;
		dac2_cntl |= RADEON_DAC2_DAC2_CLK_SEL;
		disp_hw_debug |= RADEON_CRT2_DISP1_SEL;

		/* set up the TV DAC */
		tv_dac_cntl &= ~(RADEON_TV_DAC_PEDESTAL |
				 RADEON_TV_DAC_STD_MASK |
				 RADEON_TV_DAC_RDACPD |
				 RADEON_TV_DAC_GDACPD |
				 RADEON_TV_DAC_BDACPD |
				 RADEON_TV_DAC_BGADJ_MASK |
				 RADEON_TV_DAC_DACADJ_MASK);
		tv_dac_cntl |= (RADEON_TV_DAC_NBLANK |
				RADEON_TV_DAC_NHOLD |
				RADEON_TV_DAC_STD_PS2 |
				(0x58 << 16));

		WREG32(RADEON_TV_DAC_CNTL, tv_dac_cntl);
		WREG32(RADEON_DISP_HW_DEBUG, disp_hw_debug);
		WREG32(RADEON_DAC_CNTL2, dac2_cntl);
	}

	/* switch PM block to ACPI mode */
	tmp = RREG32_PLL(RADEON_PLL_PWRMGT_CNTL);
	tmp &= ~RADEON_PM_MODE_SEL;
	WREG32_PLL(RADEON_PLL_PWRMGT_CNTL, tmp);

}

/*
 * VRAM info
 */
static void r100_vram_get_type(struct radeon_device *rdev)
{
	uint32_t tmp;

	rdev->mc.vram_is_ddr = false;
	if (rdev->flags & RADEON_IS_IGP)
		rdev->mc.vram_is_ddr = true;
	else if (RREG32(RADEON_MEM_SDRAM_MODE_REG) & RADEON_MEM_CFG_TYPE_DDR)
		rdev->mc.vram_is_ddr = true;
	if ((rdev->family == CHIP_RV100) ||
	    (rdev->family == CHIP_RS100) ||
	    (rdev->family == CHIP_RS200)) {
		tmp = RREG32(RADEON_MEM_CNTL);
		if (tmp & RV100_HALF_MODE) {
			rdev->mc.vram_width = 32;
		} else {
			rdev->mc.vram_width = 64;
		}
		if (rdev->flags & RADEON_SINGLE_CRTC) {
			rdev->mc.vram_width /= 4;
			rdev->mc.vram_is_ddr = true;
		}
	} else if (rdev->family <= CHIP_RV280) {
		tmp = RREG32(RADEON_MEM_CNTL);
		if (tmp & RADEON_MEM_NUM_CHANNELS_MASK) {
			rdev->mc.vram_width = 128;
		} else {
			rdev->mc.vram_width = 64;
		}
	} else {
		/* newer IGPs */
		rdev->mc.vram_width = 128;
	}
}

static u32 r100_get_accessible_vram(struct radeon_device *rdev)
{
	u32 aper_size;
	u8 byte;

	aper_size = RREG32(RADEON_CONFIG_APER_SIZE);

	/* Set HDP_APER_CNTL only on cards that are known not to be broken,
	 * that is has the 2nd generation multifunction PCI interface
	 */
	if (rdev->family == CHIP_RV280 ||
	    rdev->family >= CHIP_RV350) {
		WREG32_P(RADEON_HOST_PATH_CNTL, RADEON_HDP_APER_CNTL,
		       ~RADEON_HDP_APER_CNTL);
		DRM_INFO("Generation 2 PCI interface, using max accessible memory\n");
		return aper_size * 2;
	}

	/* Older cards have all sorts of funny issues to deal with. First
	 * check if it's a multifunction card by reading the PCI config
	 * header type... Limit those to one aperture size
	 */
	pci_read_config_byte(rdev->pdev, 0xe, &byte);
	if (byte & 0x80) {
		DRM_INFO("Generation 1 PCI interface in multifunction mode\n");
		DRM_INFO("Limiting VRAM to one aperture\n");
		return aper_size;
	}

	/* Single function older card. We read HDP_APER_CNTL to see how the BIOS
	 * have set it up. We don't write this as it's broken on some ASICs but
	 * we expect the BIOS to have done the right thing (might be too optimistic...)
	 */
	if (RREG32(RADEON_HOST_PATH_CNTL) & RADEON_HDP_APER_CNTL)
		return aper_size * 2;
	return aper_size;
}

void r100_vram_init_sizes(struct radeon_device *rdev)
{
	u64 config_aper_size;

	/* work out accessible VRAM */
	rdev->mc.aper_base = pci_resource_start(rdev->pdev, 0);
	rdev->mc.aper_size = pci_resource_len(rdev->pdev, 0);
	rdev->mc.visible_vram_size = r100_get_accessible_vram(rdev);
	/* FIXME we don't use the second aperture yet when we could use it */
	if (rdev->mc.visible_vram_size > rdev->mc.aper_size)
		rdev->mc.visible_vram_size = rdev->mc.aper_size;
	config_aper_size = RREG32(RADEON_CONFIG_APER_SIZE);
	if (rdev->flags & RADEON_IS_IGP) {
		uint32_t tom;
		/* read NB_TOM to get the amount of ram stolen for the GPU */
		tom = RREG32(RADEON_NB_TOM);
		rdev->mc.real_vram_size = (((tom >> 16) - (tom & 0xffff) + 1) << 16);
		WREG32(RADEON_CONFIG_MEMSIZE, rdev->mc.real_vram_size);
		rdev->mc.mc_vram_size = rdev->mc.real_vram_size;
	} else {
		rdev->mc.real_vram_size = RREG32(RADEON_CONFIG_MEMSIZE);
		/* Some production boards of m6 will report 0
		 * if it's 8 MB
		 */
		if (rdev->mc.real_vram_size == 0) {
			rdev->mc.real_vram_size = 8192 * 1024;
			WREG32(RADEON_CONFIG_MEMSIZE, rdev->mc.real_vram_size);
		}
		/* Fix for RN50, M6, M7 with 8/16/32(??) MBs of VRAM - 
		 * Novell bug 204882 + along with lots of ubuntu ones
		 */
		if (rdev->mc.aper_size > config_aper_size)
			config_aper_size = rdev->mc.aper_size;

		if (config_aper_size > rdev->mc.real_vram_size)
			rdev->mc.mc_vram_size = config_aper_size;
		else
			rdev->mc.mc_vram_size = rdev->mc.real_vram_size;
	}
}

void r100_vga_set_state(struct radeon_device *rdev, bool state)
{
	uint32_t temp;

	temp = RREG32(RADEON_CONFIG_CNTL);
	if (state == false) {
		temp &= ~RADEON_CFG_VGA_RAM_EN;
		temp |= RADEON_CFG_VGA_IO_DIS;
	} else {
		temp &= ~RADEON_CFG_VGA_IO_DIS;
	}
	WREG32(RADEON_CONFIG_CNTL, temp);
}

static void r100_mc_init(struct radeon_device *rdev)
{
	u64 base;

	r100_vram_get_type(rdev);
	r100_vram_init_sizes(rdev);
	base = rdev->mc.aper_base;
	if (rdev->flags & RADEON_IS_IGP)
		base = (RREG32(RADEON_NB_TOM) & 0xffff) << 16;
	radeon_vram_location(rdev, &rdev->mc, base);
	rdev->mc.gtt_base_align = 0;
	if (!(rdev->flags & RADEON_IS_AGP))
		radeon_gtt_location(rdev, &rdev->mc);
	radeon_update_bandwidth_info(rdev);
}


/*
 * Indirect registers accessor
 */
void r100_pll_errata_after_index(struct radeon_device *rdev)
{
	if (rdev->pll_errata & CHIP_ERRATA_PLL_DUMMYREADS) {
		(void)RREG32(RADEON_CLOCK_CNTL_DATA);
		(void)RREG32(RADEON_CRTC_GEN_CNTL);
	}
}

static void r100_pll_errata_after_data(struct radeon_device *rdev)
{
	/* This workarounds is necessary on RV100, RS100 and RS200 chips
	 * or the chip could hang on a subsequent access
	 */
	if (rdev->pll_errata & CHIP_ERRATA_PLL_DELAY) {
		mdelay(5);
	}

	/* This function is required to workaround a hardware bug in some (all?)
	 * revisions of the R300.  This workaround should be called after every
	 * CLOCK_CNTL_INDEX register access.  If not, register reads afterward
	 * may not be correct.
	 */
	if (rdev->pll_errata & CHIP_ERRATA_R300_CG) {
		uint32_t save, tmp;

		save = RREG32(RADEON_CLOCK_CNTL_INDEX);
		tmp = save & ~(0x3f | RADEON_PLL_WR_EN);
		WREG32(RADEON_CLOCK_CNTL_INDEX, tmp);
		tmp = RREG32(RADEON_CLOCK_CNTL_DATA);
		WREG32(RADEON_CLOCK_CNTL_INDEX, save);
	}
}

uint32_t r100_pll_rreg(struct radeon_device *rdev, uint32_t reg)
{
	unsigned long flags;
	uint32_t data;

	spin_lock_irqsave(&rdev->pll_idx_lock, flags);
	WREG8(RADEON_CLOCK_CNTL_INDEX, reg & 0x3f);
	r100_pll_errata_after_index(rdev);
	data = RREG32(RADEON_CLOCK_CNTL_DATA);
	r100_pll_errata_after_data(rdev);
	spin_unlock_irqrestore(&rdev->pll_idx_lock, flags);
	return data;
}

void r100_pll_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v)
{
	unsigned long flags;

	spin_lock_irqsave(&rdev->pll_idx_lock, flags);
	WREG8(RADEON_CLOCK_CNTL_INDEX, ((reg & 0x3f) | RADEON_PLL_WR_EN));
	r100_pll_errata_after_index(rdev);
	WREG32(RADEON_CLOCK_CNTL_DATA, v);
	r100_pll_errata_after_data(rdev);
	spin_unlock_irqrestore(&rdev->pll_idx_lock, flags);
}

static void r100_set_safe_registers(struct radeon_device *rdev)
{
	if (ASIC_IS_RN50(rdev)) {
		rdev->config.r100.reg_safe_bm = rn50_reg_safe_bm;
		rdev->config.r100.reg_safe_bm_size = ARRAY_SIZE(rn50_reg_safe_bm);
	} else if (rdev->family < CHIP_R200) {
		rdev->config.r100.reg_safe_bm = r100_reg_safe_bm;
		rdev->config.r100.reg_safe_bm_size = ARRAY_SIZE(r100_reg_safe_bm);
	} else {
		r200_set_safe_registers(rdev);
	}
}

/*
 * Debugfs info
 */
#if defined(CONFIG_DEBUG_FS)
static int r100_debugfs_rbbm_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t reg, value;
	unsigned i;

	seq_printf(m, "RBBM_STATUS 0x%08x\n", RREG32(RADEON_RBBM_STATUS));
	seq_printf(m, "RBBM_CMDFIFO_STAT 0x%08x\n", RREG32(0xE7C));
	seq_printf(m, "CP_STAT 0x%08x\n", RREG32(RADEON_CP_STAT));
	for (i = 0; i < 64; i++) {
		WREG32(RADEON_RBBM_CMDFIFO_ADDR, i | 0x100);
		reg = (RREG32(RADEON_RBBM_CMDFIFO_DATA) - 1) >> 2;
		WREG32(RADEON_RBBM_CMDFIFO_ADDR, i);
		value = RREG32(RADEON_RBBM_CMDFIFO_DATA);
		seq_printf(m, "[0x%03X] 0x%04X=0x%08X\n", i, reg, value);
	}
	return 0;
}

static int r100_debugfs_cp_ring_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	uint32_t rdp, wdp;
	unsigned count, i, j;

	radeon_ring_free_size(rdev, ring);
	rdp = RREG32(RADEON_CP_RB_RPTR);
	wdp = RREG32(RADEON_CP_RB_WPTR);
	count = (rdp + ring->ring_size - wdp) & ring->ptr_mask;
	seq_printf(m, "CP_STAT 0x%08x\n", RREG32(RADEON_CP_STAT));
	seq_printf(m, "CP_RB_WPTR 0x%08x\n", wdp);
	seq_printf(m, "CP_RB_RPTR 0x%08x\n", rdp);
	seq_printf(m, "%u free dwords in ring\n", ring->ring_free_dw);
	seq_printf(m, "%u dwords in ring\n", count);
	if (ring->ready) {
		for (j = 0; j <= count; j++) {
			i = (rdp + j) & ring->ptr_mask;
			seq_printf(m, "r[%04d]=0x%08x\n", i, ring->ring[i]);
		}
	}
	return 0;
}


static int r100_debugfs_cp_csq_fifo(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t csq_stat, csq2_stat, tmp;
	unsigned r_rptr, r_wptr, ib1_rptr, ib1_wptr, ib2_rptr, ib2_wptr;
	unsigned i;

	seq_printf(m, "CP_STAT 0x%08x\n", RREG32(RADEON_CP_STAT));
	seq_printf(m, "CP_CSQ_MODE 0x%08x\n", RREG32(RADEON_CP_CSQ_MODE));
	csq_stat = RREG32(RADEON_CP_CSQ_STAT);
	csq2_stat = RREG32(RADEON_CP_CSQ2_STAT);
	r_rptr = (csq_stat >> 0) & 0x3ff;
	r_wptr = (csq_stat >> 10) & 0x3ff;
	ib1_rptr = (csq_stat >> 20) & 0x3ff;
	ib1_wptr = (csq2_stat >> 0) & 0x3ff;
	ib2_rptr = (csq2_stat >> 10) & 0x3ff;
	ib2_wptr = (csq2_stat >> 20) & 0x3ff;
	seq_printf(m, "CP_CSQ_STAT 0x%08x\n", csq_stat);
	seq_printf(m, "CP_CSQ2_STAT 0x%08x\n", csq2_stat);
	seq_printf(m, "Ring rptr %u\n", r_rptr);
	seq_printf(m, "Ring wptr %u\n", r_wptr);
	seq_printf(m, "Indirect1 rptr %u\n", ib1_rptr);
	seq_printf(m, "Indirect1 wptr %u\n", ib1_wptr);
	seq_printf(m, "Indirect2 rptr %u\n", ib2_rptr);
	seq_printf(m, "Indirect2 wptr %u\n", ib2_wptr);
	/* FIXME: 0, 128, 640 depends on fifo setup see cp_init_kms
	 * 128 = indirect1_start * 8 & 640 = indirect2_start * 8 */
	seq_printf(m, "Ring fifo:\n");
	for (i = 0; i < 256; i++) {
		WREG32(RADEON_CP_CSQ_ADDR, i << 2);
		tmp = RREG32(RADEON_CP_CSQ_DATA);
		seq_printf(m, "rfifo[%04d]=0x%08X\n", i, tmp);
	}
	seq_printf(m, "Indirect1 fifo:\n");
	for (i = 256; i <= 512; i++) {
		WREG32(RADEON_CP_CSQ_ADDR, i << 2);
		tmp = RREG32(RADEON_CP_CSQ_DATA);
		seq_printf(m, "ib1fifo[%04d]=0x%08X\n", i, tmp);
	}
	seq_printf(m, "Indirect2 fifo:\n");
	for (i = 640; i < ib1_wptr; i++) {
		WREG32(RADEON_CP_CSQ_ADDR, i << 2);
		tmp = RREG32(RADEON_CP_CSQ_DATA);
		seq_printf(m, "ib2fifo[%04d]=0x%08X\n", i, tmp);
	}
	return 0;
}

static int r100_debugfs_mc_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t tmp;

	tmp = RREG32(RADEON_CONFIG_MEMSIZE);
	seq_printf(m, "CONFIG_MEMSIZE 0x%08x\n", tmp);
	tmp = RREG32(RADEON_MC_FB_LOCATION);
	seq_printf(m, "MC_FB_LOCATION 0x%08x\n", tmp);
	tmp = RREG32(RADEON_BUS_CNTL);
	seq_printf(m, "BUS_CNTL 0x%08x\n", tmp);
	tmp = RREG32(RADEON_MC_AGP_LOCATION);
	seq_printf(m, "MC_AGP_LOCATION 0x%08x\n", tmp);
	tmp = RREG32(RADEON_AGP_BASE);
	seq_printf(m, "AGP_BASE 0x%08x\n", tmp);
	tmp = RREG32(RADEON_HOST_PATH_CNTL);
	seq_printf(m, "HOST_PATH_CNTL 0x%08x\n", tmp);
	tmp = RREG32(0x01D0);
	seq_printf(m, "AIC_CTRL 0x%08x\n", tmp);
	tmp = RREG32(RADEON_AIC_LO_ADDR);
	seq_printf(m, "AIC_LO_ADDR 0x%08x\n", tmp);
	tmp = RREG32(RADEON_AIC_HI_ADDR);
	seq_printf(m, "AIC_HI_ADDR 0x%08x\n", tmp);
	tmp = RREG32(0x01E4);
	seq_printf(m, "AIC_TLB_ADDR 0x%08x\n", tmp);
	return 0;
}

static struct drm_info_list r100_debugfs_rbbm_list[] = {
	{"r100_rbbm_info", r100_debugfs_rbbm_info, 0, NULL},
};

static struct drm_info_list r100_debugfs_cp_list[] = {
	{"r100_cp_ring_info", r100_debugfs_cp_ring_info, 0, NULL},
	{"r100_cp_csq_fifo", r100_debugfs_cp_csq_fifo, 0, NULL},
};

static struct drm_info_list r100_debugfs_mc_info_list[] = {
	{"r100_mc_info", r100_debugfs_mc_info, 0, NULL},
};
#endif

int r100_debugfs_rbbm_init(struct radeon_device *rdev)
{
#if defined(CONFIG_DEBUG_FS)
	return radeon_debugfs_add_files(rdev, r100_debugfs_rbbm_list, 1);
#else
	return 0;
#endif
}

int r100_debugfs_cp_init(struct radeon_device *rdev)
{
#if defined(CONFIG_DEBUG_FS)
	return radeon_debugfs_add_files(rdev, r100_debugfs_cp_list, 2);
#else
	return 0;
#endif
}

int r100_debugfs_mc_info_init(struct radeon_device *rdev)
{
#if defined(CONFIG_DEBUG_FS)
	return radeon_debugfs_add_files(rdev, r100_debugfs_mc_info_list, 1);
#else
	return 0;
#endif
}

int r100_set_surface_reg(struct radeon_device *rdev, int reg,
			 uint32_t tiling_flags, uint32_t pitch,
			 uint32_t offset, uint32_t obj_size)
{
	int surf_index = reg * 16;
	int flags = 0;

	if (rdev->family <= CHIP_RS200) {
		if ((tiling_flags & (RADEON_TILING_MACRO|RADEON_TILING_MICRO))
				 == (RADEON_TILING_MACRO|RADEON_TILING_MICRO))
			flags |= RADEON_SURF_TILE_COLOR_BOTH;
		if (tiling_flags & RADEON_TILING_MACRO)
			flags |= RADEON_SURF_TILE_COLOR_MACRO;
		/* setting pitch to 0 disables tiling */
		if ((tiling_flags & (RADEON_TILING_MACRO|RADEON_TILING_MICRO))
				== 0)
			pitch = 0;
	} else if (rdev->family <= CHIP_RV280) {
		if (tiling_flags & (RADEON_TILING_MACRO))
			flags |= R200_SURF_TILE_COLOR_MACRO;
		if (tiling_flags & RADEON_TILING_MICRO)
			flags |= R200_SURF_TILE_COLOR_MICRO;
	} else {
		if (tiling_flags & RADEON_TILING_MACRO)
			flags |= R300_SURF_TILE_MACRO;
		if (tiling_flags & RADEON_TILING_MICRO)
			flags |= R300_SURF_TILE_MICRO;
	}

	if (tiling_flags & RADEON_TILING_SWAP_16BIT)
		flags |= RADEON_SURF_AP0_SWP_16BPP | RADEON_SURF_AP1_SWP_16BPP;
	if (tiling_flags & RADEON_TILING_SWAP_32BIT)
		flags |= RADEON_SURF_AP0_SWP_32BPP | RADEON_SURF_AP1_SWP_32BPP;

	/* r100/r200 divide by 16 */
	if (rdev->family < CHIP_R300)
		flags |= pitch / 16;
	else
		flags |= pitch / 8;


	DRM_DEBUG_KMS("writing surface %d %d %x %x\n", reg, flags, offset, offset+obj_size-1);
	WREG32(RADEON_SURFACE0_INFO + surf_index, flags);
	WREG32(RADEON_SURFACE0_LOWER_BOUND + surf_index, offset);
	WREG32(RADEON_SURFACE0_UPPER_BOUND + surf_index, offset + obj_size - 1);
	return 0;
}

void r100_clear_surface_reg(struct radeon_device *rdev, int reg)
{
	int surf_index = reg * 16;
	WREG32(RADEON_SURFACE0_INFO + surf_index, 0);
}

void r100_bandwidth_update(struct radeon_device *rdev)
{
	fixed20_12 trcd_ff, trp_ff, tras_ff, trbs_ff, tcas_ff;
	fixed20_12 sclk_ff, mclk_ff, sclk_eff_ff, sclk_delay_ff;
	fixed20_12 peak_disp_bw, mem_bw, pix_clk, pix_clk2, temp_ff;
	fixed20_12 crit_point_ff = {0};
	uint32_t temp, data, mem_trcd, mem_trp, mem_tras;
	fixed20_12 memtcas_ff[8] = {
		dfixed_init(1),
		dfixed_init(2),
		dfixed_init(3),
		dfixed_init(0),
		dfixed_init_half(1),
		dfixed_init_half(2),
		dfixed_init(0),
	};
	fixed20_12 memtcas_rs480_ff[8] = {
		dfixed_init(0),
		dfixed_init(1),
		dfixed_init(2),
		dfixed_init(3),
		dfixed_init(0),
		dfixed_init_half(1),
		dfixed_init_half(2),
		dfixed_init_half(3),
	};
	fixed20_12 memtcas2_ff[8] = {
		dfixed_init(0),
		dfixed_init(1),
		dfixed_init(2),
		dfixed_init(3),
		dfixed_init(4),
		dfixed_init(5),
		dfixed_init(6),
		dfixed_init(7),
	};
	fixed20_12 memtrbs[8] = {
		dfixed_init(1),
		dfixed_init_half(1),
		dfixed_init(2),
		dfixed_init_half(2),
		dfixed_init(3),
		dfixed_init_half(3),
		dfixed_init(4),
		dfixed_init_half(4)
	};
	fixed20_12 memtrbs_r4xx[8] = {
		dfixed_init(4),
		dfixed_init(5),
		dfixed_init(6),
		dfixed_init(7),
		dfixed_init(8),
		dfixed_init(9),
		dfixed_init(10),
		dfixed_init(11)
	};
	fixed20_12 min_mem_eff;
	fixed20_12 mc_latency_sclk, mc_latency_mclk, k1;
	fixed20_12 cur_latency_mclk, cur_latency_sclk;
	fixed20_12 disp_latency, disp_latency_overhead, disp_drain_rate = {0},
		disp_drain_rate2, read_return_rate;
	fixed20_12 time_disp1_drop_priority;
	int c;
	int cur_size = 16;       /* in octawords */
	int critical_point = 0, critical_point2;
/* 	uint32_t read_return_rate, time_disp1_drop_priority; */
	int stop_req, max_stop_req;
	struct drm_display_mode *mode1 = NULL;
	struct drm_display_mode *mode2 = NULL;
	uint32_t pixel_bytes1 = 0;
	uint32_t pixel_bytes2 = 0;

	/* Guess line buffer size to be 8192 pixels */
	u32 lb_size = 8192;

	if (!rdev->mode_info.mode_config_initialized)
		return;

	radeon_update_display_priority(rdev);

	if (rdev->mode_info.crtcs[0]->base.enabled) {
		mode1 = &rdev->mode_info.crtcs[0]->base.mode;
		pixel_bytes1 = rdev->mode_info.crtcs[0]->base.primary->fb->bits_per_pixel / 8;
	}
	if (!(rdev->flags & RADEON_SINGLE_CRTC)) {
		if (rdev->mode_info.crtcs[1]->base.enabled) {
			mode2 = &rdev->mode_info.crtcs[1]->base.mode;
			pixel_bytes2 = rdev->mode_info.crtcs[1]->base.primary->fb->bits_per_pixel / 8;
		}
	}

	min_mem_eff.full = dfixed_const_8(0);
	/* get modes */
	if ((rdev->disp_priority == 2) && ASIC_IS_R300(rdev)) {
		uint32_t mc_init_misc_lat_timer = RREG32(R300_MC_INIT_MISC_LAT_TIMER);
		mc_init_misc_lat_timer &= ~(R300_MC_DISP1R_INIT_LAT_MASK << R300_MC_DISP1R_INIT_LAT_SHIFT);
		mc_init_misc_lat_timer &= ~(R300_MC_DISP0R_INIT_LAT_MASK << R300_MC_DISP0R_INIT_LAT_SHIFT);
		/* check crtc enables */
		if (mode2)
			mc_init_misc_lat_timer |= (1 << R300_MC_DISP1R_INIT_LAT_SHIFT);
		if (mode1)
			mc_init_misc_lat_timer |= (1 << R300_MC_DISP0R_INIT_LAT_SHIFT);
		WREG32(R300_MC_INIT_MISC_LAT_TIMER, mc_init_misc_lat_timer);
	}

	/*
	 * determine is there is enough bw for current mode
	 */
	sclk_ff = rdev->pm.sclk;
	mclk_ff = rdev->pm.mclk;

	temp = (rdev->mc.vram_width / 8) * (rdev->mc.vram_is_ddr ? 2 : 1);
	temp_ff.full = dfixed_const(temp);
	mem_bw.full = dfixed_mul(mclk_ff, temp_ff);

	pix_clk.full = 0;
	pix_clk2.full = 0;
	peak_disp_bw.full = 0;
	if (mode1) {
		temp_ff.full = dfixed_const(1000);
		pix_clk.full = dfixed_const(mode1->clock); /* convert to fixed point */
		pix_clk.full = dfixed_div(pix_clk, temp_ff);
		temp_ff.full = dfixed_const(pixel_bytes1);
		peak_disp_bw.full += dfixed_mul(pix_clk, temp_ff);
	}
	if (mode2) {
		temp_ff.full = dfixed_const(1000);
		pix_clk2.full = dfixed_const(mode2->clock); /* convert to fixed point */
		pix_clk2.full = dfixed_div(pix_clk2, temp_ff);
		temp_ff.full = dfixed_const(pixel_bytes2);
		peak_disp_bw.full += dfixed_mul(pix_clk2, temp_ff);
	}

	mem_bw.full = dfixed_mul(mem_bw, min_mem_eff);
	if (peak_disp_bw.full >= mem_bw.full) {
		DRM_ERROR("You may not have enough display bandwidth for current mode\n"
			  "If you have flickering problem, try to lower resolution, refresh rate, or color depth\n");
	}

	/*  Get values from the EXT_MEM_CNTL register...converting its contents. */
	temp = RREG32(RADEON_MEM_TIMING_CNTL);
	if ((rdev->family == CHIP_RV100) || (rdev->flags & RADEON_IS_IGP)) { /* RV100, M6, IGPs */
		mem_trcd = ((temp >> 2) & 0x3) + 1;
		mem_trp  = ((temp & 0x3)) + 1;
		mem_tras = ((temp & 0x70) >> 4) + 1;
	} else if (rdev->family == CHIP_R300 ||
		   rdev->family == CHIP_R350) { /* r300, r350 */
		mem_trcd = (temp & 0x7) + 1;
		mem_trp = ((temp >> 8) & 0x7) + 1;
		mem_tras = ((temp >> 11) & 0xf) + 4;
	} else if (rdev->family == CHIP_RV350 ||
		   rdev->family <= CHIP_RV380) {
		/* rv3x0 */
		mem_trcd = (temp & 0x7) + 3;
		mem_trp = ((temp >> 8) & 0x7) + 3;
		mem_tras = ((temp >> 11) & 0xf) + 6;
	} else if (rdev->family == CHIP_R420 ||
		   rdev->family == CHIP_R423 ||
		   rdev->family == CHIP_RV410) {
		/* r4xx */
		mem_trcd = (temp & 0xf) + 3;
		if (mem_trcd > 15)
			mem_trcd = 15;
		mem_trp = ((temp >> 8) & 0xf) + 3;
		if (mem_trp > 15)
			mem_trp = 15;
		mem_tras = ((temp >> 12) & 0x1f) + 6;
		if (mem_tras > 31)
			mem_tras = 31;
	} else { /* RV200, R200 */
		mem_trcd = (temp & 0x7) + 1;
		mem_trp = ((temp >> 8) & 0x7) + 1;
		mem_tras = ((temp >> 12) & 0xf) + 4;
	}
	/* convert to FF */
	trcd_ff.full = dfixed_const(mem_trcd);
	trp_ff.full = dfixed_const(mem_trp);
	tras_ff.full = dfixed_const(mem_tras);

	/* Get values from the MEM_SDRAM_MODE_REG register...converting its */
	temp = RREG32(RADEON_MEM_SDRAM_MODE_REG);
	data = (temp & (7 << 20)) >> 20;
	if ((rdev->family == CHIP_RV100) || rdev->flags & RADEON_IS_IGP) {
		if (rdev->family == CHIP_RS480) /* don't think rs400 */
			tcas_ff = memtcas_rs480_ff[data];
		else
			tcas_ff = memtcas_ff[data];
	} else
		tcas_ff = memtcas2_ff[data];

	if (rdev->family == CHIP_RS400 ||
	    rdev->family == CHIP_RS480) {
		/* extra cas latency stored in bits 23-25 0-4 clocks */
		data = (temp >> 23) & 0x7;
		if (data < 5)
			tcas_ff.full += dfixed_const(data);
	}

	if (ASIC_IS_R300(rdev) && !(rdev->flags & RADEON_IS_IGP)) {
		/* on the R300, Tcas is included in Trbs.
		 */
		temp = RREG32(RADEON_MEM_CNTL);
		data = (R300_MEM_NUM_CHANNELS_MASK & temp);
		if (data == 1) {
			if (R300_MEM_USE_CD_CH_ONLY & temp) {
				temp = RREG32(R300_MC_IND_INDEX);
				temp &= ~R300_MC_IND_ADDR_MASK;
				temp |= R300_MC_READ_CNTL_CD_mcind;
				WREG32(R300_MC_IND_INDEX, temp);
				temp = RREG32(R300_MC_IND_DATA);
				data = (R300_MEM_RBS_POSITION_C_MASK & temp);
			} else {
				temp = RREG32(R300_MC_READ_CNTL_AB);
				data = (R300_MEM_RBS_POSITION_A_MASK & temp);
			}
		} else {
			temp = RREG32(R300_MC_READ_CNTL_AB);
			data = (R300_MEM_RBS_POSITION_A_MASK & temp);
		}
		if (rdev->family == CHIP_RV410 ||
		    rdev->family == CHIP_R420 ||
		    rdev->family == CHIP_R423)
			trbs_ff = memtrbs_r4xx[data];
		else
			trbs_ff = memtrbs[data];
		tcas_ff.full += trbs_ff.full;
	}

	sclk_eff_ff.full = sclk_ff.full;

	if (rdev->flags & RADEON_IS_AGP) {
		fixed20_12 agpmode_ff;
		agpmode_ff.full = dfixed_const(radeon_agpmode);
		temp_ff.full = dfixed_const_666(16);
		sclk_eff_ff.full -= dfixed_mul(agpmode_ff, temp_ff);
	}
	/* TODO PCIE lanes may affect this - agpmode == 16?? */

	if (ASIC_IS_R300(rdev)) {
		sclk_delay_ff.full = dfixed_const(250);
	} else {
		if ((rdev->family == CHIP_RV100) ||
		    rdev->flags & RADEON_IS_IGP) {
			if (rdev->mc.vram_is_ddr)
				sclk_delay_ff.full = dfixed_const(41);
			else
				sclk_delay_ff.full = dfixed_const(33);
		} else {
			if (rdev->mc.vram_width == 128)
				sclk_delay_ff.full = dfixed_const(57);
			else
				sclk_delay_ff.full = dfixed_const(41);
		}
	}

	mc_latency_sclk.full = dfixed_div(sclk_delay_ff, sclk_eff_ff);

	if (rdev->mc.vram_is_ddr) {
		if (rdev->mc.vram_width == 32) {
			k1.full = dfixed_const(40);
			c  = 3;
		} else {
			k1.full = dfixed_const(20);
			c  = 1;
		}
	} else {
		k1.full = dfixed_const(40);
		c  = 3;
	}

	temp_ff.full = dfixed_const(2);
	mc_latency_mclk.full = dfixed_mul(trcd_ff, temp_ff);
	temp_ff.full = dfixed_const(c);
	mc_latency_mclk.full += dfixed_mul(tcas_ff, temp_ff);
	temp_ff.full = dfixed_const(4);
	mc_latency_mclk.full += dfixed_mul(tras_ff, temp_ff);
	mc_latency_mclk.full += dfixed_mul(trp_ff, temp_ff);
	mc_latency_mclk.full += k1.full;

	mc_latency_mclk.full = dfixed_div(mc_latency_mclk, mclk_ff);
	mc_latency_mclk.full += dfixed_div(temp_ff, sclk_eff_ff);

	/*
	  HW cursor time assuming worst case of full size colour cursor.
	*/
	temp_ff.full = dfixed_const((2 * (cur_size - (rdev->mc.vram_is_ddr + 1))));
	temp_ff.full += trcd_ff.full;
	if (temp_ff.full < tras_ff.full)
		temp_ff.full = tras_ff.full;
	cur_latency_mclk.full = dfixed_div(temp_ff, mclk_ff);

	temp_ff.full = dfixed_const(cur_size);
	cur_latency_sclk.full = dfixed_div(temp_ff, sclk_eff_ff);
	/*
	  Find the total latency for the display data.
	*/
	disp_latency_overhead.full = dfixed_const(8);
	disp_latency_overhead.full = dfixed_div(disp_latency_overhead, sclk_ff);
	mc_latency_mclk.full += disp_latency_overhead.full + cur_latency_mclk.full;
	mc_latency_sclk.full += disp_latency_overhead.full + cur_latency_sclk.full;

	if (mc_latency_mclk.full > mc_latency_sclk.full)
		disp_latency.full = mc_latency_mclk.full;
	else
		disp_latency.full = mc_latency_sclk.full;

	/* setup Max GRPH_STOP_REQ default value */
	if (ASIC_IS_RV100(rdev))
		max_stop_req = 0x5c;
	else
		max_stop_req = 0x7c;

	if (mode1) {
		/*  CRTC1
		    Set GRPH_BUFFER_CNTL register using h/w defined optimal values.
		    GRPH_STOP_REQ <= MIN[ 0x7C, (CRTC_H_DISP + 1) * (bit depth) / 0x10 ]
		*/
		stop_req = mode1->hdisplay * pixel_bytes1 / 16;

		if (stop_req > max_stop_req)
			stop_req = max_stop_req;

		/*
		  Find the drain rate of the display buffer.
		*/
		temp_ff.full = dfixed_const((16/pixel_bytes1));
		disp_drain_rate.full = dfixed_div(pix_clk, temp_ff);

		/*
		  Find the critical point of the display buffer.
		*/
		crit_point_ff.full = dfixed_mul(disp_drain_rate, disp_latency);
		crit_point_ff.full += dfixed_const_half(0);

		critical_point = dfixed_trunc(crit_point_ff);

		if (rdev->disp_priority == 2) {
			critical_point = 0;
		}

		/*
		  The critical point should never be above max_stop_req-4.  Setting
		  GRPH_CRITICAL_CNTL = 0 will thus force high priority all the time.
		*/
		if (max_stop_req - critical_point < 4)
			critical_point = 0;

		if (critical_point == 0 && mode2 && rdev->family == CHIP_R300) {
			/* some R300 cards have problem with this set to 0, when CRTC2 is enabled.*/
			critical_point = 0x10;
		}

		temp = RREG32(RADEON_GRPH_BUFFER_CNTL);
		temp &= ~(RADEON_GRPH_STOP_REQ_MASK);
		temp |= (stop_req << RADEON_GRPH_STOP_REQ_SHIFT);
		temp &= ~(RADEON_GRPH_START_REQ_MASK);
		if ((rdev->family == CHIP_R350) &&
		    (stop_req > 0x15)) {
			stop_req -= 0x10;
		}
		temp |= (stop_req << RADEON_GRPH_START_REQ_SHIFT);
		temp |= RADEON_GRPH_BUFFER_SIZE;
		temp &= ~(RADEON_GRPH_CRITICAL_CNTL   |
			  RADEON_GRPH_CRITICAL_AT_SOF |
			  RADEON_GRPH_STOP_CNTL);
		/*
		  Write the result into the register.
		*/
		WREG32(RADEON_GRPH_BUFFER_CNTL, ((temp & ~RADEON_GRPH_CRITICAL_POINT_MASK) |
						       (critical_point << RADEON_GRPH_CRITICAL_POINT_SHIFT)));

#if 0
		if ((rdev->family == CHIP_RS400) ||
		    (rdev->family == CHIP_RS480)) {
			/* attempt to program RS400 disp regs correctly ??? */
			temp = RREG32(RS400_DISP1_REG_CNTL);
			temp &= ~(RS400_DISP1_START_REQ_LEVEL_MASK |
				  RS400_DISP1_STOP_REQ_LEVEL_MASK);
			WREG32(RS400_DISP1_REQ_CNTL1, (temp |
						       (critical_point << RS400_DISP1_START_REQ_LEVEL_SHIFT) |
						       (critical_point << RS400_DISP1_STOP_REQ_LEVEL_SHIFT)));
			temp = RREG32(RS400_DMIF_MEM_CNTL1);
			temp &= ~(RS400_DISP1_CRITICAL_POINT_START_MASK |
				  RS400_DISP1_CRITICAL_POINT_STOP_MASK);
			WREG32(RS400_DMIF_MEM_CNTL1, (temp |
						      (critical_point << RS400_DISP1_CRITICAL_POINT_START_SHIFT) |
						      (critical_point << RS400_DISP1_CRITICAL_POINT_STOP_SHIFT)));
		}
#endif

		DRM_DEBUG_KMS("GRPH_BUFFER_CNTL from to %x\n",
			  /* 	  (unsigned int)info->SavedReg->grph_buffer_cntl, */
			  (unsigned int)RREG32(RADEON_GRPH_BUFFER_CNTL));
	}

	if (mode2) {
		u32 grph2_cntl;
		stop_req = mode2->hdisplay * pixel_bytes2 / 16;

		if (stop_req > max_stop_req)
			stop_req = max_stop_req;

		/*
		  Find the drain rate of the display buffer.
		*/
		temp_ff.full = dfixed_const((16/pixel_bytes2));
		disp_drain_rate2.full = dfixed_div(pix_clk2, temp_ff);

		grph2_cntl = RREG32(RADEON_GRPH2_BUFFER_CNTL);
		grph2_cntl &= ~(RADEON_GRPH_STOP_REQ_MASK);
		grph2_cntl |= (stop_req << RADEON_GRPH_STOP_REQ_SHIFT);
		grph2_cntl &= ~(RADEON_GRPH_START_REQ_MASK);
		if ((rdev->family == CHIP_R350) &&
		    (stop_req > 0x15)) {
			stop_req -= 0x10;
		}
		grph2_cntl |= (stop_req << RADEON_GRPH_START_REQ_SHIFT);
		grph2_cntl |= RADEON_GRPH_BUFFER_SIZE;
		grph2_cntl &= ~(RADEON_GRPH_CRITICAL_CNTL   |
			  RADEON_GRPH_CRITICAL_AT_SOF |
			  RADEON_GRPH_STOP_CNTL);

		if ((rdev->family == CHIP_RS100) ||
		    (rdev->family == CHIP_RS200))
			critical_point2 = 0;
		else {
			temp = (rdev->mc.vram_width * rdev->mc.vram_is_ddr + 1)/128;
			temp_ff.full = dfixed_const(temp);
			temp_ff.full = dfixed_mul(mclk_ff, temp_ff);
			if (sclk_ff.full < temp_ff.full)
				temp_ff.full = sclk_ff.full;

			read_return_rate.full = temp_ff.full;

			if (mode1) {
				temp_ff.full = read_return_rate.full - disp_drain_rate.full;
				time_disp1_drop_priority.full = dfixed_div(crit_point_ff, temp_ff);
			} else {
				time_disp1_drop_priority.full = 0;
			}
			crit_point_ff.full = disp_latency.full + time_disp1_drop_priority.full + disp_latency.full;
			crit_point_ff.full = dfixed_mul(crit_point_ff, disp_drain_rate2);
			crit_point_ff.full += dfixed_const_half(0);

			critical_point2 = dfixed_trunc(crit_point_ff);

			if (rdev->disp_priority == 2) {
				critical_point2 = 0;
			}

			if (max_stop_req - critical_point2 < 4)
				critical_point2 = 0;

		}

		if (critical_point2 == 0 && rdev->family == CHIP_R300) {
			/* some R300 cards have problem with this set to 0 */
			critical_point2 = 0x10;
		}

		WREG32(RADEON_GRPH2_BUFFER_CNTL, ((grph2_cntl & ~RADEON_GRPH_CRITICAL_POINT_MASK) |
						  (critical_point2 << RADEON_GRPH_CRITICAL_POINT_SHIFT)));

		if ((rdev->family == CHIP_RS400) ||
		    (rdev->family == CHIP_RS480)) {
#if 0
			/* attempt to program RS400 disp2 regs correctly ??? */
			temp = RREG32(RS400_DISP2_REQ_CNTL1);
			temp &= ~(RS400_DISP2_START_REQ_LEVEL_MASK |
				  RS400_DISP2_STOP_REQ_LEVEL_MASK);
			WREG32(RS400_DISP2_REQ_CNTL1, (temp |
						       (critical_point2 << RS400_DISP1_START_REQ_LEVEL_SHIFT) |
						       (critical_point2 << RS400_DISP1_STOP_REQ_LEVEL_SHIFT)));
			temp = RREG32(RS400_DISP2_REQ_CNTL2);
			temp &= ~(RS400_DISP2_CRITICAL_POINT_START_MASK |
				  RS400_DISP2_CRITICAL_POINT_STOP_MASK);
			WREG32(RS400_DISP2_REQ_CNTL2, (temp |
						       (critical_point2 << RS400_DISP2_CRITICAL_POINT_START_SHIFT) |
						       (critical_point2 << RS400_DISP2_CRITICAL_POINT_STOP_SHIFT)));
#endif
			WREG32(RS400_DISP2_REQ_CNTL1, 0x105DC1CC);
			WREG32(RS400_DISP2_REQ_CNTL2, 0x2749D000);
			WREG32(RS400_DMIF_MEM_CNTL1,  0x29CA71DC);
			WREG32(RS400_DISP1_REQ_CNTL1, 0x28FBC3AC);
		}

		DRM_DEBUG_KMS("GRPH2_BUFFER_CNTL from to %x\n",
			  (unsigned int)RREG32(RADEON_GRPH2_BUFFER_CNTL));
	}

	/* Save number of lines the linebuffer leads before the scanout */
	if (mode1)
	    rdev->mode_info.crtcs[0]->lb_vblank_lead_lines = DIV_ROUND_UP(lb_size, mode1->crtc_hdisplay);

	if (mode2)
	    rdev->mode_info.crtcs[1]->lb_vblank_lead_lines = DIV_ROUND_UP(lb_size, mode2->crtc_hdisplay);
}

int r100_ring_test(struct radeon_device *rdev, struct radeon_ring *ring)
{
	uint32_t scratch;
	uint32_t tmp = 0;
	unsigned i;
	int r;

	r = radeon_scratch_get(rdev, &scratch);
	if (r) {
		DRM_ERROR("radeon: cp failed to get scratch reg (%d).\n", r);
		return r;
	}
	WREG32(scratch, 0xCAFEDEAD);
	r = radeon_ring_lock(rdev, ring, 2);
	if (r) {
		DRM_ERROR("radeon: cp failed to lock ring (%d).\n", r);
		radeon_scratch_free(rdev, scratch);
		return r;
	}
	radeon_ring_write(ring, PACKET0(scratch, 0));
	radeon_ring_write(ring, 0xDEADBEEF);
	radeon_ring_unlock_commit(rdev, ring, false);
	for (i = 0; i < rdev->usec_timeout; i++) {
		tmp = RREG32(scratch);
		if (tmp == 0xDEADBEEF) {
			break;
		}
		DRM_UDELAY(1);
	}
	if (i < rdev->usec_timeout) {
		DRM_INFO("ring test succeeded in %d usecs\n", i);
	} else {
		DRM_ERROR("radeon: ring test failed (scratch(0x%04X)=0x%08X)\n",
			  scratch, tmp);
		r = -EINVAL;
	}
	radeon_scratch_free(rdev, scratch);
	return r;
}

void r100_ring_ib_execute(struct radeon_device *rdev, struct radeon_ib *ib)
{
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];

	if (ring->rptr_save_reg) {
		u32 next_rptr = ring->wptr + 2 + 3;
		radeon_ring_write(ring, PACKET0(ring->rptr_save_reg, 0));
		radeon_ring_write(ring, next_rptr);
	}

	radeon_ring_write(ring, PACKET0(RADEON_CP_IB_BASE, 1));
	radeon_ring_write(ring, ib->gpu_addr);
	radeon_ring_write(ring, ib->length_dw);
}

int r100_ib_test(struct radeon_device *rdev, struct radeon_ring *ring)
{
	struct radeon_ib ib;
	uint32_t scratch;
	uint32_t tmp = 0;
	unsigned i;
	int r;

	r = radeon_scratch_get(rdev, &scratch);
	if (r) {
		DRM_ERROR("radeon: failed to get scratch reg (%d).\n", r);
		return r;
	}
	WREG32(scratch, 0xCAFEDEAD);
	r = radeon_ib_get(rdev, RADEON_RING_TYPE_GFX_INDEX, &ib, NULL, 256);
	if (r) {
		DRM_ERROR("radeon: failed to get ib (%d).\n", r);
		goto free_scratch;
	}
	ib.ptr[0] = PACKET0(scratch, 0);
	ib.ptr[1] = 0xDEADBEEF;
	ib.ptr[2] = PACKET2(0);
	ib.ptr[3] = PACKET2(0);
	ib.ptr[4] = PACKET2(0);
	ib.ptr[5] = PACKET2(0);
	ib.ptr[6] = PACKET2(0);
	ib.ptr[7] = PACKET2(0);
	ib.length_dw = 8;
	r = radeon_ib_schedule(rdev, &ib, NULL, false);
	if (r) {
		DRM_ERROR("radeon: failed to schedule ib (%d).\n", r);
		goto free_ib;
	}
	r = radeon_fence_wait_timeout(ib.fence, false, usecs_to_jiffies(
		RADEON_USEC_IB_TEST_TIMEOUT));
	if (r < 0) {
		DRM_ERROR("radeon: fence wait failed (%d).\n", r);
		goto free_ib;
	} else if (r == 0) {
		DRM_ERROR("radeon: fence wait timed out.\n");
		r = -ETIMEDOUT;
		goto free_ib;
	}
	r = 0;
	for (i = 0; i < rdev->usec_timeout; i++) {
		tmp = RREG32(scratch);
		if (tmp == 0xDEADBEEF) {
			break;
		}
		DRM_UDELAY(1);
	}
	if (i < rdev->usec_timeout) {
		DRM_INFO("ib test succeeded in %u usecs\n", i);
	} else {
		DRM_ERROR("radeon: ib test failed (scratch(0x%04X)=0x%08X)\n",
			  scratch, tmp);
		r = -EINVAL;
	}
free_ib:
	radeon_ib_free(rdev, &ib);
free_scratch:
	radeon_scratch_free(rdev, scratch);
	return r;
}

void r100_mc_stop(struct radeon_device *rdev, struct r100_mc_save *save)
{
	/* Shutdown CP we shouldn't need to do that but better be safe than
	 * sorry
	 */
	rdev->ring[RADEON_RING_TYPE_GFX_INDEX].ready = false;
	WREG32(R_000740_CP_CSQ_CNTL, 0);

	/* Save few CRTC registers */
	save->GENMO_WT = RREG8(R_0003C2_GENMO_WT);
	save->CRTC_EXT_CNTL = RREG32(R_000054_CRTC_EXT_CNTL);
	save->CRTC_GEN_CNTL = RREG32(R_000050_CRTC_GEN_CNTL);
	save->CUR_OFFSET = RREG32(R_000260_CUR_OFFSET);
	if (!(rdev->flags & RADEON_SINGLE_CRTC)) {
		save->CRTC2_GEN_CNTL = RREG32(R_0003F8_CRTC2_GEN_CNTL);
		save->CUR2_OFFSET = RREG32(R_000360_CUR2_OFFSET);
	}

	/* Disable VGA aperture access */
	WREG8(R_0003C2_GENMO_WT, C_0003C2_VGA_RAM_EN & save->GENMO_WT);
	/* Disable cursor, overlay, crtc */
	WREG32(R_000260_CUR_OFFSET, save->CUR_OFFSET | S_000260_CUR_LOCK(1));
	WREG32(R_000054_CRTC_EXT_CNTL, save->CRTC_EXT_CNTL |
					S_000054_CRTC_DISPLAY_DIS(1));
	WREG32(R_000050_CRTC_GEN_CNTL,
			(C_000050_CRTC_CUR_EN & save->CRTC_GEN_CNTL) |
			S_000050_CRTC_DISP_REQ_EN_B(1));
	WREG32(R_000420_OV0_SCALE_CNTL,
		C_000420_OV0_OVERLAY_EN & RREG32(R_000420_OV0_SCALE_CNTL));
	WREG32(R_000260_CUR_OFFSET, C_000260_CUR_LOCK & save->CUR_OFFSET);
	if (!(rdev->flags & RADEON_SINGLE_CRTC)) {
		WREG32(R_000360_CUR2_OFFSET, save->CUR2_OFFSET |
						S_000360_CUR2_LOCK(1));
		WREG32(R_0003F8_CRTC2_GEN_CNTL,
			(C_0003F8_CRTC2_CUR_EN & save->CRTC2_GEN_CNTL) |
			S_0003F8_CRTC2_DISPLAY_DIS(1) |
			S_0003F8_CRTC2_DISP_REQ_EN_B(1));
		WREG32(R_000360_CUR2_OFFSET,
			C_000360_CUR2_LOCK & save->CUR2_OFFSET);
	}
}

void r100_mc_resume(struct radeon_device *rdev, struct r100_mc_save *save)
{
	/* Update base address for crtc */
	WREG32(R_00023C_DISPLAY_BASE_ADDR, rdev->mc.vram_start);
	if (!(rdev->flags & RADEON_SINGLE_CRTC)) {
		WREG32(R_00033C_CRTC2_DISPLAY_BASE_ADDR, rdev->mc.vram_start);
	}
	/* Restore CRTC registers */
	WREG8(R_0003C2_GENMO_WT, save->GENMO_WT);
	WREG32(R_000054_CRTC_EXT_CNTL, save->CRTC_EXT_CNTL);
	WREG32(R_000050_CRTC_GEN_CNTL, save->CRTC_GEN_CNTL);
	if (!(rdev->flags & RADEON_SINGLE_CRTC)) {
		WREG32(R_0003F8_CRTC2_GEN_CNTL, save->CRTC2_GEN_CNTL);
	}
}

void r100_vga_render_disable(struct radeon_device *rdev)
{
	u32 tmp;

	tmp = RREG8(R_0003C2_GENMO_WT);
	WREG8(R_0003C2_GENMO_WT, C_0003C2_VGA_RAM_EN & tmp);
}

static void r100_debugfs(struct radeon_device *rdev)
{
	int r;

	r = r100_debugfs_mc_info_init(rdev);
	if (r)
		dev_warn(rdev->dev, "Failed to create r100_mc debugfs file.\n");
}

static void r100_mc_program(struct radeon_device *rdev)
{
	struct r100_mc_save save;

	/* Stops all mc clients */
	r100_mc_stop(rdev, &save);
	if (rdev->flags & RADEON_IS_AGP) {
		WREG32(R_00014C_MC_AGP_LOCATION,
			S_00014C_MC_AGP_START(rdev->mc.gtt_start >> 16) |
			S_00014C_MC_AGP_TOP(rdev->mc.gtt_end >> 16));
		WREG32(R_000170_AGP_BASE, lower_32_bits(rdev->mc.agp_base));
		if (rdev->family > CHIP_RV200)
			WREG32(R_00015C_AGP_BASE_2,
				upper_32_bits(rdev->mc.agp_base) & 0xff);
	} else {
		WREG32(R_00014C_MC_AGP_LOCATION, 0x0FFFFFFF);
		WREG32(R_000170_AGP_BASE, 0);
		if (rdev->family > CHIP_RV200)
			WREG32(R_00015C_AGP_BASE_2, 0);
	}
	/* Wait for mc idle */
	if (r100_mc_wait_for_idle(rdev))
		dev_warn(rdev->dev, "Wait for MC idle timeout.\n");
	/* Program MC, should be a 32bits limited address space */
	WREG32(R_000148_MC_FB_LOCATION,
		S_000148_MC_FB_START(rdev->mc.vram_start >> 16) |
		S_000148_MC_FB_TOP(rdev->mc.vram_end >> 16));
	r100_mc_resume(rdev, &save);
}

static void r100_clock_startup(struct radeon_device *rdev)
{
	u32 tmp;

	if (radeon_dynclks != -1 && radeon_dynclks)
		radeon_legacy_set_clock_gating(rdev, 1);
	/* We need to force on some of the block */
	tmp = RREG32_PLL(R_00000D_SCLK_CNTL);
	tmp |= S_00000D_FORCE_CP(1) | S_00000D_FORCE_VIP(1);
	if ((rdev->family == CHIP_RV250) || (rdev->family == CHIP_RV280))
		tmp |= S_00000D_FORCE_DISP1(1) | S_00000D_FORCE_DISP2(1);
	WREG32_PLL(R_00000D_SCLK_CNTL, tmp);
}

static int r100_startup(struct radeon_device *rdev)
{
	int r;

	/* set common regs */
	r100_set_common_regs(rdev);
	/* program mc */
	r100_mc_program(rdev);
	/* Resume clock */
	r100_clock_startup(rdev);
	/* Initialize GART (initialize after TTM so we can allocate
	 * memory through TTM but finalize after TTM) */
	r100_enable_bm(rdev);
	if (rdev->flags & RADEON_IS_PCI) {
		r = r100_pci_gart_enable(rdev);
		if (r)
			return r;
	}

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

	r100_irq_set(rdev);
	rdev->config.r100.hdp_cntl = RREG32(RADEON_HOST_PATH_CNTL);
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

	return 0;
}

int r100_resume(struct radeon_device *rdev)
{
	int r;

	/* Make sur GART are not working */
	if (rdev->flags & RADEON_IS_PCI)
		r100_pci_gart_disable(rdev);
	/* Resume clock before doing reset */
	r100_clock_startup(rdev);
	/* Reset gpu before posting otherwise ATOM will enter infinite loop */
	if (radeon_asic_reset(rdev)) {
		dev_warn(rdev->dev, "GPU reset failed ! (0xE40=0x%08X, 0x7C0=0x%08X)\n",
			RREG32(R_000E40_RBBM_STATUS),
			RREG32(R_0007C0_CP_STAT));
	}
	/* post */
	radeon_combios_asic_init(rdev->ddev);
	/* Resume clock after posting */
	r100_clock_startup(rdev);
	/* Initialize surface registers */
	radeon_surface_init(rdev);

	rdev->accel_working = true;
	r = r100_startup(rdev);
	if (r) {
		rdev->accel_working = false;
	}
	return r;
}

int r100_suspend(struct radeon_device *rdev)
{
	radeon_pm_suspend(rdev);
	r100_cp_disable(rdev);
	radeon_wb_disable(rdev);
	r100_irq_disable(rdev);
	if (rdev->flags & RADEON_IS_PCI)
		r100_pci_gart_disable(rdev);
	return 0;
}

void r100_fini(struct radeon_device *rdev)
{
	radeon_pm_fini(rdev);
	r100_cp_fini(rdev);
	radeon_wb_fini(rdev);
	radeon_ib_pool_fini(rdev);
	radeon_gem_fini(rdev);
	if (rdev->flags & RADEON_IS_PCI)
		r100_pci_gart_fini(rdev);
	radeon_agp_fini(rdev);
	radeon_irq_kms_fini(rdev);
	radeon_fence_driver_fini(rdev);
	radeon_bo_fini(rdev);
	radeon_atombios_fini(rdev);
	kfree(rdev->bios);
	rdev->bios = NULL;
}

/*
 * Due to how kexec works, it can leave the hw fully initialised when it
 * boots the new kernel. However doing our init sequence with the CP and
 * WB stuff setup causes GPU hangs on the RN50 at least. So at startup
 * do some quick sanity checks and restore sane values to avoid this
 * problem.
 */
void r100_restore_sanity(struct radeon_device *rdev)
{
	u32 tmp;

	tmp = RREG32(RADEON_CP_CSQ_CNTL);
	if (tmp) {
		WREG32(RADEON_CP_CSQ_CNTL, 0);
	}
	tmp = RREG32(RADEON_CP_RB_CNTL);
	if (tmp) {
		WREG32(RADEON_CP_RB_CNTL, 0);
	}
	tmp = RREG32(RADEON_SCRATCH_UMSK);
	if (tmp) {
		WREG32(RADEON_SCRATCH_UMSK, 0);
	}
}

int r100_init(struct radeon_device *rdev)
{
	int r;

	/* Register debugfs file specific to this group of asics */
	r100_debugfs(rdev);
	/* Disable VGA */
	r100_vga_render_disable(rdev);
	/* Initialize scratch registers */
	radeon_scratch_init(rdev);
	/* Initialize surface registers */
	radeon_surface_init(rdev);
	/* sanity check some register to avoid hangs like after kexec */
	r100_restore_sanity(rdev);
	/* TODO: disable VGA need to use VGA request */
	/* BIOS*/
	if (!radeon_get_bios(rdev)) {
		if (ASIC_IS_AVIVO(rdev))
			return -EINVAL;
	}
	if (rdev->is_atom_bios) {
		dev_err(rdev->dev, "Expecting combios for RS400/RS480 GPU\n");
		return -EINVAL;
	} else {
		r = radeon_combios_init(rdev);
		if (r)
			return r;
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
	/* Set asic errata */
	r100_errata(rdev);
	/* Initialize clocks */
	radeon_get_clock_info(rdev->ddev);
	/* initialize AGP */
	if (rdev->flags & RADEON_IS_AGP) {
		r = radeon_agp_init(rdev);
		if (r) {
			radeon_agp_disable(rdev);
		}
	}
	/* initialize VRAM */
	r100_mc_init(rdev);
	/* Fence driver */
	r = radeon_fence_driver_init(rdev);
	if (r)
		return r;
	/* Memory manager */
	r = radeon_bo_init(rdev);
	if (r)
		return r;
	if (rdev->flags & RADEON_IS_PCI) {
		r = r100_pci_gart_init(rdev);
		if (r)
			return r;
	}
	r100_set_safe_registers(rdev);

	/* Initialize power management */
	radeon_pm_init(rdev);

	rdev->accel_working = true;
	r = r100_startup(rdev);
	if (r) {
		/* Somethings want wront with the accel init stop accel */
		dev_err(rdev->dev, "Disabling GPU acceleration\n");
		r100_cp_fini(rdev);
		radeon_wb_fini(rdev);
		radeon_ib_pool_fini(rdev);
		radeon_irq_kms_fini(rdev);
		if (rdev->flags & RADEON_IS_PCI)
			r100_pci_gart_fini(rdev);
		rdev->accel_working = false;
	}
	return 0;
}

uint32_t r100_mm_rreg_slow(struct radeon_device *rdev, uint32_t reg)
{
	unsigned long flags;
	uint32_t ret;

	spin_lock_irqsave(&rdev->mmio_idx_lock, flags);
	writel(reg, ((void __iomem *)rdev->rmmio) + RADEON_MM_INDEX);
	ret = readl(((void __iomem *)rdev->rmmio) + RADEON_MM_DATA);
	spin_unlock_irqrestore(&rdev->mmio_idx_lock, flags);
	return ret;
}

void r100_mm_wreg_slow(struct radeon_device *rdev, uint32_t reg, uint32_t v)
{
	unsigned long flags;

	spin_lock_irqsave(&rdev->mmio_idx_lock, flags);
	writel(reg, ((void __iomem *)rdev->rmmio) + RADEON_MM_INDEX);
	writel(v, ((void __iomem *)rdev->rmmio) + RADEON_MM_DATA);
	spin_unlock_irqrestore(&rdev->mmio_idx_lock, flags);
}

u32 r100_io_rreg(struct radeon_device *rdev, u32 reg)
{
	if (reg < rdev->rio_mem_size)
		return ioread32(rdev->rio_mem + reg);
	else {
		iowrite32(reg, rdev->rio_mem + RADEON_MM_INDEX);
		return ioread32(rdev->rio_mem + RADEON_MM_DATA);
	}
}

void r100_io_wreg(struct radeon_device *rdev, u32 reg, u32 v)
{
	if (reg < rdev->rio_mem_size)
		iowrite32(v, rdev->rio_mem + reg);
	else {
		iowrite32(reg, rdev->rio_mem + RADEON_MM_INDEX);
		iowrite32(v, rdev->rio_mem + RADEON_MM_DATA);
	}
}
