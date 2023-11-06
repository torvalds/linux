// SPDX-License-Identifier: MIT
/*
 * Copyright © 2016-2019 Intel Corporation
 */

#include <linux/circ_buf.h>
#include <linux/ktime.h>
#include <linux/time64.h>
#include <linux/string_helpers.h>
#include <linux/timekeeping.h>

#include "i915_drv.h"
#include "intel_guc_ct.h"
#include "intel_guc_print.h"

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GUC)
enum {
	CT_DEAD_ALIVE = 0,
	CT_DEAD_SETUP,
	CT_DEAD_WRITE,
	CT_DEAD_DEADLOCK,
	CT_DEAD_H2G_HAS_ROOM,
	CT_DEAD_READ,
	CT_DEAD_PROCESS_FAILED,
};

static void ct_dead_ct_worker_func(struct work_struct *w);

#define CT_DEAD(ct, reason)	\
	do { \
		if (!(ct)->dead_ct_reported) { \
			(ct)->dead_ct_reason |= 1 << CT_DEAD_##reason; \
			queue_work(system_unbound_wq, &(ct)->dead_ct_worker); \
		} \
	} while (0)
#else
#define CT_DEAD(ct, reason)	do { } while (0)
#endif

static inline struct intel_guc *ct_to_guc(struct intel_guc_ct *ct)
{
	return container_of(ct, struct intel_guc, ct);
}

#define CT_ERROR(_ct, _fmt, ...) \
	guc_err(ct_to_guc(_ct), "CT: " _fmt, ##__VA_ARGS__)
#ifdef CONFIG_DRM_I915_DEBUG_GUC
#define CT_DEBUG(_ct, _fmt, ...) \
	guc_dbg(ct_to_guc(_ct), "CT: " _fmt, ##__VA_ARGS__)
#else
#define CT_DEBUG(...)	do { } while (0)
#endif
#define CT_PROBE_ERROR(_ct, _fmt, ...) \
	guc_probe_error(ct_to_guc(ct), "CT: " _fmt, ##__VA_ARGS__)

/**
 * DOC: CTB Blob
 *
 * We allocate single blob to hold both CTB descriptors and buffers:
 *
 *      +--------+-----------------------------------------------+------+
 *      | offset | contents                                      | size |
 *      +========+===============================================+======+
 *      | 0x0000 | H2G `CTB Descriptor`_ (send)                  |      |
 *      +--------+-----------------------------------------------+  4K  |
 *      | 0x0800 | G2H `CTB Descriptor`_ (recv)                  |      |
 *      +--------+-----------------------------------------------+------+
 *      | 0x1000 | H2G `CT Buffer`_ (send)                       | n*4K |
 *      |        |                                               |      |
 *      +--------+-----------------------------------------------+------+
 *      | 0x1000 | G2H `CT Buffer`_ (recv)                       | m*4K |
 *      | + n*4K |                                               |      |
 *      +--------+-----------------------------------------------+------+
 *
 * Size of each `CT Buffer`_ must be multiple of 4K.
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

struct ct_request {
	struct list_head link;
	u32 fence;
	u32 status;
	u32 response_len;
	u32 *response_buf;
};

struct ct_incoming_msg {
	struct list_head link;
	u32 size;
	u32 msg[];
};

enum { CTB_SEND = 0, CTB_RECV = 1 };

enum { CTB_OWNER_HOST = 0 };

static void ct_receive_tasklet_func(struct tasklet_struct *t);
static void ct_incoming_request_worker_func(struct work_struct *w);

/**
 * intel_guc_ct_init_early - Initialize CT state without requiring device access
 * @ct: pointer to CT struct
 */
void intel_guc_ct_init_early(struct intel_guc_ct *ct)
{
	spin_lock_init(&ct->ctbs.send.lock);
	spin_lock_init(&ct->ctbs.recv.lock);
	spin_lock_init(&ct->requests.lock);
	INIT_LIST_HEAD(&ct->requests.pending);
	INIT_LIST_HEAD(&ct->requests.incoming);
#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GUC)
	INIT_WORK(&ct->dead_ct_worker, ct_dead_ct_worker_func);
#endif
	INIT_WORK(&ct->requests.worker, ct_incoming_request_worker_func);
	tasklet_setup(&ct->receive_tasklet, ct_receive_tasklet_func);
	init_waitqueue_head(&ct->wq);
}

static void guc_ct_buffer_desc_init(struct guc_ct_buffer_desc *desc)
{
	memset(desc, 0, sizeof(*desc));
}

static void guc_ct_buffer_reset(struct intel_guc_ct_buffer *ctb)
{
	u32 space;

	ctb->broken = false;
	ctb->tail = 0;
	ctb->head = 0;
	space = CIRC_SPACE(ctb->tail, ctb->head, ctb->size) - ctb->resv_space;
	atomic_set(&ctb->space, space);

	guc_ct_buffer_desc_init(ctb->desc);
}

static void guc_ct_buffer_init(struct intel_guc_ct_buffer *ctb,
			       struct guc_ct_buffer_desc *desc,
			       u32 *cmds, u32 size_in_bytes, u32 resv_space)
{
	GEM_BUG_ON(size_in_bytes % 4);

	ctb->desc = desc;
	ctb->cmds = cmds;
	ctb->size = size_in_bytes / 4;
	ctb->resv_space = resv_space / 4;

	guc_ct_buffer_reset(ctb);
}

static int guc_action_control_ctb(struct intel_guc *guc, u32 control)
{
	u32 request[HOST2GUC_CONTROL_CTB_REQUEST_MSG_LEN] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, GUC_ACTION_HOST2GUC_CONTROL_CTB),
		FIELD_PREP(HOST2GUC_CONTROL_CTB_REQUEST_MSG_1_CONTROL, control),
	};
	int ret;

	GEM_BUG_ON(control != GUC_CTB_CONTROL_DISABLE && control != GUC_CTB_CONTROL_ENABLE);

	/* CT control must go over MMIO */
	ret = intel_guc_send_mmio(guc, request, ARRAY_SIZE(request), NULL, 0);

	return ret > 0 ? -EPROTO : ret;
}

static int ct_control_enable(struct intel_guc_ct *ct, bool enable)
{
	int err;

	err = guc_action_control_ctb(ct_to_guc(ct), enable ?
				     GUC_CTB_CONTROL_ENABLE : GUC_CTB_CONTROL_DISABLE);
	if (unlikely(err))
		CT_PROBE_ERROR(ct, "Failed to control/%s CTB (%pe)\n",
			       str_enable_disable(enable), ERR_PTR(err));

	return err;
}

static int ct_register_buffer(struct intel_guc_ct *ct, bool send,
			      u32 desc_addr, u32 buff_addr, u32 size)
{
	int err;

	err = intel_guc_self_cfg64(ct_to_guc(ct), send ?
				   GUC_KLV_SELF_CFG_H2G_CTB_DESCRIPTOR_ADDR_KEY :
				   GUC_KLV_SELF_CFG_G2H_CTB_DESCRIPTOR_ADDR_KEY,
				   desc_addr);
	if (unlikely(err))
		goto failed;

	err = intel_guc_self_cfg64(ct_to_guc(ct), send ?
				   GUC_KLV_SELF_CFG_H2G_CTB_ADDR_KEY :
				   GUC_KLV_SELF_CFG_G2H_CTB_ADDR_KEY,
				   buff_addr);
	if (unlikely(err))
		goto failed;

	err = intel_guc_self_cfg32(ct_to_guc(ct), send ?
				   GUC_KLV_SELF_CFG_H2G_CTB_SIZE_KEY :
				   GUC_KLV_SELF_CFG_G2H_CTB_SIZE_KEY,
				   size);
	if (unlikely(err))
failed:
		CT_PROBE_ERROR(ct, "Failed to register %s buffer (%pe)\n",
			       send ? "SEND" : "RECV", ERR_PTR(err));

	return err;
}

/**
 * intel_guc_ct_init - Init buffer-based communication
 * @ct: pointer to CT struct
 *
 * Allocate memory required for buffer-based communication.
 *
 * Return: 0 on success, a negative errno code on failure.
 */
int intel_guc_ct_init(struct intel_guc_ct *ct)
{
	struct intel_guc *guc = ct_to_guc(ct);
	struct guc_ct_buffer_desc *desc;
	u32 blob_size;
	u32 cmds_size;
	u32 resv_space;
	void *blob;
	u32 *cmds;
	int err;

	err = i915_inject_probe_error(guc_to_gt(guc)->i915, -ENXIO);
	if (err)
		return err;

	GEM_BUG_ON(ct->vma);

	blob_size = 2 * CTB_DESC_SIZE + CTB_H2G_BUFFER_SIZE + CTB_G2H_BUFFER_SIZE;
	err = intel_guc_allocate_and_map_vma(guc, blob_size, &ct->vma, &blob);
	if (unlikely(err)) {
		CT_PROBE_ERROR(ct, "Failed to allocate %u for CTB data (%pe)\n",
			       blob_size, ERR_PTR(err));
		return err;
	}

	CT_DEBUG(ct, "base=%#x size=%u\n", intel_guc_ggtt_offset(guc, ct->vma), blob_size);

	/* store pointers to desc and cmds for send ctb */
	desc = blob;
	cmds = blob + 2 * CTB_DESC_SIZE;
	cmds_size = CTB_H2G_BUFFER_SIZE;
	resv_space = 0;
	CT_DEBUG(ct, "%s desc %#tx cmds %#tx size %u/%u\n", "send",
		 ptrdiff(desc, blob), ptrdiff(cmds, blob), cmds_size,
		 resv_space);

	guc_ct_buffer_init(&ct->ctbs.send, desc, cmds, cmds_size, resv_space);

	/* store pointers to desc and cmds for recv ctb */
	desc = blob + CTB_DESC_SIZE;
	cmds = blob + 2 * CTB_DESC_SIZE + CTB_H2G_BUFFER_SIZE;
	cmds_size = CTB_G2H_BUFFER_SIZE;
	resv_space = G2H_ROOM_BUFFER_SIZE;
	CT_DEBUG(ct, "%s desc %#tx cmds %#tx size %u/%u\n", "recv",
		 ptrdiff(desc, blob), ptrdiff(cmds, blob), cmds_size,
		 resv_space);

	guc_ct_buffer_init(&ct->ctbs.recv, desc, cmds, cmds_size, resv_space);

	return 0;
}

/**
 * intel_guc_ct_fini - Fini buffer-based communication
 * @ct: pointer to CT struct
 *
 * Deallocate memory required for buffer-based communication.
 */
void intel_guc_ct_fini(struct intel_guc_ct *ct)
{
	GEM_BUG_ON(ct->enabled);

	tasklet_kill(&ct->receive_tasklet);
	i915_vma_unpin_and_release(&ct->vma, I915_VMA_RELEASE_MAP);
	memset(ct, 0, sizeof(*ct));
}

/**
 * intel_guc_ct_enable - Enable buffer based command transport.
 * @ct: pointer to CT struct
 *
 * Return: 0 on success, a negative errno code on failure.
 */
int intel_guc_ct_enable(struct intel_guc_ct *ct)
{
	struct intel_guc *guc = ct_to_guc(ct);
	u32 base, desc, cmds, size;
	void *blob;
	int err;

	GEM_BUG_ON(ct->enabled);

	/* vma should be already allocated and map'ed */
	GEM_BUG_ON(!ct->vma);
	GEM_BUG_ON(!i915_gem_object_has_pinned_pages(ct->vma->obj));
	base = intel_guc_ggtt_offset(guc, ct->vma);

	/* blob should start with send descriptor */
	blob = __px_vaddr(ct->vma->obj);
	GEM_BUG_ON(blob != ct->ctbs.send.desc);

	/* (re)initialize descriptors */
	guc_ct_buffer_reset(&ct->ctbs.send);
	guc_ct_buffer_reset(&ct->ctbs.recv);

	/*
	 * Register both CT buffers starting with RECV buffer.
	 * Descriptors are in first half of the blob.
	 */
	desc = base + ptrdiff(ct->ctbs.recv.desc, blob);
	cmds = base + ptrdiff(ct->ctbs.recv.cmds, blob);
	size = ct->ctbs.recv.size * 4;
	err = ct_register_buffer(ct, false, desc, cmds, size);
	if (unlikely(err))
		goto err_out;

	desc = base + ptrdiff(ct->ctbs.send.desc, blob);
	cmds = base + ptrdiff(ct->ctbs.send.cmds, blob);
	size = ct->ctbs.send.size * 4;
	err = ct_register_buffer(ct, true, desc, cmds, size);
	if (unlikely(err))
		goto err_out;

	err = ct_control_enable(ct, true);
	if (unlikely(err))
		goto err_out;

	ct->enabled = true;
	ct->stall_time = KTIME_MAX;
#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GUC)
	ct->dead_ct_reported = false;
	ct->dead_ct_reason = CT_DEAD_ALIVE;
#endif

	return 0;

err_out:
	CT_PROBE_ERROR(ct, "Failed to enable CTB (%pe)\n", ERR_PTR(err));
	CT_DEAD(ct, SETUP);
	return err;
}

/**
 * intel_guc_ct_disable - Disable buffer based command transport.
 * @ct: pointer to CT struct
 */
void intel_guc_ct_disable(struct intel_guc_ct *ct)
{
	struct intel_guc *guc = ct_to_guc(ct);

	GEM_BUG_ON(!ct->enabled);

	ct->enabled = false;

	if (intel_guc_is_fw_running(guc)) {
		ct_control_enable(ct, false);
	}
}

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM)
static void ct_track_lost_and_found(struct intel_guc_ct *ct, u32 fence, u32 action)
{
	unsigned int lost = fence % ARRAY_SIZE(ct->requests.lost_and_found);
#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GUC)
	unsigned long entries[SZ_32];
	unsigned int n;

	n = stack_trace_save(entries, ARRAY_SIZE(entries), 1);

	/* May be called under spinlock, so avoid sleeping */
	ct->requests.lost_and_found[lost].stack = stack_depot_save(entries, n, GFP_NOWAIT);
#endif
	ct->requests.lost_and_found[lost].fence = fence;
	ct->requests.lost_and_found[lost].action = action;
}
#endif

static u32 ct_get_next_fence(struct intel_guc_ct *ct)
{
	/* For now it's trivial */
	return ++ct->requests.last_fence;
}

static int ct_write(struct intel_guc_ct *ct,
		    const u32 *action,
		    u32 len /* in dwords */,
		    u32 fence, u32 flags)
{
	struct intel_guc_ct_buffer *ctb = &ct->ctbs.send;
	struct guc_ct_buffer_desc *desc = ctb->desc;
	u32 tail = ctb->tail;
	u32 size = ctb->size;
	u32 header;
	u32 hxg;
	u32 type;
	u32 *cmds = ctb->cmds;
	unsigned int i;

	if (unlikely(desc->status))
		goto corrupted;

	GEM_BUG_ON(tail > size);

#ifdef CONFIG_DRM_I915_DEBUG_GUC
	if (unlikely(tail != READ_ONCE(desc->tail))) {
		CT_ERROR(ct, "Tail was modified %u != %u\n",
			 desc->tail, tail);
		desc->status |= GUC_CTB_STATUS_MISMATCH;
		goto corrupted;
	}
	if (unlikely(READ_ONCE(desc->head) >= size)) {
		CT_ERROR(ct, "Invalid head offset %u >= %u)\n",
			 desc->head, size);
		desc->status |= GUC_CTB_STATUS_OVERFLOW;
		goto corrupted;
	}
#endif

	/*
	 * dw0: CT header (including fence)
	 * dw1: HXG header (including action code)
	 * dw2+: action data
	 */
	header = FIELD_PREP(GUC_CTB_MSG_0_FORMAT, GUC_CTB_FORMAT_HXG) |
		 FIELD_PREP(GUC_CTB_MSG_0_NUM_DWORDS, len) |
		 FIELD_PREP(GUC_CTB_MSG_0_FENCE, fence);

	type = (flags & INTEL_GUC_CT_SEND_NB) ? GUC_HXG_TYPE_FAST_REQUEST :
		GUC_HXG_TYPE_REQUEST;
	hxg = FIELD_PREP(GUC_HXG_MSG_0_TYPE, type) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION |
			   GUC_HXG_REQUEST_MSG_0_DATA0, action[0]);

	CT_DEBUG(ct, "writing (tail %u) %*ph %*ph %*ph\n",
		 tail, 4, &header, 4, &hxg, 4 * (len - 1), &action[1]);

	cmds[tail] = header;
	tail = (tail + 1) % size;

	cmds[tail] = hxg;
	tail = (tail + 1) % size;

	for (i = 1; i < len; i++) {
		cmds[tail] = action[i];
		tail = (tail + 1) % size;
	}
	GEM_BUG_ON(tail > size);

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM)
	ct_track_lost_and_found(ct, fence,
				FIELD_GET(GUC_HXG_EVENT_MSG_0_ACTION, action[0]));
#endif

	/*
	 * make sure H2G buffer update and LRC tail update (if this triggering a
	 * submission) are visible before updating the descriptor tail
	 */
	intel_guc_write_barrier(ct_to_guc(ct));

	/* update local copies */
	ctb->tail = tail;
	GEM_BUG_ON(atomic_read(&ctb->space) < len + GUC_CTB_HDR_LEN);
	atomic_sub(len + GUC_CTB_HDR_LEN, &ctb->space);

	/* now update descriptor */
	WRITE_ONCE(desc->tail, tail);

	return 0;

corrupted:
	CT_ERROR(ct, "Corrupted descriptor head=%u tail=%u status=%#x\n",
		 desc->head, desc->tail, desc->status);
	CT_DEAD(ct, WRITE);
	ctb->broken = true;
	return -EPIPE;
}

/**
 * wait_for_ct_request_update - Wait for CT request state update.
 * @ct:		pointer to CT
 * @req:	pointer to pending request
 * @status:	placeholder for status
 *
 * For each sent request, GuC shall send back CT response message.
 * Our message handler will update status of tracked request once
 * response message with given fence is received. Wait here and
 * check for valid response status value.
 *
 * Return:
 * *	0 response received (status is valid)
 * *	-ETIMEDOUT no response within hardcoded timeout
 */
static int wait_for_ct_request_update(struct intel_guc_ct *ct, struct ct_request *req, u32 *status)
{
	int err;
	bool ct_enabled;

	/*
	 * Fast commands should complete in less than 10us, so sample quickly
	 * up to that length of time, then switch to a slower sleep-wait loop.
	 * No GuC command should ever take longer than 10ms but many GuC
	 * commands can be inflight at time, so use a 1s timeout on the slower
	 * sleep-wait loop.
	 */
#define GUC_CTB_RESPONSE_TIMEOUT_SHORT_MS 10
#define GUC_CTB_RESPONSE_TIMEOUT_LONG_MS 1000
#define done \
	(!(ct_enabled = intel_guc_ct_enabled(ct)) || \
	 FIELD_GET(GUC_HXG_MSG_0_ORIGIN, READ_ONCE(req->status)) == \
	 GUC_HXG_ORIGIN_GUC)
	err = wait_for_us(done, GUC_CTB_RESPONSE_TIMEOUT_SHORT_MS);
	if (err)
		err = wait_for(done, GUC_CTB_RESPONSE_TIMEOUT_LONG_MS);
#undef done
	if (!ct_enabled)
		err = -ENODEV;

	*status = req->status;
	return err;
}

#define GUC_CTB_TIMEOUT_MS	1500
static inline bool ct_deadlocked(struct intel_guc_ct *ct)
{
	long timeout = GUC_CTB_TIMEOUT_MS;
	bool ret = ktime_ms_delta(ktime_get(), ct->stall_time) > timeout;

	if (unlikely(ret)) {
		struct guc_ct_buffer_desc *send = ct->ctbs.send.desc;
		struct guc_ct_buffer_desc *recv = ct->ctbs.send.desc;

		CT_ERROR(ct, "Communication stalled for %lld ms, desc status=%#x,%#x\n",
			 ktime_ms_delta(ktime_get(), ct->stall_time),
			 send->status, recv->status);
		CT_ERROR(ct, "H2G Space: %u (Bytes)\n",
			 atomic_read(&ct->ctbs.send.space) * 4);
		CT_ERROR(ct, "Head: %u (Dwords)\n", ct->ctbs.send.desc->head);
		CT_ERROR(ct, "Tail: %u (Dwords)\n", ct->ctbs.send.desc->tail);
		CT_ERROR(ct, "G2H Space: %u (Bytes)\n",
			 atomic_read(&ct->ctbs.recv.space) * 4);
		CT_ERROR(ct, "Head: %u\n (Dwords)", ct->ctbs.recv.desc->head);
		CT_ERROR(ct, "Tail: %u\n (Dwords)", ct->ctbs.recv.desc->tail);

		CT_DEAD(ct, DEADLOCK);
		ct->ctbs.send.broken = true;
	}

	return ret;
}

static inline bool g2h_has_room(struct intel_guc_ct *ct, u32 g2h_len_dw)
{
	struct intel_guc_ct_buffer *ctb = &ct->ctbs.recv;

	/*
	 * We leave a certain amount of space in the G2H CTB buffer for
	 * unexpected G2H CTBs (e.g. logging, engine hang, etc...)
	 */
	return !g2h_len_dw || atomic_read(&ctb->space) >= g2h_len_dw;
}

static inline void g2h_reserve_space(struct intel_guc_ct *ct, u32 g2h_len_dw)
{
	lockdep_assert_held(&ct->ctbs.send.lock);

	GEM_BUG_ON(!g2h_has_room(ct, g2h_len_dw));

	if (g2h_len_dw)
		atomic_sub(g2h_len_dw, &ct->ctbs.recv.space);
}

static inline void g2h_release_space(struct intel_guc_ct *ct, u32 g2h_len_dw)
{
	atomic_add(g2h_len_dw, &ct->ctbs.recv.space);
}

static inline bool h2g_has_room(struct intel_guc_ct *ct, u32 len_dw)
{
	struct intel_guc_ct_buffer *ctb = &ct->ctbs.send;
	struct guc_ct_buffer_desc *desc = ctb->desc;
	u32 head;
	u32 space;

	if (atomic_read(&ctb->space) >= len_dw)
		return true;

	head = READ_ONCE(desc->head);
	if (unlikely(head > ctb->size)) {
		CT_ERROR(ct, "Invalid head offset %u >= %u)\n",
			 head, ctb->size);
		desc->status |= GUC_CTB_STATUS_OVERFLOW;
		ctb->broken = true;
		CT_DEAD(ct, H2G_HAS_ROOM);
		return false;
	}

	space = CIRC_SPACE(ctb->tail, head, ctb->size);
	atomic_set(&ctb->space, space);

	return space >= len_dw;
}

static int has_room_nb(struct intel_guc_ct *ct, u32 h2g_dw, u32 g2h_dw)
{
	bool h2g = h2g_has_room(ct, h2g_dw);
	bool g2h = g2h_has_room(ct, g2h_dw);

	lockdep_assert_held(&ct->ctbs.send.lock);

	if (unlikely(!h2g || !g2h)) {
		if (ct->stall_time == KTIME_MAX)
			ct->stall_time = ktime_get();

		/* Be paranoid and kick G2H tasklet to free credits */
		if (!g2h)
			tasklet_hi_schedule(&ct->receive_tasklet);

		if (unlikely(ct_deadlocked(ct)))
			return -EPIPE;
		else
			return -EBUSY;
	}

	ct->stall_time = KTIME_MAX;
	return 0;
}

#define G2H_LEN_DW(f) ({ \
	typeof(f) f_ = (f); \
	FIELD_GET(INTEL_GUC_CT_SEND_G2H_DW_MASK, f_) ? \
	FIELD_GET(INTEL_GUC_CT_SEND_G2H_DW_MASK, f_) + \
	GUC_CTB_HXG_MSG_MIN_LEN : 0; \
})
static int ct_send_nb(struct intel_guc_ct *ct,
		      const u32 *action,
		      u32 len,
		      u32 flags)
{
	struct intel_guc_ct_buffer *ctb = &ct->ctbs.send;
	unsigned long spin_flags;
	u32 g2h_len_dw = G2H_LEN_DW(flags);
	u32 fence;
	int ret;

	spin_lock_irqsave(&ctb->lock, spin_flags);

	ret = has_room_nb(ct, len + GUC_CTB_HDR_LEN, g2h_len_dw);
	if (unlikely(ret))
		goto out;

	fence = ct_get_next_fence(ct);
	ret = ct_write(ct, action, len, fence, flags);
	if (unlikely(ret))
		goto out;

	g2h_reserve_space(ct, g2h_len_dw);
	intel_guc_notify(ct_to_guc(ct));

out:
	spin_unlock_irqrestore(&ctb->lock, spin_flags);

	return ret;
}

static int ct_send(struct intel_guc_ct *ct,
		   const u32 *action,
		   u32 len,
		   u32 *response_buf,
		   u32 response_buf_size,
		   u32 *status)
{
	struct intel_guc_ct_buffer *ctb = &ct->ctbs.send;
	struct ct_request request;
	unsigned long flags;
	unsigned int sleep_period_ms = 1;
	bool send_again;
	u32 fence;
	int err;

	GEM_BUG_ON(!ct->enabled);
	GEM_BUG_ON(!len);
	GEM_BUG_ON(len > GUC_CTB_HXG_MSG_MAX_LEN - GUC_CTB_HDR_LEN);
	GEM_BUG_ON(!response_buf && response_buf_size);
	might_sleep();

resend:
	send_again = false;

	/*
	 * We use a lazy spin wait loop here as we believe that if the CT
	 * buffers are sized correctly the flow control condition should be
	 * rare. Reserving the maximum size in the G2H credits as we don't know
	 * how big the response is going to be.
	 */
retry:
	spin_lock_irqsave(&ctb->lock, flags);
	if (unlikely(!h2g_has_room(ct, len + GUC_CTB_HDR_LEN) ||
		     !g2h_has_room(ct, GUC_CTB_HXG_MSG_MAX_LEN))) {
		if (ct->stall_time == KTIME_MAX)
			ct->stall_time = ktime_get();
		spin_unlock_irqrestore(&ctb->lock, flags);

		if (unlikely(ct_deadlocked(ct)))
			return -EPIPE;

		if (msleep_interruptible(sleep_period_ms))
			return -EINTR;
		sleep_period_ms = sleep_period_ms << 1;

		goto retry;
	}

	ct->stall_time = KTIME_MAX;

	fence = ct_get_next_fence(ct);
	request.fence = fence;
	request.status = 0;
	request.response_len = response_buf_size;
	request.response_buf = response_buf;

	spin_lock(&ct->requests.lock);
	list_add_tail(&request.link, &ct->requests.pending);
	spin_unlock(&ct->requests.lock);

	err = ct_write(ct, action, len, fence, 0);
	g2h_reserve_space(ct, GUC_CTB_HXG_MSG_MAX_LEN);

	spin_unlock_irqrestore(&ctb->lock, flags);

	if (unlikely(err))
		goto unlink;

	intel_guc_notify(ct_to_guc(ct));

	err = wait_for_ct_request_update(ct, &request, status);
	g2h_release_space(ct, GUC_CTB_HXG_MSG_MAX_LEN);
	if (unlikely(err)) {
		if (err == -ENODEV)
			/* wait_for_ct_request_update returns -ENODEV on reset/suspend in progress.
			 * In this case, output is debug rather than error info
			 */
			CT_DEBUG(ct, "Request %#x (fence %u) cancelled as CTB is disabled\n",
				 action[0], request.fence);
		else
			CT_ERROR(ct, "No response for request %#x (fence %u)\n",
				 action[0], request.fence);
		goto unlink;
	}

	if (FIELD_GET(GUC_HXG_MSG_0_TYPE, *status) == GUC_HXG_TYPE_NO_RESPONSE_RETRY) {
		CT_DEBUG(ct, "retrying request %#x (%u)\n", *action,
			 FIELD_GET(GUC_HXG_RETRY_MSG_0_REASON, *status));
		send_again = true;
		goto unlink;
	}

	if (FIELD_GET(GUC_HXG_MSG_0_TYPE, *status) != GUC_HXG_TYPE_RESPONSE_SUCCESS) {
		err = -EIO;
		goto unlink;
	}

	if (response_buf) {
		/* There shall be no data in the status */
		WARN_ON(FIELD_GET(GUC_HXG_RESPONSE_MSG_0_DATA0, request.status));
		/* Return actual response len */
		err = request.response_len;
	} else {
		/* There shall be no response payload */
		WARN_ON(request.response_len);
		/* Return data decoded from the status dword */
		err = FIELD_GET(GUC_HXG_RESPONSE_MSG_0_DATA0, *status);
	}

unlink:
	spin_lock_irqsave(&ct->requests.lock, flags);
	list_del(&request.link);
	spin_unlock_irqrestore(&ct->requests.lock, flags);

	if (unlikely(send_again))
		goto resend;

	return err;
}

/*
 * Command Transport (CT) buffer based GuC send function.
 */
int intel_guc_ct_send(struct intel_guc_ct *ct, const u32 *action, u32 len,
		      u32 *response_buf, u32 response_buf_size, u32 flags)
{
	u32 status = ~0; /* undefined */
	int ret;

	if (unlikely(!ct->enabled)) {
		struct intel_guc *guc = ct_to_guc(ct);
		struct intel_uc *uc = container_of(guc, struct intel_uc, guc);

		WARN(!uc->reset_in_progress, "Unexpected send: action=%#x\n", *action);
		return -ENODEV;
	}

	if (unlikely(ct->ctbs.send.broken))
		return -EPIPE;

	if (flags & INTEL_GUC_CT_SEND_NB)
		return ct_send_nb(ct, action, len, flags);

	ret = ct_send(ct, action, len, response_buf, response_buf_size, &status);
	if (unlikely(ret < 0)) {
		if (ret != -ENODEV)
			CT_ERROR(ct, "Sending action %#x failed (%pe) status=%#X\n",
				 action[0], ERR_PTR(ret), status);
	} else if (unlikely(ret)) {
		CT_DEBUG(ct, "send action %#x returned %d (%#x)\n",
			 action[0], ret, ret);
	}

	return ret;
}

static struct ct_incoming_msg *ct_alloc_msg(u32 num_dwords)
{
	struct ct_incoming_msg *msg;

	msg = kmalloc(struct_size(msg, msg, num_dwords), GFP_ATOMIC);
	if (msg)
		msg->size = num_dwords;
	return msg;
}

static void ct_free_msg(struct ct_incoming_msg *msg)
{
	kfree(msg);
}

/*
 * Return: number available remaining dwords to read (0 if empty)
 *         or a negative error code on failure
 */
static int ct_read(struct intel_guc_ct *ct, struct ct_incoming_msg **msg)
{
	struct intel_guc_ct_buffer *ctb = &ct->ctbs.recv;
	struct guc_ct_buffer_desc *desc = ctb->desc;
	u32 head = ctb->head;
	u32 tail = READ_ONCE(desc->tail);
	u32 size = ctb->size;
	u32 *cmds = ctb->cmds;
	s32 available;
	unsigned int len;
	unsigned int i;
	u32 header;

	if (unlikely(ctb->broken))
		return -EPIPE;

	if (unlikely(desc->status)) {
		u32 status = desc->status;

		if (status & GUC_CTB_STATUS_UNUSED) {
			/*
			 * Potentially valid if a CLIENT_RESET request resulted in
			 * contexts/engines being reset. But should never happen as
			 * no contexts should be active when CLIENT_RESET is sent.
			 */
			CT_ERROR(ct, "Unexpected G2H after GuC has stopped!\n");
			status &= ~GUC_CTB_STATUS_UNUSED;
		}

		if (status)
			goto corrupted;
	}

	GEM_BUG_ON(head > size);

#ifdef CONFIG_DRM_I915_DEBUG_GUC
	if (unlikely(head != READ_ONCE(desc->head))) {
		CT_ERROR(ct, "Head was modified %u != %u\n",
			 desc->head, head);
		desc->status |= GUC_CTB_STATUS_MISMATCH;
		goto corrupted;
	}
#endif
	if (unlikely(tail >= size)) {
		CT_ERROR(ct, "Invalid tail offset %u >= %u)\n",
			 tail, size);
		desc->status |= GUC_CTB_STATUS_OVERFLOW;
		goto corrupted;
	}

	/* tail == head condition indicates empty */
	available = tail - head;
	if (unlikely(available == 0)) {
		*msg = NULL;
		return 0;
	}

	/* beware of buffer wrap case */
	if (unlikely(available < 0))
		available += size;
	CT_DEBUG(ct, "available %d (%u:%u:%u)\n", available, head, tail, size);
	GEM_BUG_ON(available < 0);

	header = cmds[head];
	head = (head + 1) % size;

	/* message len with header */
	len = FIELD_GET(GUC_CTB_MSG_0_NUM_DWORDS, header) + GUC_CTB_MSG_MIN_LEN;
	if (unlikely(len > (u32)available)) {
		CT_ERROR(ct, "Incomplete message %*ph %*ph %*ph\n",
			 4, &header,
			 4 * (head + available - 1 > size ?
			      size - head : available - 1), &cmds[head],
			 4 * (head + available - 1 > size ?
			      available - 1 - size + head : 0), &cmds[0]);
		desc->status |= GUC_CTB_STATUS_UNDERFLOW;
		goto corrupted;
	}

	*msg = ct_alloc_msg(len);
	if (!*msg) {
		CT_ERROR(ct, "No memory for message %*ph %*ph %*ph\n",
			 4, &header,
			 4 * (head + available - 1 > size ?
			      size - head : available - 1), &cmds[head],
			 4 * (head + available - 1 > size ?
			      available - 1 - size + head : 0), &cmds[0]);
		return available;
	}

	(*msg)->msg[0] = header;

	for (i = 1; i < len; i++) {
		(*msg)->msg[i] = cmds[head];
		head = (head + 1) % size;
	}
	CT_DEBUG(ct, "received %*ph\n", 4 * len, (*msg)->msg);

	/* update local copies */
	ctb->head = head;

	/* now update descriptor */
	WRITE_ONCE(desc->head, head);

	/*
	 * Wa_22016122933: Making sure the head update is
	 * visible to GuC right away
	 */
	intel_guc_write_barrier(ct_to_guc(ct));

	return available - len;

corrupted:
	CT_ERROR(ct, "Corrupted descriptor head=%u tail=%u status=%#x\n",
		 desc->head, desc->tail, desc->status);
	ctb->broken = true;
	CT_DEAD(ct, READ);
	return -EPIPE;
}

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM)
static bool ct_check_lost_and_found(struct intel_guc_ct *ct, u32 fence)
{
	unsigned int n;
	char *buf = NULL;
	bool found = false;

	lockdep_assert_held(&ct->requests.lock);

	for (n = 0; n < ARRAY_SIZE(ct->requests.lost_and_found); n++) {
		if (ct->requests.lost_and_found[n].fence != fence)
			continue;
		found = true;

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GUC)
		buf = kmalloc(SZ_4K, GFP_NOWAIT);
		if (buf && stack_depot_snprint(ct->requests.lost_and_found[n].stack,
					       buf, SZ_4K, 0)) {
			CT_ERROR(ct, "Fence %u was used by action %#04x sent at\n%s",
				 fence, ct->requests.lost_and_found[n].action, buf);
			break;
		}
#endif
		CT_ERROR(ct, "Fence %u was used by action %#04x\n",
			 fence, ct->requests.lost_and_found[n].action);
		break;
	}
	kfree(buf);
	return found;
}
#else
static bool ct_check_lost_and_found(struct intel_guc_ct *ct, u32 fence)
{
	return false;
}
#endif

static int ct_handle_response(struct intel_guc_ct *ct, struct ct_incoming_msg *response)
{
	u32 len = FIELD_GET(GUC_CTB_MSG_0_NUM_DWORDS, response->msg[0]);
	u32 fence = FIELD_GET(GUC_CTB_MSG_0_FENCE, response->msg[0]);
	const u32 *hxg = &response->msg[GUC_CTB_MSG_MIN_LEN];
	const u32 *data = &hxg[GUC_HXG_MSG_MIN_LEN];
	u32 datalen = len - GUC_HXG_MSG_MIN_LEN;
	struct ct_request *req;
	unsigned long flags;
	bool found = false;
	int err = 0;

	GEM_BUG_ON(len < GUC_HXG_MSG_MIN_LEN);
	GEM_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_ORIGIN, hxg[0]) != GUC_HXG_ORIGIN_GUC);
	GEM_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_TYPE, hxg[0]) != GUC_HXG_TYPE_RESPONSE_SUCCESS &&
		   FIELD_GET(GUC_HXG_MSG_0_TYPE, hxg[0]) != GUC_HXG_TYPE_NO_RESPONSE_RETRY &&
		   FIELD_GET(GUC_HXG_MSG_0_TYPE, hxg[0]) != GUC_HXG_TYPE_RESPONSE_FAILURE);

	CT_DEBUG(ct, "response fence %u status %#x\n", fence, hxg[0]);

	spin_lock_irqsave(&ct->requests.lock, flags);
	list_for_each_entry(req, &ct->requests.pending, link) {
		if (unlikely(fence != req->fence)) {
			CT_DEBUG(ct, "request %u awaits response\n",
				 req->fence);
			continue;
		}
		if (unlikely(datalen > req->response_len)) {
			CT_ERROR(ct, "Response %u too long (datalen %u > %u)\n",
				 req->fence, datalen, req->response_len);
			datalen = min(datalen, req->response_len);
			err = -EMSGSIZE;
		}
		if (datalen)
			memcpy(req->response_buf, data, 4 * datalen);
		req->response_len = datalen;
		WRITE_ONCE(req->status, hxg[0]);
		found = true;
		break;
	}
	if (!found) {
		CT_ERROR(ct, "Unsolicited response message: len %u, data %#x (fence %u, last %u)\n",
			 len, hxg[0], fence, ct->requests.last_fence);
		if (!ct_check_lost_and_found(ct, fence)) {
			list_for_each_entry(req, &ct->requests.pending, link)
				CT_ERROR(ct, "request %u awaits response\n",
					 req->fence);
		}
		err = -ENOKEY;
	}
	spin_unlock_irqrestore(&ct->requests.lock, flags);

	if (unlikely(err))
		return err;

	ct_free_msg(response);
	return 0;
}

static int ct_process_request(struct intel_guc_ct *ct, struct ct_incoming_msg *request)
{
	struct intel_guc *guc = ct_to_guc(ct);
	const u32 *hxg;
	const u32 *payload;
	u32 hxg_len, action, len;
	int ret;

	hxg = &request->msg[GUC_CTB_MSG_MIN_LEN];
	hxg_len = request->size - GUC_CTB_MSG_MIN_LEN;
	payload = &hxg[GUC_HXG_MSG_MIN_LEN];
	action = FIELD_GET(GUC_HXG_EVENT_MSG_0_ACTION, hxg[0]);
	len = hxg_len - GUC_HXG_MSG_MIN_LEN;

	CT_DEBUG(ct, "request %x %*ph\n", action, 4 * len, payload);

	switch (action) {
	case INTEL_GUC_ACTION_DEFAULT:
		ret = intel_guc_to_host_process_recv_msg(guc, payload, len);
		break;
	case INTEL_GUC_ACTION_DEREGISTER_CONTEXT_DONE:
		ret = intel_guc_deregister_done_process_msg(guc, payload,
							    len);
		break;
	case INTEL_GUC_ACTION_SCHED_CONTEXT_MODE_DONE:
		ret = intel_guc_sched_done_process_msg(guc, payload, len);
		break;
	case INTEL_GUC_ACTION_CONTEXT_RESET_NOTIFICATION:
		ret = intel_guc_context_reset_process_msg(guc, payload, len);
		break;
	case INTEL_GUC_ACTION_STATE_CAPTURE_NOTIFICATION:
		ret = intel_guc_error_capture_process_msg(guc, payload, len);
		if (unlikely(ret))
			CT_ERROR(ct, "error capture notification failed %x %*ph\n",
				 action, 4 * len, payload);
		break;
	case INTEL_GUC_ACTION_ENGINE_FAILURE_NOTIFICATION:
		ret = intel_guc_engine_failure_process_msg(guc, payload, len);
		break;
	case INTEL_GUC_ACTION_NOTIFY_FLUSH_LOG_BUFFER_TO_FILE:
		intel_guc_log_handle_flush_event(&guc->log);
		ret = 0;
		break;
	case INTEL_GUC_ACTION_NOTIFY_CRASH_DUMP_POSTED:
		CT_ERROR(ct, "Received GuC crash dump notification!\n");
		ret = 0;
		break;
	case INTEL_GUC_ACTION_NOTIFY_EXCEPTION:
		CT_ERROR(ct, "Received GuC exception notification!\n");
		ret = 0;
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	if (unlikely(ret)) {
		CT_ERROR(ct, "Failed to process request %04x (%pe)\n",
			 action, ERR_PTR(ret));
		return ret;
	}

	ct_free_msg(request);
	return 0;
}

static bool ct_process_incoming_requests(struct intel_guc_ct *ct)
{
	unsigned long flags;
	struct ct_incoming_msg *request;
	bool done;
	int err;

	spin_lock_irqsave(&ct->requests.lock, flags);
	request = list_first_entry_or_null(&ct->requests.incoming,
					   struct ct_incoming_msg, link);
	if (request)
		list_del(&request->link);
	done = !!list_empty(&ct->requests.incoming);
	spin_unlock_irqrestore(&ct->requests.lock, flags);

	if (!request)
		return true;

	err = ct_process_request(ct, request);
	if (unlikely(err)) {
		CT_ERROR(ct, "Failed to process CT message (%pe) %*ph\n",
			 ERR_PTR(err), 4 * request->size, request->msg);
		CT_DEAD(ct, PROCESS_FAILED);
		ct_free_msg(request);
	}

	return done;
}

static void ct_incoming_request_worker_func(struct work_struct *w)
{
	struct intel_guc_ct *ct =
		container_of(w, struct intel_guc_ct, requests.worker);
	bool done;

	do {
		done = ct_process_incoming_requests(ct);
	} while (!done);
}

static int ct_handle_event(struct intel_guc_ct *ct, struct ct_incoming_msg *request)
{
	const u32 *hxg = &request->msg[GUC_CTB_MSG_MIN_LEN];
	u32 action = FIELD_GET(GUC_HXG_EVENT_MSG_0_ACTION, hxg[0]);
	unsigned long flags;

	GEM_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_TYPE, hxg[0]) != GUC_HXG_TYPE_EVENT);

	/*
	 * Adjusting the space must be done in IRQ or deadlock can occur as the
	 * CTB processing in the below workqueue can send CTBs which creates a
	 * circular dependency if the space was returned there.
	 */
	switch (action) {
	case INTEL_GUC_ACTION_SCHED_CONTEXT_MODE_DONE:
	case INTEL_GUC_ACTION_DEREGISTER_CONTEXT_DONE:
		g2h_release_space(ct, request->size);
	}

	spin_lock_irqsave(&ct->requests.lock, flags);
	list_add_tail(&request->link, &ct->requests.incoming);
	spin_unlock_irqrestore(&ct->requests.lock, flags);

	queue_work(system_unbound_wq, &ct->requests.worker);
	return 0;
}

static int ct_handle_hxg(struct intel_guc_ct *ct, struct ct_incoming_msg *msg)
{
	u32 origin, type;
	u32 *hxg;
	int err;

	if (unlikely(msg->size < GUC_CTB_HXG_MSG_MIN_LEN))
		return -EBADMSG;

	hxg = &msg->msg[GUC_CTB_MSG_MIN_LEN];

	origin = FIELD_GET(GUC_HXG_MSG_0_ORIGIN, hxg[0]);
	if (unlikely(origin != GUC_HXG_ORIGIN_GUC)) {
		err = -EPROTO;
		goto failed;
	}

	type = FIELD_GET(GUC_HXG_MSG_0_TYPE, hxg[0]);
	switch (type) {
	case GUC_HXG_TYPE_EVENT:
		err = ct_handle_event(ct, msg);
		break;
	case GUC_HXG_TYPE_RESPONSE_SUCCESS:
	case GUC_HXG_TYPE_RESPONSE_FAILURE:
	case GUC_HXG_TYPE_NO_RESPONSE_RETRY:
		err = ct_handle_response(ct, msg);
		break;
	default:
		err = -EOPNOTSUPP;
	}

	if (unlikely(err)) {
failed:
		CT_ERROR(ct, "Failed to handle HXG message (%pe) %*ph\n",
			 ERR_PTR(err), 4 * GUC_HXG_MSG_MIN_LEN, hxg);
	}
	return err;
}

static void ct_handle_msg(struct intel_guc_ct *ct, struct ct_incoming_msg *msg)
{
	u32 format = FIELD_GET(GUC_CTB_MSG_0_FORMAT, msg->msg[0]);
	int err;

	if (format == GUC_CTB_FORMAT_HXG)
		err = ct_handle_hxg(ct, msg);
	else
		err = -EOPNOTSUPP;

	if (unlikely(err)) {
		CT_ERROR(ct, "Failed to process CT message (%pe) %*ph\n",
			 ERR_PTR(err), 4 * msg->size, msg->msg);
		ct_free_msg(msg);
	}
}

/*
 * Return: number available remaining dwords to read (0 if empty)
 *         or a negative error code on failure
 */
static int ct_receive(struct intel_guc_ct *ct)
{
	struct ct_incoming_msg *msg = NULL;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&ct->ctbs.recv.lock, flags);
	ret = ct_read(ct, &msg);
	spin_unlock_irqrestore(&ct->ctbs.recv.lock, flags);
	if (ret < 0)
		return ret;

	if (msg)
		ct_handle_msg(ct, msg);

	return ret;
}

static void ct_try_receive_message(struct intel_guc_ct *ct)
{
	int ret;

	if (GEM_WARN_ON(!ct->enabled))
		return;

	ret = ct_receive(ct);
	if (ret > 0)
		tasklet_hi_schedule(&ct->receive_tasklet);
}

static void ct_receive_tasklet_func(struct tasklet_struct *t)
{
	struct intel_guc_ct *ct = from_tasklet(ct, t, receive_tasklet);

	ct_try_receive_message(ct);
}

/*
 * When we're communicating with the GuC over CT, GuC uses events
 * to notify us about new messages being posted on the RECV buffer.
 */
void intel_guc_ct_event_handler(struct intel_guc_ct *ct)
{
	if (unlikely(!ct->enabled)) {
		WARN(1, "Unexpected GuC event received while CT disabled!\n");
		return;
	}

	ct_try_receive_message(ct);
}

void intel_guc_ct_print_info(struct intel_guc_ct *ct,
			     struct drm_printer *p)
{
	drm_printf(p, "CT %s\n", str_enabled_disabled(ct->enabled));

	if (!ct->enabled)
		return;

	drm_printf(p, "H2G Space: %u\n",
		   atomic_read(&ct->ctbs.send.space) * 4);
	drm_printf(p, "Head: %u\n",
		   ct->ctbs.send.desc->head);
	drm_printf(p, "Tail: %u\n",
		   ct->ctbs.send.desc->tail);
	drm_printf(p, "G2H Space: %u\n",
		   atomic_read(&ct->ctbs.recv.space) * 4);
	drm_printf(p, "Head: %u\n",
		   ct->ctbs.recv.desc->head);
	drm_printf(p, "Tail: %u\n",
		   ct->ctbs.recv.desc->tail);
}

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GUC)
static void ct_dead_ct_worker_func(struct work_struct *w)
{
	struct intel_guc_ct *ct = container_of(w, struct intel_guc_ct, dead_ct_worker);
	struct intel_guc *guc = ct_to_guc(ct);

	if (ct->dead_ct_reported)
		return;

	ct->dead_ct_reported = true;

	guc_info(guc, "CTB is dead - reason=0x%X\n", ct->dead_ct_reason);
	intel_klog_error_capture(guc_to_gt(guc), (intel_engine_mask_t)~0U);
}
#endif
