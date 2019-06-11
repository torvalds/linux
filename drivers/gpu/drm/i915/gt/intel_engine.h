/* SPDX-License-Identifier: MIT */
#ifndef _INTEL_RINGBUFFER_H_
#define _INTEL_RINGBUFFER_H_

#include <drm/drm_util.h>

#include <linux/hashtable.h>
#include <linux/irq_work.h>
#include <linux/random.h>
#include <linux/seqlock.h>

#include "i915_gem_batch_pool.h"
#include "i915_pmu.h"
#include "i915_reg.h"
#include "i915_request.h"
#include "i915_selftest.h"
#include "i915_timeline.h"
#include "intel_engine_types.h"
#include "intel_gpu_commands.h"
#include "intel_workarounds.h"

struct drm_printer;

/* Early gen2 devices have a cacheline of just 32 bytes, using 64 is overkill,
 * but keeps the logic simple. Indeed, the whole purpose of this macro is just
 * to give some inclination as to some of the magic values used in the various
 * workarounds!
 */
#define CACHELINE_BYTES 64
#define CACHELINE_DWORDS (CACHELINE_BYTES / sizeof(u32))

/*
 * The register defines to be used with the following macros need to accept a
 * base param, e.g:
 *
 * REG_FOO(base) _MMIO((base) + <relative offset>)
 * ENGINE_READ(engine, REG_FOO);
 *
 * register arrays are to be defined and accessed as follows:
 *
 * REG_BAR(base, i) _MMIO((base) + <relative offset> + (i) * <shift>)
 * ENGINE_READ_IDX(engine, REG_BAR, i)
 */

#define __ENGINE_REG_OP(op__, engine__, ...) \
	intel_uncore_##op__((engine__)->uncore, __VA_ARGS__)

#define __ENGINE_READ_OP(op__, engine__, reg__) \
	__ENGINE_REG_OP(op__, (engine__), reg__((engine__)->mmio_base))

#define ENGINE_READ16(...)	__ENGINE_READ_OP(read16, __VA_ARGS__)
#define ENGINE_READ(...)	__ENGINE_READ_OP(read, __VA_ARGS__)
#define ENGINE_READ_FW(...)	__ENGINE_READ_OP(read_fw, __VA_ARGS__)
#define ENGINE_POSTING_READ(...) __ENGINE_READ_OP(posting_read, __VA_ARGS__)
#define ENGINE_POSTING_READ16(...) __ENGINE_READ_OP(posting_read16, __VA_ARGS__)

#define ENGINE_READ64(engine__, lower_reg__, upper_reg__) \
	__ENGINE_REG_OP(read64_2x32, (engine__), \
			lower_reg__((engine__)->mmio_base), \
			upper_reg__((engine__)->mmio_base))

#define ENGINE_READ_IDX(engine__, reg__, idx__) \
	__ENGINE_REG_OP(read, (engine__), reg__((engine__)->mmio_base, (idx__)))

#define __ENGINE_WRITE_OP(op__, engine__, reg__, val__) \
	__ENGINE_REG_OP(op__, (engine__), reg__((engine__)->mmio_base), (val__))

#define ENGINE_WRITE16(...)	__ENGINE_WRITE_OP(write16, __VA_ARGS__)
#define ENGINE_WRITE(...)	__ENGINE_WRITE_OP(write, __VA_ARGS__)
#define ENGINE_WRITE_FW(...)	__ENGINE_WRITE_OP(write_fw, __VA_ARGS__)

#define GEN6_RING_FAULT_REG_READ(engine__) \
	intel_uncore_read((engine__)->uncore, RING_FAULT_REG(engine__))

#define GEN6_RING_FAULT_REG_POSTING_READ(engine__) \
	intel_uncore_posting_read((engine__)->uncore, RING_FAULT_REG(engine__))

#define GEN6_RING_FAULT_REG_RMW(engine__, clear__, set__) \
({ \
	u32 __val; \
\
	__val = intel_uncore_read((engine__)->uncore, \
				  RING_FAULT_REG(engine__)); \
	__val &= ~(clear__); \
	__val |= (set__); \
	intel_uncore_write((engine__)->uncore, RING_FAULT_REG(engine__), \
			   __val); \
})

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

void intel_engines_set_scheduler_caps(struct drm_i915_private *i915);

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

struct i915_request *
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

static inline u32
intel_read_status_page(const struct intel_engine_cs *engine, int reg)
{
	/* Ensure that the compiler doesn't optimize away the load. */
	return READ_ONCE(engine->status_page.addr[reg]);
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
		clflush(&engine->status_page.addr[reg]);
		engine->status_page.addr[reg] = value;
		clflush(&engine->status_page.addr[reg]);
		mb();
	} else {
		WRITE_ONCE(engine->status_page.addr[reg], value);
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
#define I915_GEM_HWS_PREEMPT		0x32
#define I915_GEM_HWS_PREEMPT_ADDR	(I915_GEM_HWS_PREEMPT * sizeof(u32))
#define I915_GEM_HWS_SEQNO		0x40
#define I915_GEM_HWS_SEQNO_ADDR		(I915_GEM_HWS_SEQNO * sizeof(u32))
#define I915_GEM_HWS_SCRATCH		0x80
#define I915_GEM_HWS_SCRATCH_ADDR	(I915_GEM_HWS_SCRATCH * sizeof(u32))

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
void intel_ring_free(struct kref *ref);

static inline struct intel_ring *intel_ring_get(struct intel_ring *ring)
{
	kref_get(&ring->ref);
	return ring;
}

static inline void intel_ring_put(struct intel_ring *ring)
{
	kref_put(&ring->ref, intel_ring_free);
}

void intel_engine_stop(struct intel_engine_cs *engine);
void intel_engine_cleanup(struct intel_engine_cs *engine);

int __must_check intel_ring_cacheline_align(struct i915_request *rq);

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

static inline unsigned int
__intel_ring_space(unsigned int head, unsigned int tail, unsigned int size)
{
	/*
	 * "If the Ring Buffer Head Pointer and the Tail Pointer are on the
	 * same cacheline, the Head Pointer must not be greater than the Tail
	 * Pointer."
	 */
	GEM_BUG_ON(!is_power_of_2(size));
	return (head - tail - CACHELINE_BYTES) & (size - 1);
}

int intel_engines_init_mmio(struct drm_i915_private *i915);
int intel_engines_setup(struct drm_i915_private *i915);
int intel_engines_init(struct drm_i915_private *i915);
void intel_engines_cleanup(struct drm_i915_private *i915);

int intel_engine_init_common(struct intel_engine_cs *engine);
void intel_engine_cleanup_common(struct intel_engine_cs *engine);

int intel_ring_submission_setup(struct intel_engine_cs *engine);
int intel_ring_submission_init(struct intel_engine_cs *engine);

int intel_engine_stop_cs(struct intel_engine_cs *engine);
void intel_engine_cancel_stop_cs(struct intel_engine_cs *engine);

void intel_engine_set_hwsp_writemask(struct intel_engine_cs *engine, u32 mask);

u64 intel_engine_get_active_head(const struct intel_engine_cs *engine);
u64 intel_engine_get_last_batch_head(const struct intel_engine_cs *engine);

void intel_engine_get_instdone(struct intel_engine_cs *engine,
			       struct intel_instdone *instdone);

void intel_engine_init_execlists(struct intel_engine_cs *engine);

void intel_engine_init_breadcrumbs(struct intel_engine_cs *engine);
void intel_engine_fini_breadcrumbs(struct intel_engine_cs *engine);

void intel_engine_pin_breadcrumbs_irq(struct intel_engine_cs *engine);
void intel_engine_unpin_breadcrumbs_irq(struct intel_engine_cs *engine);

void intel_engine_signal_breadcrumbs(struct intel_engine_cs *engine);
void intel_engine_disarm_breadcrumbs(struct intel_engine_cs *engine);

static inline void
intel_engine_queue_breadcrumbs(struct intel_engine_cs *engine)
{
	irq_work_queue(&engine->breadcrumbs.irq_work);
}

void intel_engine_breadcrumbs_irq(struct intel_engine_cs *engine);

void intel_engine_reset_breadcrumbs(struct intel_engine_cs *engine);
void intel_engine_fini_breadcrumbs(struct intel_engine_cs *engine);

void intel_engine_print_breadcrumbs(struct intel_engine_cs *engine,
				    struct drm_printer *p);

static inline u32 *gen8_emit_pipe_control(u32 *batch, u32 flags, u32 offset)
{
	memset(batch, 0, 6 * sizeof(u32));

	batch[0] = GFX_OP_PIPE_CONTROL(6);
	batch[1] = flags;
	batch[2] = offset;

	return batch + 6;
}

static inline u32 *
gen8_emit_ggtt_write_rcs(u32 *cs, u32 value, u32 gtt_offset, u32 flags)
{
	/* We're using qword write, offset should be aligned to 8 bytes. */
	GEM_BUG_ON(!IS_ALIGNED(gtt_offset, 8));

	/* w/a for post sync ops following a GPGPU operation we
	 * need a prior CS_STALL, which is emitted by the flush
	 * following the batch.
	 */
	*cs++ = GFX_OP_PIPE_CONTROL(6);
	*cs++ = flags | PIPE_CONTROL_QW_WRITE | PIPE_CONTROL_GLOBAL_GTT_IVB;
	*cs++ = gtt_offset;
	*cs++ = 0;
	*cs++ = value;
	/* We're thrashing one dword of HWS. */
	*cs++ = 0;

	return cs;
}

static inline u32 *
gen8_emit_ggtt_write(u32 *cs, u32 value, u32 gtt_offset, u32 flags)
{
	/* w/a: bit 5 needs to be zero for MI_FLUSH_DW address. */
	GEM_BUG_ON(gtt_offset & (1 << 5));
	/* Offset should be aligned to 8 bytes for both (QW/DW) write types */
	GEM_BUG_ON(!IS_ALIGNED(gtt_offset, 8));

	*cs++ = (MI_FLUSH_DW + 1) | MI_FLUSH_DW_OP_STOREDW | flags;
	*cs++ = gtt_offset | MI_FLUSH_DW_USE_GTT;
	*cs++ = 0;
	*cs++ = value;

	return cs;
}

static inline void intel_engine_reset(struct intel_engine_cs *engine,
				      bool stalled)
{
	if (engine->reset.reset)
		engine->reset.reset(engine, stalled);
	engine->serial++; /* contexts lost */
}

bool intel_engine_is_idle(struct intel_engine_cs *engine);
bool intel_engines_are_idle(struct drm_i915_private *dev_priv);

void intel_engine_lost_context(struct intel_engine_cs *engine);

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

struct i915_request *
intel_engine_find_active_request(struct intel_engine_cs *engine);

u32 intel_engine_context_size(struct drm_i915_private *i915, u8 class);

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
