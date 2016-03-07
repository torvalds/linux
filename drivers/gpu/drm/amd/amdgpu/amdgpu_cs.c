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
#include <linux/list_sort.h>
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
				      struct amdgpu_user_fence *uf,
				      struct drm_amdgpu_cs_chunk_fence *fence_data)
{
	struct drm_gem_object *gobj;
	uint32_t handle;

	handle = fence_data->handle;
	gobj = drm_gem_object_lookup(p->adev->ddev, p->filp,
				     fence_data->handle);
	if (gobj == NULL)
		return -EINVAL;

	uf->bo = amdgpu_bo_ref(gem_to_amdgpu_bo(gobj));
	uf->offset = fence_data->offset;

	if (amdgpu_ttm_tt_get_usermm(uf->bo->tbo.ttm)) {
		drm_gem_object_unreference_unlocked(gobj);
		return -EINVAL;
	}

	p->uf_entry.robj = amdgpu_bo_ref(uf->bo);
	p->uf_entry.priority = 0;
	p->uf_entry.tv.bo = &p->uf_entry.robj->tbo;
	p->uf_entry.tv.shared = true;

	drm_gem_object_unreference_unlocked(gobj);
	return 0;
}

int amdgpu_cs_parser_init(struct amdgpu_cs_parser *p, void *data)
{
	struct amdgpu_fpriv *fpriv = p->filp->driver_priv;
	union drm_amdgpu_cs *cs = data;
	uint64_t *chunk_array_user;
	uint64_t *chunk_array;
	struct amdgpu_user_fence uf = {};
	unsigned size, num_ibs = 0;
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

			ret = amdgpu_cs_user_fence_chunk(p, &uf, (void *)p->chunks[i].kdata);
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

	ret = amdgpu_job_alloc(p->adev, num_ibs, &p->job);
	if (ret)
		goto free_all_kdata;

	p->job->uf = uf;

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

/* Returns how many bytes TTM can move per IB.
 */
static u64 amdgpu_cs_get_threshold_for_moves(struct amdgpu_device *adev)
{
	u64 real_vram_size = adev->mc.real_vram_size;
	u64 vram_usage = atomic64_read(&adev->vram_usage);

	/* This function is based on the current VRAM usage.
	 *
	 * - If all of VRAM is free, allow relocating the number of bytes that
	 *   is equal to 1/4 of the size of VRAM for this IB.

	 * - If more than one half of VRAM is occupied, only allow relocating
	 *   1 MB of data for this IB.
	 *
	 * - From 0 to one half of used VRAM, the threshold decreases
	 *   linearly.
	 *         __________________
	 * 1/4 of -|\               |
	 * VRAM    | \              |
	 *         |  \             |
	 *         |   \            |
	 *         |    \           |
	 *         |     \          |
	 *         |      \         |
	 *         |       \________|1 MB
	 *         |----------------|
	 *    VRAM 0 %             100 %
	 *         used            used
	 *
	 * Note: It's a threshold, not a limit. The threshold must be crossed
	 * for buffer relocations to stop, so any buffer of an arbitrary size
	 * can be moved as long as the threshold isn't crossed before
	 * the relocation takes place. We don't want to disable buffer
	 * relocations completely.
	 *
	 * The idea is that buffers should be placed in VRAM at creation time
	 * and TTM should only do a minimum number of relocations during
	 * command submission. In practice, you need to submit at least
	 * a dozen IBs to move all buffers to VRAM if they are in GTT.
	 *
	 * Also, things can get pretty crazy under memory pressure and actual
	 * VRAM usage can change a lot, so playing safe even at 50% does
	 * consistently increase performance.
	 */

	u64 half_vram = real_vram_size >> 1;
	u64 half_free_vram = vram_usage >= half_vram ? 0 : half_vram - vram_usage;
	u64 bytes_moved_threshold = half_free_vram >> 1;
	return max(bytes_moved_threshold, 1024*1024ull);
}

int amdgpu_cs_list_validate(struct amdgpu_cs_parser *p,
			    struct list_head *validated)
{
	struct amdgpu_bo_list_entry *lobj;
	u64 initial_bytes_moved;
	int r;

	list_for_each_entry(lobj, validated, tv.head) {
		struct amdgpu_bo *bo = lobj->robj;
		struct mm_struct *usermm;
		uint32_t domain;

		usermm = amdgpu_ttm_tt_get_usermm(bo->tbo.ttm);
		if (usermm && usermm != current->mm)
			return -EPERM;

		if (bo->pin_count)
			continue;

		/* Avoid moving this one if we have moved too many buffers
		 * for this IB already.
		 *
		 * Note that this allows moving at least one buffer of
		 * any size, because it doesn't take the current "bo"
		 * into account. We don't want to disallow buffer moves
		 * completely.
		 */
		if (p->bytes_moved <= p->bytes_moved_threshold)
			domain = bo->prefered_domains;
		else
			domain = bo->allowed_domains;

	retry:
		amdgpu_ttm_placement_from_domain(bo, domain);
		initial_bytes_moved = atomic64_read(&bo->adev->num_bytes_moved);
		r = ttm_bo_validate(&bo->tbo, &bo->placement, true, false);
		p->bytes_moved += atomic64_read(&bo->adev->num_bytes_moved) -
			       initial_bytes_moved;

		if (unlikely(r)) {
			if (r != -ERESTARTSYS && domain != bo->allowed_domains) {
				domain = bo->allowed_domains;
				goto retry;
			}
			return r;
		}
	}
	return 0;
}

static int amdgpu_cs_parser_bos(struct amdgpu_cs_parser *p,
				union drm_amdgpu_cs *cs)
{
	struct amdgpu_fpriv *fpriv = p->filp->driver_priv;
	struct list_head duplicates;
	bool need_mmap_lock = false;
	int r;

	INIT_LIST_HEAD(&p->validated);

	p->bo_list = amdgpu_bo_list_get(fpriv, cs->in.bo_list_handle);
	if (p->bo_list) {
		need_mmap_lock = p->bo_list->has_userptr;
		amdgpu_bo_list_get_list(p->bo_list, &p->validated);
	}

	INIT_LIST_HEAD(&duplicates);
	amdgpu_vm_get_pd_bo(&fpriv->vm, &p->validated, &p->vm_pd);

	if (p->job->uf.bo)
		list_add(&p->uf_entry.tv.head, &p->validated);

	if (need_mmap_lock)
		down_read(&current->mm->mmap_sem);

	r = ttm_eu_reserve_buffers(&p->ticket, &p->validated, true, &duplicates);
	if (unlikely(r != 0))
		goto error_reserve;

	amdgpu_vm_get_pt_bos(&fpriv->vm, &duplicates);

	p->bytes_moved_threshold = amdgpu_cs_get_threshold_for_moves(p->adev);
	p->bytes_moved = 0;

	r = amdgpu_cs_list_validate(p, &duplicates);
	if (r)
		goto error_validate;

	r = amdgpu_cs_list_validate(p, &p->validated);
	if (r)
		goto error_validate;

	if (p->bo_list) {
		struct amdgpu_vm *vm = &fpriv->vm;
		unsigned i;

		for (i = 0; i < p->bo_list->num_entries; i++) {
			struct amdgpu_bo *bo = p->bo_list->array[i].robj;

			p->bo_list->array[i].bo_va = amdgpu_vm_bo_find(vm, bo);
		}
	}

error_validate:
	if (r) {
		amdgpu_vm_move_pt_bos_in_lru(p->adev, &fpriv->vm);
		ttm_eu_backoff_reservation(&p->ticket, &p->validated);
	}

error_reserve:
	if (need_mmap_lock)
		up_read(&current->mm->mmap_sem);

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

static int cmp_size_smaller_first(void *priv, struct list_head *a,
				  struct list_head *b)
{
	struct amdgpu_bo_list_entry *la = list_entry(a, struct amdgpu_bo_list_entry, tv.head);
	struct amdgpu_bo_list_entry *lb = list_entry(b, struct amdgpu_bo_list_entry, tv.head);

	/* Sort A before B if A is smaller. */
	return (int)la->robj->tbo.num_pages - (int)lb->robj->tbo.num_pages;
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

		/* Sort the buffer list from the smallest to largest buffer,
		 * which affects the order of buffers in the LRU list.
		 * This assures that the smallest buffers are added first
		 * to the LRU list, so they are likely to be later evicted
		 * first, instead of large buffers whose eviction is more
		 * expensive.
		 *
		 * This slightly lowers the number of bytes moved by TTM
		 * per frame under memory pressure.
		 */
		list_sort(NULL, &parser->validated, cmp_size_smaller_first);

		ttm_eu_fence_buffer_objects(&parser->ticket,
					    &parser->validated,
					    parser->fence);
	} else if (backoff) {
		ttm_eu_backoff_reservation(&parser->ticket,
					   &parser->validated);
	}
	fence_put(parser->fence);

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
			struct fence *f;

			/* ignore duplicates */
			bo = p->bo_list->array[i].robj;
			if (!bo)
				continue;

			bo_va = p->bo_list->array[i].bo_va;
			if (bo_va == NULL)
				continue;

			r = amdgpu_vm_bo_update(adev, bo_va, &bo->tbo.mem);
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

	r = amdgpu_bo_vm_update_pte(p, vm);
	if (!r)
		amdgpu_cs_sync_rings(p);

	return r;
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

			r =  amdgpu_ib_get(adev, NULL, chunk_ib->ib_bytes, ib);
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

			ib->gpu_addr = chunk_ib->va_start;
		}

		ib->length_dw = chunk_ib->ib_bytes / 4;
		ib->flags = chunk_ib->flags;
		ib->ctx = parser->ctx;
		j++;
	}

	/* add GDS resources to first IB */
	if (parser->bo_list) {
		struct amdgpu_bo *gds = parser->bo_list->gds_obj;
		struct amdgpu_bo *gws = parser->bo_list->gws_obj;
		struct amdgpu_bo *oa = parser->bo_list->oa_obj;
		struct amdgpu_ib *ib = &parser->job->ibs[0];

		if (gds) {
			ib->gds_base = amdgpu_bo_gpu_offset(gds);
			ib->gds_size = amdgpu_bo_size(gds);
		}
		if (gws) {
			ib->gws_base = amdgpu_bo_gpu_offset(gws);
			ib->gws_size = amdgpu_bo_size(gws);
		}
		if (oa) {
			ib->oa_base = amdgpu_bo_gpu_offset(oa);
			ib->oa_size = amdgpu_bo_size(oa);
		}
	}
	/* wrap the last IB with user fence */
	if (parser->job->uf.bo) {
		struct amdgpu_ib *ib = &parser->job->ibs[parser->job->num_ibs - 1];

		/* UVD & VCE fw doesn't support user fences */
		if (parser->job->ring->type == AMDGPU_RING_TYPE_UVD ||
		    parser->job->ring->type == AMDGPU_RING_TYPE_VCE)
			return -EINVAL;

		ib->user = &parser->job->uf;
	}

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
			struct fence *fence;

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
				fence_put(fence);
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
	struct amd_sched_fence *fence;
	struct amdgpu_job *job;

	job = p->job;
	p->job = NULL;

	job->base.sched = &ring->sched;
	job->base.s_entity = &p->ctx->rings[ring->idx].entity;
	job->owner = p->filp;

	fence = amd_sched_fence_create(job->base.s_entity, p->filp);
	if (!fence) {
		amdgpu_job_free(job);
		return -ENOMEM;
	}

	job->base.s_fence = fence;
	p->fence = fence_get(&fence->base);

	cs->out.handle = amdgpu_ctx_add_fence(p->ctx, ring,
					      &fence->base);
	job->ibs[job->num_ibs - 1].sequence = cs->out.handle;

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
	struct fence *fence;
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
		r = fence_wait_timeout(fence, true, timeout);
		fence_put(fence);
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
