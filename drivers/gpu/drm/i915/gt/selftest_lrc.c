// SPDX-License-Identifier: MIT
/*
 * Copyright © 2018 Intel Corporation
 */

#include <linux/prime_numbers.h>

#include "gem/i915_gem_internal.h"

#include "i915_selftest.h"
#include "intel_engine_heartbeat.h"
#include "intel_engine_pm.h"
#include "intel_reset.h"
#include "intel_ring.h"
#include "selftest_engine_heartbeat.h"
#include "selftests/i915_random.h"
#include "selftests/igt_flush_test.h"
#include "selftests/igt_live_test.h"
#include "selftests/igt_spinner.h"
#include "selftests/lib_sw_fence.h"
#include "shmem_utils.h"

#include "gem/selftests/igt_gem_utils.h"
#include "gem/selftests/mock_context.h"

#define CS_GPR(engine, n) ((engine)->mmio_base + 0x600 + (n) * 4)
#define NUM_GPR 16
#define NUM_GPR_DW (NUM_GPR * 2) /* each GPR is 2 dwords */

static struct i915_vma *create_scratch(struct intel_gt *gt)
{
	return __vm_create_scratch_for_read_pinned(&gt->ggtt->vm, PAGE_SIZE);
}

static bool is_active(struct i915_request *rq)
{
	if (i915_request_is_active(rq))
		return true;

	if (i915_request_on_hold(rq))
		return true;

	if (i915_request_has_initial_breadcrumb(rq) && i915_request_started(rq))
		return true;

	return false;
}

static int wait_for_submit(struct intel_engine_cs *engine,
			   struct i915_request *rq,
			   unsigned long timeout)
{
	/* Ignore our own attempts to suppress excess tasklets */
	tasklet_hi_schedule(&engine->sched_engine->tasklet);

	timeout += jiffies;
	do {
		bool done = time_after(jiffies, timeout);

		if (i915_request_completed(rq)) /* that was quick! */
			return 0;

		/* Wait until the HW has acknowleged the submission (or err) */
		intel_engine_flush_submission(engine);
		if (!READ_ONCE(engine->execlists.pending[0]) && is_active(rq))
			return 0;

		if (done)
			return -ETIME;

		cond_resched();
	} while (1);
}

static int emit_semaphore_signal(struct intel_context *ce, void *slot)
{
	const u32 offset =
		i915_ggtt_offset(ce->engine->status_page.vma) +
		offset_in_page(slot);
	struct i915_request *rq;
	u32 *cs;

	rq = intel_context_create_request(ce);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	cs = intel_ring_begin(rq, 4);
	if (IS_ERR(cs)) {
		i915_request_add(rq);
		return PTR_ERR(cs);
	}

	*cs++ = MI_STORE_DWORD_IMM_GEN4 | MI_USE_GGTT;
	*cs++ = offset;
	*cs++ = 0;
	*cs++ = 1;

	intel_ring_advance(rq, cs);

	rq->sched.attr.priority = I915_PRIORITY_BARRIER;
	i915_request_add(rq);
	return 0;
}

static int context_flush(struct intel_context *ce, long timeout)
{
	struct i915_request *rq;
	struct dma_fence *fence;
	int err = 0;

	rq = intel_engine_create_kernel_request(ce->engine);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	fence = i915_active_fence_get(&ce->timeline->last_request);
	if (fence) {
		i915_request_await_dma_fence(rq, fence);
		dma_fence_put(fence);
	}

	rq = i915_request_get(rq);
	i915_request_add(rq);
	if (i915_request_wait(rq, 0, timeout) < 0)
		err = -ETIME;
	i915_request_put(rq);

	rmb(); /* We know the request is written, make sure all state is too! */
	return err;
}

static int live_lrc_layout(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	u32 *lrc;
	int err;

	/*
	 * Check the registers offsets we use to create the initial reg state
	 * match the layout saved by HW.
	 */

	lrc = (u32 *)__get_free_page(GFP_KERNEL); /* requires page alignment */
	if (!lrc)
		return -ENOMEM;
	GEM_BUG_ON(offset_in_page(lrc));

	err = 0;
	for_each_engine(engine, gt, id) {
		u32 *hw;
		int dw;

		if (!engine->default_state)
			continue;

		hw = shmem_pin_map(engine->default_state);
		if (IS_ERR(hw)) {
			err = PTR_ERR(hw);
			break;
		}
		hw += LRC_STATE_OFFSET / sizeof(*hw);

		__lrc_init_regs(memset(lrc, POISON_INUSE, PAGE_SIZE),
				engine->kernel_context, engine, true);

		dw = 0;
		do {
			u32 lri = READ_ONCE(hw[dw]);

			if (lri == 0) {
				dw++;
				continue;
			}

			if (lrc[dw] == 0) {
				pr_debug("%s: skipped instruction %x at dword %d\n",
					 engine->name, lri, dw);
				dw++;
				continue;
			}

			if ((lri & GENMASK(31, 23)) != MI_INSTR(0x22, 0)) {
				pr_err("%s: Expected LRI command at dword %d, found %08x\n",
				       engine->name, dw, lri);
				err = -EINVAL;
				break;
			}

			if (lrc[dw] != lri) {
				pr_err("%s: LRI command mismatch at dword %d, expected %08x found %08x\n",
				       engine->name, dw, lri, lrc[dw]);
				err = -EINVAL;
				break;
			}

			lri &= 0x7f;
			lri++;
			dw++;

			while (lri) {
				u32 offset = READ_ONCE(hw[dw]);

				if (offset != lrc[dw]) {
					pr_err("%s: Different registers found at dword %d, expected %x, found %x\n",
					       engine->name, dw, offset, lrc[dw]);
					err = -EINVAL;
					break;
				}

				/*
				 * Skip over the actual register value as we
				 * expect that to differ.
				 */
				dw += 2;
				lri -= 2;
			}
		} while (!err && (lrc[dw] & ~BIT(0)) != MI_BATCH_BUFFER_END);

		if (err) {
			pr_info("%s: HW register image:\n", engine->name);
			igt_hexdump(hw, PAGE_SIZE);

			pr_info("%s: SW register image:\n", engine->name);
			igt_hexdump(lrc, PAGE_SIZE);
		}

		shmem_unpin_map(engine->default_state, hw);
		if (err)
			break;
	}

	free_page((unsigned long)lrc);
	return err;
}

static int find_offset(const u32 *lri, u32 offset)
{
	int i;

	for (i = 0; i < PAGE_SIZE / sizeof(u32); i++)
		if (lri[i] == offset)
			return i;

	return -1;
}

static int live_lrc_fixed(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err = 0;

	/*
	 * Check the assumed register offsets match the actual locations in
	 * the context image.
	 */

	for_each_engine(engine, gt, id) {
		const struct {
			u32 reg;
			u32 offset;
			const char *name;
		} tbl[] = {
			{
				i915_mmio_reg_offset(RING_START(engine->mmio_base)),
				CTX_RING_START - 1,
				"RING_START"
			},
			{
				i915_mmio_reg_offset(RING_CTL(engine->mmio_base)),
				CTX_RING_CTL - 1,
				"RING_CTL"
			},
			{
				i915_mmio_reg_offset(RING_HEAD(engine->mmio_base)),
				CTX_RING_HEAD - 1,
				"RING_HEAD"
			},
			{
				i915_mmio_reg_offset(RING_TAIL(engine->mmio_base)),
				CTX_RING_TAIL - 1,
				"RING_TAIL"
			},
			{
				i915_mmio_reg_offset(RING_MI_MODE(engine->mmio_base)),
				lrc_ring_mi_mode(engine),
				"RING_MI_MODE"
			},
			{
				i915_mmio_reg_offset(RING_BBSTATE(engine->mmio_base)),
				CTX_BB_STATE - 1,
				"BB_STATE"
			},
			{
				i915_mmio_reg_offset(RING_BB_PER_CTX_PTR(engine->mmio_base)),
				lrc_ring_wa_bb_per_ctx(engine),
				"RING_BB_PER_CTX_PTR"
			},
			{
				i915_mmio_reg_offset(RING_INDIRECT_CTX(engine->mmio_base)),
				lrc_ring_indirect_ptr(engine),
				"RING_INDIRECT_CTX_PTR"
			},
			{
				i915_mmio_reg_offset(RING_INDIRECT_CTX_OFFSET(engine->mmio_base)),
				lrc_ring_indirect_offset(engine),
				"RING_INDIRECT_CTX_OFFSET"
			},
			{
				i915_mmio_reg_offset(RING_CTX_TIMESTAMP(engine->mmio_base)),
				CTX_TIMESTAMP - 1,
				"RING_CTX_TIMESTAMP"
			},
			{
				i915_mmio_reg_offset(GEN8_RING_CS_GPR(engine->mmio_base, 0)),
				lrc_ring_gpr0(engine),
				"RING_CS_GPR0"
			},
			{
				i915_mmio_reg_offset(RING_CMD_BUF_CCTL(engine->mmio_base)),
				lrc_ring_cmd_buf_cctl(engine),
				"RING_CMD_BUF_CCTL"
			},
			{ },
		}, *t;
		u32 *hw;

		if (!engine->default_state)
			continue;

		hw = shmem_pin_map(engine->default_state);
		if (IS_ERR(hw)) {
			err = PTR_ERR(hw);
			break;
		}
		hw += LRC_STATE_OFFSET / sizeof(*hw);

		for (t = tbl; t->name; t++) {
			int dw = find_offset(hw, t->reg);

			if (dw != t->offset) {
				pr_err("%s: Offset for %s [0x%x] mismatch, found %x, expected %x\n",
				       engine->name,
				       t->name,
				       t->reg,
				       dw,
				       t->offset);
				err = -EINVAL;
			}
		}

		shmem_unpin_map(engine->default_state, hw);
	}

	return err;
}

static int __live_lrc_state(struct intel_engine_cs *engine,
			    struct i915_vma *scratch)
{
	struct intel_context *ce;
	struct i915_request *rq;
	struct i915_gem_ww_ctx ww;
	enum {
		RING_START_IDX = 0,
		RING_TAIL_IDX,
		MAX_IDX
	};
	u32 expected[MAX_IDX];
	u32 *cs;
	int err;
	int n;

	ce = intel_context_create(engine);
	if (IS_ERR(ce))
		return PTR_ERR(ce);

	i915_gem_ww_ctx_init(&ww, false);
retry:
	err = i915_gem_object_lock(scratch->obj, &ww);
	if (!err)
		err = intel_context_pin_ww(ce, &ww);
	if (err)
		goto err_put;

	rq = i915_request_create(ce);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_unpin;
	}

	cs = intel_ring_begin(rq, 4 * MAX_IDX);
	if (IS_ERR(cs)) {
		err = PTR_ERR(cs);
		i915_request_add(rq);
		goto err_unpin;
	}

	*cs++ = MI_STORE_REGISTER_MEM_GEN8 | MI_USE_GGTT;
	*cs++ = i915_mmio_reg_offset(RING_START(engine->mmio_base));
	*cs++ = i915_ggtt_offset(scratch) + RING_START_IDX * sizeof(u32);
	*cs++ = 0;

	expected[RING_START_IDX] = i915_ggtt_offset(ce->ring->vma);

	*cs++ = MI_STORE_REGISTER_MEM_GEN8 | MI_USE_GGTT;
	*cs++ = i915_mmio_reg_offset(RING_TAIL(engine->mmio_base));
	*cs++ = i915_ggtt_offset(scratch) + RING_TAIL_IDX * sizeof(u32);
	*cs++ = 0;

	err = i915_request_await_object(rq, scratch->obj, true);
	if (!err)
		err = i915_vma_move_to_active(scratch, rq, EXEC_OBJECT_WRITE);

	i915_request_get(rq);
	i915_request_add(rq);
	if (err)
		goto err_rq;

	intel_engine_flush_submission(engine);
	expected[RING_TAIL_IDX] = ce->ring->tail;

	if (i915_request_wait(rq, 0, HZ / 5) < 0) {
		err = -ETIME;
		goto err_rq;
	}

	cs = i915_gem_object_pin_map(scratch->obj, I915_MAP_WB);
	if (IS_ERR(cs)) {
		err = PTR_ERR(cs);
		goto err_rq;
	}

	for (n = 0; n < MAX_IDX; n++) {
		if (cs[n] != expected[n]) {
			pr_err("%s: Stored register[%d] value[0x%x] did not match expected[0x%x]\n",
			       engine->name, n, cs[n], expected[n]);
			err = -EINVAL;
			break;
		}
	}

	i915_gem_object_unpin_map(scratch->obj);

err_rq:
	i915_request_put(rq);
err_unpin:
	intel_context_unpin(ce);
err_put:
	if (err == -EDEADLK) {
		err = i915_gem_ww_ctx_backoff(&ww);
		if (!err)
			goto retry;
	}
	i915_gem_ww_ctx_fini(&ww);
	intel_context_put(ce);
	return err;
}

static int live_lrc_state(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	struct i915_vma *scratch;
	enum intel_engine_id id;
	int err = 0;

	/*
	 * Check the live register state matches what we expect for this
	 * intel_context.
	 */

	scratch = create_scratch(gt);
	if (IS_ERR(scratch))
		return PTR_ERR(scratch);

	for_each_engine(engine, gt, id) {
		err = __live_lrc_state(engine, scratch);
		if (err)
			break;
	}

	if (igt_flush_test(gt->i915))
		err = -EIO;

	i915_vma_unpin_and_release(&scratch, 0);
	return err;
}

static int gpr_make_dirty(struct intel_context *ce)
{
	struct i915_request *rq;
	u32 *cs;
	int n;

	rq = intel_context_create_request(ce);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	cs = intel_ring_begin(rq, 2 * NUM_GPR_DW + 2);
	if (IS_ERR(cs)) {
		i915_request_add(rq);
		return PTR_ERR(cs);
	}

	*cs++ = MI_LOAD_REGISTER_IMM(NUM_GPR_DW);
	for (n = 0; n < NUM_GPR_DW; n++) {
		*cs++ = CS_GPR(ce->engine, n);
		*cs++ = STACK_MAGIC;
	}
	*cs++ = MI_NOOP;

	intel_ring_advance(rq, cs);

	rq->sched.attr.priority = I915_PRIORITY_BARRIER;
	i915_request_add(rq);

	return 0;
}

static struct i915_request *
__gpr_read(struct intel_context *ce, struct i915_vma *scratch, u32 *slot)
{
	const u32 offset =
		i915_ggtt_offset(ce->engine->status_page.vma) +
		offset_in_page(slot);
	struct i915_request *rq;
	u32 *cs;
	int err;
	int n;

	rq = intel_context_create_request(ce);
	if (IS_ERR(rq))
		return rq;

	cs = intel_ring_begin(rq, 6 + 4 * NUM_GPR_DW);
	if (IS_ERR(cs)) {
		i915_request_add(rq);
		return ERR_CAST(cs);
	}

	*cs++ = MI_ARB_ON_OFF | MI_ARB_ENABLE;
	*cs++ = MI_NOOP;

	*cs++ = MI_SEMAPHORE_WAIT |
		MI_SEMAPHORE_GLOBAL_GTT |
		MI_SEMAPHORE_POLL |
		MI_SEMAPHORE_SAD_NEQ_SDD;
	*cs++ = 0;
	*cs++ = offset;
	*cs++ = 0;

	for (n = 0; n < NUM_GPR_DW; n++) {
		*cs++ = MI_STORE_REGISTER_MEM_GEN8 | MI_USE_GGTT;
		*cs++ = CS_GPR(ce->engine, n);
		*cs++ = i915_ggtt_offset(scratch) + n * sizeof(u32);
		*cs++ = 0;
	}

	i915_vma_lock(scratch);
	err = i915_request_await_object(rq, scratch->obj, true);
	if (!err)
		err = i915_vma_move_to_active(scratch, rq, EXEC_OBJECT_WRITE);
	i915_vma_unlock(scratch);

	i915_request_get(rq);
	i915_request_add(rq);
	if (err) {
		i915_request_put(rq);
		rq = ERR_PTR(err);
	}

	return rq;
}

static int __live_lrc_gpr(struct intel_engine_cs *engine,
			  struct i915_vma *scratch,
			  bool preempt)
{
	u32 *slot = memset32(engine->status_page.addr + 1000, 0, 4);
	struct intel_context *ce;
	struct i915_request *rq;
	u32 *cs;
	int err;
	int n;

	if (GRAPHICS_VER(engine->i915) < 9 && engine->class != RENDER_CLASS)
		return 0; /* GPR only on rcs0 for gen8 */

	err = gpr_make_dirty(engine->kernel_context);
	if (err)
		return err;

	ce = intel_context_create(engine);
	if (IS_ERR(ce))
		return PTR_ERR(ce);

	rq = __gpr_read(ce, scratch, slot);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_put;
	}

	err = wait_for_submit(engine, rq, HZ / 2);
	if (err)
		goto err_rq;

	if (preempt) {
		err = gpr_make_dirty(engine->kernel_context);
		if (err)
			goto err_rq;

		err = emit_semaphore_signal(engine->kernel_context, slot);
		if (err)
			goto err_rq;

		err = wait_for_submit(engine, rq, HZ / 2);
		if (err)
			goto err_rq;
	} else {
		slot[0] = 1;
		wmb();
	}

	if (i915_request_wait(rq, 0, HZ / 5) < 0) {
		err = -ETIME;
		goto err_rq;
	}

	cs = i915_gem_object_pin_map_unlocked(scratch->obj, I915_MAP_WB);
	if (IS_ERR(cs)) {
		err = PTR_ERR(cs);
		goto err_rq;
	}

	for (n = 0; n < NUM_GPR_DW; n++) {
		if (cs[n]) {
			pr_err("%s: GPR[%d].%s was not zero, found 0x%08x!\n",
			       engine->name,
			       n / 2, n & 1 ? "udw" : "ldw",
			       cs[n]);
			err = -EINVAL;
			break;
		}
	}

	i915_gem_object_unpin_map(scratch->obj);

err_rq:
	memset32(&slot[0], -1, 4);
	wmb();
	i915_request_put(rq);
err_put:
	intel_context_put(ce);
	return err;
}

static int live_lrc_gpr(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	struct i915_vma *scratch;
	enum intel_engine_id id;
	int err = 0;

	/*
	 * Check that GPR registers are cleared in new contexts as we need
	 * to avoid leaking any information from previous contexts.
	 */

	scratch = create_scratch(gt);
	if (IS_ERR(scratch))
		return PTR_ERR(scratch);

	for_each_engine(engine, gt, id) {
		st_engine_heartbeat_disable(engine);

		err = __live_lrc_gpr(engine, scratch, false);
		if (err)
			goto err;

		err = __live_lrc_gpr(engine, scratch, true);
		if (err)
			goto err;

err:
		st_engine_heartbeat_enable(engine);
		if (igt_flush_test(gt->i915))
			err = -EIO;
		if (err)
			break;
	}

	i915_vma_unpin_and_release(&scratch, 0);
	return err;
}

static struct i915_request *
create_timestamp(struct intel_context *ce, void *slot, int idx)
{
	const u32 offset =
		i915_ggtt_offset(ce->engine->status_page.vma) +
		offset_in_page(slot);
	struct i915_request *rq;
	u32 *cs;
	int err;

	rq = intel_context_create_request(ce);
	if (IS_ERR(rq))
		return rq;

	cs = intel_ring_begin(rq, 10);
	if (IS_ERR(cs)) {
		err = PTR_ERR(cs);
		goto err;
	}

	*cs++ = MI_ARB_ON_OFF | MI_ARB_ENABLE;
	*cs++ = MI_NOOP;

	*cs++ = MI_SEMAPHORE_WAIT |
		MI_SEMAPHORE_GLOBAL_GTT |
		MI_SEMAPHORE_POLL |
		MI_SEMAPHORE_SAD_NEQ_SDD;
	*cs++ = 0;
	*cs++ = offset;
	*cs++ = 0;

	*cs++ = MI_STORE_REGISTER_MEM_GEN8 | MI_USE_GGTT;
	*cs++ = i915_mmio_reg_offset(RING_CTX_TIMESTAMP(rq->engine->mmio_base));
	*cs++ = offset + idx * sizeof(u32);
	*cs++ = 0;

	intel_ring_advance(rq, cs);

	err = 0;
err:
	i915_request_get(rq);
	i915_request_add(rq);
	if (err) {
		i915_request_put(rq);
		return ERR_PTR(err);
	}

	return rq;
}

struct lrc_timestamp {
	struct intel_engine_cs *engine;
	struct intel_context *ce[2];
	u32 poison;
};

static bool timestamp_advanced(u32 start, u32 end)
{
	return (s32)(end - start) > 0;
}

static int __lrc_timestamp(const struct lrc_timestamp *arg, bool preempt)
{
	u32 *slot = memset32(arg->engine->status_page.addr + 1000, 0, 4);
	struct i915_request *rq;
	u32 timestamp;
	int err = 0;

	arg->ce[0]->lrc_reg_state[CTX_TIMESTAMP] = arg->poison;
	rq = create_timestamp(arg->ce[0], slot, 1);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	err = wait_for_submit(rq->engine, rq, HZ / 2);
	if (err)
		goto err;

	if (preempt) {
		arg->ce[1]->lrc_reg_state[CTX_TIMESTAMP] = 0xdeadbeef;
		err = emit_semaphore_signal(arg->ce[1], slot);
		if (err)
			goto err;
	} else {
		slot[0] = 1;
		wmb();
	}

	/* And wait for switch to kernel (to save our context to memory) */
	err = context_flush(arg->ce[0], HZ / 2);
	if (err)
		goto err;

	if (!timestamp_advanced(arg->poison, slot[1])) {
		pr_err("%s(%s): invalid timestamp on restore, context:%x, request:%x\n",
		       arg->engine->name, preempt ? "preempt" : "simple",
		       arg->poison, slot[1]);
		err = -EINVAL;
	}

	timestamp = READ_ONCE(arg->ce[0]->lrc_reg_state[CTX_TIMESTAMP]);
	if (!timestamp_advanced(slot[1], timestamp)) {
		pr_err("%s(%s): invalid timestamp on save, request:%x, context:%x\n",
		       arg->engine->name, preempt ? "preempt" : "simple",
		       slot[1], timestamp);
		err = -EINVAL;
	}

err:
	memset32(slot, -1, 4);
	i915_request_put(rq);
	return err;
}

static int live_lrc_timestamp(void *arg)
{
	struct lrc_timestamp data = {};
	struct intel_gt *gt = arg;
	enum intel_engine_id id;
	const u32 poison[] = {
		0,
		S32_MAX,
		(u32)S32_MAX + 1,
		U32_MAX,
	};

	/*
	 * We want to verify that the timestamp is saved and restore across
	 * context switches and is monotonic.
	 *
	 * So we do this with a little bit of LRC poisoning to check various
	 * boundary conditions, and see what happens if we preempt the context
	 * with a second request (carrying more poison into the timestamp).
	 */

	for_each_engine(data.engine, gt, id) {
		int i, err = 0;

		st_engine_heartbeat_disable(data.engine);

		for (i = 0; i < ARRAY_SIZE(data.ce); i++) {
			struct intel_context *tmp;

			tmp = intel_context_create(data.engine);
			if (IS_ERR(tmp)) {
				err = PTR_ERR(tmp);
				goto err;
			}

			err = intel_context_pin(tmp);
			if (err) {
				intel_context_put(tmp);
				goto err;
			}

			data.ce[i] = tmp;
		}

		for (i = 0; i < ARRAY_SIZE(poison); i++) {
			data.poison = poison[i];

			err = __lrc_timestamp(&data, false);
			if (err)
				break;

			err = __lrc_timestamp(&data, true);
			if (err)
				break;
		}

err:
		st_engine_heartbeat_enable(data.engine);
		for (i = 0; i < ARRAY_SIZE(data.ce); i++) {
			if (!data.ce[i])
				break;

			intel_context_unpin(data.ce[i]);
			intel_context_put(data.ce[i]);
		}

		if (igt_flush_test(gt->i915))
			err = -EIO;
		if (err)
			return err;
	}

	return 0;
}

static struct i915_vma *
create_user_vma(struct i915_address_space *vm, unsigned long size)
{
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	int err;

	obj = i915_gem_object_create_internal(vm->i915, size);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	vma = i915_vma_instance(obj, vm, NULL);
	if (IS_ERR(vma)) {
		i915_gem_object_put(obj);
		return vma;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_USER);
	if (err) {
		i915_gem_object_put(obj);
		return ERR_PTR(err);
	}

	return vma;
}

static struct i915_vma *
store_context(struct intel_context *ce, struct i915_vma *scratch)
{
	struct i915_vma *batch;
	u32 dw, x, *cs, *hw;
	u32 *defaults;

	batch = create_user_vma(ce->vm, SZ_64K);
	if (IS_ERR(batch))
		return batch;

	cs = i915_gem_object_pin_map_unlocked(batch->obj, I915_MAP_WC);
	if (IS_ERR(cs)) {
		i915_vma_put(batch);
		return ERR_CAST(cs);
	}

	defaults = shmem_pin_map(ce->engine->default_state);
	if (!defaults) {
		i915_gem_object_unpin_map(batch->obj);
		i915_vma_put(batch);
		return ERR_PTR(-ENOMEM);
	}

	x = 0;
	dw = 0;
	hw = defaults;
	hw += LRC_STATE_OFFSET / sizeof(*hw);
	do {
		u32 len = hw[dw] & 0x7f;

		if (hw[dw] == 0) {
			dw++;
			continue;
		}

		if ((hw[dw] & GENMASK(31, 23)) != MI_INSTR(0x22, 0)) {
			dw += len + 2;
			continue;
		}

		dw++;
		len = (len + 1) / 2;
		while (len--) {
			*cs++ = MI_STORE_REGISTER_MEM_GEN8;
			*cs++ = hw[dw];
			*cs++ = lower_32_bits(scratch->node.start + x);
			*cs++ = upper_32_bits(scratch->node.start + x);

			dw += 2;
			x += 4;
		}
	} while (dw < PAGE_SIZE / sizeof(u32) &&
		 (hw[dw] & ~BIT(0)) != MI_BATCH_BUFFER_END);

	*cs++ = MI_BATCH_BUFFER_END;

	shmem_unpin_map(ce->engine->default_state, defaults);

	i915_gem_object_flush_map(batch->obj);
	i915_gem_object_unpin_map(batch->obj);

	return batch;
}

static int move_to_active(struct i915_request *rq,
			  struct i915_vma *vma,
			  unsigned int flags)
{
	int err;

	i915_vma_lock(vma);
	err = i915_request_await_object(rq, vma->obj, flags);
	if (!err)
		err = i915_vma_move_to_active(vma, rq, flags);
	i915_vma_unlock(vma);

	return err;
}

static struct i915_request *
record_registers(struct intel_context *ce,
		 struct i915_vma *before,
		 struct i915_vma *after,
		 u32 *sema)
{
	struct i915_vma *b_before, *b_after;
	struct i915_request *rq;
	u32 *cs;
	int err;

	b_before = store_context(ce, before);
	if (IS_ERR(b_before))
		return ERR_CAST(b_before);

	b_after = store_context(ce, after);
	if (IS_ERR(b_after)) {
		rq = ERR_CAST(b_after);
		goto err_before;
	}

	rq = intel_context_create_request(ce);
	if (IS_ERR(rq))
		goto err_after;

	err = move_to_active(rq, before, EXEC_OBJECT_WRITE);
	if (err)
		goto err_rq;

	err = move_to_active(rq, b_before, 0);
	if (err)
		goto err_rq;

	err = move_to_active(rq, after, EXEC_OBJECT_WRITE);
	if (err)
		goto err_rq;

	err = move_to_active(rq, b_after, 0);
	if (err)
		goto err_rq;

	cs = intel_ring_begin(rq, 14);
	if (IS_ERR(cs)) {
		err = PTR_ERR(cs);
		goto err_rq;
	}

	*cs++ = MI_ARB_ON_OFF | MI_ARB_DISABLE;
	*cs++ = MI_BATCH_BUFFER_START_GEN8 | BIT(8);
	*cs++ = lower_32_bits(b_before->node.start);
	*cs++ = upper_32_bits(b_before->node.start);

	*cs++ = MI_ARB_ON_OFF | MI_ARB_ENABLE;
	*cs++ = MI_SEMAPHORE_WAIT |
		MI_SEMAPHORE_GLOBAL_GTT |
		MI_SEMAPHORE_POLL |
		MI_SEMAPHORE_SAD_NEQ_SDD;
	*cs++ = 0;
	*cs++ = i915_ggtt_offset(ce->engine->status_page.vma) +
		offset_in_page(sema);
	*cs++ = 0;
	*cs++ = MI_NOOP;

	*cs++ = MI_ARB_ON_OFF | MI_ARB_DISABLE;
	*cs++ = MI_BATCH_BUFFER_START_GEN8 | BIT(8);
	*cs++ = lower_32_bits(b_after->node.start);
	*cs++ = upper_32_bits(b_after->node.start);

	intel_ring_advance(rq, cs);

	WRITE_ONCE(*sema, 0);
	i915_request_get(rq);
	i915_request_add(rq);
err_after:
	i915_vma_put(b_after);
err_before:
	i915_vma_put(b_before);
	return rq;

err_rq:
	i915_request_add(rq);
	rq = ERR_PTR(err);
	goto err_after;
}

static struct i915_vma *load_context(struct intel_context *ce, u32 poison)
{
	struct i915_vma *batch;
	u32 dw, *cs, *hw;
	u32 *defaults;

	batch = create_user_vma(ce->vm, SZ_64K);
	if (IS_ERR(batch))
		return batch;

	cs = i915_gem_object_pin_map_unlocked(batch->obj, I915_MAP_WC);
	if (IS_ERR(cs)) {
		i915_vma_put(batch);
		return ERR_CAST(cs);
	}

	defaults = shmem_pin_map(ce->engine->default_state);
	if (!defaults) {
		i915_gem_object_unpin_map(batch->obj);
		i915_vma_put(batch);
		return ERR_PTR(-ENOMEM);
	}

	dw = 0;
	hw = defaults;
	hw += LRC_STATE_OFFSET / sizeof(*hw);
	do {
		u32 len = hw[dw] & 0x7f;

		if (hw[dw] == 0) {
			dw++;
			continue;
		}

		if ((hw[dw] & GENMASK(31, 23)) != MI_INSTR(0x22, 0)) {
			dw += len + 2;
			continue;
		}

		dw++;
		len = (len + 1) / 2;
		*cs++ = MI_LOAD_REGISTER_IMM(len);
		while (len--) {
			*cs++ = hw[dw];
			*cs++ = poison;
			dw += 2;
		}
	} while (dw < PAGE_SIZE / sizeof(u32) &&
		 (hw[dw] & ~BIT(0)) != MI_BATCH_BUFFER_END);

	*cs++ = MI_BATCH_BUFFER_END;

	shmem_unpin_map(ce->engine->default_state, defaults);

	i915_gem_object_flush_map(batch->obj);
	i915_gem_object_unpin_map(batch->obj);

	return batch;
}

static int poison_registers(struct intel_context *ce, u32 poison, u32 *sema)
{
	struct i915_request *rq;
	struct i915_vma *batch;
	u32 *cs;
	int err;

	batch = load_context(ce, poison);
	if (IS_ERR(batch))
		return PTR_ERR(batch);

	rq = intel_context_create_request(ce);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_batch;
	}

	err = move_to_active(rq, batch, 0);
	if (err)
		goto err_rq;

	cs = intel_ring_begin(rq, 8);
	if (IS_ERR(cs)) {
		err = PTR_ERR(cs);
		goto err_rq;
	}

	*cs++ = MI_ARB_ON_OFF | MI_ARB_DISABLE;
	*cs++ = MI_BATCH_BUFFER_START_GEN8 | BIT(8);
	*cs++ = lower_32_bits(batch->node.start);
	*cs++ = upper_32_bits(batch->node.start);

	*cs++ = MI_STORE_DWORD_IMM_GEN4 | MI_USE_GGTT;
	*cs++ = i915_ggtt_offset(ce->engine->status_page.vma) +
		offset_in_page(sema);
	*cs++ = 0;
	*cs++ = 1;

	intel_ring_advance(rq, cs);

	rq->sched.attr.priority = I915_PRIORITY_BARRIER;
err_rq:
	i915_request_add(rq);
err_batch:
	i915_vma_put(batch);
	return err;
}

static bool is_moving(u32 a, u32 b)
{
	return a != b;
}

static int compare_isolation(struct intel_engine_cs *engine,
			     struct i915_vma *ref[2],
			     struct i915_vma *result[2],
			     struct intel_context *ce,
			     u32 poison)
{
	u32 x, dw, *hw, *lrc;
	u32 *A[2], *B[2];
	u32 *defaults;
	int err = 0;

	A[0] = i915_gem_object_pin_map_unlocked(ref[0]->obj, I915_MAP_WC);
	if (IS_ERR(A[0]))
		return PTR_ERR(A[0]);

	A[1] = i915_gem_object_pin_map_unlocked(ref[1]->obj, I915_MAP_WC);
	if (IS_ERR(A[1])) {
		err = PTR_ERR(A[1]);
		goto err_A0;
	}

	B[0] = i915_gem_object_pin_map_unlocked(result[0]->obj, I915_MAP_WC);
	if (IS_ERR(B[0])) {
		err = PTR_ERR(B[0]);
		goto err_A1;
	}

	B[1] = i915_gem_object_pin_map_unlocked(result[1]->obj, I915_MAP_WC);
	if (IS_ERR(B[1])) {
		err = PTR_ERR(B[1]);
		goto err_B0;
	}

	lrc = i915_gem_object_pin_map_unlocked(ce->state->obj,
					       i915_coherent_map_type(engine->i915,
								      ce->state->obj,
								      false));
	if (IS_ERR(lrc)) {
		err = PTR_ERR(lrc);
		goto err_B1;
	}
	lrc += LRC_STATE_OFFSET / sizeof(*hw);

	defaults = shmem_pin_map(ce->engine->default_state);
	if (!defaults) {
		err = -ENOMEM;
		goto err_lrc;
	}

	x = 0;
	dw = 0;
	hw = defaults;
	hw += LRC_STATE_OFFSET / sizeof(*hw);
	do {
		u32 len = hw[dw] & 0x7f;

		if (hw[dw] == 0) {
			dw++;
			continue;
		}

		if ((hw[dw] & GENMASK(31, 23)) != MI_INSTR(0x22, 0)) {
			dw += len + 2;
			continue;
		}

		dw++;
		len = (len + 1) / 2;
		while (len--) {
			if (!is_moving(A[0][x], A[1][x]) &&
			    (A[0][x] != B[0][x] || A[1][x] != B[1][x])) {
				switch (hw[dw] & 4095) {
				case 0x30: /* RING_HEAD */
				case 0x34: /* RING_TAIL */
					break;

				default:
					pr_err("%s[%d]: Mismatch for register %4x, default %08x, reference %08x, result (%08x, %08x), poison %08x, context %08x\n",
					       engine->name, dw,
					       hw[dw], hw[dw + 1],
					       A[0][x], B[0][x], B[1][x],
					       poison, lrc[dw + 1]);
					err = -EINVAL;
				}
			}
			dw += 2;
			x++;
		}
	} while (dw < PAGE_SIZE / sizeof(u32) &&
		 (hw[dw] & ~BIT(0)) != MI_BATCH_BUFFER_END);

	shmem_unpin_map(ce->engine->default_state, defaults);
err_lrc:
	i915_gem_object_unpin_map(ce->state->obj);
err_B1:
	i915_gem_object_unpin_map(result[1]->obj);
err_B0:
	i915_gem_object_unpin_map(result[0]->obj);
err_A1:
	i915_gem_object_unpin_map(ref[1]->obj);
err_A0:
	i915_gem_object_unpin_map(ref[0]->obj);
	return err;
}

static int __lrc_isolation(struct intel_engine_cs *engine, u32 poison)
{
	u32 *sema = memset32(engine->status_page.addr + 1000, 0, 1);
	struct i915_vma *ref[2], *result[2];
	struct intel_context *A, *B;
	struct i915_request *rq;
	int err;

	A = intel_context_create(engine);
	if (IS_ERR(A))
		return PTR_ERR(A);

	B = intel_context_create(engine);
	if (IS_ERR(B)) {
		err = PTR_ERR(B);
		goto err_A;
	}

	ref[0] = create_user_vma(A->vm, SZ_64K);
	if (IS_ERR(ref[0])) {
		err = PTR_ERR(ref[0]);
		goto err_B;
	}

	ref[1] = create_user_vma(A->vm, SZ_64K);
	if (IS_ERR(ref[1])) {
		err = PTR_ERR(ref[1]);
		goto err_ref0;
	}

	rq = record_registers(A, ref[0], ref[1], sema);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_ref1;
	}

	WRITE_ONCE(*sema, 1);
	wmb();

	if (i915_request_wait(rq, 0, HZ / 2) < 0) {
		i915_request_put(rq);
		err = -ETIME;
		goto err_ref1;
	}
	i915_request_put(rq);

	result[0] = create_user_vma(A->vm, SZ_64K);
	if (IS_ERR(result[0])) {
		err = PTR_ERR(result[0]);
		goto err_ref1;
	}

	result[1] = create_user_vma(A->vm, SZ_64K);
	if (IS_ERR(result[1])) {
		err = PTR_ERR(result[1]);
		goto err_result0;
	}

	rq = record_registers(A, result[0], result[1], sema);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_result1;
	}

	err = poison_registers(B, poison, sema);
	if (err) {
		WRITE_ONCE(*sema, -1);
		i915_request_put(rq);
		goto err_result1;
	}

	if (i915_request_wait(rq, 0, HZ / 2) < 0) {
		i915_request_put(rq);
		err = -ETIME;
		goto err_result1;
	}
	i915_request_put(rq);

	err = compare_isolation(engine, ref, result, A, poison);

err_result1:
	i915_vma_put(result[1]);
err_result0:
	i915_vma_put(result[0]);
err_ref1:
	i915_vma_put(ref[1]);
err_ref0:
	i915_vma_put(ref[0]);
err_B:
	intel_context_put(B);
err_A:
	intel_context_put(A);
	return err;
}

static bool skip_isolation(const struct intel_engine_cs *engine)
{
	if (engine->class == COPY_ENGINE_CLASS && GRAPHICS_VER(engine->i915) == 9)
		return true;

	if (engine->class == RENDER_CLASS && GRAPHICS_VER(engine->i915) == 11)
		return true;

	return false;
}

static int live_lrc_isolation(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	const u32 poison[] = {
		STACK_MAGIC,
		0x3a3a3a3a,
		0x5c5c5c5c,
		0xffffffff,
		0xffff0000,
	};
	int err = 0;

	/*
	 * Our goal is try and verify that per-context state cannot be
	 * tampered with by another non-privileged client.
	 *
	 * We take the list of context registers from the LRI in the default
	 * context image and attempt to modify that list from a remote context.
	 */

	for_each_engine(engine, gt, id) {
		int i;

		/* Just don't even ask */
		if (!IS_ENABLED(CONFIG_DRM_I915_SELFTEST_BROKEN) &&
		    skip_isolation(engine))
			continue;

		intel_engine_pm_get(engine);
		for (i = 0; i < ARRAY_SIZE(poison); i++) {
			int result;

			result = __lrc_isolation(engine, poison[i]);
			if (result && !err)
				err = result;

			result = __lrc_isolation(engine, ~poison[i]);
			if (result && !err)
				err = result;
		}
		intel_engine_pm_put(engine);
		if (igt_flush_test(gt->i915)) {
			err = -EIO;
			break;
		}
	}

	return err;
}

static int indirect_ctx_submit_req(struct intel_context *ce)
{
	struct i915_request *rq;
	int err = 0;

	rq = intel_context_create_request(ce);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	i915_request_get(rq);
	i915_request_add(rq);

	if (i915_request_wait(rq, 0, HZ / 5) < 0)
		err = -ETIME;

	i915_request_put(rq);

	return err;
}

#define CTX_BB_CANARY_OFFSET (3 * 1024)
#define CTX_BB_CANARY_INDEX  (CTX_BB_CANARY_OFFSET / sizeof(u32))

static u32 *
emit_indirect_ctx_bb_canary(const struct intel_context *ce, u32 *cs)
{
	*cs++ = MI_STORE_REGISTER_MEM_GEN8 |
		MI_SRM_LRM_GLOBAL_GTT |
		MI_LRI_LRM_CS_MMIO;
	*cs++ = i915_mmio_reg_offset(RING_START(0));
	*cs++ = i915_ggtt_offset(ce->state) +
		context_wa_bb_offset(ce) +
		CTX_BB_CANARY_OFFSET;
	*cs++ = 0;

	return cs;
}

static void
indirect_ctx_bb_setup(struct intel_context *ce)
{
	u32 *cs = context_indirect_bb(ce);

	cs[CTX_BB_CANARY_INDEX] = 0xdeadf00d;

	setup_indirect_ctx_bb(ce, ce->engine, emit_indirect_ctx_bb_canary);
}

static bool check_ring_start(struct intel_context *ce)
{
	const u32 * const ctx_bb = (void *)(ce->lrc_reg_state) -
		LRC_STATE_OFFSET + context_wa_bb_offset(ce);

	if (ctx_bb[CTX_BB_CANARY_INDEX] == ce->lrc_reg_state[CTX_RING_START])
		return true;

	pr_err("ring start mismatch: canary 0x%08x vs state 0x%08x\n",
	       ctx_bb[CTX_BB_CANARY_INDEX],
	       ce->lrc_reg_state[CTX_RING_START]);

	return false;
}

static int indirect_ctx_bb_check(struct intel_context *ce)
{
	int err;

	err = indirect_ctx_submit_req(ce);
	if (err)
		return err;

	if (!check_ring_start(ce))
		return -EINVAL;

	return 0;
}

static int __live_lrc_indirect_ctx_bb(struct intel_engine_cs *engine)
{
	struct intel_context *a, *b;
	int err;

	a = intel_context_create(engine);
	if (IS_ERR(a))
		return PTR_ERR(a);
	err = intel_context_pin(a);
	if (err)
		goto put_a;

	b = intel_context_create(engine);
	if (IS_ERR(b)) {
		err = PTR_ERR(b);
		goto unpin_a;
	}
	err = intel_context_pin(b);
	if (err)
		goto put_b;

	/* We use the already reserved extra page in context state */
	if (!a->wa_bb_page) {
		GEM_BUG_ON(b->wa_bb_page);
		GEM_BUG_ON(GRAPHICS_VER(engine->i915) == 12);
		goto unpin_b;
	}

	/*
	 * In order to test that our per context bb is truly per context,
	 * and executes at the intended spot on context restoring process,
	 * make the batch store the ring start value to memory.
	 * As ring start is restored apriori of starting the indirect ctx bb and
	 * as it will be different for each context, it fits to this purpose.
	 */
	indirect_ctx_bb_setup(a);
	indirect_ctx_bb_setup(b);

	err = indirect_ctx_bb_check(a);
	if (err)
		goto unpin_b;

	err = indirect_ctx_bb_check(b);

unpin_b:
	intel_context_unpin(b);
put_b:
	intel_context_put(b);
unpin_a:
	intel_context_unpin(a);
put_a:
	intel_context_put(a);

	return err;
}

static int live_lrc_indirect_ctx_bb(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err = 0;

	for_each_engine(engine, gt, id) {
		intel_engine_pm_get(engine);
		err = __live_lrc_indirect_ctx_bb(engine);
		intel_engine_pm_put(engine);

		if (igt_flush_test(gt->i915))
			err = -EIO;

		if (err)
			break;
	}

	return err;
}

static void garbage_reset(struct intel_engine_cs *engine,
			  struct i915_request *rq)
{
	const unsigned int bit = I915_RESET_ENGINE + engine->id;
	unsigned long *lock = &engine->gt->reset.flags;

	local_bh_disable();
	if (!test_and_set_bit(bit, lock)) {
		tasklet_disable(&engine->sched_engine->tasklet);

		if (!rq->fence.error)
			__intel_engine_reset_bh(engine, NULL);

		tasklet_enable(&engine->sched_engine->tasklet);
		clear_and_wake_up_bit(bit, lock);
	}
	local_bh_enable();
}

static struct i915_request *garbage(struct intel_context *ce,
				    struct rnd_state *prng)
{
	struct i915_request *rq;
	int err;

	err = intel_context_pin(ce);
	if (err)
		return ERR_PTR(err);

	prandom_bytes_state(prng,
			    ce->lrc_reg_state,
			    ce->engine->context_size -
			    LRC_STATE_OFFSET);

	rq = intel_context_create_request(ce);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_unpin;
	}

	i915_request_get(rq);
	i915_request_add(rq);
	return rq;

err_unpin:
	intel_context_unpin(ce);
	return ERR_PTR(err);
}

static int __lrc_garbage(struct intel_engine_cs *engine, struct rnd_state *prng)
{
	struct intel_context *ce;
	struct i915_request *hang;
	int err = 0;

	ce = intel_context_create(engine);
	if (IS_ERR(ce))
		return PTR_ERR(ce);

	hang = garbage(ce, prng);
	if (IS_ERR(hang)) {
		err = PTR_ERR(hang);
		goto err_ce;
	}

	if (wait_for_submit(engine, hang, HZ / 2)) {
		i915_request_put(hang);
		err = -ETIME;
		goto err_ce;
	}

	intel_context_set_banned(ce);
	garbage_reset(engine, hang);

	intel_engine_flush_submission(engine);
	if (!hang->fence.error) {
		i915_request_put(hang);
		pr_err("%s: corrupted context was not reset\n",
		       engine->name);
		err = -EINVAL;
		goto err_ce;
	}

	if (i915_request_wait(hang, 0, HZ / 2) < 0) {
		pr_err("%s: corrupted context did not recover\n",
		       engine->name);
		i915_request_put(hang);
		err = -EIO;
		goto err_ce;
	}
	i915_request_put(hang);

err_ce:
	intel_context_put(ce);
	return err;
}

static int live_lrc_garbage(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	/*
	 * Verify that we can recover if one context state is completely
	 * corrupted.
	 */

	if (!IS_ENABLED(CONFIG_DRM_I915_SELFTEST_BROKEN))
		return 0;

	for_each_engine(engine, gt, id) {
		I915_RND_STATE(prng);
		int err = 0, i;

		if (!intel_has_reset_engine(engine->gt))
			continue;

		intel_engine_pm_get(engine);
		for (i = 0; i < 3; i++) {
			err = __lrc_garbage(engine, &prng);
			if (err)
				break;
		}
		intel_engine_pm_put(engine);

		if (igt_flush_test(gt->i915))
			err = -EIO;
		if (err)
			return err;
	}

	return 0;
}

static int __live_pphwsp_runtime(struct intel_engine_cs *engine)
{
	struct intel_context *ce;
	struct i915_request *rq;
	IGT_TIMEOUT(end_time);
	int err;

	ce = intel_context_create(engine);
	if (IS_ERR(ce))
		return PTR_ERR(ce);

	ce->runtime.num_underflow = 0;
	ce->runtime.max_underflow = 0;

	do {
		unsigned int loop = 1024;

		while (loop) {
			rq = intel_context_create_request(ce);
			if (IS_ERR(rq)) {
				err = PTR_ERR(rq);
				goto err_rq;
			}

			if (--loop == 0)
				i915_request_get(rq);

			i915_request_add(rq);
		}

		if (__igt_timeout(end_time, NULL))
			break;

		i915_request_put(rq);
	} while (1);

	err = i915_request_wait(rq, 0, HZ / 5);
	if (err < 0) {
		pr_err("%s: request not completed!\n", engine->name);
		goto err_wait;
	}

	igt_flush_test(engine->i915);

	pr_info("%s: pphwsp runtime %lluns, average %lluns\n",
		engine->name,
		intel_context_get_total_runtime_ns(ce),
		intel_context_get_avg_runtime_ns(ce));

	err = 0;
	if (ce->runtime.num_underflow) {
		pr_err("%s: pphwsp underflow %u time(s), max %u cycles!\n",
		       engine->name,
		       ce->runtime.num_underflow,
		       ce->runtime.max_underflow);
		GEM_TRACE_DUMP();
		err = -EOVERFLOW;
	}

err_wait:
	i915_request_put(rq);
err_rq:
	intel_context_put(ce);
	return err;
}

static int live_pphwsp_runtime(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err = 0;

	/*
	 * Check that cumulative context runtime as stored in the pphwsp[16]
	 * is monotonic.
	 */

	for_each_engine(engine, gt, id) {
		err = __live_pphwsp_runtime(engine);
		if (err)
			break;
	}

	if (igt_flush_test(gt->i915))
		err = -EIO;

	return err;
}

int intel_lrc_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_lrc_layout),
		SUBTEST(live_lrc_fixed),
		SUBTEST(live_lrc_state),
		SUBTEST(live_lrc_gpr),
		SUBTEST(live_lrc_isolation),
		SUBTEST(live_lrc_timestamp),
		SUBTEST(live_lrc_garbage),
		SUBTEST(live_pphwsp_runtime),
		SUBTEST(live_lrc_indirect_ctx_bb),
	};

	if (!HAS_LOGICAL_RING_CONTEXTS(i915))
		return 0;

	return intel_gt_live_subtests(tests, to_gt(i915));
}
