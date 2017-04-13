/*
 * (C) COPYRIGHT 2016 ARM Limited. All rights reserved.
 * Author: Liviu Dudau <Liviu.Dudau@arm.com>
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * ARM Mali DP500/DP550/DP650 driver (crtc operations)
 */

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <linux/clk.h>
#include <video/videomode.h>

#include "malidp_drv.h"
#include "malidp_hw.h"

static bool malidp_crtc_mode_fixup(struct drm_crtc *crtc,
				   const struct drm_display_mode *mode,
				   struct drm_display_mode *adjusted_mode)
{
	struct malidp_drm *malidp = crtc_to_malidp_device(crtc);
	struct malidp_hw_device *hwdev = malidp->dev;

	/*
	 * check that the hardware can drive the required clock rate,
	 * but skip the check if the clock is meant to be disabled (req_rate = 0)
	 */
	long rate, req_rate = mode->crtc_clock * 1000;

	if (req_rate) {
		rate = clk_round_rate(hwdev->mclk, req_rate);
		if (rate < req_rate) {
			DRM_DEBUG_DRIVER("mclk clock unable to reach %d kHz\n",
					 mode->crtc_clock);
			return false;
		}

		rate = clk_round_rate(hwdev->pxlclk, req_rate);
		if (rate != req_rate) {
			DRM_DEBUG_DRIVER("pxlclk doesn't support %ld Hz\n",
					 req_rate);
			return false;
		}
	}

	return true;
}

static void malidp_crtc_enable(struct drm_crtc *crtc)
{
	struct malidp_drm *malidp = crtc_to_malidp_device(crtc);
	struct malidp_hw_device *hwdev = malidp->dev;
	struct videomode vm;

	drm_display_mode_to_videomode(&crtc->state->adjusted_mode, &vm);

	clk_prepare_enable(hwdev->pxlclk);

	/* We rely on firmware to set mclk to a sensible level. */
	clk_set_rate(hwdev->pxlclk, crtc->state->adjusted_mode.crtc_clock * 1000);

	hwdev->modeset(hwdev, &vm);
	hwdev->leave_config_mode(hwdev);
	drm_crtc_vblank_on(crtc);
}

static void malidp_crtc_disable(struct drm_crtc *crtc)
{
	struct malidp_drm *malidp = crtc_to_malidp_device(crtc);
	struct malidp_hw_device *hwdev = malidp->dev;

	drm_crtc_vblank_off(crtc);
	hwdev->enter_config_mode(hwdev);
	clk_disable_unprepare(hwdev->pxlclk);
}

static int malidp_crtc_atomic_check(struct drm_crtc *crtc,
				    struct drm_crtc_state *state)
{
	struct malidp_drm *malidp = crtc_to_malidp_device(crtc);
	struct malidp_hw_device *hwdev = malidp->dev;
	struct drm_plane *plane;
	const struct drm_plane_state *pstate;
	u32 rot_mem_free, rot_mem_usable;
	int rotated_planes = 0;

	/*
	 * check if there is enough rotation memory available for planes
	 * that need 90° and 270° rotation. Each plane has set its required
	 * memory size in the ->plane_check() callback, here we only make
	 * sure that the sums are less that the total usable memory.
	 *
	 * The rotation memory allocation algorithm (for each plane):
	 *  a. If no more rotated planes exist, all remaining rotate
	 *     memory in the bank is available for use by the plane.
	 *  b. If other rotated planes exist, and plane's layer ID is
	 *     DE_VIDEO1, it can use all the memory from first bank if
	 *     secondary rotation memory bank is available, otherwise it can
	 *     use up to half the bank's memory.
	 *  c. If other rotated planes exist, and plane's layer ID is not
	 *     DE_VIDEO1, it can use half of the available memory
	 *
	 * Note: this algorithm assumes that the order in which the planes are
	 * checked always has DE_VIDEO1 plane first in the list if it is
	 * rotated. Because that is how we create the planes in the first
	 * place, under current DRM version things work, but if ever the order
	 * in which drm_atomic_crtc_state_for_each_plane() iterates over planes
	 * changes, we need to pre-sort the planes before validation.
	 */

	/* first count the number of rotated planes */
	drm_atomic_crtc_state_for_each_plane_state(plane, pstate, state) {
		if (pstate->rotation & MALIDP_ROTATED_MASK)
			rotated_planes++;
	}

	rot_mem_free = hwdev->rotation_memory[0];
	/*
	 * if we have more than 1 plane using rotation memory, use the second
	 * block of rotation memory as well
	 */
	if (rotated_planes > 1)
		rot_mem_free += hwdev->rotation_memory[1];

	/* now validate the rotation memory requirements */
	drm_atomic_crtc_state_for_each_plane_state(plane, pstate, state) {
		struct malidp_plane *mp = to_malidp_plane(plane);
		struct malidp_plane_state *ms = to_malidp_plane_state(pstate);

		if (pstate->rotation & MALIDP_ROTATED_MASK) {
			/* process current plane */
			rotated_planes--;

			if (!rotated_planes) {
				/* no more rotated planes, we can use what's left */
				rot_mem_usable = rot_mem_free;
			} else {
				if ((mp->layer->id != DE_VIDEO1) ||
				    (hwdev->rotation_memory[1] == 0))
					rot_mem_usable = rot_mem_free / 2;
				else
					rot_mem_usable = hwdev->rotation_memory[0];
			}

			rot_mem_free -= rot_mem_usable;

			if (ms->rotmem_size > rot_mem_usable)
				return -EINVAL;
		}
	}

	return 0;
}

static const struct drm_crtc_helper_funcs malidp_crtc_helper_funcs = {
	.mode_fixup = malidp_crtc_mode_fixup,
	.enable = malidp_crtc_enable,
	.disable = malidp_crtc_disable,
	.atomic_check = malidp_crtc_atomic_check,
};

static const struct drm_crtc_funcs malidp_crtc_funcs = {
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
};

int malidp_crtc_init(struct drm_device *drm)
{
	struct malidp_drm *malidp = drm->dev_private;
	struct drm_plane *primary = NULL, *plane;
	int ret;

	ret = malidp_de_planes_init(drm);
	if (ret < 0) {
		DRM_ERROR("Failed to initialise planes\n");
		return ret;
	}

	drm_for_each_plane(plane, drm) {
		if (plane->type == DRM_PLANE_TYPE_PRIMARY) {
			primary = plane;
			break;
		}
	}

	if (!primary) {
		DRM_ERROR("no primary plane found\n");
		ret = -EINVAL;
		goto crtc_cleanup_planes;
	}

	ret = drm_crtc_init_with_planes(drm, &malidp->crtc, primary, NULL,
					&malidp_crtc_funcs, NULL);

	if (!ret) {
		drm_crtc_helper_add(&malidp->crtc, &malidp_crtc_helper_funcs);
		return 0;
	}

crtc_cleanup_planes:
	malidp_de_planes_destroy(drm);

	return ret;
}
