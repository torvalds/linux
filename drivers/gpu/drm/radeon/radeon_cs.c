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
#include <drm/radeon_drm.h>
#include "radeon_reg.h"
#include "radeon.h"
#include "radeon_trace.h"

#define RADEON_CS_MAX_PRIORITY		32u
#define RADEON_CS_NUM_BUCKETS		(RADEON_CS_MAX_PRIORITY + 1)

/* This is based on the bucket sort with O(n) time complexity.
 * An item with priority "i" is added to bucket[i]. The lists are then
 * concatenated in descending order.
 */
struct radeon_cs_buckets {
	struct list_head bucket[RADEON_CS_NUM_BUCKETS];
};

static void radeon_cs_buckets_init(struct radeon_cs_buckets *b)
{
	unsigned i;

	for (i = 0; i < RADEON_CS_NUM_BUCKETS; i++)
		INIT_LIST_HEAD(&b->bucket[i]);
}

static void radeon_cs_buckets_add(struct radeon_cs_buckets *b,
				  struct list_head *item, unsigned priority)
{
	/* Since buffers which appear sooner in the relocation list are
	 * likely to be used more often than buffers which appear later
	 * in the list, the sort mustn't change the ordering of buffers
	 * with the same priority, i.e. it must be stable.
	 */
	list_add_tail(item, &b->bucket[min(priority, RADEON_CS_MAX_PRIORITY)]);
}

static void radeon_cs_buckets_get_list(struct radeon_cs_buckets *b,
				       struct list_head *out_list)
{
	unsigned i;

	/* Connect the sorted buckets in the output list. */
	for (i = 0; i < RADEON_CS_NUM_BUCKETS; i++) {
		list_splice(&b->bucket[i], out_list);
	}
}

static int radeon_cs_parser_relocs(struct radeon_cs_parser *p)
{
	struct drm_device *ddev = p->rdev->ddev;
	struct radeon_cs_chunk *chunk;
	struct radeon_cs_buckets buckets;
	unsigned i, j;
	bool duplicate;

	if (p->chunk_relocs_idx == -1) {
		return 0;
	}
	chunk = &p->chunks[p->chunk_relocs_idx];
	p->dma_reloc_idx = 0;
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

	radeon_cs_buckets_init(&buckets);

	for (i = 0; i < p->nrelocs; i++) {
		struct drm_radeon_cs_reloc *r;
		unsigned priority;

		duplicate = false;
		r = (struct drm_radeon_cs_reloc *)&chunk->kdata[i*4];
		for (j = 0; j < i; j++) {
			if (r->handle == p->relocs[j].handle) {
				p->relocs_ptr[i] = &p->relocs[j];
				duplicate = true;
				break;
			}
		}
		if (duplicate) {
			p->relocs[i].handle = 0;
			continue;
		}

		p->relocs[i].gobj = drm_gem_object_lookup(ddev, p->filp,
							  r->handle);
		if (p->relocs[i].gobj == NULL) {
			DRM_ERROR("gem object lookup failed 0x%x\n",
				  r->handle);
			return -ENOENT;
		}
		p->relocs_ptr[i] = &p->relocs[i];
		p->relocs[i].robj = gem_to_radeon_bo(p->relocs[i].gobj);

		/* The userspace buffer priorities are from 0 to 15. A higher
		 * number means the buffer is more important.
		 * Also, the buffers used for write have a higher priority than
		 * the buffers used for read only, which doubles the range
		 * to 0 to 31. 32 is reserved for the kernel driver.
		 */
		priority = (r->flags & RADEON_RELOC_PRIO_MASK) * 2
			   + !!r->write_domain;

		/* the first reloc of an UVD job is the msg and that must be in
		   VRAM, also but everything into VRAM on AGP cards to avoid
		   image corruptions */
		if (p->ring == R600_RING_TYPE_UVD_INDEX &&
		    (i == 0 || drm_pci_device_is_agp(p->rdev->ddev))) {
			/* TODO: is this still needed for NI+ ? */
			p->relocs[i].prefered_domains =
				RADEON_GEM_DOMAIN_VRAM;

			p->relocs[i].allowed_domains =
				RADEON_GEM_DOMAIN_VRAM;

			/* prioritize this over any other relocation */
			priority = RADEON_CS_MAX_PRIORITY;
		} else {
			uint32_t domain = r->write_domain ?
				r->write_domain : r->read_domains;

			if (domain & RADEON_GEM_DOMAIN_CPU) {
				DRM_ERROR("RADEON_GEM_DOMAIN_CPU is not valid "
					  "for command submission\n");
				return -EINVAL;
			}

			p->relocs[i].prefered_domains = domain;
			if (domain == RADEON_GEM_DOMAIN_VRAM)
				domain |= RADEON_GEM_DOMAIN_GTT;
			p->relocs[i].allowed_domains = domain;
		}

		p->relocs[i].tv.bo = &p->relocs[i].robj->tbo;
		p->relocs[i].handle = r->handle;

		radeon_cs_buckets_add(&buckets, &p->relocs[i].tv.head,
				      priority);
	}

	radeon_cs_buckets_get_list(&buckets, &p->validated);

	if (p->cs_flags & RADEON_CS_USE_VM)
		p->vm_bos = radeon_vm_get_bos(p->rdev, p->ib.vm,
					      &p->validated);

	return radeon_bo_list_validate(p->rdev, &p->ticket, &p->validated, p->ring);
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
	case RADEON_CS_RING_DMA:
		if (p->rdev->family >= CHIP_CAYMAN) {
			if (p->priority > 0)
				p->ring = R600_RING_TYPE_DMA_INDEX;
			else
				p->ring = CAYMAN_RING_TYPE_DMA1_INDEX;
		} else if (p->rdev->family >= CHIP_RV770) {
			p->ring = R600_RING_TYPE_DMA_INDEX;
		} else {
			return -EINVAL;
		}
		break;
	case RADEON_CS_RING_UVD:
		p->ring = R600_RING_TYPE_UVD_INDEX;
		break;
	case RADEON_CS_RING_VCE:
		/* TODO: only use the low priority ring for now */
		p->ring = TN_RING_TYPE_VCE1_INDEX;
		break;
	}
	return 0;
}

static void radeon_cs_sync_rings(struct radeon_cs_parser *p)
{
	int i;

	for (i = 0; i < p->nrelocs; i++) {
		if (!p->relocs[i].robj)
			continue;

		radeon_semaphore_sync_to(p->ib.semaphore,
					 p->relocs[i].robj->tbo.sync_obj);
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
	if (copy_from_user(p->chunks_array, chunk_array_ptr,
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
		if (copy_from_user(&user_chunk, chunk_ptr,
				       sizeof(struct drm_radeon_cs_chunk))) {
			return -EFAULT;
		}
		p->chunks[i].length_dw = user_chunk.length_dw;
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

		size = p->chunks[i].length_dw;
		cdata = (void __user *)(unsigned long)user_chunk.chunk_data;
		p->chunks[i].user_ptr = cdata;
		if (p->chunks[i].chunk_id == RADEON_CHUNK_ID_CONST_IB)
			continue;

		if (p->chunks[i].chunk_id == RADEON_CHUNK_ID_IB) {
			if (!p->rdev || !(p->rdev->flags & RADEON_IS_AGP))
				continue;
		}

		p->chunks[i].kdata = drm_malloc_ab(size, sizeof(uint32_t));
		size *= sizeof(uint32_t);
		if (p->chunks[i].kdata == NULL) {
			return -ENOMEM;
		}
		if (copy_from_user(p->chunks[i].kdata, cdata, size)) {
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

	/* these are KMS only */
	if (p->rdev) {
		if ((p->cs_flags & RADEON_CS_USE_VM) &&
		    !p->rdev->vm_manager.enabled) {
			DRM_ERROR("VM not active on asic!\n");
			return -EINVAL;
		}

		if (radeon_cs_get_ring(p, ring, priority))
			return -EINVAL;

		/* we only support VM on some SI+ rings */
		if ((p->cs_flags & RADEON_CS_USE_VM) == 0) {
			if (p->rdev->asic->ring[p->ring]->cs_parse == NULL) {
				DRM_ERROR("Ring %d requires VM!\n", p->ring);
				return -EINVAL;
			}
		} else {
			if (p->rdev->asic->ring[p->ring]->ib_parse == NULL) {
				DRM_ERROR("VM not supported on ring %d!\n",
					  p->ring);
				return -EINVAL;
			}
		}
	}

	return 0;
}

static int cmp_size_smaller_first(void *priv, struct list_head *a,
				  struct list_head *b)
{
	struct radeon_cs_reloc *la = list_entry(a, struct radeon_cs_reloc, tv.head);
	struct radeon_cs_reloc *lb = list_entry(b, struct radeon_cs_reloc, tv.head);

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
static void radeon_cs_parser_fini(struct radeon_cs_parser *parser, int error, bool backoff)
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
					    parser->ib.fence);
	} else if (backoff) {
		ttm_eu_backoff_reservation(&parser->ticket,
					   &parser->validated);
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
	kfree(parser->vm_bos);
	for (i = 0; i < parser->nchunks; i++)
		drm_free_large(parser->chunks[i].kdata);
	kfree(parser->chunks);
	kfree(parser->chunks_array);
	radeon_ib_free(parser->rdev, &parser->ib);
	radeon_ib_free(parser->rdev, &parser->const_ib);
}

static int radeon_cs_ib_chunk(struct radeon_device *rdev,
			      struct radeon_cs_parser *parser)
{
	int r;

	if (parser->chunk_ib_idx == -1)
		return 0;

	if (parser->cs_flags & RADEON_CS_USE_VM)
		return 0;

	r = radeon_cs_parse(rdev, parser->ring, parser);
	if (r || parser->parser_error) {
		DRM_ERROR("Invalid command stream !\n");
		return r;
	}

	if (parser->ring == R600_RING_TYPE_UVD_INDEX)
		radeon_uvd_note_usage(rdev);
	else if ((parser->ring == TN_RING_TYPE_VCE1_INDEX) ||
		 (parser->ring == TN_RING_TYPE_VCE2_INDEX))
		radeon_vce_note_usage(rdev);

	radeon_cs_sync_rings(parser);
	r = radeon_ib_schedule(rdev, &parser->ib, NULL, true);
	if (r) {
		DRM_ERROR("Failed to schedule IB !\n");
	}
	return r;
}

static int radeon_bo_vm_update_pte(struct radeon_cs_parser *p,
				   struct radeon_vm *vm)
{
	struct radeon_device *rdev = p->rdev;
	struct radeon_bo_va *bo_va;
	int i, r;

	r = radeon_vm_update_page_directory(rdev, vm);
	if (r)
		return r;

	r = radeon_vm_clear_freed(rdev, vm);
	if (r)
		return r;

	if (vm->ib_bo_va == NULL) {
		DRM_ERROR("Tmp BO not in VM!\n");
		return -EINVAL;
	}

	r = radeon_vm_bo_update(rdev, vm->ib_bo_va,
				&rdev->ring_tmp_bo.bo->tbo.mem);
	if (r)
		return r;

	for (i = 0; i < p->nrelocs; i++) {
		struct radeon_bo *bo;

		/* ignore duplicates */
		if (p->relocs_ptr[i] != &p->relocs[i])
			continue;

		bo = p->relocs[i].robj;
		bo_va = radeon_vm_bo_find(vm, bo);
		if (bo_va == NULL) {
			dev_err(rdev->dev, "bo %p not in vm %p\n", bo, vm);
			return -EINVAL;
		}

		r = radeon_vm_bo_update(rdev, bo_va, &bo->tbo.mem);
		if (r)
			return r;
	}

	return radeon_vm_clear_invalids(rdev, vm);
}

static int radeon_cs_ib_vm_chunk(struct radeon_device *rdev,
				 struct radeon_cs_parser *parser)
{
	struct radeon_fpriv *fpriv = parser->filp->driver_priv;
	struct radeon_vm *vm = &fpriv->vm;
	int r;

	if (parser->chunk_ib_idx == -1)
		return 0;
	if ((parser->cs_flags & RADEON_CS_USE_VM) == 0)
		return 0;

	if (parser->const_ib.length_dw) {
		r = radeon_ring_ib_parse(rdev, parser->ring, &parser->const_ib);
		if (r) {
			return r;
		}
	}

	r = radeon_ring_ib_parse(rdev, parser->ring, &parser->ib);
	if (r) {
		return r;
	}

	if (parser->ring == R600_RING_TYPE_UVD_INDEX)
		radeon_uvd_note_usage(rdev);

	mutex_lock(&vm->mutex);
	r = radeon_bo_vm_update_pte(parser, vm);
	if (r) {
		goto out;
	}
	radeon_cs_sync_rings(parser);
	radeon_semaphore_sync_to(parser->ib.semaphore, vm->fence);

	if ((rdev->family >= CHIP_TAHITI) &&
	    (parser->chunk_const_ib_idx != -1)) {
		r = radeon_ib_schedule(rdev, &parser->ib, &parser->const_ib, true);
	} else {
		r = radeon_ib_schedule(rdev, &parser->ib, NULL, true);
	}

out:
	mutex_unlock(&vm->mutex);
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

static int radeon_cs_ib_fill(struct radeon_device *rdev, struct radeon_cs_parser *parser)
{
	struct radeon_cs_chunk *ib_chunk;
	struct radeon_vm *vm = NULL;
	int r;

	if (parser->chunk_ib_idx == -1)
		return 0;

	if (parser->cs_flags & RADEON_CS_USE_VM) {
		struct radeon_fpriv *fpriv = parser->filp->driver_priv;
		vm = &fpriv->vm;

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
			if (copy_from_user(parser->const_ib.ptr,
					       ib_chunk->user_ptr,
					       ib_chunk->length_dw * 4))
				return -EFAULT;
		}

		ib_chunk = &parser->chunks[parser->chunk_ib_idx];
		if (ib_chunk->length_dw > RADEON_IB_VM_MAX_SIZE) {
			DRM_ERROR("cs IB too big: %d\n", ib_chunk->length_dw);
			return -EINVAL;
		}
	}
	ib_chunk = &parser->chunks[parser->chunk_ib_idx];

	r =  radeon_ib_get(rdev, parser->ring, &parser->ib,
			   vm, ib_chunk->length_dw * 4);
	if (r) {
		DRM_ERROR("Failed to get ib !\n");
		return r;
	}
	parser->ib.length_dw = ib_chunk->length_dw;
	if (ib_chunk->kdata)
		memcpy(parser->ib.ptr, ib_chunk->kdata, ib_chunk->length_dw * 4);
	else if (copy_from_user(parser->ib.ptr, ib_chunk->user_ptr, ib_chunk->length_dw * 4))
		return -EFAULT;
	return 0;
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
		radeon_cs_parser_fini(&parser, r, false);
		up_read(&rdev->exclusive_lock);
		r = radeon_cs_handle_lockup(rdev, r);
		return r;
	}

	r = radeon_cs_ib_fill(rdev, &parser);
	if (!r) {
		r = radeon_cs_parser_relocs(&parser);
		if (r && r != -ERESTARTSYS)
			DRM_ERROR("Failed to parse relocation %d!\n", r);
	}

	if (r) {
		radeon_cs_parser_fini(&parser, r, false);
		up_read(&rdev->exclusive_lock);
		r = radeon_cs_handle_lockup(rdev, r);
		return r;
	}

	trace_radeon_cs(&parser);

	r = radeon_cs_ib_chunk(rdev, &parser);
	if (r) {
		goto out;
	}
	r = radeon_cs_ib_vm_chunk(rdev, &parser);
	if (r) {
		goto out;
	}
out:
	radeon_cs_parser_fini(&parser, r, true);
	up_read(&rdev->exclusive_lock);
	r = radeon_cs_handle_lockup(rdev, r);
	return r;
}

/**
 * radeon_cs_packet_parse() - parse cp packet and point ib index to next packet
 * @parser:	parser structure holding parsing context.
 * @pkt:	where to store packet information
 *
 * Assume that chunk_ib_index is properly set. Will return -EINVAL
 * if packet is bigger than remaining ib size. or if packets is unknown.
 **/
int radeon_cs_packet_parse(struct radeon_cs_parser *p,
			   struct radeon_cs_packet *pkt,
			   unsigned idx)
{
	struct radeon_cs_chunk *ib_chunk = &p->chunks[p->chunk_ib_idx];
	struct radeon_device *rdev = p->rdev;
	uint32_t header;

	if (idx >= ib_chunk->length_dw) {
		DRM_ERROR("Can not parse packet at %d after CS end %d !\n",
			  idx, ib_chunk->length_dw);
		return -EINVAL;
	}
	header = radeon_get_ib_value(p, idx);
	pkt->idx = idx;
	pkt->type = RADEON_CP_PACKET_GET_TYPE(header);
	pkt->count = RADEON_CP_PACKET_GET_COUNT(header);
	pkt->one_reg_wr = 0;
	switch (pkt->type) {
	case RADEON_PACKET_TYPE0:
		if (rdev->family < CHIP_R600) {
			pkt->reg = R100_CP_PACKET0_GET_REG(header);
			pkt->one_reg_wr =
				RADEON_CP_PACKET0_GET_ONE_REG_WR(header);
		} else
			pkt->reg = R600_CP_PACKET0_GET_REG(header);
		break;
	case RADEON_PACKET_TYPE3:
		pkt->opcode = RADEON_CP_PACKET3_GET_OPCODE(header);
		break;
	case RADEON_PACKET_TYPE2:
		pkt->count = -1;
		break;
	default:
		DRM_ERROR("Unknown packet type %d at %d !\n", pkt->type, idx);
		return -EINVAL;
	}
	if ((pkt->count + 1 + pkt->idx) >= ib_chunk->length_dw) {
		DRM_ERROR("Packet (%d:%d:%d) end after CS buffer (%d) !\n",
			  pkt->idx, pkt->type, pkt->count, ib_chunk->length_dw);
		return -EINVAL;
	}
	return 0;
}

/**
 * radeon_cs_packet_next_is_pkt3_nop() - test if the next packet is P3 NOP
 * @p:		structure holding the parser context.
 *
 * Check if the next packet is NOP relocation packet3.
 **/
bool radeon_cs_packet_next_is_pkt3_nop(struct radeon_cs_parser *p)
{
	struct radeon_cs_packet p3reloc;
	int r;

	r = radeon_cs_packet_parse(p, &p3reloc, p->idx);
	if (r)
		return false;
	if (p3reloc.type != RADEON_PACKET_TYPE3)
		return false;
	if (p3reloc.opcode != RADEON_PACKET3_NOP)
		return false;
	return true;
}

/**
 * radeon_cs_dump_packet() - dump raw packet context
 * @p:		structure holding the parser context.
 * @pkt:	structure holding the packet.
 *
 * Used mostly for debugging and error reporting.
 **/
void radeon_cs_dump_packet(struct radeon_cs_parser *p,
			   struct radeon_cs_packet *pkt)
{
	volatile uint32_t *ib;
	unsigned i;
	unsigned idx;

	ib = p->ib.ptr;
	idx = pkt->idx;
	for (i = 0; i <= (pkt->count + 1); i++, idx++)
		DRM_INFO("ib[%d]=0x%08X\n", idx, ib[idx]);
}

/**
 * radeon_cs_packet_next_reloc() - parse next (should be reloc) packet
 * @parser:		parser structure holding parsing context.
 * @data:		pointer to relocation data
 * @offset_start:	starting offset
 * @offset_mask:	offset mask (to align start offset on)
 * @reloc:		reloc informations
 *
 * Check if next packet is relocation packet3, do bo validation and compute
 * GPU offset using the provided start.
 **/
int radeon_cs_packet_next_reloc(struct radeon_cs_parser *p,
				struct radeon_cs_reloc **cs_reloc,
				int nomm)
{
	struct radeon_cs_chunk *relocs_chunk;
	struct radeon_cs_packet p3reloc;
	unsigned idx;
	int r;

	if (p->chunk_relocs_idx == -1) {
		DRM_ERROR("No relocation chunk !\n");
		return -EINVAL;
	}
	*cs_reloc = NULL;
	relocs_chunk = &p->chunks[p->chunk_relocs_idx];
	r = radeon_cs_packet_parse(p, &p3reloc, p->idx);
	if (r)
		return r;
	p->idx += p3reloc.count + 2;
	if (p3reloc.type != RADEON_PACKET_TYPE3 ||
	    p3reloc.opcode != RADEON_PACKET3_NOP) {
		DRM_ERROR("No packet3 for relocation for packet at %d.\n",
			  p3reloc.idx);
		radeon_cs_dump_packet(p, &p3reloc);
		return -EINVAL;
	}
	idx = radeon_get_ib_value(p, p3reloc.idx + 1);
	if (idx >= relocs_chunk->length_dw) {
		DRM_ERROR("Relocs at %d after relocations chunk end %d !\n",
			  idx, relocs_chunk->length_dw);
		radeon_cs_dump_packet(p, &p3reloc);
		return -EINVAL;
	}
	/* FIXME: we assume reloc size is 4 dwords */
	if (nomm) {
		*cs_reloc = p->relocs;
		(*cs_reloc)->gpu_offset =
			(u64)relocs_chunk->kdata[idx + 3] << 32;
		(*cs_reloc)->gpu_offset |= relocs_chunk->kdata[idx + 0];
	} else
		*cs_reloc = p->relocs_ptr[(idx / 4)];
	return 0;
}
