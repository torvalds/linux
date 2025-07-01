// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Broadcom.

#include <asm/byteorder.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/pci.h>

#include "bnge.h"
#include "bnge_hwrm.h"

static u64 bnge_cal_sentinel(struct bnge_hwrm_ctx *ctx, u16 req_type)
{
	return (((uintptr_t)ctx) + req_type) ^ BNGE_HWRM_SENTINEL;
}

int bnge_hwrm_req_create(struct bnge_dev *bd, void **req, u16 req_type,
			 u32 req_len)
{
	struct bnge_hwrm_ctx *ctx;
	dma_addr_t dma_handle;
	u8 *req_addr;

	if (req_len > BNGE_HWRM_CTX_OFFSET)
		return -E2BIG;

	req_addr = dma_pool_alloc(bd->hwrm_dma_pool, GFP_KERNEL | __GFP_ZERO,
				  &dma_handle);
	if (!req_addr)
		return -ENOMEM;

	ctx = (struct bnge_hwrm_ctx *)(req_addr + BNGE_HWRM_CTX_OFFSET);
	/* safety first, sentinel used to check for invalid requests */
	ctx->sentinel = bnge_cal_sentinel(ctx, req_type);
	ctx->req_len = req_len;
	ctx->req = (struct input *)req_addr;
	ctx->resp = (struct output *)(req_addr + BNGE_HWRM_RESP_OFFSET);
	ctx->dma_handle = dma_handle;
	ctx->flags = 0; /* __GFP_ZERO, but be explicit regarding ownership */
	ctx->timeout = bd->hwrm_cmd_timeout ?: BNGE_DFLT_HWRM_CMD_TIMEOUT;
	ctx->allocated = BNGE_HWRM_DMA_SIZE - BNGE_HWRM_CTX_OFFSET;
	ctx->gfp = GFP_KERNEL;
	ctx->slice_addr = NULL;

	/* initialize common request fields */
	ctx->req->req_type = cpu_to_le16(req_type);
	ctx->req->resp_addr = cpu_to_le64(dma_handle + BNGE_HWRM_RESP_OFFSET);
	ctx->req->cmpl_ring = cpu_to_le16(BNGE_HWRM_NO_CMPL_RING);
	ctx->req->target_id = cpu_to_le16(BNGE_HWRM_TARGET);
	*req = ctx->req;

	return 0;
}

static struct bnge_hwrm_ctx *__hwrm_ctx_get(struct bnge_dev *bd, u8 *req_addr)
{
	void *ctx_addr = req_addr + BNGE_HWRM_CTX_OFFSET;
	struct input *req = (struct input *)req_addr;
	struct bnge_hwrm_ctx *ctx = ctx_addr;
	u64 sentinel;

	if (!req) {
		dev_err(bd->dev, "null HWRM request");
		dump_stack();
		return NULL;
	}

	/* HWRM API has no type safety, verify sentinel to validate address */
	sentinel = bnge_cal_sentinel(ctx, le16_to_cpu(req->req_type));
	if (ctx->sentinel != sentinel) {
		dev_err(bd->dev, "HWRM sentinel mismatch, req_type = %u\n",
			(u32)le16_to_cpu(req->req_type));
		dump_stack();
		return NULL;
	}

	return ctx;
}

void bnge_hwrm_req_timeout(struct bnge_dev *bd,
			   void *req, unsigned int timeout)
{
	struct bnge_hwrm_ctx *ctx = __hwrm_ctx_get(bd, req);

	if (ctx)
		ctx->timeout = timeout;
}

void bnge_hwrm_req_alloc_flags(struct bnge_dev *bd, void *req, gfp_t gfp)
{
	struct bnge_hwrm_ctx *ctx = __hwrm_ctx_get(bd, req);

	if (ctx)
		ctx->gfp = gfp;
}

void bnge_hwrm_req_flags(struct bnge_dev *bd, void *req,
			 enum bnge_hwrm_ctx_flags flags)
{
	struct bnge_hwrm_ctx *ctx = __hwrm_ctx_get(bd, req);

	if (ctx)
		ctx->flags |= (flags & BNGE_HWRM_API_FLAGS);
}

void *bnge_hwrm_req_hold(struct bnge_dev *bd, void *req)
{
	struct bnge_hwrm_ctx *ctx = __hwrm_ctx_get(bd, req);
	struct input *input = (struct input *)req;

	if (!ctx)
		return NULL;

	if (ctx->flags & BNGE_HWRM_INTERNAL_CTX_OWNED) {
		dev_err(bd->dev, "HWRM context already owned, req_type = %u\n",
			(u32)le16_to_cpu(input->req_type));
		dump_stack();
		return NULL;
	}

	ctx->flags |= BNGE_HWRM_INTERNAL_CTX_OWNED;
	return ((u8 *)req) + BNGE_HWRM_RESP_OFFSET;
}

static void __hwrm_ctx_invalidate(struct bnge_dev *bd,
				  struct bnge_hwrm_ctx *ctx)
{
	void *addr = ((u8 *)ctx) - BNGE_HWRM_CTX_OFFSET;
	dma_addr_t dma_handle = ctx->dma_handle; /* save before invalidate */

	/* unmap any auxiliary DMA slice */
	if (ctx->slice_addr)
		dma_free_coherent(bd->dev, ctx->slice_size,
				  ctx->slice_addr, ctx->slice_handle);

	/* invalidate, ensure ownership, sentinel and dma_handle are cleared */
	memset(ctx, 0, sizeof(struct bnge_hwrm_ctx));

	/* return the buffer to the DMA pool */
	if (dma_handle)
		dma_pool_free(bd->hwrm_dma_pool, addr, dma_handle);
}

void bnge_hwrm_req_drop(struct bnge_dev *bd, void *req)
{
	struct bnge_hwrm_ctx *ctx = __hwrm_ctx_get(bd, req);

	if (ctx)
		__hwrm_ctx_invalidate(bd, ctx);
}

static int bnge_map_hwrm_error(u32 hwrm_err)
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

static struct bnge_hwrm_wait_token *
bnge_hwrm_create_token(struct bnge_dev *bd, enum bnge_hwrm_chnl dst)
{
	struct bnge_hwrm_wait_token *token;

	token = kzalloc(sizeof(*token), GFP_KERNEL);
	if (!token)
		return NULL;

	mutex_lock(&bd->hwrm_cmd_lock);

	token->dst = dst;
	token->state = BNGE_HWRM_PENDING;
	if (dst == BNGE_HWRM_CHNL_CHIMP) {
		token->seq_id = bd->hwrm_cmd_seq++;
		hlist_add_head_rcu(&token->node, &bd->hwrm_pending_list);
	} else {
		token->seq_id = bd->hwrm_cmd_kong_seq++;
	}

	return token;
}

static void
bnge_hwrm_destroy_token(struct bnge_dev *bd, struct bnge_hwrm_wait_token *token)
{
	if (token->dst == BNGE_HWRM_CHNL_CHIMP) {
		hlist_del_rcu(&token->node);
		kfree_rcu(token, rcu);
	} else {
		kfree(token);
	}
	mutex_unlock(&bd->hwrm_cmd_lock);
}

static void bnge_hwrm_req_dbg(struct bnge_dev *bd, struct input *req)
{
	u32 ring = le16_to_cpu(req->cmpl_ring);
	u32 type = le16_to_cpu(req->req_type);
	u32 tgt = le16_to_cpu(req->target_id);
	u32 seq = le16_to_cpu(req->seq_id);
	char opt[32] = "\n";

	if (unlikely(ring != (u16)BNGE_HWRM_NO_CMPL_RING))
		snprintf(opt, 16, " ring %d\n", ring);

	if (unlikely(tgt != BNGE_HWRM_TARGET))
		snprintf(opt + strlen(opt) - 1, 16, " tgt 0x%x\n", tgt);

	dev_dbg(bd->dev, "sent hwrm req_type 0x%x seq id 0x%x%s",
		type, seq, opt);
}

#define bnge_hwrm_err(bd, ctx, fmt, ...)		\
	do {							       \
		if ((ctx)->flags & BNGE_HWRM_CTX_SILENT)	       \
			dev_dbg((bd)->dev, fmt, __VA_ARGS__);       \
		else						       \
			dev_err((bd)->dev, fmt, __VA_ARGS__);       \
	} while (0)

static int __hwrm_send_ctx(struct bnge_dev *bd, struct bnge_hwrm_ctx *ctx)
{
	u32 doorbell_offset = BNGE_GRCPF_REG_CHIMP_COMM_TRIGGER;
	enum bnge_hwrm_chnl dst = BNGE_HWRM_CHNL_CHIMP;
	u32 bar_offset = BNGE_GRCPF_REG_CHIMP_COMM;
	struct bnge_hwrm_wait_token *token = NULL;
	u16 max_req_len = BNGE_HWRM_MAX_REQ_LEN;
	unsigned int i, timeout, tmo_count;
	u32 *data = (u32 *)ctx->req;
	u32 msg_len = ctx->req_len;
	int rc = -EBUSY;
	u32 req_type;
	u16 len = 0;
	u8 *valid;

	if (ctx->flags & BNGE_HWRM_INTERNAL_RESP_DIRTY)
		memset(ctx->resp, 0, PAGE_SIZE);

	req_type = le16_to_cpu(ctx->req->req_type);

	if (msg_len > BNGE_HWRM_MAX_REQ_LEN &&
	    msg_len > bd->hwrm_max_ext_req_len) {
		dev_warn(bd->dev, "oversized hwrm request, req_type 0x%x",
			 req_type);
		rc = -E2BIG;
		goto exit;
	}

	token = bnge_hwrm_create_token(bd, dst);
	if (!token) {
		rc = -ENOMEM;
		goto exit;
	}
	ctx->req->seq_id = cpu_to_le16(token->seq_id);

	/* Ensure any associated DMA buffers are written before doorbell */
	wmb();

	/* Write request msg to hwrm channel */
	__iowrite32_copy(bd->bar0 + bar_offset, data, msg_len / 4);

	for (i = msg_len; i < max_req_len; i += 4)
		writel(0, bd->bar0 + bar_offset + i);

	/* Ring channel doorbell */
	writel(1, bd->bar0 + doorbell_offset);

	bnge_hwrm_req_dbg(bd, ctx->req);

	/* Limit timeout to an upper limit */
	timeout = min(ctx->timeout,
		      bd->hwrm_cmd_max_timeout ?: BNGE_HWRM_CMD_MAX_TIMEOUT);
	/* convert timeout to usec */
	timeout *= 1000;

	i = 0;
	/* Short timeout for the first few iterations:
	 * number of loops = number of loops for short timeout +
	 * number of loops for standard timeout.
	 */
	tmo_count = BNGE_HWRM_SHORT_TIMEOUT_COUNTER;
	timeout = timeout - BNGE_HWRM_SHORT_MIN_TIMEOUT *
			BNGE_HWRM_SHORT_TIMEOUT_COUNTER;
	tmo_count += DIV_ROUND_UP(timeout, BNGE_HWRM_MIN_TIMEOUT);

	if (le16_to_cpu(ctx->req->cmpl_ring) != INVALID_HW_RING_ID) {
		/* Wait until hwrm response cmpl interrupt is processed */
		while (READ_ONCE(token->state) < BNGE_HWRM_COMPLETE &&
		       i++ < tmo_count) {
			/* on first few passes, just barely sleep */
			if (i < BNGE_HWRM_SHORT_TIMEOUT_COUNTER) {
				usleep_range(BNGE_HWRM_SHORT_MIN_TIMEOUT,
					     BNGE_HWRM_SHORT_MAX_TIMEOUT);
			} else {
				usleep_range(BNGE_HWRM_MIN_TIMEOUT,
					     BNGE_HWRM_MAX_TIMEOUT);
			}
		}

		if (READ_ONCE(token->state) != BNGE_HWRM_COMPLETE) {
			bnge_hwrm_err(bd, ctx, "No hwrm cmpl received: 0x%x\n",
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
			if (token &&
			    READ_ONCE(token->state) == BNGE_HWRM_DEFERRED) {
				bnge_hwrm_destroy_token(bd, token);
				token = NULL;
			}

			len = le16_to_cpu(READ_ONCE(ctx->resp->resp_len));
			if (len) {
				__le16 resp_seq = READ_ONCE(ctx->resp->seq_id);

				if (resp_seq == ctx->req->seq_id)
					break;
				if (resp_seq != seen_out_of_seq) {
					dev_warn(bd->dev, "Discarding out of seq response: 0x%x for msg {0x%x 0x%x}\n",
						 le16_to_cpu(resp_seq), req_type, le16_to_cpu(ctx->req->seq_id));
					seen_out_of_seq = resp_seq;
				}
			}

			/* on first few passes, just barely sleep */
			if (i < BNGE_HWRM_SHORT_TIMEOUT_COUNTER) {
				usleep_range(BNGE_HWRM_SHORT_MIN_TIMEOUT,
					     BNGE_HWRM_SHORT_MAX_TIMEOUT);
			} else {
				usleep_range(BNGE_HWRM_MIN_TIMEOUT,
					     BNGE_HWRM_MAX_TIMEOUT);
			}
		}

		if (i >= tmo_count) {
			bnge_hwrm_err(bd, ctx,
				      "Error (timeout: %u) msg {0x%x 0x%x} len:%d\n",
				      bnge_hwrm_timeout(i), req_type,
				      le16_to_cpu(ctx->req->seq_id), len);
			goto exit;
		}

		/* Last byte of resp contains valid bit */
		valid = ((u8 *)ctx->resp) + len - 1;
		for (j = 0; j < BNGE_HWRM_FIN_WAIT_USEC; ) {
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

		if (j >= BNGE_HWRM_FIN_WAIT_USEC) {
			bnge_hwrm_err(bd, ctx, "Error (timeout: %u) msg {0x%x 0x%x} len:%d v:%d\n",
				      bnge_hwrm_timeout(i) + j, req_type,
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
	if (rc == HWRM_ERR_CODE_BUSY && !(ctx->flags & BNGE_HWRM_CTX_SILENT))
		dev_warn(bd->dev, "FW returned busy, hwrm req_type 0x%x\n",
			 req_type);
	else if (rc && rc != HWRM_ERR_CODE_PF_UNAVAILABLE)
		bnge_hwrm_err(bd, ctx, "hwrm req_type 0x%x seq id 0x%x error %d\n",
			      req_type, le16_to_cpu(ctx->req->seq_id), rc);
	rc = bnge_map_hwrm_error(rc);

exit:
	if (token)
		bnge_hwrm_destroy_token(bd, token);
	if (ctx->flags & BNGE_HWRM_INTERNAL_CTX_OWNED)
		ctx->flags |= BNGE_HWRM_INTERNAL_RESP_DIRTY;
	else
		__hwrm_ctx_invalidate(bd, ctx);
	return rc;
}

int bnge_hwrm_req_send(struct bnge_dev *bd, void *req)
{
	struct bnge_hwrm_ctx *ctx = __hwrm_ctx_get(bd, req);

	if (!ctx)
		return -EINVAL;

	return __hwrm_send_ctx(bd, ctx);
}

int bnge_hwrm_req_send_silent(struct bnge_dev *bd, void *req)
{
	bnge_hwrm_req_flags(bd, req, BNGE_HWRM_CTX_SILENT);
	return bnge_hwrm_req_send(bd, req);
}

void *
bnge_hwrm_req_dma_slice(struct bnge_dev *bd, void *req, u32 size,
			dma_addr_t *dma_handle)
{
	struct bnge_hwrm_ctx *ctx = __hwrm_ctx_get(bd, req);
	u8 *end = ((u8 *)req) + BNGE_HWRM_DMA_SIZE;
	struct input *input = req;
	u8 *addr, *req_addr = req;
	u32 max_offset, offset;

	if (!ctx)
		return NULL;

	max_offset = BNGE_HWRM_DMA_SIZE - ctx->allocated;
	offset = max_offset - size;
	offset = ALIGN_DOWN(offset, BNGE_HWRM_DMA_ALIGN);
	addr = req_addr + offset;

	if (addr < req_addr + max_offset && req_addr + ctx->req_len <= addr) {
		ctx->allocated = end - addr;
		*dma_handle = ctx->dma_handle + offset;
		return addr;
	}

	if (ctx->slice_addr) {
		dev_err(bd->dev, "HWRM refusing to reallocate DMA slice, req_type = %u\n",
			(u32)le16_to_cpu(input->req_type));
		dump_stack();
		return NULL;
	}

	addr = dma_alloc_coherent(bd->dev, size, dma_handle, ctx->gfp);
	if (!addr)
		return NULL;

	ctx->slice_addr = addr;
	ctx->slice_size = size;
	ctx->slice_handle = *dma_handle;

	return addr;
}

void bnge_cleanup_hwrm_resources(struct bnge_dev *bd)
{
	struct bnge_hwrm_wait_token *token;

	dma_pool_destroy(bd->hwrm_dma_pool);
	bd->hwrm_dma_pool = NULL;

	rcu_read_lock();
	hlist_for_each_entry_rcu(token, &bd->hwrm_pending_list, node)
		WRITE_ONCE(token->state, BNGE_HWRM_CANCELLED);
	rcu_read_unlock();
}

int bnge_init_hwrm_resources(struct bnge_dev *bd)
{
	bd->hwrm_dma_pool = dma_pool_create("bnge_hwrm", bd->dev,
					    BNGE_HWRM_DMA_SIZE,
					    BNGE_HWRM_DMA_ALIGN, 0);
	if (!bd->hwrm_dma_pool)
		return -ENOMEM;

	INIT_HLIST_HEAD(&bd->hwrm_pending_list);
	mutex_init(&bd->hwrm_cmd_lock);

	return 0;
}
