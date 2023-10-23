// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#include <linux/eventfd.h>
#include <linux/syscalls.h>
#include <linux/gzvm_drv.h>
#include "gzvm_common.h"

struct gzvm_irq_ack_notifier {
	struct hlist_node link;
	unsigned int gsi;
	void (*irq_acked)(struct gzvm_irq_ack_notifier *ian);
};

/**
 * struct gzvm_kernel_irqfd_resampler - irqfd resampler descriptor.
 * @gzvm: Poiner to gzvm.
 * @list: List of resampling struct _irqfd objects sharing this gsi.
 *		    RCU list modified under gzvm->irqfds.resampler_lock.
 * @notifier: gzvm irq ack notifier.
 * @link: Entry in list of gzvm->irqfd.resampler_list.
 *		    Use for sharing esamplers among irqfds on the same gsi.
 *		    Accessed and modified under gzvm->irqfds.resampler_lock.
 *
 * Resampling irqfds are a special variety of irqfds used to emulate
 * level triggered interrupts.  The interrupt is asserted on eventfd
 * trigger.  On acknowledgment through the irq ack notifier, the
 * interrupt is de-asserted and userspace is notified through the
 * resamplefd.  All resamplers on the same gsi are de-asserted
 * together, so we don't need to track the state of each individual
 * user.  We can also therefore share the same irq source ID.
 */
struct gzvm_kernel_irqfd_resampler {
	struct gzvm *gzvm;

	struct list_head list;
	struct gzvm_irq_ack_notifier notifier;

	struct list_head link;
};

/**
 * struct gzvm_kernel_irqfd: gzvm kernel irqfd descriptor.
 * @gzvm: Pointer to struct gzvm.
 * @wait: Wait queue entry.
 * @gsi: Used for level IRQ fast-path.
 * @resampler: The resampler used by this irqfd (resampler-only).
 * @resamplefd: Eventfd notified on resample (resampler-only).
 * @resampler_link: Entry in list of irqfds for a resampler (resampler-only).
 * @eventfd: Used for setup/shutdown.
 * @list: struct list_head.
 * @pt: struct poll_table_struct.
 * @shutdown: struct work_struct.
 */
struct gzvm_kernel_irqfd {
	struct gzvm *gzvm;
	wait_queue_entry_t wait;

	int gsi;

	struct gzvm_kernel_irqfd_resampler *resampler;

	struct eventfd_ctx *resamplefd;

	struct list_head resampler_link;

	struct eventfd_ctx *eventfd;
	struct list_head list;
	poll_table pt;
	struct work_struct shutdown;
};

static struct workqueue_struct *irqfd_cleanup_wq;

/**
 * irqfd_set_spi(): irqfd to inject virtual interrupt.
 * @gzvm: Pointer to gzvm.
 * @irq_source_id: irq source id.
 * @irq: This is spi interrupt number (starts from 0 instead of 32).
 * @level: irq triggered level.
 * @line_status: irq status.
 */
static void irqfd_set_spi(struct gzvm *gzvm, int irq_source_id, u32 irq,
			  int level, bool line_status)
{
	if (level)
		gzvm_irqchip_inject_irq(gzvm, irq_source_id, 0, irq, level);
}

/**
 * irqfd_resampler_ack() - Notify all of the resampler irqfds using this GSI
 *			   when IRQ de-assert once.
 * @ian: Pointer to gzvm_irq_ack_notifier.
 *
 * Since resampler irqfds share an IRQ source ID, we de-assert once
 * then notify all of the resampler irqfds using this GSI.  We can't
 * do multiple de-asserts or we risk racing with incoming re-asserts.
 */
static void irqfd_resampler_ack(struct gzvm_irq_ack_notifier *ian)
{
	struct gzvm_kernel_irqfd_resampler *resampler;
	struct gzvm *gzvm;
	struct gzvm_kernel_irqfd *irqfd;
	int idx;

	resampler = container_of(ian,
				 struct gzvm_kernel_irqfd_resampler, notifier);
	gzvm = resampler->gzvm;

	irqfd_set_spi(gzvm, GZVM_IRQFD_RESAMPLE_IRQ_SOURCE_ID,
		      resampler->notifier.gsi, 0, false);

	idx = srcu_read_lock(&gzvm->irq_srcu);

	list_for_each_entry_srcu(irqfd, &resampler->list, resampler_link,
				 srcu_read_lock_held(&gzvm->irq_srcu)) {
		eventfd_signal(irqfd->resamplefd, 1);
	}

	srcu_read_unlock(&gzvm->irq_srcu, idx);
}

static void gzvm_register_irq_ack_notifier(struct gzvm *gzvm,
					   struct gzvm_irq_ack_notifier *ian)
{
	mutex_lock(&gzvm->irq_lock);
	hlist_add_head_rcu(&ian->link, &gzvm->irq_ack_notifier_list);
	mutex_unlock(&gzvm->irq_lock);
}

static void gzvm_unregister_irq_ack_notifier(struct gzvm *gzvm,
					     struct gzvm_irq_ack_notifier *ian)
{
	mutex_lock(&gzvm->irq_lock);
	hlist_del_init_rcu(&ian->link);
	mutex_unlock(&gzvm->irq_lock);
	synchronize_srcu(&gzvm->irq_srcu);
}

static void irqfd_resampler_shutdown(struct gzvm_kernel_irqfd *irqfd)
{
	struct gzvm_kernel_irqfd_resampler *resampler = irqfd->resampler;
	struct gzvm *gzvm = resampler->gzvm;

	mutex_lock(&gzvm->irqfds.resampler_lock);

	list_del_rcu(&irqfd->resampler_link);
	synchronize_srcu(&gzvm->irq_srcu);

	if (list_empty(&resampler->list)) {
		list_del(&resampler->link);
		gzvm_unregister_irq_ack_notifier(gzvm, &resampler->notifier);
		irqfd_set_spi(gzvm, GZVM_IRQFD_RESAMPLE_IRQ_SOURCE_ID,
			      resampler->notifier.gsi, 0, false);
		kfree(resampler);
	}

	mutex_unlock(&gzvm->irqfds.resampler_lock);
}

/**
 * irqfd_shutdown() - Race-free decouple logic (ordering is critical).
 * @work: Pointer to work_struct.
 */
static void irqfd_shutdown(struct work_struct *work)
{
	struct gzvm_kernel_irqfd *irqfd =
		container_of(work, struct gzvm_kernel_irqfd, shutdown);
	struct gzvm *gzvm = irqfd->gzvm;
	u64 cnt;

	/* Make sure irqfd has been initialized in assign path. */
	synchronize_srcu(&gzvm->irq_srcu);

	/*
	 * Synchronize with the wait-queue and unhook ourselves to prevent
	 * further events.
	 */
	eventfd_ctx_remove_wait_queue(irqfd->eventfd, &irqfd->wait, &cnt);

	if (irqfd->resampler) {
		irqfd_resampler_shutdown(irqfd);
		eventfd_ctx_put(irqfd->resamplefd);
	}

	/*
	 * It is now safe to release the object's resources
	 */
	eventfd_ctx_put(irqfd->eventfd);
	kfree(irqfd);
}

/**
 * irqfd_is_active() - Assumes gzvm->irqfds.lock is held.
 * @irqfd: Pointer to gzvm_kernel_irqfd.
 *
 * Return:
 * * true			- irqfd is active.
 */
static bool irqfd_is_active(struct gzvm_kernel_irqfd *irqfd)
{
	return list_empty(&irqfd->list) ? false : true;
}

/**
 * irqfd_deactivate() - Mark the irqfd as inactive and schedule it for removal.
 *			assumes gzvm->irqfds.lock is held.
 * @irqfd: Pointer to gzvm_kernel_irqfd.
 */
static void irqfd_deactivate(struct gzvm_kernel_irqfd *irqfd)
{
	if (!irqfd_is_active(irqfd))
		return;

	list_del_init(&irqfd->list);

	queue_work(irqfd_cleanup_wq, &irqfd->shutdown);
}

/**
 * irqfd_wakeup() - Callback of irqfd wait queue, would be woken by writing to
 *                  irqfd to do virtual interrupt injection.
 * @wait: Pointer to wait_queue_entry_t.
 * @mode: Unused.
 * @sync: Unused.
 * @key: Get flags about Epoll events.
 *
 * Return:
 * * 0			- Success
 */
static int irqfd_wakeup(wait_queue_entry_t *wait, unsigned int mode, int sync,
			void *key)
{
	struct gzvm_kernel_irqfd *irqfd =
		container_of(wait, struct gzvm_kernel_irqfd, wait);
	__poll_t flags = key_to_poll(key);
	struct gzvm *gzvm = irqfd->gzvm;

	if (flags & EPOLLIN) {
		u64 cnt;

		eventfd_ctx_do_read(irqfd->eventfd, &cnt);
		/* gzvm's irq injection is not blocked, don't need workq */
		irqfd_set_spi(gzvm, GZVM_USERSPACE_IRQ_SOURCE_ID, irqfd->gsi,
			      1, false);
	}

	if (flags & EPOLLHUP) {
		/* The eventfd is closing, detach from GZVM */
		unsigned long iflags;

		spin_lock_irqsave(&gzvm->irqfds.lock, iflags);

		/*
		 * Do more check if someone deactivated the irqfd before
		 * we could acquire the irqfds.lock.
		 */
		if (irqfd_is_active(irqfd))
			irqfd_deactivate(irqfd);

		spin_unlock_irqrestore(&gzvm->irqfds.lock, iflags);
	}

	return 0;
}

static void irqfd_ptable_queue_proc(struct file *file, wait_queue_head_t *wqh,
				    poll_table *pt)
{
	struct gzvm_kernel_irqfd *irqfd =
		container_of(pt, struct gzvm_kernel_irqfd, pt);
	add_wait_queue_priority(wqh, &irqfd->wait);
}

static int gzvm_irqfd_assign(struct gzvm *gzvm, struct gzvm_irqfd *args)
{
	struct gzvm_kernel_irqfd *irqfd, *tmp;
	struct fd f;
	struct eventfd_ctx *eventfd = NULL, *resamplefd = NULL;
	int ret;
	__poll_t events;
	int idx;

	irqfd = kzalloc(sizeof(*irqfd), GFP_KERNEL_ACCOUNT);
	if (!irqfd)
		return -ENOMEM;

	irqfd->gzvm = gzvm;
	irqfd->gsi = args->gsi;
	irqfd->resampler = NULL;

	INIT_LIST_HEAD(&irqfd->list);
	INIT_WORK(&irqfd->shutdown, irqfd_shutdown);

	f = fdget(args->fd);
	if (!f.file) {
		ret = -EBADF;
		goto out;
	}

	eventfd = eventfd_ctx_fileget(f.file);
	if (IS_ERR(eventfd)) {
		ret = PTR_ERR(eventfd);
		goto fail;
	}

	irqfd->eventfd = eventfd;

	if (args->flags & GZVM_IRQFD_FLAG_RESAMPLE) {
		struct gzvm_kernel_irqfd_resampler *resampler;

		resamplefd = eventfd_ctx_fdget(args->resamplefd);
		if (IS_ERR(resamplefd)) {
			ret = PTR_ERR(resamplefd);
			goto fail;
		}

		irqfd->resamplefd = resamplefd;
		INIT_LIST_HEAD(&irqfd->resampler_link);

		mutex_lock(&gzvm->irqfds.resampler_lock);

		list_for_each_entry(resampler,
				    &gzvm->irqfds.resampler_list, link) {
			if (resampler->notifier.gsi == irqfd->gsi) {
				irqfd->resampler = resampler;
				break;
			}
		}

		if (!irqfd->resampler) {
			resampler = kzalloc(sizeof(*resampler),
					    GFP_KERNEL_ACCOUNT);
			if (!resampler) {
				ret = -ENOMEM;
				mutex_unlock(&gzvm->irqfds.resampler_lock);
				goto fail;
			}

			resampler->gzvm = gzvm;
			INIT_LIST_HEAD(&resampler->list);
			resampler->notifier.gsi = irqfd->gsi;
			resampler->notifier.irq_acked = irqfd_resampler_ack;
			INIT_LIST_HEAD(&resampler->link);

			list_add(&resampler->link, &gzvm->irqfds.resampler_list);
			gzvm_register_irq_ack_notifier(gzvm,
						       &resampler->notifier);
			irqfd->resampler = resampler;
		}

		list_add_rcu(&irqfd->resampler_link, &irqfd->resampler->list);
		synchronize_srcu(&gzvm->irq_srcu);

		mutex_unlock(&gzvm->irqfds.resampler_lock);
	}

	/*
	 * Install our own custom wake-up handling so we are notified via
	 * a callback whenever someone signals the underlying eventfd
	 */
	init_waitqueue_func_entry(&irqfd->wait, irqfd_wakeup);
	init_poll_funcptr(&irqfd->pt, irqfd_ptable_queue_proc);

	spin_lock_irq(&gzvm->irqfds.lock);

	ret = 0;
	list_for_each_entry(tmp, &gzvm->irqfds.items, list) {
		if (irqfd->eventfd != tmp->eventfd)
			continue;
		/* This fd is used for another irq already. */
		pr_err("already used: gsi=%d fd=%d\n", args->gsi, args->fd);
		ret = -EBUSY;
		spin_unlock_irq(&gzvm->irqfds.lock);
		goto fail;
	}

	idx = srcu_read_lock(&gzvm->irq_srcu);

	list_add_tail(&irqfd->list, &gzvm->irqfds.items);

	spin_unlock_irq(&gzvm->irqfds.lock);

	/*
	 * Check if there was an event already pending on the eventfd
	 * before we registered, and trigger it as if we didn't miss it.
	 */
	events = vfs_poll(f.file, &irqfd->pt);

	/* In case there is already a pending event */
	if (events & EPOLLIN)
		irqfd_set_spi(gzvm, GZVM_IRQFD_RESAMPLE_IRQ_SOURCE_ID,
			      irqfd->gsi, 1, false);

	srcu_read_unlock(&gzvm->irq_srcu, idx);

	/*
	 * do not drop the file until the irqfd is fully initialized, otherwise
	 * we might race against the EPOLLHUP
	 */
	fdput(f);
	return 0;

fail:
	if (irqfd->resampler)
		irqfd_resampler_shutdown(irqfd);

	if (resamplefd && !IS_ERR(resamplefd))
		eventfd_ctx_put(resamplefd);

	if (eventfd && !IS_ERR(eventfd))
		eventfd_ctx_put(eventfd);

	fdput(f);

out:
	kfree(irqfd);
	return ret;
}

static void gzvm_notify_acked_gsi(struct gzvm *gzvm, int gsi)
{
	struct gzvm_irq_ack_notifier *gian;

	hlist_for_each_entry_srcu(gian, &gzvm->irq_ack_notifier_list,
				  link, srcu_read_lock_held(&gzvm->irq_srcu))
		if (gian->gsi == gsi)
			gian->irq_acked(gian);
}

void gzvm_notify_acked_irq(struct gzvm *gzvm, unsigned int gsi)
{
	int idx;

	idx = srcu_read_lock(&gzvm->irq_srcu);
	gzvm_notify_acked_gsi(gzvm, gsi);
	srcu_read_unlock(&gzvm->irq_srcu, idx);
}

/**
 * gzvm_irqfd_deassign() - Shutdown any irqfd's that match fd+gsi.
 * @gzvm: Pointer to gzvm.
 * @args: Pointer to gzvm_irqfd.
 *
 * Return:
 * * 0			- Success.
 * * Negative value	- Failure.
 */
static int gzvm_irqfd_deassign(struct gzvm *gzvm, struct gzvm_irqfd *args)
{
	struct gzvm_kernel_irqfd *irqfd, *tmp;
	struct eventfd_ctx *eventfd;

	eventfd = eventfd_ctx_fdget(args->fd);
	if (IS_ERR(eventfd))
		return PTR_ERR(eventfd);

	spin_lock_irq(&gzvm->irqfds.lock);

	list_for_each_entry_safe(irqfd, tmp, &gzvm->irqfds.items, list) {
		if (irqfd->eventfd == eventfd && irqfd->gsi == args->gsi)
			irqfd_deactivate(irqfd);
	}

	spin_unlock_irq(&gzvm->irqfds.lock);
	eventfd_ctx_put(eventfd);

	/*
	 * Block until we know all outstanding shutdown jobs have completed
	 * so that we guarantee there will not be any more interrupts on this
	 * gsi once this deassign function returns.
	 */
	flush_workqueue(irqfd_cleanup_wq);

	return 0;
}

int gzvm_irqfd(struct gzvm *gzvm, struct gzvm_irqfd *args)
{
	for (int i = 0; i < ARRAY_SIZE(args->pad); i++) {
		if (args->pad[i])
			return -EINVAL;
	}

	if (args->flags &
	    ~(GZVM_IRQFD_FLAG_DEASSIGN | GZVM_IRQFD_FLAG_RESAMPLE))
		return -EINVAL;

	if (args->flags & GZVM_IRQFD_FLAG_DEASSIGN)
		return gzvm_irqfd_deassign(gzvm, args);

	return gzvm_irqfd_assign(gzvm, args);
}

/**
 * gzvm_vm_irqfd_init() - Initialize irqfd data structure per VM
 *
 * @gzvm: Pointer to struct gzvm.
 *
 * Return:
 * * 0			- Success.
 * * Negative		- Failure.
 */
int gzvm_vm_irqfd_init(struct gzvm *gzvm)
{
	mutex_init(&gzvm->irq_lock);

	spin_lock_init(&gzvm->irqfds.lock);
	INIT_LIST_HEAD(&gzvm->irqfds.items);
	INIT_LIST_HEAD(&gzvm->irqfds.resampler_list);
	if (init_srcu_struct(&gzvm->irq_srcu))
		return -EINVAL;
	INIT_HLIST_HEAD(&gzvm->irq_ack_notifier_list);
	mutex_init(&gzvm->irqfds.resampler_lock);

	return 0;
}

/**
 * gzvm_vm_irqfd_release() - This function is called as the gzvm VM fd is being
 *			  released. Shutdown all irqfds that still remain open.
 * @gzvm: Pointer to gzvm.
 */
void gzvm_vm_irqfd_release(struct gzvm *gzvm)
{
	struct gzvm_kernel_irqfd *irqfd, *tmp;

	spin_lock_irq(&gzvm->irqfds.lock);

	list_for_each_entry_safe(irqfd, tmp, &gzvm->irqfds.items, list)
		irqfd_deactivate(irqfd);

	spin_unlock_irq(&gzvm->irqfds.lock);

	/*
	 * Block until we know all outstanding shutdown jobs have completed.
	 */
	flush_workqueue(irqfd_cleanup_wq);
}

/**
 * gzvm_drv_irqfd_init() - Erase flushing work items when a VM exits.
 *
 * Return:
 * * 0			- Success.
 * * Negative		- Failure.
 *
 * Create a host-wide workqueue for issuing deferred shutdown requests
 * aggregated from all vm* instances. We need our own isolated
 * queue to ease flushing work items when a VM exits.
 */
int gzvm_drv_irqfd_init(void)
{
	irqfd_cleanup_wq = alloc_workqueue("gzvm-irqfd-cleanup", 0, 0);
	if (!irqfd_cleanup_wq)
		return -ENOMEM;

	return 0;
}

void gzvm_drv_irqfd_exit(void)
{
	destroy_workqueue(irqfd_cleanup_wq);
}
