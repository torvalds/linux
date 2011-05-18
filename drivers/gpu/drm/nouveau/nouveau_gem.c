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

	if (unlikely(nvbo->pin_refcnt)) {
		nvbo->pin_refcnt = 1;
		nouveau_bo_unpin(nvbo);
	}

	ttm_bo_unref(&bo);

	drm_gem_object_release(gem);
	kfree(gem);
}

int
nouveau_gem_new(struct drm_device *dev, struct nouveau_channel *chan,
		int size, int align, uint32_t domain, uint32_t tile_mode,
		uint32_t tile_flags, struct nouveau_bo **pnvbo)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_bo *nvbo;
	u32 flags = 0;
	int ret;

	if (domain & NOUVEAU_GEM_DOMAIN_VRAM)
		flags |= TTM_PL_FLAG_VRAM;
	if (domain & NOUVEAU_GEM_DOMAIN_GART)
		flags |= TTM_PL_FLAG_TT;
	if (!flags || domain & NOUVEAU_GEM_DOMAIN_CPU)
		flags |= TTM_PL_FLAG_SYSTEM;

	ret = nouveau_bo_new(dev, chan, size, align, flags, tile_mode,
			     tile_flags, pnvbo);
	if (ret)
		return ret;
	nvbo = *pnvbo;

	/* we restrict allowed domains on nv50+ to only the types
	 * that were requested at creation time.  not possibly on
	 * earlier chips without busting the ABI.
	 */
	nvbo->valid_domains = NOUVEAU_GEM_DOMAIN_VRAM |
			      NOUVEAU_GEM_DOMAIN_GART;
	if (dev_priv->card_type >= NV_50)
		nvbo->valid_domains &= domain;

	nvbo->gem = drm_gem_object_alloc(dev, nvbo->bo.mem.size);
	if (!nvbo->gem) {
		nouveau_bo_ref(NULL, pnvbo);
		return -ENOMEM;
	}

	nvbo->bo.persistent_swap_storage = nvbo->gem->filp;
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
	rep->map_handle = nvbo->bo.addr_space_offset;
	rep->tile_mode = nvbo->tile_mode;
	rep->tile_flags = nvbo->tile_flags;
	return 0;
}

int
nouveau_gem_ioctl_new(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_nouveau_gem_new *req = data;
	struct nouveau_bo *nvbo = NULL;
	struct nouveau_channel *chan = NULL;
	int ret = 0;

	if (unlikely(dev_priv->ttm.bdev.dev_mapping == NULL))
		dev_priv->ttm.bdev.dev_mapping = dev_priv->dev->dev_mapping;

	if (!dev_priv->engine.vram.flags_valid(dev, req->info.tile_flags)) {
		NV_ERROR(dev, "bad page flags: 0x%08x\n", req->info.tile_flags);
		return -EINVAL;
	}

	if (req->channel_hint) {
		chan = nouveau_channel_get(dev, file_priv, req->channel_hint);
		if (IS_ERR(chan))
			return PTR_ERR(chan);
	}

	ret = nouveau_gem_new(dev, chan, req->info.size, req->align,
			      req->info.domain, req->info.tile_mode,
			      req->info.tile_flags, &nvbo);
	if (chan)
		nouveau_channel_put(&chan);
	if (ret)
		return ret;

	ret = nouveau_gem_info(nvbo->gem, &req->info);
	if (ret)
		goto out;

	ret = drm_gem_handle_create(file_priv, nvbo->gem, &req->info.handle);
	/* drop reference from allocate - handle holds it now */
	drm_gem_object_unreference_unlocked(nvbo->gem);
out:
	return ret;
}

static int
nouveau_gem_set_domain(struct drm_gem_object *gem, uint32_t read_domains,
		       uint32_t write_domains, uint32_t valid_domains)
{
	struct nouveau_bo *nvbo = gem->driver_private;
	struct ttm_buffer_object *bo = &nvbo->bo;
	uint32_t domains = valid_domains & nvbo->valid_domains &
		(write_domains ? write_domains : read_domains);
	uint32_t pref_flags = 0, valid_flags = 0;

	if (!domains)
		return -EINVAL;

	if (valid_domains & NOUVEAU_GEM_DOMAIN_VRAM)
		valid_flags |= TTM_PL_FLAG_VRAM;

	if (valid_domains & NOUVEAU_GEM_DOMAIN_GART)
		valid_flags |= TTM_PL_FLAG_TT;

	if ((domains & NOUVEAU_GEM_DOMAIN_VRAM) &&
	    bo->mem.mem_type == TTM_PL_VRAM)
		pref_flags |= TTM_PL_FLAG_VRAM;

	else if ((domains & NOUVEAU_GEM_DOMAIN_GART) &&
		 bo->mem.mem_type == TTM_PL_TT)
		pref_flags |= TTM_PL_FLAG_TT;

	else if (domains & NOUVEAU_GEM_DOMAIN_VRAM)
		pref_flags |= TTM_PL_FLAG_VRAM;

	else
		pref_flags |= TTM_PL_FLAG_TT;

	nouveau_bo_placement_set(nvbo, pref_flags, valid_flags);

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

		nouveau_bo_fence(nvbo, fence);

		if (unlikely(nvbo->validate_mapped)) {
			ttm_bo_kunmap(&nvbo->kmap);
			nvbo->validate_mapped = false;
		}

		list_del(&nvbo->entry);
		nvbo->reserved_by = NULL;
		ttm_bo_unreserve(&nvbo->bo);
		drm_gem_object_unreference_unlocked(nvbo->gem);
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
			return -ENOENT;
		}
		nvbo = gem->driver_private;

		if (nvbo->reserved_by && nvbo->reserved_by == file_priv) {
			NV_ERROR(dev, "multiple instances of buffer %d on "
				      "validation list\n", b->handle);
			validate_fini(op, NULL);
			return -EINVAL;
		}

		ret = ttm_bo_reserve(&nvbo->bo, true, false, true, sequence);
		if (ret) {
			validate_fini(op, NULL);
			if (unlikely(ret == -EAGAIN))
				ret = ttm_bo_wait_unreserved(&nvbo->bo, true);
			drm_gem_object_unreference_unlocked(gem);
			if (unlikely(ret)) {
				if (ret != -ERESTARTSYS)
					NV_ERROR(dev, "fail reserve\n");
				return ret;
			}
			goto retry;
		}

		b->user_priv = (uint64_t)(unsigned long)nvbo;
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
	}

	return 0;
}

static int
validate_list(struct nouveau_channel *chan, struct list_head *list,
	      struct drm_nouveau_gem_pushbuf_bo *pbbo, uint64_t user_pbbo_ptr)
{
	struct drm_nouveau_gem_pushbuf_bo __user *upbbo =
				(void __force __user *)(uintptr_t)user_pbbo_ptr;
	struct drm_device *dev = chan->dev;
	struct nouveau_bo *nvbo;
	int ret, relocs = 0;

	list_for_each_entry(nvbo, list, entry) {
		struct drm_nouveau_gem_pushbuf_bo *b = &pbbo[nvbo->pbbo_index];

		ret = nouveau_fence_sync(nvbo->bo.sync_obj, chan);
		if (unlikely(ret)) {
			NV_ERROR(dev, "fail pre-validate sync\n");
			return ret;
		}

		ret = nouveau_gem_set_domain(nvbo->gem, b->read_domains,
					     b->write_domains,
					     b->valid_domains);
		if (unlikely(ret)) {
			NV_ERROR(dev, "fail set_domain\n");
			return ret;
		}

		nvbo->channel = (b->read_domains & (1 << 31)) ? NULL : chan;
		ret = nouveau_bo_validate(nvbo, true, false, false);
		nvbo->channel = NULL;
		if (unlikely(ret)) {
			if (ret != -ERESTARTSYS)
				NV_ERROR(dev, "fail ttm_validate\n");
			return ret;
		}

		ret = nouveau_fence_sync(nvbo->bo.sync_obj, chan);
		if (unlikely(ret)) {
			NV_ERROR(dev, "fail post-validate sync\n");
			return ret;
		}

		if (nvbo->bo.offset == b->presumed.offset &&
		    ((nvbo->bo.mem.mem_type == TTM_PL_VRAM &&
		      b->presumed.domain & NOUVEAU_GEM_DOMAIN_VRAM) ||
		     (nvbo->bo.mem.mem_type == TTM_PL_TT &&
		      b->presumed.domain & NOUVEAU_GEM_DOMAIN_GART)))
			continue;

		if (nvbo->bo.mem.mem_type == TTM_PL_TT)
			b->presumed.domain = NOUVEAU_GEM_DOMAIN_GART;
		else
			b->presumed.domain = NOUVEAU_GEM_DOMAIN_VRAM;
		b->presumed.offset = nvbo->bo.offset;
		b->presumed.valid = 0;
		relocs++;

		if (DRM_COPY_TO_USER(&upbbo[nvbo->pbbo_index].presumed,
				     &b->presumed, sizeof(b->presumed)))
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
	struct drm_device *dev = chan->dev;
	int ret, relocs = 0;

	INIT_LIST_HEAD(&op->vram_list);
	INIT_LIST_HEAD(&op->gart_list);
	INIT_LIST_HEAD(&op->both_list);

	if (nr_buffers == 0)
		return 0;

	ret = validate_init(chan, file_priv, pbbo, nr_buffers, op);
	if (unlikely(ret)) {
		if (ret != -ERESTARTSYS)
			NV_ERROR(dev, "validate_init\n");
		return ret;
	}

	ret = validate_list(chan, &op->vram_list, pbbo, user_buffers);
	if (unlikely(ret < 0)) {
		if (ret != -ERESTARTSYS)
			NV_ERROR(dev, "validate vram_list\n");
		validate_fini(op, NULL);
		return ret;
	}
	relocs += ret;

	ret = validate_list(chan, &op->gart_list, pbbo, user_buffers);
	if (unlikely(ret < 0)) {
		if (ret != -ERESTARTSYS)
			NV_ERROR(dev, "validate gart_list\n");
		validate_fini(op, NULL);
		return ret;
	}
	relocs += ret;

	ret = validate_list(chan, &op->both_list, pbbo, user_buffers);
	if (unlikely(ret < 0)) {
		if (ret != -ERESTARTSYS)
			NV_ERROR(dev, "validate both_list\n");
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
nouveau_gem_pushbuf_reloc_apply(struct drm_device *dev,
				struct drm_nouveau_gem_pushbuf *req,
				struct drm_nouveau_gem_pushbuf_bo *bo)
{
	struct drm_nouveau_gem_pushbuf_reloc *reloc = NULL;
	int ret = 0;
	unsigned i;

	reloc = u_memcpya(req->relocs, req->nr_relocs, sizeof(*reloc));
	if (IS_ERR(reloc))
		return PTR_ERR(reloc);

	for (i = 0; i < req->nr_relocs; i++) {
		struct drm_nouveau_gem_pushbuf_reloc *r = &reloc[i];
		struct drm_nouveau_gem_pushbuf_bo *b;
		struct nouveau_bo *nvbo;
		uint32_t data;

		if (unlikely(r->bo_index > req->nr_buffers)) {
			NV_ERROR(dev, "reloc bo index invalid\n");
			ret = -EINVAL;
			break;
		}

		b = &bo[r->bo_index];
		if (b->presumed.valid)
			continue;

		if (unlikely(r->reloc_bo_index > req->nr_buffers)) {
			NV_ERROR(dev, "reloc container bo index invalid\n");
			ret = -EINVAL;
			break;
		}
		nvbo = (void *)(unsigned long)bo[r->reloc_bo_index].user_priv;

		if (unlikely(r->reloc_bo_offset + 4 >
			     nvbo->bo.mem.num_pages << PAGE_SHIFT)) {
			NV_ERROR(dev, "reloc outside of bo\n");
			ret = -EINVAL;
			break;
		}

		if (!nvbo->kmap.virtual) {
			ret = ttm_bo_kmap(&nvbo->bo, 0, nvbo->bo.mem.num_pages,
					  &nvbo->kmap);
			if (ret) {
				NV_ERROR(dev, "failed kmap for reloc\n");
				break;
			}
			nvbo->validate_mapped = true;
		}

		if (r->flags & NOUVEAU_GEM_RELOC_LOW)
			data = b->presumed.offset + r->data;
		else
		if (r->flags & NOUVEAU_GEM_RELOC_HIGH)
			data = (b->presumed.offset + r->data) >> 32;
		else
			data = r->data;

		if (r->flags & NOUVEAU_GEM_RELOC_OR) {
			if (b->presumed.domain == NOUVEAU_GEM_DOMAIN_GART)
				data |= r->tor;
			else
				data |= r->vor;
		}

		spin_lock(&nvbo->bo.bdev->fence_lock);
		ret = ttm_bo_wait(&nvbo->bo, false, false, false);
		spin_unlock(&nvbo->bo.bdev->fence_lock);
		if (ret) {
			NV_ERROR(dev, "reloc wait_idle failed: %d\n", ret);
			break;
		}

		nouveau_bo_wr32(nvbo, r->reloc_bo_offset >> 2, data);
	}

	kfree(reloc);
	return ret;
}

int
nouveau_gem_ioctl_pushbuf(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_nouveau_gem_pushbuf *req = data;
	struct drm_nouveau_gem_pushbuf_push *push;
	struct drm_nouveau_gem_pushbuf_bo *bo;
	struct nouveau_channel *chan;
	struct validate_op op;
	struct nouveau_fence *fence = NULL;
	int i, j, ret = 0, do_reloc = 0;

	chan = nouveau_channel_get(dev, file_priv, req->channel);
	if (IS_ERR(chan))
		return PTR_ERR(chan);

	req->vram_available = dev_priv->fb_aper_free;
	req->gart_available = dev_priv->gart_info.aper_free;
	if (unlikely(req->nr_push == 0))
		goto out_next;

	if (unlikely(req->nr_push > NOUVEAU_GEM_MAX_PUSH)) {
		NV_ERROR(dev, "pushbuf push count exceeds limit: %d max %d\n",
			 req->nr_push, NOUVEAU_GEM_MAX_PUSH);
		nouveau_channel_put(&chan);
		return -EINVAL;
	}

	if (unlikely(req->nr_buffers > NOUVEAU_GEM_MAX_BUFFERS)) {
		NV_ERROR(dev, "pushbuf bo count exceeds limit: %d max %d\n",
			 req->nr_buffers, NOUVEAU_GEM_MAX_BUFFERS);
		nouveau_channel_put(&chan);
		return -EINVAL;
	}

	if (unlikely(req->nr_relocs > NOUVEAU_GEM_MAX_RELOCS)) {
		NV_ERROR(dev, "pushbuf reloc count exceeds limit: %d max %d\n",
			 req->nr_relocs, NOUVEAU_GEM_MAX_RELOCS);
		nouveau_channel_put(&chan);
		return -EINVAL;
	}

	push = u_memcpya(req->push, req->nr_push, sizeof(*push));
	if (IS_ERR(push)) {
		nouveau_channel_put(&chan);
		return PTR_ERR(push);
	}

	bo = u_memcpya(req->buffers, req->nr_buffers, sizeof(*bo));
	if (IS_ERR(bo)) {
		kfree(push);
		nouveau_channel_put(&chan);
		return PTR_ERR(bo);
	}

	/* Mark push buffers as being used on PFIFO, the validation code
	 * will then make sure that if the pushbuf bo moves, that they
	 * happen on the kernel channel, which will in turn cause a sync
	 * to happen before we try and submit the push buffer.
	 */
	for (i = 0; i < req->nr_push; i++) {
		if (push[i].bo_index >= req->nr_buffers) {
			NV_ERROR(dev, "push %d buffer not in list\n", i);
			ret = -EINVAL;
			goto out_prevalid;
		}

		bo[push[i].bo_index].read_domains |= (1 << 31);
	}

	/* Validate buffer list */
	ret = nouveau_gem_pushbuf_validate(chan, file_priv, bo, req->buffers,
					   req->nr_buffers, &op, &do_reloc);
	if (ret) {
		if (ret != -ERESTARTSYS)
			NV_ERROR(dev, "validate: %d\n", ret);
		goto out_prevalid;
	}

	/* Apply any relocations that are required */
	if (do_reloc) {
		ret = nouveau_gem_pushbuf_reloc_apply(dev, req, bo);
		if (ret) {
			NV_ERROR(dev, "reloc apply: %d\n", ret);
			goto out;
		}
	}

	if (chan->dma.ib_max) {
		ret = nouveau_dma_wait(chan, req->nr_push + 1, 6);
		if (ret) {
			NV_INFO(dev, "nv50cal_space: %d\n", ret);
			goto out;
		}

		for (i = 0; i < req->nr_push; i++) {
			struct nouveau_bo *nvbo = (void *)(unsigned long)
				bo[push[i].bo_index].user_priv;

			nv50_dma_push(chan, nvbo, push[i].offset,
				      push[i].length);
		}
	} else
	if (dev_priv->chipset >= 0x25) {
		ret = RING_SPACE(chan, req->nr_push * 2);
		if (ret) {
			NV_ERROR(dev, "cal_space: %d\n", ret);
			goto out;
		}

		for (i = 0; i < req->nr_push; i++) {
			struct nouveau_bo *nvbo = (void *)(unsigned long)
				bo[push[i].bo_index].user_priv;
			struct drm_mm_node *mem = nvbo->bo.mem.mm_node;

			OUT_RING(chan, ((mem->start << PAGE_SHIFT) +
					push[i].offset) | 2);
			OUT_RING(chan, 0);
		}
	} else {
		ret = RING_SPACE(chan, req->nr_push * (2 + NOUVEAU_DMA_SKIPS));
		if (ret) {
			NV_ERROR(dev, "jmp_space: %d\n", ret);
			goto out;
		}

		for (i = 0; i < req->nr_push; i++) {
			struct nouveau_bo *nvbo = (void *)(unsigned long)
				bo[push[i].bo_index].user_priv;
			struct drm_mm_node *mem = nvbo->bo.mem.mm_node;
			uint32_t cmd;

			cmd = chan->pushbuf_base + ((chan->dma.cur + 2) << 2);
			cmd |= 0x20000000;
			if (unlikely(cmd != req->suffix0)) {
				if (!nvbo->kmap.virtual) {
					ret = ttm_bo_kmap(&nvbo->bo, 0,
							  nvbo->bo.mem.
							  num_pages,
							  &nvbo->kmap);
					if (ret) {
						WIND_RING(chan);
						goto out;
					}
					nvbo->validate_mapped = true;
				}

				nouveau_bo_wr32(nvbo, (push[i].offset +
						push[i].length - 8) / 4, cmd);
			}

			OUT_RING(chan, ((mem->start << PAGE_SHIFT) +
					push[i].offset) | 0x20000000);
			OUT_RING(chan, 0);
			for (j = 0; j < NOUVEAU_DMA_SKIPS; j++)
				OUT_RING(chan, 0);
		}
	}

	ret = nouveau_fence_new(chan, &fence, true);
	if (ret) {
		NV_ERROR(dev, "error fencing pushbuf: %d\n", ret);
		WIND_RING(chan);
		goto out;
	}

out:
	validate_fini(&op, fence);
	nouveau_fence_unref(&fence);

out_prevalid:
	kfree(bo);
	kfree(push);

out_next:
	if (chan->dma.ib_max) {
		req->suffix0 = 0x00000000;
		req->suffix1 = 0x00000000;
	} else
	if (dev_priv->chipset >= 0x25) {
		req->suffix0 = 0x00020000;
		req->suffix1 = 0x00000000;
	} else {
		req->suffix0 = 0x20000000 |
			      (chan->pushbuf_base + ((chan->dma.cur + 2) << 2));
		req->suffix1 = 0x00000000;
	}

	nouveau_channel_put(&chan);
	return ret;
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
nouveau_gem_ioctl_cpu_prep(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	struct drm_nouveau_gem_cpu_prep *req = data;
	struct drm_gem_object *gem;
	struct nouveau_bo *nvbo;
	bool no_wait = !!(req->flags & NOUVEAU_GEM_CPU_PREP_NOWAIT);
	int ret = -EINVAL;

	gem = drm_gem_object_lookup(dev, file_priv, req->handle);
	if (!gem)
		return -ENOENT;
	nvbo = nouveau_gem_object(gem);

	spin_lock(&nvbo->bo.bdev->fence_lock);
	ret = ttm_bo_wait(&nvbo->bo, true, true, no_wait);
	spin_unlock(&nvbo->bo.bdev->fence_lock);
	drm_gem_object_unreference_unlocked(gem);
	return ret;
}

int
nouveau_gem_ioctl_cpu_fini(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	return 0;
}

int
nouveau_gem_ioctl_info(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct drm_nouveau_gem_info *req = data;
	struct drm_gem_object *gem;
	int ret;

	gem = drm_gem_object_lookup(dev, file_priv, req->handle);
	if (!gem)
		return -ENOENT;

	ret = nouveau_gem_info(gem, req);
	drm_gem_object_unreference_unlocked(gem);
	return ret;
}

