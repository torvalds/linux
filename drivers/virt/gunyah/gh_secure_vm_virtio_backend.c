// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/dma-direct.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_fdt.h>
#include <linux/of_reserved_mem.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/file.h>
#include <linux/workqueue.h>
#include <linux/eventfd.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/gunyah_deprecated.h>
#include <linux/of_irq.h>
#include <uapi/linux/virtio_mmio.h>
#include <linux/gunyah/gh_rm_drv.h>
#include <linux/pgtable.h>
#include <soc/qcom/secure_buffer.h>
#include "gh_secure_vm_virtio_backend.h"
#include "hcall_virtio.h"
#include <linux/qcom_scm.h>

#define CREATE_TRACE_POINTS
#include <trace/events/gh_virtio_backend.h>
#undef CREATE_TRACE_POINTS

#define MAX_QUEUES		4
#define MAX_IO_CONTEXTS		MAX_QUEUES

#define VIRTIO_PRINT_MARKER	"gh_virtio_backend"

#define assert_virq		gh_hcall_virtio_mmio_backend_assert_virq
#define set_dev_features	gh_hcall_virtio_mmio_backend_set_dev_features
#define set_queue_num_max	gh_hcall_virtio_mmio_backend_set_queue_num_max
#define get_drv_features	gh_hcall_virtio_mmio_backend_get_drv_features
#define get_queue_info		gh_hcall_virtio_mmio_backend_get_queue_info
#define get_event		gh_hcall_virtio_mmio_backend_get_event
#define ack_reset		gh_hcall_virtio_mmio_backend_ack_reset

static DEFINE_MUTEX(vm_mutex);
static DEFINE_SPINLOCK(vm_list_lock);
static LIST_HEAD(vm_list);

enum {
	VM_STATE_NOT_ACTIVE,
	VM_STATE_ACTIVE,
};

struct shared_memory {
	struct resource r;
	u32 size;
	u32 gunyah_label, shm_memparcel;
	void *base;
};

struct virt_machine {
	struct list_head list;
	char vm_name[GH_VM_FW_NAME_MAX];
	int hyp_assign_done;
	struct device *dev;
	struct shared_memory *shmem;
	int shmem_entries;
	spinlock_t vb_dev_lock;
	struct list_head vb_dev_list;
	phys_addr_t shmem_addr;
	u64 shmem_size;
	struct device *com_mem_dev;
	bool is_static;
};

struct ioevent_context {
	int fd;
	struct eventfd_ctx *ctx;
};

struct irq_context {
	struct eventfd_ctx *ctx;
	wait_queue_entry_t wait;
	poll_table pt;
	struct fd fd;
	struct work_struct shutdown_work;
};

struct virtio_backend_device {
	struct list_head list;
	spinlock_t lock;
	wait_queue_head_t evt_queue;
	wait_queue_head_t notify_queue;
	u32 refcount;
	int notify;
	int ack_driver_ok;
	int evt_avail;
	u64 cur_event_data, vdev_event_data;
	u64 cur_event, vdev_event;
	int linux_irq;
	u32 label;
	struct device *dev;
	struct virt_machine *vm;
	struct ioevent_context ioctx[MAX_IO_CONTEXTS];
	struct irq_context irq;
	char int_name[32];
	u32 features[2];
	u32 queue_num_max[MAX_QUEUES];
	struct mutex mutex;
	gh_capid_t cap_id;
	/* Backend program supplied config data */
	char *config_data;
	u32 config_size;
	/* Page shared with frontend */
	char __iomem *config_shared_buf;
	u64  config_shared_size;
};

static struct virt_machine *find_vm_by_name(const char *vm_name)
{
	struct virt_machine *v = NULL, *tmp;

	spin_lock(&vm_list_lock);
	list_for_each_entry(tmp, &vm_list, list) {
		if (!strcmp(tmp->vm_name, vm_name)) {
			v = tmp;
			break;
		}
	}
	spin_unlock(&vm_list_lock);

	return v;
}

static struct virtio_backend_device *
vb_dev_get(struct virt_machine *vm, u32 label)
{
	struct virtio_backend_device *vb_dev = NULL, *tmp;

	spin_lock(&vm->vb_dev_lock);
	list_for_each_entry(tmp, &vm->vb_dev_list, list) {
		if (label == tmp->label) {
			if (tmp->refcount < U32_MAX)
				vb_dev = tmp;
			break;
		}
	}
	if (vb_dev)
		vb_dev->refcount++;
	spin_unlock(&vm->vb_dev_lock);

	return vb_dev;
}

static void vb_dev_put(struct virtio_backend_device *vb_dev)
{
	struct virt_machine *vm = vb_dev->vm;

	spin_lock(&vm->vb_dev_lock);
	vb_dev->refcount--;
	if (!vb_dev->refcount && vb_dev->notify)
		wake_up(&vb_dev->notify_queue);
	spin_unlock(&vm->vb_dev_lock);
}

static void
irqfd_shutdown(struct work_struct *work)
{
	struct irq_context *irq = container_of(work,
				struct irq_context, shutdown_work);
	struct virtio_backend_device *vb_dev;
	unsigned long iflags;
	u64 isr;

	vb_dev = container_of(irq, struct virtio_backend_device, irq);

	spin_lock_irqsave(&vb_dev->lock, iflags);
	if (irq->ctx) {
		eventfd_ctx_remove_wait_queue(irq->ctx, &irq->wait, &isr);
		eventfd_ctx_put(irq->ctx);
		fdput(irq->fd);
		irq->ctx = NULL;
		irq->fd.file = NULL;
	}
	spin_unlock_irqrestore(&vb_dev->lock, iflags);
}

static int vb_dev_irqfd_wakeup(wait_queue_entry_t *wait, unsigned int mode,
							int sync, void *key)
{
	struct irq_context *irq = container_of(wait, struct irq_context, wait);
	struct virtio_backend_device *vb_dev;
	__poll_t flags = key_to_poll(key);

	vb_dev = container_of(irq, struct virtio_backend_device, irq);

	if (flags & EPOLLIN) {
		int rc = assert_virq(vb_dev->cap_id, 1);

		trace_gh_virtio_backend_irq_inj(vb_dev->label, rc);
	}

	if (flags & EPOLLHUP)
		queue_work(system_wq, &irq->shutdown_work);

	return 0;
}

static void vb_dev_irqfd_ptable_queue_proc(struct file *file,
			wait_queue_head_t *wqh,	poll_table *pt)
{
	struct irq_context *irq = container_of(pt, struct irq_context, pt);

	add_wait_queue(wqh, &irq->wait);
}

static int vb_dev_irqfd(struct virtio_backend_device *vb_dev,
		struct virtio_irqfd *ifd)
{
	struct fd f;
	int ret = -EBUSY;
	struct eventfd_ctx *eventfd = NULL;
	__poll_t events;
	unsigned long flags;

	f.file = NULL;

	spin_lock_irqsave(&vb_dev->lock, flags);

	if (vb_dev->irq.fd.file)
		goto fail;

	f = fdget(ifd->fd);
	if (!f.file)
		goto fail;

	eventfd = eventfd_ctx_fileget(f.file);
	if (IS_ERR(eventfd)) {
		ret = PTR_ERR(eventfd);
		goto fail;
	}
	vb_dev->irq.ctx = eventfd;
	vb_dev->irq.fd = f;

	spin_unlock_irqrestore(&vb_dev->lock, flags);

	init_waitqueue_func_entry(&vb_dev->irq.wait, vb_dev_irqfd_wakeup);
	INIT_WORK(&vb_dev->irq.shutdown_work, irqfd_shutdown);
	init_poll_funcptr(&vb_dev->irq.pt, vb_dev_irqfd_ptable_queue_proc);
	events = vfs_poll(f.file, &vb_dev->irq.pt);
	if (events & EPOLLIN) {
		dev_dbg(vb_dev->vm->dev,
			"%s: Premature injection of interrupt\n",
			VIRTIO_PRINT_MARKER);
	}

	return 0;

fail:
	if (eventfd && !IS_ERR(eventfd))
		eventfd_ctx_put(eventfd);
	if (f.file)
		fdput(f);

	spin_unlock_irqrestore(&vb_dev->lock, flags);

	return ret;
}

static int vb_dev_ioeventfd(struct virtio_backend_device *vb_dev,
				struct virtio_eventfd *efd)
{
	struct eventfd_ctx *ctx = NULL;
	int i = efd->queue_num;
	unsigned long flags;
	int ret = -EBUSY;

	if (efd->fd <= 0)
		return -EINVAL;

	spin_lock_irqsave(&vb_dev->lock, flags);
	if (vb_dev->ioctx[i].fd > 0)
		goto out;

	ctx = eventfd_ctx_fdget(efd->fd);
	if (IS_ERR(ctx)) {
		ret = PTR_ERR(ctx);
		goto out;
	}

	vb_dev->ioctx[i].ctx = ctx;
	vb_dev->ioctx[i].fd = efd->fd;

	ret = 0;

out:
	spin_unlock_irqrestore(&vb_dev->lock, flags);

	return ret;
}

static void signal_vqs(struct virtio_backend_device *vb_dev)
{
	int i;
	u64 flags;

	for (i = 0; i < MAX_IO_CONTEXTS; ++i) {
		flags = 1 << i;
		if ((vb_dev->vdev_event_data & flags) && vb_dev->ioctx[i].ctx) {
			eventfd_signal(vb_dev->ioctx[i].ctx, 1);
			vb_dev->vdev_event_data &= ~flags;
			trace_gh_virtio_backend_queue_notify(vb_dev->label, i);
		}
	}
}

long gh_virtio_backend_ioctl(const char *vm_name, unsigned int cmd,
							unsigned long arg)
{
	struct virt_machine *vm = find_vm_by_name(vm_name);
	struct virtio_backend_device *vb_dev;
	void __user *argp = (void __user *)arg;
	struct virtio_eventfd efd;
	struct virtio_irqfd ifd;
	struct virtio_dev_features f;
	struct virtio_queue_max q;
	struct virtio_ack_reset r;
	struct virtio_config_data d;
	struct virtio_queue_info qi;
	struct gh_hcall_virtio_queue_info qinfo;
	struct virtio_driver_features df;
	struct virtio_event ve;
	u64 features;
	u32 label;
	int ret = 0, i, nr_words;
	unsigned long flags;
	u64 org_event, org_data;
	u32 *p, *x, *ack_reg;

	if (!vm)
		return -EINVAL;

	switch (cmd) {
	case GH_GET_SHARED_MEMORY_SIZE:
		if (copy_to_user(argp, &vm->shmem_size,
					sizeof(vm->shmem_size)))
			return -EFAULT;
		break;

	case GH_IOEVENTFD:
		if (copy_from_user(&efd, argp, sizeof(efd)))
			return -EFAULT;

		if (efd.queue_num >= MAX_IO_CONTEXTS)
			return -EINVAL;

		if (efd.flags != VBE_ASSIGN_IOEVENTFD)
			return -EOPNOTSUPP;

		vb_dev = vb_dev_get(vm, efd.label);
		if (!vb_dev)
			return -EINVAL;

		ret = vb_dev_ioeventfd(vb_dev, &efd);

		vb_dev_put(vb_dev);

		return ret;

	case GH_IRQFD:
		if (copy_from_user(&ifd, argp, sizeof(ifd)))
			return -EFAULT;

		if (!ifd.label || ifd.fd <= 0)
			return -EINVAL;

		if (ifd.flags != VBE_ASSIGN_IRQFD)
			return -EOPNOTSUPP;

		vb_dev = vb_dev_get(vm, ifd.label);
		if (!vb_dev)
			return -EINVAL;

		ret = vb_dev_irqfd(vb_dev, &ifd);

		vb_dev_put(vb_dev);

		return ret;

	case GH_WAIT_FOR_EVENT:
		if (copy_from_user(&ve, argp, sizeof(ve)))
			return -EFAULT;

		if (!ve.label)
			return -EINVAL;

		vb_dev = vb_dev_get(vm, ve.label);
		if (!vb_dev)
			return -EINVAL;

loop_back:
		if (!vb_dev->evt_avail) {
			ret = wait_event_interruptible(vb_dev->evt_queue,
					vb_dev->evt_avail);
			if (ret) {
				vb_dev_put(vb_dev);
				return ret;
			}
		}

		spin_lock_irqsave(&vb_dev->lock, flags);

		vb_dev->cur_event = 0;
		vb_dev->cur_event_data = 0;
		vb_dev->evt_avail = 0;
		ack_reg = (u32 *)(vb_dev->config_shared_buf + VIRTIO_MMIO_INTERRUPT_ACK);

		org_event = vb_dev->vdev_event;
		org_data = vb_dev->vdev_event_data;

		if (vb_dev->vdev_event & (EVENT_MODULE_EXIT | EVENT_VM_EXIT))
			vb_dev->cur_event = EVENT_APP_EXIT;
		else if (vb_dev->vdev_event & EVENT_RESET_RQST) {
			vb_dev->vdev_event &= ~EVENT_RESET_RQST;
			vb_dev->cur_event = EVENT_RESET_RQST;
			if (vb_dev->vdev_event)
				vb_dev->evt_avail = 1;
		} else if (vb_dev->vdev_event & EVENT_DRIVER_OK) {
			vb_dev->vdev_event &= ~EVENT_DRIVER_OK;
			if (!vb_dev->ack_driver_ok) {
				vb_dev->cur_event = EVENT_DRIVER_OK;
				if (vb_dev->vdev_event)
					vb_dev->evt_avail = 1;
			}
		} else if (vb_dev->vdev_event & EVENT_NEW_BUFFER) {
			vb_dev->vdev_event &= ~EVENT_NEW_BUFFER;
			if (vb_dev->vdev_event_data && vb_dev->ack_driver_ok)
				signal_vqs(vb_dev);
			if (vb_dev->vdev_event) {
				vb_dev->cur_event = vb_dev->vdev_event;
				vb_dev->vdev_event = 0;
				vb_dev->cur_event_data = vb_dev->vdev_event_data;
				vb_dev->vdev_event_data = 0;
				if (vb_dev->cur_event & EVENT_INTERRUPT_ACK)
					vb_dev->cur_event_data = readl_relaxed(ack_reg);
			}
		} else if (vb_dev->vdev_event & EVENT_INTERRUPT_ACK) {
			vb_dev->vdev_event &= ~EVENT_INTERRUPT_ACK;
			vb_dev->vdev_event_data = 0;
			vb_dev->cur_event = EVENT_INTERRUPT_ACK;
			vb_dev->cur_event_data = readl_relaxed(ack_reg);
		}

		spin_unlock_irqrestore(&vb_dev->lock, flags);

		trace_gh_virtio_backend_wait_event(vb_dev->label, vb_dev->cur_event,
				org_event, vb_dev->cur_event_data, org_data);

		if (!vb_dev->cur_event)
			goto loop_back;

		ve.event = (u32) vb_dev->cur_event;
		ve.event_data = (u32) vb_dev->cur_event_data;

		vb_dev_put(vb_dev);

		if (copy_to_user(argp, &ve, sizeof(ve)))
			return -EFAULT;

		break;

	case GH_GET_DRIVER_FEATURES:
		if (copy_from_user(&df, argp, sizeof(df)))
			return -EFAULT;

		if (!df.label || df.features_sel > 1)
			return -EINVAL;

		vb_dev = vb_dev_get(vm, df.label);
		if (!vb_dev)
			return -EINVAL;

		if (!vb_dev->cap_id) {
			vb_dev_put(vb_dev);
			return -EINVAL;
		}

		ret = get_drv_features(vb_dev->cap_id, df.features_sel, &features);

		vb_dev_put(vb_dev);

		dev_dbg(vm->dev, "%s: get_drv_feat %d/%x ret %d\n",
				VIRTIO_PRINT_MARKER, df.features_sel, features, ret);
		if (ret)
			return ret;

		df.features = (u32) features;
		if (copy_to_user(argp, &df, sizeof(df)))
			return -EFAULT;

		break;

	case GH_GET_QUEUE_INFO:
		if (copy_from_user(&qi, argp, sizeof(qi)))
			return -EFAULT;

		if (!qi.label || qi.queue_sel >= MAX_QUEUES)
			return -EINVAL;

		vb_dev = vb_dev_get(vm, qi.label);
		if (!vb_dev)
			return -EINVAL;

		if (!vb_dev->cap_id) {
			vb_dev_put(vb_dev);
			return -EINVAL;
		}

		ret = get_queue_info(vb_dev->cap_id, qi.queue_sel, &qinfo);
		vb_dev_put(vb_dev);

		dev_dbg(vm->dev, "%s: get_queue_info %d: que_num %d que_ready %d que_desc %llx\n",
			VIRTIO_PRINT_MARKER, qi.queue_sel, qinfo.queue_num,
			qinfo.queue_ready, qinfo.queue_desc);
		dev_dbg(vm->dev, "%s: que_driver %llx que_device %llx ret %d\n",
			VIRTIO_PRINT_MARKER, qinfo.queue_driver, qinfo.queue_device, ret);

		if (ret)
			return ret;

		qi.queue_num = (u32) qinfo.queue_num;
		qi.queue_ready = (u32) qinfo.queue_ready;
		qi.queue_desc = qinfo.queue_desc;
		qi.queue_driver = qinfo.queue_driver;
		qi.queue_device = qinfo.queue_device;

		if (copy_to_user(argp, &qi, sizeof(qi)))
			return -EFAULT;

		break;

	case GH_ACK_DRIVER_OK:
		label = (u32) arg;

		if (!label)
			return -EINVAL;

		vb_dev = vb_dev_get(vm, label);
		if (!vb_dev)
			return -EINVAL;

		vb_dev->ack_driver_ok = 1;
		vb_dev_put(vb_dev);
		dev_dbg(vm->dev, "%s: ack_driver_ok for label %x!\n", VIRTIO_PRINT_MARKER, label);

		break;

	case GH_ACK_RESET:
		if (copy_from_user(&r, argp, sizeof(r)))
			return -EFAULT;

		if (!r.label)
			return -EINVAL;

		vb_dev = vb_dev_get(vm, r.label);
		if (!vb_dev || !vb_dev->cap_id)
			return -EINVAL;

		ack_reset(vb_dev->cap_id);
		vb_dev_put(vb_dev);

		dev_dbg(vm->dev, "%s: ack_reset for label %x!\n", VIRTIO_PRINT_MARKER, r.label);
		break;

	case GH_SET_DEVICE_FEATURES:
		if (copy_from_user(&f, argp, sizeof(f)))
			return -EFAULT;

		if (!f.label || f.features_sel > 1)
			return -EINVAL;

		vb_dev = vb_dev_get(vm, f.label);
		if (!vb_dev)
			return -EINVAL;

		vb_dev->features[f.features_sel] = f.features;

		ret = set_dev_features(vb_dev->cap_id, f.features_sel,
				vb_dev->features[f.features_sel]);
		if (ret) {
			vb_dev_put(vb_dev);
			dev_err(vm->dev, "%s: set_features %d/%x failed ret %d\n",
					VIRTIO_PRINT_MARKER, f.features_sel,
					vb_dev->features[f.features_sel], ret);
			return ret;
		}

		vb_dev_put(vb_dev);

		dev_dbg(vm->dev, "%s: label %d features %d %x\n", VIRTIO_PRINT_MARKER,
				f.label, f.features_sel, f.features);
		break;

	case GH_SET_QUEUE_NUM_MAX:
		if (copy_from_user(&q, argp, sizeof(q)))
			return -EFAULT;

		if (!q.label || q.queue_sel >= MAX_QUEUES)
			return -EINVAL;

		vb_dev = vb_dev_get(vm, q.label);
		if (!vb_dev)
			return -EINVAL;

		vb_dev->queue_num_max[q.queue_sel] = q.queue_num_max;

		ret = set_queue_num_max(vb_dev->cap_id, q.queue_sel,
				vb_dev->queue_num_max[q.queue_sel]);
		if (ret) {
			vb_dev_put(vb_dev);
			dev_err(vm->dev, "%s: set_queue_num_max %d/%x failed ret %d\n",
					VIRTIO_PRINT_MARKER, q.queue_sel,
					vb_dev->queue_num_max[q.queue_sel],
					ret);
			return ret;
		}

		vb_dev_put(vb_dev);

		dev_dbg(vm->dev, "%s: label %d queue_max %d %x\n", VIRTIO_PRINT_MARKER,
				q.label, q.queue_sel, q.queue_num_max);

		break;

	case GH_GET_DRIVER_CONFIG_DATA:
		if (copy_from_user(&d, argp, sizeof(d)))
			return -EFAULT;

		vb_dev = vb_dev_get(vm, d.label);
		if (!vb_dev)
			return -EINVAL;

		mutex_lock(&vb_dev->mutex);
		if (!d.label || d.config_size > vb_dev->config_shared_size ||
			!d.config_size || !d.config_data) {
			mutex_unlock(&vb_dev->mutex);
			vb_dev_put(vb_dev);
			return -EINVAL;
		}

		if (!vb_dev->config_shared_buf) {
			mutex_unlock(&vb_dev->mutex);
			vb_dev_put(vb_dev);
			return -EINVAL;
		}
		ret = copy_to_user((char __user *)d.config_data,
			vb_dev->config_shared_buf, d.config_size);
		mutex_unlock(&vb_dev->mutex);
		vb_dev_put(vb_dev);
		return ret;

	case GH_SET_DEVICE_CONFIG_DATA:
		if (copy_from_user(&d, argp, sizeof(d)))
			return -EFAULT;

		vb_dev = vb_dev_get(vm, d.label);
		if (!vb_dev)
			return -EINVAL;

		mutex_lock(&vb_dev->mutex);
		if (!d.label || d.config_size > vb_dev->config_shared_size ||
			!d.config_size || !d.config_data) {
			mutex_unlock(&vb_dev->mutex);
			vb_dev_put(vb_dev);
			return -EINVAL;
		}

		if (vb_dev->config_data) {
			mutex_unlock(&vb_dev->mutex);
			vb_dev_put(vb_dev);
			return -EBUSY;
		}

		if (!vb_dev->config_shared_buf) {
			mutex_unlock(&vb_dev->mutex);
			vb_dev_put(vb_dev);
			return -EINVAL;
		}

		vb_dev->config_data = (char *)__get_free_pages(
					GFP_KERNEL | __GFP_ZERO, 0);
		if (!vb_dev->config_data) {
			mutex_unlock(&vb_dev->mutex);
			vb_dev_put(vb_dev);
			return -ENOMEM;
		}

		if (copy_from_user(vb_dev->config_data,
				u64_to_user_ptr(d.config_data), d.config_size)) {
			free_pages((unsigned long)vb_dev->config_data, 0);
			vb_dev->config_data = NULL;
			mutex_unlock(&vb_dev->mutex);
			vb_dev_put(vb_dev);
			return -EFAULT;
		}
		vb_dev->config_size = d.config_size;
		p = (u32 *)vb_dev->config_data;
		for (i = 0; i < 4; ++i)
			dev_dbg(vm->dev, "%s: %x: %x\n", VIRTIO_PRINT_MARKER, i*4, p[i]);

		p = (u32 *)vb_dev->config_shared_buf;
		x = (u32 *)vb_dev->config_data;
		nr_words = vb_dev->config_size / 4;

		for (i = 0; i < nr_words; ++i)
			writel_relaxed(*x++, p++);

		p = (u32 *)vb_dev->config_shared_buf;
		for (i = 0; i < nr_words; ++i) {
			dev_dbg(vm->dev, "%s: config_word %d %x\n",
					VIRTIO_PRINT_MARKER, i,
					readl_relaxed(p++));
		}

		mutex_unlock(&vb_dev->mutex);
		vb_dev_put(vb_dev);
		break;

	default:
		dev_err(vm->dev, "%s: cmd %x not supported\n", VIRTIO_PRINT_MARKER, cmd);
		return -EINVAL;
	}

	return 0;
}

int gh_virtio_backend_mmap(const char *vm_name,
				struct vm_area_struct *vma)
{
	struct virt_machine *vm = find_vm_by_name(vm_name);
	size_t mmap_size;
	u64 offset = 0;
	int i;

	if (!vm)
		return -EINVAL;

	mmap_size = vma->vm_end - vma->vm_start;
	if (mmap_size != vm->shmem_size)
		return -EINVAL;

	vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP);

	if (vm->is_static) {
		if (io_remap_pfn_range(vma, vma->vm_start,
				__phys_to_pfn(vm->shmem_addr),
				mmap_size, vma->vm_page_prot)) {
			dev_err(vm->dev, "%s: ioremap_pfn_range failed\n", VIRTIO_PRINT_MARKER);
			return -EAGAIN;
		}
	} else {
		for (i = 0; i < vm->shmem_entries; ++i) {
			if (io_remap_pfn_range(vma, vma->vm_start + offset,
					__phys_to_pfn(vm->shmem[i].r.start),
					vm->shmem[i].size, vma->vm_page_prot)) {
				dev_err(vm->dev, "%s: shmem_entry %d ioremap_pfn_range failed\n",
					VIRTIO_PRINT_MARKER, i);
				return -EAGAIN;
			}

			offset = offset + vm->shmem[i].size;
		}
	}

	return 0;
}

static void init_vb_dev_open(struct virtio_backend_device *vb_dev)
{
	unsigned long flags;

	spin_lock_irqsave(&vb_dev->lock, flags);
	vb_dev->evt_avail = 0;
	vb_dev->cur_event_data = 0;
	vb_dev->vdev_event_data = 0;
	vb_dev->cur_event = 0;
	vb_dev->vdev_event = 0;
	vb_dev->notify = 0;
	spin_unlock_irqrestore(&vb_dev->lock, flags);
}

static int
note_shared_buffers(struct device_node *np, struct virt_machine *vm)
{
	int idx = 0;
	u32 len, nr_entries = 0;

	if (of_find_property(np, "shared-buffers", &len))
		nr_entries = len / 4;

	if (!nr_entries) {
		dev_err(vm->dev, "%s: No shared-buffers specified for vm %s\n",
			VIRTIO_PRINT_MARKER, vm->vm_name);
		return -EINVAL;
	}

	vm->shmem = devm_kzalloc(vm->dev, nr_entries * sizeof(struct shared_memory),
					GFP_KERNEL);
	if (!vm->shmem)
		return -ENOMEM;

	for (idx = 0; idx < nr_entries; ++idx) {
		struct device_node *snp;
		int ret;

		snp = of_parse_phandle(np, "shared-buffers", idx);
		if (!snp) {
			dev_err(vm->dev, "%s: Can't parse shared-buffer property %d\n",
					VIRTIO_PRINT_MARKER, idx);
			return -EINVAL;
		}

		if (of_property_read_bool(snp, "no-map")) {
			vm->is_static = true;
			ret = of_address_to_resource(snp, 0, &vm->shmem[idx].r);
			if (ret) {
				of_node_put(snp);
				dev_err(vm->dev, "%s: Invalid address at index %d\n",
							VIRTIO_PRINT_MARKER, idx);
				return -EINVAL;
			}
		} else {
			vm->is_static = false;
			ret = of_property_read_u32(snp, "size", &vm->shmem[idx].size);
			if (ret) {
				of_node_put(snp);
				dev_err(vm->dev, "%s: Invalid size at index %d\n",
							VIRTIO_PRINT_MARKER, idx);
				return -EINVAL;
			}
		}

		ret = of_property_read_u32(snp, "gunyah-label",
					&vm->shmem[idx].gunyah_label);
		if (ret) {
			of_node_put(snp);
			dev_err(vm->dev, "%s: gunyah-label property absent at index %d\n",
					 VIRTIO_PRINT_MARKER, idx);
			return -EINVAL;
		}

		of_node_put(snp);
	}

	vm->shmem_entries = nr_entries;

	return 0;
}

static void gh_memdev_release(struct device *dev)
{
	of_reserved_mem_device_release(dev);
}

static struct device *gh_alloc_memdev(struct device *dev,
				const char *name, unsigned int idx)
{
	struct device *mem_dev;
	int ret;

	mem_dev = devm_kzalloc(dev, sizeof(*mem_dev), GFP_KERNEL);
	if (!mem_dev)
		return NULL;

	device_initialize(mem_dev);
	dev_set_name(mem_dev, "%s:%s", dev_name(dev), name);
	mem_dev->parent = dev;
	mem_dev->coherent_dma_mask = dev->coherent_dma_mask;
	mem_dev->dma_mask = dev->dma_mask;
	mem_dev->release = gh_memdev_release;
	mem_dev->dma_parms = devm_kzalloc(dev, sizeof(*mem_dev->dma_parms),
									GFP_KERNEL);
	if (!mem_dev->dma_parms)
		goto err;

	/*
	 * The memdevs are not proper OF platform devices, so in order for them
	 * to be treated as valid DMA masters we need a bit of a hack to force
	 * them to inherit the MFC node's DMA configuration.
	 */
	of_dma_configure(mem_dev, dev->of_node, true);

	if (device_add(mem_dev) == 0) {
		ret = of_reserved_mem_device_init_by_idx(mem_dev, dev->of_node,
							 idx);
		if (ret) {
			device_del(mem_dev);
			goto err;
		}

		return mem_dev;
	}

err:
	put_device(mem_dev);
	return NULL;
}

static struct virt_machine *
new_vm(struct device *dev, const char *vm_name, struct device_node *np)
{
	struct virt_machine *vm;
	int ret;
	struct resource r;
	u32 size;

	vm = devm_kzalloc(dev, sizeof(*vm), GFP_KERNEL);
	if (!vm)
		return NULL;

	strscpy(vm->vm_name, vm_name, sizeof(vm->vm_name));
	vm->dev = dev;
	ret = note_shared_buffers(np, vm);
	if (ret)
		return NULL;

	if (vm->is_static) {
		ret = of_address_to_resource(np, 0, &r);
		if (ret || !r.start || !resource_size(&r)) {
			dev_err(vm->dev, "%s: Invalid shared memory for VM %s\n",
						VIRTIO_PRINT_MARKER, vm_name);
			return NULL;
		}

		vm->shmem_addr = r.start;
		vm->shmem_size = resource_size(&r);

		dev_info(vm->dev, "%s: Recognized VM '%s' shmem_addr %pK shmem_size %llx\n",
				VIRTIO_PRINT_MARKER, vm->vm_name,
				(void *)vm->shmem_addr, vm->shmem_size);
	} else {
		vm->com_mem_dev = gh_alloc_memdev(dev, "com_mem", 1);
		if (!vm->com_mem_dev) {
			return NULL;
		}

		ret = of_property_read_u32(np, "shared-buffers-size", &size);
		if (ret) {
			dev_err(vm->dev, "%s: Invalid shared memory size for VM %s\n",
					VIRTIO_PRINT_MARKER, vm_name);
			return NULL;
		}

		vm->shmem_size = size;
		dev_info(vm->dev, "%s: Recognized VM '%s' shmem_size %llx\n",
			VIRTIO_PRINT_MARKER, vm->vm_name, vm->shmem_size);
	}

	spin_lock_init(&vm->vb_dev_lock);
	INIT_LIST_HEAD(&vm->vb_dev_list);

	spin_lock(&vm_list_lock);
	list_add(&vm->list, &vm_list);
	spin_unlock(&vm_list_lock);

	return vm;
}

static int gh_virtio_backend_probe(struct device *dev, struct device_node *np,
					const char *str)
{
	int ret;
	struct device_node *vm_np;
	struct virtio_backend_device *vb_dev = NULL, *tmp;
	struct virt_machine *vm;
	u32 label;

	if (!np || !str || !dev)
		return -EINVAL;

	vm_np = of_parse_phandle(np, "qcom,vm", 0);
	if (!vm_np) {
		dev_err(dev, "%s: 'qcom,vm' property not present\n",
					VIRTIO_PRINT_MARKER);
		return -EINVAL;
	}

	if (strnlen(str, GH_VM_FW_NAME_MAX) == GH_VM_FW_NAME_MAX) {
		dev_err(dev, "%s: VM name %s too long\n", VIRTIO_PRINT_MARKER, str);
		of_node_put(vm_np);
		return -EINVAL;
	}

	ret = of_property_read_u32(np, "qcom,label", &label);
	if (ret || !label) {
		dev_err(dev, "%s: Invalid qcom,label property\n",
						VIRTIO_PRINT_MARKER);
		of_node_put(vm_np);
		return -EINVAL;
	}

	mutex_lock(&vm_mutex);
	vm = find_vm_by_name(str);
	if (!vm) {
		vm = new_vm(dev, str, vm_np);
		if (!vm) {
			of_node_put(vm_np);
			mutex_unlock(&vm_mutex);
			return -ENODEV;
		}
	}

	spin_lock(&vm->vb_dev_lock);
	list_for_each_entry(tmp, &vm->vb_dev_list, list) {
		if (label == tmp->label) {
			vb_dev = tmp;
			break;
		}
	}
	spin_unlock(&vm->vb_dev_lock);

	if (vb_dev) {
		vb_dev_put(vb_dev);
		of_node_put(vm_np);
		mutex_unlock(&vm_mutex);
		dev_err(dev, "%s: Duplicate qcom,label %d\n",
				VIRTIO_PRINT_MARKER, label);
		return -EINVAL;
	}

	vb_dev = devm_kzalloc(dev, sizeof(*vb_dev),
				GFP_KERNEL | __GFP_ZERO);
	if (!vb_dev) {
		of_node_put(vm_np);
		mutex_unlock(&vm_mutex);
		return -ENOMEM;
	}

	vb_dev->label = label;
	vb_dev->vm = vm;
	spin_lock_init(&vb_dev->lock);
	init_waitqueue_head(&vb_dev->evt_queue);
	init_waitqueue_head(&vb_dev->notify_queue);
	mutex_init(&vb_dev->mutex);

	spin_lock(&vm->vb_dev_lock);
	list_add(&vb_dev->list, &vm->vb_dev_list);
	spin_unlock(&vm->vb_dev_lock);

	of_node_put(vm_np);

	snprintf(vb_dev->int_name, sizeof(vb_dev->int_name),
				"virtio_dev_%x", vb_dev->label);

	mutex_unlock(&vm_mutex);

	pr_info("%s: %s: Recognized virtio backend device with label %x\n",
			VIRTIO_PRINT_MARKER, vm->vm_name, vb_dev->label);
	return 0;
}

static void handle_probe_failure(const char *vm_name)
{
	struct virt_machine *vm;

	mutex_lock(&vm_mutex);
	vm = find_vm_by_name(vm_name);
	if (!vm)
		goto done;
	spin_lock(&vm_list_lock);
	list_del(&vm->list);
	spin_unlock(&vm_list_lock);
done:
	mutex_unlock(&vm_mutex);
}

int gh_parse_virtio_properties(struct device *dev, const char *vm_name)
{
	struct device_node *np;
	int idx = 0;
	u32 len, nr_entries = 0;

	if (!dev || !vm_name)
		return -EINVAL;

	np = dev->of_node;
	if (of_find_property(np, "virtio-backends", &len))
		nr_entries = len / 4;
	if (!nr_entries) {
		dev_dbg(dev, "%s: No virtio-backends specified for vm %s\n",
				VIRTIO_PRINT_MARKER, vm_name);
		return 0;
	}
	for (idx = 0; idx < nr_entries; ++idx) {
		struct device_node *snp;
		int ret;

		snp = of_parse_phandle(np, "virtio-backends", idx);
		if (!snp) {
			dev_err(dev, "%s: Can't parse virtio-backends property %d\n",
					VIRTIO_PRINT_MARKER, idx);
			return -EINVAL;
		}
		ret = gh_virtio_backend_probe(dev, snp, vm_name);
		if (ret) {
			of_node_put(snp);
			handle_probe_failure(vm_name);
			return ret;
		}
		of_node_put(snp);

	}
	return 0;
}

int gh_virtio_backend_remove(const char *vm_name)
{
	struct virt_machine *vm;

	vm = find_vm_by_name(vm_name);
	if (!vm) {
		pr_debug("%s: VM name %s not found\n", VIRTIO_PRINT_MARKER, vm_name);
		return 0;
	}
	spin_lock(&vm_list_lock);
	list_del(&vm->list);
	spin_unlock(&vm_list_lock);
	return 0;
}

static irqreturn_t vdev_interrupt(int irq, void *data)
{
	struct virtio_backend_device *vb_dev = data;
	u64 event_data, event;
	int ret;
	unsigned long flags;

	ret = get_event(vb_dev->cap_id, &event_data, &event);
	trace_gh_virtio_backend_irq(vb_dev->label, event, event_data, ret);
	if (ret || !event)
		return IRQ_HANDLED;

	spin_lock_irqsave(&vb_dev->lock, flags);
	if (event == EVENT_NEW_BUFFER && vb_dev->ack_driver_ok) {
		vb_dev->vdev_event_data = event_data;
		signal_vqs(vb_dev);
		goto done;
	}
	vb_dev->vdev_event |= event;
	vb_dev->vdev_event_data |= event_data;
	vb_dev->evt_avail = 1;
	wake_up_interruptible(&vb_dev->evt_queue);
done:
	spin_unlock_irqrestore(&vb_dev->lock, flags);

	return IRQ_HANDLED;
}

static int
unshare_a_vm_buffer(gh_vmid_t self, gh_vmid_t peer, struct resource *r,
		    struct shared_memory *shmem)
{
	u64 src_vmlist = BIT(self) | BIT(peer);
	struct qcom_scm_vmperm dst_vmlist[1] = {{self,
				PERM_READ | PERM_WRITE | PERM_EXEC}};
	int ret;

	ret = ghd_rm_mem_reclaim(shmem->shm_memparcel, 0);
	if (ret) {
		pr_err("%s: gh_rm_mem_reclaim failed for handle %x addr=%llx size=%lld err=%d\n",
			VIRTIO_PRINT_MARKER, shmem->shm_memparcel, r->start,
			resource_size(r), ret);
		return ret;
	}

	ret = qcom_scm_assign_mem(r->start, resource_size(r), &src_vmlist,
				      dst_vmlist, ARRAY_SIZE(dst_vmlist));
	if (ret)
		pr_err("%s: qcom_scm_assign_mem failed for addr=%llx size=%lld err=%d\n",
			VIRTIO_PRINT_MARKER, r->start, resource_size(r), ret);

	return ret;
}

static int unshare_vm_buffers(struct virt_machine *vm, gh_vmid_t peer)
{
	int ret, i;
	gh_vmid_t self_vmid;

	if (!vm->hyp_assign_done)
		return 0;

	ret = ghd_rm_get_vmid(GH_PRIMARY_VM, &self_vmid);
	if (ret)
		return ret;

	for (i = 0; i < vm->shmem_entries; ++i) {
		ret = unshare_a_vm_buffer(self_vmid, peer, &vm->shmem[i].r,
				&vm->shmem[i]);
		if (ret)
			return ret;

		if (!vm->is_static)
			dma_free_coherent(vm->com_mem_dev, vm->shmem[i].size, vm->shmem[i].base,
				phys_to_dma(vm->com_mem_dev, vm->shmem[i].r.start));
	}
	vm->hyp_assign_done = 0;
	return 0;
}

static int share_a_vm_buffer(gh_vmid_t self, gh_vmid_t peer, int gunyah_label,
				struct resource *r, u32 *shm_memparcel,
				struct shared_memory *shmem)
{
	struct qcom_scm_vmperm src_vmlist[] = {{self,
					PERM_READ | PERM_WRITE | PERM_EXEC}};
	struct qcom_scm_vmperm dst_vmlist[] = {{self, PERM_READ | PERM_WRITE},
			{peer, PERM_READ | PERM_WRITE}};
	u64 src_vmids = BIT(src_vmlist[0].vmid);
	u64 dst_vmids = BIT(dst_vmlist[0].vmid) | BIT(dst_vmlist[1].vmid);
	struct gh_acl_desc *acl;
	struct gh_sgl_desc *sgl;
	int ret;

	acl = kzalloc(offsetof(struct gh_acl_desc, acl_entries[2]), GFP_KERNEL);
	if (!acl)
		return -ENOMEM;
	sgl = kzalloc(offsetof(struct gh_sgl_desc, sgl_entries[1]), GFP_KERNEL);
	if (!sgl) {
		kfree(acl);
		return -ENOMEM;
	}

	ret = qcom_scm_assign_mem(r->start, resource_size(r), &src_vmids,
				      dst_vmlist, ARRAY_SIZE(dst_vmlist));
	if (ret) {
		pr_err("%s: qcom_scm_assign_mem failed for addr=%llx size=%lld err=%d\n",
		       VIRTIO_PRINT_MARKER, r->start, resource_size(r), ret);
		kfree(acl);
		kfree(sgl);
		return ret;
	}

	acl->n_acl_entries = 2;
	acl->acl_entries[0].vmid = (u16)self;
	acl->acl_entries[0].perms = GH_RM_ACL_R | GH_RM_ACL_W;
	acl->acl_entries[1].vmid = (u16)peer;
	acl->acl_entries[1].perms = GH_RM_ACL_R | GH_RM_ACL_W;

	sgl->n_sgl_entries = 1;
	sgl->sgl_entries[0].ipa_base = r->start;
	sgl->sgl_entries[0].size = resource_size(r);

	ret = ghd_rm_mem_share(GH_RM_MEM_TYPE_NORMAL, 0, gunyah_label,
					acl, sgl, NULL, shm_memparcel);
	if (ret) {
		pr_err("%s: Sharing memory failed %d\n", VIRTIO_PRINT_MARKER, ret);
		/* Attempt to assign resource back to HLOS */
		if (qcom_scm_assign_mem(r->start, resource_size(r), &dst_vmids,
				      src_vmlist, ARRAY_SIZE(src_vmlist)))
			pr_err("%s: qcom_scm_assign_mem to re-assign addr=%llx size=%lld back to HLOS failed\n",
			       VIRTIO_PRINT_MARKER, r->start, resource_size(r));
	}

	kfree(acl);
	kfree(sgl);

	return ret;
}

static int share_vm_buffers(struct virt_machine *vm, gh_vmid_t peer)
{
	int i, j, ret;
	gh_vmid_t self_vmid;
	dma_addr_t dma_handle;

	ret = ghd_rm_get_vmid(GH_PRIMARY_VM, &self_vmid);
	if (ret)
		return ret;

	for (i = 0; i < vm->shmem_entries; ++i) {
		if (!vm->is_static) {
			vm->shmem[i].base = dma_alloc_coherent(vm->com_mem_dev, vm->shmem[i].size,
				&dma_handle, GFP_KERNEL);
			if (!vm->shmem[i].base) {
				i--;
				j = i;
				ret = -ENOMEM;
				goto unshare;
			}

			vm->shmem[i].r.start = dma_to_phys(vm->com_mem_dev, dma_handle);
			vm->shmem[i].r.end = vm->shmem[i].r.start + vm->shmem[i].size - 1;
		}

		ret = share_a_vm_buffer(self_vmid, peer, vm->shmem[i].gunyah_label,
				&vm->shmem[i].r, &vm->shmem[i].shm_memparcel, &vm->shmem[i]);
		if (ret) {
			j = i;
			i--;
			goto unshare;
		}
	}

	vm->hyp_assign_done = 1;
	return 0;

unshare:
	for (; i >= 0; --i)
		unshare_a_vm_buffer(self_vmid, peer, &vm->shmem[i].r, &vm->shmem[i]);
	if (!vm->is_static) {
		for (; j >= 0; --j)
			dma_free_coherent(vm->com_mem_dev, vm->shmem[j].size, vm->shmem[j].base,
				phys_to_dma(vm->com_mem_dev, vm->shmem[j].r.start));
	}

	return ret;
}

static int gh_virtio_mmio_init(gh_vmid_t vmid, const char *vm_name, gh_label_t label,
			gh_capid_t cap_id, int linux_irq, u64 base, u64 size)
{
	struct virt_machine *vm;
	struct virtio_backend_device *vb_dev;
	int ret;

	vm = find_vm_by_name(vm_name);
	if (!vm) {
		pr_err("%s: VM name %s not found\n", VIRTIO_PRINT_MARKER, vm_name);
		return -ENODEV;
	}

	dev_dbg(vm->dev, "%s: vmid %d vm_name %s label %x cap_id %x irq %d base %pK size %d\n",
		VIRTIO_PRINT_MARKER, vmid, vm_name, label, cap_id, linux_irq, (void *)base, size);

	if (strnlen(vm_name, GH_VM_FW_NAME_MAX) == GH_VM_FW_NAME_MAX) {
		dev_dbg(vm->dev, "%s: VM name %s too long\n", VIRTIO_PRINT_MARKER, vm_name);
		return -EINVAL;
	}

	vb_dev = vb_dev_get(vm, label);
	if (!vb_dev) {
		dev_err(vm->dev, "%s: Device with label %x not found\n",
VIRTIO_PRINT_MARKER, label);
		return -ENODEV;
	}

	mutex_lock(&vb_dev->mutex);

	if (!vm->hyp_assign_done) {
		ret = share_vm_buffers(vm, vmid);
		if (ret) {
			mutex_unlock(&vb_dev->mutex);
			vb_dev_put(vb_dev);
			return ret;
		}
	}

	ret = request_irq(linux_irq, vdev_interrupt, 0,
			vb_dev->int_name, vb_dev);
	if (ret) {
		dev_err(vm->dev, "%s: Unable to register interrupt handler for %d\n",
				VIRTIO_PRINT_MARKER, linux_irq);
		unshare_vm_buffers(vm, vmid);
		mutex_unlock(&vb_dev->mutex);
		vb_dev_put(vb_dev);
		return ret;
	}

	vb_dev->config_shared_buf = ioremap(base, size);
	if (!vb_dev->config_shared_buf) {
		dev_err(vm->dev, "%s: Unable to map config page\n", VIRTIO_PRINT_MARKER);
		free_irq(linux_irq, vb_dev);
		unshare_vm_buffers(vm, vmid);
		mutex_unlock(&vb_dev->mutex);
		vb_dev_put(vb_dev);
		return -ENOMEM;
	}

	vb_dev->linux_irq = linux_irq;
	vb_dev->cap_id = cap_id;
	vb_dev->config_shared_size = size;
	init_vb_dev_open(vb_dev);
	mutex_unlock(&vb_dev->mutex);
	vb_dev_put(vb_dev);

	return 0;
}

int gh_virtio_mmio_exit(gh_vmid_t vmid, const char *vm_name)
{
	struct virt_machine *vm;
	struct virtio_backend_device *vb_dev;
	unsigned long flags;
	int ret = -EINVAL;
	u32 refcount;

	vm = find_vm_by_name(vm_name);
	if (!vm) {
		pr_debug("%s: VM name %s not found\n", VIRTIO_PRINT_MARKER, vm_name);
		return 0;
	}

	spin_lock(&vm->vb_dev_lock);
	list_for_each_entry(vb_dev, &vm->vb_dev_list, list) {
		spin_lock_irqsave(&vb_dev->lock, flags);
		vb_dev->vdev_event = EVENT_VM_EXIT;
		vb_dev->vdev_event_data = 0;
		vb_dev->evt_avail = 1;
		vb_dev->notify = 1;
		refcount = vb_dev->refcount;
		spin_unlock_irqrestore(&vb_dev->lock, flags);

		wake_up_interruptible(&vb_dev->evt_queue);
		spin_unlock(&vm->vb_dev_lock);
		if (refcount)
			wait_event(vb_dev->notify_queue, !vb_dev->refcount);

		free_irq(vb_dev->linux_irq, vb_dev);
		iounmap(vb_dev->config_shared_buf);
		vb_dev->config_shared_buf = NULL;
		spin_lock(&vm->vb_dev_lock);
	}

	spin_unlock(&vm->vb_dev_lock);

	ret = unshare_vm_buffers(vm, vmid);

	return ret;
}

int __init gh_virtio_backend_init(void)
{
	return gh_rm_set_virtio_mmio_cb(gh_virtio_mmio_init);
}

void __exit gh_virtio_backend_exit(void)
{
	gh_rm_unset_virtio_mmio_cb();
}
