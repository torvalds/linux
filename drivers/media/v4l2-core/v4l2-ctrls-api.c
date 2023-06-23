// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * V4L2 controls framework uAPI implementation:
 *
 * Copyright (C) 2010-2021  Hans Verkuil <hverkuil-cisco@xs4all.nl>
 */

#define pr_fmt(fmt) "v4l2-ctrls: " fmt

#include <linux/export.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>

#include "v4l2-ctrls-priv.h"

/* Internal temporary helper struct, one for each v4l2_ext_control */
struct v4l2_ctrl_helper {
	/* Pointer to the control reference of the master control */
	struct v4l2_ctrl_ref *mref;
	/* The control ref corresponding to the v4l2_ext_control ID field. */
	struct v4l2_ctrl_ref *ref;
	/*
	 * v4l2_ext_control index of the next control belonging to the
	 * same cluster, or 0 if there isn't any.
	 */
	u32 next;
};

/*
 * Helper functions to copy control payload data from kernel space to
 * user space and vice versa.
 */

/* Helper function: copy the given control value back to the caller */
static int ptr_to_user(struct v4l2_ext_control *c,
		       struct v4l2_ctrl *ctrl,
		       union v4l2_ctrl_ptr ptr)
{
	u32 len;

	if (ctrl->is_ptr && !ctrl->is_string)
		return copy_to_user(c->ptr, ptr.p_const, c->size) ?
		       -EFAULT : 0;

	switch (ctrl->type) {
	case V4L2_CTRL_TYPE_STRING:
		len = strlen(ptr.p_char);
		if (c->size < len + 1) {
			c->size = ctrl->elem_size;
			return -ENOSPC;
		}
		return copy_to_user(c->string, ptr.p_char, len + 1) ?
		       -EFAULT : 0;
	case V4L2_CTRL_TYPE_INTEGER64:
		c->value64 = *ptr.p_s64;
		break;
	default:
		c->value = *ptr.p_s32;
		break;
	}
	return 0;
}

/* Helper function: copy the current control value back to the caller */
static int cur_to_user(struct v4l2_ext_control *c, struct v4l2_ctrl *ctrl)
{
	return ptr_to_user(c, ctrl, ctrl->p_cur);
}

/* Helper function: copy the new control value back to the caller */
static int new_to_user(struct v4l2_ext_control *c,
		       struct v4l2_ctrl *ctrl)
{
	return ptr_to_user(c, ctrl, ctrl->p_new);
}

/* Helper function: copy the request value back to the caller */
static int req_to_user(struct v4l2_ext_control *c,
		       struct v4l2_ctrl_ref *ref)
{
	return ptr_to_user(c, ref->ctrl, ref->p_req);
}

/* Helper function: copy the initial control value back to the caller */
static int def_to_user(struct v4l2_ext_control *c, struct v4l2_ctrl *ctrl)
{
	int idx;

	for (idx = 0; idx < ctrl->elems; idx++)
		ctrl->type_ops->init(ctrl, idx, ctrl->p_new);

	return ptr_to_user(c, ctrl, ctrl->p_new);
}

/* Helper function: copy the caller-provider value as the new control value */
static int user_to_new(struct v4l2_ext_control *c, struct v4l2_ctrl *ctrl)
{
	int ret;
	u32 size;

	ctrl->is_new = 0;
	if (ctrl->is_dyn_array &&
	    c->size > ctrl->p_dyn_alloc_elems * ctrl->elem_size) {
		void *old = ctrl->p_dyn;
		void *tmp = kvzalloc(2 * c->size, GFP_KERNEL);

		if (!tmp)
			return -ENOMEM;
		memcpy(tmp, ctrl->p_new.p, ctrl->elems * ctrl->elem_size);
		memcpy(tmp + c->size, ctrl->p_cur.p, ctrl->elems * ctrl->elem_size);
		ctrl->p_new.p = tmp;
		ctrl->p_cur.p = tmp + c->size;
		ctrl->p_dyn = tmp;
		ctrl->p_dyn_alloc_elems = c->size / ctrl->elem_size;
		kvfree(old);
	}

	if (ctrl->is_ptr && !ctrl->is_string) {
		unsigned int elems = c->size / ctrl->elem_size;
		unsigned int idx;

		if (copy_from_user(ctrl->p_new.p, c->ptr, c->size))
			return -EFAULT;
		ctrl->is_new = 1;
		if (ctrl->is_dyn_array)
			ctrl->new_elems = elems;
		else if (ctrl->is_array)
			for (idx = elems; idx < ctrl->elems; idx++)
				ctrl->type_ops->init(ctrl, idx, ctrl->p_new);
		return 0;
	}

	switch (ctrl->type) {
	case V4L2_CTRL_TYPE_INTEGER64:
		*ctrl->p_new.p_s64 = c->value64;
		break;
	case V4L2_CTRL_TYPE_STRING:
		size = c->size;
		if (size == 0)
			return -ERANGE;
		if (size > ctrl->maximum + 1)
			size = ctrl->maximum + 1;
		ret = copy_from_user(ctrl->p_new.p_char, c->string, size) ? -EFAULT : 0;
		if (!ret) {
			char last = ctrl->p_new.p_char[size - 1];

			ctrl->p_new.p_char[size - 1] = 0;
			/*
			 * If the string was longer than ctrl->maximum,
			 * then return an error.
			 */
			if (strlen(ctrl->p_new.p_char) == ctrl->maximum && last)
				return -ERANGE;
		}
		return ret;
	default:
		*ctrl->p_new.p_s32 = c->value;
		break;
	}
	ctrl->is_new = 1;
	return 0;
}

/*
 * VIDIOC_G/TRY/S_EXT_CTRLS implementation
 */

/*
 * Some general notes on the atomic requirements of VIDIOC_G/TRY/S_EXT_CTRLS:
 *
 * It is not a fully atomic operation, just best-effort only. After all, if
 * multiple controls have to be set through multiple i2c writes (for example)
 * then some initial writes may succeed while others fail. Thus leaving the
 * system in an inconsistent state. The question is how much effort you are
 * willing to spend on trying to make something atomic that really isn't.
 *
 * From the point of view of an application the main requirement is that
 * when you call VIDIOC_S_EXT_CTRLS and some values are invalid then an
 * error should be returned without actually affecting any controls.
 *
 * If all the values are correct, then it is acceptable to just give up
 * in case of low-level errors.
 *
 * It is important though that the application can tell when only a partial
 * configuration was done. The way we do that is through the error_idx field
 * of struct v4l2_ext_controls: if that is equal to the count field then no
 * controls were affected. Otherwise all controls before that index were
 * successful in performing their 'get' or 'set' operation, the control at
 * the given index failed, and you don't know what happened with the controls
 * after the failed one. Since if they were part of a control cluster they
 * could have been successfully processed (if a cluster member was encountered
 * at index < error_idx), they could have failed (if a cluster member was at
 * error_idx), or they may not have been processed yet (if the first cluster
 * member appeared after error_idx).
 *
 * It is all fairly theoretical, though. In practice all you can do is to
 * bail out. If error_idx == count, then it is an application bug. If
 * error_idx < count then it is only an application bug if the error code was
 * EBUSY. That usually means that something started streaming just when you
 * tried to set the controls. In all other cases it is a driver/hardware
 * problem and all you can do is to retry or bail out.
 *
 * Note that these rules do not apply to VIDIOC_TRY_EXT_CTRLS: since that
 * never modifies controls the error_idx is just set to whatever control
 * has an invalid value.
 */

/*
 * Prepare for the extended g/s/try functions.
 * Find the controls in the control array and do some basic checks.
 */
static int prepare_ext_ctrls(struct v4l2_ctrl_handler *hdl,
			     struct v4l2_ext_controls *cs,
			     struct v4l2_ctrl_helper *helpers,
			     struct video_device *vdev,
			     bool get)
{
	struct v4l2_ctrl_helper *h;
	bool have_clusters = false;
	u32 i;

	for (i = 0, h = helpers; i < cs->count; i++, h++) {
		struct v4l2_ext_control *c = &cs->controls[i];
		struct v4l2_ctrl_ref *ref;
		struct v4l2_ctrl *ctrl;
		u32 id = c->id & V4L2_CTRL_ID_MASK;

		cs->error_idx = i;

		if (cs->which &&
		    cs->which != V4L2_CTRL_WHICH_DEF_VAL &&
		    cs->which != V4L2_CTRL_WHICH_REQUEST_VAL &&
		    V4L2_CTRL_ID2WHICH(id) != cs->which) {
			dprintk(vdev,
				"invalid which 0x%x or control id 0x%x\n",
				cs->which, id);
			return -EINVAL;
		}

		/*
		 * Old-style private controls are not allowed for
		 * extended controls.
		 */
		if (id >= V4L2_CID_PRIVATE_BASE) {
			dprintk(vdev,
				"old-style private controls not allowed\n");
			return -EINVAL;
		}
		ref = find_ref_lock(hdl, id);
		if (!ref) {
			dprintk(vdev, "cannot find control id 0x%x\n", id);
			return -EINVAL;
		}
		h->ref = ref;
		ctrl = ref->ctrl;
		if (ctrl->flags & V4L2_CTRL_FLAG_DISABLED) {
			dprintk(vdev, "control id 0x%x is disabled\n", id);
			return -EINVAL;
		}

		if (ctrl->cluster[0]->ncontrols > 1)
			have_clusters = true;
		if (ctrl->cluster[0] != ctrl)
			ref = find_ref_lock(hdl, ctrl->cluster[0]->id);
		if (ctrl->is_dyn_array) {
			unsigned int max_size = ctrl->dims[0] * ctrl->elem_size;
			unsigned int tot_size = ctrl->elem_size;

			if (cs->which == V4L2_CTRL_WHICH_REQUEST_VAL)
				tot_size *= ref->p_req_elems;
			else
				tot_size *= ctrl->elems;

			c->size = ctrl->elem_size * (c->size / ctrl->elem_size);
			if (get) {
				if (c->size < tot_size) {
					c->size = tot_size;
					return -ENOSPC;
				}
				c->size = tot_size;
			} else {
				if (c->size > max_size) {
					c->size = max_size;
					return -ENOSPC;
				}
				if (!c->size)
					return -EFAULT;
			}
		} else if (ctrl->is_ptr && !ctrl->is_string) {
			unsigned int tot_size = ctrl->elems * ctrl->elem_size;

			if (c->size < tot_size) {
				/*
				 * In the get case the application first
				 * queries to obtain the size of the control.
				 */
				if (get) {
					c->size = tot_size;
					return -ENOSPC;
				}
				dprintk(vdev,
					"pointer control id 0x%x size too small, %d bytes but %d bytes needed\n",
					id, c->size, tot_size);
				return -EFAULT;
			}
			c->size = tot_size;
		}
		/* Store the ref to the master control of the cluster */
		h->mref = ref;
		/*
		 * Initially set next to 0, meaning that there is no other
		 * control in this helper array belonging to the same
		 * cluster.
		 */
		h->next = 0;
	}

	/*
	 * We are done if there were no controls that belong to a multi-
	 * control cluster.
	 */
	if (!have_clusters)
		return 0;

	/*
	 * The code below figures out in O(n) time which controls in the list
	 * belong to the same cluster.
	 */

	/* This has to be done with the handler lock taken. */
	mutex_lock(hdl->lock);

	/* First zero the helper field in the master control references */
	for (i = 0; i < cs->count; i++)
		helpers[i].mref->helper = NULL;
	for (i = 0, h = helpers; i < cs->count; i++, h++) {
		struct v4l2_ctrl_ref *mref = h->mref;

		/*
		 * If the mref->helper is set, then it points to an earlier
		 * helper that belongs to the same cluster.
		 */
		if (mref->helper) {
			/*
			 * Set the next field of mref->helper to the current
			 * index: this means that the earlier helper now
			 * points to the next helper in the same cluster.
			 */
			mref->helper->next = i;
			/*
			 * mref should be set only for the first helper in the
			 * cluster, clear the others.
			 */
			h->mref = NULL;
		}
		/* Point the mref helper to the current helper struct. */
		mref->helper = h;
	}
	mutex_unlock(hdl->lock);
	return 0;
}

/*
 * Handles the corner case where cs->count == 0. It checks whether the
 * specified control class exists. If that class ID is 0, then it checks
 * whether there are any controls at all.
 */
static int class_check(struct v4l2_ctrl_handler *hdl, u32 which)
{
	if (which == 0 || which == V4L2_CTRL_WHICH_DEF_VAL ||
	    which == V4L2_CTRL_WHICH_REQUEST_VAL)
		return 0;
	return find_ref_lock(hdl, which | 1) ? 0 : -EINVAL;
}

/*
 * Get extended controls. Allocates the helpers array if needed.
 *
 * Note that v4l2_g_ext_ctrls_common() with 'which' set to
 * V4L2_CTRL_WHICH_REQUEST_VAL is only called if the request was
 * completed, and in that case p_req_valid is true for all controls.
 */
int v4l2_g_ext_ctrls_common(struct v4l2_ctrl_handler *hdl,
			    struct v4l2_ext_controls *cs,
			    struct video_device *vdev)
{
	struct v4l2_ctrl_helper helper[4];
	struct v4l2_ctrl_helper *helpers = helper;
	int ret;
	int i, j;
	bool is_default, is_request;

	is_default = (cs->which == V4L2_CTRL_WHICH_DEF_VAL);
	is_request = (cs->which == V4L2_CTRL_WHICH_REQUEST_VAL);

	cs->error_idx = cs->count;
	cs->which = V4L2_CTRL_ID2WHICH(cs->which);

	if (!hdl)
		return -EINVAL;

	if (cs->count == 0)
		return class_check(hdl, cs->which);

	if (cs->count > ARRAY_SIZE(helper)) {
		helpers = kvmalloc_array(cs->count, sizeof(helper[0]),
					 GFP_KERNEL);
		if (!helpers)
			return -ENOMEM;
	}

	ret = prepare_ext_ctrls(hdl, cs, helpers, vdev, true);
	cs->error_idx = cs->count;

	for (i = 0; !ret && i < cs->count; i++)
		if (helpers[i].ref->ctrl->flags & V4L2_CTRL_FLAG_WRITE_ONLY)
			ret = -EACCES;

	for (i = 0; !ret && i < cs->count; i++) {
		struct v4l2_ctrl *master;
		bool is_volatile = false;
		u32 idx = i;

		if (!helpers[i].mref)
			continue;

		master = helpers[i].mref->ctrl;
		cs->error_idx = i;

		v4l2_ctrl_lock(master);

		/*
		 * g_volatile_ctrl will update the new control values.
		 * This makes no sense for V4L2_CTRL_WHICH_DEF_VAL and
		 * V4L2_CTRL_WHICH_REQUEST_VAL. In the case of requests
		 * it is v4l2_ctrl_request_complete() that copies the
		 * volatile controls at the time of request completion
		 * to the request, so you don't want to do that again.
		 */
		if (!is_default && !is_request &&
		    ((master->flags & V4L2_CTRL_FLAG_VOLATILE) ||
		    (master->has_volatiles && !is_cur_manual(master)))) {
			for (j = 0; j < master->ncontrols; j++)
				cur_to_new(master->cluster[j]);
			ret = call_op(master, g_volatile_ctrl);
			is_volatile = true;
		}

		if (ret) {
			v4l2_ctrl_unlock(master);
			break;
		}

		/*
		 * Copy the default value (if is_default is true), the
		 * request value (if is_request is true and p_req is valid),
		 * the new volatile value (if is_volatile is true) or the
		 * current value.
		 */
		do {
			struct v4l2_ctrl_ref *ref = helpers[idx].ref;

			if (is_default)
				ret = def_to_user(cs->controls + idx, ref->ctrl);
			else if (is_request && ref->p_req_dyn_enomem)
				ret = -ENOMEM;
			else if (is_request && ref->p_req_valid)
				ret = req_to_user(cs->controls + idx, ref);
			else if (is_volatile)
				ret = new_to_user(cs->controls + idx, ref->ctrl);
			else
				ret = cur_to_user(cs->controls + idx, ref->ctrl);
			idx = helpers[idx].next;
		} while (!ret && idx);

		v4l2_ctrl_unlock(master);
	}

	if (cs->count > ARRAY_SIZE(helper))
		kvfree(helpers);
	return ret;
}

int v4l2_g_ext_ctrls(struct v4l2_ctrl_handler *hdl, struct video_device *vdev,
		     struct media_device *mdev, struct v4l2_ext_controls *cs)
{
	if (cs->which == V4L2_CTRL_WHICH_REQUEST_VAL)
		return v4l2_g_ext_ctrls_request(hdl, vdev, mdev, cs);

	return v4l2_g_ext_ctrls_common(hdl, cs, vdev);
}
EXPORT_SYMBOL(v4l2_g_ext_ctrls);

/* Validate a new control */
static int validate_new(const struct v4l2_ctrl *ctrl, union v4l2_ctrl_ptr p_new)
{
	unsigned int idx;
	int err = 0;

	for (idx = 0; !err && idx < ctrl->new_elems; idx++)
		err = ctrl->type_ops->validate(ctrl, idx, p_new);
	return err;
}

/* Validate controls. */
static int validate_ctrls(struct v4l2_ext_controls *cs,
			  struct v4l2_ctrl_helper *helpers,
			  struct video_device *vdev,
			  bool set)
{
	unsigned int i;
	int ret = 0;

	cs->error_idx = cs->count;
	for (i = 0; i < cs->count; i++) {
		struct v4l2_ctrl *ctrl = helpers[i].ref->ctrl;
		union v4l2_ctrl_ptr p_new;

		cs->error_idx = i;

		if (ctrl->flags & V4L2_CTRL_FLAG_READ_ONLY) {
			dprintk(vdev,
				"control id 0x%x is read-only\n",
				ctrl->id);
			return -EACCES;
		}
		/*
		 * This test is also done in try_set_control_cluster() which
		 * is called in atomic context, so that has the final say,
		 * but it makes sense to do an up-front check as well. Once
		 * an error occurs in try_set_control_cluster() some other
		 * controls may have been set already and we want to do a
		 * best-effort to avoid that.
		 */
		if (set && (ctrl->flags & V4L2_CTRL_FLAG_GRABBED)) {
			dprintk(vdev,
				"control id 0x%x is grabbed, cannot set\n",
				ctrl->id);
			return -EBUSY;
		}
		/*
		 * Skip validation for now if the payload needs to be copied
		 * from userspace into kernelspace. We'll validate those later.
		 */
		if (ctrl->is_ptr)
			continue;
		if (ctrl->type == V4L2_CTRL_TYPE_INTEGER64)
			p_new.p_s64 = &cs->controls[i].value64;
		else
			p_new.p_s32 = &cs->controls[i].value;
		ret = validate_new(ctrl, p_new);
		if (ret)
			return ret;
	}
	return 0;
}

/* Try or try-and-set controls */
int try_set_ext_ctrls_common(struct v4l2_fh *fh,
			     struct v4l2_ctrl_handler *hdl,
			     struct v4l2_ext_controls *cs,
			     struct video_device *vdev, bool set)
{
	struct v4l2_ctrl_helper helper[4];
	struct v4l2_ctrl_helper *helpers = helper;
	unsigned int i, j;
	int ret;

	cs->error_idx = cs->count;

	/* Default value cannot be changed */
	if (cs->which == V4L2_CTRL_WHICH_DEF_VAL) {
		dprintk(vdev, "%s: cannot change default value\n",
			video_device_node_name(vdev));
		return -EINVAL;
	}

	cs->which = V4L2_CTRL_ID2WHICH(cs->which);

	if (!hdl) {
		dprintk(vdev, "%s: invalid null control handler\n",
			video_device_node_name(vdev));
		return -EINVAL;
	}

	if (cs->count == 0)
		return class_check(hdl, cs->which);

	if (cs->count > ARRAY_SIZE(helper)) {
		helpers = kvmalloc_array(cs->count, sizeof(helper[0]),
					 GFP_KERNEL);
		if (!helpers)
			return -ENOMEM;
	}
	ret = prepare_ext_ctrls(hdl, cs, helpers, vdev, false);
	if (!ret)
		ret = validate_ctrls(cs, helpers, vdev, set);
	if (ret && set)
		cs->error_idx = cs->count;
	for (i = 0; !ret && i < cs->count; i++) {
		struct v4l2_ctrl *master;
		u32 idx = i;

		if (!helpers[i].mref)
			continue;

		cs->error_idx = i;
		master = helpers[i].mref->ctrl;
		v4l2_ctrl_lock(master);

		/* Reset the 'is_new' flags of the cluster */
		for (j = 0; j < master->ncontrols; j++)
			if (master->cluster[j])
				master->cluster[j]->is_new = 0;

		/*
		 * For volatile autoclusters that are currently in auto mode
		 * we need to discover if it will be set to manual mode.
		 * If so, then we have to copy the current volatile values
		 * first since those will become the new manual values (which
		 * may be overwritten by explicit new values from this set
		 * of controls).
		 */
		if (master->is_auto && master->has_volatiles &&
		    !is_cur_manual(master)) {
			/* Pick an initial non-manual value */
			s32 new_auto_val = master->manual_mode_value + 1;
			u32 tmp_idx = idx;

			do {
				/*
				 * Check if the auto control is part of the
				 * list, and remember the new value.
				 */
				if (helpers[tmp_idx].ref->ctrl == master)
					new_auto_val = cs->controls[tmp_idx].value;
				tmp_idx = helpers[tmp_idx].next;
			} while (tmp_idx);
			/*
			 * If the new value == the manual value, then copy
			 * the current volatile values.
			 */
			if (new_auto_val == master->manual_mode_value)
				update_from_auto_cluster(master);
		}

		/*
		 * Copy the new caller-supplied control values.
		 * user_to_new() sets 'is_new' to 1.
		 */
		do {
			struct v4l2_ctrl *ctrl = helpers[idx].ref->ctrl;

			ret = user_to_new(cs->controls + idx, ctrl);
			if (!ret && ctrl->is_ptr) {
				ret = validate_new(ctrl, ctrl->p_new);
				if (ret)
					dprintk(vdev,
						"failed to validate control %s (%d)\n",
						v4l2_ctrl_get_name(ctrl->id), ret);
			}
			idx = helpers[idx].next;
		} while (!ret && idx);

		if (!ret)
			ret = try_or_set_cluster(fh, master,
						 !hdl->req_obj.req && set, 0);
		if (!ret && hdl->req_obj.req && set) {
			for (j = 0; j < master->ncontrols; j++) {
				struct v4l2_ctrl_ref *ref =
					find_ref(hdl, master->cluster[j]->id);

				new_to_req(ref);
			}
		}

		/* Copy the new values back to userspace. */
		if (!ret) {
			idx = i;
			do {
				ret = new_to_user(cs->controls + idx,
						  helpers[idx].ref->ctrl);
				idx = helpers[idx].next;
			} while (!ret && idx);
		}
		v4l2_ctrl_unlock(master);
	}

	if (cs->count > ARRAY_SIZE(helper))
		kvfree(helpers);
	return ret;
}

static int try_set_ext_ctrls(struct v4l2_fh *fh,
			     struct v4l2_ctrl_handler *hdl,
			     struct video_device *vdev,
			     struct media_device *mdev,
			     struct v4l2_ext_controls *cs, bool set)
{
	int ret;

	if (cs->which == V4L2_CTRL_WHICH_REQUEST_VAL)
		return try_set_ext_ctrls_request(fh, hdl, vdev, mdev, cs, set);

	ret = try_set_ext_ctrls_common(fh, hdl, cs, vdev, set);
	if (ret)
		dprintk(vdev,
			"%s: try_set_ext_ctrls_common failed (%d)\n",
			video_device_node_name(vdev), ret);

	return ret;
}

int v4l2_try_ext_ctrls(struct v4l2_ctrl_handler *hdl,
		       struct video_device *vdev,
		       struct media_device *mdev,
		       struct v4l2_ext_controls *cs)
{
	return try_set_ext_ctrls(NULL, hdl, vdev, mdev, cs, false);
}
EXPORT_SYMBOL(v4l2_try_ext_ctrls);

int v4l2_s_ext_ctrls(struct v4l2_fh *fh,
		     struct v4l2_ctrl_handler *hdl,
		     struct video_device *vdev,
		     struct media_device *mdev,
		     struct v4l2_ext_controls *cs)
{
	return try_set_ext_ctrls(fh, hdl, vdev, mdev, cs, true);
}
EXPORT_SYMBOL(v4l2_s_ext_ctrls);

/*
 * VIDIOC_G/S_CTRL implementation
 */

/* Helper function to get a single control */
static int get_ctrl(struct v4l2_ctrl *ctrl, struct v4l2_ext_control *c)
{
	struct v4l2_ctrl *master = ctrl->cluster[0];
	int ret = 0;
	int i;

	/* Compound controls are not supported. The new_to_user() and
	 * cur_to_user() calls below would need to be modified not to access
	 * userspace memory when called from get_ctrl().
	 */
	if (!ctrl->is_int && ctrl->type != V4L2_CTRL_TYPE_INTEGER64)
		return -EINVAL;

	if (ctrl->flags & V4L2_CTRL_FLAG_WRITE_ONLY)
		return -EACCES;

	v4l2_ctrl_lock(master);
	/* g_volatile_ctrl will update the current control values */
	if (ctrl->flags & V4L2_CTRL_FLAG_VOLATILE) {
		for (i = 0; i < master->ncontrols; i++)
			cur_to_new(master->cluster[i]);
		ret = call_op(master, g_volatile_ctrl);
		new_to_user(c, ctrl);
	} else {
		cur_to_user(c, ctrl);
	}
	v4l2_ctrl_unlock(master);
	return ret;
}

int v4l2_g_ctrl(struct v4l2_ctrl_handler *hdl, struct v4l2_control *control)
{
	struct v4l2_ctrl *ctrl = v4l2_ctrl_find(hdl, control->id);
	struct v4l2_ext_control c;
	int ret;

	if (!ctrl || !ctrl->is_int)
		return -EINVAL;
	ret = get_ctrl(ctrl, &c);
	control->value = c.value;
	return ret;
}
EXPORT_SYMBOL(v4l2_g_ctrl);

/* Helper function for VIDIOC_S_CTRL compatibility */
static int set_ctrl(struct v4l2_fh *fh, struct v4l2_ctrl *ctrl, u32 ch_flags)
{
	struct v4l2_ctrl *master = ctrl->cluster[0];
	int ret;
	int i;

	/* Reset the 'is_new' flags of the cluster */
	for (i = 0; i < master->ncontrols; i++)
		if (master->cluster[i])
			master->cluster[i]->is_new = 0;

	ret = validate_new(ctrl, ctrl->p_new);
	if (ret)
		return ret;

	/*
	 * For autoclusters with volatiles that are switched from auto to
	 * manual mode we have to update the current volatile values since
	 * those will become the initial manual values after such a switch.
	 */
	if (master->is_auto && master->has_volatiles && ctrl == master &&
	    !is_cur_manual(master) && ctrl->val == master->manual_mode_value)
		update_from_auto_cluster(master);

	ctrl->is_new = 1;
	return try_or_set_cluster(fh, master, true, ch_flags);
}

/* Helper function for VIDIOC_S_CTRL compatibility */
static int set_ctrl_lock(struct v4l2_fh *fh, struct v4l2_ctrl *ctrl,
			 struct v4l2_ext_control *c)
{
	int ret;

	v4l2_ctrl_lock(ctrl);
	user_to_new(c, ctrl);
	ret = set_ctrl(fh, ctrl, 0);
	if (!ret)
		cur_to_user(c, ctrl);
	v4l2_ctrl_unlock(ctrl);
	return ret;
}

int v4l2_s_ctrl(struct v4l2_fh *fh, struct v4l2_ctrl_handler *hdl,
		struct v4l2_control *control)
{
	struct v4l2_ctrl *ctrl = v4l2_ctrl_find(hdl, control->id);
	struct v4l2_ext_control c = { control->id };
	int ret;

	if (!ctrl || !ctrl->is_int)
		return -EINVAL;

	if (ctrl->flags & V4L2_CTRL_FLAG_READ_ONLY)
		return -EACCES;

	c.value = control->value;
	ret = set_ctrl_lock(fh, ctrl, &c);
	control->value = c.value;
	return ret;
}
EXPORT_SYMBOL(v4l2_s_ctrl);

/*
 * Helper functions for drivers to get/set controls.
 */

s32 v4l2_ctrl_g_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_ext_control c;

	/* It's a driver bug if this happens. */
	if (WARN_ON(!ctrl->is_int))
		return 0;
	c.value = 0;
	get_ctrl(ctrl, &c);
	return c.value;
}
EXPORT_SYMBOL(v4l2_ctrl_g_ctrl);

s64 v4l2_ctrl_g_ctrl_int64(struct v4l2_ctrl *ctrl)
{
	struct v4l2_ext_control c;

	/* It's a driver bug if this happens. */
	if (WARN_ON(ctrl->is_ptr || ctrl->type != V4L2_CTRL_TYPE_INTEGER64))
		return 0;
	c.value64 = 0;
	get_ctrl(ctrl, &c);
	return c.value64;
}
EXPORT_SYMBOL(v4l2_ctrl_g_ctrl_int64);

int __v4l2_ctrl_s_ctrl(struct v4l2_ctrl *ctrl, s32 val)
{
	lockdep_assert_held(ctrl->handler->lock);

	/* It's a driver bug if this happens. */
	if (WARN_ON(!ctrl->is_int))
		return -EINVAL;
	ctrl->val = val;
	return set_ctrl(NULL, ctrl, 0);
}
EXPORT_SYMBOL(__v4l2_ctrl_s_ctrl);

int __v4l2_ctrl_s_ctrl_int64(struct v4l2_ctrl *ctrl, s64 val)
{
	lockdep_assert_held(ctrl->handler->lock);

	/* It's a driver bug if this happens. */
	if (WARN_ON(ctrl->is_ptr || ctrl->type != V4L2_CTRL_TYPE_INTEGER64))
		return -EINVAL;
	*ctrl->p_new.p_s64 = val;
	return set_ctrl(NULL, ctrl, 0);
}
EXPORT_SYMBOL(__v4l2_ctrl_s_ctrl_int64);

int __v4l2_ctrl_s_ctrl_string(struct v4l2_ctrl *ctrl, const char *s)
{
	lockdep_assert_held(ctrl->handler->lock);

	/* It's a driver bug if this happens. */
	if (WARN_ON(ctrl->type != V4L2_CTRL_TYPE_STRING))
		return -EINVAL;
	strscpy(ctrl->p_new.p_char, s, ctrl->maximum + 1);
	return set_ctrl(NULL, ctrl, 0);
}
EXPORT_SYMBOL(__v4l2_ctrl_s_ctrl_string);

int __v4l2_ctrl_s_ctrl_compound(struct v4l2_ctrl *ctrl,
				enum v4l2_ctrl_type type, const void *p)
{
	lockdep_assert_held(ctrl->handler->lock);

	/* It's a driver bug if this happens. */
	if (WARN_ON(ctrl->type != type))
		return -EINVAL;
	/* Setting dynamic arrays is not (yet?) supported. */
	if (WARN_ON(ctrl->is_dyn_array))
		return -EINVAL;
	memcpy(ctrl->p_new.p, p, ctrl->elems * ctrl->elem_size);
	return set_ctrl(NULL, ctrl, 0);
}
EXPORT_SYMBOL(__v4l2_ctrl_s_ctrl_compound);

/*
 * Modify the range of a control.
 */
int __v4l2_ctrl_modify_range(struct v4l2_ctrl *ctrl,
			     s64 min, s64 max, u64 step, s64 def)
{
	bool value_changed;
	bool range_changed = false;
	int ret;

	lockdep_assert_held(ctrl->handler->lock);

	switch (ctrl->type) {
	case V4L2_CTRL_TYPE_INTEGER:
	case V4L2_CTRL_TYPE_INTEGER64:
	case V4L2_CTRL_TYPE_BOOLEAN:
	case V4L2_CTRL_TYPE_MENU:
	case V4L2_CTRL_TYPE_INTEGER_MENU:
	case V4L2_CTRL_TYPE_BITMASK:
	case V4L2_CTRL_TYPE_U8:
	case V4L2_CTRL_TYPE_U16:
	case V4L2_CTRL_TYPE_U32:
		if (ctrl->is_array)
			return -EINVAL;
		ret = check_range(ctrl->type, min, max, step, def);
		if (ret)
			return ret;
		break;
	default:
		return -EINVAL;
	}
	if (ctrl->minimum != min || ctrl->maximum != max ||
	    ctrl->step != step || ctrl->default_value != def) {
		range_changed = true;
		ctrl->minimum = min;
		ctrl->maximum = max;
		ctrl->step = step;
		ctrl->default_value = def;
	}
	cur_to_new(ctrl);
	if (validate_new(ctrl, ctrl->p_new)) {
		if (ctrl->type == V4L2_CTRL_TYPE_INTEGER64)
			*ctrl->p_new.p_s64 = def;
		else
			*ctrl->p_new.p_s32 = def;
	}

	if (ctrl->type == V4L2_CTRL_TYPE_INTEGER64)
		value_changed = *ctrl->p_new.p_s64 != *ctrl->p_cur.p_s64;
	else
		value_changed = *ctrl->p_new.p_s32 != *ctrl->p_cur.p_s32;
	if (value_changed)
		ret = set_ctrl(NULL, ctrl, V4L2_EVENT_CTRL_CH_RANGE);
	else if (range_changed)
		send_event(NULL, ctrl, V4L2_EVENT_CTRL_CH_RANGE);
	return ret;
}
EXPORT_SYMBOL(__v4l2_ctrl_modify_range);

/* Implement VIDIOC_QUERY_EXT_CTRL */
int v4l2_query_ext_ctrl(struct v4l2_ctrl_handler *hdl, struct v4l2_query_ext_ctrl *qc)
{
	const unsigned int next_flags = V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
	u32 id = qc->id & V4L2_CTRL_ID_MASK;
	struct v4l2_ctrl_ref *ref;
	struct v4l2_ctrl *ctrl;

	if (!hdl)
		return -EINVAL;

	mutex_lock(hdl->lock);

	/* Try to find it */
	ref = find_ref(hdl, id);

	if ((qc->id & next_flags) && !list_empty(&hdl->ctrl_refs)) {
		bool is_compound;
		/* Match any control that is not hidden */
		unsigned int mask = 1;
		bool match = false;

		if ((qc->id & next_flags) == V4L2_CTRL_FLAG_NEXT_COMPOUND) {
			/* Match any hidden control */
			match = true;
		} else if ((qc->id & next_flags) == next_flags) {
			/* Match any control, compound or not */
			mask = 0;
		}

		/* Find the next control with ID > qc->id */

		/* Did we reach the end of the control list? */
		if (id >= node2id(hdl->ctrl_refs.prev)) {
			ref = NULL; /* Yes, so there is no next control */
		} else if (ref) {
			/*
			 * We found a control with the given ID, so just get
			 * the next valid one in the list.
			 */
			list_for_each_entry_continue(ref, &hdl->ctrl_refs, node) {
				is_compound = ref->ctrl->is_array ||
					ref->ctrl->type >= V4L2_CTRL_COMPOUND_TYPES;
				if (id < ref->ctrl->id &&
				    (is_compound & mask) == match)
					break;
			}
			if (&ref->node == &hdl->ctrl_refs)
				ref = NULL;
		} else {
			/*
			 * No control with the given ID exists, so start
			 * searching for the next largest ID. We know there
			 * is one, otherwise the first 'if' above would have
			 * been true.
			 */
			list_for_each_entry(ref, &hdl->ctrl_refs, node) {
				is_compound = ref->ctrl->is_array ||
					ref->ctrl->type >= V4L2_CTRL_COMPOUND_TYPES;
				if (id < ref->ctrl->id &&
				    (is_compound & mask) == match)
					break;
			}
			if (&ref->node == &hdl->ctrl_refs)
				ref = NULL;
		}
	}
	mutex_unlock(hdl->lock);

	if (!ref)
		return -EINVAL;

	ctrl = ref->ctrl;
	memset(qc, 0, sizeof(*qc));
	if (id >= V4L2_CID_PRIVATE_BASE)
		qc->id = id;
	else
		qc->id = ctrl->id;
	strscpy(qc->name, ctrl->name, sizeof(qc->name));
	qc->flags = user_flags(ctrl);
	qc->type = ctrl->type;
	qc->elem_size = ctrl->elem_size;
	qc->elems = ctrl->elems;
	qc->nr_of_dims = ctrl->nr_of_dims;
	memcpy(qc->dims, ctrl->dims, qc->nr_of_dims * sizeof(qc->dims[0]));
	qc->minimum = ctrl->minimum;
	qc->maximum = ctrl->maximum;
	qc->default_value = ctrl->default_value;
	if (ctrl->type == V4L2_CTRL_TYPE_MENU ||
	    ctrl->type == V4L2_CTRL_TYPE_INTEGER_MENU)
		qc->step = 1;
	else
		qc->step = ctrl->step;
	return 0;
}
EXPORT_SYMBOL(v4l2_query_ext_ctrl);

/* Implement VIDIOC_QUERYCTRL */
int v4l2_queryctrl(struct v4l2_ctrl_handler *hdl, struct v4l2_queryctrl *qc)
{
	struct v4l2_query_ext_ctrl qec = { qc->id };
	int rc;

	rc = v4l2_query_ext_ctrl(hdl, &qec);
	if (rc)
		return rc;

	qc->id = qec.id;
	qc->type = qec.type;
	qc->flags = qec.flags;
	strscpy(qc->name, qec.name, sizeof(qc->name));
	switch (qc->type) {
	case V4L2_CTRL_TYPE_INTEGER:
	case V4L2_CTRL_TYPE_BOOLEAN:
	case V4L2_CTRL_TYPE_MENU:
	case V4L2_CTRL_TYPE_INTEGER_MENU:
	case V4L2_CTRL_TYPE_STRING:
	case V4L2_CTRL_TYPE_BITMASK:
		qc->minimum = qec.minimum;
		qc->maximum = qec.maximum;
		qc->step = qec.step;
		qc->default_value = qec.default_value;
		break;
	default:
		qc->minimum = 0;
		qc->maximum = 0;
		qc->step = 0;
		qc->default_value = 0;
		break;
	}
	return 0;
}
EXPORT_SYMBOL(v4l2_queryctrl);

/* Implement VIDIOC_QUERYMENU */
int v4l2_querymenu(struct v4l2_ctrl_handler *hdl, struct v4l2_querymenu *qm)
{
	struct v4l2_ctrl *ctrl;
	u32 i = qm->index;

	ctrl = v4l2_ctrl_find(hdl, qm->id);
	if (!ctrl)
		return -EINVAL;

	qm->reserved = 0;
	/* Sanity checks */
	switch (ctrl->type) {
	case V4L2_CTRL_TYPE_MENU:
		if (!ctrl->qmenu)
			return -EINVAL;
		break;
	case V4L2_CTRL_TYPE_INTEGER_MENU:
		if (!ctrl->qmenu_int)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	if (i < ctrl->minimum || i > ctrl->maximum)
		return -EINVAL;

	/* Use mask to see if this menu item should be skipped */
	if (ctrl->menu_skip_mask & (1ULL << i))
		return -EINVAL;
	/* Empty menu items should also be skipped */
	if (ctrl->type == V4L2_CTRL_TYPE_MENU) {
		if (!ctrl->qmenu[i] || ctrl->qmenu[i][0] == '\0')
			return -EINVAL;
		strscpy(qm->name, ctrl->qmenu[i], sizeof(qm->name));
	} else {
		qm->value = ctrl->qmenu_int[i];
	}
	return 0;
}
EXPORT_SYMBOL(v4l2_querymenu);

/*
 * VIDIOC_LOG_STATUS helpers
 */

int v4l2_ctrl_log_status(struct file *file, void *fh)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_fh *vfh = file->private_data;

	if (test_bit(V4L2_FL_USES_V4L2_FH, &vfd->flags) && vfd->v4l2_dev)
		v4l2_ctrl_handler_log_status(vfh->ctrl_handler,
					     vfd->v4l2_dev->name);
	return 0;
}
EXPORT_SYMBOL(v4l2_ctrl_log_status);

int v4l2_ctrl_subdev_log_status(struct v4l2_subdev *sd)
{
	v4l2_ctrl_handler_log_status(sd->ctrl_handler, sd->name);
	return 0;
}
EXPORT_SYMBOL(v4l2_ctrl_subdev_log_status);

/*
 * VIDIOC_(UN)SUBSCRIBE_EVENT implementation
 */

static int v4l2_ctrl_add_event(struct v4l2_subscribed_event *sev,
			       unsigned int elems)
{
	struct v4l2_ctrl *ctrl = v4l2_ctrl_find(sev->fh->ctrl_handler, sev->id);

	if (!ctrl)
		return -EINVAL;

	v4l2_ctrl_lock(ctrl);
	list_add_tail(&sev->node, &ctrl->ev_subs);
	if (ctrl->type != V4L2_CTRL_TYPE_CTRL_CLASS &&
	    (sev->flags & V4L2_EVENT_SUB_FL_SEND_INITIAL))
		send_initial_event(sev->fh, ctrl);
	v4l2_ctrl_unlock(ctrl);
	return 0;
}

static void v4l2_ctrl_del_event(struct v4l2_subscribed_event *sev)
{
	struct v4l2_ctrl *ctrl = v4l2_ctrl_find(sev->fh->ctrl_handler, sev->id);

	if (!ctrl)
		return;

	v4l2_ctrl_lock(ctrl);
	list_del(&sev->node);
	v4l2_ctrl_unlock(ctrl);
}

void v4l2_ctrl_replace(struct v4l2_event *old, const struct v4l2_event *new)
{
	u32 old_changes = old->u.ctrl.changes;

	old->u.ctrl = new->u.ctrl;
	old->u.ctrl.changes |= old_changes;
}
EXPORT_SYMBOL(v4l2_ctrl_replace);

void v4l2_ctrl_merge(const struct v4l2_event *old, struct v4l2_event *new)
{
	new->u.ctrl.changes |= old->u.ctrl.changes;
}
EXPORT_SYMBOL(v4l2_ctrl_merge);

const struct v4l2_subscribed_event_ops v4l2_ctrl_sub_ev_ops = {
	.add = v4l2_ctrl_add_event,
	.del = v4l2_ctrl_del_event,
	.replace = v4l2_ctrl_replace,
	.merge = v4l2_ctrl_merge,
};
EXPORT_SYMBOL(v4l2_ctrl_sub_ev_ops);

int v4l2_ctrl_subscribe_event(struct v4l2_fh *fh,
			      const struct v4l2_event_subscription *sub)
{
	if (sub->type == V4L2_EVENT_CTRL)
		return v4l2_event_subscribe(fh, sub, 0, &v4l2_ctrl_sub_ev_ops);
	return -EINVAL;
}
EXPORT_SYMBOL(v4l2_ctrl_subscribe_event);

int v4l2_ctrl_subdev_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
				     struct v4l2_event_subscription *sub)
{
	if (!sd->ctrl_handler)
		return -EINVAL;
	return v4l2_ctrl_subscribe_event(fh, sub);
}
EXPORT_SYMBOL(v4l2_ctrl_subdev_subscribe_event);

/*
 * poll helper
 */
__poll_t v4l2_ctrl_poll(struct file *file, struct poll_table_struct *wait)
{
	struct v4l2_fh *fh = file->private_data;

	poll_wait(file, &fh->wait, wait);
	if (v4l2_event_pending(fh))
		return EPOLLPRI;
	return 0;
}
EXPORT_SYMBOL(v4l2_ctrl_poll);
