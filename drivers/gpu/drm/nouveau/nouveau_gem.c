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

#include "nouveau_drm.h"
#include "nouveau_dma.h"
#include "nouveau_fence.h"
#include "nouveau_abi16.h"

#include "nouveau_ttm.h"
#include "nouveau_gem.h"

void
nouveau_gem_object_del(struct drm_gem_object *gem)
{
	struct nouveau_bo *nvbo = nouveau_gem_object(gem);
	struct nouveau_drm *drm = nouveau_bdev(nvbo->bo.bdev);
	struct ttm_buffer_object *bo = &nvbo->bo;
	struct device *dev = drm->dev->dev;
	int ret;

	ret = pm_runtime_get_sync(dev);
	if (WARN_ON(ret < 0 && ret != -EACCES))
		return;

	if (gem->import_attach)
		drm_prime_gem_destroy(gem, nvbo->bo.sg);

	drm_gem_object_release(gem);

	/* reset filp so nouveau_bo_del_ttm() can test for it */
	gem->filp = NULL;
	ttm_bo_unref(&bo);

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
}

int
nouveau_gem_object_open(struct drm_gem_object *gem, struct drm_file *file_priv)
{
	struct nouveau_cli *cli = nouveau_cli(file_priv);
	struct nouveau_bo *nvbo = nouveau_gem_object(gem);
	struct nouveau_drm *drm = nouveau_bdev(nvbo->bo.bdev);
	struct nvkm_vma *vma;
	struct device *dev = drm->dev->dev;
	int ret;

	if (!cli->vm)
		return 0;

	ret = ttm_bo_reserve(&nvbo->bo, false, false, false, NULL);
	if (ret)
		return ret;

	vma = nouveau_bo_vma_find(nvbo, cli->vm);
	if (!vma) {
		vma = kzalloc(sizeof(*vma), GFP_KERNEL);
		if (!vma) {
			ret = -ENOMEM;
			goto out;
		}

		ret = pm_runtime_get_sync(dev);
		if (ret < 0 && ret != -EACCES)
			goto out;

		ret = nouveau_bo_vma_add(nvbo, cli->vm, vma);
		if (ret)
			kfree(vma);

		pm_runtime_mark_last_busy(dev);
		pm_runtime_put_autosuspend(dev);
	} else {
		vma->refcount++;
	}

out:
	ttm_bo_unreserve(&nvbo->bo);
	return ret;
}

static void
nouveau_gem_object_delete(void *data)
{
	struct nvkm_vma *vma = data;
	nvkm_vm_unmap(vma);
	nvkm_vm_put(vma);
	kfree(vma);
}

static void
nouveau_gem_object_unmap(struct nouveau_bo *nvbo, struct nvkm_vma *vma)
{
	const bool mapped = nvbo->bo.mem.mem_type != TTM_PL_SYSTEM;
	struct reservation_object *resv = nvbo->bo.resv;
	struct reservation_object_list *fobj;
	struct fence *fence = NULL;

	fobj = reservation_object_get_list(resv);

	list_del(&vma->head);

	if (fobj && fobj->shared_count > 1)
		ttm_bo_wait(&nvbo->bo, true, false, false);
	else if (fobj && fobj->shared_count == 1)
		fence = rcu_dereference_protected(fobj->shared[0],
						reservation_object_held(resv));
	else
		fence = reservation_object_get_excl(nvbo->bo.resv);

	if (fence && mapped) {
		nouveau_fence_work(fence, nouveau_gem_object_delete, vma);
	} else {
		if (mapped)
			nvkm_vm_unmap(vma);
		nvkm_vm_put(vma);
		kfree(vma);
	}
}

void
nouveau_gem_object_close(struct drm_gem_object *gem, struct drm_file *file_priv)
{
	struct nouveau_cli *cli = nouveau_cli(file_priv);
	struct nouveau_bo *nvbo = nouveau_gem_object(gem);
	struct nouveau_drm *drm = nouveau_bdev(nvbo->bo.bdev);
	struct device *dev = drm->dev->dev;
	struct nvkm_vma *vma;
	int ret;

	if (!cli->vm)
		return;

	ret = ttm_bo_reserve(&nvbo->bo, false, false, false, NULL);
	if (ret)
		return;

	vma = nouveau_bo_vma_find(nvbo, cli->vm);
	if (vma) {
		if (--vma->refcount == 0) {
			ret = pm_runtime_get_sync(dev);
			if (!WARN_ON(ret < 0 && ret != -EACCES)) {
				nouveau_gem_object_unmap(nvbo, vma);
				pm_runtime_mark_last_busy(dev);
				pm_runtime_put_autosuspend(dev);
			}
		}
	}
	ttm_bo_unreserve(&nvbo->bo);
}

int
nouveau_gem_new(struct drm_device *dev, int size, int align, uint32_t domain,
		uint32_t tile_mode, uint32_t tile_flags,
		struct nouveau_bo **pnvbo)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_bo *nvbo;
	u32 flags = 0;
	int ret;

	if (domain & NOUVEAU_GEM_DOMAIN_VRAM)
		flags |= TTM_PL_FLAG_VRAM;
	if (domain & NOUVEAU_GEM_DOMAIN_GART)
		flags |= TTM_PL_FLAG_TT;
	if (!flags || domain & NOUVEAU_GEM_DOMAIN_CPU)
		flags |= TTM_PL_FLAG_SYSTEM;

	ret = nouveau_bo_new(dev, size, align, flags, tile_mode,
			     tile_flags, NULL, NULL, pnvbo);
	if (ret)
		return ret;
	nvbo = *pnvbo;

	/* we restrict allowed domains on nv50+ to only the types
	 * that were requested at creation time.  not possibly on
	 * earlier chips without busting the ABI.
	 */
	nvbo->valid_domains = NOUVEAU_GEM_DOMAIN_VRAM |
			      NOUVEAU_GEM_DOMAIN_GART;
	if (drm->device.info.family >= NV_DEVICE_INFO_V0_TESLA)
		nvbo->valid_domains &= domain;

	/* Initialize the embedded gem-object. We return a single gem-reference
	 * to the caller, instead of a normal nouveau_bo ttm reference. */
	ret = drm_gem_object_init(dev, &nvbo->gem, nvbo->bo.mem.size);
	if (ret) {
		nouveau_bo_ref(NULL, pnvbo);
		return -ENOMEM;
	}

	nvbo->bo.persistent_swap_storage = nvbo->gem.filp;
	return 0;
}

static int
nouveau_gem_info(struct drm_file *file_priv, struct drm_gem_object *gem,
		 struct drm_nouveau_gem_info *rep)
{
	struct nouveau_cli *cli = nouveau_cli(file_priv);
	struct nouveau_bo *nvbo = nouveau_gem_object(gem);
	struct nvkm_vma *vma;

	if (nvbo->bo.mem.mem_type == TTM_PL_TT)
		rep->domain = NOUVEAU_GEM_DOMAIN_GART;
	else
		rep->domain = NOUVEAU_GEM_DOMAIN_VRAM;

	rep->offset = nvbo->bo.offset;
	if (cli->vm) {
		vma = nouveau_bo_vma_find(nvbo, cli->vm);
		if (!vma)
			return -EINVAL;

		rep->offset = vma->offset;
	}

	rep->size = nvbo->bo.mem.num_pages << PAGE_SHIFT;
	rep->map_handle = drm_vma_node_offset_addr(&nvbo->bo.vma_node);
	rep->tile_mode = nvbo->tile_mode;
	rep->tile_flags = nvbo->tile_flags;
	return 0;
}

int
nouveau_gem_ioctl_new(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_cli *cli = nouveau_cli(file_priv);
	struct nvkm_fb *pfb = nvxx_fb(&drm->device);
	struct drm_nouveau_gem_new *req = data;
	struct nouveau_bo *nvbo = NULL;
	int ret = 0;

	if (!pfb->memtype_valid(pfb, req->info.tile_flags)) {
		NV_PRINTK(error, cli, "bad page flags: 0x%08x\n", req->info.tile_flags);
		return -EINVAL;
	}

	ret = nouveau_gem_new(dev, req->info.size, req->align,
			      req->info.domain, req->info.tile_mode,
			      req->info.tile_flags, &nvbo);
	if (ret)
		return ret;

	ret = drm_gem_handle_create(file_priv, &nvbo->gem, &req->info.handle);
	if (ret == 0) {
		ret = nouveau_gem_info(file_priv, &nvbo->gem, &req->info);
		if (ret)
			drm_gem_handle_delete(file_priv, req->info.handle);
	}

	/* drop reference from allocate - handle holds it now */
	drm_gem_object_unreference_unlocked(&nvbo->gem);
	return ret;
}

static int
nouveau_gem_set_domain(struct drm_gem_object *gem, uint32_t read_domains,
		       uint32_t write_domains, uint32_t valid_domains)
{
	struct nouveau_bo *nvbo = nouveau_gem_object(gem);
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
	struct list_head list;
	struct ww_acquire_ctx ticket;
};

static void
validate_fini_no_ticket(struct validate_op *op, struct nouveau_fence *fence,
			struct drm_nouveau_gem_pushbuf_bo *pbbo)
{
	struct nouveau_bo *nvbo;
	struct drm_nouveau_gem_pushbuf_bo *b;

	while (!list_empty(&op->list)) {
		nvbo = list_entry(op->list.next, struct nouveau_bo, entry);
		b = &pbbo[nvbo->pbbo_index];

		if (likely(fence))
			nouveau_bo_fence(nvbo, fence, !!b->write_domains);

		if (unlikely(nvbo->validate_mapped)) {
			ttm_bo_kunmap(&nvbo->kmap);
			nvbo->validate_mapped = false;
		}

		list_del(&nvbo->entry);
		nvbo->reserved_by = NULL;
		ttm_bo_unreserve_ticket(&nvbo->bo, &op->ticket);
		drm_gem_object_unreference_unlocked(&nvbo->gem);
	}
}

static void
validate_fini(struct validate_op *op, struct nouveau_fence *fence,
	      struct drm_nouveau_gem_pushbuf_bo *pbbo)
{
	validate_fini_no_ticket(op, fence, pbbo);
	ww_acquire_fini(&op->ticket);
}

static int
validate_init(struct nouveau_channel *chan, struct drm_file *file_priv,
	      struct drm_nouveau_gem_pushbuf_bo *pbbo,
	      int nr_buffers, struct validate_op *op)
{
	struct nouveau_cli *cli = nouveau_cli(file_priv);
	struct drm_device *dev = chan->drm->dev;
	int trycnt = 0;
	int ret, i;
	struct nouveau_bo *res_bo = NULL;
	LIST_HEAD(gart_list);
	LIST_HEAD(vram_list);
	LIST_HEAD(both_list);

	ww_acquire_init(&op->ticket, &reservation_ww_class);
retry:
	if (++trycnt > 100000) {
		NV_PRINTK(error, cli, "%s failed and gave up.\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < nr_buffers; i++) {
		struct drm_nouveau_gem_pushbuf_bo *b = &pbbo[i];
		struct drm_gem_object *gem;
		struct nouveau_bo *nvbo;

		gem = drm_gem_object_lookup(dev, file_priv, b->handle);
		if (!gem) {
			NV_PRINTK(error, cli, "Unknown handle 0x%08x\n", b->handle);
			ret = -ENOENT;
			break;
		}
		nvbo = nouveau_gem_object(gem);
		if (nvbo == res_bo) {
			res_bo = NULL;
			drm_gem_object_unreference_unlocked(gem);
			continue;
		}

		if (nvbo->reserved_by && nvbo->reserved_by == file_priv) {
			NV_PRINTK(error, cli, "multiple instances of buffer %d on "
				      "validation list\n", b->handle);
			drm_gem_object_unreference_unlocked(gem);
			ret = -EINVAL;
			break;
		}

		ret = ttm_bo_reserve(&nvbo->bo, true, false, true, &op->ticket);
		if (ret) {
			list_splice_tail_init(&vram_list, &op->list);
			list_splice_tail_init(&gart_list, &op->list);
			list_splice_tail_init(&both_list, &op->list);
			validate_fini_no_ticket(op, NULL, NULL);
			if (unlikely(ret == -EDEADLK)) {
				ret = ttm_bo_reserve_slowpath(&nvbo->bo, true,
							      &op->ticket);
				if (!ret)
					res_bo = nvbo;
			}
			if (unlikely(ret)) {
				if (ret != -ERESTARTSYS)
					NV_PRINTK(error, cli, "fail reserve\n");
				break;
			}
		}

		b->user_priv = (uint64_t)(unsigned long)nvbo;
		nvbo->reserved_by = file_priv;
		nvbo->pbbo_index = i;
		if ((b->valid_domains & NOUVEAU_GEM_DOMAIN_VRAM) &&
		    (b->valid_domains & NOUVEAU_GEM_DOMAIN_GART))
			list_add_tail(&nvbo->entry, &both_list);
		else
		if (b->valid_domains & NOUVEAU_GEM_DOMAIN_VRAM)
			list_add_tail(&nvbo->entry, &vram_list);
		else
		if (b->valid_domains & NOUVEAU_GEM_DOMAIN_GART)
			list_add_tail(&nvbo->entry, &gart_list);
		else {
			NV_PRINTK(error, cli, "invalid valid domains: 0x%08x\n",
				 b->valid_domains);
			list_add_tail(&nvbo->entry, &both_list);
			ret = -EINVAL;
			break;
		}
		if (nvbo == res_bo)
			goto retry;
	}

	ww_acquire_done(&op->ticket);
	list_splice_tail(&vram_list, &op->list);
	list_splice_tail(&gart_list, &op->list);
	list_splice_tail(&both_list, &op->list);
	if (ret)
		validate_fini(op, NULL, NULL);
	return ret;

}

static int
validate_list(struct nouveau_channel *chan, struct nouveau_cli *cli,
	      struct list_head *list, struct drm_nouveau_gem_pushbuf_bo *pbbo,
	      uint64_t user_pbbo_ptr)
{
	struct nouveau_drm *drm = chan->drm;
	struct drm_nouveau_gem_pushbuf_bo __user *upbbo =
				(void __force __user *)(uintptr_t)user_pbbo_ptr;
	struct nouveau_bo *nvbo;
	int ret, relocs = 0;

	list_for_each_entry(nvbo, list, entry) {
		struct drm_nouveau_gem_pushbuf_bo *b = &pbbo[nvbo->pbbo_index];

		ret = nouveau_gem_set_domain(&nvbo->gem, b->read_domains,
					     b->write_domains,
					     b->valid_domains);
		if (unlikely(ret)) {
			NV_PRINTK(error, cli, "fail set_domain\n");
			return ret;
		}

		ret = nouveau_bo_validate(nvbo, true, false);
		if (unlikely(ret)) {
			if (ret != -ERESTARTSYS)
				NV_PRINTK(error, cli, "fail ttm_validate\n");
			return ret;
		}

		ret = nouveau_fence_sync(nvbo, chan, !!b->write_domains, true);
		if (unlikely(ret)) {
			if (ret != -ERESTARTSYS)
				NV_PRINTK(error, cli, "fail post-validate sync\n");
			return ret;
		}

		if (drm->device.info.family < NV_DEVICE_INFO_V0_TESLA) {
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

			if (copy_to_user(&upbbo[nvbo->pbbo_index].presumed,
					     &b->presumed, sizeof(b->presumed)))
				return -EFAULT;
		}
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
	struct nouveau_cli *cli = nouveau_cli(file_priv);
	int ret;

	INIT_LIST_HEAD(&op->list);

	if (nr_buffers == 0)
		return 0;

	ret = validate_init(chan, file_priv, pbbo, nr_buffers, op);
	if (unlikely(ret)) {
		if (ret != -ERESTARTSYS)
			NV_PRINTK(error, cli, "validate_init\n");
		return ret;
	}

	ret = validate_list(chan, cli, &op->list, pbbo, user_buffers);
	if (unlikely(ret < 0)) {
		if (ret != -ERESTARTSYS)
			NV_PRINTK(error, cli, "validating bo list\n");
		validate_fini(op, NULL, NULL);
		return ret;
	}
	*apply_relocs = ret;
	return 0;
}

static inline void
u_free(void *addr)
{
	if (!is_vmalloc_addr(addr))
		kfree(addr);
	else
		vfree(addr);
}

static inline void *
u_memcpya(uint64_t user, unsigned nmemb, unsigned size)
{
	void *mem;
	void __user *userptr = (void __force __user *)(uintptr_t)user;

	size *= nmemb;

	mem = kmalloc(size, GFP_KERNEL | __GFP_NOWARN);
	if (!mem)
		mem = vmalloc(size);
	if (!mem)
		return ERR_PTR(-ENOMEM);

	if (copy_from_user(mem, userptr, size)) {
		u_free(mem);
		return ERR_PTR(-EFAULT);
	}

	return mem;
}

static int
nouveau_gem_pushbuf_reloc_apply(struct nouveau_cli *cli,
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
			NV_PRINTK(error, cli, "reloc bo index invalid\n");
			ret = -EINVAL;
			break;
		}

		b = &bo[r->bo_index];
		if (b->presumed.valid)
			continue;

		if (unlikely(r->reloc_bo_index > req->nr_buffers)) {
			NV_PRINTK(error, cli, "reloc container bo index invalid\n");
			ret = -EINVAL;
			break;
		}
		nvbo = (void *)(unsigned long)bo[r->reloc_bo_index].user_priv;

		if (unlikely(r->reloc_bo_offset + 4 >
			     nvbo->bo.mem.num_pages << PAGE_SHIFT)) {
			NV_PRINTK(error, cli, "reloc outside of bo\n");
			ret = -EINVAL;
			break;
		}

		if (!nvbo->kmap.virtual) {
			ret = ttm_bo_kmap(&nvbo->bo, 0, nvbo->bo.mem.num_pages,
					  &nvbo->kmap);
			if (ret) {
				NV_PRINTK(error, cli, "failed kmap for reloc\n");
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

		ret = ttm_bo_wait(&nvbo->bo, true, false, false);
		if (ret) {
			NV_PRINTK(error, cli, "reloc wait_idle failed: %d\n", ret);
			break;
		}

		nouveau_bo_wr32(nvbo, r->reloc_bo_offset >> 2, data);
	}

	u_free(reloc);
	return ret;
}

int
nouveau_gem_ioctl_pushbuf(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	struct nouveau_abi16 *abi16 = nouveau_abi16_get(file_priv, dev);
	struct nouveau_cli *cli = nouveau_cli(file_priv);
	struct nouveau_abi16_chan *temp;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct drm_nouveau_gem_pushbuf *req = data;
	struct drm_nouveau_gem_pushbuf_push *push;
	struct drm_nouveau_gem_pushbuf_bo *bo;
	struct nouveau_channel *chan = NULL;
	struct validate_op op;
	struct nouveau_fence *fence = NULL;
	int i, j, ret = 0, do_reloc = 0;

	if (unlikely(!abi16))
		return -ENOMEM;

	list_for_each_entry(temp, &abi16->channels, head) {
		if (temp->chan->object->handle == (NVDRM_CHAN | req->channel)) {
			chan = temp->chan;
			break;
		}
	}

	if (!chan)
		return nouveau_abi16_put(abi16, -ENOENT);

	req->vram_available = drm->gem.vram_available;
	req->gart_available = drm->gem.gart_available;
	if (unlikely(req->nr_push == 0))
		goto out_next;

	if (unlikely(req->nr_push > NOUVEAU_GEM_MAX_PUSH)) {
		NV_PRINTK(error, cli, "pushbuf push count exceeds limit: %d max %d\n",
			 req->nr_push, NOUVEAU_GEM_MAX_PUSH);
		return nouveau_abi16_put(abi16, -EINVAL);
	}

	if (unlikely(req->nr_buffers > NOUVEAU_GEM_MAX_BUFFERS)) {
		NV_PRINTK(error, cli, "pushbuf bo count exceeds limit: %d max %d\n",
			 req->nr_buffers, NOUVEAU_GEM_MAX_BUFFERS);
		return nouveau_abi16_put(abi16, -EINVAL);
	}

	if (unlikely(req->nr_relocs > NOUVEAU_GEM_MAX_RELOCS)) {
		NV_PRINTK(error, cli, "pushbuf reloc count exceeds limit: %d max %d\n",
			 req->nr_relocs, NOUVEAU_GEM_MAX_RELOCS);
		return nouveau_abi16_put(abi16, -EINVAL);
	}

	push = u_memcpya(req->push, req->nr_push, sizeof(*push));
	if (IS_ERR(push))
		return nouveau_abi16_put(abi16, PTR_ERR(push));

	bo = u_memcpya(req->buffers, req->nr_buffers, sizeof(*bo));
	if (IS_ERR(bo)) {
		u_free(push);
		return nouveau_abi16_put(abi16, PTR_ERR(bo));
	}

	/* Ensure all push buffers are on validate list */
	for (i = 0; i < req->nr_push; i++) {
		if (push[i].bo_index >= req->nr_buffers) {
			NV_PRINTK(error, cli, "push %d buffer not in list\n", i);
			ret = -EINVAL;
			goto out_prevalid;
		}
	}

	/* Validate buffer list */
	ret = nouveau_gem_pushbuf_validate(chan, file_priv, bo, req->buffers,
					   req->nr_buffers, &op, &do_reloc);
	if (ret) {
		if (ret != -ERESTARTSYS)
			NV_PRINTK(error, cli, "validate: %d\n", ret);
		goto out_prevalid;
	}

	/* Apply any relocations that are required */
	if (do_reloc) {
		ret = nouveau_gem_pushbuf_reloc_apply(cli, req, bo);
		if (ret) {
			NV_PRINTK(error, cli, "reloc apply: %d\n", ret);
			goto out;
		}
	}

	if (chan->dma.ib_max) {
		ret = nouveau_dma_wait(chan, req->nr_push + 1, 16);
		if (ret) {
			NV_PRINTK(error, cli, "nv50cal_space: %d\n", ret);
			goto out;
		}

		for (i = 0; i < req->nr_push; i++) {
			struct nouveau_bo *nvbo = (void *)(unsigned long)
				bo[push[i].bo_index].user_priv;

			nv50_dma_push(chan, nvbo, push[i].offset,
				      push[i].length);
		}
	} else
	if (drm->device.info.chipset >= 0x25) {
		ret = RING_SPACE(chan, req->nr_push * 2);
		if (ret) {
			NV_PRINTK(error, cli, "cal_space: %d\n", ret);
			goto out;
		}

		for (i = 0; i < req->nr_push; i++) {
			struct nouveau_bo *nvbo = (void *)(unsigned long)
				bo[push[i].bo_index].user_priv;

			OUT_RING(chan, (nvbo->bo.offset + push[i].offset) | 2);
			OUT_RING(chan, 0);
		}
	} else {
		ret = RING_SPACE(chan, req->nr_push * (2 + NOUVEAU_DMA_SKIPS));
		if (ret) {
			NV_PRINTK(error, cli, "jmp_space: %d\n", ret);
			goto out;
		}

		for (i = 0; i < req->nr_push; i++) {
			struct nouveau_bo *nvbo = (void *)(unsigned long)
				bo[push[i].bo_index].user_priv;
			uint32_t cmd;

			cmd = chan->push.vma.offset + ((chan->dma.cur + 2) << 2);
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

			OUT_RING(chan, 0x20000000 |
				      (nvbo->bo.offset + push[i].offset));
			OUT_RING(chan, 0);
			for (j = 0; j < NOUVEAU_DMA_SKIPS; j++)
				OUT_RING(chan, 0);
		}
	}

	ret = nouveau_fence_new(chan, false, &fence);
	if (ret) {
		NV_PRINTK(error, cli, "error fencing pushbuf: %d\n", ret);
		WIND_RING(chan);
		goto out;
	}

out:
	validate_fini(&op, fence, bo);
	nouveau_fence_unref(&fence);

out_prevalid:
	u_free(bo);
	u_free(push);

out_next:
	if (chan->dma.ib_max) {
		req->suffix0 = 0x00000000;
		req->suffix1 = 0x00000000;
	} else
	if (drm->device.info.chipset >= 0x25) {
		req->suffix0 = 0x00020000;
		req->suffix1 = 0x00000000;
	} else {
		req->suffix0 = 0x20000000 |
			      (chan->push.vma.offset + ((chan->dma.cur + 2) << 2));
		req->suffix1 = 0x00000000;
	}

	return nouveau_abi16_put(abi16, ret);
}

int
nouveau_gem_ioctl_cpu_prep(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	struct drm_nouveau_gem_cpu_prep *req = data;
	struct drm_gem_object *gem;
	struct nouveau_bo *nvbo;
	bool no_wait = !!(req->flags & NOUVEAU_GEM_CPU_PREP_NOWAIT);
	bool write = !!(req->flags & NOUVEAU_GEM_CPU_PREP_WRITE);
	int ret;

	gem = drm_gem_object_lookup(dev, file_priv, req->handle);
	if (!gem)
		return -ENOENT;
	nvbo = nouveau_gem_object(gem);

	if (no_wait)
		ret = reservation_object_test_signaled_rcu(nvbo->bo.resv, write) ? 0 : -EBUSY;
	else {
		long lret;

		lret = reservation_object_wait_timeout_rcu(nvbo->bo.resv, write, true, 30 * HZ);
		if (!lret)
			ret = -EBUSY;
		else if (lret > 0)
			ret = 0;
		else
			ret = lret;
	}
	nouveau_bo_sync_for_cpu(nvbo);
	drm_gem_object_unreference_unlocked(gem);

	return ret;
}

int
nouveau_gem_ioctl_cpu_fini(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	struct drm_nouveau_gem_cpu_fini *req = data;
	struct drm_gem_object *gem;
	struct nouveau_bo *nvbo;

	gem = drm_gem_object_lookup(dev, file_priv, req->handle);
	if (!gem)
		return -ENOENT;
	nvbo = nouveau_gem_object(gem);

	nouveau_bo_sync_for_device(nvbo);
	drm_gem_object_unreference_unlocked(gem);
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

	ret = nouveau_gem_info(file_priv, gem, req);
	drm_gem_object_unreference_unlocked(gem);
	return ret;
}

