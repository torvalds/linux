/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _INTEL_RINGBUFFER_H_
#define _INTEL_RINGBUFFER_H_

#include <drm/drm_util.h>

#include <linux/hashtable.h>
#include <linux/seqlock.h>

#include "i915_gem_batch_pool.h"

#include "i915_reg.h"
#include "i915_pmu.h"
#include "i915_request.h"
#include "i915_selftest.h"
#include "i915_timeline.h"
#include "intel_gpu_commands.h"

struct drm_printer;
struct i915_sched_attr;

#define I915_CMD_HASH_ORDER 9

/* Early gen2 devices have a cacheline of just 32 bytes, using 64 is overkill,
 * but keeps the logic simple. Indeed, the whole purpose of this macro is just
 * to give some inclination as to some of the magic values used in the various
 * workarounds!
 */
#define CACHELINE_BYTES 64
#define CACHELINE_DWORDS (CACHELINE_BYTES / sizeof(uint32_t))

struct intel_hw_status_page {
	struct i915_vma *vma;
	u32 *page_addr;
	u32 ggtt_offset;
};

#define I915_READ_TAIL(engine) I915_READ(RING_TAIL((engine)->mmio_base))
#define I915_WRITE_TAIL(engine, val) I915_WRITE(RING_TAIL((engine)->mmio_base), val)

#define I915_READ_START(engine) I915_READ(RING_START((engine)->mmio_base))
#define I915_WRITE_START(engine, val) I915_WRITE(RING_START((engine)->mmio_base), val)

#define I915_READ_HEAD(engine)  I915_READ(RING_HEAD((engine)->mmio_base))
#define I915_WRITE_HEAD(engine, val) I915_WRITE(RING_HEAD((engine)->mmio_base), val)

#define I915_READ_CTL(engine) I915_READ(RING_CTL((engine)->mmio_base))
#define I915_WRITE_CTL(engine, val) I915_WRITE(RING_CTL((engine)->mmio_base), val)

#define I915_READ_IMR(engine) I915_READ(RING_IMR((engine)->mmio_base))
#define I915_WRITE_IMR(engine, val) I915_WRITE(RING_IMR((engine)->mmio_base), val)

#define I915_READ_MODE(engine) I915_READ(RING_MI_MODE((engine)->mmio_base))
#define I915_WRITE_MODE(engine, val) I915_WRITE(RING_MI_MODE((engine)->mmio_base), val)

/* seqno size is actually only a uint32, but since we plan to use MI_FLUSH_DW to
 * do the writes, and that must have qw aligned offsets, simply pretend it's 8b.
 */
enum intel_engine_hangcheck_action {
	ENGINE_IDLE = 0,
	ENGINE_WAIT,
	ENGINE_ACTIVE_SEQNO,
	ENGINE_ACTIVE_HEAD,
	ENGINE_ACTIVE_SUBUNITS,
	ENGINE_WAIT_KICK,
	ENGINE_DEAD,
};

static inline const char *
hangcheck_action_to_str(const enum intel_engine_hangcheck_action a)
{
	switch (a) {
	case ENGINE_IDLE:
		return "idle";
	case ENGINE_WAIT:
		return "wait";
	case ENGINE_ACTIVE_SEQNO:
		return "active seqno";
	case ENGINE_ACTIVE_HEAD:
		return "active head";
	case ENGINE_ACTIVE_SUBUNITS:
		return "active subunits";
	case ENGINE_WAIT_KICK:
		return "wait kick";
	case ENGINE_DEAD:
		return "dead";
	}

	return "unknown";
}

#define I915_MAX_SLICES	3
#define I915_MAX_SUBSLICES 8

#define instdone_slice_mask(dev_priv__) \
	(INTEL_GEN(dev_priv__) == 7 ? \
	 1 : INTEL_INFO(dev_priv__)->sseu.slice_mask)

#define instdone_subslice_mask(dev_priv__) \
	(INTEL_GEN(dev_priv__) == 7 ? \
	 1 : INTEL_INFO(dev_priv__)->sseu.subslice_mask[0])

#define for_each_instdone_slice_subslice(dev_priv__, slice__, subslice__) \
	for ((slice__) = 0, (subslice__) = 0; \
	     (slice__) < I915_MAX_SLICES; \
	     (subslice__) = ((subslice__) + 1) < I915_MAX_SUBSLICES ? (subslice__) + 1 : 0, \
	       (slice__) += ((subslice__) == 0)) \
		for_each_if((BIT(slice__) & instdone_slice_mask(dev_priv__)) && \
			    (BIT(subslice__) & instdone_subslice_mask(dev_priv__)))

struct intel_instdone {
	u32 instdone;
	/* The following exist only in the RCS engine */
	u32 slice_common;
	u32 sampler[I915_MAX_SLICES][I915_MAX_SUBSLICES];
	u32 row[I915_MAX_SLICES][I915_MAX_SUBSLICES];
};

struct intel_engine_hangcheck {
	u64 acthd;
	u32 seqno;
	enum intel_engine_hangcheck_action action;
	unsigned long action_timestamp;
	int deadlock;
	struct intel_instdone instdone;
	struct i915_request *active_request;
	bool stalled:1;
	bool wedged:1;
};

struct intel_ring {
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

struct i915_gem_context;
struct drm_i915_reg_table;

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

struct i915_request;

#define I915_MAX_VCS	4
#define I915_MAX_VECS	2

/*
 * Engine IDs definitions.
 * Keep instances of the same type engine together.
 */
enum intel_engine_id {
	RCS = 0,
	BCS,
	VCS,
	VCS2,
	VCS3,
	VCS4,
#define _VCS(n) (VCS + (n))
	VECS,
	VECS2
#define _VECS(n) (VECS + (n))
};

struct i915_priolist {
	struct rb_node node;
	struct list_head requests;
	int priority;
};

struct st_preempt_hang {
	struct completion completion;
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
	 * @queue_priority: Highest pending priority.
	 *
	 * When we add requests into the queue, or adjust the priority of
	 * executing requests, we compute the maximum priority of those
	 * pending requests. We can then use this value to determine if
	 * we need to preempt the executing requests to service the queue.
	 */
	int queue_priority;

	/**
	 * @queue: queue of requests, in priority lists
	 */
	struct rb_root_cached queue;

	/**
	 * @csb_read: control register for Context Switch buffer
	 *
	 * Note this register is always in mmio.
	 */
	u32 __iomem *csb_read;

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
	 * @csb_write_reset: reset value for CSB write pointer
	 *
	 * As the CSB write pointer maybe either in HWSP or as a field
	 * inside an mmio register, we want to reprogram it slightly
	 * differently to avoid later confusion.
	 */
	u32 csb_write_reset;

	/**
	 * @csb_head: context status buffer head
	 */
	u8 csb_head;

	I915_SELFTEST_DECLARE(struct st_preempt_hang preempt_hang;)
};

#define INTEL_ENGINE_CS_MAX_NAME 8

struct intel_engine_cs {
	struct drm_i915_private *i915;
	char name[INTEL_ENGINE_CS_MAX_NAME];

	enum intel_engine_id id;
	unsigned int hw_id;
	unsigned int guc_id;

	u8 uabi_id;
	u8 uabi_class;

	u8 class;
	u8 instance;
	u32 context_size;
	u32 mmio_base;

	struct intel_ring *buffer;

	struct i915_timeline timeline;

	struct drm_i915_gem_object *default_state;
	void *pinned_default_state;

	unsigned long irq_posted;
#define ENGINE_IRQ_BREADCRUMB 0

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
		spinlock_t irq_lock; /* protects irq_*; irqsafe */
		struct intel_wait *irq_wait; /* oldest waiter by retirement */

		spinlock_t rb_lock; /* protects the rb and wraps irq_lock */
		struct rb_root waiters; /* sorted by retirement, priority */
		struct list_head signals; /* sorted by retirement */
		struct task_struct *signaler; /* used for fence signalling */

		struct timer_list fake_irq; /* used after a missed interrupt */
		struct timer_list hangcheck; /* detect missed interrupts */

		unsigned int hangcheck_interrupts;
		unsigned int irq_enabled;
		unsigned int irq_count;

		bool irq_armed : 1;
		I915_SELFTEST_DECLARE(bool mock : 1);
	} breadcrumbs;

	struct {
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
		 * Index number corresponds to the bit number from @enable.
		 */
		unsigned int enable_count[I915_PMU_SAMPLE_BITS];
		/**
		 * @sample: Counter values for sampling events.
		 *
		 * Our internal timer stores the current counters in this field.
		 */
#define I915_ENGINE_SAMPLE_MAX (I915_SAMPLE_SEMA + 1)
		struct i915_pmu_sample sample[I915_ENGINE_SAMPLE_MAX];
	} pmu;

	/*
	 * A pool of objects to use as shadow copies of client batch buffers
	 * when the command parser is enabled. Prevents the client from
	 * modifying the batch contents after software parsing.
	 */
	struct i915_gem_batch_pool batch_pool;

	struct intel_hw_status_page status_page;
	struct i915_ctx_workarounds wa_ctx;
	struct i915_vma *scratch;

	u32             irq_keep_mask; /* always keep these interrupts */
	u32		irq_enable_mask; /* bitmask to enable ring interrupt */
	void		(*irq_enable)(struct intel_engine_cs *engine);
	void		(*irq_disable)(struct intel_engine_cs *engine);

	int		(*init_hw)(struct intel_engine_cs *engine);

	struct {
		struct i915_request *(*prepare)(struct intel_engine_cs *engine);
		void (*reset)(struct intel_engine_cs *engine,
			      struct i915_request *rq);
		void (*finish)(struct intel_engine_cs *engine);
	} reset;

	void		(*park)(struct intel_engine_cs *engine);
	void		(*unpark)(struct intel_engine_cs *engine);

	void		(*set_default_submission)(struct intel_engine_cs *engine);

	struct intel_context *(*context_pin)(struct intel_engine_cs *engine,
					     struct i915_gem_context *ctx);

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
	void		(*emit_breadcrumb)(struct i915_request *rq, u32 *cs);
	int		emit_breadcrumb_sz;

	/* Pass the request to the hardware queue (e.g. directly into
	 * the legacy ringbuffer or to the end of an execlist).
	 *
	 * This is called from an atomic context with irqs disabled; must
	 * be irq safe.
	 */
	void		(*submit_request)(struct i915_request *rq);

	/* Call when the priority on a request has changed and it and its
	 * dependencies may need rescheduling. Note the request itself may
	 * not be ready to run!
	 *
	 * Called under the struct_mutex.
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

	/* Some chipsets are not quite as coherent as advertised and need
	 * an expensive kick to force a true read of the up-to-date seqno.
	 * However, the up-to-date seqno is not always required and the last
	 * seen value is good enough. Note that the seqno will always be
	 * monotonic, even if not coherent.
	 */
	void		(*irq_seqno_barrier)(struct intel_engine_cs *engine);
	void		(*cleanup)(struct intel_engine_cs *engine);

	/* GEN8 signal/wait table - never trust comments!
	 *	  signal to	signal to    signal to   signal to      signal to
	 *	    RCS		   VCS          BCS        VECS		 VCS2
	 *      --------------------------------------------------------------------
	 *  RCS | NOP (0x00) | VCS (0x08) | BCS (0x10) | VECS (0x18) | VCS2 (0x20) |
	 *	|-------------------------------------------------------------------
	 *  VCS | RCS (0x28) | NOP (0x30) | BCS (0x38) | VECS (0x40) | VCS2 (0x48) |
	 *	|-------------------------------------------------------------------
	 *  BCS | RCS (0x50) | VCS (0x58) | NOP (0x60) | VECS (0x68) | VCS2 (0x70) |
	 *	|-------------------------------------------------------------------
	 * VECS | RCS (0x78) | VCS (0x80) | BCS (0x88) |  NOP (0x90) | VCS2 (0x98) |
	 *	|-------------------------------------------------------------------
	 * VCS2 | RCS (0xa0) | VCS (0xa8) | BCS (0xb0) | VECS (0xb8) | NOP  (0xc0) |
	 *	|-------------------------------------------------------------------
	 *
	 * Generalization:
	 *  f(x, y) := (x->id * NUM_RINGS * seqno_size) + (seqno_size * y->id)
	 *  ie. transpose of g(x, y)
	 *
	 *	 sync from	sync from    sync from    sync from	sync from
	 *	    RCS		   VCS          BCS        VECS		 VCS2
	 *      --------------------------------------------------------------------
	 *  RCS | NOP (0x00) | VCS (0x28) | BCS (0x50) | VECS (0x78) | VCS2 (0xa0) |
	 *	|-------------------------------------------------------------------
	 *  VCS | RCS (0x08) | NOP (0x30) | BCS (0x58) | VECS (0x80) | VCS2 (0xa8) |
	 *	|-------------------------------------------------------------------
	 *  BCS | RCS (0x10) | VCS (0x38) | NOP (0x60) | VECS (0x88) | VCS2 (0xb0) |
	 *	|-------------------------------------------------------------------
	 * VECS | RCS (0x18) | VCS (0x40) | BCS (0x68) |  NOP (0x90) | VCS2 (0xb8) |
	 *	|-------------------------------------------------------------------
	 * VCS2 | RCS (0x20) | VCS (0x48) | BCS (0x70) | VECS (0x98) |  NOP (0xc0) |
	 *	|-------------------------------------------------------------------
	 *
	 * Generalization:
	 *  g(x, y) := (y->id * NUM_RINGS * seqno_size) + (seqno_size * x->id)
	 *  ie. transpose of f(x, y)
	 */
	struct {
#define GEN6_SEMAPHORE_LAST	VECS_HW
#define GEN6_NUM_SEMAPHORES	(GEN6_SEMAPHORE_LAST + 1)
#define GEN6_SEMAPHORES_MASK	GENMASK(GEN6_SEMAPHORE_LAST, 0)
		struct {
			/* our mbox written by others */
			u32		wait[GEN6_NUM_SEMAPHORES];
			/* mboxes this ring signals to */
			i915_reg_t	signal[GEN6_NUM_SEMAPHORES];
		} mbox;

		/* AKA wait() */
		int	(*sync_to)(struct i915_request *rq,
				   struct i915_request *signal);
		u32	*(*signal)(struct i915_request *rq, u32 *cs);
	} semaphore;

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

static inline bool __execlists_need_preempt(int prio, int last)
{
	return prio > max(0, last);
}

static inline void
execlists_set_active(struct intel_engine_execlists *execlists,
		     unsigned int bit)
{
	__set_bit(bit, (unsigned long *)&execlists->active);
}

static inline bool
execlists_set_active_once(struct intel_engine_execlists *execlists,
			  unsigned int bit)
{
	return !__test_and_set_bit(bit, (unsigned long *)&execlists->active);
}

static inline void
execlists_clear_active(struct intel_engine_execlists *execlists,
		       unsigned int bit)
{
	__clear_bit(bit, (unsigned long *)&execlists->active);
}

static inline void
execlists_clear_all_active(struct intel_engine_execlists *execlists)
{
	execlists->active = 0;
}

static inline bool
execlists_is_active(const struct intel_engine_execlists *execlists,
		    unsigned int bit)
{
	return test_bit(bit, (unsigned long *)&execlists->active);
}

void execlists_user_begin(struct intel_engine_execlists *execlists,
			  const struct execlist_port *port);
void execlists_user_end(struct intel_engine_execlists *execlists);

void
execlists_cancel_port_requests(struct intel_engine_execlists * const execlists);

void
execlists_unwind_incomplete_requests(struct intel_engine_execlists *execlists);

static inline unsigned int
execlists_num_ports(const struct intel_engine_execlists * const execlists)
{
	return execlists->port_mask + 1;
}

static inline struct execlist_port *
execlists_port_complete(struct intel_engine_execlists * const execlists,
			struct execlist_port * const port)
{
	const unsigned int m = execlists->port_mask;

	GEM_BUG_ON(port_index(port, execlists) != 0);
	GEM_BUG_ON(!execlists_is_active(execlists, EXECLISTS_ACTIVE_USER));

	memmove(port, port + 1, m * sizeof(struct execlist_port));
	memset(port + m, 0, sizeof(struct execlist_port));

	return port;
}

static inline unsigned int
intel_engine_flag(const struct intel_engine_cs *engine)
{
	return BIT(engine->id);
}

static inline u32
intel_read_status_page(const struct intel_engine_cs *engine, int reg)
{
	/* Ensure that the compiler doesn't optimize away the load. */
	return READ_ONCE(engine->status_page.page_addr[reg]);
}

static inline void
intel_write_status_page(struct intel_engine_cs *engine, int reg, u32 value)
{
	/* Writing into the status page should be done sparingly. Since
	 * we do when we are uncertain of the device state, we take a bit
	 * of extra paranoia to try and ensure that the HWS takes the value
	 * we give and that it doesn't end up trapped inside the CPU!
	 */
	if (static_cpu_has(X86_FEATURE_CLFLUSH)) {
		mb();
		clflush(&engine->status_page.page_addr[reg]);
		engine->status_page.page_addr[reg] = value;
		clflush(&engine->status_page.page_addr[reg]);
		mb();
	} else {
		WRITE_ONCE(engine->status_page.page_addr[reg], value);
	}
}

/*
 * Reads a dword out of the status page, which is written to from the command
 * queue by automatic updates, MI_REPORT_HEAD, MI_STORE_DATA_INDEX, or
 * MI_STORE_DATA_IMM.
 *
 * The following dwords have a reserved meaning:
 * 0x00: ISR copy, updated when an ISR bit not set in the HWSTAM changes.
 * 0x04: ring 0 head pointer
 * 0x05: ring 1 head pointer (915-class)
 * 0x06: ring 2 head pointer (915-class)
 * 0x10-0x1b: Context status DWords (GM45)
 * 0x1f: Last written status offset. (GM45)
 * 0x20-0x2f: Reserved (Gen6+)
 *
 * The area from dword 0x30 to 0x3ff is available for driver usage.
 */
#define I915_GEM_HWS_INDEX		0x30
#define I915_GEM_HWS_INDEX_ADDR (I915_GEM_HWS_INDEX << MI_STORE_DWORD_INDEX_SHIFT)
#define I915_GEM_HWS_PREEMPT_INDEX	0x32
#define I915_GEM_HWS_PREEMPT_ADDR (I915_GEM_HWS_PREEMPT_INDEX << MI_STORE_DWORD_INDEX_SHIFT)
#define I915_GEM_HWS_SCRATCH_INDEX	0x40
#define I915_GEM_HWS_SCRATCH_ADDR (I915_GEM_HWS_SCRATCH_INDEX << MI_STORE_DWORD_INDEX_SHIFT)

#define I915_HWS_CSB_BUF0_INDEX		0x10
#define I915_HWS_CSB_WRITE_INDEX	0x1f
#define CNL_HWS_CSB_WRITE_INDEX		0x2f

struct intel_ring *
intel_engine_create_ring(struct intel_engine_cs *engine,
			 struct i915_timeline *timeline,
			 int size);
int intel_ring_pin(struct intel_ring *ring);
void intel_ring_reset(struct intel_ring *ring, u32 tail);
unsigned int intel_ring_update_space(struct intel_ring *ring);
void intel_ring_unpin(struct intel_ring *ring);
void intel_ring_free(struct intel_ring *ring);

void intel_engine_stop(struct intel_engine_cs *engine);
void intel_engine_cleanup(struct intel_engine_cs *engine);

void intel_legacy_submission_resume(struct drm_i915_private *dev_priv);

int __must_check intel_ring_cacheline_align(struct i915_request *rq);

int intel_ring_wait_for_space(struct intel_ring *ring, unsigned int bytes);
u32 __must_check *intel_ring_begin(struct i915_request *rq, unsigned int n);

static inline void intel_ring_advance(struct i915_request *rq, u32 *cs)
{
	/* Dummy function.
	 *
	 * This serves as a placeholder in the code so that the reader
	 * can compare against the preceding intel_ring_begin() and
	 * check that the number of dwords emitted matches the space
	 * reserved for the command packet (i.e. the value passed to
	 * intel_ring_begin()).
	 */
	GEM_BUG_ON((rq->ring->vaddr + rq->ring->emit) != cs);
}

static inline u32 intel_ring_wrap(const struct intel_ring *ring, u32 pos)
{
	return pos & (ring->size - 1);
}

static inline bool
intel_ring_offset_valid(const struct intel_ring *ring,
			unsigned int pos)
{
	if (pos & -ring->size) /* must be strictly within the ring */
		return false;

	if (!IS_ALIGNED(pos, 8)) /* must be qword aligned */
		return false;

	return true;
}

static inline u32 intel_ring_offset(const struct i915_request *rq, void *addr)
{
	/* Don't write ring->size (equivalent to 0) as that hangs some GPUs. */
	u32 offset = addr - rq->ring->vaddr;
	GEM_BUG_ON(offset > rq->ring->size);
	return intel_ring_wrap(rq->ring, offset);
}

static inline void
assert_ring_tail_valid(const struct intel_ring *ring, unsigned int tail)
{
	GEM_BUG_ON(!intel_ring_offset_valid(ring, tail));

	/*
	 * "Ring Buffer Use"
	 *	Gen2 BSpec "1. Programming Environment" / 1.4.4.6
	 *	Gen3 BSpec "1c Memory Interface Functions" / 2.3.4.5
	 *	Gen4+ BSpec "1c Memory Interface and Command Stream" / 5.3.4.5
	 * "If the Ring Buffer Head Pointer and the Tail Pointer are on the
	 * same cacheline, the Head Pointer must not be greater than the Tail
	 * Pointer."
	 *
	 * We use ring->head as the last known location of the actual RING_HEAD,
	 * it may have advanced but in the worst case it is equally the same
	 * as ring->head and so we should never program RING_TAIL to advance
	 * into the same cacheline as ring->head.
	 */
#define cacheline(a) round_down(a, CACHELINE_BYTES)
	GEM_BUG_ON(cacheline(tail) == cacheline(ring->head) &&
		   tail < ring->head);
#undef cacheline
}

static inline unsigned int
intel_ring_set_tail(struct intel_ring *ring, unsigned int tail)
{
	/* Whilst writes to the tail are strictly order, there is no
	 * serialisation between readers and the writers. The tail may be
	 * read by i915_request_retire() just as it is being updated
	 * by execlists, as although the breadcrumb is complete, the context
	 * switch hasn't been seen.
	 */
	assert_ring_tail_valid(ring, tail);
	ring->tail = tail;
	return tail;
}

void intel_engine_init_global_seqno(struct intel_engine_cs *engine, u32 seqno);

void intel_engine_setup_common(struct intel_engine_cs *engine);
int intel_engine_init_common(struct intel_engine_cs *engine);
void intel_engine_cleanup_common(struct intel_engine_cs *engine);

int intel_engine_create_scratch(struct intel_engine_cs *engine,
				unsigned int size);
void intel_engine_cleanup_scratch(struct intel_engine_cs *engine);

int intel_init_render_ring_buffer(struct intel_engine_cs *engine);
int intel_init_bsd_ring_buffer(struct intel_engine_cs *engine);
int intel_init_blt_ring_buffer(struct intel_engine_cs *engine);
int intel_init_vebox_ring_buffer(struct intel_engine_cs *engine);

int intel_engine_stop_cs(struct intel_engine_cs *engine);
void intel_engine_cancel_stop_cs(struct intel_engine_cs *engine);

u64 intel_engine_get_active_head(const struct intel_engine_cs *engine);
u64 intel_engine_get_last_batch_head(const struct intel_engine_cs *engine);

static inline u32 intel_engine_last_submit(struct intel_engine_cs *engine)
{
	/*
	 * We are only peeking at the tail of the submit queue (and not the
	 * queue itself) in order to gain a hint as to the current active
	 * state of the engine. Callers are not expected to be taking
	 * engine->timeline->lock, nor are they expected to be concerned
	 * wtih serialising this hint with anything, so document it as
	 * a hint and nothing more.
	 */
	return READ_ONCE(engine->timeline.seqno);
}

static inline u32 intel_engine_get_seqno(struct intel_engine_cs *engine)
{
	return intel_read_status_page(engine, I915_GEM_HWS_INDEX);
}

static inline bool intel_engine_signaled(struct intel_engine_cs *engine,
					 u32 seqno)
{
	return i915_seqno_passed(intel_engine_get_seqno(engine), seqno);
}

static inline bool intel_engine_has_completed(struct intel_engine_cs *engine,
					      u32 seqno)
{
	GEM_BUG_ON(!seqno);
	return intel_engine_signaled(engine, seqno);
}

static inline bool intel_engine_has_started(struct intel_engine_cs *engine,
					    u32 seqno)
{
	GEM_BUG_ON(!seqno);
	return intel_engine_signaled(engine, seqno - 1);
}

void intel_engine_get_instdone(struct intel_engine_cs *engine,
			       struct intel_instdone *instdone);

/*
 * Arbitrary size for largest possible 'add request' sequence. The code paths
 * are complex and variable. Empirical measurement shows that the worst case
 * is BDW at 192 bytes (6 + 6 + 36 dwords), then ILK at 136 bytes. However,
 * we need to allocate double the largest single packet within that emission
 * to account for tail wraparound (so 6 + 6 + 72 dwords for BDW).
 */
#define MIN_SPACE_FOR_ADD_REQUEST 336

static inline u32 intel_hws_seqno_address(struct intel_engine_cs *engine)
{
	return engine->status_page.ggtt_offset + I915_GEM_HWS_INDEX_ADDR;
}

static inline u32 intel_hws_preempt_done_address(struct intel_engine_cs *engine)
{
	return engine->status_page.ggtt_offset + I915_GEM_HWS_PREEMPT_ADDR;
}

/* intel_breadcrumbs.c -- user interrupt bottom-half for waiters */
int intel_engine_init_breadcrumbs(struct intel_engine_cs *engine);

static inline void intel_wait_init(struct intel_wait *wait)
{
	wait->tsk = current;
	wait->request = NULL;
}

static inline void intel_wait_init_for_seqno(struct intel_wait *wait, u32 seqno)
{
	wait->tsk = current;
	wait->seqno = seqno;
}

static inline bool intel_wait_has_seqno(const struct intel_wait *wait)
{
	return wait->seqno;
}

static inline bool
intel_wait_update_seqno(struct intel_wait *wait, u32 seqno)
{
	wait->seqno = seqno;
	return intel_wait_has_seqno(wait);
}

static inline bool
intel_wait_update_request(struct intel_wait *wait,
			  const struct i915_request *rq)
{
	return intel_wait_update_seqno(wait, i915_request_global_seqno(rq));
}

static inline bool
intel_wait_check_seqno(const struct intel_wait *wait, u32 seqno)
{
	return wait->seqno == seqno;
}

static inline bool
intel_wait_check_request(const struct intel_wait *wait,
			 const struct i915_request *rq)
{
	return intel_wait_check_seqno(wait, i915_request_global_seqno(rq));
}

static inline bool intel_wait_complete(const struct intel_wait *wait)
{
	return RB_EMPTY_NODE(&wait->node);
}

bool intel_engine_add_wait(struct intel_engine_cs *engine,
			   struct intel_wait *wait);
void intel_engine_remove_wait(struct intel_engine_cs *engine,
			      struct intel_wait *wait);
bool intel_engine_enable_signaling(struct i915_request *request, bool wakeup);
void intel_engine_cancel_signaling(struct i915_request *request);

static inline bool intel_engine_has_waiter(const struct intel_engine_cs *engine)
{
	return READ_ONCE(engine->breadcrumbs.irq_wait);
}

unsigned int intel_engine_wakeup(struct intel_engine_cs *engine);
#define ENGINE_WAKEUP_WAITER BIT(0)
#define ENGINE_WAKEUP_ASLEEP BIT(1)

void intel_engine_pin_breadcrumbs_irq(struct intel_engine_cs *engine);
void intel_engine_unpin_breadcrumbs_irq(struct intel_engine_cs *engine);

void __intel_engine_disarm_breadcrumbs(struct intel_engine_cs *engine);
void intel_engine_disarm_breadcrumbs(struct intel_engine_cs *engine);

void intel_engine_reset_breadcrumbs(struct intel_engine_cs *engine);
void intel_engine_fini_breadcrumbs(struct intel_engine_cs *engine);

static inline u32 *gen8_emit_pipe_control(u32 *batch, u32 flags, u32 offset)
{
	memset(batch, 0, 6 * sizeof(u32));

	batch[0] = GFX_OP_PIPE_CONTROL(6);
	batch[1] = flags;
	batch[2] = offset;

	return batch + 6;
}

static inline u32 *
gen8_emit_ggtt_write_rcs(u32 *cs, u32 value, u32 gtt_offset)
{
	/* We're using qword write, offset should be aligned to 8 bytes. */
	GEM_BUG_ON(!IS_ALIGNED(gtt_offset, 8));

	/* w/a for post sync ops following a GPGPU operation we
	 * need a prior CS_STALL, which is emitted by the flush
	 * following the batch.
	 */
	*cs++ = GFX_OP_PIPE_CONTROL(6);
	*cs++ = PIPE_CONTROL_GLOBAL_GTT_IVB | PIPE_CONTROL_CS_STALL |
		PIPE_CONTROL_QW_WRITE;
	*cs++ = gtt_offset;
	*cs++ = 0;
	*cs++ = value;
	/* We're thrashing one dword of HWS. */
	*cs++ = 0;

	return cs;
}

static inline u32 *
gen8_emit_ggtt_write(u32 *cs, u32 value, u32 gtt_offset)
{
	/* w/a: bit 5 needs to be zero for MI_FLUSH_DW address. */
	GEM_BUG_ON(gtt_offset & (1 << 5));
	/* Offset should be aligned to 8 bytes for both (QW/DW) write types */
	GEM_BUG_ON(!IS_ALIGNED(gtt_offset, 8));

	*cs++ = (MI_FLUSH_DW + 1) | MI_FLUSH_DW_OP_STOREDW;
	*cs++ = gtt_offset | MI_FLUSH_DW_USE_GTT;
	*cs++ = 0;
	*cs++ = value;

	return cs;
}

void intel_engines_sanitize(struct drm_i915_private *i915);

bool intel_engine_is_idle(struct intel_engine_cs *engine);
bool intel_engines_are_idle(struct drm_i915_private *dev_priv);

bool intel_engine_has_kernel_context(const struct intel_engine_cs *engine);
void intel_engine_lost_context(struct intel_engine_cs *engine);

void intel_engines_park(struct drm_i915_private *i915);
void intel_engines_unpark(struct drm_i915_private *i915);

void intel_engines_reset_default_submission(struct drm_i915_private *i915);
unsigned int intel_engines_has_context_isolation(struct drm_i915_private *i915);

bool intel_engine_can_store_dword(struct intel_engine_cs *engine);

__printf(3, 4)
void intel_engine_dump(struct intel_engine_cs *engine,
		       struct drm_printer *m,
		       const char *header, ...);

struct intel_engine_cs *
intel_engine_lookup_user(struct drm_i915_private *i915, u8 class, u8 instance);

static inline void intel_engine_context_in(struct intel_engine_cs *engine)
{
	unsigned long flags;

	if (READ_ONCE(engine->stats.enabled) == 0)
		return;

	write_seqlock_irqsave(&engine->stats.lock, flags);

	if (engine->stats.enabled > 0) {
		if (engine->stats.active++ == 0)
			engine->stats.start = ktime_get();
		GEM_BUG_ON(engine->stats.active == 0);
	}

	write_sequnlock_irqrestore(&engine->stats.lock, flags);
}

static inline void intel_engine_context_out(struct intel_engine_cs *engine)
{
	unsigned long flags;

	if (READ_ONCE(engine->stats.enabled) == 0)
		return;

	write_seqlock_irqsave(&engine->stats.lock, flags);

	if (engine->stats.enabled > 0) {
		ktime_t last;

		if (engine->stats.active && --engine->stats.active == 0) {
			/*
			 * Decrement the active context count and in case GPU
			 * is now idle add up to the running total.
			 */
			last = ktime_sub(ktime_get(), engine->stats.start);

			engine->stats.total = ktime_add(engine->stats.total,
							last);
		} else if (engine->stats.active == 0) {
			/*
			 * After turning on engine stats, context out might be
			 * the first event in which case we account from the
			 * time stats gathering was turned on.
			 */
			last = ktime_sub(ktime_get(), engine->stats.enabled_at);

			engine->stats.total = ktime_add(engine->stats.total,
							last);
		}
	}

	write_sequnlock_irqrestore(&engine->stats.lock, flags);
}

int intel_enable_engine_stats(struct intel_engine_cs *engine);
void intel_disable_engine_stats(struct intel_engine_cs *engine);

ktime_t intel_engine_get_busy_time(struct intel_engine_cs *engine);

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)

static inline bool inject_preempt_hang(struct intel_engine_execlists *execlists)
{
	if (!execlists->preempt_hang.inject_hang)
		return false;

	complete(&execlists->preempt_hang.completion);
	return true;
}

#else

static inline bool inject_preempt_hang(struct intel_engine_execlists *execlists)
{
	return false;
}

#endif

#endif /* _INTEL_RINGBUFFER_H_ */
