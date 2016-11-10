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
#include <drm/drmP.h>
#include <drm/amdgpu_drm.h>
#include "amdgpu.h"
#include "amdgpu_trace.h"

int amdgpu_cs_get_ring(struct amdgpu_device *adev, u32 ip_type,
		       u32 ip_instance, u32 ring,
		       struct amdgpu_ring **out_ring)
{
	/* Right now all IPs have only one instance - multiple rings. */
	if (ip_instance != 0) {
		DRM_ERROR("invalid ip instance: %d\n", ip_instance);
		return -EINVAL;
	}

	switch (ip_type) {
	default:
		DRM_ERROR("unknown ip type: %d\n", ip_type);
		return -EINVAL;
	case AMDGPU_HW_IP_GFX:
		if (ring < adev->gfx.num_gfx_rings) {
			*out_ring = &adev->gfx.gfx_ring[ring];
		} else {
			DRM_ERROR("only %d gfx rings are supported now\n",
				  adev->gfx.num_gfx_rings);
			return -EINVAL;
		}
		break;
	case AMDGPU_HW_IP_COMPUTE:
		if (ring < adev->gfx.num_compute_rings) {
			*out_ring = &adev->gfx.compute_ring[ring];
		} else {
			DRM_ERROR("only %d compute rings are supported now\n",
				  adev->gfx.num_compute_rings);
			return -EINVAL;
		}
		break;
	case AMDGPU_HW_IP_DMA:
		if (ring < adev->sdma.num_instances) {
			*out_ring = &adev->sdma.instance[ring].ring;
		} else {
			DRM_ERROR("only %d SDMA rings are supported\n",
				  adev->sdma.num_instances);
			return -EINVAL;
		}
		break;
	case AMDGPU_HW_IP_UVD:
		*out_ring = &adev->uvd.ring;
		break;
	case AMDGPU_HW_IP_VCE:
		if (ring < 2){
			*out_ring = &adev->vce.ring[ring];
		} else {
			DRM_ERROR("only two VCE rings are supported\n");
			return -EINVAL;
		}
		break;
	}
	return 0;
}

static int amdgpu_cs_user_fence_chunk(struct amdgpu_cs_parser *p,
				      struct drm_amdgpu_cs_chunk_fence *data,
				      uint32_t *offset)
{
	struct drm_gem_object *gobj;
	unsigned long size;

	gobj = drm_gem_object_lookup(p->filp, data->handle);
	if (gobj == NULL)
		return -EINVAL;

	p->uf_entry.robj = amdgpu_bo_ref(gem_to_amdgpu_bo(gobj));
	p->uf_entry.priority = 0;
	p->uf_entry.tv.bo = &p->uf_entry.robj->tbo;
	p->uf_entry.tv.shared = true;
	p->uf_entry.user_pages = NULL;

	size = amdgpu_bo_size(p->uf_entry.robj);
	if (size != PAGE_SIZE || (data->offset + 8) > size)
		return -EINVAL;

	*offset = data->offset;

	drm_gem_object_unreference_unlocked(gobj);

	if (amdgpu_ttm_tt_get_usermm(p->uf_entry.robj->tbo.ttm)) {
		amdgpu_bo_unref(&p->uf_entry.robj);
		return -EINVAL;
	}

	return 0;
}

int amdgpu_cs_parser_init(struct amdgpu_cs_parser *p, void *data)
{
	struct amdgpu_fpriv *fpriv = p->filp->driver_priv;
	struct amdgpu_vm *vm = &fpriv->vm;
	union drm_amdgpu_cs *cs = data;
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

	/* get chunks */
	chunk_array_user = (uint64_t __user *)(unsigned long)(cs->in.chunks);
	if (copy_from_user(chunk_array, chunk_array_user,
			   sizeof(uint64_t)*cs->in.num_chunks)) {
		ret = -EFAULT;
		goto put_ctx;
	}

	p->nchunks = cs->in.num_chunks;
	p->chunks = kmalloc_array(p->nchunks, sizeof(struct amdgpu_cs_chunk),
			    GFP_KERNEL);
	if (!p->chunks) {
		ret = -ENOMEM;
		goto put_ctx;
	}

	for (i = 0; i < p->nchunks; i++) {
		struct drm_amdgpu_cs_chunk __user **chunk_ptr = NULL;
		struct drm_amdgpu_cs_chunk user_chunk;
		uint32_t __user *cdata;

		chunk_ptr = (void __user *)(unsigned long)chunk_array[i];
		if (copy_from_user(&user_chunk, chunk_ptr,
				       sizeof(struct drm_amdgpu_cs_chunk))) {
			ret = -EFAULT;
			i--;
			goto free_partial_kdata;
		}
		p->chunks[i].chunk_id = user_chunk.chunk_id;
		p->chunks[i].length_dw = user_chunk.length_dw;

		size = p->chunks[i].length_dw;
		cdata = (void __user *)(unsigned long)user_chunk.chunk_data;

		p->chunks[i].kdata = drm_malloc_ab(size, sizeof(uint32_t));
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

		case AMDGPU_CHUNK_ID_DEPENDENCIES:
			break;

		default:
			ret = -EINVAL;
			goto free_partial_kdata;
		}
	}

	ret = amdgpu_job_alloc(p->adev, num_ibs, &p->job, vm);
	if (ret)
		goto free_all_kdata;

	if (p->uf_entry.robj)
		p->job->uf_addr = uf_offset;
	kfree(chunk_array);
	return 0;

free_all_kdata:
	i = p->nchunks - 1;
free_partial_kdata:
	for (; i >= 0; i--)
		drm_free_large(p->chunks[i].kdata);
	kfree(p->chunks);
put_ctx:
	amdgpu_ctx_put(p->ctx);
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
static u64 amdgpu_cs_get_threshold_for_moves(struct amdgpu_device *adev)
{
	s64 time_us, increment_us;
	u64 max_bytes;
	u64 free_vram, total_vram, used_vram;

	/* Allow a maximum of 200 accumulated ms. This is basically per-IB
	 * throttling.
	 *
	 * It means that in order to get full max MBps, at least 5 IBs per
	 * second must be submitted and not more than 200ms apart from each
	 * other.
	 */
	const s64 us_upper_bound = 200000;

	if (!adev->mm_stats.log2_max_MBps)
		return 0;

	total_vram = adev->mc.real_vram_size - adev->vram_pin_size;
	used_vram = atomic64_read(&adev->vram_usage);
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

	/* This returns 0 if the driver is in debt to disallow (optional)
	 * buffer moves.
	 */
	max_bytes = us_to_bytes(adev, adev->mm_stats.accum_us);

	spin_unlock(&adev->mm_stats.lock);
	return max_bytes;
}

/* Report how many bytes have really been moved for the last command
 * submission. This can result in a debt that can stop buffer migrations
 * temporarily.
 */
static void amdgpu_cs_report_moved_bytes(struct amdgpu_device *adev,
					 u64 num_bytes)
{
	spin_lock(&adev->mm_stats.lock);
	adev->mm_stats.accum_us -= bytes_to_us(adev, num_bytes);
	spin_unlock(&adev->mm_stats.lock);
}

static int amdgpu_cs_bo_validate(struct amdgpu_cs_parser *p,
				 struct amdgpu_bo *bo)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	u64 initial_bytes_moved;
	uint32_t domain;
	int r;

	if (bo->pin_count)
		return 0;

	/* Don't move this buffer if we have depleted our allowance
	 * to move it. Don't move anything if the threshold is zero.
	 */
	if (p->bytes_moved < p->bytes_moved_threshold)
		domain = bo->prefered_domains;
	else
		domain = bo->allowed_domains;

retry:
	amdgpu_ttm_placement_from_domain(bo, domain);
	initial_bytes_moved = atomic64_read(&adev->num_bytes_moved);
	r = ttm_bo_validate(&bo->tbo, &bo->placement, true, false);
	p->bytes_moved += atomic64_read(&adev->num_bytes_moved) -
		initial_bytes_moved;

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
	int r;

	if (!p->evictable)
		return false;

	for (;&p->evictable->tv.head != &p->validated;
	     p->evictable = list_prev_entry(p->evictable, tv.head)) {

		struct amdgpu_bo_list_entry *candidate = p->evictable;
		struct amdgpu_bo *bo = candidate->robj;
		struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
		u64 initial_bytes_moved;
		uint32_t other;

		/* If we reached our current BO we can forget it */
		if (candidate->robj == validated)
			break;

		other = amdgpu_mem_type_to_domain(bo->tbo.mem.mem_type);

		/* Check if this BO is in one of the domains we need space for */
		if (!(other & domain))
			continue;

		/* Check if we can move this BO somewhere else */
		other = bo->allowed_domains & ~domain;
		if (!other)
			continue;

		/* Good we can try to move this BO somewhere else */
		amdgpu_ttm_placement_from_domain(bo, other);
		initial_bytes_moved = atomic64_read(&adev->num_bytes_moved);
		r = ttm_bo_validate(&bo->tbo, &bo->placement, true, false);
		p->bytes_moved += atomic64_read(&adev->num_bytes_moved) -
			initial_bytes_moved;

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
		r = amdgpu_cs_bo_validate(p, bo);

	return r;
}

static int amdgpu_cs_list_validate(struct amdgpu_cs_parser *p,
			    struct list_head *validated)
{
	struct amdgpu_bo_list_entry *lobj;
	int r;

	list_for_each_entry(lobj, validated, tv.head) {
		struct amdgpu_bo *bo = lobj->robj;
		bool binding_userptr = false;
		struct mm_struct *usermm;

		usermm = amdgpu_ttm_tt_get_usermm(bo->tbo.ttm);
		if (usermm && usermm != current->mm)
			return -EPERM;

		/* Check if we have user pages and nobody bound the BO already */
		if (lobj->user_pages && bo->tbo.ttm->state != tt_bound) {
			size_t size = sizeof(struct page *);

			size *= bo->tbo.ttm->num_pages;
			memcpy(bo->tbo.ttm->pages, lobj->user_pages, size);
			binding_userptr = true;
		}

		if (p->evictable == lobj)
			p->evictable = NULL;

		r = amdgpu_cs_validate(p, bo);
		if (r)
			return r;

		if (binding_userptr) {
			drm_free_large(lobj->user_pages);
			lobj->user_pages = NULL;
		}
	}
	return 0;
}

static int amdgpu_cs_parser_bos(struct amdgpu_cs_parser *p,
				union drm_amdgpu_cs *cs)
{
	struct amdgpu_fpriv *fpriv = p->filp->driver_priv;
	struct amdgpu_bo_list_entry *e;
	struct list_head duplicates;
	bool need_mmap_lock = false;
	unsigned i, tries = 10;
	int r;

	INIT_LIST_HEAD(&p->validated);

	p->bo_list = amdgpu_bo_list_get(fpriv, cs->in.bo_list_handle);
	if (p->bo_list) {
		need_mmap_lock = p->bo_list->first_userptr !=
			p->bo_list->num_entries;
		amdgpu_bo_list_get_list(p->bo_list, &p->validated);
	}

	INIT_LIST_HEAD(&duplicates);
	amdgpu_vm_get_pd_bo(&fpriv->vm, &p->validated, &p->vm_pd);

	if (p->uf_entry.robj)
		list_add(&p->uf_entry.tv.head, &p->validated);

	if (need_mmap_lock)
		down_read(&current->mm->mmap_sem);

	while (1) {
		struct list_head need_pages;
		unsigned i;

		r = ttm_eu_reserve_buffers(&p->ticket, &p->validated, true,
					   &duplicates);
		if (unlikely(r != 0)) {
			if (r != -ERESTARTSYS)
				DRM_ERROR("ttm_eu_reserve_buffers failed.\n");
			goto error_free_pages;
		}

		/* Without a BO list we don't have userptr BOs */
		if (!p->bo_list)
			break;

		INIT_LIST_HEAD(&need_pages);
		for (i = p->bo_list->first_userptr;
		     i < p->bo_list->num_entries; ++i) {

			e = &p->bo_list->array[i];

			if (amdgpu_ttm_tt_userptr_invalidated(e->robj->tbo.ttm,
				 &e->user_invalidated) && e->user_pages) {

				/* We acquired a page array, but somebody
				 * invalidated it. Free it an try again
				 */
				release_pages(e->user_pages,
					      e->robj->tbo.ttm->num_pages,
					      false);
				drm_free_large(e->user_pages);
				e->user_pages = NULL;
			}

			if (e->robj->tbo.ttm->state != tt_bound &&
			    !e->user_pages) {
				list_del(&e->tv.head);
				list_add(&e->tv.head, &need_pages);

				amdgpu_bo_unreserve(e->robj);
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

		/* Fill the page arrays for all useptrs. */
		list_for_each_entry(e, &need_pages, tv.head) {
			struct ttm_tt *ttm = e->robj->tbo.ttm;

			e->user_pages = drm_calloc_large(ttm->num_pages,
							 sizeof(struct page*));
			if (!e->user_pages) {
				r = -ENOMEM;
				DRM_ERROR("calloc failure in %s\n", __func__);
				goto error_free_pages;
			}

			r = amdgpu_ttm_tt_get_user_pages(ttm, e->user_pages);
			if (r) {
				DRM_ERROR("amdgpu_ttm_tt_get_user_pages failed.\n");
				drm_free_large(e->user_pages);
				e->user_pages = NULL;
				goto error_free_pages;
			}
		}

		/* And try again. */
		list_splice(&need_pages, &p->validated);
	}

	p->bytes_moved_threshold = amdgpu_cs_get_threshold_for_moves(p->adev);
	p->bytes_moved = 0;
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

	amdgpu_cs_report_moved_bytes(p->adev, p->bytes_moved);

	fpriv->vm.last_eviction_counter =
		atomic64_read(&p->adev->num_evictions);

	if (p->bo_list) {
		struct amdgpu_bo *gds = p->bo_list->gds_obj;
		struct amdgpu_bo *gws = p->bo_list->gws_obj;
		struct amdgpu_bo *oa = p->bo_list->oa_obj;
		struct amdgpu_vm *vm = &fpriv->vm;
		unsigned i;

		for (i = 0; i < p->bo_list->num_entries; i++) {
			struct amdgpu_bo *bo = p->bo_list->array[i].robj;

			p->bo_list->array[i].bo_va = amdgpu_vm_bo_find(vm, bo);
		}

		if (gds) {
			p->job->gds_base = amdgpu_bo_gpu_offset(gds);
			p->job->gds_size = amdgpu_bo_size(gds);
		}
		if (gws) {
			p->job->gws_base = amdgpu_bo_gpu_offset(gws);
			p->job->gws_size = amdgpu_bo_size(gws);
		}
		if (oa) {
			p->job->oa_base = amdgpu_bo_gpu_offset(oa);
			p->job->oa_size = amdgpu_bo_size(oa);
		}
	}

	if (!r && p->uf_entry.robj) {
		struct amdgpu_bo *uf = p->uf_entry.robj;

		r = amdgpu_ttm_bind(&uf->tbo, &uf->tbo.mem);
		p->job->uf_addr += amdgpu_bo_gpu_offset(uf);
	}

error_validate:
	if (r) {
		amdgpu_vm_move_pt_bos_in_lru(p->adev, &fpriv->vm);
		ttm_eu_backoff_reservation(&p->ticket, &p->validated);
	}

error_free_pages:

	if (need_mmap_lock)
		up_read(&current->mm->mmap_sem);

	if (p->bo_list) {
		for (i = p->bo_list->first_userptr;
		     i < p->bo_list->num_entries; ++i) {
			e = &p->bo_list->array[i];

			if (!e->user_pages)
				continue;

			release_pages(e->user_pages,
				      e->robj->tbo.ttm->num_pages,
				      false);
			drm_free_large(e->user_pages);
		}
	}

	return r;
}

static int amdgpu_cs_sync_rings(struct amdgpu_cs_parser *p)
{
	struct amdgpu_bo_list_entry *e;
	int r;

	list_for_each_entry(e, &p->validated, tv.head) {
		struct reservation_object *resv = e->robj->tbo.resv;
		r = amdgpu_sync_resv(p->adev, &p->job->sync, resv, p->filp);

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
static void amdgpu_cs_parser_fini(struct amdgpu_cs_parser *parser, int error, bool backoff)
{
	struct amdgpu_fpriv *fpriv = parser->filp->driver_priv;
	unsigned i;

	if (!error) {
		amdgpu_vm_move_pt_bos_in_lru(parser->adev, &fpriv->vm);

		ttm_eu_fence_buffer_objects(&parser->ticket,
					    &parser->validated,
					    parser->fence);
	} else if (backoff) {
		ttm_eu_backoff_reservation(&parser->ticket,
					   &parser->validated);
	}
	dma_fence_put(parser->fence);

	if (parser->ctx)
		amdgpu_ctx_put(parser->ctx);
	if (parser->bo_list)
		amdgpu_bo_list_put(parser->bo_list);

	for (i = 0; i < parser->nchunks; i++)
		drm_free_large(parser->chunks[i].kdata);
	kfree(parser->chunks);
	if (parser->job)
		amdgpu_job_free(parser->job);
	amdgpu_bo_unref(&parser->uf_entry.robj);
}

static int amdgpu_bo_vm_update_pte(struct amdgpu_cs_parser *p,
				   struct amdgpu_vm *vm)
{
	struct amdgpu_device *adev = p->adev;
	struct amdgpu_bo_va *bo_va;
	struct amdgpu_bo *bo;
	int i, r;

	r = amdgpu_vm_update_page_directory(adev, vm);
	if (r)
		return r;

	r = amdgpu_sync_fence(adev, &p->job->sync, vm->page_directory_fence);
	if (r)
		return r;

	r = amdgpu_vm_clear_freed(adev, vm);
	if (r)
		return r;

	if (p->bo_list) {
		for (i = 0; i < p->bo_list->num_entries; i++) {
			struct dma_fence *f;

			/* ignore duplicates */
			bo = p->bo_list->array[i].robj;
			if (!bo)
				continue;

			bo_va = p->bo_list->array[i].bo_va;
			if (bo_va == NULL)
				continue;

			r = amdgpu_vm_bo_update(adev, bo_va, false);
			if (r)
				return r;

			f = bo_va->last_pt_update;
			r = amdgpu_sync_fence(adev, &p->job->sync, f);
			if (r)
				return r;
		}

	}

	r = amdgpu_vm_clear_invalids(adev, vm, &p->job->sync);

	if (amdgpu_vm_debug && p->bo_list) {
		/* Invalidate all BOs to test for userspace bugs */
		for (i = 0; i < p->bo_list->num_entries; i++) {
			/* ignore duplicates */
			bo = p->bo_list->array[i].robj;
			if (!bo)
				continue;

			amdgpu_vm_bo_invalidate(adev, bo);
		}
	}

	return r;
}

static int amdgpu_cs_ib_vm_chunk(struct amdgpu_device *adev,
				 struct amdgpu_cs_parser *p)
{
	struct amdgpu_fpriv *fpriv = p->filp->driver_priv;
	struct amdgpu_vm *vm = &fpriv->vm;
	struct amdgpu_ring *ring = p->job->ring;
	int i, r;

	/* Only for UVD/VCE VM emulation */
	if (ring->funcs->parse_cs) {
		for (i = 0; i < p->job->num_ibs; i++) {
			r = amdgpu_ring_parse_cs(ring, p, i);
			if (r)
				return r;
		}
	}

	if (p->job->vm) {
		p->job->vm_pd_addr = amdgpu_bo_gpu_offset(vm->page_directory);

		r = amdgpu_bo_vm_update_pte(p, vm);
		if (r)
			return r;
	}

	return amdgpu_cs_sync_rings(p);
}

static int amdgpu_cs_handle_lockup(struct amdgpu_device *adev, int r)
{
	if (r == -EDEADLK) {
		r = amdgpu_gpu_reset(adev);
		if (!r)
			r = -EAGAIN;
	}
	return r;
}

static int amdgpu_cs_ib_fill(struct amdgpu_device *adev,
			     struct amdgpu_cs_parser *parser)
{
	struct amdgpu_fpriv *fpriv = parser->filp->driver_priv;
	struct amdgpu_vm *vm = &fpriv->vm;
	int i, j;
	int r;

	for (i = 0, j = 0; i < parser->nchunks && j < parser->job->num_ibs; i++) {
		struct amdgpu_cs_chunk *chunk;
		struct amdgpu_ib *ib;
		struct drm_amdgpu_cs_chunk_ib *chunk_ib;
		struct amdgpu_ring *ring;

		chunk = &parser->chunks[i];
		ib = &parser->job->ibs[j];
		chunk_ib = (struct drm_amdgpu_cs_chunk_ib *)chunk->kdata;

		if (chunk->chunk_id != AMDGPU_CHUNK_ID_IB)
			continue;

		r = amdgpu_cs_get_ring(adev, chunk_ib->ip_type,
				       chunk_ib->ip_instance, chunk_ib->ring,
				       &ring);
		if (r)
			return r;

		if (ib->flags & AMDGPU_IB_FLAG_PREAMBLE) {
			parser->job->preamble_status |= AMDGPU_PREAMBLE_IB_PRESENT;
			if (!parser->ctx->preamble_presented) {
				parser->job->preamble_status |= AMDGPU_PREAMBLE_IB_PRESENT_FIRST;
				parser->ctx->preamble_presented = true;
			}
		}

		if (parser->job->ring && parser->job->ring != ring)
			return -EINVAL;

		parser->job->ring = ring;

		if (ring->funcs->parse_cs) {
			struct amdgpu_bo_va_mapping *m;
			struct amdgpu_bo *aobj = NULL;
			uint64_t offset;
			uint8_t *kptr;

			m = amdgpu_cs_find_mapping(parser, chunk_ib->va_start,
						   &aobj);
			if (!aobj) {
				DRM_ERROR("IB va_start is invalid\n");
				return -EINVAL;
			}

			if ((chunk_ib->va_start + chunk_ib->ib_bytes) >
			    (m->it.last + 1) * AMDGPU_GPU_PAGE_SIZE) {
				DRM_ERROR("IB va_start+ib_bytes is invalid\n");
				return -EINVAL;
			}

			/* the IB should be reserved at this point */
			r = amdgpu_bo_kmap(aobj, (void **)&kptr);
			if (r) {
				return r;
			}

			offset = ((uint64_t)m->it.start) * AMDGPU_GPU_PAGE_SIZE;
			kptr += chunk_ib->va_start - offset;

			r =  amdgpu_ib_get(adev, vm, chunk_ib->ib_bytes, ib);
			if (r) {
				DRM_ERROR("Failed to get ib !\n");
				return r;
			}

			memcpy(ib->ptr, kptr, chunk_ib->ib_bytes);
			amdgpu_bo_kunmap(aobj);
		} else {
			r =  amdgpu_ib_get(adev, vm, 0, ib);
			if (r) {
				DRM_ERROR("Failed to get ib !\n");
				return r;
			}

		}

		ib->gpu_addr = chunk_ib->va_start;
		ib->length_dw = chunk_ib->ib_bytes / 4;
		ib->flags = chunk_ib->flags;
		j++;
	}

	/* UVD & VCE fw doesn't support user fences */
	if (parser->job->uf_addr && (
	    parser->job->ring->funcs->type == AMDGPU_RING_TYPE_UVD ||
	    parser->job->ring->funcs->type == AMDGPU_RING_TYPE_VCE))
		return -EINVAL;

	return 0;
}

static int amdgpu_cs_dependencies(struct amdgpu_device *adev,
				  struct amdgpu_cs_parser *p)
{
	struct amdgpu_fpriv *fpriv = p->filp->driver_priv;
	int i, j, r;

	for (i = 0; i < p->nchunks; ++i) {
		struct drm_amdgpu_cs_chunk_dep *deps;
		struct amdgpu_cs_chunk *chunk;
		unsigned num_deps;

		chunk = &p->chunks[i];

		if (chunk->chunk_id != AMDGPU_CHUNK_ID_DEPENDENCIES)
			continue;

		deps = (struct drm_amdgpu_cs_chunk_dep *)chunk->kdata;
		num_deps = chunk->length_dw * 4 /
			sizeof(struct drm_amdgpu_cs_chunk_dep);

		for (j = 0; j < num_deps; ++j) {
			struct amdgpu_ring *ring;
			struct amdgpu_ctx *ctx;
			struct dma_fence *fence;

			r = amdgpu_cs_get_ring(adev, deps[j].ip_type,
					       deps[j].ip_instance,
					       deps[j].ring, &ring);
			if (r)
				return r;

			ctx = amdgpu_ctx_get(fpriv, deps[j].ctx_id);
			if (ctx == NULL)
				return -EINVAL;

			fence = amdgpu_ctx_get_fence(ctx, ring,
						     deps[j].handle);
			if (IS_ERR(fence)) {
				r = PTR_ERR(fence);
				amdgpu_ctx_put(ctx);
				return r;

			} else if (fence) {
				r = amdgpu_sync_fence(adev, &p->job->sync,
						      fence);
				dma_fence_put(fence);
				amdgpu_ctx_put(ctx);
				if (r)
					return r;
			}
		}
	}

	return 0;
}

static int amdgpu_cs_submit(struct amdgpu_cs_parser *p,
			    union drm_amdgpu_cs *cs)
{
	struct amdgpu_ring *ring = p->job->ring;
	struct amd_sched_entity *entity = &p->ctx->rings[ring->idx].entity;
	struct amdgpu_job *job;
	int r;

	job = p->job;
	p->job = NULL;

	r = amd_sched_job_init(&job->base, &ring->sched, entity, p->filp);
	if (r) {
		amdgpu_job_free(job);
		return r;
	}

	job->owner = p->filp;
	job->fence_ctx = entity->fence_context;
	p->fence = dma_fence_get(&job->base.s_fence->finished);
	cs->out.handle = amdgpu_ctx_add_fence(p->ctx, ring, p->fence);
	job->uf_sequence = cs->out.handle;
	amdgpu_job_free_resources(job);

	trace_amdgpu_cs_ioctl(job);
	amd_sched_entity_push_job(&job->base);

	return 0;
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
		DRM_ERROR("Failed to initialize parser !\n");
		amdgpu_cs_parser_fini(&parser, r, false);
		r = amdgpu_cs_handle_lockup(adev, r);
		return r;
	}
	r = amdgpu_cs_parser_bos(&parser, data);
	if (r == -ENOMEM)
		DRM_ERROR("Not enough memory for command submission!\n");
	else if (r && r != -ERESTARTSYS)
		DRM_ERROR("Failed to process the buffer list %d!\n", r);
	else if (!r) {
		reserved_buffers = true;
		r = amdgpu_cs_ib_fill(adev, &parser);
	}

	if (!r) {
		r = amdgpu_cs_dependencies(adev, &parser);
		if (r)
			DRM_ERROR("Failed in the dependencies handling %d!\n", r);
	}

	if (r)
		goto out;

	for (i = 0; i < parser.job->num_ibs; i++)
		trace_amdgpu_cs(&parser, i);

	r = amdgpu_cs_ib_vm_chunk(adev, &parser);
	if (r)
		goto out;

	r = amdgpu_cs_submit(&parser, cs);

out:
	amdgpu_cs_parser_fini(&parser, r, reserved_buffers);
	r = amdgpu_cs_handle_lockup(adev, r);
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
	struct amdgpu_device *adev = dev->dev_private;
	unsigned long timeout = amdgpu_gem_timeout(wait->in.timeout);
	struct amdgpu_ring *ring = NULL;
	struct amdgpu_ctx *ctx;
	struct dma_fence *fence;
	long r;

	r = amdgpu_cs_get_ring(adev, wait->in.ip_type, wait->in.ip_instance,
			       wait->in.ring, &ring);
	if (r)
		return r;

	ctx = amdgpu_ctx_get(filp->driver_priv, wait->in.ctx_id);
	if (ctx == NULL)
		return -EINVAL;

	fence = amdgpu_ctx_get_fence(ctx, ring, wait->in.handle);
	if (IS_ERR(fence))
		r = PTR_ERR(fence);
	else if (fence) {
		r = dma_fence_wait_timeout(fence, true, timeout);
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
	struct amdgpu_ring *ring;
	struct amdgpu_ctx *ctx;
	struct dma_fence *fence;
	int r;

	r = amdgpu_cs_get_ring(adev, user->ip_type, user->ip_instance,
			       user->ring, &ring);
	if (r)
		return ERR_PTR(r);

	ctx = amdgpu_ctx_get(filp->driver_priv, user->ctx_id);
	if (ctx == NULL)
		return ERR_PTR(-EINVAL);

	fence = amdgpu_ctx_get_fence(ctx, ring, user->seq_no);
	amdgpu_ctx_put(ctx);

	return fence;
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
		if (r < 0)
			return r;

		if (r == 0)
			break;
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
	/* set return value 0 to indicate success */
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

	fences_user = (void __user *)(unsigned long)(wait->in.fences);
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
struct amdgpu_bo_va_mapping *
amdgpu_cs_find_mapping(struct amdgpu_cs_parser *parser,
		       uint64_t addr, struct amdgpu_bo **bo)
{
	struct amdgpu_bo_va_mapping *mapping;
	unsigned i;

	if (!parser->bo_list)
		return NULL;

	addr /= AMDGPU_GPU_PAGE_SIZE;

	for (i = 0; i < parser->bo_list->num_entries; i++) {
		struct amdgpu_bo_list_entry *lobj;

		lobj = &parser->bo_list->array[i];
		if (!lobj->bo_va)
			continue;

		list_for_each_entry(mapping, &lobj->bo_va->valids, list) {
			if (mapping->it.start > addr ||
			    addr > mapping->it.last)
				continue;

			*bo = lobj->bo_va->bo;
			return mapping;
		}

		list_for_each_entry(mapping, &lobj->bo_va->invalids, list) {
			if (mapping->it.start > addr ||
			    addr > mapping->it.last)
				continue;

			*bo = lobj->bo_va->bo;
			return mapping;
		}
	}

	return NULL;
}

/**
 * amdgpu_cs_sysvm_access_required - make BOs accessible by the system VM
 *
 * @parser: command submission parser context
 *
 * Helper for UVD/VCE VM emulation, make sure BOs are accessible by the system VM.
 */
int amdgpu_cs_sysvm_access_required(struct amdgpu_cs_parser *parser)
{
	unsigned i;
	int r;

	if (!parser->bo_list)
		return 0;

	for (i = 0; i < parser->bo_list->num_entries; i++) {
		struct amdgpu_bo *bo = parser->bo_list->array[i].robj;

		r = amdgpu_ttm_bind(&bo->tbo, &bo->tbo.mem);
		if (unlikely(r))
			return r;

		if (bo->flags & AMDGPU_GEM_CREATE_VRAM_CONTIGUOUS)
			continue;

		bo->flags |= AMDGPU_GEM_CREATE_VRAM_CONTIGUOUS;
		amdgpu_ttm_placement_from_domain(bo, bo->allowed_domains);
		r = ttm_bo_validate(&bo->tbo, &bo->placement, false, false);
		if (unlikely(r))
			return r;
	}

	return 0;
}
