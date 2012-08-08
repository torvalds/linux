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
#include "drmP.h"
#include "radeon_drm.h"
#include "radeon_reg.h"
#include "radeon.h"

void r100_cs_dump_packet(struct radeon_cs_parser *p,
			 struct radeon_cs_packet *pkt);

int radeon_cs_parser_relocs(struct radeon_cs_parser *p)
{
	struct drm_device *ddev = p->rdev->ddev;
	struct radeon_cs_chunk *chunk;
	unsigned i, j;
	bool duplicate;

	if (p->chunk_relocs_idx == -1) {
		return 0;
	}
	chunk = &p->chunks[p->chunk_relocs_idx];
	/* FIXME: we assume that each relocs use 4 dwords */
	p->nrelocs = chunk->length_dw / 4;
	p->relocs_ptr = kcalloc(p->nrelocs, sizeof(void *), GFP_KERNEL);
	if (p->relocs_ptr == NULL) {
		return -ENOMEM;
	}
	p->relocs = kcalloc(p->nrelocs, sizeof(struct radeon_cs_reloc), GFP_KERNEL);
	if (p->relocs == NULL) {
		return -ENOMEM;
	}
	for (i = 0; i < p->nrelocs; i++) {
		struct drm_radeon_cs_reloc *r;

		duplicate = false;
		r = (struct drm_radeon_cs_reloc *)&chunk->kdata[i*4];
		for (j = 0; j < i; j++) {
			if (r->handle == p->relocs[j].handle) {
				p->relocs_ptr[i] = &p->relocs[j];
				duplicate = true;
				break;
			}
		}
		if (!duplicate) {
			p->relocs[i].gobj = drm_gem_object_lookup(ddev,
								  p->filp,
								  r->handle);
			if (p->relocs[i].gobj == NULL) {
				DRM_ERROR("gem object lookup failed 0x%x\n",
					  r->handle);
				return -ENOENT;
			}
			p->relocs_ptr[i] = &p->relocs[i];
			p->relocs[i].robj = gem_to_radeon_bo(p->relocs[i].gobj);
			p->relocs[i].lobj.bo = p->relocs[i].robj;
			p->relocs[i].lobj.wdomain = r->write_domain;
			p->relocs[i].lobj.rdomain = r->read_domains;
			p->relocs[i].lobj.tv.bo = &p->relocs[i].robj->tbo;
			p->relocs[i].handle = r->handle;
			p->relocs[i].flags = r->flags;
			radeon_bo_list_add_object(&p->relocs[i].lobj,
						  &p->validated);

		} else
			p->relocs[i].handle = 0;
	}
	return radeon_bo_list_validate(&p->validated);
}

static int radeon_cs_get_ring(struct radeon_cs_parser *p, u32 ring, s32 priority)
{
	p->priority = priority;

	switch (ring) {
	default:
		DRM_ERROR("unknown ring id: %d\n", ring);
		return -EINVAL;
	case RADEON_CS_RING_GFX:
		p->ring = RADEON_RING_TYPE_GFX_INDEX;
		break;
	case RADEON_CS_RING_COMPUTE:
		if (p->rdev->family >= CHIP_TAHITI) {
			if (p->priority > 0)
				p->ring = CAYMAN_RING_TYPE_CP1_INDEX;
			else
				p->ring = CAYMAN_RING_TYPE_CP2_INDEX;
		} else
			p->ring = RADEON_RING_TYPE_GFX_INDEX;
		break;
	}
	return 0;
}

static void radeon_cs_sync_to(struct radeon_cs_parser *p,
			      struct radeon_fence *fence)
{
	struct radeon_fence *other;

	if (!fence)
		return;

	other = p->ib.sync_to[fence->ring];
	p->ib.sync_to[fence->ring] = radeon_fence_later(fence, other);
}

static void radeon_cs_sync_rings(struct radeon_cs_parser *p)
{
	int i;

	for (i = 0; i < p->nrelocs; i++) {
		if (!p->relocs[i].robj)
			continue;

		radeon_cs_sync_to(p, p->relocs[i].robj->tbo.sync_obj);
	}
}

/* XXX: note that this is called from the legacy UMS CS ioctl as well */
int radeon_cs_parser_init(struct radeon_cs_parser *p, void *data)
{
	struct drm_radeon_cs *cs = data;
	uint64_t *chunk_array_ptr;
	unsigned size, i;
	u32 ring = RADEON_CS_RING_GFX;
	s32 priority = 0;

	if (!cs->num_chunks) {
		return 0;
	}
	/* get chunks */
	INIT_LIST_HEAD(&p->validated);
	p->idx = 0;
	p->ib.sa_bo = NULL;
	p->ib.semaphore = NULL;
	p->const_ib.sa_bo = NULL;
	p->const_ib.semaphore = NULL;
	p->chunk_ib_idx = -1;
	p->chunk_relocs_idx = -1;
	p->chunk_flags_idx = -1;
	p->chunk_const_ib_idx = -1;
	p->chunks_array = kcalloc(cs->num_chunks, sizeof(uint64_t), GFP_KERNEL);
	if (p->chunks_array == NULL) {
		return -ENOMEM;
	}
	chunk_array_ptr = (uint64_t *)(unsigned long)(cs->chunks);
	if (DRM_COPY_FROM_USER(p->chunks_array, chunk_array_ptr,
			       sizeof(uint64_t)*cs->num_chunks)) {
		return -EFAULT;
	}
	p->cs_flags = 0;
	p->nchunks = cs->num_chunks;
	p->chunks = kcalloc(p->nchunks, sizeof(struct radeon_cs_chunk), GFP_KERNEL);
	if (p->chunks == NULL) {
		return -ENOMEM;
	}
	for (i = 0; i < p->nchunks; i++) {
		struct drm_radeon_cs_chunk __user **chunk_ptr = NULL;
		struct drm_radeon_cs_chunk user_chunk;
		uint32_t __user *cdata;

		chunk_ptr = (void __user*)(unsigned long)p->chunks_array[i];
		if (DRM_COPY_FROM_USER(&user_chunk, chunk_ptr,
				       sizeof(struct drm_radeon_cs_chunk))) {
			return -EFAULT;
		}
		p->chunks[i].length_dw = user_chunk.length_dw;
		p->chunks[i].kdata = NULL;
		p->chunks[i].chunk_id = user_chunk.chunk_id;

		if (p->chunks[i].chunk_id == RADEON_CHUNK_ID_RELOCS) {
			p->chunk_relocs_idx = i;
		}
		if (p->chunks[i].chunk_id == RADEON_CHUNK_ID_IB) {
			p->chunk_ib_idx = i;
			/* zero length IB isn't useful */
			if (p->chunks[i].length_dw == 0)
				return -EINVAL;
		}
		if (p->chunks[i].chunk_id == RADEON_CHUNK_ID_CONST_IB) {
			p->chunk_const_ib_idx = i;
			/* zero length CONST IB isn't useful */
			if (p->chunks[i].length_dw == 0)
				return -EINVAL;
		}
		if (p->chunks[i].chunk_id == RADEON_CHUNK_ID_FLAGS) {
			p->chunk_flags_idx = i;
			/* zero length flags aren't useful */
			if (p->chunks[i].length_dw == 0)
				return -EINVAL;
		}

		p->chunks[i].length_dw = user_chunk.length_dw;
		p->chunks[i].user_ptr = (void __user *)(unsigned long)user_chunk.chunk_data;

		cdata = (uint32_t *)(unsigned long)user_chunk.chunk_data;
		if ((p->chunks[i].chunk_id == RADEON_CHUNK_ID_RELOCS) ||
		    (p->chunks[i].chunk_id == RADEON_CHUNK_ID_FLAGS)) {
			size = p->chunks[i].length_dw * sizeof(uint32_t);
			p->chunks[i].kdata = kmalloc(size, GFP_KERNEL);
			if (p->chunks[i].kdata == NULL) {
				return -ENOMEM;
			}
			if (DRM_COPY_FROM_USER(p->chunks[i].kdata,
					       p->chunks[i].user_ptr, size)) {
				return -EFAULT;
			}
			if (p->chunks[i].chunk_id == RADEON_CHUNK_ID_FLAGS) {
				p->cs_flags = p->chunks[i].kdata[0];
				if (p->chunks[i].length_dw > 1)
					ring = p->chunks[i].kdata[1];
				if (p->chunks[i].length_dw > 2)
					priority = (s32)p->chunks[i].kdata[2];
			}
		}
	}

	/* these are KMS only */
	if (p->rdev) {
		if ((p->cs_flags & RADEON_CS_USE_VM) &&
		    !p->rdev->vm_manager.enabled) {
			DRM_ERROR("VM not active on asic!\n");
			return -EINVAL;
		}

		/* we only support VM on SI+ */
		if ((p->rdev->family >= CHIP_TAHITI) &&
		    ((p->cs_flags & RADEON_CS_USE_VM) == 0)) {
			DRM_ERROR("VM required on SI+!\n");
			return -EINVAL;
		}

		if (radeon_cs_get_ring(p, ring, priority))
			return -EINVAL;
	}

	/* deal with non-vm */
	if ((p->chunk_ib_idx != -1) &&
	    ((p->cs_flags & RADEON_CS_USE_VM) == 0) &&
	    (p->chunks[p->chunk_ib_idx].chunk_id == RADEON_CHUNK_ID_IB)) {
		if (p->chunks[p->chunk_ib_idx].length_dw > (16 * 1024)) {
			DRM_ERROR("cs IB too big: %d\n",
				  p->chunks[p->chunk_ib_idx].length_dw);
			return -EINVAL;
		}
		if ((p->rdev->flags & RADEON_IS_AGP)) {
			p->chunks[p->chunk_ib_idx].kpage[0] = kmalloc(PAGE_SIZE, GFP_KERNEL);
			p->chunks[p->chunk_ib_idx].kpage[1] = kmalloc(PAGE_SIZE, GFP_KERNEL);
			if (p->chunks[p->chunk_ib_idx].kpage[0] == NULL ||
			    p->chunks[p->chunk_ib_idx].kpage[1] == NULL) {
				kfree(p->chunks[i].kpage[0]);
				kfree(p->chunks[i].kpage[1]);
				return -ENOMEM;
			}
		}
		p->chunks[p->chunk_ib_idx].kpage_idx[0] = -1;
		p->chunks[p->chunk_ib_idx].kpage_idx[1] = -1;
		p->chunks[p->chunk_ib_idx].last_copied_page = -1;
		p->chunks[p->chunk_ib_idx].last_page_index =
			((p->chunks[p->chunk_ib_idx].length_dw * 4) - 1) / PAGE_SIZE;
	}

	return 0;
}

static void radeon_bo_vm_fence_va(struct radeon_cs_parser *parser,
				  struct radeon_fence *fence)
{
	struct radeon_fpriv *fpriv = parser->filp->driver_priv;
	struct radeon_vm *vm = &fpriv->vm;
	struct radeon_bo_list *lobj;

	if (parser->chunk_ib_idx == -1) {
		return;
	}
	if ((parser->cs_flags & RADEON_CS_USE_VM) == 0) {
		return;
	}

	list_for_each_entry(lobj, &parser->validated, tv.head) {
		struct radeon_bo_va *bo_va;
		struct radeon_bo *rbo = lobj->bo;

		bo_va = radeon_bo_va(rbo, vm);
		radeon_fence_unref(&bo_va->fence);
		bo_va->fence = radeon_fence_ref(fence);
	}
}

/**
 * cs_parser_fini() - clean parser states
 * @parser:	parser structure holding parsing context.
 * @error:	error number
 *
 * If error is set than unvalidate buffer, otherwise just free memory
 * used by parsing context.
 **/
static void radeon_cs_parser_fini(struct radeon_cs_parser *parser, int error)
{
	unsigned i;

	if (!error) {
		/* fence all bo va before ttm_eu_fence_buffer_objects so bo are still reserved */
		radeon_bo_vm_fence_va(parser, parser->ib.fence);
		ttm_eu_fence_buffer_objects(&parser->validated,
					    parser->ib.fence);
	} else {
		ttm_eu_backoff_reservation(&parser->validated);
	}

	if (parser->relocs != NULL) {
		for (i = 0; i < parser->nrelocs; i++) {
			if (parser->relocs[i].gobj)
				drm_gem_object_unreference_unlocked(parser->relocs[i].gobj);
		}
	}
	kfree(parser->track);
	kfree(parser->relocs);
	kfree(parser->relocs_ptr);
	for (i = 0; i < parser->nchunks; i++) {
		kfree(parser->chunks[i].kdata);
		if ((parser->rdev->flags & RADEON_IS_AGP)) {
			kfree(parser->chunks[i].kpage[0]);
			kfree(parser->chunks[i].kpage[1]);
		}
	}
	kfree(parser->chunks);
	kfree(parser->chunks_array);
	radeon_ib_free(parser->rdev, &parser->ib);
	radeon_ib_free(parser->rdev, &parser->const_ib);
}

static int radeon_cs_ib_chunk(struct radeon_device *rdev,
			      struct radeon_cs_parser *parser)
{
	struct radeon_cs_chunk *ib_chunk;
	int r;

	if (parser->chunk_ib_idx == -1)
		return 0;

	if (parser->cs_flags & RADEON_CS_USE_VM)
		return 0;

	ib_chunk = &parser->chunks[parser->chunk_ib_idx];
	/* Copy the packet into the IB, the parser will read from the
	 * input memory (cached) and write to the IB (which can be
	 * uncached).
	 */
	r =  radeon_ib_get(rdev, parser->ring, &parser->ib,
			   NULL, ib_chunk->length_dw * 4);
	if (r) {
		DRM_ERROR("Failed to get ib !\n");
		return r;
	}
	parser->ib.length_dw = ib_chunk->length_dw;
	r = radeon_cs_parse(rdev, parser->ring, parser);
	if (r || parser->parser_error) {
		DRM_ERROR("Invalid command stream !\n");
		return r;
	}
	r = radeon_cs_finish_pages(parser);
	if (r) {
		DRM_ERROR("Invalid command stream !\n");
		return r;
	}
	radeon_cs_sync_rings(parser);
	r = radeon_ib_schedule(rdev, &parser->ib, NULL);
	if (r) {
		DRM_ERROR("Failed to schedule IB !\n");
	}
	return r;
}

static int radeon_bo_vm_update_pte(struct radeon_cs_parser *parser,
				   struct radeon_vm *vm)
{
	struct radeon_bo_list *lobj;
	struct radeon_bo *bo;
	int r;

	list_for_each_entry(lobj, &parser->validated, tv.head) {
		bo = lobj->bo;
		r = radeon_vm_bo_update_pte(parser->rdev, vm, bo, &bo->tbo.mem);
		if (r) {
			return r;
		}
	}
	return 0;
}

static int radeon_cs_ib_vm_chunk(struct radeon_device *rdev,
				 struct radeon_cs_parser *parser)
{
	struct radeon_cs_chunk *ib_chunk;
	struct radeon_fpriv *fpriv = parser->filp->driver_priv;
	struct radeon_vm *vm = &fpriv->vm;
	int r;

	if (parser->chunk_ib_idx == -1)
		return 0;
	if ((parser->cs_flags & RADEON_CS_USE_VM) == 0)
		return 0;

	if ((rdev->family >= CHIP_TAHITI) &&
	    (parser->chunk_const_ib_idx != -1)) {
		ib_chunk = &parser->chunks[parser->chunk_const_ib_idx];
		if (ib_chunk->length_dw > RADEON_IB_VM_MAX_SIZE) {
			DRM_ERROR("cs IB CONST too big: %d\n", ib_chunk->length_dw);
			return -EINVAL;
		}
		r =  radeon_ib_get(rdev, parser->ring, &parser->const_ib,
				   vm, ib_chunk->length_dw * 4);
		if (r) {
			DRM_ERROR("Failed to get const ib !\n");
			return r;
		}
		parser->const_ib.is_const_ib = true;
		parser->const_ib.length_dw = ib_chunk->length_dw;
		/* Copy the packet into the IB */
		if (DRM_COPY_FROM_USER(parser->const_ib.ptr, ib_chunk->user_ptr,
				       ib_chunk->length_dw * 4)) {
			return -EFAULT;
		}
		r = radeon_ring_ib_parse(rdev, parser->ring, &parser->const_ib);
		if (r) {
			return r;
		}
	}

	ib_chunk = &parser->chunks[parser->chunk_ib_idx];
	if (ib_chunk->length_dw > RADEON_IB_VM_MAX_SIZE) {
		DRM_ERROR("cs IB too big: %d\n", ib_chunk->length_dw);
		return -EINVAL;
	}
	r =  radeon_ib_get(rdev, parser->ring, &parser->ib,
			   vm, ib_chunk->length_dw * 4);
	if (r) {
		DRM_ERROR("Failed to get ib !\n");
		return r;
	}
	parser->ib.length_dw = ib_chunk->length_dw;
	/* Copy the packet into the IB */
	if (DRM_COPY_FROM_USER(parser->ib.ptr, ib_chunk->user_ptr,
			       ib_chunk->length_dw * 4)) {
		return -EFAULT;
	}
	r = radeon_ring_ib_parse(rdev, parser->ring, &parser->ib);
	if (r) {
		return r;
	}

	mutex_lock(&rdev->vm_manager.lock);
	mutex_lock(&vm->mutex);
	r = radeon_vm_bind(rdev, vm);
	if (r) {
		goto out;
	}
	r = radeon_bo_vm_update_pte(parser, vm);
	if (r) {
		goto out;
	}
	radeon_cs_sync_rings(parser);
	radeon_cs_sync_to(parser, vm->last_flush);

	if ((rdev->family >= CHIP_TAHITI) &&
	    (parser->chunk_const_ib_idx != -1)) {
		r = radeon_ib_schedule(rdev, &parser->ib, &parser->const_ib);
	} else {
		r = radeon_ib_schedule(rdev, &parser->ib, NULL);
	}

out:
	if (!r) {
		if (vm->fence) {
			radeon_fence_unref(&vm->fence);
		}
		vm->fence = radeon_fence_ref(parser->ib.fence);
	}
	mutex_unlock(&vm->mutex);
	mutex_unlock(&rdev->vm_manager.lock);
	return r;
}

static int radeon_cs_handle_lockup(struct radeon_device *rdev, int r)
{
	if (r == -EDEADLK) {
		r = radeon_gpu_reset(rdev);
		if (!r)
			r = -EAGAIN;
	}
	return r;
}

int radeon_cs_ioctl(struct drm_device *dev, void *data, struct drm_file *filp)
{
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_cs_parser parser;
	int r;

	down_read(&rdev->exclusive_lock);
	if (!rdev->accel_working) {
		up_read(&rdev->exclusive_lock);
		return -EBUSY;
	}
	/* initialize parser */
	memset(&parser, 0, sizeof(struct radeon_cs_parser));
	parser.filp = filp;
	parser.rdev = rdev;
	parser.dev = rdev->dev;
	parser.family = rdev->family;
	r = radeon_cs_parser_init(&parser, data);
	if (r) {
		DRM_ERROR("Failed to initialize parser !\n");
		radeon_cs_parser_fini(&parser, r);
		up_read(&rdev->exclusive_lock);
		r = radeon_cs_handle_lockup(rdev, r);
		return r;
	}
	r = radeon_cs_parser_relocs(&parser);
	if (r) {
		if (r != -ERESTARTSYS)
			DRM_ERROR("Failed to parse relocation %d!\n", r);
		radeon_cs_parser_fini(&parser, r);
		up_read(&rdev->exclusive_lock);
		r = radeon_cs_handle_lockup(rdev, r);
		return r;
	}
	r = radeon_cs_ib_chunk(rdev, &parser);
	if (r) {
		goto out;
	}
	r = radeon_cs_ib_vm_chunk(rdev, &parser);
	if (r) {
		goto out;
	}
out:
	radeon_cs_parser_fini(&parser, r);
	up_read(&rdev->exclusive_lock);
	r = radeon_cs_handle_lockup(rdev, r);
	return r;
}

int radeon_cs_finish_pages(struct radeon_cs_parser *p)
{
	struct radeon_cs_chunk *ibc = &p->chunks[p->chunk_ib_idx];
	int i;
	int size = PAGE_SIZE;

	for (i = ibc->last_copied_page + 1; i <= ibc->last_page_index; i++) {
		if (i == ibc->last_page_index) {
			size = (ibc->length_dw * 4) % PAGE_SIZE;
			if (size == 0)
				size = PAGE_SIZE;
		}
		
		if (DRM_COPY_FROM_USER(p->ib.ptr + (i * (PAGE_SIZE/4)),
				       ibc->user_ptr + (i * PAGE_SIZE),
				       size))
			return -EFAULT;
	}
	return 0;
}

static int radeon_cs_update_pages(struct radeon_cs_parser *p, int pg_idx)
{
	int new_page;
	struct radeon_cs_chunk *ibc = &p->chunks[p->chunk_ib_idx];
	int i;
	int size = PAGE_SIZE;
	bool copy1 = (p->rdev->flags & RADEON_IS_AGP) ? false : true;

	for (i = ibc->last_copied_page + 1; i < pg_idx; i++) {
		if (DRM_COPY_FROM_USER(p->ib.ptr + (i * (PAGE_SIZE/4)),
				       ibc->user_ptr + (i * PAGE_SIZE),
				       PAGE_SIZE)) {
			p->parser_error = -EFAULT;
			return 0;
		}
	}

	if (pg_idx == ibc->last_page_index) {
		size = (ibc->length_dw * 4) % PAGE_SIZE;
		if (size == 0)
			size = PAGE_SIZE;
	}

	new_page = ibc->kpage_idx[0] < ibc->kpage_idx[1] ? 0 : 1;
	if (copy1)
		ibc->kpage[new_page] = p->ib.ptr + (pg_idx * (PAGE_SIZE / 4));

	if (DRM_COPY_FROM_USER(ibc->kpage[new_page],
			       ibc->user_ptr + (pg_idx * PAGE_SIZE),
			       size)) {
		p->parser_error = -EFAULT;
		return 0;
	}

	/* copy to IB for non single case */
	if (!copy1)
		memcpy((void *)(p->ib.ptr+(pg_idx*(PAGE_SIZE/4))), ibc->kpage[new_page], size);

	ibc->last_copied_page = pg_idx;
	ibc->kpage_idx[new_page] = pg_idx;

	return new_page;
}

u32 radeon_get_ib_value(struct radeon_cs_parser *p, int idx)
{
	struct radeon_cs_chunk *ibc = &p->chunks[p->chunk_ib_idx];
	u32 pg_idx, pg_offset;
	u32 idx_value = 0;
	int new_page;

	pg_idx = (idx * 4) / PAGE_SIZE;
	pg_offset = (idx * 4) % PAGE_SIZE;

	if (ibc->kpage_idx[0] == pg_idx)
		return ibc->kpage[0][pg_offset/4];
	if (ibc->kpage_idx[1] == pg_idx)
		return ibc->kpage[1][pg_offset/4];

	new_page = radeon_cs_update_pages(p, pg_idx);
	if (new_page < 0) {
		p->parser_error = new_page;
		return 0;
	}

	idx_value = ibc->kpage[new_page][pg_offset/4];
	return idx_value;
}
