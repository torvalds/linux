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
 * struct gzvm_kernel_irqfd: gzvm kernel irqfd descriptor.
 * @gzvm: Pointer to struct gzvm.
 * @wait: Wait queue entry.
 * @gsi: Used for level IRQ fast-path.
 * @eventfd: Used for setup/shutdown.
 * @list: struct list_head.
 * @pt: struct poll_table_struct.
 * @shutdown: struct work_struct.
 */
struct gzvm_kernel_irqfd {
	struct gzvm *gzvm;
	wait_queue_entry_t wait;

	int gsi;

	struct eventfd_ctx *eventfd;
	struct list_head list;
	poll_table pt;
	struct work_struct shutdown;
};

static struct workqueue_struct *irqfd_cleanup_wq;

/**
 * irqfd_set_irq(): irqfd to inject virtual interrupt.
 * @gzvm: Pointer to gzvm.
 * @irq: This is spi interrupt number (starts from 0 instead of 32).
 * @level: irq triggered level.
 */
static void irqfd_set_irq(struct gzvm *gzvm, u32 irq, int level)
{
	if (level)
		gzvm_irqchip_inject_irq(gzvm, 0, irq, level);
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
		irqfd_set_irq(gzvm, irqfd->gsi, 1);
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
	struct eventfd_ctx *eventfd = NULL;
	int ret;
	int idx;

	irqfd = kzalloc(sizeof(*irqfd), GFP_KERNEL_ACCOUNT);
	if (!irqfd)
		return -ENOMEM;

	irqfd->gzvm = gzvm;
	irqfd->gsi = args->gsi;

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

	vfs_poll(f.file, &irqfd->pt);

	srcu_read_unlock(&gzvm->irq_srcu, idx);

	/*
	 * do not drop the file until the irqfd is fully initialized, otherwise
	 * we might race against the EPOLLHUP
	 */
	fdput(f);
	return 0;

fail:
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
	if (init_srcu_struct(&gzvm->irq_srcu))
		return -EINVAL;
	INIT_HLIST_HEAD(&gzvm->irq_ack_notifier_list);

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
