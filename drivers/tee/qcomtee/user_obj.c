// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/slab.h>
#include <linux/uaccess.h>

#include "qcomtee.h"

/**
 * DOC: User Objects aka Supplicants
 *
 * Any userspace process with access to the TEE device file can behave as a
 * supplicant by creating a user object. Any TEE parameter of type OBJREF with
 * %QCOMTEE_OBJREF_FLAG_USER flag set is considered a user object.
 *
 * A supplicant uses qcomtee_user_object_select() (i.e. TEE_IOC_SUPPL_RECV) to
 * receive a QTEE user object request and qcomtee_user_object_submit()
 * (i.e. TEE_IOC_SUPPL_SEND) to submit a response. QTEE expects to receive the
 * response, including OB and OO in a specific order in the message; parameters
 * submitted with qcomtee_user_object_submit() should maintain this order.
 */

/**
 * struct qcomtee_user_object - User object.
 * @object: &struct qcomtee_object representing the user object.
 * @ctx: context for which the user object is defined.
 * @object_id: object ID in @ctx.
 * @notify: notify on release.
 *
 * Any object managed in userspace is represented by this struct.
 * If @notify is set, a notification message is sent back to userspace
 * upon release.
 */
struct qcomtee_user_object {
	struct qcomtee_object object;
	struct tee_context *ctx;
	u64 object_id;
	bool notify;
};

#define to_qcomtee_user_object(o) \
	container_of((o), struct qcomtee_user_object, object)

static struct qcomtee_object_operations qcomtee_user_object_ops;

/* Is it a user object? */
int is_qcomtee_user_object(struct qcomtee_object *object)
{
	return object != NULL_QCOMTEE_OBJECT &&
	       typeof_qcomtee_object(object) == QCOMTEE_OBJECT_TYPE_CB &&
	       object->ops == &qcomtee_user_object_ops;
}

/* Set the user object's 'notify on release' flag. */
void qcomtee_user_object_set_notify(struct qcomtee_object *object, bool notify)
{
	if (is_qcomtee_user_object(object))
		to_qcomtee_user_object(object)->notify = notify;
}

/* Supplicant Requests: */

/**
 * enum qcomtee_req_state - Current state of request.
 * @QCOMTEE_REQ_QUEUED: Request is waiting for supplicant.
 * @QCOMTEE_REQ_PROCESSING: Request has been picked by the supplicant.
 * @QCOMTEE_REQ_PROCESSED: Response has been submitted for the request.
 */
enum qcomtee_req_state {
	QCOMTEE_REQ_QUEUED = 1,
	QCOMTEE_REQ_PROCESSING,
	QCOMTEE_REQ_PROCESSED,
};

/* User requests sent to supplicants. */
struct qcomtee_ureq {
	enum qcomtee_req_state state;

	/* User Request: */
	int req_id;
	u64 object_id;
	u32 op;
	struct qcomtee_arg *args;
	int errno;

	struct list_head node;
	struct completion c; /* Completion for whoever wait. */
};

/*
 * Placeholder for a PROCESSING request in qcomtee_context.reqs_idr.
 *
 * If the thread that calls qcomtee_object_invoke() dies and the supplicant
 * is processing the request, replace the entry in qcomtee_context.reqs_idr
 * with empty_ureq. This ensures that (1) the req_id remains busy and is not
 * reused, and (2) the supplicant fails to submit the response and performs
 * the necessary rollback.
 */
static struct qcomtee_ureq empty_ureq = { .state = QCOMTEE_REQ_PROCESSING };

/* Enqueue a user request for a context and assign a request ID. */
static int ureq_enqueue(struct qcomtee_context_data *ctxdata,
			struct qcomtee_ureq *ureq)
{
	int ret;

	guard(mutex)(&ctxdata->reqs_lock);
	/* Supplicant is dying. */
	if (ctxdata->released)
		return -ENODEV;

	/* Allocate an ID and queue the request. */
	ret = idr_alloc(&ctxdata->reqs_idr, ureq, 0, 0, GFP_KERNEL);
	if (ret < 0)
		return ret;

	ureq->req_id = ret;
	ureq->state = QCOMTEE_REQ_QUEUED;
	list_add_tail(&ureq->node, &ctxdata->reqs_list);

	return 0;
}

/**
 * ureq_dequeue() - Dequeue a user request from a context.
 * @ctxdata: context data for a context to dequeue the request.
 * @req_id: ID of the request to be dequeued.
 *
 * It dequeues a user request and releases its request ID.
 *
 * Context: The caller should hold &qcomtee_context_data->reqs_lock.
 * Return: Returns the user request associated with this ID; otherwise, NULL.
 */
static struct qcomtee_ureq *ureq_dequeue(struct qcomtee_context_data *ctxdata,
					 int req_id)
{
	struct qcomtee_ureq *ureq;

	ureq = idr_remove(&ctxdata->reqs_idr, req_id);
	if (ureq == &empty_ureq || !ureq)
		return NULL;

	list_del(&ureq->node);

	return ureq;
}

/**
 * ureq_select() - Select the next request in a context.
 * @ctxdata: context data for a context to pop a request.
 * @ubuf_size: size of the available buffer for UBUF parameters.
 * @num_params: number of entries for the TEE parameter array.
 *
 * It checks if @num_params is large enough to fit the next request arguments.
 * It checks if @ubuf_size is large enough to fit IB buffer arguments.
 *
 * Context: The caller should hold &qcomtee_context_data->reqs_lock.
 * Return: On success, returns a request;
 *         on failure, returns NULL and ERR_PTR.
 */
static struct qcomtee_ureq *ureq_select(struct qcomtee_context_data *ctxdata,
					size_t ubuf_size, int num_params)
{
	struct qcomtee_ureq *req, *ureq = NULL;
	struct qcomtee_arg *u;
	int i;

	/* Find the a queued request. */
	list_for_each_entry(req, &ctxdata->reqs_list, node) {
		if (req->state == QCOMTEE_REQ_QUEUED) {
			ureq = req;
			break;
		}
	}

	if (!ureq)
		return NULL;

	u = ureq->args;
	/* (1) Is there enough TEE parameters? */
	if (num_params < qcomtee_args_len(u))
		return ERR_PTR(-EINVAL);
	/* (2) Is there enough space to pass input buffers? */
	qcomtee_arg_for_each_input_buffer(i, u) {
		ubuf_size = size_sub(ubuf_size, u[i].b.size);
		if (ubuf_size == SIZE_MAX)
			return ERR_PTR(-EINVAL);

		ubuf_size = round_down(ubuf_size, 8);
	}

	return ureq;
}

/* Gets called when the user closes the device. */
void qcomtee_requests_destroy(struct qcomtee_context_data *ctxdata)
{
	struct qcomtee_ureq *req, *ureq;

	guard(mutex)(&ctxdata->reqs_lock);
	/* So ureq_enqueue() refuses new requests from QTEE. */
	ctxdata->released = true;
	/* ureqs in reqs_list are in QUEUED or PROCESSING (!= empty_ureq) state. */
	list_for_each_entry_safe(ureq, req, &ctxdata->reqs_list, node) {
		ureq_dequeue(ctxdata, ureq->req_id);

		if (ureq->op != QCOMTEE_MSG_OBJECT_OP_RELEASE) {
			ureq->state = QCOMTEE_REQ_PROCESSED;
			ureq->errno = -ENODEV;

			complete(&ureq->c);
		} else {
			kfree(ureq);
		}
	}
}

/* User Object API. */

/* User object dispatcher. */
static int qcomtee_user_object_dispatch(struct qcomtee_object_invoke_ctx *oic,
					struct qcomtee_object *object, u32 op,
					struct qcomtee_arg *args)
{
	struct qcomtee_user_object *uo = to_qcomtee_user_object(object);
	struct qcomtee_context_data *ctxdata = uo->ctx->data;
	struct qcomtee_ureq *ureq __free(kfree) = NULL;
	int errno;

	ureq = kzalloc(sizeof(*ureq), GFP_KERNEL);
	if (!ureq)
		return -ENOMEM;

	init_completion(&ureq->c);
	ureq->object_id = uo->object_id;
	ureq->op = op;
	ureq->args = args;

	/* Queue the request. */
	if (ureq_enqueue(ctxdata, ureq))
		return -ENODEV;
	/* Wakeup supplicant to process it. */
	complete(&ctxdata->req_c);

	/*
	 * Wait for the supplicant to process the request. Wait as KILLABLE
	 * in case the supplicant and invoke thread are both running from the
	 * same process, the supplicant crashes, or the shutdown sequence
	 * starts with supplicant dies first; otherwise, it stuck indefinitely.
	 *
	 * If the supplicant processes long-running requests, also use
	 * TASK_FREEZABLE to allow the device to safely suspend if needed.
	 */
	if (!wait_for_completion_state(&ureq->c,
				       TASK_KILLABLE | TASK_FREEZABLE)) {
		errno = ureq->errno;
		if (!errno)
			oic->data = no_free_ptr(ureq);
	} else {
		enum qcomtee_req_state prev_state;

		errno = -ENODEV;

		scoped_guard(mutex, &ctxdata->reqs_lock) {
			prev_state = ureq->state;
			/* Replace with empty_ureq to keep req_id reserved. */
			if (prev_state == QCOMTEE_REQ_PROCESSING) {
				list_del(&ureq->node);
				idr_replace(&ctxdata->reqs_idr,
					    &empty_ureq, ureq->req_id);

			/* Remove as supplicant has never seen this request. */
			} else if (prev_state == QCOMTEE_REQ_QUEUED) {
				ureq_dequeue(ctxdata, ureq->req_id);
			}
		}

		/* Supplicant did some work, do not discard it. */
		if (prev_state == QCOMTEE_REQ_PROCESSED) {
			errno = ureq->errno;
			if (!errno)
				oic->data = no_free_ptr(ureq);
		}
	}

	return errno;
}

/* Gets called after submitting the dispatcher response. */
static void qcomtee_user_object_notify(struct qcomtee_object_invoke_ctx *oic,
				       struct qcomtee_object *unused_object,
				       int err)
{
	struct qcomtee_ureq *ureq = oic->data;
	struct qcomtee_arg *u = ureq->args;
	int i;

	/*
	 * If err, there was a transport issue, and QTEE did not receive the
	 * response for the dispatcher. Release the callback object created for
	 * QTEE, in addition to the copies of objects kept for the drivers.
	 */
	qcomtee_arg_for_each_output_object(i, u) {
		if (err &&
		    (typeof_qcomtee_object(u[i].o) == QCOMTEE_OBJECT_TYPE_CB))
			qcomtee_object_put(u[i].o);
		qcomtee_object_put(u[i].o);
	}

	kfree(ureq);
}

static void qcomtee_user_object_release(struct qcomtee_object *object)
{
	struct qcomtee_user_object *uo = to_qcomtee_user_object(object);
	struct qcomtee_context_data *ctxdata = uo->ctx->data;
	struct qcomtee_ureq *ureq;

	/* RELEASE does not require any argument. */
	static struct qcomtee_arg args[] = { { .type = QCOMTEE_ARG_TYPE_INV } };

	if (!uo->notify)
		goto out_no_notify;

	ureq = kzalloc(sizeof(*ureq), GFP_KERNEL);
	if (!ureq)
		goto out_no_notify;

	/* QUEUE a release request: */
	ureq->object_id = uo->object_id;
	ureq->op = QCOMTEE_MSG_OBJECT_OP_RELEASE;
	ureq->args = args;
	if (ureq_enqueue(ctxdata, ureq)) {
		kfree(ureq);
		/* Ignore the notification if it cannot be queued. */
		goto out_no_notify;
	}

	complete(&ctxdata->req_c);

out_no_notify:
	teedev_ctx_put(uo->ctx);
	kfree(uo);
}

static struct qcomtee_object_operations qcomtee_user_object_ops = {
	.release = qcomtee_user_object_release,
	.notify = qcomtee_user_object_notify,
	.dispatch = qcomtee_user_object_dispatch,
};

/**
 * qcomtee_user_param_to_object() - OBJREF parameter to &struct qcomtee_object.
 * @object: object returned.
 * @param: TEE parameter.
 * @ctx: context in which the conversion should happen.
 *
 * @param is an OBJREF with %QCOMTEE_OBJREF_FLAG_USER flags.
 *
 * Return: On success, returns 0; on failure, returns < 0.
 */
int qcomtee_user_param_to_object(struct qcomtee_object **object,
				 struct tee_param *param,
				 struct tee_context *ctx)
{
	struct qcomtee_user_object *user_object __free(kfree) = NULL;
	int err;

	user_object = kzalloc(sizeof(*user_object), GFP_KERNEL);
	if (!user_object)
		return -ENOMEM;

	user_object->ctx = ctx;
	user_object->object_id = param->u.objref.id;
	/* By default, always notify userspace upon release. */
	user_object->notify = true;
	err = qcomtee_object_user_init(&user_object->object,
				       QCOMTEE_OBJECT_TYPE_CB,
				       &qcomtee_user_object_ops, "uo-%llu",
				       param->u.objref.id);
	if (err)
		return err;
	/* Matching teedev_ctx_put() is in qcomtee_user_object_release(). */
	teedev_ctx_get(ctx);

	*object = &no_free_ptr(user_object)->object;

	return 0;
}

/* Reverse what qcomtee_user_param_to_object() does. */
int qcomtee_user_param_from_object(struct tee_param *param,
				   struct qcomtee_object *object,
				   struct tee_context *ctx)
{
	struct qcomtee_user_object *uo;

	uo = to_qcomtee_user_object(object);
	/* Ensure the object is in the same context as the caller. */
	if (uo->ctx != ctx)
		return -EINVAL;

	param->u.objref.id = uo->object_id;
	param->u.objref.flags = QCOMTEE_OBJREF_FLAG_USER;

	/* User objects are valid in userspace; do not keep a copy. */
	qcomtee_object_put(object);

	return 0;
}

/**
 * qcomtee_cb_params_from_args() - Convert QTEE arguments to TEE parameters.
 * @params: TEE parameters.
 * @u: QTEE arguments.
 * @num_params: number of elements in the parameter array.
 * @ubuf_addr: user buffer for arguments of type %QCOMTEE_ARG_TYPE_IB.
 * @ubuf_size: size of the user buffer.
 * @ctx: context in which the conversion should happen.
 *
 * It expects @params to have enough entries for @u. Entries in @params are of
 * %TEE_IOCTL_PARAM_ATTR_TYPE_NONE.
 *
 * Return: On success, returns the number of input parameters;
 *         on failure, returns < 0.
 */
static int qcomtee_cb_params_from_args(struct tee_param *params,
				       struct qcomtee_arg *u, int num_params,
				       void __user *ubuf_addr, size_t ubuf_size,
				       struct tee_context *ctx)
{
	int i, np;
	void __user *uaddr;

	qcomtee_arg_for_each(i, u) {
		switch (u[i].type) {
		case QCOMTEE_ARG_TYPE_IB:
			params[i].attr = TEE_IOCTL_PARAM_ATTR_TYPE_UBUF_INPUT;

			/* Underflow already checked in ureq_select(). */
			ubuf_size = round_down(ubuf_size - u[i].b.size, 8);
			uaddr = (void __user *)(ubuf_addr + ubuf_size);

			params[i].u.ubuf.uaddr = uaddr;
			params[i].u.ubuf.size = u[i].b.size;
			if (copy_to_user(params[i].u.ubuf.uaddr, u[i].b.addr,
					 u[i].b.size))
				goto out_failed;

			break;
		case QCOMTEE_ARG_TYPE_OB:
			params[i].attr = TEE_IOCTL_PARAM_ATTR_TYPE_UBUF_OUTPUT;
			/* Let the user knows the maximum size QTEE expects. */
			params[i].u.ubuf.size = u[i].b.size;

			break;
		case QCOMTEE_ARG_TYPE_IO:
			params[i].attr = TEE_IOCTL_PARAM_ATTR_TYPE_OBJREF_INPUT;
			if (qcomtee_objref_from_arg(&params[i], &u[i], ctx))
				goto out_failed;

			break;
		case QCOMTEE_ARG_TYPE_OO:
			params[i].attr =
				TEE_IOCTL_PARAM_ATTR_TYPE_OBJREF_OUTPUT;

			break;
		default: /* Never get here! */
			goto out_failed;
		}
	}

	return i;

out_failed:
	/* Undo qcomtee_objref_from_arg(). */
	for (np = i; np >= 0; np--) {
		if (params[np].attr == TEE_IOCTL_PARAM_ATTR_TYPE_OBJREF_INPUT)
			qcomtee_context_del_qtee_object(&params[np], ctx);
	}

	/* Release any IO objects not processed. */
	for (; u[i].type; i++) {
		if (u[i].type == QCOMTEE_ARG_TYPE_IO)
			qcomtee_object_put(u[i].o);
	}

	return -EINVAL;
}

/**
 * qcomtee_cb_params_to_args() - Convert TEE parameters to QTEE arguments.
 * @u: QTEE arguments.
 * @params: TEE parameters.
 * @num_params: number of elements in the parameter array.
 * @ctx: context in which the conversion should happen.
 *
 * Return: On success, returns 0; on failure, returns < 0.
 */
static int qcomtee_cb_params_to_args(struct qcomtee_arg *u,
				     struct tee_param *params, int num_params,
				     struct tee_context *ctx)
{
	int i;

	qcomtee_arg_for_each(i, u) {
		switch (u[i].type) {
		case QCOMTEE_ARG_TYPE_IB:
			if (params[i].attr !=
			    TEE_IOCTL_PARAM_ATTR_TYPE_UBUF_INPUT)
				goto out_failed;

			break;
		case QCOMTEE_ARG_TYPE_OB:
			if (params[i].attr !=
			    TEE_IOCTL_PARAM_ATTR_TYPE_UBUF_OUTPUT)
				goto out_failed;

			/* Client can not send more data than requested. */
			if (params[i].u.ubuf.size > u[i].b.size)
				goto out_failed;

			if (copy_from_user(u[i].b.addr, params[i].u.ubuf.uaddr,
					   params[i].u.ubuf.size))
				goto out_failed;

			u[i].b.size = params[i].u.ubuf.size;

			break;
		case QCOMTEE_ARG_TYPE_IO:
			if (params[i].attr !=
			    TEE_IOCTL_PARAM_ATTR_TYPE_OBJREF_INPUT)
				goto out_failed;

			break;
		case QCOMTEE_ARG_TYPE_OO:
			if (params[i].attr !=
			    TEE_IOCTL_PARAM_ATTR_TYPE_OBJREF_OUTPUT)
				goto out_failed;

			if (qcomtee_objref_to_arg(&u[i], &params[i], ctx))
				goto out_failed;

			break;
		default: /* Never get here! */
			goto out_failed;
		}
	}

	return 0;

out_failed:
	/* Undo qcomtee_objref_to_arg(). */
	for (i--; i >= 0; i--) {
		if (u[i].type != QCOMTEE_ARG_TYPE_OO)
			continue;

		qcomtee_user_object_set_notify(u[i].o, false);
		if (typeof_qcomtee_object(u[i].o) == QCOMTEE_OBJECT_TYPE_CB)
			qcomtee_object_put(u[i].o);

		qcomtee_object_put(u[i].o);
	}

	return -EINVAL;
}

/**
 * qcomtee_user_object_select() - Select a request for a user object.
 * @ctx: context to look for a user object.
 * @params: parameters for @op.
 * @num_params: number of elements in the parameter array.
 * @uaddr: user buffer for output UBUF parameters.
 * @size: size of user buffer @uaddr.
 * @data: information for the selected request.
 *
 * @params is filled along with @data for the selected request.
 *
 * Return: On success, returns 0; on failure, returns < 0.
 */
int qcomtee_user_object_select(struct tee_context *ctx,
			       struct tee_param *params, int num_params,
			       void __user *uaddr, size_t size,
			       struct qcomtee_user_object_request_data *data)
{
	struct qcomtee_context_data *ctxdata = ctx->data;
	struct qcomtee_ureq *ureq;
	int ret;

	/*
	 * Hold the reqs_lock not only for ureq_select() and updating the ureq
	 * state to PROCESSING but for the entire duration of ureq access.
	 * This prevents qcomtee_user_object_dispatch() from freeing
	 * ureq while it is still in use, if client dies.
	 */

	while (1) {
		scoped_guard(mutex, &ctxdata->reqs_lock) {
			ureq = ureq_select(ctxdata, size, num_params);
			if (!ureq)
				goto wait_for_request;

			if (IS_ERR(ureq))
				return PTR_ERR(ureq);

			/* Processing the request 'QUEUED -> PROCESSING'. */
			ureq->state = QCOMTEE_REQ_PROCESSING;
			/* ''Prepare user request:'' */
			data->id = ureq->req_id;
			data->object_id = ureq->object_id;
			data->op = ureq->op;
			ret = qcomtee_cb_params_from_args(params, ureq->args,
							  num_params, uaddr,
							  size, ctx);
			if (ret >= 0)
				goto done_request;

			/* Something is wrong with the request: */
			ureq_dequeue(ctxdata, data->id);
			/* Send error to QTEE. */
			ureq->state = QCOMTEE_REQ_PROCESSED;
			ureq->errno = ret;

			complete(&ureq->c);
		}

		continue;
wait_for_request:
		/* Wait for a new QUEUED request. */
		if (wait_for_completion_interruptible(&ctxdata->req_c))
			return -ERESTARTSYS;
	}

done_request:
	/* No one is waiting for the response. */
	if (data->op == QCOMTEE_MSG_OBJECT_OP_RELEASE) {
		scoped_guard(mutex, &ctxdata->reqs_lock)
			ureq_dequeue(ctxdata, data->id);
		kfree(ureq);
	}

	data->np = ret;

	return 0;
}

/**
 * qcomtee_user_object_submit() - Submit a response for a user object.
 * @ctx: context to look for a user object.
 * @params: returned parameters.
 * @num_params: number of elements in the parameter array.
 * @req_id: request ID for the response.
 * @errno: result of user object invocation.
 *
 * Return: On success, returns 0; on failure, returns < 0.
 */
int qcomtee_user_object_submit(struct tee_context *ctx,
			       struct tee_param *params, int num_params,
			       int req_id, int errno)
{
	struct qcomtee_context_data *ctxdata = ctx->data;
	struct qcomtee_ureq *ureq;

	/* See comments for reqs_lock in qcomtee_user_object_select(). */
	guard(mutex)(&ctxdata->reqs_lock);

	ureq = ureq_dequeue(ctxdata, req_id);
	if (!ureq)
		return -EINVAL;

	ureq->state = QCOMTEE_REQ_PROCESSED;

	if (!errno)
		ureq->errno = qcomtee_cb_params_to_args(ureq->args, params,
							num_params, ctx);
	else
		ureq->errno = errno;
	/* Return errno if qcomtee_cb_params_to_args() failed; otherwise 0. */
	if (!errno && ureq->errno)
		errno = ureq->errno;
	else
		errno = 0;

	/* Send result to QTEE. */
	complete(&ureq->c);

	return errno;
}
