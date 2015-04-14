/*
 * Copyright (c) 2015, Linaro Limited
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "optee_private.h"

void optee_supp_init(struct optee_supp *supp)
{
	memset(supp, 0, sizeof(*supp));
	mutex_init(&supp->thrd_mutex);
	mutex_init(&supp->supp_mutex);
	init_completion(&supp->data_to_supp);
	init_completion(&supp->data_from_supp);
}

void optee_supp_uninit(struct optee_supp *supp)
{
	mutex_destroy(&supp->thrd_mutex);
	mutex_destroy(&supp->supp_mutex);
}

u32 optee_supp_thrd_req(struct tee_context *ctx, u32 func, size_t num_params,
			struct tee_param *param)
{
	struct optee *optee = tee_get_drvdata(ctx->teedev);
	struct optee_supp *supp = &optee->supp;
	u32 ret;

	/*
	 * Other threads blocks here until we've copied our answer from
	 * supplicant.
	 */
	mutex_lock(&supp->thrd_mutex);

	/*
	 * We have exclusive access now since the supplicant at this
	 * point is either doing a
	 * wait_for_completion_interruptible(data_to_supp) or is in
	 * userspace still about to do the ioctl() to enter
	 * optee_supp_read() below.
	 */

	supp->func = func;
	supp->num_params = num_params;
	supp->param = param;
	supp->req_posted = true;

	/* Let supplicant get the data */
	complete(&supp->data_to_supp);

	/*
	 * Wait for supplicant to process and return result, once we've
	 * returned from wait_for_completion(data_from_supp) we have
	 * exclusive access again.
	 */
	wait_for_completion(&supp->data_from_supp);

	ret = supp->ret;
	supp->param = NULL;
	supp->req_posted = false;

	/* We're done, let someone else talk to the supplicant now. */
	mutex_unlock(&supp->thrd_mutex);

	return ret;
}

int optee_supp_recv(struct tee_context *ctx, u32 *func, u32 *num_params,
		struct tee_param *param)
{
	struct tee_device *teedev = ctx->teedev;
	struct optee *optee = tee_get_drvdata(teedev);
	struct optee_supp *supp = &optee->supp;
	int rc;

	/*
	 * In case two supplicants or two threads in one supplicant is
	 * calling this function simultaneously we need to protect the
	 * data with a mutex which we'll release before returning.
	 */
	mutex_lock(&supp->supp_mutex);

	if (supp->supp_next_send) {
		/*
		 * optee_supp_recv() has been called again without
		 * a optee_supp_send() in between. Supplicant has
		 * probably been restarted before it was able to
		 * write back last result. Abort last request and
		 * wait for a new.
		 */
		if (supp->req_posted) {
			supp->ret = TEEC_ERROR_COMMUNICATION;
			supp->supp_next_send = false;
			complete(&supp->data_from_supp);
		}
	}

	/*
	 * This is where supplicant will be hanging most of the
	 * time, let's make this interruptable so we can easily
	 * restart supplicant if needed.
	 */
	if (wait_for_completion_interruptible(&supp->data_to_supp)) {
		rc = -ERESTARTSYS;
		goto out;
	}

	/* We have exlusive access to the data */

	if (*num_params < supp->num_params) {
		/*
		 * Not enough room for parameters, tell supplicant
		 * it failed and abort last request.
		 */
		supp->ret = TEEC_ERROR_COMMUNICATION;
		rc = -EINVAL;
		complete(&supp->data_from_supp);
		goto out;
	}

	*func = supp->func;
	*num_params = supp->num_params;
	memcpy(param, supp->param,
	       sizeof(struct tee_param) * supp->num_params);

	/* Allow optee_supp_send() below to do its work */
	supp->supp_next_send = true;

	rc = 0;
out:
	mutex_unlock(&supp->supp_mutex);
	return rc;
}

int optee_supp_send(struct tee_context *ctx, u32 ret, u32 num_params,
			struct tee_param *param)
{
	struct tee_device *teedev = ctx->teedev;
	struct optee *optee = tee_get_drvdata(teedev);
	struct optee_supp *supp = &optee->supp;
	size_t n;
	int rc = 0;

	/*
	 * We still have exclusive access to the data since that's how we
	 * left it when returning from optee_supp_read().
	 */

	/* See comment on mutex in optee_supp_read() above */
	mutex_lock(&supp->supp_mutex);

	if (!supp->supp_next_send) {
		/*
		 * Something strange is going on, supplicant shouldn't
		 * enter optee_supp_send() in this state
		 */
		rc = -ENOENT;
		goto out;
	}

	if (num_params != supp->num_params) {
		/*
		 * Something is wrong, let supplicant restart. Next call to
		 * optee_supp_recv() will give an error to the requesting
		 * thread and release it.
		 */
		rc = -EINVAL;
		goto out;
	}

	/* Update out and in/out parameters */
	for (n = 0; n < num_params; n++) {
		struct tee_param *p = supp->param + n;

		switch (p->attr) {
		case TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_OUTPUT:
		case TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT:
			p->u.value.a = param[n].u.value.a;
			p->u.value.b = param[n].u.value.b;
			p->u.value.c = param[n].u.value.c;
			break;
		case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT:
		case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT:
			p->u.memref.size = param[n].u.memref.size;
			break;
		default:
			break;
		}
	}
	supp->ret = ret;

	/* Allow optee_supp_recv() above to do its work */
	supp->supp_next_send = false;

	/* Let the requesting thread continue */
	complete(&supp->data_from_supp);
out:
	mutex_unlock(&supp->supp_mutex);
	return rc;
}
