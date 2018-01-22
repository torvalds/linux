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
	struct radeon_cs_chunk *chunk;
	struct radeon_cs_buckets buckets;
	unsigned i;
	bool need_mmap_lock = false;
	int r;

	if (p->chunk_relocs == NULL) {
		return 0;
	}
	chunk = p->chunk_relocs;
	p->dma_reloc_idx = 0;
	/* FIXME: we assume that each relocs use 4 dwords */
	p->nrelocs = chunk->length_dw / 4;
	p->relocs = kvmalloc_array(p->nrelocs, sizeof(struct radeon_bo_list),
			GFP_KERNEL | __GFP_ZERO);
	if (p->relocs == NULL) {
		return -ENOMEM;
	}

	radeon_cs_buckets_init(&buckets);

	for (i = 0; i < p->nrelocs; i++) {
		struct drm_radeon_cs_reloc *r;
		struct drm_gem_object *gobj;
		unsigned priority;

		r = (struct drm_radeon_cs_reloc *)&chunk->kdata[i*4];
		gobj = drm_gem_object_lookup(p->filp, r->handle);
		if (gobj == NULL) {
			DRM_ERROR("gem object lookup failed 0x%x\n",
				  r->handle);
			return -ENOENT;
		}
		p->relocs[i].robj = gem_to_radeon_bo(gobj);

		/* The userspace buffer priorities are from 0 to 15. A higher
		 * number means the buffer is more important.
		 * Also, the buffers used for write have a higher priority than
		 * the buffers used for read only, which doubles the range
		 * to 0 to 31. 32 is reserved for the kernel driver.
		 */
		priority = (r->flags & RADEON_RELOC_PRIO_MASK) * 2
			   + !!r->write_domain;

		/* The first reloc of an UVD job is the msg and that must be in
		 * VRAM, the second reloc is the DPB and for WMV that must be in
		 * VRAM as well. Also put everything into VRAM on AGP cards and older
		 * IGP chips to avoid image corruptions
		 */
		if (p->ring == R600_RING_TYPE_UVD_INDEX &&
		    (i <= 0 || pci_find_capability(p->rdev->ddev->pdev,
						   PCI_CAP_ID_AGP) ||
		     p->rdev->family == CHIP_RS780 ||
		     p->rdev->family == CHIP_RS880)) {

			/* TODO: is this still needed for NI+ ? */
			p->relocs[i].preferred_domains =
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

			p->relocs[i].preferred_domains = domain;
			if (domain == RADEON_GEM_DOMAIN_VRAM)
				domain |= RADEON_GEM_DOMAIN_GTT;
			p->relocs[i].allowed_domains = domain;
		}

		if (radeon_ttm_tt_has_userptr(p->relocs[i].robj->tbo.ttm)) {
			uint32_t domain = p->relocs[i].preferred_domains;
			if (!(domain & RADEON_GEM_DOMAIN_GTT)) {
				DRM_ERROR("Only RADEON_GEM_DOMAIN_GTT is "
					  "allowed for userptr BOs\n");
				return -EINVAL;
			}
			need_mmap_lock = true;
			domain = RADEON_GEM_DOMAIN_GTT;
			p->relocs[i].preferred_domains = domain;
			p->relocs[i].allowed_domains = domain;
		}

		/* Objects shared as dma-bufs cannot be moved to VRAM */
		if (p->relocs[i].robj->prime_shared_count) {
			p->relocs[i].allowed_domains &= ~RADEON_GEM_DOMAIN_VRAM;
			if (!p->relocs[i].allowed_domains) {
				DRM_ERROR("BO associated with dma-buf cannot "
					  "be moved to VRAM\n");
				return -EINVAL;
			}
		}

		p->relocs[i].tv.bo = &p->relocs[i].robj->tbo;
		p->relocs[i].tv.shared = !r->write_domain;

		radeon_cs_buckets_add(&buckets, &p->relocs[i].tv.head,
				      priority);
	}

	radeon_cs_buckets_get_list(&buckets, &p->validated);

	if (p->cs_flags & RADEON_CS_USE_VM)
		p->vm_bos = radeon_vm_get_bos(p->rdev, p->ib.vm,
					      &p->validated);
	if (need_mmap_lock)
		down_read(&current->mm->mmap_sem);

	r = radeon_bo_list_validate(p->rdev, &p->ticket, &p->validated, p->ring);

	if (need_mmap_lock)
		up_read(&current->mm->mmap_sem);

	return r;
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

static int radeon_cs_sync_rings(struct radeon_cs_parser *p)
{
	struct radeon_bo_list *reloc;
	int r;

	list_for_each_entry(reloc, &p->validated, tv.head) {
		struct reservation_object *resv;

		resv = reloc->robj->tbo.resv;
		r = radeon_sync_resv(p->rdev, &p->ib.sync, resv,
				     reloc->tv.shared);
		if (r)
			return r;
	}
	return 0;
}

/* XXX: note that this is called from the legacy UMS CS ioctl as well */
int radeon_cs_parser_init(struct radeon_cs_parser *p, void *data)
{
	struct drm_radeon_cs *cs = data;
	uint64_t *chunk_array_ptr;
	unsigned size, i;
	u32 ring = RADEON_CS_RING_GFX;
	s32 priority = 0;

	INIT_LIST_HEAD(&p->validated);

	if (!cs->num_chunks) {
		return 0;
	}

	/* get chunks */
	p->idx = 0;
	p->ib.sa_bo = NULL;
	p->const_ib.sa_bo = NULL;
	p->chunk_ib = NULL;
	p->chunk_relocs = NULL;
	p->chunk_flags = NULL;
	p->chunk_const_ib = NULL;
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
		if (user_chunk.chunk_id == RADEON_CHUNK_ID_RELOCS) {
			p->chunk_relocs = &p->chunks[i];
		}
		if (user_chunk.chunk_id == RADEON_CHUNK_ID_IB) {
			p->chunk_ib = &p->chunks[i];
			/* zero length IB isn't useful */
			if (p->chunks[i].length_dw == 0)
				return -EINVAL;
		}
		if (user_chunk.chunk_id == RADEON_CHUNK_ID_CONST_IB) {
			p->chunk_const_ib = &p->chunks[i];
			/* zero length CONST IB isn't useful */
			if (p->chunks[i].length_dw == 0)
				return -EINVAL;
		}
		if (user_chunk.chunk_id == RADEON_CHUNK_ID_FLAGS) {
			p->chunk_flags = &p->chunks[i];
			/* zero length flags aren't useful */
			if (p->chunks[i].length_dw == 0)
				return -EINVAL;
		}

		size = p->chunks[i].length_dw;
		cdata = (void __user *)(unsigned long)user_chunk.chunk_data;
		p->chunks[i].user_ptr = cdata;
		if (user_chunk.chunk_id == RADEON_CHUNK_ID_CONST_IB)
			continue;

		if (user_chunk.chunk_id == RADEON_CHUNK_ID_IB) {
			if (!p->rdev || !(p->rdev->flags & RADEON_IS_AGP))
				continue;
		}

		p->chunks[i].kdata = kvmalloc_array(size, sizeof(uint32_t), GFP_KERNEL);
		size *= sizeof(uint32_t);
		if (p->chunks[i].kdata == NULL) {
			return -ENOMEM;
		}
		if (copy_from_user(p->chunks[i].kdata, cdata, size)) {
			return -EFAULT;
		}
		if (user_chunk.chunk_id == RADEON_CHUNK_ID_FLAGS) {
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
	struct radeon_bo_list *la = list_entry(a, struct radeon_bo_list, tv.head);
	struct radeon_bo_list *lb = list_entry(b, struct radeon_bo_list, tv.head);

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
					    &parser->ib.fence->base);
	} else if (backoff) {
		ttm_eu_backoff_reservation(&parser->ticket,
					   &parser->validated);
	}

	if (parser->relocs != NULL) {
		for (i = 0; i < parser->nrelocs; i++) {
			struct radeon_bo *bo = parser->relocs[i].robj;
			if (bo == NULL)
				continue;

			drm_gem_object_put_unlocked(&bo->gem_base);
		}
	}
	kfree(parser->track);
	kvfree(parser->relocs);
	kvfree(parser->vm_bos);
	for (i = 0; i < parser->nchunks; i++)
		kvfree(parser->chunks[i].kdata);
	kfree(parser->chunks);
	kfree(parser->chunks_array);
	radeon_ib_free(parser->rdev, &parser->ib);
	radeon_ib_free(parser->rdev, &parser->const_ib);
}

static int radeon_cs_ib_chunk(struct radeon_device *rdev,
			      struct radeon_cs_parser *parser)
{
	int r;

	if (parser->chunk_ib == NULL)
		return 0;

	if (parser->cs_flags & RADEON_CS_USE_VM)
		return 0;

	r = radeon_cs_parse(rdev, parser->ring, parser);
	if (r || parser->parser_error) {
		DRM_ERROR("Invalid command stream !\n");
		return r;
	}

	r = radeon_cs_sync_rings(parser);
	if (r) {
		if (r != -ERESTARTSYS)
			DRM_ERROR("Failed to sync rings: %i\n", r);
		return r;
	}

	if (parser->ring == R600_RING_TYPE_UVD_INDEX)
		radeon_uvd_note_usage(rdev);
	else if ((parser->ring == TN_RING_TYPE_VCE1_INDEX) ||
		 (parser->ring == TN_RING_TYPE_VCE2_INDEX))
		radeon_vce_note_usage(rdev);

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

		bo = p->relocs[i].robj;
		bo_va = radeon_vm_bo_find(vm, bo);
		if (bo_va == NULL) {
			dev_err(rdev->dev, "bo %p not in vm %p\n", bo, vm);
			return -EINVAL;
		}

		r = radeon_vm_bo_update(rdev, bo_va, &bo->tbo.mem);
		if (r)
			return r;

		radeon_sync_fence(&p->ib.sync, bo_va->last_pt_update);
	}

	return radeon_vm_clear_invalids(rdev, vm);
}

static int radeon_cs_ib_vm_chunk(struct radeon_device *rdev,
				 struct radeon_cs_parser *parser)
{
	struct radeon_fpriv *fpriv = parser->filp->driver_priv;
	struct radeon_vm *vm = &fpriv->vm;
	int r;

	if (parser->chunk_ib == NULL)
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

	r = radeon_cs_sync_rings(parser);
	if (r) {
		if (r != -ERESTARTSYS)
			DRM_ERROR("Failed to sync rings: %i\n", r);
		goto out;
	}

	if ((rdev->family >= CHIP_TAHITI) &&
	    (parser->chunk_const_ib != NULL)) {
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

	if (parser->chunk_ib == NULL)
		return 0;

	if (parser->cs_flags & RADEON_CS_USE_VM) {
		struct radeon_fpriv *fpriv = parser->filp->driver_priv;
		vm = &fpriv->vm;

		if ((rdev->family >= CHIP_TAHITI) &&
		    (parser->chunk_const_ib != NULL)) {
			ib_chunk = parser->chunk_const_ib;
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

		ib_chunk = parser->chunk_ib;
		if (ib_chunk->length_dw > RADEON_IB_VM_MAX_SIZE) {
			DRM_ERROR("cs IB too big: %d\n", ib_chunk->length_dw);
			return -EINVAL;
		}
	}
	ib_chunk = parser->chunk_ib;

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
	if (rdev->in_reset) {
		up_read(&rdev->exclusive_lock);
		r = radeon_gpu_reset(rdev);
		if (!r)
			r = -EAGAIN;
		return r;
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
	struct radeon_cs_chunk *ib_chunk = p->chunk_ib;
	struct radeon_device *rdev = p->rdev;
	uint32_t header;
	int ret = 0, i;

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
		ret = -EINVAL;
		goto dump_ib;
	}
	if ((pkt->count + 1 + pkt->idx) >= ib_chunk->length_dw) {
		DRM_ERROR("Packet (%d:%d:%d) end after CS buffer (%d) !\n",
			  pkt->idx, pkt->type, pkt->count, ib_chunk->length_dw);
		ret = -EINVAL;
		goto dump_ib;
	}
	return 0;

dump_ib:
	for (i = 0; i < ib_chunk->length_dw; i++) {
		if (i == idx)
			printk("\t0x%08x <---\n", radeon_get_ib_value(p, i));
		else
			printk("\t0x%08x\n", radeon_get_ib_value(p, i));
	}
	return ret;
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
				struct radeon_bo_list **cs_reloc,
				int nomm)
{
	struct radeon_cs_chunk *relocs_chunk;
	struct radeon_cs_packet p3reloc;
	unsigned idx;
	int r;

	if (p->chunk_relocs == NULL) {
		DRM_ERROR("No relocation chunk !\n");
		return -EINVAL;
	}
	*cs_reloc = NULL;
	relocs_chunk = p->chunk_relocs;
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
		*cs_reloc = &p->relocs[(idx / 4)];
	return 0;
}
