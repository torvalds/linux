// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2010-2022 ARM Limited. All rights reserved.
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

#include <linux/dma-buf.h>
#if IS_ENABLED(CONFIG_COMPAT)
#include <linux/compat.h>
#endif
#include <mali_kbase.h>
#include <linux/random.h>
#include <linux/version.h>
#include <linux/ratelimit.h>
#include <linux/priority_control_manager.h>

#include <mali_kbase_jm.h>
#include <mali_kbase_kinstr_jm.h>
#include <mali_kbase_hwaccess_jm.h>
#include <tl/mali_kbase_tracepoints.h>
#include <mali_linux_trace.h>

#include "mali_kbase_dma_fence.h"
#include <mali_kbase_cs_experimental.h>

#include <mali_kbase_caps.h>

/* Return whether katom will run on the GPU or not. Currently only soft jobs and
 * dependency-only atoms do not run on the GPU
 */
#define IS_GPU_ATOM(katom) (!((katom->core_req & BASE_JD_REQ_SOFT_JOB) ||  \
			((katom->core_req & BASE_JD_REQ_ATOM_TYPE) ==    \
							BASE_JD_REQ_DEP)))

/*
 * This is the kernel side of the API. Only entry points are:
 * - kbase_jd_submit(): Called from userspace to submit a single bag
 * - kbase_jd_done(): Called from interrupt context to track the
 *   completion of a job.
 * Callouts:
 * - to the job manager (enqueue a job)
 * - to the event subsystem (signals the completion/failure of bag/job-chains).
 */

static void __user *
get_compat_pointer(struct kbase_context *kctx, const u64 p)
{
#if IS_ENABLED(CONFIG_COMPAT)
	if (kbase_ctx_flag(kctx, KCTX_COMPAT))
		return compat_ptr(p);
#endif
	return u64_to_user_ptr(p);
}

/* Mark an atom as complete, and trace it in kinstr_jm */
static void jd_mark_atom_complete(struct kbase_jd_atom *katom)
{
	katom->status = KBASE_JD_ATOM_STATE_COMPLETED;
	kbase_kinstr_jm_atom_complete(katom);
	dev_dbg(katom->kctx->kbdev->dev, "Atom %pK status to completed\n",
		(void *)katom);
	KBASE_TLSTREAM_TL_JD_ATOM_COMPLETE(katom->kctx->kbdev, katom);
}

/* Runs an atom, either by handing to the JS or by immediately running it in the case of soft-jobs
 *
 * Returns whether the JS needs a reschedule.
 *
 * Note that the caller must also check the atom status and
 * if it is KBASE_JD_ATOM_STATE_COMPLETED must call jd_done_nolock
 */
static bool jd_run_atom(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;

	dev_dbg(kctx->kbdev->dev, "JD run atom %pK in kctx %pK\n",
		(void *)katom, (void *)kctx);

	KBASE_DEBUG_ASSERT(katom->status != KBASE_JD_ATOM_STATE_UNUSED);

	if ((katom->core_req & BASE_JD_REQ_ATOM_TYPE) == BASE_JD_REQ_DEP) {
		/* Dependency only atom */
		trace_sysgraph(SGR_SUBMIT, kctx->id,
				kbase_jd_atom_id(katom->kctx, katom));
		jd_mark_atom_complete(katom);
		return false;
	} else if (katom->core_req & BASE_JD_REQ_SOFT_JOB) {
		/* Soft-job */
		if (katom->will_fail_event_code) {
			kbase_finish_soft_job(katom);
			jd_mark_atom_complete(katom);
			return false;
		}
		if (kbase_process_soft_job(katom) == 0) {
			kbase_finish_soft_job(katom);
			jd_mark_atom_complete(katom);
		}
		return false;
	}

	katom->status = KBASE_JD_ATOM_STATE_IN_JS;
	dev_dbg(kctx->kbdev->dev, "Atom %pK status to in JS\n", (void *)katom);
	/* Queue an action about whether we should try scheduling a context */
	return kbasep_js_add_job(kctx, katom);
}

void kbase_jd_dep_clear_locked(struct kbase_jd_atom *katom)
{
	struct kbase_device *kbdev;

	KBASE_DEBUG_ASSERT(katom);
	kbdev = katom->kctx->kbdev;
	KBASE_DEBUG_ASSERT(kbdev);

	/* Check whether the atom's other dependencies were already met. If
	 * katom is a GPU atom then the job scheduler may be able to represent
	 * the dependencies, hence we may attempt to submit it before they are
	 * met. Other atoms must have had both dependencies resolved.
	 */
	if (IS_GPU_ATOM(katom) ||
			(!kbase_jd_katom_dep_atom(&katom->dep[0]) &&
			!kbase_jd_katom_dep_atom(&katom->dep[1]))) {
		/* katom dep complete, attempt to run it */
		bool resched = false;

		KBASE_TLSTREAM_TL_RUN_ATOM_START(
			katom->kctx->kbdev, katom,
			kbase_jd_atom_id(katom->kctx, katom));
		resched = jd_run_atom(katom);
		KBASE_TLSTREAM_TL_RUN_ATOM_END(katom->kctx->kbdev, katom,
						  kbase_jd_atom_id(katom->kctx,
								   katom));

		if (katom->status == KBASE_JD_ATOM_STATE_COMPLETED) {
			/* The atom has already finished */
			resched |= jd_done_nolock(katom, true);
		}

		if (resched)
			kbase_js_sched_all(kbdev);
	}
}

void kbase_jd_free_external_resources(struct kbase_jd_atom *katom)
{
#ifdef CONFIG_MALI_BIFROST_DMA_FENCE
	/* Flush dma-fence workqueue to ensure that any callbacks that may have
	 * been queued are done before continuing.
	 * Any successfully completed atom would have had all it's callbacks
	 * completed before the atom was run, so only flush for failed atoms.
	 */
	if (katom->event_code != BASE_JD_EVENT_DONE)
		flush_workqueue(katom->kctx->dma_fence.wq);
#endif /* CONFIG_MALI_BIFROST_DMA_FENCE */
}

static void kbase_jd_post_external_resources(struct kbase_jd_atom *katom)
{
	KBASE_DEBUG_ASSERT(katom);
	KBASE_DEBUG_ASSERT(katom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES);

#ifdef CONFIG_MALI_BIFROST_DMA_FENCE
	kbase_dma_fence_signal(katom);
#endif /* CONFIG_MALI_BIFROST_DMA_FENCE */

	kbase_gpu_vm_lock(katom->kctx);
	/* only roll back if extres is non-NULL */
	if (katom->extres) {
		u32 res_no;

		res_no = katom->nr_extres;
		while (res_no-- > 0) {
			struct kbase_mem_phy_alloc *alloc = katom->extres[res_no].alloc;
			struct kbase_va_region *reg;

			reg = kbase_region_tracker_find_region_base_address(
					katom->kctx,
					katom->extres[res_no].gpu_address);
			kbase_unmap_external_resource(katom->kctx, reg, alloc);
		}
		kfree(katom->extres);
		katom->extres = NULL;
	}
	kbase_gpu_vm_unlock(katom->kctx);
}

/*
 * Set up external resources needed by this job.
 *
 * jctx.lock must be held when this is called.
 */

static int kbase_jd_pre_external_resources(struct kbase_jd_atom *katom, const struct base_jd_atom *user_atom)
{
	int err_ret_val = -EINVAL;
	u32 res_no;
#ifdef CONFIG_MALI_BIFROST_DMA_FENCE
	struct kbase_dma_fence_resv_info info = {
		.resv_objs = NULL,
		.dma_fence_resv_count = 0,
		.dma_fence_excl_bitmap = NULL
	};
#if defined(CONFIG_SYNC) || defined(CONFIG_SYNC_FILE)
	/*
	 * When both dma-buf fence and Android native sync is enabled, we
	 * disable dma-buf fence for contexts that are using Android native
	 * fences.
	 */
	const bool implicit_sync = !kbase_ctx_flag(katom->kctx,
						   KCTX_NO_IMPLICIT_SYNC);
#else /* CONFIG_SYNC || CONFIG_SYNC_FILE*/
	const bool implicit_sync = true;
#endif /* CONFIG_SYNC || CONFIG_SYNC_FILE */
#endif /* CONFIG_MALI_BIFROST_DMA_FENCE */
	struct base_external_resource *input_extres;

	KBASE_DEBUG_ASSERT(katom);
	KBASE_DEBUG_ASSERT(katom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES);

	/* no resources encoded, early out */
	if (!katom->nr_extres)
		return -EINVAL;

	katom->extres = kmalloc_array(katom->nr_extres, sizeof(*katom->extres), GFP_KERNEL);
	if (!katom->extres)
		return -ENOMEM;

	/* copy user buffer to the end of our real buffer.
	 * Make sure the struct sizes haven't changed in a way
	 * we don't support
	 */
	BUILD_BUG_ON(sizeof(*input_extres) > sizeof(*katom->extres));
	input_extres = (struct base_external_resource *)
			(((unsigned char *)katom->extres) +
			(sizeof(*katom->extres) - sizeof(*input_extres)) *
			katom->nr_extres);

	if (copy_from_user(input_extres,
			get_compat_pointer(katom->kctx, user_atom->extres_list),
			sizeof(*input_extres) * katom->nr_extres) != 0) {
		err_ret_val = -EINVAL;
		goto early_err_out;
	}

#ifdef CONFIG_MALI_BIFROST_DMA_FENCE
	if (implicit_sync) {
		info.resv_objs =
			kmalloc_array(katom->nr_extres,
#if (KERNEL_VERSION(5, 4, 0) > LINUX_VERSION_CODE)
				      sizeof(struct reservation_object *),
#else
				      sizeof(struct dma_resv *),
#endif
				      GFP_KERNEL);
		if (!info.resv_objs) {
			err_ret_val = -ENOMEM;
			goto early_err_out;
		}

		info.dma_fence_excl_bitmap =
				kcalloc(BITS_TO_LONGS(katom->nr_extres),
					sizeof(unsigned long), GFP_KERNEL);
		if (!info.dma_fence_excl_bitmap) {
			err_ret_val = -ENOMEM;
			goto early_err_out;
		}
	}
#endif /* CONFIG_MALI_BIFROST_DMA_FENCE */

	/* Take the processes mmap lock */
	down_read(kbase_mem_get_process_mmap_lock());

	/* need to keep the GPU VM locked while we set up UMM buffers */
	kbase_gpu_vm_lock(katom->kctx);
	for (res_no = 0; res_no < katom->nr_extres; res_no++) {
		struct base_external_resource *res = &input_extres[res_no];
		struct kbase_va_region *reg;
		struct kbase_mem_phy_alloc *alloc;
#ifdef CONFIG_MALI_BIFROST_DMA_FENCE
		bool exclusive;

		exclusive = (res->ext_resource & BASE_EXT_RES_ACCESS_EXCLUSIVE)
				? true : false;
#endif
		reg = kbase_region_tracker_find_region_enclosing_address(
				katom->kctx,
				res->ext_resource & ~BASE_EXT_RES_ACCESS_EXCLUSIVE);
		/* did we find a matching region object? */
		if (kbase_is_region_invalid_or_free(reg)) {
			/* roll back */
			goto failed_loop;
		}

		if (!(katom->core_req & BASE_JD_REQ_SOFT_JOB) &&
				(reg->flags & KBASE_REG_PROTECTED)) {
			katom->atom_flags |= KBASE_KATOM_FLAG_PROTECTED;
		}

		alloc = kbase_map_external_resource(katom->kctx, reg,
				current->mm);
		if (!alloc) {
			err_ret_val = -EINVAL;
			goto failed_loop;
		}

#ifdef CONFIG_MALI_BIFROST_DMA_FENCE
		if (implicit_sync &&
		    reg->gpu_alloc->type == KBASE_MEM_TYPE_IMPORTED_UMM) {
#if (KERNEL_VERSION(5, 4, 0) > LINUX_VERSION_CODE)
			struct reservation_object *resv;
#else
			struct dma_resv *resv;
#endif
			resv = reg->gpu_alloc->imported.umm.dma_buf->resv;
			if (resv)
				kbase_dma_fence_add_reservation(resv, &info,
								exclusive);
		}
#endif /* CONFIG_MALI_BIFROST_DMA_FENCE */

		/* finish with updating out array with the data we found */
		/* NOTE: It is important that this is the last thing we do (or
		 * at least not before the first write) as we overwrite elements
		 * as we loop and could be overwriting ourself, so no writes
		 * until the last read for an element.
		 */
		katom->extres[res_no].gpu_address = reg->start_pfn << PAGE_SHIFT; /* save the start_pfn (as an address, not pfn) to use fast lookup later */
		katom->extres[res_no].alloc = alloc;
	}
	/* successfully parsed the extres array */
	/* drop the vm lock now */
	kbase_gpu_vm_unlock(katom->kctx);

	/* Release the processes mmap lock */
	up_read(kbase_mem_get_process_mmap_lock());

#ifdef CONFIG_MALI_BIFROST_DMA_FENCE
	if (implicit_sync) {
		if (info.dma_fence_resv_count) {
			int ret;

			ret = kbase_dma_fence_wait(katom, &info);
			if (ret < 0)
				goto failed_dma_fence_setup;
		}

		kfree(info.resv_objs);
		kfree(info.dma_fence_excl_bitmap);
	}
#endif /* CONFIG_MALI_BIFROST_DMA_FENCE */

	/* all done OK */
	return 0;

/* error handling section */

#ifdef CONFIG_MALI_BIFROST_DMA_FENCE
failed_dma_fence_setup:
	/* Lock the processes mmap lock */
	down_read(kbase_mem_get_process_mmap_lock());

	/* lock before we unmap */
	kbase_gpu_vm_lock(katom->kctx);
#endif

 failed_loop:
	/* undo the loop work */
	while (res_no-- > 0) {
		struct kbase_mem_phy_alloc *alloc = katom->extres[res_no].alloc;

		kbase_unmap_external_resource(katom->kctx, NULL, alloc);
	}
	kbase_gpu_vm_unlock(katom->kctx);

	/* Release the processes mmap lock */
	up_read(kbase_mem_get_process_mmap_lock());

 early_err_out:
	kfree(katom->extres);
	katom->extres = NULL;
#ifdef CONFIG_MALI_BIFROST_DMA_FENCE
	if (implicit_sync) {
		kfree(info.resv_objs);
		kfree(info.dma_fence_excl_bitmap);
	}
#endif
	return err_ret_val;
}

static inline void jd_resolve_dep(struct list_head *out_list,
					struct kbase_jd_atom *katom,
					u8 d, bool ctx_is_dying)
{
	u8 other_d = !d;

	while (!list_empty(&katom->dep_head[d])) {
		struct kbase_jd_atom *dep_atom;
		struct kbase_jd_atom *other_dep_atom;
		u8 dep_type;

		dep_atom = list_entry(katom->dep_head[d].next,
				struct kbase_jd_atom, dep_item[d]);
		list_del(katom->dep_head[d].next);

		dep_type = kbase_jd_katom_dep_type(&dep_atom->dep[d]);
		kbase_jd_katom_dep_clear(&dep_atom->dep[d]);

		if (katom->event_code != BASE_JD_EVENT_DONE &&
			(dep_type != BASE_JD_DEP_TYPE_ORDER)) {
#ifdef CONFIG_MALI_BIFROST_DMA_FENCE
			kbase_dma_fence_cancel_callbacks(dep_atom);
#endif

			dep_atom->event_code = katom->event_code;
			KBASE_DEBUG_ASSERT(dep_atom->status !=
						KBASE_JD_ATOM_STATE_UNUSED);

			dep_atom->will_fail_event_code = dep_atom->event_code;
		}
		other_dep_atom = (struct kbase_jd_atom *)
			kbase_jd_katom_dep_atom(&dep_atom->dep[other_d]);

		if (!dep_atom->in_jd_list && (!other_dep_atom ||
				(IS_GPU_ATOM(dep_atom) && !ctx_is_dying &&
				!dep_atom->will_fail_event_code &&
				!other_dep_atom->will_fail_event_code))) {
			bool dep_satisfied = true;
#ifdef CONFIG_MALI_BIFROST_DMA_FENCE
			int dep_count;

			dep_count = kbase_fence_dep_count_read(dep_atom);
			if (likely(dep_count == -1)) {
				dep_satisfied = true;
			} else {
				/*
				 * There are either still active callbacks, or
				 * all fences for this @dep_atom has signaled,
				 * but the worker that will queue the atom has
				 * not yet run.
				 *
				 * Wait for the fences to signal and the fence
				 * worker to run and handle @dep_atom. If
				 * @dep_atom was completed due to error on
				 * @katom, then the fence worker will pick up
				 * the complete status and error code set on
				 * @dep_atom above.
				 */
				dep_satisfied = false;
			}
#endif /* CONFIG_MALI_BIFROST_DMA_FENCE */

			if (dep_satisfied) {
				dep_atom->in_jd_list = true;
				list_add_tail(&dep_atom->jd_item, out_list);
			}
		}
	}
}

/**
 * is_dep_valid - Validate that a dependency is valid for early dependency
 *                submission
 * @katom: Dependency atom to validate
 *
 * A dependency is valid if any of the following are true :
 * - It does not exist (a non-existent dependency does not block submission)
 * - It is in the job scheduler
 * - It has completed, does not have a failure event code, and has not been
 *   marked to fail in the future
 *
 * Return: true if valid, false otherwise
 */
static bool is_dep_valid(struct kbase_jd_atom *katom)
{
	/* If there's no dependency then this is 'valid' from the perspective of
	 * early dependency submission
	 */
	if (!katom)
		return true;

	/* Dependency must have reached the job scheduler */
	if (katom->status < KBASE_JD_ATOM_STATE_IN_JS)
		return false;

	/* If dependency has completed and has failed or will fail then it is
	 * not valid
	 */
	if (katom->status >= KBASE_JD_ATOM_STATE_HW_COMPLETED &&
			(katom->event_code != BASE_JD_EVENT_DONE ||
			katom->will_fail_event_code))
		return false;

	return true;
}

static void jd_try_submitting_deps(struct list_head *out_list,
		struct kbase_jd_atom *node)
{
	int i;

	for (i = 0; i < 2; i++) {
		struct list_head *pos;

		list_for_each(pos, &node->dep_head[i]) {
			struct kbase_jd_atom *dep_atom = list_entry(pos,
					struct kbase_jd_atom, dep_item[i]);

			if (IS_GPU_ATOM(dep_atom) && !dep_atom->in_jd_list) {
				/*Check if atom deps look sane*/
				bool dep0_valid = is_dep_valid(
						dep_atom->dep[0].atom);
				bool dep1_valid = is_dep_valid(
						dep_atom->dep[1].atom);
				bool dep_satisfied = true;
#ifdef CONFIG_MALI_BIFROST_DMA_FENCE
				int dep_count;

				dep_count = kbase_fence_dep_count_read(
								dep_atom);
				if (likely(dep_count == -1)) {
					dep_satisfied = true;
				} else {
				/*
				 * There are either still active callbacks, or
				 * all fences for this @dep_atom has signaled,
				 * but the worker that will queue the atom has
				 * not yet run.
				 *
				 * Wait for the fences to signal and the fence
				 * worker to run and handle @dep_atom. If
				 * @dep_atom was completed due to error on
				 * @katom, then the fence worker will pick up
				 * the complete status and error code set on
				 * @dep_atom above.
				 */
					dep_satisfied = false;
				}
#endif /* CONFIG_MALI_BIFROST_DMA_FENCE */

				if (dep0_valid && dep1_valid && dep_satisfied) {
					dep_atom->in_jd_list = true;
					list_add(&dep_atom->jd_item, out_list);
				}
			}
		}
	}
}

#if MALI_JIT_PRESSURE_LIMIT_BASE
/**
 * jd_update_jit_usage - Update just-in-time physical memory usage for an atom.
 *
 * @katom: An atom that has just finished.
 *
 * Read back actual just-in-time memory region usage from atoms that provide
 * this information, and update the current physical page pressure.
 *
 * The caller must hold the kbase_jd_context.lock.
 */
static void jd_update_jit_usage(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;
	struct kbase_va_region *reg;
	struct kbase_vmap_struct mapping;
	u64 *ptr;
	u64 used_pages;
	unsigned int idx;

	lockdep_assert_held(&kctx->jctx.lock);

	/* If this atom wrote to JIT memory, find out how much it has written
	 * and update the usage information in the region.
	 */
	for (idx = 0;
		idx < ARRAY_SIZE(katom->jit_ids) && katom->jit_ids[idx];
		idx++) {
		enum heap_pointer { LOW = 0, HIGH, COUNT };
		size_t size_to_read;
		u64 read_val;

		reg = kctx->jit_alloc[katom->jit_ids[idx]];

		if (!reg) {
			dev_warn(kctx->kbdev->dev,
					"%s: JIT id[%u]=%u has no region\n",
					__func__, idx, katom->jit_ids[idx]);
			continue;
		}

		if (reg == KBASE_RESERVED_REG_JIT_ALLOC) {
			dev_warn(kctx->kbdev->dev,
					"%s: JIT id[%u]=%u has failed to allocate a region\n",
					__func__, idx, katom->jit_ids[idx]);
			continue;
		}

		if (!reg->heap_info_gpu_addr)
			continue;

		size_to_read = sizeof(*ptr);
		if (reg->flags & KBASE_REG_HEAP_INFO_IS_SIZE)
			size_to_read = sizeof(u32);
		else if (reg->flags & KBASE_REG_TILER_ALIGN_TOP)
			size_to_read = sizeof(u64[COUNT]);

		ptr = kbase_vmap_prot(kctx, reg->heap_info_gpu_addr, size_to_read,
				KBASE_REG_CPU_RD, &mapping);

		if (!ptr) {
			dev_warn(kctx->kbdev->dev,
					"%s: JIT id[%u]=%u start=0x%llx unable to map end marker %llx\n",
					__func__, idx, katom->jit_ids[idx],
					reg->start_pfn << PAGE_SHIFT,
					reg->heap_info_gpu_addr);
			continue;
		}

		if (reg->flags & KBASE_REG_HEAP_INFO_IS_SIZE) {
			read_val = READ_ONCE(*(u32 *)ptr);
			used_pages = PFN_UP(read_val);
		} else {
			u64 addr_end;

			if (reg->flags & KBASE_REG_TILER_ALIGN_TOP) {
				const unsigned long extension_bytes =
					reg->extension << PAGE_SHIFT;
				const u64 low_ptr = ptr[LOW];
				const u64 high_ptr = ptr[HIGH];

				/* As either the low or high pointer could
				 * consume their partition and move onto the
				 * next chunk, we need to account for both.
				 * In the case where nothing has been allocated
				 * from the high pointer the whole chunk could
				 * be backed unnecessarily - but the granularity
				 * is the chunk size anyway and any non-zero
				 * offset of low pointer from the start of the
				 * chunk would result in the whole chunk being
				 * backed.
				 */
				read_val = max(high_ptr, low_ptr);

				/* kbase_check_alloc_sizes() already satisfies
				 * this, but here to avoid future maintenance
				 * hazards
				 */
				WARN_ON(!is_power_of_2(extension_bytes));
				addr_end = ALIGN(read_val, extension_bytes);
			} else {
				addr_end = read_val = READ_ONCE(*ptr);
			}

			if (addr_end >= (reg->start_pfn << PAGE_SHIFT))
				used_pages = PFN_UP(addr_end) - reg->start_pfn;
			else
				used_pages = reg->used_pages;
		}

		trace_mali_jit_report(katom, reg, idx, read_val, used_pages);
		kbase_trace_jit_report_gpu_mem(kctx, reg, 0u);

		/* We can never have used more pages than the VA size of the
		 * region
		 */
		if (used_pages > reg->nr_pages) {
			dev_warn(kctx->kbdev->dev,
				"%s: JIT id[%u]=%u start=0x%llx used_pages %llx > %zx (read 0x%llx as %s%s)\n",
				__func__, idx, katom->jit_ids[idx],
				reg->start_pfn << PAGE_SHIFT,
				used_pages, reg->nr_pages, read_val,
				(reg->flags & KBASE_REG_HEAP_INFO_IS_SIZE) ?
					"size" : "addr",
				(reg->flags & KBASE_REG_TILER_ALIGN_TOP) ?
					" with align" : "");
			used_pages = reg->nr_pages;
		}
		/* Note: one real use case has an atom correctly reporting 0
		 * pages in use. This happens in normal use-cases but may only
		 * happen for a few of the application's frames.
		 */

		kbase_vunmap(kctx, &mapping);

		kbase_jit_report_update_pressure(kctx, reg, used_pages, 0u);
	}

	kbase_jit_retry_pending_alloc(kctx);
}
#endif /* MALI_JIT_PRESSURE_LIMIT_BASE */

bool jd_done_nolock(struct kbase_jd_atom *katom, bool post_immediately)
{
	struct kbase_context *kctx = katom->kctx;
	struct list_head completed_jobs;
	struct list_head runnable_jobs;
	bool need_to_try_schedule_context = false;
	int i;

	KBASE_TLSTREAM_TL_JD_DONE_NO_LOCK_START(kctx->kbdev, katom);

	INIT_LIST_HEAD(&completed_jobs);
	INIT_LIST_HEAD(&runnable_jobs);

	KBASE_DEBUG_ASSERT(katom->status != KBASE_JD_ATOM_STATE_UNUSED);

#if MALI_JIT_PRESSURE_LIMIT_BASE
	if (kbase_ctx_flag(kctx, KCTX_JPL_ENABLED))
		jd_update_jit_usage(katom);
#endif /* MALI_JIT_PRESSURE_LIMIT_BASE */

	/* This is needed in case an atom is failed due to being invalid, this
	 * can happen *before* the jobs that the atom depends on have completed
	 */
	for (i = 0; i < 2; i++) {
		if (kbase_jd_katom_dep_atom(&katom->dep[i])) {
			list_del(&katom->dep_item[i]);
			kbase_jd_katom_dep_clear(&katom->dep[i]);
		}
	}

	jd_mark_atom_complete(katom);

	list_add_tail(&katom->jd_item, &completed_jobs);

	while (!list_empty(&completed_jobs)) {
		katom = list_entry(completed_jobs.prev, struct kbase_jd_atom, jd_item);
		list_del(completed_jobs.prev);
		KBASE_DEBUG_ASSERT(katom->status == KBASE_JD_ATOM_STATE_COMPLETED);

		for (i = 0; i < 2; i++)
			jd_resolve_dep(&runnable_jobs, katom, i,
					kbase_ctx_flag(kctx, KCTX_DYING));

		if (katom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES)
			kbase_jd_post_external_resources(katom);

		while (!list_empty(&runnable_jobs)) {
			struct kbase_jd_atom *node;

			node = list_entry(runnable_jobs.next,
					struct kbase_jd_atom, jd_item);
			list_del(runnable_jobs.next);
			node->in_jd_list = false;

			dev_dbg(kctx->kbdev->dev, "List node %pK has status %d\n",
				node, node->status);

			KBASE_DEBUG_ASSERT(node->status != KBASE_JD_ATOM_STATE_UNUSED);
			if (node->status == KBASE_JD_ATOM_STATE_IN_JS)
				continue;

			if (node->status != KBASE_JD_ATOM_STATE_COMPLETED &&
					!kbase_ctx_flag(kctx, KCTX_DYING)) {
				KBASE_TLSTREAM_TL_RUN_ATOM_START(
					kctx->kbdev, node,
					kbase_jd_atom_id(kctx, node));
				need_to_try_schedule_context |= jd_run_atom(node);
				KBASE_TLSTREAM_TL_RUN_ATOM_END(
					kctx->kbdev, node,
					kbase_jd_atom_id(kctx, node));
			} else {
				node->event_code = katom->event_code;

				if (node->core_req &
							BASE_JD_REQ_SOFT_JOB) {
					WARN_ON(!list_empty(&node->queue));
					kbase_finish_soft_job(node);
				}
				node->status = KBASE_JD_ATOM_STATE_COMPLETED;
			}

			if (node->status == KBASE_JD_ATOM_STATE_COMPLETED) {
				list_add_tail(&node->jd_item, &completed_jobs);
			} else if (node->status == KBASE_JD_ATOM_STATE_IN_JS &&
					!node->will_fail_event_code) {
				/* Node successfully submitted, try submitting
				 * dependencies as they may now be representable
				 * in JS
				 */
				jd_try_submitting_deps(&runnable_jobs, node);
			}
		}

		/* Register a completed job as a disjoint event when the GPU
		 * is in a disjoint state (ie. being reset).
		 */
		kbase_disjoint_event_potential(kctx->kbdev);
		if (post_immediately && list_empty(&kctx->completed_jobs))
			kbase_event_post(kctx, katom);
		else
			list_add_tail(&katom->jd_item, &kctx->completed_jobs);

		/* Decrement and check the TOTAL number of jobs. This includes
		 * those not tracked by the scheduler: 'not ready to run' and
		 * 'dependency-only' jobs.
		 */
		if (--kctx->jctx.job_nr == 0)
			/* All events are safely queued now, and we can signal
			 * any waiter that we've got no more jobs (so we can be
			 * safely terminated)
			 */
			wake_up(&kctx->jctx.zero_jobs_wait);
	}
	KBASE_TLSTREAM_TL_JD_DONE_NO_LOCK_END(kctx->kbdev, katom);
	return need_to_try_schedule_context;
}

KBASE_EXPORT_TEST_API(jd_done_nolock);

#if IS_ENABLED(CONFIG_GPU_TRACEPOINTS)
enum {
	CORE_REQ_DEP_ONLY,
	CORE_REQ_SOFT,
	CORE_REQ_COMPUTE,
	CORE_REQ_FRAGMENT,
	CORE_REQ_VERTEX,
	CORE_REQ_TILER,
	CORE_REQ_FRAGMENT_VERTEX,
	CORE_REQ_FRAGMENT_VERTEX_TILER,
	CORE_REQ_FRAGMENT_TILER,
	CORE_REQ_VERTEX_TILER,
	CORE_REQ_UNKNOWN
};
static const char * const core_req_strings[] = {
	"Dependency Only Job",
	"Soft Job",
	"Compute Shader Job",
	"Fragment Shader Job",
	"Vertex/Geometry Shader Job",
	"Tiler Job",
	"Fragment Shader + Vertex/Geometry Shader Job",
	"Fragment Shader + Vertex/Geometry Shader Job + Tiler Job",
	"Fragment Shader + Tiler Job",
	"Vertex/Geometry Shader Job + Tiler Job",
	"Unknown Job"
};
static const char *kbasep_map_core_reqs_to_string(base_jd_core_req core_req)
{
	if (core_req & BASE_JD_REQ_SOFT_JOB)
		return core_req_strings[CORE_REQ_SOFT];
	if (core_req & BASE_JD_REQ_ONLY_COMPUTE)
		return core_req_strings[CORE_REQ_COMPUTE];
	switch (core_req & (BASE_JD_REQ_FS | BASE_JD_REQ_CS | BASE_JD_REQ_T)) {
	case BASE_JD_REQ_DEP:
		return core_req_strings[CORE_REQ_DEP_ONLY];
	case BASE_JD_REQ_FS:
		return core_req_strings[CORE_REQ_FRAGMENT];
	case BASE_JD_REQ_CS:
		return core_req_strings[CORE_REQ_VERTEX];
	case BASE_JD_REQ_T:
		return core_req_strings[CORE_REQ_TILER];
	case (BASE_JD_REQ_FS | BASE_JD_REQ_CS):
		return core_req_strings[CORE_REQ_FRAGMENT_VERTEX];
	case (BASE_JD_REQ_FS | BASE_JD_REQ_T):
		return core_req_strings[CORE_REQ_FRAGMENT_TILER];
	case (BASE_JD_REQ_CS | BASE_JD_REQ_T):
		return core_req_strings[CORE_REQ_VERTEX_TILER];
	case (BASE_JD_REQ_FS | BASE_JD_REQ_CS | BASE_JD_REQ_T):
		return core_req_strings[CORE_REQ_FRAGMENT_VERTEX_TILER];
	}
	return core_req_strings[CORE_REQ_UNKNOWN];
}
#endif

/* Trace an atom submission. */
static void jd_trace_atom_submit(struct kbase_context *const kctx,
				 struct kbase_jd_atom *const katom,
				 int *priority)
{
	struct kbase_device *const kbdev = kctx->kbdev;

	KBASE_TLSTREAM_TL_NEW_ATOM(kbdev, katom, kbase_jd_atom_id(kctx, katom));
	KBASE_TLSTREAM_TL_RET_ATOM_CTX(kbdev, katom, kctx);
	if (priority)
		KBASE_TLSTREAM_TL_ATTRIB_ATOM_PRIORITY(kbdev, katom, *priority);
	KBASE_TLSTREAM_TL_ATTRIB_ATOM_STATE(kbdev, katom, TL_ATOM_STATE_IDLE);
	kbase_kinstr_jm_atom_queue(katom);
}

static bool jd_submit_atom(struct kbase_context *const kctx,
	const struct base_jd_atom *const user_atom,
	const struct base_jd_fragment *const user_jc_incr,
	struct kbase_jd_atom *const katom)
{
	struct kbase_device *kbdev = kctx->kbdev;
	struct kbase_jd_context *jctx = &kctx->jctx;
	int queued = 0;
	int i;
	int sched_prio;
	bool will_fail = false;
	unsigned long flags;
	enum kbase_jd_atom_state status;

	dev_dbg(kbdev->dev, "User did JD submit atom %pK\n", (void *)katom);

	/* Update the TOTAL number of jobs. This includes those not tracked by
	 * the scheduler: 'not ready to run' and 'dependency-only' jobs.
	 */
	jctx->job_nr++;

#if KERNEL_VERSION(4, 10, 0) > LINUX_VERSION_CODE
	katom->start_timestamp.tv64 = 0;
#else
	katom->start_timestamp = 0;
#endif
	katom->udata = user_atom->udata;
	katom->kctx = kctx;
	katom->nr_extres = user_atom->nr_extres;
	katom->extres = NULL;
	katom->device_nr = user_atom->device_nr;
	katom->jc = user_atom->jc;
	katom->core_req = user_atom->core_req;
	katom->jobslot = user_atom->jobslot;
	katom->seq_nr = user_atom->seq_nr;
	katom->atom_flags = 0;
	katom->retry_count = 0;
	katom->need_cache_flush_cores_retained = 0;
	katom->pre_dep = NULL;
	katom->post_dep = NULL;
	katom->x_pre_dep = NULL;
	katom->x_post_dep = NULL;
	katom->will_fail_event_code = BASE_JD_EVENT_NOT_STARTED;
	katom->softjob_data = NULL;

	trace_sysgraph(SGR_ARRIVE, kctx->id, user_atom->atom_number);

#if MALI_JIT_PRESSURE_LIMIT_BASE
	/* Older API version atoms might have random values where jit_id now
	 * lives, but we must maintain backwards compatibility - handle the
	 * issue.
	 */
	if (!mali_kbase_supports_jit_pressure_limit(kctx->api_version)) {
		katom->jit_ids[0] = 0;
		katom->jit_ids[1] = 0;
	} else {
		katom->jit_ids[0] = user_atom->jit_id[0];
		katom->jit_ids[1] = user_atom->jit_id[1];
	}
#endif /* MALI_JIT_PRESSURE_LIMIT_BASE */

	katom->renderpass_id = user_atom->renderpass_id;

	/* Implicitly sets katom->protected_state.enter as well. */
	katom->protected_state.exit = KBASE_ATOM_EXIT_PROTECTED_CHECK;

	katom->age = kctx->age_count++;

	INIT_LIST_HEAD(&katom->queue);
	INIT_LIST_HEAD(&katom->jd_item);
#ifdef CONFIG_MALI_BIFROST_DMA_FENCE
	kbase_fence_dep_count_set(katom, -1);
#endif

	/* Don't do anything if there is a mess up with dependencies.
	 * This is done in a separate cycle to check both the dependencies at ones, otherwise
	 * it will be extra complexity to deal with 1st dependency ( just added to the list )
	 * if only the 2nd one has invalid config.
	 */
	for (i = 0; i < 2; i++) {
		int dep_atom_number = user_atom->pre_dep[i].atom_id;
		base_jd_dep_type dep_atom_type = user_atom->pre_dep[i].dependency_type;

		if (dep_atom_number) {
			if (dep_atom_type != BASE_JD_DEP_TYPE_ORDER &&
					dep_atom_type != BASE_JD_DEP_TYPE_DATA) {
				katom->event_code = BASE_JD_EVENT_JOB_CONFIG_FAULT;
				katom->status = KBASE_JD_ATOM_STATE_COMPLETED;
				dev_dbg(kbdev->dev,
					"Atom %pK status to completed\n",
					(void *)katom);

				/* Wrong dependency setup. Atom will be sent
				 * back to user space. Do not record any
				 * dependencies.
				 */
				jd_trace_atom_submit(kctx, katom, NULL);
				return jd_done_nolock(katom, true);
			}
		}
	}

	/* Add dependencies */
	for (i = 0; i < 2; i++) {
		int dep_atom_number = user_atom->pre_dep[i].atom_id;
		base_jd_dep_type dep_atom_type;
		struct kbase_jd_atom *dep_atom = &jctx->atoms[dep_atom_number];

		dep_atom_type = user_atom->pre_dep[i].dependency_type;
		kbase_jd_katom_dep_clear(&katom->dep[i]);

		if (!dep_atom_number)
			continue;

		if (dep_atom->status == KBASE_JD_ATOM_STATE_UNUSED ||
				dep_atom->status == KBASE_JD_ATOM_STATE_COMPLETED) {

			if (dep_atom->event_code == BASE_JD_EVENT_DONE)
				continue;
			/* don't stop this atom if it has an order dependency
			 * only to the failed one, try to submit it through
			 * the normal path
			 */
			if (dep_atom_type == BASE_JD_DEP_TYPE_ORDER &&
					dep_atom->event_code > BASE_JD_EVENT_ACTIVE) {
				continue;
			}

			/* Atom has completed, propagate the error code if any */
			katom->event_code = dep_atom->event_code;
			katom->status = KBASE_JD_ATOM_STATE_QUEUED;
			dev_dbg(kbdev->dev, "Atom %pK status to queued\n",
				(void *)katom);

			/* This atom will be sent back to user space.
			 * Do not record any dependencies.
			 */
			jd_trace_atom_submit(kctx, katom, NULL);

			will_fail = true;

		} else {
			/* Atom is in progress, add this atom to the list */
			list_add_tail(&katom->dep_item[i], &dep_atom->dep_head[i]);
			kbase_jd_katom_dep_set(&katom->dep[i], dep_atom, dep_atom_type);
			queued = 1;
		}
	}

	if (will_fail) {
		if (!queued) {
			if (katom->core_req & BASE_JD_REQ_SOFT_JOB) {
				/* This softjob has failed due to a previous
				 * dependency, however we should still run the
				 * prepare & finish functions
				 */
				int err = kbase_prepare_soft_job(katom);

				if (err >= 0)
					kbase_finish_soft_job(katom);
			}
			return jd_done_nolock(katom, true);
		}

		katom->will_fail_event_code = katom->event_code;
	}

	/* These must occur after the above loop to ensure that an atom
	 * that depends on a previous atom with the same number behaves
	 * as expected
	 */
	katom->event_code = BASE_JD_EVENT_DONE;
	katom->status = KBASE_JD_ATOM_STATE_QUEUED;
	dev_dbg(kbdev->dev, "Atom %pK status to queued\n", (void *)katom);

	/* For invalid priority, be most lenient and choose the default */
	sched_prio = kbasep_js_atom_prio_to_sched_prio(user_atom->prio);
	if (sched_prio == KBASE_JS_ATOM_SCHED_PRIO_INVALID)
		sched_prio = KBASE_JS_ATOM_SCHED_PRIO_DEFAULT;

	/* Cap the priority to jctx.max_priority */
	katom->sched_priority = (sched_prio < kctx->jctx.max_priority) ?
			kctx->jctx.max_priority : sched_prio;

	/* Create a new atom. */
	jd_trace_atom_submit(kctx, katom, &katom->sched_priority);

#if !MALI_INCREMENTAL_RENDERING_JM
	/* Reject atoms for incremental rendering if not supported */
	if (katom->core_req &
	(BASE_JD_REQ_START_RENDERPASS|BASE_JD_REQ_END_RENDERPASS)) {
		dev_err(kctx->kbdev->dev,
			"Rejecting atom with unsupported core_req 0x%x\n",
			katom->core_req);
		katom->event_code = BASE_JD_EVENT_JOB_INVALID;
		return jd_done_nolock(katom, true);
	}
#endif /* !MALI_INCREMENTAL_RENDERING_JM */

	if (katom->core_req & BASE_JD_REQ_END_RENDERPASS) {
		WARN_ON(katom->jc != 0);
		katom->jc_fragment = *user_jc_incr;
	} else if (!katom->jc &&
		(katom->core_req & BASE_JD_REQ_ATOM_TYPE) != BASE_JD_REQ_DEP) {
		/* Reject atoms with job chain = NULL, as these cause issues
		 * with soft-stop
		 */
		dev_err(kctx->kbdev->dev, "Rejecting atom with jc = NULL\n");
		katom->event_code = BASE_JD_EVENT_JOB_INVALID;
		return jd_done_nolock(katom, true);
	}

	/* Reject atoms with an invalid device_nr */
	if ((katom->core_req & BASE_JD_REQ_SPECIFIC_COHERENT_GROUP) &&
	    (katom->device_nr >= kctx->kbdev->gpu_props.num_core_groups)) {
		dev_err(kctx->kbdev->dev,
				"Rejecting atom with invalid device_nr %d\n",
				katom->device_nr);
		katom->event_code = BASE_JD_EVENT_JOB_INVALID;
		return jd_done_nolock(katom, true);
	}

	/* Reject atoms with invalid core requirements */
	if ((katom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES) &&
			(katom->core_req & BASE_JD_REQ_EVENT_COALESCE)) {
		dev_err(kctx->kbdev->dev,
				"Rejecting atom with invalid core requirements\n");
		katom->event_code = BASE_JD_EVENT_JOB_INVALID;
		katom->core_req &= ~BASE_JD_REQ_EVENT_COALESCE;
		return jd_done_nolock(katom, true);
	}

	/* Reject soft-job atom of certain types from accessing external resources */
	if ((katom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES) &&
			(((katom->core_req & BASE_JD_REQ_SOFT_JOB_TYPE) == BASE_JD_REQ_SOFT_FENCE_WAIT) ||
			 ((katom->core_req & BASE_JD_REQ_SOFT_JOB_TYPE) == BASE_JD_REQ_SOFT_JIT_ALLOC) ||
			 ((katom->core_req & BASE_JD_REQ_SOFT_JOB_TYPE) == BASE_JD_REQ_SOFT_JIT_FREE))) {
		dev_err(kctx->kbdev->dev,
				"Rejecting soft-job atom accessing external resources\n");
		katom->event_code = BASE_JD_EVENT_JOB_INVALID;
		return jd_done_nolock(katom, true);
	}

	if (katom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES) {
		/* handle what we need to do to access the external resources */
		if (kbase_jd_pre_external_resources(katom, user_atom) != 0) {
			/* setup failed (no access, bad resource, unknown resource types, etc.) */
			katom->event_code = BASE_JD_EVENT_JOB_INVALID;
			return jd_done_nolock(katom, true);
		}
	}

#if !MALI_JIT_PRESSURE_LIMIT_BASE
	if (mali_kbase_supports_jit_pressure_limit(kctx->api_version) &&
		(user_atom->jit_id[0] || user_atom->jit_id[1])) {
		/* JIT pressure limit is disabled, but we are receiving non-0
		 * JIT IDs - atom is invalid.
		 */
		katom->event_code = BASE_JD_EVENT_JOB_INVALID;
		return jd_done_nolock(katom, true);
	}
#endif /* MALI_JIT_PRESSURE_LIMIT_BASE */

	/* Validate the atom. Function will return error if the atom is
	 * malformed.
	 *
	 * Soft-jobs never enter the job scheduler but have their own initialize method.
	 *
	 * If either fail then we immediately complete the atom with an error.
	 */
	if ((katom->core_req & BASE_JD_REQ_SOFT_JOB) == 0) {
		if (!kbase_js_is_atom_valid(kctx->kbdev, katom)) {
			katom->event_code = BASE_JD_EVENT_JOB_INVALID;
			return jd_done_nolock(katom, true);
		}
	} else {
		/* Soft-job */
		if (kbase_prepare_soft_job(katom) != 0) {
			katom->event_code = BASE_JD_EVENT_JOB_INVALID;
			return jd_done_nolock(katom, true);
		}
	}

#if IS_ENABLED(CONFIG_GPU_TRACEPOINTS)
	katom->work_id = atomic_inc_return(&jctx->work_id);
	trace_gpu_job_enqueue(kctx->id, katom->work_id,
			kbasep_map_core_reqs_to_string(katom->core_req));
#endif

	if (queued && !IS_GPU_ATOM(katom))
		return false;

#ifdef CONFIG_MALI_BIFROST_DMA_FENCE
	if (kbase_fence_dep_count_read(katom) != -1)
		return false;

#endif /* CONFIG_MALI_BIFROST_DMA_FENCE */

	if (katom->core_req & BASE_JD_REQ_SOFT_JOB) {
		if (kbase_process_soft_job(katom) == 0) {
			kbase_finish_soft_job(katom);
			return jd_done_nolock(katom, true);
		}
		return false;
	}

	if ((katom->core_req & BASE_JD_REQ_ATOM_TYPE) != BASE_JD_REQ_DEP) {
		bool need_to_try_schedule_context;

		katom->status = KBASE_JD_ATOM_STATE_IN_JS;
		dev_dbg(kctx->kbdev->dev, "Atom %pK status to in JS\n",
			(void *)katom);

		need_to_try_schedule_context = kbasep_js_add_job(kctx, katom);
		/* If job was cancelled then resolve immediately */
		if (katom->event_code != BASE_JD_EVENT_JOB_CANCELLED)
			return need_to_try_schedule_context;

		/* Synchronize with backend reset */
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		status = katom->status;
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		if (status == KBASE_JD_ATOM_STATE_HW_COMPLETED) {
			dev_dbg(kctx->kbdev->dev,
					"Atom %d cancelled on HW\n",
					kbase_jd_atom_id(katom->kctx, katom));
			return need_to_try_schedule_context;
		}
	}

	/* This is a pure dependency. Resolve it immediately */
	return jd_done_nolock(katom, true);
}

int kbase_jd_submit(struct kbase_context *kctx,
		void __user *user_addr, u32 nr_atoms, u32 stride,
		bool uk6_atom)
{
	struct kbase_jd_context *jctx = &kctx->jctx;
	int err = 0;
	int i;
	bool need_to_try_schedule_context = false;
	struct kbase_device *kbdev;
	u32 latest_flush;

	bool jd_atom_is_v2 = (stride == sizeof(struct base_jd_atom_v2) ||
		stride == offsetof(struct base_jd_atom_v2, renderpass_id));

	/*
	 * kbase_jd_submit isn't expected to fail and so all errors with the
	 * jobs are reported by immediately failing them (through event system)
	 */
	kbdev = kctx->kbdev;

	if (kbase_ctx_flag(kctx, KCTX_SUBMIT_DISABLED)) {
		dev_err(kbdev->dev, "Attempt to submit to a context that has SUBMIT_DISABLED set on it\n");
		return -EINVAL;
	}

	if (stride != offsetof(struct base_jd_atom_v2, renderpass_id) &&
		stride != sizeof(struct base_jd_atom_v2) &&
		stride != offsetof(struct base_jd_atom, renderpass_id) &&
		stride != sizeof(struct base_jd_atom)) {
		dev_err(kbdev->dev,
			"Stride %u passed to job_submit isn't supported by the kernel\n",
			stride);
		return -EINVAL;
	}

	/* All atoms submitted in this call have the same flush ID */
	latest_flush = kbase_backend_get_current_flush_id(kbdev);

	for (i = 0; i < nr_atoms; i++) {
		struct base_jd_atom user_atom;
		struct base_jd_fragment user_jc_incr;
		struct kbase_jd_atom *katom;

		if (unlikely(jd_atom_is_v2)) {
			if (copy_from_user(&user_atom.jc, user_addr, sizeof(struct base_jd_atom_v2)) != 0) {
				dev_dbg(kbdev->dev,
					"Invalid atom address %p passed to job_submit\n",
					user_addr);
				err = -EFAULT;
				break;
			}

			/* no seq_nr in v2 */
			user_atom.seq_nr = 0;
		} else {
			if (copy_from_user(&user_atom, user_addr, stride) != 0) {
				dev_dbg(kbdev->dev,
					"Invalid atom address %p passed to job_submit\n",
					user_addr);
				err = -EFAULT;
				break;
			}
		}

		if (stride == offsetof(struct base_jd_atom_v2, renderpass_id)) {
			dev_dbg(kbdev->dev, "No renderpass ID: use 0\n");
			user_atom.renderpass_id = 0;
		} else {
			/* Ensure all padding bytes are 0 for potential future
			 * extension
			 */
			size_t j;

			dev_dbg(kbdev->dev, "Renderpass ID is %d\n",
				user_atom.renderpass_id);
			for (j = 0; j < sizeof(user_atom.padding); j++) {
				if (user_atom.padding[j]) {
					dev_err(kbdev->dev,
						"Bad padding byte %zu: %d\n",
						j, user_atom.padding[j]);
					err = -EINVAL;
					break;
				}
			}
			if (err)
				break;
		}

		/* In this case 'jc' is the CPU address of a struct
		 * instead of a GPU address of a job chain.
		 */
		if (user_atom.core_req & BASE_JD_REQ_END_RENDERPASS) {
			if (copy_from_user(&user_jc_incr,
				u64_to_user_ptr(user_atom.jc),
				sizeof(user_jc_incr))) {
				dev_err(kbdev->dev,
					"Invalid jc address 0x%llx passed to job_submit\n",
					user_atom.jc);
				err = -EFAULT;
				break;
			}
			dev_dbg(kbdev->dev, "Copied IR jobchain addresses\n");
			user_atom.jc = 0;
		}

		user_addr = (void __user *)((uintptr_t) user_addr + stride);

		mutex_lock(&jctx->lock);
#ifndef compiletime_assert
#define compiletime_assert_defined
#define compiletime_assert(x, msg) do { switch (0) { case 0: case (x):; } } \
while (false)
#endif
		compiletime_assert((1 << (8*sizeof(user_atom.atom_number))) ==
					BASE_JD_ATOM_COUNT,
			"BASE_JD_ATOM_COUNT and base_atom_id type out of sync");
		compiletime_assert(sizeof(user_atom.pre_dep[0].atom_id) ==
					sizeof(user_atom.atom_number),
			"BASE_JD_ATOM_COUNT and base_atom_id type out of sync");
#ifdef compiletime_assert_defined
#undef compiletime_assert
#undef compiletime_assert_defined
#endif
		katom = &jctx->atoms[user_atom.atom_number];

		/* Record the flush ID for the cache flush optimisation */
		katom->flush_id = latest_flush;

		while (katom->status != KBASE_JD_ATOM_STATE_UNUSED) {
			/* Atom number is already in use, wait for the atom to
			 * complete
			 */
			mutex_unlock(&jctx->lock);

			/* This thread will wait for the atom to complete. Due
			 * to thread scheduling we are not sure that the other
			 * thread that owns the atom will also schedule the
			 * context, so we force the scheduler to be active and
			 * hence eventually schedule this context at some point
			 * later.
			 */
			kbase_js_sched_all(kbdev);

			if (wait_event_killable(katom->completed,
					katom->status ==
					KBASE_JD_ATOM_STATE_UNUSED) != 0) {
				/* We're being killed so the result code
				 * doesn't really matter
				 */
				return 0;
			}
			mutex_lock(&jctx->lock);
		}
		KBASE_TLSTREAM_TL_JD_SUBMIT_ATOM_START(kbdev, katom);
		need_to_try_schedule_context |= jd_submit_atom(kctx, &user_atom,
			&user_jc_incr, katom);
		KBASE_TLSTREAM_TL_JD_SUBMIT_ATOM_END(kbdev, katom);
		/* Register a completed job as a disjoint event when the GPU is in a disjoint state
		 * (ie. being reset).
		 */
		kbase_disjoint_event_potential(kbdev);

		mutex_unlock(&jctx->lock);
	}

	if (need_to_try_schedule_context)
		kbase_js_sched_all(kbdev);

	return err;
}

KBASE_EXPORT_TEST_API(kbase_jd_submit);

void kbase_jd_done_worker(struct work_struct *data)
{
	struct kbase_jd_atom *katom = container_of(data, struct kbase_jd_atom, work);
	struct kbase_jd_context *jctx;
	struct kbase_context *kctx;
	struct kbasep_js_kctx_info *js_kctx_info;
	struct kbase_device *kbdev;
	struct kbasep_js_device_data *js_devdata;
	u64 cache_jc = katom->jc;
	struct kbasep_js_atom_retained_state katom_retained_state;
	bool context_idle;
	base_jd_core_req core_req = katom->core_req;

	/* Soft jobs should never reach this function */
	KBASE_DEBUG_ASSERT((katom->core_req & BASE_JD_REQ_SOFT_JOB) == 0);

	kctx = katom->kctx;
	jctx = &kctx->jctx;
	kbdev = kctx->kbdev;
	js_kctx_info = &kctx->jctx.sched_info;
	js_devdata = &kbdev->js_data;

	dev_dbg(kbdev->dev, "Enter atom %pK done worker for kctx %pK\n",
		(void *)katom, (void *)kctx);

	KBASE_KTRACE_ADD_JM(kbdev, JD_DONE_WORKER, kctx, katom, katom->jc, 0);

	kbase_backend_complete_wq(kbdev, katom);

	/*
	 * Begin transaction on JD context and JS context
	 */
	mutex_lock(&jctx->lock);
	KBASE_TLSTREAM_TL_ATTRIB_ATOM_STATE(kbdev, katom, TL_ATOM_STATE_DONE);
	mutex_lock(&js_devdata->queue_mutex);
	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);

	/* This worker only gets called on contexts that are scheduled *in*. This is
	 * because it only happens in response to an IRQ from a job that was
	 * running.
	 */
	KBASE_DEBUG_ASSERT(kbase_ctx_flag(kctx, KCTX_SCHEDULED));

	if (katom->event_code == BASE_JD_EVENT_STOPPED) {
		unsigned long flags;

		dev_dbg(kbdev->dev, "Atom %pK has been promoted to stopped\n",
			(void *)katom);
		mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
		mutex_unlock(&js_devdata->queue_mutex);

		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

		katom->status = KBASE_JD_ATOM_STATE_IN_JS;
		dev_dbg(kctx->kbdev->dev, "Atom %pK status to in JS\n",
			(void *)katom);
		kbase_js_unpull(kctx, katom);

		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		mutex_unlock(&jctx->lock);

		return;
	}

	if ((katom->event_code != BASE_JD_EVENT_DONE) &&
			(!kbase_ctx_flag(katom->kctx, KCTX_DYING)))
		dev_err(kbdev->dev,
			"t6xx: GPU fault 0x%02lx from job slot %d\n",
					(unsigned long)katom->event_code,
								katom->slot_nr);

	/* Retain state before the katom disappears */
	kbasep_js_atom_retained_state_copy(&katom_retained_state, katom);

	context_idle = kbase_js_complete_atom_wq(kctx, katom);

	KBASE_DEBUG_ASSERT(kbasep_js_has_atom_finished(&katom_retained_state));

	kbasep_js_remove_job(kbdev, kctx, katom);
	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
	mutex_unlock(&js_devdata->queue_mutex);
	/* jd_done_nolock() requires the jsctx_mutex lock to be dropped */
	jd_done_nolock(katom, false);

	/* katom may have been freed now, do not use! */

	if (context_idle) {
		unsigned long flags;

		context_idle = false;
		mutex_lock(&js_devdata->queue_mutex);
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

		/* If kbase_sched() has scheduled this context back in then
		 * KCTX_ACTIVE will have been set after we marked it as
		 * inactive, and another pm reference will have been taken, so
		 * drop our reference. But do not call kbase_jm_idle_ctx(), as
		 * the context is active and fast-starting is allowed.
		 *
		 * If an atom has been fast-started then
		 * kbase_jsctx_atoms_pulled(kctx) will return non-zero but
		 * KCTX_ACTIVE will still be false (as the previous pm
		 * reference has been inherited). Do NOT drop our reference, as
		 * it has been re-used, and leave the context as active.
		 *
		 * If no new atoms have been started then KCTX_ACTIVE will
		 * still be false and kbase_jsctx_atoms_pulled(kctx) will
		 * return zero, so drop the reference and call
		 * kbase_jm_idle_ctx().
		 *
		 * As the checks are done under both the queue_mutex and
		 * hwaccess_lock is should be impossible for this to race
		 * with the scheduler code.
		 */
		if (kbase_ctx_flag(kctx, KCTX_ACTIVE) ||
		    !kbase_jsctx_atoms_pulled(kctx)) {
			/* Calling kbase_jm_idle_ctx() here will ensure that
			 * atoms are not fast-started when we drop the
			 * hwaccess_lock. This is not performed if
			 * KCTX_ACTIVE is set as in that case another pm
			 * reference has been taken and a fast-start would be
			 * valid.
			 */
			if (!kbase_ctx_flag(kctx, KCTX_ACTIVE))
				kbase_jm_idle_ctx(kbdev, kctx);
			context_idle = true;
		} else {
			kbase_ctx_flag_set(kctx, KCTX_ACTIVE);
		}
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		mutex_unlock(&js_devdata->queue_mutex);
	}

	/*
	 * Transaction complete
	 */
	mutex_unlock(&jctx->lock);

	/* Job is now no longer running, so can now safely release the context
	 * reference, and handle any actions that were logged against the
	 * atom's retained state
	 */

	kbasep_js_runpool_release_ctx_and_katom_retained_state(kbdev, kctx, &katom_retained_state);

	kbase_js_sched_all(kbdev);

	if (!atomic_dec_return(&kctx->work_count)) {
		/* If worker now idle then post all events that jd_done_nolock()
		 * has queued
		 */
		mutex_lock(&jctx->lock);
		while (!list_empty(&kctx->completed_jobs)) {
			struct kbase_jd_atom *atom = list_entry(
					kctx->completed_jobs.next,
					struct kbase_jd_atom, jd_item);
			list_del(kctx->completed_jobs.next);

			kbase_event_post(kctx, atom);
		}
		mutex_unlock(&jctx->lock);
	}

	kbase_backend_complete_wq_post_sched(kbdev, core_req);

	if (context_idle)
		kbase_pm_context_idle(kbdev);

	KBASE_KTRACE_ADD_JM(kbdev, JD_DONE_WORKER_END, kctx, NULL, cache_jc, 0);

	dev_dbg(kbdev->dev, "Leave atom %pK done worker for kctx %pK\n",
		(void *)katom, (void *)kctx);
}

/**
 * jd_cancel_worker - Work queue job cancel function.
 * @data: a &struct work_struct
 *
 * Only called as part of 'Zapping' a context (which occurs on termination).
 * Operates serially with the kbase_jd_done_worker() on the work queue.
 *
 * This can only be called on contexts that aren't scheduled.
 *
 * We don't need to release most of the resources that would occur on
 * kbase_jd_done() or kbase_jd_done_worker(), because the atoms here must not be
 * running (by virtue of only being called on contexts that aren't
 * scheduled).
 */
static void jd_cancel_worker(struct work_struct *data)
{
	struct kbase_jd_atom *katom = container_of(data, struct kbase_jd_atom, work);
	struct kbase_jd_context *jctx;
	struct kbase_context *kctx;
	struct kbasep_js_kctx_info *js_kctx_info;
	bool need_to_try_schedule_context;
	bool attr_state_changed;
	struct kbase_device *kbdev;

	/* Soft jobs should never reach this function */
	KBASE_DEBUG_ASSERT((katom->core_req & BASE_JD_REQ_SOFT_JOB) == 0);

	kctx = katom->kctx;
	kbdev = kctx->kbdev;
	jctx = &kctx->jctx;
	js_kctx_info = &kctx->jctx.sched_info;

	KBASE_KTRACE_ADD_JM(kbdev, JD_CANCEL_WORKER, kctx, katom, katom->jc, 0);

	/* This only gets called on contexts that are scheduled out. Hence, we must
	 * make sure we don't de-ref the number of running jobs (there aren't
	 * any), nor must we try to schedule out the context (it's already
	 * scheduled out).
	 */
	KBASE_DEBUG_ASSERT(!kbase_ctx_flag(kctx, KCTX_SCHEDULED));

	/* Scheduler: Remove the job from the system */
	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
	attr_state_changed = kbasep_js_remove_cancelled_job(kbdev, kctx, katom);
	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

	mutex_lock(&jctx->lock);

	need_to_try_schedule_context = jd_done_nolock(katom, true);
	/* Because we're zapping, we're not adding any more jobs to this ctx, so no need to
	 * schedule the context. There's also no need for the jsctx_mutex to have been taken
	 * around this too.
	 */
	KBASE_DEBUG_ASSERT(!need_to_try_schedule_context);

	/* katom may have been freed now, do not use! */
	mutex_unlock(&jctx->lock);

	if (attr_state_changed)
		kbase_js_sched_all(kbdev);
}

/**
 * kbase_jd_done - Complete a job that has been removed from the Hardware
 * @katom: atom which has been completed
 * @slot_nr: slot the atom was on
 * @end_timestamp: completion time
 * @done_code: completion code
 *
 * This must be used whenever a job has been removed from the Hardware, e.g.:
 * An IRQ indicates that the job finished (for both error and 'done' codes), or
 * the job was evicted from the JS_HEAD_NEXT registers during a Soft/Hard stop.
 *
 * Some work is carried out immediately, and the rest is deferred onto a
 * workqueue
 *
 * Context:
 *   This can be called safely from atomic context.
 *   The caller must hold kbdev->hwaccess_lock
 */
void kbase_jd_done(struct kbase_jd_atom *katom, int slot_nr,
		ktime_t *end_timestamp, kbasep_js_atom_done_code done_code)
{
	struct kbase_context *kctx;
	struct kbase_device *kbdev;

	KBASE_DEBUG_ASSERT(katom);
	kctx = katom->kctx;
	KBASE_DEBUG_ASSERT(kctx);
	kbdev = kctx->kbdev;
	KBASE_DEBUG_ASSERT(kbdev);

	if (done_code & KBASE_JS_ATOM_DONE_EVICTED_FROM_NEXT)
		katom->event_code = BASE_JD_EVENT_REMOVED_FROM_NEXT;

	KBASE_KTRACE_ADD_JM(kbdev, JD_DONE, kctx, katom, katom->jc, 0);

	kbase_job_check_leave_disjoint(kbdev, katom);

	katom->slot_nr = slot_nr;

	atomic_inc(&kctx->work_count);

#if IS_ENABLED(CONFIG_DEBUG_FS)
	/* a failed job happened and is waiting for dumping*/
	if (!katom->will_fail_event_code &&
			kbase_debug_job_fault_process(katom, katom->event_code))
		return;
#endif

	WARN_ON(work_pending(&katom->work));
	INIT_WORK(&katom->work, kbase_jd_done_worker);
	queue_work(kctx->jctx.job_done_wq, &katom->work);
}

KBASE_EXPORT_TEST_API(kbase_jd_done);

void kbase_jd_cancel(struct kbase_device *kbdev, struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(katom != NULL);
	kctx = katom->kctx;
	KBASE_DEBUG_ASSERT(kctx != NULL);

	dev_dbg(kbdev->dev, "JD: cancelling atom %pK\n", (void *)katom);
	KBASE_KTRACE_ADD_JM(kbdev, JD_CANCEL, kctx, katom, katom->jc, 0);

	/* This should only be done from a context that is not scheduled */
	KBASE_DEBUG_ASSERT(!kbase_ctx_flag(kctx, KCTX_SCHEDULED));

	WARN_ON(work_pending(&katom->work));

	katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;

	INIT_WORK(&katom->work, jd_cancel_worker);
	queue_work(kctx->jctx.job_done_wq, &katom->work);
}


void kbase_jd_zap_context(struct kbase_context *kctx)
{
	struct kbase_jd_atom *katom;
	struct list_head *entry, *tmp;
	struct kbase_device *kbdev;

	KBASE_DEBUG_ASSERT(kctx);

	kbdev = kctx->kbdev;

	KBASE_KTRACE_ADD_JM(kbdev, JD_ZAP_CONTEXT, kctx, NULL, 0u, 0u);

	kbase_js_zap_context(kctx);

	mutex_lock(&kctx->jctx.lock);

	/*
	 * While holding the struct kbase_jd_context lock clean up jobs which are known to kbase but are
	 * queued outside the job scheduler.
	 */

	del_timer_sync(&kctx->soft_job_timeout);
	list_for_each_safe(entry, tmp, &kctx->waiting_soft_jobs) {
		katom = list_entry(entry, struct kbase_jd_atom, queue);
		kbase_cancel_soft_job(katom);
	}


#ifdef CONFIG_MALI_BIFROST_DMA_FENCE
	kbase_dma_fence_cancel_all_atoms(kctx);
#endif

	mutex_unlock(&kctx->jctx.lock);

#ifdef CONFIG_MALI_BIFROST_DMA_FENCE
	/* Flush dma-fence workqueue to ensure that any callbacks that may have
	 * been queued are done before continuing.
	 */
	flush_workqueue(kctx->dma_fence.wq);
#endif

#if IS_ENABLED(CONFIG_DEBUG_FS)
	kbase_debug_job_fault_kctx_unblock(kctx);
#endif

	kbase_jm_wait_for_zero_jobs(kctx);
}

KBASE_EXPORT_TEST_API(kbase_jd_zap_context);

int kbase_jd_init(struct kbase_context *kctx)
{
	int i;
	int mali_err = 0;
	struct priority_control_manager_device *pcm_device = NULL;

	KBASE_DEBUG_ASSERT(kctx);
	pcm_device = kctx->kbdev->pcm_dev;
	kctx->jctx.max_priority = KBASE_JS_ATOM_SCHED_PRIO_REALTIME;

	kctx->jctx.job_done_wq = alloc_workqueue("mali_jd",
			WQ_HIGHPRI | WQ_UNBOUND, 1);
	if (kctx->jctx.job_done_wq == NULL) {
		mali_err = -ENOMEM;
		goto out1;
	}

	for (i = 0; i < BASE_JD_ATOM_COUNT; i++) {
		init_waitqueue_head(&kctx->jctx.atoms[i].completed);

		INIT_LIST_HEAD(&kctx->jctx.atoms[i].dep_head[0]);
		INIT_LIST_HEAD(&kctx->jctx.atoms[i].dep_head[1]);

		/* Catch userspace attempting to use an atom which doesn't exist as a pre-dependency */
		kctx->jctx.atoms[i].event_code = BASE_JD_EVENT_JOB_INVALID;
		kctx->jctx.atoms[i].status = KBASE_JD_ATOM_STATE_UNUSED;

#if defined(CONFIG_MALI_BIFROST_DMA_FENCE) || defined(CONFIG_SYNC_FILE)
		kctx->jctx.atoms[i].dma_fence.context =
						dma_fence_context_alloc(1);
		atomic_set(&kctx->jctx.atoms[i].dma_fence.seqno, 0);
		INIT_LIST_HEAD(&kctx->jctx.atoms[i].dma_fence.callbacks);
#endif
	}

	for (i = 0; i < BASE_JD_RP_COUNT; i++)
		kctx->jctx.renderpasses[i].state = KBASE_JD_RP_COMPLETE;

	mutex_init(&kctx->jctx.lock);

	init_waitqueue_head(&kctx->jctx.zero_jobs_wait);

	spin_lock_init(&kctx->jctx.tb_lock);

	kctx->jctx.job_nr = 0;
	INIT_LIST_HEAD(&kctx->completed_jobs);
	atomic_set(&kctx->work_count, 0);

	/* Check if there are platform rules for maximum priority */
	if (pcm_device)
		kctx->jctx.max_priority = pcm_device->ops.pcm_scheduler_priority_check(
				pcm_device, current, KBASE_JS_ATOM_SCHED_PRIO_REALTIME);

	return 0;

 out1:
	return mali_err;
}

KBASE_EXPORT_TEST_API(kbase_jd_init);

void kbase_jd_exit(struct kbase_context *kctx)
{
	KBASE_DEBUG_ASSERT(kctx);

	/* Work queue is emptied by this */
	destroy_workqueue(kctx->jctx.job_done_wq);
}

KBASE_EXPORT_TEST_API(kbase_jd_exit);
