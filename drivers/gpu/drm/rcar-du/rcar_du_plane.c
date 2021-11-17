// SPDX-License-Identifier: GPL-2.0+
/*
 * rcar_du_plane.c  --  R-Car Display Unit Planes
 *
 * Copyright (C) 2013-2015 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_plane_helper.h>

#include "rcar_du_drv.h"
#include "rcar_du_group.h"
#include "rcar_du_kms.h"
#include "rcar_du_plane.h"
#include "rcar_du_regs.h"

/* -----------------------------------------------------------------------------
 * Atomic hardware plane allocator
 *
 * The hardware plane allocator is solely based on the atomic plane states
 * without keeping any external state to avoid races between .atomic_check()
 * and .atomic_commit().
 *
 * The core idea is to avoid using a free planes bitmask that would need to be
 * shared between check and commit handlers with a collective knowledge based on
 * the allocated hardware plane(s) for each KMS plane. The allocator then loops
 * over all plane states to compute the free planes bitmask, allocates hardware
 * planes based on that bitmask, and stores the result back in the plane states.
 *
 * For this to work we need to access the current state of planes not touched by
 * the atomic update. To ensure that it won't be modified, we need to lock all
 * planes using drm_atomic_get_plane_state(). This effectively serializes atomic
 * updates from .atomic_check() up to completion (when swapping the states if
 * the check step has succeeded) or rollback (when freeing the states if the
 * check step has failed).
 *
 * Allocation is performed in the .atomic_check() handler and applied
 * automatically when the core swaps the old and new states.
 */

static bool rcar_du_plane_needs_realloc(
				const struct rcar_du_plane_state *old_state,
				const struct rcar_du_plane_state *new_state)
{
	/*
	 * Lowering the number of planes doesn't strictly require reallocation
	 * as the extra hardware plane will be freed when committing, but doing
	 * so could lead to more fragmentation.
	 */
	if (!old_state->format ||
	    old_state->format->planes != new_state->format->planes)
		return true;

	/* Reallocate hardware planes if the source has changed. */
	if (old_state->source != new_state->source)
		return true;

	return false;
}

static unsigned int rcar_du_plane_hwmask(struct rcar_du_plane_state *state)
{
	unsigned int mask;

	if (state->hwindex == -1)
		return 0;

	mask = 1 << state->hwindex;
	if (state->format->planes == 2)
		mask |= 1 << ((state->hwindex + 1) % 8);

	return mask;
}

/*
 * The R8A7790 DU can source frames directly from the VSP1 devices VSPD0 and
 * VSPD1. VSPD0 feeds DU0/1 plane 0, and VSPD1 feeds either DU2 plane 0 or
 * DU0/1 plane 1.
 *
 * Allocate the correct fixed plane when sourcing frames from VSPD0 or VSPD1,
 * and allocate planes in reverse index order otherwise to ensure maximum
 * availability of planes 0 and 1.
 *
 * The caller is responsible for ensuring that the requested source is
 * compatible with the DU revision.
 */
static int rcar_du_plane_hwalloc(struct rcar_du_plane *plane,
				 struct rcar_du_plane_state *state,
				 unsigned int free)
{
	unsigned int num_planes = state->format->planes;
	int fixed = -1;
	int i;

	if (state->source == RCAR_DU_PLANE_VSPD0) {
		/* VSPD0 feeds plane 0 on DU0/1. */
		if (plane->group->index != 0)
			return -EINVAL;

		fixed = 0;
	} else if (state->source == RCAR_DU_PLANE_VSPD1) {
		/* VSPD1 feeds plane 1 on DU0/1 or plane 0 on DU2. */
		fixed = plane->group->index == 0 ? 1 : 0;
	}

	if (fixed >= 0)
		return free & (1 << fixed) ? fixed : -EBUSY;

	for (i = RCAR_DU_NUM_HW_PLANES - 1; i >= 0; --i) {
		if (!(free & (1 << i)))
			continue;

		if (num_planes == 1 || free & (1 << ((i + 1) % 8)))
			break;
	}

	return i < 0 ? -EBUSY : i;
}

int rcar_du_atomic_check_planes(struct drm_device *dev,
				struct drm_atomic_state *state)
{
	struct rcar_du_device *rcdu = to_rcar_du_device(dev);
	unsigned int group_freed_planes[RCAR_DU_MAX_GROUPS] = { 0, };
	unsigned int group_free_planes[RCAR_DU_MAX_GROUPS] = { 0, };
	bool needs_realloc = false;
	unsigned int groups = 0;
	unsigned int i;
	struct drm_plane *drm_plane;
	struct drm_plane_state *old_drm_plane_state;
	struct drm_plane_state *new_drm_plane_state;

	/* Check if hardware planes need to be reallocated. */
	for_each_oldnew_plane_in_state(state, drm_plane, old_drm_plane_state,
				       new_drm_plane_state, i) {
		struct rcar_du_plane_state *old_plane_state;
		struct rcar_du_plane_state *new_plane_state;
		struct rcar_du_plane *plane;
		unsigned int index;

		plane = to_rcar_plane(drm_plane);
		old_plane_state = to_rcar_plane_state(old_drm_plane_state);
		new_plane_state = to_rcar_plane_state(new_drm_plane_state);

		dev_dbg(rcdu->dev, "%s: checking plane (%u,%tu)\n", __func__,
			plane->group->index, plane - plane->group->planes);

		/*
		 * If the plane is being disabled we don't need to go through
		 * the full reallocation procedure. Just mark the hardware
		 * plane(s) as freed.
		 */
		if (!new_plane_state->format) {
			dev_dbg(rcdu->dev, "%s: plane is being disabled\n",
				__func__);
			index = plane - plane->group->planes;
			group_freed_planes[plane->group->index] |= 1 << index;
			new_plane_state->hwindex = -1;
			continue;
		}

		/*
		 * If the plane needs to be reallocated mark it as such, and
		 * mark the hardware plane(s) as free.
		 */
		if (rcar_du_plane_needs_realloc(old_plane_state, new_plane_state)) {
			dev_dbg(rcdu->dev, "%s: plane needs reallocation\n",
				__func__);
			groups |= 1 << plane->group->index;
			needs_realloc = true;

			index = plane - plane->group->planes;
			group_freed_planes[plane->group->index] |= 1 << index;
			new_plane_state->hwindex = -1;
		}
	}

	if (!needs_realloc)
		return 0;

	/*
	 * Grab all plane states for the groups that need reallocation to ensure
	 * locking and avoid racy updates. This serializes the update operation,
	 * but there's not much we can do about it as that's the hardware
	 * design.
	 *
	 * Compute the used planes mask for each group at the same time to avoid
	 * looping over the planes separately later.
	 */
	while (groups) {
		unsigned int index = ffs(groups) - 1;
		struct rcar_du_group *group = &rcdu->groups[index];
		unsigned int used_planes = 0;

		dev_dbg(rcdu->dev, "%s: finding free planes for group %u\n",
			__func__, index);

		for (i = 0; i < group->num_planes; ++i) {
			struct rcar_du_plane *plane = &group->planes[i];
			struct rcar_du_plane_state *new_plane_state;
			struct drm_plane_state *s;

			s = drm_atomic_get_plane_state(state, &plane->plane);
			if (IS_ERR(s))
				return PTR_ERR(s);

			/*
			 * If the plane has been freed in the above loop its
			 * hardware planes must not be added to the used planes
			 * bitmask. However, the current state doesn't reflect
			 * the free state yet, as we've modified the new state
			 * above. Use the local freed planes list to check for
			 * that condition instead.
			 */
			if (group_freed_planes[index] & (1 << i)) {
				dev_dbg(rcdu->dev,
					"%s: plane (%u,%tu) has been freed, skipping\n",
					__func__, plane->group->index,
					plane - plane->group->planes);
				continue;
			}

			new_plane_state = to_rcar_plane_state(s);
			used_planes |= rcar_du_plane_hwmask(new_plane_state);

			dev_dbg(rcdu->dev,
				"%s: plane (%u,%tu) uses %u hwplanes (index %d)\n",
				__func__, plane->group->index,
				plane - plane->group->planes,
				new_plane_state->format ?
				new_plane_state->format->planes : 0,
				new_plane_state->hwindex);
		}

		group_free_planes[index] = 0xff & ~used_planes;
		groups &= ~(1 << index);

		dev_dbg(rcdu->dev, "%s: group %u free planes mask 0x%02x\n",
			__func__, index, group_free_planes[index]);
	}

	/* Reallocate hardware planes for each plane that needs it. */
	for_each_oldnew_plane_in_state(state, drm_plane, old_drm_plane_state,
				       new_drm_plane_state, i) {
		struct rcar_du_plane_state *old_plane_state;
		struct rcar_du_plane_state *new_plane_state;
		struct rcar_du_plane *plane;
		unsigned int crtc_planes;
		unsigned int free;
		int idx;

		plane = to_rcar_plane(drm_plane);
		old_plane_state = to_rcar_plane_state(old_drm_plane_state);
		new_plane_state = to_rcar_plane_state(new_drm_plane_state);

		dev_dbg(rcdu->dev, "%s: allocating plane (%u,%tu)\n", __func__,
			plane->group->index, plane - plane->group->planes);

		/*
		 * Skip planes that are being disabled or don't need to be
		 * reallocated.
		 */
		if (!new_plane_state->format ||
		    !rcar_du_plane_needs_realloc(old_plane_state, new_plane_state))
			continue;

		/*
		 * Try to allocate the plane from the free planes currently
		 * associated with the target CRTC to avoid restarting the CRTC
		 * group and thus minimize flicker. If it fails fall back to
		 * allocating from all free planes.
		 */
		crtc_planes = to_rcar_crtc(new_plane_state->state.crtc)->index % 2
			    ? plane->group->dptsr_planes
			    : ~plane->group->dptsr_planes;
		free = group_free_planes[plane->group->index];

		idx = rcar_du_plane_hwalloc(plane, new_plane_state,
					    free & crtc_planes);
		if (idx < 0)
			idx = rcar_du_plane_hwalloc(plane, new_plane_state,
						    free);
		if (idx < 0) {
			dev_dbg(rcdu->dev, "%s: no available hardware plane\n",
				__func__);
			return idx;
		}

		dev_dbg(rcdu->dev, "%s: allocated %u hwplanes (index %u)\n",
			__func__, new_plane_state->format->planes, idx);

		new_plane_state->hwindex = idx;

		group_free_planes[plane->group->index] &=
			~rcar_du_plane_hwmask(new_plane_state);

		dev_dbg(rcdu->dev, "%s: group %u free planes mask 0x%02x\n",
			__func__, plane->group->index,
			group_free_planes[plane->group->index]);
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * Plane Setup
 */

#define RCAR_DU_COLORKEY_NONE		(0 << 24)
#define RCAR_DU_COLORKEY_SOURCE		(1 << 24)
#define RCAR_DU_COLORKEY_MASK		(1 << 24)

static void rcar_du_plane_write(struct rcar_du_group *rgrp,
				unsigned int index, u32 reg, u32 data)
{
	rcar_du_write(rgrp->dev, rgrp->mmio_offset + index * PLANE_OFF + reg,
		      data);
}

static void rcar_du_plane_setup_scanout(struct rcar_du_group *rgrp,
					const struct rcar_du_plane_state *state)
{
	unsigned int src_x = state->state.src.x1 >> 16;
	unsigned int src_y = state->state.src.y1 >> 16;
	unsigned int index = state->hwindex;
	unsigned int pitch;
	bool interlaced;
	u32 dma[2];

	interlaced = state->state.crtc->state->adjusted_mode.flags
		   & DRM_MODE_FLAG_INTERLACE;

	if (state->source == RCAR_DU_PLANE_MEMORY) {
		struct drm_framebuffer *fb = state->state.fb;
		struct drm_gem_cma_object *gem;
		unsigned int i;

		if (state->format->planes == 2)
			pitch = fb->pitches[0];
		else
			pitch = fb->pitches[0] * 8 / state->format->bpp;

		for (i = 0; i < state->format->planes; ++i) {
			gem = drm_fb_cma_get_gem_obj(fb, i);
			dma[i] = gem->paddr + fb->offsets[i];
		}
	} else {
		pitch = drm_rect_width(&state->state.src) >> 16;
		dma[0] = 0;
		dma[1] = 0;
	}

	/*
	 * Memory pitch (expressed in pixels). Must be doubled for interlaced
	 * operation with 32bpp formats.
	 */
	rcar_du_plane_write(rgrp, index, PnMWR,
			    (interlaced && state->format->bpp == 32) ?
			    pitch * 2 : pitch);

	/*
	 * The Y position is expressed in raster line units and must be doubled
	 * for 32bpp formats, according to the R8A7790 datasheet. No mention of
	 * doubling the Y position is found in the R8A7779 datasheet, but the
	 * rule seems to apply there as well.
	 *
	 * Despite not being documented, doubling seem not to be needed when
	 * operating in interlaced mode.
	 *
	 * Similarly, for the second plane, NV12 and NV21 formats seem to
	 * require a halved Y position value, in both progressive and interlaced
	 * modes.
	 */
	rcar_du_plane_write(rgrp, index, PnSPXR, src_x);
	rcar_du_plane_write(rgrp, index, PnSPYR, src_y *
			    (!interlaced && state->format->bpp == 32 ? 2 : 1));

	rcar_du_plane_write(rgrp, index, PnDSA0R, dma[0]);

	if (state->format->planes == 2) {
		index = (index + 1) % 8;

		rcar_du_plane_write(rgrp, index, PnMWR, pitch);

		rcar_du_plane_write(rgrp, index, PnSPXR, src_x);
		rcar_du_plane_write(rgrp, index, PnSPYR, src_y *
				    (state->format->bpp == 16 ? 2 : 1) / 2);

		rcar_du_plane_write(rgrp, index, PnDSA0R, dma[1]);
	}
}

static void rcar_du_plane_setup_mode(struct rcar_du_group *rgrp,
				     unsigned int index,
				     const struct rcar_du_plane_state *state)
{
	u32 colorkey;
	u32 pnmr;

	/*
	 * The PnALPHAR register controls alpha-blending in 16bpp formats
	 * (ARGB1555 and XRGB1555).
	 *
	 * For ARGB, set the alpha value to 0, and enable alpha-blending when
	 * the A bit is 0. This maps A=0 to alpha=0 and A=1 to alpha=255.
	 *
	 * For XRGB, set the alpha value to the plane-wide alpha value and
	 * enable alpha-blending regardless of the X bit value.
	 */
	if (state->format->fourcc != DRM_FORMAT_XRGB1555)
		rcar_du_plane_write(rgrp, index, PnALPHAR, PnALPHAR_ABIT_0);
	else
		rcar_du_plane_write(rgrp, index, PnALPHAR,
				    PnALPHAR_ABIT_X | state->state.alpha >> 8);

	pnmr = PnMR_BM_MD | state->format->pnmr;

	/*
	 * Disable color keying when requested. YUV formats have the
	 * PnMR_SPIM_TP_OFF bit set in their pnmr field, disabling color keying
	 * automatically.
	 */
	if ((state->colorkey & RCAR_DU_COLORKEY_MASK) == RCAR_DU_COLORKEY_NONE)
		pnmr |= PnMR_SPIM_TP_OFF;

	/* For packed YUV formats we need to select the U/V order. */
	if (state->format->fourcc == DRM_FORMAT_YUYV)
		pnmr |= PnMR_YCDF_YUYV;

	rcar_du_plane_write(rgrp, index, PnMR, pnmr);

	switch (state->format->fourcc) {
	case DRM_FORMAT_RGB565:
		colorkey = ((state->colorkey & 0xf80000) >> 8)
			 | ((state->colorkey & 0x00fc00) >> 5)
			 | ((state->colorkey & 0x0000f8) >> 3);
		rcar_du_plane_write(rgrp, index, PnTC2R, colorkey);
		break;

	case DRM_FORMAT_ARGB1555:
	case DRM_FORMAT_XRGB1555:
		colorkey = ((state->colorkey & 0xf80000) >> 9)
			 | ((state->colorkey & 0x00f800) >> 6)
			 | ((state->colorkey & 0x0000f8) >> 3);
		rcar_du_plane_write(rgrp, index, PnTC2R, colorkey);
		break;

	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		rcar_du_plane_write(rgrp, index, PnTC3R,
				    PnTC3R_CODE | (state->colorkey & 0xffffff));
		break;
	}
}

static void rcar_du_plane_setup_format_gen2(struct rcar_du_group *rgrp,
					    unsigned int index,
					    const struct rcar_du_plane_state *state)
{
	u32 ddcr2 = PnDDCR2_CODE;
	u32 ddcr4;

	/*
	 * Data format
	 *
	 * The data format is selected by the DDDF field in PnMR and the EDF
	 * field in DDCR4.
	 */

	rcar_du_plane_setup_mode(rgrp, index, state);

	if (state->format->planes == 2) {
		if (state->hwindex != index) {
			if (state->format->fourcc == DRM_FORMAT_NV12 ||
			    state->format->fourcc == DRM_FORMAT_NV21)
				ddcr2 |= PnDDCR2_Y420;

			if (state->format->fourcc == DRM_FORMAT_NV21)
				ddcr2 |= PnDDCR2_NV21;

			ddcr2 |= PnDDCR2_DIVU;
		} else {
			ddcr2 |= PnDDCR2_DIVY;
		}
	}

	rcar_du_plane_write(rgrp, index, PnDDCR2, ddcr2);

	ddcr4 = state->format->edf | PnDDCR4_CODE;
	if (state->source != RCAR_DU_PLANE_MEMORY)
		ddcr4 |= PnDDCR4_VSPS;

	rcar_du_plane_write(rgrp, index, PnDDCR4, ddcr4);
}

static void rcar_du_plane_setup_format_gen3(struct rcar_du_group *rgrp,
					    unsigned int index,
					    const struct rcar_du_plane_state *state)
{
	rcar_du_plane_write(rgrp, index, PnMR,
			    PnMR_SPIM_TP_OFF | state->format->pnmr);

	rcar_du_plane_write(rgrp, index, PnDDCR4,
			    state->format->edf | PnDDCR4_CODE);
}

static void rcar_du_plane_setup_format(struct rcar_du_group *rgrp,
				       unsigned int index,
				       const struct rcar_du_plane_state *state)
{
	struct rcar_du_device *rcdu = rgrp->dev;
	const struct drm_rect *dst = &state->state.dst;

	if (rcdu->info->gen < 3)
		rcar_du_plane_setup_format_gen2(rgrp, index, state);
	else
		rcar_du_plane_setup_format_gen3(rgrp, index, state);

	/* Destination position and size */
	rcar_du_plane_write(rgrp, index, PnDSXR, drm_rect_width(dst));
	rcar_du_plane_write(rgrp, index, PnDSYR, drm_rect_height(dst));
	rcar_du_plane_write(rgrp, index, PnDPXR, dst->x1);
	rcar_du_plane_write(rgrp, index, PnDPYR, dst->y1);

	if (rcdu->info->gen < 3) {
		/* Wrap-around and blinking, disabled */
		rcar_du_plane_write(rgrp, index, PnWASPR, 0);
		rcar_du_plane_write(rgrp, index, PnWAMWR, 4095);
		rcar_du_plane_write(rgrp, index, PnBTR, 0);
		rcar_du_plane_write(rgrp, index, PnMLR, 0);
	}
}

void __rcar_du_plane_setup(struct rcar_du_group *rgrp,
			   const struct rcar_du_plane_state *state)
{
	struct rcar_du_device *rcdu = rgrp->dev;

	rcar_du_plane_setup_format(rgrp, state->hwindex, state);
	if (state->format->planes == 2)
		rcar_du_plane_setup_format(rgrp, (state->hwindex + 1) % 8,
					   state);

	if (rcdu->info->gen < 3)
		rcar_du_plane_setup_scanout(rgrp, state);

	if (state->source == RCAR_DU_PLANE_VSPD1) {
		unsigned int vspd1_sink = rgrp->index ? 2 : 0;

		if (rcdu->vspd1_sink != vspd1_sink) {
			rcdu->vspd1_sink = vspd1_sink;
			rcar_du_set_dpad0_vsp1_routing(rcdu);
		}
	}
}

int __rcar_du_plane_atomic_check(struct drm_plane *plane,
				 struct drm_plane_state *state,
				 const struct rcar_du_format_info **format)
{
	struct drm_device *dev = plane->dev;
	struct drm_crtc_state *crtc_state;
	int ret;

	if (!state->crtc) {
		/*
		 * The visible field is not reset by the DRM core but only
		 * updated by drm_plane_helper_check_state(), set it manually.
		 */
		state->visible = false;
		*format = NULL;
		return 0;
	}

	crtc_state = drm_atomic_get_crtc_state(state->state, state->crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	ret = drm_atomic_helper_check_plane_state(state, crtc_state,
						  DRM_PLANE_HELPER_NO_SCALING,
						  DRM_PLANE_HELPER_NO_SCALING,
						  true, true);
	if (ret < 0)
		return ret;

	if (!state->visible) {
		*format = NULL;
		return 0;
	}

	*format = rcar_du_format_info(state->fb->format->format);
	if (*format == NULL) {
		dev_dbg(dev->dev, "%s: unsupported format %08x\n", __func__,
			state->fb->format->format);
		return -EINVAL;
	}

	return 0;
}

static int rcar_du_plane_atomic_check(struct drm_plane *plane,
				      struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct rcar_du_plane_state *rstate = to_rcar_plane_state(new_plane_state);

	return __rcar_du_plane_atomic_check(plane, new_plane_state,
					    &rstate->format);
}

static void rcar_du_plane_atomic_update(struct drm_plane *plane,
					struct drm_atomic_state *state)
{
	struct drm_plane_state *old_state = drm_atomic_get_old_plane_state(state, plane);
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state, plane);
	struct rcar_du_plane *rplane = to_rcar_plane(plane);
	struct rcar_du_plane_state *old_rstate;
	struct rcar_du_plane_state *new_rstate;

	if (!new_state->visible)
		return;

	rcar_du_plane_setup(rplane);

	/*
	 * Check whether the source has changed from memory to live source or
	 * from live source to memory. The source has been configured by the
	 * VSPS bit in the PnDDCR4 register. Although the datasheet states that
	 * the bit is updated during vertical blanking, it seems that updates
	 * only occur when the DU group is held in reset through the DSYSR.DRES
	 * bit. We thus need to restart the group if the source changes.
	 */
	old_rstate = to_rcar_plane_state(old_state);
	new_rstate = to_rcar_plane_state(new_state);

	if ((old_rstate->source == RCAR_DU_PLANE_MEMORY) !=
	    (new_rstate->source == RCAR_DU_PLANE_MEMORY))
		rplane->group->need_restart = true;
}

static const struct drm_plane_helper_funcs rcar_du_plane_helper_funcs = {
	.atomic_check = rcar_du_plane_atomic_check,
	.atomic_update = rcar_du_plane_atomic_update,
};

static struct drm_plane_state *
rcar_du_plane_atomic_duplicate_state(struct drm_plane *plane)
{
	struct rcar_du_plane_state *state;
	struct rcar_du_plane_state *copy;

	if (WARN_ON(!plane->state))
		return NULL;

	state = to_rcar_plane_state(plane->state);
	copy = kmemdup(state, sizeof(*state), GFP_KERNEL);
	if (copy == NULL)
		return NULL;

	__drm_atomic_helper_plane_duplicate_state(plane, &copy->state);

	return &copy->state;
}

static void rcar_du_plane_atomic_destroy_state(struct drm_plane *plane,
					       struct drm_plane_state *state)
{
	__drm_atomic_helper_plane_destroy_state(state);
	kfree(to_rcar_plane_state(state));
}

static void rcar_du_plane_reset(struct drm_plane *plane)
{
	struct rcar_du_plane_state *state;

	if (plane->state) {
		rcar_du_plane_atomic_destroy_state(plane, plane->state);
		plane->state = NULL;
	}

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (state == NULL)
		return;

	__drm_atomic_helper_plane_reset(plane, &state->state);

	state->hwindex = -1;
	state->source = RCAR_DU_PLANE_MEMORY;
	state->colorkey = RCAR_DU_COLORKEY_NONE;
	state->state.zpos = plane->type == DRM_PLANE_TYPE_PRIMARY ? 0 : 1;
}

static int rcar_du_plane_atomic_set_property(struct drm_plane *plane,
					     struct drm_plane_state *state,
					     struct drm_property *property,
					     uint64_t val)
{
	struct rcar_du_plane_state *rstate = to_rcar_plane_state(state);
	struct rcar_du_device *rcdu = to_rcar_plane(plane)->group->dev;

	if (property == rcdu->props.colorkey)
		rstate->colorkey = val;
	else
		return -EINVAL;

	return 0;
}

static int rcar_du_plane_atomic_get_property(struct drm_plane *plane,
	const struct drm_plane_state *state, struct drm_property *property,
	uint64_t *val)
{
	const struct rcar_du_plane_state *rstate =
		container_of(state, const struct rcar_du_plane_state, state);
	struct rcar_du_device *rcdu = to_rcar_plane(plane)->group->dev;

	if (property == rcdu->props.colorkey)
		*val = rstate->colorkey;
	else
		return -EINVAL;

	return 0;
}

static const struct drm_plane_funcs rcar_du_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.reset = rcar_du_plane_reset,
	.destroy = drm_plane_cleanup,
	.atomic_duplicate_state = rcar_du_plane_atomic_duplicate_state,
	.atomic_destroy_state = rcar_du_plane_atomic_destroy_state,
	.atomic_set_property = rcar_du_plane_atomic_set_property,
	.atomic_get_property = rcar_du_plane_atomic_get_property,
};

static const uint32_t formats[] = {
	DRM_FORMAT_RGB565,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV21,
	DRM_FORMAT_NV16,
};

int rcar_du_planes_init(struct rcar_du_group *rgrp)
{
	struct rcar_du_device *rcdu = rgrp->dev;
	unsigned int crtcs;
	unsigned int i;
	int ret;

	 /*
	  * Create one primary plane per CRTC in this group and seven overlay
	  * planes.
	  */
	rgrp->num_planes = rgrp->num_crtcs + 7;

	crtcs = ((1 << rcdu->num_crtcs) - 1) & (3 << (2 * rgrp->index));

	for (i = 0; i < rgrp->num_planes; ++i) {
		enum drm_plane_type type = i < rgrp->num_crtcs
					 ? DRM_PLANE_TYPE_PRIMARY
					 : DRM_PLANE_TYPE_OVERLAY;
		struct rcar_du_plane *plane = &rgrp->planes[i];

		plane->group = rgrp;

		ret = drm_universal_plane_init(&rcdu->ddev, &plane->plane,
					       crtcs, &rcar_du_plane_funcs,
					       formats, ARRAY_SIZE(formats),
					       NULL, type, NULL);
		if (ret < 0)
			return ret;

		drm_plane_helper_add(&plane->plane,
				     &rcar_du_plane_helper_funcs);

		drm_plane_create_alpha_property(&plane->plane);

		if (type == DRM_PLANE_TYPE_PRIMARY) {
			drm_plane_create_zpos_immutable_property(&plane->plane,
								 0);
		} else {
			drm_object_attach_property(&plane->plane.base,
						   rcdu->props.colorkey,
						   RCAR_DU_COLORKEY_NONE);
			drm_plane_create_zpos_property(&plane->plane, 1, 1, 7);
		}
	}

	return 0;
}
