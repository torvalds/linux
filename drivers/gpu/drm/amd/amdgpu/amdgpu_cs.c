/*
 * Copyright 2008 Jerome Glisse.
 * All Rights Reserved.
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
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Jerome Glisse <glisse@freedesktop.org>
 */
#include <linux/pagemap.h>
#include <linux/sync_file.h>
#include <drm/drmP.h>
#include <drm/amdgpu_drm.h>
#include <drm/drm_syncobj.h>
#include "amdgpu.h"
#include "amdgpu_trace.h"
#include "amdgpu_gmc.h"
#include "amdgpu_gem.h"

static int amdgpu_cs_user_fence_chunk(struct amdgpu_cs_parser *p,
				      struct drm_amdgpu_cs_chunk_fence *data,
				      uint32_t *offset)
{
	struct drm_gem_object *gobj;
	struct amdgpu_bo *bo;
	unsigned long size;
	int r;

	gobj = drm_gem_object_lookup(p->filp, data->handle);
	if (gobj == NULL)
		return -EINVAL;

	bo = amdgpu_bo_ref(gem_to_amdgpu_bo(gobj));
	p->uf_entry.priority = 0;
	p->uf_entry.tv.bo = &bo->tbo;
	/* One for TTM and one for the CS job */
	p->uf_entry.tv.num_shared = 2;
	p->uf_entry.user_pages = NULL;

	drm_gem_object_put_unlocked(gobj);

	size = amdgpu_bo_size(bo);
	if (size != PAGE_SIZE || (data->offset + 8) > size) {
		r = -EINVAL;
		goto error_unref;
	}

	if (amdgpu_ttm_tt_get_usermm(bo->tbo.ttm)) {
		r = -EINVAL;
		goto error_unref;
	}

	*offset = data->offset;

	return 0;

error_unref:
	amdgpu_bo_unref(&bo);
	return r;
}

static int amdgpu_cs_bo_handles_chunk(struct amdgpu_cs_parser *p,
				      struct drm_amdgpu_bo_list_in *data)
{
	int r;
	struct drm_amdgpu_bo_list_entry *info = NULL;

	r = amdgpu_bo_create_list_entry_array(data, &info);
	if (r)
		return r;

	r = amdgpu_bo_list_create(p->adev, p->filp, info, data->bo_number,
				  &p->bo_list);
	if (r)
		goto error_free;

	kvfree(info);
	return 0;

error_free:
	if (info)
		kvfree(info);

	return r;
}

static int amdgpu_cs_parser_init(struct amdgpu_cs_parser *p, union drm_amdgpu_cs *cs)
{
	struct amdgpu_fpriv *fpriv = p->filp->driver_priv;
	struct amdgpu_vm *vm = &fpriv->vm;
	uint64_t *chunk_array_user;
	uint64_t *chunk_array;
	unsigned size, num_ibs = 0;
	uint32_t uf_offset = 0;
	int i;
	int ret;

	if (cs->in.num_chunks == 0)
		return 0;

	chunk_array = kmalloc_array(cs->in.num_chunks, sizeof(uint64_t), GFP_KERNEL);
	if (!chunk_array)
		return -ENOMEM;

	p->ctx = amdgpu_ctx_get(fpriv, cs->in.ctx_id);
	if (!p->ctx) {
		ret = -EINVAL;
		goto free_chunk;
	}

	mutex_lock(&p->ctx->lock);

	/* skip guilty context job */
	if (atomic_read(&p->ctx->guilty) == 1) {
		ret = -ECANCELED;
		goto free_chunk;
	}

	/* get chunks */
	chunk_array_user = u64_to_user_ptr(cs->in.chunks);
	if (copy_from_user(chunk_array, chunk_array_user,
			   sizeof(uint64_t)*cs->in.num_chunks)) {
		ret = -EFAULT;
		goto free_chunk;
	}

	p->nchunks = cs->in.num_chunks;
	p->chunks = kmalloc_array(p->nchunks, sizeof(struct amdgpu_cs_chunk),
			    GFP_KERNEL);
	if (!p->chunks) {
		ret = -ENOMEM;
		goto free_chunk;
	}

	for (i = 0; i < p->nchunks; i++) {
		struct drm_amdgpu_cs_chunk __user **chunk_ptr = NULL;
		struct drm_amdgpu_cs_chunk user_chunk;
		uint32_t __user *cdata;

		chunk_ptr = u64_to_user_ptr(chunk_array[i]);
		if (copy_from_user(&user_chunk, chunk_ptr,
				       sizeof(struct drm_amdgpu_cs_chunk))) {
			ret = -EFAULT;
			i--;
			goto free_partial_kdata;
		}
		p->chunks[i].chunk_id = user_chunk.chunk_id;
		p->chunks[i].length_dw = user_chunk.length_dw;

		size = p->chunks[i].length_dw;
		cdata = u64_to_user_ptr(user_chunk.chunk_data);

		p->chunks[i].kdata = kvmalloc_array(size, sizeof(uint32_t), GFP_KERNEL);
		if (p->chunks[i].kdata == NULL) {
			ret = -ENOMEM;
			i--;
			goto free_partial_kdata;
		}
		size *= sizeof(uint32_t);
		if (copy_from_user(p->chunks[i].kdata, cdata, size)) {
			ret = -EFAULT;
			goto free_partial_kdata;
		}

		switch (p->chunks[i].chunk_id) {
		case AMDGPU_CHUNK_ID_IB:
			++num_ibs;
			break;

		case AMDGPU_CHUNK_ID_FENCE:
			size = sizeof(struct drm_amdgpu_cs_chunk_fence);
			if (p->chunks[i].length_dw * sizeof(uint32_t) < size) {
				ret = -EINVAL;
				goto free_partial_kdata;
			}

			ret = amdgpu_cs_user_fence_chunk(p, p->chunks[i].kdata,
							 &uf_offset);
			if (ret)
				goto free_partial_kdata;

			break;

		case AMDGPU_CHUNK_ID_BO_HANDLES:
			size = sizeof(struct drm_amdgpu_bo_list_in);
			if (p->chunks[i].length_dw * sizeof(uint32_t) < size) {
				ret = -EINVAL;
				goto free_partial_kdata;
			}

			ret = amdgpu_cs_bo_handles_chunk(p, p->chunks[i].kdata);
			if (ret)
				goto free_partial_kdata;

			break;

		case AMDGPU_CHUNK_ID_DEPENDENCIES:
		case AMDGPU_CHUNK_ID_SYNCOBJ_IN:
		case AMDGPU_CHUNK_ID_SYNCOBJ_OUT:
		case AMDGPU_CHUNK_ID_SCHEDULED_DEPENDENCIES:
		case AMDGPU_CHUNK_ID_SYNCOBJ_TIMELINE_WAIT:
		case AMDGPU_CHUNK_ID_SYNCOBJ_TIMELINE_SIGNAL:
			break;

		default:
			ret = -EINVAL;
			goto free_partial_kdata;
		}
	}

	ret = amdgpu_job_alloc(p->adev, num_ibs, &p->job, vm);
	if (ret)
		goto free_all_kdata;

	if (p->ctx->vram_lost_counter != p->job->vram_lost_counter) {
		ret = -ECANCELED;
		goto free_all_kdata;
	}

	if (p->uf_entry.tv.bo)
		p->job->uf_addr = uf_offset;
	kfree(chunk_array);

	/* Use this opportunity to fill in task info for the vm */
	amdgpu_vm_set_task_info(vm);

	return 0;

free_all_kdata:
	i = p->nchunks - 1;
free_partial_kdata:
	for (; i >= 0; i--)
		kvfree(p->chunks[i].kdata);
	kfree(p->chunks);
	p->chunks = NULL;
	p->nchunks = 0;
free_chunk:
	kfree(chunk_array);

	return ret;
}

/* Convert microseconds to bytes. */
static u64 us_to_bytes(struct amdgpu_device *adev, s64 us)
{
	if (us <= 0 || !adev->mm_stats.log2_max_MBps)
		return 0;

	/* Since accum_us is incremented by a million per second, just
	 * multiply it by the number of MB/s to get the number of bytes.
	 */
	return us << adev->mm_stats.log2_max_MBps;
}

static s64 bytes_to_us(struct amdgpu_device *adev, u64 bytes)
{
	if (!adev->mm_stats.log2_max_MBps)
		return 0;

	return bytes >> adev->mm_stats.log2_max_MBps;
}

/* Returns how many bytes TTM can move right now. If no bytes can be moved,
 * it returns 0. If it returns non-zero, it's OK to move at least one buffer,
 * which means it can go over the threshold once. If that happens, the driver
 * will be in debt and no other buffer migrations can be done until that debt
 * is repaid.
 *
 * This approach allows moving a buffer of any size (it's important to allow
 * that).
 *
 * The currency is simply time in microseconds and it increases as the clock
 * ticks. The accumulated microseconds (us) are converted to bytes and
 * returned.
 */
static void amdgpu_cs_get_threshold_for_moves(struct amdgpu_device *adev,
					      u64 *max_bytes,
					      u64 *max_vis_bytes)
{
	s64 time_us, increment_us;
	u64 free_vram, total_vram, used_vram;

	/* Allow a maximum of 200 accumulated ms. This is basically per-IB
	 * throttling.
	 *
	 * It means that in order to get full max MBps, at least 5 IBs per
	 * second must be submitted and not more than 200ms apart from each
	 * other.
	 */
	const s64 us_upper_bound = 200000;

	if (!adev->mm_stats.log2_max_MBps) {
		*max_bytes = 0;
		*max_vis_bytes = 0;
		return;
	}

	total_vram = adev->gmc.real_vram_size - atomic64_read(&adev->vram_pin_size);
	used_vram = amdgpu_vram_mgr_usage(&adev->mman.bdev.man[TTM_PL_VRAM]);
	free_vram = used_vram >= total_vram ? 0 : total_vram - used_vram;

	spin_lock(&adev->mm_stats.lock);

	/* Increase the amount of accumulated us. */
	time_us = ktime_to_us(ktime_get());
	increment_us = time_us - adev->mm_stats.last_update_us;
	adev->mm_stats.last_update_us = time_us;
	adev->mm_stats.accum_us = min(adev->mm_stats.accum_us + increment_us,
                                      us_upper_bound);

	/* This prevents the short period of low performance when the VRAM
	 * usage is low and the driver is in debt or doesn't have enough
	 * accumulated us to fill VRAM quickly.
	 *
	 * The situation can occur in these cases:
	 * - a lot of VRAM is freed by userspace
	 * - the presence of a big buffer causes a lot of evictions
	 *   (solution: split buffers into smaller ones)
	 *
	 * If 128 MB or 1/8th of VRAM is free, start filling it now by setting
	 * accum_us to a positive number.
	 */
	if (free_vram >= 128 * 1024 * 1024 || free_vram >= total_vram / 8) {
		s64 min_us;

		/* Be more aggresive on dGPUs. Try to fill a portion of free
		 * VRAM now.
		 */
		if (!(adev->flags & AMD_IS_APU))
			min_us = bytes_to_us(adev, free_vram / 4);
		else
			min_us = 0; /* Reset accum_us on APUs. */

		adev->mm_stats.accum_us = max(min_us, adev->mm_stats.accum_us);
	}

	/* This is set to 0 if the driver is in debt to disallow (optional)
	 * buffer moves.
	 */
	*max_bytes = us_to_bytes(adev, adev->mm_stats.accum_us);

	/* Do the same for visible VRAM if half of it is free */
	if (!amdgpu_gmc_vram_full_visible(&adev->gmc)) {
		u64 total_vis_vram = adev->gmc.visible_vram_size;
		u64 used_vis_vram =
			amdgpu_vram_mgr_vis_usage(&adev->mman.bdev.man[TTM_PL_VRAM]);

		if (used_vis_vram < total_vis_vram) {
			u64 free_vis_vram = total_vis_vram - used_vis_vram;
			adev->mm_stats.accum_us_vis = min(adev->mm_stats.accum_us_vis +
							  increment_us, us_upper_bound);

			if (free_vis_vram >= total_vis_vram / 2)
				adev->mm_stats.accum_us_vis =
					max(bytes_to_us(adev, free_vis_vram / 2),
					    adev->mm_stats.accum_us_vis);
		}

		*max_vis_bytes = us_to_bytes(adev, adev->mm_stats.accum_us_vis);
	} else {
		*max_vis_bytes = 0;
	}

	spin_unlock(&adev->mm_stats.lock);
}

/* Report how many bytes have really been moved for the last command
 * submission. This can result in a debt that can stop buffer migrations
 * temporarily.
 */
void amdgpu_cs_report_moved_bytes(struct amdgpu_device *adev, u64 num_bytes,
				  u64 num_vis_bytes)
{
	spin_lock(&adev->mm_stats.lock);
	adev->mm_stats.accum_us -= bytes_to_us(adev, num_bytes);
	adev->mm_stats.accum_us_vis -= bytes_to_us(adev, num_vis_bytes);
	spin_unlock(&adev->mm_stats.lock);
}

static int amdgpu_cs_bo_validate(struct amdgpu_cs_parser *p,
				 struct amdgpu_bo *bo)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	struct ttm_operation_ctx ctx = {
		.interruptible = true,
		.no_wait_gpu = false,
		.resv = bo->tbo.resv,
		.flags = 0
	};
	uint32_t domain;
	int r;

	if (bo->pin_count)
		return 0;

	/* Don't move this buffer if we have depleted our allowance
	 * to move it. Don't move anything if the threshold is zero.
	 */
	if (p->bytes_moved < p->bytes_moved_threshold) {
		if (!amdgpu_gmc_vram_full_visible(&adev->gmc) &&
		    (bo->flags & AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED)) {
			/* And don't move a CPU_ACCESS_REQUIRED BO to limited
			 * visible VRAM if we've depleted our allowance to do
			 * that.
			 */
			if (p->bytes_moved_vis < p->bytes_moved_vis_threshold)
				domain = bo->preferred_domains;
			else
				domain = bo->allowed_domains;
		} else {
			domain = bo->preferred_domains;
		}
	} else {
		domain = bo->allowed_domains;
	}

retry:
	amdgpu_bo_placement_from_domain(bo, domain);
	r = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);

	p->bytes_moved += ctx.bytes_moved;
	if (!amdgpu_gmc_vram_full_visible(&adev->gmc) &&
	    amdgpu_bo_in_cpu_visible_vram(bo))
		p->bytes_moved_vis += ctx.bytes_moved;

	if (unlikely(r == -ENOMEM) && domain != bo->allowed_domains) {
		domain = bo->allowed_domains;
		goto retry;
	}

	return r;
}

/* Last resort, try to evict something from the current working set */
static bool amdgpu_cs_try_evict(struct amdgpu_cs_parser *p,
				struct amdgpu_bo *validated)
{
	uint32_t domain = validated->allowed_domains;
	struct ttm_operation_ctx ctx = { true, false };
	int r;

	if (!p->evictable)
		return false;

	for (;&p->evictable->tv.head != &p->validated;
	     p->evictable = list_prev_entry(p->evictable, tv.head)) {

		struct amdgpu_bo_list_entry *candidate = p->evictable;
		struct amdgpu_bo *bo = ttm_to_amdgpu_bo(candidate->tv.bo);
		struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
		bool update_bytes_moved_vis;
		uint32_t other;

		/* If we reached our current BO we can forget it */
		if (bo == validated)
			break;

		/* We can't move pinned BOs here */
		if (bo->pin_count)
			continue;

		other = amdgpu_mem_type_to_domain(bo->tbo.mem.mem_type);

		/* Check if this BO is in one of the domains we need space for */
		if (!(other & domain))
			continue;

		/* Check if we can move this BO somewhere else */
		other = bo->allowed_domains & ~domain;
		if (!other)
			continue;

		/* Good we can try to move this BO somewhere else */
		update_bytes_moved_vis =
				!amdgpu_gmc_vram_full_visible(&adev->gmc) &&
				amdgpu_bo_in_cpu_visible_vram(bo);
		amdgpu_bo_placement_from_domain(bo, other);
		r = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
		p->bytes_moved += ctx.bytes_moved;
		if (update_bytes_moved_vis)
			p->bytes_moved_vis += ctx.bytes_moved;

		if (unlikely(r))
			break;

		p->evictable = list_prev_entry(p->evictable, tv.head);
		list_move(&candidate->tv.head, &p->validated);

		return true;
	}

	return false;
}

static int amdgpu_cs_validate(void *param, struct amdgpu_bo *bo)
{
	struct amdgpu_cs_parser *p = param;
	int r;

	do {
		r = amdgpu_cs_bo_validate(p, bo);
	} while (r == -ENOMEM && amdgpu_cs_try_evict(p, bo));
	if (r)
		return r;

	if (bo->shadow)
		r = amdgpu_cs_bo_validate(p, bo->shadow);

	return r;
}

static int amdgpu_cs_list_validate(struct amdgpu_cs_parser *p,
			    struct list_head *validated)
{
	struct ttm_operation_ctx ctx = { true, false };
	struct amdgpu_bo_list_entry *lobj;
	int r;

	list_for_each_entry(lobj, validated, tv.head) {
		struct amdgpu_bo *bo = ttm_to_amdgpu_bo(lobj->tv.bo);
		bool binding_userptr = false;
		struct mm_struct *usermm;

		usermm = amdgpu_ttm_tt_get_usermm(bo->tbo.ttm);
		if (usermm && usermm != current->mm)
			return -EPERM;

		/* Check if we have user pages and nobody bound the BO already */
		if (amdgpu_ttm_tt_userptr_needs_pages(bo->tbo.ttm) &&
		    lobj->user_pages) {
			amdgpu_bo_placement_from_domain(bo,
							AMDGPU_GEM_DOMAIN_CPU);
			r = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
			if (r)
				return r;
			amdgpu_ttm_tt_set_user_pages(bo->tbo.ttm,
						     lobj->user_pages);
			binding_userptr = true;
		}

		if (p->evictable == lobj)
			p->evictable = NULL;

		r = amdgpu_cs_validate(p, bo);
		if (r)
			return r;

		if (binding_userptr) {
			kvfree(lobj->user_pages);
			lobj->user_pages = NULL;
		}
	}
	return 0;
}

static int amdgpu_cs_parser_bos(struct amdgpu_cs_parser *p,
				union drm_amdgpu_cs *cs)
{
	struct amdgpu_fpriv *fpriv = p->filp->driver_priv;
	struct amdgpu_vm *vm = &fpriv->vm;
	struct amdgpu_bo_list_entry *e;
	struct list_head duplicates;
	struct amdgpu_bo *gds;
	struct amdgpu_bo *gws;
	struct amdgpu_bo *oa;
	unsigned tries = 10;
	int r;

	INIT_LIST_HEAD(&p->validated);

	/* p->bo_list could already be assigned if AMDGPU_CHUNK_ID_BO_HANDLES is present */
	if (cs->in.bo_list_handle) {
		if (p->bo_list)
			return -EINVAL;

		r = amdgpu_bo_list_get(fpriv, cs->in.bo_list_handle,
				       &p->bo_list);
		if (r)
			return r;
	} else if (!p->bo_list) {
		/* Create a empty bo_list when no handle is provided */
		r = amdgpu_bo_list_create(p->adev, p->filp, NULL, 0,
					  &p->bo_list);
		if (r)
			return r;
	}

	/* One for TTM and one for the CS job */
	amdgpu_bo_list_for_each_entry(e, p->bo_list)
		e->tv.num_shared = 2;

	amdgpu_bo_list_get_list(p->bo_list, &p->validated);
	if (p->bo_list->first_userptr != p->bo_list->num_entries)
		p->mn = amdgpu_mn_get(p->adev, AMDGPU_MN_TYPE_GFX);

	INIT_LIST_HEAD(&duplicates);
	amdgpu_vm_get_pd_bo(&fpriv->vm, &p->validated, &p->vm_pd);

	if (p->uf_entry.tv.bo && !ttm_to_amdgpu_bo(p->uf_entry.tv.bo)->parent)
		list_add(&p->uf_entry.tv.head, &p->validated);

	while (1) {
		struct list_head need_pages;

		r = ttm_eu_reserve_buffers(&p->ticket, &p->validated, true,
					   &duplicates);
		if (unlikely(r != 0)) {
			if (r != -ERESTARTSYS)
				DRM_ERROR("ttm_eu_reserve_buffers failed.\n");
			goto error_free_pages;
		}

		INIT_LIST_HEAD(&need_pages);
		amdgpu_bo_list_for_each_userptr_entry(e, p->bo_list) {
			struct amdgpu_bo *bo = ttm_to_amdgpu_bo(e->tv.bo);

			if (amdgpu_ttm_tt_userptr_invalidated(bo->tbo.ttm,
				 &e->user_invalidated) && e->user_pages) {

				/* We acquired a page array, but somebody
				 * invalidated it. Free it and try again
				 */
				release_pages(e->user_pages,
					      bo->tbo.ttm->num_pages);
				kvfree(e->user_pages);
				e->user_pages = NULL;
			}

			if (amdgpu_ttm_tt_userptr_needs_pages(bo->tbo.ttm) &&
			    !e->user_pages) {
				list_del(&e->tv.head);
				list_add(&e->tv.head, &need_pages);

				amdgpu_bo_unreserve(bo);
			}
		}

		if (list_empty(&need_pages))
			break;

		/* Unreserve everything again. */
		ttm_eu_backoff_reservation(&p->ticket, &p->validated);

		/* We tried too many times, just abort */
		if (!--tries) {
			r = -EDEADLK;
			DRM_ERROR("deadlock in %s\n", __func__);
			goto error_free_pages;
		}

		/* Fill the page arrays for all userptrs. */
		list_for_each_entry(e, &need_pages, tv.head) {
			struct ttm_tt *ttm = e->tv.bo->ttm;

			e->user_pages = kvmalloc_array(ttm->num_pages,
							 sizeof(struct page*),
							 GFP_KERNEL | __GFP_ZERO);
			if (!e->user_pages) {
				r = -ENOMEM;
				DRM_ERROR("calloc failure in %s\n", __func__);
				goto error_free_pages;
			}

			r = amdgpu_ttm_tt_get_user_pages(ttm, e->user_pages);
			if (r) {
				DRM_ERROR("amdgpu_ttm_tt_get_user_pages failed.\n");
				kvfree(e->user_pages);
				e->user_pages = NULL;
				goto error_free_pages;
			}
		}

		/* And try again. */
		list_splice(&need_pages, &p->validated);
	}

	amdgpu_cs_get_threshold_for_moves(p->adev, &p->bytes_moved_threshold,
					  &p->bytes_moved_vis_threshold);
	p->bytes_moved = 0;
	p->bytes_moved_vis = 0;
	p->evictable = list_last_entry(&p->validated,
				       struct amdgpu_bo_list_entry,
				       tv.head);

	r = amdgpu_vm_validate_pt_bos(p->adev, &fpriv->vm,
				      amdgpu_cs_validate, p);
	if (r) {
		DRM_ERROR("amdgpu_vm_validate_pt_bos() failed.\n");
		goto error_validate;
	}

	r = amdgpu_cs_list_validate(p, &duplicates);
	if (r) {
		DRM_ERROR("amdgpu_cs_list_validate(duplicates) failed.\n");
		goto error_validate;
	}

	r = amdgpu_cs_list_validate(p, &p->validated);
	if (r) {
		DRM_ERROR("amdgpu_cs_list_validate(validated) failed.\n");
		goto error_validate;
	}

	amdgpu_cs_report_moved_bytes(p->adev, p->bytes_moved,
				     p->bytes_moved_vis);

	gds = p->bo_list->gds_obj;
	gws = p->bo_list->gws_obj;
	oa = p->bo_list->oa_obj;

	amdgpu_bo_list_for_each_entry(e, p->bo_list) {
		struct amdgpu_bo *bo = ttm_to_amdgpu_bo(e->tv.bo);

		/* Make sure we use the exclusive slot for shared BOs */
		if (bo->prime_shared_count)
			e->tv.num_shared = 0;
		e->bo_va = amdgpu_vm_bo_find(vm, bo);
	}

	if (gds) {
		p->job->gds_base = amdgpu_bo_gpu_offset(gds) >> PAGE_SHIFT;
		p->job->gds_size = amdgpu_bo_size(gds) >> PAGE_SHIFT;
	}
	if (gws) {
		p->job->gws_base = amdgpu_bo_gpu_offset(gws) >> PAGE_SHIFT;
		p->job->gws_size = amdgpu_bo_size(gws) >> PAGE_SHIFT;
	}
	if (oa) {
		p->job->oa_base = amdgpu_bo_gpu_offset(oa) >> PAGE_SHIFT;
		p->job->oa_size = amdgpu_bo_size(oa) >> PAGE_SHIFT;
	}

	if (!r && p->uf_entry.tv.bo) {
		struct amdgpu_bo *uf = ttm_to_amdgpu_bo(p->uf_entry.tv.bo);

		r = amdgpu_ttm_alloc_gart(&uf->tbo);
		p->job->uf_addr += amdgpu_bo_gpu_offset(uf);
	}

error_validate:
	if (r)
		ttm_eu_backoff_reservation(&p->ticket, &p->validated);

error_free_pages:

	amdgpu_bo_list_for_each_userptr_entry(e, p->bo_list) {
		if (!e->user_pages)
			continue;

		release_pages(e->user_pages, e->tv.bo->ttm->num_pages);
		kvfree(e->user_pages);
	}

	return r;
}

static int amdgpu_cs_sync_rings(struct amdgpu_cs_parser *p)
{
	struct amdgpu_bo_list_entry *e;
	int r;

	list_for_each_entry(e, &p->validated, tv.head) {
		struct amdgpu_bo *bo = ttm_to_amdgpu_bo(e->tv.bo);
		struct reservation_object *resv = bo->tbo.resv;

		r = amdgpu_sync_resv(p->adev, &p->job->sync, resv, p->filp,
				     amdgpu_bo_explicit_sync(bo));

		if (r)
			return r;
	}
	return 0;
}

/**
 * cs_parser_fini() - clean parser states
 * @parser:	parser structure holding parsing context.
 * @error:	error number
 *
 * If error is set than unvalidate buffer, otherwise just free memory
 * used by parsing context.
 **/
static void amdgpu_cs_parser_fini(struct amdgpu_cs_parser *parser, int error,
				  bool backoff)
{
	unsigned i;

	if (error && backoff)
		ttm_eu_backoff_reservation(&parser->ticket,
					   &parser->validated);

	for (i = 0; i < parser->num_post_deps; i++) {
		drm_syncobj_put(parser->post_deps[i].syncobj);
		kfree(parser->post_deps[i].chain);
	}
	kfree(parser->post_deps);

	dma_fence_put(parser->fence);

	if (parser->ctx) {
		mutex_unlock(&parser->ctx->lock);
		amdgpu_ctx_put(parser->ctx);
	}
	if (parser->bo_list)
		amdgpu_bo_list_put(parser->bo_list);

	for (i = 0; i < parser->nchunks; i++)
		kvfree(parser->chunks[i].kdata);
	kfree(parser->chunks);
	if (parser->job)
		amdgpu_job_free(parser->job);
	if (parser->uf_entry.tv.bo) {
		struct amdgpu_bo *uf = ttm_to_amdgpu_bo(parser->uf_entry.tv.bo);

		amdgpu_bo_unref(&uf);
	}
}

static int amdgpu_cs_vm_handling(struct amdgpu_cs_parser *p)
{
	struct amdgpu_ring *ring = to_amdgpu_ring(p->entity->rq->sched);
	struct amdgpu_fpriv *fpriv = p->filp->driver_priv;
	struct amdgpu_device *adev = p->adev;
	struct amdgpu_vm *vm = &fpriv->vm;
	struct amdgpu_bo_list_entry *e;
	struct amdgpu_bo_va *bo_va;
	struct amdgpu_bo *bo;
	int r;

	/* Only for UVD/VCE VM emulation */
	if (ring->funcs->parse_cs || ring->funcs->patch_cs_in_place) {
		unsigned i, j;

		for (i = 0, j = 0; i < p->nchunks && j < p->job->num_ibs; i++) {
			struct drm_amdgpu_cs_chunk_ib *chunk_ib;
			struct amdgpu_bo_va_mapping *m;
			struct amdgpu_bo *aobj = NULL;
			struct amdgpu_cs_chunk *chunk;
			uint64_t offset, va_start;
			struct amdgpu_ib *ib;
			uint8_t *kptr;

			chunk = &p->chunks[i];
			ib = &p->job->ibs[j];
			chunk_ib = chunk->kdata;

			if (chunk->chunk_id != AMDGPU_CHUNK_ID_IB)
				continue;

			va_start = chunk_ib->va_start & AMDGPU_GMC_HOLE_MASK;
			r = amdgpu_cs_find_mapping(p, va_start, &aobj, &m);
			if (r) {
				DRM_ERROR("IB va_start is invalid\n");
				return r;
			}

			if ((va_start + chunk_ib->ib_bytes) >
			    (m->last + 1) * AMDGPU_GPU_PAGE_SIZE) {
				DRM_ERROR("IB va_start+ib_bytes is invalid\n");
				return -EINVAL;
			}

			/* the IB should be reserved at this point */
			r = amdgpu_bo_kmap(aobj, (void **)&kptr);
			if (r) {
				return r;
			}

			offset = m->start * AMDGPU_GPU_PAGE_SIZE;
			kptr += va_start - offset;

			if (ring->funcs->parse_cs) {
				memcpy(ib->ptr, kptr, chunk_ib->ib_bytes);
				amdgpu_bo_kunmap(aobj);

				r = amdgpu_ring_parse_cs(ring, p, j);
				if (r)
					return r;
			} else {
				ib->ptr = (uint32_t *)kptr;
				r = amdgpu_ring_patch_cs_in_place(ring, p, j);
				amdgpu_bo_kunmap(aobj);
				if (r)
					return r;
			}

			j++;
		}
	}

	if (!p->job->vm)
		return amdgpu_cs_sync_rings(p);


	r = amdgpu_vm_clear_freed(adev, vm, NULL);
	if (r)
		return r;

	r = amdgpu_vm_bo_update(adev, fpriv->prt_va, false);
	if (r)
		return r;

	r = amdgpu_sync_fence(adev, &p->job->sync,
			      fpriv->prt_va->last_pt_update, false);
	if (r)
		return r;

	if (amdgpu_sriov_vf(adev)) {
		struct dma_fence *f;

		bo_va = fpriv->csa_va;
		BUG_ON(!bo_va);
		r = amdgpu_vm_bo_update(adev, bo_va, false);
		if (r)
			return r;

		f = bo_va->last_pt_update;
		r = amdgpu_sync_fence(adev, &p->job->sync, f, false);
		if (r)
			return r;
	}

	amdgpu_bo_list_for_each_entry(e, p->bo_list) {
		struct dma_fence *f;

		/* ignore duplicates */
		bo = ttm_to_amdgpu_bo(e->tv.bo);
		if (!bo)
			continue;

		bo_va = e->bo_va;
		if (bo_va == NULL)
			continue;

		r = amdgpu_vm_bo_update(adev, bo_va, false);
		if (r)
			return r;

		f = bo_va->last_pt_update;
		r = amdgpu_sync_fence(adev, &p->job->sync, f, false);
		if (r)
			return r;
	}

	r = amdgpu_vm_handle_moved(adev, vm);
	if (r)
		return r;

	r = amdgpu_vm_update_directories(adev, vm);
	if (r)
		return r;

	r = amdgpu_sync_fence(adev, &p->job->sync, vm->last_update, false);
	if (r)
		return r;

	p->job->vm_pd_addr = amdgpu_gmc_pd_addr(vm->root.base.bo);

	if (amdgpu_vm_debug) {
		/* Invalidate all BOs to test for userspace bugs */
		amdgpu_bo_list_for_each_entry(e, p->bo_list) {
			struct amdgpu_bo *bo = ttm_to_amdgpu_bo(e->tv.bo);

			/* ignore duplicates */
			if (!bo)
				continue;

			amdgpu_vm_bo_invalidate(adev, bo, false);
		}
	}

	return amdgpu_cs_sync_rings(p);
}

static int amdgpu_cs_ib_fill(struct amdgpu_device *adev,
			     struct amdgpu_cs_parser *parser)
{
	struct amdgpu_fpriv *fpriv = parser->filp->driver_priv;
	struct amdgpu_vm *vm = &fpriv->vm;
	int r, ce_preempt = 0, de_preempt = 0;
	struct amdgpu_ring *ring;
	int i, j;

	for (i = 0, j = 0; i < parser->nchunks && j < parser->job->num_ibs; i++) {
		struct amdgpu_cs_chunk *chunk;
		struct amdgpu_ib *ib;
		struct drm_amdgpu_cs_chunk_ib *chunk_ib;
		struct drm_sched_entity *entity;

		chunk = &parser->chunks[i];
		ib = &parser->job->ibs[j];
		chunk_ib = (struct drm_amdgpu_cs_chunk_ib *)chunk->kdata;

		if (chunk->chunk_id != AMDGPU_CHUNK_ID_IB)
			continue;

		if (chunk_ib->ip_type == AMDGPU_HW_IP_GFX && amdgpu_sriov_vf(adev)) {
			if (chunk_ib->flags & AMDGPU_IB_FLAG_PREEMPT) {
				if (chunk_ib->flags & AMDGPU_IB_FLAG_CE)
					ce_preempt++;
				else
					de_preempt++;
			}

			/* each GFX command submit allows 0 or 1 IB preemptible for CE & DE */
			if (ce_preempt > 1 || de_preempt > 1)
				return -EINVAL;
		}

		r = amdgpu_ctx_get_entity(parser->ctx, chunk_ib->ip_type,
					  chunk_ib->ip_instance, chunk_ib->ring,
					  &entity);
		if (r)
			return r;

		if (chunk_ib->flags & AMDGPU_IB_FLAG_PREAMBLE)
			parser->job->preamble_status |=
				AMDGPU_PREAMBLE_IB_PRESENT;

		if (parser->entity && parser->entity != entity)
			return -EINVAL;

		parser->entity = entity;

		ring = to_amdgpu_ring(entity->rq->sched);
		r =  amdgpu_ib_get(adev, vm, ring->funcs->parse_cs ?
				   chunk_ib->ib_bytes : 0, ib);
		if (r) {
			DRM_ERROR("Failed to get ib !\n");
			return r;
		}

		ib->gpu_addr = chunk_ib->va_start;
		ib->length_dw = chunk_ib->ib_bytes / 4;
		ib->flags = chunk_ib->flags;

		j++;
	}

	/* UVD & VCE fw doesn't support user fences */
	ring = to_amdgpu_ring(parser->entity->rq->sched);
	if (parser->job->uf_addr && (
	    ring->funcs->type == AMDGPU_RING_TYPE_UVD ||
	    ring->funcs->type == AMDGPU_RING_TYPE_VCE))
		return -EINVAL;

	return amdgpu_ctx_wait_prev_fence(parser->ctx, parser->entity);
}

static int amdgpu_cs_process_fence_dep(struct amdgpu_cs_parser *p,
				       struct amdgpu_cs_chunk *chunk)
{
	struct amdgpu_fpriv *fpriv = p->filp->driver_priv;
	unsigned num_deps;
	int i, r;
	struct drm_amdgpu_cs_chunk_dep *deps;

	deps = (struct drm_amdgpu_cs_chunk_dep *)chunk->kdata;
	num_deps = chunk->length_dw * 4 /
		sizeof(struct drm_amdgpu_cs_chunk_dep);

	for (i = 0; i < num_deps; ++i) {
		struct amdgpu_ctx *ctx;
		struct drm_sched_entity *entity;
		struct dma_fence *fence;

		ctx = amdgpu_ctx_get(fpriv, deps[i].ctx_id);
		if (ctx == NULL)
			return -EINVAL;

		r = amdgpu_ctx_get_entity(ctx, deps[i].ip_type,
					  deps[i].ip_instance,
					  deps[i].ring, &entity);
		if (r) {
			amdgpu_ctx_put(ctx);
			return r;
		}

		fence = amdgpu_ctx_get_fence(ctx, entity,
					     deps[i].handle);

		if (chunk->chunk_id == AMDGPU_CHUNK_ID_SCHEDULED_DEPENDENCIES) {
			struct drm_sched_fence *s_fence = to_drm_sched_fence(fence);
			struct dma_fence *old = fence;

			fence = dma_fence_get(&s_fence->scheduled);
			dma_fence_put(old);
		}

		if (IS_ERR(fence)) {
			r = PTR_ERR(fence);
			amdgpu_ctx_put(ctx);
			return r;
		} else if (fence) {
			r = amdgpu_sync_fence(p->adev, &p->job->sync, fence,
					true);
			dma_fence_put(fence);
			amdgpu_ctx_put(ctx);
			if (r)
				return r;
		}
	}
	return 0;
}

static int amdgpu_syncobj_lookup_and_add_to_sync(struct amdgpu_cs_parser *p,
						 uint32_t handle, u64 point,
						 u64 flags)
{
	struct dma_fence *fence;
	int r;

	r = drm_syncobj_find_fence(p->filp, handle, point, flags, &fence);
	if (r) {
		DRM_ERROR("syncobj %u failed to find fence @ %llu (%d)!\n",
			  handle, point, r);
		return r;
	}

	r = amdgpu_sync_fence(p->adev, &p->job->sync, fence, true);
	dma_fence_put(fence);

	return r;
}

static int amdgpu_cs_process_syncobj_in_dep(struct amdgpu_cs_parser *p,
					    struct amdgpu_cs_chunk *chunk)
{
	struct drm_amdgpu_cs_chunk_sem *deps;
	unsigned num_deps;
	int i, r;

	deps = (struct drm_amdgpu_cs_chunk_sem *)chunk->kdata;
	num_deps = chunk->length_dw * 4 /
		sizeof(struct drm_amdgpu_cs_chunk_sem);
	for (i = 0; i < num_deps; ++i) {
		r = amdgpu_syncobj_lookup_and_add_to_sync(p, deps[i].handle,
							  0, 0);
		if (r)
			return r;
	}

	return 0;
}


static int amdgpu_cs_process_syncobj_timeline_in_dep(struct amdgpu_cs_parser *p,
						     struct amdgpu_cs_chunk *chunk)
{
	struct drm_amdgpu_cs_chunk_syncobj *syncobj_deps;
	unsigned num_deps;
	int i, r;

	syncobj_deps = (struct drm_amdgpu_cs_chunk_syncobj *)chunk->kdata;
	num_deps = chunk->length_dw * 4 /
		sizeof(struct drm_amdgpu_cs_chunk_syncobj);
	for (i = 0; i < num_deps; ++i) {
		r = amdgpu_syncobj_lookup_and_add_to_sync(p,
							  syncobj_deps[i].handle,
							  syncobj_deps[i].point,
							  syncobj_deps[i].flags);
		if (r)
			return r;
	}

	return 0;
}

static int amdgpu_cs_process_syncobj_out_dep(struct amdgpu_cs_parser *p,
					     struct amdgpu_cs_chunk *chunk)
{
	struct drm_amdgpu_cs_chunk_sem *deps;
	unsigned num_deps;
	int i;

	deps = (struct drm_amdgpu_cs_chunk_sem *)chunk->kdata;
	num_deps = chunk->length_dw * 4 /
		sizeof(struct drm_amdgpu_cs_chunk_sem);

	p->post_deps = kmalloc_array(num_deps, sizeof(*p->post_deps),
				     GFP_KERNEL);
	p->num_post_deps = 0;

	if (!p->post_deps)
		return -ENOMEM;


	for (i = 0; i < num_deps; ++i) {
		p->post_deps[i].syncobj =
			drm_syncobj_find(p->filp, deps[i].handle);
		if (!p->post_deps[i].syncobj)
			return -EINVAL;
		p->post_deps[i].chain = NULL;
		p->post_deps[i].point = 0;
		p->num_post_deps++;
	}

	return 0;
}


static int amdgpu_cs_process_syncobj_timeline_out_dep(struct amdgpu_cs_parser *p,
						      struct amdgpu_cs_chunk
						      *chunk)
{
	struct drm_amdgpu_cs_chunk_syncobj *syncobj_deps;
	unsigned num_deps;
	int i;

	syncobj_deps = (struct drm_amdgpu_cs_chunk_syncobj *)chunk->kdata;
	num_deps = chunk->length_dw * 4 /
		sizeof(struct drm_amdgpu_cs_chunk_syncobj);

	p->post_deps = kmalloc_array(num_deps, sizeof(*p->post_deps),
				     GFP_KERNEL);
	p->num_post_deps = 0;

	if (!p->post_deps)
		return -ENOMEM;

	for (i = 0; i < num_deps; ++i) {
		struct amdgpu_cs_post_dep *dep = &p->post_deps[i];

		dep->chain = NULL;
		if (syncobj_deps[i].point) {
			dep->chain = kmalloc(sizeof(*dep->chain), GFP_KERNEL);
			if (!dep->chain)
				return -ENOMEM;
		}

		dep->syncobj = drm_syncobj_find(p->filp,
						syncobj_deps[i].handle);
		if (!dep->syncobj) {
			kfree(dep->chain);
			return -EINVAL;
		}
		dep->point = syncobj_deps[i].point;
		p->num_post_deps++;
	}

	return 0;
}

static int amdgpu_cs_dependencies(struct amdgpu_device *adev,
				  struct amdgpu_cs_parser *p)
{
	int i, r;

	for (i = 0; i < p->nchunks; ++i) {
		struct amdgpu_cs_chunk *chunk;

		chunk = &p->chunks[i];

		switch (chunk->chunk_id) {
		case AMDGPU_CHUNK_ID_DEPENDENCIES:
		case AMDGPU_CHUNK_ID_SCHEDULED_DEPENDENCIES:
			r = amdgpu_cs_process_fence_dep(p, chunk);
			if (r)
				return r;
			break;
		case AMDGPU_CHUNK_ID_SYNCOBJ_IN:
			r = amdgpu_cs_process_syncobj_in_dep(p, chunk);
			if (r)
				return r;
			break;
		case AMDGPU_CHUNK_ID_SYNCOBJ_OUT:
			r = amdgpu_cs_process_syncobj_out_dep(p, chunk);
			if (r)
				return r;
			break;
		case AMDGPU_CHUNK_ID_SYNCOBJ_TIMELINE_WAIT:
			r = amdgpu_cs_process_syncobj_timeline_in_dep(p, chunk);
			if (r)
				return r;
			break;
		case AMDGPU_CHUNK_ID_SYNCOBJ_TIMELINE_SIGNAL:
			r = amdgpu_cs_process_syncobj_timeline_out_dep(p, chunk);
			if (r)
				return r;
			break;
		}
	}

	return 0;
}

static void amdgpu_cs_post_dependencies(struct amdgpu_cs_parser *p)
{
	int i;

	for (i = 0; i < p->num_post_deps; ++i) {
		if (p->post_deps[i].chain && p->post_deps[i].point) {
			drm_syncobj_add_point(p->post_deps[i].syncobj,
					      p->post_deps[i].chain,
					      p->fence, p->post_deps[i].point);
			p->post_deps[i].chain = NULL;
		} else {
			drm_syncobj_replace_fence(p->post_deps[i].syncobj,
						  p->fence);
		}
	}
}

static int amdgpu_cs_submit(struct amdgpu_cs_parser *p,
			    union drm_amdgpu_cs *cs)
{
	struct amdgpu_fpriv *fpriv = p->filp->driver_priv;
	struct drm_sched_entity *entity = p->entity;
	enum drm_sched_priority priority;
	struct amdgpu_ring *ring;
	struct amdgpu_bo_list_entry *e;
	struct amdgpu_job *job;
	uint64_t seq;

	int r;

	job = p->job;
	p->job = NULL;

	r = drm_sched_job_init(&job->base, entity, p->filp);
	if (r)
		goto error_unlock;

	/* No memory allocation is allowed while holding the mn lock */
	amdgpu_mn_lock(p->mn);
	amdgpu_bo_list_for_each_userptr_entry(e, p->bo_list) {
		struct amdgpu_bo *bo = ttm_to_amdgpu_bo(e->tv.bo);

		if (amdgpu_ttm_tt_userptr_needs_pages(bo->tbo.ttm)) {
			r = -ERESTARTSYS;
			goto error_abort;
		}
	}

	job->owner = p->filp;
	p->fence = dma_fence_get(&job->base.s_fence->finished);

	amdgpu_ctx_add_fence(p->ctx, entity, p->fence, &seq);
	amdgpu_cs_post_dependencies(p);

	if ((job->preamble_status & AMDGPU_PREAMBLE_IB_PRESENT) &&
	    !p->ctx->preamble_presented) {
		job->preamble_status |= AMDGPU_PREAMBLE_IB_PRESENT_FIRST;
		p->ctx->preamble_presented = true;
	}

	cs->out.handle = seq;
	job->uf_sequence = seq;

	amdgpu_job_free_resources(job);

	trace_amdgpu_cs_ioctl(job);
	amdgpu_vm_bo_trace_cs(&fpriv->vm, &p->ticket);
	priority = job->base.s_priority;
	drm_sched_entity_push_job(&job->base, entity);

	ring = to_amdgpu_ring(entity->rq->sched);
	amdgpu_ring_priority_get(ring, priority);

	amdgpu_vm_move_to_lru_tail(p->adev, &fpriv->vm);

	ttm_eu_fence_buffer_objects(&p->ticket, &p->validated, p->fence);
	amdgpu_mn_unlock(p->mn);

	return 0;

error_abort:
	drm_sched_job_cleanup(&job->base);
	amdgpu_mn_unlock(p->mn);

error_unlock:
	amdgpu_job_free(job);
	return r;
}

int amdgpu_cs_ioctl(struct drm_device *dev, void *data, struct drm_file *filp)
{
	struct amdgpu_device *adev = dev->dev_private;
	union drm_amdgpu_cs *cs = data;
	struct amdgpu_cs_parser parser = {};
	bool reserved_buffers = false;
	int i, r;

	if (!adev->accel_working)
		return -EBUSY;

	parser.adev = adev;
	parser.filp = filp;

	r = amdgpu_cs_parser_init(&parser, data);
	if (r) {
		DRM_ERROR("Failed to initialize parser %d!\n", r);
		goto out;
	}

	r = amdgpu_cs_ib_fill(adev, &parser);
	if (r)
		goto out;

	r = amdgpu_cs_dependencies(adev, &parser);
	if (r) {
		DRM_ERROR("Failed in the dependencies handling %d!\n", r);
		goto out;
	}

	r = amdgpu_cs_parser_bos(&parser, data);
	if (r) {
		if (r == -ENOMEM)
			DRM_ERROR("Not enough memory for command submission!\n");
		else if (r != -ERESTARTSYS)
			DRM_ERROR("Failed to process the buffer list %d!\n", r);
		goto out;
	}

	reserved_buffers = true;

	for (i = 0; i < parser.job->num_ibs; i++)
		trace_amdgpu_cs(&parser, i);

	r = amdgpu_cs_vm_handling(&parser);
	if (r)
		goto out;

	r = amdgpu_cs_submit(&parser, cs);

out:
	amdgpu_cs_parser_fini(&parser, r, reserved_buffers);
	return r;
}

/**
 * amdgpu_cs_wait_ioctl - wait for a command submission to finish
 *
 * @dev: drm device
 * @data: data from userspace
 * @filp: file private
 *
 * Wait for the command submission identified by handle to finish.
 */
int amdgpu_cs_wait_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *filp)
{
	union drm_amdgpu_wait_cs *wait = data;
	unsigned long timeout = amdgpu_gem_timeout(wait->in.timeout);
	struct drm_sched_entity *entity;
	struct amdgpu_ctx *ctx;
	struct dma_fence *fence;
	long r;

	ctx = amdgpu_ctx_get(filp->driver_priv, wait->in.ctx_id);
	if (ctx == NULL)
		return -EINVAL;

	r = amdgpu_ctx_get_entity(ctx, wait->in.ip_type, wait->in.ip_instance,
				  wait->in.ring, &entity);
	if (r) {
		amdgpu_ctx_put(ctx);
		return r;
	}

	fence = amdgpu_ctx_get_fence(ctx, entity, wait->in.handle);
	if (IS_ERR(fence))
		r = PTR_ERR(fence);
	else if (fence) {
		r = dma_fence_wait_timeout(fence, true, timeout);
		if (r > 0 && fence->error)
			r = fence->error;
		dma_fence_put(fence);
	} else
		r = 1;

	amdgpu_ctx_put(ctx);
	if (r < 0)
		return r;

	memset(wait, 0, sizeof(*wait));
	wait->out.status = (r == 0);

	return 0;
}

/**
 * amdgpu_cs_get_fence - helper to get fence from drm_amdgpu_fence
 *
 * @adev: amdgpu device
 * @filp: file private
 * @user: drm_amdgpu_fence copied from user space
 */
static struct dma_fence *amdgpu_cs_get_fence(struct amdgpu_device *adev,
					     struct drm_file *filp,
					     struct drm_amdgpu_fence *user)
{
	struct drm_sched_entity *entity;
	struct amdgpu_ctx *ctx;
	struct dma_fence *fence;
	int r;

	ctx = amdgpu_ctx_get(filp->driver_priv, user->ctx_id);
	if (ctx == NULL)
		return ERR_PTR(-EINVAL);

	r = amdgpu_ctx_get_entity(ctx, user->ip_type, user->ip_instance,
				  user->ring, &entity);
	if (r) {
		amdgpu_ctx_put(ctx);
		return ERR_PTR(r);
	}

	fence = amdgpu_ctx_get_fence(ctx, entity, user->seq_no);
	amdgpu_ctx_put(ctx);

	return fence;
}

int amdgpu_cs_fence_to_handle_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *filp)
{
	struct amdgpu_device *adev = dev->dev_private;
	union drm_amdgpu_fence_to_handle *info = data;
	struct dma_fence *fence;
	struct drm_syncobj *syncobj;
	struct sync_file *sync_file;
	int fd, r;

	fence = amdgpu_cs_get_fence(adev, filp, &info->in.fence);
	if (IS_ERR(fence))
		return PTR_ERR(fence);

	if (!fence)
		fence = dma_fence_get_stub();

	switch (info->in.what) {
	case AMDGPU_FENCE_TO_HANDLE_GET_SYNCOBJ:
		r = drm_syncobj_create(&syncobj, 0, fence);
		dma_fence_put(fence);
		if (r)
			return r;
		r = drm_syncobj_get_handle(filp, syncobj, &info->out.handle);
		drm_syncobj_put(syncobj);
		return r;

	case AMDGPU_FENCE_TO_HANDLE_GET_SYNCOBJ_FD:
		r = drm_syncobj_create(&syncobj, 0, fence);
		dma_fence_put(fence);
		if (r)
			return r;
		r = drm_syncobj_get_fd(syncobj, (int*)&info->out.handle);
		drm_syncobj_put(syncobj);
		return r;

	case AMDGPU_FENCE_TO_HANDLE_GET_SYNC_FILE_FD:
		fd = get_unused_fd_flags(O_CLOEXEC);
		if (fd < 0) {
			dma_fence_put(fence);
			return fd;
		}

		sync_file = sync_file_create(fence);
		dma_fence_put(fence);
		if (!sync_file) {
			put_unused_fd(fd);
			return -ENOMEM;
		}

		fd_install(fd, sync_file->file);
		info->out.handle = fd;
		return 0;

	default:
		return -EINVAL;
	}
}

/**
 * amdgpu_cs_wait_all_fence - wait on all fences to signal
 *
 * @adev: amdgpu device
 * @filp: file private
 * @wait: wait parameters
 * @fences: array of drm_amdgpu_fence
 */
static int amdgpu_cs_wait_all_fences(struct amdgpu_device *adev,
				     struct drm_file *filp,
				     union drm_amdgpu_wait_fences *wait,
				     struct drm_amdgpu_fence *fences)
{
	uint32_t fence_count = wait->in.fence_count;
	unsigned int i;
	long r = 1;

	for (i = 0; i < fence_count; i++) {
		struct dma_fence *fence;
		unsigned long timeout = amdgpu_gem_timeout(wait->in.timeout_ns);

		fence = amdgpu_cs_get_fence(adev, filp, &fences[i]);
		if (IS_ERR(fence))
			return PTR_ERR(fence);
		else if (!fence)
			continue;

		r = dma_fence_wait_timeout(fence, true, timeout);
		dma_fence_put(fence);
		if (r < 0)
			return r;

		if (r == 0)
			break;

		if (fence->error)
			return fence->error;
	}

	memset(wait, 0, sizeof(*wait));
	wait->out.status = (r > 0);

	return 0;
}

/**
 * amdgpu_cs_wait_any_fence - wait on any fence to signal
 *
 * @adev: amdgpu device
 * @filp: file private
 * @wait: wait parameters
 * @fences: array of drm_amdgpu_fence
 */
static int amdgpu_cs_wait_any_fence(struct amdgpu_device *adev,
				    struct drm_file *filp,
				    union drm_amdgpu_wait_fences *wait,
				    struct drm_amdgpu_fence *fences)
{
	unsigned long timeout = amdgpu_gem_timeout(wait->in.timeout_ns);
	uint32_t fence_count = wait->in.fence_count;
	uint32_t first = ~0;
	struct dma_fence **array;
	unsigned int i;
	long r;

	/* Prepare the fence array */
	array = kcalloc(fence_count, sizeof(struct dma_fence *), GFP_KERNEL);

	if (array == NULL)
		return -ENOMEM;

	for (i = 0; i < fence_count; i++) {
		struct dma_fence *fence;

		fence = amdgpu_cs_get_fence(adev, filp, &fences[i]);
		if (IS_ERR(fence)) {
			r = PTR_ERR(fence);
			goto err_free_fence_array;
		} else if (fence) {
			array[i] = fence;
		} else { /* NULL, the fence has been already signaled */
			r = 1;
			first = i;
			goto out;
		}
	}

	r = dma_fence_wait_any_timeout(array, fence_count, true, timeout,
				       &first);
	if (r < 0)
		goto err_free_fence_array;

out:
	memset(wait, 0, sizeof(*wait));
	wait->out.status = (r > 0);
	wait->out.first_signaled = first;

	if (first < fence_count && array[first])
		r = array[first]->error;
	else
		r = 0;

err_free_fence_array:
	for (i = 0; i < fence_count; i++)
		dma_fence_put(array[i]);
	kfree(array);

	return r;
}

/**
 * amdgpu_cs_wait_fences_ioctl - wait for multiple command submissions to finish
 *
 * @dev: drm device
 * @data: data from userspace
 * @filp: file private
 */
int amdgpu_cs_wait_fences_ioctl(struct drm_device *dev, void *data,
				struct drm_file *filp)
{
	struct amdgpu_device *adev = dev->dev_private;
	union drm_amdgpu_wait_fences *wait = data;
	uint32_t fence_count = wait->in.fence_count;
	struct drm_amdgpu_fence *fences_user;
	struct drm_amdgpu_fence *fences;
	int r;

	/* Get the fences from userspace */
	fences = kmalloc_array(fence_count, sizeof(struct drm_amdgpu_fence),
			GFP_KERNEL);
	if (fences == NULL)
		return -ENOMEM;

	fences_user = u64_to_user_ptr(wait->in.fences);
	if (copy_from_user(fences, fences_user,
		sizeof(struct drm_amdgpu_fence) * fence_count)) {
		r = -EFAULT;
		goto err_free_fences;
	}

	if (wait->in.wait_all)
		r = amdgpu_cs_wait_all_fences(adev, filp, wait, fences);
	else
		r = amdgpu_cs_wait_any_fence(adev, filp, wait, fences);

err_free_fences:
	kfree(fences);

	return r;
}

/**
 * amdgpu_cs_find_bo_va - find bo_va for VM address
 *
 * @parser: command submission parser context
 * @addr: VM address
 * @bo: resulting BO of the mapping found
 *
 * Search the buffer objects in the command submission context for a certain
 * virtual memory address. Returns allocation structure when found, NULL
 * otherwise.
 */
int amdgpu_cs_find_mapping(struct amdgpu_cs_parser *parser,
			   uint64_t addr, struct amdgpu_bo **bo,
			   struct amdgpu_bo_va_mapping **map)
{
	struct amdgpu_fpriv *fpriv = parser->filp->driver_priv;
	struct ttm_operation_ctx ctx = { false, false };
	struct amdgpu_vm *vm = &fpriv->vm;
	struct amdgpu_bo_va_mapping *mapping;
	int r;

	addr /= AMDGPU_GPU_PAGE_SIZE;

	mapping = amdgpu_vm_bo_lookup_mapping(vm, addr);
	if (!mapping || !mapping->bo_va || !mapping->bo_va->base.bo)
		return -EINVAL;

	*bo = mapping->bo_va->base.bo;
	*map = mapping;

	/* Double check that the BO is reserved by this CS */
	if (READ_ONCE((*bo)->tbo.resv->lock.ctx) != &parser->ticket)
		return -EINVAL;

	if (!((*bo)->flags & AMDGPU_GEM_CREATE_VRAM_CONTIGUOUS)) {
		(*bo)->flags |= AMDGPU_GEM_CREATE_VRAM_CONTIGUOUS;
		amdgpu_bo_placement_from_domain(*bo, (*bo)->allowed_domains);
		r = ttm_bo_validate(&(*bo)->tbo, &(*bo)->placement, &ctx);
		if (r)
			return r;
	}

	return amdgpu_ttm_alloc_gart(&(*bo)->tbo);
}
