/*
 * Copyright © 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include <drm/drm_print.h>

#include "gem/i915_gem_context.h"

#include "i915_drv.h"

#include "intel_breadcrumbs.h"
#include "intel_context.h"
#include "intel_engine.h"
#include "intel_engine_pm.h"
#include "intel_engine_user.h"
#include "intel_gt.h"
#include "intel_gt_requests.h"
#include "intel_gt_pm.h"
#include "intel_lrc.h"
#include "intel_reset.h"
#include "intel_ring.h"

/* Haswell does have the CXT_SIZE register however it does not appear to be
 * valid. Now, docs explain in dwords what is in the context object. The full
 * size is 70720 bytes, however, the power context and execlist context will
 * never be saved (power context is stored elsewhere, and execlists don't work
 * on HSW) - so the final size, including the extra state required for the
 * Resource Streamer, is 66944 bytes, which rounds to 17 pages.
 */
#define HSW_CXT_TOTAL_SIZE		(17 * PAGE_SIZE)

#define DEFAULT_LR_CONTEXT_RENDER_SIZE	(22 * PAGE_SIZE)
#define GEN8_LR_CONTEXT_RENDER_SIZE	(20 * PAGE_SIZE)
#define GEN9_LR_CONTEXT_RENDER_SIZE	(22 * PAGE_SIZE)
#define GEN10_LR_CONTEXT_RENDER_SIZE	(18 * PAGE_SIZE)
#define GEN11_LR_CONTEXT_RENDER_SIZE	(14 * PAGE_SIZE)

#define GEN8_LR_CONTEXT_OTHER_SIZE	( 2 * PAGE_SIZE)

#define MAX_MMIO_BASES 3
struct engine_info {
	unsigned int hw_id;
	u8 class;
	u8 instance;
	/* mmio bases table *must* be sorted in reverse gen order */
	struct engine_mmio_base {
		u32 gen : 8;
		u32 base : 24;
	} mmio_bases[MAX_MMIO_BASES];
};

static const struct engine_info intel_engines[] = {
	[RCS0] = {
		.hw_id = RCS0_HW,
		.class = RENDER_CLASS,
		.instance = 0,
		.mmio_bases = {
			{ .gen = 1, .base = RENDER_RING_BASE }
		},
	},
	[BCS0] = {
		.hw_id = BCS0_HW,
		.class = COPY_ENGINE_CLASS,
		.instance = 0,
		.mmio_bases = {
			{ .gen = 6, .base = BLT_RING_BASE }
		},
	},
	[VCS0] = {
		.hw_id = VCS0_HW,
		.class = VIDEO_DECODE_CLASS,
		.instance = 0,
		.mmio_bases = {
			{ .gen = 11, .base = GEN11_BSD_RING_BASE },
			{ .gen = 6, .base = GEN6_BSD_RING_BASE },
			{ .gen = 4, .base = BSD_RING_BASE }
		},
	},
	[VCS1] = {
		.hw_id = VCS1_HW,
		.class = VIDEO_DECODE_CLASS,
		.instance = 1,
		.mmio_bases = {
			{ .gen = 11, .base = GEN11_BSD2_RING_BASE },
			{ .gen = 8, .base = GEN8_BSD2_RING_BASE }
		},
	},
	[VCS2] = {
		.hw_id = VCS2_HW,
		.class = VIDEO_DECODE_CLASS,
		.instance = 2,
		.mmio_bases = {
			{ .gen = 11, .base = GEN11_BSD3_RING_BASE }
		},
	},
	[VCS3] = {
		.hw_id = VCS3_HW,
		.class = VIDEO_DECODE_CLASS,
		.instance = 3,
		.mmio_bases = {
			{ .gen = 11, .base = GEN11_BSD4_RING_BASE }
		},
	},
	[VECS0] = {
		.hw_id = VECS0_HW,
		.class = VIDEO_ENHANCEMENT_CLASS,
		.instance = 0,
		.mmio_bases = {
			{ .gen = 11, .base = GEN11_VEBOX_RING_BASE },
			{ .gen = 7, .base = VEBOX_RING_BASE }
		},
	},
	[VECS1] = {
		.hw_id = VECS1_HW,
		.class = VIDEO_ENHANCEMENT_CLASS,
		.instance = 1,
		.mmio_bases = {
			{ .gen = 11, .base = GEN11_VEBOX2_RING_BASE }
		},
	},
};

/**
 * intel_engine_context_size() - return the size of the context for an engine
 * @gt: the gt
 * @class: engine class
 *
 * Each engine class may require a different amount of space for a context
 * image.
 *
 * Return: size (in bytes) of an engine class specific context image
 *
 * Note: this size includes the HWSP, which is part of the context image
 * in LRC mode, but does not include the "shared data page" used with
 * GuC submission. The caller should account for this if using the GuC.
 */
u32 intel_engine_context_size(struct intel_gt *gt, u8 class)
{
	struct intel_uncore *uncore = gt->uncore;
	u32 cxt_size;

	BUILD_BUG_ON(I915_GTT_PAGE_SIZE != PAGE_SIZE);

	switch (class) {
	case RENDER_CLASS:
		switch (INTEL_GEN(gt->i915)) {
		default:
			MISSING_CASE(INTEL_GEN(gt->i915));
			return DEFAULT_LR_CONTEXT_RENDER_SIZE;
		case 12:
		case 11:
			return GEN11_LR_CONTEXT_RENDER_SIZE;
		case 10:
			return GEN10_LR_CONTEXT_RENDER_SIZE;
		case 9:
			return GEN9_LR_CONTEXT_RENDER_SIZE;
		case 8:
			return GEN8_LR_CONTEXT_RENDER_SIZE;
		case 7:
			if (IS_HASWELL(gt->i915))
				return HSW_CXT_TOTAL_SIZE;

			cxt_size = intel_uncore_read(uncore, GEN7_CXT_SIZE);
			return round_up(GEN7_CXT_TOTAL_SIZE(cxt_size) * 64,
					PAGE_SIZE);
		case 6:
			cxt_size = intel_uncore_read(uncore, CXT_SIZE);
			return round_up(GEN6_CXT_TOTAL_SIZE(cxt_size) * 64,
					PAGE_SIZE);
		case 5:
		case 4:
			/*
			 * There is a discrepancy here between the size reported
			 * by the register and the size of the context layout
			 * in the docs. Both are described as authorative!
			 *
			 * The discrepancy is on the order of a few cachelines,
			 * but the total is under one page (4k), which is our
			 * minimum allocation anyway so it should all come
			 * out in the wash.
			 */
			cxt_size = intel_uncore_read(uncore, CXT_SIZE) + 1;
			drm_dbg(&gt->i915->drm,
				"gen%d CXT_SIZE = %d bytes [0x%08x]\n",
				INTEL_GEN(gt->i915), cxt_size * 64,
				cxt_size - 1);
			return round_up(cxt_size * 64, PAGE_SIZE);
		case 3:
		case 2:
		/* For the special day when i810 gets merged. */
		case 1:
			return 0;
		}
		break;
	default:
		MISSING_CASE(class);
		fallthrough;
	case VIDEO_DECODE_CLASS:
	case VIDEO_ENHANCEMENT_CLASS:
	case COPY_ENGINE_CLASS:
		if (INTEL_GEN(gt->i915) < 8)
			return 0;
		return GEN8_LR_CONTEXT_OTHER_SIZE;
	}
}

static u32 __engine_mmio_base(struct drm_i915_private *i915,
			      const struct engine_mmio_base *bases)
{
	int i;

	for (i = 0; i < MAX_MMIO_BASES; i++)
		if (INTEL_GEN(i915) >= bases[i].gen)
			break;

	GEM_BUG_ON(i == MAX_MMIO_BASES);
	GEM_BUG_ON(!bases[i].base);

	return bases[i].base;
}

static void __sprint_engine_name(struct intel_engine_cs *engine)
{
	/*
	 * Before we know what the uABI name for this engine will be,
	 * we still would like to keep track of this engine in the debug logs.
	 * We throw in a ' here as a reminder that this isn't its final name.
	 */
	GEM_WARN_ON(snprintf(engine->name, sizeof(engine->name), "%s'%u",
			     intel_engine_class_repr(engine->class),
			     engine->instance) >= sizeof(engine->name));
}

void intel_engine_set_hwsp_writemask(struct intel_engine_cs *engine, u32 mask)
{
	/*
	 * Though they added more rings on g4x/ilk, they did not add
	 * per-engine HWSTAM until gen6.
	 */
	if (INTEL_GEN(engine->i915) < 6 && engine->class != RENDER_CLASS)
		return;

	if (INTEL_GEN(engine->i915) >= 3)
		ENGINE_WRITE(engine, RING_HWSTAM, mask);
	else
		ENGINE_WRITE16(engine, RING_HWSTAM, mask);
}

static void intel_engine_sanitize_mmio(struct intel_engine_cs *engine)
{
	/* Mask off all writes into the unknown HWSP */
	intel_engine_set_hwsp_writemask(engine, ~0u);
}

static int intel_engine_setup(struct intel_gt *gt, enum intel_engine_id id)
{
	const struct engine_info *info = &intel_engines[id];
	struct drm_i915_private *i915 = gt->i915;
	struct intel_engine_cs *engine;

	BUILD_BUG_ON(MAX_ENGINE_CLASS >= BIT(GEN11_ENGINE_CLASS_WIDTH));
	BUILD_BUG_ON(MAX_ENGINE_INSTANCE >= BIT(GEN11_ENGINE_INSTANCE_WIDTH));

	if (GEM_DEBUG_WARN_ON(id >= ARRAY_SIZE(gt->engine)))
		return -EINVAL;

	if (GEM_DEBUG_WARN_ON(info->class > MAX_ENGINE_CLASS))
		return -EINVAL;

	if (GEM_DEBUG_WARN_ON(info->instance > MAX_ENGINE_INSTANCE))
		return -EINVAL;

	if (GEM_DEBUG_WARN_ON(gt->engine_class[info->class][info->instance]))
		return -EINVAL;

	engine = kzalloc(sizeof(*engine), GFP_KERNEL);
	if (!engine)
		return -ENOMEM;

	BUILD_BUG_ON(BITS_PER_TYPE(engine->mask) < I915_NUM_ENGINES);

	engine->id = id;
	engine->legacy_idx = INVALID_ENGINE;
	engine->mask = BIT(id);
	engine->i915 = i915;
	engine->gt = gt;
	engine->uncore = gt->uncore;
	engine->hw_id = engine->guc_id = info->hw_id;
	engine->mmio_base = __engine_mmio_base(i915, info->mmio_bases);

	engine->class = info->class;
	engine->instance = info->instance;
	__sprint_engine_name(engine);

	engine->props.heartbeat_interval_ms =
		CONFIG_DRM_I915_HEARTBEAT_INTERVAL;
	engine->props.max_busywait_duration_ns =
		CONFIG_DRM_I915_MAX_REQUEST_BUSYWAIT;
	engine->props.preempt_timeout_ms =
		CONFIG_DRM_I915_PREEMPT_TIMEOUT;
	engine->props.stop_timeout_ms =
		CONFIG_DRM_I915_STOP_TIMEOUT;
	engine->props.timeslice_duration_ms =
		CONFIG_DRM_I915_TIMESLICE_DURATION;

	/* Override to uninterruptible for OpenCL workloads. */
	if (INTEL_GEN(i915) == 12 && engine->class == RENDER_CLASS)
		engine->props.preempt_timeout_ms = 0;

	engine->defaults = engine->props; /* never to change again */

	engine->context_size = intel_engine_context_size(gt, engine->class);
	if (WARN_ON(engine->context_size > BIT(20)))
		engine->context_size = 0;
	if (engine->context_size)
		DRIVER_CAPS(i915)->has_logical_contexts = true;

	/* Nothing to do here, execute in order of dependencies */
	engine->schedule = NULL;

	ewma__engine_latency_init(&engine->latency);
	seqlock_init(&engine->stats.lock);

	ATOMIC_INIT_NOTIFIER_HEAD(&engine->context_status_notifier);

	/* Scrub mmio state on takeover */
	intel_engine_sanitize_mmio(engine);

	gt->engine_class[info->class][info->instance] = engine;
	gt->engine[id] = engine;

	return 0;
}

static void __setup_engine_capabilities(struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = engine->i915;

	if (engine->class == VIDEO_DECODE_CLASS) {
		/*
		 * HEVC support is present on first engine instance
		 * before Gen11 and on all instances afterwards.
		 */
		if (INTEL_GEN(i915) >= 11 ||
		    (INTEL_GEN(i915) >= 9 && engine->instance == 0))
			engine->uabi_capabilities |=
				I915_VIDEO_CLASS_CAPABILITY_HEVC;

		/*
		 * SFC block is present only on even logical engine
		 * instances.
		 */
		if ((INTEL_GEN(i915) >= 11 &&
		     engine->gt->info.vdbox_sfc_access & engine->mask) ||
		    (INTEL_GEN(i915) >= 9 && engine->instance == 0))
			engine->uabi_capabilities |=
				I915_VIDEO_AND_ENHANCE_CLASS_CAPABILITY_SFC;
	} else if (engine->class == VIDEO_ENHANCEMENT_CLASS) {
		if (INTEL_GEN(i915) >= 9)
			engine->uabi_capabilities |=
				I915_VIDEO_AND_ENHANCE_CLASS_CAPABILITY_SFC;
	}
}

static void intel_setup_engine_capabilities(struct intel_gt *gt)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, gt, id)
		__setup_engine_capabilities(engine);
}

/**
 * intel_engines_release() - free the resources allocated for Command Streamers
 * @gt: pointer to struct intel_gt
 */
void intel_engines_release(struct intel_gt *gt)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	/*
	 * Before we release the resources held by engine, we must be certain
	 * that the HW is no longer accessing them -- having the GPU scribble
	 * to or read from a page being used for something else causes no end
	 * of fun.
	 *
	 * The GPU should be reset by this point, but assume the worst just
	 * in case we aborted before completely initialising the engines.
	 */
	GEM_BUG_ON(intel_gt_pm_is_awake(gt));
	if (!INTEL_INFO(gt->i915)->gpu_reset_clobbers_display)
		__intel_gt_reset(gt, ALL_ENGINES);

	/* Decouple the backend; but keep the layout for late GPU resets */
	for_each_engine(engine, gt, id) {
		if (!engine->release)
			continue;

		intel_wakeref_wait_for_idle(&engine->wakeref);
		GEM_BUG_ON(intel_engine_pm_is_awake(engine));

		engine->release(engine);
		engine->release = NULL;

		memset(&engine->reset, 0, sizeof(engine->reset));
	}
}

void intel_engine_free_request_pool(struct intel_engine_cs *engine)
{
	if (!engine->request_pool)
		return;

	kmem_cache_free(i915_request_slab_cache(), engine->request_pool);
}

void intel_engines_free(struct intel_gt *gt)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	/* Free the requests! dma-resv keeps fences around for an eternity */
	rcu_barrier();

	for_each_engine(engine, gt, id) {
		intel_engine_free_request_pool(engine);
		kfree(engine);
		gt->engine[id] = NULL;
	}
}

/*
 * Determine which engines are fused off in our particular hardware.
 * Note that we have a catch-22 situation where we need to be able to access
 * the blitter forcewake domain to read the engine fuses, but at the same time
 * we need to know which engines are available on the system to know which
 * forcewake domains are present. We solve this by intializing the forcewake
 * domains based on the full engine mask in the platform capabilities before
 * calling this function and pruning the domains for fused-off engines
 * afterwards.
 */
static intel_engine_mask_t init_engine_mask(struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;
	struct intel_gt_info *info = &gt->info;
	struct intel_uncore *uncore = gt->uncore;
	unsigned int logical_vdbox = 0;
	unsigned int i;
	u32 media_fuse;
	u16 vdbox_mask;
	u16 vebox_mask;

	info->engine_mask = INTEL_INFO(i915)->platform_engine_mask;

	if (INTEL_GEN(i915) < 11)
		return info->engine_mask;

	media_fuse = ~intel_uncore_read(uncore, GEN11_GT_VEBOX_VDBOX_DISABLE);

	vdbox_mask = media_fuse & GEN11_GT_VDBOX_DISABLE_MASK;
	vebox_mask = (media_fuse & GEN11_GT_VEBOX_DISABLE_MASK) >>
		      GEN11_GT_VEBOX_DISABLE_SHIFT;

	for (i = 0; i < I915_MAX_VCS; i++) {
		if (!HAS_ENGINE(gt, _VCS(i))) {
			vdbox_mask &= ~BIT(i);
			continue;
		}

		if (!(BIT(i) & vdbox_mask)) {
			info->engine_mask &= ~BIT(_VCS(i));
			drm_dbg(&i915->drm, "vcs%u fused off\n", i);
			continue;
		}

		/*
		 * In Gen11, only even numbered logical VDBOXes are
		 * hooked up to an SFC (Scaler & Format Converter) unit.
		 * In TGL each VDBOX has access to an SFC.
		 */
		if (INTEL_GEN(i915) >= 12 || logical_vdbox++ % 2 == 0)
			gt->info.vdbox_sfc_access |= BIT(i);
	}
	drm_dbg(&i915->drm, "vdbox enable: %04x, instances: %04lx\n",
		vdbox_mask, VDBOX_MASK(gt));
	GEM_BUG_ON(vdbox_mask != VDBOX_MASK(gt));

	for (i = 0; i < I915_MAX_VECS; i++) {
		if (!HAS_ENGINE(gt, _VECS(i))) {
			vebox_mask &= ~BIT(i);
			continue;
		}

		if (!(BIT(i) & vebox_mask)) {
			info->engine_mask &= ~BIT(_VECS(i));
			drm_dbg(&i915->drm, "vecs%u fused off\n", i);
		}
	}
	drm_dbg(&i915->drm, "vebox enable: %04x, instances: %04lx\n",
		vebox_mask, VEBOX_MASK(gt));
	GEM_BUG_ON(vebox_mask != VEBOX_MASK(gt));

	return info->engine_mask;
}

/**
 * intel_engines_init_mmio() - allocate and prepare the Engine Command Streamers
 * @gt: pointer to struct intel_gt
 *
 * Return: non-zero if the initialization failed.
 */
int intel_engines_init_mmio(struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;
	const unsigned int engine_mask = init_engine_mask(gt);
	unsigned int mask = 0;
	unsigned int i;
	int err;

	drm_WARN_ON(&i915->drm, engine_mask == 0);
	drm_WARN_ON(&i915->drm, engine_mask &
		    GENMASK(BITS_PER_TYPE(mask) - 1, I915_NUM_ENGINES));

	if (i915_inject_probe_failure(i915))
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(intel_engines); i++) {
		if (!HAS_ENGINE(gt, i))
			continue;

		err = intel_engine_setup(gt, i);
		if (err)
			goto cleanup;

		mask |= BIT(i);
	}

	/*
	 * Catch failures to update intel_engines table when the new engines
	 * are added to the driver by a warning and disabling the forgotten
	 * engines.
	 */
	if (drm_WARN_ON(&i915->drm, mask != engine_mask))
		gt->info.engine_mask = mask;

	gt->info.num_engines = hweight32(mask);

	intel_gt_check_and_clear_faults(gt);

	intel_setup_engine_capabilities(gt);

	intel_uncore_prune_engine_fw_domains(gt->uncore, gt);

	return 0;

cleanup:
	intel_engines_free(gt);
	return err;
}

void intel_engine_init_execlists(struct intel_engine_cs *engine)
{
	struct intel_engine_execlists * const execlists = &engine->execlists;

	execlists->port_mask = 1;
	GEM_BUG_ON(!is_power_of_2(execlists_num_ports(execlists)));
	GEM_BUG_ON(execlists_num_ports(execlists) > EXECLIST_MAX_PORTS);

	memset(execlists->pending, 0, sizeof(execlists->pending));
	execlists->active =
		memset(execlists->inflight, 0, sizeof(execlists->inflight));

	execlists->queue_priority_hint = INT_MIN;
	execlists->queue = RB_ROOT_CACHED;
}

static void cleanup_status_page(struct intel_engine_cs *engine)
{
	struct i915_vma *vma;

	/* Prevent writes into HWSP after returning the page to the system */
	intel_engine_set_hwsp_writemask(engine, ~0u);

	vma = fetch_and_zero(&engine->status_page.vma);
	if (!vma)
		return;

	if (!HWS_NEEDS_PHYSICAL(engine->i915))
		i915_vma_unpin(vma);

	i915_gem_object_unpin_map(vma->obj);
	i915_gem_object_put(vma->obj);
}

static int pin_ggtt_status_page(struct intel_engine_cs *engine,
				struct i915_vma *vma)
{
	unsigned int flags;

	if (!HAS_LLC(engine->i915) && i915_ggtt_has_aperture(engine->gt->ggtt))
		/*
		 * On g33, we cannot place HWS above 256MiB, so
		 * restrict its pinning to the low mappable arena.
		 * Though this restriction is not documented for
		 * gen4, gen5, or byt, they also behave similarly
		 * and hang if the HWS is placed at the top of the
		 * GTT. To generalise, it appears that all !llc
		 * platforms have issues with us placing the HWS
		 * above the mappable region (even though we never
		 * actually map it).
		 */
		flags = PIN_MAPPABLE;
	else
		flags = PIN_HIGH;

	return i915_ggtt_pin(vma, NULL, 0, flags);
}

static int init_status_page(struct intel_engine_cs *engine)
{
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	void *vaddr;
	int ret;

	/*
	 * Though the HWS register does support 36bit addresses, historically
	 * we have had hangs and corruption reported due to wild writes if
	 * the HWS is placed above 4G. We only allow objects to be allocated
	 * in GFP_DMA32 for i965, and no earlier physical address users had
	 * access to more than 4G.
	 */
	obj = i915_gem_object_create_internal(engine->i915, PAGE_SIZE);
	if (IS_ERR(obj)) {
		drm_err(&engine->i915->drm,
			"Failed to allocate status page\n");
		return PTR_ERR(obj);
	}

	i915_gem_object_set_cache_coherency(obj, I915_CACHE_LLC);

	vma = i915_vma_instance(obj, &engine->gt->ggtt->vm, NULL);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto err;
	}

	vaddr = i915_gem_object_pin_map(obj, I915_MAP_WB);
	if (IS_ERR(vaddr)) {
		ret = PTR_ERR(vaddr);
		goto err;
	}

	engine->status_page.addr = memset(vaddr, 0, PAGE_SIZE);
	engine->status_page.vma = vma;

	if (!HWS_NEEDS_PHYSICAL(engine->i915)) {
		ret = pin_ggtt_status_page(engine, vma);
		if (ret)
			goto err_unpin;
	}

	return 0;

err_unpin:
	i915_gem_object_unpin_map(obj);
err:
	i915_gem_object_put(obj);
	return ret;
}

static int engine_setup_common(struct intel_engine_cs *engine)
{
	int err;

	init_llist_head(&engine->barrier_tasks);

	err = init_status_page(engine);
	if (err)
		return err;

	engine->breadcrumbs = intel_breadcrumbs_create(engine);
	if (!engine->breadcrumbs) {
		err = -ENOMEM;
		goto err_status;
	}

	intel_engine_init_active(engine, ENGINE_PHYSICAL);
	intel_engine_init_execlists(engine);
	intel_engine_init_cmd_parser(engine);
	intel_engine_init__pm(engine);
	intel_engine_init_retire(engine);

	/* Use the whole device by default */
	engine->sseu =
		intel_sseu_from_device_info(&engine->gt->info.sseu);

	intel_engine_init_workarounds(engine);
	intel_engine_init_whitelist(engine);
	intel_engine_init_ctx_wa(engine);

	return 0;

err_status:
	cleanup_status_page(engine);
	return err;
}

struct measure_breadcrumb {
	struct i915_request rq;
	struct intel_ring ring;
	u32 cs[2048];
};

static int measure_breadcrumb_dw(struct intel_context *ce)
{
	struct intel_engine_cs *engine = ce->engine;
	struct measure_breadcrumb *frame;
	int dw;

	GEM_BUG_ON(!engine->gt->scratch);

	frame = kzalloc(sizeof(*frame), GFP_KERNEL);
	if (!frame)
		return -ENOMEM;

	frame->rq.engine = engine;
	frame->rq.context = ce;
	rcu_assign_pointer(frame->rq.timeline, ce->timeline);

	frame->ring.vaddr = frame->cs;
	frame->ring.size = sizeof(frame->cs);
	frame->ring.wrap =
		BITS_PER_TYPE(frame->ring.size) - ilog2(frame->ring.size);
	frame->ring.effective_size = frame->ring.size;
	intel_ring_update_space(&frame->ring);
	frame->rq.ring = &frame->ring;

	mutex_lock(&ce->timeline->mutex);
	spin_lock_irq(&engine->active.lock);

	dw = engine->emit_fini_breadcrumb(&frame->rq, frame->cs) - frame->cs;

	spin_unlock_irq(&engine->active.lock);
	mutex_unlock(&ce->timeline->mutex);

	GEM_BUG_ON(dw & 1); /* RING_TAIL must be qword aligned */

	kfree(frame);
	return dw;
}

void
intel_engine_init_active(struct intel_engine_cs *engine, unsigned int subclass)
{
	INIT_LIST_HEAD(&engine->active.requests);
	INIT_LIST_HEAD(&engine->active.hold);

	spin_lock_init(&engine->active.lock);
	lockdep_set_subclass(&engine->active.lock, subclass);

	/*
	 * Due to an interesting quirk in lockdep's internal debug tracking,
	 * after setting a subclass we must ensure the lock is used. Otherwise,
	 * nr_unused_locks is incremented once too often.
	 */
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	local_irq_disable();
	lock_map_acquire(&engine->active.lock.dep_map);
	lock_map_release(&engine->active.lock.dep_map);
	local_irq_enable();
#endif
}

static struct intel_context *
create_pinned_context(struct intel_engine_cs *engine,
		      unsigned int hwsp,
		      struct lock_class_key *key,
		      const char *name)
{
	struct intel_context *ce;
	int err;

	ce = intel_context_create(engine);
	if (IS_ERR(ce))
		return ce;

	__set_bit(CONTEXT_BARRIER_BIT, &ce->flags);
	ce->timeline = page_pack_bits(NULL, hwsp);

	err = intel_context_pin(ce); /* perma-pin so it is always available */
	if (err) {
		intel_context_put(ce);
		return ERR_PTR(err);
	}

	/*
	 * Give our perma-pinned kernel timelines a separate lockdep class,
	 * so that we can use them from within the normal user timelines
	 * should we need to inject GPU operations during their request
	 * construction.
	 */
	lockdep_set_class_and_name(&ce->timeline->mutex, key, name);

	return ce;
}

static struct intel_context *
create_kernel_context(struct intel_engine_cs *engine)
{
	static struct lock_class_key kernel;

	return create_pinned_context(engine, I915_GEM_HWS_SEQNO_ADDR,
				     &kernel, "kernel_context");
}

/**
 * intel_engines_init_common - initialize cengine state which might require hw access
 * @engine: Engine to initialize.
 *
 * Initializes @engine@ structure members shared between legacy and execlists
 * submission modes which do require hardware access.
 *
 * Typcally done at later stages of submission mode specific engine setup.
 *
 * Returns zero on success or an error code on failure.
 */
static int engine_init_common(struct intel_engine_cs *engine)
{
	struct intel_context *ce;
	int ret;

	engine->set_default_submission(engine);

	/*
	 * We may need to do things with the shrinker which
	 * require us to immediately switch back to the default
	 * context. This can cause a problem as pinning the
	 * default context also requires GTT space which may not
	 * be available. To avoid this we always pin the default
	 * context.
	 */
	ce = create_kernel_context(engine);
	if (IS_ERR(ce))
		return PTR_ERR(ce);

	ret = measure_breadcrumb_dw(ce);
	if (ret < 0)
		goto err_context;

	engine->emit_fini_breadcrumb_dw = ret;
	engine->kernel_context = ce;

	return 0;

err_context:
	intel_context_put(ce);
	return ret;
}

int intel_engines_init(struct intel_gt *gt)
{
	int (*setup)(struct intel_engine_cs *engine);
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err;

	if (HAS_EXECLISTS(gt->i915))
		setup = intel_execlists_submission_setup;
	else
		setup = intel_ring_submission_setup;

	for_each_engine(engine, gt, id) {
		err = engine_setup_common(engine);
		if (err)
			return err;

		err = setup(engine);
		if (err)
			return err;

		err = engine_init_common(engine);
		if (err)
			return err;

		intel_engine_add_user(engine);
	}

	return 0;
}

/**
 * intel_engines_cleanup_common - cleans up the engine state created by
 *                                the common initiailizers.
 * @engine: Engine to cleanup.
 *
 * This cleans up everything created by the common helpers.
 */
void intel_engine_cleanup_common(struct intel_engine_cs *engine)
{
	GEM_BUG_ON(!list_empty(&engine->active.requests));
	tasklet_kill(&engine->execlists.tasklet); /* flush the callback */

	cleanup_status_page(engine);
	intel_breadcrumbs_free(engine->breadcrumbs);

	intel_engine_fini_retire(engine);
	intel_engine_cleanup_cmd_parser(engine);

	if (engine->default_state)
		fput(engine->default_state);

	if (engine->kernel_context) {
		intel_context_unpin(engine->kernel_context);
		intel_context_put(engine->kernel_context);
	}
	GEM_BUG_ON(!llist_empty(&engine->barrier_tasks));

	intel_wa_list_free(&engine->ctx_wa_list);
	intel_wa_list_free(&engine->wa_list);
	intel_wa_list_free(&engine->whitelist);
}

/**
 * intel_engine_resume - re-initializes the HW state of the engine
 * @engine: Engine to resume.
 *
 * Returns zero on success or an error code on failure.
 */
int intel_engine_resume(struct intel_engine_cs *engine)
{
	intel_engine_apply_workarounds(engine);
	intel_engine_apply_whitelist(engine);

	return engine->resume(engine);
}

u64 intel_engine_get_active_head(const struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = engine->i915;

	u64 acthd;

	if (INTEL_GEN(i915) >= 8)
		acthd = ENGINE_READ64(engine, RING_ACTHD, RING_ACTHD_UDW);
	else if (INTEL_GEN(i915) >= 4)
		acthd = ENGINE_READ(engine, RING_ACTHD);
	else
		acthd = ENGINE_READ(engine, ACTHD);

	return acthd;
}

u64 intel_engine_get_last_batch_head(const struct intel_engine_cs *engine)
{
	u64 bbaddr;

	if (INTEL_GEN(engine->i915) >= 8)
		bbaddr = ENGINE_READ64(engine, RING_BBADDR, RING_BBADDR_UDW);
	else
		bbaddr = ENGINE_READ(engine, RING_BBADDR);

	return bbaddr;
}

static unsigned long stop_timeout(const struct intel_engine_cs *engine)
{
	if (in_atomic() || irqs_disabled()) /* inside atomic preempt-reset? */
		return 0;

	/*
	 * If we are doing a normal GPU reset, we can take our time and allow
	 * the engine to quiesce. We've stopped submission to the engine, and
	 * if we wait long enough an innocent context should complete and
	 * leave the engine idle. So they should not be caught unaware by
	 * the forthcoming GPU reset (which usually follows the stop_cs)!
	 */
	return READ_ONCE(engine->props.stop_timeout_ms);
}

int intel_engine_stop_cs(struct intel_engine_cs *engine)
{
	struct intel_uncore *uncore = engine->uncore;
	const u32 base = engine->mmio_base;
	const i915_reg_t mode = RING_MI_MODE(base);
	int err;

	if (INTEL_GEN(engine->i915) < 3)
		return -ENODEV;

	ENGINE_TRACE(engine, "\n");

	intel_uncore_write_fw(uncore, mode, _MASKED_BIT_ENABLE(STOP_RING));

	err = 0;
	if (__intel_wait_for_register_fw(uncore,
					 mode, MODE_IDLE, MODE_IDLE,
					 1000, stop_timeout(engine),
					 NULL)) {
		ENGINE_TRACE(engine, "timed out on STOP_RING -> IDLE\n");
		err = -ETIMEDOUT;
	}

	/* A final mmio read to let GPU writes be hopefully flushed to memory */
	intel_uncore_posting_read_fw(uncore, mode);

	return err;
}

void intel_engine_cancel_stop_cs(struct intel_engine_cs *engine)
{
	ENGINE_TRACE(engine, "\n");

	ENGINE_WRITE_FW(engine, RING_MI_MODE, _MASKED_BIT_DISABLE(STOP_RING));
}

const char *i915_cache_level_str(struct drm_i915_private *i915, int type)
{
	switch (type) {
	case I915_CACHE_NONE: return " uncached";
	case I915_CACHE_LLC: return HAS_LLC(i915) ? " LLC" : " snooped";
	case I915_CACHE_L3_LLC: return " L3+LLC";
	case I915_CACHE_WT: return " WT";
	default: return "";
	}
}

static u32
read_subslice_reg(const struct intel_engine_cs *engine,
		  int slice, int subslice, i915_reg_t reg)
{
	struct drm_i915_private *i915 = engine->i915;
	struct intel_uncore *uncore = engine->uncore;
	u32 mcr_mask, mcr_ss, mcr, old_mcr, val;
	enum forcewake_domains fw_domains;

	if (INTEL_GEN(i915) >= 11) {
		mcr_mask = GEN11_MCR_SLICE_MASK | GEN11_MCR_SUBSLICE_MASK;
		mcr_ss = GEN11_MCR_SLICE(slice) | GEN11_MCR_SUBSLICE(subslice);
	} else {
		mcr_mask = GEN8_MCR_SLICE_MASK | GEN8_MCR_SUBSLICE_MASK;
		mcr_ss = GEN8_MCR_SLICE(slice) | GEN8_MCR_SUBSLICE(subslice);
	}

	fw_domains = intel_uncore_forcewake_for_reg(uncore, reg,
						    FW_REG_READ);
	fw_domains |= intel_uncore_forcewake_for_reg(uncore,
						     GEN8_MCR_SELECTOR,
						     FW_REG_READ | FW_REG_WRITE);

	spin_lock_irq(&uncore->lock);
	intel_uncore_forcewake_get__locked(uncore, fw_domains);

	old_mcr = mcr = intel_uncore_read_fw(uncore, GEN8_MCR_SELECTOR);

	mcr &= ~mcr_mask;
	mcr |= mcr_ss;
	intel_uncore_write_fw(uncore, GEN8_MCR_SELECTOR, mcr);

	val = intel_uncore_read_fw(uncore, reg);

	mcr &= ~mcr_mask;
	mcr |= old_mcr & mcr_mask;

	intel_uncore_write_fw(uncore, GEN8_MCR_SELECTOR, mcr);

	intel_uncore_forcewake_put__locked(uncore, fw_domains);
	spin_unlock_irq(&uncore->lock);

	return val;
}

/* NB: please notice the memset */
void intel_engine_get_instdone(const struct intel_engine_cs *engine,
			       struct intel_instdone *instdone)
{
	struct drm_i915_private *i915 = engine->i915;
	const struct sseu_dev_info *sseu = &engine->gt->info.sseu;
	struct intel_uncore *uncore = engine->uncore;
	u32 mmio_base = engine->mmio_base;
	int slice;
	int subslice;

	memset(instdone, 0, sizeof(*instdone));

	switch (INTEL_GEN(i915)) {
	default:
		instdone->instdone =
			intel_uncore_read(uncore, RING_INSTDONE(mmio_base));

		if (engine->id != RCS0)
			break;

		instdone->slice_common =
			intel_uncore_read(uncore, GEN7_SC_INSTDONE);
		if (INTEL_GEN(i915) >= 12) {
			instdone->slice_common_extra[0] =
				intel_uncore_read(uncore, GEN12_SC_INSTDONE_EXTRA);
			instdone->slice_common_extra[1] =
				intel_uncore_read(uncore, GEN12_SC_INSTDONE_EXTRA2);
		}
		for_each_instdone_slice_subslice(i915, sseu, slice, subslice) {
			instdone->sampler[slice][subslice] =
				read_subslice_reg(engine, slice, subslice,
						  GEN7_SAMPLER_INSTDONE);
			instdone->row[slice][subslice] =
				read_subslice_reg(engine, slice, subslice,
						  GEN7_ROW_INSTDONE);
		}
		break;
	case 7:
		instdone->instdone =
			intel_uncore_read(uncore, RING_INSTDONE(mmio_base));

		if (engine->id != RCS0)
			break;

		instdone->slice_common =
			intel_uncore_read(uncore, GEN7_SC_INSTDONE);
		instdone->sampler[0][0] =
			intel_uncore_read(uncore, GEN7_SAMPLER_INSTDONE);
		instdone->row[0][0] =
			intel_uncore_read(uncore, GEN7_ROW_INSTDONE);

		break;
	case 6:
	case 5:
	case 4:
		instdone->instdone =
			intel_uncore_read(uncore, RING_INSTDONE(mmio_base));
		if (engine->id == RCS0)
			/* HACK: Using the wrong struct member */
			instdone->slice_common =
				intel_uncore_read(uncore, GEN4_INSTDONE1);
		break;
	case 3:
	case 2:
		instdone->instdone = intel_uncore_read(uncore, GEN2_INSTDONE);
		break;
	}
}

static bool ring_is_idle(struct intel_engine_cs *engine)
{
	bool idle = true;

	if (I915_SELFTEST_ONLY(!engine->mmio_base))
		return true;

	if (!intel_engine_pm_get_if_awake(engine))
		return true;

	/* First check that no commands are left in the ring */
	if ((ENGINE_READ(engine, RING_HEAD) & HEAD_ADDR) !=
	    (ENGINE_READ(engine, RING_TAIL) & TAIL_ADDR))
		idle = false;

	/* No bit for gen2, so assume the CS parser is idle */
	if (INTEL_GEN(engine->i915) > 2 &&
	    !(ENGINE_READ(engine, RING_MI_MODE) & MODE_IDLE))
		idle = false;

	intel_engine_pm_put(engine);

	return idle;
}

void intel_engine_flush_submission(struct intel_engine_cs *engine)
{
	struct tasklet_struct *t = &engine->execlists.tasklet;

	if (!t->func)
		return;

	/* Synchronise and wait for the tasklet on another CPU */
	tasklet_kill(t);

	/* Having cancelled the tasklet, ensure that is run */
	local_bh_disable();
	if (tasklet_trylock(t)) {
		/* Must wait for any GPU reset in progress. */
		if (__tasklet_is_enabled(t))
			t->func(t->data);
		tasklet_unlock(t);
	}
	local_bh_enable();
}

/**
 * intel_engine_is_idle() - Report if the engine has finished process all work
 * @engine: the intel_engine_cs
 *
 * Return true if there are no requests pending, nothing left to be submitted
 * to hardware, and that the engine is idle.
 */
bool intel_engine_is_idle(struct intel_engine_cs *engine)
{
	/* More white lies, if wedged, hw state is inconsistent */
	if (intel_gt_is_wedged(engine->gt))
		return true;

	if (!intel_engine_pm_is_awake(engine))
		return true;

	/* Waiting to drain ELSP? */
	if (execlists_active(&engine->execlists)) {
		synchronize_hardirq(engine->i915->drm.pdev->irq);

		intel_engine_flush_submission(engine);

		if (execlists_active(&engine->execlists))
			return false;
	}

	/* ELSP is empty, but there are ready requests? E.g. after reset */
	if (!RB_EMPTY_ROOT(&engine->execlists.queue.rb_root))
		return false;

	/* Ring stopped? */
	return ring_is_idle(engine);
}

bool intel_engines_are_idle(struct intel_gt *gt)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	/*
	 * If the driver is wedged, HW state may be very inconsistent and
	 * report that it is still busy, even though we have stopped using it.
	 */
	if (intel_gt_is_wedged(gt))
		return true;

	/* Already parked (and passed an idleness test); must still be idle */
	if (!READ_ONCE(gt->awake))
		return true;

	for_each_engine(engine, gt, id) {
		if (!intel_engine_is_idle(engine))
			return false;
	}

	return true;
}

void intel_engines_reset_default_submission(struct intel_gt *gt)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, gt, id)
		engine->set_default_submission(engine);
}

bool intel_engine_can_store_dword(struct intel_engine_cs *engine)
{
	switch (INTEL_GEN(engine->i915)) {
	case 2:
		return false; /* uses physical not virtual addresses */
	case 3:
		/* maybe only uses physical not virtual addresses */
		return !(IS_I915G(engine->i915) || IS_I915GM(engine->i915));
	case 4:
		return !IS_I965G(engine->i915); /* who knows! */
	case 6:
		return engine->class != VIDEO_DECODE_CLASS; /* b0rked */
	default:
		return true;
	}
}

static int print_sched_attr(const struct i915_sched_attr *attr,
			    char *buf, int x, int len)
{
	if (attr->priority == I915_PRIORITY_INVALID)
		return x;

	x += snprintf(buf + x, len - x,
		      " prio=%d", attr->priority);

	return x;
}

static void print_request(struct drm_printer *m,
			  struct i915_request *rq,
			  const char *prefix)
{
	const char *name = rq->fence.ops->get_timeline_name(&rq->fence);
	char buf[80] = "";
	int x = 0;

	x = print_sched_attr(&rq->sched.attr, buf, x, sizeof(buf));

	drm_printf(m, "%s %llx:%llx%s%s %s @ %dms: %s\n",
		   prefix,
		   rq->fence.context, rq->fence.seqno,
		   i915_request_completed(rq) ? "!" :
		   i915_request_started(rq) ? "*" :
		   "",
		   test_bit(DMA_FENCE_FLAG_SIGNALED_BIT,
			    &rq->fence.flags) ? "+" :
		   test_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT,
			    &rq->fence.flags) ? "-" :
		   "",
		   buf,
		   jiffies_to_msecs(jiffies - rq->emitted_jiffies),
		   name);
}

static struct intel_timeline *get_timeline(struct i915_request *rq)
{
	struct intel_timeline *tl;

	/*
	 * Even though we are holding the engine->active.lock here, there
	 * is no control over the submission queue per-se and we are
	 * inspecting the active state at a random point in time, with an
	 * unknown queue. Play safe and make sure the timeline remains valid.
	 * (Only being used for pretty printing, one extra kref shouldn't
	 * cause a camel stampede!)
	 */
	rcu_read_lock();
	tl = rcu_dereference(rq->timeline);
	if (!kref_get_unless_zero(&tl->kref))
		tl = NULL;
	rcu_read_unlock();

	return tl;
}

static int print_ring(char *buf, int sz, struct i915_request *rq)
{
	int len = 0;

	if (!i915_request_signaled(rq)) {
		struct intel_timeline *tl = get_timeline(rq);

		len = scnprintf(buf, sz,
				"ring:{start:%08x, hwsp:%08x, seqno:%08x, runtime:%llums}, ",
				i915_ggtt_offset(rq->ring->vma),
				tl ? tl->hwsp_offset : 0,
				hwsp_seqno(rq),
				DIV_ROUND_CLOSEST_ULL(intel_context_get_total_runtime_ns(rq->context),
						      1000 * 1000));

		if (tl)
			intel_timeline_put(tl);
	}

	return len;
}

static void hexdump(struct drm_printer *m, const void *buf, size_t len)
{
	const size_t rowsize = 8 * sizeof(u32);
	const void *prev = NULL;
	bool skip = false;
	size_t pos;

	for (pos = 0; pos < len; pos += rowsize) {
		char line[128];

		if (prev && !memcmp(prev, buf + pos, rowsize)) {
			if (!skip) {
				drm_printf(m, "*\n");
				skip = true;
			}
			continue;
		}

		WARN_ON_ONCE(hex_dump_to_buffer(buf + pos, len - pos,
						rowsize, sizeof(u32),
						line, sizeof(line),
						false) >= sizeof(line));
		drm_printf(m, "[%04zx] %s\n", pos, line);

		prev = buf + pos;
		skip = false;
	}
}

static const char *repr_timer(const struct timer_list *t)
{
	if (!READ_ONCE(t->expires))
		return "inactive";

	if (timer_pending(t))
		return "active";

	return "expired";
}

static void intel_engine_print_registers(struct intel_engine_cs *engine,
					 struct drm_printer *m)
{
	struct drm_i915_private *dev_priv = engine->i915;
	struct intel_engine_execlists * const execlists = &engine->execlists;
	u64 addr;

	if (engine->id == RENDER_CLASS && IS_GEN_RANGE(dev_priv, 4, 7))
		drm_printf(m, "\tCCID: 0x%08x\n", ENGINE_READ(engine, CCID));
	if (HAS_EXECLISTS(dev_priv)) {
		drm_printf(m, "\tEL_STAT_HI: 0x%08x\n",
			   ENGINE_READ(engine, RING_EXECLIST_STATUS_HI));
		drm_printf(m, "\tEL_STAT_LO: 0x%08x\n",
			   ENGINE_READ(engine, RING_EXECLIST_STATUS_LO));
	}
	drm_printf(m, "\tRING_START: 0x%08x\n",
		   ENGINE_READ(engine, RING_START));
	drm_printf(m, "\tRING_HEAD:  0x%08x\n",
		   ENGINE_READ(engine, RING_HEAD) & HEAD_ADDR);
	drm_printf(m, "\tRING_TAIL:  0x%08x\n",
		   ENGINE_READ(engine, RING_TAIL) & TAIL_ADDR);
	drm_printf(m, "\tRING_CTL:   0x%08x%s\n",
		   ENGINE_READ(engine, RING_CTL),
		   ENGINE_READ(engine, RING_CTL) & (RING_WAIT | RING_WAIT_SEMAPHORE) ? " [waiting]" : "");
	if (INTEL_GEN(engine->i915) > 2) {
		drm_printf(m, "\tRING_MODE:  0x%08x%s\n",
			   ENGINE_READ(engine, RING_MI_MODE),
			   ENGINE_READ(engine, RING_MI_MODE) & (MODE_IDLE) ? " [idle]" : "");
	}

	if (INTEL_GEN(dev_priv) >= 6) {
		drm_printf(m, "\tRING_IMR:   0x%08x\n",
			   ENGINE_READ(engine, RING_IMR));
		drm_printf(m, "\tRING_ESR:   0x%08x\n",
			   ENGINE_READ(engine, RING_ESR));
		drm_printf(m, "\tRING_EMR:   0x%08x\n",
			   ENGINE_READ(engine, RING_EMR));
		drm_printf(m, "\tRING_EIR:   0x%08x\n",
			   ENGINE_READ(engine, RING_EIR));
	}

	addr = intel_engine_get_active_head(engine);
	drm_printf(m, "\tACTHD:  0x%08x_%08x\n",
		   upper_32_bits(addr), lower_32_bits(addr));
	addr = intel_engine_get_last_batch_head(engine);
	drm_printf(m, "\tBBADDR: 0x%08x_%08x\n",
		   upper_32_bits(addr), lower_32_bits(addr));
	if (INTEL_GEN(dev_priv) >= 8)
		addr = ENGINE_READ64(engine, RING_DMA_FADD, RING_DMA_FADD_UDW);
	else if (INTEL_GEN(dev_priv) >= 4)
		addr = ENGINE_READ(engine, RING_DMA_FADD);
	else
		addr = ENGINE_READ(engine, DMA_FADD_I8XX);
	drm_printf(m, "\tDMA_FADDR: 0x%08x_%08x\n",
		   upper_32_bits(addr), lower_32_bits(addr));
	if (INTEL_GEN(dev_priv) >= 4) {
		drm_printf(m, "\tIPEIR: 0x%08x\n",
			   ENGINE_READ(engine, RING_IPEIR));
		drm_printf(m, "\tIPEHR: 0x%08x\n",
			   ENGINE_READ(engine, RING_IPEHR));
	} else {
		drm_printf(m, "\tIPEIR: 0x%08x\n", ENGINE_READ(engine, IPEIR));
		drm_printf(m, "\tIPEHR: 0x%08x\n", ENGINE_READ(engine, IPEHR));
	}

	if (HAS_EXECLISTS(dev_priv)) {
		struct i915_request * const *port, *rq;
		const u32 *hws =
			&engine->status_page.addr[I915_HWS_CSB_BUF0_INDEX];
		const u8 num_entries = execlists->csb_size;
		unsigned int idx;
		u8 read, write;

		drm_printf(m, "\tExeclist tasklet queued? %s (%s), preempt? %s, timeslice? %s\n",
			   yesno(test_bit(TASKLET_STATE_SCHED,
					  &engine->execlists.tasklet.state)),
			   enableddisabled(!atomic_read(&engine->execlists.tasklet.count)),
			   repr_timer(&engine->execlists.preempt),
			   repr_timer(&engine->execlists.timer));

		read = execlists->csb_head;
		write = READ_ONCE(*execlists->csb_write);

		drm_printf(m, "\tExeclist status: 0x%08x %08x; CSB read:%d, write:%d, entries:%d\n",
			   ENGINE_READ(engine, RING_EXECLIST_STATUS_LO),
			   ENGINE_READ(engine, RING_EXECLIST_STATUS_HI),
			   read, write, num_entries);

		if (read >= num_entries)
			read = 0;
		if (write >= num_entries)
			write = 0;
		if (read > write)
			write += num_entries;
		while (read < write) {
			idx = ++read % num_entries;
			drm_printf(m, "\tExeclist CSB[%d]: 0x%08x, context: %d\n",
				   idx, hws[idx * 2], hws[idx * 2 + 1]);
		}

		execlists_active_lock_bh(execlists);
		rcu_read_lock();
		for (port = execlists->active; (rq = *port); port++) {
			char hdr[160];
			int len;

			len = scnprintf(hdr, sizeof(hdr),
					"\t\tActive[%d]:  ccid:%08x%s%s, ",
					(int)(port - execlists->active),
					rq->context->lrc.ccid,
					intel_context_is_closed(rq->context) ? "!" : "",
					intel_context_is_banned(rq->context) ? "*" : "");
			len += print_ring(hdr + len, sizeof(hdr) - len, rq);
			scnprintf(hdr + len, sizeof(hdr) - len, "rq: ");
			print_request(m, rq, hdr);
		}
		for (port = execlists->pending; (rq = *port); port++) {
			char hdr[160];
			int len;

			len = scnprintf(hdr, sizeof(hdr),
					"\t\tPending[%d]: ccid:%08x%s%s, ",
					(int)(port - execlists->pending),
					rq->context->lrc.ccid,
					intel_context_is_closed(rq->context) ? "!" : "",
					intel_context_is_banned(rq->context) ? "*" : "");
			len += print_ring(hdr + len, sizeof(hdr) - len, rq);
			scnprintf(hdr + len, sizeof(hdr) - len, "rq: ");
			print_request(m, rq, hdr);
		}
		rcu_read_unlock();
		execlists_active_unlock_bh(execlists);
	} else if (INTEL_GEN(dev_priv) > 6) {
		drm_printf(m, "\tPP_DIR_BASE: 0x%08x\n",
			   ENGINE_READ(engine, RING_PP_DIR_BASE));
		drm_printf(m, "\tPP_DIR_BASE_READ: 0x%08x\n",
			   ENGINE_READ(engine, RING_PP_DIR_BASE_READ));
		drm_printf(m, "\tPP_DIR_DCLV: 0x%08x\n",
			   ENGINE_READ(engine, RING_PP_DIR_DCLV));
	}
}

static void print_request_ring(struct drm_printer *m, struct i915_request *rq)
{
	void *ring;
	int size;

	drm_printf(m,
		   "[head %04x, postfix %04x, tail %04x, batch 0x%08x_%08x]:\n",
		   rq->head, rq->postfix, rq->tail,
		   rq->batch ? upper_32_bits(rq->batch->node.start) : ~0u,
		   rq->batch ? lower_32_bits(rq->batch->node.start) : ~0u);

	size = rq->tail - rq->head;
	if (rq->tail < rq->head)
		size += rq->ring->size;

	ring = kmalloc(size, GFP_ATOMIC);
	if (ring) {
		const void *vaddr = rq->ring->vaddr;
		unsigned int head = rq->head;
		unsigned int len = 0;

		if (rq->tail < head) {
			len = rq->ring->size - head;
			memcpy(ring, vaddr + head, len);
			head = 0;
		}
		memcpy(ring + len, vaddr + head, size - len);

		hexdump(m, ring, size);
		kfree(ring);
	}
}

static unsigned long list_count(struct list_head *list)
{
	struct list_head *pos;
	unsigned long count = 0;

	list_for_each(pos, list)
		count++;

	return count;
}

void intel_engine_dump(struct intel_engine_cs *engine,
		       struct drm_printer *m,
		       const char *header, ...)
{
	struct i915_gpu_error * const error = &engine->i915->gpu_error;
	struct i915_request *rq;
	intel_wakeref_t wakeref;
	unsigned long flags;
	ktime_t dummy;

	if (header) {
		va_list ap;

		va_start(ap, header);
		drm_vprintf(m, header, &ap);
		va_end(ap);
	}

	if (intel_gt_is_wedged(engine->gt))
		drm_printf(m, "*** WEDGED ***\n");

	drm_printf(m, "\tAwake? %d\n", atomic_read(&engine->wakeref.count));
	drm_printf(m, "\tBarriers?: %s\n",
		   yesno(!llist_empty(&engine->barrier_tasks)));
	drm_printf(m, "\tLatency: %luus\n",
		   ewma__engine_latency_read(&engine->latency));
	if (intel_engine_supports_stats(engine))
		drm_printf(m, "\tRuntime: %llums\n",
			   ktime_to_ms(intel_engine_get_busy_time(engine,
								  &dummy)));
	drm_printf(m, "\tForcewake: %x domains, %d active\n",
		   engine->fw_domain, atomic_read(&engine->fw_active));

	rcu_read_lock();
	rq = READ_ONCE(engine->heartbeat.systole);
	if (rq)
		drm_printf(m, "\tHeartbeat: %d ms ago\n",
			   jiffies_to_msecs(jiffies - rq->emitted_jiffies));
	rcu_read_unlock();
	drm_printf(m, "\tReset count: %d (global %d)\n",
		   i915_reset_engine_count(error, engine),
		   i915_reset_count(error));

	drm_printf(m, "\tRequests:\n");

	spin_lock_irqsave(&engine->active.lock, flags);
	rq = intel_engine_find_active_request(engine);
	if (rq) {
		struct intel_timeline *tl = get_timeline(rq);

		print_request(m, rq, "\t\tactive ");

		drm_printf(m, "\t\tring->start:  0x%08x\n",
			   i915_ggtt_offset(rq->ring->vma));
		drm_printf(m, "\t\tring->head:   0x%08x\n",
			   rq->ring->head);
		drm_printf(m, "\t\tring->tail:   0x%08x\n",
			   rq->ring->tail);
		drm_printf(m, "\t\tring->emit:   0x%08x\n",
			   rq->ring->emit);
		drm_printf(m, "\t\tring->space:  0x%08x\n",
			   rq->ring->space);

		if (tl) {
			drm_printf(m, "\t\tring->hwsp:   0x%08x\n",
				   tl->hwsp_offset);
			intel_timeline_put(tl);
		}

		print_request_ring(m, rq);

		if (rq->context->lrc_reg_state) {
			drm_printf(m, "Logical Ring Context:\n");
			hexdump(m, rq->context->lrc_reg_state, PAGE_SIZE);
		}
	}
	drm_printf(m, "\tOn hold?: %lu\n", list_count(&engine->active.hold));
	spin_unlock_irqrestore(&engine->active.lock, flags);

	drm_printf(m, "\tMMIO base:  0x%08x\n", engine->mmio_base);
	wakeref = intel_runtime_pm_get_if_in_use(engine->uncore->rpm);
	if (wakeref) {
		intel_engine_print_registers(engine, m);
		intel_runtime_pm_put(engine->uncore->rpm, wakeref);
	} else {
		drm_printf(m, "\tDevice is asleep; skipping register dump\n");
	}

	intel_execlists_show_requests(engine, m, print_request, 8);

	drm_printf(m, "HWSP:\n");
	hexdump(m, engine->status_page.addr, PAGE_SIZE);

	drm_printf(m, "Idle? %s\n", yesno(intel_engine_is_idle(engine)));

	intel_engine_print_breadcrumbs(engine, m);
}

static ktime_t __intel_engine_get_busy_time(struct intel_engine_cs *engine,
					    ktime_t *now)
{
	ktime_t total = engine->stats.total;

	/*
	 * If the engine is executing something at the moment
	 * add it to the total.
	 */
	*now = ktime_get();
	if (atomic_read(&engine->stats.active))
		total = ktime_add(total, ktime_sub(*now, engine->stats.start));

	return total;
}

/**
 * intel_engine_get_busy_time() - Return current accumulated engine busyness
 * @engine: engine to report on
 * @now: monotonic timestamp of sampling
 *
 * Returns accumulated time @engine was busy since engine stats were enabled.
 */
ktime_t intel_engine_get_busy_time(struct intel_engine_cs *engine, ktime_t *now)
{
	unsigned int seq;
	ktime_t total;

	do {
		seq = read_seqbegin(&engine->stats.lock);
		total = __intel_engine_get_busy_time(engine, now);
	} while (read_seqretry(&engine->stats.lock, seq));

	return total;
}

static bool match_ring(struct i915_request *rq)
{
	u32 ring = ENGINE_READ(rq->engine, RING_START);

	return ring == i915_ggtt_offset(rq->ring->vma);
}

struct i915_request *
intel_engine_find_active_request(struct intel_engine_cs *engine)
{
	struct i915_request *request, *active = NULL;

	/*
	 * We are called by the error capture, reset and to dump engine
	 * state at random points in time. In particular, note that neither is
	 * crucially ordered with an interrupt. After a hang, the GPU is dead
	 * and we assume that no more writes can happen (we waited long enough
	 * for all writes that were in transaction to be flushed) - adding an
	 * extra delay for a recent interrupt is pointless. Hence, we do
	 * not need an engine->irq_seqno_barrier() before the seqno reads.
	 * At all other times, we must assume the GPU is still running, but
	 * we only care about the snapshot of this moment.
	 */
	lockdep_assert_held(&engine->active.lock);

	rcu_read_lock();
	request = execlists_active(&engine->execlists);
	if (request) {
		struct intel_timeline *tl = request->context->timeline;

		list_for_each_entry_from_reverse(request, &tl->requests, link) {
			if (i915_request_completed(request))
				break;

			active = request;
		}
	}
	rcu_read_unlock();
	if (active)
		return active;

	list_for_each_entry(request, &engine->active.requests, sched.link) {
		if (i915_request_completed(request))
			continue;

		if (!i915_request_started(request))
			continue;

		/* More than one preemptible request may match! */
		if (!match_ring(request))
			continue;

		active = request;
		break;
	}

	return active;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "mock_engine.c"
#include "selftest_engine.c"
#include "selftest_engine_cs.c"
#endif
