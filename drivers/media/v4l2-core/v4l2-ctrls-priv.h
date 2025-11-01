/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * V4L2 controls framework private header.
 *
 * Copyright (C) 2010-2021  Hans Verkuil <hverkuil@kernel.org>
 */

#ifndef _V4L2_CTRLS_PRIV_H_
#define _V4L2_CTRLS_PRIV_H_

#define dprintk(vdev, fmt, arg...) do {					\
	if (!WARN_ON(!(vdev)) && ((vdev)->dev_debug & V4L2_DEV_DEBUG_CTRL)) \
		printk(KERN_DEBUG pr_fmt("%s: %s: " fmt),		\
		       __func__, video_device_node_name(vdev), ##arg);	\
} while (0)

#define has_op(master, op) \
	((master)->ops && (master)->ops->op)
#define call_op(master, op) \
	(has_op(master, op) ? (master)->ops->op(master) : 0)

static inline u32 node2id(struct list_head *node)
{
	return list_entry(node, struct v4l2_ctrl_ref, node)->ctrl->id;
}

/*
 * Small helper function to determine if the autocluster is set to manual
 * mode.
 */
static inline bool is_cur_manual(const struct v4l2_ctrl *master)
{
	return master->is_auto && master->cur.val == master->manual_mode_value;
}

/*
 * Small helper function to determine if the autocluster will be set to manual
 * mode.
 */
static inline bool is_new_manual(const struct v4l2_ctrl *master)
{
	return master->is_auto && master->val == master->manual_mode_value;
}

static inline u32 user_flags(const struct v4l2_ctrl *ctrl)
{
	u32 flags = ctrl->flags;

	if (ctrl->is_ptr)
		flags |= V4L2_CTRL_FLAG_HAS_PAYLOAD;

	return flags;
}

/* v4l2-ctrls-core.c */
void cur_to_new(struct v4l2_ctrl *ctrl);
void cur_to_req(struct v4l2_ctrl_ref *ref);
void new_to_cur(struct v4l2_fh *fh, struct v4l2_ctrl *ctrl, u32 ch_flags);
void new_to_req(struct v4l2_ctrl_ref *ref);
int req_to_new(struct v4l2_ctrl_ref *ref);
void send_initial_event(struct v4l2_fh *fh, struct v4l2_ctrl *ctrl);
void send_event(struct v4l2_fh *fh, struct v4l2_ctrl *ctrl, u32 changes);
int handler_new_ref(struct v4l2_ctrl_handler *hdl,
		    struct v4l2_ctrl *ctrl,
		    struct v4l2_ctrl_ref **ctrl_ref,
		    bool from_other_dev, bool allocate_req);
struct v4l2_ctrl_ref *find_ref(struct v4l2_ctrl_handler *hdl, u32 id);
struct v4l2_ctrl_ref *find_ref_lock(struct v4l2_ctrl_handler *hdl, u32 id);
int check_range(enum v4l2_ctrl_type type,
		s64 min, s64 max, u64 step, s64 def);
void update_from_auto_cluster(struct v4l2_ctrl *master);
int try_or_set_cluster(struct v4l2_fh *fh, struct v4l2_ctrl *master,
		       bool set, u32 ch_flags);

/* v4l2-ctrls-api.c */
int v4l2_g_ext_ctrls_common(struct v4l2_ctrl_handler *hdl,
			    struct v4l2_ext_controls *cs,
			    struct video_device *vdev);
int try_set_ext_ctrls_common(struct v4l2_fh *fh,
			     struct v4l2_ctrl_handler *hdl,
			     struct v4l2_ext_controls *cs,
			     struct video_device *vdev, bool set);

/* v4l2-ctrls-request.c */
void v4l2_ctrl_handler_init_request(struct v4l2_ctrl_handler *hdl);
void v4l2_ctrl_handler_free_request(struct v4l2_ctrl_handler *hdl);
int v4l2_g_ext_ctrls_request(struct v4l2_ctrl_handler *hdl, struct video_device *vdev,
			     struct media_device *mdev, struct v4l2_ext_controls *cs);
int try_set_ext_ctrls_request(struct v4l2_fh *fh,
			      struct v4l2_ctrl_handler *hdl,
			      struct video_device *vdev,
			      struct media_device *mdev,
			      struct v4l2_ext_controls *cs, bool set);

#endif
