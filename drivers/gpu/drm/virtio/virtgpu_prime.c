/*
 * Copyright 2014 Canonical
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Andreas Pokorny
 */

#include <drm/drm_prime.h>
#include <linux/virtio_dma_buf.h>

#include "virtgpu_drv.h"

MODULE_IMPORT_NS("DMA_BUF");

static int virtgpu_virtio_get_uuid(struct dma_buf *buf,
				   uuid_t *uuid)
{
	struct drm_gem_object *obj = buf->priv;
	struct virtio_gpu_object *bo = gem_to_virtio_gpu_obj(obj);
	struct virtio_gpu_device *vgdev = obj->dev->dev_private;

	wait_event(vgdev->resp_wq, bo->uuid_state != STATE_INITIALIZING);
	if (bo->uuid_state != STATE_OK)
		return -ENODEV;

	uuid_copy(uuid, &bo->uuid);

	return 0;
}

static struct sg_table *
virtgpu_gem_map_dma_buf(struct dma_buf_attachment *attach,
			enum dma_data_direction dir)
{
	struct drm_gem_object *obj = attach->dmabuf->priv;
	struct virtio_gpu_object *bo = gem_to_virtio_gpu_obj(obj);

	if (virtio_gpu_is_vram(bo))
		return virtio_gpu_vram_map_dma_buf(bo, attach->dev, dir);

	return drm_gem_map_dma_buf(attach, dir);
}

static void virtgpu_gem_unmap_dma_buf(struct dma_buf_attachment *attach,
				      struct sg_table *sgt,
				      enum dma_data_direction dir)
{
	struct drm_gem_object *obj = attach->dmabuf->priv;
	struct virtio_gpu_object *bo = gem_to_virtio_gpu_obj(obj);

	if (virtio_gpu_is_vram(bo)) {
		virtio_gpu_vram_unmap_dma_buf(attach->dev, sgt, dir);
		return;
	}

	drm_gem_unmap_dma_buf(attach, sgt, dir);
}

static const struct virtio_dma_buf_ops virtgpu_dmabuf_ops =  {
	.ops = {
		.cache_sgt_mapping = true,
		.attach = virtio_dma_buf_attach,
		.detach = drm_gem_map_detach,
		.map_dma_buf = virtgpu_gem_map_dma_buf,
		.unmap_dma_buf = virtgpu_gem_unmap_dma_buf,
		.release = drm_gem_dmabuf_release,
		.mmap = drm_gem_dmabuf_mmap,
		.vmap = drm_gem_dmabuf_vmap,
		.vunmap = drm_gem_dmabuf_vunmap,
	},
	.device_attach = drm_gem_map_attach,
	.get_uuid = virtgpu_virtio_get_uuid,
};

int virtio_gpu_resource_assign_uuid(struct virtio_gpu_device *vgdev,
				    struct virtio_gpu_object *bo)
{
	struct virtio_gpu_object_array *objs;

	objs = virtio_gpu_array_alloc(1);
	if (!objs)
		return -ENOMEM;

	virtio_gpu_array_add_obj(objs, &bo->base.base);

	return virtio_gpu_cmd_resource_assign_uuid(vgdev, objs);
}

struct dma_buf *virtgpu_gem_prime_export(struct drm_gem_object *obj,
					 int flags)
{
	struct dma_buf *buf;
	struct drm_device *dev = obj->dev;
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct virtio_gpu_object *bo = gem_to_virtio_gpu_obj(obj);
	int ret = 0;
	bool blob = bo->host3d_blob || bo->guest_blob;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

	if (!blob) {
		if (vgdev->has_resource_assign_uuid) {
			ret = virtio_gpu_resource_assign_uuid(vgdev, bo);
			if (ret)
				return ERR_PTR(ret);

			virtio_gpu_notify(vgdev);
		} else {
			bo->uuid_state = STATE_ERR;
		}
	} else if (!(bo->blob_flags & VIRTGPU_BLOB_FLAG_USE_CROSS_DEVICE)) {
		bo->uuid_state = STATE_ERR;
	}

	exp_info.ops = &virtgpu_dmabuf_ops.ops;
	exp_info.size = obj->size;
	exp_info.flags = flags;
	exp_info.priv = obj;
	exp_info.resv = obj->resv;

	buf = virtio_dma_buf_export(&exp_info);
	if (IS_ERR(buf))
		return buf;

	drm_dev_get(dev);
	drm_gem_object_get(obj);

	return buf;
}

int virtgpu_dma_buf_import_sgt(struct virtio_gpu_mem_entry **ents,
			       unsigned int *nents,
			       struct virtio_gpu_object *bo,
			       struct dma_buf_attachment *attach)
{
	struct scatterlist *sl;
	struct sg_table *sgt;
	long i, ret;

	dma_resv_assert_held(attach->dmabuf->resv);

	ret = dma_resv_wait_timeout(attach->dmabuf->resv,
				    DMA_RESV_USAGE_KERNEL,
				    false, MAX_SCHEDULE_TIMEOUT);
	if (ret <= 0)
		return ret < 0 ? ret : -ETIMEDOUT;

	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt))
		return PTR_ERR(sgt);

	*ents = kvmalloc_array(sgt->nents,
			       sizeof(struct virtio_gpu_mem_entry),
			       GFP_KERNEL);
	if (!(*ents)) {
		dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
		return -ENOMEM;
	}

	*nents = sgt->nents;
	for_each_sgtable_dma_sg(sgt, sl, i) {
		(*ents)[i].addr = cpu_to_le64(sg_dma_address(sl));
		(*ents)[i].length = cpu_to_le32(sg_dma_len(sl));
		(*ents)[i].padding = 0;
	}

	bo->sgt = sgt;
	return 0;
}

static void virtgpu_dma_buf_unmap(struct virtio_gpu_object *bo)
{
	struct dma_buf_attachment *attach = bo->base.base.import_attach;

	dma_resv_assert_held(attach->dmabuf->resv);

	if (bo->created) {
		virtio_gpu_detach_object_fenced(bo);

		if (bo->sgt)
			dma_buf_unmap_attachment(attach, bo->sgt,
						 DMA_BIDIRECTIONAL);

		bo->sgt = NULL;
	}
}

static void virtgpu_dma_buf_free_obj(struct drm_gem_object *obj)
{
	struct virtio_gpu_object *bo = gem_to_virtio_gpu_obj(obj);
	struct virtio_gpu_device *vgdev = obj->dev->dev_private;
	struct dma_buf_attachment *attach = obj->import_attach;

	if (attach) {
		struct dma_buf *dmabuf = attach->dmabuf;

		dma_resv_lock(dmabuf->resv, NULL);
		virtgpu_dma_buf_unmap(bo);
		dma_resv_unlock(dmabuf->resv);

		dma_buf_detach(dmabuf, attach);
		dma_buf_put(dmabuf);
	}

	if (bo->created) {
		virtio_gpu_cmd_unref_resource(vgdev, bo);
		virtio_gpu_notify(vgdev);
		return;
	}
	virtio_gpu_cleanup_object(bo);
}

static int virtgpu_dma_buf_init_obj(struct drm_device *dev,
				    struct virtio_gpu_object *bo,
				    struct dma_buf_attachment *attach)
{
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct virtio_gpu_object_params params = { 0 };
	struct dma_resv *resv = attach->dmabuf->resv;
	struct virtio_gpu_mem_entry *ents = NULL;
	unsigned int nents;
	int ret;

	ret = virtio_gpu_resource_id_get(vgdev, &bo->hw_res_handle);
	if (ret) {
		virtgpu_dma_buf_free_obj(&bo->base.base);
		return ret;
	}

	dma_resv_lock(resv, NULL);

	ret = dma_buf_pin(attach);
	if (ret)
		goto err_pin;

	ret = virtgpu_dma_buf_import_sgt(&ents, &nents, bo, attach);
	if (ret)
		goto err_import;

	params.blob = true;
	params.blob_mem = VIRTGPU_BLOB_MEM_GUEST;
	params.blob_flags = VIRTGPU_BLOB_FLAG_USE_SHAREABLE;
	params.size = attach->dmabuf->size;

	virtio_gpu_cmd_resource_create_blob(vgdev, bo, &params,
					    ents, nents);
	bo->guest_blob = true;

	dma_buf_unpin(attach);
	dma_resv_unlock(resv);

	return 0;

err_import:
	dma_buf_unpin(attach);
err_pin:
	dma_resv_unlock(resv);
	virtgpu_dma_buf_free_obj(&bo->base.base);
	return ret;
}

static const struct drm_gem_object_funcs virtgpu_gem_dma_buf_funcs = {
	.free = virtgpu_dma_buf_free_obj,
};

static void virtgpu_dma_buf_move_notify(struct dma_buf_attachment *attach)
{
	struct drm_gem_object *obj = attach->importer_priv;
	struct virtio_gpu_object *bo = gem_to_virtio_gpu_obj(obj);

	virtgpu_dma_buf_unmap(bo);
}

static const struct dma_buf_attach_ops virtgpu_dma_buf_attach_ops = {
	.allow_peer2peer = true,
	.move_notify = virtgpu_dma_buf_move_notify
};

struct drm_gem_object *virtgpu_gem_prime_import(struct drm_device *dev,
						struct dma_buf *buf)
{
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct dma_buf_attachment *attach;
	struct virtio_gpu_object *bo;
	struct drm_gem_object *obj;
	int ret;

	if (buf->ops == &virtgpu_dmabuf_ops.ops) {
		obj = buf->priv;
		if (obj->dev == dev) {
			/*
			 * Importing dmabuf exported from our own gem increases
			 * refcount on gem itself instead of f_count of dmabuf.
			 */
			drm_gem_object_get(obj);
			return obj;
		}
	}

	if (!vgdev->has_resource_blob || vgdev->has_virgl_3d)
		return drm_gem_prime_import(dev, buf);

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (!bo)
		return ERR_PTR(-ENOMEM);

	obj = &bo->base.base;
	obj->resv = buf->resv;
	obj->funcs = &virtgpu_gem_dma_buf_funcs;
	drm_gem_private_object_init(dev, obj, buf->size);

	attach = dma_buf_dynamic_attach(buf, dev->dev,
					&virtgpu_dma_buf_attach_ops, obj);
	if (IS_ERR(attach)) {
		kfree(bo);
		return ERR_CAST(attach);
	}

	obj->import_attach = attach;
	get_dma_buf(buf);

	ret = virtgpu_dma_buf_init_obj(dev, bo, attach);
	if (ret < 0)
		return ERR_PTR(ret);

	return obj;
}

struct drm_gem_object *virtgpu_gem_prime_import_sg_table(
	struct drm_device *dev, struct dma_buf_attachment *attach,
	struct sg_table *table)
{
	return ERR_PTR(-ENODEV);
}
