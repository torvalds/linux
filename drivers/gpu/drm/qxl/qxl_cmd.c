/*
 * Copyright 2013 Red Hat Inc.
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
 * Authors: Dave Airlie
 *          Alon Levy
 */

/* QXL cmd/ring handling */

#include "qxl_drv.h"
#include "qxl_object.h"

static int qxl_reap_surface_id(struct qxl_device *qdev, int max_to_reap);

struct ring {
	struct qxl_ring_header      header;
	uint8_t                     elements[0];
};

struct qxl_ring {
	struct ring	       *ring;
	int			element_size;
	int			n_elements;
	int			prod_notify;
	wait_queue_head_t      *push_event;
	spinlock_t             lock;
};

void qxl_ring_free(struct qxl_ring *ring)
{
	kfree(ring);
}

void qxl_ring_init_hdr(struct qxl_ring *ring)
{
	ring->ring->header.notify_on_prod = ring->n_elements;
}

struct qxl_ring *
qxl_ring_create(struct qxl_ring_header *header,
		int element_size,
		int n_elements,
		int prod_notify,
		bool set_prod_notify,
		wait_queue_head_t *push_event)
{
	struct qxl_ring *ring;

	ring = kmalloc(sizeof(*ring), GFP_KERNEL);
	if (!ring)
		return NULL;

	ring->ring = (struct ring *)header;
	ring->element_size = element_size;
	ring->n_elements = n_elements;
	ring->prod_notify = prod_notify;
	ring->push_event = push_event;
	if (set_prod_notify)
		qxl_ring_init_hdr(ring);
	spin_lock_init(&ring->lock);
	return ring;
}

static int qxl_check_header(struct qxl_ring *ring)
{
	int ret;
	struct qxl_ring_header *header = &(ring->ring->header);
	unsigned long flags;
	spin_lock_irqsave(&ring->lock, flags);
	ret = header->prod - header->cons < header->num_items;
	if (ret == 0)
		header->notify_on_cons = header->cons + 1;
	spin_unlock_irqrestore(&ring->lock, flags);
	return ret;
}

int qxl_check_idle(struct qxl_ring *ring)
{
	int ret;
	struct qxl_ring_header *header = &(ring->ring->header);
	unsigned long flags;
	spin_lock_irqsave(&ring->lock, flags);
	ret = header->prod == header->cons;
	spin_unlock_irqrestore(&ring->lock, flags);
	return ret;
}

int qxl_ring_push(struct qxl_ring *ring,
		  const void *new_elt, bool interruptible)
{
	struct qxl_ring_header *header = &(ring->ring->header);
	uint8_t *elt;
	int idx, ret;
	unsigned long flags;
	spin_lock_irqsave(&ring->lock, flags);
	if (header->prod - header->cons == header->num_items) {
		header->notify_on_cons = header->cons + 1;
		mb();
		spin_unlock_irqrestore(&ring->lock, flags);
		if (!drm_can_sleep()) {
			while (!qxl_check_header(ring))
				udelay(1);
		} else {
			if (interruptible) {
				ret = wait_event_interruptible(*ring->push_event,
							       qxl_check_header(ring));
				if (ret)
					return ret;
			} else {
				wait_event(*ring->push_event,
					   qxl_check_header(ring));
			}

		}
		spin_lock_irqsave(&ring->lock, flags);
	}

	idx = header->prod & (ring->n_elements - 1);
	elt = ring->ring->elements + idx * ring->element_size;

	memcpy((void *)elt, new_elt, ring->element_size);

	header->prod++;

	mb();

	if (header->prod == header->notify_on_prod)
		outb(0, ring->prod_notify);

	spin_unlock_irqrestore(&ring->lock, flags);
	return 0;
}

static bool qxl_ring_pop(struct qxl_ring *ring,
			 void *element)
{
	volatile struct qxl_ring_header *header = &(ring->ring->header);
	volatile uint8_t *ring_elt;
	int idx;
	unsigned long flags;
	spin_lock_irqsave(&ring->lock, flags);
	if (header->cons == header->prod) {
		header->notify_on_prod = header->cons + 1;
		spin_unlock_irqrestore(&ring->lock, flags);
		return false;
	}

	idx = header->cons & (ring->n_elements - 1);
	ring_elt = ring->ring->elements + idx * ring->element_size;

	memcpy(element, (void *)ring_elt, ring->element_size);

	header->cons++;

	spin_unlock_irqrestore(&ring->lock, flags);
	return true;
}

int
qxl_push_command_ring_release(struct qxl_device *qdev, struct qxl_release *release,
			      uint32_t type, bool interruptible)
{
	struct qxl_command cmd;
	struct qxl_bo_list *entry = list_first_entry(&release->bos, struct qxl_bo_list, tv.head);

	cmd.type = type;
	cmd.data = qxl_bo_physical_address(qdev, to_qxl_bo(entry->tv.bo), release->release_offset);

	return qxl_ring_push(qdev->command_ring, &cmd, interruptible);
}

int
qxl_push_cursor_ring_release(struct qxl_device *qdev, struct qxl_release *release,
			     uint32_t type, bool interruptible)
{
	struct qxl_command cmd;
	struct qxl_bo_list *entry = list_first_entry(&release->bos, struct qxl_bo_list, tv.head);

	cmd.type = type;
	cmd.data = qxl_bo_physical_address(qdev, to_qxl_bo(entry->tv.bo), release->release_offset);

	return qxl_ring_push(qdev->cursor_ring, &cmd, interruptible);
}

bool qxl_queue_garbage_collect(struct qxl_device *qdev, bool flush)
{
	if (!qxl_check_idle(qdev->release_ring)) {
		queue_work(qdev->gc_queue, &qdev->gc_work);
		if (flush)
			flush_work(&qdev->gc_work);
		return true;
	}
	return false;
}

int qxl_garbage_collect(struct qxl_device *qdev)
{
	struct qxl_release *release;
	uint64_t id, next_id;
	int i = 0;
	union qxl_release_info *info;

	while (qxl_ring_pop(qdev->release_ring, &id)) {
		QXL_INFO(qdev, "popped %lld\n", id);
		while (id) {
			release = qxl_release_from_id_locked(qdev, id);
			if (release == NULL)
				break;

			info = qxl_release_map(qdev, release);
			next_id = info->next;
			qxl_release_unmap(qdev, release, info);

			QXL_INFO(qdev, "popped %lld, next %lld\n", id,
				next_id);

			switch (release->type) {
			case QXL_RELEASE_DRAWABLE:
			case QXL_RELEASE_SURFACE_CMD:
			case QXL_RELEASE_CURSOR_CMD:
				break;
			default:
				DRM_ERROR("unexpected release type\n");
				break;
			}
			id = next_id;

			qxl_release_free(qdev, release);
			++i;
		}
	}

	QXL_INFO(qdev, "%s: %d\n", __func__, i);

	return i;
}

int qxl_alloc_bo_reserved(struct qxl_device *qdev,
			  struct qxl_release *release,
			  unsigned long size,
			  struct qxl_bo **_bo)
{
	struct qxl_bo *bo;
	int ret;

	ret = qxl_bo_create(qdev, size, false /* not kernel - device */,
			    false, QXL_GEM_DOMAIN_VRAM, NULL, &bo);
	if (ret) {
		DRM_ERROR("failed to allocate VRAM BO\n");
		return ret;
	}
	ret = qxl_release_list_add(release, bo);
	if (ret)
		goto out_unref;

	*_bo = bo;
	return 0;
out_unref:
	qxl_bo_unref(&bo);
	return ret;
}

static int wait_for_io_cmd_user(struct qxl_device *qdev, uint8_t val, long port, bool intr)
{
	int irq_num;
	long addr = qdev->io_base + port;
	int ret;

	mutex_lock(&qdev->async_io_mutex);
	irq_num = atomic_read(&qdev->irq_received_io_cmd);
	if (qdev->last_sent_io_cmd > irq_num) {
		if (intr)
			ret = wait_event_interruptible_timeout(qdev->io_cmd_event,
							       atomic_read(&qdev->irq_received_io_cmd) > irq_num, 5*HZ);
		else
			ret = wait_event_timeout(qdev->io_cmd_event,
						 atomic_read(&qdev->irq_received_io_cmd) > irq_num, 5*HZ);
		/* 0 is timeout, just bail the "hw" has gone away */
		if (ret <= 0)
			goto out;
		irq_num = atomic_read(&qdev->irq_received_io_cmd);
	}
	outb(val, addr);
	qdev->last_sent_io_cmd = irq_num + 1;
	if (intr)
		ret = wait_event_interruptible_timeout(qdev->io_cmd_event,
						       atomic_read(&qdev->irq_received_io_cmd) > irq_num, 5*HZ);
	else
		ret = wait_event_timeout(qdev->io_cmd_event,
					 atomic_read(&qdev->irq_received_io_cmd) > irq_num, 5*HZ);
out:
	if (ret > 0)
		ret = 0;
	mutex_unlock(&qdev->async_io_mutex);
	return ret;
}

static void wait_for_io_cmd(struct qxl_device *qdev, uint8_t val, long port)
{
	int ret;

restart:
	ret = wait_for_io_cmd_user(qdev, val, port, false);
	if (ret == -ERESTARTSYS)
		goto restart;
}

int qxl_io_update_area(struct qxl_device *qdev, struct qxl_bo *surf,
			const struct qxl_rect *area)
{
	int surface_id;
	uint32_t surface_width, surface_height;
	int ret;

	if (!surf->hw_surf_alloc)
		DRM_ERROR("got io update area with no hw surface\n");

	if (surf->is_primary)
		surface_id = 0;
	else
		surface_id = surf->surface_id;
	surface_width = surf->surf.width;
	surface_height = surf->surf.height;

	if (area->left < 0 || area->top < 0 ||
	    area->right > surface_width || area->bottom > surface_height) {
		qxl_io_log(qdev, "%s: not doing area update for "
			   "%d, (%d,%d,%d,%d) (%d,%d)\n", __func__, surface_id, area->left,
			   area->top, area->right, area->bottom, surface_width, surface_height);
		return -EINVAL;
	}
	mutex_lock(&qdev->update_area_mutex);
	qdev->ram_header->update_area = *area;
	qdev->ram_header->update_surface = surface_id;
	ret = wait_for_io_cmd_user(qdev, 0, QXL_IO_UPDATE_AREA_ASYNC, true);
	mutex_unlock(&qdev->update_area_mutex);
	return ret;
}

void qxl_io_notify_oom(struct qxl_device *qdev)
{
	outb(0, qdev->io_base + QXL_IO_NOTIFY_OOM);
}

void qxl_io_flush_release(struct qxl_device *qdev)
{
	outb(0, qdev->io_base + QXL_IO_FLUSH_RELEASE);
}

void qxl_io_flush_surfaces(struct qxl_device *qdev)
{
	wait_for_io_cmd(qdev, 0, QXL_IO_FLUSH_SURFACES_ASYNC);
}


void qxl_io_destroy_primary(struct qxl_device *qdev)
{
	wait_for_io_cmd(qdev, 0, QXL_IO_DESTROY_PRIMARY_ASYNC);
}

void qxl_io_create_primary(struct qxl_device *qdev,
			   unsigned offset, struct qxl_bo *bo)
{
	struct qxl_surface_create *create;

	QXL_INFO(qdev, "%s: qdev %p, ram_header %p\n", __func__, qdev,
		 qdev->ram_header);
	create = &qdev->ram_header->create_surface;
	create->format = bo->surf.format;
	create->width = bo->surf.width;
	create->height = bo->surf.height;
	create->stride = bo->surf.stride;
	create->mem = qxl_bo_physical_address(qdev, bo, offset);

	QXL_INFO(qdev, "%s: mem = %llx, from %p\n", __func__, create->mem,
		 bo->kptr);

	create->flags = QXL_SURF_FLAG_KEEP_DATA;
	create->type = QXL_SURF_TYPE_PRIMARY;

	wait_for_io_cmd(qdev, 0, QXL_IO_CREATE_PRIMARY_ASYNC);
}

void qxl_io_memslot_add(struct qxl_device *qdev, uint8_t id)
{
	QXL_INFO(qdev, "qxl_memslot_add %d\n", id);
	wait_for_io_cmd(qdev, id, QXL_IO_MEMSLOT_ADD_ASYNC);
}

void qxl_io_log(struct qxl_device *qdev, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsnprintf(qdev->ram_header->log_buf, QXL_LOG_BUF_SIZE, fmt, args);
	va_end(args);
	/*
	 * DO not do a DRM output here - this will call printk, which will
	 * call back into qxl for rendering (qxl_fb)
	 */
	outb(0, qdev->io_base + QXL_IO_LOG);
}

void qxl_io_reset(struct qxl_device *qdev)
{
	outb(0, qdev->io_base + QXL_IO_RESET);
}

void qxl_io_monitors_config(struct qxl_device *qdev)
{
	qxl_io_log(qdev, "%s: %d [%dx%d+%d+%d]\n", __func__,
		   qdev->monitors_config ?
		   qdev->monitors_config->count : -1,
		   qdev->monitors_config && qdev->monitors_config->count ?
		   qdev->monitors_config->heads[0].width : -1,
		   qdev->monitors_config && qdev->monitors_config->count ?
		   qdev->monitors_config->heads[0].height : -1,
		   qdev->monitors_config && qdev->monitors_config->count ?
		   qdev->monitors_config->heads[0].x : -1,
		   qdev->monitors_config && qdev->monitors_config->count ?
		   qdev->monitors_config->heads[0].y : -1
		   );

	wait_for_io_cmd(qdev, 0, QXL_IO_MONITORS_CONFIG_ASYNC);
}

int qxl_surface_id_alloc(struct qxl_device *qdev,
		      struct qxl_bo *surf)
{
	uint32_t handle;
	int idr_ret;
	int count = 0;
again:
	idr_preload(GFP_ATOMIC);
	spin_lock(&qdev->surf_id_idr_lock);
	idr_ret = idr_alloc(&qdev->surf_id_idr, NULL, 1, 0, GFP_NOWAIT);
	spin_unlock(&qdev->surf_id_idr_lock);
	idr_preload_end();
	if (idr_ret < 0)
		return idr_ret;
	handle = idr_ret;

	if (handle >= qdev->rom->n_surfaces) {
		count++;
		spin_lock(&qdev->surf_id_idr_lock);
		idr_remove(&qdev->surf_id_idr, handle);
		spin_unlock(&qdev->surf_id_idr_lock);
		qxl_reap_surface_id(qdev, 2);
		goto again;
	}
	surf->surface_id = handle;

	spin_lock(&qdev->surf_id_idr_lock);
	qdev->last_alloced_surf_id = handle;
	spin_unlock(&qdev->surf_id_idr_lock);
	return 0;
}

void qxl_surface_id_dealloc(struct qxl_device *qdev,
			    uint32_t surface_id)
{
	spin_lock(&qdev->surf_id_idr_lock);
	idr_remove(&qdev->surf_id_idr, surface_id);
	spin_unlock(&qdev->surf_id_idr_lock);
}

int qxl_hw_surface_alloc(struct qxl_device *qdev,
			 struct qxl_bo *surf,
			 struct ttm_mem_reg *new_mem)
{
	struct qxl_surface_cmd *cmd;
	struct qxl_release *release;
	int ret;

	if (surf->hw_surf_alloc)
		return 0;

	ret = qxl_alloc_surface_release_reserved(qdev, QXL_SURFACE_CMD_CREATE,
						 NULL,
						 &release);
	if (ret)
		return ret;

	ret = qxl_release_reserve_list(release, true);
	if (ret)
		return ret;

	cmd = (struct qxl_surface_cmd *)qxl_release_map(qdev, release);
	cmd->type = QXL_SURFACE_CMD_CREATE;
	cmd->flags = QXL_SURF_FLAG_KEEP_DATA;
	cmd->u.surface_create.format = surf->surf.format;
	cmd->u.surface_create.width = surf->surf.width;
	cmd->u.surface_create.height = surf->surf.height;
	cmd->u.surface_create.stride = surf->surf.stride;
	if (new_mem) {
		int slot_id = surf->type == QXL_GEM_DOMAIN_VRAM ? qdev->main_mem_slot : qdev->surfaces_mem_slot;
		struct qxl_memslot *slot = &(qdev->mem_slots[slot_id]);

		/* TODO - need to hold one of the locks to read tbo.offset */
		cmd->u.surface_create.data = slot->high_bits;

		cmd->u.surface_create.data |= (new_mem->start << PAGE_SHIFT) + surf->tbo.bdev->man[new_mem->mem_type].gpu_offset;
	} else
		cmd->u.surface_create.data = qxl_bo_physical_address(qdev, surf, 0);
	cmd->surface_id = surf->surface_id;
	qxl_release_unmap(qdev, release, &cmd->release_info);

	surf->surf_create = release;

	/* no need to add a release to the fence for this surface bo,
	   since it is only released when we ask to destroy the surface
	   and it would never signal otherwise */
	qxl_push_command_ring_release(qdev, release, QXL_CMD_SURFACE, false);
	qxl_release_fence_buffer_objects(release);

	surf->hw_surf_alloc = true;
	spin_lock(&qdev->surf_id_idr_lock);
	idr_replace(&qdev->surf_id_idr, surf, surf->surface_id);
	spin_unlock(&qdev->surf_id_idr_lock);
	return 0;
}

int qxl_hw_surface_dealloc(struct qxl_device *qdev,
			   struct qxl_bo *surf)
{
	struct qxl_surface_cmd *cmd;
	struct qxl_release *release;
	int ret;
	int id;

	if (!surf->hw_surf_alloc)
		return 0;

	ret = qxl_alloc_surface_release_reserved(qdev, QXL_SURFACE_CMD_DESTROY,
						 surf->surf_create,
						 &release);
	if (ret)
		return ret;

	surf->surf_create = NULL;
	/* remove the surface from the idr, but not the surface id yet */
	spin_lock(&qdev->surf_id_idr_lock);
	idr_replace(&qdev->surf_id_idr, NULL, surf->surface_id);
	spin_unlock(&qdev->surf_id_idr_lock);
	surf->hw_surf_alloc = false;

	id = surf->surface_id;
	surf->surface_id = 0;

	release->surface_release_id = id;
	cmd = (struct qxl_surface_cmd *)qxl_release_map(qdev, release);
	cmd->type = QXL_SURFACE_CMD_DESTROY;
	cmd->surface_id = id;
	qxl_release_unmap(qdev, release, &cmd->release_info);

	qxl_push_command_ring_release(qdev, release, QXL_CMD_SURFACE, false);

	qxl_release_fence_buffer_objects(release);

	return 0;
}

int qxl_update_surface(struct qxl_device *qdev, struct qxl_bo *surf)
{
	struct qxl_rect rect;
	int ret;

	/* if we are evicting, we need to make sure the surface is up
	   to date */
	rect.left = 0;
	rect.right = surf->surf.width;
	rect.top = 0;
	rect.bottom = surf->surf.height;
retry:
	ret = qxl_io_update_area(qdev, surf, &rect);
	if (ret == -ERESTARTSYS)
		goto retry;
	return ret;
}

static void qxl_surface_evict_locked(struct qxl_device *qdev, struct qxl_bo *surf, bool do_update_area)
{
	/* no need to update area if we are just freeing the surface normally */
	if (do_update_area)
		qxl_update_surface(qdev, surf);

	/* nuke the surface id at the hw */
	qxl_hw_surface_dealloc(qdev, surf);
}

void qxl_surface_evict(struct qxl_device *qdev, struct qxl_bo *surf, bool do_update_area)
{
	mutex_lock(&qdev->surf_evict_mutex);
	qxl_surface_evict_locked(qdev, surf, do_update_area);
	mutex_unlock(&qdev->surf_evict_mutex);
}

static int qxl_reap_surf(struct qxl_device *qdev, struct qxl_bo *surf, bool stall)
{
	int ret;

	ret = qxl_bo_reserve(surf, false);
	if (ret)
		return ret;

	if (stall)
		mutex_unlock(&qdev->surf_evict_mutex);

	ret = ttm_bo_wait(&surf->tbo, true, true, !stall);

	if (stall)
		mutex_lock(&qdev->surf_evict_mutex);
	if (ret) {
		qxl_bo_unreserve(surf);
		return ret;
	}

	qxl_surface_evict_locked(qdev, surf, true);
	qxl_bo_unreserve(surf);
	return 0;
}

static int qxl_reap_surface_id(struct qxl_device *qdev, int max_to_reap)
{
	int num_reaped = 0;
	int i, ret;
	bool stall = false;
	int start = 0;

	mutex_lock(&qdev->surf_evict_mutex);
again:

	spin_lock(&qdev->surf_id_idr_lock);
	start = qdev->last_alloced_surf_id + 1;
	spin_unlock(&qdev->surf_id_idr_lock);

	for (i = start; i < start + qdev->rom->n_surfaces; i++) {
		void *objptr;
		int surfid = i % qdev->rom->n_surfaces;

		/* this avoids the case where the objects is in the
		   idr but has been evicted half way - its makes
		   the idr lookup atomic with the eviction */
		spin_lock(&qdev->surf_id_idr_lock);
		objptr = idr_find(&qdev->surf_id_idr, surfid);
		spin_unlock(&qdev->surf_id_idr_lock);

		if (!objptr)
			continue;

		ret = qxl_reap_surf(qdev, objptr, stall);
		if (ret == 0)
			num_reaped++;
		if (num_reaped >= max_to_reap)
			break;
	}
	if (num_reaped == 0 && stall == false) {
		stall = true;
		goto again;
	}

	mutex_unlock(&qdev->surf_evict_mutex);
	if (num_reaped) {
		usleep_range(500, 1000);
		qxl_queue_garbage_collect(qdev, true);
	}

	return 0;
}
