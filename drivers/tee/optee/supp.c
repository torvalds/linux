// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015, Linaro Limited
 */
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "optee_private.h"

struct optee_supp_req {
	struct list_head link;

	bool in_queue;
	u32 func;
	u32 ret;
	size_t num_params;
	struct tee_param *param;

	struct completion c;
};

void optee_supp_init(struct optee_supp *supp)
{
	memset(supp, 0, sizeof(*supp));
	mutex_init(&supp->mutex);
	init_completion(&supp->reqs_c);
	idr_init(&supp->idr);
	INIT_LIST_HEAD(&supp->reqs);
	supp->req_id = -1;
}

void optee_supp_uninit(struct optee_supp *supp)
{
	mutex_destroy(&supp->mutex);
	idr_destroy(&supp->idr);
}

void optee_supp_release(struct optee_supp *supp)
{
	int id;
	struct optee_supp_req *req;
	struct optee_supp_req *req_tmp;

	mutex_lock(&supp->mutex);

	/* Abort all request retrieved by supplicant */
	idr_for_each_entry(&supp->idr, req, id) {
		idr_remove(&supp->idr, id);
		req->ret = TEEC_ERROR_COMMUNICATION;
		complete(&req->c);
	}

	/* Abort all queued requests */
	list_for_each_entry_safe(req, req_tmp, &supp->reqs, link) {
		list_del(&req->link);
		req->in_queue = false;
		req->ret = TEEC_ERROR_COMMUNICATION;
		complete(&req->c);
	}

	supp->ctx = NULL;
	supp->req_id = -1;

	mutex_unlock(&supp->mutex);
}

/**
 * optee_supp_thrd_req() - request service from supplicant
 * @ctx:	context doing the request
 * @func:	function requested
 * @num_params:	number of elements in @param array
 * @param:	parameters for function
 *
 * Returns result of operation to be passed to secure world
 */
u32 optee_supp_thrd_req(struct tee_context *ctx, u32 func, size_t num_params,
			struct tee_param *param)

{
	struct optee *optee = tee_get_drvdata(ctx->teedev);
	struct optee_supp *supp = &optee->supp;
	struct optee_supp_req *req;
	bool interruptable;
	u32 ret;

	/*
	 * Return in case there is no supplicant available and
	 * non-blocking request.
	 */
	if (!supp->ctx && ctx->supp_nowait)
		return TEEC_ERROR_COMMUNICATION;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return TEEC_ERROR_OUT_OF_MEMORY;

	init_completion(&req->c);
	req->func = func;
	req->num_params = num_params;
	req->param = param;

	/* Insert the request in the request list */
	mutex_lock(&supp->mutex);
	list_add_tail(&req->link, &supp->reqs);
	req->in_queue = true;
	mutex_unlock(&supp->mutex);

	/* Tell an eventual waiter there's a new request */
	complete(&supp->reqs_c);

	/*
	 * Wait for supplicant to process and return result, once we've
	 * returned from wait_for_completion(&req->c) successfully we have
	 * exclusive access again.
	 */
	while (wait_for_completion_interruptible(&req->c)) {
		mutex_lock(&supp->mutex);
		interruptable = !supp->ctx;
		if (interruptable) {
			/*
			 * There's no supplicant available and since the
			 * supp->mutex currently is held none can
			 * become available until the mutex released
			 * again.
			 *
			 * Interrupting an RPC to supplicant is only
			 * allowed as a way of slightly improving the user
			 * experience in case the supplicant hasn't been
			 * started yet. During normal operation the supplicant
			 * will serve all requests in a timely manner and
			 * interrupting then wouldn't make sense.
			 */
			if (req->in_queue) {
				list_del(&req->link);
				req->in_queue = false;
			}
		}
		mutex_unlock(&supp->mutex);

		if (interruptable) {
			req->ret = TEEC_ERROR_COMMUNICATION;
			break;
		}
	}

	ret = req->ret;
	kfree(req);

	return ret;
}

static struct optee_supp_req  *supp_pop_entry(struct optee_supp *supp,
					      int num_params, int *id)
{
	struct optee_supp_req *req;

	if (supp->req_id != -1) {
		/*
		 * Supplicant should not mix synchronous and asnynchronous
		 * requests.
		 */
		return ERR_PTR(-EINVAL);
	}

	if (list_empty(&supp->reqs))
		return NULL;

	req = list_first_entry(&supp->reqs, struct optee_supp_req, link);

	if (num_params < req->num_params) {
		/* Not enough room for parameters */
		return ERR_PTR(-EINVAL);
	}

	*id = idr_alloc(&supp->idr, req, 1, 0, GFP_KERNEL);
	if (*id < 0)
		return ERR_PTR(-ENOMEM);

	list_del(&req->link);
	req->in_queue = false;

	return req;
}

static int supp_check_recv_params(size_t num_params, struct tee_param *params,
				  size_t *num_meta)
{
	size_t n;

	if (!num_params)
		return -EINVAL;

	/*
	 * If there's memrefs we need to decrease those as they where
	 * increased earlier and we'll even refuse to accept any below.
	 */
	for (n = 0; n < num_params; n++)
		if (tee_param_is_memref(params + n) && params[n].u.memref.shm)
			tee_shm_put(params[n].u.memref.shm);

	/*
	 * We only expect parameters as TEE_IOCTL_PARAM_ATTR_TYPE_NONE with
	 * or without the TEE_IOCTL_PARAM_ATTR_META bit set.
	 */
	for (n = 0; n < num_params; n++)
		if (params[n].attr &&
		    params[n].attr != TEE_IOCTL_PARAM_ATTR_META)
			return -EINVAL;

	/* At most we'll need one meta parameter so no need to check for more */
	if (params->attr == TEE_IOCTL_PARAM_ATTR_META)
		*num_meta = 1;
	else
		*num_meta = 0;

	return 0;
}

/**
 * optee_supp_recv() - receive request for supplicant
 * @ctx:	context receiving the request
 * @func:	requested function in supplicant
 * @num_params:	number of elements allocated in @param, updated with number
 *		used elements
 * @param:	space for parameters for @func
 *
 * Returns 0 on success or <0 on failure
 */
int optee_supp_recv(struct tee_context *ctx, u32 *func, u32 *num_params,
		    struct tee_param *param)
{
	struct tee_device *teedev = ctx->teedev;
	struct optee *optee = tee_get_drvdata(teedev);
	struct optee_supp *supp = &optee->supp;
	struct optee_supp_req *req = NULL;
	int id;
	size_t num_meta;
	int rc;

	rc = supp_check_recv_params(*num_params, param, &num_meta);
	if (rc)
		return rc;

	while (true) {
		mutex_lock(&supp->mutex);
		req = supp_pop_entry(supp, *num_params - num_meta, &id);
		mutex_unlock(&supp->mutex);

		if (req) {
			if (IS_ERR(req))
				return PTR_ERR(req);
			break;
		}

		/*
		 * If we didn't get a request we'll block in
		 * wait_for_completion() to avoid needless spinning.
		 *
		 * This is where supplicant will be hanging most of
		 * the time, let's make this interruptable so we
		 * can easily restart supplicant if needed.
		 */
		if (wait_for_completion_interruptible(&supp->reqs_c))
			return -ERESTARTSYS;
	}

	if (num_meta) {
		/*
		 * tee-supplicant support meta parameters -> requsts can be
		 * processed asynchronously.
		 */
		param->attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT |
			      TEE_IOCTL_PARAM_ATTR_META;
		param->u.value.a = id;
		param->u.value.b = 0;
		param->u.value.c = 0;
	} else {
		mutex_lock(&supp->mutex);
		supp->req_id = id;
		mutex_unlock(&supp->mutex);
	}

	*func = req->func;
	*num_params = req->num_params + num_meta;
	memcpy(param + num_meta, req->param,
	       sizeof(struct tee_param) * req->num_params);

	return 0;
}

static struct optee_supp_req *supp_pop_req(struct optee_supp *supp,
					   size_t num_params,
					   struct tee_param *param,
					   size_t *num_meta)
{
	struct optee_supp_req *req;
	int id;
	size_t nm;
	const u32 attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT |
			 TEE_IOCTL_PARAM_ATTR_META;

	if (!num_params)
		return ERR_PTR(-EINVAL);

	if (supp->req_id == -1) {
		if (param->attr != attr)
			return ERR_PTR(-EINVAL);
		id = param->u.value.a;
		nm = 1;
	} else {
		id = supp->req_id;
		nm = 0;
	}

	req = idr_find(&supp->idr, id);
	if (!req)
		return ERR_PTR(-ENOENT);

	if ((num_params - nm) != req->num_params)
		return ERR_PTR(-EINVAL);

	idr_remove(&supp->idr, id);
	supp->req_id = -1;
	*num_meta = nm;

	return req;
}

/**
 * optee_supp_send() - send result of request from supplicant
 * @ctx:	context sending result
 * @ret:	return value of request
 * @num_params:	number of parameters returned
 * @param:	returned parameters
 *
 * Returns 0 on success or <0 on failure.
 */
int optee_supp_send(struct tee_context *ctx, u32 ret, u32 num_params,
		    struct tee_param *param)
{
	struct tee_device *teedev = ctx->teedev;
	struct optee *optee = tee_get_drvdata(teedev);
	struct optee_supp *supp = &optee->supp;
	struct optee_supp_req *req;
	size_t n;
	size_t num_meta;

	mutex_lock(&supp->mutex);
	req = supp_pop_req(supp, num_params, param, &num_meta);
	mutex_unlock(&supp->mutex);

	if (IS_ERR(req)) {
		/* Something is wrong, let supplicant restart. */
		return PTR_ERR(req);
	}

	/* Update out and in/out parameters */
	for (n = 0; n < req->num_params; n++) {
		struct tee_param *p = req->param + n;

		switch (p->attr & TEE_IOCTL_PARAM_ATTR_TYPE_MASK) {
		case TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_OUTPUT:
		case TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT:
			p->u.value.a = param[n + num_meta].u.value.a;
			p->u.value.b = param[n + num_meta].u.value.b;
			p->u.value.c = param[n + num_meta].u.value.c;
			break;
		case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT:
		case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT:
			p->u.memref.size = param[n + num_meta].u.memref.size;
			break;
		default:
			break;
		}
	}
	req->ret = ret;

	/* Let the requesting thread continue */
	complete(&req->c);

	return 0;
}
