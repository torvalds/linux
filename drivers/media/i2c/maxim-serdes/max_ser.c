// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim GMSL2 Serializer Driver
 *
 * Copyright (C) 2025 Analog Devices Inc.
 */

#include <linux/delay.h>
#include <linux/i2c-atr.h>
#include <linux/i2c-mux.h>
#include <linux/module.h>

#include <media/mipi-csi2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#include "max_ser.h"
#include "max_serdes.h"

#define MAX_SER_NUM_LINKS	1
#define MAX_SER_NUM_PHYS	1

struct max_ser_priv {
	struct max_ser *ser;
	struct device *dev;
	struct i2c_client *client;

	struct i2c_atr *atr;
	struct i2c_mux_core *mux;

	struct media_pad *pads;
	struct max_serdes_source *sources;
	u64 *streams_masks;
	u32 double_bpps;

	struct v4l2_subdev sd;
	struct v4l2_async_notifier nf;
	struct v4l2_ctrl_handler ctrl_handler;
};

struct max_ser_route_hw {
	struct max_serdes_source *source;
	struct max_ser_pipe *pipe;
	struct v4l2_mbus_frame_desc_entry entry;
	bool is_tpg;
};

static inline struct max_ser_priv *sd_to_priv(struct v4l2_subdev *sd)
{
	return container_of(sd, struct max_ser_priv, sd);
}

static inline struct max_ser_priv *nf_to_priv(struct v4l2_async_notifier *nf)
{
	return container_of(nf, struct max_ser_priv, nf);
}

static inline struct max_ser_priv *ctrl_to_priv(struct v4l2_ctrl_handler *handler)
{
	return container_of(handler, struct max_ser_priv, ctrl_handler);
}

static inline bool max_ser_pad_is_sink(struct max_ser *ser, u32 pad)
{
	return pad < ser->ops->num_phys;
}

static inline bool max_ser_pad_is_source(struct max_ser *ser, u32 pad)
{
	return pad >= ser->ops->num_phys &&
	       pad < ser->ops->num_phys + MAX_SER_NUM_LINKS;
}

static inline u32 max_ser_source_pad(struct max_ser *ser)
{
	return ser->ops->num_phys;
}

static inline bool max_ser_pad_is_tpg(struct max_ser *ser, u32 pad)
{
	return pad >= ser->ops->num_phys + MAX_SER_NUM_LINKS;
}

static inline unsigned int max_ser_phy_to_pad(struct max_ser *ser,
					      struct max_ser_phy *phy)
{
	return phy->index;
}

static inline unsigned int max_ser_num_pads(struct max_ser *ser)
{
	return ser->ops->num_phys + MAX_SER_NUM_LINKS;
	/*
	 * TODO: once MEDIA_PAD_FL_INTERNAL is enabled in the uAPI we
	 * can add the TPG pad:
	 *	return ser->ops->num_phys + MAX_SER_NUM_LINKS +
	 *		(ser->ops->set_tpg ? 1 : 0);
	 */
}

static struct max_ser_phy *max_ser_pad_to_phy(struct max_ser *ser, u32 pad)
{
	if (!max_ser_pad_is_sink(ser, pad))
		return NULL;

	return &ser->phys[pad];
}

static struct max_ser_pipe *
max_ser_find_phy_pipe(struct max_ser *ser, struct max_ser_phy *phy)
{
	unsigned int i;

	for (i = 0; i < ser->ops->num_pipes; i++) {
		struct max_ser_pipe *pipe = &ser->pipes[i];

		if (pipe->phy_id == phy->index)
			return pipe;
	}

	return NULL;
}

static struct max_serdes_source *
max_ser_get_phy_source(struct max_ser_priv *priv, struct max_ser_phy *phy)
{
	return &priv->sources[phy->index];
}

static const struct max_serdes_tpg_entry *
max_ser_find_tpg_entry(struct max_ser *ser, u32 target_index,
		       u32 width, u32 height, u32 code,
		       u32 numerator, u32 denominator)
{
	const struct max_serdes_tpg_entry *entry;
	unsigned int index = 0;
	unsigned int i;

	for (i = 0; i < ser->ops->tpg_entries.num_entries; i++) {
		entry = &ser->ops->tpg_entries.entries[i];

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

	if (i == ser->ops->tpg_entries.num_entries)
		return NULL;

	return &ser->ops->tpg_entries.entries[i];
}

static const struct max_serdes_tpg_entry *
max_ser_find_state_tpg_entry(struct max_ser *ser, struct v4l2_subdev_state *state,
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

	return max_ser_find_tpg_entry(ser, 0, fmt->width, fmt->height, fmt->code,
				      in->numerator, in->denominator);
}

static int max_ser_get_tpg_fd_entry_state(struct max_ser *ser,
					  struct v4l2_subdev_state *state,
					  struct v4l2_mbus_frame_desc_entry *fd_entry,
					  unsigned int pad)
{
	const struct max_serdes_tpg_entry *entry;

	entry = max_ser_find_state_tpg_entry(ser, state, pad);
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

static int max_ser_tpg_route_to_hw(struct max_ser_priv *priv,
				   struct v4l2_subdev_state *state,
				   struct v4l2_subdev_route *route,
				   struct max_ser_route_hw *hw)
{
	struct max_ser *ser = priv->ser;

	hw->pipe = &ser->pipes[0];

	return max_ser_get_tpg_fd_entry_state(ser, state, &hw->entry,
					      route->sink_pad);
}

static int max_ser_route_to_hw(struct max_ser_priv *priv,
			       struct v4l2_subdev_state *state,
			       struct v4l2_subdev_route *route,
			       struct max_ser_route_hw *hw)
{
	struct max_ser *ser = priv->ser;
	struct v4l2_mbus_frame_desc fd = {};
	struct max_ser_phy *phy;
	unsigned int i;
	int ret;

	memset(hw, 0, sizeof(*hw));

	hw->is_tpg = max_ser_pad_is_tpg(ser, route->sink_pad);
	if (hw->is_tpg)
		return max_ser_tpg_route_to_hw(priv, state, route, hw);

	phy = max_ser_pad_to_phy(ser, route->sink_pad);
	if (!phy)
		return -ENOENT;

	hw->pipe = max_ser_find_phy_pipe(ser, phy);
	if (!hw->pipe)
		return -ENOENT;

	hw->source = max_ser_get_phy_source(priv, phy);
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

static int max_ser_phy_set_active(struct max_ser *ser, struct max_ser_phy *phy,
				  bool active)
{
	int ret;

	if (ser->ops->set_phy_active) {
		ret = ser->ops->set_phy_active(ser, phy, active);
		if (ret)
			return ret;
	}

	phy->active = active;

	return 0;
}

static int max_ser_set_pipe_dts(struct max_ser_priv *priv, struct max_ser_pipe *pipe,
				unsigned int *dts, unsigned int num_dts)
{
	struct max_ser *ser = priv->ser;
	unsigned int i;
	int ret;

	if (!ser->ops->set_pipe_dt || !ser->ops->set_pipe_dt_en)
		return 0;

	for (i = 0; i < num_dts; i++) {
		ret = ser->ops->set_pipe_dt(ser, pipe, i, dts[i]);
		if (ret)
			return ret;

		ret = ser->ops->set_pipe_dt_en(ser, pipe, i, true);
		if (ret)
			return ret;
	}

	if (num_dts == pipe->num_dts)
		return 0;

	for (i = num_dts; i < ser->ops->num_dts_per_pipe; i++) {
		ret = ser->ops->set_pipe_dt_en(ser, pipe, i, false);
		if (ret)
			return ret;
	}

	return 0;
}

static int max_ser_set_pipe_mode(struct max_ser_priv *priv, struct max_ser_pipe *pipe,
				 struct max_ser_pipe_mode *mode)
{
	struct max_ser *ser = priv->ser;

	if (mode->bpp == pipe->mode.bpp &&
	    mode->soft_bpp == pipe->mode.soft_bpp &&
	    mode->dbl8 == pipe->mode.dbl8 &&
	    mode->dbl10 == pipe->mode.dbl10 &&
	    mode->dbl12 == pipe->mode.dbl12)
		return 0;

	if (!ser->ops->set_pipe_mode)
		return 0;

	return ser->ops->set_pipe_mode(ser, pipe, mode);
}

static int max_ser_i2c_atr_attach_addr(struct i2c_atr *atr, u32 chan_id,
				       u16 addr, u16 alias)
{
	struct max_serdes_i2c_xlate xlate = {
		.src = alias,
		.dst = addr,
		.en = true,
	};
	struct max_ser_priv *priv = i2c_atr_get_driver_data(atr);
	struct max_ser *ser = priv->ser;
	unsigned int i;
	int ret;

	for (i = 0; i < ser->ops->num_i2c_xlates; i++)
		if (!ser->i2c_xlates[i].en)
			break;

	if (i == ser->ops->num_i2c_xlates) {
		dev_err(priv->dev,
			"Reached maximum number of I2C translations\n");
		return -EINVAL;
	}

	ret = ser->ops->set_i2c_xlate(ser, i, &xlate);
	if (ret)
		return ret;

	ser->i2c_xlates[i] = xlate;

	return 0;
}

static void max_ser_i2c_atr_detach_addr(struct i2c_atr *atr, u32 chan_id, u16 addr)
{
	struct max_ser_priv *priv = i2c_atr_get_driver_data(atr);
	struct max_ser *ser = priv->ser;
	struct max_serdes_i2c_xlate xlate = { 0 };
	unsigned int i;

	/* Find index of matching I2C translation. */
	for (i = 0; i < ser->ops->num_i2c_xlates; i++)
		if (ser->i2c_xlates[i].dst == addr)
			break;

	if (WARN_ON(i == ser->ops->num_i2c_xlates))
		return;

	ser->ops->set_i2c_xlate(ser, i, &xlate);
	ser->i2c_xlates[i] = xlate;
}

static const struct i2c_atr_ops max_ser_i2c_atr_ops = {
	.attach_addr = max_ser_i2c_atr_attach_addr,
	.detach_addr = max_ser_i2c_atr_detach_addr,
};

static void max_ser_i2c_atr_deinit(struct max_ser_priv *priv)
{
	/* Deleting adapters that haven't been added does no harm. */
	i2c_atr_del_adapter(priv->atr, 0);

	i2c_atr_delete(priv->atr);
	priv->atr = NULL;
}

static int max_ser_i2c_atr_init(struct max_ser_priv *priv)
{
	struct i2c_atr_adap_desc desc = {
		.chan_id = 0,
	};
	int ret;

	if (!i2c_check_functionality(priv->client->adapter,
				     I2C_FUNC_SMBUS_WRITE_BYTE_DATA))
		return -ENODEV;

	priv->atr = i2c_atr_new(priv->client->adapter, priv->dev,
				&max_ser_i2c_atr_ops, 1, 0);
	if (IS_ERR(priv->atr))
		return PTR_ERR(priv->atr);

	i2c_atr_set_driver_data(priv->atr, priv);

	ret = i2c_atr_add_adapter(priv->atr, &desc);
	if (ret)
		max_ser_i2c_atr_deinit(priv);

	return ret;
}

static int max_ser_i2c_mux_select(struct i2c_mux_core *mux, u32 chan)
{
	return 0;
}

static void max_ser_i2c_mux_deinit(struct max_ser_priv *priv)
{
	i2c_mux_del_adapters(priv->mux);
}

static int max_ser_i2c_mux_init(struct max_ser_priv *priv)
{
	priv->mux = i2c_mux_alloc(priv->client->adapter, &priv->client->dev,
				  1, 0, I2C_MUX_LOCKED | I2C_MUX_GATE,
				  max_ser_i2c_mux_select, NULL);
	if (!priv->mux)
		return -ENOMEM;

	return i2c_mux_add_adapter(priv->mux, 0, 0);
}

static int max_ser_i2c_adapter_init(struct max_ser_priv *priv)
{
	struct fwnode_handle *fwnode;

	fwnode = device_get_named_child_node(priv->dev, "i2c-gate");
	if (fwnode) {
		fwnode_handle_put(fwnode);
		return max_ser_i2c_mux_init(priv);
	}

	return max_ser_i2c_atr_init(priv);
}

static void max_ser_i2c_adapter_deinit(struct max_ser_priv *priv)
{
	if (priv->mux)
		max_ser_i2c_mux_deinit(priv);
	else
		max_ser_i2c_atr_deinit(priv);
}

static int max_ser_set_tpg_fmt(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *state,
			       struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt = &format->format;
	struct max_ser_priv *priv = v4l2_get_subdevdata(sd);
	struct max_ser *ser = priv->ser;
	const struct max_serdes_tpg_entry *entry;
	struct v4l2_fract *in;

	if (format->stream != MAX_SERDES_TPG_STREAM)
		return -EINVAL;

	entry = max_ser_find_tpg_entry(ser, 0, fmt->width, fmt->height,
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

static int max_ser_set_fmt(struct v4l2_subdev *sd,
			   const struct v4l2_subdev_client_info *ci,
			   struct v4l2_subdev_state *state,
			   struct v4l2_subdev_format *format)
{
	struct max_ser_priv *priv = v4l2_get_subdevdata(sd);
	struct max_ser *ser = priv->ser;
	struct v4l2_mbus_framefmt *fmt;
	int ret;

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE && ser->active)
		return -EBUSY;

	/* No transcoding, source and sink formats must match. */
	if (max_ser_pad_is_source(ser, format->pad))
		return v4l2_subdev_get_fmt(sd, state, format);

	if (max_ser_pad_is_tpg(ser, format->pad)) {
		ret = max_ser_set_tpg_fmt(sd, state, format);
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

static int max_ser_log_status(struct v4l2_subdev *sd)
{
	struct max_ser_priv *priv = sd_to_priv(sd);
	struct max_ser *ser = priv->ser;
	struct v4l2_subdev_state *state;
	unsigned int i, j;
	int ret;

	state = v4l2_subdev_lock_and_get_active_state(&priv->sd);

	v4l2_info(sd, "mode: %s\n", max_serdes_gmsl_mode_str(ser->mode));
	if (ser->ops->set_tpg) {
		const struct max_serdes_tpg_entry *entry = ser->tpg_entry;

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
	if (ser->ops->log_status) {
		ret = ser->ops->log_status(ser);
		if (ret)
			goto out_unlock;
	}
	v4l2_info(sd, "i2c_xlates:\n");
	for (i = 0; i < ser->ops->num_i2c_xlates; i++) {
		v4l2_info(sd, "\ten: %u, src: 0x%02x dst: 0x%02x\n",
			  ser->i2c_xlates[i].en, ser->i2c_xlates[i].src,
			  ser->i2c_xlates[i].dst);
		if (!ser->i2c_xlates[i].en)
			break;
	}
	v4l2_info(sd, "\n");
	if (ser->ops->set_vc_remap) {
		v4l2_info(sd, "vc_remaps: %u\n", ser->num_vc_remaps);
		for (j = 0; j < ser->num_vc_remaps; j++) {
			v4l2_info(sd, "\tvc_remap: src: %u, dst: %u\n",
				  ser->vc_remaps[j].src, ser->vc_remaps[j].dst);
		}
	}
	v4l2_info(sd, "\n");

	for (i = 0; i < ser->ops->num_pipes; i++) {
		struct max_ser_pipe *pipe = &ser->pipes[i];

		v4l2_info(sd, "pipe: %u\n", pipe->index);
		v4l2_info(sd, "\tenabled: %u\n", pipe->enabled);

		if (!pipe->enabled) {
			v4l2_info(sd, "\n");
			continue;
		}

		v4l2_info(sd, "\tphy_id: %u\n", pipe->phy_id);
		v4l2_info(sd, "\tstream_id: %u\n", pipe->stream_id);
		if (ser->ops->set_pipe_phy)
			v4l2_info(sd, "\tphy_id: %u\n", pipe->phy_id);
		if (ser->ops->set_pipe_dt) {
			v4l2_info(sd, "\tdts: %u\n", pipe->num_dts);
			for (j = 0; j < pipe->num_dts; j++)
				v4l2_info(sd, "\t\tdt: 0x%02x\n", pipe->dts[j]);
		}
		if (ser->ops->set_pipe_vcs)
			v4l2_info(sd, "\tvcs: 0x%08x\n", pipe->vcs);
		if (ser->ops->set_pipe_mode) {
			v4l2_info(sd, "\tdbl8: %u\n", pipe->mode.dbl8);
			v4l2_info(sd, "\tdbl10: %u\n", pipe->mode.dbl10);
			v4l2_info(sd, "\tdbl12: %u\n", pipe->mode.dbl12);
			v4l2_info(sd, "\tsoft_bpp: %u\n", pipe->mode.soft_bpp);
			v4l2_info(sd, "\tbpp: %u\n", pipe->mode.bpp);
		}
		if (ser->ops->log_pipe_status) {
			ret = ser->ops->log_pipe_status(ser, pipe);
			if (ret)
				goto out_unlock;
		}
		v4l2_info(sd, "\n");
	}

	for (i = 0; i < ser->ops->num_phys; i++) {
		struct max_ser_phy *phy = &ser->phys[i];

		v4l2_info(sd, "phy: %u\n", phy->index);
		v4l2_info(sd, "\tenabled: %u\n", phy->enabled);

		if (!phy->enabled) {
			v4l2_info(sd, "\n");
			continue;
		}

		v4l2_info(sd, "\tactive: %u\n", phy->active);
		v4l2_info(sd, "\tnum_data_lanes: %u\n", phy->mipi.num_data_lanes);
		v4l2_info(sd, "\tclock_lane: %u\n", phy->mipi.clock_lane);
		v4l2_info(sd, "\tnoncontinuous_clock: %u\n",
			  !!(phy->mipi.flags & V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK));
		if (ser->ops->log_phy_status) {
			ret = ser->ops->log_phy_status(ser, phy);
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

static int max_ser_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct max_ser_priv *priv = ctrl_to_priv(ctrl->handler);
	struct max_ser *ser = priv->ser;

	switch (ctrl->id) {
	case V4L2_CID_TEST_PATTERN:
		ser->tpg_pattern = ctrl->val;
		return 0;
	}

	return -EINVAL;
}

static int max_ser_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *state,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	struct max_ser_priv *priv = v4l2_get_subdevdata(sd);
	struct max_ser *ser = priv->ser;
	const struct max_serdes_tpg_entry *entry;

	if (!max_ser_pad_is_tpg(ser, fie->pad) ||
	    fie->stream != MAX_SERDES_TPG_STREAM)
		return -ENOTTY;

	entry = max_ser_find_tpg_entry(ser, fie->index, fie->width, fie->height,
				       fie->code, 0, 0);
	if (!entry)
		return -EINVAL;

	fie->interval.numerator = entry->interval.numerator;
	fie->interval.denominator = entry->interval.denominator;

	return 0;
}

static int max_ser_set_frame_interval(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *state,
				      struct v4l2_subdev_frame_interval *fi)
{
	struct max_ser_priv *priv = v4l2_get_subdevdata(sd);
	struct max_ser *ser = priv->ser;
	const struct max_serdes_tpg_entry *entry;
	struct v4l2_mbus_framefmt *fmt;
	struct v4l2_fract *in;

	if (!max_ser_pad_is_tpg(ser, fi->pad) ||
	    fi->stream != MAX_SERDES_TPG_STREAM)
		return -ENOTTY;

	if (fi->which == V4L2_SUBDEV_FORMAT_ACTIVE && ser->active)
		return -EBUSY;

	fmt = v4l2_subdev_state_get_format(state, fi->pad, fi->stream);
	if (!fmt)
		return -EINVAL;

	entry = max_ser_find_tpg_entry(ser, 0, fmt->width, fmt->height,
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

static int max_ser_get_frame_interval(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *state,
				      struct v4l2_subdev_frame_interval *fi)
{
	struct max_ser_priv *priv = v4l2_get_subdevdata(sd);
	struct max_ser *ser = priv->ser;

	if (!max_ser_pad_is_tpg(ser, fi->pad) ||
	    fi->stream != MAX_SERDES_TPG_STREAM)
		return -ENOTTY;

	return v4l2_subdev_get_frame_interval(sd, state, fi);
}

static int max_ser_get_frame_desc_state(struct v4l2_subdev *sd,
					struct v4l2_subdev_state *state,
					struct v4l2_mbus_frame_desc *fd,
					unsigned int pad)
{
	struct max_ser_priv *priv = sd_to_priv(sd);
	struct max_ser *ser = priv->ser;
	struct v4l2_subdev_route *route;
	int ret;

	if (!max_ser_pad_is_source(ser, pad))
		return -ENOENT;

	fd->type = V4L2_MBUS_FRAME_DESC_TYPE_CSI2;

	for_each_active_route(&state->routing, route) {
		struct max_ser_route_hw hw;

		if (pad != route->source_pad)
			continue;

		ret = max_ser_route_to_hw(priv, state, route, &hw);
		if (ret)
			return ret;

		hw.entry.stream = route->source_stream;

		fd->entry[fd->num_entries++] = hw.entry;
	}

	return 0;
}

static int max_ser_get_frame_desc(struct v4l2_subdev *sd, unsigned int pad,
				  struct v4l2_mbus_frame_desc *fd)
{
	struct max_ser_priv *priv = sd_to_priv(sd);
	struct v4l2_subdev_state *state;
	int ret;

	state = v4l2_subdev_lock_and_get_active_state(&priv->sd);

	ret = max_ser_get_frame_desc_state(sd, state, fd, pad);

	v4l2_subdev_unlock_state(state);

	return ret;
}

static int max_ser_set_tpg_routing(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state,
				   struct v4l2_subdev_krouting *routing)
{
	struct max_ser_priv *priv = sd_to_priv(sd);
	struct max_ser *ser = priv->ser;
	const struct max_serdes_tpg_entry *entry;
	struct v4l2_mbus_framefmt fmt = { 0 };
	int ret;

	ret = max_serdes_validate_tpg_routing(routing);
	if (ret)
		return ret;

	entry = &ser->ops->tpg_entries.entries[0];

	fmt.width = entry->width;
	fmt.height = entry->height;
	fmt.code = entry->code;

	return v4l2_subdev_set_routing_with_fmt(sd, state, routing, &fmt);
}

static int __max_ser_set_routing(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_krouting *routing)
{
	struct max_ser_priv *priv = sd_to_priv(sd);
	struct max_ser *ser = priv->ser;
	struct v4l2_subdev_route *route;
	bool is_tpg = false;
	int ret;

	ret = v4l2_subdev_routing_validate(sd, routing,
					   V4L2_SUBDEV_ROUTING_ONLY_1_TO_1 |
					   V4L2_SUBDEV_ROUTING_NO_SINK_STREAM_MIX);
	if (ret)
		return ret;

	for_each_active_route(routing, route) {
		if (max_ser_pad_is_tpg(ser, route->sink_pad)) {
			is_tpg = true;
			break;
		}
	}

	if (is_tpg)
		return max_ser_set_tpg_routing(sd, state, routing);

	static const struct v4l2_mbus_framefmt format = {
			.code = MEDIA_BUS_FMT_Y8_1X8,
			.field = V4L2_FIELD_NONE,
			.width = 640,
			.height = 480,
		};

	return v4l2_subdev_set_routing_with_fmt(sd, state, routing, &format);
}

static int max_ser_set_routing(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *state,
			       enum v4l2_subdev_format_whence which,
			       struct v4l2_subdev_krouting *routing)
{
	struct max_ser_priv *priv = sd_to_priv(sd);
	struct max_ser *ser = priv->ser;

	if (which == V4L2_SUBDEV_FORMAT_ACTIVE && ser->active)
		return -EBUSY;

	return __max_ser_set_routing(sd, state, routing);
}

static int max_ser_get_pipe_vcs_dts(struct max_ser_priv *priv,
				    struct v4l2_subdev_state *state,
				    struct max_ser_pipe *pipe,
				    unsigned int *vcs,
				    unsigned int *dts, unsigned int *num_dts,
				    u64 *streams_masks)
{
	struct v4l2_subdev_route *route;
	struct max_ser *ser = priv->ser;
	unsigned int i;
	int ret;

	*vcs = 0;
	*num_dts = 0;

	if (ser->mode != MAX_SERDES_GMSL_PIXEL_MODE)
		return 0;

	for_each_active_route(&state->routing, route) {
		struct max_ser_route_hw hw;
		unsigned int vc, dt;

		if (!(BIT_ULL(route->sink_stream) & streams_masks[route->sink_pad]))
			continue;

		ret = max_ser_route_to_hw(priv, state, route, &hw);
		if (ret)
			return ret;

		if (hw.pipe != pipe)
			continue;

		vc = hw.entry.bus.csi2.vc;
		dt = hw.entry.bus.csi2.dt;

		if (vc >= MAX_SERDES_VC_ID_NUM)
			return -E2BIG;

		*vcs |= BIT(vc);

		/* Skip already added DT. */
		for (i = 0; i < *num_dts; i++)
			if (dts[i] == dt)
				break;

		if (i < *num_dts)
			continue;

		if (*num_dts >= ser->ops->num_dts_per_pipe)
			return -EINVAL;

		dts[*num_dts] = dt;
		(*num_dts)++;
	}

	/*
	 * Hardware cannot distinguish between different pairs of VC and DT,
	 * issue a warning.
	 */
	for_each_active_route(&state->routing, route) {
		struct max_ser_route_hw hw;
		unsigned int vc, dt;

		/*
		 * Skip enabled streams, we only want to check for leaks
		 * among the disabled streams.
		 */
		if ((BIT_ULL(route->sink_stream) & streams_masks[route->sink_pad]))
			continue;

		ret = max_ser_route_to_hw(priv, state, route, &hw);
		if (ret)
			return ret;

		if (hw.pipe != pipe)
			continue;

		vc = hw.entry.bus.csi2.vc;
		dt = hw.entry.bus.csi2.dt;

		if (vc >= MAX_SERDES_VC_ID_NUM)
			return -E2BIG;

		if (!(*vcs & BIT(vc)))
			continue;

		for (i = 0; i < *num_dts; i++)
			if (dts[i] == dt)
				break;

		if (i == *num_dts)
			continue;

		dev_warn(priv->dev, "Leaked disabled stream %u:%u with VC: %u, DT: %u",
			 route->source_pad, route->source_stream, vc, dt);
	}

	return 0;
}

static int max_ser_get_pipe_mode(struct max_ser_priv *priv,
				 struct v4l2_subdev_state *state,
				 struct max_ser_pipe *pipe,
				 struct max_ser_pipe_mode *mode,
				 u64 *streams_masks)
{
	struct v4l2_subdev_route *route;
	struct max_ser *ser = priv->ser;
	bool force_set_bpp = false;
	unsigned int doubled_bpp = 0;
	unsigned int min_bpp;
	unsigned int max_bpp;
	u32 bpps = 0;
	int ret;

	if (ser->mode != MAX_SERDES_GMSL_PIXEL_MODE)
		return 0;

	for_each_active_route(&state->routing, route) {
		struct max_ser_route_hw hw;
		unsigned int bpp;

		if (!(BIT_ULL(route->sink_stream) & streams_masks[route->sink_pad]))
			continue;

		ret = max_ser_route_to_hw(priv, state, route, &hw);
		if (ret)
			return ret;

		if (hw.pipe != pipe)
			continue;

		if (hw.is_tpg)
			force_set_bpp = true;

		ret = max_serdes_get_fd_bpp(&hw.entry, &bpp);
		if (ret)
			return ret;

		bpps |= BIT(bpp);
	}

	ret = max_serdes_process_bpps(priv->dev, bpps, priv->double_bpps, &doubled_bpp);
	if (ret)
		return ret;

	if (doubled_bpp == 8)
		mode->dbl8 = true;
	else if (doubled_bpp == 10)
		mode->dbl10 = true;
	else if (doubled_bpp == 12)
		mode->dbl12 = true;

	if (doubled_bpp) {
		bpps &= ~BIT(doubled_bpp);
		bpps |= BIT(doubled_bpp * 2);
	}

	if (!bpps)
		return 0;

	min_bpp = __ffs(bpps);
	max_bpp = __fls(bpps);

	if (doubled_bpp)
		mode->soft_bpp = min_bpp;

	if (min_bpp != max_bpp || force_set_bpp)
		mode->bpp = max_bpp;

	return 0;
}

static int max_ser_update_pipe_enable(struct max_ser_priv *priv,
				      struct max_ser_pipe *pipe,
				      struct v4l2_subdev_state *state,
				      u64 *streams_masks)
{
	struct max_ser *ser = priv->ser;
	struct v4l2_subdev_route *route;
	bool enable = false;
	int ret;

	for_each_active_route(&state->routing, route) {
		struct max_ser_route_hw hw;

		if (!(BIT_ULL(route->sink_stream) & streams_masks[route->sink_pad]))
			continue;

		ret = max_ser_route_to_hw(priv, state, route, &hw);
		if (ret)
			return ret;

		if (hw.pipe != pipe)
			continue;

		enable = true;
		break;
	}

	if (enable == pipe->enabled)
		return 0;

	ret = ser->ops->set_pipe_enable(ser, pipe, enable);
	if (ret)
		return ret;

	pipe->enabled = enable;

	return 0;
}

static int max_ser_update_pipe(struct max_ser_priv *priv,
			       struct max_ser_pipe *pipe,
			       struct v4l2_subdev_state *state,
			       u64 *streams_masks)
{
	struct max_ser *ser = priv->ser;
	struct max_ser_pipe_mode mode = { 0 };
	unsigned int num_dts;
	unsigned int *dts;
	unsigned int vcs;
	int ret;

	if (!ser->ops->num_dts_per_pipe)
		return 0;

	dts = devm_kcalloc(priv->dev, ser->ops->num_dts_per_pipe, sizeof(*dts),
			   GFP_KERNEL);
	if (!dts)
		return -ENOMEM;

	ret = max_ser_get_pipe_vcs_dts(priv, state, pipe, &vcs, dts, &num_dts,
				       streams_masks);
	if (ret)
		goto err_free_dts;

	ret = max_ser_get_pipe_mode(priv, state, pipe, &mode, streams_masks);
	if (ret)
		goto err_free_dts;

	if (ser->ops->set_pipe_vcs) {
		ret = ser->ops->set_pipe_vcs(ser, pipe, vcs);
		if (ret)
			goto err_free_dts;
	}

	ret = max_ser_set_pipe_mode(priv, pipe, &mode);
	if (ret)
		goto err_revert_vcs;

	ret = max_ser_set_pipe_dts(priv, pipe, dts, num_dts);
	if (ret)
		goto err_revert_mode;

	pipe->vcs = vcs;
	pipe->mode = mode;

	if (pipe->dts)
		devm_kfree(priv->dev, pipe->dts);

	pipe->dts = dts;
	pipe->num_dts = num_dts;

	return 0;

err_revert_mode:
	if (ser->ops->set_pipe_mode)
		ser->ops->set_pipe_mode(ser, pipe, &pipe->mode);

err_revert_vcs:
	if (ser->ops->set_pipe_vcs)
		ser->ops->set_pipe_vcs(ser, pipe, pipe->vcs);

err_free_dts:
	devm_kfree(priv->dev, dts);

	return ret;
}

static int max_ser_update_phy(struct max_ser_priv *priv,
			      struct v4l2_subdev_state *state,
			      struct max_ser_phy *phy, u64 *streams_masks,
			      u64 *old_streams_masks)
{
	struct max_ser *ser = priv->ser;
	u32 pad = max_ser_phy_to_pad(ser, phy);
	bool enable_changed = !streams_masks[pad] != !old_streams_masks[pad];
	bool enable = !!streams_masks[pad];
	struct max_ser_pipe *pipe;
	int ret;

	pipe = max_ser_find_phy_pipe(ser, phy);
	if (!pipe)
		return -ENOENT;

	if (!enable && enable_changed) {
		ret = max_ser_phy_set_active(ser, phy, enable);
		if (ret)
			return ret;
	}

	ret = max_ser_update_pipe(priv, pipe, state, streams_masks);
	if (ret)
		goto err_revert_phy_disable;

	ret = max_ser_update_pipe_enable(priv, pipe, state, streams_masks);
	if (ret)
		goto err_revert_pipe_update;

	if (enable && enable_changed) {
		ret = max_ser_phy_set_active(ser, phy, enable);
		if (ret)
			goto err_revert_update_pipe_enable;
	}

	return 0;

err_revert_update_pipe_enable:
	max_ser_update_pipe_enable(priv, pipe, state, priv->streams_masks);

err_revert_pipe_update:
	max_ser_update_pipe(priv, pipe, state, priv->streams_masks);

err_revert_phy_disable:
	if (!enable && enable_changed)
		max_ser_phy_set_active(ser, phy, !enable);

	return ret;
}

static int max_ser_update_phys(struct max_ser_priv *priv,
			       struct v4l2_subdev_state *state,
			       u64 *streams_masks, u64 *old_streams_masks)
{
	struct max_ser *ser = priv->ser;
	unsigned int failed_update_phy_id = ser->ops->num_phys;
	unsigned int i;
	int ret;

	for (i = 0; i < ser->ops->num_phys; i++) {
		struct max_ser_phy *phy = &ser->phys[i];

		ret = max_ser_update_phy(priv, state, phy, streams_masks,
					 old_streams_masks);
		if (ret) {
			failed_update_phy_id = i;
			goto err;
		}
	}

	return 0;

err:
	for (i = 0; i < failed_update_phy_id; i++) {
		struct max_ser_phy *phy = &ser->phys[i];

		max_ser_update_phy(priv, state, phy, old_streams_masks,
				   streams_masks);
	}

	return ret;
}

static int max_ser_enable_disable_streams(struct max_ser_priv *priv,
					  struct v4l2_subdev_state *state,
					  u32 pad, u64 updated_streams_mask,
					  bool enable)
{
	struct max_ser *ser = priv->ser;

	return max_serdes_xlate_enable_disable_streams(priv->sources, 0, state,
						       pad, updated_streams_mask, 0,
						       ser->ops->num_phys, enable);
}

static bool max_ser_is_tpg_routed(struct max_ser_priv *priv,
				  struct v4l2_subdev_state *state)
{
	struct v4l2_subdev_route *route;
	int ret;

	for_each_active_route(&state->routing, route) {
		struct max_ser_route_hw hw;

		ret = max_ser_route_to_hw(priv, state, route, &hw);
		if (ret)
			return false;

		if (hw.is_tpg)
			return true;
	}

	return false;
}

static int max_ser_update_tpg(struct max_ser_priv *priv,
			      struct v4l2_subdev_state *state,
			      u64 *streams_masks)
{
	const struct max_serdes_tpg_entry *entry = NULL;
	struct max_ser *ser = priv->ser;
	struct v4l2_subdev_route *route;
	int ret;

	for_each_active_route(&state->routing, route) {
		struct max_ser_route_hw hw;

		if (!(BIT_ULL(route->sink_stream) & streams_masks[route->sink_pad]))
			continue;

		ret = max_ser_route_to_hw(priv, state, route, &hw);
		if (ret)
			return ret;

		if (!hw.is_tpg)
			continue;

		entry = max_ser_find_state_tpg_entry(ser, state, route->sink_pad);
		break;
	}

	if (entry == ser->tpg_entry)
		return 0;

	ret = ser->ops->set_tpg(ser, entry);
	if (ret)
		return ret;

	ser->tpg_entry = entry;

	return 0;
}

static int max_ser_update_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  u32 pad, u64 updated_streams_mask, bool enable)
{
	struct max_ser_priv *priv = v4l2_get_subdevdata(sd);
	struct max_ser *ser = priv->ser;
	unsigned int num_pads = max_ser_num_pads(ser);
	u64 *streams_masks;
	int ret;

	ret = max_serdes_get_streams_masks(priv->dev, state, pad, updated_streams_mask,
					   num_pads, priv->streams_masks, &streams_masks,
					   enable);
	if (ret)
		return ret;

	if (!enable) {
		ret = max_ser_enable_disable_streams(priv, state, pad,
						     updated_streams_mask, enable);
		if (ret)
			goto err_free_streams_masks;
	}

	ret = max_ser_update_tpg(priv, state, streams_masks);
	if (ret)
		goto err_revert_streams_disable;

	ret = max_ser_update_phys(priv, state, streams_masks,
				  priv->streams_masks);
	if (ret)
		goto err_revert_update_tpg;

	if (enable) {
		ret = max_ser_enable_disable_streams(priv, state, pad,
						     updated_streams_mask, enable);
		if (ret)
			goto err_revert_phys_update;
	}

	devm_kfree(priv->dev, priv->streams_masks);
	priv->streams_masks = streams_masks;
	ser->active = !!streams_masks[pad];

	return 0;

err_revert_phys_update:
	max_ser_update_phys(priv, state, priv->streams_masks, streams_masks);

err_revert_update_tpg:
	max_ser_update_tpg(priv, state, priv->streams_masks);

err_revert_streams_disable:
	if (!enable)
		max_ser_enable_disable_streams(priv, state, pad,
					       updated_streams_mask, !enable);

err_free_streams_masks:
	devm_kfree(priv->dev, streams_masks);

	return ret;
}

static int max_ser_enable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  u32 pad, u64 streams_mask)
{
	return max_ser_update_streams(sd, state, pad, streams_mask, true);
}

static int max_ser_disable_streams(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state,
				   u32 pad, u64 streams_mask)
{
	return max_ser_update_streams(sd, state, pad, streams_mask, false);
}

static int max_ser_init_state(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state)
{
	struct v4l2_subdev_route routes[MAX_SER_NUM_PHYS] = { 0 };
	struct v4l2_subdev_krouting routing = {
		.routes = routes,
	};
	struct max_ser_priv *priv = v4l2_get_subdevdata(sd);
	struct max_ser *ser = priv->ser;
	unsigned int stream = 0;
	unsigned int i;

	for (i = 0; i < ser->ops->num_phys; i++) {
		struct max_ser_phy *phy = &ser->phys[i];

		if (!phy->enabled)
			continue;

		routing.routes[routing.num_routes++] = (struct v4l2_subdev_route) {
			.sink_pad = max_ser_phy_to_pad(ser, phy),
			.sink_stream = 0,
			.source_pad = max_ser_source_pad(ser),
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

	return __max_ser_set_routing(sd, state, &routing);
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int max_ser_g_register(struct v4l2_subdev *sd,
			      struct v4l2_dbg_register *reg)
{
	struct max_ser_priv *priv = sd_to_priv(sd);
	struct max_ser *ser = priv->ser;
	unsigned int val;
	int ret;

	ret = ser->ops->reg_read(ser, reg->reg, &val);
	if (ret)
		return ret;

	reg->val = val;
	reg->size = 1;

	return 0;
}

static int max_ser_s_register(struct v4l2_subdev *sd,
			      const struct v4l2_dbg_register *reg)
{
	struct max_ser_priv *priv = sd_to_priv(sd);
	struct max_ser *ser = priv->ser;

	return ser->ops->reg_write(ser, reg->reg, reg->val);
}
#endif

static const struct v4l2_subdev_core_ops max_ser_core_ops = {
	.log_status = max_ser_log_status,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = max_ser_g_register,
	.s_register = max_ser_s_register,
#endif
};

static const struct v4l2_ctrl_ops max_ser_ctrl_ops = {
	.s_ctrl = max_ser_s_ctrl,
};

static const struct v4l2_subdev_pad_ops max_ser_pad_ops = {
	.enable_streams = max_ser_enable_streams,
	.disable_streams = max_ser_disable_streams,

	.set_routing = max_ser_set_routing,
	.get_frame_desc = max_ser_get_frame_desc,

	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = max_ser_set_fmt,

	.enum_frame_interval = max_ser_enum_frame_interval,
	.get_frame_interval = max_ser_get_frame_interval,
	.set_frame_interval = max_ser_set_frame_interval,
};

static const struct v4l2_subdev_ops max_ser_subdev_ops = {
	.core = &max_ser_core_ops,
	.pad = &max_ser_pad_ops,
};

static const struct v4l2_subdev_internal_ops max_ser_internal_ops = {
	.init_state = &max_ser_init_state,
};

static const struct media_entity_operations max_ser_media_ops = {
	.get_fwnode_pad = v4l2_subdev_get_fwnode_pad_1_to_1,
	.has_pad_interdep = v4l2_subdev_has_pad_interdep,
	.link_validate = v4l2_subdev_link_validate,
};

static int max_ser_init(struct max_ser_priv *priv)
{
	struct max_ser *ser = priv->ser;
	unsigned int i;
	int ret;

	if (ser->ops->init) {
		ret = ser->ops->init(ser);
		if (ret)
			return ret;
	}

	if (ser->ops->set_tunnel_enable) {
		ret = ser->ops->set_tunnel_enable(ser, false);
		if (ret)
			return ret;
	}

	for (i = 0; i < ser->ops->num_phys; i++) {
		struct max_ser_phy *phy = &ser->phys[i];

		if (phy->enabled) {
			ret = ser->ops->init_phy(ser, phy);
			if (ret)
				return ret;
		}

		if (ser->ops->set_phy_active) {
			ret = ser->ops->set_phy_active(ser, phy, false);
			if (ret)
				return ret;
		}
	}

	for (i = 0; i < ser->ops->num_pipes; i++) {
		struct max_ser_pipe *pipe = &ser->pipes[i];
		struct max_ser_phy *phy = &ser->phys[pipe->phy_id];

		ret = ser->ops->set_pipe_enable(ser, pipe, false);
		if (ret)
			return ret;

		if (ser->ops->set_pipe_stream_id) {
			ret = ser->ops->set_pipe_stream_id(ser, pipe, pipe->stream_id);
			if (ret)
				return ret;
		}

		if (ser->ops->set_pipe_phy) {
			ret = ser->ops->set_pipe_phy(ser, pipe, phy);
			if (ret)
				return ret;
		}

		if (ser->ops->set_pipe_vcs) {
			ret = ser->ops->set_pipe_vcs(ser, pipe, 0);
			if (ret)
				return ret;
		}

		if (ser->ops->set_pipe_mode) {
			ret = ser->ops->set_pipe_mode(ser, pipe, &pipe->mode);
			if (ret)
				return ret;
		}

		ret = max_ser_set_pipe_dts(priv, pipe, NULL, 0);
		if (ret)
			return ret;
	}

	return 0;
}

static int max_ser_notify_bound(struct v4l2_async_notifier *nf,
				struct v4l2_subdev *subdev,
				struct v4l2_async_connection *base_asc)
{
	struct max_ser_priv *priv = nf_to_priv(nf);
	struct max_serdes_asc *asc = asc_to_max(base_asc);
	struct max_serdes_source *source = asc->source;
	u32 pad = source->index;
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

static void max_ser_notify_unbind(struct v4l2_async_notifier *nf,
				  struct v4l2_subdev *subdev,
				  struct v4l2_async_connection *base_asc)
{
	struct max_serdes_asc *asc = asc_to_max(base_asc);
	struct max_serdes_source *source = asc->source;

	source->sd = NULL;
}

static const struct v4l2_async_notifier_operations max_ser_notify_ops = {
	.bound = max_ser_notify_bound,
	.unbind = max_ser_notify_unbind,
};

static int max_ser_v4l2_notifier_register(struct max_ser_priv *priv)
{
	struct max_ser *ser = priv->ser;
	unsigned int i;
	int ret;

	v4l2_async_subdev_nf_init(&priv->nf, &priv->sd);

	for (i = 0; i < ser->ops->num_phys; i++) {
		struct max_ser_phy *phy = &ser->phys[i];
		struct max_serdes_source *source;
		struct max_serdes_asc *asc;

		source = max_ser_get_phy_source(priv, phy);
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

	priv->nf.ops = &max_ser_notify_ops;

	ret = v4l2_async_nf_register(&priv->nf);
	if (ret) {
		dev_err(priv->dev, "Failed to register subdev notifier");
		v4l2_async_nf_cleanup(&priv->nf);
		return ret;
	}

	return 0;
}

static void max_ser_v4l2_notifier_unregister(struct max_ser_priv *priv)
{
	v4l2_async_nf_unregister(&priv->nf);
	v4l2_async_nf_cleanup(&priv->nf);
}

static int max_ser_v4l2_register(struct max_ser_priv *priv)
{
	struct v4l2_subdev *sd = &priv->sd;
	struct max_ser *ser = priv->ser;
	void *data = i2c_get_clientdata(priv->client);
	unsigned int num_pads = max_ser_num_pads(ser);
	unsigned int i;
	int ret;

	v4l2_i2c_subdev_init(sd, priv->client, &max_ser_subdev_ops);
	i2c_set_clientdata(priv->client, data);
	sd->internal_ops = &max_ser_internal_ops;
	sd->entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	sd->entity.ops = &max_ser_media_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_STREAMS;

	priv->pads = devm_kcalloc(priv->dev, num_pads, sizeof(*priv->pads), GFP_KERNEL);
	if (!priv->pads)
		return -ENOMEM;

	for (i = 0; i < num_pads; i++) {
		if (max_ser_pad_is_sink(ser, i))
			priv->pads[i].flags = MEDIA_PAD_FL_SINK;
		else if (max_ser_pad_is_source(ser, i))
			priv->pads[i].flags = MEDIA_PAD_FL_SOURCE;
		else if (max_ser_pad_is_tpg(ser, i))
			priv->pads[i].flags = MEDIA_PAD_FL_SINK |
					      MEDIA_PAD_FL_INTERNAL;
		else
			return -EINVAL;
	}

	v4l2_set_subdevdata(sd, priv);

	if (ser->ops->tpg_patterns) {
		v4l2_ctrl_handler_init(&priv->ctrl_handler, 1);
		priv->sd.ctrl_handler = &priv->ctrl_handler;

		v4l2_ctrl_new_std_menu_items(&priv->ctrl_handler,
					     &max_ser_ctrl_ops,
					     V4L2_CID_TEST_PATTERN,
					     MAX_SERDES_TPG_PATTERN_MAX,
					     ~ser->ops->tpg_patterns,
					     __ffs(ser->ops->tpg_patterns),
					     max_serdes_tpg_patterns);
		if (priv->ctrl_handler.error) {
			ret = priv->ctrl_handler.error;
			goto err_free_ctrl;
		}
	}

	ret = media_entity_pads_init(&sd->entity, num_pads, priv->pads);
	if (ret)
		goto err_free_ctrl;

	ret = max_ser_v4l2_notifier_register(priv);
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
	max_ser_v4l2_notifier_unregister(priv);
err_media_entity_cleanup:
	media_entity_cleanup(&sd->entity);
err_free_ctrl:
	v4l2_ctrl_handler_free(&priv->ctrl_handler);

	return ret;
}

static void max_ser_put_source_ep_fwnodes(struct max_ser_priv *priv)
{
	struct max_ser *ser = priv->ser;
	unsigned int i;

	for (i = 0; i < ser->ops->num_phys; i++) {
		fwnode_handle_put(priv->sources[i].ep_fwnode);
		priv->sources[i].ep_fwnode = NULL;
	}
}

static void max_ser_v4l2_unregister(struct max_ser_priv *priv)
{
	struct v4l2_subdev *sd = &priv->sd;

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	max_ser_v4l2_notifier_unregister(priv);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&priv->ctrl_handler);
}

static int max_ser_parse_sink_dt_endpoint(struct max_ser_priv *priv,
					  struct max_ser_phy *phy,
					  struct max_serdes_source *source,
					  struct fwnode_handle *fwnode)
{
	struct max_ser *ser = priv->ser;
	u32 pad = max_ser_phy_to_pad(ser, phy);
	struct v4l2_fwnode_endpoint v4l2_ep = { .bus_type = V4L2_MBUS_CSI2_DPHY };
	struct fwnode_handle *ep;
	int ret;

	ep = fwnode_graph_get_endpoint_by_id(fwnode, pad, 0, 0);
	if (!ep)
		return 0;

	source->ep_fwnode = fwnode_graph_get_remote_endpoint(ep);
	if (!source->ep_fwnode) {
		fwnode_handle_put(ep);
		dev_err(priv->dev,
			"Failed to get remote endpoint on port %u\n", pad);
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_parse(ep, &v4l2_ep);
	fwnode_handle_put(ep);
	if (ret) {
		dev_err(priv->dev, "Could not parse endpoint on port %u\n", pad);
		return ret;
	}

	if (!v4l2_ep.bus.mipi_csi2.num_data_lanes) {
		dev_err(priv->dev, "No data lanes on port %u\n", pad);
		return -EINVAL;
	}

	phy->mipi = v4l2_ep.bus.mipi_csi2;
	phy->enabled = true;

	return 0;
}

static int max_ser_find_phys_config(struct max_ser_priv *priv)
{
	struct max_ser *ser = priv->ser;
	const struct max_serdes_phys_configs *configs = &ser->ops->phys_configs;
	struct max_ser_phy *phy;
	unsigned int i, j;

	if (!configs->num_configs)
		return 0;

	for (i = 0; i < configs->num_configs; i++) {
		const struct max_serdes_phys_config *config = &configs->configs[i];
		bool matching = true;

		for (j = 0; j < ser->ops->num_phys; j++) {
			phy = &ser->phys[j];

			if (!phy->enabled)
				continue;

			if (phy->mipi.num_data_lanes <= config->lanes[j])
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

	ser->phys_config = i;

	return 0;
}

static int max_ser_parse_dt(struct max_ser_priv *priv)
{
	struct fwnode_handle *fwnode = dev_fwnode(priv->dev);
	struct max_ser *ser = priv->ser;
	struct max_ser_pipe *pipe;
	struct max_ser_phy *phy;
	unsigned int i;
	int ret;

	for (i = 0; i < ser->ops->num_phys; i++) {
		phy = &ser->phys[i];
		phy->index = i;
	}

	for (i = 0; i < ser->ops->num_pipes; i++) {
		pipe = &ser->pipes[i];
		pipe->index = i;
		pipe->phy_id = i % ser->ops->num_phys;
		pipe->stream_id = i % MAX_SERDES_STREAMS_NUM;
	}

	for (i = 0; i < ser->ops->num_phys; i++) {
		struct max_ser_phy *phy = &ser->phys[i];
		struct max_serdes_source *source;

		source = max_ser_get_phy_source(priv, phy);
		source->index = i;

		ret = max_ser_parse_sink_dt_endpoint(priv, phy, source, fwnode);
		if (ret)
			return ret;
	}

	return max_ser_find_phys_config(priv);
}

static int max_ser_allocate(struct max_ser_priv *priv)
{
	struct max_ser *ser = priv->ser;
	unsigned int num_pads = max_ser_num_pads(ser);

	ser->phys = devm_kcalloc(priv->dev, ser->ops->num_phys,
				 sizeof(*ser->phys), GFP_KERNEL);
	if (!ser->phys)
		return -ENOMEM;

	ser->pipes = devm_kcalloc(priv->dev, ser->ops->num_pipes,
				  sizeof(*ser->pipes), GFP_KERNEL);
	if (!ser->pipes)
		return -ENOMEM;

	ser->vc_remaps = devm_kcalloc(priv->dev, ser->ops->num_vc_remaps,
				      sizeof(*ser->vc_remaps), GFP_KERNEL);
	if (!ser->vc_remaps)
		return -ENOMEM;

	ser->i2c_xlates = devm_kcalloc(priv->dev, ser->ops->num_i2c_xlates,
				       sizeof(*ser->i2c_xlates), GFP_KERNEL);
	if (!ser->i2c_xlates)
		return -ENOMEM;

	priv->sources = devm_kcalloc(priv->dev, ser->ops->num_phys,
				     sizeof(*priv->sources), GFP_KERNEL);
	if (!priv->sources)
		return -ENOMEM;

	priv->streams_masks = devm_kcalloc(priv->dev, num_pads,
					   sizeof(*priv->streams_masks),
					   GFP_KERNEL);
	if (!priv->streams_masks)
		return -ENOMEM;

	return 0;
}

int max_ser_probe(struct i2c_client *client, struct max_ser *ser)
{
	struct device *dev = &client->dev;
	struct max_ser_priv *priv;
	int ret;

	if (ser->ops->num_phys > MAX_SER_NUM_PHYS)
		return -E2BIG;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = client;
	priv->dev = dev;
	priv->ser = ser;
	ser->priv = priv;
	ser->mode = __ffs(ser->ops->modes);

	ret = max_ser_allocate(priv);
	if (ret)
		return ret;

	ret = max_ser_parse_dt(priv);
	if (ret)
		goto err_put_source_ep_fwnodes;

	ret = max_ser_init(priv);
	if (ret)
		goto err_put_source_ep_fwnodes;

	ret = max_ser_i2c_adapter_init(priv);
	if (ret)
		goto err_put_source_ep_fwnodes;

	ret = max_ser_v4l2_register(priv);
	if (ret)
		goto err_i2c_adapter_deinit;

	return 0;

err_i2c_adapter_deinit:
	max_ser_i2c_adapter_deinit(priv);
err_put_source_ep_fwnodes:
	max_ser_put_source_ep_fwnodes(priv);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(max_ser_probe, "MAX_SERDES");

int max_ser_remove(struct max_ser *ser)
{
	struct max_ser_priv *priv = ser->priv;

	max_ser_v4l2_unregister(priv);

	max_ser_i2c_adapter_deinit(priv);

	max_ser_put_source_ep_fwnodes(priv);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(max_ser_remove, "MAX_SERDES");

int max_ser_set_double_bpps(struct v4l2_subdev *sd, u32 double_bpps)
{
	struct max_ser_priv *priv = sd_to_priv(sd);

	priv->double_bpps = double_bpps;

	return 0;
}

int max_ser_set_stream_id(struct v4l2_subdev *sd, unsigned int stream_id)
{
	struct max_ser_priv *priv = sd_to_priv(sd);
	struct max_ser *ser = priv->ser;
	struct max_ser_pipe *pipe = &ser->pipes[0];

	if (!ser->ops->set_pipe_stream_id)
		return -EOPNOTSUPP;

	return ser->ops->set_pipe_stream_id(ser, pipe, stream_id);
}

int max_ser_get_stream_id(struct v4l2_subdev *sd, unsigned int *stream_id)
{
	struct max_ser_priv *priv = sd_to_priv(sd);
	struct max_ser *ser = priv->ser;
	struct max_ser_pipe *pipe = &ser->pipes[0];

	if (!ser->ops->get_pipe_stream_id)
		return -EOPNOTSUPP;

	*stream_id = ser->ops->get_pipe_stream_id(ser, pipe);

	return 0;
}

unsigned int max_ser_get_supported_modes(struct v4l2_subdev *sd)
{
	struct max_ser_priv *priv = sd_to_priv(sd);
	struct max_ser *ser = priv->ser;
	struct v4l2_subdev_state *state;
	unsigned int modes = ser->ops->modes;

	state = v4l2_subdev_lock_and_get_active_state(&priv->sd);

	if (max_ser_is_tpg_routed(priv, state))
		modes = BIT(ser->ops->tpg_mode);

	v4l2_subdev_unlock_state(state);

	return modes;
}

bool max_ser_supports_vc_remap(struct v4l2_subdev *sd)
{
	struct max_ser_priv *priv = sd_to_priv(sd);
	struct max_ser *ser = priv->ser;

	return !!ser->ops->set_vc_remap;
}

int max_ser_set_mode(struct v4l2_subdev *sd, enum max_serdes_gmsl_mode mode)
{
	struct max_ser_priv *priv = sd_to_priv(sd);
	struct max_ser *ser = priv->ser;
	int ret;

	if (!(ser->ops->modes & BIT(mode)))
		return -EINVAL;

	if (ser->mode == mode)
		return 0;

	if (ser->ops->set_tunnel_enable) {
		bool tunnel_enable = mode == MAX_SERDES_GMSL_TUNNEL_MODE;

		ret = ser->ops->set_tunnel_enable(ser, tunnel_enable);
		if (ret)
			return ret;
	}

	ser->mode = mode;

	return 0;
}

int max_ser_set_vc_remaps(struct v4l2_subdev *sd,
			  struct max_serdes_vc_remap *vc_remaps,
			  int num_vc_remaps)
{
	struct max_ser_priv *priv = sd_to_priv(sd);
	struct max_ser *ser = priv->ser;
	unsigned int mask = 0;
	unsigned int i;
	int ret;

	if (!ser->ops->set_vc_remap)
		return -EOPNOTSUPP;

	if (num_vc_remaps > ser->ops->num_vc_remaps)
		return -E2BIG;

	for (i = 0; i < num_vc_remaps; i++) {
		ret = ser->ops->set_vc_remap(ser, i, &vc_remaps[i]);
		if (ret)
			return ret;

		mask |= BIT(i);
	}

	ret = ser->ops->set_vc_remaps_enable(ser, mask);
	if (ret)
		return ret;

	for (i = 0; i < num_vc_remaps; i++)
		ser->vc_remaps[i] = vc_remaps[i];

	ser->num_vc_remaps = num_vc_remaps;

	return 0;
}

static int max_ser_read_reg(struct i2c_adapter *adapter, u8 addr,
			    u16 reg, u8 *val)
{
	u8 buf[2] = { reg >> 8, reg & 0xff };
	struct i2c_msg msg[2] = {
		{
			.addr = addr,
			.flags = 0,
			.buf = buf,
			.len = sizeof(buf),
		},
		{
			.addr = addr,
			.flags = I2C_M_RD,
			.buf = buf,
			.len = 1,
		},
	};
	int ret;

	ret = i2c_transfer(adapter, msg, ARRAY_SIZE(msg));
	if (ret < 0)
		return ret;

	*val = buf[0];

	return 0;
}

static int max_ser_write_reg(struct i2c_adapter *adapter, u8 addr,
			     u16 reg, u8 val)
{
	u8 buf[3] = { reg >> 8, reg & 0xff, val };
	struct i2c_msg msg[1] = {
		{
			.addr = addr,
			.flags = 0,
			.buf = buf,
			.len = sizeof(buf),
		},
	};
	int ret;

	ret = i2c_transfer(adapter, msg, ARRAY_SIZE(msg));
	if (ret < 0)
		return ret;

	return 0;
}

int max_ser_reset(struct i2c_adapter *adapter, u8 addr)
{
	int ret;
	u8 val;

	ret = max_ser_read_reg(adapter, addr, MAX_SER_CTRL0, &val);
	if (ret)
		return ret;

	val |= MAX_SER_CTRL0_RESET_ALL;

	return max_ser_write_reg(adapter, addr, MAX_SER_CTRL0, val);
}

int max_ser_wait_for_multiple(struct i2c_adapter *adapter, u8 *addrs,
			      unsigned int num_addrs, u8 *current_addr)
{
	unsigned int i, j;
	int ret = 0;
	u8 val;

	for (i = 0; i < 10; i++) {
		for (j = 0; j < num_addrs; j++) {
			ret = max_ser_read_reg(adapter, addrs[j], MAX_SER_REG0, &val);
			if (!ret && val) {
				*current_addr = addrs[j];
				return 0;
			}

			msleep(100);
		}
	}

	return ret ?: -ETIMEDOUT;
}

int max_ser_wait(struct i2c_adapter *adapter, u8 addr)
{
	u8 current_addr;

	return max_ser_wait_for_multiple(adapter, &addr, 1, &current_addr);
}

int max_ser_fix_tx_ids(struct i2c_adapter *adapter, u8 addr)
{
	unsigned int addr_regs[] = {
		MAX_SER_CFGI_INFOFR_TR3,
		MAX_SER_CFGL_SPI_TR3,
		MAX_SER_CFGC_CC_TR3,
		MAX_SER_CFGC_GPIO_TR3,
		MAX_SER_CFGL_IIC_X_TR3,
		MAX_SER_CFGL_IIC_Y_TR3,
	};
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(addr_regs); i++) {
		ret = max_ser_write_reg(adapter, addr, addr_regs[i], addr);
		if (ret)
			return ret;
	}

	return 0;
}

int max_ser_change_address(struct i2c_adapter *adapter, u8 addr, u8 new_addr)
{
	u8 val = FIELD_PREP(MAX_SER_REG0_DEV_ADDR, new_addr);

	return max_ser_write_reg(adapter, addr, MAX_SER_REG0, val);
}

MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("I2C_ATR");
