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

#include <drm/drm_gem_ttm_helper.h>

#include "nouveau_drv.h"
#include "nouveau_dma.h"
#include "nouveau_fence.h"
#include "nouveau_abi16.h"

#include "nouveau_ttm.h"
#include "nouveau_gem.h"
#include "nouveau_mem.h"
#include "nouveau_vmm.h"

#include <nvif/class.h>
#include <nvif/push206e.h>

void
nouveau_gem_object_del(struct drm_gem_object *gem)
{
	struct nouveau_bo *nvbo = nouveau_gem_object(gem);
	struct nouveau_drm *drm = nouveau_bdev(nvbo->bo.bdev);
	struct device *dev = drm->dev->dev;
	int ret;

	ret = pm_runtime_get_sync(dev);
	if (WARN_ON(ret < 0 && ret != -EACCES)) {
		pm_runtime_put_autosuspend(dev);
		return;
	}

	if (gem->import_attach)
		drm_prime_gem_destroy(gem, nvbo->bo.sg);

	ttm_bo_put(&nvbo->bo);

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
}

int
nouveau_gem_object_open(struct drm_gem_object *gem, struct drm_file *file_priv)
{
	struct nouveau_cli *cli = nouveau_cli(file_priv);
	struct nouveau_bo *nvbo = nouveau_gem_object(gem);
	struct nouveau_drm *drm = nouveau_bdev(nvbo->bo.bdev);
	struct device *dev = drm->dev->dev;
	struct nouveau_vmm *vmm = cli->svm.cli ? &cli->svm : &cli->vmm;
	struct nouveau_vma *vma;
	int ret;

	if (vmm->vmm.object.oclass < NVIF_CLASS_VMM_NV50)
		return 0;

	ret = ttm_bo_reserve(&nvbo->bo, false, false, NULL);
	if (ret)
		return ret;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0 && ret != -EACCES) {
		pm_runtime_put_autosuspend(dev);
		goto out;
	}

	ret = nouveau_vma_new(nvbo, vmm, &vma);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
out:
	ttm_bo_unreserve(&nvbo->bo);
	return ret;
}

struct nouveau_gem_object_unmap {
	struct nouveau_cli_work work;
	struct nouveau_vma *vma;
};

static void
nouveau_gem_object_delete(struct nouveau_vma *vma)
{
	nouveau_fence_unref(&vma->fence);
	nouveau_vma_del(&vma);
}

static void
nouveau_gem_object_delete_work(struct nouveau_cli_work *w)
{
	struct nouveau_gem_object_unmap *work =
		container_of(w, typeof(*work), work);
	nouveau_gem_object_delete(work->vma);
	kfree(work);
}

static void
nouveau_gem_object_unmap(struct nouveau_bo *nvbo, struct nouveau_vma *vma)
{
	struct dma_fence *fence = vma->fence ? &vma->fence->base : NULL;
	struct nouveau_gem_object_unmap *work;

	list_del_init(&vma->head);

	if (!fence) {
		nouveau_gem_object_delete(vma);
		return;
	}

	if (!(work = kmalloc(sizeof(*work), GFP_KERNEL))) {
		WARN_ON(dma_fence_wait_timeout(fence, false, 2 * HZ) <= 0);
		nouveau_gem_object_delete(vma);
		return;
	}

	work->work.func = nouveau_gem_object_delete_work;
	work->vma = vma;
	nouveau_cli_work_queue(vma->vmm->cli, fence, &work->work);
}

void
nouveau_gem_object_close(struct drm_gem_object *gem, struct drm_file *file_priv)
{
	struct nouveau_cli *cli = nouveau_cli(file_priv);
	struct nouveau_bo *nvbo = nouveau_gem_object(gem);
	struct nouveau_drm *drm = nouveau_bdev(nvbo->bo.bdev);
	struct device *dev = drm->dev->dev;
	struct nouveau_vmm *vmm = cli->svm.cli ? &cli->svm : & cli->vmm;
	struct nouveau_vma *vma;
	int ret;

	if (vmm->vmm.object.oclass < NVIF_CLASS_VMM_NV50)
		return;

	ret = ttm_bo_reserve(&nvbo->bo, false, false, NULL);
	if (ret)
		return;

	vma = nouveau_vma_find(nvbo, vmm);
	if (vma) {
		if (--vma->refs == 0) {
			ret = pm_runtime_get_sync(dev);
			if (!WARN_ON(ret < 0 && ret != -EACCES)) {
				nouveau_gem_object_unmap(nvbo, vma);
				pm_runtime_mark_last_busy(dev);
			}
			pm_runtime_put_autosuspend(dev);
		}
	}
	ttm_bo_unreserve(&nvbo->bo);
}

const struct drm_gem_object_funcs nouveau_gem_object_funcs = {
	.free = nouveau_gem_object_del,
	.open = nouveau_gem_object_open,
	.close = nouveau_gem_object_close,
	.pin = nouveau_gem_prime_pin,
	.unpin = nouveau_gem_prime_unpin,
	.get_sg_table = nouveau_gem_prime_get_sg_table,
	.vmap = drm_gem_ttm_vmap,
	.vunmap = drm_gem_ttm_vunmap,
};

int
nouveau_gem_new(struct nouveau_cli *cli, u64 size, int align, uint32_t domain,
		uint32_t tile_mode, uint32_t tile_flags,
		struct nouveau_bo **pnvbo)
{
	struct nouveau_drm *drm = cli->drm;
	struct nouveau_bo *nvbo;
	int ret;

	if (!(domain & (NOUVEAU_GEM_DOMAIN_VRAM | NOUVEAU_GEM_DOMAIN_GART)))
		domain |= NOUVEAU_GEM_DOMAIN_CPU;

	nvbo = nouveau_bo_alloc(cli, &size, &align, domain, tile_mode,
				tile_flags);
	if (IS_ERR(nvbo))
		return PTR_ERR(nvbo);

	nvbo->bo.base.funcs = &nouveau_gem_object_funcs;

	/* Initialize the embedded gem-object. We return a single gem-reference
	 * to the caller, instead of a normal nouveau_bo ttm reference. */
	ret = drm_gem_object_init(drm->dev, &nvbo->bo.base, size);
	if (ret) {
		drm_gem_object_release(&nvbo->bo.base);
		kfree(nvbo);
		return ret;
	}

	ret = nouveau_bo_init(nvbo, size, align, domain, NULL, NULL);
	if (ret) {
		nouveau_bo_ref(NULL, &nvbo);
		return ret;
	}

	/* we restrict allowed domains on nv50+ to only the types
	 * that were requested at creation time.  not possibly on
	 * earlier chips without busting the ABI.
	 */
	nvbo->valid_domains = NOUVEAU_GEM_DOMAIN_VRAM |
			      NOUVEAU_GEM_DOMAIN_GART;
	if (drm->client.device.info.family >= NV_DEVICE_INFO_V0_TESLA)
		nvbo->valid_domains &= domain;

	*pnvbo = nvbo;
	return 0;
}

static int
nouveau_gem_info(struct drm_file *file_priv, struct drm_gem_object *gem,
		 struct drm_nouveau_gem_info *rep)
{
	struct nouveau_cli *cli = nouveau_cli(file_priv);
	struct nouveau_bo *nvbo = nouveau_gem_object(gem);
	struct nouveau_vmm *vmm = cli->svm.cli ? &cli->svm : &cli->vmm;
	struct nouveau_vma *vma;

	if (is_power_of_2(nvbo->valid_domains))
		rep->domain = nvbo->valid_domains;
	else if (nvbo->bo.mem.mem_type == TTM_PL_TT)
		rep->domain = NOUVEAU_GEM_DOMAIN_GART;
	else
		rep->domain = NOUVEAU_GEM_DOMAIN_VRAM;
	rep->offset = nvbo->offset;
	if (vmm->vmm.object.oclass >= NVIF_CLASS_VMM_NV50) {
		vma = nouveau_vma_find(nvbo, vmm);
		if (!vma)
			return -EINVAL;

		rep->offset = vma->addr;
	}

	rep->size = nvbo->bo.mem.num_pages << PAGE_SHIFT;
	rep->map_handle = drm_vma_node_offset_addr(&nvbo->bo.base.vma_node);
	rep->tile_mode = nvbo->mode;
	rep->tile_flags = nvbo->contig ? 0 : NOUVEAU_GEM_TILE_NONCONTIG;
	if (cli->device.info.family >= NV_DEVICE_INFO_V0_FERMI)
		rep->tile_flags |= nvbo->kind << 8;
	else
	if (cli->device.info.family >= NV_DEVICE_INFO_V0_TESLA)
		rep->tile_flags |= nvbo->kind << 8 | nvbo->comp << 16;
	else
		rep->tile_flags |= nvbo->zeta;
	return 0;
}

int
nouveau_gem_ioctl_new(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct nouveau_cli *cli = nouveau_cli(file_priv);
	struct drm_nouveau_gem_new *req = data;
	struct nouveau_bo *nvbo = NULL;
	int ret = 0;

	ret = nouveau_gem_new(cli, req->info.size, req->align,
			      req->info.domain, req->info.tile_mode,
			      req->info.tile_flags, &nvbo);
	if (ret)
		return ret;

	ret = drm_gem_handle_create(file_priv, &nvbo->bo.base,
				    &req->info.handle);
	if (ret == 0) {
		ret = nouveau_gem_info(file_priv, &nvbo->bo.base, &req->info);
		if (ret)
			drm_gem_handle_delete(file_priv, req->info.handle);
	}

	/* drop reference from allocate - handle holds it now */
	drm_gem_object_put(&nvbo->bo.base);
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
	uint32_t pref_domains = 0;;

	if (!domains)
		return -EINVAL;

	valid_domains &= ~(NOUVEAU_GEM_DOMAIN_VRAM | NOUVEAU_GEM_DOMAIN_GART);

	if ((domains & NOUVEAU_GEM_DOMAIN_VRAM) &&
	    bo->mem.mem_type == TTM_PL_VRAM)
		pref_domains |= NOUVEAU_GEM_DOMAIN_VRAM;

	else if ((domains & NOUVEAU_GEM_DOMAIN_GART) &&
		 bo->mem.mem_type == TTM_PL_TT)
		pref_domains |= NOUVEAU_GEM_DOMAIN_GART;

	else if (domains & NOUVEAU_GEM_DOMAIN_VRAM)
		pref_domains |= NOUVEAU_GEM_DOMAIN_VRAM;

	else
		pref_domains |= NOUVEAU_GEM_DOMAIN_GART;

	nouveau_bo_placement_set(nvbo, pref_domains, valid_domains);

	return 0;
}

struct validate_op {
	struct list_head list;
	struct ww_acquire_ctx ticket;
};

static void
validate_fini_no_ticket(struct validate_op *op, struct nouveau_channel *chan,
			struct nouveau_fence *fence,
			struct drm_nouveau_gem_pushbuf_bo *pbbo)
{
	struct nouveau_bo *nvbo;
	struct drm_nouveau_gem_pushbuf_bo *b;

	while (!list_empty(&op->list)) {
		nvbo = list_entry(op->list.next, struct nouveau_bo, entry);
		b = &pbbo[nvbo->pbbo_index];

		if (likely(fence)) {
			nouveau_bo_fence(nvbo, fence, !!b->write_domains);

			if (chan->vmm->vmm.object.oclass >= NVIF_CLASS_VMM_NV50) {
				struct nouveau_vma *vma =
					(void *)(unsigned long)b->user_priv;
				nouveau_fence_unref(&vma->fence);
				dma_fence_get(&fence->base);
				vma->fence = fence;
			}
		}

		if (unlikely(nvbo->validate_mapped)) {
			ttm_bo_kunmap(&nvbo->kmap);
			nvbo->validate_mapped = false;
		}

		list_del(&nvbo->entry);
		nvbo->reserved_by = NULL;
		ttm_bo_unreserve(&nvbo->bo);
		drm_gem_object_put(&nvbo->bo.base);
	}
}

static void
validate_fini(struct validate_op *op, struct nouveau_channel *chan,
	      struct nouveau_fence *fence,
	      struct drm_nouveau_gem_pushbuf_bo *pbbo)
{
	validate_fini_no_ticket(op, chan, fence, pbbo);
	ww_acquire_fini(&op->ticket);
}

static int
validate_init(struct nouveau_channel *chan, struct drm_file *file_priv,
	      struct drm_nouveau_gem_pushbuf_bo *pbbo,
	      int nr_buffers, struct validate_op *op)
{
	struct nouveau_cli *cli = nouveau_cli(file_priv);
	int trycnt = 0;
	int ret = -EINVAL, i;
	struct nouveau_bo *res_bo = NULL;
	LIST_HEAD(gart_list);
	LIST_HEAD(vram_list);
	LIST_HEAD(both_list);

	ww_acquire_init(&op->ticket, &reservation_ww_class);
retry:
	if (++trycnt > 100000) {
		NV_PRINTK(err, cli, "%s failed and gave up.\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < nr_buffers; i++) {
		struct drm_nouveau_gem_pushbuf_bo *b = &pbbo[i];
		struct drm_gem_object *gem;
		struct nouveau_bo *nvbo;

		gem = drm_gem_object_lookup(file_priv, b->handle);
		if (!gem) {
			NV_PRINTK(err, cli, "Unknown handle 0x%08x\n", b->handle);
			ret = -ENOENT;
			break;
		}
		nvbo = nouveau_gem_object(gem);
		if (nvbo == res_bo) {
			res_bo = NULL;
			drm_gem_object_put(gem);
			continue;
		}

		if (nvbo->reserved_by && nvbo->reserved_by == file_priv) {
			NV_PRINTK(err, cli, "multiple instances of buffer %d on "
				      "validation list\n", b->handle);
			drm_gem_object_put(gem);
			ret = -EINVAL;
			break;
		}

		ret = ttm_bo_reserve(&nvbo->bo, true, false, &op->ticket);
		if (ret) {
			list_splice_tail_init(&vram_list, &op->list);
			list_splice_tail_init(&gart_list, &op->list);
			list_splice_tail_init(&both_list, &op->list);
			validate_fini_no_ticket(op, chan, NULL, NULL);
			if (unlikely(ret == -EDEADLK)) {
				ret = ttm_bo_reserve_slowpath(&nvbo->bo, true,
							      &op->ticket);
				if (!ret)
					res_bo = nvbo;
			}
			if (unlikely(ret)) {
				if (ret != -ERESTARTSYS)
					NV_PRINTK(err, cli, "fail reserve\n");
				break;
			}
		}

		if (chan->vmm->vmm.object.oclass >= NVIF_CLASS_VMM_NV50) {
			struct nouveau_vmm *vmm = chan->vmm;
			struct nouveau_vma *vma = nouveau_vma_find(nvbo, vmm);
			if (!vma) {
				NV_PRINTK(err, cli, "vma not found!\n");
				ret = -EINVAL;
				break;
			}

			b->user_priv = (uint64_t)(unsigned long)vma;
		} else {
			b->user_priv = (uint64_t)(unsigned long)nvbo;
		}

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
			NV_PRINTK(err, cli, "invalid valid domains: 0x%08x\n",
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
		validate_fini(op, chan, NULL, NULL);
	return ret;

}

static int
validate_list(struct nouveau_channel *chan, struct nouveau_cli *cli,
	      struct list_head *list, struct drm_nouveau_gem_pushbuf_bo *pbbo)
{
	struct nouveau_drm *drm = chan->drm;
	struct nouveau_bo *nvbo;
	int ret, relocs = 0;

	list_for_each_entry(nvbo, list, entry) {
		struct drm_nouveau_gem_pushbuf_bo *b = &pbbo[nvbo->pbbo_index];

		ret = nouveau_gem_set_domain(&nvbo->bo.base, b->read_domains,
					     b->write_domains,
					     b->valid_domains);
		if (unlikely(ret)) {
			NV_PRINTK(err, cli, "fail set_domain\n");
			return ret;
		}

		ret = nouveau_bo_validate(nvbo, true, false);
		if (unlikely(ret)) {
			if (ret != -ERESTARTSYS)
				NV_PRINTK(err, cli, "fail ttm_validate\n");
			return ret;
		}

		ret = nouveau_fence_sync(nvbo, chan, !!b->write_domains, true);
		if (unlikely(ret)) {
			if (ret != -ERESTARTSYS)
				NV_PRINTK(err, cli, "fail post-validate sync\n");
			return ret;
		}

		if (drm->client.device.info.family < NV_DEVICE_INFO_V0_TESLA) {
			if (nvbo->offset == b->presumed.offset &&
			    ((nvbo->bo.mem.mem_type == TTM_PL_VRAM &&
			      b->presumed.domain & NOUVEAU_GEM_DOMAIN_VRAM) ||
			     (nvbo->bo.mem.mem_type == TTM_PL_TT &&
			      b->presumed.domain & NOUVEAU_GEM_DOMAIN_GART)))
				continue;

			if (nvbo->bo.mem.mem_type == TTM_PL_TT)
				b->presumed.domain = NOUVEAU_GEM_DOMAIN_GART;
			else
				b->presumed.domain = NOUVEAU_GEM_DOMAIN_VRAM;
			b->presumed.offset = nvbo->offset;
			b->presumed.valid = 0;
			relocs++;
		}
	}

	return relocs;
}

static int
nouveau_gem_pushbuf_validate(struct nouveau_channel *chan,
			     struct drm_file *file_priv,
			     struct drm_nouveau_gem_pushbuf_bo *pbbo,
			     int nr_buffers,
			     struct validate_op *op, bool *apply_relocs)
{
	struct nouveau_cli *cli = nouveau_cli(file_priv);
	int ret;

	INIT_LIST_HEAD(&op->list);

	if (nr_buffers == 0)
		return 0;

	ret = validate_init(chan, file_priv, pbbo, nr_buffers, op);
	if (unlikely(ret)) {
		if (ret != -ERESTARTSYS)
			NV_PRINTK(err, cli, "validate_init\n");
		return ret;
	}

	ret = validate_list(chan, cli, &op->list, pbbo);
	if (unlikely(ret < 0)) {
		if (ret != -ERESTARTSYS)
			NV_PRINTK(err, cli, "validating bo list\n");
		validate_fini(op, chan, NULL, NULL);
		return ret;
	}
	*apply_relocs = ret;
	return 0;
}

static inline void
u_free(void *addr)
{
	kvfree(addr);
}

static inline void *
u_memcpya(uint64_t user, unsigned nmemb, unsigned size)
{
	void *mem;
	void __user *userptr = (void __force __user *)(uintptr_t)user;

	size *= nmemb;

	mem = kvmalloc(size, GFP_KERNEL);
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
				struct drm_nouveau_gem_pushbuf_reloc *reloc,
				struct drm_nouveau_gem_pushbuf_bo *bo)
{
	int ret = 0;
	unsigned i;

	for (i = 0; i < req->nr_relocs; i++) {
		struct drm_nouveau_gem_pushbuf_reloc *r = &reloc[i];
		struct drm_nouveau_gem_pushbuf_bo *b;
		struct nouveau_bo *nvbo;
		uint32_t data;

		if (unlikely(r->bo_index >= req->nr_buffers)) {
			NV_PRINTK(err, cli, "reloc bo index invalid\n");
			ret = -EINVAL;
			break;
		}

		b = &bo[r->bo_index];
		if (b->presumed.valid)
			continue;

		if (unlikely(r->reloc_bo_index >= req->nr_buffers)) {
			NV_PRINTK(err, cli, "reloc container bo index invalid\n");
			ret = -EINVAL;
			break;
		}
		nvbo = (void *)(unsigned long)bo[r->reloc_bo_index].user_priv;

		if (unlikely(r->reloc_bo_offset + 4 >
			     nvbo->bo.mem.num_pages << PAGE_SHIFT)) {
			NV_PRINTK(err, cli, "reloc outside of bo\n");
			ret = -EINVAL;
			break;
		}

		if (!nvbo->kmap.virtual) {
			ret = ttm_bo_kmap(&nvbo->bo, 0, nvbo->bo.mem.num_pages,
					  &nvbo->kmap);
			if (ret) {
				NV_PRINTK(err, cli, "failed kmap for reloc\n");
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

		ret = ttm_bo_wait(&nvbo->bo, false, false);
		if (ret) {
			NV_PRINTK(err, cli, "reloc wait_idle failed: %d\n", ret);
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
	struct nouveau_abi16 *abi16 = nouveau_abi16_get(file_priv);
	struct nouveau_cli *cli = nouveau_cli(file_priv);
	struct nouveau_abi16_chan *temp;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct drm_nouveau_gem_pushbuf *req = data;
	struct drm_nouveau_gem_pushbuf_push *push;
	struct drm_nouveau_gem_pushbuf_reloc *reloc = NULL;
	struct drm_nouveau_gem_pushbuf_bo *bo;
	struct nouveau_channel *chan = NULL;
	struct validate_op op;
	struct nouveau_fence *fence = NULL;
	int i, j, ret = 0;
	bool do_reloc = false, sync = false;

	if (unlikely(!abi16))
		return -ENOMEM;

	list_for_each_entry(temp, &abi16->channels, head) {
		if (temp->chan->chid == req->channel) {
			chan = temp->chan;
			break;
		}
	}

	if (!chan)
		return nouveau_abi16_put(abi16, -ENOENT);
	if (unlikely(atomic_read(&chan->killed)))
		return nouveau_abi16_put(abi16, -ENODEV);

	sync = req->vram_available & NOUVEAU_GEM_PUSHBUF_SYNC;

	req->vram_available = drm->gem.vram_available;
	req->gart_available = drm->gem.gart_available;
	if (unlikely(req->nr_push == 0))
		goto out_next;

	if (unlikely(req->nr_push > NOUVEAU_GEM_MAX_PUSH)) {
		NV_PRINTK(err, cli, "pushbuf push count exceeds limit: %d max %d\n",
			 req->nr_push, NOUVEAU_GEM_MAX_PUSH);
		return nouveau_abi16_put(abi16, -EINVAL);
	}

	if (unlikely(req->nr_buffers > NOUVEAU_GEM_MAX_BUFFERS)) {
		NV_PRINTK(err, cli, "pushbuf bo count exceeds limit: %d max %d\n",
			 req->nr_buffers, NOUVEAU_GEM_MAX_BUFFERS);
		return nouveau_abi16_put(abi16, -EINVAL);
	}

	if (unlikely(req->nr_relocs > NOUVEAU_GEM_MAX_RELOCS)) {
		NV_PRINTK(err, cli, "pushbuf reloc count exceeds limit: %d max %d\n",
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
			NV_PRINTK(err, cli, "push %d buffer not in list\n", i);
			ret = -EINVAL;
			goto out_prevalid;
		}
	}

	/* Validate buffer list */
revalidate:
	ret = nouveau_gem_pushbuf_validate(chan, file_priv, bo,
					   req->nr_buffers, &op, &do_reloc);
	if (ret) {
		if (ret != -ERESTARTSYS)
			NV_PRINTK(err, cli, "validate: %d\n", ret);
		goto out_prevalid;
	}

	/* Apply any relocations that are required */
	if (do_reloc) {
		if (!reloc) {
			validate_fini(&op, chan, NULL, bo);
			reloc = u_memcpya(req->relocs, req->nr_relocs, sizeof(*reloc));
			if (IS_ERR(reloc)) {
				ret = PTR_ERR(reloc);
				goto out_prevalid;
			}

			goto revalidate;
		}

		ret = nouveau_gem_pushbuf_reloc_apply(cli, req, reloc, bo);
		if (ret) {
			NV_PRINTK(err, cli, "reloc apply: %d\n", ret);
			goto out;
		}
	}

	if (chan->dma.ib_max) {
		ret = nouveau_dma_wait(chan, req->nr_push + 1, 16);
		if (ret) {
			NV_PRINTK(err, cli, "nv50cal_space: %d\n", ret);
			goto out;
		}

		for (i = 0; i < req->nr_push; i++) {
			struct nouveau_vma *vma = (void *)(unsigned long)
				bo[push[i].bo_index].user_priv;

			nv50_dma_push(chan, vma->addr + push[i].offset,
				      push[i].length);
		}
	} else
	if (drm->client.device.info.chipset >= 0x25) {
		ret = PUSH_WAIT(chan->chan.push, req->nr_push * 2);
		if (ret) {
			NV_PRINTK(err, cli, "cal_space: %d\n", ret);
			goto out;
		}

		for (i = 0; i < req->nr_push; i++) {
			struct nouveau_bo *nvbo = (void *)(unsigned long)
				bo[push[i].bo_index].user_priv;

			PUSH_CALL(chan->chan.push, nvbo->offset + push[i].offset);
			PUSH_DATA(chan->chan.push, 0);
		}
	} else {
		ret = PUSH_WAIT(chan->chan.push, req->nr_push * (2 + NOUVEAU_DMA_SKIPS));
		if (ret) {
			NV_PRINTK(err, cli, "jmp_space: %d\n", ret);
			goto out;
		}

		for (i = 0; i < req->nr_push; i++) {
			struct nouveau_bo *nvbo = (void *)(unsigned long)
				bo[push[i].bo_index].user_priv;
			uint32_t cmd;

			cmd = chan->push.addr + ((chan->dma.cur + 2) << 2);
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

			PUSH_JUMP(chan->chan.push, nvbo->offset + push[i].offset);
			PUSH_DATA(chan->chan.push, 0);
			for (j = 0; j < NOUVEAU_DMA_SKIPS; j++)
				PUSH_DATA(chan->chan.push, 0);
		}
	}

	ret = nouveau_fence_new(chan, false, &fence);
	if (ret) {
		NV_PRINTK(err, cli, "error fencing pushbuf: %d\n", ret);
		WIND_RING(chan);
		goto out;
	}

	if (sync) {
		if (!(ret = nouveau_fence_wait(fence, false, false))) {
			if ((ret = dma_fence_get_status(&fence->base)) == 1)
				ret = 0;
		}
	}

out:
	validate_fini(&op, chan, fence, bo);
	nouveau_fence_unref(&fence);

	if (do_reloc) {
		struct drm_nouveau_gem_pushbuf_bo __user *upbbo =
			u64_to_user_ptr(req->buffers);

		for (i = 0; i < req->nr_buffers; i++) {
			if (bo[i].presumed.valid)
				continue;

			if (copy_to_user(&upbbo[i].presumed, &bo[i].presumed,
					 sizeof(bo[i].presumed))) {
				ret = -EFAULT;
				break;
			}
		}
		u_free(reloc);
	}
out_prevalid:
	u_free(bo);
	u_free(push);

out_next:
	if (chan->dma.ib_max) {
		req->suffix0 = 0x00000000;
		req->suffix1 = 0x00000000;
	} else
	if (drm->client.device.info.chipset >= 0x25) {
		req->suffix0 = 0x00020000;
		req->suffix1 = 0x00000000;
	} else {
		req->suffix0 = 0x20000000 |
			      (chan->push.addr + ((chan->dma.cur + 2) << 2));
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
	long lret;
	int ret;

	gem = drm_gem_object_lookup(file_priv, req->handle);
	if (!gem)
		return -ENOENT;
	nvbo = nouveau_gem_object(gem);

	lret = dma_resv_wait_timeout_rcu(nvbo->bo.base.resv, write, true,
						   no_wait ? 0 : 30 * HZ);
	if (!lret)
		ret = -EBUSY;
	else if (lret > 0)
		ret = 0;
	else
		ret = lret;

	nouveau_bo_sync_for_cpu(nvbo);
	drm_gem_object_put(gem);

	return ret;
}

int
nouveau_gem_ioctl_cpu_fini(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	struct drm_nouveau_gem_cpu_fini *req = data;
	struct drm_gem_object *gem;
	struct nouveau_bo *nvbo;

	gem = drm_gem_object_lookup(file_priv, req->handle);
	if (!gem)
		return -ENOENT;
	nvbo = nouveau_gem_object(gem);

	nouveau_bo_sync_for_device(nvbo);
	drm_gem_object_put(gem);
	return 0;
}

int
nouveau_gem_ioctl_info(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct drm_nouveau_gem_info *req = data;
	struct drm_gem_object *gem;
	int ret;

	gem = drm_gem_object_lookup(file_priv, req->handle);
	if (!gem)
		return -ENOENT;

	ret = nouveau_gem_info(file_priv, gem, req);
	drm_gem_object_put(gem);
	return ret;
}

