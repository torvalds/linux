/*
 *
 * (C) COPYRIGHT 2018-2020 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#include "mali_kbase_hwcnt_backend_jm.h"
#include "mali_kbase_hwcnt_gpu.h"
#include "mali_kbase_hwcnt_types.h"
#include "mali_kbase.h"
#include "mali_kbase_pm_ca.h"
#include "mali_kbase_hwaccess_instr.h"
#include "mali_kbase_hwaccess_time.h"
#include "mali_kbase_ccswe.h"

#ifdef CONFIG_MALI_BIFROST_NO_MALI
#include "backend/gpu/mali_kbase_model_dummy.h"
#endif
#include "backend/gpu/mali_kbase_clk_rate_trace_mgr.h"

#if MALI_USE_CSF
#include "mali_kbase_ctx_sched.h"
#else
#include "backend/gpu/mali_kbase_pm_internal.h"
#endif

/**
 * struct kbase_hwcnt_backend_jm_info - Information used to create an instance
 *                                      of a JM hardware counter backend.
 * @kbdev:         KBase device.
 * @use_secondary: True if secondary performance counters should be used,
 *                 else false. Ignored if secondary counters are not supported.
 * @metadata:      Hardware counter metadata.
 * @dump_bytes:    Bytes of GPU memory required to perform a
 *                 hardware counter dump.
 */
struct kbase_hwcnt_backend_jm_info {
	struct kbase_device *kbdev;
	bool use_secondary;
	const struct kbase_hwcnt_metadata *metadata;
	size_t dump_bytes;
};

/**
 * struct kbase_hwcnt_backend_jm - Instance of a JM hardware counter backend.
 * @info:             Info used to create the backend.
 * @kctx:             KBase context used for GPU memory allocation and
 *                    counter dumping.
 * @gpu_dump_va:      GPU hardware counter dump buffer virtual address.
 * @cpu_dump_va:      CPU mapping of gpu_dump_va.
 * @vmap:             Dump buffer vmap.
 * @enabled:          True if dumping has been enabled, else false.
 * @pm_core_mask:     PM state sync-ed shaders core mask for the enabled
 *                    dumping.
 * @clk_enable_map:   The enable map specifying enabled clock domains.
 * @cycle_count_elapsed:
 *                    Cycle count elapsed for a given sample period.
 *                    The top clock cycle, index 0, is read directly from
 *                    hardware, but the other clock domains need to be
 *                    calculated with software estimation.
 * @prev_cycle_count: Previous cycle count to calculate the cycle count for
 *                    sample period.
 * @rate_listener:    Clock rate listener callback state.
 * @ccswe_shader_cores: Shader cores cycle count software estimator.
 */
struct kbase_hwcnt_backend_jm {
	const struct kbase_hwcnt_backend_jm_info *info;
	struct kbase_context *kctx;
	u64 gpu_dump_va;
	void *cpu_dump_va;
	struct kbase_vmap_struct *vmap;
	bool enabled;
	u64 pm_core_mask;
	u64 clk_enable_map;
	u64 cycle_count_elapsed[BASE_MAX_NR_CLOCKS_REGULATORS];
	u64 prev_cycle_count[BASE_MAX_NR_CLOCKS_REGULATORS];
	struct kbase_clk_rate_listener rate_listener;
	struct kbase_ccswe ccswe_shader_cores;
};

/**
 * kbasep_hwcnt_backend_jm_on_freq_change() - On freq change callback
 *
 * @rate_listener:    Callback state
 * @clk_index:        Clock index
 * @clk_rate_hz:      Clock frequency(hz)
 */
static void kbasep_hwcnt_backend_jm_on_freq_change(
	struct kbase_clk_rate_listener *rate_listener,
	u32 clk_index,
	u32 clk_rate_hz)
{
	struct kbase_hwcnt_backend_jm *backend_jm = container_of(
		rate_listener, struct kbase_hwcnt_backend_jm, rate_listener);
	u64 timestamp_ns;

	if (clk_index != KBASE_CLOCK_DOMAIN_SHADER_CORES)
		return;

	timestamp_ns = ktime_get_raw_ns();
	kbase_ccswe_freq_change(
		&backend_jm->ccswe_shader_cores, timestamp_ns, clk_rate_hz);
}

/**
 * kbasep_hwcnt_backend_jm_cc_enable() - Enable cycle count tracking
 *
 * @backend:      Non-NULL pointer to backend.
 * @enable_map:   Non-NULL pointer to enable map specifying enabled counters.
 * @timestamp_ns: Timestamp(ns) when HWCNT were enabled.
 */
static void kbasep_hwcnt_backend_jm_cc_enable(
	struct kbase_hwcnt_backend_jm *backend_jm,
	const struct kbase_hwcnt_enable_map *enable_map,
	u64 timestamp_ns)
{
	struct kbase_device *kbdev = backend_jm->kctx->kbdev;
	u64 clk_enable_map = enable_map->clk_enable_map;
	u64 cycle_count;

	if (kbase_hwcnt_clk_enable_map_enabled(
		    clk_enable_map, KBASE_CLOCK_DOMAIN_TOP)) {
#if !MALI_USE_CSF
		/* turn on the cycle counter */
		kbase_pm_request_gpu_cycle_counter_l2_is_on(kbdev);
#endif
		/* Read cycle count for top clock domain. */
		kbase_backend_get_gpu_time_norequest(
			kbdev, &cycle_count, NULL, NULL);

		backend_jm->prev_cycle_count[KBASE_CLOCK_DOMAIN_TOP] =
			cycle_count;
	}

	if (kbase_hwcnt_clk_enable_map_enabled(
		    clk_enable_map, KBASE_CLOCK_DOMAIN_SHADER_CORES)) {
		/* software estimation for non-top clock domains */
		struct kbase_clk_rate_trace_manager *rtm = &kbdev->pm.clk_rtm;
		const struct kbase_clk_data *clk_data =
			rtm->clks[KBASE_CLOCK_DOMAIN_SHADER_CORES];
		u32 cur_freq;
		unsigned long flags;

		spin_lock_irqsave(&rtm->lock, flags);

		cur_freq = (u32) clk_data->clock_val;
		kbase_ccswe_reset(&backend_jm->ccswe_shader_cores);
		kbase_ccswe_freq_change(
			&backend_jm->ccswe_shader_cores,
			timestamp_ns,
			cur_freq);

		kbase_clk_rate_trace_manager_subscribe_no_lock(
			rtm, &backend_jm->rate_listener);

		spin_unlock_irqrestore(&rtm->lock, flags);

		/* ccswe was reset. The estimated cycle is zero. */
		backend_jm->prev_cycle_count[
			KBASE_CLOCK_DOMAIN_SHADER_CORES] = 0;
	}

	/* Keep clk_enable_map for dump_request. */
	backend_jm->clk_enable_map = clk_enable_map;
}

/**
 * kbasep_hwcnt_backend_jm_cc_disable() - Disable cycle count tracking
 *
 * @backend:      Non-NULL pointer to backend.
 */
static void kbasep_hwcnt_backend_jm_cc_disable(
	struct kbase_hwcnt_backend_jm *backend_jm)
{
	struct kbase_device *kbdev = backend_jm->kctx->kbdev;
	struct kbase_clk_rate_trace_manager *rtm = &kbdev->pm.clk_rtm;
	u64 clk_enable_map = backend_jm->clk_enable_map;

#if !MALI_USE_CSF
	if (kbase_hwcnt_clk_enable_map_enabled(
		clk_enable_map, KBASE_CLOCK_DOMAIN_TOP)) {
		/* turn off the cycle counter */
		kbase_pm_release_gpu_cycle_counter(kbdev);
	}
#endif
	if (kbase_hwcnt_clk_enable_map_enabled(
		clk_enable_map, KBASE_CLOCK_DOMAIN_SHADER_CORES)) {

		kbase_clk_rate_trace_manager_unsubscribe(
			rtm, &backend_jm->rate_listener);
	}
}


/* JM backend implementation of kbase_hwcnt_backend_timestamp_ns_fn */
static u64 kbasep_hwcnt_backend_jm_timestamp_ns(
	struct kbase_hwcnt_backend *backend)
{
	(void)backend;
	return ktime_get_raw_ns();
}

/* JM backend implementation of kbase_hwcnt_backend_dump_enable_nolock_fn */
static int kbasep_hwcnt_backend_jm_dump_enable_nolock(
	struct kbase_hwcnt_backend *backend,
	const struct kbase_hwcnt_enable_map *enable_map)
{
	int errcode;
	struct kbase_hwcnt_backend_jm *backend_jm =
		(struct kbase_hwcnt_backend_jm *)backend;
	struct kbase_context *kctx;
	struct kbase_device *kbdev;
	struct kbase_hwcnt_physical_enable_map phys;
	struct kbase_instr_hwcnt_enable enable;
	u64 timestamp_ns;

	if (!backend_jm || !enable_map || backend_jm->enabled ||
	    (enable_map->metadata != backend_jm->info->metadata))
		return -EINVAL;

	kctx = backend_jm->kctx;
	kbdev = backend_jm->kctx->kbdev;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	kbase_hwcnt_gpu_enable_map_to_physical(&phys, enable_map);

	enable.fe_bm = phys.fe_bm;
	enable.shader_bm = phys.shader_bm;
	enable.tiler_bm = phys.tiler_bm;
	enable.mmu_l2_bm = phys.mmu_l2_bm;
	enable.use_secondary = backend_jm->info->use_secondary;
	enable.dump_buffer = backend_jm->gpu_dump_va;
	enable.dump_buffer_bytes = backend_jm->info->dump_bytes;

	timestamp_ns = kbasep_hwcnt_backend_jm_timestamp_ns(backend);

	errcode = kbase_instr_hwcnt_enable_internal(kbdev, kctx, &enable);
	if (errcode)
		goto error;

	backend_jm->pm_core_mask = kbase_pm_ca_get_instr_core_mask(kbdev);
	backend_jm->enabled = true;

	kbasep_hwcnt_backend_jm_cc_enable(backend_jm, enable_map, timestamp_ns);

	return 0;
error:
	return errcode;
}

/* JM backend implementation of kbase_hwcnt_backend_dump_enable_fn */
static int kbasep_hwcnt_backend_jm_dump_enable(
	struct kbase_hwcnt_backend *backend,
	const struct kbase_hwcnt_enable_map *enable_map)
{
	unsigned long flags;
	int errcode;
	struct kbase_hwcnt_backend_jm *backend_jm =
		(struct kbase_hwcnt_backend_jm *)backend;
	struct kbase_device *kbdev;

	if (!backend_jm)
		return -EINVAL;

	kbdev = backend_jm->kctx->kbdev;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	errcode = kbasep_hwcnt_backend_jm_dump_enable_nolock(
		backend, enable_map);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return errcode;
}

/* JM backend implementation of kbase_hwcnt_backend_dump_disable_fn */
static void kbasep_hwcnt_backend_jm_dump_disable(
	struct kbase_hwcnt_backend *backend)
{
	int errcode;
	struct kbase_hwcnt_backend_jm *backend_jm =
		(struct kbase_hwcnt_backend_jm *)backend;

	if (WARN_ON(!backend_jm) || !backend_jm->enabled)
		return;

	kbasep_hwcnt_backend_jm_cc_disable(backend_jm);

	errcode = kbase_instr_hwcnt_disable_internal(backend_jm->kctx);
	WARN_ON(errcode);

	backend_jm->enabled = false;
}

/* JM backend implementation of kbase_hwcnt_backend_dump_clear_fn */
static int kbasep_hwcnt_backend_jm_dump_clear(
	struct kbase_hwcnt_backend *backend)
{
	struct kbase_hwcnt_backend_jm *backend_jm =
		(struct kbase_hwcnt_backend_jm *)backend;

	if (!backend_jm || !backend_jm->enabled)
		return -EINVAL;

	return kbase_instr_hwcnt_clear(backend_jm->kctx);
}

/* JM backend implementation of kbase_hwcnt_backend_dump_request_fn */
static int kbasep_hwcnt_backend_jm_dump_request(
	struct kbase_hwcnt_backend *backend,
	u64 *dump_time_ns)
{
	struct kbase_hwcnt_backend_jm *backend_jm =
		(struct kbase_hwcnt_backend_jm *)backend;
	struct kbase_device *kbdev;
	const struct kbase_hwcnt_metadata *metadata;
	u64 current_cycle_count;
	size_t clk;
	int ret;

	if (!backend_jm || !backend_jm->enabled)
		return -EINVAL;

	kbdev = backend_jm->kctx->kbdev;
	metadata = backend_jm->info->metadata;

	/* Disable pre-emption, to make the timestamp as accurate as possible */
	preempt_disable();
	{
		*dump_time_ns = kbasep_hwcnt_backend_jm_timestamp_ns(backend);
		ret = kbase_instr_hwcnt_request_dump(backend_jm->kctx);

		kbase_hwcnt_metadata_for_each_clock(metadata, clk) {
			if (!kbase_hwcnt_clk_enable_map_enabled(
				backend_jm->clk_enable_map, clk))
				continue;

			if (clk == KBASE_CLOCK_DOMAIN_TOP) {
				/* Read cycle count for top clock domain. */
				kbase_backend_get_gpu_time_norequest(
					kbdev, &current_cycle_count,
					NULL, NULL);
			} else {
				/*
				 * Estimate cycle count for non-top clock
				 * domain.
				 */
				current_cycle_count = kbase_ccswe_cycle_at(
					&backend_jm->ccswe_shader_cores,
					*dump_time_ns);
			}
			backend_jm->cycle_count_elapsed[clk] =
				current_cycle_count -
				backend_jm->prev_cycle_count[clk];

			/*
			 * Keep the current cycle count for later calculation.
			 */
			backend_jm->prev_cycle_count[clk] = current_cycle_count;
		}
	}
	preempt_enable();

	return ret;
}

/* JM backend implementation of kbase_hwcnt_backend_dump_wait_fn */
static int kbasep_hwcnt_backend_jm_dump_wait(
	struct kbase_hwcnt_backend *backend)
{
	struct kbase_hwcnt_backend_jm *backend_jm =
		(struct kbase_hwcnt_backend_jm *)backend;

	if (!backend_jm || !backend_jm->enabled)
		return -EINVAL;

	return kbase_instr_hwcnt_wait_for_dump(backend_jm->kctx);
}

/* JM backend implementation of kbase_hwcnt_backend_dump_get_fn */
static int kbasep_hwcnt_backend_jm_dump_get(
	struct kbase_hwcnt_backend *backend,
	struct kbase_hwcnt_dump_buffer *dst,
	const struct kbase_hwcnt_enable_map *dst_enable_map,
	bool accumulate)
{
	struct kbase_hwcnt_backend_jm *backend_jm =
		(struct kbase_hwcnt_backend_jm *)backend;
	size_t clk;

	if (!backend_jm || !dst || !dst_enable_map ||
	    (backend_jm->info->metadata != dst->metadata) ||
	    (dst_enable_map->metadata != dst->metadata))
		return -EINVAL;

	/* Invalidate the kernel buffer before reading from it. */
	kbase_sync_mem_regions(
		backend_jm->kctx, backend_jm->vmap, KBASE_SYNC_TO_CPU);

	kbase_hwcnt_metadata_for_each_clock(dst_enable_map->metadata, clk) {
		if (!kbase_hwcnt_clk_enable_map_enabled(
			dst_enable_map->clk_enable_map, clk))
			continue;

		/* Extract elapsed cycle count for each clock domain. */
		dst->clk_cnt_buf[clk] = backend_jm->cycle_count_elapsed[clk];
	}

	return kbase_hwcnt_gpu_dump_get(
		dst, backend_jm->cpu_dump_va, dst_enable_map,
		backend_jm->pm_core_mask, accumulate);
}

/**
 * kbasep_hwcnt_backend_jm_dump_alloc() - Allocate a GPU dump buffer.
 * @info:        Non-NULL pointer to JM backend info.
 * @kctx:        Non-NULL pointer to kbase context.
 * @gpu_dump_va: Non-NULL pointer to where GPU dump buffer virtual address
 *               is stored on success.
 *
 * Return: 0 on success, else error code.
 */
static int kbasep_hwcnt_backend_jm_dump_alloc(
	const struct kbase_hwcnt_backend_jm_info *info,
	struct kbase_context *kctx,
	u64 *gpu_dump_va)
{
	struct kbase_va_region *reg;
	u64 flags;
	u64 nr_pages;

	WARN_ON(!info);
	WARN_ON(!kctx);
	WARN_ON(!gpu_dump_va);

	flags = BASE_MEM_PROT_CPU_RD |
		BASE_MEM_PROT_GPU_WR |
		BASEP_MEM_PERMANENT_KERNEL_MAPPING |
		BASE_MEM_CACHED_CPU;

	if (kctx->kbdev->mmu_mode->flags & KBASE_MMU_MODE_HAS_NON_CACHEABLE)
		flags |= BASE_MEM_UNCACHED_GPU;

	nr_pages = PFN_UP(info->dump_bytes);

	reg = kbase_mem_alloc(kctx, nr_pages, nr_pages, 0, &flags, gpu_dump_va);

	if (!reg)
		return -ENOMEM;

	return 0;
}

/**
 * kbasep_hwcnt_backend_jm_dump_free() - Free an allocated GPU dump buffer.
 * @kctx:        Non-NULL pointer to kbase context.
 * @gpu_dump_va: GPU dump buffer virtual address.
 */
static void kbasep_hwcnt_backend_jm_dump_free(
	struct kbase_context *kctx,
	u64 gpu_dump_va)
{
	WARN_ON(!kctx);
	if (gpu_dump_va)
		kbase_mem_free(kctx, gpu_dump_va);
}

/**
 * kbasep_hwcnt_backend_jm_destroy() - Destroy a JM backend.
 * @backend: Pointer to JM backend to destroy.
 *
 * Can be safely called on a backend in any state of partial construction.
 */
static void kbasep_hwcnt_backend_jm_destroy(
	struct kbase_hwcnt_backend_jm *backend)
{
	if (!backend)
		return;

	if (backend->kctx) {
#if MALI_USE_CSF
		unsigned long flags;
#endif
		struct kbase_context *kctx = backend->kctx;
		struct kbase_device *kbdev = kctx->kbdev;

		if (backend->cpu_dump_va)
			kbase_phy_alloc_mapping_put(kctx, backend->vmap);

		if (backend->gpu_dump_va)
			kbasep_hwcnt_backend_jm_dump_free(
				kctx, backend->gpu_dump_va);

#if MALI_USE_CSF
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		kbase_ctx_sched_release_ctx(kctx);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
#else
		kbasep_js_release_privileged_ctx(kbdev, kctx);
#endif
		kbase_destroy_context(kctx);
	}

	kfree(backend);
}

/**
 * kbasep_hwcnt_backend_jm_create() - Create a JM backend.
 * @info:        Non-NULL pointer to backend info.
 * @out_backend: Non-NULL pointer to where backend is stored on success.
 *
 * Return: 0 on success, else error code.
 */
static int kbasep_hwcnt_backend_jm_create(
	const struct kbase_hwcnt_backend_jm_info *info,
	struct kbase_hwcnt_backend_jm **out_backend)
{
#if MALI_USE_CSF
	unsigned long flags;
#endif
	int errcode;
	struct kbase_device *kbdev;
	struct kbase_hwcnt_backend_jm *backend = NULL;

	WARN_ON(!info);
	WARN_ON(!out_backend);

	kbdev = info->kbdev;

	backend = kzalloc(sizeof(*backend), GFP_KERNEL);
	if (!backend)
		goto alloc_error;

	backend->info = info;

	backend->kctx = kbase_create_context(kbdev, true,
		BASE_CONTEXT_SYSTEM_MONITOR_SUBMIT_DISABLED, 0, NULL);
	if (!backend->kctx)
		goto alloc_error;

#if MALI_USE_CSF
	kbase_pm_context_active(kbdev);
	mutex_lock(&kbdev->mmu_hw_mutex);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_ctx_sched_retain_ctx(backend->kctx);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	mutex_unlock(&kbdev->mmu_hw_mutex);
	kbase_pm_context_idle(kbdev);
#else
	kbasep_js_schedule_privileged_ctx(kbdev, backend->kctx);
#endif

	errcode = kbasep_hwcnt_backend_jm_dump_alloc(
		info, backend->kctx, &backend->gpu_dump_va);
	if (errcode)
		goto error;

	backend->cpu_dump_va = kbase_phy_alloc_mapping_get(backend->kctx,
		backend->gpu_dump_va, &backend->vmap);
	if (!backend->cpu_dump_va)
		goto alloc_error;

	kbase_ccswe_init(&backend->ccswe_shader_cores);
	backend->rate_listener.notify = kbasep_hwcnt_backend_jm_on_freq_change;

#ifdef CONFIG_MALI_BIFROST_NO_MALI
	/* The dummy model needs the CPU mapping. */
	gpu_model_set_dummy_prfcnt_base_cpu(backend->cpu_dump_va);
#endif

	*out_backend = backend;
	return 0;

alloc_error:
	errcode = -ENOMEM;
error:
	kbasep_hwcnt_backend_jm_destroy(backend);
	return errcode;
}

/* JM backend implementation of kbase_hwcnt_backend_init_fn */
static int kbasep_hwcnt_backend_jm_init(
	const struct kbase_hwcnt_backend_info *info,
	struct kbase_hwcnt_backend **out_backend)
{
	int errcode;
	struct kbase_hwcnt_backend_jm *backend = NULL;

	if (!info || !out_backend)
		return -EINVAL;

	errcode = kbasep_hwcnt_backend_jm_create(
		(const struct kbase_hwcnt_backend_jm_info *) info, &backend);
	if (errcode)
		return errcode;

	*out_backend = (struct kbase_hwcnt_backend *)backend;

	return 0;
}

/* JM backend implementation of kbase_hwcnt_backend_term_fn */
static void kbasep_hwcnt_backend_jm_term(struct kbase_hwcnt_backend *backend)
{
	if (!backend)
		return;

	kbasep_hwcnt_backend_jm_dump_disable(backend);
	kbasep_hwcnt_backend_jm_destroy(
		(struct kbase_hwcnt_backend_jm *)backend);
}

/**
 * kbasep_hwcnt_backend_jm_info_destroy() - Destroy a JM backend info.
 * @info: Pointer to info to destroy.
 *
 * Can be safely called on a backend info in any state of partial construction.
 */
static void kbasep_hwcnt_backend_jm_info_destroy(
	const struct kbase_hwcnt_backend_jm_info *info)
{
	if (!info)
		return;

	kbase_hwcnt_gpu_metadata_destroy(info->metadata);
	kfree(info);
}

/**
 * kbasep_hwcnt_backend_jm_info_create() - Create a JM backend info.
 * @kbdev: Non_NULL pointer to kbase device.
 * @out_info: Non-NULL pointer to where info is stored on success.
 *
 * Return 0 on success, else error code.
 */
static int kbasep_hwcnt_backend_jm_info_create(
	struct kbase_device *kbdev,
	const struct kbase_hwcnt_backend_jm_info **out_info)
{
	int errcode = -ENOMEM;
	struct kbase_hwcnt_gpu_info hwcnt_gpu_info;
	struct kbase_hwcnt_backend_jm_info *info = NULL;

	WARN_ON(!kbdev);
	WARN_ON(!out_info);

	errcode = kbase_hwcnt_gpu_info_init(kbdev, &hwcnt_gpu_info);
	if (errcode)
		return errcode;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		goto error;

	info->kbdev = kbdev;

#ifdef CONFIG_MALI_BIFROST_PRFCNT_SET_SECONDARY
	info->use_secondary = true;
#else
	info->use_secondary = false;
#endif

	errcode = kbase_hwcnt_gpu_metadata_create(
		&hwcnt_gpu_info, info->use_secondary,
		&info->metadata,
		&info->dump_bytes);
	if (errcode)
		goto error;

	*out_info = info;

	return 0;
error:
	kbasep_hwcnt_backend_jm_info_destroy(info);
	return errcode;
}

int kbase_hwcnt_backend_jm_create(
	struct kbase_device *kbdev,
	struct kbase_hwcnt_backend_interface *iface)
{
	int errcode;
	const struct kbase_hwcnt_backend_jm_info *info = NULL;

	if (!kbdev || !iface)
		return -EINVAL;

	errcode = kbasep_hwcnt_backend_jm_info_create(kbdev, &info);

	if (errcode)
		return errcode;

	iface->metadata = info->metadata;
	iface->info = (struct kbase_hwcnt_backend_info *)info;
	iface->init = kbasep_hwcnt_backend_jm_init;
	iface->term = kbasep_hwcnt_backend_jm_term;
	iface->timestamp_ns = kbasep_hwcnt_backend_jm_timestamp_ns;
	iface->dump_enable = kbasep_hwcnt_backend_jm_dump_enable;
	iface->dump_enable_nolock = kbasep_hwcnt_backend_jm_dump_enable_nolock;
	iface->dump_disable = kbasep_hwcnt_backend_jm_dump_disable;
	iface->dump_clear = kbasep_hwcnt_backend_jm_dump_clear;
	iface->dump_request = kbasep_hwcnt_backend_jm_dump_request;
	iface->dump_wait = kbasep_hwcnt_backend_jm_dump_wait;
	iface->dump_get = kbasep_hwcnt_backend_jm_dump_get;

	return 0;
}

void kbase_hwcnt_backend_jm_destroy(
	struct kbase_hwcnt_backend_interface *iface)
{
	if (!iface)
		return;

	kbasep_hwcnt_backend_jm_info_destroy(
		(const struct kbase_hwcnt_backend_jm_info *)iface->info);
	memset(iface, 0, sizeof(*iface));
}
