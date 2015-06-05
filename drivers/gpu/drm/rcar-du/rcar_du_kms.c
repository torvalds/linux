/*
 * rcar_du_kms.c  --  R-Car Display Unit Mode Setting
 *
 * Copyright (C) 2013-2014 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>

#include <linux/of_graph.h>
#include <linux/wait.h>

#include "rcar_du_crtc.h"
#include "rcar_du_drv.h"
#include "rcar_du_encoder.h"
#include "rcar_du_kms.h"
#include "rcar_du_lvdsenc.h"
#include "rcar_du_regs.h"

/* -----------------------------------------------------------------------------
 * Format helpers
 */

static const struct rcar_du_format_info rcar_du_format_infos[] = {
	{
		.fourcc = DRM_FORMAT_RGB565,
		.bpp = 16,
		.planes = 1,
		.pnmr = PnMR_SPIM_TP | PnMR_DDDF_16BPP,
		.edf = PnDDCR4_EDF_NONE,
	}, {
		.fourcc = DRM_FORMAT_ARGB1555,
		.bpp = 16,
		.planes = 1,
		.pnmr = PnMR_SPIM_ALP | PnMR_DDDF_ARGB,
		.edf = PnDDCR4_EDF_NONE,
	}, {
		.fourcc = DRM_FORMAT_XRGB1555,
		.bpp = 16,
		.planes = 1,
		.pnmr = PnMR_SPIM_ALP | PnMR_DDDF_ARGB,
		.edf = PnDDCR4_EDF_NONE,
	}, {
		.fourcc = DRM_FORMAT_XRGB8888,
		.bpp = 32,
		.planes = 1,
		.pnmr = PnMR_SPIM_TP | PnMR_DDDF_16BPP,
		.edf = PnDDCR4_EDF_RGB888,
	}, {
		.fourcc = DRM_FORMAT_ARGB8888,
		.bpp = 32,
		.planes = 1,
		.pnmr = PnMR_SPIM_ALP | PnMR_DDDF_16BPP,
		.edf = PnDDCR4_EDF_ARGB8888,
	}, {
		.fourcc = DRM_FORMAT_UYVY,
		.bpp = 16,
		.planes = 1,
		.pnmr = PnMR_SPIM_TP_OFF | PnMR_DDDF_YC,
		.edf = PnDDCR4_EDF_NONE,
	}, {
		.fourcc = DRM_FORMAT_YUYV,
		.bpp = 16,
		.planes = 1,
		.pnmr = PnMR_SPIM_TP_OFF | PnMR_DDDF_YC,
		.edf = PnDDCR4_EDF_NONE,
	}, {
		.fourcc = DRM_FORMAT_NV12,
		.bpp = 12,
		.planes = 2,
		.pnmr = PnMR_SPIM_TP_OFF | PnMR_DDDF_YC,
		.edf = PnDDCR4_EDF_NONE,
	}, {
		.fourcc = DRM_FORMAT_NV21,
		.bpp = 12,
		.planes = 2,
		.pnmr = PnMR_SPIM_TP_OFF | PnMR_DDDF_YC,
		.edf = PnDDCR4_EDF_NONE,
	}, {
		/* In YUV 4:2:2, only NV16 is supported (NV61 isn't) */
		.fourcc = DRM_FORMAT_NV16,
		.bpp = 16,
		.planes = 2,
		.pnmr = PnMR_SPIM_TP_OFF | PnMR_DDDF_YC,
		.edf = PnDDCR4_EDF_NONE,
	},
};

const struct rcar_du_format_info *rcar_du_format_info(u32 fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rcar_du_format_infos); ++i) {
		if (rcar_du_format_infos[i].fourcc == fourcc)
			return &rcar_du_format_infos[i];
	}

	return NULL;
}

/* -----------------------------------------------------------------------------
 * Frame buffer
 */

int rcar_du_dumb_create(struct drm_file *file, struct drm_device *dev,
			struct drm_mode_create_dumb *args)
{
	struct rcar_du_device *rcdu = dev->dev_private;
	unsigned int min_pitch = DIV_ROUND_UP(args->width * args->bpp, 8);
	unsigned int align;

	/* The R8A7779 DU requires a 16 pixels pitch alignment as documented,
	 * but the R8A7790 DU seems to require a 128 bytes pitch alignment.
	 */
	if (rcar_du_needs(rcdu, RCAR_DU_QUIRK_ALIGN_128B))
		align = 128;
	else
		align = 16 * args->bpp / 8;

	args->pitch = roundup(min_pitch, align);

	return drm_gem_cma_dumb_create_internal(file, dev, args);
}

static struct drm_framebuffer *
rcar_du_fb_create(struct drm_device *dev, struct drm_file *file_priv,
		  struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct rcar_du_device *rcdu = dev->dev_private;
	const struct rcar_du_format_info *format;
	unsigned int max_pitch;
	unsigned int align;
	unsigned int bpp;

	format = rcar_du_format_info(mode_cmd->pixel_format);
	if (format == NULL) {
		dev_dbg(dev->dev, "unsupported pixel format %08x\n",
			mode_cmd->pixel_format);
		return ERR_PTR(-EINVAL);
	}

	/*
	 * The pitch and alignment constraints are expressed in pixels on the
	 * hardware side and in bytes in the DRM API.
	 */
	bpp = format->planes == 2 ? 1 : format->bpp / 8;
	max_pitch =  4096 * bpp;

	if (rcar_du_needs(rcdu, RCAR_DU_QUIRK_ALIGN_128B))
		align = 128;
	else
		align = 16 * bpp;

	if (mode_cmd->pitches[0] & (align - 1) ||
	    mode_cmd->pitches[0] >= max_pitch) {
		dev_dbg(dev->dev, "invalid pitch value %u\n",
			mode_cmd->pitches[0]);
		return ERR_PTR(-EINVAL);
	}

	if (format->planes == 2) {
		if (mode_cmd->pitches[1] != mode_cmd->pitches[0]) {
			dev_dbg(dev->dev,
				"luma and chroma pitches do not match\n");
			return ERR_PTR(-EINVAL);
		}
	}

	return drm_fb_cma_create(dev, file_priv, mode_cmd);
}

static void rcar_du_output_poll_changed(struct drm_device *dev)
{
	struct rcar_du_device *rcdu = dev->dev_private;

	drm_fbdev_cma_hotplug_event(rcdu->fbdev);
}

/* -----------------------------------------------------------------------------
 * Atomic Check and Update
 */

/*
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

static bool rcar_du_plane_needs_realloc(struct rcar_du_plane *plane,
					struct rcar_du_plane_state *state)
{
	const struct rcar_du_format_info *cur_format;

	cur_format = to_rcar_du_plane_state(plane->plane.state)->format;

	/* Lowering the number of planes doesn't strictly require reallocation
	 * as the extra hardware plane will be freed when committing, but doing
	 * so could lead to more fragmentation.
	 */
	return !cur_format || cur_format->planes != state->format->planes;
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

static int rcar_du_plane_hwalloc(unsigned int num_planes, unsigned int free)
{
	unsigned int i;

	for (i = 0; i < RCAR_DU_NUM_HW_PLANES; ++i) {
		if (!(free & (1 << i)))
			continue;

		if (num_planes == 1 || free & (1 << ((i + 1) % 8)))
			break;
	}

	return i == RCAR_DU_NUM_HW_PLANES ? -EBUSY : i;
}

static int rcar_du_atomic_check(struct drm_device *dev,
				struct drm_atomic_state *state)
{
	struct rcar_du_device *rcdu = dev->dev_private;
	unsigned int group_freed_planes[RCAR_DU_MAX_GROUPS] = { 0, };
	unsigned int group_free_planes[RCAR_DU_MAX_GROUPS] = { 0, };
	bool needs_realloc = false;
	unsigned int groups = 0;
	unsigned int i;
	int ret;

	ret = drm_atomic_helper_check(dev, state);
	if (ret < 0)
		return ret;

	/* Check if hardware planes need to be reallocated. */
	for (i = 0; i < dev->mode_config.num_total_plane; ++i) {
		struct rcar_du_plane_state *plane_state;
		struct rcar_du_plane *plane;
		unsigned int index;

		if (!state->planes[i])
			continue;

		plane = to_rcar_plane(state->planes[i]);
		plane_state = to_rcar_du_plane_state(state->plane_states[i]);

		/* If the plane is being disabled we don't need to go through
		 * the full reallocation procedure. Just mark the hardware
		 * plane(s) as freed.
		 */
		if (!plane_state->format) {
			index = plane - plane->group->planes.planes;
			group_freed_planes[plane->group->index] |= 1 << index;
			plane_state->hwindex = -1;
			continue;
		}

		/* If the plane needs to be reallocated mark it as such, and
		 * mark the hardware plane(s) as free.
		 */
		if (rcar_du_plane_needs_realloc(plane, plane_state)) {
			groups |= 1 << plane->group->index;
			needs_realloc = true;

			index = plane - plane->group->planes.planes;
			group_freed_planes[plane->group->index] |= 1 << index;
			plane_state->hwindex = -1;
		}
	}

	if (!needs_realloc)
		return 0;

	/* Grab all plane states for the groups that need reallocation to ensure
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

		for (i = 0; i < RCAR_DU_NUM_KMS_PLANES; ++i) {
			struct rcar_du_plane *plane = &group->planes.planes[i];
			struct rcar_du_plane_state *plane_state;
			struct drm_plane_state *s;

			s = drm_atomic_get_plane_state(state, &plane->plane);
			if (IS_ERR(s))
				return PTR_ERR(s);

			/* If the plane has been freed in the above loop its
			 * hardware planes must not be added to the used planes
			 * bitmask. However, the current state doesn't reflect
			 * the free state yet, as we've modified the new state
			 * above. Use the local freed planes list to check for
			 * that condition instead.
			 */
			if (group_freed_planes[index] & (1 << i))
				continue;

			plane_state = to_rcar_du_plane_state(plane->plane.state);
			used_planes |= rcar_du_plane_hwmask(plane_state);
		}

		group_free_planes[index] = 0xff & ~used_planes;
		groups &= ~(1 << index);
	}

	/* Reallocate hardware planes for each plane that needs it. */
	for (i = 0; i < dev->mode_config.num_total_plane; ++i) {
		struct rcar_du_plane_state *plane_state;
		struct rcar_du_plane *plane;
		int idx;

		if (!state->planes[i])
			continue;

		plane = to_rcar_plane(state->planes[i]);
		plane_state = to_rcar_du_plane_state(state->plane_states[i]);

		/* Skip planes that are being disabled or don't need to be
		 * reallocated.
		 */
		if (!plane_state->format ||
		    !rcar_du_plane_needs_realloc(plane, plane_state))
			continue;

		idx = rcar_du_plane_hwalloc(plane_state->format->planes,
					group_free_planes[plane->group->index]);
		if (idx < 0) {
			dev_dbg(rcdu->dev, "%s: no available hardware plane\n",
				__func__);
			return idx;
		}

		plane_state->hwindex = idx;

		group_free_planes[plane->group->index] &=
			~rcar_du_plane_hwmask(plane_state);
	}

	return 0;
}

struct rcar_du_commit {
	struct work_struct work;
	struct drm_device *dev;
	struct drm_atomic_state *state;
	u32 crtcs;
};

static void rcar_du_atomic_complete(struct rcar_du_commit *commit)
{
	struct drm_device *dev = commit->dev;
	struct rcar_du_device *rcdu = dev->dev_private;
	struct drm_atomic_state *old_state = commit->state;

	/* Apply the atomic update. */
	drm_atomic_helper_commit_modeset_disables(dev, old_state);
	drm_atomic_helper_commit_modeset_enables(dev, old_state);
	drm_atomic_helper_commit_planes(dev, old_state);

	drm_atomic_helper_wait_for_vblanks(dev, old_state);

	drm_atomic_helper_cleanup_planes(dev, old_state);

	drm_atomic_state_free(old_state);

	/* Complete the commit, wake up any waiter. */
	spin_lock(&rcdu->commit.wait.lock);
	rcdu->commit.pending &= ~commit->crtcs;
	wake_up_all_locked(&rcdu->commit.wait);
	spin_unlock(&rcdu->commit.wait.lock);

	kfree(commit);
}

static void rcar_du_atomic_work(struct work_struct *work)
{
	struct rcar_du_commit *commit =
		container_of(work, struct rcar_du_commit, work);

	rcar_du_atomic_complete(commit);
}

static int rcar_du_atomic_commit(struct drm_device *dev,
				 struct drm_atomic_state *state, bool async)
{
	struct rcar_du_device *rcdu = dev->dev_private;
	struct rcar_du_commit *commit;
	unsigned int i;
	int ret;

	ret = drm_atomic_helper_prepare_planes(dev, state);
	if (ret)
		return ret;

	/* Allocate the commit object. */
	commit = kzalloc(sizeof(*commit), GFP_KERNEL);
	if (commit == NULL)
		return -ENOMEM;

	INIT_WORK(&commit->work, rcar_du_atomic_work);
	commit->dev = dev;
	commit->state = state;

	/* Wait until all affected CRTCs have completed previous commits and
	 * mark them as pending.
	 */
	for (i = 0; i < dev->mode_config.num_crtc; ++i) {
		if (state->crtcs[i])
			commit->crtcs |= 1 << drm_crtc_index(state->crtcs[i]);
	}

	spin_lock(&rcdu->commit.wait.lock);
	ret = wait_event_interruptible_locked(rcdu->commit.wait,
			!(rcdu->commit.pending & commit->crtcs));
	if (ret == 0)
		rcdu->commit.pending |= commit->crtcs;
	spin_unlock(&rcdu->commit.wait.lock);

	if (ret) {
		kfree(commit);
		return ret;
	}

	/* Swap the state, this is the point of no return. */
	drm_atomic_helper_swap_state(dev, state);

	if (async)
		schedule_work(&commit->work);
	else
		rcar_du_atomic_complete(commit);

	return 0;
}

/* -----------------------------------------------------------------------------
 * Initialization
 */

static const struct drm_mode_config_funcs rcar_du_mode_config_funcs = {
	.fb_create = rcar_du_fb_create,
	.output_poll_changed = rcar_du_output_poll_changed,
	.atomic_check = rcar_du_atomic_check,
	.atomic_commit = rcar_du_atomic_commit,
};

static int rcar_du_encoders_init_one(struct rcar_du_device *rcdu,
				     enum rcar_du_output output,
				     struct of_endpoint *ep)
{
	static const struct {
		const char *compatible;
		enum rcar_du_encoder_type type;
	} encoders[] = {
		{ "adi,adv7123", RCAR_DU_ENCODER_VGA },
		{ "adi,adv7511w", RCAR_DU_ENCODER_HDMI },
		{ "thine,thc63lvdm83d", RCAR_DU_ENCODER_LVDS },
	};

	enum rcar_du_encoder_type enc_type = RCAR_DU_ENCODER_NONE;
	struct device_node *connector = NULL;
	struct device_node *encoder = NULL;
	struct device_node *ep_node = NULL;
	struct device_node *entity_ep_node;
	struct device_node *entity;
	int ret;

	/*
	 * Locate the connected entity and infer its type from the number of
	 * endpoints.
	 */
	entity = of_graph_get_remote_port_parent(ep->local_node);
	if (!entity) {
		dev_dbg(rcdu->dev, "unconnected endpoint %s, skipping\n",
			ep->local_node->full_name);
		return 0;
	}

	entity_ep_node = of_parse_phandle(ep->local_node, "remote-endpoint", 0);

	for_each_endpoint_of_node(entity, ep_node) {
		if (ep_node == entity_ep_node)
			continue;

		/*
		 * We've found one endpoint other than the input, this must
		 * be an encoder. Locate the connector.
		 */
		encoder = entity;
		connector = of_graph_get_remote_port_parent(ep_node);
		of_node_put(ep_node);

		if (!connector) {
			dev_warn(rcdu->dev,
				 "no connector for encoder %s, skipping\n",
				 encoder->full_name);
			of_node_put(entity_ep_node);
			of_node_put(encoder);
			return 0;
		}

		break;
	}

	of_node_put(entity_ep_node);

	if (encoder) {
		/*
		 * If an encoder has been found, get its type based on its
		 * compatible string.
		 */
		unsigned int i;

		for (i = 0; i < ARRAY_SIZE(encoders); ++i) {
			if (of_device_is_compatible(encoder,
						    encoders[i].compatible)) {
				enc_type = encoders[i].type;
				break;
			}
		}

		if (i == ARRAY_SIZE(encoders)) {
			dev_warn(rcdu->dev,
				 "unknown encoder type for %s, skipping\n",
				 encoder->full_name);
			of_node_put(encoder);
			of_node_put(connector);
			return 0;
		}
	} else {
		/*
		 * If no encoder has been found the entity must be the
		 * connector.
		 */
		connector = entity;
	}

	ret = rcar_du_encoder_init(rcdu, enc_type, output, encoder, connector);
	of_node_put(encoder);
	of_node_put(connector);

	return ret < 0 ? ret : 1;
}

static int rcar_du_encoders_init(struct rcar_du_device *rcdu)
{
	struct device_node *np = rcdu->dev->of_node;
	struct device_node *ep_node;
	unsigned int num_encoders = 0;

	/*
	 * Iterate over the endpoints and create one encoder for each output
	 * pipeline.
	 */
	for_each_endpoint_of_node(np, ep_node) {
		enum rcar_du_output output;
		struct of_endpoint ep;
		unsigned int i;
		int ret;

		ret = of_graph_parse_endpoint(ep_node, &ep);
		if (ret < 0) {
			of_node_put(ep_node);
			return ret;
		}

		/* Find the output route corresponding to the port number. */
		for (i = 0; i < RCAR_DU_OUTPUT_MAX; ++i) {
			if (rcdu->info->routes[i].possible_crtcs &&
			    rcdu->info->routes[i].port == ep.port) {
				output = i;
				break;
			}
		}

		if (i == RCAR_DU_OUTPUT_MAX) {
			dev_warn(rcdu->dev,
				 "port %u references unexisting output, skipping\n",
				 ep.port);
			continue;
		}

		/* Process the output pipeline. */
		ret = rcar_du_encoders_init_one(rcdu, output, &ep);
		if (ret < 0) {
			if (ret == -EPROBE_DEFER) {
				of_node_put(ep_node);
				return ret;
			}

			dev_info(rcdu->dev,
				 "encoder initialization failed, skipping\n");
			continue;
		}

		num_encoders += ret;
	}

	return num_encoders;
}

int rcar_du_modeset_init(struct rcar_du_device *rcdu)
{
	static const unsigned int mmio_offsets[] = {
		DU0_REG_OFFSET, DU2_REG_OFFSET
	};

	struct drm_device *dev = rcdu->ddev;
	struct drm_encoder *encoder;
	struct drm_fbdev_cma *fbdev;
	unsigned int num_encoders;
	unsigned int num_groups;
	unsigned int i;
	int ret;

	drm_mode_config_init(dev);

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.max_width = 4095;
	dev->mode_config.max_height = 2047;
	dev->mode_config.funcs = &rcar_du_mode_config_funcs;

	rcdu->num_crtcs = rcdu->info->num_crtcs;

	/* Initialize the groups. */
	num_groups = DIV_ROUND_UP(rcdu->num_crtcs, 2);

	for (i = 0; i < num_groups; ++i) {
		struct rcar_du_group *rgrp = &rcdu->groups[i];

		mutex_init(&rgrp->lock);

		rgrp->dev = rcdu;
		rgrp->mmio_offset = mmio_offsets[i];
		rgrp->index = i;

		ret = rcar_du_planes_init(rgrp);
		if (ret < 0)
			return ret;
	}

	/* Create the CRTCs. */
	for (i = 0; i < rcdu->num_crtcs; ++i) {
		struct rcar_du_group *rgrp = &rcdu->groups[i / 2];

		ret = rcar_du_crtc_create(rgrp, i);
		if (ret < 0)
			return ret;
	}

	/* Initialize the encoders. */
	ret = rcar_du_lvdsenc_init(rcdu);
	if (ret < 0)
		return ret;

	ret = rcar_du_encoders_init(rcdu);
	if (ret < 0)
		return ret;

	if (ret == 0) {
		dev_err(rcdu->dev, "error: no encoder could be initialized\n");
		return -EINVAL;
	}

	num_encoders = ret;

	/* Set the possible CRTCs and possible clones. There's always at least
	 * one way for all encoders to clone each other, set all bits in the
	 * possible clones field.
	 */
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		struct rcar_du_encoder *renc = to_rcar_encoder(encoder);
		const struct rcar_du_output_routing *route =
			&rcdu->info->routes[renc->output];

		encoder->possible_crtcs = route->possible_crtcs;
		encoder->possible_clones = (1 << num_encoders) - 1;
	}

	drm_mode_config_reset(dev);

	drm_kms_helper_poll_init(dev);

	if (dev->mode_config.num_connector) {
		fbdev = drm_fbdev_cma_init(dev, 32, dev->mode_config.num_crtc,
					   dev->mode_config.num_connector);
		if (IS_ERR(fbdev))
			return PTR_ERR(fbdev);

		rcdu->fbdev = fbdev;
	} else {
		dev_info(rcdu->dev,
			 "no connector found, disabling fbdev emulation\n");
	}

	return 0;
}
