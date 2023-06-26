// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2009 Red Hat, Inc.
 * Copyright (C) 2006 Rusty Russell IBM Corporation
 *
 * Author: Michael S. Tsirkin <mst@redhat.com>
 *
 * Inspiration, some code, and most witty comments come from
 * Documentation/virtual/lguest/lguest.c, by Rusty Russell
 *
 * Generic code for virtio server in host kernel.
 */

#include <linux/eventfd.h>
#include <linux/vhost.h>
#include <linux/uio.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/file.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/sort.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include <linux/sched/vhost_task.h>
#include <linux/interval_tree_generic.h>
#include <linux/nospec.h>
#include <linux/kcov.h>

#include "vhost.h"

static ushort max_mem_regions = 64;
module_param(max_mem_regions, ushort, 0444);
MODULE_PARM_DESC(max_mem_regions,
	"Maximum number of memory regions in memory map. (default: 64)");
static int max_iotlb_entries = 2048;
module_param(max_iotlb_entries, int, 0444);
MODULE_PARM_DESC(max_iotlb_entries,
	"Maximum number of iotlb entries. (default: 2048)");

enum {
	VHOST_MEMORY_F_LOG = 0x1,
};

#define vhost_used_event(vq) ((__virtio16 __user *)&vq->avail->ring[vq->num])
#define vhost_avail_event(vq) ((__virtio16 __user *)&vq->used->ring[vq->num])

#ifdef CONFIG_VHOST_CROSS_ENDIAN_LEGACY
static void vhost_disable_cross_endian(struct vhost_virtqueue *vq)
{
	vq->user_be = !virtio_legacy_is_little_endian();
}

static void vhost_enable_cross_endian_big(struct vhost_virtqueue *vq)
{
	vq->user_be = true;
}

static void vhost_enable_cross_endian_little(struct vhost_virtqueue *vq)
{
	vq->user_be = false;
}

static long vhost_set_vring_endian(struct vhost_virtqueue *vq, int __user *argp)
{
	struct vhost_vring_state s;

	if (vq->private_data)
		return -EBUSY;

	if (copy_from_user(&s, argp, sizeof(s)))
		return -EFAULT;

	if (s.num != VHOST_VRING_LITTLE_ENDIAN &&
	    s.num != VHOST_VRING_BIG_ENDIAN)
		return -EINVAL;

	if (s.num == VHOST_VRING_BIG_ENDIAN)
		vhost_enable_cross_endian_big(vq);
	else
		vhost_enable_cross_endian_little(vq);

	return 0;
}

static long vhost_get_vring_endian(struct vhost_virtqueue *vq, u32 idx,
				   int __user *argp)
{
	struct vhost_vring_state s = {
		.index = idx,
		.num = vq->user_be
	};

	if (copy_to_user(argp, &s, sizeof(s)))
		return -EFAULT;

	return 0;
}

static void vhost_init_is_le(struct vhost_virtqueue *vq)
{
	/* Note for legacy virtio: user_be is initialized at reset time
	 * according to the host endianness. If userspace does not set an
	 * explicit endianness, the default behavior is native endian, as
	 * expected by legacy virtio.
	 */
	vq->is_le = vhost_has_feature(vq, VIRTIO_F_VERSION_1) || !vq->user_be;
}
#else
static void vhost_disable_cross_endian(struct vhost_virtqueue *vq)
{
}

static long vhost_set_vring_endian(struct vhost_virtqueue *vq, int __user *argp)
{
	return -ENOIOCTLCMD;
}

static long vhost_get_vring_endian(struct vhost_virtqueue *vq, u32 idx,
				   int __user *argp)
{
	return -ENOIOCTLCMD;
}

static void vhost_init_is_le(struct vhost_virtqueue *vq)
{
	vq->is_le = vhost_has_feature(vq, VIRTIO_F_VERSION_1)
		|| virtio_legacy_is_little_endian();
}
#endif /* CONFIG_VHOST_CROSS_ENDIAN_LEGACY */

static void vhost_reset_is_le(struct vhost_virtqueue *vq)
{
	vhost_init_is_le(vq);
}

struct vhost_flush_struct {
	struct vhost_work work;
	struct completion wait_event;
};

static void vhost_flush_work(struct vhost_work *work)
{
	struct vhost_flush_struct *s;

	s = container_of(work, struct vhost_flush_struct, work);
	complete(&s->wait_event);
}

static void vhost_poll_func(struct file *file, wait_queue_head_t *wqh,
			    poll_table *pt)
{
	struct vhost_poll *poll;

	poll = container_of(pt, struct vhost_poll, table);
	poll->wqh = wqh;
	add_wait_queue(wqh, &poll->wait);
}

static int vhost_poll_wakeup(wait_queue_entry_t *wait, unsigned mode, int sync,
			     void *key)
{
	struct vhost_poll *poll = container_of(wait, struct vhost_poll, wait);
	struct vhost_work *work = &poll->work;

	if (!(key_to_poll(key) & poll->mask))
		return 0;

	if (!poll->dev->use_worker)
		work->fn(work);
	else
		vhost_poll_queue(poll);

	return 0;
}

void vhost_work_init(struct vhost_work *work, vhost_work_fn_t fn)
{
	clear_bit(VHOST_WORK_QUEUED, &work->flags);
	work->fn = fn;
}
EXPORT_SYMBOL_GPL(vhost_work_init);

/* Init poll structure */
void vhost_poll_init(struct vhost_poll *poll, vhost_work_fn_t fn,
		     __poll_t mask, struct vhost_dev *dev)
{
	init_waitqueue_func_entry(&poll->wait, vhost_poll_wakeup);
	init_poll_funcptr(&poll->table, vhost_poll_func);
	poll->mask = mask;
	poll->dev = dev;
	poll->wqh = NULL;

	vhost_work_init(&poll->work, fn);
}
EXPORT_SYMBOL_GPL(vhost_poll_init);

/* Start polling a file. We add ourselves to file's wait queue. The caller must
 * keep a reference to a file until after vhost_poll_stop is called. */
int vhost_poll_start(struct vhost_poll *poll, struct file *file)
{
	__poll_t mask;

	if (poll->wqh)
		return 0;

	mask = vfs_poll(file, &poll->table);
	if (mask)
		vhost_poll_wakeup(&poll->wait, 0, 0, poll_to_key(mask));
	if (mask & EPOLLERR) {
		vhost_poll_stop(poll);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(vhost_poll_start);

/* Stop polling a file. After this function returns, it becomes safe to drop the
 * file reference. You must also flush afterwards. */
void vhost_poll_stop(struct vhost_poll *poll)
{
	if (poll->wqh) {
		remove_wait_queue(poll->wqh, &poll->wait);
		poll->wqh = NULL;
	}
}
EXPORT_SYMBOL_GPL(vhost_poll_stop);

void vhost_dev_flush(struct vhost_dev *dev)
{
	struct vhost_flush_struct flush;

	init_completion(&flush.wait_event);
	vhost_work_init(&flush.work, vhost_flush_work);

	if (vhost_work_queue(dev, &flush.work))
		wait_for_completion(&flush.wait_event);
}
EXPORT_SYMBOL_GPL(vhost_dev_flush);

bool vhost_work_queue(struct vhost_dev *dev, struct vhost_work *work)
{
	if (!dev->worker)
		return false;
	/*
	 * vsock can queue while we do a VHOST_SET_OWNER, so we have a smp_wmb
	 * when setting up the worker. We don't have a smp_rmb here because
	 * test_and_set_bit gives us a mb already.
	 */
	if (!test_and_set_bit(VHOST_WORK_QUEUED, &work->flags)) {
		/* We can only add the work to the list after we're
		 * sure it was not in the list.
		 * test_and_set_bit() implies a memory barrier.
		 */
		llist_add(&work->node, &dev->worker->work_list);
		vhost_task_wake(dev->worker->vtsk);
	}

	return true;
}
EXPORT_SYMBOL_GPL(vhost_work_queue);

/* A lockless hint for busy polling code to exit the loop */
bool vhost_has_work(struct vhost_dev *dev)
{
	return !llist_empty(&dev->worker->work_list);
}
EXPORT_SYMBOL_GPL(vhost_has_work);

void vhost_poll_queue(struct vhost_poll *poll)
{
	vhost_work_queue(poll->dev, &poll->work);
}
EXPORT_SYMBOL_GPL(vhost_poll_queue);

static void __vhost_vq_meta_reset(struct vhost_virtqueue *vq)
{
	int j;

	for (j = 0; j < VHOST_NUM_ADDRS; j++)
		vq->meta_iotlb[j] = NULL;
}

static void vhost_vq_meta_reset(struct vhost_dev *d)
{
	int i;

	for (i = 0; i < d->nvqs; ++i)
		__vhost_vq_meta_reset(d->vqs[i]);
}

static void vhost_vring_call_reset(struct vhost_vring_call *call_ctx)
{
	call_ctx->ctx = NULL;
	memset(&call_ctx->producer, 0x0, sizeof(struct irq_bypass_producer));
}

bool vhost_vq_is_setup(struct vhost_virtqueue *vq)
{
	return vq->avail && vq->desc && vq->used && vhost_vq_access_ok(vq);
}
EXPORT_SYMBOL_GPL(vhost_vq_is_setup);

static void vhost_vq_reset(struct vhost_dev *dev,
			   struct vhost_virtqueue *vq)
{
	vq->num = 1;
	vq->desc = NULL;
	vq->avail = NULL;
	vq->used = NULL;
	vq->last_avail_idx = 0;
	vq->avail_idx = 0;
	vq->last_used_idx = 0;
	vq->signalled_used = 0;
	vq->signalled_used_valid = false;
	vq->used_flags = 0;
	vq->log_used = false;
	vq->log_addr = -1ull;
	vq->private_data = NULL;
	vq->acked_features = 0;
	vq->acked_backend_features = 0;
	vq->log_base = NULL;
	vq->error_ctx = NULL;
	vq->kick = NULL;
	vq->log_ctx = NULL;
	vhost_disable_cross_endian(vq);
	vhost_reset_is_le(vq);
	vq->busyloop_timeout = 0;
	vq->umem = NULL;
	vq->iotlb = NULL;
	vq->worker = NULL;
	vhost_vring_call_reset(&vq->call_ctx);
	__vhost_vq_meta_reset(vq);
}

static bool vhost_worker(void *data)
{
	struct vhost_worker *worker = data;
	struct vhost_work *work, *work_next;
	struct llist_node *node;

	node = llist_del_all(&worker->work_list);
	if (node) {
		__set_current_state(TASK_RUNNING);

		node = llist_reverse_order(node);
		/* make sure flag is seen after deletion */
		smp_wmb();
		llist_for_each_entry_safe(work, work_next, node, node) {
			clear_bit(VHOST_WORK_QUEUED, &work->flags);
			kcov_remote_start_common(worker->kcov_handle);
			work->fn(work);
			kcov_remote_stop();
			cond_resched();
		}
	}

	return !!node;
}

static void vhost_vq_free_iovecs(struct vhost_virtqueue *vq)
{
	kfree(vq->indirect);
	vq->indirect = NULL;
	kfree(vq->log);
	vq->log = NULL;
	kfree(vq->heads);
	vq->heads = NULL;
}

/* Helper to allocate iovec buffers for all vqs. */
static long vhost_dev_alloc_iovecs(struct vhost_dev *dev)
{
	struct vhost_virtqueue *vq;
	int i;

	for (i = 0; i < dev->nvqs; ++i) {
		vq = dev->vqs[i];
		vq->indirect = kmalloc_array(UIO_MAXIOV,
					     sizeof(*vq->indirect),
					     GFP_KERNEL);
		vq->log = kmalloc_array(dev->iov_limit, sizeof(*vq->log),
					GFP_KERNEL);
		vq->heads = kmalloc_array(dev->iov_limit, sizeof(*vq->heads),
					  GFP_KERNEL);
		if (!vq->indirect || !vq->log || !vq->heads)
			goto err_nomem;
	}
	return 0;

err_nomem:
	for (; i >= 0; --i)
		vhost_vq_free_iovecs(dev->vqs[i]);
	return -ENOMEM;
}

static void vhost_dev_free_iovecs(struct vhost_dev *dev)
{
	int i;

	for (i = 0; i < dev->nvqs; ++i)
		vhost_vq_free_iovecs(dev->vqs[i]);
}

bool vhost_exceeds_weight(struct vhost_virtqueue *vq,
			  int pkts, int total_len)
{
	struct vhost_dev *dev = vq->dev;

	if ((dev->byte_weight && total_len >= dev->byte_weight) ||
	    pkts >= dev->weight) {
		vhost_poll_queue(&vq->poll);
		return true;
	}

	return false;
}
EXPORT_SYMBOL_GPL(vhost_exceeds_weight);

static size_t vhost_get_avail_size(struct vhost_virtqueue *vq,
				   unsigned int num)
{
	size_t event __maybe_unused =
	       vhost_has_feature(vq, VIRTIO_RING_F_EVENT_IDX) ? 2 : 0;

	return size_add(struct_size(vq->avail, ring, num), event);
}

static size_t vhost_get_used_size(struct vhost_virtqueue *vq,
				  unsigned int num)
{
	size_t event __maybe_unused =
	       vhost_has_feature(vq, VIRTIO_RING_F_EVENT_IDX) ? 2 : 0;

	return size_add(struct_size(vq->used, ring, num), event);
}

static size_t vhost_get_desc_size(struct vhost_virtqueue *vq,
				  unsigned int num)
{
	return sizeof(*vq->desc) * num;
}

void vhost_dev_init(struct vhost_dev *dev,
		    struct vhost_virtqueue **vqs, int nvqs,
		    int iov_limit, int weight, int byte_weight,
		    bool use_worker,
		    int (*msg_handler)(struct vhost_dev *dev, u32 asid,
				       struct vhost_iotlb_msg *msg))
{
	struct vhost_virtqueue *vq;
	int i;

	dev->vqs = vqs;
	dev->nvqs = nvqs;
	mutex_init(&dev->mutex);
	dev->log_ctx = NULL;
	dev->umem = NULL;
	dev->iotlb = NULL;
	dev->mm = NULL;
	dev->worker = NULL;
	dev->iov_limit = iov_limit;
	dev->weight = weight;
	dev->byte_weight = byte_weight;
	dev->use_worker = use_worker;
	dev->msg_handler = msg_handler;
	init_waitqueue_head(&dev->wait);
	INIT_LIST_HEAD(&dev->read_list);
	INIT_LIST_HEAD(&dev->pending_list);
	spin_lock_init(&dev->iotlb_lock);


	for (i = 0; i < dev->nvqs; ++i) {
		vq = dev->vqs[i];
		vq->log = NULL;
		vq->indirect = NULL;
		vq->heads = NULL;
		vq->dev = dev;
		mutex_init(&vq->mutex);
		vhost_vq_reset(dev, vq);
		if (vq->handle_kick)
			vhost_poll_init(&vq->poll, vq->handle_kick,
					EPOLLIN, dev);
	}
}
EXPORT_SYMBOL_GPL(vhost_dev_init);

/* Caller should have device mutex */
long vhost_dev_check_owner(struct vhost_dev *dev)
{
	/* Are you the owner? If not, I don't think you mean to do that */
	return dev->mm == current->mm ? 0 : -EPERM;
}
EXPORT_SYMBOL_GPL(vhost_dev_check_owner);

/* Caller should have device mutex */
bool vhost_dev_has_owner(struct vhost_dev *dev)
{
	return dev->mm;
}
EXPORT_SYMBOL_GPL(vhost_dev_has_owner);

static void vhost_attach_mm(struct vhost_dev *dev)
{
	/* No owner, become one */
	if (dev->use_worker) {
		dev->mm = get_task_mm(current);
	} else {
		/* vDPA device does not use worker thead, so there's
		 * no need to hold the address space for mm. This help
		 * to avoid deadlock in the case of mmap() which may
		 * held the refcnt of the file and depends on release
		 * method to remove vma.
		 */
		dev->mm = current->mm;
		mmgrab(dev->mm);
	}
}

static void vhost_detach_mm(struct vhost_dev *dev)
{
	if (!dev->mm)
		return;

	if (dev->use_worker)
		mmput(dev->mm);
	else
		mmdrop(dev->mm);

	dev->mm = NULL;
}

static void vhost_worker_free(struct vhost_dev *dev)
{
	if (!dev->worker)
		return;

	WARN_ON(!llist_empty(&dev->worker->work_list));
	vhost_task_stop(dev->worker->vtsk);
	kfree(dev->worker);
	dev->worker = NULL;
}

static struct vhost_worker *vhost_worker_create(struct vhost_dev *dev)
{
	struct vhost_worker *worker;
	struct vhost_task *vtsk;
	char name[TASK_COMM_LEN];

	worker = kzalloc(sizeof(*worker), GFP_KERNEL_ACCOUNT);
	if (!worker)
		return NULL;

	snprintf(name, sizeof(name), "vhost-%d", current->pid);

	vtsk = vhost_task_create(vhost_worker, worker, name);
	if (!vtsk)
		goto free_worker;

	init_llist_head(&worker->work_list);
	worker->kcov_handle = kcov_common_handle();
	worker->vtsk = vtsk;
	/*
	 * vsock can already try to queue so make sure llist and vtsk are both
	 * set before vhost_work_queue sees dev->worker is set.
	 */
	smp_wmb();
	dev->worker = worker;

	vhost_task_start(vtsk);
	return worker;

free_worker:
	kfree(worker);
	return NULL;
}

/* Caller should have device mutex */
long vhost_dev_set_owner(struct vhost_dev *dev)
{
	struct vhost_worker *worker;
	int err, i;

	/* Is there an owner already? */
	if (vhost_dev_has_owner(dev)) {
		err = -EBUSY;
		goto err_mm;
	}

	vhost_attach_mm(dev);

	err = vhost_dev_alloc_iovecs(dev);
	if (err)
		goto err_iovecs;

	if (dev->use_worker) {
		/*
		 * This should be done last, because vsock can queue work
		 * before VHOST_SET_OWNER so it simplifies the failure path
		 * below since we don't have to worry about vsock queueing
		 * while we free the worker.
		 */
		worker = vhost_worker_create(dev);
		if (!worker) {
			err = -ENOMEM;
			goto err_worker;
		}

		for (i = 0; i < dev->nvqs; i++)
			dev->vqs[i]->worker = worker;
	}

	return 0;

err_worker:
	vhost_dev_free_iovecs(dev);
err_iovecs:
	vhost_detach_mm(dev);
err_mm:
	return err;
}
EXPORT_SYMBOL_GPL(vhost_dev_set_owner);

static struct vhost_iotlb *iotlb_alloc(void)
{
	return vhost_iotlb_alloc(max_iotlb_entries,
				 VHOST_IOTLB_FLAG_RETIRE);
}

struct vhost_iotlb *vhost_dev_reset_owner_prepare(void)
{
	return iotlb_alloc();
}
EXPORT_SYMBOL_GPL(vhost_dev_reset_owner_prepare);

/* Caller should have device mutex */
void vhost_dev_reset_owner(struct vhost_dev *dev, struct vhost_iotlb *umem)
{
	int i;

	vhost_dev_cleanup(dev);

	dev->umem = umem;
	/* We don't need VQ locks below since vhost_dev_cleanup makes sure
	 * VQs aren't running.
	 */
	for (i = 0; i < dev->nvqs; ++i)
		dev->vqs[i]->umem = umem;
}
EXPORT_SYMBOL_GPL(vhost_dev_reset_owner);

void vhost_dev_stop(struct vhost_dev *dev)
{
	int i;

	for (i = 0; i < dev->nvqs; ++i) {
		if (dev->vqs[i]->kick && dev->vqs[i]->handle_kick)
			vhost_poll_stop(&dev->vqs[i]->poll);
	}

	vhost_dev_flush(dev);
}
EXPORT_SYMBOL_GPL(vhost_dev_stop);

void vhost_clear_msg(struct vhost_dev *dev)
{
	struct vhost_msg_node *node, *n;

	spin_lock(&dev->iotlb_lock);

	list_for_each_entry_safe(node, n, &dev->read_list, node) {
		list_del(&node->node);
		kfree(node);
	}

	list_for_each_entry_safe(node, n, &dev->pending_list, node) {
		list_del(&node->node);
		kfree(node);
	}

	spin_unlock(&dev->iotlb_lock);
}
EXPORT_SYMBOL_GPL(vhost_clear_msg);

void vhost_dev_cleanup(struct vhost_dev *dev)
{
	int i;

	for (i = 0; i < dev->nvqs; ++i) {
		if (dev->vqs[i]->error_ctx)
			eventfd_ctx_put(dev->vqs[i]->error_ctx);
		if (dev->vqs[i]->kick)
			fput(dev->vqs[i]->kick);
		if (dev->vqs[i]->call_ctx.ctx)
			eventfd_ctx_put(dev->vqs[i]->call_ctx.ctx);
		vhost_vq_reset(dev, dev->vqs[i]);
	}
	vhost_dev_free_iovecs(dev);
	if (dev->log_ctx)
		eventfd_ctx_put(dev->log_ctx);
	dev->log_ctx = NULL;
	/* No one will access memory at this point */
	vhost_iotlb_free(dev->umem);
	dev->umem = NULL;
	vhost_iotlb_free(dev->iotlb);
	dev->iotlb = NULL;
	vhost_clear_msg(dev);
	wake_up_interruptible_poll(&dev->wait, EPOLLIN | EPOLLRDNORM);
	vhost_worker_free(dev);
	vhost_detach_mm(dev);
}
EXPORT_SYMBOL_GPL(vhost_dev_cleanup);

static bool log_access_ok(void __user *log_base, u64 addr, unsigned long sz)
{
	u64 a = addr / VHOST_PAGE_SIZE / 8;

	/* Make sure 64 bit math will not overflow. */
	if (a > ULONG_MAX - (unsigned long)log_base ||
	    a + (unsigned long)log_base > ULONG_MAX)
		return false;

	return access_ok(log_base + a,
			 (sz + VHOST_PAGE_SIZE * 8 - 1) / VHOST_PAGE_SIZE / 8);
}

/* Make sure 64 bit math will not overflow. */
static bool vhost_overflow(u64 uaddr, u64 size)
{
	if (uaddr > ULONG_MAX || size > ULONG_MAX)
		return true;

	if (!size)
		return false;

	return uaddr > ULONG_MAX - size + 1;
}

/* Caller should have vq mutex and device mutex. */
static bool vq_memory_access_ok(void __user *log_base, struct vhost_iotlb *umem,
				int log_all)
{
	struct vhost_iotlb_map *map;

	if (!umem)
		return false;

	list_for_each_entry(map, &umem->list, link) {
		unsigned long a = map->addr;

		if (vhost_overflow(map->addr, map->size))
			return false;


		if (!access_ok((void __user *)a, map->size))
			return false;
		else if (log_all && !log_access_ok(log_base,
						   map->start,
						   map->size))
			return false;
	}
	return true;
}

static inline void __user *vhost_vq_meta_fetch(struct vhost_virtqueue *vq,
					       u64 addr, unsigned int size,
					       int type)
{
	const struct vhost_iotlb_map *map = vq->meta_iotlb[type];

	if (!map)
		return NULL;

	return (void __user *)(uintptr_t)(map->addr + addr - map->start);
}

/* Can we switch to this memory table? */
/* Caller should have device mutex but not vq mutex */
static bool memory_access_ok(struct vhost_dev *d, struct vhost_iotlb *umem,
			     int log_all)
{
	int i;

	for (i = 0; i < d->nvqs; ++i) {
		bool ok;
		bool log;

		mutex_lock(&d->vqs[i]->mutex);
		log = log_all || vhost_has_feature(d->vqs[i], VHOST_F_LOG_ALL);
		/* If ring is inactive, will check when it's enabled. */
		if (d->vqs[i]->private_data)
			ok = vq_memory_access_ok(d->vqs[i]->log_base,
						 umem, log);
		else
			ok = true;
		mutex_unlock(&d->vqs[i]->mutex);
		if (!ok)
			return false;
	}
	return true;
}

static int translate_desc(struct vhost_virtqueue *vq, u64 addr, u32 len,
			  struct iovec iov[], int iov_size, int access);

static int vhost_copy_to_user(struct vhost_virtqueue *vq, void __user *to,
			      const void *from, unsigned size)
{
	int ret;

	if (!vq->iotlb)
		return __copy_to_user(to, from, size);
	else {
		/* This function should be called after iotlb
		 * prefetch, which means we're sure that all vq
		 * could be access through iotlb. So -EAGAIN should
		 * not happen in this case.
		 */
		struct iov_iter t;
		void __user *uaddr = vhost_vq_meta_fetch(vq,
				     (u64)(uintptr_t)to, size,
				     VHOST_ADDR_USED);

		if (uaddr)
			return __copy_to_user(uaddr, from, size);

		ret = translate_desc(vq, (u64)(uintptr_t)to, size, vq->iotlb_iov,
				     ARRAY_SIZE(vq->iotlb_iov),
				     VHOST_ACCESS_WO);
		if (ret < 0)
			goto out;
		iov_iter_init(&t, ITER_DEST, vq->iotlb_iov, ret, size);
		ret = copy_to_iter(from, size, &t);
		if (ret == size)
			ret = 0;
	}
out:
	return ret;
}

static int vhost_copy_from_user(struct vhost_virtqueue *vq, void *to,
				void __user *from, unsigned size)
{
	int ret;

	if (!vq->iotlb)
		return __copy_from_user(to, from, size);
	else {
		/* This function should be called after iotlb
		 * prefetch, which means we're sure that vq
		 * could be access through iotlb. So -EAGAIN should
		 * not happen in this case.
		 */
		void __user *uaddr = vhost_vq_meta_fetch(vq,
				     (u64)(uintptr_t)from, size,
				     VHOST_ADDR_DESC);
		struct iov_iter f;

		if (uaddr)
			return __copy_from_user(to, uaddr, size);

		ret = translate_desc(vq, (u64)(uintptr_t)from, size, vq->iotlb_iov,
				     ARRAY_SIZE(vq->iotlb_iov),
				     VHOST_ACCESS_RO);
		if (ret < 0) {
			vq_err(vq, "IOTLB translation failure: uaddr "
			       "%p size 0x%llx\n", from,
			       (unsigned long long) size);
			goto out;
		}
		iov_iter_init(&f, ITER_SOURCE, vq->iotlb_iov, ret, size);
		ret = copy_from_iter(to, size, &f);
		if (ret == size)
			ret = 0;
	}

out:
	return ret;
}

static void __user *__vhost_get_user_slow(struct vhost_virtqueue *vq,
					  void __user *addr, unsigned int size,
					  int type)
{
	int ret;

	ret = translate_desc(vq, (u64)(uintptr_t)addr, size, vq->iotlb_iov,
			     ARRAY_SIZE(vq->iotlb_iov),
			     VHOST_ACCESS_RO);
	if (ret < 0) {
		vq_err(vq, "IOTLB translation failure: uaddr "
			"%p size 0x%llx\n", addr,
			(unsigned long long) size);
		return NULL;
	}

	if (ret != 1 || vq->iotlb_iov[0].iov_len != size) {
		vq_err(vq, "Non atomic userspace memory access: uaddr "
			"%p size 0x%llx\n", addr,
			(unsigned long long) size);
		return NULL;
	}

	return vq->iotlb_iov[0].iov_base;
}

/* This function should be called after iotlb
 * prefetch, which means we're sure that vq
 * could be access through iotlb. So -EAGAIN should
 * not happen in this case.
 */
static inline void __user *__vhost_get_user(struct vhost_virtqueue *vq,
					    void __user *addr, unsigned int size,
					    int type)
{
	void __user *uaddr = vhost_vq_meta_fetch(vq,
			     (u64)(uintptr_t)addr, size, type);
	if (uaddr)
		return uaddr;

	return __vhost_get_user_slow(vq, addr, size, type);
}

#define vhost_put_user(vq, x, ptr)		\
({ \
	int ret; \
	if (!vq->iotlb) { \
		ret = __put_user(x, ptr); \
	} else { \
		__typeof__(ptr) to = \
			(__typeof__(ptr)) __vhost_get_user(vq, ptr,	\
					  sizeof(*ptr), VHOST_ADDR_USED); \
		if (to != NULL) \
			ret = __put_user(x, to); \
		else \
			ret = -EFAULT;	\
	} \
	ret; \
})

static inline int vhost_put_avail_event(struct vhost_virtqueue *vq)
{
	return vhost_put_user(vq, cpu_to_vhost16(vq, vq->avail_idx),
			      vhost_avail_event(vq));
}

static inline int vhost_put_used(struct vhost_virtqueue *vq,
				 struct vring_used_elem *head, int idx,
				 int count)
{
	return vhost_copy_to_user(vq, vq->used->ring + idx, head,
				  count * sizeof(*head));
}

static inline int vhost_put_used_flags(struct vhost_virtqueue *vq)

{
	return vhost_put_user(vq, cpu_to_vhost16(vq, vq->used_flags),
			      &vq->used->flags);
}

static inline int vhost_put_used_idx(struct vhost_virtqueue *vq)

{
	return vhost_put_user(vq, cpu_to_vhost16(vq, vq->last_used_idx),
			      &vq->used->idx);
}

#define vhost_get_user(vq, x, ptr, type)		\
({ \
	int ret; \
	if (!vq->iotlb) { \
		ret = __get_user(x, ptr); \
	} else { \
		__typeof__(ptr) from = \
			(__typeof__(ptr)) __vhost_get_user(vq, ptr, \
							   sizeof(*ptr), \
							   type); \
		if (from != NULL) \
			ret = __get_user(x, from); \
		else \
			ret = -EFAULT; \
	} \
	ret; \
})

#define vhost_get_avail(vq, x, ptr) \
	vhost_get_user(vq, x, ptr, VHOST_ADDR_AVAIL)

#define vhost_get_used(vq, x, ptr) \
	vhost_get_user(vq, x, ptr, VHOST_ADDR_USED)

static void vhost_dev_lock_vqs(struct vhost_dev *d)
{
	int i = 0;
	for (i = 0; i < d->nvqs; ++i)
		mutex_lock_nested(&d->vqs[i]->mutex, i);
}

static void vhost_dev_unlock_vqs(struct vhost_dev *d)
{
	int i = 0;
	for (i = 0; i < d->nvqs; ++i)
		mutex_unlock(&d->vqs[i]->mutex);
}

static inline int vhost_get_avail_idx(struct vhost_virtqueue *vq,
				      __virtio16 *idx)
{
	return vhost_get_avail(vq, *idx, &vq->avail->idx);
}

static inline int vhost_get_avail_head(struct vhost_virtqueue *vq,
				       __virtio16 *head, int idx)
{
	return vhost_get_avail(vq, *head,
			       &vq->avail->ring[idx & (vq->num - 1)]);
}

static inline int vhost_get_avail_flags(struct vhost_virtqueue *vq,
					__virtio16 *flags)
{
	return vhost_get_avail(vq, *flags, &vq->avail->flags);
}

static inline int vhost_get_used_event(struct vhost_virtqueue *vq,
				       __virtio16 *event)
{
	return vhost_get_avail(vq, *event, vhost_used_event(vq));
}

static inline int vhost_get_used_idx(struct vhost_virtqueue *vq,
				     __virtio16 *idx)
{
	return vhost_get_used(vq, *idx, &vq->used->idx);
}

static inline int vhost_get_desc(struct vhost_virtqueue *vq,
				 struct vring_desc *desc, int idx)
{
	return vhost_copy_from_user(vq, desc, vq->desc + idx, sizeof(*desc));
}

static void vhost_iotlb_notify_vq(struct vhost_dev *d,
				  struct vhost_iotlb_msg *msg)
{
	struct vhost_msg_node *node, *n;

	spin_lock(&d->iotlb_lock);

	list_for_each_entry_safe(node, n, &d->pending_list, node) {
		struct vhost_iotlb_msg *vq_msg = &node->msg.iotlb;
		if (msg->iova <= vq_msg->iova &&
		    msg->iova + msg->size - 1 >= vq_msg->iova &&
		    vq_msg->type == VHOST_IOTLB_MISS) {
			vhost_poll_queue(&node->vq->poll);
			list_del(&node->node);
			kfree(node);
		}
	}

	spin_unlock(&d->iotlb_lock);
}

static bool umem_access_ok(u64 uaddr, u64 size, int access)
{
	unsigned long a = uaddr;

	/* Make sure 64 bit math will not overflow. */
	if (vhost_overflow(uaddr, size))
		return false;

	if ((access & VHOST_ACCESS_RO) &&
	    !access_ok((void __user *)a, size))
		return false;
	if ((access & VHOST_ACCESS_WO) &&
	    !access_ok((void __user *)a, size))
		return false;
	return true;
}

static int vhost_process_iotlb_msg(struct vhost_dev *dev, u32 asid,
				   struct vhost_iotlb_msg *msg)
{
	int ret = 0;

	if (asid != 0)
		return -EINVAL;

	mutex_lock(&dev->mutex);
	vhost_dev_lock_vqs(dev);
	switch (msg->type) {
	case VHOST_IOTLB_UPDATE:
		if (!dev->iotlb) {
			ret = -EFAULT;
			break;
		}
		if (!umem_access_ok(msg->uaddr, msg->size, msg->perm)) {
			ret = -EFAULT;
			break;
		}
		vhost_vq_meta_reset(dev);
		if (vhost_iotlb_add_range(dev->iotlb, msg->iova,
					  msg->iova + msg->size - 1,
					  msg->uaddr, msg->perm)) {
			ret = -ENOMEM;
			break;
		}
		vhost_iotlb_notify_vq(dev, msg);
		break;
	case VHOST_IOTLB_INVALIDATE:
		if (!dev->iotlb) {
			ret = -EFAULT;
			break;
		}
		vhost_vq_meta_reset(dev);
		vhost_iotlb_del_range(dev->iotlb, msg->iova,
				      msg->iova + msg->size - 1);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	vhost_dev_unlock_vqs(dev);
	mutex_unlock(&dev->mutex);

	return ret;
}
ssize_t vhost_chr_write_iter(struct vhost_dev *dev,
			     struct iov_iter *from)
{
	struct vhost_iotlb_msg msg;
	size_t offset;
	int type, ret;
	u32 asid = 0;

	ret = copy_from_iter(&type, sizeof(type), from);
	if (ret != sizeof(type)) {
		ret = -EINVAL;
		goto done;
	}

	switch (type) {
	case VHOST_IOTLB_MSG:
		/* There maybe a hole after type for V1 message type,
		 * so skip it here.
		 */
		offset = offsetof(struct vhost_msg, iotlb) - sizeof(int);
		break;
	case VHOST_IOTLB_MSG_V2:
		if (vhost_backend_has_feature(dev->vqs[0],
					      VHOST_BACKEND_F_IOTLB_ASID)) {
			ret = copy_from_iter(&asid, sizeof(asid), from);
			if (ret != sizeof(asid)) {
				ret = -EINVAL;
				goto done;
			}
			offset = 0;
		} else
			offset = sizeof(__u32);
		break;
	default:
		ret = -EINVAL;
		goto done;
	}

	iov_iter_advance(from, offset);
	ret = copy_from_iter(&msg, sizeof(msg), from);
	if (ret != sizeof(msg)) {
		ret = -EINVAL;
		goto done;
	}

	if ((msg.type == VHOST_IOTLB_UPDATE ||
	     msg.type == VHOST_IOTLB_INVALIDATE) &&
	     msg.size == 0) {
		ret = -EINVAL;
		goto done;
	}

	if (dev->msg_handler)
		ret = dev->msg_handler(dev, asid, &msg);
	else
		ret = vhost_process_iotlb_msg(dev, asid, &msg);
	if (ret) {
		ret = -EFAULT;
		goto done;
	}

	ret = (type == VHOST_IOTLB_MSG) ? sizeof(struct vhost_msg) :
	      sizeof(struct vhost_msg_v2);
done:
	return ret;
}
EXPORT_SYMBOL(vhost_chr_write_iter);

__poll_t vhost_chr_poll(struct file *file, struct vhost_dev *dev,
			    poll_table *wait)
{
	__poll_t mask = 0;

	poll_wait(file, &dev->wait, wait);

	if (!list_empty(&dev->read_list))
		mask |= EPOLLIN | EPOLLRDNORM;

	return mask;
}
EXPORT_SYMBOL(vhost_chr_poll);

ssize_t vhost_chr_read_iter(struct vhost_dev *dev, struct iov_iter *to,
			    int noblock)
{
	DEFINE_WAIT(wait);
	struct vhost_msg_node *node;
	ssize_t ret = 0;
	unsigned size = sizeof(struct vhost_msg);

	if (iov_iter_count(to) < size)
		return 0;

	while (1) {
		if (!noblock)
			prepare_to_wait(&dev->wait, &wait,
					TASK_INTERRUPTIBLE);

		node = vhost_dequeue_msg(dev, &dev->read_list);
		if (node)
			break;
		if (noblock) {
			ret = -EAGAIN;
			break;
		}
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
		if (!dev->iotlb) {
			ret = -EBADFD;
			break;
		}

		schedule();
	}

	if (!noblock)
		finish_wait(&dev->wait, &wait);

	if (node) {
		struct vhost_iotlb_msg *msg;
		void *start = &node->msg;

		switch (node->msg.type) {
		case VHOST_IOTLB_MSG:
			size = sizeof(node->msg);
			msg = &node->msg.iotlb;
			break;
		case VHOST_IOTLB_MSG_V2:
			size = sizeof(node->msg_v2);
			msg = &node->msg_v2.iotlb;
			break;
		default:
			BUG();
			break;
		}

		ret = copy_to_iter(start, size, to);
		if (ret != size || msg->type != VHOST_IOTLB_MISS) {
			kfree(node);
			return ret;
		}
		vhost_enqueue_msg(dev, &dev->pending_list, node);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(vhost_chr_read_iter);

static int vhost_iotlb_miss(struct vhost_virtqueue *vq, u64 iova, int access)
{
	struct vhost_dev *dev = vq->dev;
	struct vhost_msg_node *node;
	struct vhost_iotlb_msg *msg;
	bool v2 = vhost_backend_has_feature(vq, VHOST_BACKEND_F_IOTLB_MSG_V2);

	node = vhost_new_msg(vq, v2 ? VHOST_IOTLB_MSG_V2 : VHOST_IOTLB_MSG);
	if (!node)
		return -ENOMEM;

	if (v2) {
		node->msg_v2.type = VHOST_IOTLB_MSG_V2;
		msg = &node->msg_v2.iotlb;
	} else {
		msg = &node->msg.iotlb;
	}

	msg->type = VHOST_IOTLB_MISS;
	msg->iova = iova;
	msg->perm = access;

	vhost_enqueue_msg(dev, &dev->read_list, node);

	return 0;
}

static bool vq_access_ok(struct vhost_virtqueue *vq, unsigned int num,
			 vring_desc_t __user *desc,
			 vring_avail_t __user *avail,
			 vring_used_t __user *used)

{
	/* If an IOTLB device is present, the vring addresses are
	 * GIOVAs. Access validation occurs at prefetch time. */
	if (vq->iotlb)
		return true;

	return access_ok(desc, vhost_get_desc_size(vq, num)) &&
	       access_ok(avail, vhost_get_avail_size(vq, num)) &&
	       access_ok(used, vhost_get_used_size(vq, num));
}

static void vhost_vq_meta_update(struct vhost_virtqueue *vq,
				 const struct vhost_iotlb_map *map,
				 int type)
{
	int access = (type == VHOST_ADDR_USED) ?
		     VHOST_ACCESS_WO : VHOST_ACCESS_RO;

	if (likely(map->perm & access))
		vq->meta_iotlb[type] = map;
}

static bool iotlb_access_ok(struct vhost_virtqueue *vq,
			    int access, u64 addr, u64 len, int type)
{
	const struct vhost_iotlb_map *map;
	struct vhost_iotlb *umem = vq->iotlb;
	u64 s = 0, size, orig_addr = addr, last = addr + len - 1;

	if (vhost_vq_meta_fetch(vq, addr, len, type))
		return true;

	while (len > s) {
		map = vhost_iotlb_itree_first(umem, addr, last);
		if (map == NULL || map->start > addr) {
			vhost_iotlb_miss(vq, addr, access);
			return false;
		} else if (!(map->perm & access)) {
			/* Report the possible access violation by
			 * request another translation from userspace.
			 */
			return false;
		}

		size = map->size - addr + map->start;

		if (orig_addr == addr && size >= len)
			vhost_vq_meta_update(vq, map, type);

		s += size;
		addr += size;
	}

	return true;
}

int vq_meta_prefetch(struct vhost_virtqueue *vq)
{
	unsigned int num = vq->num;

	if (!vq->iotlb)
		return 1;

	return iotlb_access_ok(vq, VHOST_MAP_RO, (u64)(uintptr_t)vq->desc,
			       vhost_get_desc_size(vq, num), VHOST_ADDR_DESC) &&
	       iotlb_access_ok(vq, VHOST_MAP_RO, (u64)(uintptr_t)vq->avail,
			       vhost_get_avail_size(vq, num),
			       VHOST_ADDR_AVAIL) &&
	       iotlb_access_ok(vq, VHOST_MAP_WO, (u64)(uintptr_t)vq->used,
			       vhost_get_used_size(vq, num), VHOST_ADDR_USED);
}
EXPORT_SYMBOL_GPL(vq_meta_prefetch);

/* Can we log writes? */
/* Caller should have device mutex but not vq mutex */
bool vhost_log_access_ok(struct vhost_dev *dev)
{
	return memory_access_ok(dev, dev->umem, 1);
}
EXPORT_SYMBOL_GPL(vhost_log_access_ok);

static bool vq_log_used_access_ok(struct vhost_virtqueue *vq,
				  void __user *log_base,
				  bool log_used,
				  u64 log_addr)
{
	/* If an IOTLB device is present, log_addr is a GIOVA that
	 * will never be logged by log_used(). */
	if (vq->iotlb)
		return true;

	return !log_used || log_access_ok(log_base, log_addr,
					  vhost_get_used_size(vq, vq->num));
}

/* Verify access for write logging. */
/* Caller should have vq mutex and device mutex */
static bool vq_log_access_ok(struct vhost_virtqueue *vq,
			     void __user *log_base)
{
	return vq_memory_access_ok(log_base, vq->umem,
				   vhost_has_feature(vq, VHOST_F_LOG_ALL)) &&
		vq_log_used_access_ok(vq, log_base, vq->log_used, vq->log_addr);
}

/* Can we start vq? */
/* Caller should have vq mutex and device mutex */
bool vhost_vq_access_ok(struct vhost_virtqueue *vq)
{
	if (!vq_log_access_ok(vq, vq->log_base))
		return false;

	return vq_access_ok(vq, vq->num, vq->desc, vq->avail, vq->used);
}
EXPORT_SYMBOL_GPL(vhost_vq_access_ok);

static long vhost_set_memory(struct vhost_dev *d, struct vhost_memory __user *m)
{
	struct vhost_memory mem, *newmem;
	struct vhost_memory_region *region;
	struct vhost_iotlb *newumem, *oldumem;
	unsigned long size = offsetof(struct vhost_memory, regions);
	int i;

	if (copy_from_user(&mem, m, size))
		return -EFAULT;
	if (mem.padding)
		return -EOPNOTSUPP;
	if (mem.nregions > max_mem_regions)
		return -E2BIG;
	newmem = kvzalloc(struct_size(newmem, regions, mem.nregions),
			GFP_KERNEL);
	if (!newmem)
		return -ENOMEM;

	memcpy(newmem, &mem, size);
	if (copy_from_user(newmem->regions, m->regions,
			   flex_array_size(newmem, regions, mem.nregions))) {
		kvfree(newmem);
		return -EFAULT;
	}

	newumem = iotlb_alloc();
	if (!newumem) {
		kvfree(newmem);
		return -ENOMEM;
	}

	for (region = newmem->regions;
	     region < newmem->regions + mem.nregions;
	     region++) {
		if (vhost_iotlb_add_range(newumem,
					  region->guest_phys_addr,
					  region->guest_phys_addr +
					  region->memory_size - 1,
					  region->userspace_addr,
					  VHOST_MAP_RW))
			goto err;
	}

	if (!memory_access_ok(d, newumem, 0))
		goto err;

	oldumem = d->umem;
	d->umem = newumem;

	/* All memory accesses are done under some VQ mutex. */
	for (i = 0; i < d->nvqs; ++i) {
		mutex_lock(&d->vqs[i]->mutex);
		d->vqs[i]->umem = newumem;
		mutex_unlock(&d->vqs[i]->mutex);
	}

	kvfree(newmem);
	vhost_iotlb_free(oldumem);
	return 0;

err:
	vhost_iotlb_free(newumem);
	kvfree(newmem);
	return -EFAULT;
}

static long vhost_vring_set_num(struct vhost_dev *d,
				struct vhost_virtqueue *vq,
				void __user *argp)
{
	struct vhost_vring_state s;

	/* Resizing ring with an active backend?
	 * You don't want to do that. */
	if (vq->private_data)
		return -EBUSY;

	if (copy_from_user(&s, argp, sizeof s))
		return -EFAULT;

	if (!s.num || s.num > 0xffff || (s.num & (s.num - 1)))
		return -EINVAL;
	vq->num = s.num;

	return 0;
}

static long vhost_vring_set_addr(struct vhost_dev *d,
				 struct vhost_virtqueue *vq,
				 void __user *argp)
{
	struct vhost_vring_addr a;

	if (copy_from_user(&a, argp, sizeof a))
		return -EFAULT;
	if (a.flags & ~(0x1 << VHOST_VRING_F_LOG))
		return -EOPNOTSUPP;

	/* For 32bit, verify that the top 32bits of the user
	   data are set to zero. */
	if ((u64)(unsigned long)a.desc_user_addr != a.desc_user_addr ||
	    (u64)(unsigned long)a.used_user_addr != a.used_user_addr ||
	    (u64)(unsigned long)a.avail_user_addr != a.avail_user_addr)
		return -EFAULT;

	/* Make sure it's safe to cast pointers to vring types. */
	BUILD_BUG_ON(__alignof__ *vq->avail > VRING_AVAIL_ALIGN_SIZE);
	BUILD_BUG_ON(__alignof__ *vq->used > VRING_USED_ALIGN_SIZE);
	if ((a.avail_user_addr & (VRING_AVAIL_ALIGN_SIZE - 1)) ||
	    (a.used_user_addr & (VRING_USED_ALIGN_SIZE - 1)) ||
	    (a.log_guest_addr & (VRING_USED_ALIGN_SIZE - 1)))
		return -EINVAL;

	/* We only verify access here if backend is configured.
	 * If it is not, we don't as size might not have been setup.
	 * We will verify when backend is configured. */
	if (vq->private_data) {
		if (!vq_access_ok(vq, vq->num,
			(void __user *)(unsigned long)a.desc_user_addr,
			(void __user *)(unsigned long)a.avail_user_addr,
			(void __user *)(unsigned long)a.used_user_addr))
			return -EINVAL;

		/* Also validate log access for used ring if enabled. */
		if (!vq_log_used_access_ok(vq, vq->log_base,
				a.flags & (0x1 << VHOST_VRING_F_LOG),
				a.log_guest_addr))
			return -EINVAL;
	}

	vq->log_used = !!(a.flags & (0x1 << VHOST_VRING_F_LOG));
	vq->desc = (void __user *)(unsigned long)a.desc_user_addr;
	vq->avail = (void __user *)(unsigned long)a.avail_user_addr;
	vq->log_addr = a.log_guest_addr;
	vq->used = (void __user *)(unsigned long)a.used_user_addr;

	return 0;
}

static long vhost_vring_set_num_addr(struct vhost_dev *d,
				     struct vhost_virtqueue *vq,
				     unsigned int ioctl,
				     void __user *argp)
{
	long r;

	mutex_lock(&vq->mutex);

	switch (ioctl) {
	case VHOST_SET_VRING_NUM:
		r = vhost_vring_set_num(d, vq, argp);
		break;
	case VHOST_SET_VRING_ADDR:
		r = vhost_vring_set_addr(d, vq, argp);
		break;
	default:
		BUG();
	}

	mutex_unlock(&vq->mutex);

	return r;
}
long vhost_vring_ioctl(struct vhost_dev *d, unsigned int ioctl, void __user *argp)
{
	struct file *eventfp, *filep = NULL;
	bool pollstart = false, pollstop = false;
	struct eventfd_ctx *ctx = NULL;
	u32 __user *idxp = argp;
	struct vhost_virtqueue *vq;
	struct vhost_vring_state s;
	struct vhost_vring_file f;
	u32 idx;
	long r;

	r = get_user(idx, idxp);
	if (r < 0)
		return r;
	if (idx >= d->nvqs)
		return -ENOBUFS;

	idx = array_index_nospec(idx, d->nvqs);
	vq = d->vqs[idx];

	if (ioctl == VHOST_SET_VRING_NUM ||
	    ioctl == VHOST_SET_VRING_ADDR) {
		return vhost_vring_set_num_addr(d, vq, ioctl, argp);
	}

	mutex_lock(&vq->mutex);

	switch (ioctl) {
	case VHOST_SET_VRING_BASE:
		/* Moving base with an active backend?
		 * You don't want to do that. */
		if (vq->private_data) {
			r = -EBUSY;
			break;
		}
		if (copy_from_user(&s, argp, sizeof s)) {
			r = -EFAULT;
			break;
		}
		if (vhost_has_feature(vq, VIRTIO_F_RING_PACKED)) {
			vq->last_avail_idx = s.num & 0xffff;
			vq->last_used_idx = (s.num >> 16) & 0xffff;
		} else {
			if (s.num > 0xffff) {
				r = -EINVAL;
				break;
			}
			vq->last_avail_idx = s.num;
		}
		/* Forget the cached index value. */
		vq->avail_idx = vq->last_avail_idx;
		break;
	case VHOST_GET_VRING_BASE:
		s.index = idx;
		if (vhost_has_feature(vq, VIRTIO_F_RING_PACKED))
			s.num = (u32)vq->last_avail_idx | ((u32)vq->last_used_idx << 16);
		else
			s.num = vq->last_avail_idx;
		if (copy_to_user(argp, &s, sizeof s))
			r = -EFAULT;
		break;
	case VHOST_SET_VRING_KICK:
		if (copy_from_user(&f, argp, sizeof f)) {
			r = -EFAULT;
			break;
		}
		eventfp = f.fd == VHOST_FILE_UNBIND ? NULL : eventfd_fget(f.fd);
		if (IS_ERR(eventfp)) {
			r = PTR_ERR(eventfp);
			break;
		}
		if (eventfp != vq->kick) {
			pollstop = (filep = vq->kick) != NULL;
			pollstart = (vq->kick = eventfp) != NULL;
		} else
			filep = eventfp;
		break;
	case VHOST_SET_VRING_CALL:
		if (copy_from_user(&f, argp, sizeof f)) {
			r = -EFAULT;
			break;
		}
		ctx = f.fd == VHOST_FILE_UNBIND ? NULL : eventfd_ctx_fdget(f.fd);
		if (IS_ERR(ctx)) {
			r = PTR_ERR(ctx);
			break;
		}

		swap(ctx, vq->call_ctx.ctx);
		break;
	case VHOST_SET_VRING_ERR:
		if (copy_from_user(&f, argp, sizeof f)) {
			r = -EFAULT;
			break;
		}
		ctx = f.fd == VHOST_FILE_UNBIND ? NULL : eventfd_ctx_fdget(f.fd);
		if (IS_ERR(ctx)) {
			r = PTR_ERR(ctx);
			break;
		}
		swap(ctx, vq->error_ctx);
		break;
	case VHOST_SET_VRING_ENDIAN:
		r = vhost_set_vring_endian(vq, argp);
		break;
	case VHOST_GET_VRING_ENDIAN:
		r = vhost_get_vring_endian(vq, idx, argp);
		break;
	case VHOST_SET_VRING_BUSYLOOP_TIMEOUT:
		if (copy_from_user(&s, argp, sizeof(s))) {
			r = -EFAULT;
			break;
		}
		vq->busyloop_timeout = s.num;
		break;
	case VHOST_GET_VRING_BUSYLOOP_TIMEOUT:
		s.index = idx;
		s.num = vq->busyloop_timeout;
		if (copy_to_user(argp, &s, sizeof(s)))
			r = -EFAULT;
		break;
	default:
		r = -ENOIOCTLCMD;
	}

	if (pollstop && vq->handle_kick)
		vhost_poll_stop(&vq->poll);

	if (!IS_ERR_OR_NULL(ctx))
		eventfd_ctx_put(ctx);
	if (filep)
		fput(filep);

	if (pollstart && vq->handle_kick)
		r = vhost_poll_start(&vq->poll, vq->kick);

	mutex_unlock(&vq->mutex);

	if (pollstop && vq->handle_kick)
		vhost_dev_flush(vq->poll.dev);
	return r;
}
EXPORT_SYMBOL_GPL(vhost_vring_ioctl);

int vhost_init_device_iotlb(struct vhost_dev *d)
{
	struct vhost_iotlb *niotlb, *oiotlb;
	int i;

	niotlb = iotlb_alloc();
	if (!niotlb)
		return -ENOMEM;

	oiotlb = d->iotlb;
	d->iotlb = niotlb;

	for (i = 0; i < d->nvqs; ++i) {
		struct vhost_virtqueue *vq = d->vqs[i];

		mutex_lock(&vq->mutex);
		vq->iotlb = niotlb;
		__vhost_vq_meta_reset(vq);
		mutex_unlock(&vq->mutex);
	}

	vhost_iotlb_free(oiotlb);

	return 0;
}
EXPORT_SYMBOL_GPL(vhost_init_device_iotlb);

/* Caller must have device mutex */
long vhost_dev_ioctl(struct vhost_dev *d, unsigned int ioctl, void __user *argp)
{
	struct eventfd_ctx *ctx;
	u64 p;
	long r;
	int i, fd;

	/* If you are not the owner, you can become one */
	if (ioctl == VHOST_SET_OWNER) {
		r = vhost_dev_set_owner(d);
		goto done;
	}

	/* You must be the owner to do anything else */
	r = vhost_dev_check_owner(d);
	if (r)
		goto done;

	switch (ioctl) {
	case VHOST_SET_MEM_TABLE:
		r = vhost_set_memory(d, argp);
		break;
	case VHOST_SET_LOG_BASE:
		if (copy_from_user(&p, argp, sizeof p)) {
			r = -EFAULT;
			break;
		}
		if ((u64)(unsigned long)p != p) {
			r = -EFAULT;
			break;
		}
		for (i = 0; i < d->nvqs; ++i) {
			struct vhost_virtqueue *vq;
			void __user *base = (void __user *)(unsigned long)p;
			vq = d->vqs[i];
			mutex_lock(&vq->mutex);
			/* If ring is inactive, will check when it's enabled. */
			if (vq->private_data && !vq_log_access_ok(vq, base))
				r = -EFAULT;
			else
				vq->log_base = base;
			mutex_unlock(&vq->mutex);
		}
		break;
	case VHOST_SET_LOG_FD:
		r = get_user(fd, (int __user *)argp);
		if (r < 0)
			break;
		ctx = fd == VHOST_FILE_UNBIND ? NULL : eventfd_ctx_fdget(fd);
		if (IS_ERR(ctx)) {
			r = PTR_ERR(ctx);
			break;
		}
		swap(ctx, d->log_ctx);
		for (i = 0; i < d->nvqs; ++i) {
			mutex_lock(&d->vqs[i]->mutex);
			d->vqs[i]->log_ctx = d->log_ctx;
			mutex_unlock(&d->vqs[i]->mutex);
		}
		if (ctx)
			eventfd_ctx_put(ctx);
		break;
	default:
		r = -ENOIOCTLCMD;
		break;
	}
done:
	return r;
}
EXPORT_SYMBOL_GPL(vhost_dev_ioctl);

/* TODO: This is really inefficient.  We need something like get_user()
 * (instruction directly accesses the data, with an exception table entry
 * returning -EFAULT). See Documentation/arch/x86/exception-tables.rst.
 */
static int set_bit_to_user(int nr, void __user *addr)
{
	unsigned long log = (unsigned long)addr;
	struct page *page;
	void *base;
	int bit = nr + (log % PAGE_SIZE) * 8;
	int r;

	r = pin_user_pages_fast(log, 1, FOLL_WRITE, &page);
	if (r < 0)
		return r;
	BUG_ON(r != 1);
	base = kmap_atomic(page);
	set_bit(bit, base);
	kunmap_atomic(base);
	unpin_user_pages_dirty_lock(&page, 1, true);
	return 0;
}

static int log_write(void __user *log_base,
		     u64 write_address, u64 write_length)
{
	u64 write_page = write_address / VHOST_PAGE_SIZE;
	int r;

	if (!write_length)
		return 0;
	write_length += write_address % VHOST_PAGE_SIZE;
	for (;;) {
		u64 base = (u64)(unsigned long)log_base;
		u64 log = base + write_page / 8;
		int bit = write_page % 8;
		if ((u64)(unsigned long)log != log)
			return -EFAULT;
		r = set_bit_to_user(bit, (void __user *)(unsigned long)log);
		if (r < 0)
			return r;
		if (write_length <= VHOST_PAGE_SIZE)
			break;
		write_length -= VHOST_PAGE_SIZE;
		write_page += 1;
	}
	return r;
}

static int log_write_hva(struct vhost_virtqueue *vq, u64 hva, u64 len)
{
	struct vhost_iotlb *umem = vq->umem;
	struct vhost_iotlb_map *u;
	u64 start, end, l, min;
	int r;
	bool hit = false;

	while (len) {
		min = len;
		/* More than one GPAs can be mapped into a single HVA. So
		 * iterate all possible umems here to be safe.
		 */
		list_for_each_entry(u, &umem->list, link) {
			if (u->addr > hva - 1 + len ||
			    u->addr - 1 + u->size < hva)
				continue;
			start = max(u->addr, hva);
			end = min(u->addr - 1 + u->size, hva - 1 + len);
			l = end - start + 1;
			r = log_write(vq->log_base,
				      u->start + start - u->addr,
				      l);
			if (r < 0)
				return r;
			hit = true;
			min = min(l, min);
		}

		if (!hit)
			return -EFAULT;

		len -= min;
		hva += min;
	}

	return 0;
}

static int log_used(struct vhost_virtqueue *vq, u64 used_offset, u64 len)
{
	struct iovec *iov = vq->log_iov;
	int i, ret;

	if (!vq->iotlb)
		return log_write(vq->log_base, vq->log_addr + used_offset, len);

	ret = translate_desc(vq, (uintptr_t)vq->used + used_offset,
			     len, iov, 64, VHOST_ACCESS_WO);
	if (ret < 0)
		return ret;

	for (i = 0; i < ret; i++) {
		ret = log_write_hva(vq,	(uintptr_t)iov[i].iov_base,
				    iov[i].iov_len);
		if (ret)
			return ret;
	}

	return 0;
}

int vhost_log_write(struct vhost_virtqueue *vq, struct vhost_log *log,
		    unsigned int log_num, u64 len, struct iovec *iov, int count)
{
	int i, r;

	/* Make sure data written is seen before log. */
	smp_wmb();

	if (vq->iotlb) {
		for (i = 0; i < count; i++) {
			r = log_write_hva(vq, (uintptr_t)iov[i].iov_base,
					  iov[i].iov_len);
			if (r < 0)
				return r;
		}
		return 0;
	}

	for (i = 0; i < log_num; ++i) {
		u64 l = min(log[i].len, len);
		r = log_write(vq->log_base, log[i].addr, l);
		if (r < 0)
			return r;
		len -= l;
		if (!len) {
			if (vq->log_ctx)
				eventfd_signal(vq->log_ctx, 1);
			return 0;
		}
	}
	/* Length written exceeds what we have stored. This is a bug. */
	BUG();
	return 0;
}
EXPORT_SYMBOL_GPL(vhost_log_write);

static int vhost_update_used_flags(struct vhost_virtqueue *vq)
{
	void __user *used;
	if (vhost_put_used_flags(vq))
		return -EFAULT;
	if (unlikely(vq->log_used)) {
		/* Make sure the flag is seen before log. */
		smp_wmb();
		/* Log used flag write. */
		used = &vq->used->flags;
		log_used(vq, (used - (void __user *)vq->used),
			 sizeof vq->used->flags);
		if (vq->log_ctx)
			eventfd_signal(vq->log_ctx, 1);
	}
	return 0;
}

static int vhost_update_avail_event(struct vhost_virtqueue *vq)
{
	if (vhost_put_avail_event(vq))
		return -EFAULT;
	if (unlikely(vq->log_used)) {
		void __user *used;
		/* Make sure the event is seen before log. */
		smp_wmb();
		/* Log avail event write */
		used = vhost_avail_event(vq);
		log_used(vq, (used - (void __user *)vq->used),
			 sizeof *vhost_avail_event(vq));
		if (vq->log_ctx)
			eventfd_signal(vq->log_ctx, 1);
	}
	return 0;
}

int vhost_vq_init_access(struct vhost_virtqueue *vq)
{
	__virtio16 last_used_idx;
	int r;
	bool is_le = vq->is_le;

	if (!vq->private_data)
		return 0;

	vhost_init_is_le(vq);

	r = vhost_update_used_flags(vq);
	if (r)
		goto err;
	vq->signalled_used_valid = false;
	if (!vq->iotlb &&
	    !access_ok(&vq->used->idx, sizeof vq->used->idx)) {
		r = -EFAULT;
		goto err;
	}
	r = vhost_get_used_idx(vq, &last_used_idx);
	if (r) {
		vq_err(vq, "Can't access used idx at %p\n",
		       &vq->used->idx);
		goto err;
	}
	vq->last_used_idx = vhost16_to_cpu(vq, last_used_idx);
	return 0;

err:
	vq->is_le = is_le;
	return r;
}
EXPORT_SYMBOL_GPL(vhost_vq_init_access);

static int translate_desc(struct vhost_virtqueue *vq, u64 addr, u32 len,
			  struct iovec iov[], int iov_size, int access)
{
	const struct vhost_iotlb_map *map;
	struct vhost_dev *dev = vq->dev;
	struct vhost_iotlb *umem = dev->iotlb ? dev->iotlb : dev->umem;
	struct iovec *_iov;
	u64 s = 0, last = addr + len - 1;
	int ret = 0;

	while ((u64)len > s) {
		u64 size;
		if (unlikely(ret >= iov_size)) {
			ret = -ENOBUFS;
			break;
		}

		map = vhost_iotlb_itree_first(umem, addr, last);
		if (map == NULL || map->start > addr) {
			if (umem != dev->iotlb) {
				ret = -EFAULT;
				break;
			}
			ret = -EAGAIN;
			break;
		} else if (!(map->perm & access)) {
			ret = -EPERM;
			break;
		}

		_iov = iov + ret;
		size = map->size - addr + map->start;
		_iov->iov_len = min((u64)len - s, size);
		_iov->iov_base = (void __user *)(unsigned long)
				 (map->addr + addr - map->start);
		s += size;
		addr += size;
		++ret;
	}

	if (ret == -EAGAIN)
		vhost_iotlb_miss(vq, addr, access);
	return ret;
}

/* Each buffer in the virtqueues is actually a chain of descriptors.  This
 * function returns the next descriptor in the chain,
 * or -1U if we're at the end. */
static unsigned next_desc(struct vhost_virtqueue *vq, struct vring_desc *desc)
{
	unsigned int next;

	/* If this descriptor says it doesn't chain, we're done. */
	if (!(desc->flags & cpu_to_vhost16(vq, VRING_DESC_F_NEXT)))
		return -1U;

	/* Check they're not leading us off end of descriptors. */
	next = vhost16_to_cpu(vq, READ_ONCE(desc->next));
	return next;
}

static int get_indirect(struct vhost_virtqueue *vq,
			struct iovec iov[], unsigned int iov_size,
			unsigned int *out_num, unsigned int *in_num,
			struct vhost_log *log, unsigned int *log_num,
			struct vring_desc *indirect)
{
	struct vring_desc desc;
	unsigned int i = 0, count, found = 0;
	u32 len = vhost32_to_cpu(vq, indirect->len);
	struct iov_iter from;
	int ret, access;

	/* Sanity check */
	if (unlikely(len % sizeof desc)) {
		vq_err(vq, "Invalid length in indirect descriptor: "
		       "len 0x%llx not multiple of 0x%zx\n",
		       (unsigned long long)len,
		       sizeof desc);
		return -EINVAL;
	}

	ret = translate_desc(vq, vhost64_to_cpu(vq, indirect->addr), len, vq->indirect,
			     UIO_MAXIOV, VHOST_ACCESS_RO);
	if (unlikely(ret < 0)) {
		if (ret != -EAGAIN)
			vq_err(vq, "Translation failure %d in indirect.\n", ret);
		return ret;
	}
	iov_iter_init(&from, ITER_SOURCE, vq->indirect, ret, len);
	count = len / sizeof desc;
	/* Buffers are chained via a 16 bit next field, so
	 * we can have at most 2^16 of these. */
	if (unlikely(count > USHRT_MAX + 1)) {
		vq_err(vq, "Indirect buffer length too big: %d\n",
		       indirect->len);
		return -E2BIG;
	}

	do {
		unsigned iov_count = *in_num + *out_num;
		if (unlikely(++found > count)) {
			vq_err(vq, "Loop detected: last one at %u "
			       "indirect size %u\n",
			       i, count);
			return -EINVAL;
		}
		if (unlikely(!copy_from_iter_full(&desc, sizeof(desc), &from))) {
			vq_err(vq, "Failed indirect descriptor: idx %d, %zx\n",
			       i, (size_t)vhost64_to_cpu(vq, indirect->addr) + i * sizeof desc);
			return -EINVAL;
		}
		if (unlikely(desc.flags & cpu_to_vhost16(vq, VRING_DESC_F_INDIRECT))) {
			vq_err(vq, "Nested indirect descriptor: idx %d, %zx\n",
			       i, (size_t)vhost64_to_cpu(vq, indirect->addr) + i * sizeof desc);
			return -EINVAL;
		}

		if (desc.flags & cpu_to_vhost16(vq, VRING_DESC_F_WRITE))
			access = VHOST_ACCESS_WO;
		else
			access = VHOST_ACCESS_RO;

		ret = translate_desc(vq, vhost64_to_cpu(vq, desc.addr),
				     vhost32_to_cpu(vq, desc.len), iov + iov_count,
				     iov_size - iov_count, access);
		if (unlikely(ret < 0)) {
			if (ret != -EAGAIN)
				vq_err(vq, "Translation failure %d indirect idx %d\n",
					ret, i);
			return ret;
		}
		/* If this is an input descriptor, increment that count. */
		if (access == VHOST_ACCESS_WO) {
			*in_num += ret;
			if (unlikely(log && ret)) {
				log[*log_num].addr = vhost64_to_cpu(vq, desc.addr);
				log[*log_num].len = vhost32_to_cpu(vq, desc.len);
				++*log_num;
			}
		} else {
			/* If it's an output descriptor, they're all supposed
			 * to come before any input descriptors. */
			if (unlikely(*in_num)) {
				vq_err(vq, "Indirect descriptor "
				       "has out after in: idx %d\n", i);
				return -EINVAL;
			}
			*out_num += ret;
		}
	} while ((i = next_desc(vq, &desc)) != -1);
	return 0;
}

/* This looks in the virtqueue and for the first available buffer, and converts
 * it to an iovec for convenient access.  Since descriptors consist of some
 * number of output then some number of input descriptors, it's actually two
 * iovecs, but we pack them into one and note how many of each there were.
 *
 * This function returns the descriptor number found, or vq->num (which is
 * never a valid descriptor number) if none was found.  A negative code is
 * returned on error. */
int vhost_get_vq_desc(struct vhost_virtqueue *vq,
		      struct iovec iov[], unsigned int iov_size,
		      unsigned int *out_num, unsigned int *in_num,
		      struct vhost_log *log, unsigned int *log_num)
{
	struct vring_desc desc;
	unsigned int i, head, found = 0;
	u16 last_avail_idx;
	__virtio16 avail_idx;
	__virtio16 ring_head;
	int ret, access;

	/* Check it isn't doing very strange things with descriptor numbers. */
	last_avail_idx = vq->last_avail_idx;

	if (vq->avail_idx == vq->last_avail_idx) {
		if (unlikely(vhost_get_avail_idx(vq, &avail_idx))) {
			vq_err(vq, "Failed to access avail idx at %p\n",
				&vq->avail->idx);
			return -EFAULT;
		}
		vq->avail_idx = vhost16_to_cpu(vq, avail_idx);

		if (unlikely((u16)(vq->avail_idx - last_avail_idx) > vq->num)) {
			vq_err(vq, "Guest moved used index from %u to %u",
				last_avail_idx, vq->avail_idx);
			return -EFAULT;
		}

		/* If there's nothing new since last we looked, return
		 * invalid.
		 */
		if (vq->avail_idx == last_avail_idx)
			return vq->num;

		/* Only get avail ring entries after they have been
		 * exposed by guest.
		 */
		smp_rmb();
	}

	/* Grab the next descriptor number they're advertising, and increment
	 * the index we've seen. */
	if (unlikely(vhost_get_avail_head(vq, &ring_head, last_avail_idx))) {
		vq_err(vq, "Failed to read head: idx %d address %p\n",
		       last_avail_idx,
		       &vq->avail->ring[last_avail_idx % vq->num]);
		return -EFAULT;
	}

	head = vhost16_to_cpu(vq, ring_head);

	/* If their number is silly, that's an error. */
	if (unlikely(head >= vq->num)) {
		vq_err(vq, "Guest says index %u > %u is available",
		       head, vq->num);
		return -EINVAL;
	}

	/* When we start there are none of either input nor output. */
	*out_num = *in_num = 0;
	if (unlikely(log))
		*log_num = 0;

	i = head;
	do {
		unsigned iov_count = *in_num + *out_num;
		if (unlikely(i >= vq->num)) {
			vq_err(vq, "Desc index is %u > %u, head = %u",
			       i, vq->num, head);
			return -EINVAL;
		}
		if (unlikely(++found > vq->num)) {
			vq_err(vq, "Loop detected: last one at %u "
			       "vq size %u head %u\n",
			       i, vq->num, head);
			return -EINVAL;
		}
		ret = vhost_get_desc(vq, &desc, i);
		if (unlikely(ret)) {
			vq_err(vq, "Failed to get descriptor: idx %d addr %p\n",
			       i, vq->desc + i);
			return -EFAULT;
		}
		if (desc.flags & cpu_to_vhost16(vq, VRING_DESC_F_INDIRECT)) {
			ret = get_indirect(vq, iov, iov_size,
					   out_num, in_num,
					   log, log_num, &desc);
			if (unlikely(ret < 0)) {
				if (ret != -EAGAIN)
					vq_err(vq, "Failure detected "
						"in indirect descriptor at idx %d\n", i);
				return ret;
			}
			continue;
		}

		if (desc.flags & cpu_to_vhost16(vq, VRING_DESC_F_WRITE))
			access = VHOST_ACCESS_WO;
		else
			access = VHOST_ACCESS_RO;
		ret = translate_desc(vq, vhost64_to_cpu(vq, desc.addr),
				     vhost32_to_cpu(vq, desc.len), iov + iov_count,
				     iov_size - iov_count, access);
		if (unlikely(ret < 0)) {
			if (ret != -EAGAIN)
				vq_err(vq, "Translation failure %d descriptor idx %d\n",
					ret, i);
			return ret;
		}
		if (access == VHOST_ACCESS_WO) {
			/* If this is an input descriptor,
			 * increment that count. */
			*in_num += ret;
			if (unlikely(log && ret)) {
				log[*log_num].addr = vhost64_to_cpu(vq, desc.addr);
				log[*log_num].len = vhost32_to_cpu(vq, desc.len);
				++*log_num;
			}
		} else {
			/* If it's an output descriptor, they're all supposed
			 * to come before any input descriptors. */
			if (unlikely(*in_num)) {
				vq_err(vq, "Descriptor has out after in: "
				       "idx %d\n", i);
				return -EINVAL;
			}
			*out_num += ret;
		}
	} while ((i = next_desc(vq, &desc)) != -1);

	/* On success, increment avail index. */
	vq->last_avail_idx++;

	/* Assume notifications from guest are disabled at this point,
	 * if they aren't we would need to update avail_event index. */
	BUG_ON(!(vq->used_flags & VRING_USED_F_NO_NOTIFY));
	return head;
}
EXPORT_SYMBOL_GPL(vhost_get_vq_desc);

/* Reverse the effect of vhost_get_vq_desc. Useful for error handling. */
void vhost_discard_vq_desc(struct vhost_virtqueue *vq, int n)
{
	vq->last_avail_idx -= n;
}
EXPORT_SYMBOL_GPL(vhost_discard_vq_desc);

/* After we've used one of their buffers, we tell them about it.  We'll then
 * want to notify the guest, using eventfd. */
int vhost_add_used(struct vhost_virtqueue *vq, unsigned int head, int len)
{
	struct vring_used_elem heads = {
		cpu_to_vhost32(vq, head),
		cpu_to_vhost32(vq, len)
	};

	return vhost_add_used_n(vq, &heads, 1);
}
EXPORT_SYMBOL_GPL(vhost_add_used);

static int __vhost_add_used_n(struct vhost_virtqueue *vq,
			    struct vring_used_elem *heads,
			    unsigned count)
{
	vring_used_elem_t __user *used;
	u16 old, new;
	int start;

	start = vq->last_used_idx & (vq->num - 1);
	used = vq->used->ring + start;
	if (vhost_put_used(vq, heads, start, count)) {
		vq_err(vq, "Failed to write used");
		return -EFAULT;
	}
	if (unlikely(vq->log_used)) {
		/* Make sure data is seen before log. */
		smp_wmb();
		/* Log used ring entry write. */
		log_used(vq, ((void __user *)used - (void __user *)vq->used),
			 count * sizeof *used);
	}
	old = vq->last_used_idx;
	new = (vq->last_used_idx += count);
	/* If the driver never bothers to signal in a very long while,
	 * used index might wrap around. If that happens, invalidate
	 * signalled_used index we stored. TODO: make sure driver
	 * signals at least once in 2^16 and remove this. */
	if (unlikely((u16)(new - vq->signalled_used) < (u16)(new - old)))
		vq->signalled_used_valid = false;
	return 0;
}

/* After we've used one of their buffers, we tell them about it.  We'll then
 * want to notify the guest, using eventfd. */
int vhost_add_used_n(struct vhost_virtqueue *vq, struct vring_used_elem *heads,
		     unsigned count)
{
	int start, n, r;

	start = vq->last_used_idx & (vq->num - 1);
	n = vq->num - start;
	if (n < count) {
		r = __vhost_add_used_n(vq, heads, n);
		if (r < 0)
			return r;
		heads += n;
		count -= n;
	}
	r = __vhost_add_used_n(vq, heads, count);

	/* Make sure buffer is written before we update index. */
	smp_wmb();
	if (vhost_put_used_idx(vq)) {
		vq_err(vq, "Failed to increment used idx");
		return -EFAULT;
	}
	if (unlikely(vq->log_used)) {
		/* Make sure used idx is seen before log. */
		smp_wmb();
		/* Log used index update. */
		log_used(vq, offsetof(struct vring_used, idx),
			 sizeof vq->used->idx);
		if (vq->log_ctx)
			eventfd_signal(vq->log_ctx, 1);
	}
	return r;
}
EXPORT_SYMBOL_GPL(vhost_add_used_n);

static bool vhost_notify(struct vhost_dev *dev, struct vhost_virtqueue *vq)
{
	__u16 old, new;
	__virtio16 event;
	bool v;
	/* Flush out used index updates. This is paired
	 * with the barrier that the Guest executes when enabling
	 * interrupts. */
	smp_mb();

	if (vhost_has_feature(vq, VIRTIO_F_NOTIFY_ON_EMPTY) &&
	    unlikely(vq->avail_idx == vq->last_avail_idx))
		return true;

	if (!vhost_has_feature(vq, VIRTIO_RING_F_EVENT_IDX)) {
		__virtio16 flags;
		if (vhost_get_avail_flags(vq, &flags)) {
			vq_err(vq, "Failed to get flags");
			return true;
		}
		return !(flags & cpu_to_vhost16(vq, VRING_AVAIL_F_NO_INTERRUPT));
	}
	old = vq->signalled_used;
	v = vq->signalled_used_valid;
	new = vq->signalled_used = vq->last_used_idx;
	vq->signalled_used_valid = true;

	if (unlikely(!v))
		return true;

	if (vhost_get_used_event(vq, &event)) {
		vq_err(vq, "Failed to get used event idx");
		return true;
	}
	return vring_need_event(vhost16_to_cpu(vq, event), new, old);
}

/* This actually signals the guest, using eventfd. */
void vhost_signal(struct vhost_dev *dev, struct vhost_virtqueue *vq)
{
	/* Signal the Guest tell them we used something up. */
	if (vq->call_ctx.ctx && vhost_notify(dev, vq))
		eventfd_signal(vq->call_ctx.ctx, 1);
}
EXPORT_SYMBOL_GPL(vhost_signal);

/* And here's the combo meal deal.  Supersize me! */
void vhost_add_used_and_signal(struct vhost_dev *dev,
			       struct vhost_virtqueue *vq,
			       unsigned int head, int len)
{
	vhost_add_used(vq, head, len);
	vhost_signal(dev, vq);
}
EXPORT_SYMBOL_GPL(vhost_add_used_and_signal);

/* multi-buffer version of vhost_add_used_and_signal */
void vhost_add_used_and_signal_n(struct vhost_dev *dev,
				 struct vhost_virtqueue *vq,
				 struct vring_used_elem *heads, unsigned count)
{
	vhost_add_used_n(vq, heads, count);
	vhost_signal(dev, vq);
}
EXPORT_SYMBOL_GPL(vhost_add_used_and_signal_n);

/* return true if we're sure that avaiable ring is empty */
bool vhost_vq_avail_empty(struct vhost_dev *dev, struct vhost_virtqueue *vq)
{
	__virtio16 avail_idx;
	int r;

	if (vq->avail_idx != vq->last_avail_idx)
		return false;

	r = vhost_get_avail_idx(vq, &avail_idx);
	if (unlikely(r))
		return false;
	vq->avail_idx = vhost16_to_cpu(vq, avail_idx);

	return vq->avail_idx == vq->last_avail_idx;
}
EXPORT_SYMBOL_GPL(vhost_vq_avail_empty);

/* OK, now we need to know about added descriptors. */
bool vhost_enable_notify(struct vhost_dev *dev, struct vhost_virtqueue *vq)
{
	__virtio16 avail_idx;
	int r;

	if (!(vq->used_flags & VRING_USED_F_NO_NOTIFY))
		return false;
	vq->used_flags &= ~VRING_USED_F_NO_NOTIFY;
	if (!vhost_has_feature(vq, VIRTIO_RING_F_EVENT_IDX)) {
		r = vhost_update_used_flags(vq);
		if (r) {
			vq_err(vq, "Failed to enable notification at %p: %d\n",
			       &vq->used->flags, r);
			return false;
		}
	} else {
		r = vhost_update_avail_event(vq);
		if (r) {
			vq_err(vq, "Failed to update avail event index at %p: %d\n",
			       vhost_avail_event(vq), r);
			return false;
		}
	}
	/* They could have slipped one in as we were doing that: make
	 * sure it's written, then check again. */
	smp_mb();
	r = vhost_get_avail_idx(vq, &avail_idx);
	if (r) {
		vq_err(vq, "Failed to check avail idx at %p: %d\n",
		       &vq->avail->idx, r);
		return false;
	}
	vq->avail_idx = vhost16_to_cpu(vq, avail_idx);

	return vq->avail_idx != vq->last_avail_idx;
}
EXPORT_SYMBOL_GPL(vhost_enable_notify);

/* We don't need to be notified again. */
void vhost_disable_notify(struct vhost_dev *dev, struct vhost_virtqueue *vq)
{
	int r;

	if (vq->used_flags & VRING_USED_F_NO_NOTIFY)
		return;
	vq->used_flags |= VRING_USED_F_NO_NOTIFY;
	if (!vhost_has_feature(vq, VIRTIO_RING_F_EVENT_IDX)) {
		r = vhost_update_used_flags(vq);
		if (r)
			vq_err(vq, "Failed to disable notification at %p: %d\n",
			       &vq->used->flags, r);
	}
}
EXPORT_SYMBOL_GPL(vhost_disable_notify);

/* Create a new message. */
struct vhost_msg_node *vhost_new_msg(struct vhost_virtqueue *vq, int type)
{
	/* Make sure all padding within the structure is initialized. */
	struct vhost_msg_node *node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return NULL;

	node->vq = vq;
	node->msg.type = type;
	return node;
}
EXPORT_SYMBOL_GPL(vhost_new_msg);

void vhost_enqueue_msg(struct vhost_dev *dev, struct list_head *head,
		       struct vhost_msg_node *node)
{
	spin_lock(&dev->iotlb_lock);
	list_add_tail(&node->node, head);
	spin_unlock(&dev->iotlb_lock);

	wake_up_interruptible_poll(&dev->wait, EPOLLIN | EPOLLRDNORM);
}
EXPORT_SYMBOL_GPL(vhost_enqueue_msg);

struct vhost_msg_node *vhost_dequeue_msg(struct vhost_dev *dev,
					 struct list_head *head)
{
	struct vhost_msg_node *node = NULL;

	spin_lock(&dev->iotlb_lock);
	if (!list_empty(head)) {
		node = list_first_entry(head, struct vhost_msg_node,
					node);
		list_del(&node->node);
	}
	spin_unlock(&dev->iotlb_lock);

	return node;
}
EXPORT_SYMBOL_GPL(vhost_dequeue_msg);

void vhost_set_backend_features(struct vhost_dev *dev, u64 features)
{
	struct vhost_virtqueue *vq;
	int i;

	mutex_lock(&dev->mutex);
	for (i = 0; i < dev->nvqs; ++i) {
		vq = dev->vqs[i];
		mutex_lock(&vq->mutex);
		vq->acked_backend_features = features;
		mutex_unlock(&vq->mutex);
	}
	mutex_unlock(&dev->mutex);
}
EXPORT_SYMBOL_GPL(vhost_set_backend_features);

static int __init vhost_init(void)
{
	return 0;
}

static void __exit vhost_exit(void)
{
}

module_init(vhost_init);
module_exit(vhost_exit);

MODULE_VERSION("0.0.1");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Michael S. Tsirkin");
MODULE_DESCRIPTION("Host kernel accelerator for virtio");
