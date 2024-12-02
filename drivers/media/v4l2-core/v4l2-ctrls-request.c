// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * V4L2 controls framework Request API implementation.
 *
 * Copyright (C) 2018-2021  Hans Verkuil <hverkuil-cisco@xs4all.nl>
 */

#define pr_fmt(fmt) "v4l2-ctrls: " fmt

#include <linux/export.h>
#include <linux/slab.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>

#include "v4l2-ctrls-priv.h"

/* Initialize the request-related fields in a control handler */
void v4l2_ctrl_handler_init_request(struct v4l2_ctrl_handler *hdl)
{
	INIT_LIST_HEAD(&hdl->requests);
	INIT_LIST_HEAD(&hdl->requests_queued);
	hdl->request_is_queued = false;
	media_request_object_init(&hdl->req_obj);
}

/* Free the request-related fields in a control handler */
void v4l2_ctrl_handler_free_request(struct v4l2_ctrl_handler *hdl)
{
	struct v4l2_ctrl_handler *req, *next_req;

	/*
	 * Do nothing if this isn't the main handler or the main
	 * handler is not used in any request.
	 *
	 * The main handler can be identified by having a NULL ops pointer in
	 * the request object.
	 */
	if (hdl->req_obj.ops || list_empty(&hdl->requests))
		return;

	/*
	 * If the main handler is freed and it is used by handler objects in
	 * outstanding requests, then unbind and put those objects before
	 * freeing the main handler.
	 */
	list_for_each_entry_safe(req, next_req, &hdl->requests, requests) {
		media_request_object_unbind(&req->req_obj);
		media_request_object_put(&req->req_obj);
	}
}

static int v4l2_ctrl_request_clone(struct v4l2_ctrl_handler *hdl,
				   const struct v4l2_ctrl_handler *from)
{
	struct v4l2_ctrl_ref *ref;
	int err = 0;

	if (WARN_ON(!hdl || hdl == from))
		return -EINVAL;

	if (hdl->error)
		return hdl->error;

	WARN_ON(hdl->lock != &hdl->_lock);

	mutex_lock(from->lock);
	list_for_each_entry(ref, &from->ctrl_refs, node) {
		struct v4l2_ctrl *ctrl = ref->ctrl;
		struct v4l2_ctrl_ref *new_ref;

		/* Skip refs inherited from other devices */
		if (ref->from_other_dev)
			continue;
		err = handler_new_ref(hdl, ctrl, &new_ref, false, true);
		if (err)
			break;
	}
	mutex_unlock(from->lock);
	return err;
}

static void v4l2_ctrl_request_queue(struct media_request_object *obj)
{
	struct v4l2_ctrl_handler *hdl =
		container_of(obj, struct v4l2_ctrl_handler, req_obj);
	struct v4l2_ctrl_handler *main_hdl = obj->priv;

	mutex_lock(main_hdl->lock);
	list_add_tail(&hdl->requests_queued, &main_hdl->requests_queued);
	hdl->request_is_queued = true;
	mutex_unlock(main_hdl->lock);
}

static void v4l2_ctrl_request_unbind(struct media_request_object *obj)
{
	struct v4l2_ctrl_handler *hdl =
		container_of(obj, struct v4l2_ctrl_handler, req_obj);
	struct v4l2_ctrl_handler *main_hdl = obj->priv;

	mutex_lock(main_hdl->lock);
	list_del_init(&hdl->requests);
	if (hdl->request_is_queued) {
		list_del_init(&hdl->requests_queued);
		hdl->request_is_queued = false;
	}
	mutex_unlock(main_hdl->lock);
}

static void v4l2_ctrl_request_release(struct media_request_object *obj)
{
	struct v4l2_ctrl_handler *hdl =
		container_of(obj, struct v4l2_ctrl_handler, req_obj);

	v4l2_ctrl_handler_free(hdl);
	kfree(hdl);
}

static const struct media_request_object_ops req_ops = {
	.queue = v4l2_ctrl_request_queue,
	.unbind = v4l2_ctrl_request_unbind,
	.release = v4l2_ctrl_request_release,
};

struct v4l2_ctrl_handler *v4l2_ctrl_request_hdl_find(struct media_request *req,
						     struct v4l2_ctrl_handler *parent)
{
	struct media_request_object *obj;

	if (WARN_ON(req->state != MEDIA_REQUEST_STATE_VALIDATING &&
		    req->state != MEDIA_REQUEST_STATE_QUEUED))
		return NULL;

	obj = media_request_object_find(req, &req_ops, parent);
	if (obj)
		return container_of(obj, struct v4l2_ctrl_handler, req_obj);
	return NULL;
}
EXPORT_SYMBOL_GPL(v4l2_ctrl_request_hdl_find);

struct v4l2_ctrl *
v4l2_ctrl_request_hdl_ctrl_find(struct v4l2_ctrl_handler *hdl, u32 id)
{
	struct v4l2_ctrl_ref *ref = find_ref_lock(hdl, id);

	return (ref && ref->p_req_valid) ? ref->ctrl : NULL;
}
EXPORT_SYMBOL_GPL(v4l2_ctrl_request_hdl_ctrl_find);

static int v4l2_ctrl_request_bind(struct media_request *req,
				  struct v4l2_ctrl_handler *hdl,
				  struct v4l2_ctrl_handler *from)
{
	int ret;

	ret = v4l2_ctrl_request_clone(hdl, from);

	if (!ret) {
		ret = media_request_object_bind(req, &req_ops,
						from, false, &hdl->req_obj);
		if (!ret) {
			mutex_lock(from->lock);
			list_add_tail(&hdl->requests, &from->requests);
			mutex_unlock(from->lock);
		}
	}
	return ret;
}

static struct media_request_object *
v4l2_ctrls_find_req_obj(struct v4l2_ctrl_handler *hdl,
			struct media_request *req, bool set)
{
	struct media_request_object *obj;
	struct v4l2_ctrl_handler *new_hdl;
	int ret;

	if (IS_ERR(req))
		return ERR_CAST(req);

	if (set && WARN_ON(req->state != MEDIA_REQUEST_STATE_UPDATING))
		return ERR_PTR(-EBUSY);

	obj = media_request_object_find(req, &req_ops, hdl);
	if (obj)
		return obj;
	/*
	 * If there are no controls in this completed request,
	 * then that can only happen if:
	 *
	 * 1) no controls were present in the queued request, and
	 * 2) v4l2_ctrl_request_complete() could not allocate a
	 *    control handler object to store the completed state in.
	 *
	 * So return ENOMEM to indicate that there was an out-of-memory
	 * error.
	 */
	if (!set)
		return ERR_PTR(-ENOMEM);

	new_hdl = kzalloc(sizeof(*new_hdl), GFP_KERNEL);
	if (!new_hdl)
		return ERR_PTR(-ENOMEM);

	obj = &new_hdl->req_obj;
	ret = v4l2_ctrl_handler_init(new_hdl, (hdl->nr_of_buckets - 1) * 8);
	if (!ret)
		ret = v4l2_ctrl_request_bind(req, new_hdl, hdl);
	if (ret) {
		v4l2_ctrl_handler_free(new_hdl);
		kfree(new_hdl);
		return ERR_PTR(ret);
	}

	media_request_object_get(obj);
	return obj;
}

int v4l2_g_ext_ctrls_request(struct v4l2_ctrl_handler *hdl, struct video_device *vdev,
			     struct media_device *mdev, struct v4l2_ext_controls *cs)
{
	struct media_request_object *obj = NULL;
	struct media_request *req = NULL;
	int ret;

	if (!mdev || cs->request_fd < 0)
		return -EINVAL;

	req = media_request_get_by_fd(mdev, cs->request_fd);
	if (IS_ERR(req))
		return PTR_ERR(req);

	if (req->state != MEDIA_REQUEST_STATE_COMPLETE) {
		media_request_put(req);
		return -EACCES;
	}

	ret = media_request_lock_for_access(req);
	if (ret) {
		media_request_put(req);
		return ret;
	}

	obj = v4l2_ctrls_find_req_obj(hdl, req, false);
	if (IS_ERR(obj)) {
		media_request_unlock_for_access(req);
		media_request_put(req);
		return PTR_ERR(obj);
	}

	hdl = container_of(obj, struct v4l2_ctrl_handler,
			   req_obj);
	ret = v4l2_g_ext_ctrls_common(hdl, cs, vdev);

	media_request_unlock_for_access(req);
	media_request_object_put(obj);
	media_request_put(req);
	return ret;
}

int try_set_ext_ctrls_request(struct v4l2_fh *fh,
			      struct v4l2_ctrl_handler *hdl,
			      struct video_device *vdev,
			      struct media_device *mdev,
			      struct v4l2_ext_controls *cs, bool set)
{
	struct media_request_object *obj = NULL;
	struct media_request *req = NULL;
	int ret;

	if (!mdev) {
		dprintk(vdev, "%s: missing media device\n",
			video_device_node_name(vdev));
		return -EINVAL;
	}

	if (cs->request_fd < 0) {
		dprintk(vdev, "%s: invalid request fd %d\n",
			video_device_node_name(vdev), cs->request_fd);
		return -EINVAL;
	}

	req = media_request_get_by_fd(mdev, cs->request_fd);
	if (IS_ERR(req)) {
		dprintk(vdev, "%s: cannot find request fd %d\n",
			video_device_node_name(vdev), cs->request_fd);
		return PTR_ERR(req);
	}

	ret = media_request_lock_for_update(req);
	if (ret) {
		dprintk(vdev, "%s: cannot lock request fd %d\n",
			video_device_node_name(vdev), cs->request_fd);
		media_request_put(req);
		return ret;
	}

	obj = v4l2_ctrls_find_req_obj(hdl, req, set);
	if (IS_ERR(obj)) {
		dprintk(vdev,
			"%s: cannot find request object for request fd %d\n",
			video_device_node_name(vdev),
			cs->request_fd);
		media_request_unlock_for_update(req);
		media_request_put(req);
		return PTR_ERR(obj);
	}

	hdl = container_of(obj, struct v4l2_ctrl_handler,
			   req_obj);
	ret = try_set_ext_ctrls_common(fh, hdl, cs, vdev, set);
	if (ret)
		dprintk(vdev,
			"%s: try_set_ext_ctrls_common failed (%d)\n",
			video_device_node_name(vdev), ret);

	media_request_unlock_for_update(req);
	media_request_object_put(obj);
	media_request_put(req);

	return ret;
}

void v4l2_ctrl_request_complete(struct media_request *req,
				struct v4l2_ctrl_handler *main_hdl)
{
	struct media_request_object *obj;
	struct v4l2_ctrl_handler *hdl;
	struct v4l2_ctrl_ref *ref;

	if (!req || !main_hdl)
		return;

	/*
	 * Note that it is valid if nothing was found. It means
	 * that this request doesn't have any controls and so just
	 * wants to leave the controls unchanged.
	 */
	obj = media_request_object_find(req, &req_ops, main_hdl);
	if (!obj) {
		int ret;

		/* Create a new request so the driver can return controls */
		hdl = kzalloc(sizeof(*hdl), GFP_KERNEL);
		if (!hdl)
			return;

		ret = v4l2_ctrl_handler_init(hdl, (main_hdl->nr_of_buckets - 1) * 8);
		if (!ret)
			ret = v4l2_ctrl_request_bind(req, hdl, main_hdl);
		if (ret) {
			v4l2_ctrl_handler_free(hdl);
			kfree(hdl);
			return;
		}
		hdl->request_is_queued = true;
		obj = media_request_object_find(req, &req_ops, main_hdl);
	}
	hdl = container_of(obj, struct v4l2_ctrl_handler, req_obj);

	list_for_each_entry(ref, &hdl->ctrl_refs, node) {
		struct v4l2_ctrl *ctrl = ref->ctrl;
		struct v4l2_ctrl *master = ctrl->cluster[0];
		unsigned int i;

		if (ctrl->flags & V4L2_CTRL_FLAG_VOLATILE) {
			v4l2_ctrl_lock(master);
			/* g_volatile_ctrl will update the current control values */
			for (i = 0; i < master->ncontrols; i++)
				cur_to_new(master->cluster[i]);
			call_op(master, g_volatile_ctrl);
			new_to_req(ref);
			v4l2_ctrl_unlock(master);
			continue;
		}
		if (ref->p_req_valid)
			continue;

		/* Copy the current control value into the request */
		v4l2_ctrl_lock(ctrl);
		cur_to_req(ref);
		v4l2_ctrl_unlock(ctrl);
	}

	mutex_lock(main_hdl->lock);
	WARN_ON(!hdl->request_is_queued);
	list_del_init(&hdl->requests_queued);
	hdl->request_is_queued = false;
	mutex_unlock(main_hdl->lock);
	media_request_object_complete(obj);
	media_request_object_put(obj);
}
EXPORT_SYMBOL(v4l2_ctrl_request_complete);

int v4l2_ctrl_request_setup(struct media_request *req,
			    struct v4l2_ctrl_handler *main_hdl)
{
	struct media_request_object *obj;
	struct v4l2_ctrl_handler *hdl;
	struct v4l2_ctrl_ref *ref;
	int ret = 0;

	if (!req || !main_hdl)
		return 0;

	if (WARN_ON(req->state != MEDIA_REQUEST_STATE_QUEUED))
		return -EBUSY;

	/*
	 * Note that it is valid if nothing was found. It means
	 * that this request doesn't have any controls and so just
	 * wants to leave the controls unchanged.
	 */
	obj = media_request_object_find(req, &req_ops, main_hdl);
	if (!obj)
		return 0;
	if (obj->completed) {
		media_request_object_put(obj);
		return -EBUSY;
	}
	hdl = container_of(obj, struct v4l2_ctrl_handler, req_obj);

	list_for_each_entry(ref, &hdl->ctrl_refs, node)
		ref->req_done = false;

	list_for_each_entry(ref, &hdl->ctrl_refs, node) {
		struct v4l2_ctrl *ctrl = ref->ctrl;
		struct v4l2_ctrl *master = ctrl->cluster[0];
		bool have_new_data = false;
		int i;

		/*
		 * Skip if this control was already handled by a cluster.
		 * Skip button controls and read-only controls.
		 */
		if (ref->req_done || (ctrl->flags & V4L2_CTRL_FLAG_READ_ONLY))
			continue;

		v4l2_ctrl_lock(master);
		for (i = 0; i < master->ncontrols; i++) {
			if (master->cluster[i]) {
				struct v4l2_ctrl_ref *r =
					find_ref(hdl, master->cluster[i]->id);

				if (r->p_req_valid) {
					have_new_data = true;
					break;
				}
			}
		}
		if (!have_new_data) {
			v4l2_ctrl_unlock(master);
			continue;
		}

		for (i = 0; i < master->ncontrols; i++) {
			if (master->cluster[i]) {
				struct v4l2_ctrl_ref *r =
					find_ref(hdl, master->cluster[i]->id);

				ret = req_to_new(r);
				if (ret) {
					v4l2_ctrl_unlock(master);
					goto error;
				}
				master->cluster[i]->is_new = 1;
				r->req_done = true;
			}
		}
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
			s32 new_auto_val = *master->p_new.p_s32;

			/*
			 * If the new value == the manual value, then copy
			 * the current volatile values.
			 */
			if (new_auto_val == master->manual_mode_value)
				update_from_auto_cluster(master);
		}

		ret = try_or_set_cluster(NULL, master, true, 0);
		v4l2_ctrl_unlock(master);

		if (ret)
			break;
	}

error:
	media_request_object_put(obj);
	return ret;
}
EXPORT_SYMBOL(v4l2_ctrl_request_setup);
