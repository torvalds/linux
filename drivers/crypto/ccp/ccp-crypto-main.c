/*
 * AMD Cryptographic Coprocessor (CCP) crypto API support
 *
 * Copyright (C) 2013 Advanced Micro Devices, Inc.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/ccp.h>
#include <linux/scatterlist.h>
#include <crypto/internal/hash.h>

#include "ccp-crypto.h"

MODULE_AUTHOR("Tom Lendacky <thomas.lendacky@amd.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");
MODULE_DESCRIPTION("AMD Cryptographic Coprocessor crypto API support");

static unsigned int aes_disable;
module_param(aes_disable, uint, 0444);
MODULE_PARM_DESC(aes_disable, "Disable use of AES - any non-zero value");

static unsigned int sha_disable;
module_param(sha_disable, uint, 0444);
MODULE_PARM_DESC(sha_disable, "Disable use of SHA - any non-zero value");


/* List heads for the supported algorithms */
static LIST_HEAD(hash_algs);
static LIST_HEAD(cipher_algs);

/* For any tfm, requests for that tfm must be returned on the order
 * received.  With multiple queues available, the CCP can process more
 * than one cmd at a time.  Therefore we must maintain a cmd list to insure
 * the proper ordering of requests on a given tfm.
 */
struct ccp_crypto_queue {
	struct list_head cmds;
	struct list_head *backlog;
	unsigned int cmd_count;
};
#define CCP_CRYPTO_MAX_QLEN	100

static struct ccp_crypto_queue req_queue;
static spinlock_t req_queue_lock;

struct ccp_crypto_cmd {
	struct list_head entry;

	struct ccp_cmd *cmd;

	/* Save the crypto_tfm and crypto_async_request addresses
	 * separately to avoid any reference to a possibly invalid
	 * crypto_async_request structure after invoking the request
	 * callback
	 */
	struct crypto_async_request *req;
	struct crypto_tfm *tfm;

	/* Used for held command processing to determine state */
	int ret;
};

struct ccp_crypto_cpu {
	struct work_struct work;
	struct completion completion;
	struct ccp_crypto_cmd *crypto_cmd;
	int err;
};


static inline bool ccp_crypto_success(int err)
{
	if (err && (err != -EINPROGRESS) && (err != -EBUSY))
		return false;

	return true;
}

static struct ccp_crypto_cmd *ccp_crypto_cmd_complete(
	struct ccp_crypto_cmd *crypto_cmd, struct ccp_crypto_cmd **backlog)
{
	struct ccp_crypto_cmd *held = NULL, *tmp;
	unsigned long flags;

	*backlog = NULL;

	spin_lock_irqsave(&req_queue_lock, flags);

	/* Held cmds will be after the current cmd in the queue so start
	 * searching for a cmd with a matching tfm for submission.
	 */
	tmp = crypto_cmd;
	list_for_each_entry_continue(tmp, &req_queue.cmds, entry) {
		if (crypto_cmd->tfm != tmp->tfm)
			continue;
		held = tmp;
		break;
	}

	/* Process the backlog:
	 *   Because cmds can be executed from any point in the cmd list
	 *   special precautions have to be taken when handling the backlog.
	 */
	if (req_queue.backlog != &req_queue.cmds) {
		/* Skip over this cmd if it is the next backlog cmd */
		if (req_queue.backlog == &crypto_cmd->entry)
			req_queue.backlog = crypto_cmd->entry.next;

		*backlog = container_of(req_queue.backlog,
					struct ccp_crypto_cmd, entry);
		req_queue.backlog = req_queue.backlog->next;

		/* Skip over this cmd if it is now the next backlog cmd */
		if (req_queue.backlog == &crypto_cmd->entry)
			req_queue.backlog = crypto_cmd->entry.next;
	}

	/* Remove the cmd entry from the list of cmds */
	req_queue.cmd_count--;
	list_del(&crypto_cmd->entry);

	spin_unlock_irqrestore(&req_queue_lock, flags);

	return held;
}

static void ccp_crypto_complete(void *data, int err)
{
	struct ccp_crypto_cmd *crypto_cmd = data;
	struct ccp_crypto_cmd *held, *next, *backlog;
	struct crypto_async_request *req = crypto_cmd->req;
	struct ccp_ctx *ctx = crypto_tfm_ctx(req->tfm);
	int ret;

	if (err == -EINPROGRESS) {
		/* Only propogate the -EINPROGRESS if necessary */
		if (crypto_cmd->ret == -EBUSY) {
			crypto_cmd->ret = -EINPROGRESS;
			req->complete(req, -EINPROGRESS);
		}

		return;
	}

	/* Operation has completed - update the queue before invoking
	 * the completion callbacks and retrieve the next cmd (cmd with
	 * a matching tfm) that can be submitted to the CCP.
	 */
	held = ccp_crypto_cmd_complete(crypto_cmd, &backlog);
	if (backlog) {
		backlog->ret = -EINPROGRESS;
		backlog->req->complete(backlog->req, -EINPROGRESS);
	}

	/* Transition the state from -EBUSY to -EINPROGRESS first */
	if (crypto_cmd->ret == -EBUSY)
		req->complete(req, -EINPROGRESS);

	/* Completion callbacks */
	ret = err;
	if (ctx->complete)
		ret = ctx->complete(req, ret);
	req->complete(req, ret);

	/* Submit the next cmd */
	while (held) {
		/* Since we have already queued the cmd, we must indicate that
		 * we can backlog so as not to "lose" this request.
		 */
		held->cmd->flags |= CCP_CMD_MAY_BACKLOG;
		ret = ccp_enqueue_cmd(held->cmd);
		if (ccp_crypto_success(ret))
			break;

		/* Error occurred, report it and get the next entry */
		ctx = crypto_tfm_ctx(held->req->tfm);
		if (ctx->complete)
			ret = ctx->complete(held->req, ret);
		held->req->complete(held->req, ret);

		next = ccp_crypto_cmd_complete(held, &backlog);
		if (backlog) {
			backlog->ret = -EINPROGRESS;
			backlog->req->complete(backlog->req, -EINPROGRESS);
		}

		kfree(held);
		held = next;
	}

	kfree(crypto_cmd);
}

static int ccp_crypto_enqueue_cmd(struct ccp_crypto_cmd *crypto_cmd)
{
	struct ccp_crypto_cmd *active = NULL, *tmp;
	unsigned long flags;
	bool free_cmd = true;
	int ret;

	spin_lock_irqsave(&req_queue_lock, flags);

	/* Check if the cmd can/should be queued */
	if (req_queue.cmd_count >= CCP_CRYPTO_MAX_QLEN) {
		ret = -EBUSY;
		if (!(crypto_cmd->cmd->flags & CCP_CMD_MAY_BACKLOG))
			goto e_lock;
	}

	/* Look for an entry with the same tfm.  If there is a cmd
	 * with the same tfm in the list then the current cmd cannot
	 * be submitted to the CCP yet.
	 */
	list_for_each_entry(tmp, &req_queue.cmds, entry) {
		if (crypto_cmd->tfm != tmp->tfm)
			continue;
		active = tmp;
		break;
	}

	ret = -EINPROGRESS;
	if (!active) {
		ret = ccp_enqueue_cmd(crypto_cmd->cmd);
		if (!ccp_crypto_success(ret))
			goto e_lock;	/* Error, don't queue it */
		if ((ret == -EBUSY) &&
		    !(crypto_cmd->cmd->flags & CCP_CMD_MAY_BACKLOG))
			goto e_lock;	/* Not backlogging, don't queue it */
	}

	if (req_queue.cmd_count >= CCP_CRYPTO_MAX_QLEN) {
		ret = -EBUSY;
		if (req_queue.backlog == &req_queue.cmds)
			req_queue.backlog = &crypto_cmd->entry;
	}
	crypto_cmd->ret = ret;

	req_queue.cmd_count++;
	list_add_tail(&crypto_cmd->entry, &req_queue.cmds);

	free_cmd = false;

e_lock:
	spin_unlock_irqrestore(&req_queue_lock, flags);

	if (free_cmd)
		kfree(crypto_cmd);

	return ret;
}

/**
 * ccp_crypto_enqueue_request - queue an crypto async request for processing
 *				by the CCP
 *
 * @req: crypto_async_request struct to be processed
 * @cmd: ccp_cmd struct to be sent to the CCP
 */
int ccp_crypto_enqueue_request(struct crypto_async_request *req,
			       struct ccp_cmd *cmd)
{
	struct ccp_crypto_cmd *crypto_cmd;
	gfp_t gfp;

	gfp = req->flags & CRYPTO_TFM_REQ_MAY_SLEEP ? GFP_KERNEL : GFP_ATOMIC;

	crypto_cmd = kzalloc(sizeof(*crypto_cmd), gfp);
	if (!crypto_cmd)
		return -ENOMEM;

	/* The tfm pointer must be saved and not referenced from the
	 * crypto_async_request (req) pointer because it is used after
	 * completion callback for the request and the req pointer
	 * might not be valid anymore.
	 */
	crypto_cmd->cmd = cmd;
	crypto_cmd->req = req;
	crypto_cmd->tfm = req->tfm;

	cmd->callback = ccp_crypto_complete;
	cmd->data = crypto_cmd;

	if (req->flags & CRYPTO_TFM_REQ_MAY_BACKLOG)
		cmd->flags |= CCP_CMD_MAY_BACKLOG;
	else
		cmd->flags &= ~CCP_CMD_MAY_BACKLOG;

	return ccp_crypto_enqueue_cmd(crypto_cmd);
}

struct scatterlist *ccp_crypto_sg_table_add(struct sg_table *table,
					    struct scatterlist *sg_add)
{
	struct scatterlist *sg, *sg_last = NULL;

	for (sg = table->sgl; sg; sg = sg_next(sg))
		if (!sg_page(sg))
			break;
	BUG_ON(!sg);

	for (; sg && sg_add; sg = sg_next(sg), sg_add = sg_next(sg_add)) {
		sg_set_page(sg, sg_page(sg_add), sg_add->length,
			    sg_add->offset);
		sg_last = sg;
	}
	BUG_ON(sg_add);

	return sg_last;
}

static int ccp_register_algs(void)
{
	int ret;

	if (!aes_disable) {
		ret = ccp_register_aes_algs(&cipher_algs);
		if (ret)
			return ret;

		ret = ccp_register_aes_cmac_algs(&hash_algs);
		if (ret)
			return ret;

		ret = ccp_register_aes_xts_algs(&cipher_algs);
		if (ret)
			return ret;
	}

	if (!sha_disable) {
		ret = ccp_register_sha_algs(&hash_algs);
		if (ret)
			return ret;
	}

	return 0;
}

static void ccp_unregister_algs(void)
{
	struct ccp_crypto_ahash_alg *ahash_alg, *ahash_tmp;
	struct ccp_crypto_ablkcipher_alg *ablk_alg, *ablk_tmp;

	list_for_each_entry_safe(ahash_alg, ahash_tmp, &hash_algs, entry) {
		crypto_unregister_ahash(&ahash_alg->alg);
		list_del(&ahash_alg->entry);
		kfree(ahash_alg);
	}

	list_for_each_entry_safe(ablk_alg, ablk_tmp, &cipher_algs, entry) {
		crypto_unregister_alg(&ablk_alg->alg);
		list_del(&ablk_alg->entry);
		kfree(ablk_alg);
	}
}

static int ccp_crypto_init(void)
{
	int ret;

	ret = ccp_present();
	if (ret)
		return ret;

	spin_lock_init(&req_queue_lock);
	INIT_LIST_HEAD(&req_queue.cmds);
	req_queue.backlog = &req_queue.cmds;
	req_queue.cmd_count = 0;

	ret = ccp_register_algs();
	if (ret)
		ccp_unregister_algs();

	return ret;
}

static void ccp_crypto_exit(void)
{
	ccp_unregister_algs();
}

module_init(ccp_crypto_init);
module_exit(ccp_crypto_exit);
