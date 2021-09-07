// SPDX-License-Identifier: GPL-2.0-only
/*
 * VDUSE: vDPA Device in Userspace
 *
 * Copyright (C) 2020-2021 Bytedance Inc. and/or its affiliates. All rights reserved.
 *
 * Author: Xie Yongji <xieyongji@bytedance.com>
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/eventfd.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/dma-map-ops.h>
#include <linux/poll.h>
#include <linux/file.h>
#include <linux/uio.h>
#include <linux/vdpa.h>
#include <linux/nospec.h>
#include <uapi/linux/vduse.h>
#include <uapi/linux/vdpa.h>
#include <uapi/linux/virtio_config.h>
#include <uapi/linux/virtio_ids.h>
#include <uapi/linux/virtio_blk.h>
#include <linux/mod_devicetable.h>

#include "iova_domain.h"

#define DRV_AUTHOR   "Yongji Xie <xieyongji@bytedance.com>"
#define DRV_DESC     "vDPA Device in Userspace"
#define DRV_LICENSE  "GPL v2"

#define VDUSE_DEV_MAX (1U << MINORBITS)
#define VDUSE_BOUNCE_SIZE (64 * 1024 * 1024)
#define VDUSE_IOVA_SIZE (128 * 1024 * 1024)
#define VDUSE_MSG_DEFAULT_TIMEOUT 30

struct vduse_virtqueue {
	u16 index;
	u16 num_max;
	u32 num;
	u64 desc_addr;
	u64 driver_addr;
	u64 device_addr;
	struct vdpa_vq_state state;
	bool ready;
	bool kicked;
	spinlock_t kick_lock;
	spinlock_t irq_lock;
	struct eventfd_ctx *kickfd;
	struct vdpa_callback cb;
	struct work_struct inject;
	struct work_struct kick;
};

struct vduse_dev;

struct vduse_vdpa {
	struct vdpa_device vdpa;
	struct vduse_dev *dev;
};

struct vduse_dev {
	struct vduse_vdpa *vdev;
	struct device *dev;
	struct vduse_virtqueue *vqs;
	struct vduse_iova_domain *domain;
	char *name;
	struct mutex lock;
	spinlock_t msg_lock;
	u64 msg_unique;
	u32 msg_timeout;
	wait_queue_head_t waitq;
	struct list_head send_list;
	struct list_head recv_list;
	struct vdpa_callback config_cb;
	struct work_struct inject;
	spinlock_t irq_lock;
	int minor;
	bool broken;
	bool connected;
	u64 api_version;
	u64 device_features;
	u64 driver_features;
	u32 device_id;
	u32 vendor_id;
	u32 generation;
	u32 config_size;
	void *config;
	u8 status;
	u32 vq_num;
	u32 vq_align;
};

struct vduse_dev_msg {
	struct vduse_dev_request req;
	struct vduse_dev_response resp;
	struct list_head list;
	wait_queue_head_t waitq;
	bool completed;
};

struct vduse_control {
	u64 api_version;
};

static DEFINE_MUTEX(vduse_lock);
static DEFINE_IDR(vduse_idr);

static dev_t vduse_major;
static struct class *vduse_class;
static struct cdev vduse_ctrl_cdev;
static struct cdev vduse_cdev;
static struct workqueue_struct *vduse_irq_wq;

static u32 allowed_device_id[] = {
	VIRTIO_ID_BLOCK,
};

static inline struct vduse_dev *vdpa_to_vduse(struct vdpa_device *vdpa)
{
	struct vduse_vdpa *vdev = container_of(vdpa, struct vduse_vdpa, vdpa);

	return vdev->dev;
}

static inline struct vduse_dev *dev_to_vduse(struct device *dev)
{
	struct vdpa_device *vdpa = dev_to_vdpa(dev);

	return vdpa_to_vduse(vdpa);
}

static struct vduse_dev_msg *vduse_find_msg(struct list_head *head,
					    uint32_t request_id)
{
	struct vduse_dev_msg *msg;

	list_for_each_entry(msg, head, list) {
		if (msg->req.request_id == request_id) {
			list_del(&msg->list);
			return msg;
		}
	}

	return NULL;
}

static struct vduse_dev_msg *vduse_dequeue_msg(struct list_head *head)
{
	struct vduse_dev_msg *msg = NULL;

	if (!list_empty(head)) {
		msg = list_first_entry(head, struct vduse_dev_msg, list);
		list_del(&msg->list);
	}

	return msg;
}

static void vduse_enqueue_msg(struct list_head *head,
			      struct vduse_dev_msg *msg)
{
	list_add_tail(&msg->list, head);
}

static void vduse_dev_broken(struct vduse_dev *dev)
{
	struct vduse_dev_msg *msg, *tmp;

	if (unlikely(dev->broken))
		return;

	list_splice_init(&dev->recv_list, &dev->send_list);
	list_for_each_entry_safe(msg, tmp, &dev->send_list, list) {
		list_del(&msg->list);
		msg->completed = 1;
		msg->resp.result = VDUSE_REQ_RESULT_FAILED;
		wake_up(&msg->waitq);
	}
	dev->broken = true;
	wake_up(&dev->waitq);
}

static int vduse_dev_msg_sync(struct vduse_dev *dev,
			      struct vduse_dev_msg *msg)
{
	int ret;

	if (unlikely(dev->broken))
		return -EIO;

	init_waitqueue_head(&msg->waitq);
	spin_lock(&dev->msg_lock);
	if (unlikely(dev->broken)) {
		spin_unlock(&dev->msg_lock);
		return -EIO;
	}
	msg->req.request_id = dev->msg_unique++;
	vduse_enqueue_msg(&dev->send_list, msg);
	wake_up(&dev->waitq);
	spin_unlock(&dev->msg_lock);
	if (dev->msg_timeout)
		ret = wait_event_killable_timeout(msg->waitq, msg->completed,
						  (long)dev->msg_timeout * HZ);
	else
		ret = wait_event_killable(msg->waitq, msg->completed);

	spin_lock(&dev->msg_lock);
	if (!msg->completed) {
		list_del(&msg->list);
		msg->resp.result = VDUSE_REQ_RESULT_FAILED;
		/* Mark the device as malfunction when there is a timeout */
		if (!ret)
			vduse_dev_broken(dev);
	}
	ret = (msg->resp.result == VDUSE_REQ_RESULT_OK) ? 0 : -EIO;
	spin_unlock(&dev->msg_lock);

	return ret;
}

static int vduse_dev_get_vq_state_packed(struct vduse_dev *dev,
					 struct vduse_virtqueue *vq,
					 struct vdpa_vq_state_packed *packed)
{
	struct vduse_dev_msg msg = { 0 };
	int ret;

	msg.req.type = VDUSE_GET_VQ_STATE;
	msg.req.vq_state.index = vq->index;

	ret = vduse_dev_msg_sync(dev, &msg);
	if (ret)
		return ret;

	packed->last_avail_counter =
			msg.resp.vq_state.packed.last_avail_counter & 0x0001;
	packed->last_avail_idx =
			msg.resp.vq_state.packed.last_avail_idx & 0x7FFF;
	packed->last_used_counter =
			msg.resp.vq_state.packed.last_used_counter & 0x0001;
	packed->last_used_idx =
			msg.resp.vq_state.packed.last_used_idx & 0x7FFF;

	return 0;
}

static int vduse_dev_get_vq_state_split(struct vduse_dev *dev,
					struct vduse_virtqueue *vq,
					struct vdpa_vq_state_split *split)
{
	struct vduse_dev_msg msg = { 0 };
	int ret;

	msg.req.type = VDUSE_GET_VQ_STATE;
	msg.req.vq_state.index = vq->index;

	ret = vduse_dev_msg_sync(dev, &msg);
	if (ret)
		return ret;

	split->avail_index = msg.resp.vq_state.split.avail_index;

	return 0;
}

static int vduse_dev_set_status(struct vduse_dev *dev, u8 status)
{
	struct vduse_dev_msg msg = { 0 };

	msg.req.type = VDUSE_SET_STATUS;
	msg.req.s.status = status;

	return vduse_dev_msg_sync(dev, &msg);
}

static int vduse_dev_update_iotlb(struct vduse_dev *dev,
				  u64 start, u64 last)
{
	struct vduse_dev_msg msg = { 0 };

	if (last < start)
		return -EINVAL;

	msg.req.type = VDUSE_UPDATE_IOTLB;
	msg.req.iova.start = start;
	msg.req.iova.last = last;

	return vduse_dev_msg_sync(dev, &msg);
}

static ssize_t vduse_dev_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *file = iocb->ki_filp;
	struct vduse_dev *dev = file->private_data;
	struct vduse_dev_msg *msg;
	int size = sizeof(struct vduse_dev_request);
	ssize_t ret;

	if (iov_iter_count(to) < size)
		return -EINVAL;

	spin_lock(&dev->msg_lock);
	while (1) {
		msg = vduse_dequeue_msg(&dev->send_list);
		if (msg)
			break;

		ret = -EAGAIN;
		if (file->f_flags & O_NONBLOCK)
			goto unlock;

		spin_unlock(&dev->msg_lock);
		ret = wait_event_interruptible_exclusive(dev->waitq,
					!list_empty(&dev->send_list));
		if (ret)
			return ret;

		spin_lock(&dev->msg_lock);
	}
	spin_unlock(&dev->msg_lock);
	ret = copy_to_iter(&msg->req, size, to);
	spin_lock(&dev->msg_lock);
	if (ret != size) {
		ret = -EFAULT;
		vduse_enqueue_msg(&dev->send_list, msg);
		goto unlock;
	}
	vduse_enqueue_msg(&dev->recv_list, msg);
unlock:
	spin_unlock(&dev->msg_lock);

	return ret;
}

static bool is_mem_zero(const char *ptr, int size)
{
	int i;

	for (i = 0; i < size; i++) {
		if (ptr[i])
			return false;
	}
	return true;
}

static ssize_t vduse_dev_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct vduse_dev *dev = file->private_data;
	struct vduse_dev_response resp;
	struct vduse_dev_msg *msg;
	size_t ret;

	ret = copy_from_iter(&resp, sizeof(resp), from);
	if (ret != sizeof(resp))
		return -EINVAL;

	if (!is_mem_zero((const char *)resp.reserved, sizeof(resp.reserved)))
		return -EINVAL;

	spin_lock(&dev->msg_lock);
	msg = vduse_find_msg(&dev->recv_list, resp.request_id);
	if (!msg) {
		ret = -ENOENT;
		goto unlock;
	}

	memcpy(&msg->resp, &resp, sizeof(resp));
	msg->completed = 1;
	wake_up(&msg->waitq);
unlock:
	spin_unlock(&dev->msg_lock);

	return ret;
}

static __poll_t vduse_dev_poll(struct file *file, poll_table *wait)
{
	struct vduse_dev *dev = file->private_data;
	__poll_t mask = 0;

	poll_wait(file, &dev->waitq, wait);

	spin_lock(&dev->msg_lock);

	if (unlikely(dev->broken))
		mask |= EPOLLERR;
	if (!list_empty(&dev->send_list))
		mask |= EPOLLIN | EPOLLRDNORM;
	if (!list_empty(&dev->recv_list))
		mask |= EPOLLOUT | EPOLLWRNORM;

	spin_unlock(&dev->msg_lock);

	return mask;
}

static void vduse_dev_reset(struct vduse_dev *dev)
{
	int i;
	struct vduse_iova_domain *domain = dev->domain;

	/* The coherent mappings are handled in vduse_dev_free_coherent() */
	if (domain->bounce_map)
		vduse_domain_reset_bounce_map(domain);

	dev->status = 0;
	dev->driver_features = 0;
	dev->generation++;
	spin_lock(&dev->irq_lock);
	dev->config_cb.callback = NULL;
	dev->config_cb.private = NULL;
	spin_unlock(&dev->irq_lock);
	flush_work(&dev->inject);

	for (i = 0; i < dev->vq_num; i++) {
		struct vduse_virtqueue *vq = &dev->vqs[i];

		vq->ready = false;
		vq->desc_addr = 0;
		vq->driver_addr = 0;
		vq->device_addr = 0;
		vq->num = 0;
		memset(&vq->state, 0, sizeof(vq->state));

		spin_lock(&vq->kick_lock);
		vq->kicked = false;
		if (vq->kickfd)
			eventfd_ctx_put(vq->kickfd);
		vq->kickfd = NULL;
		spin_unlock(&vq->kick_lock);

		spin_lock(&vq->irq_lock);
		vq->cb.callback = NULL;
		vq->cb.private = NULL;
		spin_unlock(&vq->irq_lock);
		flush_work(&vq->inject);
		flush_work(&vq->kick);
	}
}

static int vduse_vdpa_set_vq_address(struct vdpa_device *vdpa, u16 idx,
				u64 desc_area, u64 driver_area,
				u64 device_area)
{
	struct vduse_dev *dev = vdpa_to_vduse(vdpa);
	struct vduse_virtqueue *vq = &dev->vqs[idx];

	vq->desc_addr = desc_area;
	vq->driver_addr = driver_area;
	vq->device_addr = device_area;

	return 0;
}

static void vduse_vq_kick(struct vduse_virtqueue *vq)
{
	spin_lock(&vq->kick_lock);
	if (!vq->ready)
		goto unlock;

	if (vq->kickfd)
		eventfd_signal(vq->kickfd, 1);
	else
		vq->kicked = true;
unlock:
	spin_unlock(&vq->kick_lock);
}

static void vduse_vq_kick_work(struct work_struct *work)
{
	struct vduse_virtqueue *vq = container_of(work,
					struct vduse_virtqueue, kick);

	vduse_vq_kick(vq);
}

static void vduse_vdpa_kick_vq(struct vdpa_device *vdpa, u16 idx)
{
	struct vduse_dev *dev = vdpa_to_vduse(vdpa);
	struct vduse_virtqueue *vq = &dev->vqs[idx];

	if (!eventfd_signal_allowed()) {
		schedule_work(&vq->kick);
		return;
	}
	vduse_vq_kick(vq);
}

static void vduse_vdpa_set_vq_cb(struct vdpa_device *vdpa, u16 idx,
			      struct vdpa_callback *cb)
{
	struct vduse_dev *dev = vdpa_to_vduse(vdpa);
	struct vduse_virtqueue *vq = &dev->vqs[idx];

	spin_lock(&vq->irq_lock);
	vq->cb.callback = cb->callback;
	vq->cb.private = cb->private;
	spin_unlock(&vq->irq_lock);
}

static void vduse_vdpa_set_vq_num(struct vdpa_device *vdpa, u16 idx, u32 num)
{
	struct vduse_dev *dev = vdpa_to_vduse(vdpa);
	struct vduse_virtqueue *vq = &dev->vqs[idx];

	vq->num = num;
}

static void vduse_vdpa_set_vq_ready(struct vdpa_device *vdpa,
					u16 idx, bool ready)
{
	struct vduse_dev *dev = vdpa_to_vduse(vdpa);
	struct vduse_virtqueue *vq = &dev->vqs[idx];

	vq->ready = ready;
}

static bool vduse_vdpa_get_vq_ready(struct vdpa_device *vdpa, u16 idx)
{
	struct vduse_dev *dev = vdpa_to_vduse(vdpa);
	struct vduse_virtqueue *vq = &dev->vqs[idx];

	return vq->ready;
}

static int vduse_vdpa_set_vq_state(struct vdpa_device *vdpa, u16 idx,
				const struct vdpa_vq_state *state)
{
	struct vduse_dev *dev = vdpa_to_vduse(vdpa);
	struct vduse_virtqueue *vq = &dev->vqs[idx];

	if (dev->driver_features & BIT_ULL(VIRTIO_F_RING_PACKED)) {
		vq->state.packed.last_avail_counter =
				state->packed.last_avail_counter;
		vq->state.packed.last_avail_idx = state->packed.last_avail_idx;
		vq->state.packed.last_used_counter =
				state->packed.last_used_counter;
		vq->state.packed.last_used_idx = state->packed.last_used_idx;
	} else
		vq->state.split.avail_index = state->split.avail_index;

	return 0;
}

static int vduse_vdpa_get_vq_state(struct vdpa_device *vdpa, u16 idx,
				struct vdpa_vq_state *state)
{
	struct vduse_dev *dev = vdpa_to_vduse(vdpa);
	struct vduse_virtqueue *vq = &dev->vqs[idx];

	if (dev->driver_features & BIT_ULL(VIRTIO_F_RING_PACKED))
		return vduse_dev_get_vq_state_packed(dev, vq, &state->packed);

	return vduse_dev_get_vq_state_split(dev, vq, &state->split);
}

static u32 vduse_vdpa_get_vq_align(struct vdpa_device *vdpa)
{
	struct vduse_dev *dev = vdpa_to_vduse(vdpa);

	return dev->vq_align;
}

static u64 vduse_vdpa_get_features(struct vdpa_device *vdpa)
{
	struct vduse_dev *dev = vdpa_to_vduse(vdpa);

	return dev->device_features;
}

static int vduse_vdpa_set_features(struct vdpa_device *vdpa, u64 features)
{
	struct vduse_dev *dev = vdpa_to_vduse(vdpa);

	dev->driver_features = features;
	return 0;
}

static void vduse_vdpa_set_config_cb(struct vdpa_device *vdpa,
				  struct vdpa_callback *cb)
{
	struct vduse_dev *dev = vdpa_to_vduse(vdpa);

	spin_lock(&dev->irq_lock);
	dev->config_cb.callback = cb->callback;
	dev->config_cb.private = cb->private;
	spin_unlock(&dev->irq_lock);
}

static u16 vduse_vdpa_get_vq_num_max(struct vdpa_device *vdpa)
{
	struct vduse_dev *dev = vdpa_to_vduse(vdpa);
	u16 num_max = 0;
	int i;

	for (i = 0; i < dev->vq_num; i++)
		if (num_max < dev->vqs[i].num_max)
			num_max = dev->vqs[i].num_max;

	return num_max;
}

static u32 vduse_vdpa_get_device_id(struct vdpa_device *vdpa)
{
	struct vduse_dev *dev = vdpa_to_vduse(vdpa);

	return dev->device_id;
}

static u32 vduse_vdpa_get_vendor_id(struct vdpa_device *vdpa)
{
	struct vduse_dev *dev = vdpa_to_vduse(vdpa);

	return dev->vendor_id;
}

static u8 vduse_vdpa_get_status(struct vdpa_device *vdpa)
{
	struct vduse_dev *dev = vdpa_to_vduse(vdpa);

	return dev->status;
}

static void vduse_vdpa_set_status(struct vdpa_device *vdpa, u8 status)
{
	struct vduse_dev *dev = vdpa_to_vduse(vdpa);

	if (vduse_dev_set_status(dev, status))
		return;

	dev->status = status;
}

static size_t vduse_vdpa_get_config_size(struct vdpa_device *vdpa)
{
	struct vduse_dev *dev = vdpa_to_vduse(vdpa);

	return dev->config_size;
}

static void vduse_vdpa_get_config(struct vdpa_device *vdpa, unsigned int offset,
				  void *buf, unsigned int len)
{
	struct vduse_dev *dev = vdpa_to_vduse(vdpa);

	if (len > dev->config_size - offset)
		return;

	memcpy(buf, dev->config + offset, len);
}

static void vduse_vdpa_set_config(struct vdpa_device *vdpa, unsigned int offset,
			const void *buf, unsigned int len)
{
	/* Now we only support read-only configuration space */
}

static int vduse_vdpa_reset(struct vdpa_device *vdpa)
{
	struct vduse_dev *dev = vdpa_to_vduse(vdpa);

	if (vduse_dev_set_status(dev, 0))
		return -EIO;

	vduse_dev_reset(dev);

	return 0;
}

static u32 vduse_vdpa_get_generation(struct vdpa_device *vdpa)
{
	struct vduse_dev *dev = vdpa_to_vduse(vdpa);

	return dev->generation;
}

static int vduse_vdpa_set_map(struct vdpa_device *vdpa,
				struct vhost_iotlb *iotlb)
{
	struct vduse_dev *dev = vdpa_to_vduse(vdpa);
	int ret;

	ret = vduse_domain_set_map(dev->domain, iotlb);
	if (ret)
		return ret;

	ret = vduse_dev_update_iotlb(dev, 0ULL, ULLONG_MAX);
	if (ret) {
		vduse_domain_clear_map(dev->domain, iotlb);
		return ret;
	}

	return 0;
}

static void vduse_vdpa_free(struct vdpa_device *vdpa)
{
	struct vduse_dev *dev = vdpa_to_vduse(vdpa);

	dev->vdev = NULL;
}

static const struct vdpa_config_ops vduse_vdpa_config_ops = {
	.set_vq_address		= vduse_vdpa_set_vq_address,
	.kick_vq		= vduse_vdpa_kick_vq,
	.set_vq_cb		= vduse_vdpa_set_vq_cb,
	.set_vq_num             = vduse_vdpa_set_vq_num,
	.set_vq_ready		= vduse_vdpa_set_vq_ready,
	.get_vq_ready		= vduse_vdpa_get_vq_ready,
	.set_vq_state		= vduse_vdpa_set_vq_state,
	.get_vq_state		= vduse_vdpa_get_vq_state,
	.get_vq_align		= vduse_vdpa_get_vq_align,
	.get_features		= vduse_vdpa_get_features,
	.set_features		= vduse_vdpa_set_features,
	.set_config_cb		= vduse_vdpa_set_config_cb,
	.get_vq_num_max		= vduse_vdpa_get_vq_num_max,
	.get_device_id		= vduse_vdpa_get_device_id,
	.get_vendor_id		= vduse_vdpa_get_vendor_id,
	.get_status		= vduse_vdpa_get_status,
	.set_status		= vduse_vdpa_set_status,
	.get_config_size	= vduse_vdpa_get_config_size,
	.get_config		= vduse_vdpa_get_config,
	.set_config		= vduse_vdpa_set_config,
	.get_generation		= vduse_vdpa_get_generation,
	.reset			= vduse_vdpa_reset,
	.set_map		= vduse_vdpa_set_map,
	.free			= vduse_vdpa_free,
};

static dma_addr_t vduse_dev_map_page(struct device *dev, struct page *page,
				     unsigned long offset, size_t size,
				     enum dma_data_direction dir,
				     unsigned long attrs)
{
	struct vduse_dev *vdev = dev_to_vduse(dev);
	struct vduse_iova_domain *domain = vdev->domain;

	return vduse_domain_map_page(domain, page, offset, size, dir, attrs);
}

static void vduse_dev_unmap_page(struct device *dev, dma_addr_t dma_addr,
				size_t size, enum dma_data_direction dir,
				unsigned long attrs)
{
	struct vduse_dev *vdev = dev_to_vduse(dev);
	struct vduse_iova_domain *domain = vdev->domain;

	return vduse_domain_unmap_page(domain, dma_addr, size, dir, attrs);
}

static void *vduse_dev_alloc_coherent(struct device *dev, size_t size,
					dma_addr_t *dma_addr, gfp_t flag,
					unsigned long attrs)
{
	struct vduse_dev *vdev = dev_to_vduse(dev);
	struct vduse_iova_domain *domain = vdev->domain;
	unsigned long iova;
	void *addr;

	*dma_addr = DMA_MAPPING_ERROR;
	addr = vduse_domain_alloc_coherent(domain, size,
				(dma_addr_t *)&iova, flag, attrs);
	if (!addr)
		return NULL;

	*dma_addr = (dma_addr_t)iova;

	return addr;
}

static void vduse_dev_free_coherent(struct device *dev, size_t size,
					void *vaddr, dma_addr_t dma_addr,
					unsigned long attrs)
{
	struct vduse_dev *vdev = dev_to_vduse(dev);
	struct vduse_iova_domain *domain = vdev->domain;

	vduse_domain_free_coherent(domain, size, vaddr, dma_addr, attrs);
}

static size_t vduse_dev_max_mapping_size(struct device *dev)
{
	struct vduse_dev *vdev = dev_to_vduse(dev);
	struct vduse_iova_domain *domain = vdev->domain;

	return domain->bounce_size;
}

static const struct dma_map_ops vduse_dev_dma_ops = {
	.map_page = vduse_dev_map_page,
	.unmap_page = vduse_dev_unmap_page,
	.alloc = vduse_dev_alloc_coherent,
	.free = vduse_dev_free_coherent,
	.max_mapping_size = vduse_dev_max_mapping_size,
};

static unsigned int perm_to_file_flags(u8 perm)
{
	unsigned int flags = 0;

	switch (perm) {
	case VDUSE_ACCESS_WO:
		flags |= O_WRONLY;
		break;
	case VDUSE_ACCESS_RO:
		flags |= O_RDONLY;
		break;
	case VDUSE_ACCESS_RW:
		flags |= O_RDWR;
		break;
	default:
		WARN(1, "invalidate vhost IOTLB permission\n");
		break;
	}

	return flags;
}

static int vduse_kickfd_setup(struct vduse_dev *dev,
			struct vduse_vq_eventfd *eventfd)
{
	struct eventfd_ctx *ctx = NULL;
	struct vduse_virtqueue *vq;
	u32 index;

	if (eventfd->index >= dev->vq_num)
		return -EINVAL;

	index = array_index_nospec(eventfd->index, dev->vq_num);
	vq = &dev->vqs[index];
	if (eventfd->fd >= 0) {
		ctx = eventfd_ctx_fdget(eventfd->fd);
		if (IS_ERR(ctx))
			return PTR_ERR(ctx);
	} else if (eventfd->fd != VDUSE_EVENTFD_DEASSIGN)
		return 0;

	spin_lock(&vq->kick_lock);
	if (vq->kickfd)
		eventfd_ctx_put(vq->kickfd);
	vq->kickfd = ctx;
	if (vq->ready && vq->kicked && vq->kickfd) {
		eventfd_signal(vq->kickfd, 1);
		vq->kicked = false;
	}
	spin_unlock(&vq->kick_lock);

	return 0;
}

static bool vduse_dev_is_ready(struct vduse_dev *dev)
{
	int i;

	for (i = 0; i < dev->vq_num; i++)
		if (!dev->vqs[i].num_max)
			return false;

	return true;
}

static void vduse_dev_irq_inject(struct work_struct *work)
{
	struct vduse_dev *dev = container_of(work, struct vduse_dev, inject);

	spin_lock_irq(&dev->irq_lock);
	if (dev->config_cb.callback)
		dev->config_cb.callback(dev->config_cb.private);
	spin_unlock_irq(&dev->irq_lock);
}

static void vduse_vq_irq_inject(struct work_struct *work)
{
	struct vduse_virtqueue *vq = container_of(work,
					struct vduse_virtqueue, inject);

	spin_lock_irq(&vq->irq_lock);
	if (vq->ready && vq->cb.callback)
		vq->cb.callback(vq->cb.private);
	spin_unlock_irq(&vq->irq_lock);
}

static long vduse_dev_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	struct vduse_dev *dev = file->private_data;
	void __user *argp = (void __user *)arg;
	int ret;

	if (unlikely(dev->broken))
		return -EPERM;

	switch (cmd) {
	case VDUSE_IOTLB_GET_FD: {
		struct vduse_iotlb_entry entry;
		struct vhost_iotlb_map *map;
		struct vdpa_map_file *map_file;
		struct vduse_iova_domain *domain = dev->domain;
		struct file *f = NULL;

		ret = -EFAULT;
		if (copy_from_user(&entry, argp, sizeof(entry)))
			break;

		ret = -EINVAL;
		if (entry.start > entry.last)
			break;

		spin_lock(&domain->iotlb_lock);
		map = vhost_iotlb_itree_first(domain->iotlb,
					      entry.start, entry.last);
		if (map) {
			map_file = (struct vdpa_map_file *)map->opaque;
			f = get_file(map_file->file);
			entry.offset = map_file->offset;
			entry.start = map->start;
			entry.last = map->last;
			entry.perm = map->perm;
		}
		spin_unlock(&domain->iotlb_lock);
		ret = -EINVAL;
		if (!f)
			break;

		ret = -EFAULT;
		if (copy_to_user(argp, &entry, sizeof(entry))) {
			fput(f);
			break;
		}
		ret = receive_fd(f, perm_to_file_flags(entry.perm));
		fput(f);
		break;
	}
	case VDUSE_DEV_GET_FEATURES:
		/*
		 * Just mirror what driver wrote here.
		 * The driver is expected to check FEATURE_OK later.
		 */
		ret = put_user(dev->driver_features, (u64 __user *)argp);
		break;
	case VDUSE_DEV_SET_CONFIG: {
		struct vduse_config_data config;
		unsigned long size = offsetof(struct vduse_config_data,
					      buffer);

		ret = -EFAULT;
		if (copy_from_user(&config, argp, size))
			break;

		ret = -EINVAL;
		if (config.length == 0 ||
		    config.length > dev->config_size - config.offset)
			break;

		ret = -EFAULT;
		if (copy_from_user(dev->config + config.offset, argp + size,
				   config.length))
			break;

		ret = 0;
		break;
	}
	case VDUSE_DEV_INJECT_CONFIG_IRQ:
		ret = 0;
		queue_work(vduse_irq_wq, &dev->inject);
		break;
	case VDUSE_VQ_SETUP: {
		struct vduse_vq_config config;
		u32 index;

		ret = -EFAULT;
		if (copy_from_user(&config, argp, sizeof(config)))
			break;

		ret = -EINVAL;
		if (config.index >= dev->vq_num)
			break;

		if (!is_mem_zero((const char *)config.reserved,
				 sizeof(config.reserved)))
			break;

		index = array_index_nospec(config.index, dev->vq_num);
		dev->vqs[index].num_max = config.max_size;
		ret = 0;
		break;
	}
	case VDUSE_VQ_GET_INFO: {
		struct vduse_vq_info vq_info;
		struct vduse_virtqueue *vq;
		u32 index;

		ret = -EFAULT;
		if (copy_from_user(&vq_info, argp, sizeof(vq_info)))
			break;

		ret = -EINVAL;
		if (vq_info.index >= dev->vq_num)
			break;

		index = array_index_nospec(vq_info.index, dev->vq_num);
		vq = &dev->vqs[index];
		vq_info.desc_addr = vq->desc_addr;
		vq_info.driver_addr = vq->driver_addr;
		vq_info.device_addr = vq->device_addr;
		vq_info.num = vq->num;

		if (dev->driver_features & BIT_ULL(VIRTIO_F_RING_PACKED)) {
			vq_info.packed.last_avail_counter =
				vq->state.packed.last_avail_counter;
			vq_info.packed.last_avail_idx =
				vq->state.packed.last_avail_idx;
			vq_info.packed.last_used_counter =
				vq->state.packed.last_used_counter;
			vq_info.packed.last_used_idx =
				vq->state.packed.last_used_idx;
		} else
			vq_info.split.avail_index =
				vq->state.split.avail_index;

		vq_info.ready = vq->ready;

		ret = -EFAULT;
		if (copy_to_user(argp, &vq_info, sizeof(vq_info)))
			break;

		ret = 0;
		break;
	}
	case VDUSE_VQ_SETUP_KICKFD: {
		struct vduse_vq_eventfd eventfd;

		ret = -EFAULT;
		if (copy_from_user(&eventfd, argp, sizeof(eventfd)))
			break;

		ret = vduse_kickfd_setup(dev, &eventfd);
		break;
	}
	case VDUSE_VQ_INJECT_IRQ: {
		u32 index;

		ret = -EFAULT;
		if (get_user(index, (u32 __user *)argp))
			break;

		ret = -EINVAL;
		if (index >= dev->vq_num)
			break;

		ret = 0;
		index = array_index_nospec(index, dev->vq_num);
		queue_work(vduse_irq_wq, &dev->vqs[index].inject);
		break;
	}
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

static int vduse_dev_release(struct inode *inode, struct file *file)
{
	struct vduse_dev *dev = file->private_data;

	spin_lock(&dev->msg_lock);
	/* Make sure the inflight messages can processed after reconncection */
	list_splice_init(&dev->recv_list, &dev->send_list);
	spin_unlock(&dev->msg_lock);
	dev->connected = false;

	return 0;
}

static struct vduse_dev *vduse_dev_get_from_minor(int minor)
{
	struct vduse_dev *dev;

	mutex_lock(&vduse_lock);
	dev = idr_find(&vduse_idr, minor);
	mutex_unlock(&vduse_lock);

	return dev;
}

static int vduse_dev_open(struct inode *inode, struct file *file)
{
	int ret;
	struct vduse_dev *dev = vduse_dev_get_from_minor(iminor(inode));

	if (!dev)
		return -ENODEV;

	ret = -EBUSY;
	mutex_lock(&dev->lock);
	if (dev->connected)
		goto unlock;

	ret = 0;
	dev->connected = true;
	file->private_data = dev;
unlock:
	mutex_unlock(&dev->lock);

	return ret;
}

static const struct file_operations vduse_dev_fops = {
	.owner		= THIS_MODULE,
	.open		= vduse_dev_open,
	.release	= vduse_dev_release,
	.read_iter	= vduse_dev_read_iter,
	.write_iter	= vduse_dev_write_iter,
	.poll		= vduse_dev_poll,
	.unlocked_ioctl	= vduse_dev_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.llseek		= noop_llseek,
};

static struct vduse_dev *vduse_dev_create(void)
{
	struct vduse_dev *dev = kzalloc(sizeof(*dev), GFP_KERNEL);

	if (!dev)
		return NULL;

	mutex_init(&dev->lock);
	spin_lock_init(&dev->msg_lock);
	INIT_LIST_HEAD(&dev->send_list);
	INIT_LIST_HEAD(&dev->recv_list);
	spin_lock_init(&dev->irq_lock);

	INIT_WORK(&dev->inject, vduse_dev_irq_inject);
	init_waitqueue_head(&dev->waitq);

	return dev;
}

static void vduse_dev_destroy(struct vduse_dev *dev)
{
	kfree(dev);
}

static struct vduse_dev *vduse_find_dev(const char *name)
{
	struct vduse_dev *dev;
	int id;

	idr_for_each_entry(&vduse_idr, dev, id)
		if (!strcmp(dev->name, name))
			return dev;

	return NULL;
}

static int vduse_destroy_dev(char *name)
{
	struct vduse_dev *dev = vduse_find_dev(name);

	if (!dev)
		return -EINVAL;

	mutex_lock(&dev->lock);
	if (dev->vdev || dev->connected) {
		mutex_unlock(&dev->lock);
		return -EBUSY;
	}
	dev->connected = true;
	mutex_unlock(&dev->lock);

	vduse_dev_reset(dev);
	device_destroy(vduse_class, MKDEV(MAJOR(vduse_major), dev->minor));
	idr_remove(&vduse_idr, dev->minor);
	kvfree(dev->config);
	kfree(dev->vqs);
	vduse_domain_destroy(dev->domain);
	kfree(dev->name);
	vduse_dev_destroy(dev);
	module_put(THIS_MODULE);

	return 0;
}

static bool device_is_allowed(u32 device_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(allowed_device_id); i++)
		if (allowed_device_id[i] == device_id)
			return true;

	return false;
}

static bool features_is_valid(u64 features)
{
	if (!(features & (1ULL << VIRTIO_F_ACCESS_PLATFORM)))
		return false;

	/* Now we only support read-only configuration space */
	if (features & (1ULL << VIRTIO_BLK_F_CONFIG_WCE))
		return false;

	return true;
}

static bool vduse_validate_config(struct vduse_dev_config *config)
{
	if (!is_mem_zero((const char *)config->reserved,
			 sizeof(config->reserved)))
		return false;

	if (config->vq_align > PAGE_SIZE)
		return false;

	if (config->config_size > PAGE_SIZE)
		return false;

	if (!device_is_allowed(config->device_id))
		return false;

	if (!features_is_valid(config->features))
		return false;

	return true;
}

static ssize_t msg_timeout_show(struct device *device,
				struct device_attribute *attr, char *buf)
{
	struct vduse_dev *dev = dev_get_drvdata(device);

	return sysfs_emit(buf, "%u\n", dev->msg_timeout);
}

static ssize_t msg_timeout_store(struct device *device,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct vduse_dev *dev = dev_get_drvdata(device);
	int ret;

	ret = kstrtouint(buf, 10, &dev->msg_timeout);
	if (ret < 0)
		return ret;

	return count;
}

static DEVICE_ATTR_RW(msg_timeout);

static struct attribute *vduse_dev_attrs[] = {
	&dev_attr_msg_timeout.attr,
	NULL
};

ATTRIBUTE_GROUPS(vduse_dev);

static int vduse_create_dev(struct vduse_dev_config *config,
			    void *config_buf, u64 api_version)
{
	int i, ret;
	struct vduse_dev *dev;

	ret = -EEXIST;
	if (vduse_find_dev(config->name))
		goto err;

	ret = -ENOMEM;
	dev = vduse_dev_create();
	if (!dev)
		goto err;

	dev->api_version = api_version;
	dev->device_features = config->features;
	dev->device_id = config->device_id;
	dev->vendor_id = config->vendor_id;
	dev->name = kstrdup(config->name, GFP_KERNEL);
	if (!dev->name)
		goto err_str;

	dev->domain = vduse_domain_create(VDUSE_IOVA_SIZE - 1,
					  VDUSE_BOUNCE_SIZE);
	if (!dev->domain)
		goto err_domain;

	dev->config = config_buf;
	dev->config_size = config->config_size;
	dev->vq_align = config->vq_align;
	dev->vq_num = config->vq_num;
	dev->vqs = kcalloc(dev->vq_num, sizeof(*dev->vqs), GFP_KERNEL);
	if (!dev->vqs)
		goto err_vqs;

	for (i = 0; i < dev->vq_num; i++) {
		dev->vqs[i].index = i;
		INIT_WORK(&dev->vqs[i].inject, vduse_vq_irq_inject);
		INIT_WORK(&dev->vqs[i].kick, vduse_vq_kick_work);
		spin_lock_init(&dev->vqs[i].kick_lock);
		spin_lock_init(&dev->vqs[i].irq_lock);
	}

	ret = idr_alloc(&vduse_idr, dev, 1, VDUSE_DEV_MAX, GFP_KERNEL);
	if (ret < 0)
		goto err_idr;

	dev->minor = ret;
	dev->msg_timeout = VDUSE_MSG_DEFAULT_TIMEOUT;
	dev->dev = device_create(vduse_class, NULL,
				 MKDEV(MAJOR(vduse_major), dev->minor),
				 dev, "%s", config->name);
	if (IS_ERR(dev->dev)) {
		ret = PTR_ERR(dev->dev);
		goto err_dev;
	}
	__module_get(THIS_MODULE);

	return 0;
err_dev:
	idr_remove(&vduse_idr, dev->minor);
err_idr:
	kfree(dev->vqs);
err_vqs:
	vduse_domain_destroy(dev->domain);
err_domain:
	kfree(dev->name);
err_str:
	vduse_dev_destroy(dev);
err:
	kvfree(config_buf);
	return ret;
}

static long vduse_ioctl(struct file *file, unsigned int cmd,
			unsigned long arg)
{
	int ret;
	void __user *argp = (void __user *)arg;
	struct vduse_control *control = file->private_data;

	mutex_lock(&vduse_lock);
	switch (cmd) {
	case VDUSE_GET_API_VERSION:
		ret = put_user(control->api_version, (u64 __user *)argp);
		break;
	case VDUSE_SET_API_VERSION: {
		u64 api_version;

		ret = -EFAULT;
		if (get_user(api_version, (u64 __user *)argp))
			break;

		ret = -EINVAL;
		if (api_version > VDUSE_API_VERSION)
			break;

		ret = 0;
		control->api_version = api_version;
		break;
	}
	case VDUSE_CREATE_DEV: {
		struct vduse_dev_config config;
		unsigned long size = offsetof(struct vduse_dev_config, config);
		void *buf;

		ret = -EFAULT;
		if (copy_from_user(&config, argp, size))
			break;

		ret = -EINVAL;
		if (vduse_validate_config(&config) == false)
			break;

		buf = vmemdup_user(argp + size, config.config_size);
		if (IS_ERR(buf)) {
			ret = PTR_ERR(buf);
			break;
		}
		config.name[VDUSE_NAME_MAX - 1] = '\0';
		ret = vduse_create_dev(&config, buf, control->api_version);
		break;
	}
	case VDUSE_DESTROY_DEV: {
		char name[VDUSE_NAME_MAX];

		ret = -EFAULT;
		if (copy_from_user(name, argp, VDUSE_NAME_MAX))
			break;

		name[VDUSE_NAME_MAX - 1] = '\0';
		ret = vduse_destroy_dev(name);
		break;
	}
	default:
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&vduse_lock);

	return ret;
}

static int vduse_release(struct inode *inode, struct file *file)
{
	struct vduse_control *control = file->private_data;

	kfree(control);
	return 0;
}

static int vduse_open(struct inode *inode, struct file *file)
{
	struct vduse_control *control;

	control = kmalloc(sizeof(struct vduse_control), GFP_KERNEL);
	if (!control)
		return -ENOMEM;

	control->api_version = VDUSE_API_VERSION;
	file->private_data = control;

	return 0;
}

static const struct file_operations vduse_ctrl_fops = {
	.owner		= THIS_MODULE,
	.open		= vduse_open,
	.release	= vduse_release,
	.unlocked_ioctl	= vduse_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.llseek		= noop_llseek,
};

static char *vduse_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "vduse/%s", dev_name(dev));
}

static void vduse_mgmtdev_release(struct device *dev)
{
}

static struct device vduse_mgmtdev = {
	.init_name = "vduse",
	.release = vduse_mgmtdev_release,
};

static struct vdpa_mgmt_dev mgmt_dev;

static int vduse_dev_init_vdpa(struct vduse_dev *dev, const char *name)
{
	struct vduse_vdpa *vdev;
	int ret;

	if (dev->vdev)
		return -EEXIST;

	vdev = vdpa_alloc_device(struct vduse_vdpa, vdpa, dev->dev,
				 &vduse_vdpa_config_ops, name, true);
	if (IS_ERR(vdev))
		return PTR_ERR(vdev);

	dev->vdev = vdev;
	vdev->dev = dev;
	vdev->vdpa.dev.dma_mask = &vdev->vdpa.dev.coherent_dma_mask;
	ret = dma_set_mask_and_coherent(&vdev->vdpa.dev, DMA_BIT_MASK(64));
	if (ret) {
		put_device(&vdev->vdpa.dev);
		return ret;
	}
	set_dma_ops(&vdev->vdpa.dev, &vduse_dev_dma_ops);
	vdev->vdpa.dma_dev = &vdev->vdpa.dev;
	vdev->vdpa.mdev = &mgmt_dev;

	return 0;
}

static int vdpa_dev_add(struct vdpa_mgmt_dev *mdev, const char *name)
{
	struct vduse_dev *dev;
	int ret;

	mutex_lock(&vduse_lock);
	dev = vduse_find_dev(name);
	if (!dev || !vduse_dev_is_ready(dev)) {
		mutex_unlock(&vduse_lock);
		return -EINVAL;
	}
	ret = vduse_dev_init_vdpa(dev, name);
	mutex_unlock(&vduse_lock);
	if (ret)
		return ret;

	ret = _vdpa_register_device(&dev->vdev->vdpa, dev->vq_num);
	if (ret) {
		put_device(&dev->vdev->vdpa.dev);
		return ret;
	}

	return 0;
}

static void vdpa_dev_del(struct vdpa_mgmt_dev *mdev, struct vdpa_device *dev)
{
	_vdpa_unregister_device(dev);
}

static const struct vdpa_mgmtdev_ops vdpa_dev_mgmtdev_ops = {
	.dev_add = vdpa_dev_add,
	.dev_del = vdpa_dev_del,
};

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_BLOCK, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static struct vdpa_mgmt_dev mgmt_dev = {
	.device = &vduse_mgmtdev,
	.id_table = id_table,
	.ops = &vdpa_dev_mgmtdev_ops,
};

static int vduse_mgmtdev_init(void)
{
	int ret;

	ret = device_register(&vduse_mgmtdev);
	if (ret)
		return ret;

	ret = vdpa_mgmtdev_register(&mgmt_dev);
	if (ret)
		goto err;

	return 0;
err:
	device_unregister(&vduse_mgmtdev);
	return ret;
}

static void vduse_mgmtdev_exit(void)
{
	vdpa_mgmtdev_unregister(&mgmt_dev);
	device_unregister(&vduse_mgmtdev);
}

static int vduse_init(void)
{
	int ret;
	struct device *dev;

	vduse_class = class_create(THIS_MODULE, "vduse");
	if (IS_ERR(vduse_class))
		return PTR_ERR(vduse_class);

	vduse_class->devnode = vduse_devnode;
	vduse_class->dev_groups = vduse_dev_groups;

	ret = alloc_chrdev_region(&vduse_major, 0, VDUSE_DEV_MAX, "vduse");
	if (ret)
		goto err_chardev_region;

	/* /dev/vduse/control */
	cdev_init(&vduse_ctrl_cdev, &vduse_ctrl_fops);
	vduse_ctrl_cdev.owner = THIS_MODULE;
	ret = cdev_add(&vduse_ctrl_cdev, vduse_major, 1);
	if (ret)
		goto err_ctrl_cdev;

	dev = device_create(vduse_class, NULL, vduse_major, NULL, "control");
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto err_device;
	}

	/* /dev/vduse/$DEVICE */
	cdev_init(&vduse_cdev, &vduse_dev_fops);
	vduse_cdev.owner = THIS_MODULE;
	ret = cdev_add(&vduse_cdev, MKDEV(MAJOR(vduse_major), 1),
		       VDUSE_DEV_MAX - 1);
	if (ret)
		goto err_cdev;

	vduse_irq_wq = alloc_workqueue("vduse-irq",
				WQ_HIGHPRI | WQ_SYSFS | WQ_UNBOUND, 0);
	if (!vduse_irq_wq) {
		ret = -ENOMEM;
		goto err_wq;
	}

	ret = vduse_domain_init();
	if (ret)
		goto err_domain;

	ret = vduse_mgmtdev_init();
	if (ret)
		goto err_mgmtdev;

	return 0;
err_mgmtdev:
	vduse_domain_exit();
err_domain:
	destroy_workqueue(vduse_irq_wq);
err_wq:
	cdev_del(&vduse_cdev);
err_cdev:
	device_destroy(vduse_class, vduse_major);
err_device:
	cdev_del(&vduse_ctrl_cdev);
err_ctrl_cdev:
	unregister_chrdev_region(vduse_major, VDUSE_DEV_MAX);
err_chardev_region:
	class_destroy(vduse_class);
	return ret;
}
module_init(vduse_init);

static void vduse_exit(void)
{
	vduse_mgmtdev_exit();
	vduse_domain_exit();
	destroy_workqueue(vduse_irq_wq);
	cdev_del(&vduse_cdev);
	device_destroy(vduse_class, vduse_major);
	cdev_del(&vduse_ctrl_cdev);
	unregister_chrdev_region(vduse_major, VDUSE_DEV_MAX);
	class_destroy(vduse_class);
}
module_exit(vduse_exit);

MODULE_LICENSE(DRV_LICENSE);
MODULE_AUTHOR(DRV_AUTHOR);
MODULE_DESCRIPTION(DRV_DESC);
