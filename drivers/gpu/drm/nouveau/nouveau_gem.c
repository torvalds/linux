/*
 * Copyright (C) 2008 Ben Skeggs.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include "drmP.h"
#include "drm.h"

#include "nouveau_drv.h"
#include "nouveau_drm.h"
#include "nouveau_dma.h"

#define nouveau_gem_pushbuf_sync(chan) 0

int
nouveau_gem_object_new(struct drm_gem_object *gem)
{
	return 0;
}

void
nouveau_gem_object_del(struct drm_gem_object *gem)
{
	struct nouveau_bo *nvbo = gem->driver_private;
	struct ttm_buffer_object *bo = &nvbo->bo;

	if (!nvbo)
		return;
	nvbo->gem = NULL;

	if (unlikely(nvbo->cpu_filp))
		ttm_bo_synccpu_write_release(bo);

	if (unlikely(nvbo->pin_refcnt)) {
		nvbo->pin_refcnt = 1;
		nouveau_bo_unpin(nvbo);
	}

	ttm_bo_unref(&bo);
}

int
nouveau_gem_new(struct drm_device *dev, struct nouveau_channel *chan,
		int size, int align, uint32_t flags, uint32_t tile_mode,
		uint32_t tile_flags, bool no_vm, bool mappable,
		struct nouveau_bo **pnvbo)
{
	struct nouveau_bo *nvbo;
	int ret;

	ret = nouveau_bo_new(dev, chan, size, align, flags, tile_mode,
			     tile_flags, no_vm, mappable, pnvbo);
	if (ret)
		return ret;
	nvbo = *pnvbo;

	nvbo->gem = drm_gem_object_alloc(dev, nvbo->bo.mem.size);
	if (!nvbo->gem) {
		nouveau_bo_ref(NULL, pnvbo);
		return -ENOMEM;
	}

	nvbo->bo.persistant_swap_storage = nvbo->gem->filp;
	nvbo->gem->driver_private = nvbo;
	return 0;
}

static int
nouveau_gem_info(struct drm_gem_object *gem, struct drm_nouveau_gem_info *rep)
{
	struct nouveau_bo *nvbo = nouveau_gem_object(gem);

	if (nvbo->bo.mem.mem_type == TTM_PL_TT)
		rep->domain = NOUVEAU_GEM_DOMAIN_GART;
	else
		rep->domain = NOUVEAU_GEM_DOMAIN_VRAM;

	rep->size = nvbo->bo.mem.num_pages << PAGE_SHIFT;
	rep->offset = nvbo->bo.offset;
	rep->map_handle = nvbo->mappable ? nvbo->bo.addr_space_offset : 0;
	rep->tile_mode = nvbo->tile_mode;
	rep->tile_flags = nvbo->tile_flags;
	return 0;
}

static bool
nouveau_gem_tile_flags_valid(struct drm_device *dev, uint32_t tile_flags) {
	switch (tile_flags) {
	case 0x0000:
	case 0x1800:
	case 0x2800:
	case 0x4800:
	case 0x7000:
	case 0x7400:
	case 0x7a00:
	case 0xe000:
		break;
	default:
		NV_ERROR(dev, "bad page flags: 0x%08x\n", tile_flags);
		return false;
	}

	return true;
}

int
nouveau_gem_ioctl_new(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_nouveau_gem_new *req = data;
	struct nouveau_bo *nvbo = NULL;
	struct nouveau_channel *chan = NULL;
	uint32_t flags = 0;
	int ret = 0;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	if (unlikely(dev_priv->ttm.bdev.dev_mapping == NULL))
		dev_priv->ttm.bdev.dev_mapping = dev_priv->dev->dev_mapping;

	if (req->channel_hint) {
		NOUVEAU_GET_USER_CHANNEL_WITH_RETURN(req->channel_hint,
						     file_priv, chan);
	}

	if (req->info.domain & NOUVEAU_GEM_DOMAIN_VRAM)
		flags |= TTM_PL_FLAG_VRAM;
	if (req->info.domain & NOUVEAU_GEM_DOMAIN_GART)
		flags |= TTM_PL_FLAG_TT;
	if (!flags || req->info.domain & NOUVEAU_GEM_DOMAIN_CPU)
		flags |= TTM_PL_FLAG_SYSTEM;

	if (!nouveau_gem_tile_flags_valid(dev, req->info.tile_flags))
		return -EINVAL;

	ret = nouveau_gem_new(dev, chan, req->info.size, req->align, flags,
			      req->info.tile_mode, req->info.tile_flags, false,
			      (req->info.domain & NOUVEAU_GEM_DOMAIN_MAPPABLE),
			      &nvbo);
	if (ret)
		return ret;

	ret = nouveau_gem_info(nvbo->gem, &req->info);
	if (ret)
		goto out;

	ret = drm_gem_handle_create(file_priv, nvbo->gem, &req->info.handle);
out:
	drm_gem_object_handle_unreference_unlocked(nvbo->gem);

	if (ret)
		drm_gem_object_unreference_unlocked(nvbo->gem);
	return ret;
}

static int
nouveau_gem_set_domain(struct drm_gem_object *gem, uint32_t read_domains,
		       uint32_t write_domains, uint32_t valid_domains)
{
	struct nouveau_bo *nvbo = gem->driver_private;
	struct ttm_buffer_object *bo = &nvbo->bo;
	uint64_t flags;

	if (!valid_domains || (!read_domains && !write_domains))
		return -EINVAL;

	if (write_domains) {
		if ((valid_domains & NOUVEAU_GEM_DOMAIN_VRAM) &&
		    (write_domains & NOUVEAU_GEM_DOMAIN_VRAM))
			flags = TTM_PL_FLAG_VRAM;
		else
		if ((valid_domains & NOUVEAU_GEM_DOMAIN_GART) &&
		    (write_domains & NOUVEAU_GEM_DOMAIN_GART))
			flags = TTM_PL_FLAG_TT;
		else
			return -EINVAL;
	} else {
		if ((valid_domains & NOUVEAU_GEM_DOMAIN_VRAM) &&
		    (read_domains & NOUVEAU_GEM_DOMAIN_VRAM) &&
		    bo->mem.mem_type == TTM_PL_VRAM)
			flags = TTM_PL_FLAG_VRAM;
		else
		if ((valid_domains & NOUVEAU_GEM_DOMAIN_GART) &&
		    (read_domains & NOUVEAU_GEM_DOMAIN_GART) &&
		    bo->mem.mem_type == TTM_PL_TT)
			flags = TTM_PL_FLAG_TT;
		else
		if ((valid_domains & NOUVEAU_GEM_DOMAIN_VRAM) &&
		    (read_domains & NOUVEAU_GEM_DOMAIN_VRAM))
			flags = TTM_PL_FLAG_VRAM;
		else
			flags = TTM_PL_FLAG_TT;
	}

	nouveau_bo_placement_set(nvbo, flags);
	return 0;
}

struct validate_op {
	struct list_head vram_list;
	struct list_head gart_list;
	struct list_head both_list;
};

static void
validate_fini_list(struct list_head *list, struct nouveau_fence *fence)
{
	struct list_head *entry, *tmp;
	struct nouveau_bo *nvbo;

	list_for_each_safe(entry, tmp, list) {
		nvbo = list_entry(entry, struct nouveau_bo, entry);
		if (likely(fence)) {
			struct nouveau_fence *prev_fence;

			spin_lock(&nvbo->bo.lock);
			prev_fence = nvbo->bo.sync_obj;
			nvbo->bo.sync_obj = nouveau_fence_ref(fence);
			spin_unlock(&nvbo->bo.lock);
			nouveau_fence_unref((void *)&prev_fence);
		}

		list_del(&nvbo->entry);
		nvbo->reserved_by = NULL;
		ttm_bo_unreserve(&nvbo->bo);
		drm_gem_object_unreference(nvbo->gem);
	}
}

static void
validate_fini(struct validate_op *op, struct nouveau_fence* fence)
{
	validate_fini_list(&op->vram_list, fence);
	validate_fini_list(&op->gart_list, fence);
	validate_fini_list(&op->both_list, fence);
}

static int
validate_init(struct nouveau_channel *chan, struct drm_file *file_priv,
	      struct drm_nouveau_gem_pushbuf_bo *pbbo,
	      int nr_buffers, struct validate_op *op)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t sequence;
	int trycnt = 0;
	int ret, i;

	sequence = atomic_add_return(1, &dev_priv->ttm.validate_sequence);
retry:
	if (++trycnt > 100000) {
		NV_ERROR(dev, "%s failed and gave up.\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < nr_buffers; i++) {
		struct drm_nouveau_gem_pushbuf_bo *b = &pbbo[i];
		struct drm_gem_object *gem;
		struct nouveau_bo *nvbo;

		gem = drm_gem_object_lookup(dev, file_priv, b->handle);
		if (!gem) {
			NV_ERROR(dev, "Unknown handle 0x%08x\n", b->handle);
			validate_fini(op, NULL);
			return -EINVAL;
		}
		nvbo = gem->driver_private;

		if (nvbo->reserved_by && nvbo->reserved_by == file_priv) {
			NV_ERROR(dev, "multiple instances of buffer %d on "
				      "validation list\n", b->handle);
			validate_fini(op, NULL);
			return -EINVAL;
		}

		ret = ttm_bo_reserve(&nvbo->bo, false, false, true, sequence);
		if (ret) {
			validate_fini(op, NULL);
			if (ret == -EAGAIN)
				ret = ttm_bo_wait_unreserved(&nvbo->bo, false);
			drm_gem_object_unreference(gem);
			if (ret)
				return ret;
			goto retry;
		}

		nvbo->reserved_by = file_priv;
		nvbo->pbbo_index = i;
		if ((b->valid_domains & NOUVEAU_GEM_DOMAIN_VRAM) &&
		    (b->valid_domains & NOUVEAU_GEM_DOMAIN_GART))
			list_add_tail(&nvbo->entry, &op->both_list);
		else
		if (b->valid_domains & NOUVEAU_GEM_DOMAIN_VRAM)
			list_add_tail(&nvbo->entry, &op->vram_list);
		else
		if (b->valid_domains & NOUVEAU_GEM_DOMAIN_GART)
			list_add_tail(&nvbo->entry, &op->gart_list);
		else {
			NV_ERROR(dev, "invalid valid domains: 0x%08x\n",
				 b->valid_domains);
			list_add_tail(&nvbo->entry, &op->both_list);
			validate_fini(op, NULL);
			return -EINVAL;
		}

		if (unlikely(atomic_read(&nvbo->bo.cpu_writers) > 0)) {
			validate_fini(op, NULL);

			if (nvbo->cpu_filp == file_priv) {
				NV_ERROR(dev, "bo %p mapped by process trying "
					      "to validate it!\n", nvbo);
				return -EINVAL;
			}

			ret = ttm_bo_wait_cpu(&nvbo->bo, false);
			if (ret)
				return ret;
			goto retry;
		}
	}

	return 0;
}

static int
validate_list(struct nouveau_channel *chan, struct list_head *list,
	      struct drm_nouveau_gem_pushbuf_bo *pbbo, uint64_t user_pbbo_ptr)
{
	struct drm_nouveau_gem_pushbuf_bo __user *upbbo =
				(void __force __user *)(uintptr_t)user_pbbo_ptr;
	struct nouveau_bo *nvbo;
	int ret, relocs = 0;

	list_for_each_entry(nvbo, list, entry) {
		struct drm_nouveau_gem_pushbuf_bo *b = &pbbo[nvbo->pbbo_index];
		struct nouveau_fence *prev_fence = nvbo->bo.sync_obj;

		if (prev_fence && nouveau_fence_channel(prev_fence) != chan) {
			spin_lock(&nvbo->bo.lock);
			ret = ttm_bo_wait(&nvbo->bo, false, false, false);
			spin_unlock(&nvbo->bo.lock);
			if (unlikely(ret))
				return ret;
		}

		ret = nouveau_gem_set_domain(nvbo->gem, b->read_domains,
					     b->write_domains,
					     b->valid_domains);
		if (unlikely(ret))
			return ret;

		nvbo->channel = chan;
		ret = ttm_bo_validate(&nvbo->bo, &nvbo->placement,
				      false, false);
		nvbo->channel = NULL;
		if (unlikely(ret))
			return ret;

		if (nvbo->bo.offset == b->presumed_offset &&
		    ((nvbo->bo.mem.mem_type == TTM_PL_VRAM &&
		      b->presumed_domain & NOUVEAU_GEM_DOMAIN_VRAM) ||
		     (nvbo->bo.mem.mem_type == TTM_PL_TT &&
		      b->presumed_domain & NOUVEAU_GEM_DOMAIN_GART)))
			continue;

		if (nvbo->bo.mem.mem_type == TTM_PL_TT)
			b->presumed_domain = NOUVEAU_GEM_DOMAIN_GART;
		else
			b->presumed_domain = NOUVEAU_GEM_DOMAIN_VRAM;
		b->presumed_offset = nvbo->bo.offset;
		b->presumed_ok = 0;
		relocs++;

		if (DRM_COPY_TO_USER(&upbbo[nvbo->pbbo_index], b, sizeof(*b)))
			return -EFAULT;
	}

	return relocs;
}

static int
nouveau_gem_pushbuf_validate(struct nouveau_channel *chan,
			     struct drm_file *file_priv,
			     struct drm_nouveau_gem_pushbuf_bo *pbbo,
			     uint64_t user_buffers, int nr_buffers,
			     struct validate_op *op, int *apply_relocs)
{
	int ret, relocs = 0;

	INIT_LIST_HEAD(&op->vram_list);
	INIT_LIST_HEAD(&op->gart_list);
	INIT_LIST_HEAD(&op->both_list);

	if (nr_buffers == 0)
		return 0;

	ret = validate_init(chan, file_priv, pbbo, nr_buffers, op);
	if (unlikely(ret))
		return ret;

	ret = validate_list(chan, &op->vram_list, pbbo, user_buffers);
	if (unlikely(ret < 0)) {
		validate_fini(op, NULL);
		return ret;
	}
	relocs += ret;

	ret = validate_list(chan, &op->gart_list, pbbo, user_buffers);
	if (unlikely(ret < 0)) {
		validate_fini(op, NULL);
		return ret;
	}
	relocs += ret;

	ret = validate_list(chan, &op->both_list, pbbo, user_buffers);
	if (unlikely(ret < 0)) {
		validate_fini(op, NULL);
		return ret;
	}
	relocs += ret;

	*apply_relocs = relocs;
	return 0;
}

static inline void *
u_memcpya(uint64_t user, unsigned nmemb, unsigned size)
{
	void *mem;
	void __user *userptr = (void __force __user *)(uintptr_t)user;

	mem = kmalloc(nmemb * size, GFP_KERNEL);
	if (!mem)
		return ERR_PTR(-ENOMEM);

	if (DRM_COPY_FROM_USER(mem, userptr, nmemb * size)) {
		kfree(mem);
		return ERR_PTR(-EFAULT);
	}

	return mem;
}

static int
nouveau_gem_pushbuf_reloc_apply(struct nouveau_channel *chan, int nr_bo,
				struct drm_nouveau_gem_pushbuf_bo *bo,
				unsigned nr_relocs, uint64_t ptr_relocs,
				unsigned nr_dwords, unsigned first_dword,
				uint32_t *pushbuf, bool is_iomem)
{
	struct drm_nouveau_gem_pushbuf_reloc *reloc = NULL;
	struct drm_device *dev = chan->dev;
	int ret = 0;
	unsigned i;

	reloc = u_memcpya(ptr_relocs, nr_relocs, sizeof(*reloc));
	if (IS_ERR(reloc))
		return PTR_ERR(reloc);

	for (i = 0; i < nr_relocs; i++) {
		struct drm_nouveau_gem_pushbuf_reloc *r = &reloc[i];
		struct drm_nouveau_gem_pushbuf_bo *b;
		uint32_t data;

		if (r->bo_index >= nr_bo || r->reloc_index < first_dword ||
		    r->reloc_index >= first_dword + nr_dwords) {
			NV_ERROR(dev, "Bad relocation %d\n", i);
			NV_ERROR(dev, "  bo: %d max %d\n", r->bo_index, nr_bo);
			NV_ERROR(dev, "  id: %d max %d\n", r->reloc_index, nr_dwords);
			ret = -EINVAL;
			break;
		}

		b = &bo[r->bo_index];
		if (b->presumed_ok)
			continue;

		if (r->flags & NOUVEAU_GEM_RELOC_LOW)
			data = b->presumed_offset + r->data;
		else
		if (r->flags & NOUVEAU_GEM_RELOC_HIGH)
			data = (b->presumed_offset + r->data) >> 32;
		else
			data = r->data;

		if (r->flags & NOUVEAU_GEM_RELOC_OR) {
			if (b->presumed_domain == NOUVEAU_GEM_DOMAIN_GART)
				data |= r->tor;
			else
				data |= r->vor;
		}

		if (is_iomem)
			iowrite32_native(data, (void __force __iomem *)
						&pushbuf[r->reloc_index]);
		else
			pushbuf[r->reloc_index] = data;
	}

	kfree(reloc);
	return ret;
}

int
nouveau_gem_ioctl_pushbuf(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	struct drm_nouveau_gem_pushbuf *req = data;
	struct drm_nouveau_gem_pushbuf_bo *bo = NULL;
	struct nouveau_channel *chan;
	struct validate_op op;
	struct nouveau_fence* fence = 0;
	uint32_t *pushbuf = NULL;
	int ret = 0, do_reloc = 0, i;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;
	NOUVEAU_GET_USER_CHANNEL_WITH_RETURN(req->channel, file_priv, chan);

	if (req->nr_dwords >= chan->dma.max ||
	    req->nr_buffers > NOUVEAU_GEM_MAX_BUFFERS ||
	    req->nr_relocs > NOUVEAU_GEM_MAX_RELOCS) {
		NV_ERROR(dev, "Pushbuf config exceeds limits:\n");
		NV_ERROR(dev, "  dwords : %d max %d\n", req->nr_dwords,
			 chan->dma.max - 1);
		NV_ERROR(dev, "  buffers: %d max %d\n", req->nr_buffers,
			 NOUVEAU_GEM_MAX_BUFFERS);
		NV_ERROR(dev, "  relocs : %d max %d\n", req->nr_relocs,
			 NOUVEAU_GEM_MAX_RELOCS);
		return -EINVAL;
	}

	pushbuf = u_memcpya(req->dwords, req->nr_dwords, sizeof(uint32_t));
	if (IS_ERR(pushbuf))
		return PTR_ERR(pushbuf);

	bo = u_memcpya(req->buffers, req->nr_buffers, sizeof(*bo));
	if (IS_ERR(bo)) {
		kfree(pushbuf);
		return PTR_ERR(bo);
	}

	mutex_lock(&dev->struct_mutex);

	/* Validate buffer list */
	ret = nouveau_gem_pushbuf_validate(chan, file_priv, bo, req->buffers,
					   req->nr_buffers, &op, &do_reloc);
	if (ret)
		goto out;

	/* Apply any relocations that are required */
	if (do_reloc) {
		ret = nouveau_gem_pushbuf_reloc_apply(chan, req->nr_buffers,
						      bo, req->nr_relocs,
						      req->relocs,
						      req->nr_dwords, 0,
						      pushbuf, false);
		if (ret)
			goto out;
	}

	/* Emit push buffer to the hw
	 */
	ret = RING_SPACE(chan, req->nr_dwords);
	if (ret)
		goto out;

	OUT_RINGp(chan, pushbuf, req->nr_dwords);

	ret = nouveau_fence_new(chan, &fence, true);
	if (ret) {
		NV_ERROR(dev, "error fencing pushbuf: %d\n", ret);
		WIND_RING(chan);
		goto out;
	}

	if (nouveau_gem_pushbuf_sync(chan)) {
		ret = nouveau_fence_wait(fence, NULL, false, false);
		if (ret) {
			for (i = 0; i < req->nr_dwords; i++)
				NV_ERROR(dev, "0x%08x\n", pushbuf[i]);
			NV_ERROR(dev, "^^ above push buffer is fail :(\n");
		}
	}

out:
	validate_fini(&op, fence);
	nouveau_fence_unref((void**)&fence);
	mutex_unlock(&dev->struct_mutex);
	kfree(pushbuf);
	kfree(bo);
	return ret;
}

#define PUSHBUF_CAL (dev_priv->card_type >= NV_20)

int
nouveau_gem_ioctl_pushbuf_call(struct drm_device *dev, void *data,
			       struct drm_file *file_priv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_nouveau_gem_pushbuf_call *req = data;
	struct drm_nouveau_gem_pushbuf_bo *bo = NULL;
	struct nouveau_channel *chan;
	struct drm_gem_object *gem;
	struct nouveau_bo *pbbo;
	struct validate_op op;
	struct nouveau_fence* fence = 0;
	int i, ret = 0, do_reloc = 0;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;
	NOUVEAU_GET_USER_CHANNEL_WITH_RETURN(req->channel, file_priv, chan);

	if (unlikely(req->handle == 0))
		goto out_next;

	if (req->nr_buffers > NOUVEAU_GEM_MAX_BUFFERS ||
	    req->nr_relocs > NOUVEAU_GEM_MAX_RELOCS) {
		NV_ERROR(dev, "Pushbuf config exceeds limits:\n");
		NV_ERROR(dev, "  buffers: %d max %d\n", req->nr_buffers,
			 NOUVEAU_GEM_MAX_BUFFERS);
		NV_ERROR(dev, "  relocs : %d max %d\n", req->nr_relocs,
			 NOUVEAU_GEM_MAX_RELOCS);
		return -EINVAL;
	}

	bo = u_memcpya(req->buffers, req->nr_buffers, sizeof(*bo));
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	mutex_lock(&dev->struct_mutex);

	/* Validate buffer list */
	ret = nouveau_gem_pushbuf_validate(chan, file_priv, bo, req->buffers,
					   req->nr_buffers, &op, &do_reloc);
	if (ret) {
		NV_ERROR(dev, "validate: %d\n", ret);
		goto out;
	}

	/* Validate DMA push buffer */
	gem = drm_gem_object_lookup(dev, file_priv, req->handle);
	if (!gem) {
		NV_ERROR(dev, "Unknown pb handle 0x%08x\n", req->handle);
		ret = -EINVAL;
		goto out;
	}
	pbbo = nouveau_gem_object(gem);

	if ((req->offset & 3) || req->nr_dwords < 2 ||
	    (unsigned long)req->offset > (unsigned long)pbbo->bo.mem.size ||
	    (unsigned long)req->nr_dwords >
	     ((unsigned long)(pbbo->bo.mem.size - req->offset ) >> 2)) {
		NV_ERROR(dev, "pb call misaligned or out of bounds: "
			      "%d + %d * 4 > %ld\n",
			 req->offset, req->nr_dwords, pbbo->bo.mem.size);
		ret = -EINVAL;
		drm_gem_object_unreference(gem);
		goto out;
	}

	ret = ttm_bo_reserve(&pbbo->bo, false, false, true,
			     chan->fence.sequence);
	if (ret) {
		NV_ERROR(dev, "resv pb: %d\n", ret);
		drm_gem_object_unreference(gem);
		goto out;
	}

	nouveau_bo_placement_set(pbbo, 1 << chan->pushbuf_bo->bo.mem.mem_type);
	ret = ttm_bo_validate(&pbbo->bo, &pbbo->placement, false, false);
	if (ret) {
		NV_ERROR(dev, "validate pb: %d\n", ret);
		ttm_bo_unreserve(&pbbo->bo);
		drm_gem_object_unreference(gem);
		goto out;
	}

	list_add_tail(&pbbo->entry, &op.both_list);

	/* If presumed return address doesn't match, we need to map the
	 * push buffer and fix it..
	 */
	if (!PUSHBUF_CAL) {
		uint32_t retaddy;

		if (chan->dma.free < 4 + NOUVEAU_DMA_SKIPS) {
			ret = nouveau_dma_wait(chan, 4 + NOUVEAU_DMA_SKIPS);
			if (ret) {
				NV_ERROR(dev, "jmp_space: %d\n", ret);
				goto out;
			}
		}

		retaddy  = chan->pushbuf_base + ((chan->dma.cur + 2) << 2);
		retaddy |= 0x20000000;
		if (retaddy != req->suffix0) {
			req->suffix0 = retaddy;
			do_reloc = 1;
		}
	}

	/* Apply any relocations that are required */
	if (do_reloc) {
		void *pbvirt;
		bool is_iomem;
		ret = ttm_bo_kmap(&pbbo->bo, 0, pbbo->bo.mem.num_pages,
				  &pbbo->kmap);
		if (ret) {
			NV_ERROR(dev, "kmap pb: %d\n", ret);
			goto out;
		}

		pbvirt = ttm_kmap_obj_virtual(&pbbo->kmap, &is_iomem);
		ret = nouveau_gem_pushbuf_reloc_apply(chan, req->nr_buffers, bo,
						      req->nr_relocs,
						      req->relocs,
						      req->nr_dwords,
						      req->offset / 4,
						      pbvirt, is_iomem);

		if (!PUSHBUF_CAL) {
			nouveau_bo_wr32(pbbo,
					req->offset / 4 + req->nr_dwords - 2,
					req->suffix0);
		}

		ttm_bo_kunmap(&pbbo->kmap);
		if (ret) {
			NV_ERROR(dev, "reloc apply: %d\n", ret);
			goto out;
		}
	}

	if (PUSHBUF_CAL) {
		ret = RING_SPACE(chan, 2);
		if (ret) {
			NV_ERROR(dev, "cal_space: %d\n", ret);
			goto out;
		}
		OUT_RING(chan, ((pbbo->bo.mem.mm_node->start << PAGE_SHIFT) +
				  req->offset) | 2);
		OUT_RING(chan, 0);
	} else {
		ret = RING_SPACE(chan, 2 + NOUVEAU_DMA_SKIPS);
		if (ret) {
			NV_ERROR(dev, "jmp_space: %d\n", ret);
			goto out;
		}
		OUT_RING(chan, ((pbbo->bo.mem.mm_node->start << PAGE_SHIFT) +
				  req->offset) | 0x20000000);
		OUT_RING(chan, 0);

		/* Space the jumps apart with NOPs. */
		for (i = 0; i < NOUVEAU_DMA_SKIPS; i++)
			OUT_RING(chan, 0);
	}

	ret = nouveau_fence_new(chan, &fence, true);
	if (ret) {
		NV_ERROR(dev, "error fencing pushbuf: %d\n", ret);
		WIND_RING(chan);
		goto out;
	}

out:
	validate_fini(&op, fence);
	nouveau_fence_unref((void**)&fence);
	mutex_unlock(&dev->struct_mutex);
	kfree(bo);

out_next:
	if (PUSHBUF_CAL) {
		req->suffix0 = 0x00020000;
		req->suffix1 = 0x00000000;
	} else {
		req->suffix0 = 0x20000000 |
			      (chan->pushbuf_base + ((chan->dma.cur + 2) << 2));
		req->suffix1 = 0x00000000;
	}

	return ret;
}

int
nouveau_gem_ioctl_pushbuf_call2(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_nouveau_gem_pushbuf_call *req = data;

	req->vram_available = dev_priv->fb_aper_free;
	req->gart_available = dev_priv->gart_info.aper_free;

	return nouveau_gem_ioctl_pushbuf_call(dev, data, file_priv);
}

static inline uint32_t
domain_to_ttm(struct nouveau_bo *nvbo, uint32_t domain)
{
	uint32_t flags = 0;

	if (domain & NOUVEAU_GEM_DOMAIN_VRAM)
		flags |= TTM_PL_FLAG_VRAM;
	if (domain & NOUVEAU_GEM_DOMAIN_GART)
		flags |= TTM_PL_FLAG_TT;

	return flags;
}

int
nouveau_gem_ioctl_pin(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct drm_nouveau_gem_pin *req = data;
	struct drm_gem_object *gem;
	struct nouveau_bo *nvbo;
	int ret = 0;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		NV_ERROR(dev, "pin only allowed without kernel modesetting\n");
		return -EINVAL;
	}

	if (!DRM_SUSER(DRM_CURPROC))
		return -EPERM;

	gem = drm_gem_object_lookup(dev, file_priv, req->handle);
	if (!gem)
		return -EINVAL;
	nvbo = nouveau_gem_object(gem);

	ret = nouveau_bo_pin(nvbo, domain_to_ttm(nvbo, req->domain));
	if (ret)
		goto out;

	req->offset = nvbo->bo.offset;
	if (nvbo->bo.mem.mem_type == TTM_PL_TT)
		req->domain = NOUVEAU_GEM_DOMAIN_GART;
	else
		req->domain = NOUVEAU_GEM_DOMAIN_VRAM;

out:
	drm_gem_object_unreference_unlocked(gem);

	return ret;
}

int
nouveau_gem_ioctl_unpin(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	struct drm_nouveau_gem_pin *req = data;
	struct drm_gem_object *gem;
	int ret;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	gem = drm_gem_object_lookup(dev, file_priv, req->handle);
	if (!gem)
		return -EINVAL;

	ret = nouveau_bo_unpin(nouveau_gem_object(gem));

	drm_gem_object_unreference_unlocked(gem);

	return ret;
}

int
nouveau_gem_ioctl_cpu_prep(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	struct drm_nouveau_gem_cpu_prep *req = data;
	struct drm_gem_object *gem;
	struct nouveau_bo *nvbo;
	bool no_wait = !!(req->flags & NOUVEAU_GEM_CPU_PREP_NOWAIT);
	int ret = -EINVAL;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	gem = drm_gem_object_lookup(dev, file_priv, req->handle);
	if (!gem)
		return ret;
	nvbo = nouveau_gem_object(gem);

	if (nvbo->cpu_filp) {
		if (nvbo->cpu_filp == file_priv)
			goto out;

		ret = ttm_bo_wait_cpu(&nvbo->bo, no_wait);
		if (ret)
			goto out;
	}

	if (req->flags & NOUVEAU_GEM_CPU_PREP_NOBLOCK) {
		spin_lock(&nvbo->bo.lock);
		ret = ttm_bo_wait(&nvbo->bo, false, false, no_wait);
		spin_unlock(&nvbo->bo.lock);
	} else {
		ret = ttm_bo_synccpu_write_grab(&nvbo->bo, no_wait);
		if (ret == 0)
			nvbo->cpu_filp = file_priv;
	}

out:
	drm_gem_object_unreference_unlocked(gem);
	return ret;
}

int
nouveau_gem_ioctl_cpu_fini(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	struct drm_nouveau_gem_cpu_prep *req = data;
	struct drm_gem_object *gem;
	struct nouveau_bo *nvbo;
	int ret = -EINVAL;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	gem = drm_gem_object_lookup(dev, file_priv, req->handle);
	if (!gem)
		return ret;
	nvbo = nouveau_gem_object(gem);

	if (nvbo->cpu_filp != file_priv)
		goto out;
	nvbo->cpu_filp = NULL;

	ttm_bo_synccpu_write_release(&nvbo->bo);
	ret = 0;

out:
	drm_gem_object_unreference_unlocked(gem);
	return ret;
}

int
nouveau_gem_ioctl_info(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct drm_nouveau_gem_info *req = data;
	struct drm_gem_object *gem;
	int ret;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	gem = drm_gem_object_lookup(dev, file_priv, req->handle);
	if (!gem)
		return -EINVAL;

	ret = nouveau_gem_info(gem, req);
	drm_gem_object_unreference_unlocked(gem);
	return ret;
}

