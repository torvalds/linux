/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_ENGINE_TYPES__
#define __INTEL_ENGINE_TYPES__

#include <linux/hashtable.h>
#include <linux/irq_work.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/types.h>

#include "i915_gem.h"
#include "i915_priolist_types.h"
#include "i915_selftest.h"
#include "i915_timeline_types.h"
#include "intel_workarounds_types.h"

#include "i915_gem_batch_pool.h"
#include "i915_pmu.h"

#define I915_MAX_SLICES	3
#define I915_MAX_SUBSLICES 8

#define I915_CMD_HASH_ORDER 9

struct dma_fence;
struct drm_i915_reg_table;
struct i915_gem_context;
struct i915_request;
struct i915_sched_attr;
struct intel_uncore;

typedef u8 intel_engine_mask_t;
#define ALL_ENGINES ((intel_engine_mask_t)~0ul)

struct intel_hw_status_page {
	struct i915_vma *vma;
	u32 *addr;
};

struct intel_instdone {
	u32 instdone;
	/* The following exist only in the RCS engine */
	u32 slice_common;
	u32 sampler[I915_MAX_SLICES][I915_MAX_SUBSLICES];
	u32 row[I915_MAX_SLICES][I915_MAX_SUBSLICES];
};

struct intel_engine_hangcheck {
	u64 acthd;
	u32 last_seqno;
	u32 next_seqno;
	unsigned long action_timestamp;
	struct intel_instdone instdone;
};

struct intel_ring {
	struct kref ref;
	struct i915_vma *vma;
	void *vaddr;

	struct i915_timeline *timeline;
	struct list_head request_list;
	struct list_head active_link;

	u32 head;
	u32 tail;
	u32 emit;

	u32 space;
	u32 size;
	u32 effective_size;
};

/*
 * we use a single page to load ctx workarounds so all of these
 * values are referred in terms of dwords
 *
 * struct i915_wa_ctx_bb:
 *  offset: specifies batch starting position, also helpful in case
 *    if we want to have multiple batches at different offsets based on
 *    some criteria. It is not a requirement at the moment but provides
 *    an option for future use.
 *  size: size of the batch in DWORDS
 */
struct i915_ctx_workarounds {
	struct i915_wa_ctx_bb {
		u32 offset;
		u32 size;
	} indirect_ctx, per_ctx;
	struct i915_vma *vma;
};

#define I915_MAX_VCS	4
#define I915_MAX_VECS	2

/*
 * Engine IDs definitions.
 * Keep instances of the same type engine together.
 */
enum intel_engine_id {
	RCS0 = 0,
	BCS0,
	VCS0,
	VCS1,
	VCS2,
	VCS3,
#define _VCS(n) (VCS0 + (n))
	VECS0,
	VECS1,
#define _VECS(n) (VECS0 + (n))
	I915_NUM_ENGINES
};

struct st_preempt_hang {
	struct completion completion;
	unsigned int count;
	bool inject_hang;
};

/**
 * struct intel_engine_execlists - execlist submission queue and port state
 *
 * The struct intel_engine_execlists represents the combined logical state of
 * driver and the hardware state for execlist mode of submission.
 */
struct intel_engine_execlists {
	/**
	 * @tasklet: softirq tasklet for bottom handler
	 */
	struct tasklet_struct tasklet;

	/**
	 * @default_priolist: priority list for I915_PRIORITY_NORMAL
	 */
	struct i915_priolist default_priolist;

	/**
	 * @no_priolist: priority lists disabled
	 */
	bool no_priolist;

	/**
	 * @submit_reg: gen-specific execlist submission register
	 * set to the ExecList Submission Port (elsp) register pre-Gen11 and to
	 * the ExecList Submission Queue Contents register array for Gen11+
	 */
	u32 __iomem *submit_reg;

	/**
	 * @ctrl_reg: the enhanced execlists control register, used to load the
	 * submit queue on the HW and to request preemptions to idle
	 */
	u32 __iomem *ctrl_reg;

	/**
	 * @port: execlist port states
	 *
	 * For each hardware ELSP (ExecList Submission Port) we keep
	 * track of the last request and the number of times we submitted
	 * that port to hw. We then count the number of times the hw reports
	 * a context completion or preemption. As only one context can
	 * be active on hw, we limit resubmission of context to port[0]. This
	 * is called Lite Restore, of the context.
	 */
	struct execlist_port {
		/**
		 * @request_count: combined request and submission count
		 */
		struct i915_request *request_count;
#define EXECLIST_COUNT_BITS 2
#define port_request(p) ptr_mask_bits((p)->request_count, EXECLIST_COUNT_BITS)
#define port_count(p) ptr_unmask_bits((p)->request_count, EXECLIST_COUNT_BITS)
#define port_pack(rq, count) ptr_pack_bits(rq, count, EXECLIST_COUNT_BITS)
#define port_unpack(p, count) ptr_unpack_bits((p)->request_count, count, EXECLIST_COUNT_BITS)
#define port_set(p, packed) ((p)->request_count = (packed))
#define port_isset(p) ((p)->request_count)
#define port_index(p, execlists) ((p) - (execlists)->port)

		/**
		 * @context_id: context ID for port
		 */
		GEM_DEBUG_DECL(u32 context_id);

#define EXECLIST_MAX_PORTS 2
	} port[EXECLIST_MAX_PORTS];

	/**
	 * @active: is the HW active? We consider the HW as active after
	 * submitting any context for execution and until we have seen the
	 * last context completion event. After that, we do not expect any
	 * more events until we submit, and so can park the HW.
	 *
	 * As we have a small number of different sources from which we feed
	 * the HW, we track the state of each inside a single bitfield.
	 */
	unsigned int active;
#define EXECLISTS_ACTIVE_USER 0
#define EXECLISTS_ACTIVE_PREEMPT 1
#define EXECLISTS_ACTIVE_HWACK 2

	/**
	 * @port_mask: number of execlist ports - 1
	 */
	unsigned int port_mask;

	/**
	 * @queue_priority_hint: Highest pending priority.
	 *
	 * When we add requests into the queue, or adjust the priority of
	 * executing requests, we compute the maximum priority of those
	 * pending requests. We can then use this value to determine if
	 * we need to preempt the executing requests to service the queue.
	 * However, since the we may have recorded the priority of an inflight
	 * request we wanted to preempt but since completed, at the time of
	 * dequeuing the priority hint may no longer may match the highest
	 * available request priority.
	 */
	int queue_priority_hint;

	/**
	 * @queue: queue of requests, in priority lists
	 */
	struct rb_root_cached queue;

	/**
	 * @csb_write: control register for Context Switch buffer
	 *
	 * Note this register may be either mmio or HWSP shadow.
	 */
	u32 *csb_write;

	/**
	 * @csb_status: status array for Context Switch buffer
	 *
	 * Note these register may be either mmio or HWSP shadow.
	 */
	u32 *csb_status;

	/**
	 * @preempt_complete_status: expected CSB upon completing preemption
	 */
	u32 preempt_complete_status;

	/**
	 * @csb_head: context status buffer head
	 */
	u8 csb_head;

	I915_SELFTEST_DECLARE(struct st_preempt_hang preempt_hang;)
};

#define INTEL_ENGINE_CS_MAX_NAME 8

struct intel_engine_cs {
	struct drm_i915_private *i915;
	struct intel_uncore *uncore;
	char name[INTEL_ENGINE_CS_MAX_NAME];

	enum intel_engine_id id;
	unsigned int hw_id;
	unsigned int guc_id;
	intel_engine_mask_t mask;

	u8 uabi_class;

	u8 class;
	u8 instance;
	u32 context_size;
	u32 mmio_base;

	struct intel_ring *buffer;

	struct i915_timeline timeline;

	struct intel_context *kernel_context; /* pinned */
	struct intel_context *preempt_context; /* pinned; optional */

	struct drm_i915_gem_object *default_state;
	void *pinned_default_state;

	/* Rather than have every client wait upon all user interrupts,
	 * with the herd waking after every interrupt and each doing the
	 * heavyweight seqno dance, we delegate the task (of being the
	 * bottom-half of the user interrupt) to the first client. After
	 * every interrupt, we wake up one client, who does the heavyweight
	 * coherent seqno read and either goes back to sleep (if incomplete),
	 * or wakes up all the completed clients in parallel, before then
	 * transferring the bottom-half status to the next client in the queue.
	 *
	 * Compared to walking the entire list of waiters in a single dedicated
	 * bottom-half, we reduce the latency of the first waiter by avoiding
	 * a context switch, but incur additional coherent seqno reads when
	 * following the chain of request breadcrumbs. Since it is most likely
	 * that we have a single client waiting on each seqno, then reducing
	 * the overhead of waking that client is much preferred.
	 */
	struct intel_breadcrumbs {
		spinlock_t irq_lock;
		struct list_head signalers;

		struct irq_work irq_work; /* for use from inside irq_lock */

		unsigned int irq_enabled;

		bool irq_armed;
	} breadcrumbs;

	struct intel_engine_pmu {
		/**
		 * @enable: Bitmask of enable sample events on this engine.
		 *
		 * Bits correspond to sample event types, for instance
		 * I915_SAMPLE_QUEUED is bit 0 etc.
		 */
		u32 enable;
		/**
		 * @enable_count: Reference count for the enabled samplers.
		 *
		 * Index number corresponds to @enum drm_i915_pmu_engine_sample.
		 */
		unsigned int enable_count[I915_ENGINE_SAMPLE_COUNT];
		/**
		 * @sample: Counter values for sampling events.
		 *
		 * Our internal timer stores the current counters in this field.
		 *
		 * Index number corresponds to @enum drm_i915_pmu_engine_sample.
		 */
		struct i915_pmu_sample sample[I915_ENGINE_SAMPLE_COUNT];
	} pmu;

	/*
	 * A pool of objects to use as shadow copies of client batch buffers
	 * when the command parser is enabled. Prevents the client from
	 * modifying the batch contents after software parsing.
	 */
	struct i915_gem_batch_pool batch_pool;

	struct intel_hw_status_page status_page;
	struct i915_ctx_workarounds wa_ctx;
	struct i915_wa_list ctx_wa_list;
	struct i915_wa_list wa_list;
	struct i915_wa_list whitelist;

	u32             irq_keep_mask; /* always keep these interrupts */
	u32		irq_enable_mask; /* bitmask to enable ring interrupt */
	void		(*irq_enable)(struct intel_engine_cs *engine);
	void		(*irq_disable)(struct intel_engine_cs *engine);

	int		(*init_hw)(struct intel_engine_cs *engine);

	struct {
		void (*prepare)(struct intel_engine_cs *engine);
		void (*reset)(struct intel_engine_cs *engine, bool stalled);
		void (*finish)(struct intel_engine_cs *engine);
	} reset;

	void		(*park)(struct intel_engine_cs *engine);
	void		(*unpark)(struct intel_engine_cs *engine);

	void		(*set_default_submission)(struct intel_engine_cs *engine);

	const struct intel_context_ops *cops;

	int		(*request_alloc)(struct i915_request *rq);
	int		(*init_context)(struct i915_request *rq);

	int		(*emit_flush)(struct i915_request *request, u32 mode);
#define EMIT_INVALIDATE	BIT(0)
#define EMIT_FLUSH	BIT(1)
#define EMIT_BARRIER	(EMIT_INVALIDATE | EMIT_FLUSH)
	int		(*emit_bb_start)(struct i915_request *rq,
					 u64 offset, u32 length,
					 unsigned int dispatch_flags);
#define I915_DISPATCH_SECURE BIT(0)
#define I915_DISPATCH_PINNED BIT(1)
	int		 (*emit_init_breadcrumb)(struct i915_request *rq);
	u32		*(*emit_fini_breadcrumb)(struct i915_request *rq,
						 u32 *cs);
	unsigned int	emit_fini_breadcrumb_dw;

	/* Pass the request to the hardware queue (e.g. directly into
	 * the legacy ringbuffer or to the end of an execlist).
	 *
	 * This is called from an atomic context with irqs disabled; must
	 * be irq safe.
	 */
	void		(*submit_request)(struct i915_request *rq);

	/*
	 * Call when the priority on a request has changed and it and its
	 * dependencies may need rescheduling. Note the request itself may
	 * not be ready to run!
	 */
	void		(*schedule)(struct i915_request *request,
				    const struct i915_sched_attr *attr);

	/*
	 * Cancel all requests on the hardware, or queued for execution.
	 * This should only cancel the ready requests that have been
	 * submitted to the engine (via the engine->submit_request callback).
	 * This is called when marking the device as wedged.
	 */
	void		(*cancel_requests)(struct intel_engine_cs *engine);

	void		(*cleanup)(struct intel_engine_cs *engine);

	struct intel_engine_execlists execlists;

	/* Contexts are pinned whilst they are active on the GPU. The last
	 * context executed remains active whilst the GPU is idle - the
	 * switch away and write to the context object only occurs on the
	 * next execution.  Contexts are only unpinned on retirement of the
	 * following request ensuring that we can always write to the object
	 * on the context switch even after idling. Across suspend, we switch
	 * to the kernel context and trash it as the save may not happen
	 * before the hardware is powered down.
	 */
	struct intel_context *last_retired_context;

	/* status_notifier: list of callbacks for context-switch changes */
	struct atomic_notifier_head context_status_notifier;

	struct intel_engine_hangcheck hangcheck;

#define I915_ENGINE_NEEDS_CMD_PARSER BIT(0)
#define I915_ENGINE_SUPPORTS_STATS   BIT(1)
#define I915_ENGINE_HAS_PREEMPTION   BIT(2)
#define I915_ENGINE_HAS_SEMAPHORES   BIT(3)
#define I915_ENGINE_NEEDS_BREADCRUMB_TASKLET BIT(4)
	unsigned int flags;

	/*
	 * Table of commands the command parser needs to know about
	 * for this engine.
	 */
	DECLARE_HASHTABLE(cmd_hash, I915_CMD_HASH_ORDER);

	/*
	 * Table of registers allowed in commands that read/write registers.
	 */
	const struct drm_i915_reg_table *reg_tables;
	int reg_table_count;

	/*
	 * Returns the bitmask for the length field of the specified command.
	 * Return 0 for an unrecognized/invalid command.
	 *
	 * If the command parser finds an entry for a command in the engine's
	 * cmd_tables, it gets the command's length based on the table entry.
	 * If not, it calls this function to determine the per-engine length
	 * field encoding for the command (i.e. different opcode ranges use
	 * certain bits to encode the command length in the header).
	 */
	u32 (*get_cmd_length_mask)(u32 cmd_header);

	struct {
		/**
		 * @lock: Lock protecting the below fields.
		 */
		seqlock_t lock;
		/**
		 * @enabled: Reference count indicating number of listeners.
		 */
		unsigned int enabled;
		/**
		 * @active: Number of contexts currently scheduled in.
		 */
		unsigned int active;
		/**
		 * @enabled_at: Timestamp when busy stats were enabled.
		 */
		ktime_t enabled_at;
		/**
		 * @start: Timestamp of the last idle to active transition.
		 *
		 * Idle is defined as active == 0, active is active > 0.
		 */
		ktime_t start;
		/**
		 * @total: Total time this engine was busy.
		 *
		 * Accumulated time not counting the most recent block in cases
		 * where engine is currently busy (active > 0).
		 */
		ktime_t total;
	} stats;
};

static inline bool
intel_engine_needs_cmd_parser(const struct intel_engine_cs *engine)
{
	return engine->flags & I915_ENGINE_NEEDS_CMD_PARSER;
}

static inline bool
intel_engine_supports_stats(const struct intel_engine_cs *engine)
{
	return engine->flags & I915_ENGINE_SUPPORTS_STATS;
}

static inline bool
intel_engine_has_preemption(const struct intel_engine_cs *engine)
{
	return engine->flags & I915_ENGINE_HAS_PREEMPTION;
}

static inline bool
intel_engine_has_semaphores(const struct intel_engine_cs *engine)
{
	return engine->flags & I915_ENGINE_HAS_SEMAPHORES;
}

static inline bool
intel_engine_needs_breadcrumb_tasklet(const struct intel_engine_cs *engine)
{
	return engine->flags & I915_ENGINE_NEEDS_BREADCRUMB_TASKLET;
}

#define instdone_slice_mask(dev_priv__) \
	(IS_GEN(dev_priv__, 7) ? \
	 1 : RUNTIME_INFO(dev_priv__)->sseu.slice_mask)

#define instdone_subslice_mask(dev_priv__) \
	(IS_GEN(dev_priv__, 7) ? \
	 1 : RUNTIME_INFO(dev_priv__)->sseu.subslice_mask[0])

#define for_each_instdone_slice_subslice(dev_priv__, slice__, subslice__) \
	for ((slice__) = 0, (subslice__) = 0; \
	     (slice__) < I915_MAX_SLICES; \
	     (subslice__) = ((subslice__) + 1) < I915_MAX_SUBSLICES ? (subslice__) + 1 : 0, \
	       (slice__) += ((subslice__) == 0)) \
		for_each_if((BIT(slice__) & instdone_slice_mask(dev_priv__)) && \
			    (BIT(subslice__) & instdone_subslice_mask(dev_priv__)))

#endif /* __INTEL_ENGINE_TYPES_H__ */
