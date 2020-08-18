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

#include "mali_kbase_hwcnt_backend_gpu.h"
#include "mali_kbase_hwcnt_gpu.h"
#include "mali_kbase_hwcnt_types.h"
#include "mali_kbase.h"
#include "mali_kbase_pm_ca.h"
#include "mali_kbase_hwaccess_instr.h"
#ifdef CONFIG_MALI_BIFROST_NO_MALI
#include "backend/gpu/mali_kbase_model_dummy.h"
#endif


/**
 * struct kbase_hwcnt_backend_gpu_info - Information used to create an instance
 *                                       of a GPU hardware counter backend.
 * @kbdev:         KBase device.
 * @use_secondary: True if secondary performance counters should be used,
 *                 else false. Ignored if secondary counters are not supported.
 * @metadata:      Hardware counter metadata.
 * @dump_bytes:    Bytes of GPU memory required to perform a
 *                 hardware counter dump.
 */
struct kbase_hwcnt_backend_gpu_info {
	struct kbase_device *kbdev;
	bool use_secondary;
	const struct kbase_hwcnt_metadata *metadata;
	size_t dump_bytes;
};

/**
 * struct kbase_hwcnt_backend_gpu - Instance of a GPU hardware counter backend.
 * @info:         Info used to create the backend.
 * @kctx:         KBase context used for GPU memory allocation and
 *                counter dumping.
 * @gpu_dump_va:  GPU hardware counter dump buffer virtual address.
 * @cpu_dump_va:  CPU mapping of gpu_dump_va.
 * @vmap:         Dump buffer vmap.
 * @enabled:      True if dumping has been enabled, else false.
 * @pm_core_mask:  PM state sync-ed shaders core mask for the enabled dumping.
 */
struct kbase_hwcnt_backend_gpu {
	const struct kbase_hwcnt_backend_gpu_info *info;
	struct kbase_context *kctx;
	u64 gpu_dump_va;
	void *cpu_dump_va;
	struct kbase_vmap_struct *vmap;
	bool enabled;
	u64 pm_core_mask;
};

/* GPU backend implementation of kbase_hwcnt_backend_timestamp_ns_fn */
static u64 kbasep_hwcnt_backend_gpu_timestamp_ns(
	struct kbase_hwcnt_backend *backend)
{
	(void)backend;
	return ktime_get_raw_ns();
}

/* GPU backend implementation of kbase_hwcnt_backend_dump_enable_nolock_fn */
static int kbasep_hwcnt_backend_gpu_dump_enable_nolock(
	struct kbase_hwcnt_backend *backend,
	const struct kbase_hwcnt_enable_map *enable_map)
{
	int errcode;
	struct kbase_hwcnt_backend_gpu *backend_gpu =
		(struct kbase_hwcnt_backend_gpu *)backend;
	struct kbase_context *kctx;
	struct kbase_device *kbdev;
	struct kbase_hwcnt_physical_enable_map phys;
	struct kbase_instr_hwcnt_enable enable;

	if (!backend_gpu || !enable_map || backend_gpu->enabled ||
	    (enable_map->metadata != backend_gpu->info->metadata))
		return -EINVAL;

	kctx = backend_gpu->kctx;
	kbdev = backend_gpu->kctx->kbdev;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	kbase_hwcnt_gpu_enable_map_to_physical(&phys, enable_map);

	enable.jm_bm = phys.jm_bm;
	enable.shader_bm = phys.shader_bm;
	enable.tiler_bm = phys.tiler_bm;
	enable.mmu_l2_bm = phys.mmu_l2_bm;
	enable.use_secondary = backend_gpu->info->use_secondary;
	enable.dump_buffer = backend_gpu->gpu_dump_va;
	enable.dump_buffer_bytes = backend_gpu->info->dump_bytes;

	errcode = kbase_instr_hwcnt_enable_internal(kbdev, kctx, &enable);
	if (errcode)
		goto error;

	backend_gpu->pm_core_mask = kbase_pm_ca_get_instr_core_mask(kbdev);
	backend_gpu->enabled = true;

	return 0;
error:
	return errcode;
}

/* GPU backend implementation of kbase_hwcnt_backend_dump_enable_fn */
static int kbasep_hwcnt_backend_gpu_dump_enable(
	struct kbase_hwcnt_backend *backend,
	const struct kbase_hwcnt_enable_map *enable_map)
{
	unsigned long flags;
	int errcode;
	struct kbase_hwcnt_backend_gpu *backend_gpu =
		(struct kbase_hwcnt_backend_gpu *)backend;
	struct kbase_device *kbdev;

	if (!backend_gpu)
		return -EINVAL;

	kbdev = backend_gpu->kctx->kbdev;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	errcode = kbasep_hwcnt_backend_gpu_dump_enable_nolock(
		backend, enable_map);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return errcode;
}

/* GPU backend implementation of kbase_hwcnt_backend_dump_disable_fn */
static void kbasep_hwcnt_backend_gpu_dump_disable(
	struct kbase_hwcnt_backend *backend)
{
	int errcode;
	struct kbase_hwcnt_backend_gpu *backend_gpu =
		(struct kbase_hwcnt_backend_gpu *)backend;

	if (WARN_ON(!backend_gpu) || !backend_gpu->enabled)
		return;

	errcode = kbase_instr_hwcnt_disable_internal(backend_gpu->kctx);
	WARN_ON(errcode);

	backend_gpu->enabled = false;
}

/* GPU backend implementation of kbase_hwcnt_backend_dump_clear_fn */
static int kbasep_hwcnt_backend_gpu_dump_clear(
	struct kbase_hwcnt_backend *backend)
{
	struct kbase_hwcnt_backend_gpu *backend_gpu =
		(struct kbase_hwcnt_backend_gpu *)backend;

	if (!backend_gpu || !backend_gpu->enabled)
		return -EINVAL;

	return kbase_instr_hwcnt_clear(backend_gpu->kctx);
}

/* GPU backend implementation of kbase_hwcnt_backend_dump_request_fn */
static int kbasep_hwcnt_backend_gpu_dump_request(
	struct kbase_hwcnt_backend *backend)
{
	struct kbase_hwcnt_backend_gpu *backend_gpu =
		(struct kbase_hwcnt_backend_gpu *)backend;

	if (!backend_gpu || !backend_gpu->enabled)
		return -EINVAL;

	return kbase_instr_hwcnt_request_dump(backend_gpu->kctx);
}

/* GPU backend implementation of kbase_hwcnt_backend_dump_wait_fn */
static int kbasep_hwcnt_backend_gpu_dump_wait(
	struct kbase_hwcnt_backend *backend)
{
	struct kbase_hwcnt_backend_gpu *backend_gpu =
		(struct kbase_hwcnt_backend_gpu *)backend;

	if (!backend_gpu || !backend_gpu->enabled)
		return -EINVAL;

	return kbase_instr_hwcnt_wait_for_dump(backend_gpu->kctx);
}

/* GPU backend implementation of kbase_hwcnt_backend_dump_get_fn */
static int kbasep_hwcnt_backend_gpu_dump_get(
	struct kbase_hwcnt_backend *backend,
	struct kbase_hwcnt_dump_buffer *dst,
	const struct kbase_hwcnt_enable_map *dst_enable_map,
	bool accumulate)
{
	struct kbase_hwcnt_backend_gpu *backend_gpu =
		(struct kbase_hwcnt_backend_gpu *)backend;

	if (!backend_gpu || !dst || !dst_enable_map ||
	    (backend_gpu->info->metadata != dst->metadata) ||
	    (dst_enable_map->metadata != dst->metadata))
		return -EINVAL;

	/* Invalidate the kernel buffer before reading from it. */
	kbase_sync_mem_regions(
		backend_gpu->kctx, backend_gpu->vmap, KBASE_SYNC_TO_CPU);

	return kbase_hwcnt_gpu_dump_get(
		dst, backend_gpu->cpu_dump_va, dst_enable_map,
		backend_gpu->pm_core_mask, accumulate);
}

/**
 * kbasep_hwcnt_backend_gpu_dump_alloc() - Allocate a GPU dump buffer.
 * @info:        Non-NULL pointer to GPU backend info.
 * @kctx:        Non-NULL pointer to kbase context.
 * @gpu_dump_va: Non-NULL pointer to where GPU dump buffer virtual address
 *               is stored on success.
 *
 * Return: 0 on success, else error code.
 */
static int kbasep_hwcnt_backend_gpu_dump_alloc(
	const struct kbase_hwcnt_backend_gpu_info *info,
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
 * kbasep_hwcnt_backend_gpu_dump_free() - Free an allocated GPU dump buffer.
 * @kctx:        Non-NULL pointer to kbase context.
 * @gpu_dump_va: GPU dump buffer virtual address.
 */
static void kbasep_hwcnt_backend_gpu_dump_free(
	struct kbase_context *kctx,
	u64 gpu_dump_va)
{
	WARN_ON(!kctx);
	if (gpu_dump_va)
		kbase_mem_free(kctx, gpu_dump_va);
}

/**
 * kbasep_hwcnt_backend_gpu_destroy() - Destroy a GPU backend.
 * @backend: Pointer to GPU backend to destroy.
 *
 * Can be safely called on a backend in any state of partial construction.
 */
static void kbasep_hwcnt_backend_gpu_destroy(
	struct kbase_hwcnt_backend_gpu *backend)
{
	if (!backend)
		return;

	if (backend->kctx) {
		struct kbase_context *kctx = backend->kctx;
		struct kbase_device *kbdev = kctx->kbdev;

		if (backend->cpu_dump_va)
			kbase_phy_alloc_mapping_put(kctx, backend->vmap);

		if (backend->gpu_dump_va)
			kbasep_hwcnt_backend_gpu_dump_free(
				kctx, backend->gpu_dump_va);

		kbasep_js_release_privileged_ctx(kbdev, kctx);
		kbase_destroy_context(kctx);
	}

	kfree(backend);
}

/**
 * kbasep_hwcnt_backend_gpu_create() - Create a GPU backend.
 * @info:        Non-NULL pointer to backend info.
 * @out_backend: Non-NULL pointer to where backend is stored on success.
 *
 * Return: 0 on success, else error code.
 */
static int kbasep_hwcnt_backend_gpu_create(
	const struct kbase_hwcnt_backend_gpu_info *info,
	struct kbase_hwcnt_backend_gpu **out_backend)
{

	int errcode;
	struct kbase_device *kbdev;
	struct kbase_hwcnt_backend_gpu *backend = NULL;

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

	kbasep_js_schedule_privileged_ctx(kbdev, backend->kctx);

	errcode = kbasep_hwcnt_backend_gpu_dump_alloc(
		info, backend->kctx, &backend->gpu_dump_va);
	if (errcode)
		goto error;

	backend->cpu_dump_va = kbase_phy_alloc_mapping_get(backend->kctx,
		backend->gpu_dump_va, &backend->vmap);
	if (!backend->cpu_dump_va)
		goto alloc_error;

#ifdef CONFIG_MALI_BIFROST_NO_MALI
	/* The dummy model needs the CPU mapping. */
	gpu_model_set_dummy_prfcnt_base_cpu(backend->cpu_dump_va);
#endif

	*out_backend = backend;
	return 0;

alloc_error:
	errcode = -ENOMEM;
error:
	kbasep_hwcnt_backend_gpu_destroy(backend);
	return errcode;
}

/* GPU backend implementation of kbase_hwcnt_backend_init_fn */
static int kbasep_hwcnt_backend_gpu_init(
	const struct kbase_hwcnt_backend_info *info,
	struct kbase_hwcnt_backend **out_backend)
{
	int errcode;
	struct kbase_hwcnt_backend_gpu *backend = NULL;

	if (!info || !out_backend)
		return -EINVAL;

	errcode = kbasep_hwcnt_backend_gpu_create(
		(const struct kbase_hwcnt_backend_gpu_info *) info, &backend);
	if (errcode)
		return errcode;

	*out_backend = (struct kbase_hwcnt_backend *)backend;

	return 0;
}

/* GPU backend implementation of kbase_hwcnt_backend_term_fn */
static void kbasep_hwcnt_backend_gpu_term(struct kbase_hwcnt_backend *backend)
{
	if (!backend)
		return;

	kbasep_hwcnt_backend_gpu_dump_disable(backend);
	kbasep_hwcnt_backend_gpu_destroy(
		(struct kbase_hwcnt_backend_gpu *)backend);
}

/**
 * kbasep_hwcnt_backend_gpu_info_destroy() - Destroy a GPU backend info.
 * @info: Pointer to info to destroy.
 *
 * Can be safely called on a backend info in any state of partial construction.
 */
static void kbasep_hwcnt_backend_gpu_info_destroy(
	const struct kbase_hwcnt_backend_gpu_info *info)
{
	if (!info)
		return;

	kbase_hwcnt_gpu_metadata_destroy(info->metadata);
	kfree(info);
}

/**
 * kbasep_hwcnt_backend_gpu_info_create() - Create a GPU backend info.
 * @kbdev: Non_NULL pointer to kbase device.
 * @out_info: Non-NULL pointer to where info is stored on success.
 *
 * Return 0 on success, else error code.
 */
static int kbasep_hwcnt_backend_gpu_info_create(
	struct kbase_device *kbdev,
	const struct kbase_hwcnt_backend_gpu_info **out_info)
{
	int errcode = -ENOMEM;
	struct kbase_hwcnt_gpu_info hwcnt_gpu_info;
	struct kbase_hwcnt_backend_gpu_info *info = NULL;

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
	kbasep_hwcnt_backend_gpu_info_destroy(info);
	return errcode;
}

int kbase_hwcnt_backend_gpu_create(
	struct kbase_device *kbdev,
	struct kbase_hwcnt_backend_interface *iface)
{
	int errcode;
	const struct kbase_hwcnt_backend_gpu_info *info = NULL;

	if (!kbdev || !iface)
		return -EINVAL;

	errcode = kbasep_hwcnt_backend_gpu_info_create(kbdev, &info);

	if (errcode)
		return errcode;

	iface->metadata = info->metadata;
	iface->info = (struct kbase_hwcnt_backend_info *)info;
	iface->init = kbasep_hwcnt_backend_gpu_init;
	iface->term = kbasep_hwcnt_backend_gpu_term;
	iface->timestamp_ns = kbasep_hwcnt_backend_gpu_timestamp_ns;
	iface->dump_enable = kbasep_hwcnt_backend_gpu_dump_enable;
	iface->dump_enable_nolock = kbasep_hwcnt_backend_gpu_dump_enable_nolock;
	iface->dump_disable = kbasep_hwcnt_backend_gpu_dump_disable;
	iface->dump_clear = kbasep_hwcnt_backend_gpu_dump_clear;
	iface->dump_request = kbasep_hwcnt_backend_gpu_dump_request;
	iface->dump_wait = kbasep_hwcnt_backend_gpu_dump_wait;
	iface->dump_get = kbasep_hwcnt_backend_gpu_dump_get;

	return 0;
}

void kbase_hwcnt_backend_gpu_destroy(
	struct kbase_hwcnt_backend_interface *iface)
{
	if (!iface)
		return;

	kbasep_hwcnt_backend_gpu_info_destroy(
		(const struct kbase_hwcnt_backend_gpu_info *)iface->info);
	memset(iface, 0, sizeof(*iface));
}
