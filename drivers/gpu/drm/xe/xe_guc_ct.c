// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_guc_ct.h"

#include <linux/bitfield.h>
#include <linux/circ_buf.h>
#include <linux/delay.h>
#include <linux/fault-inject.h>

#include <kunit/static_stub.h>

#include <drm/drm_managed.h>

#include "abi/guc_actions_abi.h"
#include "abi/guc_actions_sriov_abi.h"
#include "abi/guc_klvs_abi.h"
#include "xe_bo.h"
#include "xe_devcoredump.h"
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_gt_pagefault.h"
#include "xe_gt_printk.h"
#include "xe_gt_sriov_pf_control.h"
#include "xe_gt_sriov_pf_monitor.h"
#include "xe_gt_sriov_printk.h"
#include "xe_guc.h"
#include "xe_guc_log.h"
#include "xe_guc_relay.h"
#include "xe_guc_submit.h"
#include "xe_guc_tlb_inval.h"
#include "xe_map.h"
#include "xe_pm.h"
#include "xe_trace_guc.h"

static void receive_g2h(struct xe_guc_ct *ct);
static void g2h_worker_func(struct work_struct *w);
static void safe_mode_worker_func(struct work_struct *w);
static void ct_exit_safe_mode(struct xe_guc_ct *ct);
static void guc_ct_change_state(struct xe_guc_ct *ct,
				enum xe_guc_ct_state state);

#if IS_ENABLED(CONFIG_DRM_XE_DEBUG)
enum {
	/* Internal states, not error conditions */
	CT_DEAD_STATE_REARM,			/* 0x0001 */
	CT_DEAD_STATE_CAPTURE,			/* 0x0002 */

	/* Error conditions */
	CT_DEAD_SETUP,				/* 0x0004 */
	CT_DEAD_H2G_WRITE,			/* 0x0008 */
	CT_DEAD_H2G_HAS_ROOM,			/* 0x0010 */
	CT_DEAD_G2H_READ,			/* 0x0020 */
	CT_DEAD_G2H_RECV,			/* 0x0040 */
	CT_DEAD_G2H_RELEASE,			/* 0x0080 */
	CT_DEAD_DEADLOCK,			/* 0x0100 */
	CT_DEAD_PROCESS_FAILED,			/* 0x0200 */
	CT_DEAD_FAST_G2H,			/* 0x0400 */
	CT_DEAD_PARSE_G2H_RESPONSE,		/* 0x0800 */
	CT_DEAD_PARSE_G2H_UNKNOWN,		/* 0x1000 */
	CT_DEAD_PARSE_G2H_ORIGIN,		/* 0x2000 */
	CT_DEAD_PARSE_G2H_TYPE,			/* 0x4000 */
	CT_DEAD_CRASH,				/* 0x8000 */
};

static void ct_dead_worker_func(struct work_struct *w);
static void ct_dead_capture(struct xe_guc_ct *ct, struct guc_ctb *ctb, u32 reason_code);

#define CT_DEAD(ct, ctb, reason_code)		ct_dead_capture((ct), (ctb), CT_DEAD_##reason_code)
#else
#define CT_DEAD(ct, ctb, reason)			\
	do {						\
		struct guc_ctb *_ctb = (ctb);		\
		if (_ctb)				\
			_ctb->info.broken = true;	\
	} while (0)
#endif

/* Used when a CT send wants to block and / or receive data */
struct g2h_fence {
	u32 *response_buffer;
	u32 seqno;
	u32 response_data;
	u16 response_len;
	u16 error;
	u16 hint;
	u16 reason;
	bool cancel;
	bool retry;
	bool fail;
	bool done;
};

#define make_u64(hi, lo) ((u64)((u64)(u32)(hi) << 32 | (u32)(lo)))

static void g2h_fence_init(struct g2h_fence *g2h_fence, u32 *response_buffer)
{
	memset(g2h_fence, 0, sizeof(*g2h_fence));
	g2h_fence->response_buffer = response_buffer;
	g2h_fence->seqno = ~0x0;
}

static void g2h_fence_cancel(struct g2h_fence *g2h_fence)
{
	g2h_fence->cancel = true;
	g2h_fence->fail = true;
	g2h_fence->done = true;
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
 *
 * In addition to submissions, the G2H buffer needs to be able to hold
 * enough space for recoverable page fault notifications. The number of
 * page faults is interrupt driven and can be as much as the number of
 * compute resources available. However, most of the actual work for these
 * is in a separate page fault worker thread. Therefore we only need to
 * make sure the queue has enough space to handle all of the submissions
 * and responses and an extra buffer for incoming page faults.
 */

#define CTB_DESC_SIZE		ALIGN(sizeof(struct guc_ct_buffer_desc), SZ_2K)
#define CTB_H2G_BUFFER_SIZE	(SZ_4K)
#define CTB_G2H_BUFFER_SIZE	(SZ_128K)
#define G2H_ROOM_BUFFER_SIZE	(CTB_G2H_BUFFER_SIZE / 2)

/**
 * xe_guc_ct_queue_proc_time_jiffies - Return maximum time to process a full
 * CT command queue
 * @ct: the &xe_guc_ct. Unused at this moment but will be used in the future.
 *
 * Observation is that a 4KiB buffer full of commands takes a little over a
 * second to process. Use that to calculate maximum time to process a full CT
 * command queue.
 *
 * Return: Maximum time to process a full CT queue in jiffies.
 */
long xe_guc_ct_queue_proc_time_jiffies(struct xe_guc_ct *ct)
{
	BUILD_BUG_ON(!IS_ALIGNED(CTB_H2G_BUFFER_SIZE, SZ_4));
	return (CTB_H2G_BUFFER_SIZE / SZ_4K) * HZ;
}

static size_t guc_ct_size(void)
{
	return 2 * CTB_DESC_SIZE + CTB_H2G_BUFFER_SIZE +
		CTB_G2H_BUFFER_SIZE;
}

static void guc_ct_fini(struct drm_device *drm, void *arg)
{
	struct xe_guc_ct *ct = arg;

#if IS_ENABLED(CONFIG_DRM_XE_DEBUG)
	cancel_work_sync(&ct->dead.worker);
#endif
	ct_exit_safe_mode(ct);
	destroy_workqueue(ct->g2h_wq);
	xa_destroy(&ct->fence_lookup);
}

static void primelockdep(struct xe_guc_ct *ct)
{
	if (!IS_ENABLED(CONFIG_LOCKDEP))
		return;

	fs_reclaim_acquire(GFP_KERNEL);
	might_lock(&ct->lock);
	fs_reclaim_release(GFP_KERNEL);
}

int xe_guc_ct_init_noalloc(struct xe_guc_ct *ct)
{
	struct xe_device *xe = ct_to_xe(ct);
	struct xe_gt *gt = ct_to_gt(ct);
	int err;

	xe_gt_assert(gt, !(guc_ct_size() % PAGE_SIZE));

	err = drmm_mutex_init(&xe->drm, &ct->lock);
	if (err)
		return err;

	primelockdep(ct);

	ct->g2h_wq = alloc_ordered_workqueue("xe-g2h-wq", WQ_MEM_RECLAIM);
	if (!ct->g2h_wq)
		return -ENOMEM;

	spin_lock_init(&ct->fast_lock);
	xa_init(&ct->fence_lookup);
	INIT_WORK(&ct->g2h_worker, g2h_worker_func);
	INIT_DELAYED_WORK(&ct->safe_mode_worker, safe_mode_worker_func);
#if IS_ENABLED(CONFIG_DRM_XE_DEBUG)
	spin_lock_init(&ct->dead.lock);
	INIT_WORK(&ct->dead.worker, ct_dead_worker_func);
#if IS_ENABLED(CONFIG_DRM_XE_DEBUG_GUC)
	stack_depot_init();
#endif
#endif
	init_waitqueue_head(&ct->wq);
	init_waitqueue_head(&ct->g2h_fence_wq);

	err = drmm_add_action_or_reset(&xe->drm, guc_ct_fini, ct);
	if (err)
		return err;

	xe_gt_assert(gt, ct->state == XE_GUC_CT_STATE_NOT_INITIALIZED);
	ct->state = XE_GUC_CT_STATE_DISABLED;
	return 0;
}
ALLOW_ERROR_INJECTION(xe_guc_ct_init_noalloc, ERRNO); /* See xe_pci_probe() */

static void guc_action_disable_ct(void *arg)
{
	struct xe_guc_ct *ct = arg;

	guc_ct_change_state(ct, XE_GUC_CT_STATE_DISABLED);
}

int xe_guc_ct_init(struct xe_guc_ct *ct)
{
	struct xe_device *xe = ct_to_xe(ct);
	struct xe_gt *gt = ct_to_gt(ct);
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_bo *bo;

	bo = xe_managed_bo_create_pin_map(xe, tile, guc_ct_size(),
					  XE_BO_FLAG_SYSTEM |
					  XE_BO_FLAG_GGTT |
					  XE_BO_FLAG_GGTT_INVALIDATE |
					  XE_BO_FLAG_PINNED_NORESTORE);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	ct->bo = bo;

	return devm_add_action_or_reset(xe->drm.dev, guc_action_disable_ct, ct);
}
ALLOW_ERROR_INJECTION(xe_guc_ct_init, ERRNO); /* See xe_pci_probe() */

/**
 * xe_guc_ct_init_post_hwconfig - Reinitialize the GuC CTB in VRAM
 * @ct: the &xe_guc_ct
 *
 * Allocate a new BO in VRAM and free the previous BO that was allocated
 * in system memory (SMEM). Applicable only for DGFX products.
 *
 * Return: 0 on success, or a negative errno on failure.
 */
int xe_guc_ct_init_post_hwconfig(struct xe_guc_ct *ct)
{
	struct xe_device *xe = ct_to_xe(ct);
	struct xe_gt *gt = ct_to_gt(ct);
	struct xe_tile *tile = gt_to_tile(gt);
	int ret;

	xe_assert(xe, !xe_guc_ct_enabled(ct));

	if (IS_DGFX(xe)) {
		ret = xe_managed_bo_reinit_in_vram(xe, tile, &ct->bo);
		if (ret)
			return ret;
	}

	devm_remove_action(xe->drm.dev, guc_action_disable_ct, ct);
	return devm_add_action_or_reset(xe->drm.dev, guc_action_disable_ct, ct);
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
	h2g->info.size = CTB_H2G_BUFFER_SIZE / sizeof(u32);
	h2g->info.resv_space = 0;
	h2g->info.tail = 0;
	h2g->info.head = 0;
	h2g->info.space = CIRC_SPACE(h2g->info.tail, h2g->info.head,
				     h2g->info.size) -
			  h2g->info.resv_space;
	h2g->info.broken = false;

	h2g->desc = *map;
	xe_map_memset(xe, &h2g->desc, 0, 0, sizeof(struct guc_ct_buffer_desc));

	h2g->cmds = IOSYS_MAP_INIT_OFFSET(map, CTB_DESC_SIZE * 2);
}

static void guc_ct_ctb_g2h_init(struct xe_device *xe, struct guc_ctb *g2h,
				struct iosys_map *map)
{
	g2h->info.size = CTB_G2H_BUFFER_SIZE / sizeof(u32);
	g2h->info.resv_space = G2H_ROOM_BUFFER_SIZE / sizeof(u32);
	g2h->info.head = 0;
	g2h->info.tail = 0;
	g2h->info.space = CIRC_SPACE(g2h->info.tail, g2h->info.head,
				     g2h->info.size) -
			  g2h->info.resv_space;
	g2h->info.broken = false;

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
	size = ct->ctbs.h2g.info.size * sizeof(u32);

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
	size = ct->ctbs.g2h.info.size * sizeof(u32);

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

static void guc_ct_change_state(struct xe_guc_ct *ct,
				enum xe_guc_ct_state state)
{
	struct xe_gt *gt = ct_to_gt(ct);
	struct g2h_fence *g2h_fence;
	unsigned long idx;

	mutex_lock(&ct->lock);		/* Serialise dequeue_one_g2h() */
	spin_lock_irq(&ct->fast_lock);	/* Serialise CT fast-path */

	xe_gt_assert(ct_to_gt(ct), ct->g2h_outstanding == 0 ||
		     state == XE_GUC_CT_STATE_STOPPED);

	if (ct->g2h_outstanding)
		xe_pm_runtime_put(ct_to_xe(ct));
	ct->g2h_outstanding = 0;
	ct->state = state;

	xe_gt_dbg(gt, "GuC CT communication channel %s\n",
		  state == XE_GUC_CT_STATE_STOPPED ? "stopped" :
		  str_enabled_disabled(state == XE_GUC_CT_STATE_ENABLED));

	spin_unlock_irq(&ct->fast_lock);

	/* cancel all in-flight send-recv requests */
	xa_for_each(&ct->fence_lookup, idx, g2h_fence)
		g2h_fence_cancel(g2h_fence);

	/* make sure guc_ct_send_recv() will see g2h_fence changes */
	smp_mb();
	wake_up_all(&ct->g2h_fence_wq);

	/*
	 * Lockdep doesn't like this under the fast lock and he destroy only
	 * needs to be serialized with the send path which ct lock provides.
	 */
	xa_destroy(&ct->fence_lookup);

	mutex_unlock(&ct->lock);
}

static bool ct_needs_safe_mode(struct xe_guc_ct *ct)
{
	return !pci_dev_msi_enabled(to_pci_dev(ct_to_xe(ct)->drm.dev));
}

static bool ct_restart_safe_mode_worker(struct xe_guc_ct *ct)
{
	if (!ct_needs_safe_mode(ct))
		return false;

	queue_delayed_work(ct->g2h_wq, &ct->safe_mode_worker, HZ / 10);
	return true;
}

static void safe_mode_worker_func(struct work_struct *w)
{
	struct xe_guc_ct *ct = container_of(w, struct xe_guc_ct, safe_mode_worker.work);

	receive_g2h(ct);

	if (!ct_restart_safe_mode_worker(ct))
		xe_gt_dbg(ct_to_gt(ct), "GuC CT safe-mode canceled\n");
}

static void ct_enter_safe_mode(struct xe_guc_ct *ct)
{
	if (ct_restart_safe_mode_worker(ct))
		xe_gt_dbg(ct_to_gt(ct), "GuC CT safe-mode enabled\n");
}

static void ct_exit_safe_mode(struct xe_guc_ct *ct)
{
	if (cancel_delayed_work_sync(&ct->safe_mode_worker))
		xe_gt_dbg(ct_to_gt(ct), "GuC CT safe-mode disabled\n");
}

int xe_guc_ct_enable(struct xe_guc_ct *ct)
{
	struct xe_device *xe = ct_to_xe(ct);
	struct xe_gt *gt = ct_to_gt(ct);
	int err;

	xe_gt_assert(gt, !xe_guc_ct_enabled(ct));

	xe_map_memset(xe, &ct->bo->vmap, 0, 0, xe_bo_size(ct->bo));
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

	guc_ct_change_state(ct, XE_GUC_CT_STATE_ENABLED);

	smp_mb();
	wake_up_all(&ct->wq);

	if (ct_needs_safe_mode(ct))
		ct_enter_safe_mode(ct);

#if IS_ENABLED(CONFIG_DRM_XE_DEBUG)
	/*
	 * The CT has now been reset so the dumper can be re-armed
	 * after any existing dead state has been dumped.
	 */
	spin_lock_irq(&ct->dead.lock);
	if (ct->dead.reason) {
		ct->dead.reason |= (1 << CT_DEAD_STATE_REARM);
		queue_work(system_unbound_wq, &ct->dead.worker);
	}
	spin_unlock_irq(&ct->dead.lock);
#endif

	return 0;

err_out:
	xe_gt_err(gt, "Failed to enable GuC CT (%pe)\n", ERR_PTR(err));
	CT_DEAD(ct, NULL, SETUP);

	return err;
}

static void stop_g2h_handler(struct xe_guc_ct *ct)
{
	cancel_work_sync(&ct->g2h_worker);
}

/**
 * xe_guc_ct_disable - Set GuC to disabled state
 * @ct: the &xe_guc_ct
 *
 * Set GuC CT to disabled state and stop g2h handler. No outstanding g2h expected
 * in this transition.
 */
void xe_guc_ct_disable(struct xe_guc_ct *ct)
{
	guc_ct_change_state(ct, XE_GUC_CT_STATE_DISABLED);
	ct_exit_safe_mode(ct);
	stop_g2h_handler(ct);
}

/**
 * xe_guc_ct_stop - Set GuC to stopped state
 * @ct: the &xe_guc_ct
 *
 * Set GuC CT to stopped state, stop g2h handler, and clear any outstanding g2h
 */
void xe_guc_ct_stop(struct xe_guc_ct *ct)
{
	if (!xe_guc_ct_initialized(ct))
		return;

	guc_ct_change_state(ct, XE_GUC_CT_STATE_STOPPED);
	stop_g2h_handler(ct);
}

static bool h2g_has_room(struct xe_guc_ct *ct, u32 cmd_len)
{
	struct guc_ctb *h2g = &ct->ctbs.h2g;

	lockdep_assert_held(&ct->lock);

	if (cmd_len > h2g->info.space) {
		h2g->info.head = desc_read(ct_to_xe(ct), h2g, head);

		if (h2g->info.head > h2g->info.size) {
			struct xe_device *xe = ct_to_xe(ct);
			u32 desc_status = desc_read(xe, h2g, status);

			desc_write(xe, h2g, status, desc_status | GUC_CTB_STATUS_OVERFLOW);

			xe_gt_err(ct_to_gt(ct), "CT: invalid head offset %u >= %u)\n",
				  h2g->info.head, h2g->info.size);
			CT_DEAD(ct, h2g, H2G_HAS_ROOM);
			return false;
		}

		h2g->info.space = CIRC_SPACE(h2g->info.tail, h2g->info.head,
					     h2g->info.size) -
				  h2g->info.resv_space;
		if (cmd_len > h2g->info.space)
			return false;
	}

	return true;
}

static bool g2h_has_room(struct xe_guc_ct *ct, u32 g2h_len)
{
	if (!g2h_len)
		return true;

	lockdep_assert_held(&ct->fast_lock);

	return ct->ctbs.g2h.info.space > g2h_len;
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
	ct->ctbs.h2g.info.space -= cmd_len;
}

static void __g2h_reserve_space(struct xe_guc_ct *ct, u32 g2h_len, u32 num_g2h)
{
	xe_gt_assert(ct_to_gt(ct), g2h_len <= ct->ctbs.g2h.info.space);
	xe_gt_assert(ct_to_gt(ct), (!g2h_len && !num_g2h) ||
		     (g2h_len && num_g2h));

	if (g2h_len) {
		lockdep_assert_held(&ct->fast_lock);

		if (!ct->g2h_outstanding)
			xe_pm_runtime_get_noresume(ct_to_xe(ct));

		ct->ctbs.g2h.info.space -= g2h_len;
		ct->g2h_outstanding += num_g2h;
	}
}

static void __g2h_release_space(struct xe_guc_ct *ct, u32 g2h_len)
{
	bool bad = false;

	lockdep_assert_held(&ct->fast_lock);

	bad = ct->ctbs.g2h.info.space + g2h_len >
		     ct->ctbs.g2h.info.size - ct->ctbs.g2h.info.resv_space;
	bad |= !ct->g2h_outstanding;

	if (bad) {
		xe_gt_err(ct_to_gt(ct), "Invalid G2H release: %d + %d vs %d - %d -> %d vs %d, outstanding = %d!\n",
			  ct->ctbs.g2h.info.space, g2h_len,
			  ct->ctbs.g2h.info.size, ct->ctbs.g2h.info.resv_space,
			  ct->ctbs.g2h.info.space + g2h_len,
			  ct->ctbs.g2h.info.size - ct->ctbs.g2h.info.resv_space,
			  ct->g2h_outstanding);
		CT_DEAD(ct, &ct->ctbs.g2h, G2H_RELEASE);
		return;
	}

	ct->ctbs.g2h.info.space += g2h_len;
	if (!--ct->g2h_outstanding)
		xe_pm_runtime_put(ct_to_xe(ct));
}

static void g2h_release_space(struct xe_guc_ct *ct, u32 g2h_len)
{
	spin_lock_irq(&ct->fast_lock);
	__g2h_release_space(ct, g2h_len);
	spin_unlock_irq(&ct->fast_lock);
}

#if IS_ENABLED(CONFIG_DRM_XE_DEBUG)
static void fast_req_track(struct xe_guc_ct *ct, u16 fence, u16 action)
{
	unsigned int slot = fence % ARRAY_SIZE(ct->fast_req);
#if IS_ENABLED(CONFIG_DRM_XE_DEBUG_GUC)
	unsigned long entries[SZ_32];
	unsigned int n;

	n = stack_trace_save(entries, ARRAY_SIZE(entries), 1);

	/* May be called under spinlock, so avoid sleeping */
	ct->fast_req[slot].stack = stack_depot_save(entries, n, GFP_NOWAIT);
#endif
	ct->fast_req[slot].fence = fence;
	ct->fast_req[slot].action = action;
}
#else
static void fast_req_track(struct xe_guc_ct *ct, u16 fence, u16 action)
{
}
#endif

/*
 * The CT protocol accepts a 16 bits fence. This field is fully owned by the
 * driver, the GuC will just copy it to the reply message. Since we need to
 * be able to distinguish between replies to REQUEST and FAST_REQUEST messages,
 * we use one bit of the seqno as an indicator for that and a rolling counter
 * for the remaining 15 bits.
 */
#define CT_SEQNO_MASK GENMASK(14, 0)
#define CT_SEQNO_UNTRACKED BIT(15)
static u16 next_ct_seqno(struct xe_guc_ct *ct, bool is_g2h_fence)
{
	u32 seqno = ct->fence_seqno++ & CT_SEQNO_MASK;

	if (!is_g2h_fence)
		seqno |= CT_SEQNO_UNTRACKED;

	return seqno;
}

#define H2G_CT_HEADERS (GUC_CTB_HDR_LEN + 1) /* one DW CTB header and one DW HxG header */

static int h2g_write(struct xe_guc_ct *ct, const u32 *action, u32 len,
		     u32 ct_fence_value, bool want_response)
{
	struct xe_device *xe = ct_to_xe(ct);
	struct xe_gt *gt = ct_to_gt(ct);
	struct guc_ctb *h2g = &ct->ctbs.h2g;
	u32 cmd[H2G_CT_HEADERS];
	u32 tail = h2g->info.tail;
	u32 full_len;
	struct iosys_map map = IOSYS_MAP_INIT_OFFSET(&h2g->cmds,
							 tail * sizeof(u32));
	u32 desc_status;

	full_len = len + GUC_CTB_HDR_LEN;

	lockdep_assert_held(&ct->lock);
	xe_gt_assert(gt, full_len <= GUC_CTB_MSG_MAX_LEN);

	desc_status = desc_read(xe, h2g, status);
	if (desc_status) {
		xe_gt_err(gt, "CT write: non-zero status: %u\n", desc_status);
		goto corrupted;
	}

	if (IS_ENABLED(CONFIG_DRM_XE_DEBUG)) {
		u32 desc_tail = desc_read(xe, h2g, tail);
		u32 desc_head = desc_read(xe, h2g, head);

		if (tail != desc_tail) {
			desc_write(xe, h2g, status, desc_status | GUC_CTB_STATUS_MISMATCH);
			xe_gt_err(gt, "CT write: tail was modified %u != %u\n", desc_tail, tail);
			goto corrupted;
		}

		if (tail > h2g->info.size) {
			desc_write(xe, h2g, status, desc_status | GUC_CTB_STATUS_OVERFLOW);
			xe_gt_err(gt, "CT write: tail out of range: %u vs %u\n",
				  tail, h2g->info.size);
			goto corrupted;
		}

		if (desc_head >= h2g->info.size) {
			desc_write(xe, h2g, status, desc_status | GUC_CTB_STATUS_OVERFLOW);
			xe_gt_err(gt, "CT write: invalid head offset %u >= %u)\n",
				  desc_head, h2g->info.size);
			goto corrupted;
		}
	}

	/* Command will wrap, zero fill (NOPs), return and check credits again */
	if (tail + full_len > h2g->info.size) {
		xe_map_memset(xe, &map, 0, 0,
			      (h2g->info.size - tail) * sizeof(u32));
		h2g_reserve_space(ct, (h2g->info.size - tail));
		h2g->info.tail = 0;
		desc_write(xe, h2g, tail, h2g->info.tail);

		return -EAGAIN;
	}

	/*
	 * dw0: CT header (including fence)
	 * dw1: HXG header (including action code)
	 * dw2+: action data
	 */
	cmd[0] = FIELD_PREP(GUC_CTB_MSG_0_FORMAT, GUC_CTB_FORMAT_HXG) |
		FIELD_PREP(GUC_CTB_MSG_0_NUM_DWORDS, len) |
		FIELD_PREP(GUC_CTB_MSG_0_FENCE, ct_fence_value);
	if (want_response) {
		cmd[1] =
			FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
			FIELD_PREP(GUC_HXG_EVENT_MSG_0_ACTION |
				   GUC_HXG_EVENT_MSG_0_DATA0, action[0]);
	} else {
		fast_req_track(ct, ct_fence_value,
			       FIELD_GET(GUC_HXG_EVENT_MSG_0_ACTION, action[0]));

		cmd[1] =
			FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_FAST_REQUEST) |
			FIELD_PREP(GUC_HXG_EVENT_MSG_0_ACTION |
				   GUC_HXG_EVENT_MSG_0_DATA0, action[0]);
	}

	/* H2G header in cmd[1] replaces action[0] so: */
	--len;
	++action;

	/* Write H2G ensuring visible before descriptor update */
	xe_map_memcpy_to(xe, &map, 0, cmd, H2G_CT_HEADERS * sizeof(u32));
	xe_map_memcpy_to(xe, &map, H2G_CT_HEADERS * sizeof(u32), action, len * sizeof(u32));
	xe_device_wmb(xe);

	/* Update local copies */
	h2g->info.tail = (tail + full_len) % h2g->info.size;
	h2g_reserve_space(ct, full_len);

	/* Update descriptor */
	desc_write(xe, h2g, tail, h2g->info.tail);

	trace_xe_guc_ctb_h2g(xe, gt->info.id, *(action - 1), full_len,
			     desc_read(xe, h2g, head), h2g->info.tail);

	return 0;

corrupted:
	CT_DEAD(ct, &ct->ctbs.h2g, H2G_WRITE);
	return -EPIPE;
}

static int __guc_ct_send_locked(struct xe_guc_ct *ct, const u32 *action,
				u32 len, u32 g2h_len, u32 num_g2h,
				struct g2h_fence *g2h_fence)
{
	struct xe_gt *gt __maybe_unused = ct_to_gt(ct);
	u16 seqno;
	int ret;

	xe_gt_assert(gt, xe_guc_ct_initialized(ct));
	xe_gt_assert(gt, !g2h_len || !g2h_fence);
	xe_gt_assert(gt, !num_g2h || !g2h_fence);
	xe_gt_assert(gt, !g2h_len || num_g2h);
	xe_gt_assert(gt, g2h_len || !num_g2h);
	lockdep_assert_held(&ct->lock);

	if (unlikely(ct->ctbs.h2g.info.broken)) {
		ret = -EPIPE;
		goto out;
	}

	if (ct->state == XE_GUC_CT_STATE_DISABLED) {
		ret = -ENODEV;
		goto out;
	}

	if (ct->state == XE_GUC_CT_STATE_STOPPED) {
		ret = -ECANCELED;
		goto out;
	}

	xe_gt_assert(gt, xe_guc_ct_enabled(ct));

	if (g2h_fence) {
		g2h_len = GUC_CTB_HXG_MSG_MAX_LEN;
		num_g2h = 1;

		if (g2h_fence_needs_alloc(g2h_fence)) {
			g2h_fence->seqno = next_ct_seqno(ct, true);
			ret = xa_err(xa_store(&ct->fence_lookup,
					      g2h_fence->seqno, g2h_fence,
					      GFP_ATOMIC));
			if (ret)
				goto out;
		}

		seqno = g2h_fence->seqno;
	} else {
		seqno = next_ct_seqno(ct, false);
	}

	if (g2h_len)
		spin_lock_irq(&ct->fast_lock);
retry:
	ret = has_room(ct, len + GUC_CTB_HDR_LEN, g2h_len);
	if (unlikely(ret))
		goto out_unlock;

	ret = h2g_write(ct, action, len, seqno, !!g2h_fence);
	if (unlikely(ret)) {
		if (ret == -EAGAIN)
			goto retry;
		goto out_unlock;
	}

	__g2h_reserve_space(ct, g2h_len, num_g2h);
	xe_guc_notify(ct_to_guc(ct));
out_unlock:
	if (g2h_len)
		spin_unlock_irq(&ct->fast_lock);
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
	struct xe_device *xe = ct_to_xe(ct);
	struct xe_gt *gt = ct_to_gt(ct);
	unsigned int sleep_period_ms = 1;
	int ret;

	xe_gt_assert(gt, !g2h_len || !g2h_fence);
	lockdep_assert_held(&ct->lock);
	xe_device_assert_mem_access(ct_to_xe(ct));

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

		trace_xe_guc_ct_h2g_flow_control(xe, h2g->info.head, h2g->info.tail,
						 h2g->info.size,
						 h2g->info.space,
						 len + GUC_CTB_HDR_LEN);
		msleep(sleep_period_ms);
		sleep_period_ms <<= 1;

		goto try_again;
	} else if (unlikely(ret == -EBUSY)) {
		struct xe_device *xe = ct_to_xe(ct);
		struct guc_ctb *g2h = &ct->ctbs.g2h;

		trace_xe_guc_ct_g2h_flow_control(xe, g2h->info.head,
						 desc_read(xe, g2h, tail),
						 g2h->info.size,
						 g2h->info.space,
						 g2h_fence ?
						 GUC_CTB_HXG_MSG_MAX_LEN :
						 g2h_len);

#define g2h_avail(ct)	\
	(desc_read(ct_to_xe(ct), (&ct->ctbs.g2h), tail) != ct->ctbs.g2h.info.head)
		if (!wait_event_timeout(ct->wq, !ct->g2h_outstanding ||
					g2h_avail(ct), HZ))
			goto broken;
#undef g2h_avail

		ret = dequeue_one_g2h(ct);
		if (ret < 0) {
			if (ret != -ECANCELED)
				xe_gt_err(ct_to_gt(ct), "CTB receive failed (%pe)",
					  ERR_PTR(ret));
			goto broken;
		}

		goto try_again;
	}

	return ret;

broken:
	xe_gt_err(gt, "No forward process on H2G, reset required\n");
	CT_DEAD(ct, &ct->ctbs.h2g, DEADLOCK);

	return -EDEADLK;
}

static int guc_ct_send(struct xe_guc_ct *ct, const u32 *action, u32 len,
		       u32 g2h_len, u32 num_g2h, struct g2h_fence *g2h_fence)
{
	int ret;

	xe_gt_assert(ct_to_gt(ct), !g2h_len || !g2h_fence);

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
	(xe_guc_ct_enabled(ct) && !ct->ctbs.h2g.info.broken && \
	 !ct->ctbs.g2h.info.broken)
	if (!wait_event_interruptible_timeout(ct->wq, ct_alive(ct), HZ * 5))
		return false;
#undef ct_alive

	return true;
}

#define GUC_SEND_RETRY_LIMIT	50
#define GUC_SEND_RETRY_MSLEEP	5

static int guc_ct_send_recv(struct xe_guc_ct *ct, const u32 *action, u32 len,
			    u32 *response_buffer, bool no_fail)
{
	struct xe_gt *gt = ct_to_gt(ct);
	struct g2h_fence g2h_fence;
	unsigned int retries = 0;
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
		/* Retry allocation /w GFP_KERNEL */
		ret = xa_err(xa_store(&ct->fence_lookup, g2h_fence.seqno,
				      &g2h_fence, GFP_KERNEL));
		if (ret)
			return ret;

		goto retry_same_fence;
	} else if (unlikely(ret)) {
		if (ret == -EDEADLK)
			kick_reset(ct);

		if (no_fail && retry_failure(ct, ret))
			goto retry_same_fence;

		if (!g2h_fence_needs_alloc(&g2h_fence))
			xa_erase(&ct->fence_lookup, g2h_fence.seqno);

		return ret;
	}

	ret = wait_event_timeout(ct->g2h_fence_wq, g2h_fence.done, HZ);
	if (!ret) {
		LNL_FLUSH_WORK(&ct->g2h_worker);
		if (g2h_fence.done) {
			xe_gt_warn(gt, "G2H fence %u, action %04x, done\n",
				   g2h_fence.seqno, action[0]);
			ret = 1;
		}
	}

	/*
	 * Ensure we serialize with completion side to prevent UAF with fence going out of scope on
	 * the stack, since we have no clue if it will fire after the timeout before we can erase
	 * from the xa. Also we have some dependent loads and stores below for which we need the
	 * correct ordering, and we lack the needed barriers.
	 */
	mutex_lock(&ct->lock);
	if (!ret) {
		xe_gt_err(gt, "Timed out wait for G2H, fence %u, action %04x, done %s",
			  g2h_fence.seqno, action[0], str_yes_no(g2h_fence.done));
		xa_erase(&ct->fence_lookup, g2h_fence.seqno);
		mutex_unlock(&ct->lock);
		return -ETIME;
	}

	if (g2h_fence.retry) {
		xe_gt_dbg(gt, "H2G action %#x retrying: reason %#x\n",
			  action[0], g2h_fence.reason);
		mutex_unlock(&ct->lock);
		if (++retries > GUC_SEND_RETRY_LIMIT) {
			xe_gt_err(gt, "H2G action %#x reached retry limit=%u, aborting\n",
				  action[0], GUC_SEND_RETRY_LIMIT);
			return -ELOOP;
		}
		msleep(GUC_SEND_RETRY_MSLEEP * retries);
		goto retry;
	}
	if (g2h_fence.fail) {
		if (g2h_fence.cancel) {
			xe_gt_dbg(gt, "H2G request %#x canceled!\n", action[0]);
			ret = -ECANCELED;
			goto unlock;
		}
		xe_gt_err(gt, "H2G request %#x failed: error %#x hint %#x\n",
			  action[0], g2h_fence.error, g2h_fence.hint);
		ret = -EIO;
	}

	if (ret > 0)
		ret = response_buffer ? g2h_fence.response_len : g2h_fence.response_data;

unlock:
	mutex_unlock(&ct->lock);

	return ret;
}

/**
 * xe_guc_ct_send_recv - Send and receive HXG to the GuC
 * @ct: the &xe_guc_ct
 * @action: the dword array with `HXG Request`_ message (can't be NULL)
 * @len: length of the `HXG Request`_ message (in dwords, can't be 0)
 * @response_buffer: placeholder for the `HXG Response`_ message (can be NULL)
 *
 * Send a `HXG Request`_ message to the GuC over CT communication channel and
 * blocks until GuC replies with a `HXG Response`_ message.
 *
 * For non-blocking communication with GuC use xe_guc_ct_send().
 *
 * Note: The size of &response_buffer must be at least GUC_CTB_MAX_DWORDS_.
 *
 * Return: response length (in dwords) if &response_buffer was not NULL, or
 *         DATA0 from `HXG Response`_ if &response_buffer was NULL, or
 *         a negative error code on failure.
 */
int xe_guc_ct_send_recv(struct xe_guc_ct *ct, const u32 *action, u32 len,
			u32 *response_buffer)
{
	KUNIT_STATIC_STUB_REDIRECT(xe_guc_ct_send_recv, ct, action, len, response_buffer);
	return guc_ct_send_recv(ct, action, len, response_buffer, false);
}
ALLOW_ERROR_INJECTION(xe_guc_ct_send_recv, ERRNO);

int xe_guc_ct_send_recv_no_fail(struct xe_guc_ct *ct, const u32 *action,
				u32 len, u32 *response_buffer)
{
	return guc_ct_send_recv(ct, action, len, response_buffer, true);
}

static u32 *msg_to_hxg(u32 *msg)
{
	return msg + GUC_CTB_MSG_MIN_LEN;
}

static u32 msg_len_to_hxg_len(u32 len)
{
	return len - GUC_CTB_MSG_MIN_LEN;
}

static int parse_g2h_event(struct xe_guc_ct *ct, u32 *msg, u32 len)
{
	u32 *hxg = msg_to_hxg(msg);
	u32 action = FIELD_GET(GUC_HXG_EVENT_MSG_0_ACTION, hxg[0]);

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

static int guc_crash_process_msg(struct xe_guc_ct *ct, u32 action)
{
	struct xe_gt *gt = ct_to_gt(ct);

	if (action == XE_GUC_ACTION_NOTIFY_CRASH_DUMP_POSTED)
		xe_gt_err(gt, "GuC Crash dump notification\n");
	else if (action == XE_GUC_ACTION_NOTIFY_EXCEPTION)
		xe_gt_err(gt, "GuC Exception notification\n");
	else
		xe_gt_err(gt, "Unknown GuC crash notification: 0x%04X\n", action);

	CT_DEAD(ct, NULL, CRASH);

	kick_reset(ct);

	return 0;
}

#if IS_ENABLED(CONFIG_DRM_XE_DEBUG)
static void fast_req_report(struct xe_guc_ct *ct, u16 fence)
{
	u16 fence_min = U16_MAX, fence_max = 0;
	struct xe_gt *gt = ct_to_gt(ct);
	bool found = false;
	unsigned int n;
#if IS_ENABLED(CONFIG_DRM_XE_DEBUG_GUC)
	char *buf;
#endif

	lockdep_assert_held(&ct->lock);

	for (n = 0; n < ARRAY_SIZE(ct->fast_req); n++) {
		if (ct->fast_req[n].fence < fence_min)
			fence_min = ct->fast_req[n].fence;
		if (ct->fast_req[n].fence > fence_max)
			fence_max = ct->fast_req[n].fence;

		if (ct->fast_req[n].fence != fence)
			continue;
		found = true;

#if IS_ENABLED(CONFIG_DRM_XE_DEBUG_GUC)
		buf = kmalloc(SZ_4K, GFP_NOWAIT);
		if (buf && stack_depot_snprint(ct->fast_req[n].stack, buf, SZ_4K, 0))
			xe_gt_err(gt, "Fence 0x%x was used by action %#04x sent at:\n%s",
				  fence, ct->fast_req[n].action, buf);
		else
			xe_gt_err(gt, "Fence 0x%x was used by action %#04x [failed to retrieve stack]\n",
				  fence, ct->fast_req[n].action);
		kfree(buf);
#else
		xe_gt_err(gt, "Fence 0x%x was used by action %#04x\n",
			  fence, ct->fast_req[n].action);
#endif
		break;
	}

	if (!found)
		xe_gt_warn(gt, "Fence 0x%x not found - tracking buffer wrapped? [range = 0x%x -> 0x%x, next = 0x%X]\n",
			   fence, fence_min, fence_max, ct->fence_seqno);
}
#else
static void fast_req_report(struct xe_guc_ct *ct, u16 fence)
{
}
#endif

static int parse_g2h_response(struct xe_guc_ct *ct, u32 *msg, u32 len)
{
	struct xe_gt *gt =  ct_to_gt(ct);
	u32 *hxg = msg_to_hxg(msg);
	u32 hxg_len = msg_len_to_hxg_len(len);
	u32 fence = FIELD_GET(GUC_CTB_MSG_0_FENCE, msg[0]);
	u32 type = FIELD_GET(GUC_HXG_MSG_0_TYPE, hxg[0]);
	struct g2h_fence *g2h_fence;

	lockdep_assert_held(&ct->lock);

	/*
	 * Fences for FAST_REQUEST messages are not tracked in ct->fence_lookup.
	 * Those messages should never fail, so if we do get an error back it
	 * means we're likely doing an illegal operation and the GuC is
	 * rejecting it. We have no way to inform the code that submitted the
	 * H2G that the message was rejected, so we need to escalate the
	 * failure to trigger a reset.
	 */
	if (fence & CT_SEQNO_UNTRACKED) {
		if (type == GUC_HXG_TYPE_RESPONSE_FAILURE)
			xe_gt_err(gt, "FAST_REQ H2G fence 0x%x failed! e=0x%x, h=%u\n",
				  fence,
				  FIELD_GET(GUC_HXG_FAILURE_MSG_0_ERROR, hxg[0]),
				  FIELD_GET(GUC_HXG_FAILURE_MSG_0_HINT, hxg[0]));
		else
			xe_gt_err(gt, "unexpected response %u for FAST_REQ H2G fence 0x%x!\n",
				  type, fence);

		fast_req_report(ct, fence);

		CT_DEAD(ct, NULL, PARSE_G2H_RESPONSE);

		return -EPROTO;
	}

	g2h_fence = xa_erase(&ct->fence_lookup, fence);
	if (unlikely(!g2h_fence)) {
		/* Don't tear down channel, as send could've timed out */
		/* CT_DEAD(ct, NULL, PARSE_G2H_UNKNOWN); */
		xe_gt_warn(gt, "G2H fence (%u) not found!\n", fence);
		g2h_release_space(ct, GUC_CTB_HXG_MSG_MAX_LEN);
		return 0;
	}

	xe_gt_assert(gt, fence == g2h_fence->seqno);

	if (type == GUC_HXG_TYPE_RESPONSE_FAILURE) {
		g2h_fence->fail = true;
		g2h_fence->error = FIELD_GET(GUC_HXG_FAILURE_MSG_0_ERROR, hxg[0]);
		g2h_fence->hint = FIELD_GET(GUC_HXG_FAILURE_MSG_0_HINT, hxg[0]);
	} else if (type == GUC_HXG_TYPE_NO_RESPONSE_RETRY) {
		g2h_fence->retry = true;
		g2h_fence->reason = FIELD_GET(GUC_HXG_RETRY_MSG_0_REASON, hxg[0]);
	} else if (g2h_fence->response_buffer) {
		g2h_fence->response_len = hxg_len;
		memcpy(g2h_fence->response_buffer, hxg, hxg_len * sizeof(u32));
	} else {
		g2h_fence->response_data = FIELD_GET(GUC_HXG_RESPONSE_MSG_0_DATA0, hxg[0]);
	}

	g2h_release_space(ct, GUC_CTB_HXG_MSG_MAX_LEN);

	g2h_fence->done = true;
	smp_mb();

	wake_up_all(&ct->g2h_fence_wq);

	return 0;
}

static int parse_g2h_msg(struct xe_guc_ct *ct, u32 *msg, u32 len)
{
	struct xe_gt *gt = ct_to_gt(ct);
	u32 *hxg = msg_to_hxg(msg);
	u32 origin, type;
	int ret;

	lockdep_assert_held(&ct->lock);

	origin = FIELD_GET(GUC_HXG_MSG_0_ORIGIN, hxg[0]);
	if (unlikely(origin != GUC_HXG_ORIGIN_GUC)) {
		xe_gt_err(gt, "G2H channel broken on read, origin=%u, reset required\n",
			  origin);
		CT_DEAD(ct, &ct->ctbs.g2h, PARSE_G2H_ORIGIN);

		return -EPROTO;
	}

	type = FIELD_GET(GUC_HXG_MSG_0_TYPE, hxg[0]);
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
		xe_gt_err(gt, "G2H channel broken on read, type=%u, reset required\n",
			  type);
		CT_DEAD(ct, &ct->ctbs.g2h, PARSE_G2H_TYPE);

		ret = -EOPNOTSUPP;
	}

	return ret;
}

static int process_g2h_msg(struct xe_guc_ct *ct, u32 *msg, u32 len)
{
	struct xe_guc *guc = ct_to_guc(ct);
	struct xe_gt *gt = ct_to_gt(ct);
	u32 hxg_len = msg_len_to_hxg_len(len);
	u32 *hxg = msg_to_hxg(msg);
	u32 action, adj_len;
	u32 *payload;
	int ret = 0;

	if (FIELD_GET(GUC_HXG_MSG_0_TYPE, hxg[0]) != GUC_HXG_TYPE_EVENT)
		return 0;

	action = FIELD_GET(GUC_HXG_EVENT_MSG_0_ACTION, hxg[0]);
	payload = hxg + GUC_HXG_EVENT_MSG_MIN_LEN;
	adj_len = hxg_len - GUC_HXG_EVENT_MSG_MIN_LEN;

	switch (action) {
	case XE_GUC_ACTION_SCHED_CONTEXT_MODE_DONE:
		ret = xe_guc_sched_done_handler(guc, payload, adj_len);
		break;
	case XE_GUC_ACTION_DEREGISTER_CONTEXT_DONE:
		ret = xe_guc_deregister_done_handler(guc, payload, adj_len);
		break;
	case XE_GUC_ACTION_CONTEXT_RESET_NOTIFICATION:
		ret = xe_guc_exec_queue_reset_handler(guc, payload, adj_len);
		break;
	case XE_GUC_ACTION_ENGINE_FAILURE_NOTIFICATION:
		ret = xe_guc_exec_queue_reset_failure_handler(guc, payload,
							      adj_len);
		break;
	case XE_GUC_ACTION_SCHED_ENGINE_MODE_DONE:
		/* Selftest only at the moment */
		break;
	case XE_GUC_ACTION_STATE_CAPTURE_NOTIFICATION:
		ret = xe_guc_error_capture_handler(guc, payload, adj_len);
		break;
	case XE_GUC_ACTION_NOTIFY_FLUSH_LOG_BUFFER_TO_FILE:
		/* FIXME: Handle this */
		break;
	case XE_GUC_ACTION_NOTIFY_MEMORY_CAT_ERROR:
		ret = xe_guc_exec_queue_memory_cat_error_handler(guc, payload,
								 adj_len);
		break;
	case XE_GUC_ACTION_REPORT_PAGE_FAULT_REQ_DESC:
		ret = xe_guc_pagefault_handler(guc, payload, adj_len);
		break;
	case XE_GUC_ACTION_TLB_INVALIDATION_DONE:
		ret = xe_guc_tlb_inval_done_handler(guc, payload, adj_len);
		break;
	case XE_GUC_ACTION_ACCESS_COUNTER_NOTIFY:
		ret = xe_guc_access_counter_notify_handler(guc, payload,
							   adj_len);
		break;
	case XE_GUC_ACTION_GUC2PF_RELAY_FROM_VF:
		ret = xe_guc_relay_process_guc2pf(&guc->relay, hxg, hxg_len);
		break;
	case XE_GUC_ACTION_GUC2VF_RELAY_FROM_PF:
		ret = xe_guc_relay_process_guc2vf(&guc->relay, hxg, hxg_len);
		break;
	case GUC_ACTION_GUC2PF_VF_STATE_NOTIFY:
		ret = xe_gt_sriov_pf_control_process_guc2pf(gt, hxg, hxg_len);
		break;
	case GUC_ACTION_GUC2PF_ADVERSE_EVENT:
		ret = xe_gt_sriov_pf_monitor_process_guc2pf(gt, hxg, hxg_len);
		break;
	case XE_GUC_ACTION_NOTIFY_CRASH_DUMP_POSTED:
	case XE_GUC_ACTION_NOTIFY_EXCEPTION:
		ret = guc_crash_process_msg(ct, action);
		break;
#if IS_ENABLED(CONFIG_DRM_XE_KUNIT_TEST)
	case XE_GUC_ACTION_TEST_G2G_RECV:
		ret = xe_guc_g2g_test_notification(guc, payload, adj_len);
		break;
#endif
	default:
		xe_gt_err(gt, "unexpected G2H action 0x%04x\n", action);
	}

	if (ret) {
		xe_gt_err(gt, "G2H action %#04x failed (%pe) len %u msg %*ph\n",
			  action, ERR_PTR(ret), hxg_len, (int)sizeof(u32) * hxg_len, hxg);
		CT_DEAD(ct, NULL, PROCESS_FAILED);
	}

	return 0;
}

static int g2h_read(struct xe_guc_ct *ct, u32 *msg, bool fast_path)
{
	struct xe_device *xe = ct_to_xe(ct);
	struct xe_gt *gt = ct_to_gt(ct);
	struct guc_ctb *g2h = &ct->ctbs.g2h;
	u32 tail, head, len, desc_status;
	s32 avail;
	u32 action;
	u32 *hxg;

	xe_gt_assert(gt, xe_guc_ct_initialized(ct));
	lockdep_assert_held(&ct->fast_lock);

	if (ct->state == XE_GUC_CT_STATE_DISABLED)
		return -ENODEV;

	if (ct->state == XE_GUC_CT_STATE_STOPPED)
		return -ECANCELED;

	if (g2h->info.broken)
		return -EPIPE;

	xe_gt_assert(gt, xe_guc_ct_enabled(ct));

	desc_status = desc_read(xe, g2h, status);
	if (desc_status) {
		if (desc_status & GUC_CTB_STATUS_DISABLED) {
			/*
			 * Potentially valid if a CLIENT_RESET request resulted in
			 * contexts/engines being reset. But should never happen as
			 * no contexts should be active when CLIENT_RESET is sent.
			 */
			xe_gt_err(gt, "CT read: unexpected G2H after GuC has stopped!\n");
			desc_status &= ~GUC_CTB_STATUS_DISABLED;
		}

		if (desc_status) {
			xe_gt_err(gt, "CT read: non-zero status: %u\n", desc_status);
			goto corrupted;
		}
	}

	if (IS_ENABLED(CONFIG_DRM_XE_DEBUG)) {
		u32 desc_tail = desc_read(xe, g2h, tail);
		/*
		u32 desc_head = desc_read(xe, g2h, head);

		 * info.head and desc_head are updated back-to-back at the end of
		 * this function and nowhere else. Hence, they cannot be different
		 * unless two g2h_read calls are running concurrently. Which is not
		 * possible because it is guarded by ct->fast_lock. And yet, some
		 * discrete platforms are regularly hitting this error :(.
		 *
		 * desc_head rolling backwards shouldn't cause any noticeable
		 * problems - just a delay in GuC being allowed to proceed past that
		 * point in the queue. So for now, just disable the error until it
		 * can be root caused.
		 *
		if (g2h->info.head != desc_head) {
			desc_write(xe, g2h, status, desc_status | GUC_CTB_STATUS_MISMATCH);
			xe_gt_err(gt, "CT read: head was modified %u != %u\n",
				  desc_head, g2h->info.head);
			goto corrupted;
		}
		 */

		if (g2h->info.head > g2h->info.size) {
			desc_write(xe, g2h, status, desc_status | GUC_CTB_STATUS_OVERFLOW);
			xe_gt_err(gt, "CT read: head out of range: %u vs %u\n",
				  g2h->info.head, g2h->info.size);
			goto corrupted;
		}

		if (desc_tail >= g2h->info.size) {
			desc_write(xe, g2h, status, desc_status | GUC_CTB_STATUS_OVERFLOW);
			xe_gt_err(gt, "CT read: invalid tail offset %u >= %u)\n",
				  desc_tail, g2h->info.size);
			goto corrupted;
		}
	}

	/* Calculate DW available to read */
	tail = desc_read(xe, g2h, tail);
	avail = tail - g2h->info.head;
	if (unlikely(avail == 0))
		return 0;

	if (avail < 0)
		avail += g2h->info.size;

	/* Read header */
	xe_map_memcpy_from(xe, msg, &g2h->cmds, sizeof(u32) * g2h->info.head,
			   sizeof(u32));
	len = FIELD_GET(GUC_CTB_MSG_0_NUM_DWORDS, msg[0]) + GUC_CTB_MSG_MIN_LEN;
	if (len > avail) {
		xe_gt_err(gt, "G2H channel broken on read, avail=%d, len=%d, reset required\n",
			  avail, len);
		goto corrupted;
	}

	head = (g2h->info.head + 1) % g2h->info.size;
	avail = len - 1;

	/* Read G2H message */
	if (avail + head > g2h->info.size) {
		u32 avail_til_wrap = g2h->info.size - head;

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

	hxg = msg_to_hxg(msg);
	action = FIELD_GET(GUC_HXG_EVENT_MSG_0_ACTION, hxg[0]);

	if (fast_path) {
		if (FIELD_GET(GUC_HXG_MSG_0_TYPE, hxg[0]) != GUC_HXG_TYPE_EVENT)
			return 0;

		switch (action) {
		case XE_GUC_ACTION_REPORT_PAGE_FAULT_REQ_DESC:
		case XE_GUC_ACTION_TLB_INVALIDATION_DONE:
			break;	/* Process these in fast-path */
		default:
			return 0;
		}
	}

	/* Update local / descriptor header */
	g2h->info.head = (head + avail) % g2h->info.size;
	desc_write(xe, g2h, head, g2h->info.head);

	trace_xe_guc_ctb_g2h(xe, ct_to_gt(ct)->info.id,
			     action, len, g2h->info.head, tail);

	return len;

corrupted:
	CT_DEAD(ct, &ct->ctbs.g2h, G2H_READ);
	return -EPROTO;
}

static void g2h_fast_path(struct xe_guc_ct *ct, u32 *msg, u32 len)
{
	struct xe_gt *gt = ct_to_gt(ct);
	struct xe_guc *guc = ct_to_guc(ct);
	u32 hxg_len = msg_len_to_hxg_len(len);
	u32 *hxg = msg_to_hxg(msg);
	u32 action = FIELD_GET(GUC_HXG_EVENT_MSG_0_ACTION, hxg[0]);
	u32 *payload = hxg + GUC_HXG_MSG_MIN_LEN;
	u32 adj_len = hxg_len - GUC_HXG_MSG_MIN_LEN;
	int ret = 0;

	switch (action) {
	case XE_GUC_ACTION_REPORT_PAGE_FAULT_REQ_DESC:
		ret = xe_guc_pagefault_handler(guc, payload, adj_len);
		break;
	case XE_GUC_ACTION_TLB_INVALIDATION_DONE:
		__g2h_release_space(ct, len);
		ret = xe_guc_tlb_inval_done_handler(guc, payload, adj_len);
		break;
	default:
		xe_gt_warn(gt, "NOT_POSSIBLE");
	}

	if (ret) {
		xe_gt_err(gt, "G2H action 0x%04x failed (%pe)\n",
			  action, ERR_PTR(ret));
		CT_DEAD(ct, NULL, FAST_G2H);
	}
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
	bool ongoing;
	int len;

	ongoing = xe_pm_runtime_get_if_active(ct_to_xe(ct));
	if (!ongoing && xe_pm_read_callback_task(ct_to_xe(ct)) == NULL)
		return;

	spin_lock(&ct->fast_lock);
	do {
		len = g2h_read(ct, ct->fast_msg, true);
		if (len > 0)
			g2h_fast_path(ct, ct->fast_msg, len);
	} while (len > 0);
	spin_unlock(&ct->fast_lock);

	if (ongoing)
		xe_pm_runtime_put(xe);
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

static void receive_g2h(struct xe_guc_ct *ct)
{
	bool ongoing;
	int ret;

	/*
	 * Normal users must always hold mem_access.ref around CT calls. However
	 * during the runtime pm callbacks we rely on CT to talk to the GuC, but
	 * at this stage we can't rely on mem_access.ref and even the
	 * callback_task will be different than current.  For such cases we just
	 * need to ensure we always process the responses from any blocking
	 * ct_send requests or where we otherwise expect some response when
	 * initiated from those callbacks (which will need to wait for the below
	 * dequeue_one_g2h()).  The dequeue_one_g2h() will gracefully fail if
	 * the device has suspended to the point that the CT communication has
	 * been disabled.
	 *
	 * If we are inside the runtime pm callback, we can be the only task
	 * still issuing CT requests (since that requires having the
	 * mem_access.ref).  It seems like it might in theory be possible to
	 * receive unsolicited events from the GuC just as we are
	 * suspending-resuming, but those will currently anyway be lost when
	 * eventually exiting from suspend, hence no need to wake up the device
	 * here. If we ever need something stronger than get_if_ongoing() then
	 * we need to be careful with blocking the pm callbacks from getting CT
	 * responses, if the worker here is blocked on those callbacks
	 * completing, creating a deadlock.
	 */
	ongoing = xe_pm_runtime_get_if_active(ct_to_xe(ct));
	if (!ongoing && xe_pm_read_callback_task(ct_to_xe(ct)) == NULL)
		return;

	do {
		mutex_lock(&ct->lock);
		ret = dequeue_one_g2h(ct);
		mutex_unlock(&ct->lock);

		if (unlikely(ret == -EPROTO || ret == -EOPNOTSUPP)) {
			xe_gt_err(ct_to_gt(ct), "CT dequeue failed: %d", ret);
			CT_DEAD(ct, NULL, G2H_RECV);
			kick_reset(ct);
		}
	} while (ret == 1);

	if (ongoing)
		xe_pm_runtime_put(ct_to_xe(ct));
}

static void g2h_worker_func(struct work_struct *w)
{
	struct xe_guc_ct *ct = container_of(w, struct xe_guc_ct, g2h_worker);

	receive_g2h(ct);
}

static void xe_fixup_u64_in_cmds(struct xe_device *xe, struct iosys_map *cmds,
				 u32 size, u32 idx, s64 shift)
{
	u32 hi, lo;
	u64 offset;

	lo = xe_map_rd_ring_u32(xe, cmds, idx, size);
	hi = xe_map_rd_ring_u32(xe, cmds, idx + 1, size);
	offset = make_u64(hi, lo);
	offset += shift;
	lo = lower_32_bits(offset);
	hi = upper_32_bits(offset);
	xe_map_wr_ring_u32(xe, cmds, idx, size, lo);
	xe_map_wr_ring_u32(xe, cmds, idx + 1, size, hi);
}

/*
 * Shift any GGTT addresses within a single message left within CTB from
 * before post-migration recovery.
 * @ct: pointer to CT struct of the target GuC
 * @cmds: iomap buffer containing CT messages
 * @head: start of the target message within the buffer
 * @len: length of the target message
 * @size: size of the commands buffer
 * @shift: the address shift to be added to each GGTT reference
 * Return: true if the message was fixed or needed no fixups, false on failure
 */
static bool ct_fixup_ggtt_in_message(struct xe_guc_ct *ct,
				     struct iosys_map *cmds, u32 head,
				     u32 len, u32 size, s64 shift)
{
	struct xe_gt *gt = ct_to_gt(ct);
	struct xe_device *xe = ct_to_xe(ct);
	u32 msg[GUC_HXG_MSG_MIN_LEN];
	u32 action, i, n;

	xe_gt_assert(gt, len >= GUC_HXG_MSG_MIN_LEN);

	msg[0] = xe_map_rd_ring_u32(xe, cmds, head, size);
	action = FIELD_GET(GUC_HXG_REQUEST_MSG_0_ACTION, msg[0]);

	xe_gt_sriov_dbg_verbose(gt, "fixing H2G %#x\n", action);

	switch (action) {
	case XE_GUC_ACTION_REGISTER_CONTEXT:
		if (len != XE_GUC_REGISTER_CONTEXT_MSG_LEN)
			goto err_len;
		xe_fixup_u64_in_cmds(xe, cmds, size, head +
				     XE_GUC_REGISTER_CONTEXT_DATA_5_WQ_DESC_ADDR_LOWER,
				     shift);
		xe_fixup_u64_in_cmds(xe, cmds, size, head +
				     XE_GUC_REGISTER_CONTEXT_DATA_7_WQ_BUF_BASE_LOWER,
				     shift);
		xe_fixup_u64_in_cmds(xe, cmds, size, head +
				     XE_GUC_REGISTER_CONTEXT_DATA_10_HW_LRC_ADDR, shift);
		break;
	case XE_GUC_ACTION_REGISTER_CONTEXT_MULTI_LRC:
		if (len < XE_GUC_REGISTER_CONTEXT_MULTI_LRC_MSG_MIN_LEN)
			goto err_len;
		n = xe_map_rd_ring_u32(xe, cmds, head +
				       XE_GUC_REGISTER_CONTEXT_MULTI_LRC_DATA_10_NUM_CTXS, size);
		if (len != XE_GUC_REGISTER_CONTEXT_MULTI_LRC_MSG_MIN_LEN + 2 * n)
			goto err_len;
		xe_fixup_u64_in_cmds(xe, cmds, size, head +
				     XE_GUC_REGISTER_CONTEXT_MULTI_LRC_DATA_5_WQ_DESC_ADDR_LOWER,
				     shift);
		xe_fixup_u64_in_cmds(xe, cmds, size, head +
				     XE_GUC_REGISTER_CONTEXT_MULTI_LRC_DATA_7_WQ_BUF_BASE_LOWER,
				     shift);
		for (i = 0; i < n; i++)
			xe_fixup_u64_in_cmds(xe, cmds, size, head +
					     XE_GUC_REGISTER_CONTEXT_MULTI_LRC_DATA_11_HW_LRC_ADDR
					     + 2 * i, shift);
		break;
	default:
		break;
	}
	return true;

err_len:
	xe_gt_err(gt, "Skipped G2G %#x message fixups, unexpected length (%u)\n", action, len);
	return false;
}

/*
 * Apply fixups to the next outgoing CT message within given CTB
 * @ct: the &xe_guc_ct struct instance representing the target GuC
 * @h2g: the &guc_ctb struct instance of the target buffer
 * @shift: shift to be added to all GGTT addresses within the CTB
 * @mhead: pointer to an integer storing message start position; the
 *   position is changed to next message before this function return
 * @avail: size of the area available for parsing, that is length
 *   of all remaining messages stored within the CTB
 * Return: size of the area available for parsing after one message
 *   has been parsed, that is length remaining from the updated mhead
 */
static int ct_fixup_ggtt_in_buffer(struct xe_guc_ct *ct, struct guc_ctb *h2g,
				   s64 shift, u32 *mhead, s32 avail)
{
	struct xe_gt *gt = ct_to_gt(ct);
	struct xe_device *xe = ct_to_xe(ct);
	u32 msg[GUC_HXG_MSG_MIN_LEN];
	u32 size = h2g->info.size;
	u32 head = *mhead;
	u32 len;

	xe_gt_assert(gt, avail >= (s32)GUC_CTB_MSG_MIN_LEN);

	/* Read header */
	msg[0] = xe_map_rd_ring_u32(xe, &h2g->cmds, head, size);
	len = FIELD_GET(GUC_CTB_MSG_0_NUM_DWORDS, msg[0]) + GUC_CTB_MSG_MIN_LEN;

	if (unlikely(len > (u32)avail)) {
		xe_gt_err(gt, "H2G channel broken on read, avail=%d, len=%d, fixups skipped\n",
			  avail, len);
		return 0;
	}

	head = (head + GUC_CTB_MSG_MIN_LEN) % size;
	if (!ct_fixup_ggtt_in_message(ct, &h2g->cmds, head, msg_len_to_hxg_len(len), size, shift))
		return 0;
	*mhead = (head + msg_len_to_hxg_len(len)) % size;

	return avail - len;
}

/**
 * xe_guc_ct_fixup_messages_with_ggtt - Fixup any pending H2G CTB messages
 * @ct: pointer to CT struct of the target GuC
 * @ggtt_shift: shift to be added to all GGTT addresses within the CTB
 *
 * Messages in GuC to Host CTB are owned by GuC and any fixups in them
 * are made by GuC. But content of the Host to GuC CTB is owned by the
 * KMD, so fixups to GGTT references in any pending messages need to be
 * applied here.
 * This function updates GGTT offsets in payloads of pending H2G CTB
 * messages (messages which were not consumed by GuC before the VF got
 * paused).
 */
void xe_guc_ct_fixup_messages_with_ggtt(struct xe_guc_ct *ct, s64 ggtt_shift)
{
	struct guc_ctb *h2g = &ct->ctbs.h2g;
	struct xe_guc *guc = ct_to_guc(ct);
	struct xe_gt *gt = guc_to_gt(guc);
	u32 head, tail, size;
	s32 avail;

	if (unlikely(h2g->info.broken))
		return;

	h2g->info.head = desc_read(ct_to_xe(ct), h2g, head);
	head = h2g->info.head;
	tail = READ_ONCE(h2g->info.tail);
	size = h2g->info.size;

	if (unlikely(head > size))
		goto corrupted;

	if (unlikely(tail >= size))
		goto corrupted;

	avail = tail - head;

	/* beware of buffer wrap case */
	if (unlikely(avail < 0))
		avail += size;
	xe_gt_dbg(gt, "available %d (%u:%u:%u)\n", avail, head, tail, size);
	xe_gt_assert(gt, avail >= 0);

	while (avail > 0)
		avail = ct_fixup_ggtt_in_buffer(ct, h2g, ggtt_shift, &head, avail);

	return;

corrupted:
	xe_gt_err(gt, "Corrupted H2G descriptor head=%u tail=%u size=%u, fixups not applied\n",
		  head, tail, size);
	h2g->info.broken = true;
}

static struct xe_guc_ct_snapshot *guc_ct_snapshot_alloc(struct xe_guc_ct *ct, bool atomic,
							bool want_ctb)
{
	struct xe_guc_ct_snapshot *snapshot;

	snapshot = kzalloc(sizeof(*snapshot), atomic ? GFP_ATOMIC : GFP_KERNEL);
	if (!snapshot)
		return NULL;

	if (ct->bo && want_ctb) {
		snapshot->ctb_size = xe_bo_size(ct->bo);
		snapshot->ctb = kmalloc(snapshot->ctb_size, atomic ? GFP_ATOMIC : GFP_KERNEL);
	}

	return snapshot;
}

static void guc_ctb_snapshot_capture(struct xe_device *xe, struct guc_ctb *ctb,
				     struct guc_ctb_snapshot *snapshot)
{
	xe_map_memcpy_from(xe, &snapshot->desc, &ctb->desc, 0,
			   sizeof(struct guc_ct_buffer_desc));
	memcpy(&snapshot->info, &ctb->info, sizeof(struct guc_ctb_info));
}

static void guc_ctb_snapshot_print(struct guc_ctb_snapshot *snapshot,
				   struct drm_printer *p)
{
	drm_printf(p, "\tsize: %d\n", snapshot->info.size);
	drm_printf(p, "\tresv_space: %d\n", snapshot->info.resv_space);
	drm_printf(p, "\thead: %d\n", snapshot->info.head);
	drm_printf(p, "\ttail: %d\n", snapshot->info.tail);
	drm_printf(p, "\tspace: %d\n", snapshot->info.space);
	drm_printf(p, "\tbroken: %d\n", snapshot->info.broken);
	drm_printf(p, "\thead (memory): %d\n", snapshot->desc.head);
	drm_printf(p, "\ttail (memory): %d\n", snapshot->desc.tail);
	drm_printf(p, "\tstatus (memory): 0x%x\n", snapshot->desc.status);
}

static struct xe_guc_ct_snapshot *guc_ct_snapshot_capture(struct xe_guc_ct *ct, bool atomic,
							  bool want_ctb)
{
	struct xe_device *xe = ct_to_xe(ct);
	struct xe_guc_ct_snapshot *snapshot;

	snapshot = guc_ct_snapshot_alloc(ct, atomic, want_ctb);
	if (!snapshot) {
		xe_gt_err(ct_to_gt(ct), "Skipping CTB snapshot entirely.\n");
		return NULL;
	}

	if (xe_guc_ct_enabled(ct) || ct->state == XE_GUC_CT_STATE_STOPPED) {
		snapshot->ct_enabled = true;
		snapshot->g2h_outstanding = READ_ONCE(ct->g2h_outstanding);
		guc_ctb_snapshot_capture(xe, &ct->ctbs.h2g, &snapshot->h2g);
		guc_ctb_snapshot_capture(xe, &ct->ctbs.g2h, &snapshot->g2h);
	}

	if (ct->bo && snapshot->ctb)
		xe_map_memcpy_from(xe, snapshot->ctb, &ct->bo->vmap, 0, snapshot->ctb_size);

	return snapshot;
}

/**
 * xe_guc_ct_snapshot_capture - Take a quick snapshot of the CT state.
 * @ct: GuC CT object.
 *
 * This can be printed out in a later stage like during dev_coredump
 * analysis. This is safe to be called during atomic context.
 *
 * Returns: a GuC CT snapshot object that must be freed by the caller
 * by using `xe_guc_ct_snapshot_free`.
 */
struct xe_guc_ct_snapshot *xe_guc_ct_snapshot_capture(struct xe_guc_ct *ct)
{
	return guc_ct_snapshot_capture(ct, true, true);
}

/**
 * xe_guc_ct_snapshot_print - Print out a given GuC CT snapshot.
 * @snapshot: GuC CT snapshot object.
 * @p: drm_printer where it will be printed out.
 *
 * This function prints out a given GuC CT snapshot object.
 */
void xe_guc_ct_snapshot_print(struct xe_guc_ct_snapshot *snapshot,
			      struct drm_printer *p)
{
	if (!snapshot)
		return;

	if (snapshot->ct_enabled) {
		drm_puts(p, "H2G CTB (all sizes in DW):\n");
		guc_ctb_snapshot_print(&snapshot->h2g, p);

		drm_puts(p, "G2H CTB (all sizes in DW):\n");
		guc_ctb_snapshot_print(&snapshot->g2h, p);
		drm_printf(p, "\tg2h outstanding: %d\n",
			   snapshot->g2h_outstanding);

		if (snapshot->ctb) {
			drm_printf(p, "[CTB].length: 0x%zx\n", snapshot->ctb_size);
			xe_print_blob_ascii85(p, "[CTB].data", '\n',
					      snapshot->ctb, 0, snapshot->ctb_size);
		}
	} else {
		drm_puts(p, "CT disabled\n");
	}
}

/**
 * xe_guc_ct_snapshot_free - Free all allocated objects for a given snapshot.
 * @snapshot: GuC CT snapshot object.
 *
 * This function free all the memory that needed to be allocated at capture
 * time.
 */
void xe_guc_ct_snapshot_free(struct xe_guc_ct_snapshot *snapshot)
{
	if (!snapshot)
		return;

	kfree(snapshot->ctb);
	kfree(snapshot);
}

/**
 * xe_guc_ct_print - GuC CT Print.
 * @ct: GuC CT.
 * @p: drm_printer where it will be printed out.
 * @want_ctb: Should the full CTB content be dumped (vs just the headers)
 *
 * This function will quickly capture a snapshot of the CT state
 * and immediately print it out.
 */
void xe_guc_ct_print(struct xe_guc_ct *ct, struct drm_printer *p, bool want_ctb)
{
	struct xe_guc_ct_snapshot *snapshot;

	snapshot = guc_ct_snapshot_capture(ct, false, want_ctb);
	xe_guc_ct_snapshot_print(snapshot, p);
	xe_guc_ct_snapshot_free(snapshot);
}

#if IS_ENABLED(CONFIG_DRM_XE_DEBUG)

#ifdef CONFIG_FUNCTION_ERROR_INJECTION
/*
 * This is a helper function which assists the driver in identifying if a fault
 * injection test is currently active, allowing it to reduce unnecessary debug
 * output. Typically, the function returns zero, but the fault injection
 * framework can alter this to return an error. Since faults are injected
 * through this function, it's important to ensure the compiler doesn't optimize
 * it into an inline function. To avoid such optimization, the 'noinline'
 * attribute is applied. Compiler optimizes the static function defined in the
 * header file as an inline function.
 */
noinline int xe_is_injection_active(void) { return 0; }
ALLOW_ERROR_INJECTION(xe_is_injection_active, ERRNO);
#else
int xe_is_injection_active(void) { return 0; }
#endif

static void ct_dead_capture(struct xe_guc_ct *ct, struct guc_ctb *ctb, u32 reason_code)
{
	struct xe_guc_log_snapshot *snapshot_log;
	struct xe_guc_ct_snapshot *snapshot_ct;
	struct xe_guc *guc = ct_to_guc(ct);
	unsigned long flags;
	bool have_capture;

	if (ctb)
		ctb->info.broken = true;
	/*
	 * Huge dump is getting generated when injecting error for guc CT/MMIO
	 * functions. So, let us suppress the dump when fault is injected.
	 */
	if (xe_is_injection_active())
		return;

	/* Ignore further errors after the first dump until a reset */
	if (ct->dead.reported)
		return;

	spin_lock_irqsave(&ct->dead.lock, flags);

	/* And only capture one dump at a time */
	have_capture = ct->dead.reason & (1 << CT_DEAD_STATE_CAPTURE);
	ct->dead.reason |= (1 << reason_code) |
			   (1 << CT_DEAD_STATE_CAPTURE);

	spin_unlock_irqrestore(&ct->dead.lock, flags);

	if (have_capture)
		return;

	snapshot_log = xe_guc_log_snapshot_capture(&guc->log, true);
	snapshot_ct = xe_guc_ct_snapshot_capture((ct));

	spin_lock_irqsave(&ct->dead.lock, flags);

	if (ct->dead.snapshot_log || ct->dead.snapshot_ct) {
		xe_gt_err(ct_to_gt(ct), "Got unexpected dead CT capture!\n");
		xe_guc_log_snapshot_free(snapshot_log);
		xe_guc_ct_snapshot_free(snapshot_ct);
	} else {
		ct->dead.snapshot_log = snapshot_log;
		ct->dead.snapshot_ct = snapshot_ct;
	}

	spin_unlock_irqrestore(&ct->dead.lock, flags);

	queue_work(system_unbound_wq, &(ct)->dead.worker);
}

static void ct_dead_print(struct xe_dead_ct *dead)
{
	struct xe_guc_ct *ct = container_of(dead, struct xe_guc_ct, dead);
	struct xe_device *xe = ct_to_xe(ct);
	struct xe_gt *gt = ct_to_gt(ct);
	static int g_count;
	struct drm_printer ip = xe_gt_info_printer(gt);
	struct drm_printer lp = drm_line_printer(&ip, "Capture", ++g_count);

	if (!dead->reason) {
		xe_gt_err(gt, "CTB is dead for no reason!?\n");
		return;
	}

	/* Can't generate a genuine core dump at this point, so just do the good bits */
	drm_puts(&lp, "**** Xe Device Coredump ****\n");
	drm_printf(&lp, "Reason: CTB is dead - 0x%X\n", dead->reason);
	xe_device_snapshot_print(xe, &lp);

	drm_printf(&lp, "**** GT #%d ****\n", gt->info.id);
	drm_printf(&lp, "\tTile: %d\n", gt->tile->id);

	drm_puts(&lp, "**** GuC Log ****\n");
	xe_guc_log_snapshot_print(dead->snapshot_log, &lp);

	drm_puts(&lp, "**** GuC CT ****\n");
	xe_guc_ct_snapshot_print(dead->snapshot_ct, &lp);

	drm_puts(&lp, "Done.\n");
}

static void ct_dead_worker_func(struct work_struct *w)
{
	struct xe_guc_ct *ct = container_of(w, struct xe_guc_ct, dead.worker);

	if (!ct->dead.reported) {
		ct->dead.reported = true;
		ct_dead_print(&ct->dead);
	}

	spin_lock_irq(&ct->dead.lock);

	xe_guc_log_snapshot_free(ct->dead.snapshot_log);
	ct->dead.snapshot_log = NULL;
	xe_guc_ct_snapshot_free(ct->dead.snapshot_ct);
	ct->dead.snapshot_ct = NULL;

	if (ct->dead.reason & (1 << CT_DEAD_STATE_REARM)) {
		/* A reset has occurred so re-arm the error reporting */
		ct->dead.reason = 0;
		ct->dead.reported = false;
	}

	spin_unlock_irq(&ct->dead.lock);
}
#endif
