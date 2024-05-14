/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2020 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <asm/byteorder.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/errno.h>
#include <linux/ethtool.h>
#include <linux/if_ether.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/skbuff.h>

#include "bnxt_hsi.h"
#include "bnxt.h"
#include "bnxt_hwrm.h"

static u64 hwrm_calc_sentinel(struct bnxt_hwrm_ctx *ctx, u16 req_type)
{
	return (((uintptr_t)ctx) + req_type) ^ BNXT_HWRM_SENTINEL;
}

/**
 * __hwrm_req_init() - Initialize an HWRM request.
 * @bp: The driver context.
 * @req: A pointer to the request pointer to initialize.
 * @req_type: The request type. This will be converted to the little endian
 *	before being written to the req_type field of the returned request.
 * @req_len: The length of the request to be allocated.
 *
 * Allocate DMA resources and initialize a new HWRM request object of the
 * given type. The response address field in the request is configured with
 * the DMA bus address that has been mapped for the response and the passed
 * request is pointed to kernel virtual memory mapped for the request (such
 * that short_input indirection can be accomplished without copying). The
 * request’s target and completion ring are initialized to default values and
 * can be overridden by writing to the returned request object directly.
 *
 * The initialized request can be further customized by writing to its fields
 * directly, taking care to covert such fields to little endian. The request
 * object will be consumed (and all its associated resources release) upon
 * passing it to hwrm_req_send() unless ownership of the request has been
 * claimed by the caller via a call to hwrm_req_hold(). If the request is not
 * consumed, either because it is never sent or because ownership has been
 * claimed, then it must be released by a call to hwrm_req_drop().
 *
 * Return: zero on success, negative error code otherwise:
 *	E2BIG: the type of request pointer is too large to fit.
 *	ENOMEM: an allocation failure occurred.
 */
int __hwrm_req_init(struct bnxt *bp, void **req, u16 req_type, u32 req_len)
{
	struct bnxt_hwrm_ctx *ctx;
	dma_addr_t dma_handle;
	u8 *req_addr;

	if (req_len > BNXT_HWRM_CTX_OFFSET)
		return -E2BIG;

	req_addr = dma_pool_alloc(bp->hwrm_dma_pool, GFP_KERNEL | __GFP_ZERO,
				  &dma_handle);
	if (!req_addr)
		return -ENOMEM;

	ctx = (struct bnxt_hwrm_ctx *)(req_addr + BNXT_HWRM_CTX_OFFSET);
	/* safety first, sentinel used to check for invalid requests */
	ctx->sentinel = hwrm_calc_sentinel(ctx, req_type);
	ctx->req_len = req_len;
	ctx->req = (struct input *)req_addr;
	ctx->resp = (struct output *)(req_addr + BNXT_HWRM_RESP_OFFSET);
	ctx->dma_handle = dma_handle;
	ctx->flags = 0; /* __GFP_ZERO, but be explicit regarding ownership */
	ctx->timeout = bp->hwrm_cmd_timeout ?: DFLT_HWRM_CMD_TIMEOUT;
	ctx->allocated = BNXT_HWRM_DMA_SIZE - BNXT_HWRM_CTX_OFFSET;
	ctx->gfp = GFP_KERNEL;
	ctx->slice_addr = NULL;

	/* initialize common request fields */
	ctx->req->req_type = cpu_to_le16(req_type);
	ctx->req->resp_addr = cpu_to_le64(dma_handle + BNXT_HWRM_RESP_OFFSET);
	ctx->req->cmpl_ring = cpu_to_le16(BNXT_HWRM_NO_CMPL_RING);
	ctx->req->target_id = cpu_to_le16(BNXT_HWRM_TARGET);
	*req = ctx->req;

	return 0;
}

static struct bnxt_hwrm_ctx *__hwrm_ctx(struct bnxt *bp, u8 *req_addr)
{
	void *ctx_addr = req_addr + BNXT_HWRM_CTX_OFFSET;
	struct input *req = (struct input *)req_addr;
	struct bnxt_hwrm_ctx *ctx = ctx_addr;
	u64 sentinel;

	if (!req) {
		/* can only be due to software bug, be loud */
		netdev_err(bp->dev, "null HWRM request");
		dump_stack();
		return NULL;
	}

	/* HWRM API has no type safety, verify sentinel to validate address */
	sentinel = hwrm_calc_sentinel(ctx, le16_to_cpu(req->req_type));
	if (ctx->sentinel != sentinel) {
		/* can only be due to software bug, be loud */
		netdev_err(bp->dev, "HWRM sentinel mismatch, req_type = %u\n",
			   (u32)le16_to_cpu(req->req_type));
		dump_stack();
		return NULL;
	}

	return ctx;
}

/**
 * hwrm_req_timeout() - Set the completion timeout for the request.
 * @bp: The driver context.
 * @req: The request to set the timeout.
 * @timeout: The timeout in milliseconds.
 *
 * Set the timeout associated with the request for subsequent calls to
 * hwrm_req_send(). Some requests are long running and require a different
 * timeout than the default.
 */
void hwrm_req_timeout(struct bnxt *bp, void *req, unsigned int timeout)
{
	struct bnxt_hwrm_ctx *ctx = __hwrm_ctx(bp, req);

	if (ctx)
		ctx->timeout = timeout;
}

/**
 * hwrm_req_alloc_flags() - Sets GFP allocation flags for slices.
 * @bp: The driver context.
 * @req: The request for which calls to hwrm_req_dma_slice() will have altered
 *	allocation flags.
 * @gfp: A bitmask of GFP flags. These flags are passed to dma_alloc_coherent()
 *	whenever it is used to allocate backing memory for slices. Note that
 *	calls to hwrm_req_dma_slice() will not always result in new allocations,
 *	however, memory suballocated from the request buffer is already
 *	__GFP_ZERO.
 *
 * Sets the GFP allocation flags associated with the request for subsequent
 * calls to hwrm_req_dma_slice(). This can be useful for specifying __GFP_ZERO
 * for slice allocations.
 */
void hwrm_req_alloc_flags(struct bnxt *bp, void *req, gfp_t gfp)
{
	struct bnxt_hwrm_ctx *ctx = __hwrm_ctx(bp, req);

	if (ctx)
		ctx->gfp = gfp;
}

/**
 * hwrm_req_replace() - Replace request data.
 * @bp: The driver context.
 * @req: The request to modify. A call to hwrm_req_replace() is conceptually
 *	an assignment of new_req to req. Subsequent calls to HWRM API functions,
 *	such as hwrm_req_send(), should thus use req and not new_req (in fact,
 *	calls to HWRM API functions will fail if non-managed request objects
 *	are passed).
 * @len: The length of new_req.
 * @new_req: The pre-built request to copy or reference.
 *
 * Replaces the request data in req with that of new_req. This is useful in
 * scenarios where a request object has already been constructed by a third
 * party prior to creating a resource managed request using hwrm_req_init().
 * Depending on the length, hwrm_req_replace() will either copy the new
 * request data into the DMA memory allocated for req, or it will simply
 * reference the new request and use it in lieu of req during subsequent
 * calls to hwrm_req_send(). The resource management is associated with
 * req and is independent of and does not apply to new_req. The caller must
 * ensure that the lifetime of new_req is least as long as req. Any slices
 * that may have been associated with the original request are released.
 *
 * Return: zero on success, negative error code otherwise:
 *     E2BIG: Request is too large.
 *     EINVAL: Invalid request to modify.
 */
int hwrm_req_replace(struct bnxt *bp, void *req, void *new_req, u32 len)
{
	struct bnxt_hwrm_ctx *ctx = __hwrm_ctx(bp, req);
	struct input *internal_req = req;
	u16 req_type;

	if (!ctx)
		return -EINVAL;

	if (len > BNXT_HWRM_CTX_OFFSET)
		return -E2BIG;

	/* free any existing slices */
	ctx->allocated = BNXT_HWRM_DMA_SIZE - BNXT_HWRM_CTX_OFFSET;
	if (ctx->slice_addr) {
		dma_free_coherent(&bp->pdev->dev, ctx->slice_size,
				  ctx->slice_addr, ctx->slice_handle);
		ctx->slice_addr = NULL;
	}
	ctx->gfp = GFP_KERNEL;

	if ((bp->fw_cap & BNXT_FW_CAP_SHORT_CMD) || len > BNXT_HWRM_MAX_REQ_LEN) {
		memcpy(internal_req, new_req, len);
	} else {
		internal_req->req_type = ((struct input *)new_req)->req_type;
		ctx->req = new_req;
	}

	ctx->req_len = len;
	ctx->req->resp_addr = cpu_to_le64(ctx->dma_handle +
					  BNXT_HWRM_RESP_OFFSET);

	/* update sentinel for potentially new request type */
	req_type = le16_to_cpu(internal_req->req_type);
	ctx->sentinel = hwrm_calc_sentinel(ctx, req_type);

	return 0;
}

/**
 * hwrm_req_flags() - Set non internal flags of the ctx
 * @bp: The driver context.
 * @req: The request containing the HWRM command
 * @flags: ctx flags that don't have BNXT_HWRM_INTERNAL_FLAG set
 *
 * ctx flags can be used by the callers to instruct how the subsequent
 * hwrm_req_send() should behave. Example: callers can use hwrm_req_flags
 * with BNXT_HWRM_CTX_SILENT to omit kernel prints of errors of hwrm_req_send()
 * or with BNXT_HWRM_FULL_WAIT enforce hwrm_req_send() to wait for full timeout
 * even if FW is not responding.
 * This generic function can be used to set any flag that is not an internal flag
 * of the HWRM module.
 */
void hwrm_req_flags(struct bnxt *bp, void *req, enum bnxt_hwrm_ctx_flags flags)
{
	struct bnxt_hwrm_ctx *ctx = __hwrm_ctx(bp, req);

	if (ctx)
		ctx->flags |= (flags & HWRM_API_FLAGS);
}

/**
 * hwrm_req_hold() - Claim ownership of the request's resources.
 * @bp: The driver context.
 * @req: A pointer to the request to own. The request will no longer be
 *	consumed by calls to hwrm_req_send().
 *
 * Take ownership of the request. Ownership places responsibility on the
 * caller to free the resources associated with the request via a call to
 * hwrm_req_drop(). The caller taking ownership implies that a subsequent
 * call to hwrm_req_send() will not consume the request (ie. sending will
 * not free the associated resources if the request is owned by the caller).
 * Taking ownership returns a reference to the response. Retaining and
 * accessing the response data is the most common reason to take ownership
 * of the request. Ownership can also be acquired in order to reuse the same
 * request object across multiple invocations of hwrm_req_send().
 *
 * Return: A pointer to the response object.
 *
 * The resources associated with the response will remain available to the
 * caller until ownership of the request is relinquished via a call to
 * hwrm_req_drop(). It is not possible for hwrm_req_hold() to return NULL if
 * a valid request is provided. A returned NULL value would imply a driver
 * bug and the implementation will complain loudly in the logs to aid in
 * detection. It should not be necessary to check the result for NULL.
 */
void *hwrm_req_hold(struct bnxt *bp, void *req)
{
	struct bnxt_hwrm_ctx *ctx = __hwrm_ctx(bp, req);
	struct input *input = (struct input *)req;

	if (!ctx)
		return NULL;

	if (ctx->flags & BNXT_HWRM_INTERNAL_CTX_OWNED) {
		/* can only be due to software bug, be loud */
		netdev_err(bp->dev, "HWRM context already owned, req_type = %u\n",
			   (u32)le16_to_cpu(input->req_type));
		dump_stack();
		return NULL;
	}

	ctx->flags |= BNXT_HWRM_INTERNAL_CTX_OWNED;
	return ((u8 *)req) + BNXT_HWRM_RESP_OFFSET;
}

static void __hwrm_ctx_drop(struct bnxt *bp, struct bnxt_hwrm_ctx *ctx)
{
	void *addr = ((u8 *)ctx) - BNXT_HWRM_CTX_OFFSET;
	dma_addr_t dma_handle = ctx->dma_handle; /* save before invalidate */

	/* unmap any auxiliary DMA slice */
	if (ctx->slice_addr)
		dma_free_coherent(&bp->pdev->dev, ctx->slice_size,
				  ctx->slice_addr, ctx->slice_handle);

	/* invalidate, ensure ownership, sentinel and dma_handle are cleared */
	memset(ctx, 0, sizeof(struct bnxt_hwrm_ctx));

	/* return the buffer to the DMA pool */
	if (dma_handle)
		dma_pool_free(bp->hwrm_dma_pool, addr, dma_handle);
}

/**
 * hwrm_req_drop() - Release all resources associated with the request.
 * @bp: The driver context.
 * @req: The request to consume, releasing the associated resources. The
 *	request object, any slices, and its associated response are no
 *	longer valid.
 *
 * It is legal to call hwrm_req_drop() on an unowned request, provided it
 * has not already been consumed by hwrm_req_send() (for example, to release
 * an aborted request). A given request should not be dropped more than once,
 * nor should it be dropped after having been consumed by hwrm_req_send(). To
 * do so is an error (the context will not be found and a stack trace will be
 * rendered in the kernel log).
 */
void hwrm_req_drop(struct bnxt *bp, void *req)
{
	struct bnxt_hwrm_ctx *ctx = __hwrm_ctx(bp, req);

	if (ctx)
		__hwrm_ctx_drop(bp, ctx);
}

static int __hwrm_to_stderr(u32 hwrm_err)
{
	switch (hwrm_err) {
	case HWRM_ERR_CODE_SUCCESS:
		return 0;
	case HWRM_ERR_CODE_RESOURCE_LOCKED:
		return -EROFS;
	case HWRM_ERR_CODE_RESOURCE_ACCESS_DENIED:
		return -EACCES;
	case HWRM_ERR_CODE_RESOURCE_ALLOC_ERROR:
		return -ENOSPC;
	case HWRM_ERR_CODE_INVALID_PARAMS:
	case HWRM_ERR_CODE_INVALID_FLAGS:
	case HWRM_ERR_CODE_INVALID_ENABLES:
	case HWRM_ERR_CODE_UNSUPPORTED_TLV:
	case HWRM_ERR_CODE_UNSUPPORTED_OPTION_ERR:
		return -EINVAL;
	case HWRM_ERR_CODE_NO_BUFFER:
		return -ENOMEM;
	case HWRM_ERR_CODE_HOT_RESET_PROGRESS:
	case HWRM_ERR_CODE_BUSY:
		return -EAGAIN;
	case HWRM_ERR_CODE_CMD_NOT_SUPPORTED:
		return -EOPNOTSUPP;
	case HWRM_ERR_CODE_PF_UNAVAILABLE:
		return -ENODEV;
	default:
		return -EIO;
	}
}

static struct bnxt_hwrm_wait_token *
__hwrm_acquire_token(struct bnxt *bp, enum bnxt_hwrm_chnl dst)
{
	struct bnxt_hwrm_wait_token *token;

	token = kzalloc(sizeof(*token), GFP_KERNEL);
	if (!token)
		return NULL;

	mutex_lock(&bp->hwrm_cmd_lock);

	token->dst = dst;
	token->state = BNXT_HWRM_PENDING;
	if (dst == BNXT_HWRM_CHNL_CHIMP) {
		token->seq_id = bp->hwrm_cmd_seq++;
		hlist_add_head_rcu(&token->node, &bp->hwrm_pending_list);
	} else {
		token->seq_id = bp->hwrm_cmd_kong_seq++;
	}

	return token;
}

static void
__hwrm_release_token(struct bnxt *bp, struct bnxt_hwrm_wait_token *token)
{
	if (token->dst == BNXT_HWRM_CHNL_CHIMP) {
		hlist_del_rcu(&token->node);
		kfree_rcu(token, rcu);
	} else {
		kfree(token);
	}
	mutex_unlock(&bp->hwrm_cmd_lock);
}

void
hwrm_update_token(struct bnxt *bp, u16 seq_id, enum bnxt_hwrm_wait_state state)
{
	struct bnxt_hwrm_wait_token *token;

	rcu_read_lock();
	hlist_for_each_entry_rcu(token, &bp->hwrm_pending_list, node) {
		if (token->seq_id == seq_id) {
			WRITE_ONCE(token->state, state);
			rcu_read_unlock();
			return;
		}
	}
	rcu_read_unlock();
	netdev_err(bp->dev, "Invalid hwrm seq id %d\n", seq_id);
}

static void hwrm_req_dbg(struct bnxt *bp, struct input *req)
{
	u32 ring = le16_to_cpu(req->cmpl_ring);
	u32 type = le16_to_cpu(req->req_type);
	u32 tgt = le16_to_cpu(req->target_id);
	u32 seq = le16_to_cpu(req->seq_id);
	char opt[32] = "\n";

	if (unlikely(ring != (u16)BNXT_HWRM_NO_CMPL_RING))
		snprintf(opt, 16, " ring %d\n", ring);

	if (unlikely(tgt != BNXT_HWRM_TARGET))
		snprintf(opt + strlen(opt) - 1, 16, " tgt 0x%x\n", tgt);

	netdev_dbg(bp->dev, "sent hwrm req_type 0x%x seq id 0x%x%s",
		   type, seq, opt);
}

#define hwrm_err(bp, ctx, fmt, ...)				       \
	do {							       \
		if ((ctx)->flags & BNXT_HWRM_CTX_SILENT)	       \
			netdev_dbg((bp)->dev, fmt, __VA_ARGS__);       \
		else						       \
			netdev_err((bp)->dev, fmt, __VA_ARGS__);       \
	} while (0)

static bool hwrm_wait_must_abort(struct bnxt *bp, u32 req_type, u32 *fw_status)
{
	if (req_type == HWRM_VER_GET)
		return false;

	if (!bp->fw_health || !bp->fw_health->status_reliable)
		return false;

	*fw_status = bnxt_fw_health_readl(bp, BNXT_FW_HEALTH_REG);
	return *fw_status && !BNXT_FW_IS_HEALTHY(*fw_status);
}

static int __hwrm_send(struct bnxt *bp, struct bnxt_hwrm_ctx *ctx)
{
	u32 doorbell_offset = BNXT_GRCPF_REG_CHIMP_COMM_TRIGGER;
	enum bnxt_hwrm_chnl dst = BNXT_HWRM_CHNL_CHIMP;
	u32 bar_offset = BNXT_GRCPF_REG_CHIMP_COMM;
	struct bnxt_hwrm_wait_token *token = NULL;
	struct hwrm_short_input short_input = {0};
	u16 max_req_len = BNXT_HWRM_MAX_REQ_LEN;
	unsigned int i, timeout, tmo_count;
	u32 *data = (u32 *)ctx->req;
	u32 msg_len = ctx->req_len;
	u32 req_type, sts;
	int rc = -EBUSY;
	u16 len = 0;
	u8 *valid;

	if (ctx->flags & BNXT_HWRM_INTERNAL_RESP_DIRTY)
		memset(ctx->resp, 0, PAGE_SIZE);

	req_type = le16_to_cpu(ctx->req->req_type);
	if (BNXT_NO_FW_ACCESS(bp) &&
	    (req_type != HWRM_FUNC_RESET && req_type != HWRM_VER_GET)) {
		netdev_dbg(bp->dev, "hwrm req_type 0x%x skipped, FW channel down\n",
			   req_type);
		goto exit;
	}

	if (msg_len > BNXT_HWRM_MAX_REQ_LEN &&
	    msg_len > bp->hwrm_max_ext_req_len) {
		rc = -E2BIG;
		goto exit;
	}

	if (bnxt_kong_hwrm_message(bp, ctx->req)) {
		dst = BNXT_HWRM_CHNL_KONG;
		bar_offset = BNXT_GRCPF_REG_KONG_COMM;
		doorbell_offset = BNXT_GRCPF_REG_KONG_COMM_TRIGGER;
		if (le16_to_cpu(ctx->req->cmpl_ring) != INVALID_HW_RING_ID) {
			netdev_err(bp->dev, "Ring completions not supported for KONG commands, req_type = %d\n",
				   req_type);
			rc = -EINVAL;
			goto exit;
		}
	}

	token = __hwrm_acquire_token(bp, dst);
	if (!token) {
		rc = -ENOMEM;
		goto exit;
	}
	ctx->req->seq_id = cpu_to_le16(token->seq_id);

	if ((bp->fw_cap & BNXT_FW_CAP_SHORT_CMD) ||
	    msg_len > BNXT_HWRM_MAX_REQ_LEN) {
		short_input.req_type = ctx->req->req_type;
		short_input.signature =
				cpu_to_le16(SHORT_REQ_SIGNATURE_SHORT_CMD);
		short_input.size = cpu_to_le16(msg_len);
		short_input.req_addr = cpu_to_le64(ctx->dma_handle);

		data = (u32 *)&short_input;
		msg_len = sizeof(short_input);

		max_req_len = BNXT_HWRM_SHORT_REQ_LEN;
	}

	/* Ensure any associated DMA buffers are written before doorbell */
	wmb();

	/* Write request msg to hwrm channel */
	__iowrite32_copy(bp->bar0 + bar_offset, data, msg_len / 4);

	for (i = msg_len; i < max_req_len; i += 4)
		writel(0, bp->bar0 + bar_offset + i);

	/* Ring channel doorbell */
	writel(1, bp->bar0 + doorbell_offset);

	hwrm_req_dbg(bp, ctx->req);

	if (!pci_is_enabled(bp->pdev)) {
		rc = -ENODEV;
		goto exit;
	}

	/* Limit timeout to an upper limit */
	timeout = min(ctx->timeout, bp->hwrm_cmd_max_timeout ?: HWRM_CMD_MAX_TIMEOUT);
	/* convert timeout to usec */
	timeout *= 1000;

	i = 0;
	/* Short timeout for the first few iterations:
	 * number of loops = number of loops for short timeout +
	 * number of loops for standard timeout.
	 */
	tmo_count = HWRM_SHORT_TIMEOUT_COUNTER;
	timeout = timeout - HWRM_SHORT_MIN_TIMEOUT * HWRM_SHORT_TIMEOUT_COUNTER;
	tmo_count += DIV_ROUND_UP(timeout, HWRM_MIN_TIMEOUT);

	if (le16_to_cpu(ctx->req->cmpl_ring) != INVALID_HW_RING_ID) {
		/* Wait until hwrm response cmpl interrupt is processed */
		while (READ_ONCE(token->state) < BNXT_HWRM_COMPLETE &&
		       i++ < tmo_count) {
			/* Abort the wait for completion if the FW health
			 * check has failed.
			 */
			if (test_bit(BNXT_STATE_FW_FATAL_COND, &bp->state))
				goto exit;
			/* on first few passes, just barely sleep */
			if (i < HWRM_SHORT_TIMEOUT_COUNTER) {
				usleep_range(HWRM_SHORT_MIN_TIMEOUT,
					     HWRM_SHORT_MAX_TIMEOUT);
			} else {
				if (hwrm_wait_must_abort(bp, req_type, &sts)) {
					hwrm_err(bp, ctx, "Resp cmpl intr abandoning msg: 0x%x due to firmware status: 0x%x\n",
						 req_type, sts);
					goto exit;
				}
				usleep_range(HWRM_MIN_TIMEOUT,
					     HWRM_MAX_TIMEOUT);
			}
		}

		if (READ_ONCE(token->state) != BNXT_HWRM_COMPLETE) {
			hwrm_err(bp, ctx, "Resp cmpl intr err msg: 0x%x\n",
				 req_type);
			goto exit;
		}
		len = le16_to_cpu(READ_ONCE(ctx->resp->resp_len));
		valid = ((u8 *)ctx->resp) + len - 1;
	} else {
		__le16 seen_out_of_seq = ctx->req->seq_id; /* will never see */
		int j;

		/* Check if response len is updated */
		for (i = 0; i < tmo_count; i++) {
			/* Abort the wait for completion if the FW health
			 * check has failed.
			 */
			if (test_bit(BNXT_STATE_FW_FATAL_COND, &bp->state))
				goto exit;

			if (token &&
			    READ_ONCE(token->state) == BNXT_HWRM_DEFERRED) {
				__hwrm_release_token(bp, token);
				token = NULL;
			}

			len = le16_to_cpu(READ_ONCE(ctx->resp->resp_len));
			if (len) {
				__le16 resp_seq = READ_ONCE(ctx->resp->seq_id);

				if (resp_seq == ctx->req->seq_id)
					break;
				if (resp_seq != seen_out_of_seq) {
					netdev_warn(bp->dev, "Discarding out of seq response: 0x%x for msg {0x%x 0x%x}\n",
						    le16_to_cpu(resp_seq),
						    req_type,
						    le16_to_cpu(ctx->req->seq_id));
					seen_out_of_seq = resp_seq;
				}
			}

			/* on first few passes, just barely sleep */
			if (i < HWRM_SHORT_TIMEOUT_COUNTER) {
				usleep_range(HWRM_SHORT_MIN_TIMEOUT,
					     HWRM_SHORT_MAX_TIMEOUT);
			} else {
				if (hwrm_wait_must_abort(bp, req_type, &sts)) {
					hwrm_err(bp, ctx, "Abandoning msg {0x%x 0x%x} len: %d due to firmware status: 0x%x\n",
						 req_type,
						 le16_to_cpu(ctx->req->seq_id),
						 len, sts);
					goto exit;
				}
				usleep_range(HWRM_MIN_TIMEOUT,
					     HWRM_MAX_TIMEOUT);
			}
		}

		if (i >= tmo_count) {
			hwrm_err(bp, ctx, "Error (timeout: %u) msg {0x%x 0x%x} len:%d\n",
				 hwrm_total_timeout(i), req_type,
				 le16_to_cpu(ctx->req->seq_id), len);
			goto exit;
		}

		/* Last byte of resp contains valid bit */
		valid = ((u8 *)ctx->resp) + len - 1;
		for (j = 0; j < HWRM_VALID_BIT_DELAY_USEC; ) {
			/* make sure we read from updated DMA memory */
			dma_rmb();
			if (*valid)
				break;
			if (j < 10) {
				udelay(1);
				j++;
			} else {
				usleep_range(20, 30);
				j += 20;
			}
		}

		if (j >= HWRM_VALID_BIT_DELAY_USEC) {
			hwrm_err(bp, ctx, "Error (timeout: %u) msg {0x%x 0x%x} len:%d v:%d\n",
				 hwrm_total_timeout(i) + j, req_type,
				 le16_to_cpu(ctx->req->seq_id), len, *valid);
			goto exit;
		}
	}

	/* Zero valid bit for compatibility.  Valid bit in an older spec
	 * may become a new field in a newer spec.  We must make sure that
	 * a new field not implemented by old spec will read zero.
	 */
	*valid = 0;
	rc = le16_to_cpu(ctx->resp->error_code);
	if (rc == HWRM_ERR_CODE_BUSY && !(ctx->flags & BNXT_HWRM_CTX_SILENT))
		netdev_warn(bp->dev, "FW returned busy, hwrm req_type 0x%x\n",
			    req_type);
	else if (rc && rc != HWRM_ERR_CODE_PF_UNAVAILABLE)
		hwrm_err(bp, ctx, "hwrm req_type 0x%x seq id 0x%x error 0x%x\n",
			 req_type, token->seq_id, rc);
	rc = __hwrm_to_stderr(rc);
exit:
	if (token)
		__hwrm_release_token(bp, token);
	if (ctx->flags & BNXT_HWRM_INTERNAL_CTX_OWNED)
		ctx->flags |= BNXT_HWRM_INTERNAL_RESP_DIRTY;
	else
		__hwrm_ctx_drop(bp, ctx);
	return rc;
}

/**
 * hwrm_req_send() - Execute an HWRM command.
 * @bp: The driver context.
 * @req: A pointer to the request to send. The DMA resources associated with
 *	the request will be released (ie. the request will be consumed) unless
 *	ownership of the request has been assumed by the caller via a call to
 *	hwrm_req_hold().
 *
 * Send an HWRM request to the device and wait for a response. The request is
 * consumed if it is not owned by the caller. This function will block until
 * the request has either completed or times out due to an error.
 *
 * Return: A result code.
 *
 * The result is zero on success, otherwise the negative error code indicates
 * one of the following errors:
 *	E2BIG: The request was too large.
 *	EBUSY: The firmware is in a fatal state or the request timed out
 *	EACCESS: HWRM access denied.
 *	ENOSPC: HWRM resource allocation error.
 *	EINVAL: Request parameters are invalid.
 *	ENOMEM: HWRM has no buffers.
 *	EAGAIN: HWRM busy or reset in progress.
 *	EOPNOTSUPP: Invalid request type.
 *	EIO: Any other error.
 * Error handling is orthogonal to request ownership. An unowned request will
 * still be consumed on error. If the caller owns the request, then the caller
 * is responsible for releasing the resources. Otherwise, hwrm_req_send() will
 * always consume the request.
 */
int hwrm_req_send(struct bnxt *bp, void *req)
{
	struct bnxt_hwrm_ctx *ctx = __hwrm_ctx(bp, req);

	if (!ctx)
		return -EINVAL;

	return __hwrm_send(bp, ctx);
}

/**
 * hwrm_req_send_silent() - A silent version of hwrm_req_send().
 * @bp: The driver context.
 * @req: The request to send without logging.
 *
 * The same as hwrm_req_send(), except that the request is silenced using
 * hwrm_req_silence() prior the call. This version of the function is
 * provided solely to preserve the legacy API’s flavor for this functionality.
 *
 * Return: A result code, see hwrm_req_send().
 */
int hwrm_req_send_silent(struct bnxt *bp, void *req)
{
	hwrm_req_flags(bp, req, BNXT_HWRM_CTX_SILENT);
	return hwrm_req_send(bp, req);
}

/**
 * hwrm_req_dma_slice() - Allocate a slice of DMA mapped memory.
 * @bp: The driver context.
 * @req: The request for which indirect data will be associated.
 * @size: The size of the allocation.
 * @dma_handle: The bus address associated with the allocation. The HWRM API has
 *	no knowledge about the type of the request and so cannot infer how the
 *	caller intends to use the indirect data. Thus, the caller is
 *	responsible for configuring the request object appropriately to
 *	point to the associated indirect memory. Note, DMA handle has the
 *	same definition as it does in dma_alloc_coherent(), the caller is
 *	responsible for endian conversions via cpu_to_le64() before assigning
 *	this address.
 *
 * Allocates DMA mapped memory for indirect data related to a request. The
 * lifetime of the DMA resources will be bound to that of the request (ie.
 * they will be automatically released when the request is either consumed by
 * hwrm_req_send() or dropped by hwrm_req_drop()). Small allocations are
 * efficiently suballocated out of the request buffer space, hence the name
 * slice, while larger requests are satisfied via an underlying call to
 * dma_alloc_coherent(). Multiple suballocations are supported, however, only
 * one externally mapped region is.
 *
 * Return: The kernel virtual address of the DMA mapping.
 */
void *
hwrm_req_dma_slice(struct bnxt *bp, void *req, u32 size, dma_addr_t *dma_handle)
{
	struct bnxt_hwrm_ctx *ctx = __hwrm_ctx(bp, req);
	u8 *end = ((u8 *)req) + BNXT_HWRM_DMA_SIZE;
	struct input *input = req;
	u8 *addr, *req_addr = req;
	u32 max_offset, offset;

	if (!ctx)
		return NULL;

	max_offset = BNXT_HWRM_DMA_SIZE - ctx->allocated;
	offset = max_offset - size;
	offset = ALIGN_DOWN(offset, BNXT_HWRM_DMA_ALIGN);
	addr = req_addr + offset;

	if (addr < req_addr + max_offset && req_addr + ctx->req_len <= addr) {
		ctx->allocated = end - addr;
		*dma_handle = ctx->dma_handle + offset;
		return addr;
	}

	/* could not suballocate from ctx buffer, try create a new mapping */
	if (ctx->slice_addr) {
		/* if one exists, can only be due to software bug, be loud */
		netdev_err(bp->dev, "HWRM refusing to reallocate DMA slice, req_type = %u\n",
			   (u32)le16_to_cpu(input->req_type));
		dump_stack();
		return NULL;
	}

	addr = dma_alloc_coherent(&bp->pdev->dev, size, dma_handle, ctx->gfp);

	if (!addr)
		return NULL;

	ctx->slice_addr = addr;
	ctx->slice_size = size;
	ctx->slice_handle = *dma_handle;

	return addr;
}
