// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include <drm/drm_managed.h>
#include <drm/intel-gtt.h>

#include "gem/i915_gem_internal.h"
#include "gem/i915_gem_lmem.h"

#include "i915_drv.h"
#include "i915_perf_oa_regs.h"
#include "i915_reg.h"
#include "intel_context.h"
#include "intel_engine_pm.h"
#include "intel_engine_regs.h"
#include "intel_ggtt_gmch.h"
#include "intel_gt.h"
#include "intel_gt_buffer_pool.h"
#include "intel_gt_clock_utils.h"
#include "intel_gt_debugfs.h"
#include "intel_gt_mcr.h"
#include "intel_gt_pm.h"
#include "intel_gt_print.h"
#include "intel_gt_regs.h"
#include "intel_gt_requests.h"
#include "intel_migrate.h"
#include "intel_mocs.h"
#include "intel_pci_config.h"
#include "intel_pm.h"
#include "intel_rc6.h"
#include "intel_renderstate.h"
#include "intel_rps.h"
#include "intel_sa_media.h"
#include "intel_gt_sysfs.h"
#include "intel_uncore.h"
#include "shmem_utils.h"

void intel_gt_common_init_early(struct intel_gt *gt)
{
	spin_lock_init(gt->irq_lock);

	INIT_LIST_HEAD(&gt->closed_vma);
	spin_lock_init(&gt->closed_lock);

	init_llist_head(&gt->watchdog.list);
	INIT_WORK(&gt->watchdog.work, intel_gt_watchdog_work);

	intel_gt_init_buffer_pool(gt);
	intel_gt_init_reset(gt);
	intel_gt_init_requests(gt);
	intel_gt_init_timelines(gt);
	mutex_init(&gt->tlb.invalidate_lock);
	seqcount_mutex_init(&gt->tlb.seqno, &gt->tlb.invalidate_lock);
	intel_gt_pm_init_early(gt);

	intel_wopcm_init_early(&gt->wopcm);
	intel_uc_init_early(&gt->uc);
	intel_rps_init_early(&gt->rps);
}

/* Preliminary initialization of Tile 0 */
int intel_root_gt_init_early(struct drm_i915_private *i915)
{
	struct intel_gt *gt = to_gt(i915);

	gt->i915 = i915;
	gt->uncore = &i915->uncore;
	gt->irq_lock = drmm_kzalloc(&i915->drm, sizeof(*gt->irq_lock), GFP_KERNEL);
	if (!gt->irq_lock)
		return -ENOMEM;

	intel_gt_common_init_early(gt);

	return 0;
}

static int intel_gt_probe_lmem(struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;
	unsigned int instance = gt->info.id;
	int id = INTEL_REGION_LMEM_0 + instance;
	struct intel_memory_region *mem;
	int err;

	mem = intel_gt_setup_lmem(gt);
	if (IS_ERR(mem)) {
		err = PTR_ERR(mem);
		if (err == -ENODEV)
			return 0;

		gt_err(gt, "Failed to setup region(%d) type=%d\n",
		       err, INTEL_MEMORY_LOCAL);
		return err;
	}

	mem->id = id;
	mem->instance = instance;

	intel_memory_region_set_name(mem, "local%u", mem->instance);

	GEM_BUG_ON(!HAS_REGION(i915, id));
	GEM_BUG_ON(i915->mm.regions[id]);
	i915->mm.regions[id] = mem;

	return 0;
}

int intel_gt_assign_ggtt(struct intel_gt *gt)
{
	/* Media GT shares primary GT's GGTT */
	if (gt->type == GT_MEDIA) {
		gt->ggtt = to_gt(gt->i915)->ggtt;
	} else {
		gt->ggtt = i915_ggtt_create(gt->i915);
		if (IS_ERR(gt->ggtt))
			return PTR_ERR(gt->ggtt);
	}

	list_add_tail(&gt->ggtt_link, &gt->ggtt->gt_list);

	return 0;
}

int intel_gt_init_mmio(struct intel_gt *gt)
{
	intel_gt_init_clock_frequency(gt);

	intel_uc_init_mmio(&gt->uc);
	intel_sseu_info_init(gt);
	intel_gt_mcr_init(gt);

	return intel_engines_init_mmio(gt);
}

static void init_unused_ring(struct intel_gt *gt, u32 base)
{
	struct intel_uncore *uncore = gt->uncore;

	intel_uncore_write(uncore, RING_CTL(base), 0);
	intel_uncore_write(uncore, RING_HEAD(base), 0);
	intel_uncore_write(uncore, RING_TAIL(base), 0);
	intel_uncore_write(uncore, RING_START(base), 0);
}

static void init_unused_rings(struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;

	if (IS_I830(i915)) {
		init_unused_ring(gt, PRB1_BASE);
		init_unused_ring(gt, SRB0_BASE);
		init_unused_ring(gt, SRB1_BASE);
		init_unused_ring(gt, SRB2_BASE);
		init_unused_ring(gt, SRB3_BASE);
	} else if (GRAPHICS_VER(i915) == 2) {
		init_unused_ring(gt, SRB0_BASE);
		init_unused_ring(gt, SRB1_BASE);
	} else if (GRAPHICS_VER(i915) == 3) {
		init_unused_ring(gt, PRB1_BASE);
		init_unused_ring(gt, PRB2_BASE);
	}
}

int intel_gt_init_hw(struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;
	struct intel_uncore *uncore = gt->uncore;
	int ret;

	gt->last_init_time = ktime_get();

	/* Double layer security blanket, see i915_gem_init() */
	intel_uncore_forcewake_get(uncore, FORCEWAKE_ALL);

	if (HAS_EDRAM(i915) && GRAPHICS_VER(i915) < 9)
		intel_uncore_rmw(uncore, HSW_IDICR, 0, IDIHASHMSK(0xf));

	if (IS_HASWELL(i915))
		intel_uncore_write(uncore,
				   HSW_MI_PREDICATE_RESULT_2,
				   IS_HSW_GT3(i915) ?
				   LOWER_SLICE_ENABLED : LOWER_SLICE_DISABLED);

	/* Apply the GT workarounds... */
	intel_gt_apply_workarounds(gt);
	/* ...and determine whether they are sticking. */
	intel_gt_verify_workarounds(gt, "init");

	intel_gt_init_swizzling(gt);

	/*
	 * At least 830 can leave some of the unused rings
	 * "active" (ie. head != tail) after resume which
	 * will prevent c3 entry. Makes sure all unused rings
	 * are totally idle.
	 */
	init_unused_rings(gt);

	ret = i915_ppgtt_init_hw(gt);
	if (ret) {
		gt_err(gt, "Enabling PPGTT failed (%d)\n", ret);
		goto out;
	}

	/* We can't enable contexts until all firmware is loaded */
	ret = intel_uc_init_hw(&gt->uc);
	if (ret) {
		gt_probe_error(gt, "Enabling uc failed (%d)\n", ret);
		goto out;
	}

	intel_mocs_init(gt);

out:
	intel_uncore_forcewake_put(uncore, FORCEWAKE_ALL);
	return ret;
}

static void gen6_clear_engine_error_register(struct intel_engine_cs *engine)
{
	GEN6_RING_FAULT_REG_RMW(engine, RING_FAULT_VALID, 0);
	GEN6_RING_FAULT_REG_POSTING_READ(engine);
}

i915_reg_t intel_gt_perf_limit_reasons_reg(struct intel_gt *gt)
{
	/* GT0_PERF_LIMIT_REASONS is available only for Gen11+ */
	if (GRAPHICS_VER(gt->i915) < 11)
		return INVALID_MMIO_REG;

	return gt->type == GT_MEDIA ?
		MTL_MEDIA_PERF_LIMIT_REASONS : GT0_PERF_LIMIT_REASONS;
}

void
intel_gt_clear_error_registers(struct intel_gt *gt,
			       intel_engine_mask_t engine_mask)
{
	struct drm_i915_private *i915 = gt->i915;
	struct intel_uncore *uncore = gt->uncore;
	u32 eir;

	if (GRAPHICS_VER(i915) != 2)
		intel_uncore_write(uncore, PGTBL_ER, 0);

	if (GRAPHICS_VER(i915) < 4)
		intel_uncore_write(uncore, IPEIR(RENDER_RING_BASE), 0);
	else
		intel_uncore_write(uncore, IPEIR_I965, 0);

	intel_uncore_write(uncore, EIR, 0);
	eir = intel_uncore_read(uncore, EIR);
	if (eir) {
		/*
		 * some errors might have become stuck,
		 * mask them.
		 */
		gt_dbg(gt, "EIR stuck: 0x%08x, masking\n", eir);
		intel_uncore_rmw(uncore, EMR, 0, eir);
		intel_uncore_write(uncore, GEN2_IIR,
				   I915_MASTER_ERROR_INTERRUPT);
	}

	if (GRAPHICS_VER_FULL(i915) >= IP_VER(12, 50)) {
		intel_gt_mcr_multicast_rmw(gt, XEHP_RING_FAULT_REG,
					   RING_FAULT_VALID, 0);
		intel_gt_mcr_read_any(gt, XEHP_RING_FAULT_REG);
	} else if (GRAPHICS_VER(i915) >= 12) {
		intel_uncore_rmw(uncore, GEN12_RING_FAULT_REG, RING_FAULT_VALID, 0);
		intel_uncore_posting_read(uncore, GEN12_RING_FAULT_REG);
	} else if (GRAPHICS_VER(i915) >= 8) {
		intel_uncore_rmw(uncore, GEN8_RING_FAULT_REG, RING_FAULT_VALID, 0);
		intel_uncore_posting_read(uncore, GEN8_RING_FAULT_REG);
	} else if (GRAPHICS_VER(i915) >= 6) {
		struct intel_engine_cs *engine;
		enum intel_engine_id id;

		for_each_engine_masked(engine, gt, engine_mask, id)
			gen6_clear_engine_error_register(engine);
	}
}

static void gen6_check_faults(struct intel_gt *gt)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	u32 fault;

	for_each_engine(engine, gt, id) {
		fault = GEN6_RING_FAULT_REG_READ(engine);
		if (fault & RING_FAULT_VALID) {
			gt_dbg(gt, "Unexpected fault\n"
			       "\tAddr: 0x%08lx\n"
			       "\tAddress space: %s\n"
			       "\tSource ID: %d\n"
			       "\tType: %d\n",
			       fault & PAGE_MASK,
			       fault & RING_FAULT_GTTSEL_MASK ?
			       "GGTT" : "PPGTT",
			       RING_FAULT_SRCID(fault),
			       RING_FAULT_FAULT_TYPE(fault));
		}
	}
}

static void xehp_check_faults(struct intel_gt *gt)
{
	u32 fault;

	/*
	 * Although the fault register now lives in an MCR register range,
	 * the GAM registers are special and we only truly need to read
	 * the "primary" GAM instance rather than handling each instance
	 * individually.  intel_gt_mcr_read_any() will automatically steer
	 * toward the primary instance.
	 */
	fault = intel_gt_mcr_read_any(gt, XEHP_RING_FAULT_REG);
	if (fault & RING_FAULT_VALID) {
		u32 fault_data0, fault_data1;
		u64 fault_addr;

		fault_data0 = intel_gt_mcr_read_any(gt, XEHP_FAULT_TLB_DATA0);
		fault_data1 = intel_gt_mcr_read_any(gt, XEHP_FAULT_TLB_DATA1);

		fault_addr = ((u64)(fault_data1 & FAULT_VA_HIGH_BITS) << 44) |
			     ((u64)fault_data0 << 12);

		gt_dbg(gt, "Unexpected fault\n"
		       "\tAddr: 0x%08x_%08x\n"
		       "\tAddress space: %s\n"
		       "\tEngine ID: %d\n"
		       "\tSource ID: %d\n"
		       "\tType: %d\n",
		       upper_32_bits(fault_addr), lower_32_bits(fault_addr),
		       fault_data1 & FAULT_GTT_SEL ? "GGTT" : "PPGTT",
		       GEN8_RING_FAULT_ENGINE_ID(fault),
		       RING_FAULT_SRCID(fault),
		       RING_FAULT_FAULT_TYPE(fault));
	}
}

static void gen8_check_faults(struct intel_gt *gt)
{
	struct intel_uncore *uncore = gt->uncore;
	i915_reg_t fault_reg, fault_data0_reg, fault_data1_reg;
	u32 fault;

	if (GRAPHICS_VER(gt->i915) >= 12) {
		fault_reg = GEN12_RING_FAULT_REG;
		fault_data0_reg = GEN12_FAULT_TLB_DATA0;
		fault_data1_reg = GEN12_FAULT_TLB_DATA1;
	} else {
		fault_reg = GEN8_RING_FAULT_REG;
		fault_data0_reg = GEN8_FAULT_TLB_DATA0;
		fault_data1_reg = GEN8_FAULT_TLB_DATA1;
	}

	fault = intel_uncore_read(uncore, fault_reg);
	if (fault & RING_FAULT_VALID) {
		u32 fault_data0, fault_data1;
		u64 fault_addr;

		fault_data0 = intel_uncore_read(uncore, fault_data0_reg);
		fault_data1 = intel_uncore_read(uncore, fault_data1_reg);

		fault_addr = ((u64)(fault_data1 & FAULT_VA_HIGH_BITS) << 44) |
			     ((u64)fault_data0 << 12);

		gt_dbg(gt, "Unexpected fault\n"
		       "\tAddr: 0x%08x_%08x\n"
		       "\tAddress space: %s\n"
		       "\tEngine ID: %d\n"
		       "\tSource ID: %d\n"
		       "\tType: %d\n",
		       upper_32_bits(fault_addr), lower_32_bits(fault_addr),
		       fault_data1 & FAULT_GTT_SEL ? "GGTT" : "PPGTT",
		       GEN8_RING_FAULT_ENGINE_ID(fault),
		       RING_FAULT_SRCID(fault),
		       RING_FAULT_FAULT_TYPE(fault));
	}
}

void intel_gt_check_and_clear_faults(struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;

	/* From GEN8 onwards we only have one 'All Engine Fault Register' */
	if (GRAPHICS_VER_FULL(i915) >= IP_VER(12, 50))
		xehp_check_faults(gt);
	else if (GRAPHICS_VER(i915) >= 8)
		gen8_check_faults(gt);
	else if (GRAPHICS_VER(i915) >= 6)
		gen6_check_faults(gt);
	else
		return;

	intel_gt_clear_error_registers(gt, ALL_ENGINES);
}

void intel_gt_flush_ggtt_writes(struct intel_gt *gt)
{
	struct intel_uncore *uncore = gt->uncore;
	intel_wakeref_t wakeref;

	/*
	 * No actual flushing is required for the GTT write domain for reads
	 * from the GTT domain. Writes to it "immediately" go to main memory
	 * as far as we know, so there's no chipset flush. It also doesn't
	 * land in the GPU render cache.
	 *
	 * However, we do have to enforce the order so that all writes through
	 * the GTT land before any writes to the device, such as updates to
	 * the GATT itself.
	 *
	 * We also have to wait a bit for the writes to land from the GTT.
	 * An uncached read (i.e. mmio) seems to be ideal for the round-trip
	 * timing. This issue has only been observed when switching quickly
	 * between GTT writes and CPU reads from inside the kernel on recent hw,
	 * and it appears to only affect discrete GTT blocks (i.e. on LLC
	 * system agents we cannot reproduce this behaviour, until Cannonlake
	 * that was!).
	 */

	wmb();

	if (INTEL_INFO(gt->i915)->has_coherent_ggtt)
		return;

	intel_gt_chipset_flush(gt);

	with_intel_runtime_pm_if_in_use(uncore->rpm, wakeref) {
		unsigned long flags;

		spin_lock_irqsave(&uncore->lock, flags);
		intel_uncore_posting_read_fw(uncore,
					     RING_HEAD(RENDER_RING_BASE));
		spin_unlock_irqrestore(&uncore->lock, flags);
	}
}

void intel_gt_chipset_flush(struct intel_gt *gt)
{
	wmb();
	if (GRAPHICS_VER(gt->i915) < 6)
		intel_ggtt_gmch_flush();
}

void intel_gt_driver_register(struct intel_gt *gt)
{
	intel_gsc_init(&gt->gsc, gt->i915);

	intel_rps_driver_register(&gt->rps);

	intel_gt_debugfs_register(gt);
	intel_gt_sysfs_register(gt);
}

static int intel_gt_init_scratch(struct intel_gt *gt, unsigned int size)
{
	struct drm_i915_private *i915 = gt->i915;
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	int ret;

	obj = i915_gem_object_create_lmem(i915, size,
					  I915_BO_ALLOC_VOLATILE |
					  I915_BO_ALLOC_GPU_ONLY);
	if (IS_ERR(obj))
		obj = i915_gem_object_create_stolen(i915, size);
	if (IS_ERR(obj))
		obj = i915_gem_object_create_internal(i915, size);
	if (IS_ERR(obj)) {
		gt_err(gt, "Failed to allocate scratch page\n");
		return PTR_ERR(obj);
	}

	vma = i915_vma_instance(obj, &gt->ggtt->vm, NULL);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto err_unref;
	}

	ret = i915_ggtt_pin(vma, NULL, 0, PIN_HIGH);
	if (ret)
		goto err_unref;

	gt->scratch = i915_vma_make_unshrinkable(vma);

	return 0;

err_unref:
	i915_gem_object_put(obj);
	return ret;
}

static void intel_gt_fini_scratch(struct intel_gt *gt)
{
	i915_vma_unpin_and_release(&gt->scratch, 0);
}

static struct i915_address_space *kernel_vm(struct intel_gt *gt)
{
	if (INTEL_PPGTT(gt->i915) > INTEL_PPGTT_ALIASING)
		return &i915_ppgtt_create(gt, I915_BO_ALLOC_PM_EARLY)->vm;
	else
		return i915_vm_get(&gt->ggtt->vm);
}

static int __engines_record_defaults(struct intel_gt *gt)
{
	struct i915_request *requests[I915_NUM_ENGINES] = {};
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err = 0;

	/*
	 * As we reset the gpu during very early sanitisation, the current
	 * register state on the GPU should reflect its defaults values.
	 * We load a context onto the hw (with restore-inhibit), then switch
	 * over to a second context to save that default register state. We
	 * can then prime every new context with that state so they all start
	 * from the same default HW values.
	 */

	for_each_engine(engine, gt, id) {
		struct intel_renderstate so;
		struct intel_context *ce;
		struct i915_request *rq;

		/* We must be able to switch to something! */
		GEM_BUG_ON(!engine->kernel_context);

		ce = intel_context_create(engine);
		if (IS_ERR(ce)) {
			err = PTR_ERR(ce);
			goto out;
		}

		err = intel_renderstate_init(&so, ce);
		if (err)
			goto err;

		rq = i915_request_create(ce);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto err_fini;
		}

		err = intel_engine_emit_ctx_wa(rq);
		if (err)
			goto err_rq;

		err = intel_renderstate_emit(&so, rq);
		if (err)
			goto err_rq;

err_rq:
		requests[id] = i915_request_get(rq);
		i915_request_add(rq);
err_fini:
		intel_renderstate_fini(&so, ce);
err:
		if (err) {
			intel_context_put(ce);
			goto out;
		}
	}

	/* Flush the default context image to memory, and enable powersaving. */
	if (intel_gt_wait_for_idle(gt, I915_GEM_IDLE_TIMEOUT) == -ETIME) {
		err = -EIO;
		goto out;
	}

	for (id = 0; id < ARRAY_SIZE(requests); id++) {
		struct i915_request *rq;
		struct file *state;

		rq = requests[id];
		if (!rq)
			continue;

		if (rq->fence.error) {
			err = -EIO;
			goto out;
		}

		GEM_BUG_ON(!test_bit(CONTEXT_ALLOC_BIT, &rq->context->flags));
		if (!rq->context->state)
			continue;

		/* Keep a copy of the state's backing pages; free the obj */
		state = shmem_create_from_object(rq->context->state->obj);
		if (IS_ERR(state)) {
			err = PTR_ERR(state);
			goto out;
		}
		rq->engine->default_state = state;
	}

out:
	/*
	 * If we have to abandon now, we expect the engines to be idle
	 * and ready to be torn-down. The quickest way we can accomplish
	 * this is by declaring ourselves wedged.
	 */
	if (err)
		intel_gt_set_wedged(gt);

	for (id = 0; id < ARRAY_SIZE(requests); id++) {
		struct intel_context *ce;
		struct i915_request *rq;

		rq = requests[id];
		if (!rq)
			continue;

		ce = rq->context;
		i915_request_put(rq);
		intel_context_put(ce);
	}
	return err;
}

static int __engines_verify_workarounds(struct intel_gt *gt)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err = 0;

	if (!IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM))
		return 0;

	for_each_engine(engine, gt, id) {
		if (intel_engine_verify_workarounds(engine, "load"))
			err = -EIO;
	}

	/* Flush and restore the kernel context for safety */
	if (intel_gt_wait_for_idle(gt, I915_GEM_IDLE_TIMEOUT) == -ETIME)
		err = -EIO;

	return err;
}

static void __intel_gt_disable(struct intel_gt *gt)
{
	intel_gt_set_wedged_on_fini(gt);

	intel_gt_suspend_prepare(gt);
	intel_gt_suspend_late(gt);

	GEM_BUG_ON(intel_gt_pm_is_awake(gt));
}

int intel_gt_wait_for_idle(struct intel_gt *gt, long timeout)
{
	long remaining_timeout;

	/* If the device is asleep, we have no requests outstanding */
	if (!intel_gt_pm_is_awake(gt))
		return 0;

	while ((timeout = intel_gt_retire_requests_timeout(gt, timeout,
							   &remaining_timeout)) > 0) {
		cond_resched();
		if (signal_pending(current))
			return -EINTR;
	}

	if (timeout)
		return timeout;

	if (remaining_timeout < 0)
		remaining_timeout = 0;

	return intel_uc_wait_for_idle(&gt->uc, remaining_timeout);
}

int intel_gt_init(struct intel_gt *gt)
{
	int err;

	err = i915_inject_probe_error(gt->i915, -ENODEV);
	if (err)
		return err;

	intel_gt_init_workarounds(gt);

	/*
	 * This is just a security blanket to placate dragons.
	 * On some systems, we very sporadically observe that the first TLBs
	 * used by the CS may be stale, despite us poking the TLB reset. If
	 * we hold the forcewake during initialisation these problems
	 * just magically go away.
	 */
	intel_uncore_forcewake_get(gt->uncore, FORCEWAKE_ALL);

	err = intel_gt_init_scratch(gt,
				    GRAPHICS_VER(gt->i915) == 2 ? SZ_256K : SZ_4K);
	if (err)
		goto out_fw;

	intel_gt_pm_init(gt);

	gt->vm = kernel_vm(gt);
	if (!gt->vm) {
		err = -ENOMEM;
		goto err_pm;
	}

	intel_set_mocs_index(gt);

	err = intel_engines_init(gt);
	if (err)
		goto err_engines;

	err = intel_uc_init(&gt->uc);
	if (err)
		goto err_engines;

	err = intel_gt_resume(gt);
	if (err)
		goto err_uc_init;

	err = intel_gt_init_hwconfig(gt);
	if (err)
		gt_err(gt, "Failed to retrieve hwconfig table: %pe\n", ERR_PTR(err));

	err = __engines_record_defaults(gt);
	if (err)
		goto err_gt;

	err = __engines_verify_workarounds(gt);
	if (err)
		goto err_gt;

	err = i915_inject_probe_error(gt->i915, -EIO);
	if (err)
		goto err_gt;

	intel_uc_init_late(&gt->uc);

	intel_migrate_init(&gt->migrate, gt);

	goto out_fw;
err_gt:
	__intel_gt_disable(gt);
	intel_uc_fini_hw(&gt->uc);
err_uc_init:
	intel_uc_fini(&gt->uc);
err_engines:
	intel_engines_release(gt);
	i915_vm_put(fetch_and_zero(&gt->vm));
err_pm:
	intel_gt_pm_fini(gt);
	intel_gt_fini_scratch(gt);
out_fw:
	if (err)
		intel_gt_set_wedged_on_init(gt);
	intel_uncore_forcewake_put(gt->uncore, FORCEWAKE_ALL);
	return err;
}

void intel_gt_driver_remove(struct intel_gt *gt)
{
	__intel_gt_disable(gt);

	intel_migrate_fini(&gt->migrate);
	intel_uc_driver_remove(&gt->uc);

	intel_engines_release(gt);

	intel_gt_flush_buffer_pool(gt);
}

void intel_gt_driver_unregister(struct intel_gt *gt)
{
	intel_wakeref_t wakeref;

	intel_gt_sysfs_unregister(gt);
	intel_rps_driver_unregister(&gt->rps);
	intel_gsc_fini(&gt->gsc);

	/*
	 * Upon unregistering the device to prevent any new users, cancel
	 * all in-flight requests so that we can quickly unbind the active
	 * resources.
	 */
	intel_gt_set_wedged_on_fini(gt);

	/* Scrub all HW state upon release */
	with_intel_runtime_pm(gt->uncore->rpm, wakeref)
		__intel_gt_reset(gt, ALL_ENGINES);
}

void intel_gt_driver_release(struct intel_gt *gt)
{
	struct i915_address_space *vm;

	vm = fetch_and_zero(&gt->vm);
	if (vm) /* FIXME being called twice on error paths :( */
		i915_vm_put(vm);

	intel_wa_list_free(&gt->wa_list);
	intel_gt_pm_fini(gt);
	intel_gt_fini_scratch(gt);
	intel_gt_fini_buffer_pool(gt);
	intel_gt_fini_hwconfig(gt);
}

void intel_gt_driver_late_release_all(struct drm_i915_private *i915)
{
	struct intel_gt *gt;
	unsigned int id;

	/* We need to wait for inflight RCU frees to release their grip */
	rcu_barrier();

	for_each_gt(gt, i915, id) {
		intel_uc_driver_late_release(&gt->uc);
		intel_gt_fini_requests(gt);
		intel_gt_fini_reset(gt);
		intel_gt_fini_timelines(gt);
		mutex_destroy(&gt->tlb.invalidate_lock);
		intel_engines_free(gt);
	}
}

static int intel_gt_tile_setup(struct intel_gt *gt, phys_addr_t phys_addr)
{
	int ret;

	if (!gt_is_root(gt)) {
		struct intel_uncore *uncore;
		spinlock_t *irq_lock;

		uncore = drmm_kzalloc(&gt->i915->drm, sizeof(*uncore), GFP_KERNEL);
		if (!uncore)
			return -ENOMEM;

		irq_lock = drmm_kzalloc(&gt->i915->drm, sizeof(*irq_lock), GFP_KERNEL);
		if (!irq_lock)
			return -ENOMEM;

		gt->uncore = uncore;
		gt->irq_lock = irq_lock;

		intel_gt_common_init_early(gt);
	}

	intel_uncore_init_early(gt->uncore, gt);

	ret = intel_uncore_setup_mmio(gt->uncore, phys_addr);
	if (ret)
		return ret;

	gt->phys_addr = phys_addr;

	return 0;
}

int intel_gt_probe_all(struct drm_i915_private *i915)
{
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);
	struct intel_gt *gt = &i915->gt0;
	const struct intel_gt_definition *gtdef;
	phys_addr_t phys_addr;
	unsigned int mmio_bar;
	unsigned int i;
	int ret;

	mmio_bar = intel_mmio_bar(GRAPHICS_VER(i915));
	phys_addr = pci_resource_start(pdev, mmio_bar);

	/*
	 * We always have at least one primary GT on any device
	 * and it has been already initialized early during probe
	 * in i915_driver_probe()
	 */
	gt->i915 = i915;
	gt->name = "Primary GT";
	gt->info.engine_mask = RUNTIME_INFO(i915)->platform_engine_mask;

	gt_dbg(gt, "Setting up %s\n", gt->name);
	ret = intel_gt_tile_setup(gt, phys_addr);
	if (ret)
		return ret;

	i915->gt[0] = gt;

	if (!HAS_EXTRA_GT_LIST(i915))
		return 0;

	for (i = 1, gtdef = &INTEL_INFO(i915)->extra_gt_list[i - 1];
	     gtdef->name != NULL;
	     i++, gtdef = &INTEL_INFO(i915)->extra_gt_list[i - 1]) {
		gt = drmm_kzalloc(&i915->drm, sizeof(*gt), GFP_KERNEL);
		if (!gt) {
			ret = -ENOMEM;
			goto err;
		}

		gt->i915 = i915;
		gt->name = gtdef->name;
		gt->type = gtdef->type;
		gt->info.engine_mask = gtdef->engine_mask;
		gt->info.id = i;

		gt_dbg(gt, "Setting up %s\n", gt->name);
		if (GEM_WARN_ON(range_overflows_t(resource_size_t,
						  gtdef->mapping_base,
						  SZ_16M,
						  pci_resource_len(pdev, mmio_bar)))) {
			ret = -ENODEV;
			goto err;
		}

		switch (gtdef->type) {
		case GT_TILE:
			ret = intel_gt_tile_setup(gt, phys_addr + gtdef->mapping_base);
			break;

		case GT_MEDIA:
			ret = intel_sa_mediagt_setup(gt, phys_addr + gtdef->mapping_base,
						     gtdef->gsi_offset);
			break;

		case GT_PRIMARY:
			/* Primary GT should not appear in extra GT list */
		default:
			MISSING_CASE(gtdef->type);
			ret = -ENODEV;
		}

		if (ret)
			goto err;

		i915->gt[i] = gt;
	}

	return 0;

err:
	i915_probe_error(i915, "Failed to initialize %s! (%d)\n", gtdef->name, ret);
	intel_gt_release_all(i915);

	return ret;
}

int intel_gt_tiles_init(struct drm_i915_private *i915)
{
	struct intel_gt *gt;
	unsigned int id;
	int ret;

	for_each_gt(gt, i915, id) {
		ret = intel_gt_probe_lmem(gt);
		if (ret)
			return ret;
	}

	return 0;
}

void intel_gt_release_all(struct drm_i915_private *i915)
{
	struct intel_gt *gt;
	unsigned int id;

	for_each_gt(gt, i915, id)
		i915->gt[id] = NULL;
}

void intel_gt_info_print(const struct intel_gt_info *info,
			 struct drm_printer *p)
{
	drm_printf(p, "available engines: %x\n", info->engine_mask);

	intel_sseu_dump(&info->sseu, p);
}

struct reg_and_bit {
	union {
		i915_reg_t reg;
		i915_mcr_reg_t mcr_reg;
	};
	u32 bit;
};

static struct reg_and_bit
get_reg_and_bit(const struct intel_engine_cs *engine, const bool gen8,
		const i915_reg_t *regs, const unsigned int num)
{
	const unsigned int class = engine->class;
	struct reg_and_bit rb = { };

	if (gt_WARN_ON_ONCE(engine->gt, class >= num || !regs[class].reg))
		return rb;

	rb.reg = regs[class];
	if (gen8 && class == VIDEO_DECODE_CLASS)
		rb.reg.reg += 4 * engine->instance; /* GEN8_M2TCR */
	else
		rb.bit = engine->instance;

	rb.bit = BIT(rb.bit);

	return rb;
}

/*
 * HW architecture suggest typical invalidation time at 40us,
 * with pessimistic cases up to 100us and a recommendation to
 * cap at 1ms. We go a bit higher just in case.
 */
#define TLB_INVAL_TIMEOUT_US 100
#define TLB_INVAL_TIMEOUT_MS 4

/*
 * On Xe_HP the TLB invalidation registers are located at the same MMIO offsets
 * but are now considered MCR registers.  Since they exist within a GAM range,
 * the primary instance of the register rolls up the status from each unit.
 */
static int wait_for_invalidate(struct intel_gt *gt, struct reg_and_bit rb)
{
	if (GRAPHICS_VER_FULL(gt->i915) >= IP_VER(12, 50))
		return intel_gt_mcr_wait_for_reg(gt, rb.mcr_reg, rb.bit, 0,
						 TLB_INVAL_TIMEOUT_US,
						 TLB_INVAL_TIMEOUT_MS);
	else
		return __intel_wait_for_register_fw(gt->uncore, rb.reg, rb.bit, 0,
						    TLB_INVAL_TIMEOUT_US,
						    TLB_INVAL_TIMEOUT_MS,
						    NULL);
}

static void mmio_invalidate_full(struct intel_gt *gt)
{
	static const i915_reg_t gen8_regs[] = {
		[RENDER_CLASS]			= GEN8_RTCR,
		[VIDEO_DECODE_CLASS]		= GEN8_M1TCR, /* , GEN8_M2TCR */
		[VIDEO_ENHANCEMENT_CLASS]	= GEN8_VTCR,
		[COPY_ENGINE_CLASS]		= GEN8_BTCR,
	};
	static const i915_reg_t gen12_regs[] = {
		[RENDER_CLASS]			= GEN12_GFX_TLB_INV_CR,
		[VIDEO_DECODE_CLASS]		= GEN12_VD_TLB_INV_CR,
		[VIDEO_ENHANCEMENT_CLASS]	= GEN12_VE_TLB_INV_CR,
		[COPY_ENGINE_CLASS]		= GEN12_BLT_TLB_INV_CR,
		[COMPUTE_CLASS]			= GEN12_COMPCTX_TLB_INV_CR,
	};
	static const i915_mcr_reg_t xehp_regs[] = {
		[RENDER_CLASS]			= XEHP_GFX_TLB_INV_CR,
		[VIDEO_DECODE_CLASS]		= XEHP_VD_TLB_INV_CR,
		[VIDEO_ENHANCEMENT_CLASS]	= XEHP_VE_TLB_INV_CR,
		[COPY_ENGINE_CLASS]		= XEHP_BLT_TLB_INV_CR,
		[COMPUTE_CLASS]			= XEHP_COMPCTX_TLB_INV_CR,
	};
	struct drm_i915_private *i915 = gt->i915;
	struct intel_uncore *uncore = gt->uncore;
	struct intel_engine_cs *engine;
	intel_engine_mask_t awake, tmp;
	enum intel_engine_id id;
	const i915_reg_t *regs;
	unsigned int num = 0;
	unsigned long flags;

	/*
	 * New platforms should not be added with catch-all-newer (>=)
	 * condition so that any later platform added triggers the below warning
	 * and in turn mandates a human cross-check of whether the invalidation
	 * flows have compatible semantics.
	 *
	 * For instance with the 11.00 -> 12.00 transition three out of five
	 * respective engine registers were moved to masked type. Then after the
	 * 12.00 -> 12.50 transition multi cast handling is required too.
	 */

	if (GRAPHICS_VER_FULL(i915) == IP_VER(12, 50) ||
	    GRAPHICS_VER_FULL(i915) == IP_VER(12, 55)) {
		regs = NULL;
		num = ARRAY_SIZE(xehp_regs);
	} else if (GRAPHICS_VER_FULL(i915) == IP_VER(12, 0) ||
		   GRAPHICS_VER_FULL(i915) == IP_VER(12, 10)) {
		regs = gen12_regs;
		num = ARRAY_SIZE(gen12_regs);
	} else if (GRAPHICS_VER(i915) >= 8 && GRAPHICS_VER(i915) <= 11) {
		regs = gen8_regs;
		num = ARRAY_SIZE(gen8_regs);
	} else if (GRAPHICS_VER(i915) < 8) {
		return;
	}

	if (gt_WARN_ONCE(gt, !num, "Platform does not implement TLB invalidation!"))
		return;

	intel_uncore_forcewake_get(uncore, FORCEWAKE_ALL);

	intel_gt_mcr_lock(gt, &flags);
	spin_lock(&uncore->lock); /* serialise invalidate with GT reset */

	awake = 0;
	for_each_engine(engine, gt, id) {
		struct reg_and_bit rb;

		if (!intel_engine_pm_is_awake(engine))
			continue;

		if (GRAPHICS_VER_FULL(i915) >= IP_VER(12, 50)) {
			u32 val = BIT(engine->instance);

			if (engine->class == VIDEO_DECODE_CLASS ||
			    engine->class == VIDEO_ENHANCEMENT_CLASS ||
			    engine->class == COMPUTE_CLASS)
				val = _MASKED_BIT_ENABLE(val);
			intel_gt_mcr_multicast_write_fw(gt,
							xehp_regs[engine->class],
							val);
		} else {
			rb = get_reg_and_bit(engine, regs == gen8_regs, regs, num);
			if (!i915_mmio_reg_offset(rb.reg))
				continue;

			if (GRAPHICS_VER(i915) == 12 && (engine->class == VIDEO_DECODE_CLASS ||
			    engine->class == VIDEO_ENHANCEMENT_CLASS ||
			    engine->class == COMPUTE_CLASS))
				rb.bit = _MASKED_BIT_ENABLE(rb.bit);

			intel_uncore_write_fw(uncore, rb.reg, rb.bit);
		}
		awake |= engine->mask;
	}

	GT_TRACE(gt, "invalidated engines %08x\n", awake);

	/* Wa_2207587034:tgl,dg1,rkl,adl-s,adl-p */
	if (awake &&
	    (IS_TIGERLAKE(i915) ||
	     IS_DG1(i915) ||
	     IS_ROCKETLAKE(i915) ||
	     IS_ALDERLAKE_S(i915) ||
	     IS_ALDERLAKE_P(i915)))
		intel_uncore_write_fw(uncore, GEN12_OA_TLB_INV_CR, 1);

	spin_unlock(&uncore->lock);
	intel_gt_mcr_unlock(gt, flags);

	for_each_engine_masked(engine, gt, awake, tmp) {
		struct reg_and_bit rb;

		if (GRAPHICS_VER_FULL(i915) >= IP_VER(12, 50)) {
			rb.mcr_reg = xehp_regs[engine->class];
			rb.bit = BIT(engine->instance);
		} else {
			rb = get_reg_and_bit(engine, regs == gen8_regs, regs, num);
		}

		if (wait_for_invalidate(gt, rb))
			gt_err_ratelimited(gt, "%s TLB invalidation did not complete in %ums!\n",
					   engine->name, TLB_INVAL_TIMEOUT_MS);
	}

	/*
	 * Use delayed put since a) we mostly expect a flurry of TLB
	 * invalidations so it is good to avoid paying the forcewake cost and
	 * b) it works around a bug in Icelake which cannot cope with too rapid
	 * transitions.
	 */
	intel_uncore_forcewake_put_delayed(uncore, FORCEWAKE_ALL);
}

static bool tlb_seqno_passed(const struct intel_gt *gt, u32 seqno)
{
	u32 cur = intel_gt_tlb_seqno(gt);

	/* Only skip if a *full* TLB invalidate barrier has passed */
	return (s32)(cur - ALIGN(seqno, 2)) > 0;
}

void intel_gt_invalidate_tlb(struct intel_gt *gt, u32 seqno)
{
	intel_wakeref_t wakeref;

	if (I915_SELFTEST_ONLY(gt->awake == -ENODEV))
		return;

	if (intel_gt_is_wedged(gt))
		return;

	if (tlb_seqno_passed(gt, seqno))
		return;

	with_intel_gt_pm_if_awake(gt, wakeref) {
		mutex_lock(&gt->tlb.invalidate_lock);
		if (tlb_seqno_passed(gt, seqno))
			goto unlock;

		mmio_invalidate_full(gt);

		write_seqcount_invalidate(&gt->tlb.seqno);
unlock:
		mutex_unlock(&gt->tlb.invalidate_lock);
	}
}
