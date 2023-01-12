// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <linux/bitfield.h>
#include <linux/circ_buf.h>
#include <linux/delay.h>

#include <drm/drm_managed.h>

#include "xe_bo.h"
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_guc.h"
#include "xe_guc_ct.h"
#include "xe_gt_pagefault.h"
#include "xe_guc_submit.h"
#include "xe_map.h"
#include "xe_trace.h"

/* Used when a CT send wants to block and / or receive data */
struct g2h_fence {
	wait_queue_head_t wq;
	u32 *response_buffer;
	u32 seqno;
	u16 response_len;
	u16 error;
	u16 hint;
	u16 reason;
	bool retry;
	bool fail;
	bool done;
};

static void g2h_fence_init(struct g2h_fence *g2h_fence, u32 *response_buffer)
{
	g2h_fence->response_buffer = response_buffer;
	g2h_fence->response_len = 0;
	g2h_fence->fail = false;
	g2h_fence->retry = false;
	g2h_fence->done = false;
	g2h_fence->seqno = ~0x0;
}

static bool g2h_fence_needs_alloc(struct g2h_fence *g2h_fence)
{
	return g2h_fence->seqno == ~0x0;
}

static struct xe_guc *
ct_to_guc(struct xe_guc_ct *ct)
{
	return container_of(ct, struct xe_guc, ct);
}

static struct xe_gt *
ct_to_gt(struct xe_guc_ct *ct)
{
	return container_of(ct, struct xe_gt, uc.guc.ct);
}

static struct xe_device *
ct_to_xe(struct xe_guc_ct *ct)
{
	return gt_to_xe(ct_to_gt(ct));
}

/**
 * DOC: GuC CTB Blob
 *
 * We allocate single blob to hold both CTB descriptors and buffers:
 *
 *      +--------+-----------------------------------------------+------+
 *      | offset | contents                                      | size |
 *      +========+===============================================+======+
 *      | 0x0000 | H2G CTB Descriptor (send)                     |      |
 *      +--------+-----------------------------------------------+  4K  |
 *      | 0x0800 | G2H CTB Descriptor (g2h)                      |      |
 *      +--------+-----------------------------------------------+------+
 *      | 0x1000 | H2G CT Buffer (send)                          | n*4K |
 *      |        |                                               |      |
 *      +--------+-----------------------------------------------+------+
 *      | 0x1000 | G2H CT Buffer (g2h)                           | m*4K |
 *      | + n*4K |                                               |      |
 *      +--------+-----------------------------------------------+------+
 *
 * Size of each ``CT Buffer`` must be multiple of 4K.
 * We don't expect too many messages in flight at any time, unless we are
 * using the GuC submission. In that case each request requires a minimum
 * 2 dwords which gives us a maximum 256 queue'd requests. Hopefully this
 * enough space to avoid backpressure on the driver. We increase the size
 * of the receive buffer (relative to the send) to ensure a G2H response
 * CTB has a landing spot.
 */

#define CTB_DESC_SIZE		ALIGN(sizeof(struct guc_ct_buffer_desc), SZ_2K)
#define CTB_H2G_BUFFER_SIZE	(SZ_4K)
#define CTB_G2H_BUFFER_SIZE	(4 * CTB_H2G_BUFFER_SIZE)
#define G2H_ROOM_BUFFER_SIZE	(CTB_G2H_BUFFER_SIZE / 4)

static size_t guc_ct_size(void)
{
	return 2 * CTB_DESC_SIZE + CTB_H2G_BUFFER_SIZE +
		CTB_G2H_BUFFER_SIZE;
}

static void guc_ct_fini(struct drm_device *drm, void *arg)
{
	struct xe_guc_ct *ct = arg;

	xa_destroy(&ct->fence_lookup);
	xe_bo_unpin_map_no_vm(ct->bo);
}

static void g2h_worker_func(struct work_struct *w);

static void primelockdep(struct xe_guc_ct *ct)
{
	if (!IS_ENABLED(CONFIG_LOCKDEP))
		return;

	fs_reclaim_acquire(GFP_KERNEL);
	might_lock(&ct->lock);
	fs_reclaim_release(GFP_KERNEL);
}

int xe_guc_ct_init(struct xe_guc_ct *ct)
{
	struct xe_device *xe = ct_to_xe(ct);
	struct xe_gt *gt = ct_to_gt(ct);
	struct xe_bo *bo;
	int err;

	XE_BUG_ON(guc_ct_size() % PAGE_SIZE);

	mutex_init(&ct->lock);
	spin_lock_init(&ct->fast_lock);
	xa_init(&ct->fence_lookup);
	ct->fence_context = dma_fence_context_alloc(1);
	INIT_WORK(&ct->g2h_worker, g2h_worker_func);
	init_waitqueue_head(&ct->wq);

	primelockdep(ct);

	bo = xe_bo_create_pin_map(xe, gt, NULL, guc_ct_size(),
				  ttm_bo_type_kernel,
				  XE_BO_CREATE_VRAM_IF_DGFX(gt) |
				  XE_BO_CREATE_GGTT_BIT);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	ct->bo = bo;

	err = drmm_add_action_or_reset(&xe->drm, guc_ct_fini, ct);
	if (err)
		return err;

	return 0;
}

#define desc_read(xe_, guc_ctb__, field_)			\
	xe_map_rd_field(xe_, &guc_ctb__->desc, 0,		\
			struct guc_ct_buffer_desc, field_)

#define desc_write(xe_, guc_ctb__, field_, val_)		\
	xe_map_wr_field(xe_, &guc_ctb__->desc, 0,		\
			struct guc_ct_buffer_desc, field_, val_)

static void guc_ct_ctb_h2g_init(struct xe_device *xe, struct guc_ctb *h2g,
				struct iosys_map *map)
{
	h2g->size = CTB_H2G_BUFFER_SIZE / sizeof(u32);
	h2g->resv_space = 0;
	h2g->tail = 0;
	h2g->head = 0;
	h2g->space = CIRC_SPACE(h2g->tail, h2g->head, h2g->size) -
		h2g->resv_space;
	h2g->broken = false;

	h2g->desc = *map;
	xe_map_memset(xe, &h2g->desc, 0, 0, sizeof(struct guc_ct_buffer_desc));

	h2g->cmds = IOSYS_MAP_INIT_OFFSET(map, CTB_DESC_SIZE * 2);
}

static void guc_ct_ctb_g2h_init(struct xe_device *xe, struct guc_ctb *g2h,
				struct iosys_map *map)
{
	g2h->size = CTB_G2H_BUFFER_SIZE / sizeof(u32);
	g2h->resv_space = G2H_ROOM_BUFFER_SIZE / sizeof(u32);
	g2h->head = 0;
	g2h->tail = 0;
	g2h->space = CIRC_SPACE(g2h->tail, g2h->head, g2h->size) -
		g2h->resv_space;
	g2h->broken = false;

	g2h->desc = IOSYS_MAP_INIT_OFFSET(map, CTB_DESC_SIZE);
	xe_map_memset(xe, &g2h->desc, 0, 0, sizeof(struct guc_ct_buffer_desc));

	g2h->cmds = IOSYS_MAP_INIT_OFFSET(map, CTB_DESC_SIZE * 2 +
					    CTB_H2G_BUFFER_SIZE);
}

static int guc_ct_ctb_h2g_register(struct xe_guc_ct *ct)
{
	struct xe_guc *guc = ct_to_guc(ct);
	u32 desc_addr, ctb_addr, size;
	int err;

	desc_addr = xe_bo_ggtt_addr(ct->bo);
	ctb_addr = xe_bo_ggtt_addr(ct->bo) + CTB_DESC_SIZE * 2;
	size = ct->ctbs.h2g.size * sizeof(u32);

	err = xe_guc_self_cfg64(guc,
				GUC_KLV_SELF_CFG_H2G_CTB_DESCRIPTOR_ADDR_KEY,
				desc_addr);
	if (err)
		return err;

	err = xe_guc_self_cfg64(guc,
				GUC_KLV_SELF_CFG_H2G_CTB_ADDR_KEY,
				ctb_addr);
	if (err)
		return err;

	return xe_guc_self_cfg32(guc,
				 GUC_KLV_SELF_CFG_H2G_CTB_SIZE_KEY,
				 size);
}

static int guc_ct_ctb_g2h_register(struct xe_guc_ct *ct)
{
	struct xe_guc *guc = ct_to_guc(ct);
	u32 desc_addr, ctb_addr, size;
	int err;

	desc_addr = xe_bo_ggtt_addr(ct->bo) + CTB_DESC_SIZE;
	ctb_addr = xe_bo_ggtt_addr(ct->bo) + CTB_DESC_SIZE * 2 +
		CTB_H2G_BUFFER_SIZE;
	size = ct->ctbs.g2h.size * sizeof(u32);

	err = xe_guc_self_cfg64(guc,
				GUC_KLV_SELF_CFG_G2H_CTB_DESCRIPTOR_ADDR_KEY,
				desc_addr);
	if (err)
		return err;

	err = xe_guc_self_cfg64(guc,
				GUC_KLV_SELF_CFG_G2H_CTB_ADDR_KEY,
				ctb_addr);
	if (err)
		return err;

	return xe_guc_self_cfg32(guc,
				 GUC_KLV_SELF_CFG_G2H_CTB_SIZE_KEY,
				 size);
}

static int guc_ct_control_toggle(struct xe_guc_ct *ct, bool enable)
{
	u32 request[HOST2GUC_CONTROL_CTB_REQUEST_MSG_LEN] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION,
			   GUC_ACTION_HOST2GUC_CONTROL_CTB),
		FIELD_PREP(HOST2GUC_CONTROL_CTB_REQUEST_MSG_1_CONTROL,
			   enable ? GUC_CTB_CONTROL_ENABLE :
			   GUC_CTB_CONTROL_DISABLE),
	};
	int ret = xe_guc_mmio_send(ct_to_guc(ct), request, ARRAY_SIZE(request));

	return ret > 0 ? -EPROTO : ret;
}

int xe_guc_ct_enable(struct xe_guc_ct *ct)
{
	struct xe_device *xe = ct_to_xe(ct);
	int err;

	XE_BUG_ON(ct->enabled);

	guc_ct_ctb_h2g_init(xe, &ct->ctbs.h2g, &ct->bo->vmap);
	guc_ct_ctb_g2h_init(xe, &ct->ctbs.g2h, &ct->bo->vmap);

	err = guc_ct_ctb_h2g_register(ct);
	if (err)
		goto err_out;

	err = guc_ct_ctb_g2h_register(ct);
	if (err)
		goto err_out;

	err = guc_ct_control_toggle(ct, true);
	if (err)
		goto err_out;

	mutex_lock(&ct->lock);
	ct->g2h_outstanding = 0;
	ct->enabled = true;
	mutex_unlock(&ct->lock);

	smp_mb();
	wake_up_all(&ct->wq);
	drm_dbg(&xe->drm, "GuC CT communication channel enabled\n");

	return 0;

err_out:
	drm_err(&xe->drm, "Failed to enabled CT (%d)\n", err);

	return err;
}

void xe_guc_ct_disable(struct xe_guc_ct *ct)
{
	mutex_lock(&ct->lock);
	ct->enabled = false;
	mutex_unlock(&ct->lock);

	xa_destroy(&ct->fence_lookup);
}

static bool h2g_has_room(struct xe_guc_ct *ct, u32 cmd_len)
{
	struct guc_ctb *h2g = &ct->ctbs.h2g;

	lockdep_assert_held(&ct->lock);

	if (cmd_len > h2g->space) {
		h2g->head = desc_read(ct_to_xe(ct), h2g, head);
		h2g->space = CIRC_SPACE(h2g->tail, h2g->head, h2g->size) -
			h2g->resv_space;
		if (cmd_len > h2g->space)
			return false;
	}

	return true;
}

static bool g2h_has_room(struct xe_guc_ct *ct, u32 g2h_len)
{
	lockdep_assert_held(&ct->lock);

	return ct->ctbs.g2h.space > g2h_len;
}

static int has_room(struct xe_guc_ct *ct, u32 cmd_len, u32 g2h_len)
{
	lockdep_assert_held(&ct->lock);

	if (!g2h_has_room(ct, g2h_len) || !h2g_has_room(ct, cmd_len))
		return -EBUSY;

	return 0;
}

static void h2g_reserve_space(struct xe_guc_ct *ct, u32 cmd_len)
{
	lockdep_assert_held(&ct->lock);
	ct->ctbs.h2g.space -= cmd_len;
}

static void g2h_reserve_space(struct xe_guc_ct *ct, u32 g2h_len, u32 num_g2h)
{
	XE_BUG_ON(g2h_len > ct->ctbs.g2h.space);

	if (g2h_len) {
		spin_lock_irq(&ct->fast_lock);
		ct->ctbs.g2h.space -= g2h_len;
		ct->g2h_outstanding += num_g2h;
		spin_unlock_irq(&ct->fast_lock);
	}
}

static void __g2h_release_space(struct xe_guc_ct *ct, u32 g2h_len)
{
	lockdep_assert_held(&ct->fast_lock);
	XE_WARN_ON(ct->ctbs.g2h.space + g2h_len >
		   ct->ctbs.g2h.size - ct->ctbs.g2h.resv_space);

	ct->ctbs.g2h.space += g2h_len;
	--ct->g2h_outstanding;
}

static void g2h_release_space(struct xe_guc_ct *ct, u32 g2h_len)
{
	spin_lock_irq(&ct->fast_lock);
	__g2h_release_space(ct, g2h_len);
	spin_unlock_irq(&ct->fast_lock);
}

static int h2g_write(struct xe_guc_ct *ct, const u32 *action, u32 len,
		     u32 ct_fence_value, bool want_response)
{
	struct xe_device *xe = ct_to_xe(ct);
	struct guc_ctb *h2g = &ct->ctbs.h2g;
	u32 cmd[GUC_CTB_MSG_MAX_LEN / sizeof(u32)];
	u32 cmd_len = len + GUC_CTB_HDR_LEN;
	u32 cmd_idx = 0, i;
	u32 tail = h2g->tail;
	struct iosys_map map = IOSYS_MAP_INIT_OFFSET(&h2g->cmds,
							 tail * sizeof(u32));

	lockdep_assert_held(&ct->lock);
	XE_BUG_ON(len * sizeof(u32) > GUC_CTB_MSG_MAX_LEN);
	XE_BUG_ON(tail > h2g->size);

	/* Command will wrap, zero fill (NOPs), return and check credits again */
	if (tail + cmd_len > h2g->size) {
		xe_map_memset(xe, &map, 0, 0, (h2g->size - tail) * sizeof(u32));
		h2g_reserve_space(ct, (h2g->size - tail));
		h2g->tail = 0;
		desc_write(xe, h2g, tail, h2g->tail);

		return -EAGAIN;
	}

	/*
	 * dw0: CT header (including fence)
	 * dw1: HXG header (including action code)
	 * dw2+: action data
	 */
	cmd[cmd_idx++] = FIELD_PREP(GUC_CTB_MSG_0_FORMAT, GUC_CTB_FORMAT_HXG) |
		FIELD_PREP(GUC_CTB_MSG_0_NUM_DWORDS, len) |
		FIELD_PREP(GUC_CTB_MSG_0_FENCE, ct_fence_value);
	if (want_response) {
		cmd[cmd_idx++] =
			FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
			FIELD_PREP(GUC_HXG_EVENT_MSG_0_ACTION |
				   GUC_HXG_EVENT_MSG_0_DATA0, action[0]);
	} else {
		cmd[cmd_idx++] =
			FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_EVENT) |
			FIELD_PREP(GUC_HXG_EVENT_MSG_0_ACTION |
				   GUC_HXG_EVENT_MSG_0_DATA0, action[0]);
	}
	for (i = 1; i < len; ++i)
		cmd[cmd_idx++] = action[i];

	/* Write H2G ensuring visable before descriptor update */
	xe_map_memcpy_to(xe, &map, 0, cmd, cmd_len * sizeof(u32));
	xe_device_wmb(ct_to_xe(ct));

	/* Update local copies */
	h2g->tail = (tail + cmd_len) % h2g->size;
	h2g_reserve_space(ct, cmd_len);

	/* Update descriptor */
	desc_write(xe, h2g, tail, h2g->tail);

	return 0;
}

static int __guc_ct_send_locked(struct xe_guc_ct *ct, const u32 *action,
				u32 len, u32 g2h_len, u32 num_g2h,
				struct g2h_fence *g2h_fence)
{
	int ret;

	XE_BUG_ON(g2h_len && g2h_fence);
	XE_BUG_ON(num_g2h && g2h_fence);
	XE_BUG_ON(g2h_len && !num_g2h);
	XE_BUG_ON(!g2h_len && num_g2h);
	lockdep_assert_held(&ct->lock);

	if (unlikely(ct->ctbs.h2g.broken)) {
		ret = -EPIPE;
		goto out;
	}

	if (unlikely(!ct->enabled)) {
		ret = -ENODEV;
		goto out;
	}

	if (g2h_fence) {
		g2h_len = GUC_CTB_HXG_MSG_MAX_LEN;
		num_g2h = 1;

		if (g2h_fence_needs_alloc(g2h_fence)) {
			void *ptr;

			g2h_fence->seqno = (ct->fence_seqno++ & 0xffff);
			init_waitqueue_head(&g2h_fence->wq);
			ptr = xa_store(&ct->fence_lookup,
				       g2h_fence->seqno,
				       g2h_fence, GFP_ATOMIC);
			if (IS_ERR(ptr)) {
				ret = PTR_ERR(ptr);
				goto out;
			}
		}
	}

	xe_device_mem_access_get(ct_to_xe(ct));
retry:
	ret = has_room(ct, len + GUC_CTB_HDR_LEN, g2h_len);
	if (unlikely(ret))
		goto put_wa;

	ret = h2g_write(ct, action, len, g2h_fence ? g2h_fence->seqno : 0,
			!!g2h_fence);
	if (unlikely(ret)) {
		if (ret == -EAGAIN)
			goto retry;
		goto put_wa;
	}

	g2h_reserve_space(ct, g2h_len, num_g2h);
	xe_guc_notify(ct_to_guc(ct));
put_wa:
	xe_device_mem_access_put(ct_to_xe(ct));
out:

	return ret;
}

static void kick_reset(struct xe_guc_ct *ct)
{
	xe_gt_reset_async(ct_to_gt(ct));
}

static int dequeue_one_g2h(struct xe_guc_ct *ct);

static int guc_ct_send_locked(struct xe_guc_ct *ct, const u32 *action, u32 len,
			      u32 g2h_len, u32 num_g2h,
			      struct g2h_fence *g2h_fence)
{
	struct drm_device *drm = &ct_to_xe(ct)->drm;
	struct drm_printer p = drm_info_printer(drm->dev);
	unsigned int sleep_period_ms = 1;
	int ret;

	XE_BUG_ON(g2h_len && g2h_fence);
	lockdep_assert_held(&ct->lock);

try_again:
	ret = __guc_ct_send_locked(ct, action, len, g2h_len, num_g2h,
				   g2h_fence);

	/*
	 * We wait to try to restore credits for about 1 second before bailing.
	 * In the case of H2G credits we have no choice but just to wait for the
	 * GuC to consume H2Gs in the channel so we use a wait / sleep loop. In
	 * the case of G2H we process any G2H in the channel, hopefully freeing
	 * credits as we consume the G2H messages.
	 */
	if (unlikely(ret == -EBUSY &&
		     !h2g_has_room(ct, len + GUC_CTB_HDR_LEN))) {
		struct guc_ctb *h2g = &ct->ctbs.h2g;

		if (sleep_period_ms == 1024)
			goto broken;

		trace_xe_guc_ct_h2g_flow_control(h2g->head, h2g->tail,
						 h2g->size, h2g->space,
						 len + GUC_CTB_HDR_LEN);
		msleep(sleep_period_ms);
		sleep_period_ms <<= 1;

		goto try_again;
	} else if (unlikely(ret == -EBUSY)) {
		struct xe_device *xe = ct_to_xe(ct);
		struct guc_ctb *g2h = &ct->ctbs.g2h;

		trace_xe_guc_ct_g2h_flow_control(g2h->head,
						 desc_read(xe, g2h, tail),
						 g2h->size, g2h->space,
						 g2h_fence ?
						 GUC_CTB_HXG_MSG_MAX_LEN :
						 g2h_len);

#define g2h_avail(ct)	\
	(desc_read(ct_to_xe(ct), (&ct->ctbs.g2h), tail) != ct->ctbs.g2h.head)
		if (!wait_event_timeout(ct->wq, !ct->g2h_outstanding ||
					g2h_avail(ct), HZ))
			goto broken;
#undef g2h_avail

		if (dequeue_one_g2h(ct) < 0)
			goto broken;

		goto try_again;
	}

	return ret;

broken:
	drm_err(drm, "No forward process on H2G, reset required");
	xe_guc_ct_print(ct, &p);
	ct->ctbs.h2g.broken = true;

	return -EDEADLK;
}

static int guc_ct_send(struct xe_guc_ct *ct, const u32 *action, u32 len,
		       u32 g2h_len, u32 num_g2h, struct g2h_fence *g2h_fence)
{
	int ret;

	XE_BUG_ON(g2h_len && g2h_fence);

	mutex_lock(&ct->lock);
	ret = guc_ct_send_locked(ct, action, len, g2h_len, num_g2h, g2h_fence);
	mutex_unlock(&ct->lock);

	return ret;
}

int xe_guc_ct_send(struct xe_guc_ct *ct, const u32 *action, u32 len,
		   u32 g2h_len, u32 num_g2h)
{
	int ret;

	ret = guc_ct_send(ct, action, len, g2h_len, num_g2h, NULL);
	if (ret == -EDEADLK)
		kick_reset(ct);

	return ret;
}

int xe_guc_ct_send_locked(struct xe_guc_ct *ct, const u32 *action, u32 len,
			  u32 g2h_len, u32 num_g2h)
{
	int ret;

	ret = guc_ct_send_locked(ct, action, len, g2h_len, num_g2h, NULL);
	if (ret == -EDEADLK)
		kick_reset(ct);

	return ret;
}

int xe_guc_ct_send_g2h_handler(struct xe_guc_ct *ct, const u32 *action, u32 len)
{
	int ret;

	lockdep_assert_held(&ct->lock);

	ret = guc_ct_send_locked(ct, action, len, 0, 0, NULL);
	if (ret == -EDEADLK)
		kick_reset(ct);

	return ret;
}

/*
 * Check if a GT reset is in progress or will occur and if GT reset brought the
 * CT back up. Randomly picking 5 seconds for an upper limit to do a GT a reset.
 */
static bool retry_failure(struct xe_guc_ct *ct, int ret)
{
	if (!(ret == -EDEADLK || ret == -EPIPE || ret == -ENODEV))
		return false;

#define ct_alive(ct)	\
	(ct->enabled && !ct->ctbs.h2g.broken && !ct->ctbs.g2h.broken)
	if (!wait_event_interruptible_timeout(ct->wq, ct_alive(ct),  HZ * 5))
		return false;
#undef ct_alive

	return true;
}

static int guc_ct_send_recv(struct xe_guc_ct *ct, const u32 *action, u32 len,
			    u32 *response_buffer, bool no_fail)
{
	struct xe_device *xe = ct_to_xe(ct);
	struct g2h_fence g2h_fence;
	int ret = 0;

	/*
	 * We use a fence to implement blocking sends / receiving response data.
	 * The seqno of the fence is sent in the H2G, returned in the G2H, and
	 * an xarray is used as storage media with the seqno being to key.
	 * Fields in the fence hold success, failure, retry status and the
	 * response data. Safe to allocate on the stack as the xarray is the
	 * only reference and it cannot be present after this function exits.
	 */
retry:
	g2h_fence_init(&g2h_fence, response_buffer);
retry_same_fence:
	ret = guc_ct_send(ct, action, len, 0, 0, &g2h_fence);
	if (unlikely(ret == -ENOMEM)) {
		void *ptr;

		/* Retry allocation /w GFP_KERNEL */
		ptr = xa_store(&ct->fence_lookup,
			       g2h_fence.seqno,
			       &g2h_fence, GFP_KERNEL);
		if (IS_ERR(ptr)) {
			return PTR_ERR(ptr);
		}

		goto retry_same_fence;
	} else if (unlikely(ret)) {
		if (ret == -EDEADLK)
			kick_reset(ct);

		if (no_fail && retry_failure(ct, ret))
			goto retry_same_fence;

		if (!g2h_fence_needs_alloc(&g2h_fence))
			xa_erase_irq(&ct->fence_lookup, g2h_fence.seqno);

		return ret;
	}

	ret = wait_event_timeout(g2h_fence.wq, g2h_fence.done, HZ);
	if (!ret) {
		drm_err(&xe->drm, "Timed out wait for G2H, fence %u, action %04x",
			g2h_fence.seqno, action[0]);
		xa_erase_irq(&ct->fence_lookup, g2h_fence.seqno);
		return -ETIME;
	}

	if (g2h_fence.retry) {
		drm_warn(&xe->drm, "Send retry, action 0x%04x, reason %d",
			 action[0], g2h_fence.reason);
		goto retry;
	}
	if (g2h_fence.fail) {
		drm_err(&xe->drm, "Send failed, action 0x%04x, error %d, hint %d",
			action[0], g2h_fence.error, g2h_fence.hint);
		ret = -EIO;
	}

	return ret > 0 ? 0 : ret;
}

int xe_guc_ct_send_recv(struct xe_guc_ct *ct, const u32 *action, u32 len,
			u32 *response_buffer)
{
	return guc_ct_send_recv(ct, action, len, response_buffer, false);
}

int xe_guc_ct_send_recv_no_fail(struct xe_guc_ct *ct, const u32 *action,
				u32 len, u32 *response_buffer)
{
	return guc_ct_send_recv(ct, action, len, response_buffer, true);
}

static int parse_g2h_event(struct xe_guc_ct *ct, u32 *msg, u32 len)
{
	u32 action = FIELD_GET(GUC_HXG_EVENT_MSG_0_ACTION, msg[1]);

	lockdep_assert_held(&ct->lock);

	switch (action) {
	case XE_GUC_ACTION_SCHED_CONTEXT_MODE_DONE:
	case XE_GUC_ACTION_DEREGISTER_CONTEXT_DONE:
	case XE_GUC_ACTION_SCHED_ENGINE_MODE_DONE:
	case XE_GUC_ACTION_TLB_INVALIDATION_DONE:
		g2h_release_space(ct, len);
	}

	return 0;
}

static int parse_g2h_response(struct xe_guc_ct *ct, u32 *msg, u32 len)
{
	struct xe_device *xe = ct_to_xe(ct);
	u32 response_len = len - GUC_CTB_MSG_MIN_LEN;
	u32 fence = FIELD_GET(GUC_CTB_MSG_0_FENCE, msg[0]);
	u32 type = FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[1]);
	struct g2h_fence *g2h_fence;

	lockdep_assert_held(&ct->lock);

	g2h_fence = xa_erase(&ct->fence_lookup, fence);
	if (unlikely(!g2h_fence)) {
		/* Don't tear down channel, as send could've timed out */
		drm_warn(&xe->drm, "G2H fence (%u) not found!\n", fence);
		g2h_release_space(ct, GUC_CTB_HXG_MSG_MAX_LEN);
		return 0;
	}

	XE_WARN_ON(fence != g2h_fence->seqno);

	if (type == GUC_HXG_TYPE_RESPONSE_FAILURE) {
		g2h_fence->fail = true;
		g2h_fence->error =
			FIELD_GET(GUC_HXG_FAILURE_MSG_0_ERROR, msg[0]);
		g2h_fence->hint =
			FIELD_GET(GUC_HXG_FAILURE_MSG_0_HINT, msg[0]);
	} else if (type == GUC_HXG_TYPE_NO_RESPONSE_RETRY) {
		g2h_fence->retry = true;
		g2h_fence->reason =
			FIELD_GET(GUC_HXG_RETRY_MSG_0_REASON, msg[0]);
	} else if (g2h_fence->response_buffer) {
		g2h_fence->response_len = response_len;
		memcpy(g2h_fence->response_buffer, msg + GUC_CTB_MSG_MIN_LEN,
		       response_len * sizeof(u32));
	}

	g2h_release_space(ct, GUC_CTB_HXG_MSG_MAX_LEN);

	g2h_fence->done = true;
	smp_mb();

	wake_up(&g2h_fence->wq);

	return 0;
}

static int parse_g2h_msg(struct xe_guc_ct *ct, u32 *msg, u32 len)
{
	struct xe_device *xe = ct_to_xe(ct);
	u32 header, hxg, origin, type;
	int ret;

	lockdep_assert_held(&ct->lock);

	header = msg[0];
	hxg = msg[1];

	origin = FIELD_GET(GUC_HXG_MSG_0_ORIGIN, hxg);
	if (unlikely(origin != GUC_HXG_ORIGIN_GUC)) {
		drm_err(&xe->drm,
			"G2H channel broken on read, origin=%d, reset required\n",
			origin);
		ct->ctbs.g2h.broken = true;

		return -EPROTO;
	}

	type = FIELD_GET(GUC_HXG_MSG_0_TYPE, hxg);
	switch (type) {
	case GUC_HXG_TYPE_EVENT:
		ret = parse_g2h_event(ct, msg, len);
		break;
	case GUC_HXG_TYPE_RESPONSE_SUCCESS:
	case GUC_HXG_TYPE_RESPONSE_FAILURE:
	case GUC_HXG_TYPE_NO_RESPONSE_RETRY:
		ret = parse_g2h_response(ct, msg, len);
		break;
	default:
		drm_err(&xe->drm,
			"G2H channel broken on read, type=%d, reset required\n",
			type);
		ct->ctbs.g2h.broken = true;

		ret = -EOPNOTSUPP;
	}

	return ret;
}

static int process_g2h_msg(struct xe_guc_ct *ct, u32 *msg, u32 len)
{
	struct xe_device *xe = ct_to_xe(ct);
	struct xe_guc *guc = ct_to_guc(ct);
	u32 action = FIELD_GET(GUC_HXG_EVENT_MSG_0_ACTION, msg[1]);
	u32 *payload = msg + GUC_CTB_HXG_MSG_MIN_LEN;
	u32 adj_len = len - GUC_CTB_HXG_MSG_MIN_LEN;
	int ret = 0;

	if (FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[1]) != GUC_HXG_TYPE_EVENT)
		return 0;

	switch (action) {
	case XE_GUC_ACTION_SCHED_CONTEXT_MODE_DONE:
		ret = xe_guc_sched_done_handler(guc, payload, adj_len);
		break;
	case XE_GUC_ACTION_DEREGISTER_CONTEXT_DONE:
		ret = xe_guc_deregister_done_handler(guc, payload, adj_len);
		break;
	case XE_GUC_ACTION_CONTEXT_RESET_NOTIFICATION:
		ret = xe_guc_engine_reset_handler(guc, payload, adj_len);
		break;
	case XE_GUC_ACTION_ENGINE_FAILURE_NOTIFICATION:
		ret = xe_guc_engine_reset_failure_handler(guc, payload,
							  adj_len);
		break;
	case XE_GUC_ACTION_SCHED_ENGINE_MODE_DONE:
		/* Selftest only at the moment */
		break;
	case XE_GUC_ACTION_STATE_CAPTURE_NOTIFICATION:
	case XE_GUC_ACTION_NOTIFY_FLUSH_LOG_BUFFER_TO_FILE:
		/* FIXME: Handle this */
		break;
	case XE_GUC_ACTION_NOTIFY_MEMORY_CAT_ERROR:
		ret = xe_guc_engine_memory_cat_error_handler(guc, payload,
							     adj_len);
		break;
	case XE_GUC_ACTION_REPORT_PAGE_FAULT_REQ_DESC:
		ret = xe_guc_pagefault_handler(guc, payload, adj_len);
		break;
	case XE_GUC_ACTION_TLB_INVALIDATION_DONE:
		ret = xe_guc_tlb_invalidation_done_handler(guc, payload,
							   adj_len);
		break;
	case XE_GUC_ACTION_ACCESS_COUNTER_NOTIFY:
		ret = xe_guc_access_counter_notify_handler(guc, payload,
							   adj_len);
		break;
	default:
		drm_err(&xe->drm, "unexpected action 0x%04x\n", action);
	}

	if (ret)
		drm_err(&xe->drm, "action 0x%04x failed processing, ret=%d\n",
			action, ret);

	return 0;
}

static int g2h_read(struct xe_guc_ct *ct, u32 *msg, bool fast_path)
{
	struct xe_device *xe = ct_to_xe(ct);
	struct guc_ctb *g2h = &ct->ctbs.g2h;
	u32 tail, head, len;
	s32 avail;

	lockdep_assert_held(&ct->fast_lock);

	if (!ct->enabled)
		return -ENODEV;

	if (g2h->broken)
		return -EPIPE;

	/* Calculate DW available to read */
	tail = desc_read(xe, g2h, tail);
	avail = tail - g2h->head;
	if (unlikely(avail == 0))
		return 0;

	if (avail < 0)
		avail += g2h->size;

	/* Read header */
	xe_map_memcpy_from(xe, msg, &g2h->cmds, sizeof(u32) * g2h->head, sizeof(u32));
	len = FIELD_GET(GUC_CTB_MSG_0_NUM_DWORDS, msg[0]) + GUC_CTB_MSG_MIN_LEN;
	if (len > avail) {
		drm_err(&xe->drm,
			"G2H channel broken on read, avail=%d, len=%d, reset required\n",
			avail, len);
		g2h->broken = true;

		return -EPROTO;
	}

	head = (g2h->head + 1) % g2h->size;
	avail = len - 1;

	/* Read G2H message */
	if (avail + head > g2h->size) {
		u32 avail_til_wrap = g2h->size - head;

		xe_map_memcpy_from(xe, msg + 1,
				   &g2h->cmds, sizeof(u32) * head,
				   avail_til_wrap * sizeof(u32));
		xe_map_memcpy_from(xe, msg + 1 + avail_til_wrap,
				   &g2h->cmds, 0,
				   (avail - avail_til_wrap) * sizeof(u32));
	} else {
		xe_map_memcpy_from(xe, msg + 1,
				   &g2h->cmds, sizeof(u32) * head,
				   avail * sizeof(u32));
	}

	if (fast_path) {
		if (FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[1]) != GUC_HXG_TYPE_EVENT)
			return 0;

		switch (FIELD_GET(GUC_HXG_EVENT_MSG_0_ACTION, msg[1])) {
		case XE_GUC_ACTION_TLB_INVALIDATION_DONE:
		case XE_GUC_ACTION_REPORT_PAGE_FAULT_REQ_DESC:
			break;	/* Process these in fast-path */
		default:
			return 0;
		}
	}

	/* Update local / descriptor header */
	g2h->head = (head + avail) % g2h->size;
	desc_write(xe, g2h, head, g2h->head);

	return len;
}

static void g2h_fast_path(struct xe_guc_ct *ct, u32 *msg, u32 len)
{
	struct xe_device *xe = ct_to_xe(ct);
	struct xe_guc *guc = ct_to_guc(ct);
	u32 action = FIELD_GET(GUC_HXG_EVENT_MSG_0_ACTION, msg[1]);
	u32 *payload = msg + GUC_CTB_HXG_MSG_MIN_LEN;
	u32 adj_len = len - GUC_CTB_HXG_MSG_MIN_LEN;
	int ret = 0;

	switch (action) {
	case XE_GUC_ACTION_REPORT_PAGE_FAULT_REQ_DESC:
		ret = xe_guc_pagefault_handler(guc, payload, adj_len);
		break;
	case XE_GUC_ACTION_TLB_INVALIDATION_DONE:
		__g2h_release_space(ct, len);
		ret = xe_guc_tlb_invalidation_done_handler(guc, payload,
							   adj_len);
		break;
	default:
		XE_WARN_ON("NOT_POSSIBLE");
	}

	if (ret)
		drm_err(&xe->drm, "action 0x%04x failed processing, ret=%d\n",
			action, ret);
}

/**
 * xe_guc_ct_fast_path - process critical G2H in the IRQ handler
 * @ct: GuC CT object
 *
 * Anything related to page faults is critical for performance, process these
 * critical G2H in the IRQ. This is safe as these handlers either just wake up
 * waiters or queue another worker.
 */
void xe_guc_ct_fast_path(struct xe_guc_ct *ct)
{
	struct xe_device *xe = ct_to_xe(ct);
	int len;

	if (!xe_device_in_fault_mode(xe) || !xe_device_mem_access_ongoing(xe))
		return;

	spin_lock(&ct->fast_lock);
	do {
		len = g2h_read(ct, ct->fast_msg, true);
		if (len > 0)
			g2h_fast_path(ct, ct->fast_msg, len);
	} while (len > 0);
	spin_unlock(&ct->fast_lock);
}

/* Returns less than zero on error, 0 on done, 1 on more available */
static int dequeue_one_g2h(struct xe_guc_ct *ct)
{
	int len;
	int ret;

	lockdep_assert_held(&ct->lock);

	spin_lock_irq(&ct->fast_lock);
	len = g2h_read(ct, ct->msg, false);
	spin_unlock_irq(&ct->fast_lock);
	if (len <= 0)
		return len;

	ret = parse_g2h_msg(ct, ct->msg, len);
	if (unlikely(ret < 0))
		return ret;

	ret = process_g2h_msg(ct, ct->msg, len);
	if (unlikely(ret < 0))
		return ret;

	return 1;
}

static void g2h_worker_func(struct work_struct *w)
{
	struct xe_guc_ct *ct = container_of(w, struct xe_guc_ct, g2h_worker);
	int ret;

	xe_device_mem_access_get(ct_to_xe(ct));
	do {
		mutex_lock(&ct->lock);
		ret = dequeue_one_g2h(ct);
		mutex_unlock(&ct->lock);

		if (unlikely(ret == -EPROTO || ret == -EOPNOTSUPP)) {
			struct drm_device *drm = &ct_to_xe(ct)->drm;
			struct drm_printer p = drm_info_printer(drm->dev);

			xe_guc_ct_print(ct, &p);
			kick_reset(ct);
		}
	} while (ret == 1);
	xe_device_mem_access_put(ct_to_xe(ct));
}

static void guc_ct_ctb_print(struct xe_device *xe, struct guc_ctb *ctb,
			     struct drm_printer *p)
{
	u32 head, tail;

	drm_printf(p, "\tsize: %d\n", ctb->size);
	drm_printf(p, "\tresv_space: %d\n", ctb->resv_space);
	drm_printf(p, "\thead: %d\n", ctb->head);
	drm_printf(p, "\ttail: %d\n", ctb->tail);
	drm_printf(p, "\tspace: %d\n", ctb->space);
	drm_printf(p, "\tbroken: %d\n", ctb->broken);

	head = desc_read(xe, ctb, head);
	tail = desc_read(xe, ctb, tail);
	drm_printf(p, "\thead (memory): %d\n", head);
	drm_printf(p, "\ttail (memory): %d\n", tail);
	drm_printf(p, "\tstatus (memory): 0x%x\n", desc_read(xe, ctb, status));

	if (head != tail) {
		struct iosys_map map =
			IOSYS_MAP_INIT_OFFSET(&ctb->cmds, head * sizeof(u32));

		while (head != tail) {
			drm_printf(p, "\tcmd[%d]: 0x%08x\n", head,
				   xe_map_rd(xe, &map, 0, u32));
			++head;
			if (head == ctb->size) {
				head = 0;
				map = ctb->cmds;
			} else {
				iosys_map_incr(&map, sizeof(u32));
			}
		}
	}
}

void xe_guc_ct_print(struct xe_guc_ct *ct, struct drm_printer *p)
{
	if (ct->enabled) {
		drm_puts(p, "\nH2G CTB (all sizes in DW):\n");
		guc_ct_ctb_print(ct_to_xe(ct), &ct->ctbs.h2g, p);

		drm_puts(p, "\nG2H CTB (all sizes in DW):\n");
		guc_ct_ctb_print(ct_to_xe(ct), &ct->ctbs.g2h, p);
		drm_printf(p, "\tg2h outstanding: %d\n", ct->g2h_outstanding);
	} else {
		drm_puts(p, "\nCT disabled\n");
	}
}

#ifdef XE_GUC_CT_SELFTEST
/*
 * Disable G2H processing in IRQ handler to force xe_guc_ct_send to enter flow
 * control if enough sent, 8k sends is enough. Verify forward process, verify
 * credits expected values on exit.
 */
void xe_guc_ct_selftest(struct xe_guc_ct *ct, struct drm_printer *p)
{
	struct guc_ctb *g2h = &ct->ctbs.g2h;
	u32 action[] = { XE_GUC_ACTION_SCHED_ENGINE_MODE_SET, 0, 0, 1, };
	u32 bad_action[] = { XE_GUC_ACTION_SCHED_CONTEXT_MODE_SET, 0, 0, };
	int ret;
	int i;

	ct->suppress_irq_handler = true;
	drm_puts(p, "Starting GuC CT selftest\n");

	for (i = 0; i < 8192; ++i) {
		ret = xe_guc_ct_send(ct, action, ARRAY_SIZE(action), 4, 1);
		if (ret) {
			drm_printf(p, "Aborted pass %d, ret %d\n", i, ret);
			xe_guc_ct_print(ct, p);
			break;
		}
	}

	ct->suppress_irq_handler = false;
	if (!ret) {
		xe_guc_ct_irq_handler(ct);
		msleep(200);
		if (g2h->space !=
		    CIRC_SPACE(0, 0, g2h->size) - g2h->resv_space) {
			drm_printf(p, "Mismatch on space %d, %d\n",
				   g2h->space,
				   CIRC_SPACE(0, 0, g2h->size) -
				   g2h->resv_space);
			ret = -EIO;
		}
		if (ct->g2h_outstanding) {
			drm_printf(p, "Outstanding G2H, %d\n",
				   ct->g2h_outstanding);
			ret = -EIO;
		}
	}

	/* Check failure path for blocking CTs too */
	xe_guc_ct_send_block(ct, bad_action, ARRAY_SIZE(bad_action));
	if (g2h->space !=
	    CIRC_SPACE(0, 0, g2h->size) - g2h->resv_space) {
		drm_printf(p, "Mismatch on space %d, %d\n",
			   g2h->space,
			   CIRC_SPACE(0, 0, g2h->size) -
			   g2h->resv_space);
		ret = -EIO;
	}
	if (ct->g2h_outstanding) {
		drm_printf(p, "Outstanding G2H, %d\n",
			   ct->g2h_outstanding);
		ret = -EIO;
	}

	drm_printf(p, "GuC CT selftest done - %s\n", ret ? "FAIL" : "PASS");
}
#endif
