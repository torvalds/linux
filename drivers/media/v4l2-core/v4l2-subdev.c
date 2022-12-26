// SPDX-License-Identifier: GPL-2.0-only
/*
 * V4L2 sub-device
 *
 * Copyright (C) 2010 Nokia Corporation
 *
 * Contact: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	    Sakari Ailus <sakari.ailus@iki.fi>
 */

#include <linux/ioctl.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/videodev2.h>
#include <linux/export.h>
#include <linux/version.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-event.h>

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

static int check_state_pads(u32 which, struct v4l2_subdev_state *state)
{
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
	       check_state_pads(format->which, state);
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
	       check_state_pads(code->which, state) ? :
	       sd->ops->pad->enum_mbus_code(sd, state, code);
}

static int call_enum_frame_size(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_frame_size_enum *fse)
{
	if (!fse)
		return -EINVAL;

	return check_which(fse->which) ? : check_pad(sd, fse->pad) ? :
	       check_state_pads(fse->which, state) ? :
	       sd->ops->pad->enum_frame_size(sd, state, fse);
}

static inline int check_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_frame_interval *fi)
{
	if (!fi)
		return -EINVAL;

	return check_pad(sd, fi->pad);
}

static int call_g_frame_interval(struct v4l2_subdev *sd,
				 struct v4l2_subdev_frame_interval *fi)
{
	return check_frame_interval(sd, fi) ? :
	       sd->ops->video->g_frame_interval(sd, fi);
}

static int call_s_frame_interval(struct v4l2_subdev *sd,
				 struct v4l2_subdev_frame_interval *fi)
{
	return check_frame_interval(sd, fi) ? :
	       sd->ops->video->s_frame_interval(sd, fi);
}

static int call_enum_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *state,
				    struct v4l2_subdev_frame_interval_enum *fie)
{
	if (!fie)
		return -EINVAL;

	return check_which(fie->which) ? : check_pad(sd, fie->pad) ? :
	       check_state_pads(fie->which, state) ? :
	       sd->ops->pad->enum_frame_interval(sd, state, fie);
}

static inline int check_selection(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_selection *sel)
{
	if (!sel)
		return -EINVAL;

	return check_which(sel->which) ? : check_pad(sd, sel->pad) ? :
	       check_state_pads(sel->which, state);
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

	ret = sd->ops->video->s_stream(sd, enable);

	if (!enable && ret < 0) {
		dev_warn(sd->dev, "disabling streaming failed (%d)\n", ret);
		return 0;
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
	.get_edid		= call_get_edid,
	.set_edid		= call_set_edid,
	.dv_timings_cap		= call_dv_timings_cap,
	.enum_dv_timings	= call_enum_dv_timings,
	.get_mbus_config	= call_get_mbus_config,
};

static const struct v4l2_subdev_video_ops v4l2_subdev_call_video_wrappers = {
	.g_frame_interval	= call_g_frame_interval,
	.s_frame_interval	= call_s_frame_interval,
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
	bool ro_subdev = test_bit(V4L2_FL_SUBDEV_RO_DEVNODE, &vdev->flags);
	int rval;

	switch (cmd) {
	case VIDIOC_SUBDEV_QUERYCAP: {
		struct v4l2_subdev_capability *cap = arg;

		memset(cap->reserved, 0, sizeof(cap->reserved));
		cap->version = LINUX_VERSION_CODE;
		cap->capabilities = ro_subdev ? V4L2_SUBDEV_CAP_RO_SUBDEV : 0;

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

		memset(format->reserved, 0, sizeof(format->reserved));
		memset(format->format.reserved, 0, sizeof(format->format.reserved));
		return v4l2_subdev_call(sd, pad, get_fmt, state, format);
	}

	case VIDIOC_SUBDEV_S_FMT: {
		struct v4l2_subdev_format *format = arg;

		if (format->which != V4L2_SUBDEV_FORMAT_TRY && ro_subdev)
			return -EPERM;

		memset(format->reserved, 0, sizeof(format->reserved));
		memset(format->format.reserved, 0, sizeof(format->format.reserved));
		return v4l2_subdev_call(sd, pad, set_fmt, state, format);
	}

	case VIDIOC_SUBDEV_G_CROP: {
		struct v4l2_subdev_crop *crop = arg;
		struct v4l2_subdev_selection sel;

		memset(crop->reserved, 0, sizeof(crop->reserved));
		memset(&sel, 0, sizeof(sel));
		sel.which = crop->which;
		sel.pad = crop->pad;
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

		memset(crop->reserved, 0, sizeof(crop->reserved));
		memset(&sel, 0, sizeof(sel));
		sel.which = crop->which;
		sel.pad = crop->pad;
		sel.target = V4L2_SEL_TGT_CROP;
		sel.r = crop->rect;

		rval = v4l2_subdev_call(
			sd, pad, set_selection, state, &sel);

		crop->rect = sel.r;

		return rval;
	}

	case VIDIOC_SUBDEV_ENUM_MBUS_CODE: {
		struct v4l2_subdev_mbus_code_enum *code = arg;

		memset(code->reserved, 0, sizeof(code->reserved));
		return v4l2_subdev_call(sd, pad, enum_mbus_code, state,
					code);
	}

	case VIDIOC_SUBDEV_ENUM_FRAME_SIZE: {
		struct v4l2_subdev_frame_size_enum *fse = arg;

		memset(fse->reserved, 0, sizeof(fse->reserved));
		return v4l2_subdev_call(sd, pad, enum_frame_size, state,
					fse);
	}

	case VIDIOC_SUBDEV_G_FRAME_INTERVAL: {
		struct v4l2_subdev_frame_interval *fi = arg;

		memset(fi->reserved, 0, sizeof(fi->reserved));
		return v4l2_subdev_call(sd, video, g_frame_interval, arg);
	}

	case VIDIOC_SUBDEV_S_FRAME_INTERVAL: {
		struct v4l2_subdev_frame_interval *fi = arg;

		if (ro_subdev)
			return -EPERM;

		memset(fi->reserved, 0, sizeof(fi->reserved));
		return v4l2_subdev_call(sd, video, s_frame_interval, arg);
	}

	case VIDIOC_SUBDEV_ENUM_FRAME_INTERVAL: {
		struct v4l2_subdev_frame_interval_enum *fie = arg;

		memset(fie->reserved, 0, sizeof(fie->reserved));
		return v4l2_subdev_call(sd, pad, enum_frame_interval, state,
					fie);
	}

	case VIDIOC_SUBDEV_G_SELECTION: {
		struct v4l2_subdev_selection *sel = arg;

		memset(sel->reserved, 0, sizeof(sel->reserved));
		return v4l2_subdev_call(
			sd, pad, get_selection, state, sel);
	}

	case VIDIOC_SUBDEV_S_SELECTION: {
		struct v4l2_subdev_selection *sel = arg;

		if (sel->which != V4L2_SUBDEV_FORMAT_TRY && ro_subdev)
			return -EPERM;

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
		return v4l2_subdev_call(sd, video, query_dv_timings, arg);

	case VIDIOC_SUBDEV_G_DV_TIMINGS:
		return v4l2_subdev_call(sd, video, g_dv_timings, arg);

	case VIDIOC_SUBDEV_S_DV_TIMINGS:
		if (ro_subdev)
			return -EPERM;

		return v4l2_subdev_call(sd, video, s_dv_timings, arg);

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
v4l2_subdev_link_validate_get_format(struct media_pad *pad,
				     struct v4l2_subdev_format *fmt)
{
	if (is_media_entity_v4l2_subdev(pad->entity)) {
		struct v4l2_subdev *sd =
			media_entity_to_v4l2_subdev(pad->entity);

		fmt->which = V4L2_SUBDEV_FORMAT_ACTIVE;
		fmt->pad = pad->index;
		return v4l2_subdev_call_state_active(sd, pad, get_fmt, fmt);
	}

	WARN(pad->entity->function != MEDIA_ENT_F_IO_V4L,
	     "Driver bug! Wrong media entity type 0x%08x, entity %s\n",
	     pad->entity->function, pad->entity->name);

	return -EINVAL;
}

int v4l2_subdev_link_validate(struct media_link *link)
{
	struct v4l2_subdev *sink;
	struct v4l2_subdev_format sink_fmt, source_fmt;
	int rval;

	rval = v4l2_subdev_link_validate_get_format(
		link->source, &source_fmt);
	if (rval < 0)
		return 0;

	rval = v4l2_subdev_link_validate_get_format(
		link->sink, &sink_fmt);
	if (rval < 0)
		return 0;

	sink = media_entity_to_v4l2_subdev(link->sink->entity);

	rval = v4l2_subdev_call(sink, pad, link_validate, link,
				&source_fmt, &sink_fmt);
	if (rval != -ENOIOCTLCMD)
		return rval;

	return v4l2_subdev_link_validate_default(
		sink, link, &source_fmt, &sink_fmt);
}
EXPORT_SYMBOL_GPL(v4l2_subdev_link_validate);

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

	if (sd->entity.num_pads) {
		state->pads = kvcalloc(sd->entity.num_pads,
				       sizeof(*state->pads), GFP_KERNEL);
		if (!state->pads) {
			ret = -ENOMEM;
			goto err;
		}
	}

	/*
	 * There can be no race at this point, but we lock the state anyway to
	 * satisfy lockdep checks.
	 */
	v4l2_subdev_lock_state(state);
	ret = v4l2_subdev_call(sd, pad, init_cfg, state);
	v4l2_subdev_unlock_state(state);

	if (ret < 0 && ret != -ENOIOCTLCMD)
		goto err;

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
	__v4l2_subdev_state_free(sd->active_state);
	sd->active_state = NULL;
}
EXPORT_SYMBOL_GPL(v4l2_subdev_cleanup);

#if defined(CONFIG_VIDEO_V4L2_SUBDEV_API)

int v4l2_subdev_get_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_state *state,
			struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt;

	if (format->pad >= sd->entity.num_pads)
		return -EINVAL;

	fmt = v4l2_subdev_get_pad_format(sd, state, format->pad);
	if (!fmt)
		return -EINVAL;

	format->format = *fmt;

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_subdev_get_fmt);

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
