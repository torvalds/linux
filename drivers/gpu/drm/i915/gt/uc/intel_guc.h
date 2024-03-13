/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2019 Intel Corporation
 */

#ifndef _INTEL_GUC_H_
#define _INTEL_GUC_H_

#include <linux/delay.h>
#include <linux/iosys-map.h>
#include <linux/xarray.h>

#include "intel_guc_ct.h"
#include "intel_guc_fw.h"
#include "intel_guc_fwif.h"
#include "intel_guc_log.h"
#include "intel_guc_reg.h"
#include "intel_guc_slpc_types.h"
#include "intel_uc_fw.h"
#include "intel_uncore.h"
#include "i915_utils.h"
#include "i915_vma.h"

struct __guc_ads_blob;
struct intel_guc_state_capture;

/**
 * struct intel_guc - Top level structure of GuC.
 *
 * It handles firmware loading and manages client pool. intel_guc owns an
 * i915_sched_engine for submission.
 */
struct intel_guc {
	/** @fw: the GuC firmware */
	struct intel_uc_fw fw;
	/** @log: sub-structure containing GuC log related data and objects */
	struct intel_guc_log log;
	/** @ct: the command transport communication channel */
	struct intel_guc_ct ct;
	/** @slpc: sub-structure containing SLPC related data and objects */
	struct intel_guc_slpc slpc;
	/** @capture: the error-state-capture module's data and objects */
	struct intel_guc_state_capture *capture;

	/** @dbgfs_node: debugfs node */
	struct dentry *dbgfs_node;

	/** @sched_engine: Global engine used to submit requests to GuC */
	struct i915_sched_engine *sched_engine;
	/**
	 * @stalled_request: if GuC can't process a request for any reason, we
	 * save it until GuC restarts processing. No other request can be
	 * submitted until the stalled request is processed.
	 */
	struct i915_request *stalled_request;
	/**
	 * @submission_stall_reason: reason why submission is stalled
	 */
	enum {
		STALL_NONE,
		STALL_REGISTER_CONTEXT,
		STALL_MOVE_LRC_TAIL,
		STALL_ADD_REQUEST,
	} submission_stall_reason;

	/* intel_guc_recv interrupt related state */
	/** @irq_lock: protects GuC irq state */
	spinlock_t irq_lock;
	/**
	 * @msg_enabled_mask: mask of events that are processed when receiving
	 * an INTEL_GUC_ACTION_DEFAULT G2H message.
	 */
	unsigned int msg_enabled_mask;

	/**
	 * @outstanding_submission_g2h: number of outstanding GuC to Host
	 * responses related to GuC submission, used to determine if the GT is
	 * idle
	 */
	atomic_t outstanding_submission_g2h;

	/** @tlb_lookup: xarray to store all pending TLB invalidation requests */
	struct xarray tlb_lookup;

	/**
	 * @serial_slot: id to the initial waiter created in tlb_lookup,
	 * which is used only when failed to allocate new waiter.
	 */
	u32 serial_slot;

	/** @next_seqno: the next id (sequence number) to allocate. */
	u32 next_seqno;

	/** @interrupts: pointers to GuC interrupt-managing functions. */
	struct {
		bool enabled;
		void (*reset)(struct intel_guc *guc);
		void (*enable)(struct intel_guc *guc);
		void (*disable)(struct intel_guc *guc);
	} interrupts;

	/**
	 * @submission_state: sub-structure for submission state protected by
	 * single lock
	 */
	struct {
		/**
		 * @submission_state.lock: protects everything in
		 * submission_state, ce->guc_id.id, and ce->guc_id.ref
		 * when transitioning in and out of zero
		 */
		spinlock_t lock;
		/**
		 * @submission_state.guc_ids: used to allocate new
		 * guc_ids, single-lrc
		 */
		struct ida guc_ids;
		/**
		 * @submission_state.num_guc_ids: Number of guc_ids, selftest
		 * feature to be able to reduce this number while testing.
		 */
		int num_guc_ids;
		/**
		 * @submission_state.guc_ids_bitmap: used to allocate
		 * new guc_ids, multi-lrc
		 */
		unsigned long *guc_ids_bitmap;
		/**
		 * @submission_state.guc_id_list: list of intel_context
		 * with valid guc_ids but no refs
		 */
		struct list_head guc_id_list;
		/**
		 * @submission_state.guc_ids_in_use: Number single-lrc
		 * guc_ids in use
		 */
		unsigned int guc_ids_in_use;
		/**
		 * @submission_state.destroyed_contexts: list of contexts
		 * waiting to be destroyed (deregistered with the GuC)
		 */
		struct list_head destroyed_contexts;
		/**
		 * @submission_state.destroyed_worker: worker to deregister
		 * contexts, need as we need to take a GT PM reference and
		 * can't from destroy function as it might be in an atomic
		 * context (no sleeping)
		 */
		struct work_struct destroyed_worker;
		/**
		 * @submission_state.reset_fail_worker: worker to trigger
		 * a GT reset after an engine reset fails
		 */
		struct work_struct reset_fail_worker;
		/**
		 * @submission_state.reset_fail_mask: mask of engines that
		 * failed to reset
		 */
		intel_engine_mask_t reset_fail_mask;
		/**
		 * @submission_state.sched_disable_delay_ms: schedule
		 * disable delay, in ms, for contexts
		 */
		unsigned int sched_disable_delay_ms;
		/**
		 * @submission_state.sched_disable_gucid_threshold:
		 * threshold of min remaining available guc_ids before
		 * we start bypassing the schedule disable delay
		 */
		unsigned int sched_disable_gucid_threshold;
	} submission_state;

	/**
	 * @submission_supported: tracks whether we support GuC submission on
	 * the current platform
	 */
	bool submission_supported;
	/** @submission_selected: tracks whether the user enabled GuC submission */
	bool submission_selected;
	/** @submission_initialized: tracks whether GuC submission has been initialised */
	bool submission_initialized;
	/** @submission_version: Submission API version of the currently loaded firmware */
	struct intel_uc_fw_ver submission_version;

	/**
	 * @rc_supported: tracks whether we support GuC rc on the current platform
	 */
	bool rc_supported;
	/** @rc_selected: tracks whether the user enabled GuC rc */
	bool rc_selected;

	/** @ads_vma: object allocated to hold the GuC ADS */
	struct i915_vma *ads_vma;
	/** @ads_map: contents of the GuC ADS */
	struct iosys_map ads_map;
	/** @ads_regset_size: size of the save/restore regsets in the ADS */
	u32 ads_regset_size;
	/**
	 * @ads_regset_count: number of save/restore registers in the ADS for
	 * each engine
	 */
	u32 ads_regset_count[I915_NUM_ENGINES];
	/** @ads_regset: save/restore regsets in the ADS */
	struct guc_mmio_reg *ads_regset;
	/** @ads_golden_ctxt_size: size of the golden contexts in the ADS */
	u32 ads_golden_ctxt_size;
	/** @ads_capture_size: size of register lists in the ADS used for error capture */
	u32 ads_capture_size;
	/** @ads_engine_usage_size: size of engine usage in the ADS */
	u32 ads_engine_usage_size;

	/** @lrc_desc_pool_v69: object allocated to hold the GuC LRC descriptor pool */
	struct i915_vma *lrc_desc_pool_v69;
	/** @lrc_desc_pool_vaddr_v69: contents of the GuC LRC descriptor pool */
	void *lrc_desc_pool_vaddr_v69;

	/**
	 * @context_lookup: used to resolve intel_context from guc_id, if a
	 * context is present in this structure it is registered with the GuC
	 */
	struct xarray context_lookup;

	/** @params: Control params for fw initialization */
	u32 params[GUC_CTL_MAX_DWORDS];

	/** @send_regs: GuC's FW specific registers used for sending MMIO H2G */
	struct {
		u32 base;
		unsigned int count;
		enum forcewake_domains fw_domains;
	} send_regs;

	/** @notify_reg: register used to send interrupts to the GuC FW */
	i915_reg_t notify_reg;

	/**
	 * @mmio_msg: notification bitmask that the GuC writes in one of its
	 * registers when the CT channel is disabled, to be processed when the
	 * channel is back up.
	 */
	u32 mmio_msg;

	/** @send_mutex: used to serialize the intel_guc_send actions */
	struct mutex send_mutex;

	/**
	 * @timestamp: GT timestamp object that stores a copy of the timestamp
	 * and adjusts it for overflow using a worker.
	 */
	struct {
		/**
		 * @timestamp.lock: Lock protecting the below fields and
		 * the engine stats.
		 */
		spinlock_t lock;

		/**
		 * @timestamp.gt_stamp: 64-bit extended value of the GT
		 * timestamp.
		 */
		u64 gt_stamp;

		/**
		 * @timestamp.ping_delay: Period for polling the GT
		 * timestamp for overflow.
		 */
		unsigned long ping_delay;

		/**
		 * @timestamp.work: Periodic work to adjust GT timestamp,
		 * engine and context usage for overflows.
		 */
		struct delayed_work work;

		/**
		 * @timestamp.shift: Right shift value for the gpm timestamp
		 */
		u32 shift;

		/**
		 * @timestamp.last_stat_jiffies: jiffies at last actual
		 * stats collection time. We use this timestamp to ensure
		 * we don't oversample the stats because runtime power
		 * management events can trigger stats collection at much
		 * higher rates than required.
		 */
		unsigned long last_stat_jiffies;
	} timestamp;

	/**
	 * @dead_guc_worker: Asynchronous worker thread for forcing a GuC reset.
	 * Specifically used when the G2H handler wants to issue a reset. Resets
	 * require flushing the G2H queue. So, the G2H processing itself must not
	 * trigger a reset directly. Instead, go via this worker.
	 */
	struct work_struct dead_guc_worker;
	/**
	 * @last_dead_guc_jiffies: timestamp of previous 'dead guc' occurrance
	 * used to prevent a fundamentally broken system from continuously
	 * reloading the GuC.
	 */
	unsigned long last_dead_guc_jiffies;

#ifdef CONFIG_DRM_I915_SELFTEST
	/**
	 * @number_guc_id_stolen: The number of guc_ids that have been stolen
	 */
	int number_guc_id_stolen;
	/**
	 * @fast_response_selftest: Backdoor to CT handler for fast response selftest
	 */
	u32 fast_response_selftest;
#endif
};

struct intel_guc_tlb_wait {
	struct wait_queue_head wq;
	bool busy;
};

/*
 * GuC version number components are only 8-bit, so converting to a 32bit 8.8.8
 * integer works.
 */
#define MAKE_GUC_VER(maj, min, pat)	(((maj) << 16) | ((min) << 8) | (pat))
#define MAKE_GUC_VER_STRUCT(ver)	MAKE_GUC_VER((ver).major, (ver).minor, (ver).patch)
#define GUC_SUBMIT_VER(guc)		MAKE_GUC_VER_STRUCT((guc)->submission_version)
#define GUC_FIRMWARE_VER(guc)		MAKE_GUC_VER_STRUCT((guc)->fw.file_selected.ver)

static inline struct intel_guc *log_to_guc(struct intel_guc_log *log)
{
	return container_of(log, struct intel_guc, log);
}

static
inline int intel_guc_send(struct intel_guc *guc, const u32 *action, u32 len)
{
	return intel_guc_ct_send(&guc->ct, action, len, NULL, 0, 0);
}

static
inline int intel_guc_send_nb(struct intel_guc *guc, const u32 *action, u32 len,
			     u32 g2h_len_dw)
{
	return intel_guc_ct_send(&guc->ct, action, len, NULL, 0,
				 MAKE_SEND_FLAGS(g2h_len_dw));
}

static inline int
intel_guc_send_and_receive(struct intel_guc *guc, const u32 *action, u32 len,
			   u32 *response_buf, u32 response_buf_size)
{
	return intel_guc_ct_send(&guc->ct, action, len,
				 response_buf, response_buf_size, 0);
}

static inline int intel_guc_send_busy_loop(struct intel_guc *guc,
					   const u32 *action,
					   u32 len,
					   u32 g2h_len_dw,
					   bool loop)
{
	int err;
	unsigned int sleep_period_ms = 1;
	bool not_atomic = !in_atomic() && !irqs_disabled();

	/*
	 * FIXME: Have caller pass in if we are in an atomic context to avoid
	 * using in_atomic(). It is likely safe here as we check for irqs
	 * disabled which basically all the spin locks in the i915 do but
	 * regardless this should be cleaned up.
	 */

	/* No sleeping with spin locks, just busy loop */
	might_sleep_if(loop && not_atomic);

retry:
	err = intel_guc_send_nb(guc, action, len, g2h_len_dw);
	if (unlikely(err == -EBUSY && loop)) {
		if (likely(not_atomic)) {
			if (msleep_interruptible(sleep_period_ms))
				return -EINTR;
			sleep_period_ms = sleep_period_ms << 1;
		} else {
			cpu_relax();
		}
		goto retry;
	}

	return err;
}

/* Only call this from the interrupt handler code */
static inline void intel_guc_to_host_event_handler(struct intel_guc *guc)
{
	if (guc->interrupts.enabled)
		intel_guc_ct_event_handler(&guc->ct);
}

/* GuC addresses above GUC_GGTT_TOP also don't map through the GTT */
#define GUC_GGTT_TOP	0xFEE00000

/**
 * intel_guc_ggtt_offset() - Get and validate the GGTT offset of @vma
 * @guc: intel_guc structure.
 * @vma: i915 graphics virtual memory area.
 *
 * GuC does not allow any gfx GGTT address that falls into range
 * [0, ggtt.pin_bias), which is reserved for Boot ROM, SRAM and WOPCM.
 * Currently, in order to exclude [0, ggtt.pin_bias) address space from
 * GGTT, all gfx objects used by GuC are allocated with intel_guc_allocate_vma()
 * and pinned with PIN_OFFSET_BIAS along with the value of ggtt.pin_bias.
 *
 * Return: GGTT offset of the @vma.
 */
static inline u32 intel_guc_ggtt_offset(struct intel_guc *guc,
					struct i915_vma *vma)
{
	u32 offset = i915_ggtt_offset(vma);

	GEM_BUG_ON(offset < i915_ggtt_pin_bias(vma));
	GEM_BUG_ON(range_overflows_t(u64, offset, vma->size, GUC_GGTT_TOP));

	return offset;
}

void intel_guc_init_early(struct intel_guc *guc);
void intel_guc_init_late(struct intel_guc *guc);
void intel_guc_init_send_regs(struct intel_guc *guc);
void intel_guc_write_params(struct intel_guc *guc);
int intel_guc_init(struct intel_guc *guc);
void intel_guc_fini(struct intel_guc *guc);
void intel_guc_notify(struct intel_guc *guc);
int intel_guc_send_mmio(struct intel_guc *guc, const u32 *action, u32 len,
			u32 *response_buf, u32 response_buf_size);
int intel_guc_to_host_process_recv_msg(struct intel_guc *guc,
				       const u32 *payload, u32 len);
int intel_guc_auth_huc(struct intel_guc *guc, u32 rsa_offset);
int intel_guc_suspend(struct intel_guc *guc);
int intel_guc_resume(struct intel_guc *guc);
struct i915_vma *intel_guc_allocate_vma(struct intel_guc *guc, u32 size);
int intel_guc_allocate_and_map_vma(struct intel_guc *guc, u32 size,
				   struct i915_vma **out_vma, void **out_vaddr);
int intel_guc_self_cfg32(struct intel_guc *guc, u16 key, u32 value);
int intel_guc_self_cfg64(struct intel_guc *guc, u16 key, u64 value);

static inline bool intel_guc_is_supported(struct intel_guc *guc)
{
	return intel_uc_fw_is_supported(&guc->fw);
}

static inline bool intel_guc_is_wanted(struct intel_guc *guc)
{
	return intel_uc_fw_is_enabled(&guc->fw);
}

static inline bool intel_guc_is_used(struct intel_guc *guc)
{
	GEM_BUG_ON(__intel_uc_fw_status(&guc->fw) == INTEL_UC_FIRMWARE_SELECTED);
	return intel_uc_fw_is_available(&guc->fw);
}

static inline bool intel_guc_is_fw_running(struct intel_guc *guc)
{
	return intel_uc_fw_is_running(&guc->fw);
}

static inline bool intel_guc_is_ready(struct intel_guc *guc)
{
	return intel_guc_is_fw_running(guc) && intel_guc_ct_enabled(&guc->ct);
}

static inline void intel_guc_reset_interrupts(struct intel_guc *guc)
{
	guc->interrupts.reset(guc);
}

static inline void intel_guc_enable_interrupts(struct intel_guc *guc)
{
	guc->interrupts.enable(guc);
}

static inline void intel_guc_disable_interrupts(struct intel_guc *guc)
{
	guc->interrupts.disable(guc);
}

static inline int intel_guc_sanitize(struct intel_guc *guc)
{
	intel_uc_fw_sanitize(&guc->fw);
	intel_guc_disable_interrupts(guc);
	intel_guc_ct_sanitize(&guc->ct);
	guc->mmio_msg = 0;

	return 0;
}

static inline void intel_guc_enable_msg(struct intel_guc *guc, u32 mask)
{
	spin_lock_irq(&guc->irq_lock);
	guc->msg_enabled_mask |= mask;
	spin_unlock_irq(&guc->irq_lock);
}

static inline void intel_guc_disable_msg(struct intel_guc *guc, u32 mask)
{
	spin_lock_irq(&guc->irq_lock);
	guc->msg_enabled_mask &= ~mask;
	spin_unlock_irq(&guc->irq_lock);
}

int intel_guc_wait_for_idle(struct intel_guc *guc, long timeout);

int intel_guc_deregister_done_process_msg(struct intel_guc *guc,
					  const u32 *msg, u32 len);
int intel_guc_sched_done_process_msg(struct intel_guc *guc,
				     const u32 *msg, u32 len);
int intel_guc_context_reset_process_msg(struct intel_guc *guc,
					const u32 *msg, u32 len);
int intel_guc_engine_failure_process_msg(struct intel_guc *guc,
					 const u32 *msg, u32 len);
int intel_guc_error_capture_process_msg(struct intel_guc *guc,
					const u32 *msg, u32 len);
int intel_guc_crash_process_msg(struct intel_guc *guc, u32 action);

struct intel_engine_cs *
intel_guc_lookup_engine(struct intel_guc *guc, u8 guc_class, u8 instance);

void intel_guc_find_hung_context(struct intel_engine_cs *engine);

int intel_guc_global_policies_update(struct intel_guc *guc);

void intel_guc_context_ban(struct intel_context *ce, struct i915_request *rq);

void intel_guc_submission_reset_prepare(struct intel_guc *guc);
void intel_guc_submission_reset(struct intel_guc *guc, intel_engine_mask_t stalled);
void intel_guc_submission_reset_finish(struct intel_guc *guc);
void intel_guc_submission_cancel_requests(struct intel_guc *guc);

void intel_guc_load_status(struct intel_guc *guc, struct drm_printer *p);

void intel_guc_write_barrier(struct intel_guc *guc);

void intel_guc_dump_time_info(struct intel_guc *guc, struct drm_printer *p);

int intel_guc_sched_disable_gucid_threshold_max(struct intel_guc *guc);

bool intel_guc_tlb_invalidation_is_available(struct intel_guc *guc);
int intel_guc_invalidate_tlb_engines(struct intel_guc *guc);
int intel_guc_invalidate_tlb_guc(struct intel_guc *guc);
int intel_guc_tlb_invalidation_done(struct intel_guc *guc,
				    const u32 *payload, u32 len);
void wake_up_all_tlb_invalidate(struct intel_guc *guc);
#endif
