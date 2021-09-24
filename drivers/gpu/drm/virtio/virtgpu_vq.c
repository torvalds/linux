/*
 * Copyright (C) 2015 Red Hat, Inc.
 * All Rights Reserved.
 *
 * Authors:
 *    Dave Airlie <airlied@redhat.com>
 *    Gerd Hoffmann <kraxel@redhat.com>
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
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/dma-mapping.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ring.h>

#include "virtgpu_drv.h"
#include "virtgpu_trace.h"

#define MAX_INLINE_CMD_SIZE   96
#define MAX_INLINE_RESP_SIZE  24
#define VBUFFER_SIZE          (sizeof(struct virtio_gpu_vbuffer) \
			       + MAX_INLINE_CMD_SIZE		 \
			       + MAX_INLINE_RESP_SIZE)

static void convert_to_hw_box(struct virtio_gpu_box *dst,
			      const struct drm_virtgpu_3d_box *src)
{
	dst->x = cpu_to_le32(src->x);
	dst->y = cpu_to_le32(src->y);
	dst->z = cpu_to_le32(src->z);
	dst->w = cpu_to_le32(src->w);
	dst->h = cpu_to_le32(src->h);
	dst->d = cpu_to_le32(src->d);
}

void virtio_gpu_ctrl_ack(struct virtqueue *vq)
{
	struct drm_device *dev = vq->vdev->priv;
	struct virtio_gpu_device *vgdev = dev->dev_private;

	schedule_work(&vgdev->ctrlq.dequeue_work);
}

void virtio_gpu_cursor_ack(struct virtqueue *vq)
{
	struct drm_device *dev = vq->vdev->priv;
	struct virtio_gpu_device *vgdev = dev->dev_private;

	schedule_work(&vgdev->cursorq.dequeue_work);
}

int virtio_gpu_alloc_vbufs(struct virtio_gpu_device *vgdev)
{
	vgdev->vbufs = kmem_cache_create("virtio-gpu-vbufs",
					 VBUFFER_SIZE,
					 __alignof__(struct virtio_gpu_vbuffer),
					 0, NULL);
	if (!vgdev->vbufs)
		return -ENOMEM;
	return 0;
}

void virtio_gpu_free_vbufs(struct virtio_gpu_device *vgdev)
{
	kmem_cache_destroy(vgdev->vbufs);
	vgdev->vbufs = NULL;
}

static struct virtio_gpu_vbuffer*
virtio_gpu_get_vbuf(struct virtio_gpu_device *vgdev,
		    int size, int resp_size, void *resp_buf,
		    virtio_gpu_resp_cb resp_cb)
{
	struct virtio_gpu_vbuffer *vbuf;

	vbuf = kmem_cache_zalloc(vgdev->vbufs, GFP_KERNEL | __GFP_NOFAIL);

	BUG_ON(size > MAX_INLINE_CMD_SIZE ||
	       size < sizeof(struct virtio_gpu_ctrl_hdr));
	vbuf->buf = (void *)vbuf + sizeof(*vbuf);
	vbuf->size = size;

	vbuf->resp_cb = resp_cb;
	vbuf->resp_size = resp_size;
	if (resp_size <= MAX_INLINE_RESP_SIZE)
		vbuf->resp_buf = (void *)vbuf->buf + size;
	else
		vbuf->resp_buf = resp_buf;
	BUG_ON(!vbuf->resp_buf);
	return vbuf;
}

static struct virtio_gpu_ctrl_hdr *
virtio_gpu_vbuf_ctrl_hdr(struct virtio_gpu_vbuffer *vbuf)
{
	/* this assumes a vbuf contains a command that starts with a
	 * virtio_gpu_ctrl_hdr, which is true for both ctrl and cursor
	 * virtqueues.
	 */
	return (struct virtio_gpu_ctrl_hdr *)vbuf->buf;
}

static struct virtio_gpu_update_cursor*
virtio_gpu_alloc_cursor(struct virtio_gpu_device *vgdev,
			struct virtio_gpu_vbuffer **vbuffer_p)
{
	struct virtio_gpu_vbuffer *vbuf;

	vbuf = virtio_gpu_get_vbuf
		(vgdev, sizeof(struct virtio_gpu_update_cursor),
		 0, NULL, NULL);
	if (IS_ERR(vbuf)) {
		*vbuffer_p = NULL;
		return ERR_CAST(vbuf);
	}
	*vbuffer_p = vbuf;
	return (struct virtio_gpu_update_cursor *)vbuf->buf;
}

static void *virtio_gpu_alloc_cmd_resp(struct virtio_gpu_device *vgdev,
				       virtio_gpu_resp_cb cb,
				       struct virtio_gpu_vbuffer **vbuffer_p,
				       int cmd_size, int resp_size,
				       void *resp_buf)
{
	struct virtio_gpu_vbuffer *vbuf;

	vbuf = virtio_gpu_get_vbuf(vgdev, cmd_size,
				   resp_size, resp_buf, cb);
	*vbuffer_p = vbuf;
	return (struct virtio_gpu_command *)vbuf->buf;
}

static void *virtio_gpu_alloc_cmd(struct virtio_gpu_device *vgdev,
				  struct virtio_gpu_vbuffer **vbuffer_p,
				  int size)
{
	return virtio_gpu_alloc_cmd_resp(vgdev, NULL, vbuffer_p, size,
					 sizeof(struct virtio_gpu_ctrl_hdr),
					 NULL);
}

static void *virtio_gpu_alloc_cmd_cb(struct virtio_gpu_device *vgdev,
				     struct virtio_gpu_vbuffer **vbuffer_p,
				     int size,
				     virtio_gpu_resp_cb cb)
{
	return virtio_gpu_alloc_cmd_resp(vgdev, cb, vbuffer_p, size,
					 sizeof(struct virtio_gpu_ctrl_hdr),
					 NULL);
}

static void free_vbuf(struct virtio_gpu_device *vgdev,
		      struct virtio_gpu_vbuffer *vbuf)
{
	if (vbuf->resp_size > MAX_INLINE_RESP_SIZE)
		kfree(vbuf->resp_buf);
	kvfree(vbuf->data_buf);
	kmem_cache_free(vgdev->vbufs, vbuf);
}

static void reclaim_vbufs(struct virtqueue *vq, struct list_head *reclaim_list)
{
	struct virtio_gpu_vbuffer *vbuf;
	unsigned int len;
	int freed = 0;

	while ((vbuf = virtqueue_get_buf(vq, &len))) {
		list_add_tail(&vbuf->list, reclaim_list);
		freed++;
	}
	if (freed == 0)
		DRM_DEBUG("Huh? zero vbufs reclaimed");
}

void virtio_gpu_dequeue_ctrl_func(struct work_struct *work)
{
	struct virtio_gpu_device *vgdev =
		container_of(work, struct virtio_gpu_device,
			     ctrlq.dequeue_work);
	struct list_head reclaim_list;
	struct virtio_gpu_vbuffer *entry, *tmp;
	struct virtio_gpu_ctrl_hdr *resp;
	u64 fence_id = 0;

	INIT_LIST_HEAD(&reclaim_list);
	spin_lock(&vgdev->ctrlq.qlock);
	do {
		virtqueue_disable_cb(vgdev->ctrlq.vq);
		reclaim_vbufs(vgdev->ctrlq.vq, &reclaim_list);

	} while (!virtqueue_enable_cb(vgdev->ctrlq.vq));
	spin_unlock(&vgdev->ctrlq.qlock);

	list_for_each_entry(entry, &reclaim_list, list) {
		resp = (struct virtio_gpu_ctrl_hdr *)entry->resp_buf;

		trace_virtio_gpu_cmd_response(vgdev->ctrlq.vq, resp);

		if (resp->type != cpu_to_le32(VIRTIO_GPU_RESP_OK_NODATA)) {
			if (le32_to_cpu(resp->type) >= VIRTIO_GPU_RESP_ERR_UNSPEC) {
				struct virtio_gpu_ctrl_hdr *cmd;
				cmd = virtio_gpu_vbuf_ctrl_hdr(entry);
				DRM_ERROR_RATELIMITED("response 0x%x (command 0x%x)\n",
						      le32_to_cpu(resp->type),
						      le32_to_cpu(cmd->type));
			} else
				DRM_DEBUG("response 0x%x\n", le32_to_cpu(resp->type));
		}
		if (resp->flags & cpu_to_le32(VIRTIO_GPU_FLAG_FENCE)) {
			u64 f = le64_to_cpu(resp->fence_id);

			if (fence_id > f) {
				DRM_ERROR("%s: Oops: fence %llx -> %llx\n",
					  __func__, fence_id, f);
			} else {
				fence_id = f;
			}
		}
		if (entry->resp_cb)
			entry->resp_cb(vgdev, entry);
	}
	wake_up(&vgdev->ctrlq.ack_queue);

	if (fence_id)
		virtio_gpu_fence_event_process(vgdev, fence_id);

	list_for_each_entry_safe(entry, tmp, &reclaim_list, list) {
		if (entry->objs)
			virtio_gpu_array_put_free_delayed(vgdev, entry->objs);
		list_del(&entry->list);
		free_vbuf(vgdev, entry);
	}
}

void virtio_gpu_dequeue_cursor_func(struct work_struct *work)
{
	struct virtio_gpu_device *vgdev =
		container_of(work, struct virtio_gpu_device,
			     cursorq.dequeue_work);
	struct list_head reclaim_list;
	struct virtio_gpu_vbuffer *entry, *tmp;

	INIT_LIST_HEAD(&reclaim_list);
	spin_lock(&vgdev->cursorq.qlock);
	do {
		virtqueue_disable_cb(vgdev->cursorq.vq);
		reclaim_vbufs(vgdev->cursorq.vq, &reclaim_list);
	} while (!virtqueue_enable_cb(vgdev->cursorq.vq));
	spin_unlock(&vgdev->cursorq.qlock);

	list_for_each_entry_safe(entry, tmp, &reclaim_list, list) {
		list_del(&entry->list);
		free_vbuf(vgdev, entry);
	}
	wake_up(&vgdev->cursorq.ack_queue);
}

/* Create sg_table from a vmalloc'd buffer. */
static struct sg_table *vmalloc_to_sgt(char *data, uint32_t size, int *sg_ents)
{
	int ret, s, i;
	struct sg_table *sgt;
	struct scatterlist *sg;
	struct page *pg;

	if (WARN_ON(!PAGE_ALIGNED(data)))
		return NULL;

	sgt = kmalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return NULL;

	*sg_ents = DIV_ROUND_UP(size, PAGE_SIZE);
	ret = sg_alloc_table(sgt, *sg_ents, GFP_KERNEL);
	if (ret) {
		kfree(sgt);
		return NULL;
	}

	for_each_sgtable_sg(sgt, sg, i) {
		pg = vmalloc_to_page(data);
		if (!pg) {
			sg_free_table(sgt);
			kfree(sgt);
			return NULL;
		}

		s = min_t(int, PAGE_SIZE, size);
		sg_set_page(sg, pg, s, 0);

		size -= s;
		data += s;
	}

	return sgt;
}

static int virtio_gpu_queue_ctrl_sgs(struct virtio_gpu_device *vgdev,
				     struct virtio_gpu_vbuffer *vbuf,
				     struct virtio_gpu_fence *fence,
				     int elemcnt,
				     struct scatterlist **sgs,
				     int outcnt,
				     int incnt)
{
	struct virtqueue *vq = vgdev->ctrlq.vq;
	int ret, idx;

	if (!drm_dev_enter(vgdev->ddev, &idx)) {
		if (fence && vbuf->objs)
			virtio_gpu_array_unlock_resv(vbuf->objs);
		free_vbuf(vgdev, vbuf);
		return -1;
	}

	if (vgdev->has_indirect)
		elemcnt = 1;

again:
	spin_lock(&vgdev->ctrlq.qlock);

	if (vq->num_free < elemcnt) {
		spin_unlock(&vgdev->ctrlq.qlock);
		virtio_gpu_notify(vgdev);
		wait_event(vgdev->ctrlq.ack_queue, vq->num_free >= elemcnt);
		goto again;
	}

	/* now that the position of the vbuf in the virtqueue is known, we can
	 * finally set the fence id
	 */
	if (fence) {
		virtio_gpu_fence_emit(vgdev, virtio_gpu_vbuf_ctrl_hdr(vbuf),
				      fence);
		if (vbuf->objs) {
			virtio_gpu_array_add_fence(vbuf->objs, &fence->f);
			virtio_gpu_array_unlock_resv(vbuf->objs);
		}
	}

	ret = virtqueue_add_sgs(vq, sgs, outcnt, incnt, vbuf, GFP_ATOMIC);
	WARN_ON(ret);

	trace_virtio_gpu_cmd_queue(vq, virtio_gpu_vbuf_ctrl_hdr(vbuf));

	atomic_inc(&vgdev->pending_commands);

	spin_unlock(&vgdev->ctrlq.qlock);

	drm_dev_exit(idx);
	return 0;
}

static int virtio_gpu_queue_fenced_ctrl_buffer(struct virtio_gpu_device *vgdev,
					       struct virtio_gpu_vbuffer *vbuf,
					       struct virtio_gpu_fence *fence)
{
	struct scatterlist *sgs[3], vcmd, vout, vresp;
	struct sg_table *sgt = NULL;
	int elemcnt = 0, outcnt = 0, incnt = 0, ret;

	/* set up vcmd */
	sg_init_one(&vcmd, vbuf->buf, vbuf->size);
	elemcnt++;
	sgs[outcnt] = &vcmd;
	outcnt++;

	/* set up vout */
	if (vbuf->data_size) {
		if (is_vmalloc_addr(vbuf->data_buf)) {
			int sg_ents;
			sgt = vmalloc_to_sgt(vbuf->data_buf, vbuf->data_size,
					     &sg_ents);
			if (!sgt) {
				if (fence && vbuf->objs)
					virtio_gpu_array_unlock_resv(vbuf->objs);
				return -1;
			}

			elemcnt += sg_ents;
			sgs[outcnt] = sgt->sgl;
		} else {
			sg_init_one(&vout, vbuf->data_buf, vbuf->data_size);
			elemcnt++;
			sgs[outcnt] = &vout;
		}
		outcnt++;
	}

	/* set up vresp */
	if (vbuf->resp_size) {
		sg_init_one(&vresp, vbuf->resp_buf, vbuf->resp_size);
		elemcnt++;
		sgs[outcnt + incnt] = &vresp;
		incnt++;
	}

	ret = virtio_gpu_queue_ctrl_sgs(vgdev, vbuf, fence, elemcnt, sgs, outcnt,
					incnt);

	if (sgt) {
		sg_free_table(sgt);
		kfree(sgt);
	}
	return ret;
}

void virtio_gpu_notify(struct virtio_gpu_device *vgdev)
{
	bool notify;

	if (!atomic_read(&vgdev->pending_commands))
		return;

	spin_lock(&vgdev->ctrlq.qlock);
	atomic_set(&vgdev->pending_commands, 0);
	notify = virtqueue_kick_prepare(vgdev->ctrlq.vq);
	spin_unlock(&vgdev->ctrlq.qlock);

	if (notify)
		virtqueue_notify(vgdev->ctrlq.vq);
}

static int virtio_gpu_queue_ctrl_buffer(struct virtio_gpu_device *vgdev,
					struct virtio_gpu_vbuffer *vbuf)
{
	return virtio_gpu_queue_fenced_ctrl_buffer(vgdev, vbuf, NULL);
}

static void virtio_gpu_queue_cursor(struct virtio_gpu_device *vgdev,
				    struct virtio_gpu_vbuffer *vbuf)
{
	struct virtqueue *vq = vgdev->cursorq.vq;
	struct scatterlist *sgs[1], ccmd;
	int idx, ret, outcnt;
	bool notify;

	if (!drm_dev_enter(vgdev->ddev, &idx)) {
		free_vbuf(vgdev, vbuf);
		return;
	}

	sg_init_one(&ccmd, vbuf->buf, vbuf->size);
	sgs[0] = &ccmd;
	outcnt = 1;

	spin_lock(&vgdev->cursorq.qlock);
retry:
	ret = virtqueue_add_sgs(vq, sgs, outcnt, 0, vbuf, GFP_ATOMIC);
	if (ret == -ENOSPC) {
		spin_unlock(&vgdev->cursorq.qlock);
		wait_event(vgdev->cursorq.ack_queue, vq->num_free >= outcnt);
		spin_lock(&vgdev->cursorq.qlock);
		goto retry;
	} else {
		trace_virtio_gpu_cmd_queue(vq,
			virtio_gpu_vbuf_ctrl_hdr(vbuf));

		notify = virtqueue_kick_prepare(vq);
	}

	spin_unlock(&vgdev->cursorq.qlock);

	if (notify)
		virtqueue_notify(vq);

	drm_dev_exit(idx);
}

/* just create gem objects for userspace and long lived objects,
 * just use dma_alloced pages for the queue objects?
 */

/* create a basic resource */
void virtio_gpu_cmd_create_resource(struct virtio_gpu_device *vgdev,
				    struct virtio_gpu_object *bo,
				    struct virtio_gpu_object_params *params,
				    struct virtio_gpu_object_array *objs,
				    struct virtio_gpu_fence *fence)
{
	struct virtio_gpu_resource_create_2d *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	memset(cmd_p, 0, sizeof(*cmd_p));
	vbuf->objs = objs;

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_RESOURCE_CREATE_2D);
	cmd_p->resource_id = cpu_to_le32(bo->hw_res_handle);
	cmd_p->format = cpu_to_le32(params->format);
	cmd_p->width = cpu_to_le32(params->width);
	cmd_p->height = cpu_to_le32(params->height);

	virtio_gpu_queue_fenced_ctrl_buffer(vgdev, vbuf, fence);
	bo->created = true;
}

static void virtio_gpu_cmd_unref_cb(struct virtio_gpu_device *vgdev,
				    struct virtio_gpu_vbuffer *vbuf)
{
	struct virtio_gpu_object *bo;

	bo = vbuf->resp_cb_data;
	vbuf->resp_cb_data = NULL;

	virtio_gpu_cleanup_object(bo);
}

void virtio_gpu_cmd_unref_resource(struct virtio_gpu_device *vgdev,
				   struct virtio_gpu_object *bo)
{
	struct virtio_gpu_resource_unref *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;
	int ret;

	cmd_p = virtio_gpu_alloc_cmd_cb(vgdev, &vbuf, sizeof(*cmd_p),
					virtio_gpu_cmd_unref_cb);
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_RESOURCE_UNREF);
	cmd_p->resource_id = cpu_to_le32(bo->hw_res_handle);

	vbuf->resp_cb_data = bo;
	ret = virtio_gpu_queue_ctrl_buffer(vgdev, vbuf);
	if (ret < 0)
		virtio_gpu_cleanup_object(bo);
}

void virtio_gpu_cmd_set_scanout(struct virtio_gpu_device *vgdev,
				uint32_t scanout_id, uint32_t resource_id,
				uint32_t width, uint32_t height,
				uint32_t x, uint32_t y)
{
	struct virtio_gpu_set_scanout *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_SET_SCANOUT);
	cmd_p->resource_id = cpu_to_le32(resource_id);
	cmd_p->scanout_id = cpu_to_le32(scanout_id);
	cmd_p->r.width = cpu_to_le32(width);
	cmd_p->r.height = cpu_to_le32(height);
	cmd_p->r.x = cpu_to_le32(x);
	cmd_p->r.y = cpu_to_le32(y);

	virtio_gpu_queue_ctrl_buffer(vgdev, vbuf);
}

void virtio_gpu_cmd_resource_flush(struct virtio_gpu_device *vgdev,
				   uint32_t resource_id,
				   uint32_t x, uint32_t y,
				   uint32_t width, uint32_t height,
				   struct virtio_gpu_object_array *objs,
				   struct virtio_gpu_fence *fence)
{
	struct virtio_gpu_resource_flush *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	memset(cmd_p, 0, sizeof(*cmd_p));
	vbuf->objs = objs;

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_RESOURCE_FLUSH);
	cmd_p->resource_id = cpu_to_le32(resource_id);
	cmd_p->r.width = cpu_to_le32(width);
	cmd_p->r.height = cpu_to_le32(height);
	cmd_p->r.x = cpu_to_le32(x);
	cmd_p->r.y = cpu_to_le32(y);

	virtio_gpu_queue_fenced_ctrl_buffer(vgdev, vbuf, fence);
}

void virtio_gpu_cmd_transfer_to_host_2d(struct virtio_gpu_device *vgdev,
					uint64_t offset,
					uint32_t width, uint32_t height,
					uint32_t x, uint32_t y,
					struct virtio_gpu_object_array *objs,
					struct virtio_gpu_fence *fence)
{
	struct virtio_gpu_object *bo = gem_to_virtio_gpu_obj(objs->objs[0]);
	struct virtio_gpu_transfer_to_host_2d *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;
	bool use_dma_api = !virtio_has_dma_quirk(vgdev->vdev);
	struct virtio_gpu_object_shmem *shmem = to_virtio_gpu_shmem(bo);

	if (use_dma_api)
		dma_sync_sgtable_for_device(vgdev->vdev->dev.parent,
					    shmem->pages, DMA_TO_DEVICE);

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	memset(cmd_p, 0, sizeof(*cmd_p));
	vbuf->objs = objs;

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D);
	cmd_p->resource_id = cpu_to_le32(bo->hw_res_handle);
	cmd_p->offset = cpu_to_le64(offset);
	cmd_p->r.width = cpu_to_le32(width);
	cmd_p->r.height = cpu_to_le32(height);
	cmd_p->r.x = cpu_to_le32(x);
	cmd_p->r.y = cpu_to_le32(y);

	virtio_gpu_queue_fenced_ctrl_buffer(vgdev, vbuf, fence);
}

static void
virtio_gpu_cmd_resource_attach_backing(struct virtio_gpu_device *vgdev,
				       uint32_t resource_id,
				       struct virtio_gpu_mem_entry *ents,
				       uint32_t nents,
				       struct virtio_gpu_fence *fence)
{
	struct virtio_gpu_resource_attach_backing *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING);
	cmd_p->resource_id = cpu_to_le32(resource_id);
	cmd_p->nr_entries = cpu_to_le32(nents);

	vbuf->data_buf = ents;
	vbuf->data_size = sizeof(*ents) * nents;

	virtio_gpu_queue_fenced_ctrl_buffer(vgdev, vbuf, fence);
}

static void virtio_gpu_cmd_get_display_info_cb(struct virtio_gpu_device *vgdev,
					       struct virtio_gpu_vbuffer *vbuf)
{
	struct virtio_gpu_resp_display_info *resp =
		(struct virtio_gpu_resp_display_info *)vbuf->resp_buf;
	int i;

	spin_lock(&vgdev->display_info_lock);
	for (i = 0; i < vgdev->num_scanouts; i++) {
		vgdev->outputs[i].info = resp->pmodes[i];
		if (resp->pmodes[i].enabled) {
			DRM_DEBUG("output %d: %dx%d+%d+%d", i,
				  le32_to_cpu(resp->pmodes[i].r.width),
				  le32_to_cpu(resp->pmodes[i].r.height),
				  le32_to_cpu(resp->pmodes[i].r.x),
				  le32_to_cpu(resp->pmodes[i].r.y));
		} else {
			DRM_DEBUG("output %d: disabled", i);
		}
	}

	vgdev->display_info_pending = false;
	spin_unlock(&vgdev->display_info_lock);
	wake_up(&vgdev->resp_wq);

	if (!drm_helper_hpd_irq_event(vgdev->ddev))
		drm_kms_helper_hotplug_event(vgdev->ddev);
}

static void virtio_gpu_cmd_get_capset_info_cb(struct virtio_gpu_device *vgdev,
					      struct virtio_gpu_vbuffer *vbuf)
{
	struct virtio_gpu_get_capset_info *cmd =
		(struct virtio_gpu_get_capset_info *)vbuf->buf;
	struct virtio_gpu_resp_capset_info *resp =
		(struct virtio_gpu_resp_capset_info *)vbuf->resp_buf;
	int i = le32_to_cpu(cmd->capset_index);

	spin_lock(&vgdev->display_info_lock);
	if (vgdev->capsets) {
		vgdev->capsets[i].id = le32_to_cpu(resp->capset_id);
		vgdev->capsets[i].max_version = le32_to_cpu(resp->capset_max_version);
		vgdev->capsets[i].max_size = le32_to_cpu(resp->capset_max_size);
	} else {
		DRM_ERROR("invalid capset memory.");
	}
	spin_unlock(&vgdev->display_info_lock);
	wake_up(&vgdev->resp_wq);
}

static void virtio_gpu_cmd_capset_cb(struct virtio_gpu_device *vgdev,
				     struct virtio_gpu_vbuffer *vbuf)
{
	struct virtio_gpu_get_capset *cmd =
		(struct virtio_gpu_get_capset *)vbuf->buf;
	struct virtio_gpu_resp_capset *resp =
		(struct virtio_gpu_resp_capset *)vbuf->resp_buf;
	struct virtio_gpu_drv_cap_cache *cache_ent;

	spin_lock(&vgdev->display_info_lock);
	list_for_each_entry(cache_ent, &vgdev->cap_cache, head) {
		if (cache_ent->version == le32_to_cpu(cmd->capset_version) &&
		    cache_ent->id == le32_to_cpu(cmd->capset_id)) {
			memcpy(cache_ent->caps_cache, resp->capset_data,
			       cache_ent->size);
			/* Copy must occur before is_valid is signalled. */
			smp_wmb();
			atomic_set(&cache_ent->is_valid, 1);
			break;
		}
	}
	spin_unlock(&vgdev->display_info_lock);
	wake_up_all(&vgdev->resp_wq);
}

static int virtio_get_edid_block(void *data, u8 *buf,
				 unsigned int block, size_t len)
{
	struct virtio_gpu_resp_edid *resp = data;
	size_t start = block * EDID_LENGTH;

	if (start + len > le32_to_cpu(resp->size))
		return -1;
	memcpy(buf, resp->edid + start, len);
	return 0;
}

static void virtio_gpu_cmd_get_edid_cb(struct virtio_gpu_device *vgdev,
				       struct virtio_gpu_vbuffer *vbuf)
{
	struct virtio_gpu_cmd_get_edid *cmd =
		(struct virtio_gpu_cmd_get_edid *)vbuf->buf;
	struct virtio_gpu_resp_edid *resp =
		(struct virtio_gpu_resp_edid *)vbuf->resp_buf;
	uint32_t scanout = le32_to_cpu(cmd->scanout);
	struct virtio_gpu_output *output;
	struct edid *new_edid, *old_edid;

	if (scanout >= vgdev->num_scanouts)
		return;
	output = vgdev->outputs + scanout;

	new_edid = drm_do_get_edid(&output->conn, virtio_get_edid_block, resp);
	drm_connector_update_edid_property(&output->conn, new_edid);

	spin_lock(&vgdev->display_info_lock);
	old_edid = output->edid;
	output->edid = new_edid;
	spin_unlock(&vgdev->display_info_lock);

	kfree(old_edid);
	wake_up(&vgdev->resp_wq);
}

int virtio_gpu_cmd_get_display_info(struct virtio_gpu_device *vgdev)
{
	struct virtio_gpu_ctrl_hdr *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;
	void *resp_buf;

	resp_buf = kzalloc(sizeof(struct virtio_gpu_resp_display_info),
			   GFP_KERNEL);
	if (!resp_buf)
		return -ENOMEM;

	cmd_p = virtio_gpu_alloc_cmd_resp
		(vgdev, &virtio_gpu_cmd_get_display_info_cb, &vbuf,
		 sizeof(*cmd_p), sizeof(struct virtio_gpu_resp_display_info),
		 resp_buf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	vgdev->display_info_pending = true;
	cmd_p->type = cpu_to_le32(VIRTIO_GPU_CMD_GET_DISPLAY_INFO);
	virtio_gpu_queue_ctrl_buffer(vgdev, vbuf);
	return 0;
}

int virtio_gpu_cmd_get_capset_info(struct virtio_gpu_device *vgdev, int idx)
{
	struct virtio_gpu_get_capset_info *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;
	void *resp_buf;

	resp_buf = kzalloc(sizeof(struct virtio_gpu_resp_capset_info),
			   GFP_KERNEL);
	if (!resp_buf)
		return -ENOMEM;

	cmd_p = virtio_gpu_alloc_cmd_resp
		(vgdev, &virtio_gpu_cmd_get_capset_info_cb, &vbuf,
		 sizeof(*cmd_p), sizeof(struct virtio_gpu_resp_capset_info),
		 resp_buf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_GET_CAPSET_INFO);
	cmd_p->capset_index = cpu_to_le32(idx);
	virtio_gpu_queue_ctrl_buffer(vgdev, vbuf);
	return 0;
}

int virtio_gpu_cmd_get_capset(struct virtio_gpu_device *vgdev,
			      int idx, int version,
			      struct virtio_gpu_drv_cap_cache **cache_p)
{
	struct virtio_gpu_get_capset *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;
	int max_size;
	struct virtio_gpu_drv_cap_cache *cache_ent;
	struct virtio_gpu_drv_cap_cache *search_ent;
	void *resp_buf;

	*cache_p = NULL;

	if (idx >= vgdev->num_capsets)
		return -EINVAL;

	if (version > vgdev->capsets[idx].max_version)
		return -EINVAL;

	cache_ent = kzalloc(sizeof(*cache_ent), GFP_KERNEL);
	if (!cache_ent)
		return -ENOMEM;

	max_size = vgdev->capsets[idx].max_size;
	cache_ent->caps_cache = kmalloc(max_size, GFP_KERNEL);
	if (!cache_ent->caps_cache) {
		kfree(cache_ent);
		return -ENOMEM;
	}

	resp_buf = kzalloc(sizeof(struct virtio_gpu_resp_capset) + max_size,
			   GFP_KERNEL);
	if (!resp_buf) {
		kfree(cache_ent->caps_cache);
		kfree(cache_ent);
		return -ENOMEM;
	}

	cache_ent->version = version;
	cache_ent->id = vgdev->capsets[idx].id;
	atomic_set(&cache_ent->is_valid, 0);
	cache_ent->size = max_size;
	spin_lock(&vgdev->display_info_lock);
	/* Search while under lock in case it was added by another task. */
	list_for_each_entry(search_ent, &vgdev->cap_cache, head) {
		if (search_ent->id == vgdev->capsets[idx].id &&
		    search_ent->version == version) {
			*cache_p = search_ent;
			break;
		}
	}
	if (!*cache_p)
		list_add_tail(&cache_ent->head, &vgdev->cap_cache);
	spin_unlock(&vgdev->display_info_lock);

	if (*cache_p) {
		/* Entry was found, so free everything that was just created. */
		kfree(resp_buf);
		kfree(cache_ent->caps_cache);
		kfree(cache_ent);
		return 0;
	}

	cmd_p = virtio_gpu_alloc_cmd_resp
		(vgdev, &virtio_gpu_cmd_capset_cb, &vbuf, sizeof(*cmd_p),
		 sizeof(struct virtio_gpu_resp_capset) + max_size,
		 resp_buf);
	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_GET_CAPSET);
	cmd_p->capset_id = cpu_to_le32(vgdev->capsets[idx].id);
	cmd_p->capset_version = cpu_to_le32(version);
	*cache_p = cache_ent;
	virtio_gpu_queue_ctrl_buffer(vgdev, vbuf);

	return 0;
}

int virtio_gpu_cmd_get_edids(struct virtio_gpu_device *vgdev)
{
	struct virtio_gpu_cmd_get_edid *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;
	void *resp_buf;
	int scanout;

	if (WARN_ON(!vgdev->has_edid))
		return -EINVAL;

	for (scanout = 0; scanout < vgdev->num_scanouts; scanout++) {
		resp_buf = kzalloc(sizeof(struct virtio_gpu_resp_edid),
				   GFP_KERNEL);
		if (!resp_buf)
			return -ENOMEM;

		cmd_p = virtio_gpu_alloc_cmd_resp
			(vgdev, &virtio_gpu_cmd_get_edid_cb, &vbuf,
			 sizeof(*cmd_p), sizeof(struct virtio_gpu_resp_edid),
			 resp_buf);
		cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_GET_EDID);
		cmd_p->scanout = cpu_to_le32(scanout);
		virtio_gpu_queue_ctrl_buffer(vgdev, vbuf);
	}

	return 0;
}

void virtio_gpu_cmd_context_create(struct virtio_gpu_device *vgdev, uint32_t id,
				   uint32_t nlen, const char *name)
{
	struct virtio_gpu_ctx_create *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_CTX_CREATE);
	cmd_p->hdr.ctx_id = cpu_to_le32(id);
	cmd_p->nlen = cpu_to_le32(nlen);
	strncpy(cmd_p->debug_name, name, sizeof(cmd_p->debug_name) - 1);
	cmd_p->debug_name[sizeof(cmd_p->debug_name) - 1] = 0;
	virtio_gpu_queue_ctrl_buffer(vgdev, vbuf);
}

void virtio_gpu_cmd_context_destroy(struct virtio_gpu_device *vgdev,
				    uint32_t id)
{
	struct virtio_gpu_ctx_destroy *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_CTX_DESTROY);
	cmd_p->hdr.ctx_id = cpu_to_le32(id);
	virtio_gpu_queue_ctrl_buffer(vgdev, vbuf);
}

void virtio_gpu_cmd_context_attach_resource(struct virtio_gpu_device *vgdev,
					    uint32_t ctx_id,
					    struct virtio_gpu_object_array *objs)
{
	struct virtio_gpu_object *bo = gem_to_virtio_gpu_obj(objs->objs[0]);
	struct virtio_gpu_ctx_resource *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	memset(cmd_p, 0, sizeof(*cmd_p));
	vbuf->objs = objs;

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE);
	cmd_p->hdr.ctx_id = cpu_to_le32(ctx_id);
	cmd_p->resource_id = cpu_to_le32(bo->hw_res_handle);
	virtio_gpu_queue_ctrl_buffer(vgdev, vbuf);
}

void virtio_gpu_cmd_context_detach_resource(struct virtio_gpu_device *vgdev,
					    uint32_t ctx_id,
					    struct virtio_gpu_object_array *objs)
{
	struct virtio_gpu_object *bo = gem_to_virtio_gpu_obj(objs->objs[0]);
	struct virtio_gpu_ctx_resource *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	memset(cmd_p, 0, sizeof(*cmd_p));
	vbuf->objs = objs;

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE);
	cmd_p->hdr.ctx_id = cpu_to_le32(ctx_id);
	cmd_p->resource_id = cpu_to_le32(bo->hw_res_handle);
	virtio_gpu_queue_ctrl_buffer(vgdev, vbuf);
}

void
virtio_gpu_cmd_resource_create_3d(struct virtio_gpu_device *vgdev,
				  struct virtio_gpu_object *bo,
				  struct virtio_gpu_object_params *params,
				  struct virtio_gpu_object_array *objs,
				  struct virtio_gpu_fence *fence)
{
	struct virtio_gpu_resource_create_3d *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	memset(cmd_p, 0, sizeof(*cmd_p));
	vbuf->objs = objs;

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_RESOURCE_CREATE_3D);
	cmd_p->resource_id = cpu_to_le32(bo->hw_res_handle);
	cmd_p->format = cpu_to_le32(params->format);
	cmd_p->width = cpu_to_le32(params->width);
	cmd_p->height = cpu_to_le32(params->height);

	cmd_p->target = cpu_to_le32(params->target);
	cmd_p->bind = cpu_to_le32(params->bind);
	cmd_p->depth = cpu_to_le32(params->depth);
	cmd_p->array_size = cpu_to_le32(params->array_size);
	cmd_p->last_level = cpu_to_le32(params->last_level);
	cmd_p->nr_samples = cpu_to_le32(params->nr_samples);
	cmd_p->flags = cpu_to_le32(params->flags);

	virtio_gpu_queue_fenced_ctrl_buffer(vgdev, vbuf, fence);

	bo->created = true;
}

void virtio_gpu_cmd_transfer_to_host_3d(struct virtio_gpu_device *vgdev,
					uint32_t ctx_id,
					uint64_t offset, uint32_t level,
					uint32_t stride,
					uint32_t layer_stride,
					struct drm_virtgpu_3d_box *box,
					struct virtio_gpu_object_array *objs,
					struct virtio_gpu_fence *fence)
{
	struct virtio_gpu_object *bo = gem_to_virtio_gpu_obj(objs->objs[0]);
	struct virtio_gpu_transfer_host_3d *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;
	bool use_dma_api = !virtio_has_dma_quirk(vgdev->vdev);

	if (virtio_gpu_is_shmem(bo) && use_dma_api) {
		struct virtio_gpu_object_shmem *shmem = to_virtio_gpu_shmem(bo);
		dma_sync_sgtable_for_device(vgdev->vdev->dev.parent,
					    shmem->pages, DMA_TO_DEVICE);
	}

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	memset(cmd_p, 0, sizeof(*cmd_p));

	vbuf->objs = objs;

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D);
	cmd_p->hdr.ctx_id = cpu_to_le32(ctx_id);
	cmd_p->resource_id = cpu_to_le32(bo->hw_res_handle);
	convert_to_hw_box(&cmd_p->box, box);
	cmd_p->offset = cpu_to_le64(offset);
	cmd_p->level = cpu_to_le32(level);
	cmd_p->stride = cpu_to_le32(stride);
	cmd_p->layer_stride = cpu_to_le32(layer_stride);

	virtio_gpu_queue_fenced_ctrl_buffer(vgdev, vbuf, fence);
}

void virtio_gpu_cmd_transfer_from_host_3d(struct virtio_gpu_device *vgdev,
					  uint32_t ctx_id,
					  uint64_t offset, uint32_t level,
					  uint32_t stride,
					  uint32_t layer_stride,
					  struct drm_virtgpu_3d_box *box,
					  struct virtio_gpu_object_array *objs,
					  struct virtio_gpu_fence *fence)
{
	struct virtio_gpu_object *bo = gem_to_virtio_gpu_obj(objs->objs[0]);
	struct virtio_gpu_transfer_host_3d *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	memset(cmd_p, 0, sizeof(*cmd_p));

	vbuf->objs = objs;

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D);
	cmd_p->hdr.ctx_id = cpu_to_le32(ctx_id);
	cmd_p->resource_id = cpu_to_le32(bo->hw_res_handle);
	convert_to_hw_box(&cmd_p->box, box);
	cmd_p->offset = cpu_to_le64(offset);
	cmd_p->level = cpu_to_le32(level);
	cmd_p->stride = cpu_to_le32(stride);
	cmd_p->layer_stride = cpu_to_le32(layer_stride);

	virtio_gpu_queue_fenced_ctrl_buffer(vgdev, vbuf, fence);
}

void virtio_gpu_cmd_submit(struct virtio_gpu_device *vgdev,
			   void *data, uint32_t data_size,
			   uint32_t ctx_id,
			   struct virtio_gpu_object_array *objs,
			   struct virtio_gpu_fence *fence)
{
	struct virtio_gpu_cmd_submit *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	memset(cmd_p, 0, sizeof(*cmd_p));

	vbuf->data_buf = data;
	vbuf->data_size = data_size;
	vbuf->objs = objs;

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_SUBMIT_3D);
	cmd_p->hdr.ctx_id = cpu_to_le32(ctx_id);
	cmd_p->size = cpu_to_le32(data_size);

	virtio_gpu_queue_fenced_ctrl_buffer(vgdev, vbuf, fence);
}

void virtio_gpu_object_attach(struct virtio_gpu_device *vgdev,
			      struct virtio_gpu_object *obj,
			      struct virtio_gpu_mem_entry *ents,
			      unsigned int nents)
{
	virtio_gpu_cmd_resource_attach_backing(vgdev, obj->hw_res_handle,
					       ents, nents, NULL);
}

void virtio_gpu_cursor_ping(struct virtio_gpu_device *vgdev,
			    struct virtio_gpu_output *output)
{
	struct virtio_gpu_vbuffer *vbuf;
	struct virtio_gpu_update_cursor *cur_p;

	output->cursor.pos.scanout_id = cpu_to_le32(output->index);
	cur_p = virtio_gpu_alloc_cursor(vgdev, &vbuf);
	memcpy(cur_p, &output->cursor, sizeof(output->cursor));
	virtio_gpu_queue_cursor(vgdev, vbuf);
}

static void virtio_gpu_cmd_resource_uuid_cb(struct virtio_gpu_device *vgdev,
					    struct virtio_gpu_vbuffer *vbuf)
{
	struct virtio_gpu_object *obj =
		gem_to_virtio_gpu_obj(vbuf->objs->objs[0]);
	struct virtio_gpu_resp_resource_uuid *resp =
		(struct virtio_gpu_resp_resource_uuid *)vbuf->resp_buf;
	uint32_t resp_type = le32_to_cpu(resp->hdr.type);

	spin_lock(&vgdev->resource_export_lock);
	WARN_ON(obj->uuid_state != STATE_INITIALIZING);

	if (resp_type == VIRTIO_GPU_RESP_OK_RESOURCE_UUID &&
	    obj->uuid_state == STATE_INITIALIZING) {
		import_uuid(&obj->uuid, resp->uuid);
		obj->uuid_state = STATE_OK;
	} else {
		obj->uuid_state = STATE_ERR;
	}
	spin_unlock(&vgdev->resource_export_lock);

	wake_up_all(&vgdev->resp_wq);
}

int
virtio_gpu_cmd_resource_assign_uuid(struct virtio_gpu_device *vgdev,
				    struct virtio_gpu_object_array *objs)
{
	struct virtio_gpu_object *bo = gem_to_virtio_gpu_obj(objs->objs[0]);
	struct virtio_gpu_resource_assign_uuid *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;
	struct virtio_gpu_resp_resource_uuid *resp_buf;

	resp_buf = kzalloc(sizeof(*resp_buf), GFP_KERNEL);
	if (!resp_buf) {
		spin_lock(&vgdev->resource_export_lock);
		bo->uuid_state = STATE_ERR;
		spin_unlock(&vgdev->resource_export_lock);
		virtio_gpu_array_put_free(objs);
		return -ENOMEM;
	}

	cmd_p = virtio_gpu_alloc_cmd_resp
		(vgdev, virtio_gpu_cmd_resource_uuid_cb, &vbuf, sizeof(*cmd_p),
		 sizeof(struct virtio_gpu_resp_resource_uuid), resp_buf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_RESOURCE_ASSIGN_UUID);
	cmd_p->resource_id = cpu_to_le32(bo->hw_res_handle);

	vbuf->objs = objs;
	virtio_gpu_queue_ctrl_buffer(vgdev, vbuf);
	return 0;
}

static void virtio_gpu_cmd_resource_map_cb(struct virtio_gpu_device *vgdev,
					   struct virtio_gpu_vbuffer *vbuf)
{
	struct virtio_gpu_object *bo =
		gem_to_virtio_gpu_obj(vbuf->objs->objs[0]);
	struct virtio_gpu_resp_map_info *resp =
		(struct virtio_gpu_resp_map_info *)vbuf->resp_buf;
	struct virtio_gpu_object_vram *vram = to_virtio_gpu_vram(bo);
	uint32_t resp_type = le32_to_cpu(resp->hdr.type);

	spin_lock(&vgdev->host_visible_lock);

	if (resp_type == VIRTIO_GPU_RESP_OK_MAP_INFO) {
		vram->map_info = resp->map_info;
		vram->map_state = STATE_OK;
	} else {
		vram->map_state = STATE_ERR;
	}

	spin_unlock(&vgdev->host_visible_lock);
	wake_up_all(&vgdev->resp_wq);
}

int virtio_gpu_cmd_map(struct virtio_gpu_device *vgdev,
		       struct virtio_gpu_object_array *objs, uint64_t offset)
{
	struct virtio_gpu_resource_map_blob *cmd_p;
	struct virtio_gpu_object *bo = gem_to_virtio_gpu_obj(objs->objs[0]);
	struct virtio_gpu_vbuffer *vbuf;
	struct virtio_gpu_resp_map_info *resp_buf;

	resp_buf = kzalloc(sizeof(*resp_buf), GFP_KERNEL);
	if (!resp_buf)
		return -ENOMEM;

	cmd_p = virtio_gpu_alloc_cmd_resp
		(vgdev, virtio_gpu_cmd_resource_map_cb, &vbuf, sizeof(*cmd_p),
		 sizeof(struct virtio_gpu_resp_map_info), resp_buf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_RESOURCE_MAP_BLOB);
	cmd_p->resource_id = cpu_to_le32(bo->hw_res_handle);
	cmd_p->offset = cpu_to_le64(offset);
	vbuf->objs = objs;

	virtio_gpu_queue_ctrl_buffer(vgdev, vbuf);
	return 0;
}

void virtio_gpu_cmd_unmap(struct virtio_gpu_device *vgdev,
			  struct virtio_gpu_object *bo)
{
	struct virtio_gpu_resource_unmap_blob *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_RESOURCE_UNMAP_BLOB);
	cmd_p->resource_id = cpu_to_le32(bo->hw_res_handle);

	virtio_gpu_queue_ctrl_buffer(vgdev, vbuf);
}

void
virtio_gpu_cmd_resource_create_blob(struct virtio_gpu_device *vgdev,
				    struct virtio_gpu_object *bo,
				    struct virtio_gpu_object_params *params,
				    struct virtio_gpu_mem_entry *ents,
				    uint32_t nents)
{
	struct virtio_gpu_resource_create_blob *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB);
	cmd_p->hdr.ctx_id = cpu_to_le32(params->ctx_id);
	cmd_p->resource_id = cpu_to_le32(bo->hw_res_handle);
	cmd_p->blob_mem = cpu_to_le32(params->blob_mem);
	cmd_p->blob_flags = cpu_to_le32(params->blob_flags);
	cmd_p->blob_id = cpu_to_le64(params->blob_id);
	cmd_p->size = cpu_to_le64(params->size);
	cmd_p->nr_entries = cpu_to_le32(nents);

	vbuf->data_buf = ents;
	vbuf->data_size = sizeof(*ents) * nents;

	virtio_gpu_queue_ctrl_buffer(vgdev, vbuf);
	bo->created = true;
}

void virtio_gpu_cmd_set_scanout_blob(struct virtio_gpu_device *vgdev,
				     uint32_t scanout_id,
				     struct virtio_gpu_object *bo,
				     struct drm_framebuffer *fb,
				     uint32_t width, uint32_t height,
				     uint32_t x, uint32_t y)
{
	uint32_t i;
	struct virtio_gpu_set_scanout_blob *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;
	uint32_t format = virtio_gpu_translate_format(fb->format->format);

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_SET_SCANOUT_BLOB);
	cmd_p->resource_id = cpu_to_le32(bo->hw_res_handle);
	cmd_p->scanout_id = cpu_to_le32(scanout_id);

	cmd_p->format = cpu_to_le32(format);
	cmd_p->width  = cpu_to_le32(fb->width);
	cmd_p->height = cpu_to_le32(fb->height);

	for (i = 0; i < 4; i++) {
		cmd_p->strides[i] = cpu_to_le32(fb->pitches[i]);
		cmd_p->offsets[i] = cpu_to_le32(fb->offsets[i]);
	}

	cmd_p->r.width = cpu_to_le32(width);
	cmd_p->r.height = cpu_to_le32(height);
	cmd_p->r.x = cpu_to_le32(x);
	cmd_p->r.y = cpu_to_le32(y);

	virtio_gpu_queue_ctrl_buffer(vgdev, vbuf);
}
