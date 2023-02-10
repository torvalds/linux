// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2019-2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
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
 */

/*
 * Base kernel context APIs for CSF GPUs
 */

#include <context/mali_kbase_context_internal.h>
#include <gpu/mali_kbase_gpu_regmap.h>
#include <mali_kbase.h>
#include <mali_kbase_mem_linux.h>
#include <mali_kbase_mem_pool_group.h>
#include <mmu/mali_kbase_mmu.h>
#include <tl/mali_kbase_timeline.h>

#if IS_ENABLED(CONFIG_DEBUG_FS)
#include <csf/mali_kbase_csf_csg_debugfs.h>
#include <csf/mali_kbase_csf_kcpu_debugfs.h>
#include <csf/mali_kbase_csf_sync_debugfs.h>
#include <csf/mali_kbase_csf_tiler_heap_debugfs.h>
#include <csf/mali_kbase_csf_cpu_queue_debugfs.h>
#include <mali_kbase_debug_mem_view.h>
#include <mali_kbase_debug_mem_zones.h>
#include <mali_kbase_debug_mem_allocs.h>
#include <mali_kbase_mem_pool_debugfs.h>

void kbase_context_debugfs_init(struct kbase_context *const kctx)
{
	kbase_debug_mem_view_init(kctx);
	kbase_debug_mem_zones_init(kctx);
	kbase_debug_mem_allocs_init(kctx);
	kbase_mem_pool_debugfs_init(kctx->kctx_dentry, kctx);
	kbase_jit_debugfs_init(kctx);
	kbase_csf_queue_group_debugfs_init(kctx);
	kbase_csf_kcpu_debugfs_init(kctx);
	kbase_csf_sync_debugfs_init(kctx);
	kbase_csf_tiler_heap_debugfs_init(kctx);
	kbase_csf_tiler_heap_total_debugfs_init(kctx);
	kbase_csf_cpu_queue_debugfs_init(kctx);
}
KBASE_EXPORT_SYMBOL(kbase_context_debugfs_init);

void kbase_context_debugfs_term(struct kbase_context *const kctx)
{
	debugfs_remove_recursive(kctx->kctx_dentry);
}
KBASE_EXPORT_SYMBOL(kbase_context_debugfs_term);
#else
void kbase_context_debugfs_init(struct kbase_context *const kctx)
{
	CSTD_UNUSED(kctx);
}
KBASE_EXPORT_SYMBOL(kbase_context_debugfs_init);

void kbase_context_debugfs_term(struct kbase_context *const kctx)
{
	CSTD_UNUSED(kctx);
}
KBASE_EXPORT_SYMBOL(kbase_context_debugfs_term);
#endif /* CONFIG_DEBUG_FS */

static void kbase_context_free(struct kbase_context *kctx)
{
	kbase_timeline_post_kbase_context_destroy(kctx);

	vfree(kctx);
}

static const struct kbase_context_init context_init[] = {
	{ NULL, kbase_context_free, NULL },
	{ kbase_context_common_init, kbase_context_common_term,
	  "Common context initialization failed" },
	{ kbase_context_mem_pool_group_init, kbase_context_mem_pool_group_term,
	  "Memory pool group initialization failed" },
	{ kbase_mem_evictable_init, kbase_mem_evictable_deinit,
	  "Memory evictable initialization failed" },
	{ kbase_context_mmu_init, kbase_context_mmu_term,
	  "MMU initialization failed" },
	{ kbase_context_mem_alloc_page, kbase_context_mem_pool_free,
	  "Memory alloc page failed" },
	{ kbase_region_tracker_init, kbase_region_tracker_term,
	  "Region tracker initialization failed" },
	{ kbase_sticky_resource_init, kbase_context_sticky_resource_term,
	  "Sticky resource initialization failed" },
	{ kbase_jit_init, kbase_jit_term, "JIT initialization failed" },
	{ kbase_csf_ctx_init, kbase_csf_ctx_term,
	  "CSF context initialization failed" },
	{ kbase_context_add_to_dev_list, kbase_context_remove_from_dev_list,
	  "Adding kctx to device failed" },
};

static void kbase_context_term_partial(
	struct kbase_context *kctx,
	unsigned int i)
{
	while (i-- > 0) {
		if (context_init[i].term)
			context_init[i].term(kctx);
	}
}

struct kbase_context *kbase_create_context(struct kbase_device *kbdev,
	bool is_compat,
	base_context_create_flags const flags,
	unsigned long const api_version,
	struct file *const filp)
{
	struct kbase_context *kctx;
	unsigned int i = 0;

	if (WARN_ON(!kbdev))
		return NULL;

	/* Validate flags */
	if (WARN_ON(flags != (flags & BASEP_CONTEXT_CREATE_KERNEL_FLAGS)))
		return NULL;

	/* zero-inited as lot of code assume it's zero'ed out on create */
	kctx = vzalloc(sizeof(*kctx));
	if (WARN_ON(!kctx))
		return NULL;

	kctx->kbdev = kbdev;
	kctx->api_version = api_version;
	kctx->filp = filp;
	kctx->create_flags = flags;

	if (is_compat)
		kbase_ctx_flag_set(kctx, KCTX_COMPAT);
#if defined(CONFIG_64BIT)
	else
		kbase_ctx_flag_set(kctx, KCTX_FORCE_SAME_VA);
#endif /* defined(CONFIG_64BIT) */

	for (i = 0; i < ARRAY_SIZE(context_init); i++) {
		int err = 0;

		if (context_init[i].init)
			err = context_init[i].init(kctx);

		if (err) {
			dev_err(kbdev->dev, "%s error = %d\n",
						context_init[i].err_mes, err);

			/* kctx should be freed by kbase_context_free().
			 * Otherwise it will result in memory leak.
			 */
			WARN_ON(i == 0);

			kbase_context_term_partial(kctx, i);
			return NULL;
		}
	}

	return kctx;
}
KBASE_EXPORT_SYMBOL(kbase_create_context);

void kbase_destroy_context(struct kbase_context *kctx)
{
	struct kbase_device *kbdev;

	if (WARN_ON(!kctx))
		return;

	kbdev = kctx->kbdev;
	if (WARN_ON(!kbdev))
		return;

	/* Context termination could happen whilst the system suspend of
	 * the GPU device is ongoing or has completed. It has been seen on
	 * Customer side that a hang could occur if context termination is
	 * not blocked until the resume of GPU device.
	 */
	while (kbase_pm_context_active_handle_suspend(
		kbdev, KBASE_PM_SUSPEND_HANDLER_DONT_INCREASE)) {
		dev_info(kbdev->dev,
			 "Suspend in progress when destroying context");
		wait_event(kbdev->pm.resume_wait,
			   !kbase_pm_is_suspending(kbdev));
	}

	kbase_mem_pool_group_mark_dying(&kctx->mem_pools);

	kbase_context_term_partial(kctx, ARRAY_SIZE(context_init));

	kbase_pm_context_idle(kbdev);
}
KBASE_EXPORT_SYMBOL(kbase_destroy_context);
