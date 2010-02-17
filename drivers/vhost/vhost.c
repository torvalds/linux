/* Copyright (C) 2009 Red Hat, Inc.
 * Copyright (C) 2006 Rusty Russell IBM Corporation
 *
 * Author: Michael S. Tsirkin <mst@redhat.com>
 *
 * Inspiration, some code, and most witty comments come from
 * Documentation/lguest/lguest.c, by Rusty Russell
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 *
 * Generic code for virtio server in host kernel.
 */

#include <linux/eventfd.h>
#include <linux/vhost.h>
#include <linux/virtio_net.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/rcupdate.h>
#include <linux/poll.h>
#include <linux/file.h>
#include <linux/highmem.h>

#include <linux/net.h>
#include <linux/if_packet.h>
#include <linux/if_arp.h>

#include <net/sock.h>

#include "vhost.h"

enum {
	VHOST_MEMORY_MAX_NREGIONS = 64,
	VHOST_MEMORY_F_LOG = 0x1,
};

static struct workqueue_struct *vhost_workqueue;

static void vhost_poll_func(struct file *file, wait_queue_head_t *wqh,
			    poll_table *pt)
{
	struct vhost_poll *poll;
	poll = container_of(pt, struct vhost_poll, table);

	poll->wqh = wqh;
	add_wait_queue(wqh, &poll->wait);
}

static int vhost_poll_wakeup(wait_queue_t *wait, unsigned mode, int sync,
			     void *key)
{
	struct vhost_poll *poll;
	poll = container_of(wait, struct vhost_poll, wait);
	if (!((unsigned long)key & poll->mask))
		return 0;

	queue_work(vhost_workqueue, &poll->work);
	return 0;
}

/* Init poll structure */
void vhost_poll_init(struct vhost_poll *poll, work_func_t func,
		     unsigned long mask)
{
	INIT_WORK(&poll->work, func);
	init_waitqueue_func_entry(&poll->wait, vhost_poll_wakeup);
	init_poll_funcptr(&poll->table, vhost_poll_func);
	poll->mask = mask;
}

/* Start polling a file. We add ourselves to file's wait queue. The caller must
 * keep a reference to a file until after vhost_poll_stop is called. */
void vhost_poll_start(struct vhost_poll *poll, struct file *file)
{
	unsigned long mask;
	mask = file->f_op->poll(file, &poll->table);
	if (mask)
		vhost_poll_wakeup(&poll->wait, 0, 0, (void *)mask);
}

/* Stop polling a file. After this function returns, it becomes safe to drop the
 * file reference. You must also flush afterwards. */
void vhost_poll_stop(struct vhost_poll *poll)
{
	remove_wait_queue(poll->wqh, &poll->wait);
}

/* Flush any work that has been scheduled. When calling this, don't hold any
 * locks that are also used by the callback. */
void vhost_poll_flush(struct vhost_poll *poll)
{
	flush_work(&poll->work);
}

void vhost_poll_queue(struct vhost_poll *poll)
{
	queue_work(vhost_workqueue, &poll->work);
}

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
	vq->used_flags = 0;
	vq->used_flags = 0;
	vq->log_used = false;
	vq->log_addr = -1ull;
	vq->hdr_size = 0;
	vq->private_data = NULL;
	vq->log_base = NULL;
	vq->error_ctx = NULL;
	vq->error = NULL;
	vq->kick = NULL;
	vq->call_ctx = NULL;
	vq->call = NULL;
}

long vhost_dev_init(struct vhost_dev *dev,
		    struct vhost_virtqueue *vqs, int nvqs)
{
	int i;
	dev->vqs = vqs;
	dev->nvqs = nvqs;
	mutex_init(&dev->mutex);
	dev->log_ctx = NULL;
	dev->log_file = NULL;
	dev->memory = NULL;
	dev->mm = NULL;

	for (i = 0; i < dev->nvqs; ++i) {
		dev->vqs[i].dev = dev;
		mutex_init(&dev->vqs[i].mutex);
		vhost_vq_reset(dev, dev->vqs + i);
		if (dev->vqs[i].handle_kick)
			vhost_poll_init(&dev->vqs[i].poll,
					dev->vqs[i].handle_kick,
					POLLIN);
	}
	return 0;
}

/* Caller should have device mutex */
long vhost_dev_check_owner(struct vhost_dev *dev)
{
	/* Are you the owner? If not, I don't think you mean to do that */
	return dev->mm == current->mm ? 0 : -EPERM;
}

/* Caller should have device mutex */
static long vhost_dev_set_owner(struct vhost_dev *dev)
{
	/* Is there an owner already? */
	if (dev->mm)
		return -EBUSY;
	/* No owner, become one */
	dev->mm = get_task_mm(current);
	return 0;
}

/* Caller should have device mutex */
long vhost_dev_reset_owner(struct vhost_dev *dev)
{
	struct vhost_memory *memory;

	/* Restore memory to default empty mapping. */
	memory = kmalloc(offsetof(struct vhost_memory, regions), GFP_KERNEL);
	if (!memory)
		return -ENOMEM;

	vhost_dev_cleanup(dev);

	memory->nregions = 0;
	dev->memory = memory;
	return 0;
}

/* Caller should have device mutex */
void vhost_dev_cleanup(struct vhost_dev *dev)
{
	int i;
	for (i = 0; i < dev->nvqs; ++i) {
		if (dev->vqs[i].kick && dev->vqs[i].handle_kick) {
			vhost_poll_stop(&dev->vqs[i].poll);
			vhost_poll_flush(&dev->vqs[i].poll);
		}
		if (dev->vqs[i].error_ctx)
			eventfd_ctx_put(dev->vqs[i].error_ctx);
		if (dev->vqs[i].error)
			fput(dev->vqs[i].error);
		if (dev->vqs[i].kick)
			fput(dev->vqs[i].kick);
		if (dev->vqs[i].call_ctx)
			eventfd_ctx_put(dev->vqs[i].call_ctx);
		if (dev->vqs[i].call)
			fput(dev->vqs[i].call);
		vhost_vq_reset(dev, dev->vqs + i);
	}
	if (dev->log_ctx)
		eventfd_ctx_put(dev->log_ctx);
	dev->log_ctx = NULL;
	if (dev->log_file)
		fput(dev->log_file);
	dev->log_file = NULL;
	/* No one will access memory at this point */
	kfree(dev->memory);
	dev->memory = NULL;
	if (dev->mm)
		mmput(dev->mm);
	dev->mm = NULL;
}

static int log_access_ok(void __user *log_base, u64 addr, unsigned long sz)
{
	u64 a = addr / VHOST_PAGE_SIZE / 8;
	/* Make sure 64 bit math will not overflow. */
	if (a > ULONG_MAX - (unsigned long)log_base ||
	    a + (unsigned long)log_base > ULONG_MAX)
		return -EFAULT;

	return access_ok(VERIFY_WRITE, log_base + a,
			 (sz + VHOST_PAGE_SIZE * 8 - 1) / VHOST_PAGE_SIZE / 8);
}

/* Caller should have vq mutex and device mutex. */
static int vq_memory_access_ok(void __user *log_base, struct vhost_memory *mem,
			       int log_all)
{
	int i;
	for (i = 0; i < mem->nregions; ++i) {
		struct vhost_memory_region *m = mem->regions + i;
		unsigned long a = m->userspace_addr;
		if (m->memory_size > ULONG_MAX)
			return 0;
		else if (!access_ok(VERIFY_WRITE, (void __user *)a,
				    m->memory_size))
			return 0;
		else if (log_all && !log_access_ok(log_base,
						   m->guest_phys_addr,
						   m->memory_size))
			return 0;
	}
	return 1;
}

/* Can we switch to this memory table? */
/* Caller should have device mutex but not vq mutex */
static int memory_access_ok(struct vhost_dev *d, struct vhost_memory *mem,
			    int log_all)
{
	int i;
	for (i = 0; i < d->nvqs; ++i) {
		int ok;
		mutex_lock(&d->vqs[i].mutex);
		/* If ring is inactive, will check when it's enabled. */
		if (d->vqs[i].private_data)
			ok = vq_memory_access_ok(d->vqs[i].log_base, mem,
						 log_all);
		else
			ok = 1;
		mutex_unlock(&d->vqs[i].mutex);
		if (!ok)
			return 0;
	}
	return 1;
}

static int vq_access_ok(unsigned int num,
			struct vring_desc __user *desc,
			struct vring_avail __user *avail,
			struct vring_used __user *used)
{
	return access_ok(VERIFY_READ, desc, num * sizeof *desc) &&
	       access_ok(VERIFY_READ, avail,
			 sizeof *avail + num * sizeof *avail->ring) &&
	       access_ok(VERIFY_WRITE, used,
			sizeof *used + num * sizeof *used->ring);
}

/* Can we log writes? */
/* Caller should have device mutex but not vq mutex */
int vhost_log_access_ok(struct vhost_dev *dev)
{
	return memory_access_ok(dev, dev->memory, 1);
}

/* Verify access for write logging. */
/* Caller should have vq mutex and device mutex */
static int vq_log_access_ok(struct vhost_virtqueue *vq, void __user *log_base)
{
	return vq_memory_access_ok(log_base, vq->dev->memory,
			    vhost_has_feature(vq->dev, VHOST_F_LOG_ALL)) &&
		(!vq->log_used || log_access_ok(log_base, vq->log_addr,
					sizeof *vq->used +
					vq->num * sizeof *vq->used->ring));
}

/* Can we start vq? */
/* Caller should have vq mutex and device mutex */
int vhost_vq_access_ok(struct vhost_virtqueue *vq)
{
	return vq_access_ok(vq->num, vq->desc, vq->avail, vq->used) &&
		vq_log_access_ok(vq, vq->log_base);
}

static long vhost_set_memory(struct vhost_dev *d, struct vhost_memory __user *m)
{
	struct vhost_memory mem, *newmem, *oldmem;
	unsigned long size = offsetof(struct vhost_memory, regions);
	long r;
	r = copy_from_user(&mem, m, size);
	if (r)
		return r;
	if (mem.padding)
		return -EOPNOTSUPP;
	if (mem.nregions > VHOST_MEMORY_MAX_NREGIONS)
		return -E2BIG;
	newmem = kmalloc(size + mem.nregions * sizeof *m->regions, GFP_KERNEL);
	if (!newmem)
		return -ENOMEM;

	memcpy(newmem, &mem, size);
	r = copy_from_user(newmem->regions, m->regions,
			   mem.nregions * sizeof *m->regions);
	if (r) {
		kfree(newmem);
		return r;
	}

	if (!memory_access_ok(d, newmem, vhost_has_feature(d, VHOST_F_LOG_ALL)))
		return -EFAULT;
	oldmem = d->memory;
	rcu_assign_pointer(d->memory, newmem);
	synchronize_rcu();
	kfree(oldmem);
	return 0;
}

static int init_used(struct vhost_virtqueue *vq,
		     struct vring_used __user *used)
{
	int r = put_user(vq->used_flags, &used->flags);
	if (r)
		return r;
	return get_user(vq->last_used_idx, &used->idx);
}

static long vhost_set_vring(struct vhost_dev *d, int ioctl, void __user *argp)
{
	struct file *eventfp, *filep = NULL,
		    *pollstart = NULL, *pollstop = NULL;
	struct eventfd_ctx *ctx = NULL;
	u32 __user *idxp = argp;
	struct vhost_virtqueue *vq;
	struct vhost_vring_state s;
	struct vhost_vring_file f;
	struct vhost_vring_addr a;
	u32 idx;
	long r;

	r = get_user(idx, idxp);
	if (r < 0)
		return r;
	if (idx > d->nvqs)
		return -ENOBUFS;

	vq = d->vqs + idx;

	mutex_lock(&vq->mutex);

	switch (ioctl) {
	case VHOST_SET_VRING_NUM:
		/* Resizing ring with an active backend?
		 * You don't want to do that. */
		if (vq->private_data) {
			r = -EBUSY;
			break;
		}
		r = copy_from_user(&s, argp, sizeof s);
		if (r < 0)
			break;
		if (!s.num || s.num > 0xffff || (s.num & (s.num - 1))) {
			r = -EINVAL;
			break;
		}
		vq->num = s.num;
		break;
	case VHOST_SET_VRING_BASE:
		/* Moving base with an active backend?
		 * You don't want to do that. */
		if (vq->private_data) {
			r = -EBUSY;
			break;
		}
		r = copy_from_user(&s, argp, sizeof s);
		if (r < 0)
			break;
		if (s.num > 0xffff) {
			r = -EINVAL;
			break;
		}
		vq->last_avail_idx = s.num;
		/* Forget the cached index value. */
		vq->avail_idx = vq->last_avail_idx;
		break;
	case VHOST_GET_VRING_BASE:
		s.index = idx;
		s.num = vq->last_avail_idx;
		r = copy_to_user(argp, &s, sizeof s);
		break;
	case VHOST_SET_VRING_ADDR:
		r = copy_from_user(&a, argp, sizeof a);
		if (r < 0)
			break;
		if (a.flags & ~(0x1 << VHOST_VRING_F_LOG)) {
			r = -EOPNOTSUPP;
			break;
		}
		/* For 32bit, verify that the top 32bits of the user
		   data are set to zero. */
		if ((u64)(unsigned long)a.desc_user_addr != a.desc_user_addr ||
		    (u64)(unsigned long)a.used_user_addr != a.used_user_addr ||
		    (u64)(unsigned long)a.avail_user_addr != a.avail_user_addr) {
			r = -EFAULT;
			break;
		}
		if ((a.avail_user_addr & (sizeof *vq->avail->ring - 1)) ||
		    (a.used_user_addr & (sizeof *vq->used->ring - 1)) ||
		    (a.log_guest_addr & (sizeof *vq->used->ring - 1))) {
			r = -EINVAL;
			break;
		}

		/* We only verify access here if backend is configured.
		 * If it is not, we don't as size might not have been setup.
		 * We will verify when backend is configured. */
		if (vq->private_data) {
			if (!vq_access_ok(vq->num,
				(void __user *)(unsigned long)a.desc_user_addr,
				(void __user *)(unsigned long)a.avail_user_addr,
				(void __user *)(unsigned long)a.used_user_addr)) {
				r = -EINVAL;
				break;
			}

			/* Also validate log access for used ring if enabled. */
			if ((a.flags & (0x1 << VHOST_VRING_F_LOG)) &&
			    !log_access_ok(vq->log_base, a.log_guest_addr,
					   sizeof *vq->used +
					   vq->num * sizeof *vq->used->ring)) {
				r = -EINVAL;
				break;
			}
		}

		r = init_used(vq, (struct vring_used __user *)(unsigned long)
			      a.used_user_addr);
		if (r)
			break;
		vq->log_used = !!(a.flags & (0x1 << VHOST_VRING_F_LOG));
		vq->desc = (void __user *)(unsigned long)a.desc_user_addr;
		vq->avail = (void __user *)(unsigned long)a.avail_user_addr;
		vq->log_addr = a.log_guest_addr;
		vq->used = (void __user *)(unsigned long)a.used_user_addr;
		break;
	case VHOST_SET_VRING_KICK:
		r = copy_from_user(&f, argp, sizeof f);
		if (r < 0)
			break;
		eventfp = f.fd == -1 ? NULL : eventfd_fget(f.fd);
		if (IS_ERR(eventfp))
			return PTR_ERR(eventfp);
		if (eventfp != vq->kick) {
			pollstop = filep = vq->kick;
			pollstart = vq->kick = eventfp;
		} else
			filep = eventfp;
		break;
	case VHOST_SET_VRING_CALL:
		r = copy_from_user(&f, argp, sizeof f);
		if (r < 0)
			break;
		eventfp = f.fd == -1 ? NULL : eventfd_fget(f.fd);
		if (IS_ERR(eventfp))
			return PTR_ERR(eventfp);
		if (eventfp != vq->call) {
			filep = vq->call;
			ctx = vq->call_ctx;
			vq->call = eventfp;
			vq->call_ctx = eventfp ?
				eventfd_ctx_fileget(eventfp) : NULL;
		} else
			filep = eventfp;
		break;
	case VHOST_SET_VRING_ERR:
		r = copy_from_user(&f, argp, sizeof f);
		if (r < 0)
			break;
		eventfp = f.fd == -1 ? NULL : eventfd_fget(f.fd);
		if (IS_ERR(eventfp))
			return PTR_ERR(eventfp);
		if (eventfp != vq->error) {
			filep = vq->error;
			vq->error = eventfp;
			ctx = vq->error_ctx;
			vq->error_ctx = eventfp ?
				eventfd_ctx_fileget(eventfp) : NULL;
		} else
			filep = eventfp;
		break;
	default:
		r = -ENOIOCTLCMD;
	}

	if (pollstop && vq->handle_kick)
		vhost_poll_stop(&vq->poll);

	if (ctx)
		eventfd_ctx_put(ctx);
	if (filep)
		fput(filep);

	if (pollstart && vq->handle_kick)
		vhost_poll_start(&vq->poll, vq->kick);

	mutex_unlock(&vq->mutex);

	if (pollstop && vq->handle_kick)
		vhost_poll_flush(&vq->poll);
	return r;
}

/* Caller must have device mutex */
long vhost_dev_ioctl(struct vhost_dev *d, unsigned int ioctl, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct file *eventfp, *filep = NULL;
	struct eventfd_ctx *ctx = NULL;
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
		r = copy_from_user(&p, argp, sizeof p);
		if (r < 0)
			break;
		if ((u64)(unsigned long)p != p) {
			r = -EFAULT;
			break;
		}
		for (i = 0; i < d->nvqs; ++i) {
			struct vhost_virtqueue *vq;
			void __user *base = (void __user *)(unsigned long)p;
			vq = d->vqs + i;
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
		eventfp = fd == -1 ? NULL : eventfd_fget(fd);
		if (IS_ERR(eventfp)) {
			r = PTR_ERR(eventfp);
			break;
		}
		if (eventfp != d->log_file) {
			filep = d->log_file;
			ctx = d->log_ctx;
			d->log_ctx = eventfp ?
				eventfd_ctx_fileget(eventfp) : NULL;
		} else
			filep = eventfp;
		for (i = 0; i < d->nvqs; ++i) {
			mutex_lock(&d->vqs[i].mutex);
			d->vqs[i].log_ctx = d->log_ctx;
			mutex_unlock(&d->vqs[i].mutex);
		}
		if (ctx)
			eventfd_ctx_put(ctx);
		if (filep)
			fput(filep);
		break;
	default:
		r = vhost_set_vring(d, ioctl, argp);
		break;
	}
done:
	return r;
}

static const struct vhost_memory_region *find_region(struct vhost_memory *mem,
						     __u64 addr, __u32 len)
{
	struct vhost_memory_region *reg;
	int i;
	/* linear search is not brilliant, but we really have on the order of 6
	 * regions in practice */
	for (i = 0; i < mem->nregions; ++i) {
		reg = mem->regions + i;
		if (reg->guest_phys_addr <= addr &&
		    reg->guest_phys_addr + reg->memory_size - 1 >= addr)
			return reg;
	}
	return NULL;
}

/* TODO: This is really inefficient.  We need something like get_user()
 * (instruction directly accesses the data, with an exception table entry
 * returning -EFAULT). See Documentation/x86/exception-tables.txt.
 */
static int set_bit_to_user(int nr, void __user *addr)
{
	unsigned long log = (unsigned long)addr;
	struct page *page;
	void *base;
	int bit = nr + (log % PAGE_SIZE) * 8;
	int r;
	r = get_user_pages_fast(log, 1, 1, &page);
	if (r)
		return r;
	base = kmap_atomic(page, KM_USER0);
	set_bit(bit, base);
	kunmap_atomic(base, KM_USER0);
	set_page_dirty_lock(page);
	put_page(page);
	return 0;
}

static int log_write(void __user *log_base,
		     u64 write_address, u64 write_length)
{
	int r;
	if (!write_length)
		return 0;
	write_address /= VHOST_PAGE_SIZE;
	for (;;) {
		u64 base = (u64)(unsigned long)log_base;
		u64 log = base + write_address / 8;
		int bit = write_address % 8;
		if ((u64)(unsigned long)log != log)
			return -EFAULT;
		r = set_bit_to_user(bit, (void __user *)(unsigned long)log);
		if (r < 0)
			return r;
		if (write_length <= VHOST_PAGE_SIZE)
			break;
		write_length -= VHOST_PAGE_SIZE;
		write_address += VHOST_PAGE_SIZE;
	}
	return r;
}

int vhost_log_write(struct vhost_virtqueue *vq, struct vhost_log *log,
		    unsigned int log_num, u64 len)
{
	int i, r;

	/* Make sure data written is seen before log. */
	smp_wmb();
	for (i = 0; i < log_num; ++i) {
		u64 l = min(log[i].len, len);
		r = log_write(vq->log_base, log[i].addr, l);
		if (r < 0)
			return r;
		len -= l;
		if (!len)
			return 0;
	}
	if (vq->log_ctx)
		eventfd_signal(vq->log_ctx, 1);
	/* Length written exceeds what we have stored. This is a bug. */
	BUG();
	return 0;
}

int translate_desc(struct vhost_dev *dev, u64 addr, u32 len,
		   struct iovec iov[], int iov_size)
{
	const struct vhost_memory_region *reg;
	struct vhost_memory *mem;
	struct iovec *_iov;
	u64 s = 0;
	int ret = 0;

	rcu_read_lock();

	mem = rcu_dereference(dev->memory);
	while ((u64)len > s) {
		u64 size;
		if (ret >= iov_size) {
			ret = -ENOBUFS;
			break;
		}
		reg = find_region(mem, addr, len);
		if (!reg) {
			ret = -EFAULT;
			break;
		}
		_iov = iov + ret;
		size = reg->memory_size - addr + reg->guest_phys_addr;
		_iov->iov_len = min((u64)len, size);
		_iov->iov_base = (void *)(unsigned long)
			(reg->userspace_addr + addr - reg->guest_phys_addr);
		s += size;
		addr += size;
		++ret;
	}

	rcu_read_unlock();
	return ret;
}

/* Each buffer in the virtqueues is actually a chain of descriptors.  This
 * function returns the next descriptor in the chain,
 * or -1U if we're at the end. */
static unsigned next_desc(struct vring_desc *desc)
{
	unsigned int next;

	/* If this descriptor says it doesn't chain, we're done. */
	if (!(desc->flags & VRING_DESC_F_NEXT))
		return -1U;

	/* Check they're not leading us off end of descriptors. */
	next = desc->next;
	/* Make sure compiler knows to grab that: we don't want it changing! */
	/* We will use the result as an index in an array, so most
	 * architectures only need a compiler barrier here. */
	read_barrier_depends();

	return next;
}

static unsigned get_indirect(struct vhost_dev *dev, struct vhost_virtqueue *vq,
			     struct iovec iov[], unsigned int iov_size,
			     unsigned int *out_num, unsigned int *in_num,
			     struct vhost_log *log, unsigned int *log_num,
			     struct vring_desc *indirect)
{
	struct vring_desc desc;
	unsigned int i = 0, count, found = 0;
	int ret;

	/* Sanity check */
	if (indirect->len % sizeof desc) {
		vq_err(vq, "Invalid length in indirect descriptor: "
		       "len 0x%llx not multiple of 0x%zx\n",
		       (unsigned long long)indirect->len,
		       sizeof desc);
		return -EINVAL;
	}

	ret = translate_desc(dev, indirect->addr, indirect->len, vq->indirect,
			     ARRAY_SIZE(vq->indirect));
	if (ret < 0) {
		vq_err(vq, "Translation failure %d in indirect.\n", ret);
		return ret;
	}

	/* We will use the result as an address to read from, so most
	 * architectures only need a compiler barrier here. */
	read_barrier_depends();

	count = indirect->len / sizeof desc;
	/* Buffers are chained via a 16 bit next field, so
	 * we can have at most 2^16 of these. */
	if (count > USHORT_MAX + 1) {
		vq_err(vq, "Indirect buffer length too big: %d\n",
		       indirect->len);
		return -E2BIG;
	}

	do {
		unsigned iov_count = *in_num + *out_num;
		if (++found > count) {
			vq_err(vq, "Loop detected: last one at %u "
			       "indirect size %u\n",
			       i, count);
			return -EINVAL;
		}
		if (memcpy_fromiovec((unsigned char *)&desc, vq->indirect,
				     sizeof desc)) {
			vq_err(vq, "Failed indirect descriptor: idx %d, %zx\n",
			       i, (size_t)indirect->addr + i * sizeof desc);
			return -EINVAL;
		}
		if (desc.flags & VRING_DESC_F_INDIRECT) {
			vq_err(vq, "Nested indirect descriptor: idx %d, %zx\n",
			       i, (size_t)indirect->addr + i * sizeof desc);
			return -EINVAL;
		}

		ret = translate_desc(dev, desc.addr, desc.len, iov + iov_count,
				     iov_size - iov_count);
		if (ret < 0) {
			vq_err(vq, "Translation failure %d indirect idx %d\n",
			       ret, i);
			return ret;
		}
		/* If this is an input descriptor, increment that count. */
		if (desc.flags & VRING_DESC_F_WRITE) {
			*in_num += ret;
			if (unlikely(log)) {
				log[*log_num].addr = desc.addr;
				log[*log_num].len = desc.len;
				++*log_num;
			}
		} else {
			/* If it's an output descriptor, they're all supposed
			 * to come before any input descriptors. */
			if (*in_num) {
				vq_err(vq, "Indirect descriptor "
				       "has out after in: idx %d\n", i);
				return -EINVAL;
			}
			*out_num += ret;
		}
	} while ((i = next_desc(&desc)) != -1);
	return 0;
}

/* This looks in the virtqueue and for the first available buffer, and converts
 * it to an iovec for convenient access.  Since descriptors consist of some
 * number of output then some number of input descriptors, it's actually two
 * iovecs, but we pack them into one and note how many of each there were.
 *
 * This function returns the descriptor number found, or vq->num (which
 * is never a valid descriptor number) if none was found. */
unsigned vhost_get_vq_desc(struct vhost_dev *dev, struct vhost_virtqueue *vq,
			   struct iovec iov[], unsigned int iov_size,
			   unsigned int *out_num, unsigned int *in_num,
			   struct vhost_log *log, unsigned int *log_num)
{
	struct vring_desc desc;
	unsigned int i, head, found = 0;
	u16 last_avail_idx;
	int ret;

	/* Check it isn't doing very strange things with descriptor numbers. */
	last_avail_idx = vq->last_avail_idx;
	if (get_user(vq->avail_idx, &vq->avail->idx)) {
		vq_err(vq, "Failed to access avail idx at %p\n",
		       &vq->avail->idx);
		return vq->num;
	}

	if ((u16)(vq->avail_idx - last_avail_idx) > vq->num) {
		vq_err(vq, "Guest moved used index from %u to %u",
		       last_avail_idx, vq->avail_idx);
		return vq->num;
	}

	/* If there's nothing new since last we looked, return invalid. */
	if (vq->avail_idx == last_avail_idx)
		return vq->num;

	/* Only get avail ring entries after they have been exposed by guest. */
	smp_rmb();

	/* Grab the next descriptor number they're advertising, and increment
	 * the index we've seen. */
	if (get_user(head, &vq->avail->ring[last_avail_idx % vq->num])) {
		vq_err(vq, "Failed to read head: idx %d address %p\n",
		       last_avail_idx,
		       &vq->avail->ring[last_avail_idx % vq->num]);
		return vq->num;
	}

	/* If their number is silly, that's an error. */
	if (head >= vq->num) {
		vq_err(vq, "Guest says index %u > %u is available",
		       head, vq->num);
		return vq->num;
	}

	/* When we start there are none of either input nor output. */
	*out_num = *in_num = 0;
	if (unlikely(log))
		*log_num = 0;

	i = head;
	do {
		unsigned iov_count = *in_num + *out_num;
		if (i >= vq->num) {
			vq_err(vq, "Desc index is %u > %u, head = %u",
			       i, vq->num, head);
			return vq->num;
		}
		if (++found > vq->num) {
			vq_err(vq, "Loop detected: last one at %u "
			       "vq size %u head %u\n",
			       i, vq->num, head);
			return vq->num;
		}
		ret = copy_from_user(&desc, vq->desc + i, sizeof desc);
		if (ret) {
			vq_err(vq, "Failed to get descriptor: idx %d addr %p\n",
			       i, vq->desc + i);
			return vq->num;
		}
		if (desc.flags & VRING_DESC_F_INDIRECT) {
			ret = get_indirect(dev, vq, iov, iov_size,
					   out_num, in_num,
					   log, log_num, &desc);
			if (ret < 0) {
				vq_err(vq, "Failure detected "
				       "in indirect descriptor at idx %d\n", i);
				return vq->num;
			}
			continue;
		}

		ret = translate_desc(dev, desc.addr, desc.len, iov + iov_count,
				     iov_size - iov_count);
		if (ret < 0) {
			vq_err(vq, "Translation failure %d descriptor idx %d\n",
			       ret, i);
			return vq->num;
		}
		if (desc.flags & VRING_DESC_F_WRITE) {
			/* If this is an input descriptor,
			 * increment that count. */
			*in_num += ret;
			if (unlikely(log)) {
				log[*log_num].addr = desc.addr;
				log[*log_num].len = desc.len;
				++*log_num;
			}
		} else {
			/* If it's an output descriptor, they're all supposed
			 * to come before any input descriptors. */
			if (*in_num) {
				vq_err(vq, "Descriptor has out after in: "
				       "idx %d\n", i);
				return vq->num;
			}
			*out_num += ret;
		}
	} while ((i = next_desc(&desc)) != -1);

	/* On success, increment avail index. */
	vq->last_avail_idx++;
	return head;
}

/* Reverse the effect of vhost_get_vq_desc. Useful for error handling. */
void vhost_discard_vq_desc(struct vhost_virtqueue *vq)
{
	vq->last_avail_idx--;
}

/* After we've used one of their buffers, we tell them about it.  We'll then
 * want to notify the guest, using eventfd. */
int vhost_add_used(struct vhost_virtqueue *vq, unsigned int head, int len)
{
	struct vring_used_elem *used;

	/* The virtqueue contains a ring of used buffers.  Get a pointer to the
	 * next entry in that used ring. */
	used = &vq->used->ring[vq->last_used_idx % vq->num];
	if (put_user(head, &used->id)) {
		vq_err(vq, "Failed to write used id");
		return -EFAULT;
	}
	if (put_user(len, &used->len)) {
		vq_err(vq, "Failed to write used len");
		return -EFAULT;
	}
	/* Make sure buffer is written before we update index. */
	smp_wmb();
	if (put_user(vq->last_used_idx + 1, &vq->used->idx)) {
		vq_err(vq, "Failed to increment used idx");
		return -EFAULT;
	}
	if (unlikely(vq->log_used)) {
		/* Make sure data is seen before log. */
		smp_wmb();
		/* Log used ring entry write. */
		log_write(vq->log_base,
			  vq->log_addr + ((void *)used - (void *)vq->used),
			  sizeof *used);
		/* Log used index update. */
		log_write(vq->log_base,
			  vq->log_addr + offsetof(struct vring_used, idx),
			  sizeof vq->used->idx);
		if (vq->log_ctx)
			eventfd_signal(vq->log_ctx, 1);
	}
	vq->last_used_idx++;
	return 0;
}

/* This actually signals the guest, using eventfd. */
void vhost_signal(struct vhost_dev *dev, struct vhost_virtqueue *vq)
{
	__u16 flags = 0;
	if (get_user(flags, &vq->avail->flags)) {
		vq_err(vq, "Failed to get flags");
		return;
	}

	/* If they don't want an interrupt, don't signal, unless empty. */
	if ((flags & VRING_AVAIL_F_NO_INTERRUPT) &&
	    (vq->avail_idx != vq->last_avail_idx ||
	     !vhost_has_feature(dev, VIRTIO_F_NOTIFY_ON_EMPTY)))
		return;

	/* Signal the Guest tell them we used something up. */
	if (vq->call_ctx)
		eventfd_signal(vq->call_ctx, 1);
}

/* And here's the combo meal deal.  Supersize me! */
void vhost_add_used_and_signal(struct vhost_dev *dev,
			       struct vhost_virtqueue *vq,
			       unsigned int head, int len)
{
	vhost_add_used(vq, head, len);
	vhost_signal(dev, vq);
}

/* OK, now we need to know about added descriptors. */
bool vhost_enable_notify(struct vhost_virtqueue *vq)
{
	u16 avail_idx;
	int r;
	if (!(vq->used_flags & VRING_USED_F_NO_NOTIFY))
		return false;
	vq->used_flags &= ~VRING_USED_F_NO_NOTIFY;
	r = put_user(vq->used_flags, &vq->used->flags);
	if (r) {
		vq_err(vq, "Failed to enable notification at %p: %d\n",
		       &vq->used->flags, r);
		return false;
	}
	/* They could have slipped one in as we were doing that: make
	 * sure it's written, then check again. */
	smp_mb();
	r = get_user(avail_idx, &vq->avail->idx);
	if (r) {
		vq_err(vq, "Failed to check avail idx at %p: %d\n",
		       &vq->avail->idx, r);
		return false;
	}

	return avail_idx != vq->last_avail_idx;
}

/* We don't need to be notified again. */
void vhost_disable_notify(struct vhost_virtqueue *vq)
{
	int r;
	if (vq->used_flags & VRING_USED_F_NO_NOTIFY)
		return;
	vq->used_flags |= VRING_USED_F_NO_NOTIFY;
	r = put_user(vq->used_flags, &vq->used->flags);
	if (r)
		vq_err(vq, "Failed to enable notification at %p: %d\n",
		       &vq->used->flags, r);
}

int vhost_init(void)
{
	vhost_workqueue = create_singlethread_workqueue("vhost");
	if (!vhost_workqueue)
		return -ENOMEM;
	return 0;
}

void vhost_cleanup(void)
{
	destroy_workqueue(vhost_workqueue);
}
