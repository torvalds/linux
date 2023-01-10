// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2014 IBM Corp.
 */

#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/bitmap.h>
#include <linux/sched/signal.h>
#include <linux/poll.h>
#include <linux/pid.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/sched/mm.h>
#include <linux/mmu_context.h>
#include <asm/cputable.h>
#include <asm/current.h>
#include <asm/copro.h>

#include "cxl.h"
#include "trace.h"

#define CXL_NUM_MINORS 256 /* Total to reserve */

#define CXL_AFU_MINOR_D(afu) (CXL_CARD_MINOR(afu->adapter) + 1 + (3 * afu->slice))
#define CXL_AFU_MINOR_M(afu) (CXL_AFU_MINOR_D(afu) + 1)
#define CXL_AFU_MINOR_S(afu) (CXL_AFU_MINOR_D(afu) + 2)
#define CXL_AFU_MKDEV_D(afu) MKDEV(MAJOR(cxl_dev), CXL_AFU_MINOR_D(afu))
#define CXL_AFU_MKDEV_M(afu) MKDEV(MAJOR(cxl_dev), CXL_AFU_MINOR_M(afu))
#define CXL_AFU_MKDEV_S(afu) MKDEV(MAJOR(cxl_dev), CXL_AFU_MINOR_S(afu))

#define CXL_DEVT_AFU(dev) ((MINOR(dev) % CXL_DEV_MINORS - 1) / 3)

#define CXL_DEVT_IS_CARD(dev) (MINOR(dev) % CXL_DEV_MINORS == 0)

static dev_t cxl_dev;

static struct class *cxl_class;

static int __afu_open(struct inode *inode, struct file *file, bool master)
{
	struct cxl *adapter;
	struct cxl_afu *afu;
	struct cxl_context *ctx;
	int adapter_num = CXL_DEVT_ADAPTER(inode->i_rdev);
	int slice = CXL_DEVT_AFU(inode->i_rdev);
	int rc = -ENODEV;

	pr_devel("afu_open afu%i.%i\n", slice, adapter_num);

	if (!(adapter = get_cxl_adapter(adapter_num)))
		return -ENODEV;

	if (slice > adapter->slices)
		goto err_put_adapter;

	spin_lock(&adapter->afu_list_lock);
	if (!(afu = adapter->afu[slice])) {
		spin_unlock(&adapter->afu_list_lock);
		goto err_put_adapter;
	}

	/*
	 * taking a ref to the afu so that it doesn't go away
	 * for rest of the function. This ref is released before
	 * we return.
	 */
	cxl_afu_get(afu);
	spin_unlock(&adapter->afu_list_lock);

	if (!afu->current_mode)
		goto err_put_afu;

	if (!cxl_ops->link_ok(adapter, afu)) {
		rc = -EIO;
		goto err_put_afu;
	}

	if (!(ctx = cxl_context_alloc())) {
		rc = -ENOMEM;
		goto err_put_afu;
	}

	rc = cxl_context_init(ctx, afu, master);
	if (rc)
		goto err_put_afu;

	cxl_context_set_mapping(ctx, inode->i_mapping);

	pr_devel("afu_open pe: %i\n", ctx->pe);
	file->private_data = ctx;

	/* indicate success */
	rc = 0;

err_put_afu:
	/* release the ref taken earlier */
	cxl_afu_put(afu);
err_put_adapter:
	put_device(&adapter->dev);
	return rc;
}

int afu_open(struct inode *inode, struct file *file)
{
	return __afu_open(inode, file, false);
}

static int afu_master_open(struct inode *inode, struct file *file)
{
	return __afu_open(inode, file, true);
}

int afu_release(struct inode *inode, struct file *file)
{
	struct cxl_context *ctx = file->private_data;

	pr_devel("%s: closing cxl file descriptor. pe: %i\n",
		 __func__, ctx->pe);
	cxl_context_detach(ctx);


	/*
	 * Delete the context's mapping pointer, unless it's created by the
	 * kernel API, in which case leave it so it can be freed by reclaim_ctx()
	 */
	if (!ctx->kernelapi) {
		mutex_lock(&ctx->mapping_lock);
		ctx->mapping = NULL;
		mutex_unlock(&ctx->mapping_lock);
	}

	/*
	 * At this this point all bottom halfs have finished and we should be
	 * getting no more IRQs from the hardware for this context.  Once it's
	 * removed from the IDR (and RCU synchronised) it's safe to free the
	 * sstp and context.
	 */
	cxl_context_free(ctx);

	return 0;
}

static long afu_ioctl_start_work(struct cxl_context *ctx,
				 struct cxl_ioctl_start_work __user *uwork)
{
	struct cxl_ioctl_start_work work;
	u64 amr = 0;
	int rc;

	pr_devel("%s: pe: %i\n", __func__, ctx->pe);

	/* Do this outside the status_mutex to avoid a circular dependency with
	 * the locking in cxl_mmap_fault() */
	if (copy_from_user(&work, uwork, sizeof(work)))
		return -EFAULT;

	mutex_lock(&ctx->status_mutex);
	if (ctx->status != OPENED) {
		rc = -EIO;
		goto out;
	}

	/*
	 * if any of the reserved fields are set or any of the unused
	 * flags are set it's invalid
	 */
	if (work.reserved1 || work.reserved2 || work.reserved3 ||
	    work.reserved4 || work.reserved5 ||
	    (work.flags & ~CXL_START_WORK_ALL)) {
		rc = -EINVAL;
		goto out;
	}

	if (!(work.flags & CXL_START_WORK_NUM_IRQS))
		work.num_interrupts = ctx->afu->pp_irqs;
	else if ((work.num_interrupts < ctx->afu->pp_irqs) ||
		 (work.num_interrupts > ctx->afu->irqs_max)) {
		rc =  -EINVAL;
		goto out;
	}

	if ((rc = afu_register_irqs(ctx, work.num_interrupts)))
		goto out;

	if (work.flags & CXL_START_WORK_AMR)
		amr = work.amr & mfspr(SPRN_UAMOR);

	if (work.flags & CXL_START_WORK_TID)
		ctx->assign_tidr = true;

	ctx->mmio_err_ff = !!(work.flags & CXL_START_WORK_ERR_FF);

	/*
	 * Increment the mapped context count for adapter. This also checks
	 * if adapter_context_lock is taken.
	 */
	rc = cxl_adapter_context_get(ctx->afu->adapter);
	if (rc) {
		afu_release_irqs(ctx, ctx);
		goto out;
	}

	/*
	 * We grab the PID here and not in the file open to allow for the case
	 * where a process (master, some daemon, etc) has opened the chardev on
	 * behalf of another process, so the AFU's mm gets bound to the process
	 * that performs this ioctl and not the process that opened the file.
	 * Also we grab the PID of the group leader so that if the task that
	 * has performed the attach operation exits the mm context of the
	 * process is still accessible.
	 */
	ctx->pid = get_task_pid(current, PIDTYPE_PID);

	/* acquire a reference to the task's mm */
	ctx->mm = get_task_mm(current);

	/* ensure this mm_struct can't be freed */
	cxl_context_mm_count_get(ctx);

	if (ctx->mm) {
		/* decrement the use count from above */
		mmput(ctx->mm);
		/* make TLBIs for this context global */
		mm_context_add_copro(ctx->mm);
	}

	/*
	 * Increment driver use count. Enables global TLBIs for hash
	 * and callbacks to handle the segment table
	 */
	cxl_ctx_get();

	/*
	 * A barrier is needed to make sure all TLBIs are global
	 * before we attach and the context starts being used by the
	 * adapter.
	 *
	 * Needed after mm_context_add_copro() for radix and
	 * cxl_ctx_get() for hash/p8.
	 *
	 * The barrier should really be mb(), since it involves a
	 * device. However, it's only useful when we have local
	 * vs. global TLBIs, i.e SMP=y. So keep smp_mb().
	 */
	smp_mb();

	trace_cxl_attach(ctx, work.work_element_descriptor, work.num_interrupts, amr);

	if ((rc = cxl_ops->attach_process(ctx, false, work.work_element_descriptor,
							amr))) {
		afu_release_irqs(ctx, ctx);
		cxl_adapter_context_put(ctx->afu->adapter);
		put_pid(ctx->pid);
		ctx->pid = NULL;
		cxl_ctx_put();
		cxl_context_mm_count_put(ctx);
		if (ctx->mm)
			mm_context_remove_copro(ctx->mm);
		goto out;
	}

	rc = 0;
	if (work.flags & CXL_START_WORK_TID) {
		work.tid = ctx->tidr;
		if (copy_to_user(uwork, &work, sizeof(work)))
			rc = -EFAULT;
	}

	ctx->status = STARTED;

out:
	mutex_unlock(&ctx->status_mutex);
	return rc;
}

static long afu_ioctl_process_element(struct cxl_context *ctx,
				      int __user *upe)
{
	pr_devel("%s: pe: %i\n", __func__, ctx->pe);

	if (copy_to_user(upe, &ctx->external_pe, sizeof(__u32)))
		return -EFAULT;

	return 0;
}

static long afu_ioctl_get_afu_id(struct cxl_context *ctx,
				 struct cxl_afu_id __user *upafuid)
{
	struct cxl_afu_id afuid = { 0 };

	afuid.card_id = ctx->afu->adapter->adapter_num;
	afuid.afu_offset = ctx->afu->slice;
	afuid.afu_mode = ctx->afu->current_mode;

	/* set the flag bit in case the afu is a slave */
	if (ctx->afu->current_mode == CXL_MODE_DIRECTED && !ctx->master)
		afuid.flags |= CXL_AFUID_FLAG_SLAVE;

	if (copy_to_user(upafuid, &afuid, sizeof(afuid)))
		return -EFAULT;

	return 0;
}

long afu_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct cxl_context *ctx = file->private_data;

	if (ctx->status == CLOSED)
		return -EIO;

	if (!cxl_ops->link_ok(ctx->afu->adapter, ctx->afu))
		return -EIO;

	pr_devel("afu_ioctl\n");
	switch (cmd) {
	case CXL_IOCTL_START_WORK:
		return afu_ioctl_start_work(ctx, (struct cxl_ioctl_start_work __user *)arg);
	case CXL_IOCTL_GET_PROCESS_ELEMENT:
		return afu_ioctl_process_element(ctx, (__u32 __user *)arg);
	case CXL_IOCTL_GET_AFU_ID:
		return afu_ioctl_get_afu_id(ctx, (struct cxl_afu_id __user *)
					    arg);
	}
	return -EINVAL;
}

static long afu_compat_ioctl(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	return afu_ioctl(file, cmd, arg);
}

int afu_mmap(struct file *file, struct vm_area_struct *vm)
{
	struct cxl_context *ctx = file->private_data;

	/* AFU must be started before we can MMIO */
	if (ctx->status != STARTED)
		return -EIO;

	if (!cxl_ops->link_ok(ctx->afu->adapter, ctx->afu))
		return -EIO;

	return cxl_context_iomap(ctx, vm);
}

static inline bool ctx_event_pending(struct cxl_context *ctx)
{
	if (ctx->pending_irq || ctx->pending_fault || ctx->pending_afu_err)
		return true;

	if (ctx->afu_driver_ops && atomic_read(&ctx->afu_driver_events))
		return true;

	return false;
}

__poll_t afu_poll(struct file *file, struct poll_table_struct *poll)
{
	struct cxl_context *ctx = file->private_data;
	__poll_t mask = 0;
	unsigned long flags;


	poll_wait(file, &ctx->wq, poll);

	pr_devel("afu_poll wait done pe: %i\n", ctx->pe);

	spin_lock_irqsave(&ctx->lock, flags);
	if (ctx_event_pending(ctx))
		mask |= EPOLLIN | EPOLLRDNORM;
	else if (ctx->status == CLOSED)
		/* Only error on closed when there are no futher events pending
		 */
		mask |= EPOLLERR;
	spin_unlock_irqrestore(&ctx->lock, flags);

	pr_devel("afu_poll pe: %i returning %#x\n", ctx->pe, mask);

	return mask;
}

static ssize_t afu_driver_event_copy(struct cxl_context *ctx,
				     char __user *buf,
				     struct cxl_event *event,
				     struct cxl_event_afu_driver_reserved *pl)
{
	/* Check event */
	if (!pl) {
		ctx->afu_driver_ops->event_delivered(ctx, pl, -EINVAL);
		return -EFAULT;
	}

	/* Check event size */
	event->header.size += pl->data_size;
	if (event->header.size > CXL_READ_MIN_SIZE) {
		ctx->afu_driver_ops->event_delivered(ctx, pl, -EINVAL);
		return -EFAULT;
	}

	/* Copy event header */
	if (copy_to_user(buf, event, sizeof(struct cxl_event_header))) {
		ctx->afu_driver_ops->event_delivered(ctx, pl, -EFAULT);
		return -EFAULT;
	}

	/* Copy event data */
	buf += sizeof(struct cxl_event_header);
	if (copy_to_user(buf, &pl->data, pl->data_size)) {
		ctx->afu_driver_ops->event_delivered(ctx, pl, -EFAULT);
		return -EFAULT;
	}

	ctx->afu_driver_ops->event_delivered(ctx, pl, 0); /* Success */
	return event->header.size;
}

ssize_t afu_read(struct file *file, char __user *buf, size_t count,
			loff_t *off)
{
	struct cxl_context *ctx = file->private_data;
	struct cxl_event_afu_driver_reserved *pl = NULL;
	struct cxl_event event;
	unsigned long flags;
	int rc;
	DEFINE_WAIT(wait);

	if (!cxl_ops->link_ok(ctx->afu->adapter, ctx->afu))
		return -EIO;

	if (count < CXL_READ_MIN_SIZE)
		return -EINVAL;

	spin_lock_irqsave(&ctx->lock, flags);

	for (;;) {
		prepare_to_wait(&ctx->wq, &wait, TASK_INTERRUPTIBLE);
		if (ctx_event_pending(ctx) || (ctx->status == CLOSED))
			break;

		if (!cxl_ops->link_ok(ctx->afu->adapter, ctx->afu)) {
			rc = -EIO;
			goto out;
		}

		if (file->f_flags & O_NONBLOCK) {
			rc = -EAGAIN;
			goto out;
		}

		if (signal_pending(current)) {
			rc = -ERESTARTSYS;
			goto out;
		}

		spin_unlock_irqrestore(&ctx->lock, flags);
		pr_devel("afu_read going to sleep...\n");
		schedule();
		pr_devel("afu_read woken up\n");
		spin_lock_irqsave(&ctx->lock, flags);
	}

	finish_wait(&ctx->wq, &wait);

	memset(&event, 0, sizeof(event));
	event.header.process_element = ctx->pe;
	event.header.size = sizeof(struct cxl_event_header);
	if (ctx->afu_driver_ops && atomic_read(&ctx->afu_driver_events)) {
		pr_devel("afu_read delivering AFU driver specific event\n");
		pl = ctx->afu_driver_ops->fetch_event(ctx);
		atomic_dec(&ctx->afu_driver_events);
		event.header.type = CXL_EVENT_AFU_DRIVER;
	} else if (ctx->pending_irq) {
		pr_devel("afu_read delivering AFU interrupt\n");
		event.header.size += sizeof(struct cxl_event_afu_interrupt);
		event.header.type = CXL_EVENT_AFU_INTERRUPT;
		event.irq.irq = find_first_bit(ctx->irq_bitmap, ctx->irq_count) + 1;
		clear_bit(event.irq.irq - 1, ctx->irq_bitmap);
		if (bitmap_empty(ctx->irq_bitmap, ctx->irq_count))
			ctx->pending_irq = false;
	} else if (ctx->pending_fault) {
		pr_devel("afu_read delivering data storage fault\n");
		event.header.size += sizeof(struct cxl_event_data_storage);
		event.header.type = CXL_EVENT_DATA_STORAGE;
		event.fault.addr = ctx->fault_addr;
		event.fault.dsisr = ctx->fault_dsisr;
		ctx->pending_fault = false;
	} else if (ctx->pending_afu_err) {
		pr_devel("afu_read delivering afu error\n");
		event.header.size += sizeof(struct cxl_event_afu_error);
		event.header.type = CXL_EVENT_AFU_ERROR;
		event.afu_error.error = ctx->afu_err;
		ctx->pending_afu_err = false;
	} else if (ctx->status == CLOSED) {
		pr_devel("afu_read fatal error\n");
		spin_unlock_irqrestore(&ctx->lock, flags);
		return -EIO;
	} else
		WARN(1, "afu_read must be buggy\n");

	spin_unlock_irqrestore(&ctx->lock, flags);

	if (event.header.type == CXL_EVENT_AFU_DRIVER)
		return afu_driver_event_copy(ctx, buf, &event, pl);

	if (copy_to_user(buf, &event, event.header.size))
		return -EFAULT;
	return event.header.size;

out:
	finish_wait(&ctx->wq, &wait);
	spin_unlock_irqrestore(&ctx->lock, flags);
	return rc;
}

/* 
 * Note: if this is updated, we need to update api.c to patch the new ones in
 * too
 */
const struct file_operations afu_fops = {
	.owner		= THIS_MODULE,
	.open           = afu_open,
	.poll		= afu_poll,
	.read		= afu_read,
	.release        = afu_release,
	.unlocked_ioctl = afu_ioctl,
	.compat_ioctl   = afu_compat_ioctl,
	.mmap           = afu_mmap,
};

static const struct file_operations afu_master_fops = {
	.owner		= THIS_MODULE,
	.open           = afu_master_open,
	.poll		= afu_poll,
	.read		= afu_read,
	.release        = afu_release,
	.unlocked_ioctl = afu_ioctl,
	.compat_ioctl   = afu_compat_ioctl,
	.mmap           = afu_mmap,
};


static char *cxl_devnode(const struct device *dev, umode_t *mode)
{
	if (cpu_has_feature(CPU_FTR_HVMODE) &&
	    CXL_DEVT_IS_CARD(dev->devt)) {
		/*
		 * These minor numbers will eventually be used to program the
		 * PSL and AFUs once we have dynamic reprogramming support
		 */
		return NULL;
	}
	return kasprintf(GFP_KERNEL, "cxl/%s", dev_name(dev));
}

extern struct class *cxl_class;

static int cxl_add_chardev(struct cxl_afu *afu, dev_t devt, struct cdev *cdev,
			   struct device **chardev, char *postfix, char *desc,
			   const struct file_operations *fops)
{
	struct device *dev;
	int rc;

	cdev_init(cdev, fops);
	rc = cdev_add(cdev, devt, 1);
	if (rc) {
		dev_err(&afu->dev, "Unable to add %s chardev: %i\n", desc, rc);
		return rc;
	}

	dev = device_create(cxl_class, &afu->dev, devt, afu,
			"afu%i.%i%s", afu->adapter->adapter_num, afu->slice, postfix);
	if (IS_ERR(dev)) {
		rc = PTR_ERR(dev);
		dev_err(&afu->dev, "Unable to create %s chardev in sysfs: %i\n", desc, rc);
		goto err;
	}

	*chardev = dev;

	return 0;
err:
	cdev_del(cdev);
	return rc;
}

int cxl_chardev_d_afu_add(struct cxl_afu *afu)
{
	return cxl_add_chardev(afu, CXL_AFU_MKDEV_D(afu), &afu->afu_cdev_d,
			       &afu->chardev_d, "d", "dedicated",
			       &afu_master_fops); /* Uses master fops */
}

int cxl_chardev_m_afu_add(struct cxl_afu *afu)
{
	return cxl_add_chardev(afu, CXL_AFU_MKDEV_M(afu), &afu->afu_cdev_m,
			       &afu->chardev_m, "m", "master",
			       &afu_master_fops);
}

int cxl_chardev_s_afu_add(struct cxl_afu *afu)
{
	return cxl_add_chardev(afu, CXL_AFU_MKDEV_S(afu), &afu->afu_cdev_s,
			       &afu->chardev_s, "s", "shared",
			       &afu_fops);
}

void cxl_chardev_afu_remove(struct cxl_afu *afu)
{
	if (afu->chardev_d) {
		cdev_del(&afu->afu_cdev_d);
		device_unregister(afu->chardev_d);
		afu->chardev_d = NULL;
	}
	if (afu->chardev_m) {
		cdev_del(&afu->afu_cdev_m);
		device_unregister(afu->chardev_m);
		afu->chardev_m = NULL;
	}
	if (afu->chardev_s) {
		cdev_del(&afu->afu_cdev_s);
		device_unregister(afu->chardev_s);
		afu->chardev_s = NULL;
	}
}

int cxl_register_afu(struct cxl_afu *afu)
{
	afu->dev.class = cxl_class;

	return device_register(&afu->dev);
}

int cxl_register_adapter(struct cxl *adapter)
{
	adapter->dev.class = cxl_class;

	/*
	 * Future: When we support dynamically reprogramming the PSL & AFU we
	 * will expose the interface to do that via a chardev:
	 * adapter->dev.devt = CXL_CARD_MKDEV(adapter);
	 */

	return device_register(&adapter->dev);
}

dev_t cxl_get_dev(void)
{
	return cxl_dev;
}

int __init cxl_file_init(void)
{
	int rc;

	/*
	 * If these change we really need to update API.  Either change some
	 * flags or update API version number CXL_API_VERSION.
	 */
	BUILD_BUG_ON(CXL_API_VERSION != 3);
	BUILD_BUG_ON(sizeof(struct cxl_ioctl_start_work) != 64);
	BUILD_BUG_ON(sizeof(struct cxl_event_header) != 8);
	BUILD_BUG_ON(sizeof(struct cxl_event_afu_interrupt) != 8);
	BUILD_BUG_ON(sizeof(struct cxl_event_data_storage) != 32);
	BUILD_BUG_ON(sizeof(struct cxl_event_afu_error) != 16);

	if ((rc = alloc_chrdev_region(&cxl_dev, 0, CXL_NUM_MINORS, "cxl"))) {
		pr_err("Unable to allocate CXL major number: %i\n", rc);
		return rc;
	}

	pr_devel("CXL device allocated, MAJOR %i\n", MAJOR(cxl_dev));

	cxl_class = class_create(THIS_MODULE, "cxl");
	if (IS_ERR(cxl_class)) {
		pr_err("Unable to create CXL class\n");
		rc = PTR_ERR(cxl_class);
		goto err;
	}
	cxl_class->devnode = cxl_devnode;

	return 0;

err:
	unregister_chrdev_region(cxl_dev, CXL_NUM_MINORS);
	return rc;
}

void cxl_file_exit(void)
{
	unregister_chrdev_region(cxl_dev, CXL_NUM_MINORS);
	class_destroy(cxl_class);
}
