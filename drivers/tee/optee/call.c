// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2021, Linaro Limited
 */
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/tee_drv.h>
#include <linux/types.h>
#include "optee_private.h"

void optee_cq_wait_init(struct optee_call_queue *cq,
			struct optee_call_waiter *w)
{
	/*
	 * We're preparing to make a call to secure world. In case we can't
	 * allocate a thread in secure world we'll end up waiting in
	 * optee_cq_wait_for_completion().
	 *
	 * Normally if there's no contention in secure world the call will
	 * complete and we can cleanup directly with optee_cq_wait_final().
	 */
	mutex_lock(&cq->mutex);

	/*
	 * We add ourselves to the queue, but we don't wait. This
	 * guarantees that we don't lose a completion if secure world
	 * returns busy and another thread just exited and try to complete
	 * someone.
	 */
	init_completion(&w->c);
	list_add_tail(&w->list_node, &cq->waiters);

	mutex_unlock(&cq->mutex);
}

void optee_cq_wait_for_completion(struct optee_call_queue *cq,
				  struct optee_call_waiter *w)
{
	wait_for_completion(&w->c);

	mutex_lock(&cq->mutex);

	/* Move to end of list to get out of the way for other waiters */
	list_del(&w->list_node);
	reinit_completion(&w->c);
	list_add_tail(&w->list_node, &cq->waiters);

	mutex_unlock(&cq->mutex);
}

static void optee_cq_complete_one(struct optee_call_queue *cq)
{
	struct optee_call_waiter *w;

	list_for_each_entry(w, &cq->waiters, list_node) {
		if (!completion_done(&w->c)) {
			complete(&w->c);
			break;
		}
	}
}

void optee_cq_wait_final(struct optee_call_queue *cq,
			 struct optee_call_waiter *w)
{
	/*
	 * We're done with the call to secure world. The thread in secure
	 * world that was used for this call is now available for some
	 * other task to use.
	 */
	mutex_lock(&cq->mutex);

	/* Get out of the list */
	list_del(&w->list_node);

	/* Wake up one eventual waiting task */
	optee_cq_complete_one(cq);

	/*
	 * If we're completed we've got a completion from another task that
	 * was just done with its call to secure world. Since yet another
	 * thread now is available in secure world wake up another eventual
	 * waiting task.
	 */
	if (completion_done(&w->c))
		optee_cq_complete_one(cq);

	mutex_unlock(&cq->mutex);
}

/* Requires the filpstate mutex to be held */
static struct optee_session *find_session(struct optee_context_data *ctxdata,
					  u32 session_id)
{
	struct optee_session *sess;

	list_for_each_entry(sess, &ctxdata->sess_list, list_node)
		if (sess->session_id == session_id)
			return sess;

	return NULL;
}

struct tee_shm *optee_get_msg_arg(struct tee_context *ctx, size_t num_params,
				  struct optee_msg_arg **msg_arg)
{
	struct optee *optee = tee_get_drvdata(ctx->teedev);
	size_t sz = OPTEE_MSG_GET_ARG_SIZE(num_params);
	struct tee_shm *shm;
	struct optee_msg_arg *ma;

	/*
	 * rpc_arg_count is set to the number of allocated parameters in
	 * the RPC argument struct if a second MSG arg struct is expected.
	 * The second arg struct will then be used for RPC.
	 */
	if (optee->rpc_arg_count)
		sz += OPTEE_MSG_GET_ARG_SIZE(optee->rpc_arg_count);

	shm = tee_shm_alloc(ctx, sz, TEE_SHM_MAPPED | TEE_SHM_PRIV);
	if (IS_ERR(shm))
		return shm;

	ma = tee_shm_get_va(shm, 0);
	if (IS_ERR(ma)) {
		tee_shm_free(shm);
		return (void *)ma;
	}

	memset(ma, 0, OPTEE_MSG_GET_ARG_SIZE(num_params));
	ma->num_params = num_params;
	*msg_arg = ma;

	return shm;
}

int optee_open_session(struct tee_context *ctx,
		       struct tee_ioctl_open_session_arg *arg,
		       struct tee_param *param)
{
	struct optee *optee = tee_get_drvdata(ctx->teedev);
	struct optee_context_data *ctxdata = ctx->data;
	int rc;
	struct tee_shm *shm;
	struct optee_msg_arg *msg_arg;
	struct optee_session *sess = NULL;
	uuid_t client_uuid;

	/* +2 for the meta parameters added below */
	shm = optee_get_msg_arg(ctx, arg->num_params + 2, &msg_arg);
	if (IS_ERR(shm))
		return PTR_ERR(shm);

	msg_arg->cmd = OPTEE_MSG_CMD_OPEN_SESSION;
	msg_arg->cancel_id = arg->cancel_id;

	/*
	 * Initialize and add the meta parameters needed when opening a
	 * session.
	 */
	msg_arg->params[0].attr = OPTEE_MSG_ATTR_TYPE_VALUE_INPUT |
				  OPTEE_MSG_ATTR_META;
	msg_arg->params[1].attr = OPTEE_MSG_ATTR_TYPE_VALUE_INPUT |
				  OPTEE_MSG_ATTR_META;
	memcpy(&msg_arg->params[0].u.value, arg->uuid, sizeof(arg->uuid));
	msg_arg->params[1].u.value.c = arg->clnt_login;

	rc = tee_session_calc_client_uuid(&client_uuid, arg->clnt_login,
					  arg->clnt_uuid);
	if (rc)
		goto out;
	export_uuid(msg_arg->params[1].u.octets, &client_uuid);

	rc = optee->ops->to_msg_param(optee, msg_arg->params + 2,
				      arg->num_params, param);
	if (rc)
		goto out;

	sess = kzalloc(sizeof(*sess), GFP_KERNEL);
	if (!sess) {
		rc = -ENOMEM;
		goto out;
	}

	if (optee->ops->do_call_with_arg(ctx, shm)) {
		msg_arg->ret = TEEC_ERROR_COMMUNICATION;
		msg_arg->ret_origin = TEEC_ORIGIN_COMMS;
	}

	if (msg_arg->ret == TEEC_SUCCESS) {
		/* A new session has been created, add it to the list. */
		sess->session_id = msg_arg->session;
		mutex_lock(&ctxdata->mutex);
		list_add(&sess->list_node, &ctxdata->sess_list);
		mutex_unlock(&ctxdata->mutex);
	} else {
		kfree(sess);
	}

	if (optee->ops->from_msg_param(optee, param, arg->num_params,
				       msg_arg->params + 2)) {
		arg->ret = TEEC_ERROR_COMMUNICATION;
		arg->ret_origin = TEEC_ORIGIN_COMMS;
		/* Close session again to avoid leakage */
		optee_close_session(ctx, msg_arg->session);
	} else {
		arg->session = msg_arg->session;
		arg->ret = msg_arg->ret;
		arg->ret_origin = msg_arg->ret_origin;
	}
out:
	tee_shm_free(shm);

	return rc;
}

int optee_close_session_helper(struct tee_context *ctx, u32 session)
{
	struct tee_shm *shm;
	struct optee *optee = tee_get_drvdata(ctx->teedev);
	struct optee_msg_arg *msg_arg;

	shm = optee_get_msg_arg(ctx, 0, &msg_arg);
	if (IS_ERR(shm))
		return PTR_ERR(shm);

	msg_arg->cmd = OPTEE_MSG_CMD_CLOSE_SESSION;
	msg_arg->session = session;
	optee->ops->do_call_with_arg(ctx, shm);

	tee_shm_free(shm);

	return 0;
}

int optee_close_session(struct tee_context *ctx, u32 session)
{
	struct optee_context_data *ctxdata = ctx->data;
	struct optee_session *sess;

	/* Check that the session is valid and remove it from the list */
	mutex_lock(&ctxdata->mutex);
	sess = find_session(ctxdata, session);
	if (sess)
		list_del(&sess->list_node);
	mutex_unlock(&ctxdata->mutex);
	if (!sess)
		return -EINVAL;
	kfree(sess);

	return optee_close_session_helper(ctx, session);
}

int optee_invoke_func(struct tee_context *ctx, struct tee_ioctl_invoke_arg *arg,
		      struct tee_param *param)
{
	struct optee *optee = tee_get_drvdata(ctx->teedev);
	struct optee_context_data *ctxdata = ctx->data;
	struct tee_shm *shm;
	struct optee_msg_arg *msg_arg;
	struct optee_session *sess;
	int rc;

	/* Check that the session is valid */
	mutex_lock(&ctxdata->mutex);
	sess = find_session(ctxdata, arg->session);
	mutex_unlock(&ctxdata->mutex);
	if (!sess)
		return -EINVAL;

	shm = optee_get_msg_arg(ctx, arg->num_params, &msg_arg);
	if (IS_ERR(shm))
		return PTR_ERR(shm);
	msg_arg->cmd = OPTEE_MSG_CMD_INVOKE_COMMAND;
	msg_arg->func = arg->func;
	msg_arg->session = arg->session;
	msg_arg->cancel_id = arg->cancel_id;

	rc = optee->ops->to_msg_param(optee, msg_arg->params, arg->num_params,
				      param);
	if (rc)
		goto out;

	if (optee->ops->do_call_with_arg(ctx, shm)) {
		msg_arg->ret = TEEC_ERROR_COMMUNICATION;
		msg_arg->ret_origin = TEEC_ORIGIN_COMMS;
	}

	if (optee->ops->from_msg_param(optee, param, arg->num_params,
				       msg_arg->params)) {
		msg_arg->ret = TEEC_ERROR_COMMUNICATION;
		msg_arg->ret_origin = TEEC_ORIGIN_COMMS;
	}

	arg->ret = msg_arg->ret;
	arg->ret_origin = msg_arg->ret_origin;
out:
	tee_shm_free(shm);
	return rc;
}

int optee_cancel_req(struct tee_context *ctx, u32 cancel_id, u32 session)
{
	struct optee *optee = tee_get_drvdata(ctx->teedev);
	struct optee_context_data *ctxdata = ctx->data;
	struct tee_shm *shm;
	struct optee_msg_arg *msg_arg;
	struct optee_session *sess;

	/* Check that the session is valid */
	mutex_lock(&ctxdata->mutex);
	sess = find_session(ctxdata, session);
	mutex_unlock(&ctxdata->mutex);
	if (!sess)
		return -EINVAL;

	shm = optee_get_msg_arg(ctx, 0, &msg_arg);
	if (IS_ERR(shm))
		return PTR_ERR(shm);

	msg_arg->cmd = OPTEE_MSG_CMD_CANCEL;
	msg_arg->session = session;
	msg_arg->cancel_id = cancel_id;
	optee->ops->do_call_with_arg(ctx, shm);

	tee_shm_free(shm);
	return 0;
}

static bool is_normal_memory(pgprot_t p)
{
#if defined(CONFIG_ARM)
	return (((pgprot_val(p) & L_PTE_MT_MASK) == L_PTE_MT_WRITEALLOC) ||
		((pgprot_val(p) & L_PTE_MT_MASK) == L_PTE_MT_WRITEBACK));
#elif defined(CONFIG_ARM64)
	return (pgprot_val(p) & PTE_ATTRINDX_MASK) == PTE_ATTRINDX(MT_NORMAL);
#else
#error "Unuspported architecture"
#endif
}

static int __check_mem_type(struct vm_area_struct *vma, unsigned long end)
{
	while (vma && is_normal_memory(vma->vm_page_prot)) {
		if (vma->vm_end >= end)
			return 0;
		vma = vma->vm_next;
	}

	return -EINVAL;
}

int optee_check_mem_type(unsigned long start, size_t num_pages)
{
	struct mm_struct *mm = current->mm;
	int rc;

	/*
	 * Allow kernel address to register with OP-TEE as kernel
	 * pages are configured as normal memory only.
	 */
	if (virt_addr_valid(start))
		return 0;

	mmap_read_lock(mm);
	rc = __check_mem_type(find_vma(mm, start),
			      start + num_pages * PAGE_SIZE);
	mmap_read_unlock(mm);

	return rc;
}
