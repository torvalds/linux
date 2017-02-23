#ifndef _INTEL_RINGBUFFER_H_
#define _INTEL_RINGBUFFER_H_

#include <linux/hashtable.h>
#include "i915_gem_batch_pool.h"
#include "i915_gem_request.h"
#include "i915_gem_timeline.h"
#include "i915_selftest.h"

#define I915_CMD_HASH_ORDER 9

/* Early gen2 devices have a cacheline of just 32 bytes, using 64 is overkill,
 * but keeps the logic simple. Indeed, the whole purpose of this macro is just
 * to give some inclination as to some of the magic values used in the various
 * workarounds!
 */
#define CACHELINE_BYTES 64
#define CACHELINE_DWORDS (CACHELINE_BYTES / sizeof(uint32_t))

/*
 * Gen2 BSpec "1. Programming Environment" / 1.4.4.6 "Ring Buffer Use"
 * Gen3 BSpec "vol1c Memory Interface Functions" / 2.3.4.5 "Ring Buffer Use"
 * Gen4+ BSpec "vol1c Memory Interface and Command Stream" / 5.3.4.5 "Ring Buffer Use"
 *
 * "If the Ring Buffer Head Pointer and the Tail Pointer are on the same
 * cacheline, the Head Pointer must not be greater than the Tail
 * Pointer."
 */
#define I915_RING_FREE_SPACE 64

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
#define gen8_semaphore_seqno_size sizeof(uint64_t)
#define GEN8_SEMAPHORE_OFFSET(__from, __to)			     \
	(((__from) * I915_NUM_ENGINES  + (__to)) * gen8_semaphore_seqno_size)
#define GEN8_SIGNAL_OFFSET(__ring, to)			     \
	(dev_priv->semaphore->node.start + \
	 GEN8_SEMAPHORE_OFFSET((__ring)->id, (to)))
#define GEN8_WAIT_OFFSET(__ring, from)			     \
	(dev_priv->semaphore->node.start + \
	 GEN8_SEMAPHORE_OFFSET(from, (__ring)->id))

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
#define I915_MAX_SUBSLICES 3

#define instdone_slice_mask(dev_priv__) \
	(INTEL_GEN(dev_priv__) == 7 ? \
	 1 : INTEL_INFO(dev_priv__)->sseu.slice_mask)

#define instdone_subslice_mask(dev_priv__) \
	(INTEL_GEN(dev_priv__) == 7 ? \
	 1 : INTEL_INFO(dev_priv__)->sseu.subslice_mask)

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
	bool stalled;
};

struct intel_ring {
	struct i915_vma *vma;
	void *vaddr;

	struct intel_engine_cs *engine;

	struct list_head request_list;

	u32 head;
	u32 tail;

	int space;
	int size;
	int effective_size;

	/** We track the position of the requests in the ring buffer, and
	 * when each is retired we increment last_retired_head as the GPU
	 * must have finished processing the request and so we know we
	 * can advance the ringbuffer up to that position.
	 *
	 * last_retired_head is set to -1 after the value is consumed so
	 * we can detect new retirements.
	 */
	u32 last_retired_head;
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

struct drm_i915_gem_request;
struct intel_render_state;

struct intel_engine_cs {
	struct drm_i915_private *i915;
	const char	*name;
	enum intel_engine_id {
		RCS = 0,
		BCS,
		VCS,
		VCS2,	/* Keep instances of the same type engine together. */
		VECS
	} id;
#define _VCS(n) (VCS + (n))
	unsigned int exec_id;
	enum intel_engine_hw_id {
		RCS_HW = 0,
		VCS_HW,
		BCS_HW,
		VECS_HW,
		VCS2_HW
	} hw_id;
	enum intel_engine_hw_id guc_id; /* XXX same as hw_id? */
	u32		mmio_base;
	unsigned int irq_shift;
	struct intel_ring *buffer;
	struct intel_timeline *timeline;

	struct intel_render_state *render_state;

	atomic_t irq_count;
	unsigned long irq_posted;
#define ENGINE_IRQ_BREADCRUMB 0
#define ENGINE_IRQ_EXECLIST 1

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
		struct task_struct __rcu *irq_seqno_bh; /* bh for interrupts */

		spinlock_t lock; /* protects the lists of requests; irqsafe */
		struct rb_root waiters; /* sorted by retirement, priority */
		struct rb_root signals; /* sorted by retirement */
		struct intel_wait *first_wait; /* oldest waiter by retirement */
		struct task_struct *signaler; /* used for fence signalling */
		struct drm_i915_gem_request *first_signal;
		struct timer_list fake_irq; /* used after a missed interrupt */
		struct timer_list hangcheck; /* detect missed interrupts */

		unsigned int hangcheck_interrupts;

		bool irq_enabled : 1;
		bool rpm_wakelock : 1;
		I915_SELFTEST_DECLARE(bool mock : 1);
	} breadcrumbs;

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
	void		(*reset_hw)(struct intel_engine_cs *engine,
				    struct drm_i915_gem_request *req);

	int		(*context_pin)(struct intel_engine_cs *engine,
				       struct i915_gem_context *ctx);
	void		(*context_unpin)(struct intel_engine_cs *engine,
					 struct i915_gem_context *ctx);
	int		(*request_alloc)(struct drm_i915_gem_request *req);
	int		(*init_context)(struct drm_i915_gem_request *req);

	int		(*emit_flush)(struct drm_i915_gem_request *request,
				      u32 mode);
#define EMIT_INVALIDATE	BIT(0)
#define EMIT_FLUSH	BIT(1)
#define EMIT_BARRIER	(EMIT_INVALIDATE | EMIT_FLUSH)
	int		(*emit_bb_start)(struct drm_i915_gem_request *req,
					 u64 offset, u32 length,
					 unsigned int dispatch_flags);
#define I915_DISPATCH_SECURE BIT(0)
#define I915_DISPATCH_PINNED BIT(1)
#define I915_DISPATCH_RS     BIT(2)
	void		(*emit_breadcrumb)(struct drm_i915_gem_request *req,
					   u32 *cs);
	int		emit_breadcrumb_sz;

	/* Pass the request to the hardware queue (e.g. directly into
	 * the legacy ringbuffer or to the end of an execlist).
	 *
	 * This is called from an atomic context with irqs disabled; must
	 * be irq safe.
	 */
	void		(*submit_request)(struct drm_i915_gem_request *req);

	/* Call when the priority on a request has changed and it and its
	 * dependencies may need rescheduling. Note the request itself may
	 * not be ready to run!
	 *
	 * Called under the struct_mutex.
	 */
	void		(*schedule)(struct drm_i915_gem_request *request,
				    int priority);

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
		union {
#define GEN6_SEMAPHORE_LAST	VECS_HW
#define GEN6_NUM_SEMAPHORES	(GEN6_SEMAPHORE_LAST + 1)
#define GEN6_SEMAPHORES_MASK	GENMASK(GEN6_SEMAPHORE_LAST, 0)
			struct {
				/* our mbox written by others */
				u32		wait[GEN6_NUM_SEMAPHORES];
				/* mboxes this ring signals to */
				i915_reg_t	signal[GEN6_NUM_SEMAPHORES];
			} mbox;
			u64		signal_ggtt[I915_NUM_ENGINES];
		};

		/* AKA wait() */
		int	(*sync_to)(struct drm_i915_gem_request *req,
				   struct drm_i915_gem_request *signal);
		u32	*(*signal)(struct drm_i915_gem_request *req, u32 *cs);
	} semaphore;

	/* Execlists */
	struct tasklet_struct irq_tasklet;
	struct execlist_port {
		struct drm_i915_gem_request *request;
		unsigned int count;
		GEM_DEBUG_DECL(u32 context_id);
	} execlist_port[2];
	struct rb_root execlist_queue;
	struct rb_node *execlist_first;
	unsigned int fw_domains;

	/* Contexts are pinned whilst they are active on the GPU. The last
	 * context executed remains active whilst the GPU is idle - the
	 * switch away and write to the context object only occurs on the
	 * next execution.  Contexts are only unpinned on retirement of the
	 * following request ensuring that we can always write to the object
	 * on the context switch even after idling. Across suspend, we switch
	 * to the kernel context and trash it as the save may not happen
	 * before the hardware is powered down.
	 */
	struct i915_gem_context *last_retired_context;

	/* We track the current MI_SET_CONTEXT in order to eliminate
	 * redudant context switches. This presumes that requests are not
	 * reordered! Or when they are the tracking is updated along with
	 * the emission of individual requests into the legacy command
	 * stream (ring).
	 */
	struct i915_gem_context *legacy_active_context;

	struct intel_engine_hangcheck hangcheck;

	bool needs_cmd_parser;

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
};

static inline unsigned
intel_engine_flag(const struct intel_engine_cs *engine)
{
	return 1 << engine->id;
}

static inline void
intel_flush_status_page(struct intel_engine_cs *engine, int reg)
{
	mb();
	clflush(&engine->status_page.page_addr[reg]);
	mb();
}

static inline u32
intel_read_status_page(struct intel_engine_cs *engine, int reg)
{
	/* Ensure that the compiler doesn't optimize away the load. */
	return READ_ONCE(engine->status_page.page_addr[reg]);
}

static inline void
intel_write_status_page(struct intel_engine_cs *engine,
			int reg, u32 value)
{
	engine->status_page.page_addr[reg] = value;
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
#define I915_GEM_HWS_SCRATCH_INDEX	0x40
#define I915_GEM_HWS_SCRATCH_ADDR (I915_GEM_HWS_SCRATCH_INDEX << MI_STORE_DWORD_INDEX_SHIFT)

struct intel_ring *
intel_engine_create_ring(struct intel_engine_cs *engine, int size);
int intel_ring_pin(struct intel_ring *ring, unsigned int offset_bias);
void intel_ring_unpin(struct intel_ring *ring);
void intel_ring_free(struct intel_ring *ring);

void intel_engine_stop(struct intel_engine_cs *engine);
void intel_engine_cleanup(struct intel_engine_cs *engine);

void intel_legacy_submission_resume(struct drm_i915_private *dev_priv);

int __must_check intel_ring_cacheline_align(struct drm_i915_gem_request *req);

u32 __must_check *intel_ring_begin(struct drm_i915_gem_request *req, int n);

static inline void
intel_ring_advance(struct drm_i915_gem_request *req, u32 *cs)
{
	/* Dummy function.
	 *
	 * This serves as a placeholder in the code so that the reader
	 * can compare against the preceding intel_ring_begin() and
	 * check that the number of dwords emitted matches the space
	 * reserved for the command packet (i.e. the value passed to
	 * intel_ring_begin()).
	 */
	GEM_BUG_ON((req->ring->vaddr + req->ring->tail) != cs);
}

static inline u32
intel_ring_offset(struct drm_i915_gem_request *req, void *addr)
{
	/* Don't write ring->size (equivalent to 0) as that hangs some GPUs. */
	u32 offset = addr - req->ring->vaddr;
	GEM_BUG_ON(offset > req->ring->size);
	return offset & (req->ring->size - 1);
}

void intel_ring_update_space(struct intel_ring *ring);

void intel_engine_init_global_seqno(struct intel_engine_cs *engine, u32 seqno);

void intel_engine_setup_common(struct intel_engine_cs *engine);
int intel_engine_init_common(struct intel_engine_cs *engine);
int intel_engine_create_scratch(struct intel_engine_cs *engine, int size);
void intel_engine_cleanup_common(struct intel_engine_cs *engine);

int intel_init_render_ring_buffer(struct intel_engine_cs *engine);
int intel_init_bsd_ring_buffer(struct intel_engine_cs *engine);
int intel_init_bsd2_ring_buffer(struct intel_engine_cs *engine);
int intel_init_blt_ring_buffer(struct intel_engine_cs *engine);
int intel_init_vebox_ring_buffer(struct intel_engine_cs *engine);

u64 intel_engine_get_active_head(struct intel_engine_cs *engine);
u64 intel_engine_get_last_batch_head(struct intel_engine_cs *engine);

static inline u32 intel_engine_get_seqno(struct intel_engine_cs *engine)
{
	return intel_read_status_page(engine, I915_GEM_HWS_INDEX);
}

static inline u32 intel_engine_last_submit(struct intel_engine_cs *engine)
{
	/* We are only peeking at the tail of the submit queue (and not the
	 * queue itself) in order to gain a hint as to the current active
	 * state of the engine. Callers are not expected to be taking
	 * engine->timeline->lock, nor are they expected to be concerned
	 * wtih serialising this hint with anything, so document it as
	 * a hint and nothing more.
	 */
	return READ_ONCE(engine->timeline->seqno);
}

int init_workarounds_ring(struct intel_engine_cs *engine);
int intel_ring_workarounds_emit(struct drm_i915_gem_request *req);

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

/* intel_breadcrumbs.c -- user interrupt bottom-half for waiters */
int intel_engine_init_breadcrumbs(struct intel_engine_cs *engine);

static inline void intel_wait_init(struct intel_wait *wait)
{
	wait->tsk = current;
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
			  const struct drm_i915_gem_request *rq)
{
	return intel_wait_update_seqno(wait, i915_gem_request_global_seqno(rq));
}

static inline bool
intel_wait_check_seqno(const struct intel_wait *wait, u32 seqno)
{
	return wait->seqno == seqno;
}

static inline bool
intel_wait_check_request(const struct intel_wait *wait,
			 const struct drm_i915_gem_request *rq)
{
	return intel_wait_check_seqno(wait, i915_gem_request_global_seqno(rq));
}

static inline bool intel_wait_complete(const struct intel_wait *wait)
{
	return RB_EMPTY_NODE(&wait->node);
}

bool intel_engine_add_wait(struct intel_engine_cs *engine,
			   struct intel_wait *wait);
void intel_engine_remove_wait(struct intel_engine_cs *engine,
			      struct intel_wait *wait);
void intel_engine_enable_signaling(struct drm_i915_gem_request *request);

static inline bool intel_engine_has_waiter(const struct intel_engine_cs *engine)
{
	return rcu_access_pointer(engine->breadcrumbs.irq_seqno_bh);
}

static inline bool intel_engine_wakeup(const struct intel_engine_cs *engine)
{
	bool wakeup = false;

	/* Note that for this not to dangerously chase a dangling pointer,
	 * we must hold the rcu_read_lock here.
	 *
	 * Also note that tsk is likely to be in !TASK_RUNNING state so an
	 * early test for tsk->state != TASK_RUNNING before wake_up_process()
	 * is unlikely to be beneficial.
	 */
	if (intel_engine_has_waiter(engine)) {
		struct task_struct *tsk;

		rcu_read_lock();
		tsk = rcu_dereference(engine->breadcrumbs.irq_seqno_bh);
		if (tsk)
			wakeup = wake_up_process(tsk);
		rcu_read_unlock();
	}

	return wakeup;
}

void intel_engine_reset_breadcrumbs(struct intel_engine_cs *engine);
void intel_engine_fini_breadcrumbs(struct intel_engine_cs *engine);
bool intel_breadcrumbs_busy(struct intel_engine_cs *engine);

static inline u32 *gen8_emit_pipe_control(u32 *batch, u32 flags, u32 offset)
{
	memset(batch, 0, 6 * sizeof(u32));

	batch[0] = GFX_OP_PIPE_CONTROL(6);
	batch[1] = flags;
	batch[2] = offset;

	return batch + 6;
}

#endif /* _INTEL_RINGBUFFER_H_ */
