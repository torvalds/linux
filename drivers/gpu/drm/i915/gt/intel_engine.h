/* SPDX-License-Identifier: MIT */
#ifndef _INTEL_RINGBUFFER_H_
#define _INTEL_RINGBUFFER_H_

#include <asm/cacheflush.h>
#include <drm/drm_util.h>
#include <drm/drm_cache.h>

#include <linux/hashtable.h>
#include <linux/irq_work.h>
#include <linux/random.h>
#include <linux/seqlock.h>

#include "i915_pmu.h"
#include "i915_request.h"
#include "i915_selftest.h"
#include "intel_engine_types.h"
#include "intel_gt_types.h"
#include "intel_timeline.h"
#include "intel_workarounds.h"

struct drm_printer;
struct intel_context;
struct intel_gt;
struct lock_class_key;

/* Early gen2 devices have a cacheline of just 32 bytes, using 64 is overkill,
 * but keeps the logic simple. Indeed, the whole purpose of this macro is just
 * to give some inclination as to some of the magic values used in the various
 * workarounds!
 */
#define CACHELINE_BYTES 64
#define CACHELINE_DWORDS (CACHELINE_BYTES / sizeof(u32))

#define ENGINE_TRACE(e, fmt, ...) do {					\
	const struct intel_engine_cs *e__ __maybe_unused = (e);		\
	GEM_TRACE("%s %s: " fmt,					\
		  dev_name(e__->i915->drm.dev), e__->name,		\
		  ##__VA_ARGS__);					\
} while (0)

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
#define ENGINE_POSTING_READ(...) __ENGINE_READ_OP(posting_read_fw, __VA_ARGS__)
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

static inline unsigned int
execlists_num_ports(const struct intel_engine_execlists * const execlists)
{
	return execlists->port_mask + 1;
}

static inline struct i915_request *
execlists_active(const struct intel_engine_execlists *execlists)
{
	struct i915_request * const *cur, * const *old, *active;

	cur = READ_ONCE(execlists->active);
	smp_rmb(); /* pairs with overwrite protection in process_csb() */
	do {
		old = cur;

		active = READ_ONCE(*cur);
		cur = READ_ONCE(execlists->active);

		smp_rmb(); /* and complete the seqlock retry */
	} while (unlikely(cur != old));

	return active;
}

struct i915_request *
execlists_unwind_incomplete_requests(struct intel_engine_execlists *execlists);

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
	drm_clflush_virt_range(&engine->status_page.addr[reg], sizeof(value));
	WRITE_ONCE(engine->status_page.addr[reg], value);
	drm_clflush_virt_range(&engine->status_page.addr[reg], sizeof(value));
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
#define I915_GEM_HWS_MIGRATE		(0x42 * sizeof(u32))
#define I915_GEM_HWS_PXP		0x60
#define I915_GEM_HWS_PXP_ADDR		(I915_GEM_HWS_PXP * sizeof(u32))
#define I915_GEM_HWS_SCRATCH		0x80

#define I915_HWS_CSB_BUF0_INDEX		0x10
#define I915_HWS_CSB_WRITE_INDEX	0x1f
#define ICL_HWS_CSB_WRITE_INDEX		0x2f
#define INTEL_HWS_CSB_WRITE_INDEX(__i915) \
	(GRAPHICS_VER(__i915) >= 11 ? ICL_HWS_CSB_WRITE_INDEX : I915_HWS_CSB_WRITE_INDEX)

void intel_engine_stop(struct intel_engine_cs *engine);
void intel_engine_cleanup(struct intel_engine_cs *engine);

int intel_engines_init_mmio(struct intel_gt *gt);
int intel_engines_init(struct intel_gt *gt);

void intel_engine_free_request_pool(struct intel_engine_cs *engine);

void intel_engines_release(struct intel_gt *gt);
void intel_engines_free(struct intel_gt *gt);

int intel_engine_init_common(struct intel_engine_cs *engine);
void intel_engine_cleanup_common(struct intel_engine_cs *engine);

int intel_engine_resume(struct intel_engine_cs *engine);

int intel_ring_submission_setup(struct intel_engine_cs *engine);

int intel_engine_stop_cs(struct intel_engine_cs *engine);
void intel_engine_cancel_stop_cs(struct intel_engine_cs *engine);

void intel_engine_wait_for_pending_mi_fw(struct intel_engine_cs *engine);

void intel_engine_set_hwsp_writemask(struct intel_engine_cs *engine, u32 mask);

u64 intel_engine_get_active_head(const struct intel_engine_cs *engine);
u64 intel_engine_get_last_batch_head(const struct intel_engine_cs *engine);

void intel_engine_get_instdone(const struct intel_engine_cs *engine,
			       struct intel_instdone *instdone);

void intel_engine_init_execlists(struct intel_engine_cs *engine);

bool intel_engine_irq_enable(struct intel_engine_cs *engine);
void intel_engine_irq_disable(struct intel_engine_cs *engine);

static inline void __intel_engine_reset(struct intel_engine_cs *engine,
					bool stalled)
{
	if (engine->reset.rewind)
		engine->reset.rewind(engine, stalled);
	engine->serial++; /* contexts lost */
}

bool intel_engines_are_idle(struct intel_gt *gt);
bool intel_engine_is_idle(struct intel_engine_cs *engine);

void __intel_engine_flush_submission(struct intel_engine_cs *engine, bool sync);
static inline void intel_engine_flush_submission(struct intel_engine_cs *engine)
{
	__intel_engine_flush_submission(engine, true);
}

void intel_engines_reset_default_submission(struct intel_gt *gt);

bool intel_engine_can_store_dword(struct intel_engine_cs *engine);

__printf(3, 4)
void intel_engine_dump(struct intel_engine_cs *engine,
		       struct drm_printer *m,
		       const char *header, ...);
void intel_engine_dump_active_requests(struct list_head *requests,
				       struct i915_request *hung_rq,
				       struct drm_printer *m);

ktime_t intel_engine_get_busy_time(struct intel_engine_cs *engine,
				   ktime_t *now);

struct i915_request *
intel_engine_execlist_find_hung_request(struct intel_engine_cs *engine);

u32 intel_engine_context_size(struct intel_gt *gt, u8 class);
struct intel_context *
intel_engine_create_pinned_context(struct intel_engine_cs *engine,
				   struct i915_address_space *vm,
				   unsigned int ring_size,
				   unsigned int hwsp,
				   struct lock_class_key *key,
				   const char *name);

void intel_engine_destroy_pinned_context(struct intel_context *ce);

void xehp_enable_ccs_engines(struct intel_engine_cs *engine);

#define ENGINE_PHYSICAL	0
#define ENGINE_MOCK	1
#define ENGINE_VIRTUAL	2

static inline bool intel_engine_uses_guc(const struct intel_engine_cs *engine)
{
	return engine->gt->submission_method >= INTEL_SUBMISSION_GUC;
}

static inline bool
intel_engine_has_preempt_reset(const struct intel_engine_cs *engine)
{
	if (!CONFIG_DRM_I915_PREEMPT_TIMEOUT)
		return false;

	return intel_engine_has_preemption(engine);
}

#define FORCE_VIRTUAL	BIT(0)
struct intel_context *
intel_engine_create_virtual(struct intel_engine_cs **siblings,
			    unsigned int count, unsigned long flags);

static inline struct intel_context *
intel_engine_create_parallel(struct intel_engine_cs **engines,
			     unsigned int num_engines,
			     unsigned int width)
{
	GEM_BUG_ON(!engines[0]->cops->create_parallel);
	return engines[0]->cops->create_parallel(engines, num_engines, width);
}

static inline bool
intel_virtual_engine_has_heartbeat(const struct intel_engine_cs *engine)
{
	/*
	 * For non-GuC submission we expect the back-end to look at the
	 * heartbeat status of the actual physical engine that the work
	 * has been (or is being) scheduled on, so we should only reach
	 * here with GuC submission enabled.
	 */
	GEM_BUG_ON(!intel_engine_uses_guc(engine));

	return intel_guc_virtual_engine_has_heartbeat(engine);
}

static inline bool
intel_engine_has_heartbeat(const struct intel_engine_cs *engine)
{
	if (!CONFIG_DRM_I915_HEARTBEAT_INTERVAL)
		return false;

	if (intel_engine_is_virtual(engine))
		return intel_virtual_engine_has_heartbeat(engine);
	else
		return READ_ONCE(engine->props.heartbeat_interval_ms);
}

static inline struct intel_engine_cs *
intel_engine_get_sibling(struct intel_engine_cs *engine, unsigned int sibling)
{
	GEM_BUG_ON(!intel_engine_is_virtual(engine));
	return engine->cops->get_sibling(engine, sibling);
}

static inline void
intel_engine_set_hung_context(struct intel_engine_cs *engine,
			      struct intel_context *ce)
{
	engine->hung_ce = ce;
}

static inline void
intel_engine_clear_hung_context(struct intel_engine_cs *engine)
{
	intel_engine_set_hung_context(engine, NULL);
}

static inline struct intel_context *
intel_engine_get_hung_context(struct intel_engine_cs *engine)
{
	return engine->hung_ce;
}

#endif /* _INTEL_RINGBUFFER_H_ */
