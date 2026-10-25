// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim GMSL2 Deserializer Driver
 *
 * Copyright (C) 2025 Analog Devices Inc.
 */

#include <linux/delay.h>
#include <linux/i2c-atr.h>
#include <linux/i2c-mux.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/sched.h>
#include <linux/units.h>

#include <media/mipi-csi2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#include "max_des.h"
#include "max_ser.h"
#include "max_serdes.h"

#define MAX_DES_LINK_FREQUENCY_MIN		(100  * HZ_PER_MHZ)
#define MAX_DES_LINK_FREQUENCY_DEFAULT		(750  * HZ_PER_MHZ)
#define MAX_DES_LINK_FREQUENCY_MAX		(1250 * HZ_PER_MHZ)

#define MAX_DES_NUM_PHYS			4
#define MAX_DES_NUM_LINKS			4
#define MAX_DES_NUM_PIPES			8

struct max_des_priv {
	struct max_des *des;

	struct device *dev;
	struct i2c_client *client;
	struct i2c_atr *atr;
	struct i2c_mux_core *mux;

	struct media_pad *pads;
	struct regulator **pocs;
	struct max_serdes_source *sources;
	u64 *streams_masks;

	struct notifier_block i2c_nb;
	struct max_des_link *pending_link;
	struct task_struct *pending_task;
	struct v4l2_subdev sd;
	struct v4l2_async_notifier nf;
	struct v4l2_ctrl_handler ctrl_handler;

	struct max_des_phy *unused_phy;
};

struct max_des_remap_context {
	enum max_serdes_gmsl_mode mode;
	/* Mark whether TPG is enabled */
	bool tpg;
	/* Mark the PHYs to which each pipe is mapped. */
	unsigned long pipe_phy_masks[MAX_DES_NUM_PIPES];
	/* Mark the pipes in use. */
	bool pipe_in_use[MAX_DES_NUM_PIPES];
	/* Mark whether pipe has remapped VC ids. */
	bool vc_ids_remapped[MAX_DES_NUM_PIPES];
	/* Map between pipe VC ids and PHY VC ids. */
	unsigned int vc_ids_map[MAX_DES_NUM_PIPES][MAX_DES_NUM_PHYS][MAX_SERDES_VC_ID_NUM];
	/* Mark whether a pipe VC id has been mapped to a PHY VC id. */
	unsigned long vc_ids_masks[MAX_DES_NUM_PIPES][MAX_DES_NUM_PHYS];
	/* Mark whether a PHY VC id has been mapped. */
	unsigned long dst_vc_ids_masks[MAX_DES_NUM_PHYS];
};

struct max_des_mode_context {
	bool phys_bpp8_shared_with_16[MAX_DES_NUM_PHYS];
	bool pipes_bpp8_shared_with_16[MAX_DES_NUM_PIPES];
	u32 phys_double_bpps[MAX_DES_NUM_PHYS];
	u32 pipes_double_bpps[MAX_DES_NUM_PIPES];
};

struct max_des_route_hw {
	struct max_serdes_source *source;
	struct max_des_pipe *pipe;
	struct max_des_phy *phy;
	struct v4l2_mbus_frame_desc_entry entry;
	bool is_tpg;
};

struct max_des_link_hw {
	struct max_serdes_source *source;
	struct max_des_link *link;
	struct max_des_pipe *pipe;
};

static inline struct max_des_priv *sd_to_priv(struct v4l2_subdev *sd)
{
	return container_of(sd, struct max_des_priv, sd);
}

static inline struct max_des_priv *nf_to_priv(struct v4l2_async_notifier *nf)
{
	return container_of(nf, struct max_des_priv, nf);
}

static inline struct max_des_priv *ctrl_to_priv(struct v4l2_ctrl_handler *handler)
{
	return container_of(handler, struct max_des_priv, ctrl_handler);
}

static inline bool max_des_pad_is_sink(struct max_des *des, u32 pad)
{
	return pad < des->info->num_links;
}

static inline bool max_des_pad_is_source(struct max_des *des, u32 pad)
{
	return pad >= des->info->num_links &&
	       pad < des->info->num_links + des->info->num_phys;
}

static inline bool max_des_pad_is_tpg(struct max_des *des, u32 pad)
{
	return pad == des->info->num_links + des->info->num_phys;
}

static inline unsigned int max_des_link_to_pad(struct max_des *des,
					       struct max_des_link *link)
{
	return link->index;
}

static inline unsigned int max_des_phy_to_pad(struct max_des *des,
					      struct max_des_phy *phy)
{
	return phy->index + des->info->num_links;
}

static inline unsigned int max_des_num_pads(struct max_des *des)
{
	return des->info->num_links + des->info->num_phys;
	/*
	 * TODO: once MEDIA_PAD_FL_INTERNAL is enabled in the uAPI we
	 * can add the TPG pad:
	 *	return des->info->num_links + des->info->num_phys +
	 *		(des->ops->set_tpg ? 1 : 0);
	 */
}

static struct max_des_phy *max_des_pad_to_phy(struct max_des *des, u32 pad)
{
	if (!max_des_pad_is_source(des, pad))
		return NULL;

	return &des->phys[pad - des->info->num_links];
}

static struct max_des_link *max_des_pad_to_link(struct max_des *des, u32 pad)
{
	if (!max_des_pad_is_sink(des, pad))
		return NULL;

	return &des->links[pad];
}

static struct max_des_pipe *
max_des_find_link_pipe(struct max_des *des, struct max_des_link *link)
{
	unsigned int i;

	for (i = 0; i < des->info->num_pipes; i++) {
		struct max_des_pipe *pipe = &des->pipes[i];

		if (pipe->link_id == link->index)
			return pipe;
	}

	return NULL;
}

static struct max_serdes_source *
max_des_get_link_source(struct max_des_priv *priv, struct max_des_link *link)
{
	return &priv->sources[link->index];
}

static const struct max_serdes_tpg_entry *
max_des_find_tpg_entry(struct max_des *des, u32 target_index,
		       u32 width, u32 height, u32 code,
		       u32 numerator, u32 denominator)
{
	const struct max_serdes_tpg_entry *entry;
	unsigned int index = 0;
	unsigned int i;

	for (i = 0; i < des->info->tpg_entries.num_entries; i++) {
		entry = &des->info->tpg_entries.entries[i];

		if ((width != 0 && width != entry->width) ||
		    (height != 0 && height != entry->height) ||
		    (code != 0 && code != entry->code) ||
		    (numerator != 0 && numerator != entry->interval.numerator) ||
		    (denominator != 0 && denominator != entry->interval.denominator))
			continue;

		if (index == target_index)
			break;

		index++;
	}

	if (i == des->info->tpg_entries.num_entries)
		return NULL;

	return &des->info->tpg_entries.entries[i];
}

static const struct max_serdes_tpg_entry *
max_des_find_state_tpg_entry(struct max_des *des, struct v4l2_subdev_state *state,
			     unsigned int pad)
{
	struct v4l2_mbus_framefmt *fmt;
	struct v4l2_fract *in;

	fmt = v4l2_subdev_state_get_format(state, pad, MAX_SERDES_TPG_STREAM);
	if (!fmt)
		return NULL;

	in = v4l2_subdev_state_get_interval(state, pad, MAX_SERDES_TPG_STREAM);
	if (!in)
		return NULL;

	return max_des_find_tpg_entry(des, 0, fmt->width, fmt->height, fmt->code,
				      in->numerator, in->denominator);
}

static int max_des_get_tpg_fd_entry_state(struct max_des *des,
					  struct v4l2_subdev_state *state,
					  struct v4l2_mbus_frame_desc_entry *fd_entry,
					  unsigned int pad)
{
	const struct max_serdes_tpg_entry *entry;

	entry = max_des_find_state_tpg_entry(des, state, pad);
	if (!entry)
		return -EINVAL;

	fd_entry->stream = MAX_SERDES_TPG_STREAM;
	fd_entry->flags = V4L2_MBUS_FRAME_DESC_FL_LEN_MAX;
	fd_entry->length = entry->width * entry->height * entry->bpp / 8;
	fd_entry->pixelcode = entry->code;
	fd_entry->bus.csi2.vc = 0;
	fd_entry->bus.csi2.dt = entry->dt;

	return 0;
}

static int max_des_tpg_route_to_hw(struct max_des_priv *priv,
				   struct v4l2_subdev_state *state,
				   struct v4l2_subdev_route *route,
				   struct max_des_route_hw *hw)
{
	struct max_des *des = priv->des;

	/* TPG injects its data into all pipes, but use pipe 0 for simplicity. */
	hw->pipe = &des->pipes[0];

	hw->phy = max_des_pad_to_phy(des, route->source_pad);
	if (!hw->phy)
		return -ENOENT;

	return max_des_get_tpg_fd_entry_state(des, state, &hw->entry,
					      route->sink_pad);
}

static int max_des_route_to_hw(struct max_des_priv *priv,
			       struct v4l2_subdev_state *state,
			       struct v4l2_subdev_route *route,
			       struct max_des_route_hw *hw)
{
	struct max_des *des = priv->des;
	struct v4l2_mbus_frame_desc fd = {};
	struct max_des_link *link;
	unsigned int i;
	int ret;

	memset(hw, 0, sizeof(*hw));

	hw->is_tpg = max_des_pad_is_tpg(des, route->sink_pad);
	if (hw->is_tpg)
		return max_des_tpg_route_to_hw(priv, state, route, hw);

	link = max_des_pad_to_link(des, route->sink_pad);
	if (!link)
		return -ENOENT;

	hw->phy = max_des_pad_to_phy(des, route->source_pad);
	if (!hw->phy)
		return -ENOENT;

	hw->pipe = max_des_find_link_pipe(des, link);
	if (!hw->pipe)
		return -ENOENT;

	hw->source = max_des_get_link_source(priv, link);
	if (!hw->source->sd)
		return 0;

	ret = v4l2_subdev_call(hw->source->sd, pad, get_frame_desc,
			       hw->source->pad, &fd);
	if (ret)
		return ret;

	for (i = 0; i < fd.num_entries; i++)
		if (fd.entry[i].stream == route->sink_stream)
			break;

	if (i == fd.num_entries)
		return -ENOENT;

	hw->entry = fd.entry[i];

	return 0;
}

static int max_des_link_to_hw(struct max_des_priv *priv,
			      struct max_des_link *link,
			      struct max_des_link_hw *hw)
{
	struct max_des *des = priv->des;

	memset(hw, 0, sizeof(*hw));

	hw->link = link;

	hw->pipe = max_des_find_link_pipe(des, hw->link);
	if (!hw->pipe)
		return -ENOENT;

	hw->source = max_des_get_link_source(priv, hw->link);

	return 0;
}

static int max_des_link_index_to_hw(struct max_des_priv *priv, unsigned int i,
				    struct max_des_link_hw *hw)
{
	return max_des_link_to_hw(priv, &priv->des->links[i], hw);
}

static int max_des_set_pipe_remaps(struct max_des_priv *priv,
				   struct max_des_pipe *pipe,
				   struct max_des_remap *remaps,
				   unsigned int num_remaps)
{
	struct max_des *des = priv->des;
	unsigned int mask = 0;
	unsigned int i;
	int ret;

	if (!des->ops->set_pipe_remap)
		return 0;

	for (i = 0; i < num_remaps; i++) {
		ret = des->ops->set_pipe_remap(des, pipe, i, &remaps[i]);
		if (ret)
			return ret;

		mask |= BIT(i);
	}

	return des->ops->set_pipe_remaps_enable(des, pipe, mask);
}

static int max_des_set_pipe_vc_remaps(struct max_des_priv *priv,
				      struct max_des_pipe *pipe,
				      struct max_serdes_vc_remap *vc_remaps,
				      unsigned int num_vc_remaps)
{
	struct max_des *des = priv->des;
	unsigned int mask = 0;
	unsigned int i;
	int ret;

	for (i = 0; i < num_vc_remaps; i++) {
		ret = des->ops->set_pipe_vc_remap(des, pipe, i, &vc_remaps[i]);
		if (ret)
			return ret;

		mask |= BIT(i);
	}

	return des->ops->set_pipe_vc_remaps_enable(des, pipe, mask);
}

static int max_des_map_src_dst_vc_id(struct max_des_remap_context *context,
				     unsigned int pipe_id, unsigned int phy_id,
				     unsigned int src_vc_id, bool keep_vc)
{
	unsigned int vc_id;

	if (src_vc_id >= MAX_SERDES_VC_ID_NUM)
		return -E2BIG;

	if (context->vc_ids_masks[pipe_id][phy_id] & BIT(src_vc_id))
		return 0;

	if (keep_vc && !(context->dst_vc_ids_masks[phy_id] & BIT(src_vc_id)))
		vc_id = src_vc_id;
	else
		vc_id = ffz(context->dst_vc_ids_masks[phy_id]);

	if (vc_id != src_vc_id)
		context->vc_ids_remapped[pipe_id] = true;

	if (vc_id >= MAX_SERDES_VC_ID_NUM)
		return -E2BIG;

	context->pipe_phy_masks[pipe_id] |= BIT(phy_id);
	context->dst_vc_ids_masks[phy_id] |= BIT(vc_id);

	context->vc_ids_map[pipe_id][phy_id][src_vc_id] = vc_id;
	context->vc_ids_masks[pipe_id][phy_id] |= BIT(src_vc_id);

	return 0;
}

static int max_des_get_src_dst_vc_id(struct max_des_remap_context *context,
				     unsigned int pipe_id, unsigned int phy_id,
				     unsigned int src_vc_id, unsigned int *dst_vc_id)
{
	if (!(context->vc_ids_masks[pipe_id][phy_id] & BIT(src_vc_id)))
		return -ENOENT;

	*dst_vc_id = context->vc_ids_map[pipe_id][phy_id][src_vc_id];

	return 0;
}

static int max_des_populate_remap_usage(struct max_des_priv *priv,
					struct max_des_remap_context *context,
					struct v4l2_subdev_state *state)
{
	struct v4l2_subdev_route *route;
	int ret;

	for_each_active_route(&state->routing, route) {
		struct max_des_route_hw hw;

		ret = max_des_route_to_hw(priv, state, route, &hw);
		if (ret)
			return ret;

		if (hw.is_tpg)
			context->tpg = true;

		context->pipe_in_use[hw.pipe->index] = true;
	}

	return 0;
}

static int max_des_get_supported_modes(struct max_des_priv *priv,
				       struct max_des_remap_context *context,
				       unsigned int *modes)
{
	struct max_des *des = priv->des;
	unsigned int i;
	int ret;

	*modes = des->info->modes;

	if (context->tpg)
		*modes = BIT(des->info->tpg_mode);

	for (i = 0; i < des->info->num_links; i++) {
		struct max_des_link_hw hw;

		ret = max_des_link_index_to_hw(priv, i, &hw);
		if (ret)
			return ret;

		if (!hw.link->enabled)
			continue;

		if (!hw.source->sd)
			continue;

		if (!context->pipe_in_use[hw.pipe->index])
			continue;

		*modes &= max_ser_get_supported_modes(hw.source->sd);
	}

	/*
	 * Serializers need to all be in the same mode because of hardware
	 * issues when running them in mixed modes.
	 */
	if (!*modes)
		return -EINVAL;

	return 0;
}

static int max_des_populate_remap_context_mode(struct max_des_priv *priv,
					       struct max_des_remap_context *context,
					       unsigned int modes)
{
	struct max_des *des = priv->des;
	unsigned int i;
	int ret;

	context->mode = MAX_SERDES_GMSL_PIXEL_MODE;

	/*
	 * If pixel mode is the only supported mode, do not try to see if
	 * tunnel mode can be used.
	 */
	if (modes == BIT(MAX_SERDES_GMSL_PIXEL_MODE))
		return 0;

	for (i = 0; i < des->info->num_links; i++) {
		struct max_des_link_hw hw;

		ret = max_des_link_index_to_hw(priv, i, &hw);
		if (ret)
			return ret;

		if (!hw.link->enabled)
			continue;

		if (!hw.source->sd)
			continue;

		if (!context->pipe_in_use[hw.pipe->index])
			continue;

		if (hweight_long(context->pipe_phy_masks[hw.pipe->index]) == 1 &&
		    (!context->vc_ids_remapped[hw.pipe->index] ||
		     max_ser_supports_vc_remap(hw.source->sd) ||
		     des->ops->set_pipe_vc_remap))
			continue;

		return 0;
	}

	context->mode = MAX_SERDES_GMSL_TUNNEL_MODE;

	return 0;
}

static int max_des_should_keep_vc(struct max_des_priv *priv,
				  struct max_des_route_hw *hw,
				  unsigned int modes)
{
	struct max_des *des = priv->des;

	/* Pixel mode deserializers always have the ability to remap VCs. */
	if (modes == BIT(MAX_SERDES_GMSL_PIXEL_MODE))
		return false;

	if (des->ops->set_pipe_vc_remap)
		return false;

	if (!hw->is_tpg && hw->source && hw->source->sd &&
	    max_ser_supports_vc_remap(hw->source->sd))
		return false;

	return true;
}

static int max_des_populate_remap_context(struct max_des_priv *priv,
					  struct max_des_remap_context *context,
					  struct v4l2_subdev_state *state)
{
	struct v4l2_subdev_route *route;
	unsigned int modes;
	int ret;

	ret = max_des_populate_remap_usage(priv, context, state);
	if (ret)
		return ret;

	ret = max_des_get_supported_modes(priv, context, &modes);
	if (ret)
		return ret;

	for_each_active_route(&state->routing, route) {
		struct max_des_route_hw hw;
		bool keep_vc;

		ret = max_des_route_to_hw(priv, state, route, &hw);
		if (ret)
			return ret;

		keep_vc = max_des_should_keep_vc(priv, &hw, modes);

		ret = max_des_map_src_dst_vc_id(context, hw.pipe->index, hw.phy->index,
						hw.entry.bus.csi2.vc, keep_vc);
		if (ret)
			return ret;
	}

	return max_des_populate_remap_context_mode(priv, context, modes);
}

static int max_des_populate_mode_context(struct max_des_priv *priv,
					 struct max_des_mode_context *context,
					 struct v4l2_subdev_state *state,
					 enum max_serdes_gmsl_mode mode)
{
	bool bpp8_not_shared_with_16_phys[MAX_DES_NUM_PHYS] = { 0 };
	u32 undoubled_bpps_phys[MAX_DES_NUM_PHYS] = { 0 };
	u32 bpps_pipes[MAX_DES_NUM_PIPES] = { 0 };
	struct max_des *des = priv->des;
	struct v4l2_subdev_route *route;
	unsigned int i;
	int ret;

	if (mode != MAX_SERDES_GMSL_PIXEL_MODE)
		return 0;

	/*
	 * Go over all streams and gather the bpps for all pipes.
	 *
	 * Then, go over all the streams again and check if the
	 * current stream is doubled.
	 *
	 * If the current stream is doubled, add it to a doubled mask for both
	 * the pipe and the PHY.
	 *
	 * If the current stream is not doubled, add it to a local undoubled
	 * mask for the PHY.
	 *
	 * Also, track whether an 8bpp stream is shared with any bpp > 8 on both
	 * the PHYs and the pipes, since that needs to be special cased.
	 *
	 * After going over all the streams, remove the undoubled streams from
	 * the doubled ones. Doubled and undoubled streams cannot be streamed
	 * over the same PHY.
	 *
	 * Then, do a second pass to remove the undoubled streams from the pipes.
	 *
	 * This operation cannot be done in a single pass because any pipe might
	 * generate an undoubled stream for a specific bpp, causing already
	 * processed pipes to need to have their doubled bpps updated.
	 */

	for_each_active_route(&state->routing, route) {
		struct max_des_route_hw hw;
		unsigned int bpp;

		ret = max_des_route_to_hw(priv, state, route, &hw);
		if (ret)
			return ret;

		ret = max_serdes_get_fd_bpp(&hw.entry, &bpp);
		if (ret)
			return ret;

		bpps_pipes[hw.pipe->index] |= BIT(bpp);
	}

	for_each_active_route(&state->routing, route) {
		unsigned int bpp, min_bpp, max_bpp, doubled_bpp;
		unsigned int pipe_id, phy_id;
		struct max_des_route_hw hw;
		u32 sink_bpps;

		ret = max_des_route_to_hw(priv, state, route, &hw);
		if (ret)
			return ret;

		ret = max_serdes_get_fd_bpp(&hw.entry, &bpp);
		if (ret)
			return ret;

		sink_bpps = bpps_pipes[hw.pipe->index];

		ret = max_serdes_process_bpps(priv->dev, sink_bpps, ~0U, &doubled_bpp);
		if (ret)
			return ret;

		min_bpp = __ffs(sink_bpps);
		max_bpp = __fls(sink_bpps);
		pipe_id = hw.pipe->index;
		phy_id = hw.phy->index;

		if (bpp == doubled_bpp) {
			context->phys_double_bpps[phy_id] |= BIT(bpp);
			context->pipes_double_bpps[pipe_id] |= BIT(bpp);
		} else {
			undoubled_bpps_phys[phy_id] |= BIT(bpp);
		}

		if (min_bpp == 8 && max_bpp > 8) {
			context->phys_bpp8_shared_with_16[phy_id] = true;
			context->pipes_bpp8_shared_with_16[pipe_id] = true;
		} else if (min_bpp == 8 && max_bpp == 8) {
			bpp8_not_shared_with_16_phys[phy_id] = true;
		}
	}

	for (i = 0; i < des->info->num_phys; i++) {
		if (context->phys_bpp8_shared_with_16[i] && bpp8_not_shared_with_16_phys[i]) {
			dev_err(priv->dev,
				"Cannot stream 8bpp coming from pipes padded to 16bpp and pipes not padded to 16bpp on the same PHY\n");
			return -EINVAL;
		}
	}

	for (i = 0; i < des->info->num_phys; i++)
		context->phys_double_bpps[i] &= ~undoubled_bpps_phys[i];

	for_each_active_route(&state->routing, route) {
		struct max_des_route_hw hw;

		ret = max_des_route_to_hw(priv, state, route, &hw);
		if (ret)
			return ret;

		context->pipes_double_bpps[hw.pipe->index] &=
			context->phys_double_bpps[hw.phy->index];
	}

	return 0;
}

static int max_des_add_vc_remap(struct max_des *des, struct max_serdes_vc_remap *vc_remaps,
				unsigned int *num_vc_remaps, unsigned int src_vc_id,
				unsigned int dst_vc_id)
{
	struct max_serdes_vc_remap *vc_remap;
	unsigned int i;

	for (i = 0; i < *num_vc_remaps; i++) {
		vc_remap = &vc_remaps[i];

		if (vc_remap->src == src_vc_id && vc_remap->dst == dst_vc_id)
			return 0;
	}

	if (*num_vc_remaps == MAX_SERDES_VC_ID_NUM)
		return -E2BIG;

	vc_remaps[*num_vc_remaps].src = src_vc_id;
	vc_remaps[*num_vc_remaps].dst = dst_vc_id;

	(*num_vc_remaps)++;

	return 0;
}

static int max_des_get_pipe_vc_remaps(struct max_des_priv *priv,
				      struct max_des_remap_context *context,
				      struct max_des_pipe *pipe,
				      struct max_serdes_vc_remap *vc_remaps,
				      unsigned int *num_vc_remaps,
				      struct v4l2_subdev_state *state,
				      u64 *streams_masks, bool with_tpg)
{
	struct max_des *des = priv->des;
	struct v4l2_subdev_route *route;
	int ret;

	*num_vc_remaps = 0;

	if (context->mode != MAX_SERDES_GMSL_TUNNEL_MODE)
		return 0;

	for_each_active_route(&state->routing, route) {
		unsigned int src_vc_id, dst_vc_id;
		struct max_des_route_hw hw;

		if (!(BIT_ULL(route->sink_stream) & streams_masks[route->sink_pad]))
			continue;

		ret = max_des_route_to_hw(priv, state, route, &hw);
		if (ret)
			return ret;

		if (!with_tpg && hw.is_tpg)
			continue;

		if (hw.pipe != pipe)
			continue;

		src_vc_id = hw.entry.bus.csi2.vc;

		ret = max_des_get_src_dst_vc_id(context, pipe->index, hw.phy->index,
						src_vc_id, &dst_vc_id);
		if (ret)
			return ret;

		ret = max_des_add_vc_remap(des, vc_remaps, num_vc_remaps,
					   src_vc_id, dst_vc_id);
		if (ret)
			return ret;
	}

	return 0;
}

static void max_des_get_pipe_mode(struct max_des_mode_context *context,
				  struct max_des_pipe *pipe,
				  struct max_des_pipe_mode *mode)
{
	u32 double_bpps = context->pipes_double_bpps[pipe->index];

	if ((double_bpps & BIT(8)) &&
	    !context->pipes_bpp8_shared_with_16[pipe->index]) {
		mode->dbl8 = true;
		mode->dbl8mode = true;
	}
}

static void max_des_get_phy_mode(struct max_des_mode_context *context,
				 struct max_des_phy *phy,
				 struct max_des_phy_mode *mode)
{
	bool bpp8_pipe_shared_with_16 = context->phys_bpp8_shared_with_16[phy->index];
	u32 double_bpps = context->phys_double_bpps[phy->index];

	if (BIT(8) & double_bpps) {
		if (bpp8_pipe_shared_with_16)
			mode->alt2_mem_map8 = true;
		else
			mode->alt_mem_map8 = true;
	}

	if (BIT(10) & double_bpps)
		mode->alt_mem_map10 = true;

	if (BIT(12) & double_bpps)
		mode->alt_mem_map12 = true;
}

static int max_des_set_modes(struct max_des_priv *priv,
			     struct max_des_mode_context *context)
{
	struct max_des *des = priv->des;
	unsigned int i;
	int ret;

	for (i = 0; i < des->info->num_phys; i++) {
		struct max_des_phy *phy = &des->phys[i];
		struct max_des_phy_mode mode = { 0 };

		max_des_get_phy_mode(context, phy, &mode);

		if (phy->mode.alt_mem_map8 == mode.alt_mem_map8 &&
		    phy->mode.alt_mem_map10 == mode.alt_mem_map10 &&
		    phy->mode.alt_mem_map12 == mode.alt_mem_map12 &&
		    phy->mode.alt2_mem_map8 == mode.alt2_mem_map8)
			continue;

		if (des->ops->set_phy_mode) {
			ret = des->ops->set_phy_mode(des, phy, &mode);
			if (ret)
				return ret;
		}

		phy->mode = mode;
	}

	for (i = 0; i < des->info->num_pipes; i++) {
		struct max_des_pipe *pipe = &des->pipes[i];
		struct max_des_pipe_mode mode = { 0 };

		max_des_get_pipe_mode(context, pipe, &mode);

		if (pipe->mode.dbl8 == mode.dbl8 &&
		    pipe->mode.dbl10 == mode.dbl10 &&
		    pipe->mode.dbl12 == mode.dbl12 &&
		    pipe->mode.dbl8mode == mode.dbl8mode &&
		    pipe->mode.dbl10mode == mode.dbl10mode)
			continue;

		if (des->ops->set_pipe_mode) {
			ret = des->ops->set_pipe_mode(des, pipe, &mode);
			if (ret)
				return ret;
		}

		pipe->mode = mode;
	}

	for (i = 0; i < des->info->num_links; i++) {
		struct max_des_link_hw hw;
		u32 pipe_double_bpps = 0;

		ret = max_des_link_index_to_hw(priv, i, &hw);
		if (ret)
			return ret;

		if (!hw.link->enabled)
			continue;

		if (!hw.source->sd)
			continue;

		pipe_double_bpps = context->pipes_double_bpps[hw.pipe->index];

		ret = max_ser_set_double_bpps(hw.source->sd, pipe_double_bpps);
		if (ret)
			return ret;
	}

	return 0;
}

static int max_des_set_tunnel(struct max_des_priv *priv,
			      struct max_des_remap_context *context)
{
	struct max_des *des = priv->des;
	unsigned int i;
	int ret;

	if (des->ops->set_pipe_tunnel_enable) {
		for (i = 0; i < des->info->num_pipes; i++) {
			struct max_des_pipe *pipe = &des->pipes[i];
			bool tunnel_mode = context->mode == MAX_SERDES_GMSL_TUNNEL_MODE;

			ret = des->ops->set_pipe_tunnel_enable(des, pipe, tunnel_mode);
			if (ret)
				return ret;
		}
	}

	for (i = 0; i < des->info->num_links; i++) {
		struct max_des_link_hw hw;

		ret = max_des_link_index_to_hw(priv, i, &hw);
		if (ret)
			return ret;

		if (!hw.link->enabled)
			continue;

		if (!hw.source->sd)
			continue;

		if (!context->pipe_in_use[hw.pipe->index])
			continue;

		ret = max_ser_set_mode(hw.source->sd, context->mode);
		if (ret)
			return ret;
	}

	des->mode = context->mode;

	return 0;
}

static int max_des_set_vc_remaps(struct max_des_priv *priv,
				 struct max_des_remap_context *context,
				 struct v4l2_subdev_state *state,
				 u64 *streams_masks)
{
	struct max_des *des = priv->des;
	unsigned int i;
	int ret;

	if (des->ops->set_pipe_vc_remap)
		return 0;

	for (i = 0; i < des->info->num_links; i++) {
		struct max_serdes_vc_remap vc_remaps[MAX_SERDES_VC_ID_NUM];
		struct max_des_link_hw hw;
		unsigned int num_vc_remaps;

		ret = max_des_link_index_to_hw(priv, i, &hw);
		if (ret)
			return ret;

		if (!hw.link->enabled)
			continue;

		if (!hw.source->sd)
			continue;

		if (!max_ser_supports_vc_remap(hw.source->sd))
			continue;

		ret = max_des_get_pipe_vc_remaps(priv, context, hw.pipe,
						 vc_remaps, &num_vc_remaps,
						 state, streams_masks, false);
		if (ret)
			return ret;

		ret = max_ser_set_vc_remaps(hw.source->sd, vc_remaps, num_vc_remaps);
		if (ret)
			return ret;
	}

	return 0;
}

static int max_des_set_pipes_stream_id(struct max_des_priv *priv)
{
	bool stream_id_usage[MAX_SERDES_STREAMS_NUM] = { 0 };
	struct max_des *des = priv->des;
	unsigned int i;
	int ret;

	for (i = 0; i < des->info->num_links; i++) {
		struct max_des_link_hw hw;
		unsigned int stream_id;

		ret = max_des_link_index_to_hw(priv, i, &hw);
		if (ret)
			return ret;

		if (!hw.link->enabled)
			continue;

		if (!hw.source->sd)
			continue;

		stream_id = hw.pipe->stream_id;

		ret = max_ser_set_stream_id(hw.source->sd, stream_id);
		if (ret == -EOPNOTSUPP) {
			/*
			 * Serializer does not support setting the stream id,
			 * retrieve its hardcoded stream id.
			 */
			ret = max_ser_get_stream_id(hw.source->sd, &stream_id);
		}

		if (ret)
			return ret;

		if (stream_id >= MAX_SERDES_STREAMS_NUM) {
			dev_err(priv->dev, "Invalid stream id %u\n", stream_id);
			return -EINVAL;
		}

		if (stream_id_usage[stream_id] && des->info->needs_unique_stream_id) {
			dev_err(priv->dev, "Duplicate stream id %u\n", stream_id);
			return -EINVAL;
		}

		ret = des->ops->set_pipe_stream_id(des, hw.pipe, stream_id);
		if (ret)
			return ret;

		stream_id_usage[stream_id] = true;
		hw.pipe->stream_id = stream_id;
	}

	return 0;
}

static int max_des_set_pipes_phy(struct max_des_priv *priv,
				 struct max_des_remap_context *context)
{
	struct max_des *des = priv->des;
	unsigned int i;
	int ret;

	if (!des->ops->set_pipe_phy && !des->ops->set_pipe_tunnel_phy)
		return 0;

	for (i = 0; i < des->info->num_pipes; i++) {
		struct max_des_pipe *pipe = &des->pipes[i];
		struct max_des_phy *phy;
		unsigned int phy_id;

		phy_id = find_first_bit(&context->pipe_phy_masks[pipe->index],
					des->info->num_phys);

		if (priv->unused_phy &&
		    (context->mode != MAX_SERDES_GMSL_TUNNEL_MODE ||
		     phy_id == des->info->num_phys))
			phy_id = priv->unused_phy->index;

		if (phy_id != des->info->num_phys) {
			phy = &des->phys[phy_id];

			if (context->mode == MAX_SERDES_GMSL_PIXEL_MODE &&
			    des->ops->set_pipe_phy)
				ret = des->ops->set_pipe_phy(des, pipe, phy);
			else if (context->mode == MAX_SERDES_GMSL_TUNNEL_MODE &&
				 des->ops->set_pipe_tunnel_phy)
				ret = des->ops->set_pipe_tunnel_phy(des, pipe, phy);
			else
				ret = 0;

			if (ret)
				return ret;
		}

		pipe->phy_id = phy_id;
	}

	return 0;
}

static int max_des_add_remap(struct max_des *des, struct max_des_remap *remaps,
			     unsigned int *num_remaps, unsigned int phy_id,
			     unsigned int src_vc_id, unsigned int dst_vc_id,
			     unsigned int dt)
{
	struct max_des_remap *remap;
	unsigned int i;

	for (i = 0; i < *num_remaps; i++) {
		remap = &remaps[i];

		if (remap->from_dt == dt && remap->to_dt == dt &&
		    remap->from_vc == src_vc_id && remap->to_vc == dst_vc_id &&
		    remap->phy == phy_id)
			return 0;
	}

	if (*num_remaps == des->info->num_remaps_per_pipe)
		return -E2BIG;

	remap = &remaps[*num_remaps];
	remap->from_dt = dt;
	remap->from_vc = src_vc_id;
	remap->to_dt = dt;
	remap->to_vc = dst_vc_id;
	remap->phy = phy_id;

	(*num_remaps)++;

	return 0;
}

static int max_des_add_remaps(struct max_des *des, struct max_des_remap *remaps,
			      unsigned int *num_remaps, unsigned int phy_id,
			      unsigned int src_vc_id, unsigned int dst_vc_id,
			      unsigned int dt)
{
	int ret;

	ret = max_des_add_remap(des, remaps, num_remaps, phy_id,
				src_vc_id, dst_vc_id, dt);
	if (ret)
		return ret;

	ret = max_des_add_remap(des, remaps, num_remaps, phy_id,
				src_vc_id, dst_vc_id, MIPI_CSI2_DT_FS);
	if (ret)
		return ret;

	ret = max_des_add_remap(des, remaps, num_remaps, phy_id,
				src_vc_id, dst_vc_id, MIPI_CSI2_DT_FE);
	if (ret)
		return ret;

	return 0;
}

static int max_des_get_pipe_remaps(struct max_des_priv *priv,
				   struct max_des_remap_context *context,
				   struct max_des_pipe *pipe,
				   struct max_des_remap *remaps,
				   unsigned int *num_remaps,
				   struct v4l2_subdev_state *state,
				   u64 *streams_masks)
{
	struct v4l2_mbus_frame_desc_entry tpg_entry = { 0 };
	struct max_des *des = priv->des;
	struct v4l2_subdev_route *route;
	bool is_tpg_pipe = true;
	int ret;

	*num_remaps = 0;

	if (context->mode != MAX_SERDES_GMSL_PIXEL_MODE)
		return 0;

	for_each_active_route(&state->routing, route) {
		struct max_des_route_hw hw;
		unsigned int src_vc_id, dst_vc_id;

		if (!(BIT_ULL(route->sink_stream) & streams_masks[route->sink_pad]))
			continue;

		ret = max_des_route_to_hw(priv, state, route, &hw);
		if (ret)
			return ret;

		if (hw.is_tpg && hw.pipe != pipe) {
			is_tpg_pipe = false;
			tpg_entry = hw.entry;
		}

		if (hw.pipe != pipe)
			continue;

		src_vc_id = hw.entry.bus.csi2.vc;

		ret = max_des_get_src_dst_vc_id(context, pipe->index, hw.phy->index,
						src_vc_id, &dst_vc_id);
		if (ret)
			return ret;

		ret = max_des_add_remaps(des, remaps, num_remaps, hw.phy->index,
					 src_vc_id, dst_vc_id,
					 hw.entry.bus.csi2.dt);
		if (ret)
			return ret;
	}

	/*
	 * TPG mode is only handled on pipe 0, but the TPG pollutes other pipes
	 * with the same data.
	 * For devices that do not support setting the default PHY of a pipe,
	 * we want to filter out this data so it does not end up on the wrong
	 * PHY.
	 * Devices that support setting the default PHY of a pipe already use it
	 * to route unused pipes to an unused PHY.
	 */
	if (context->tpg && !is_tpg_pipe && !des->ops->set_pipe_phy &&
	    priv->unused_phy) {
		ret = max_des_add_remaps(des, remaps, num_remaps,
					 priv->unused_phy->index,
					 tpg_entry.bus.csi2.vc,
					 tpg_entry.bus.csi2.vc,
					 tpg_entry.bus.csi2.dt);
		if (ret)
			return ret;
	}

	return 0;
}

static int max_des_update_pipe_vc_remaps(struct max_des_priv *priv,
					 struct max_des_remap_context *context,
					 struct max_des_pipe *pipe,
					 struct v4l2_subdev_state *state,
					 u64 *streams_masks)
{
	struct max_des *des = priv->des;
	struct max_serdes_vc_remap *vc_remaps;
	unsigned int num_vc_remaps;
	int ret;

	if (!des->ops->set_pipe_vc_remap)
		return 0;

	vc_remaps = devm_kcalloc(priv->dev, MAX_SERDES_VC_ID_NUM,
				 sizeof(*vc_remaps), GFP_KERNEL);
	if (!vc_remaps)
		return -ENOMEM;

	ret = max_des_get_pipe_vc_remaps(priv, context, pipe, vc_remaps, &num_vc_remaps,
					 state, streams_masks, true);
	if (ret)
		goto err_free_new_vc_remaps;

	ret = max_des_set_pipe_vc_remaps(priv, pipe, vc_remaps, num_vc_remaps);
	if (ret)
		goto err_free_new_vc_remaps;

	if (pipe->vc_remaps)
		devm_kfree(priv->dev, pipe->vc_remaps);

	pipe->vc_remaps = vc_remaps;
	pipe->num_vc_remaps = num_vc_remaps;

	return 0;

err_free_new_vc_remaps:
	devm_kfree(priv->dev, vc_remaps);

	return ret;
}

static int max_des_update_pipe_remaps(struct max_des_priv *priv,
				      struct max_des_remap_context *context,
				      struct max_des_pipe *pipe,
				      struct v4l2_subdev_state *state,
				      u64 *streams_masks)
{
	struct max_des *des = priv->des;
	struct max_des_remap *remaps;
	unsigned int num_remaps;
	int ret;

	if (!des->ops->set_pipe_remap)
		return 0;

	remaps = devm_kcalloc(priv->dev, des->info->num_remaps_per_pipe,
			      sizeof(*remaps), GFP_KERNEL);
	if (!remaps)
		return -ENOMEM;

	ret = max_des_get_pipe_remaps(priv, context, pipe, remaps, &num_remaps,
				      state, streams_masks);
	if (ret)
		goto err_free_new_remaps;

	ret = max_des_set_pipe_remaps(priv, pipe, remaps, num_remaps);
	if (ret)
		goto err_free_new_remaps;

	if (pipe->remaps)
		devm_kfree(priv->dev, pipe->remaps);

	pipe->remaps = remaps;
	pipe->num_remaps = num_remaps;

	return 0;

err_free_new_remaps:
	devm_kfree(priv->dev, remaps);

	return ret;
}

static int max_des_update_pipe_enable(struct max_des_priv *priv,
				      struct max_des_pipe *pipe,
				      struct v4l2_subdev_state *state,
				      u64 *streams_masks)
{
	struct max_des *des = priv->des;
	struct v4l2_subdev_route *route;
	bool enable = false;
	int ret;

	for_each_active_route(&state->routing, route) {
		struct max_des_route_hw hw;

		if (!(BIT_ULL(route->sink_stream) & streams_masks[route->sink_pad]))
			continue;

		ret = max_des_route_to_hw(priv, state, route, &hw);
		if (ret)
			return ret;

		if (hw.pipe != pipe)
			continue;

		enable = true;
		break;
	}

	if (enable == pipe->enabled)
		return 0;

	ret = des->ops->set_pipe_enable(des, pipe, enable);
	if (ret)
		return ret;

	pipe->enabled = enable;

	return 0;
}

static int max_des_update_pipe(struct max_des_priv *priv,
			       struct max_des_remap_context *context,
			       struct max_des_pipe *pipe,
			       struct v4l2_subdev_state *state,
			       u64 *streams_masks)
{
	int ret;

	ret = max_des_update_pipe_remaps(priv, context, pipe,
					 state, streams_masks);
	if (ret)
		return ret;

	ret = max_des_update_pipe_vc_remaps(priv, context, pipe, state,
					    streams_masks);
	if (ret)
		goto err_revert_update_pipe_remaps;

	ret = max_des_update_pipe_enable(priv, pipe, state, streams_masks);
	if (ret)
		goto err_revert_update_pipe_vc_remaps;

	return 0;

err_revert_update_pipe_vc_remaps:
	max_des_update_pipe_vc_remaps(priv, context, pipe, state,
				      priv->streams_masks);

err_revert_update_pipe_remaps:
	max_des_update_pipe_remaps(priv, context, pipe, state,
				   priv->streams_masks);

	return ret;
}

static int max_des_init_link_ser_xlate(struct max_des_priv *priv,
				       struct max_des_link *link,
				       struct i2c_adapter *adapter,
				       u8 power_up_addr, u8 new_addr)
{
	struct max_des *des = priv->des;
	u8 addrs[] = { power_up_addr, new_addr };
	u8 current_addr;
	int ret;

	if (des->ops->select_links) {
		ret = des->ops->select_links(des, BIT(link->index));
		if (ret)
			return ret;
	}

	ret = max_ser_wait_for_multiple(adapter, addrs, ARRAY_SIZE(addrs),
					&current_addr);
	if (ret) {
		dev_err(priv->dev,
			"Failed to wait for serializer at 0x%02x or 0x%02x: %d\n",
			power_up_addr, new_addr, ret);
		return ret;
	}

	ret = max_ser_reset(adapter, current_addr);
	if (ret) {
		dev_err(priv->dev, "Failed to reset serializer: %d\n", ret);
		return ret;
	}

	ret = max_ser_wait(adapter, power_up_addr);
	if (ret) {
		dev_err(priv->dev,
			"Failed to wait for serializer at 0x%02x: %d\n",
			power_up_addr, ret);
		return ret;
	}

	ret = max_ser_change_address(adapter, power_up_addr, new_addr);
	if (ret) {
		dev_err(priv->dev,
			"Failed to change serializer from 0x%02x to 0x%02x: %d\n",
			power_up_addr, new_addr, ret);
		return ret;
	}

	ret = max_ser_wait(adapter, new_addr);
	if (ret) {
		dev_err(priv->dev,
			"Failed to wait for serializer at 0x%02x: %d\n",
			new_addr, ret);
		return ret;
	}

	if (des->info->fix_tx_ids) {
		ret = max_ser_fix_tx_ids(adapter, new_addr);
		if (ret)
			return ret;
	}

	return ret;
}

static int max_des_init(struct max_des_priv *priv)
{
	struct max_des *des = priv->des;
	unsigned int i;
	int ret;

	if (des->ops->init) {
		ret = des->ops->init(des);
		if (ret)
			return ret;
	}

	if (des->ops->set_enable) {
		ret = des->ops->set_enable(des, false);
		if (ret)
			return ret;
	}

	for (i = 0; i < des->info->num_phys; i++) {
		struct max_des_phy *phy = &des->phys[i];

		if (phy->enabled) {
			ret = des->ops->init_phy(des, phy);
			if (ret)
				return ret;
		}

		ret = des->ops->set_phy_enable(des, phy, phy->enabled);
		if (ret)
			return ret;
	}

	for (i = 0; i < des->info->num_pipes; i++) {
		struct max_des_pipe *pipe = &des->pipes[i];
		struct max_des_link *link = &des->links[pipe->link_id];

		ret = des->ops->set_pipe_enable(des, pipe, false);
		if (ret)
			return ret;

		if (des->ops->set_pipe_tunnel_enable) {
			ret = des->ops->set_pipe_tunnel_enable(des, pipe, false);
			if (ret)
				return ret;
		}

		if (des->ops->set_pipe_stream_id) {
			ret = des->ops->set_pipe_stream_id(des, pipe, pipe->stream_id);
			if (ret)
				return ret;
		}

		if (des->ops->set_pipe_link) {
			ret = des->ops->set_pipe_link(des, pipe, link);
			if (ret)
				return ret;
		}

		ret = max_des_set_pipe_remaps(priv, pipe, pipe->remaps,
					      pipe->num_remaps);
		if (ret)
			return ret;
	}

	if (!des->ops->init_link)
		return 0;

	for (i = 0; i < des->info->num_links; i++) {
		struct max_des_link *link = &des->links[i];

		if (!link->enabled)
			continue;

		ret = des->ops->init_link(des, link);
		if (ret)
			return ret;
	}

	return 0;
}

static void max_des_ser_find_version_range(struct max_des *des, int *min, int *max)
{
	unsigned int i;

	*min = MAX_SERDES_GMSL_MIN;
	*max = MAX_SERDES_GMSL_MAX;

	if (!des->info->needs_single_link_version)
		return;

	for (i = 0; i < des->info->num_links; i++) {
		struct max_des_link *link = &des->links[i];

		if (!link->enabled)
			continue;

		if (!link->ser_xlate.en)
			continue;

		*min = *max = link->version;

		return;
	}
}

static unsigned int max_des_enabled_links_mask(struct max_des *des)
{
	unsigned int mask = 0;
	unsigned int i;

	for (i = 0; i < des->info->num_links; i++) {
		struct max_des_link *link = &des->links[i];

		if (link->enabled)
			mask |= BIT(link->index);
	}

	return mask;
}

static int max_des_ser_attach_addr(struct max_des_priv *priv, u32 chan_id,
				   u16 addr, u16 alias)
{
	struct max_des *des = priv->des;
	struct max_des_link *link = &des->links[chan_id];
	unsigned int mask;
	int i, min, max;
	int ret = -ENOENT;
	int err;

	max_des_ser_find_version_range(des, &min, &max);

	if (link->ser_xlate.en) {
		dev_err(priv->dev, "Serializer for link %u already bound\n",
			link->index);
		return -EINVAL;
	}

	for (i = max; i >= min; i--) {
		if (!(des->info->versions & BIT(i)))
			continue;

		if (des->ops->set_link_version) {
			ret = des->ops->set_link_version(des, link, i);
			if (ret)
				goto out_select_links;
		}

		ret = max_des_init_link_ser_xlate(priv, link, priv->client->adapter,
						  addr, alias);
		if (!ret)
			break;
	}

	if (ret) {
		dev_err(priv->dev, "Cannot find serializer for link %u\n",
			link->index);
		ret = -ENOENT;
		goto out_select_links;
	}

	link->version = i;
	link->ser_xlate.src = alias;
	link->ser_xlate.dst = addr;
	link->ser_xlate.en = true;

out_select_links:
	if (!des->ops->select_links)
		return ret;

	mask = max_des_enabled_links_mask(des);
	err = des->ops->select_links(des, mask);
	if (err)
		dev_warn(priv->dev, "Failed to restore link selection: %d\n",
			 err);

	return ret;
}

static int max_des_ser_atr_attach_addr(struct i2c_atr *atr, u32 chan_id,
				       u16 addr, u16 alias)
{
	struct max_des_priv *priv = i2c_atr_get_driver_data(atr);

	return max_des_ser_attach_addr(priv, chan_id, addr, alias);
}

static void max_des_ser_atr_detach_addr(struct i2c_atr *atr, u32 chan_id, u16 addr)
{
	/* Don't do anything. */
}

static const struct i2c_atr_ops max_des_i2c_atr_ops = {
	.attach_addr = max_des_ser_atr_attach_addr,
	.detach_addr = max_des_ser_atr_detach_addr,
};

static void max_des_i2c_atr_deinit(struct max_des_priv *priv)
{
	struct max_des *des = priv->des;
	unsigned int i;

	for (i = 0; i < des->info->num_links; i++) {
		struct max_des_link *link = &des->links[i];

		/* Deleting adapters that haven't been added does no harm. */
		i2c_atr_del_adapter(priv->atr, link->index);
	}

	i2c_atr_delete(priv->atr);
	priv->atr = NULL;
}

static int max_des_i2c_atr_init(struct max_des_priv *priv)
{
	struct max_des *des = priv->des;
	unsigned int mask = 0;
	unsigned int i;
	int ret;

	if (!i2c_check_functionality(priv->client->adapter,
				     I2C_FUNC_SMBUS_WRITE_BYTE_DATA))
		return -ENODEV;

	priv->atr = i2c_atr_new(priv->client->adapter, priv->dev,
				&max_des_i2c_atr_ops, des->info->num_links,
				I2C_ATR_F_STATIC | I2C_ATR_F_PASSTHROUGH);
	if (IS_ERR(priv->atr))
		return PTR_ERR(priv->atr);

	i2c_atr_set_driver_data(priv->atr, priv);

	for (i = 0; i < des->info->num_links; i++) {
		struct max_des_link *link = &des->links[i];
		struct i2c_atr_adap_desc desc = {
			.chan_id = i,
		};

		if (!link->enabled)
			continue;

		ret = i2c_atr_add_adapter(priv->atr, &desc);
		if (ret)
			goto err_add_adapters;
	}

	if (des->ops->select_links) {
		mask = max_des_enabled_links_mask(des);

		ret = des->ops->select_links(des, mask);
		if (ret) {
			dev_warn(priv->dev, "Failed to select links: %d\n", ret);

			goto err_add_adapters;
		}
	}

	return 0;

err_add_adapters:
	max_des_i2c_atr_deinit(priv);

	return ret;
}

static void max_des_i2c_mux_deinit(struct max_des_priv *priv)
{
	struct max_des *des = priv->des;
	unsigned int i;

	bus_unregister_notifier(&i2c_bus_type, &priv->i2c_nb);
	i2c_mux_del_adapters(priv->mux);

	for (i = 0; i < des->info->num_links; i++)
		des->links[i].adapter = NULL;
}

static int max_des_i2c_mux_bus_notifier_call(struct notifier_block *nb,
					     unsigned long event, void *device)
{
	struct max_des_priv *priv = container_of(nb, struct max_des_priv, i2c_nb);
	struct max_des *des = priv->des;
	struct device *dev = device;
	struct i2c_client *client;
	unsigned int i;

	/*
	 * Ideally, we would want to negotiate the GMSL version on
	 * BUS_NOTIFY_ADD_DEVICE, but the adapters list is only populated with
	 * the new adapter after BUS_NOTIFY_ADD_DEVICE is issued.
	 */
	if (event != BUS_NOTIFY_BIND_DRIVER)
		return NOTIFY_DONE;

	client = i2c_verify_client(dev);
	if (!client)
		return NOTIFY_DONE;

	for (i = 0; i < des->info->num_links; i++) {
		if (des->links[i].enabled &&
		    client->adapter == des->links[i].adapter)
			break;
	}

	/*
	 * i2c_mux_add_adapter() only publishes the adapter once it has been
	 * registered, but registering it already probes the children declared
	 * under it. Such a child binds synchronously, from this same task, and
	 * lands here before link->adapter has been assigned. Match it against
	 * the link whose adapter is being added.
	 */
	if (i == des->info->num_links && priv->pending_task == current)
		i = priv->pending_link->index;

	if (i == des->info->num_links)
		return NOTIFY_DONE;

	max_des_ser_attach_addr(priv, i, client->addr, client->addr);

	return NOTIFY_DONE;
}

static int max_des_i2c_mux_select(struct i2c_mux_core *muxc, u32 chan)
{
	struct max_des_priv *priv = i2c_mux_priv(muxc);
	struct max_des *des = priv->des;
	int ret;

	if (!des->ops->select_links)
		return 0;

	/*
	 * This runs on every i2c transfer. With a single link there is nothing
	 * to select, so avoid resetting it on every transfer.
	 *
	 * Check must be done here due to select_links() use in multiple paths.
	 */
	if (des->info->num_links == 1)
		return 0;

	ret = des->ops->select_links(des, BIT(chan));
	if (ret)
		dev_warn(priv->dev, "Failed to select link %u: %d\n", chan, ret);

	return ret;
}

static int max_des_i2c_mux_init(struct max_des_priv *priv)
{
	struct max_des *des = priv->des;
	u32 flags = I2C_MUX_LOCKED;
	unsigned int i;
	int ret;

	if (des->info->num_links == 1)
		flags |= I2C_MUX_GATE;

	priv->mux = i2c_mux_alloc(priv->client->adapter, priv->dev,
				  des->info->num_links, 0, flags,
				  max_des_i2c_mux_select, NULL);
	if (!priv->mux)
		return -ENOMEM;

	priv->mux->priv = priv;

	priv->i2c_nb.notifier_call = max_des_i2c_mux_bus_notifier_call;
	ret = bus_register_notifier(&i2c_bus_type, &priv->i2c_nb);
	if (ret)
		return ret;

	for (i = 0; i < des->info->num_links; i++) {
		struct max_des_link *link = &des->links[i];

		if (!link->enabled)
			continue;

		priv->pending_link = link;
		priv->pending_task = current;
		ret = i2c_mux_add_adapter(priv->mux, 0, i);
		priv->pending_task = NULL;
		priv->pending_link = NULL;
		if (ret)
			goto err_add_adapters;

		link->adapter = priv->mux->adapter[priv->mux->num_adapters - 1];
	}

	return 0;

err_add_adapters:
	max_des_i2c_mux_deinit(priv);

	return ret;
}

static void max_des_i2c_adapter_deinit(struct max_des_priv *priv)
{
	struct max_des *des = priv->des;

	if (des->info->use_atr)
		return max_des_i2c_atr_deinit(priv);
	else
		return max_des_i2c_mux_deinit(priv);
}

static int max_des_i2c_adapter_init(struct max_des_priv *priv)
{
	struct max_des *des = priv->des;

	if (des->info->use_atr)
		return max_des_i2c_atr_init(priv);
	else
		return max_des_i2c_mux_init(priv);

	return 0;
}

static int max_des_set_tpg_fmt(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *state,
			       struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt = &format->format;
	struct max_des_priv *priv = v4l2_get_subdevdata(sd);
	struct max_des *des = priv->des;
	const struct max_serdes_tpg_entry *entry;
	struct v4l2_fract *in;

	if (format->stream != MAX_SERDES_TPG_STREAM)
		return -EINVAL;

	entry = max_des_find_tpg_entry(des, 0, fmt->width, fmt->height,
				       fmt->code, 0, 0);
	if (!entry)
		return -EINVAL;

	in = v4l2_subdev_state_get_interval(state, format->pad, format->stream);
	if (!in)
		return -EINVAL;

	in->numerator = entry->interval.numerator;
	in->denominator = entry->interval.denominator;

	return 0;
}

static int max_des_set_fmt(struct v4l2_subdev *sd,
			   const struct v4l2_subdev_client_info *ci,
			   struct v4l2_subdev_state *state,
			   struct v4l2_subdev_format *format)
{
	struct max_des_priv *priv = v4l2_get_subdevdata(sd);
	struct max_des *des = priv->des;
	struct v4l2_mbus_framefmt *fmt;
	int ret;

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE && des->active)
		return -EBUSY;

	/* No transcoding, source and sink formats must match. */
	if (max_des_pad_is_source(des, format->pad))
		return v4l2_subdev_get_fmt(sd, state, format);

	if (max_des_pad_is_tpg(des, format->pad)) {
		ret = max_des_set_tpg_fmt(sd, state, format);
		if (ret)
			return ret;
	}

	if (format->format.code == ~0U)
		format->format.code = MEDIA_BUS_FMT_UYVY8_1X16;

	fmt = v4l2_subdev_state_get_format(state, format->pad, format->stream);
	if (!fmt)
		return -EINVAL;

	*fmt = format->format;

	fmt = v4l2_subdev_state_get_opposite_stream_format(state, format->pad,
							   format->stream);
	if (!fmt)
		return -EINVAL;

	*fmt = format->format;

	return 0;
}

static int max_des_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *state,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	struct max_des_priv *priv = v4l2_get_subdevdata(sd);
	struct max_des *des = priv->des;
	const struct max_serdes_tpg_entry *entry;

	if (!max_des_pad_is_tpg(des, fie->pad) ||
	    fie->stream != MAX_SERDES_TPG_STREAM)
		return -ENOTTY;

	entry = max_des_find_tpg_entry(des, fie->index, fie->width, fie->height,
				       fie->code, 0, 0);
	if (!entry)
		return -EINVAL;

	fie->interval.numerator = entry->interval.numerator;
	fie->interval.denominator = entry->interval.denominator;

	return 0;
}

static int max_des_get_frame_interval(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *state,
				      struct v4l2_subdev_frame_interval *fi)
{
	struct max_des_priv *priv = v4l2_get_subdevdata(sd);
	struct max_des *des = priv->des;

	if (!max_des_pad_is_tpg(des, fi->pad) ||
	    fi->stream != MAX_SERDES_TPG_STREAM)
		return -ENOTTY;

	return v4l2_subdev_get_frame_interval(sd, state, fi);
}

static int max_des_set_frame_interval(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *state,
				      struct v4l2_subdev_frame_interval *fi)
{
	struct max_des_priv *priv = v4l2_get_subdevdata(sd);
	struct max_des *des = priv->des;
	const struct max_serdes_tpg_entry *entry;
	struct v4l2_mbus_framefmt *fmt;
	struct v4l2_fract *in;

	if (!max_des_pad_is_tpg(des, fi->pad) ||
	    fi->stream != MAX_SERDES_TPG_STREAM)
		return -ENOTTY;

	if (fi->which == V4L2_SUBDEV_FORMAT_ACTIVE && des->active)
		return -EBUSY;

	fmt = v4l2_subdev_state_get_format(state, fi->pad, fi->stream);
	if (!fmt)
		return -EINVAL;

	entry = max_des_find_tpg_entry(des, 0, fmt->width, fmt->height,
				       fmt->code, fi->interval.numerator,
				       fi->interval.denominator);
	if (!entry)
		return -EINVAL;

	in = v4l2_subdev_state_get_interval(state, fi->pad, fi->stream);
	if (!in)
		return -EINVAL;

	in->numerator = fi->interval.numerator;
	in->denominator = fi->interval.denominator;

	return 0;
}

static int max_des_log_status(struct v4l2_subdev *sd)
{
	struct max_des_priv *priv = v4l2_get_subdevdata(sd);
	struct max_des *des = priv->des;
	struct v4l2_subdev_state *state;
	unsigned int i, j;
	int ret;

	state = v4l2_subdev_lock_and_get_active_state(&priv->sd);

	v4l2_info(sd, "active: %u\n", des->active);
	v4l2_info(sd, "mode: %s", max_serdes_gmsl_mode_str(des->mode));
	if (des->ops->set_tpg) {
		const struct max_serdes_tpg_entry *entry = des->tpg_entry;

		if (entry) {
			v4l2_info(sd, "tpg: %ux%u@%u/%u, code: %u, dt: %u, bpp: %u\n",
				  entry->width, entry->height,
				  entry->interval.numerator,
				  entry->interval.denominator,
				  entry->code, entry->dt,  entry->bpp);
		} else {
			v4l2_info(sd, "tpg: disabled\n");
		}
	}
	if (des->ops->log_status) {
		ret = des->ops->log_status(des);
		if (ret)
			goto out_unlock;
	}
	v4l2_info(sd, "\n");

	for (i = 0; i < des->info->num_links; i++) {
		struct max_des_link *link = &des->links[i];

		v4l2_info(sd, "link: %u\n", link->index);
		v4l2_info(sd, "\tenabled: %u\n", link->enabled);

		if (!link->enabled) {
			v4l2_info(sd, "\n");
			continue;
		}

		v4l2_info(sd, "\tversion: %s\n", max_serdes_gmsl_version_str(link->version));
		v4l2_info(sd, "\tser_xlate: en: %u, src: 0x%02x dst: 0x%02x\n",
			  link->ser_xlate.en, link->ser_xlate.src,
			  link->ser_xlate.dst);
		v4l2_info(sd, "\n");
	}

	for (i = 0; i < des->info->num_pipes; i++) {
		struct max_des_pipe *pipe = &des->pipes[i];

		v4l2_info(sd, "pipe: %u\n", pipe->index);
		v4l2_info(sd, "\tenabled: %u\n", pipe->enabled);
		if (pipe->phy_id == des->info->num_phys ||
		    (priv->unused_phy && pipe->phy_id == priv->unused_phy->index))
			v4l2_info(sd, "\tphy_id: invalid\n");
		else
			v4l2_info(sd, "\tphy_id: %u\n", pipe->phy_id);
		v4l2_info(sd, "\tlink_id: %u\n", pipe->link_id);
		if (des->ops->set_pipe_stream_id)
			v4l2_info(sd, "\tstream_id: %u\n", pipe->stream_id);
		if (des->ops->set_pipe_mode) {
			v4l2_info(sd, "\tdbl8: %u\n", pipe->mode.dbl8);
			v4l2_info(sd, "\tdbl8mode: %u\n", pipe->mode.dbl8mode);
			v4l2_info(sd, "\tdbl10: %u\n", pipe->mode.dbl10);
			v4l2_info(sd, "\tdbl10mode: %u\n", pipe->mode.dbl10mode);
			v4l2_info(sd, "\tdbl12: %u\n", pipe->mode.dbl12);
		}
		if (des->ops->set_pipe_remap) {
			v4l2_info(sd, "\tremaps: %u\n", pipe->num_remaps);
			for (j = 0; j < pipe->num_remaps; j++) {
				struct max_des_remap *remap = &pipe->remaps[j];

				v4l2_info(sd, "\t\tremap: from: vc: %u, dt: 0x%02x\n",
					  remap->from_vc, remap->from_dt);
				v4l2_info(sd, "\t\t       to:   vc: %u, dt: 0x%02x, phy: %u\n",
					  remap->to_vc, remap->to_dt, remap->phy);
			}
		}
		if (des->ops->set_pipe_vc_remap) {
			v4l2_info(sd, "\tvc_remaps: %u\n", pipe->num_vc_remaps);
			for (j = 0; j < pipe->num_vc_remaps; j++) {
				v4l2_info(sd, "\t\tvc_remap: src: %u, dst: %u\n",
					  pipe->vc_remaps[j].src, pipe->vc_remaps[j].dst);
			}
		}
		if (des->ops->log_pipe_status) {
			ret = des->ops->log_pipe_status(des, pipe);
			if (ret)
				goto out_unlock;
		}
		v4l2_info(sd, "\n");
	}

	for (i = 0; i < des->info->num_phys; i++) {
		struct max_des_phy *phy = &des->phys[i];

		v4l2_info(sd, "phy: %u\n", phy->index);
		v4l2_info(sd, "\tenabled: %u\n", phy->enabled);

		if (!phy->enabled) {
			v4l2_info(sd, "\n");
			continue;
		}

		v4l2_info(sd, "\tlink_frequency: %llu\n", phy->link_frequency);
		v4l2_info(sd, "\tnum_data_lanes: %u\n", phy->mipi.num_data_lanes);
		v4l2_info(sd, "\tclock_lane: %u\n", phy->mipi.clock_lane);
		if (des->ops->set_phy_mode) {
			v4l2_info(sd, "\talt_mem_map8: %u\n", phy->mode.alt_mem_map8);
			v4l2_info(sd, "\talt2_mem_map8: %u\n", phy->mode.alt2_mem_map8);
			v4l2_info(sd, "\talt_mem_map10: %u\n", phy->mode.alt_mem_map10);
			v4l2_info(sd, "\talt_mem_map12: %u\n", phy->mode.alt_mem_map12);
		}
		if (des->ops->log_phy_status) {
			ret = des->ops->log_phy_status(des, phy);
			if (ret)
				goto out_unlock;
		}
		v4l2_info(sd, "\n");
	}

	ret = 0;

out_unlock:
	v4l2_subdev_unlock_state(state);

	return ret;
}

static int max_des_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct max_des_priv *priv = ctrl_to_priv(ctrl->handler);
	struct max_des *des = priv->des;

	switch (ctrl->id) {
	case V4L2_CID_TEST_PATTERN:
		des->tpg_pattern = ctrl->val;
		return 0;
	}

	return -EINVAL;
}

static int max_des_get_frame_desc_state(struct v4l2_subdev *sd,
					struct v4l2_subdev_state *state,
					struct v4l2_mbus_frame_desc *fd,
					unsigned int pad)
{
	struct max_des_remap_context context = { 0 };
	struct max_des_priv *priv = sd_to_priv(sd);
	struct v4l2_subdev_route *route;
	int ret;

	fd->type = V4L2_MBUS_FRAME_DESC_TYPE_CSI2;
	fd->num_entries = 0;

	ret = max_des_populate_remap_context(priv, &context, state);
	if (ret)
		return ret;

	for_each_active_route(&state->routing, route) {
		struct max_des_route_hw hw;
		unsigned int dst_vc_id;

		if (pad != route->source_pad)
			continue;

		ret = max_des_route_to_hw(priv, state, route, &hw);
		if (ret)
			return ret;

		ret = max_des_get_src_dst_vc_id(&context, hw.pipe->index, hw.phy->index,
						hw.entry.bus.csi2.vc, &dst_vc_id);
		if (ret)
			return ret;

		hw.entry.bus.csi2.vc = dst_vc_id;
		hw.entry.stream = route->source_stream;

		fd->entry[fd->num_entries++] = hw.entry;
	}

	return 0;
}

static int max_des_get_frame_desc(struct v4l2_subdev *sd, unsigned int pad,
				  struct v4l2_mbus_frame_desc *fd)
{
	struct max_des_priv *priv = sd_to_priv(sd);
	struct v4l2_subdev_state *state;
	int ret;

	state = v4l2_subdev_lock_and_get_active_state(&priv->sd);

	ret = max_des_get_frame_desc_state(sd, state, fd, pad);

	v4l2_subdev_unlock_state(state);

	return ret;
}

static int max_des_get_mbus_config(struct v4l2_subdev *sd, unsigned int pad,
				   struct v4l2_mbus_config *cfg)
{
	struct max_des_priv *priv = sd_to_priv(sd);
	struct max_des *des = priv->des;
	struct max_des_phy *phy;

	phy = max_des_pad_to_phy(des, pad);
	if (!phy)
		return -EINVAL;

	cfg->type = phy->bus_type;
	cfg->bus.mipi_csi2 = phy->mipi;
	cfg->link_freq = phy->link_frequency;

	return 0;
}

static int max_des_set_tpg_routing(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state,
				   struct v4l2_subdev_krouting *routing)
{
	struct max_des_priv *priv = sd_to_priv(sd);
	struct max_des *des = priv->des;
	const struct max_serdes_tpg_entry *entry;
	struct v4l2_mbus_framefmt fmt = { 0 };
	int ret;

	ret = max_serdes_validate_tpg_routing(routing);
	if (ret)
		return ret;

	entry = &des->info->tpg_entries.entries[0];

	fmt.width = entry->width;
	fmt.height = entry->height;
	fmt.code = entry->code;

	return v4l2_subdev_set_routing_with_fmt(sd, state, routing, &fmt);
}

static int __max_des_set_routing(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_krouting *routing)
{
	struct max_des_priv *priv = sd_to_priv(sd);
	struct max_des *des = priv->des;
	struct v4l2_subdev_route *route;
	bool is_tpg = false;
	int ret;

	ret = v4l2_subdev_routing_validate(sd, routing,
					   V4L2_SUBDEV_ROUTING_ONLY_1_TO_1 |
					   V4L2_SUBDEV_ROUTING_NO_SINK_STREAM_MIX);
	if (ret)
		return ret;

	for_each_active_route(routing, route) {
		if (max_des_pad_is_tpg(des, route->sink_pad)) {
			is_tpg = true;
			break;
		}
	}

	if (is_tpg)
		return max_des_set_tpg_routing(sd, state, routing);

	static const struct v4l2_mbus_framefmt format = {
			.code = MEDIA_BUS_FMT_Y8_1X8,
			.field = V4L2_FIELD_NONE,
			.width = 640,
			.height = 480,
		};

	return v4l2_subdev_set_routing_with_fmt(sd, state, routing, &format);
}

static int max_des_set_routing(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *state,
			       enum v4l2_subdev_format_whence which,
			       struct v4l2_subdev_krouting *routing)
{
	struct max_des_priv *priv = sd_to_priv(sd);
	struct max_des *des = priv->des;

	if (which == V4L2_SUBDEV_FORMAT_ACTIVE && des->active)
		return -EBUSY;

	return __max_des_set_routing(sd, state, routing);
}

static int max_des_update_link(struct max_des_priv *priv,
			       struct max_des_remap_context *context,
			       struct max_des_link *link,
			       struct v4l2_subdev_state *state,
			       u64 *streams_masks)
{
	struct max_des *des = priv->des;
	struct max_des_pipe *pipe;
	int ret;

	pipe = max_des_find_link_pipe(des, link);
	if (!pipe)
		return -ENOENT;

	ret = max_des_update_pipe(priv, context, pipe, state, streams_masks);
	if (ret)
		return ret;

	return 0;
}

static int max_des_update_tpg(struct max_des_priv *priv,
			      struct v4l2_subdev_state *state,
			      u64 *streams_masks)
{
	const struct max_serdes_tpg_entry *entry = NULL;
	struct max_des *des = priv->des;
	struct v4l2_subdev_route *route;
	int ret;

	for_each_active_route(&state->routing, route) {
		struct max_des_route_hw hw;

		if (!(BIT_ULL(route->sink_stream) & streams_masks[route->sink_pad]))
			continue;

		ret = max_des_route_to_hw(priv, state, route, &hw);
		if (ret)
			return ret;

		if (!hw.is_tpg)
			continue;

		entry = max_des_find_state_tpg_entry(des, state, route->sink_pad);
		break;
	}

	if (entry == des->tpg_entry)
		return 0;

	ret = des->ops->set_tpg(des, entry);
	if (ret)
		return ret;

	des->tpg_entry = entry;

	return 0;
}

static int max_des_update_active(struct max_des_priv *priv, u64 *streams_masks,
				 bool expected_active)
{
	struct max_des *des = priv->des;
	bool active = false;
	unsigned int i;
	int ret;

	for (i = 0; i < des->info->num_phys; i++) {
		struct max_des_phy *phy = &des->phys[i];
		u32 pad = max_des_phy_to_pad(des, phy);

		if (streams_masks[pad]) {
			active = true;
			break;
		}
	}

	if (active != expected_active || des->active == active)
		return 0;

	if (des->ops->set_enable) {
		ret = des->ops->set_enable(des, active);
		if (ret)
			return ret;
	}

	des->active = active;

	return 0;
}

static int max_des_update_links(struct max_des_priv *priv,
				struct max_des_remap_context *context,
				struct v4l2_subdev_state *state,
				u64 *streams_masks)
{
	struct max_des *des = priv->des;
	unsigned int failed_update_link_id = des->info->num_links;
	unsigned int i;
	int ret;

	for (i = 0; i < des->info->num_links; i++) {
		struct max_des_link *link = &des->links[i];

		ret = max_des_update_link(priv, context, link, state,
					  streams_masks);
		if (ret) {
			failed_update_link_id = i;
			goto err;
		}
	}

	return 0;

err:
	for (i = 0; i < failed_update_link_id; i++) {
		struct max_des_link *link = &des->links[i];

		max_des_update_link(priv, context, link, state,
				    priv->streams_masks);
	}

	return ret;
}

static int max_des_enable_disable_streams(struct max_des_priv *priv,
					  struct v4l2_subdev_state *state,
					  u32 pad, u64 updated_streams_mask,
					  bool enable)
{
	struct max_des *des = priv->des;

	return max_serdes_xlate_enable_disable_streams(priv->sources, 0, state,
						       pad, updated_streams_mask, 0,
						       des->info->num_links, enable);
}

static int max_des_update_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  u32 pad, u64 updated_streams_mask, bool enable)
{
	struct max_des_priv *priv = v4l2_get_subdevdata(sd);
	struct max_des_remap_context context = { 0 };
	struct max_des_mode_context mode_context = { 0 };
	struct max_des *des = priv->des;
	unsigned int num_pads = max_des_num_pads(des);
	u64 *streams_masks;
	int ret;

	ret = max_des_populate_remap_context(priv, &context, state);
	if (ret)
		return ret;

	ret = max_des_populate_mode_context(priv, &mode_context, state, context.mode);
	if (ret)
		return ret;

	ret = max_serdes_get_streams_masks(priv->dev, state, pad, updated_streams_mask,
					   num_pads, priv->streams_masks, &streams_masks,
					   enable);
	if (ret)
		return ret;

	ret = max_des_set_pipes_phy(priv, &context);
	if (ret)
		goto err_free_streams_masks;

	ret = max_des_set_tunnel(priv, &context);
	if (ret)
		goto err_free_streams_masks;

	ret = max_des_set_modes(priv, &mode_context);
	if (ret)
		goto err_free_streams_masks;

	ret = max_des_set_vc_remaps(priv, &context, state, streams_masks);
	if (ret)
		goto err_free_streams_masks;

	ret = max_des_set_pipes_stream_id(priv);
	if (ret)
		goto err_free_streams_masks;

	if (!enable) {
		ret = max_des_enable_disable_streams(priv, state, pad,
						     updated_streams_mask, enable);
		if (ret)
			goto err_free_streams_masks;
	}

	ret = max_des_update_active(priv, streams_masks, false);
	if (ret)
		goto err_revert_streams_disable;

	ret = max_des_update_links(priv, &context, state, streams_masks);
	if (ret)
		goto err_revert_active_disable;

	ret = max_des_update_tpg(priv, state, streams_masks);
	if (ret)
		goto err_revert_links_update;

	ret = max_des_update_active(priv, streams_masks, true);
	if (ret)
		goto err_revert_tpg_update;

	if (enable) {
		ret = max_des_enable_disable_streams(priv, state, pad,
						     updated_streams_mask, enable);
		if (ret)
			goto err_revert_active_enable;
	}

	devm_kfree(priv->dev, priv->streams_masks);
	priv->streams_masks = streams_masks;

	return 0;

err_revert_active_enable:
	max_des_update_active(priv, priv->streams_masks, false);

err_revert_tpg_update:
	max_des_update_tpg(priv, state, priv->streams_masks);

err_revert_links_update:
	max_des_update_links(priv, &context, state, priv->streams_masks);

err_revert_active_disable:
	max_des_update_active(priv, priv->streams_masks, true);

err_revert_streams_disable:
	if (!enable)
		max_des_enable_disable_streams(priv, state, pad,
					       updated_streams_mask, !enable);

err_free_streams_masks:
	devm_kfree(priv->dev, streams_masks);

	return ret;
}

static int max_des_enable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  u32 pad, u64 streams_mask)
{
	return max_des_update_streams(sd, state, pad, streams_mask, true);
}

static int max_des_disable_streams(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state,
				   u32 pad, u64 streams_mask)
{
	return max_des_update_streams(sd, state, pad, streams_mask, false);
}

static int max_des_init_state(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state)
{
	struct v4l2_subdev_route routes[MAX_DES_NUM_LINKS] = { 0 };
	struct v4l2_subdev_krouting routing = {
		.routes = routes,
	};
	struct max_des_priv *priv = v4l2_get_subdevdata(sd);
	struct max_des *des = priv->des;
	struct max_des_phy *phy = NULL;
	unsigned int stream = 0;
	unsigned int i;

	for (i = 0; i < des->info->num_phys; i++) {
		if (des->phys[i].enabled) {
			phy = &des->phys[i];
			break;
		}
	}

	if (!phy)
		return 0;

	for (i = 0; i < des->info->num_links; i++) {
		struct max_des_link *link = &des->links[i];

		if (!link->enabled)
			continue;

		routing.routes[routing.num_routes++] = (struct v4l2_subdev_route) {
			.sink_pad = max_des_link_to_pad(des, link),
			.sink_stream = 0,
			.source_pad = max_des_phy_to_pad(des, phy),
			.source_stream = stream,
			.flags = V4L2_SUBDEV_ROUTE_FL_ACTIVE,
		};
		stream++;

		/*
		 * The Streams API is an experimental feature.
		 * If multiple routes are provided here, userspace will not be
		 * able to configure them unless the Streams API is enabled.
		 * Provide a single route until it is enabled.
		 */
		break;
	}

	return __max_des_set_routing(sd, state, &routing);
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int max_des_g_register(struct v4l2_subdev *sd,
			      struct v4l2_dbg_register *reg)
{
	struct max_des_priv *priv = v4l2_get_subdevdata(sd);
	struct max_des *des = priv->des;
	unsigned int val;
	int ret;

	ret = des->ops->reg_read(des, reg->reg, &val);
	if (ret)
		return ret;

	reg->val = val;
	reg->size = 1;

	return 0;
}

static int max_des_s_register(struct v4l2_subdev *sd,
			      const struct v4l2_dbg_register *reg)
{
	struct max_des_priv *priv = v4l2_get_subdevdata(sd);
	struct max_des *des = priv->des;

	return des->ops->reg_write(des, reg->reg, reg->val);
}
#endif

static const struct v4l2_subdev_core_ops max_des_core_ops = {
	.log_status = max_des_log_status,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = max_des_g_register,
	.s_register = max_des_s_register,
#endif
};

static const struct v4l2_ctrl_ops max_des_ctrl_ops = {
	.s_ctrl = max_des_s_ctrl,
};

static const struct v4l2_subdev_pad_ops max_des_pad_ops = {
	.enable_streams = max_des_enable_streams,
	.disable_streams = max_des_disable_streams,

	.set_routing = max_des_set_routing,
	.get_frame_desc = max_des_get_frame_desc,

	.get_mbus_config = max_des_get_mbus_config,

	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = max_des_set_fmt,

	.enum_frame_interval = max_des_enum_frame_interval,
	.get_frame_interval = max_des_get_frame_interval,
	.set_frame_interval = max_des_set_frame_interval,
};

static const struct v4l2_subdev_ops max_des_subdev_ops = {
	.core = &max_des_core_ops,
	.pad = &max_des_pad_ops,
};

static const struct v4l2_subdev_internal_ops max_des_internal_ops = {
	.init_state = &max_des_init_state,
};

static const struct media_entity_operations max_des_media_ops = {
	.get_fwnode_pad = v4l2_subdev_get_fwnode_pad_1_to_1,
	.has_pad_interdep = v4l2_subdev_has_pad_interdep,
	.link_validate = v4l2_subdev_link_validate,
};

static int max_des_notify_bound(struct v4l2_async_notifier *nf,
				struct v4l2_subdev *subdev,
				struct v4l2_async_connection *base_asc)
{
	struct max_des_priv *priv = nf_to_priv(nf);
	struct max_serdes_asc *asc = asc_to_max(base_asc);
	struct max_serdes_source *source = asc->source;
	struct max_des *des = priv->des;
	struct max_des_link *link = &des->links[source->index];
	u32 pad = max_des_link_to_pad(des, link);
	int ret;

	ret = media_entity_get_fwnode_pad(&subdev->entity,
					  source->ep_fwnode,
					  MEDIA_PAD_FL_SOURCE);
	if (ret < 0) {
		dev_err(priv->dev, "Failed to find pad for %s\n", subdev->name);
		return ret;
	}

	source->sd = subdev;
	source->pad = ret;

	ret = media_create_pad_link(&source->sd->entity, source->pad,
				    &priv->sd.entity, pad,
				    MEDIA_LNK_FL_ENABLED |
				    MEDIA_LNK_FL_IMMUTABLE);
	if (ret) {
		dev_err(priv->dev, "Unable to link %s:%u -> %s:%u\n",
			source->sd->name, source->pad, priv->sd.name, pad);
		source->sd = NULL;
		return ret;
	}

	return 0;
}

static void max_des_notify_unbind(struct v4l2_async_notifier *nf,
				  struct v4l2_subdev *subdev,
				  struct v4l2_async_connection *base_asc)
{
	struct max_serdes_asc *asc = asc_to_max(base_asc);
	struct max_serdes_source *source = asc->source;

	source->sd = NULL;
}

static const struct v4l2_async_notifier_operations max_des_notify_ops = {
	.bound = max_des_notify_bound,
	.unbind = max_des_notify_unbind,
};

static int max_des_v4l2_notifier_register(struct max_des_priv *priv)
{
	struct max_des *des = priv->des;
	unsigned int i;
	int ret;

	v4l2_async_subdev_nf_init(&priv->nf, &priv->sd);

	for (i = 0; i < des->info->num_links; i++) {
		struct max_des_link *link = &des->links[i];
		struct max_serdes_source *source;
		struct max_serdes_asc *asc;

		if (!link->enabled)
			continue;

		source = max_des_get_link_source(priv, link);
		if (!source->ep_fwnode)
			continue;

		asc = v4l2_async_nf_add_fwnode(&priv->nf, source->ep_fwnode,
					       struct max_serdes_asc);
		if (IS_ERR(asc)) {
			dev_err(priv->dev,
				"Failed to add subdev for source %u: %pe", i,
				asc);

			v4l2_async_nf_cleanup(&priv->nf);

			return PTR_ERR(asc);
		}

		asc->source = source;
	}

	priv->nf.ops = &max_des_notify_ops;

	ret = v4l2_async_nf_register(&priv->nf);
	if (ret) {
		dev_err(priv->dev, "Failed to register subdev notifier");
		v4l2_async_nf_cleanup(&priv->nf);
		return ret;
	}

	return 0;
}

static void max_des_v4l2_notifier_unregister(struct max_des_priv *priv)
{
	v4l2_async_nf_unregister(&priv->nf);
	v4l2_async_nf_cleanup(&priv->nf);
}

static int max_des_v4l2_register(struct max_des_priv *priv)
{
	struct v4l2_subdev *sd = &priv->sd;
	struct max_des *des = priv->des;
	void *data = i2c_get_clientdata(priv->client);
	unsigned int num_pads = max_des_num_pads(des);
	unsigned int i;
	int ret;

	v4l2_i2c_subdev_init(sd, priv->client, &max_des_subdev_ops);
	i2c_set_clientdata(priv->client, data);
	sd->internal_ops = &max_des_internal_ops;
	sd->entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	sd->entity.ops = &max_des_media_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_STREAMS;

	for (i = 0; i < num_pads; i++) {
		if (max_des_pad_is_sink(des, i))
			priv->pads[i].flags = MEDIA_PAD_FL_SINK;
		else if (max_des_pad_is_source(des, i))
			priv->pads[i].flags = MEDIA_PAD_FL_SOURCE;
		else if (max_des_pad_is_tpg(des, i))
			priv->pads[i].flags = MEDIA_PAD_FL_SINK |
					      MEDIA_PAD_FL_INTERNAL;
		else
			return -EINVAL;
	}

	v4l2_set_subdevdata(sd, priv);

	if (des->info->tpg_patterns) {
		v4l2_ctrl_handler_init(&priv->ctrl_handler, 1);
		priv->sd.ctrl_handler = &priv->ctrl_handler;

		v4l2_ctrl_new_std_menu_items(&priv->ctrl_handler,
					     &max_des_ctrl_ops,
					     V4L2_CID_TEST_PATTERN,
					     MAX_SERDES_TPG_PATTERN_MAX,
					     ~des->info->tpg_patterns,
					     __ffs(des->info->tpg_patterns),
					     max_serdes_tpg_patterns);
		if (priv->ctrl_handler.error) {
			ret = priv->ctrl_handler.error;
			goto err_free_ctrl;
		}
	}

	ret = media_entity_pads_init(&sd->entity, num_pads, priv->pads);
	if (ret)
		goto err_free_ctrl;

	ret = max_des_v4l2_notifier_register(priv);
	if (ret)
		goto err_media_entity_cleanup;

	ret = v4l2_subdev_init_finalize(sd);
	if (ret)
		goto err_nf_cleanup;

	ret = v4l2_async_register_subdev(sd);
	if (ret)
		goto err_sd_cleanup;

	return 0;

err_sd_cleanup:
	v4l2_subdev_cleanup(sd);
err_nf_cleanup:
	max_des_v4l2_notifier_unregister(priv);
err_media_entity_cleanup:
	media_entity_cleanup(&sd->entity);
err_free_ctrl:
	v4l2_ctrl_handler_free(&priv->ctrl_handler);

	return ret;
}

static void max_des_put_source_ep_fwnodes(struct max_des_priv *priv)
{
	struct max_des *des = priv->des;
	unsigned int i;

	for (i = 0; i < des->info->num_links; i++) {
		fwnode_handle_put(priv->sources[i].ep_fwnode);
		priv->sources[i].ep_fwnode = NULL;
	}
}

static void max_des_v4l2_unregister(struct max_des_priv *priv)
{
	struct v4l2_subdev *sd = &priv->sd;

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	max_des_v4l2_notifier_unregister(priv);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&priv->ctrl_handler);
}

static int max_des_update_pocs(struct max_des_priv *priv, bool enable)
{
	struct max_des *des = priv->des;
	unsigned int i;
	int err = 0;
	int ret;

	for (i = 0; i < des->info->num_links; i++) {
		struct max_des_link *link = &des->links[i];

		if (!link->enabled)
			continue;

		if (!priv->pocs[i])
			continue;

		if (enable)
			ret = regulator_enable(priv->pocs[i]);
		else
			ret = regulator_disable(priv->pocs[i]);

		if (ret) {
			dev_err(priv->dev,
				"Failed to set POC supply to %u: %u\n",
				enable, ret);
			if (!enable) {
				err = err ?: ret;
				continue;
			}
			goto err_rollback;
		}
	}

	return err;

err_rollback:
	while (i--) {
		struct max_des_link *link = &des->links[i];

		if (!link->enabled)
			continue;

		if (!priv->pocs[i])
			continue;

		regulator_disable(priv->pocs[i]);
	}

	return ret;
}

static int max_des_parse_sink_dt_endpoint(struct max_des_priv *priv,
					  struct max_des_link *link,
					  struct max_serdes_source *source,
					  struct fwnode_handle *fwnode)
{
	struct max_des *des = priv->des;
	u32 pad = max_des_link_to_pad(des, link);
	unsigned int index = link->index;
	struct fwnode_handle *ep;
	char poc_name[10];
	int ret;

	ep = fwnode_graph_get_endpoint_by_id(fwnode, pad, 0, 0);
	if (!ep)
		return 0;

	source->ep_fwnode = fwnode_graph_get_remote_endpoint(ep);
	fwnode_handle_put(ep);
	if (!source->ep_fwnode) {
		dev_err(priv->dev,
			"Failed to get remote endpoint on port %u\n", pad);
		return -ENODEV;
	}

	snprintf(poc_name, sizeof(poc_name), "port%u-poc", index);
	priv->pocs[index] = devm_regulator_get_optional(priv->dev, poc_name);
	if (IS_ERR(priv->pocs[index])) {
		ret = PTR_ERR(priv->pocs[index]);
		if (ret != -ENODEV) {
			dev_err(priv->dev,
				"Failed to get POC supply on port %u: %d\n",
				index, ret);
			goto err_put_source_ep_fwnode;
		}

		priv->pocs[index] = NULL;
	}

	link->enabled = true;

	return 0;

err_put_source_ep_fwnode:
	fwnode_handle_put(source->ep_fwnode);
	source->ep_fwnode = NULL;

	return ret;
}

static int max_des_parse_src_dt_endpoint(struct max_des_priv *priv,
					 struct max_des_phy *phy,
					 struct fwnode_handle *fwnode)
{
	struct max_des *des = priv->des;
	u32 pad = max_des_phy_to_pad(des, phy);
	struct v4l2_fwnode_endpoint v4l2_ep = { .bus_type = V4L2_MBUS_UNKNOWN };
	struct v4l2_mbus_config_mipi_csi2 *mipi = &v4l2_ep.bus.mipi_csi2;
	enum v4l2_mbus_type bus_type;
	struct fwnode_handle *ep;
	u64 link_frequency;
	unsigned int i;
	int ret;

	ep = fwnode_graph_get_endpoint_by_id(fwnode, pad, 0, 0);
	if (!ep)
		return 0;

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &v4l2_ep);
	fwnode_handle_put(ep);
	if (ret) {
		dev_err(priv->dev, "Could not parse endpoint on port %u\n", pad);
		return ret;
	}

	bus_type = v4l2_ep.bus_type;
	if (bus_type != V4L2_MBUS_CSI2_DPHY &&
	    bus_type != V4L2_MBUS_CSI2_CPHY) {
		v4l2_fwnode_endpoint_free(&v4l2_ep);
		dev_err(priv->dev, "Unsupported bus-type %u on port %u\n",
			bus_type, pad);
		return -EINVAL;
	}

	if (v4l2_ep.nr_of_link_frequencies == 0) {
		link_frequency = MAX_DES_LINK_FREQUENCY_DEFAULT;
	} else if (v4l2_ep.nr_of_link_frequencies == 1) {
		link_frequency = v4l2_ep.link_frequencies[0];
	} else {
		dev_err(priv->dev, "Invalid link frequencies %u on port %u\n",
			v4l2_ep.nr_of_link_frequencies, pad);
		ret = -EINVAL;
	}

	v4l2_fwnode_endpoint_free(&v4l2_ep);

	if (ret)
		return ret;

	if (link_frequency < MAX_DES_LINK_FREQUENCY_MIN ||
	    link_frequency > MAX_DES_LINK_FREQUENCY_MAX) {
		dev_err(priv->dev, "Invalid link frequency %llu on port %u\n",
			link_frequency, pad);
		return -EINVAL;
	}

	if (!mipi->num_data_lanes) {
		dev_err(priv->dev, "No data lanes on port %u\n", pad);
		return -EINVAL;
	}

	for (i = 0; i < mipi->num_data_lanes; i++) {
		if (mipi->data_lanes[i] > mipi->num_data_lanes) {
			dev_err(priv->dev, "Invalid data lane %u on port %u\n",
				mipi->data_lanes[i], pad);
			return -EINVAL;
		}
	}

	phy->bus_type = bus_type;
	phy->mipi = *mipi;
	phy->link_frequency = link_frequency;
	phy->enabled = true;

	return 0;
}

int max_des_phy_hw_data_lanes(struct max_des *des, struct max_des_phy *phy)
{
	const struct max_serdes_phys_configs *configs = &des->info->phys_configs;
	const struct max_serdes_phys_config *config =
		&configs->configs[des->phys_config];

	return config->lanes[phy->index];
}
EXPORT_SYMBOL_NS_GPL(max_des_phy_hw_data_lanes, "MAX_SERDES");

static int max_des_find_phys_config(struct max_des_priv *priv)
{
	struct max_des *des = priv->des;
	const struct max_serdes_phys_configs *configs = &des->info->phys_configs;
	struct max_des_phy *phy;
	unsigned int i, j;

	if (!configs->num_configs)
		return 0;

	for (i = 0; i < configs->num_configs; i++) {
		const struct max_serdes_phys_config *config = &configs->configs[i];
		bool matching = true;

		for (j = 0; j < des->info->num_phys; j++) {
			phy = &des->phys[j];

			if (!phy->enabled)
				continue;

			if (phy->mipi.num_data_lanes <= config->lanes[j] &&
			    phy->mipi.clock_lane == config->clock_lane[j])
				continue;

			matching = false;

			break;
		}

		if (matching)
			break;
	}

	if (i == configs->num_configs) {
		dev_err(priv->dev, "Invalid lane configuration\n");
		return -EINVAL;
	}

	des->phys_config = i;

	return 0;
}

static int max_des_parse_dt(struct max_des_priv *priv)
{
	struct fwnode_handle *fwnode = dev_fwnode(priv->dev);
	struct max_des *des = priv->des;
	struct max_des_link *link;
	struct max_des_pipe *pipe;
	struct max_des_phy *phy;
	unsigned int i;
	int ret;

	for (i = 0; i < des->info->num_phys; i++) {
		phy = &des->phys[i];
		phy->index = i;

		ret = max_des_parse_src_dt_endpoint(priv, phy, fwnode);
		if (ret)
			return ret;
	}

	ret = max_des_find_phys_config(priv);
	if (ret)
		return ret;

	/*
	 * Find an unused PHY to send unmapped data to.
	 *
	 * Skip PHYs that have no hardware data lanes of their own: those are
	 * absorbed into another PHY by the selected configuration, so sending
	 * unmapped data to them would corrupt the PHY they are merged with.
	 */
	for (i = 0; i < des->info->num_phys; i++) {
		phy = &des->phys[i];

		if (!phy->enabled && max_des_phy_hw_data_lanes(des, phy)) {
			priv->unused_phy = phy;
			break;
		}
	}

	for (i = 0; i < des->info->num_pipes; i++) {
		pipe = &des->pipes[i];
		pipe->index = i;

		/*
		 * Serializers can send data on different stream ids over the
		 * same link, and some deserializers support stream id autoselect
		 * allowing them to receive data from all stream ids.
		 * Deserializers that support that feature should enable it.
		 * Deserializers that support per-link stream ids do not need
		 * to assign unique stream ids to each serializer.
		 */
		if (des->info->needs_unique_stream_id)
			pipe->stream_id = i;
		else
			pipe->stream_id = 0;

		/*
		 * We already checked that num_pipes >= num_links.
		 * Set up pipe to receive data from the link with the same index.
		 * This is already the default for most chips, and some of them
		 * don't even support receiving pipe data from a different link.
		 */
		pipe->link_id = i % des->info->num_links;
	}

	for (i = 0; i < des->info->num_links; i++) {
		link = &des->links[i];
		link->index = i;
	}

	for (i = 0; i < des->info->num_links; i++) {
		struct max_des_link *link = &des->links[i];
		struct max_serdes_source *source;

		source = max_des_get_link_source(priv, link);
		source->index = i;

		ret = max_des_parse_sink_dt_endpoint(priv, link, source, fwnode);
		if (ret)
			return ret;
	}

	return 0;
}

static int max_des_allocate(struct max_des_priv *priv)
{
	struct max_des *des = priv->des;
	unsigned int num_pads = max_des_num_pads(des);

	des->phys = devm_kcalloc(priv->dev, des->info->num_phys,
				 sizeof(*des->phys), GFP_KERNEL);
	if (!des->phys)
		return -ENOMEM;

	des->pipes = devm_kcalloc(priv->dev, des->info->num_pipes,
				  sizeof(*des->pipes), GFP_KERNEL);
	if (!des->pipes)
		return -ENOMEM;

	des->links = devm_kcalloc(priv->dev, des->info->num_links,
				  sizeof(*des->links), GFP_KERNEL);
	if (!des->links)
		return -ENOMEM;

	priv->sources = devm_kcalloc(priv->dev, des->info->num_links,
				     sizeof(*priv->sources), GFP_KERNEL);
	if (!priv->sources)
		return -ENOMEM;

	priv->pocs = devm_kcalloc(priv->dev, des->info->num_links,
				  sizeof(*priv->pocs), GFP_KERNEL);
	if (!priv->pocs)
		return -ENOMEM;

	priv->pads = devm_kcalloc(priv->dev, num_pads,
				  sizeof(*priv->pads), GFP_KERNEL);
	if (!priv->pads)
		return -ENOMEM;

	priv->streams_masks = devm_kcalloc(priv->dev, num_pads,
					   sizeof(*priv->streams_masks),
					   GFP_KERNEL);
	if (!priv->streams_masks)
		return -ENOMEM;

	return 0;
}

int max_des_probe(struct i2c_client *client, struct max_des *des)
{
	struct device *dev = &client->dev;
	struct max_des_priv *priv;
	int ret;

	if (des->info->num_phys > MAX_DES_NUM_PHYS)
		return -E2BIG;

	if (des->info->num_pipes > MAX_DES_NUM_PIPES)
		return -E2BIG;

	if (des->info->num_links > MAX_DES_NUM_LINKS)
		return -E2BIG;

	if (des->info->num_links > des->info->num_pipes)
		return -E2BIG;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	if (!des->info->versions) {
		dev_err(dev, "No GMSL version declared\n");
		return -EINVAL;
	}

	if (des->ops->set_link_version && !des->ops->select_links) {
		dev_err(dev,
			"Cannot implement .set_link_version() without .select_links()\n");
		return -EINVAL;
	}

	if (hweight_long(des->info->versions) > 1 &&
	    !des->ops->set_link_version) {
		dev_err(dev, "Multiple versions without .set_link_version()\n");
		return -EINVAL;
	}

	priv->client = client;
	priv->dev = dev;
	priv->des = des;
	des->priv = priv;

	ret = max_des_allocate(priv);
	if (ret)
		return ret;

	ret = max_des_parse_dt(priv);
	if (ret)
		goto err_put_source_ep_fwnodes;

	ret = max_des_init(priv);
	if (ret)
		goto err_put_source_ep_fwnodes;

	ret = max_des_update_pocs(priv, true);
	if (ret)
		goto err_put_source_ep_fwnodes;

	ret = max_des_i2c_adapter_init(priv);
	if (ret)
		goto err_disable_pocs;

	ret = max_des_v4l2_register(priv);
	if (ret)
		goto err_i2c_adapter_deinit;

	return 0;

err_i2c_adapter_deinit:
	max_des_i2c_adapter_deinit(priv);

err_disable_pocs:
	max_des_update_pocs(priv, false);

err_put_source_ep_fwnodes:
	max_des_put_source_ep_fwnodes(priv);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(max_des_probe, "MAX_SERDES");

int max_des_remove(struct max_des *des)
{
	struct max_des_priv *priv = des->priv;

	max_des_v4l2_unregister(priv);

	max_des_i2c_adapter_deinit(priv);

	max_des_update_pocs(priv, false);

	max_des_put_source_ep_fwnodes(priv);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(max_des_remove, "MAX_SERDES");

MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("I2C_ATR");
