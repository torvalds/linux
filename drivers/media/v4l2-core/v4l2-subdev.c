// SPDX-License-Identifier: GPL-2.0-only
/*
 * V4L2 sub-device
 *
 * Copyright (C) 2010 Nokia Corporation
 *
 * Contact: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	    Sakari Ailus <sakari.ailus@iki.fi>
 */

#include <linux/export.h>
#include <linux/ioctl.h>
#include <linux/leds.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/videodev2.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>

#if defined(CONFIG_VIDEO_V4L2_SUBDEV_API)
/*
 * The Streams API is an experimental feature. To use the Streams API, set
 * 'v4l2_subdev_enable_streams_api' to 1 below.
 */

static bool v4l2_subdev_enable_streams_api;
#endif

/*
 * Maximum stream ID is 63 for now, as we use u64 bitmask to represent a set
 * of streams.
 *
 * Note that V4L2_FRAME_DESC_ENTRY_MAX is related: V4L2_FRAME_DESC_ENTRY_MAX
 * restricts the total number of streams in a pad, although the stream ID is
 * not restricted.
 */
#define V4L2_SUBDEV_MAX_STREAM_ID 63

#include "v4l2-subdev-priv.h"

#if defined(CONFIG_VIDEO_V4L2_SUBDEV_API)
static int subdev_fh_init(struct v4l2_subdev_fh *fh, struct v4l2_subdev *sd)
{
	struct v4l2_subdev_state *state;
	static struct lock_class_key key;

	state = __v4l2_subdev_state_alloc(sd, "fh->state->lock", &key);
	if (IS_ERR(state))
		return PTR_ERR(state);

	fh->state = state;

	return 0;
}

static void subdev_fh_free(struct v4l2_subdev_fh *fh)
{
	__v4l2_subdev_state_free(fh->state);
	fh->state = NULL;
}

static int subdev_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct v4l2_subdev *sd = vdev_to_v4l2_subdev(vdev);
	struct v4l2_subdev_fh *subdev_fh;
	int ret;

	subdev_fh = kzalloc(sizeof(*subdev_fh), GFP_KERNEL);
	if (subdev_fh == NULL)
		return -ENOMEM;

	ret = subdev_fh_init(subdev_fh, sd);
	if (ret) {
		kfree(subdev_fh);
		return ret;
	}

	v4l2_fh_init(&subdev_fh->vfh, vdev);
	v4l2_fh_add(&subdev_fh->vfh);
	file->private_data = &subdev_fh->vfh;

	if (sd->v4l2_dev->mdev && sd->entity.graph_obj.mdev->dev) {
		struct module *owner;

		owner = sd->entity.graph_obj.mdev->dev->driver->owner;
		if (!try_module_get(owner)) {
			ret = -EBUSY;
			goto err;
		}
		subdev_fh->owner = owner;
	}

	if (sd->internal_ops && sd->internal_ops->open) {
		ret = sd->internal_ops->open(sd, subdev_fh);
		if (ret < 0)
			goto err;
	}

	return 0;

err:
	module_put(subdev_fh->owner);
	v4l2_fh_del(&subdev_fh->vfh);
	v4l2_fh_exit(&subdev_fh->vfh);
	subdev_fh_free(subdev_fh);
	kfree(subdev_fh);

	return ret;
}

static int subdev_close(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct v4l2_subdev *sd = vdev_to_v4l2_subdev(vdev);
	struct v4l2_fh *vfh = file->private_data;
	struct v4l2_subdev_fh *subdev_fh = to_v4l2_subdev_fh(vfh);

	if (sd->internal_ops && sd->internal_ops->close)
		sd->internal_ops->close(sd, subdev_fh);
	module_put(subdev_fh->owner);
	v4l2_fh_del(vfh);
	v4l2_fh_exit(vfh);
	subdev_fh_free(subdev_fh);
	kfree(subdev_fh);
	file->private_data = NULL;

	return 0;
}
#else /* CONFIG_VIDEO_V4L2_SUBDEV_API */
static int subdev_open(struct file *file)
{
	return -ENODEV;
}

static int subdev_close(struct file *file)
{
	return -ENODEV;
}
#endif /* CONFIG_VIDEO_V4L2_SUBDEV_API */

static inline int check_which(u32 which)
{
	if (which != V4L2_SUBDEV_FORMAT_TRY &&
	    which != V4L2_SUBDEV_FORMAT_ACTIVE)
		return -EINVAL;

	return 0;
}

static inline int check_pad(struct v4l2_subdev *sd, u32 pad)
{
#if defined(CONFIG_MEDIA_CONTROLLER)
	if (sd->entity.num_pads) {
		if (pad >= sd->entity.num_pads)
			return -EINVAL;
		return 0;
	}
#endif
	/* allow pad 0 on subdevices not registered as media entities */
	if (pad > 0)
		return -EINVAL;
	return 0;
}

static int check_state(struct v4l2_subdev *sd, struct v4l2_subdev_state *state,
		       u32 which, u32 pad, u32 stream)
{
	if (sd->flags & V4L2_SUBDEV_FL_STREAMS) {
#if defined(CONFIG_VIDEO_V4L2_SUBDEV_API)
		if (!v4l2_subdev_state_get_format(state, pad, stream))
			return -EINVAL;
		return 0;
#else
		return -EINVAL;
#endif
	}

	if (stream != 0)
		return -EINVAL;

	if (which == V4L2_SUBDEV_FORMAT_TRY && (!state || !state->pads))
		return -EINVAL;

	return 0;
}

static inline int check_format(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *state,
			       struct v4l2_subdev_format *format)
{
	if (!format)
		return -EINVAL;

	return check_which(format->which) ? : check_pad(sd, format->pad) ? :
	       check_state(sd, state, format->which, format->pad, format->stream);
}

static int call_get_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *state,
			struct v4l2_subdev_format *format)
{
	return check_format(sd, state, format) ? :
	       sd->ops->pad->get_fmt(sd, state, format);
}

static int call_set_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *state,
			struct v4l2_subdev_format *format)
{
	return check_format(sd, state, format) ? :
	       sd->ops->pad->set_fmt(sd, state, format);
}

static int call_enum_mbus_code(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *state,
			       struct v4l2_subdev_mbus_code_enum *code)
{
	if (!code)
		return -EINVAL;

	return check_which(code->which) ? : check_pad(sd, code->pad) ? :
	       check_state(sd, state, code->which, code->pad, code->stream) ? :
	       sd->ops->pad->enum_mbus_code(sd, state, code);
}

static int call_enum_frame_size(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_frame_size_enum *fse)
{
	if (!fse)
		return -EINVAL;

	return check_which(fse->which) ? : check_pad(sd, fse->pad) ? :
	       check_state(sd, state, fse->which, fse->pad, fse->stream) ? :
	       sd->ops->pad->enum_frame_size(sd, state, fse);
}

static int call_enum_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *state,
				    struct v4l2_subdev_frame_interval_enum *fie)
{
	if (!fie)
		return -EINVAL;

	return check_which(fie->which) ? : check_pad(sd, fie->pad) ? :
	       check_state(sd, state, fie->which, fie->pad, fie->stream) ? :
	       sd->ops->pad->enum_frame_interval(sd, state, fie);
}

static inline int check_selection(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_selection *sel)
{
	if (!sel)
		return -EINVAL;

	return check_which(sel->which) ? : check_pad(sd, sel->pad) ? :
	       check_state(sd, state, sel->which, sel->pad, sel->stream);
}

static int call_get_selection(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state,
			      struct v4l2_subdev_selection *sel)
{
	return check_selection(sd, state, sel) ? :
	       sd->ops->pad->get_selection(sd, state, sel);
}

static int call_set_selection(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state,
			      struct v4l2_subdev_selection *sel)
{
	return check_selection(sd, state, sel) ? :
	       sd->ops->pad->set_selection(sd, state, sel);
}

static inline int check_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *state,
				       struct v4l2_subdev_frame_interval *fi)
{
	if (!fi)
		return -EINVAL;

	return check_which(fi->which) ? : check_pad(sd, fi->pad) ? :
	       check_state(sd, state, fi->which, fi->pad, fi->stream);
}

static int call_get_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state,
				   struct v4l2_subdev_frame_interval *fi)
{
	return check_frame_interval(sd, state, fi) ? :
	       sd->ops->pad->get_frame_interval(sd, state, fi);
}

static int call_set_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state,
				   struct v4l2_subdev_frame_interval *fi)
{
	return check_frame_interval(sd, state, fi) ? :
	       sd->ops->pad->set_frame_interval(sd, state, fi);
}

static int call_get_frame_desc(struct v4l2_subdev *sd, unsigned int pad,
			       struct v4l2_mbus_frame_desc *fd)
{
	unsigned int i;
	int ret;

	memset(fd, 0, sizeof(*fd));

	ret = sd->ops->pad->get_frame_desc(sd, pad, fd);
	if (ret)
		return ret;

	dev_dbg(sd->dev, "Frame descriptor on pad %u, type %s\n", pad,
		fd->type == V4L2_MBUS_FRAME_DESC_TYPE_PARALLEL ? "parallel" :
		fd->type == V4L2_MBUS_FRAME_DESC_TYPE_CSI2 ? "CSI-2" :
		"unknown");

	for (i = 0; i < fd->num_entries; i++) {
		struct v4l2_mbus_frame_desc_entry *entry = &fd->entry[i];
		char buf[20] = "";

		if (fd->type == V4L2_MBUS_FRAME_DESC_TYPE_CSI2)
			WARN_ON(snprintf(buf, sizeof(buf),
					 ", vc %u, dt 0x%02x",
					 entry->bus.csi2.vc,
					 entry->bus.csi2.dt) >= sizeof(buf));

		dev_dbg(sd->dev,
			"\tstream %u, code 0x%04x, length %u, flags 0x%04x%s\n",
			entry->stream, entry->pixelcode, entry->length,
			entry->flags, buf);
	}

	return 0;
}

static inline int check_edid(struct v4l2_subdev *sd,
			     struct v4l2_subdev_edid *edid)
{
	if (!edid)
		return -EINVAL;

	if (edid->blocks && edid->edid == NULL)
		return -EINVAL;

	return check_pad(sd, edid->pad);
}

static int call_get_edid(struct v4l2_subdev *sd, struct v4l2_subdev_edid *edid)
{
	return check_edid(sd, edid) ? : sd->ops->pad->get_edid(sd, edid);
}

static int call_set_edid(struct v4l2_subdev *sd, struct v4l2_subdev_edid *edid)
{
	return check_edid(sd, edid) ? : sd->ops->pad->set_edid(sd, edid);
}

static int call_s_dv_timings(struct v4l2_subdev *sd, unsigned int pad,
			     struct v4l2_dv_timings *timings)
{
	if (!timings)
		return -EINVAL;

	return check_pad(sd, pad) ? :
	       sd->ops->pad->s_dv_timings(sd, pad, timings);
}

static int call_g_dv_timings(struct v4l2_subdev *sd, unsigned int pad,
			     struct v4l2_dv_timings *timings)
{
	if (!timings)
		return -EINVAL;

	return check_pad(sd, pad) ? :
	       sd->ops->pad->g_dv_timings(sd, pad, timings);
}

static int call_query_dv_timings(struct v4l2_subdev *sd, unsigned int pad,
				 struct v4l2_dv_timings *timings)
{
	if (!timings)
		return -EINVAL;

	return check_pad(sd, pad) ? :
	       sd->ops->pad->query_dv_timings(sd, pad, timings);
}

static int call_dv_timings_cap(struct v4l2_subdev *sd,
			       struct v4l2_dv_timings_cap *cap)
{
	if (!cap)
		return -EINVAL;

	return check_pad(sd, cap->pad) ? :
	       sd->ops->pad->dv_timings_cap(sd, cap);
}

static int call_enum_dv_timings(struct v4l2_subdev *sd,
				struct v4l2_enum_dv_timings *dvt)
{
	if (!dvt)
		return -EINVAL;

	return check_pad(sd, dvt->pad) ? :
	       sd->ops->pad->enum_dv_timings(sd, dvt);
}

static int call_get_mbus_config(struct v4l2_subdev *sd, unsigned int pad,
				struct v4l2_mbus_config *config)
{
	return check_pad(sd, pad) ? :
	       sd->ops->pad->get_mbus_config(sd, pad, config);
}

static int call_s_stream(struct v4l2_subdev *sd, int enable)
{
	int ret;

	/*
	 * The .s_stream() operation must never be called to start or stop an
	 * already started or stopped subdev. Catch offenders but don't return
	 * an error yet to avoid regressions.
	 *
	 * As .s_stream() is mutually exclusive with the .enable_streams() and
	 * .disable_streams() operation, we can use the enabled_streams field
	 * to store the subdev streaming state.
	 */
	if (WARN_ON(!!sd->enabled_streams == !!enable))
		return 0;

	ret = sd->ops->video->s_stream(sd, enable);

	if (!enable && ret < 0) {
		dev_warn(sd->dev, "disabling streaming failed (%d)\n", ret);
		ret = 0;
	}

	if (!ret) {
		sd->enabled_streams = enable ? BIT(0) : 0;

#if IS_REACHABLE(CONFIG_LEDS_CLASS)
		if (!IS_ERR_OR_NULL(sd->privacy_led)) {
			if (enable)
				led_set_brightness(sd->privacy_led,
						   sd->privacy_led->max_brightness);
			else
				led_set_brightness(sd->privacy_led, 0);
		}
#endif
	}

	return ret;
}

#ifdef CONFIG_MEDIA_CONTROLLER
/*
 * Create state-management wrapper for pad ops dealing with subdev state. The
 * wrapper handles the case where the caller does not provide the called
 * subdev's state. This should be removed when all the callers are fixed.
 */
#define DEFINE_STATE_WRAPPER(f, arg_type)                                  \
	static int call_##f##_state(struct v4l2_subdev *sd,                \
				    struct v4l2_subdev_state *_state,      \
				    arg_type *arg)                         \
	{                                                                  \
		struct v4l2_subdev_state *state = _state;                  \
		int ret;                                                   \
		if (!_state)                                               \
			state = v4l2_subdev_lock_and_get_active_state(sd); \
		ret = call_##f(sd, state, arg);                            \
		if (!_state && state)                                      \
			v4l2_subdev_unlock_state(state);                   \
		return ret;                                                \
	}

#else /* CONFIG_MEDIA_CONTROLLER */

#define DEFINE_STATE_WRAPPER(f, arg_type)                            \
	static int call_##f##_state(struct v4l2_subdev *sd,          \
				    struct v4l2_subdev_state *state, \
				    arg_type *arg)                   \
	{                                                            \
		return call_##f(sd, state, arg);                     \
	}

#endif /* CONFIG_MEDIA_CONTROLLER */

DEFINE_STATE_WRAPPER(get_fmt, struct v4l2_subdev_format);
DEFINE_STATE_WRAPPER(set_fmt, struct v4l2_subdev_format);
DEFINE_STATE_WRAPPER(enum_mbus_code, struct v4l2_subdev_mbus_code_enum);
DEFINE_STATE_WRAPPER(enum_frame_size, struct v4l2_subdev_frame_size_enum);
DEFINE_STATE_WRAPPER(enum_frame_interval, struct v4l2_subdev_frame_interval_enum);
DEFINE_STATE_WRAPPER(get_selection, struct v4l2_subdev_selection);
DEFINE_STATE_WRAPPER(set_selection, struct v4l2_subdev_selection);

static const struct v4l2_subdev_pad_ops v4l2_subdev_call_pad_wrappers = {
	.get_fmt		= call_get_fmt_state,
	.set_fmt		= call_set_fmt_state,
	.enum_mbus_code		= call_enum_mbus_code_state,
	.enum_frame_size	= call_enum_frame_size_state,
	.enum_frame_interval	= call_enum_frame_interval_state,
	.get_selection		= call_get_selection_state,
	.set_selection		= call_set_selection_state,
	.get_frame_interval	= call_get_frame_interval,
	.set_frame_interval	= call_set_frame_interval,
	.get_edid		= call_get_edid,
	.set_edid		= call_set_edid,
	.s_dv_timings		= call_s_dv_timings,
	.g_dv_timings		= call_g_dv_timings,
	.query_dv_timings	= call_query_dv_timings,
	.dv_timings_cap		= call_dv_timings_cap,
	.enum_dv_timings	= call_enum_dv_timings,
	.get_frame_desc		= call_get_frame_desc,
	.get_mbus_config	= call_get_mbus_config,
};

static const struct v4l2_subdev_video_ops v4l2_subdev_call_video_wrappers = {
	.s_stream		= call_s_stream,
};

const struct v4l2_subdev_ops v4l2_subdev_call_wrappers = {
	.pad	= &v4l2_subdev_call_pad_wrappers,
	.video	= &v4l2_subdev_call_video_wrappers,
};
EXPORT_SYMBOL(v4l2_subdev_call_wrappers);

#if defined(CONFIG_VIDEO_V4L2_SUBDEV_API)

static struct v4l2_subdev_state *
subdev_ioctl_get_state(struct v4l2_subdev *sd, struct v4l2_subdev_fh *subdev_fh,
		       unsigned int cmd, void *arg)
{
	u32 which;

	switch (cmd) {
	default:
		return NULL;
	case VIDIOC_SUBDEV_G_FMT:
	case VIDIOC_SUBDEV_S_FMT:
		which = ((struct v4l2_subdev_format *)arg)->which;
		break;
	case VIDIOC_SUBDEV_G_CROP:
	case VIDIOC_SUBDEV_S_CROP:
		which = ((struct v4l2_subdev_crop *)arg)->which;
		break;
	case VIDIOC_SUBDEV_ENUM_MBUS_CODE:
		which = ((struct v4l2_subdev_mbus_code_enum *)arg)->which;
		break;
	case VIDIOC_SUBDEV_ENUM_FRAME_SIZE:
		which = ((struct v4l2_subdev_frame_size_enum *)arg)->which;
		break;
	case VIDIOC_SUBDEV_ENUM_FRAME_INTERVAL:
		which = ((struct v4l2_subdev_frame_interval_enum *)arg)->which;
		break;
	case VIDIOC_SUBDEV_G_SELECTION:
	case VIDIOC_SUBDEV_S_SELECTION:
		which = ((struct v4l2_subdev_selection *)arg)->which;
		break;
	case VIDIOC_SUBDEV_G_FRAME_INTERVAL:
	case VIDIOC_SUBDEV_S_FRAME_INTERVAL: {
		struct v4l2_subdev_frame_interval *fi = arg;

		if (!(subdev_fh->client_caps &
		      V4L2_SUBDEV_CLIENT_CAP_INTERVAL_USES_WHICH))
			fi->which = V4L2_SUBDEV_FORMAT_ACTIVE;

		which = fi->which;
		break;
	}
	case VIDIOC_SUBDEV_G_ROUTING:
	case VIDIOC_SUBDEV_S_ROUTING:
		which = ((struct v4l2_subdev_routing *)arg)->which;
		break;
	}

	return which == V4L2_SUBDEV_FORMAT_TRY ?
			     subdev_fh->state :
			     v4l2_subdev_get_unlocked_active_state(sd);
}

static long subdev_do_ioctl(struct file *file, unsigned int cmd, void *arg,
			    struct v4l2_subdev_state *state)
{
	struct video_device *vdev = video_devdata(file);
	struct v4l2_subdev *sd = vdev_to_v4l2_subdev(vdev);
	struct v4l2_fh *vfh = file->private_data;
	struct v4l2_subdev_fh *subdev_fh = to_v4l2_subdev_fh(vfh);
	bool ro_subdev = test_bit(V4L2_FL_SUBDEV_RO_DEVNODE, &vdev->flags);
	bool streams_subdev = sd->flags & V4L2_SUBDEV_FL_STREAMS;
	bool client_supports_streams = subdev_fh->client_caps &
				       V4L2_SUBDEV_CLIENT_CAP_STREAMS;
	int rval;

	/*
	 * If the streams API is not enabled, remove V4L2_SUBDEV_CAP_STREAMS.
	 * Remove this when the API is no longer experimental.
	 */
	if (!v4l2_subdev_enable_streams_api)
		streams_subdev = false;

	switch (cmd) {
	case VIDIOC_SUBDEV_QUERYCAP: {
		struct v4l2_subdev_capability *cap = arg;

		memset(cap->reserved, 0, sizeof(cap->reserved));
		cap->version = LINUX_VERSION_CODE;
		cap->capabilities =
			(ro_subdev ? V4L2_SUBDEV_CAP_RO_SUBDEV : 0) |
			(streams_subdev ? V4L2_SUBDEV_CAP_STREAMS : 0);

		return 0;
	}

	case VIDIOC_QUERYCTRL:
		/*
		 * TODO: this really should be folded into v4l2_queryctrl (this
		 * currently returns -EINVAL for NULL control handlers).
		 * However, v4l2_queryctrl() is still called directly by
		 * drivers as well and until that has been addressed I believe
		 * it is safer to do the check here. The same is true for the
		 * other control ioctls below.
		 */
		if (!vfh->ctrl_handler)
			return -ENOTTY;
		return v4l2_queryctrl(vfh->ctrl_handler, arg);

	case VIDIOC_QUERY_EXT_CTRL:
		if (!vfh->ctrl_handler)
			return -ENOTTY;
		return v4l2_query_ext_ctrl(vfh->ctrl_handler, arg);

	case VIDIOC_QUERYMENU:
		if (!vfh->ctrl_handler)
			return -ENOTTY;
		return v4l2_querymenu(vfh->ctrl_handler, arg);

	case VIDIOC_G_CTRL:
		if (!vfh->ctrl_handler)
			return -ENOTTY;
		return v4l2_g_ctrl(vfh->ctrl_handler, arg);

	case VIDIOC_S_CTRL:
		if (!vfh->ctrl_handler)
			return -ENOTTY;
		return v4l2_s_ctrl(vfh, vfh->ctrl_handler, arg);

	case VIDIOC_G_EXT_CTRLS:
		if (!vfh->ctrl_handler)
			return -ENOTTY;
		return v4l2_g_ext_ctrls(vfh->ctrl_handler,
					vdev, sd->v4l2_dev->mdev, arg);

	case VIDIOC_S_EXT_CTRLS:
		if (!vfh->ctrl_handler)
			return -ENOTTY;
		return v4l2_s_ext_ctrls(vfh, vfh->ctrl_handler,
					vdev, sd->v4l2_dev->mdev, arg);

	case VIDIOC_TRY_EXT_CTRLS:
		if (!vfh->ctrl_handler)
			return -ENOTTY;
		return v4l2_try_ext_ctrls(vfh->ctrl_handler,
					  vdev, sd->v4l2_dev->mdev, arg);

	case VIDIOC_DQEVENT:
		if (!(sd->flags & V4L2_SUBDEV_FL_HAS_EVENTS))
			return -ENOIOCTLCMD;

		return v4l2_event_dequeue(vfh, arg, file->f_flags & O_NONBLOCK);

	case VIDIOC_SUBSCRIBE_EVENT:
		return v4l2_subdev_call(sd, core, subscribe_event, vfh, arg);

	case VIDIOC_UNSUBSCRIBE_EVENT:
		return v4l2_subdev_call(sd, core, unsubscribe_event, vfh, arg);

#ifdef CONFIG_VIDEO_ADV_DEBUG
	case VIDIOC_DBG_G_REGISTER:
	{
		struct v4l2_dbg_register *p = arg;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		return v4l2_subdev_call(sd, core, g_register, p);
	}
	case VIDIOC_DBG_S_REGISTER:
	{
		struct v4l2_dbg_register *p = arg;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		return v4l2_subdev_call(sd, core, s_register, p);
	}
	case VIDIOC_DBG_G_CHIP_INFO:
	{
		struct v4l2_dbg_chip_info *p = arg;

		if (p->match.type != V4L2_CHIP_MATCH_SUBDEV || p->match.addr)
			return -EINVAL;
		if (sd->ops->core && sd->ops->core->s_register)
			p->flags |= V4L2_CHIP_FL_WRITABLE;
		if (sd->ops->core && sd->ops->core->g_register)
			p->flags |= V4L2_CHIP_FL_READABLE;
		strscpy(p->name, sd->name, sizeof(p->name));
		return 0;
	}
#endif

	case VIDIOC_LOG_STATUS: {
		int ret;

		pr_info("%s: =================  START STATUS  =================\n",
			sd->name);
		ret = v4l2_subdev_call(sd, core, log_status);
		pr_info("%s: ==================  END STATUS  ==================\n",
			sd->name);
		return ret;
	}

	case VIDIOC_SUBDEV_G_FMT: {
		struct v4l2_subdev_format *format = arg;

		if (!client_supports_streams)
			format->stream = 0;

		memset(format->reserved, 0, sizeof(format->reserved));
		memset(format->format.reserved, 0, sizeof(format->format.reserved));
		return v4l2_subdev_call(sd, pad, get_fmt, state, format);
	}

	case VIDIOC_SUBDEV_S_FMT: {
		struct v4l2_subdev_format *format = arg;

		if (format->which != V4L2_SUBDEV_FORMAT_TRY && ro_subdev)
			return -EPERM;

		if (!client_supports_streams)
			format->stream = 0;

		memset(format->reserved, 0, sizeof(format->reserved));
		memset(format->format.reserved, 0, sizeof(format->format.reserved));
		return v4l2_subdev_call(sd, pad, set_fmt, state, format);
	}

	case VIDIOC_SUBDEV_G_CROP: {
		struct v4l2_subdev_crop *crop = arg;
		struct v4l2_subdev_selection sel;

		if (!client_supports_streams)
			crop->stream = 0;

		memset(crop->reserved, 0, sizeof(crop->reserved));
		memset(&sel, 0, sizeof(sel));
		sel.which = crop->which;
		sel.pad = crop->pad;
		sel.stream = crop->stream;
		sel.target = V4L2_SEL_TGT_CROP;

		rval = v4l2_subdev_call(
			sd, pad, get_selection, state, &sel);

		crop->rect = sel.r;

		return rval;
	}

	case VIDIOC_SUBDEV_S_CROP: {
		struct v4l2_subdev_crop *crop = arg;
		struct v4l2_subdev_selection sel;

		if (crop->which != V4L2_SUBDEV_FORMAT_TRY && ro_subdev)
			return -EPERM;

		if (!client_supports_streams)
			crop->stream = 0;

		memset(crop->reserved, 0, sizeof(crop->reserved));
		memset(&sel, 0, sizeof(sel));
		sel.which = crop->which;
		sel.pad = crop->pad;
		sel.stream = crop->stream;
		sel.target = V4L2_SEL_TGT_CROP;
		sel.r = crop->rect;

		rval = v4l2_subdev_call(
			sd, pad, set_selection, state, &sel);

		crop->rect = sel.r;

		return rval;
	}

	case VIDIOC_SUBDEV_ENUM_MBUS_CODE: {
		struct v4l2_subdev_mbus_code_enum *code = arg;

		if (!client_supports_streams)
			code->stream = 0;

		memset(code->reserved, 0, sizeof(code->reserved));
		return v4l2_subdev_call(sd, pad, enum_mbus_code, state,
					code);
	}

	case VIDIOC_SUBDEV_ENUM_FRAME_SIZE: {
		struct v4l2_subdev_frame_size_enum *fse = arg;

		if (!client_supports_streams)
			fse->stream = 0;

		memset(fse->reserved, 0, sizeof(fse->reserved));
		return v4l2_subdev_call(sd, pad, enum_frame_size, state,
					fse);
	}

	case VIDIOC_SUBDEV_G_FRAME_INTERVAL: {
		struct v4l2_subdev_frame_interval *fi = arg;

		if (!client_supports_streams)
			fi->stream = 0;

		memset(fi->reserved, 0, sizeof(fi->reserved));
		return v4l2_subdev_call(sd, pad, get_frame_interval, state, fi);
	}

	case VIDIOC_SUBDEV_S_FRAME_INTERVAL: {
		struct v4l2_subdev_frame_interval *fi = arg;

		if (!client_supports_streams)
			fi->stream = 0;

		if (fi->which != V4L2_SUBDEV_FORMAT_TRY && ro_subdev)
			return -EPERM;

		memset(fi->reserved, 0, sizeof(fi->reserved));
		return v4l2_subdev_call(sd, pad, set_frame_interval, state, fi);
	}

	case VIDIOC_SUBDEV_ENUM_FRAME_INTERVAL: {
		struct v4l2_subdev_frame_interval_enum *fie = arg;

		if (!client_supports_streams)
			fie->stream = 0;

		memset(fie->reserved, 0, sizeof(fie->reserved));
		return v4l2_subdev_call(sd, pad, enum_frame_interval, state,
					fie);
	}

	case VIDIOC_SUBDEV_G_SELECTION: {
		struct v4l2_subdev_selection *sel = arg;

		if (!client_supports_streams)
			sel->stream = 0;

		memset(sel->reserved, 0, sizeof(sel->reserved));
		return v4l2_subdev_call(
			sd, pad, get_selection, state, sel);
	}

	case VIDIOC_SUBDEV_S_SELECTION: {
		struct v4l2_subdev_selection *sel = arg;

		if (sel->which != V4L2_SUBDEV_FORMAT_TRY && ro_subdev)
			return -EPERM;

		if (!client_supports_streams)
			sel->stream = 0;

		memset(sel->reserved, 0, sizeof(sel->reserved));
		return v4l2_subdev_call(
			sd, pad, set_selection, state, sel);
	}

	case VIDIOC_G_EDID: {
		struct v4l2_subdev_edid *edid = arg;

		return v4l2_subdev_call(sd, pad, get_edid, edid);
	}

	case VIDIOC_S_EDID: {
		struct v4l2_subdev_edid *edid = arg;

		return v4l2_subdev_call(sd, pad, set_edid, edid);
	}

	case VIDIOC_SUBDEV_DV_TIMINGS_CAP: {
		struct v4l2_dv_timings_cap *cap = arg;

		return v4l2_subdev_call(sd, pad, dv_timings_cap, cap);
	}

	case VIDIOC_SUBDEV_ENUM_DV_TIMINGS: {
		struct v4l2_enum_dv_timings *dvt = arg;

		return v4l2_subdev_call(sd, pad, enum_dv_timings, dvt);
	}

	case VIDIOC_SUBDEV_QUERY_DV_TIMINGS:
		return v4l2_subdev_call(sd, pad, query_dv_timings, 0, arg);

	case VIDIOC_SUBDEV_G_DV_TIMINGS:
		return v4l2_subdev_call(sd, pad, g_dv_timings, 0, arg);

	case VIDIOC_SUBDEV_S_DV_TIMINGS:
		if (ro_subdev)
			return -EPERM;

		return v4l2_subdev_call(sd, pad, s_dv_timings, 0, arg);

	case VIDIOC_SUBDEV_G_STD:
		return v4l2_subdev_call(sd, video, g_std, arg);

	case VIDIOC_SUBDEV_S_STD: {
		v4l2_std_id *std = arg;

		if (ro_subdev)
			return -EPERM;

		return v4l2_subdev_call(sd, video, s_std, *std);
	}

	case VIDIOC_SUBDEV_ENUMSTD: {
		struct v4l2_standard *p = arg;
		v4l2_std_id id;

		if (v4l2_subdev_call(sd, video, g_tvnorms, &id))
			return -EINVAL;

		return v4l_video_std_enumstd(p, id);
	}

	case VIDIOC_SUBDEV_QUERYSTD:
		return v4l2_subdev_call(sd, video, querystd, arg);

	case VIDIOC_SUBDEV_G_ROUTING: {
		struct v4l2_subdev_routing *routing = arg;
		struct v4l2_subdev_krouting *krouting;

		if (!v4l2_subdev_enable_streams_api)
			return -ENOIOCTLCMD;

		if (!(sd->flags & V4L2_SUBDEV_FL_STREAMS))
			return -ENOIOCTLCMD;

		memset(routing->reserved, 0, sizeof(routing->reserved));

		krouting = &state->routing;

		memcpy((struct v4l2_subdev_route *)(uintptr_t)routing->routes,
		       krouting->routes,
		       min(krouting->num_routes, routing->len_routes) *
		       sizeof(*krouting->routes));
		routing->num_routes = krouting->num_routes;

		return 0;
	}

	case VIDIOC_SUBDEV_S_ROUTING: {
		struct v4l2_subdev_routing *routing = arg;
		struct v4l2_subdev_route *routes =
			(struct v4l2_subdev_route *)(uintptr_t)routing->routes;
		struct v4l2_subdev_krouting krouting = {};
		unsigned int i;

		if (!v4l2_subdev_enable_streams_api)
			return -ENOIOCTLCMD;

		if (!(sd->flags & V4L2_SUBDEV_FL_STREAMS))
			return -ENOIOCTLCMD;

		if (routing->which != V4L2_SUBDEV_FORMAT_TRY && ro_subdev)
			return -EPERM;

		if (routing->num_routes > routing->len_routes)
			return -EINVAL;

		memset(routing->reserved, 0, sizeof(routing->reserved));

		for (i = 0; i < routing->num_routes; ++i) {
			const struct v4l2_subdev_route *route = &routes[i];
			const struct media_pad *pads = sd->entity.pads;

			if (route->sink_stream > V4L2_SUBDEV_MAX_STREAM_ID ||
			    route->source_stream > V4L2_SUBDEV_MAX_STREAM_ID)
				return -EINVAL;

			if (route->sink_pad >= sd->entity.num_pads)
				return -EINVAL;

			if (!(pads[route->sink_pad].flags &
			      MEDIA_PAD_FL_SINK))
				return -EINVAL;

			if (route->source_pad >= sd->entity.num_pads)
				return -EINVAL;

			if (!(pads[route->source_pad].flags &
			      MEDIA_PAD_FL_SOURCE))
				return -EINVAL;
		}

		/*
		 * If the driver doesn't support setting routing, just return
		 * the routing table.
		 */
		if (!v4l2_subdev_has_op(sd, pad, set_routing)) {
			memcpy((struct v4l2_subdev_route *)(uintptr_t)routing->routes,
			       state->routing.routes,
			       min(state->routing.num_routes, routing->len_routes) *
			       sizeof(*state->routing.routes));
			routing->num_routes = state->routing.num_routes;

			return 0;
		}

		krouting.num_routes = routing->num_routes;
		krouting.len_routes = routing->len_routes;
		krouting.routes = routes;

		rval = v4l2_subdev_call(sd, pad, set_routing, state,
					routing->which, &krouting);
		if (rval < 0)
			return rval;

		memcpy((struct v4l2_subdev_route *)(uintptr_t)routing->routes,
		       state->routing.routes,
		       min(state->routing.num_routes, routing->len_routes) *
		       sizeof(*state->routing.routes));
		routing->num_routes = state->routing.num_routes;

		return 0;
	}

	case VIDIOC_SUBDEV_G_CLIENT_CAP: {
		struct v4l2_subdev_client_capability *client_cap = arg;

		client_cap->capabilities = subdev_fh->client_caps;

		return 0;
	}

	case VIDIOC_SUBDEV_S_CLIENT_CAP: {
		struct v4l2_subdev_client_capability *client_cap = arg;

		/*
		 * Clear V4L2_SUBDEV_CLIENT_CAP_STREAMS if streams API is not
		 * enabled. Remove this when streams API is no longer
		 * experimental.
		 */
		if (!v4l2_subdev_enable_streams_api)
			client_cap->capabilities &= ~V4L2_SUBDEV_CLIENT_CAP_STREAMS;

		/* Filter out unsupported capabilities */
		client_cap->capabilities &= (V4L2_SUBDEV_CLIENT_CAP_STREAMS |
					     V4L2_SUBDEV_CLIENT_CAP_INTERVAL_USES_WHICH);

		subdev_fh->client_caps = client_cap->capabilities;

		return 0;
	}

	default:
		return v4l2_subdev_call(sd, core, ioctl, cmd, arg);
	}

	return 0;
}

static long subdev_do_ioctl_lock(struct file *file, unsigned int cmd, void *arg)
{
	struct video_device *vdev = video_devdata(file);
	struct mutex *lock = vdev->lock;
	long ret = -ENODEV;

	if (lock && mutex_lock_interruptible(lock))
		return -ERESTARTSYS;

	if (video_is_registered(vdev)) {
		struct v4l2_subdev *sd = vdev_to_v4l2_subdev(vdev);
		struct v4l2_fh *vfh = file->private_data;
		struct v4l2_subdev_fh *subdev_fh = to_v4l2_subdev_fh(vfh);
		struct v4l2_subdev_state *state;

		state = subdev_ioctl_get_state(sd, subdev_fh, cmd, arg);

		if (state)
			v4l2_subdev_lock_state(state);

		ret = subdev_do_ioctl(file, cmd, arg, state);

		if (state)
			v4l2_subdev_unlock_state(state);
	}

	if (lock)
		mutex_unlock(lock);
	return ret;
}

static long subdev_ioctl(struct file *file, unsigned int cmd,
	unsigned long arg)
{
	return video_usercopy(file, cmd, arg, subdev_do_ioctl_lock);
}

#ifdef CONFIG_COMPAT
static long subdev_compat_ioctl32(struct file *file, unsigned int cmd,
	unsigned long arg)
{
	struct video_device *vdev = video_devdata(file);
	struct v4l2_subdev *sd = vdev_to_v4l2_subdev(vdev);

	return v4l2_subdev_call(sd, core, compat_ioctl32, cmd, arg);
}
#endif

#else /* CONFIG_VIDEO_V4L2_SUBDEV_API */
static long subdev_ioctl(struct file *file, unsigned int cmd,
			 unsigned long arg)
{
	return -ENODEV;
}

#ifdef CONFIG_COMPAT
static long subdev_compat_ioctl32(struct file *file, unsigned int cmd,
				  unsigned long arg)
{
	return -ENODEV;
}
#endif
#endif /* CONFIG_VIDEO_V4L2_SUBDEV_API */

static __poll_t subdev_poll(struct file *file, poll_table *wait)
{
	struct video_device *vdev = video_devdata(file);
	struct v4l2_subdev *sd = vdev_to_v4l2_subdev(vdev);
	struct v4l2_fh *fh = file->private_data;

	if (!(sd->flags & V4L2_SUBDEV_FL_HAS_EVENTS))
		return EPOLLERR;

	poll_wait(file, &fh->wait, wait);

	if (v4l2_event_pending(fh))
		return EPOLLPRI;

	return 0;
}

const struct v4l2_file_operations v4l2_subdev_fops = {
	.owner = THIS_MODULE,
	.open = subdev_open,
	.unlocked_ioctl = subdev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = subdev_compat_ioctl32,
#endif
	.release = subdev_close,
	.poll = subdev_poll,
};

#ifdef CONFIG_MEDIA_CONTROLLER

int v4l2_subdev_get_fwnode_pad_1_to_1(struct media_entity *entity,
				      struct fwnode_endpoint *endpoint)
{
	struct fwnode_handle *fwnode;
	struct v4l2_subdev *sd;

	if (!is_media_entity_v4l2_subdev(entity))
		return -EINVAL;

	sd = media_entity_to_v4l2_subdev(entity);

	fwnode = fwnode_graph_get_port_parent(endpoint->local_fwnode);
	fwnode_handle_put(fwnode);

	if (device_match_fwnode(sd->dev, fwnode))
		return endpoint->port;

	return -ENXIO;
}
EXPORT_SYMBOL_GPL(v4l2_subdev_get_fwnode_pad_1_to_1);

int v4l2_subdev_link_validate_default(struct v4l2_subdev *sd,
				      struct media_link *link,
				      struct v4l2_subdev_format *source_fmt,
				      struct v4l2_subdev_format *sink_fmt)
{
	bool pass = true;

	/* The width, height and code must match. */
	if (source_fmt->format.width != sink_fmt->format.width) {
		dev_dbg(sd->entity.graph_obj.mdev->dev,
			"%s: width does not match (source %u, sink %u)\n",
			__func__,
			source_fmt->format.width, sink_fmt->format.width);
		pass = false;
	}

	if (source_fmt->format.height != sink_fmt->format.height) {
		dev_dbg(sd->entity.graph_obj.mdev->dev,
			"%s: height does not match (source %u, sink %u)\n",
			__func__,
			source_fmt->format.height, sink_fmt->format.height);
		pass = false;
	}

	if (source_fmt->format.code != sink_fmt->format.code) {
		dev_dbg(sd->entity.graph_obj.mdev->dev,
			"%s: media bus code does not match (source 0x%8.8x, sink 0x%8.8x)\n",
			__func__,
			source_fmt->format.code, sink_fmt->format.code);
		pass = false;
	}

	/* The field order must match, or the sink field order must be NONE
	 * to support interlaced hardware connected to bridges that support
	 * progressive formats only.
	 */
	if (source_fmt->format.field != sink_fmt->format.field &&
	    sink_fmt->format.field != V4L2_FIELD_NONE) {
		dev_dbg(sd->entity.graph_obj.mdev->dev,
			"%s: field does not match (source %u, sink %u)\n",
			__func__,
			source_fmt->format.field, sink_fmt->format.field);
		pass = false;
	}

	if (pass)
		return 0;

	dev_dbg(sd->entity.graph_obj.mdev->dev,
		"%s: link was \"%s\":%u -> \"%s\":%u\n", __func__,
		link->source->entity->name, link->source->index,
		link->sink->entity->name, link->sink->index);

	return -EPIPE;
}
EXPORT_SYMBOL_GPL(v4l2_subdev_link_validate_default);

static int
v4l2_subdev_link_validate_get_format(struct media_pad *pad, u32 stream,
				     struct v4l2_subdev_format *fmt,
				     bool states_locked)
{
	struct v4l2_subdev_state *state;
	struct v4l2_subdev *sd;
	int ret;

	if (!is_media_entity_v4l2_subdev(pad->entity)) {
		WARN(pad->entity->function != MEDIA_ENT_F_IO_V4L,
		     "Driver bug! Wrong media entity type 0x%08x, entity %s\n",
		     pad->entity->function, pad->entity->name);

		return -EINVAL;
	}

	sd = media_entity_to_v4l2_subdev(pad->entity);

	fmt->which = V4L2_SUBDEV_FORMAT_ACTIVE;
	fmt->pad = pad->index;
	fmt->stream = stream;

	if (states_locked)
		state = v4l2_subdev_get_locked_active_state(sd);
	else
		state = v4l2_subdev_lock_and_get_active_state(sd);

	ret = v4l2_subdev_call(sd, pad, get_fmt, state, fmt);

	if (!states_locked && state)
		v4l2_subdev_unlock_state(state);

	return ret;
}

#if defined(CONFIG_VIDEO_V4L2_SUBDEV_API)

static void __v4l2_link_validate_get_streams(struct media_pad *pad,
					     u64 *streams_mask,
					     bool states_locked)
{
	struct v4l2_subdev_route *route;
	struct v4l2_subdev_state *state;
	struct v4l2_subdev *subdev;

	subdev = media_entity_to_v4l2_subdev(pad->entity);

	*streams_mask = 0;

	if (states_locked)
		state = v4l2_subdev_get_locked_active_state(subdev);
	else
		state = v4l2_subdev_lock_and_get_active_state(subdev);

	if (WARN_ON(!state))
		return;

	for_each_active_route(&state->routing, route) {
		u32 route_pad;
		u32 route_stream;

		if (pad->flags & MEDIA_PAD_FL_SOURCE) {
			route_pad = route->source_pad;
			route_stream = route->source_stream;
		} else {
			route_pad = route->sink_pad;
			route_stream = route->sink_stream;
		}

		if (route_pad != pad->index)
			continue;

		*streams_mask |= BIT_ULL(route_stream);
	}

	if (!states_locked)
		v4l2_subdev_unlock_state(state);
}

#endif /* CONFIG_VIDEO_V4L2_SUBDEV_API */

static void v4l2_link_validate_get_streams(struct media_pad *pad,
					   u64 *streams_mask,
					   bool states_locked)
{
	struct v4l2_subdev *subdev = media_entity_to_v4l2_subdev(pad->entity);

	if (!(subdev->flags & V4L2_SUBDEV_FL_STREAMS)) {
		/* Non-streams subdevs have an implicit stream 0 */
		*streams_mask = BIT_ULL(0);
		return;
	}

#if defined(CONFIG_VIDEO_V4L2_SUBDEV_API)
	__v4l2_link_validate_get_streams(pad, streams_mask, states_locked);
#else
	/* This shouldn't happen */
	*streams_mask = 0;
#endif
}

static int v4l2_subdev_link_validate_locked(struct media_link *link, bool states_locked)
{
	struct v4l2_subdev *sink_subdev =
		media_entity_to_v4l2_subdev(link->sink->entity);
	struct device *dev = sink_subdev->entity.graph_obj.mdev->dev;
	u64 source_streams_mask;
	u64 sink_streams_mask;
	u64 dangling_sink_streams;
	u32 stream;
	int ret;

	dev_dbg(dev, "validating link \"%s\":%u -> \"%s\":%u\n",
		link->source->entity->name, link->source->index,
		link->sink->entity->name, link->sink->index);

	v4l2_link_validate_get_streams(link->source, &source_streams_mask, states_locked);
	v4l2_link_validate_get_streams(link->sink, &sink_streams_mask, states_locked);

	/*
	 * It is ok to have more source streams than sink streams as extra
	 * source streams can just be ignored by the receiver, but having extra
	 * sink streams is an error as streams must have a source.
	 */
	dangling_sink_streams = (source_streams_mask ^ sink_streams_mask) &
				sink_streams_mask;
	if (dangling_sink_streams) {
		dev_err(dev, "Dangling sink streams: mask %#llx\n",
			dangling_sink_streams);
		return -EINVAL;
	}

	/* Validate source and sink stream formats */

	for (stream = 0; stream < sizeof(sink_streams_mask) * 8; ++stream) {
		struct v4l2_subdev_format sink_fmt, source_fmt;

		if (!(sink_streams_mask & BIT_ULL(stream)))
			continue;

		dev_dbg(dev, "validating stream \"%s\":%u:%u -> \"%s\":%u:%u\n",
			link->source->entity->name, link->source->index, stream,
			link->sink->entity->name, link->sink->index, stream);

		ret = v4l2_subdev_link_validate_get_format(link->source, stream,
							   &source_fmt, states_locked);
		if (ret < 0) {
			dev_dbg(dev,
				"Failed to get format for \"%s\":%u:%u (but that's ok)\n",
				link->source->entity->name, link->source->index,
				stream);
			continue;
		}

		ret = v4l2_subdev_link_validate_get_format(link->sink, stream,
							   &sink_fmt, states_locked);
		if (ret < 0) {
			dev_dbg(dev,
				"Failed to get format for \"%s\":%u:%u (but that's ok)\n",
				link->sink->entity->name, link->sink->index,
				stream);
			continue;
		}

		/* TODO: add stream number to link_validate() */
		ret = v4l2_subdev_call(sink_subdev, pad, link_validate, link,
				       &source_fmt, &sink_fmt);
		if (!ret)
			continue;

		if (ret != -ENOIOCTLCMD)
			return ret;

		ret = v4l2_subdev_link_validate_default(sink_subdev, link,
							&source_fmt, &sink_fmt);

		if (ret)
			return ret;
	}

	return 0;
}

int v4l2_subdev_link_validate(struct media_link *link)
{
	struct v4l2_subdev *source_sd, *sink_sd;
	struct v4l2_subdev_state *source_state, *sink_state;
	bool states_locked;
	int ret;

	if (!is_media_entity_v4l2_subdev(link->sink->entity) ||
	    !is_media_entity_v4l2_subdev(link->source->entity)) {
		pr_warn_once("%s of link '%s':%u->'%s':%u is not a V4L2 sub-device, driver bug!\n",
			     !is_media_entity_v4l2_subdev(link->sink->entity) ?
			     "sink" : "source",
			     link->source->entity->name, link->source->index,
			     link->sink->entity->name, link->sink->index);
		return 0;
	}

	sink_sd = media_entity_to_v4l2_subdev(link->sink->entity);
	source_sd = media_entity_to_v4l2_subdev(link->source->entity);

	sink_state = v4l2_subdev_get_unlocked_active_state(sink_sd);
	source_state = v4l2_subdev_get_unlocked_active_state(source_sd);

	states_locked = sink_state && source_state;

	if (states_locked)
		v4l2_subdev_lock_states(sink_state, source_state);

	ret = v4l2_subdev_link_validate_locked(link, states_locked);

	if (states_locked)
		v4l2_subdev_unlock_states(sink_state, source_state);

	return ret;
}
EXPORT_SYMBOL_GPL(v4l2_subdev_link_validate);

bool v4l2_subdev_has_pad_interdep(struct media_entity *entity,
				  unsigned int pad0, unsigned int pad1)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct v4l2_subdev_krouting *routing;
	struct v4l2_subdev_state *state;
	unsigned int i;

	state = v4l2_subdev_lock_and_get_active_state(sd);

	routing = &state->routing;

	for (i = 0; i < routing->num_routes; ++i) {
		struct v4l2_subdev_route *route = &routing->routes[i];

		if (!(route->flags & V4L2_SUBDEV_ROUTE_FL_ACTIVE))
			continue;

		if ((route->sink_pad == pad0 && route->source_pad == pad1) ||
		    (route->source_pad == pad0 && route->sink_pad == pad1)) {
			v4l2_subdev_unlock_state(state);
			return true;
		}
	}

	v4l2_subdev_unlock_state(state);

	return false;
}
EXPORT_SYMBOL_GPL(v4l2_subdev_has_pad_interdep);

struct v4l2_subdev_state *
__v4l2_subdev_state_alloc(struct v4l2_subdev *sd, const char *lock_name,
			  struct lock_class_key *lock_key)
{
	struct v4l2_subdev_state *state;
	int ret;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return ERR_PTR(-ENOMEM);

	__mutex_init(&state->_lock, lock_name, lock_key);
	if (sd->state_lock)
		state->lock = sd->state_lock;
	else
		state->lock = &state->_lock;

	state->sd = sd;

	/* Drivers that support streams do not need the legacy pad config */
	if (!(sd->flags & V4L2_SUBDEV_FL_STREAMS) && sd->entity.num_pads) {
		state->pads = kvcalloc(sd->entity.num_pads,
				       sizeof(*state->pads), GFP_KERNEL);
		if (!state->pads) {
			ret = -ENOMEM;
			goto err;
		}
	}

	if (sd->internal_ops && sd->internal_ops->init_state) {
		/*
		 * There can be no race at this point, but we lock the state
		 * anyway to satisfy lockdep checks.
		 */
		v4l2_subdev_lock_state(state);
		ret = sd->internal_ops->init_state(sd, state);
		v4l2_subdev_unlock_state(state);

		if (ret)
			goto err;
	}

	return state;

err:
	if (state && state->pads)
		kvfree(state->pads);

	kfree(state);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(__v4l2_subdev_state_alloc);

void __v4l2_subdev_state_free(struct v4l2_subdev_state *state)
{
	if (!state)
		return;

	mutex_destroy(&state->_lock);

	kfree(state->routing.routes);
	kvfree(state->stream_configs.configs);
	kvfree(state->pads);
	kfree(state);
}
EXPORT_SYMBOL_GPL(__v4l2_subdev_state_free);

int __v4l2_subdev_init_finalize(struct v4l2_subdev *sd, const char *name,
				struct lock_class_key *key)
{
	struct v4l2_subdev_state *state;

	state = __v4l2_subdev_state_alloc(sd, name, key);
	if (IS_ERR(state))
		return PTR_ERR(state);

	sd->active_state = state;

	return 0;
}
EXPORT_SYMBOL_GPL(__v4l2_subdev_init_finalize);

void v4l2_subdev_cleanup(struct v4l2_subdev *sd)
{
	struct v4l2_async_subdev_endpoint *ase, *ase_tmp;

	__v4l2_subdev_state_free(sd->active_state);
	sd->active_state = NULL;

	/* Uninitialised sub-device, bail out here. */
	if (!sd->async_subdev_endpoint_list.next)
		return;

	list_for_each_entry_safe(ase, ase_tmp, &sd->async_subdev_endpoint_list,
				 async_subdev_endpoint_entry) {
		list_del(&ase->async_subdev_endpoint_entry);

		kfree(ase);
	}
}
EXPORT_SYMBOL_GPL(v4l2_subdev_cleanup);

struct v4l2_mbus_framefmt *
__v4l2_subdev_state_get_format(struct v4l2_subdev_state *state,
			       unsigned int pad, u32 stream)
{
	struct v4l2_subdev_stream_configs *stream_configs;
	unsigned int i;

	if (WARN_ON_ONCE(!state))
		return NULL;

	if (state->pads) {
		if (stream)
			return NULL;

		if (pad >= state->sd->entity.num_pads)
			return NULL;

		return &state->pads[pad].format;
	}

	lockdep_assert_held(state->lock);

	stream_configs = &state->stream_configs;

	for (i = 0; i < stream_configs->num_configs; ++i) {
		if (stream_configs->configs[i].pad == pad &&
		    stream_configs->configs[i].stream == stream)
			return &stream_configs->configs[i].fmt;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(__v4l2_subdev_state_get_format);

struct v4l2_rect *
__v4l2_subdev_state_get_crop(struct v4l2_subdev_state *state, unsigned int pad,
			     u32 stream)
{
	struct v4l2_subdev_stream_configs *stream_configs;
	unsigned int i;

	if (WARN_ON_ONCE(!state))
		return NULL;

	if (state->pads) {
		if (stream)
			return NULL;

		if (pad >= state->sd->entity.num_pads)
			return NULL;

		return &state->pads[pad].crop;
	}

	lockdep_assert_held(state->lock);

	stream_configs = &state->stream_configs;

	for (i = 0; i < stream_configs->num_configs; ++i) {
		if (stream_configs->configs[i].pad == pad &&
		    stream_configs->configs[i].stream == stream)
			return &stream_configs->configs[i].crop;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(__v4l2_subdev_state_get_crop);

struct v4l2_rect *
__v4l2_subdev_state_get_compose(struct v4l2_subdev_state *state,
				unsigned int pad, u32 stream)
{
	struct v4l2_subdev_stream_configs *stream_configs;
	unsigned int i;

	if (WARN_ON_ONCE(!state))
		return NULL;

	if (state->pads) {
		if (stream)
			return NULL;

		if (pad >= state->sd->entity.num_pads)
			return NULL;

		return &state->pads[pad].compose;
	}

	lockdep_assert_held(state->lock);

	stream_configs = &state->stream_configs;

	for (i = 0; i < stream_configs->num_configs; ++i) {
		if (stream_configs->configs[i].pad == pad &&
		    stream_configs->configs[i].stream == stream)
			return &stream_configs->configs[i].compose;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(__v4l2_subdev_state_get_compose);

struct v4l2_fract *
__v4l2_subdev_state_get_interval(struct v4l2_subdev_state *state,
				 unsigned int pad, u32 stream)
{
	struct v4l2_subdev_stream_configs *stream_configs;
	unsigned int i;

	if (WARN_ON(!state))
		return NULL;

	lockdep_assert_held(state->lock);

	if (state->pads) {
		if (stream)
			return NULL;

		if (pad >= state->sd->entity.num_pads)
			return NULL;

		return &state->pads[pad].interval;
	}

	lockdep_assert_held(state->lock);

	stream_configs = &state->stream_configs;

	for (i = 0; i < stream_configs->num_configs; ++i) {
		if (stream_configs->configs[i].pad == pad &&
		    stream_configs->configs[i].stream == stream)
			return &stream_configs->configs[i].interval;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(__v4l2_subdev_state_get_interval);

#if defined(CONFIG_VIDEO_V4L2_SUBDEV_API)

static int
v4l2_subdev_init_stream_configs(struct v4l2_subdev_stream_configs *stream_configs,
				const struct v4l2_subdev_krouting *routing)
{
	struct v4l2_subdev_stream_configs new_configs = { 0 };
	struct v4l2_subdev_route *route;
	u32 idx;

	/* Count number of formats needed */
	for_each_active_route(routing, route) {
		/*
		 * Each route needs a format on both ends of the route.
		 */
		new_configs.num_configs += 2;
	}

	if (new_configs.num_configs) {
		new_configs.configs = kvcalloc(new_configs.num_configs,
					       sizeof(*new_configs.configs),
					       GFP_KERNEL);

		if (!new_configs.configs)
			return -ENOMEM;
	}

	/*
	 * Fill in the 'pad' and stream' value for each item in the array from
	 * the routing table
	 */
	idx = 0;

	for_each_active_route(routing, route) {
		new_configs.configs[idx].pad = route->sink_pad;
		new_configs.configs[idx].stream = route->sink_stream;

		idx++;

		new_configs.configs[idx].pad = route->source_pad;
		new_configs.configs[idx].stream = route->source_stream;

		idx++;
	}

	kvfree(stream_configs->configs);
	*stream_configs = new_configs;

	return 0;
}

int v4l2_subdev_get_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_state *state,
			struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt;

	fmt = v4l2_subdev_state_get_format(state, format->pad, format->stream);
	if (!fmt)
		return -EINVAL;

	format->format = *fmt;

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_subdev_get_fmt);

int v4l2_subdev_get_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct v4l2_fract *interval;

	interval = v4l2_subdev_state_get_interval(state, fi->pad, fi->stream);
	if (!interval)
		return -EINVAL;

	fi->interval = *interval;

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_subdev_get_frame_interval);

int v4l2_subdev_set_routing(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *state,
			    const struct v4l2_subdev_krouting *routing)
{
	struct v4l2_subdev_krouting *dst = &state->routing;
	const struct v4l2_subdev_krouting *src = routing;
	struct v4l2_subdev_krouting new_routing = { 0 };
	size_t bytes;
	int r;

	if (unlikely(check_mul_overflow((size_t)src->num_routes,
					sizeof(*src->routes), &bytes)))
		return -EOVERFLOW;

	lockdep_assert_held(state->lock);

	if (src->num_routes > 0) {
		new_routing.routes = kmemdup(src->routes, bytes, GFP_KERNEL);
		if (!new_routing.routes)
			return -ENOMEM;
	}

	new_routing.num_routes = src->num_routes;

	r = v4l2_subdev_init_stream_configs(&state->stream_configs,
					    &new_routing);
	if (r) {
		kfree(new_routing.routes);
		return r;
	}

	kfree(dst->routes);
	*dst = new_routing;

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_subdev_set_routing);

struct v4l2_subdev_route *
__v4l2_subdev_next_active_route(const struct v4l2_subdev_krouting *routing,
				struct v4l2_subdev_route *route)
{
	if (route)
		++route;
	else
		route = &routing->routes[0];

	for (; route < routing->routes + routing->num_routes; ++route) {
		if (!(route->flags & V4L2_SUBDEV_ROUTE_FL_ACTIVE))
			continue;

		return route;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(__v4l2_subdev_next_active_route);

int v4l2_subdev_set_routing_with_fmt(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *state,
				     const struct v4l2_subdev_krouting *routing,
				     const struct v4l2_mbus_framefmt *fmt)
{
	struct v4l2_subdev_stream_configs *stream_configs;
	unsigned int i;
	int ret;

	ret = v4l2_subdev_set_routing(sd, state, routing);
	if (ret)
		return ret;

	stream_configs = &state->stream_configs;

	for (i = 0; i < stream_configs->num_configs; ++i)
		stream_configs->configs[i].fmt = *fmt;

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_subdev_set_routing_with_fmt);

int v4l2_subdev_routing_find_opposite_end(const struct v4l2_subdev_krouting *routing,
					  u32 pad, u32 stream, u32 *other_pad,
					  u32 *other_stream)
{
	unsigned int i;

	for (i = 0; i < routing->num_routes; ++i) {
		struct v4l2_subdev_route *route = &routing->routes[i];

		if (route->source_pad == pad &&
		    route->source_stream == stream) {
			if (other_pad)
				*other_pad = route->sink_pad;
			if (other_stream)
				*other_stream = route->sink_stream;
			return 0;
		}

		if (route->sink_pad == pad && route->sink_stream == stream) {
			if (other_pad)
				*other_pad = route->source_pad;
			if (other_stream)
				*other_stream = route->source_stream;
			return 0;
		}
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(v4l2_subdev_routing_find_opposite_end);

struct v4l2_mbus_framefmt *
v4l2_subdev_state_get_opposite_stream_format(struct v4l2_subdev_state *state,
					     u32 pad, u32 stream)
{
	u32 other_pad, other_stream;
	int ret;

	ret = v4l2_subdev_routing_find_opposite_end(&state->routing,
						    pad, stream,
						    &other_pad, &other_stream);
	if (ret)
		return NULL;

	return v4l2_subdev_state_get_format(state, other_pad, other_stream);
}
EXPORT_SYMBOL_GPL(v4l2_subdev_state_get_opposite_stream_format);

u64 v4l2_subdev_state_xlate_streams(const struct v4l2_subdev_state *state,
				    u32 pad0, u32 pad1, u64 *streams)
{
	const struct v4l2_subdev_krouting *routing = &state->routing;
	struct v4l2_subdev_route *route;
	u64 streams0 = 0;
	u64 streams1 = 0;

	for_each_active_route(routing, route) {
		if (route->sink_pad == pad0 && route->source_pad == pad1 &&
		    (*streams & BIT_ULL(route->sink_stream))) {
			streams0 |= BIT_ULL(route->sink_stream);
			streams1 |= BIT_ULL(route->source_stream);
		}
		if (route->source_pad == pad0 && route->sink_pad == pad1 &&
		    (*streams & BIT_ULL(route->source_stream))) {
			streams0 |= BIT_ULL(route->source_stream);
			streams1 |= BIT_ULL(route->sink_stream);
		}
	}

	*streams = streams0;
	return streams1;
}
EXPORT_SYMBOL_GPL(v4l2_subdev_state_xlate_streams);

int v4l2_subdev_routing_validate(struct v4l2_subdev *sd,
				 const struct v4l2_subdev_krouting *routing,
				 enum v4l2_subdev_routing_restriction disallow)
{
	u32 *remote_pads = NULL;
	unsigned int i, j;
	int ret = -EINVAL;

	if (disallow & (V4L2_SUBDEV_ROUTING_NO_STREAM_MIX |
			V4L2_SUBDEV_ROUTING_NO_MULTIPLEXING)) {
		remote_pads = kcalloc(sd->entity.num_pads, sizeof(*remote_pads),
				      GFP_KERNEL);
		if (!remote_pads)
			return -ENOMEM;

		for (i = 0; i < sd->entity.num_pads; ++i)
			remote_pads[i] = U32_MAX;
	}

	for (i = 0; i < routing->num_routes; ++i) {
		const struct v4l2_subdev_route *route = &routing->routes[i];

		/* Validate the sink and source pad numbers. */
		if (route->sink_pad >= sd->entity.num_pads ||
		    !(sd->entity.pads[route->sink_pad].flags & MEDIA_PAD_FL_SINK)) {
			dev_dbg(sd->dev, "route %u sink (%u) is not a sink pad\n",
				i, route->sink_pad);
			goto out;
		}

		if (route->source_pad >= sd->entity.num_pads ||
		    !(sd->entity.pads[route->source_pad].flags & MEDIA_PAD_FL_SOURCE)) {
			dev_dbg(sd->dev, "route %u source (%u) is not a source pad\n",
				i, route->source_pad);
			goto out;
		}

		/*
		 * V4L2_SUBDEV_ROUTING_NO_SINK_STREAM_MIX: all streams from a
		 * sink pad must be routed to a single source pad.
		 */
		if (disallow & V4L2_SUBDEV_ROUTING_NO_SINK_STREAM_MIX) {
			if (remote_pads[route->sink_pad] != U32_MAX &&
			    remote_pads[route->sink_pad] != route->source_pad) {
				dev_dbg(sd->dev,
					"route %u attempts to mix %s streams\n",
					i, "sink");
				goto out;
			}
		}

		/*
		 * V4L2_SUBDEV_ROUTING_NO_SOURCE_STREAM_MIX: all streams on a
		 * source pad must originate from a single sink pad.
		 */
		if (disallow & V4L2_SUBDEV_ROUTING_NO_SOURCE_STREAM_MIX) {
			if (remote_pads[route->source_pad] != U32_MAX &&
			    remote_pads[route->source_pad] != route->sink_pad) {
				dev_dbg(sd->dev,
					"route %u attempts to mix %s streams\n",
					i, "source");
				goto out;
			}
		}

		/*
		 * V4L2_SUBDEV_ROUTING_NO_SINK_MULTIPLEXING: Pads on the sink
		 * side can not do stream multiplexing, i.e. there can be only
		 * a single stream in a sink pad.
		 */
		if (disallow & V4L2_SUBDEV_ROUTING_NO_SINK_MULTIPLEXING) {
			if (remote_pads[route->sink_pad] != U32_MAX) {
				dev_dbg(sd->dev,
					"route %u attempts to multiplex on %s pad %u\n",
					i, "sink", route->sink_pad);
				goto out;
			}
		}

		/*
		 * V4L2_SUBDEV_ROUTING_NO_SOURCE_MULTIPLEXING: Pads on the
		 * source side can not do stream multiplexing, i.e. there can
		 * be only a single stream in a source pad.
		 */
		if (disallow & V4L2_SUBDEV_ROUTING_NO_SOURCE_MULTIPLEXING) {
			if (remote_pads[route->source_pad] != U32_MAX) {
				dev_dbg(sd->dev,
					"route %u attempts to multiplex on %s pad %u\n",
					i, "source", route->source_pad);
				goto out;
			}
		}

		if (remote_pads) {
			remote_pads[route->sink_pad] = route->source_pad;
			remote_pads[route->source_pad] = route->sink_pad;
		}

		for (j = i + 1; j < routing->num_routes; ++j) {
			const struct v4l2_subdev_route *r = &routing->routes[j];

			/*
			 * V4L2_SUBDEV_ROUTING_NO_1_TO_N: No two routes can
			 * originate from the same (sink) stream.
			 */
			if ((disallow & V4L2_SUBDEV_ROUTING_NO_1_TO_N) &&
			    route->sink_pad == r->sink_pad &&
			    route->sink_stream == r->sink_stream) {
				dev_dbg(sd->dev,
					"routes %u and %u originate from same sink (%u/%u)\n",
					i, j, route->sink_pad,
					route->sink_stream);
				goto out;
			}

			/*
			 * V4L2_SUBDEV_ROUTING_NO_N_TO_1: No two routes can end
			 * at the same (source) stream.
			 */
			if ((disallow & V4L2_SUBDEV_ROUTING_NO_N_TO_1) &&
			    route->source_pad == r->source_pad &&
			    route->source_stream == r->source_stream) {
				dev_dbg(sd->dev,
					"routes %u and %u end at same source (%u/%u)\n",
					i, j, route->source_pad,
					route->source_stream);
				goto out;
			}
		}
	}

	ret = 0;

out:
	kfree(remote_pads);
	return ret;
}
EXPORT_SYMBOL_GPL(v4l2_subdev_routing_validate);

static int v4l2_subdev_enable_streams_fallback(struct v4l2_subdev *sd, u32 pad,
					       u64 streams_mask)
{
	struct device *dev = sd->entity.graph_obj.mdev->dev;
	unsigned int i;
	int ret;

	/*
	 * The subdev doesn't implement pad-based stream enable, fall back
	 * on the .s_stream() operation. This can only be done for subdevs that
	 * have a single source pad, as sd->enabled_streams is global to the
	 * subdev.
	 */
	if (!(sd->entity.pads[pad].flags & MEDIA_PAD_FL_SOURCE))
		return -EOPNOTSUPP;

	for (i = 0; i < sd->entity.num_pads; ++i) {
		if (i != pad && sd->entity.pads[i].flags & MEDIA_PAD_FL_SOURCE)
			return -EOPNOTSUPP;
	}

	if (sd->enabled_streams & streams_mask) {
		dev_dbg(dev, "set of streams %#llx already enabled on %s:%u\n",
			streams_mask, sd->entity.name, pad);
		return -EALREADY;
	}

	/* Start streaming when the first streams are enabled. */
	if (!sd->enabled_streams) {
		ret = v4l2_subdev_call(sd, video, s_stream, 1);
		if (ret)
			return ret;
	}

	sd->enabled_streams |= streams_mask;

	return 0;
}

int v4l2_subdev_enable_streams(struct v4l2_subdev *sd, u32 pad,
			       u64 streams_mask)
{
	struct device *dev = sd->entity.graph_obj.mdev->dev;
	struct v4l2_subdev_state *state;
	u64 found_streams = 0;
	unsigned int i;
	int ret;

	/* A few basic sanity checks first. */
	if (pad >= sd->entity.num_pads)
		return -EINVAL;

	if (!streams_mask)
		return 0;

	/* Fallback on .s_stream() if .enable_streams() isn't available. */
	if (!sd->ops->pad || !sd->ops->pad->enable_streams)
		return v4l2_subdev_enable_streams_fallback(sd, pad,
							   streams_mask);

	state = v4l2_subdev_lock_and_get_active_state(sd);

	/*
	 * Verify that the requested streams exist and that they are not
	 * already enabled.
	 */
	for (i = 0; i < state->stream_configs.num_configs; ++i) {
		struct v4l2_subdev_stream_config *cfg =
			&state->stream_configs.configs[i];

		if (cfg->pad != pad || !(streams_mask & BIT_ULL(cfg->stream)))
			continue;

		found_streams |= BIT_ULL(cfg->stream);

		if (cfg->enabled) {
			dev_dbg(dev, "stream %u already enabled on %s:%u\n",
				cfg->stream, sd->entity.name, pad);
			ret = -EALREADY;
			goto done;
		}
	}

	if (found_streams != streams_mask) {
		dev_dbg(dev, "streams 0x%llx not found on %s:%u\n",
			streams_mask & ~found_streams, sd->entity.name, pad);
		ret = -EINVAL;
		goto done;
	}

	dev_dbg(dev, "enable streams %u:%#llx\n", pad, streams_mask);

	/* Call the .enable_streams() operation. */
	ret = v4l2_subdev_call(sd, pad, enable_streams, state, pad,
			       streams_mask);
	if (ret) {
		dev_dbg(dev, "enable streams %u:%#llx failed: %d\n", pad,
			streams_mask, ret);
		goto done;
	}

	/* Mark the streams as enabled. */
	for (i = 0; i < state->stream_configs.num_configs; ++i) {
		struct v4l2_subdev_stream_config *cfg =
			&state->stream_configs.configs[i];

		if (cfg->pad == pad && (streams_mask & BIT_ULL(cfg->stream)))
			cfg->enabled = true;
	}

done:
	v4l2_subdev_unlock_state(state);

	return ret;
}
EXPORT_SYMBOL_GPL(v4l2_subdev_enable_streams);

static int v4l2_subdev_disable_streams_fallback(struct v4l2_subdev *sd, u32 pad,
						u64 streams_mask)
{
	struct device *dev = sd->entity.graph_obj.mdev->dev;
	unsigned int i;
	int ret;

	/*
	 * If the subdev doesn't implement pad-based stream enable, fall  back
	 * on the .s_stream() operation. This can only be done for subdevs that
	 * have a single source pad, as sd->enabled_streams is global to the
	 * subdev.
	 */
	if (!(sd->entity.pads[pad].flags & MEDIA_PAD_FL_SOURCE))
		return -EOPNOTSUPP;

	for (i = 0; i < sd->entity.num_pads; ++i) {
		if (i != pad && sd->entity.pads[i].flags & MEDIA_PAD_FL_SOURCE)
			return -EOPNOTSUPP;
	}

	if ((sd->enabled_streams & streams_mask) != streams_mask) {
		dev_dbg(dev, "set of streams %#llx already disabled on %s:%u\n",
			streams_mask, sd->entity.name, pad);
		return -EALREADY;
	}

	/* Stop streaming when the last streams are disabled. */
	if (!(sd->enabled_streams & ~streams_mask)) {
		ret = v4l2_subdev_call(sd, video, s_stream, 0);
		if (ret)
			return ret;
	}

	sd->enabled_streams &= ~streams_mask;

	return 0;
}

int v4l2_subdev_disable_streams(struct v4l2_subdev *sd, u32 pad,
				u64 streams_mask)
{
	struct device *dev = sd->entity.graph_obj.mdev->dev;
	struct v4l2_subdev_state *state;
	u64 found_streams = 0;
	unsigned int i;
	int ret;

	/* A few basic sanity checks first. */
	if (pad >= sd->entity.num_pads)
		return -EINVAL;

	if (!streams_mask)
		return 0;

	/* Fallback on .s_stream() if .disable_streams() isn't available. */
	if (!sd->ops->pad || !sd->ops->pad->disable_streams)
		return v4l2_subdev_disable_streams_fallback(sd, pad,
							    streams_mask);

	state = v4l2_subdev_lock_and_get_active_state(sd);

	/*
	 * Verify that the requested streams exist and that they are not
	 * already disabled.
	 */
	for (i = 0; i < state->stream_configs.num_configs; ++i) {
		struct v4l2_subdev_stream_config *cfg =
			&state->stream_configs.configs[i];

		if (cfg->pad != pad || !(streams_mask & BIT_ULL(cfg->stream)))
			continue;

		found_streams |= BIT_ULL(cfg->stream);

		if (!cfg->enabled) {
			dev_dbg(dev, "stream %u already disabled on %s:%u\n",
				cfg->stream, sd->entity.name, pad);
			ret = -EALREADY;
			goto done;
		}
	}

	if (found_streams != streams_mask) {
		dev_dbg(dev, "streams 0x%llx not found on %s:%u\n",
			streams_mask & ~found_streams, sd->entity.name, pad);
		ret = -EINVAL;
		goto done;
	}

	dev_dbg(dev, "disable streams %u:%#llx\n", pad, streams_mask);

	/* Call the .disable_streams() operation. */
	ret = v4l2_subdev_call(sd, pad, disable_streams, state, pad,
			       streams_mask);
	if (ret) {
		dev_dbg(dev, "disable streams %u:%#llx failed: %d\n", pad,
			streams_mask, ret);
		goto done;
	}

	/* Mark the streams as disabled. */
	for (i = 0; i < state->stream_configs.num_configs; ++i) {
		struct v4l2_subdev_stream_config *cfg =
			&state->stream_configs.configs[i];

		if (cfg->pad == pad && (streams_mask & BIT_ULL(cfg->stream)))
			cfg->enabled = false;
	}

done:
	v4l2_subdev_unlock_state(state);

	return ret;
}
EXPORT_SYMBOL_GPL(v4l2_subdev_disable_streams);

int v4l2_subdev_s_stream_helper(struct v4l2_subdev *sd, int enable)
{
	struct v4l2_subdev_state *state;
	struct v4l2_subdev_route *route;
	struct media_pad *pad;
	u64 source_mask = 0;
	int pad_index = -1;

	/*
	 * Find the source pad. This helper is meant for subdevs that have a
	 * single source pad, so failures shouldn't happen, but catch them
	 * loudly nonetheless as they indicate a driver bug.
	 */
	media_entity_for_each_pad(&sd->entity, pad) {
		if (pad->flags & MEDIA_PAD_FL_SOURCE) {
			pad_index = pad->index;
			break;
		}
	}

	if (WARN_ON(pad_index == -1))
		return -EINVAL;

	/*
	 * As there's a single source pad, just collect all the source streams.
	 */
	state = v4l2_subdev_lock_and_get_active_state(sd);

	for_each_active_route(&state->routing, route)
		source_mask |= BIT_ULL(route->source_stream);

	v4l2_subdev_unlock_state(state);

	if (enable)
		return v4l2_subdev_enable_streams(sd, pad_index, source_mask);
	else
		return v4l2_subdev_disable_streams(sd, pad_index, source_mask);
}
EXPORT_SYMBOL_GPL(v4l2_subdev_s_stream_helper);

#endif /* CONFIG_VIDEO_V4L2_SUBDEV_API */

#endif /* CONFIG_MEDIA_CONTROLLER */

void v4l2_subdev_init(struct v4l2_subdev *sd, const struct v4l2_subdev_ops *ops)
{
	INIT_LIST_HEAD(&sd->list);
	BUG_ON(!ops);
	sd->ops = ops;
	sd->v4l2_dev = NULL;
	sd->flags = 0;
	sd->name[0] = '\0';
	sd->grp_id = 0;
	sd->dev_priv = NULL;
	sd->host_priv = NULL;
	sd->privacy_led = NULL;
	INIT_LIST_HEAD(&sd->async_subdev_endpoint_list);
#if defined(CONFIG_MEDIA_CONTROLLER)
	sd->entity.name = sd->name;
	sd->entity.obj_type = MEDIA_ENTITY_TYPE_V4L2_SUBDEV;
	sd->entity.function = MEDIA_ENT_F_V4L2_SUBDEV_UNKNOWN;
#endif
}
EXPORT_SYMBOL(v4l2_subdev_init);

void v4l2_subdev_notify_event(struct v4l2_subdev *sd,
			      const struct v4l2_event *ev)
{
	v4l2_event_queue(sd->devnode, ev);
	v4l2_subdev_notify(sd, V4L2_DEVICE_NOTIFY_EVENT, (void *)ev);
}
EXPORT_SYMBOL_GPL(v4l2_subdev_notify_event);

int v4l2_subdev_get_privacy_led(struct v4l2_subdev *sd)
{
#if IS_REACHABLE(CONFIG_LEDS_CLASS)
	sd->privacy_led = led_get(sd->dev, "privacy-led");
	if (IS_ERR(sd->privacy_led) && PTR_ERR(sd->privacy_led) != -ENOENT)
		return dev_err_probe(sd->dev, PTR_ERR(sd->privacy_led),
				     "getting privacy LED\n");

	if (!IS_ERR_OR_NULL(sd->privacy_led)) {
		mutex_lock(&sd->privacy_led->led_access);
		led_sysfs_disable(sd->privacy_led);
		led_trigger_remove(sd->privacy_led);
		led_set_brightness(sd->privacy_led, 0);
		mutex_unlock(&sd->privacy_led->led_access);
	}
#endif
	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_subdev_get_privacy_led);

void v4l2_subdev_put_privacy_led(struct v4l2_subdev *sd)
{
#if IS_REACHABLE(CONFIG_LEDS_CLASS)
	if (!IS_ERR_OR_NULL(sd->privacy_led)) {
		mutex_lock(&sd->privacy_led->led_access);
		led_sysfs_enable(sd->privacy_led);
		mutex_unlock(&sd->privacy_led->led_access);
		led_put(sd->privacy_led);
	}
#endif
}
EXPORT_SYMBOL_GPL(v4l2_subdev_put_privacy_led);
