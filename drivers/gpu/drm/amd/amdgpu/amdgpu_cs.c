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

#define AMDGPU_CS_MAX_PRIORITY		32u
#define AMDGPU_CS_NUM_BUCKETS		(AMDGPU_CS_MAX_PRIORITY + 1)

/* This is based on the bucket sort with O(n) time complexity.
 * An item with priority "i" is added to bucket[i]. The lists are then
 * concatenated in descending order.
 */
struct amdgpu_cs_buckets {
	struct list_head bucket[AMDGPU_CS_NUM_BUCKETS];
};

static void amdgpu_cs_buckets_init(struct amdgpu_cs_buckets *b)
{
	unsigned i;

	for (i = 0; i < AMDGPU_CS_NUM_BUCKETS; i++)
		INIT_LIST_HEAD(&b->bucket[i]);
}

static void amdgpu_cs_buckets_add(struct amdgpu_cs_buckets *b,
				  struct list_head *item, unsigned priority)
{
	/* Since buffers which appear sooner in the relocation list are
	 * likely to be used more often than buffers which appear later
	 * in the list, the sort mustn't change the ordering of buffers
	 * with the same priority, i.e. it must be stable.
	 */
	list_add_tail(item, &b->bucket[min(priority, AMDGPU_CS_MAX_PRIORITY)]);
}

static void amdgpu_cs_buckets_get_list(struct amdgpu_cs_buckets *b,
				       struct list_head *out_list)
{
	unsigned i;

	/* Connect the sorted buckets in the output list. */
	for (i = 0; i < AMDGPU_CS_NUM_BUCKETS; i++) {
		list_splice(&b->bucket[i], out_list);
	}
}

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
		if (ring < 2) {
			*out_ring = &adev->sdma[ring].ring;
		} else {
			DRM_ERROR("only two SDMA rings are supported\n");
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

int amdgpu_cs_parser_init(struct amdgpu_cs_parser *p, void *data)
{
	union drm_amdgpu_cs *cs = data;
	uint64_t *chunk_array_user;
	uint64_t *chunk_array = NULL;
	struct amdgpu_fpriv *fpriv = p->filp->driver_priv;
	unsigned size, i;
	int r = 0;

	if (!cs->in.num_chunks)
		goto out;

	p->ctx = amdgpu_ctx_get(fpriv, cs->in.ctx_id);
	if (!p->ctx) {
		r = -EINVAL;
		goto out;
	}
	p->bo_list = amdgpu_bo_list_get(fpriv, cs->in.bo_list_handle);

	/* get chunks */
	INIT_LIST_HEAD(&p->validated);
	chunk_array = kcalloc(cs->in.num_chunks, sizeof(uint64_t), GFP_KERNEL);
	if (chunk_array == NULL) {
		r = -ENOMEM;
		goto out;
	}

	chunk_array_user = (uint64_t *)(unsigned long)(cs->in.chunks);
	if (copy_from_user(chunk_array, chunk_array_user,
			   sizeof(uint64_t)*cs->in.num_chunks)) {
		r = -EFAULT;
		goto out;
	}

	p->nchunks = cs->in.num_chunks;
	p->chunks = kcalloc(p->nchunks, sizeof(struct amdgpu_cs_chunk),
			    GFP_KERNEL);
	if (p->chunks == NULL) {
		r = -ENOMEM;
		goto out;
	}

	for (i = 0; i < p->nchunks; i++) {
		struct drm_amdgpu_cs_chunk __user **chunk_ptr = NULL;
		struct drm_amdgpu_cs_chunk user_chunk;
		uint32_t __user *cdata;

		chunk_ptr = (void __user *)(unsigned long)chunk_array[i];
		if (copy_from_user(&user_chunk, chunk_ptr,
				       sizeof(struct drm_amdgpu_cs_chunk))) {
			r = -EFAULT;
			goto out;
		}
		p->chunks[i].chunk_id = user_chunk.chunk_id;
		p->chunks[i].length_dw = user_chunk.length_dw;

		size = p->chunks[i].length_dw;
		cdata = (void __user *)(unsigned long)user_chunk.chunk_data;
		p->chunks[i].user_ptr = cdata;

		p->chunks[i].kdata = drm_malloc_ab(size, sizeof(uint32_t));
		if (p->chunks[i].kdata == NULL) {
			r = -ENOMEM;
			goto out;
		}
		size *= sizeof(uint32_t);
		if (copy_from_user(p->chunks[i].kdata, cdata, size)) {
			r = -EFAULT;
			goto out;
		}

		switch (p->chunks[i].chunk_id) {
		case AMDGPU_CHUNK_ID_IB:
			p->num_ibs++;
			break;

		case AMDGPU_CHUNK_ID_FENCE:
			size = sizeof(struct drm_amdgpu_cs_chunk_fence);
			if (p->chunks[i].length_dw * sizeof(uint32_t) >= size) {
				uint32_t handle;
				struct drm_gem_object *gobj;
				struct drm_amdgpu_cs_chunk_fence *fence_data;

				fence_data = (void *)p->chunks[i].kdata;
				handle = fence_data->handle;
				gobj = drm_gem_object_lookup(p->adev->ddev,
							     p->filp, handle);
				if (gobj == NULL) {
					r = -EINVAL;
					goto out;
				}

				p->uf.bo = gem_to_amdgpu_bo(gobj);
				p->uf.offset = fence_data->offset;
			} else {
				r = -EINVAL;
				goto out;
			}
			break;

		case AMDGPU_CHUNK_ID_DEPENDENCIES:
			break;

		default:
			r = -EINVAL;
			goto out;
		}
	}

	p->ibs = kcalloc(p->num_ibs, sizeof(struct amdgpu_ib), GFP_KERNEL);
	if (!p->ibs) {
		r = -ENOMEM;
		goto out;
	}

out:
	kfree(chunk_array);
	return r;
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

int amdgpu_cs_list_validate(struct amdgpu_cs_parser *p)
{
	struct amdgpu_fpriv *fpriv = p->filp->driver_priv;
	struct amdgpu_vm *vm = &fpriv->vm;
	struct amdgpu_device *adev = p->adev;
	struct amdgpu_bo_list_entry *lobj;
	struct list_head duplicates;
	struct amdgpu_bo *bo;
	u64 bytes_moved = 0, initial_bytes_moved;
	u64 bytes_moved_threshold = amdgpu_cs_get_threshold_for_moves(adev);
	int r;

	INIT_LIST_HEAD(&duplicates);
	r = ttm_eu_reserve_buffers(&p->ticket, &p->validated, true, &duplicates);
	if (unlikely(r != 0)) {
		return r;
	}

	list_for_each_entry(lobj, &p->validated, tv.head) {
		bo = lobj->robj;
		if (!bo->pin_count) {
			u32 domain = lobj->prefered_domains;
			u32 current_domain =
				amdgpu_mem_type_to_domain(bo->tbo.mem.mem_type);

			/* Check if this buffer will be moved and don't move it
			 * if we have moved too many buffers for this IB already.
			 *
			 * Note that this allows moving at least one buffer of
			 * any size, because it doesn't take the current "bo"
			 * into account. We don't want to disallow buffer moves
			 * completely.
			 */
			if (current_domain != AMDGPU_GEM_DOMAIN_CPU &&
			    (domain & current_domain) == 0 && /* will be moved */
			    bytes_moved > bytes_moved_threshold) {
				/* don't move it */
				domain = current_domain;
			}

		retry:
			amdgpu_ttm_placement_from_domain(bo, domain);
			initial_bytes_moved = atomic64_read(&adev->num_bytes_moved);
			r = ttm_bo_validate(&bo->tbo, &bo->placement, true, false);
			bytes_moved += atomic64_read(&adev->num_bytes_moved) -
				       initial_bytes_moved;

			if (unlikely(r)) {
				if (r != -ERESTARTSYS && domain != lobj->allowed_domains) {
					domain = lobj->allowed_domains;
					goto retry;
				}
				ttm_eu_backoff_reservation(&p->ticket, &p->validated);
				return r;
			}
		}
		lobj->bo_va = amdgpu_vm_bo_find(vm, bo);
	}
	return 0;
}

static int amdgpu_cs_parser_relocs(struct amdgpu_cs_parser *p)
{
	struct amdgpu_fpriv *fpriv = p->filp->driver_priv;
	struct amdgpu_cs_buckets buckets;
	bool need_mmap_lock = false;
	int i, r;

	if (p->bo_list) {
		need_mmap_lock = p->bo_list->has_userptr;
		amdgpu_cs_buckets_init(&buckets);
		for (i = 0; i < p->bo_list->num_entries; i++)
			amdgpu_cs_buckets_add(&buckets, &p->bo_list->array[i].tv.head,
								  p->bo_list->array[i].priority);

		amdgpu_cs_buckets_get_list(&buckets, &p->validated);
	}

	p->vm_bos = amdgpu_vm_get_bos(p->adev, &fpriv->vm,
				      &p->validated);

	if (need_mmap_lock)
		down_read(&current->mm->mmap_sem);

	r = amdgpu_cs_list_validate(p);

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
		r = amdgpu_sync_resv(p->adev, &p->ibs[0].sync, resv, p->filp);

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
	unsigned i;

	if (!error) {
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
				&parser->ibs[parser->num_ibs-1].fence->base);
	} else if (backoff) {
		ttm_eu_backoff_reservation(&parser->ticket,
					   &parser->validated);
	}

	if (parser->ctx)
		amdgpu_ctx_put(parser->ctx);
	if (parser->bo_list)
		amdgpu_bo_list_put(parser->bo_list);
	drm_free_large(parser->vm_bos);
	for (i = 0; i < parser->nchunks; i++)
		drm_free_large(parser->chunks[i].kdata);
	kfree(parser->chunks);
	if (parser->ibs)
		for (i = 0; i < parser->num_ibs; i++)
			amdgpu_ib_free(parser->adev, &parser->ibs[i]);
	kfree(parser->ibs);
	if (parser->uf.bo)
		drm_gem_object_unreference_unlocked(&parser->uf.bo->gem_base);
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

	r = amdgpu_vm_clear_freed(adev, vm);
	if (r)
		return r;

	if (p->bo_list) {
		for (i = 0; i < p->bo_list->num_entries; i++) {
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

			amdgpu_sync_fence(&p->ibs[0].sync, bo_va->last_pt_update);
		}
	}

	return amdgpu_vm_clear_invalids(adev, vm, &p->ibs[0].sync);
}

static int amdgpu_cs_ib_vm_chunk(struct amdgpu_device *adev,
				 struct amdgpu_cs_parser *parser)
{
	struct amdgpu_fpriv *fpriv = parser->filp->driver_priv;
	struct amdgpu_vm *vm = &fpriv->vm;
	struct amdgpu_ring *ring;
	int i, r;

	if (parser->num_ibs == 0)
		return 0;

	/* Only for UVD/VCE VM emulation */
	for (i = 0; i < parser->num_ibs; i++) {
		ring = parser->ibs[i].ring;
		if (ring->funcs->parse_cs) {
			r = amdgpu_ring_parse_cs(ring, parser, i);
			if (r)
				return r;
		}
	}

	mutex_lock(&vm->mutex);
	r = amdgpu_bo_vm_update_pte(parser, vm);
	if (r) {
		goto out;
	}
	amdgpu_cs_sync_rings(parser);

	r = amdgpu_ib_schedule(adev, parser->num_ibs, parser->ibs,
			       parser->filp);

out:
	mutex_unlock(&vm->mutex);
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

	for (i = 0, j = 0; i < parser->nchunks && j < parser->num_ibs; i++) {
		struct amdgpu_cs_chunk *chunk;
		struct amdgpu_ib *ib;
		struct drm_amdgpu_cs_chunk_ib *chunk_ib;
		struct amdgpu_ring *ring;

		chunk = &parser->chunks[i];
		ib = &parser->ibs[j];
		chunk_ib = (struct drm_amdgpu_cs_chunk_ib *)chunk->kdata;

		if (chunk->chunk_id != AMDGPU_CHUNK_ID_IB)
			continue;

		r = amdgpu_cs_get_ring(adev, chunk_ib->ip_type,
				       chunk_ib->ip_instance, chunk_ib->ring,
				       &ring);
		if (r)
			return r;

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

			r =  amdgpu_ib_get(ring, NULL, chunk_ib->ib_bytes, ib);
			if (r) {
				DRM_ERROR("Failed to get ib !\n");
				return r;
			}

			memcpy(ib->ptr, kptr, chunk_ib->ib_bytes);
			amdgpu_bo_kunmap(aobj);
		} else {
			r =  amdgpu_ib_get(ring, vm, 0, ib);
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

	if (!parser->num_ibs)
		return 0;

	/* add GDS resources to first IB */
	if (parser->bo_list) {
		struct amdgpu_bo *gds = parser->bo_list->gds_obj;
		struct amdgpu_bo *gws = parser->bo_list->gws_obj;
		struct amdgpu_bo *oa = parser->bo_list->oa_obj;
		struct amdgpu_ib *ib = &parser->ibs[0];

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
	if (parser->uf.bo) {
		struct amdgpu_ib *ib = &parser->ibs[parser->num_ibs - 1];

		/* UVD & VCE fw doesn't support user fences */
		if (ib->ring->type == AMDGPU_RING_TYPE_UVD ||
		    ib->ring->type == AMDGPU_RING_TYPE_VCE)
			return -EINVAL;

		ib->user = &parser->uf;
	}

	return 0;
}

static int amdgpu_cs_dependencies(struct amdgpu_device *adev,
				  struct amdgpu_cs_parser *p)
{
	struct amdgpu_ib *ib;
	int i, j, r;

	if (!p->num_ibs)
		return 0;

	/* Add dependencies to first IB */
	ib = &p->ibs[0];
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
			struct amdgpu_fence *fence;
			struct amdgpu_ring *ring;

			r = amdgpu_cs_get_ring(adev, deps[j].ip_type,
					       deps[j].ip_instance,
					       deps[j].ring, &ring);
			if (r)
				return r;

			r = amdgpu_fence_recreate(ring, p->filp,
						  deps[j].handle,
						  &fence);
			if (r)
				return r;

			amdgpu_sync_fence(&ib->sync, fence);
			amdgpu_fence_unref(&fence);
		}
	}

	return 0;
}

int amdgpu_cs_ioctl(struct drm_device *dev, void *data, struct drm_file *filp)
{
	struct amdgpu_device *adev = dev->dev_private;
	union drm_amdgpu_cs *cs = data;
	struct amdgpu_cs_parser parser;
	int r, i;
	bool reserved_buffers = false;

	down_read(&adev->exclusive_lock);
	if (!adev->accel_working) {
		up_read(&adev->exclusive_lock);
		return -EBUSY;
	}
	/* initialize parser */
	memset(&parser, 0, sizeof(struct amdgpu_cs_parser));
	parser.filp = filp;
	parser.adev = adev;
	r = amdgpu_cs_parser_init(&parser, data);
	if (r) {
		DRM_ERROR("Failed to initialize parser !\n");
		amdgpu_cs_parser_fini(&parser, r, false);
		up_read(&adev->exclusive_lock);
		r = amdgpu_cs_handle_lockup(adev, r);
		return r;
	}

	r = amdgpu_cs_parser_relocs(&parser);
	if (r) {
		if (r != -ERESTARTSYS) {
			if (r == -ENOMEM)
				DRM_ERROR("Not enough memory for command submission!\n");
			else
				DRM_ERROR("Failed to process the buffer list %d!\n", r);
		}
	}

	if (!r) {
		reserved_buffers = true;
		r = amdgpu_cs_ib_fill(adev, &parser);
	}

	if (!r)
		r = amdgpu_cs_dependencies(adev, &parser);

	if (r) {
		amdgpu_cs_parser_fini(&parser, r, reserved_buffers);
		up_read(&adev->exclusive_lock);
		r = amdgpu_cs_handle_lockup(adev, r);
		return r;
	}

	for (i = 0; i < parser.num_ibs; i++)
		trace_amdgpu_cs(&parser, i);

	r = amdgpu_cs_ib_vm_chunk(adev, &parser);
	if (r) {
		goto out;
	}

	cs->out.handle = parser.ibs[parser.num_ibs - 1].fence->seq;
out:
	amdgpu_cs_parser_fini(&parser, r, true);
	up_read(&adev->exclusive_lock);
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
	struct amdgpu_fence *fence = NULL;
	struct amdgpu_ring *ring = NULL;
	struct amdgpu_ctx *ctx;
	long r;

	ctx = amdgpu_ctx_get(filp->driver_priv, wait->in.ctx_id);
	if (ctx == NULL)
		return -EINVAL;

	r = amdgpu_cs_get_ring(adev, wait->in.ip_type, wait->in.ip_instance,
			       wait->in.ring, &ring);
	if (r)
		return r;

	r = amdgpu_fence_recreate(ring, filp, wait->in.handle, &fence);
	if (r)
		return r;

	r = fence_wait_timeout(&fence->base, true, timeout);
	amdgpu_fence_unref(&fence);
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
	struct amdgpu_bo_list_entry *reloc;
	struct amdgpu_bo_va_mapping *mapping;

	addr /= AMDGPU_GPU_PAGE_SIZE;

	list_for_each_entry(reloc, &parser->validated, tv.head) {
		if (!reloc->bo_va)
			continue;

		list_for_each_entry(mapping, &reloc->bo_va->mappings, list) {
			if (mapping->it.start > addr ||
			    addr > mapping->it.last)
				continue;

			*bo = reloc->bo_va->bo;
			return mapping;
		}
	}

	return NULL;
}
