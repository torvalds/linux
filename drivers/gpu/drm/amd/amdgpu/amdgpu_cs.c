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

#include <linux/file.h>
#include <linux/pagemap.h>
#include <linux/sync_file.h>
#include <linux/dma-buf.h>

#include <drm/amdgpu_drm.h>
#include <drm/drm_syncobj.h>
#include "amdgpu_cs.h"
#include "amdgpu.h"
#include "amdgpu_trace.h"
#include "amdgpu_gmc.h"
#include "amdgpu_gem.h"
#include "amdgpu_ras.h"

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
	/* One for TTM and two for the CS job */
	p->uf_entry.tv.num_shared = 3;

	drm_gem_object_put(gobj);

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
		return -EINVAL;

	chunk_array = kvmalloc_array(cs->in.num_chunks, sizeof(uint64_t), GFP_KERNEL);
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
	p->chunks = kvmalloc_array(p->nchunks, sizeof(struct amdgpu_cs_chunk),
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
	kvfree(chunk_array);

	/* Use this opportunity to fill in task info for the vm */
	amdgpu_vm_set_task_info(vm);

	return 0;

free_all_kdata:
	i = p->nchunks - 1;
free_partial_kdata:
	for (; i >= 0; i--)
		kvfree(p->chunks[i].kdata);
	kvfree(p->chunks);
	p->chunks = NULL;
	p->nchunks = 0;
free_chunk:
	kvfree(chunk_array);

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
	used_vram = ttm_resource_manager_usage(&adev->mman.vram_mgr.manager);
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

		/* Be more aggressive on dGPUs. Try to fill a portion of free
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
		  amdgpu_vram_mgr_vis_usage(&adev->mman.vram_mgr);

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

static int amdgpu_cs_bo_validate(void *param, struct amdgpu_bo *bo)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	struct amdgpu_cs_parser *p = param;
	struct ttm_operation_ctx ctx = {
		.interruptible = true,
		.no_wait_gpu = false,
		.resv = bo->tbo.base.resv
	};
	uint32_t domain;
	int r;

	if (bo->tbo.pin_count)
		return 0;

	/* Don't move this buffer if we have depleted our allowance
	 * to move it. Don't move anything if the threshold is zero.
	 */
	if (p->bytes_moved < p->bytes_moved_threshold &&
	    (!bo->tbo.base.dma_buf ||
	    list_empty(&bo->tbo.base.dma_buf->attachments))) {
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

static int amdgpu_cs_list_validate(struct amdgpu_cs_parser *p,
			    struct list_head *validated)
{
	struct ttm_operation_ctx ctx = { true, false };
	struct amdgpu_bo_list_entry *lobj;
	int r;

	list_for_each_entry(lobj, validated, tv.head) {
		struct amdgpu_bo *bo = ttm_to_amdgpu_bo(lobj->tv.bo);
		struct mm_struct *usermm;

		usermm = amdgpu_ttm_tt_get_usermm(bo->tbo.ttm);
		if (usermm && usermm != current->mm)
			return -EPERM;

		if (amdgpu_ttm_tt_is_userptr(bo->tbo.ttm) &&
		    lobj->user_invalidated && lobj->user_pages) {
			amdgpu_bo_placement_from_domain(bo,
							AMDGPU_GEM_DOMAIN_CPU);
			r = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
			if (r)
				return r;

			amdgpu_ttm_tt_set_user_pages(bo->tbo.ttm,
						     lobj->user_pages);
		}

		r = amdgpu_cs_bo_validate(p, bo);
		if (r)
			return r;

		kvfree(lobj->user_pages);
		lobj->user_pages = NULL;
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

	mutex_lock(&p->bo_list->bo_list_mutex);

	/* One for TTM and one for the CS job */
	amdgpu_bo_list_for_each_entry(e, p->bo_list)
		e->tv.num_shared = 2;

	amdgpu_bo_list_get_list(p->bo_list, &p->validated);

	INIT_LIST_HEAD(&duplicates);
	amdgpu_vm_get_pd_bo(&fpriv->vm, &p->validated, &p->vm_pd);

	if (p->uf_entry.tv.bo && !ttm_to_amdgpu_bo(p->uf_entry.tv.bo)->parent)
		list_add(&p->uf_entry.tv.head, &p->validated);

	/* Get userptr backing pages. If pages are updated after registered
	 * in amdgpu_gem_userptr_ioctl(), amdgpu_cs_list_validate() will do
	 * amdgpu_ttm_backend_bind() to flush and invalidate new pages
	 */
	amdgpu_bo_list_for_each_userptr_entry(e, p->bo_list) {
		struct amdgpu_bo *bo = ttm_to_amdgpu_bo(e->tv.bo);
		bool userpage_invalidated = false;
		int i;

		e->user_pages = kvmalloc_array(bo->tbo.ttm->num_pages,
					sizeof(struct page *),
					GFP_KERNEL | __GFP_ZERO);
		if (!e->user_pages) {
			DRM_ERROR("kvmalloc_array failure\n");
			r = -ENOMEM;
			goto out_free_user_pages;
		}

		r = amdgpu_ttm_tt_get_user_pages(bo, e->user_pages);
		if (r) {
			kvfree(e->user_pages);
			e->user_pages = NULL;
			goto out_free_user_pages;
		}

		for (i = 0; i < bo->tbo.ttm->num_pages; i++) {
			if (bo->tbo.ttm->pages[i] != e->user_pages[i]) {
				userpage_invalidated = true;
				break;
			}
		}
		e->user_invalidated = userpage_invalidated;
	}

	r = ttm_eu_reserve_buffers(&p->ticket, &p->validated, true,
				   &duplicates);
	if (unlikely(r != 0)) {
		if (r != -ERESTARTSYS)
			DRM_ERROR("ttm_eu_reserve_buffers failed.\n");
		goto out_free_user_pages;
	}

	amdgpu_bo_list_for_each_entry(e, p->bo_list) {
		struct amdgpu_bo *bo = ttm_to_amdgpu_bo(e->tv.bo);

		e->bo_va = amdgpu_vm_bo_find(vm, bo);
	}

	/* Move fence waiting after getting reservation lock of
	 * PD root. Then there is no need on a ctx mutex lock.
	 */
	r = amdgpu_ctx_wait_prev_fence(p->ctx, p->entity);
	if (unlikely(r != 0)) {
		if (r != -ERESTARTSYS)
			DRM_ERROR("amdgpu_ctx_wait_prev_fence failed.\n");
		goto error_validate;
	}

	amdgpu_cs_get_threshold_for_moves(p->adev, &p->bytes_moved_threshold,
					  &p->bytes_moved_vis_threshold);
	p->bytes_moved = 0;
	p->bytes_moved_vis = 0;

	r = amdgpu_vm_validate_pt_bos(p->adev, &fpriv->vm,
				      amdgpu_cs_bo_validate, p);
	if (r) {
		DRM_ERROR("amdgpu_vm_validate_pt_bos() failed.\n");
		goto error_validate;
	}

	r = amdgpu_cs_list_validate(p, &duplicates);
	if (r)
		goto error_validate;

	r = amdgpu_cs_list_validate(p, &p->validated);
	if (r)
		goto error_validate;

	amdgpu_cs_report_moved_bytes(p->adev, p->bytes_moved,
				     p->bytes_moved_vis);

	gds = p->bo_list->gds_obj;
	gws = p->bo_list->gws_obj;
	oa = p->bo_list->oa_obj;

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

out_free_user_pages:
	if (r) {
		amdgpu_bo_list_for_each_userptr_entry(e, p->bo_list) {
			struct amdgpu_bo *bo = ttm_to_amdgpu_bo(e->tv.bo);

			if (!e->user_pages)
				continue;
			amdgpu_ttm_tt_get_user_pages_done(bo->tbo.ttm);
			kvfree(e->user_pages);
			e->user_pages = NULL;
		}
		mutex_unlock(&p->bo_list->bo_list_mutex);
	}
	return r;
}

static int amdgpu_cs_sync_rings(struct amdgpu_cs_parser *p)
{
	struct amdgpu_fpriv *fpriv = p->filp->driver_priv;
	struct amdgpu_bo_list_entry *e;
	int r;

	list_for_each_entry(e, &p->validated, tv.head) {
		struct amdgpu_bo *bo = ttm_to_amdgpu_bo(e->tv.bo);
		struct dma_resv *resv = bo->tbo.base.resv;
		enum amdgpu_sync_mode sync_mode;

		sync_mode = amdgpu_bo_explicit_sync(bo) ?
			AMDGPU_SYNC_EXPLICIT : AMDGPU_SYNC_NE_OWNER;
		r = amdgpu_sync_resv(p->adev, &p->job->sync, resv, sync_mode,
				     &fpriv->vm);
		if (r)
			return r;
	}
	return 0;
}

/**
 * amdgpu_cs_parser_fini() - clean parser states
 * @parser:	parser structure holding parsing context.
 * @error:	error number
 * @backoff:	indicator to backoff the reservation
 *
 * If error is set then unvalidate buffer, otherwise just free memory
 * used by parsing context.
 **/
static void amdgpu_cs_parser_fini(struct amdgpu_cs_parser *parser, int error,
				  bool backoff)
{
	unsigned i;

	if (error && backoff) {
		ttm_eu_backoff_reservation(&parser->ticket,
					   &parser->validated);
		mutex_unlock(&parser->bo_list->bo_list_mutex);
	}

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
	kvfree(parser->chunks);
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

				r = amdgpu_ring_parse_cs(ring, p, p->job, ib);
				if (r)
					return r;
			} else {
				ib->ptr = (uint32_t *)kptr;
				r = amdgpu_ring_patch_cs_in_place(ring, p, p->job, ib);
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

	r = amdgpu_sync_fence(&p->job->sync, fpriv->prt_va->last_pt_update);
	if (r)
		return r;

	if (amdgpu_mcbp || amdgpu_sriov_vf(adev)) {
		bo_va = fpriv->csa_va;
		BUG_ON(!bo_va);
		r = amdgpu_vm_bo_update(adev, bo_va, false);
		if (r)
			return r;

		r = amdgpu_sync_fence(&p->job->sync, bo_va->last_pt_update);
		if (r)
			return r;
	}

	amdgpu_bo_list_for_each_entry(e, p->bo_list) {
		/* ignore duplicates */
		bo = ttm_to_amdgpu_bo(e->tv.bo);
		if (!bo)
			continue;

		bo_va = e->bo_va;
		if (bo_va == NULL)
			continue;

		r = amdgpu_vm_bo_update(adev, bo_va, false);
		if (r) {
			mutex_unlock(&p->bo_list->bo_list_mutex);
			return r;
		}

		r = amdgpu_sync_fence(&p->job->sync, bo_va->last_pt_update);
		if (r) {
			mutex_unlock(&p->bo_list->bo_list_mutex);
			return r;
		}
	}

	r = amdgpu_vm_handle_moved(adev, vm);
	if (r)
		return r;

	r = amdgpu_vm_update_pdes(adev, vm, false);
	if (r)
		return r;

	r = amdgpu_sync_fence(&p->job->sync, vm->last_update);
	if (r)
		return r;

	p->job->vm_pd_addr = amdgpu_gmc_pd_addr(vm->root.bo);

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

		if (chunk_ib->ip_type == AMDGPU_HW_IP_GFX &&
		    (amdgpu_mcbp || amdgpu_sriov_vf(adev))) {
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

		/* Return if there is no run queue associated with this entity.
		 * Possibly because of disabled HW IP*/
		if (entity->rq == NULL)
			return -EINVAL;

		parser->entity = entity;

		ring = to_amdgpu_ring(entity->rq->sched);
		r =  amdgpu_ib_get(adev, vm, ring->funcs->parse_cs ?
				   chunk_ib->ib_bytes : 0,
				   AMDGPU_IB_POOL_DELAYED, ib);
		if (r) {
			DRM_ERROR("Failed to get ib !\n");
			return r;
		}

		ib->gpu_addr = chunk_ib->va_start;
		ib->length_dw = chunk_ib->ib_bytes / 4;
		ib->flags = chunk_ib->flags;

		j++;
	}

	/* MM engine doesn't support user fences */
	ring = to_amdgpu_ring(parser->entity->rq->sched);
	if (parser->job->uf_addr && ring->funcs->no_user_fence)
		return -EINVAL;

	return 0;
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

		fence = amdgpu_ctx_get_fence(ctx, entity, deps[i].handle);
		amdgpu_ctx_put(ctx);

		if (IS_ERR(fence))
			return PTR_ERR(fence);
		else if (!fence)
			continue;

		if (chunk->chunk_id == AMDGPU_CHUNK_ID_SCHEDULED_DEPENDENCIES) {
			struct drm_sched_fence *s_fence;
			struct dma_fence *old = fence;

			s_fence = to_drm_sched_fence(fence);
			fence = dma_fence_get(&s_fence->scheduled);
			dma_fence_put(old);
		}

		r = amdgpu_sync_fence(&p->job->sync, fence);
		dma_fence_put(fence);
		if (r)
			return r;
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

	r = amdgpu_sync_fence(&p->job->sync, fence);
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

	if (p->post_deps)
		return -EINVAL;

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
						      struct amdgpu_cs_chunk *chunk)
{
	struct drm_amdgpu_cs_chunk_syncobj *syncobj_deps;
	unsigned num_deps;
	int i;

	syncobj_deps = (struct drm_amdgpu_cs_chunk_syncobj *)chunk->kdata;
	num_deps = chunk->length_dw * 4 /
		sizeof(struct drm_amdgpu_cs_chunk_syncobj);

	if (p->post_deps)
		return -EINVAL;

	p->post_deps = kmalloc_array(num_deps, sizeof(*p->post_deps),
				     GFP_KERNEL);
	p->num_post_deps = 0;

	if (!p->post_deps)
		return -ENOMEM;

	for (i = 0; i < num_deps; ++i) {
		struct amdgpu_cs_post_dep *dep = &p->post_deps[i];

		dep->chain = NULL;
		if (syncobj_deps[i].point) {
			dep->chain = dma_fence_chain_alloc();
			if (!dep->chain)
				return -ENOMEM;
		}

		dep->syncobj = drm_syncobj_find(p->filp,
						syncobj_deps[i].handle);
		if (!dep->syncobj) {
			dma_fence_chain_free(dep->chain);
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

	/* TODO: Investigate why we still need the context lock */
	mutex_unlock(&p->ctx->lock);

	for (i = 0; i < p->nchunks; ++i) {
		struct amdgpu_cs_chunk *chunk;

		chunk = &p->chunks[i];

		switch (chunk->chunk_id) {
		case AMDGPU_CHUNK_ID_DEPENDENCIES:
		case AMDGPU_CHUNK_ID_SCHEDULED_DEPENDENCIES:
			r = amdgpu_cs_process_fence_dep(p, chunk);
			if (r)
				goto out;
			break;
		case AMDGPU_CHUNK_ID_SYNCOBJ_IN:
			r = amdgpu_cs_process_syncobj_in_dep(p, chunk);
			if (r)
				goto out;
			break;
		case AMDGPU_CHUNK_ID_SYNCOBJ_OUT:
			r = amdgpu_cs_process_syncobj_out_dep(p, chunk);
			if (r)
				goto out;
			break;
		case AMDGPU_CHUNK_ID_SYNCOBJ_TIMELINE_WAIT:
			r = amdgpu_cs_process_syncobj_timeline_in_dep(p, chunk);
			if (r)
				goto out;
			break;
		case AMDGPU_CHUNK_ID_SYNCOBJ_TIMELINE_SIGNAL:
			r = amdgpu_cs_process_syncobj_timeline_out_dep(p, chunk);
			if (r)
				goto out;
			break;
		}
	}

out:
	mutex_lock(&p->ctx->lock);
	return r;
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
	struct amdgpu_bo_list_entry *e;
	struct amdgpu_job *job;
	uint64_t seq;
	int r;

	job = p->job;
	p->job = NULL;

	r = drm_sched_job_init(&job->base, entity, &fpriv->vm);
	if (r)
		goto error_unlock;

	drm_sched_job_arm(&job->base);

	/* No memory allocation is allowed while holding the notifier lock.
	 * The lock is held until amdgpu_cs_submit is finished and fence is
	 * added to BOs.
	 */
	mutex_lock(&p->adev->notifier_lock);

	/* If userptr are invalidated after amdgpu_cs_parser_bos(), return
	 * -EAGAIN, drmIoctl in libdrm will restart the amdgpu_cs_ioctl.
	 */
	amdgpu_bo_list_for_each_userptr_entry(e, p->bo_list) {
		struct amdgpu_bo *bo = ttm_to_amdgpu_bo(e->tv.bo);

		r |= !amdgpu_ttm_tt_get_user_pages_done(bo->tbo.ttm);
	}
	if (r) {
		r = -EAGAIN;
		goto error_abort;
	}

	p->fence = dma_fence_get(&job->base.s_fence->finished);

	seq = amdgpu_ctx_add_fence(p->ctx, entity, p->fence);
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
	drm_sched_entity_push_job(&job->base);

	amdgpu_vm_move_to_lru_tail(p->adev, &fpriv->vm);

	/* Make sure all BOs are remembered as writers */
	amdgpu_bo_list_for_each_entry(e, p->bo_list)
		e->tv.num_shared = 0;

	ttm_eu_fence_buffer_objects(&p->ticket, &p->validated, p->fence);
	mutex_unlock(&p->adev->notifier_lock);
	mutex_unlock(&p->bo_list->bo_list_mutex);

	return 0;

error_abort:
	drm_sched_job_cleanup(&job->base);
	mutex_unlock(&p->adev->notifier_lock);

error_unlock:
	amdgpu_job_free(job);
	return r;
}

static void trace_amdgpu_cs_ibs(struct amdgpu_cs_parser *parser)
{
	int i;

	if (!trace_amdgpu_cs_enabled())
		return;

	for (i = 0; i < parser->job->num_ibs; i++)
		trace_amdgpu_cs(parser, i);
}

int amdgpu_cs_ioctl(struct drm_device *dev, void *data, struct drm_file *filp)
{
	struct amdgpu_device *adev = drm_to_adev(dev);
	union drm_amdgpu_cs *cs = data;
	struct amdgpu_cs_parser parser = {};
	bool reserved_buffers = false;
	int r;

	if (amdgpu_ras_intr_triggered())
		return -EHWPOISON;

	if (!adev->accel_working)
		return -EBUSY;

	parser.adev = adev;
	parser.filp = filp;

	r = amdgpu_cs_parser_init(&parser, data);
	if (r) {
		if (printk_ratelimit())
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
		else if (r != -ERESTARTSYS && r != -EAGAIN)
			DRM_ERROR("Failed to process the buffer list %d!\n", r);
		goto out;
	}

	reserved_buffers = true;

	trace_amdgpu_cs_ibs(&parser);

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
	struct amdgpu_device *adev = drm_to_adev(dev);
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
		r = drm_syncobj_get_fd(syncobj, (int *)&info->out.handle);
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
		dma_fence_put(fence);
		return -EINVAL;
	}
}

/**
 * amdgpu_cs_wait_all_fences - wait on all fences to signal
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
	struct amdgpu_device *adev = drm_to_adev(dev);
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
 * amdgpu_cs_find_mapping - find bo_va for VM address
 *
 * @parser: command submission parser context
 * @addr: VM address
 * @bo: resulting BO of the mapping found
 * @map: Placeholder to return found BO mapping
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
	if (dma_resv_locking_ctx((*bo)->tbo.base.resv) != &parser->ticket)
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
