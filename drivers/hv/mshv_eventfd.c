// SPDX-License-Identifier: GPL-2.0-only
/*
 * eventfd support for mshv
 *
 * Heavily inspired from KVM implementation of irqfd/ioeventfd. The basic
 * framework code is taken from the kvm implementation.
 *
 * All credits to kvm developers.
 */

#include <linux/syscalls.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/file.h>
#include <linux/list.h>
#include <linux/workqueue.h>
#include <linux/eventfd.h>

#if IS_ENABLED(CONFIG_X86_64)
#include <asm/apic.h>
#endif
#include <asm/mshyperv.h>

#include "mshv_eventfd.h"
#include "mshv.h"
#include "mshv_root.h"

static struct workqueue_struct *irqfd_cleanup_wq;

void mshv_register_irq_ack_notifier(struct mshv_partition *partition,
				    struct mshv_irq_ack_notifier *mian)
{
	mutex_lock(&partition->pt_irq_lock);
	hlist_add_head_rcu(&mian->link, &partition->irq_ack_notifier_list);
	mutex_unlock(&partition->pt_irq_lock);
}

void mshv_unregister_irq_ack_notifier(struct mshv_partition *partition,
				      struct mshv_irq_ack_notifier *mian)
{
	mutex_lock(&partition->pt_irq_lock);
	hlist_del_init_rcu(&mian->link);
	mutex_unlock(&partition->pt_irq_lock);
	synchronize_rcu();
}

bool mshv_notify_acked_gsi(struct mshv_partition *partition, int gsi)
{
	struct mshv_irq_ack_notifier *mian;
	bool acked = false;

	rcu_read_lock();
	hlist_for_each_entry_rcu(mian, &partition->irq_ack_notifier_list,
				 link) {
		if (mian->irq_ack_gsi == gsi) {
			mian->irq_acked(mian);
			acked = true;
		}
	}
	rcu_read_unlock();

	return acked;
}

#if IS_ENABLED(CONFIG_ARM64)
static inline bool hv_should_clear_interrupt(enum hv_interrupt_type type)
{
	return false;
}
#elif IS_ENABLED(CONFIG_X86_64)
static inline bool hv_should_clear_interrupt(enum hv_interrupt_type type)
{
	return type == HV_X64_INTERRUPT_TYPE_EXTINT;
}
#endif

static void mshv_irqfd_resampler_ack(struct mshv_irq_ack_notifier *mian)
{
	struct mshv_irqfd_resampler *resampler;
	struct mshv_partition *partition;
	struct mshv_irqfd *irqfd;
	int idx;

	resampler = container_of(mian, struct mshv_irqfd_resampler,
				 rsmplr_notifier);
	partition = resampler->rsmplr_partn;

	idx = srcu_read_lock(&partition->pt_irq_srcu);

	hlist_for_each_entry_rcu(irqfd, &resampler->rsmplr_irqfd_list,
				 irqfd_resampler_hnode) {
		if (hv_should_clear_interrupt(irqfd->irqfd_lapic_irq.lapic_control.interrupt_type))
			hv_call_clear_virtual_interrupt(partition->pt_id);

		eventfd_signal(irqfd->irqfd_resamplefd);
	}

	srcu_read_unlock(&partition->pt_irq_srcu, idx);
}

#if IS_ENABLED(CONFIG_X86_64)
static bool
mshv_vp_irq_vector_injected(union hv_vp_register_page_interrupt_vectors iv,
			    u32 vector)
{
	int i;

	for (i = 0; i < iv.vector_count; i++) {
		if (iv.vector[i] == vector)
			return true;
	}

	return false;
}

static int mshv_vp_irq_try_set_vector(struct mshv_vp *vp, u32 vector)
{
	union hv_vp_register_page_interrupt_vectors iv, new_iv;

	iv = vp->vp_register_page->interrupt_vectors;
	new_iv = iv;

	if (mshv_vp_irq_vector_injected(iv, vector))
		return 0;

	if (iv.vector_count >= HV_VP_REGISTER_PAGE_MAX_VECTOR_COUNT)
		return -ENOSPC;

	new_iv.vector[new_iv.vector_count++] = vector;

	if (cmpxchg(&vp->vp_register_page->interrupt_vectors.as_uint64,
		    iv.as_uint64, new_iv.as_uint64) != iv.as_uint64)
		return -EAGAIN;

	return 0;
}

static int mshv_vp_irq_set_vector(struct mshv_vp *vp, u32 vector)
{
	int ret;

	do {
		ret = mshv_vp_irq_try_set_vector(vp, vector);
	} while (ret == -EAGAIN && !need_resched());

	return ret;
}

/*
 * Try to raise irq for guest via shared vector array. hyp does the actual
 * inject of the interrupt.
 */
static int mshv_try_assert_irq_fast(struct mshv_irqfd *irqfd)
{
	struct mshv_partition *partition = irqfd->irqfd_partn;
	struct mshv_lapic_irq *irq = &irqfd->irqfd_lapic_irq;
	struct mshv_vp *vp;

	if (!(ms_hyperv.ext_features &
	      HV_VP_DISPATCH_INTERRUPT_INJECTION_AVAILABLE))
		return -EOPNOTSUPP;

	if (hv_scheduler_type != HV_SCHEDULER_TYPE_ROOT)
		return -EOPNOTSUPP;

	if (irq->lapic_control.logical_dest_mode)
		return -EOPNOTSUPP;

	vp = partition->pt_vp_array[irq->lapic_apic_id];

	if (!vp->vp_register_page)
		return -EOPNOTSUPP;

	if (mshv_vp_irq_set_vector(vp, irq->lapic_vector))
		return -EINVAL;

	if (vp->run.flags.root_sched_dispatched &&
	    vp->vp_register_page->interrupt_vectors.as_uint64)
		return -EBUSY;

	wake_up(&vp->run.vp_suspend_queue);

	return 0;
}
#else /* CONFIG_X86_64 */
static int mshv_try_assert_irq_fast(struct mshv_irqfd *irqfd)
{
	return -EOPNOTSUPP;
}
#endif

static void mshv_assert_irq_slow(struct mshv_irqfd *irqfd)
{
	struct mshv_partition *partition = irqfd->irqfd_partn;
	struct mshv_lapic_irq *irq = &irqfd->irqfd_lapic_irq;
	unsigned int seq;
	int idx;

	WARN_ON(irqfd->irqfd_resampler &&
		!irq->lapic_control.level_triggered);

	idx = srcu_read_lock(&partition->pt_irq_srcu);
	if (irqfd->irqfd_girq_ent.guest_irq_num) {
		if (!irqfd->irqfd_girq_ent.girq_entry_valid) {
			srcu_read_unlock(&partition->pt_irq_srcu, idx);
			return;
		}

		do {
			seq = read_seqcount_begin(&irqfd->irqfd_irqe_sc);
		} while (read_seqcount_retry(&irqfd->irqfd_irqe_sc, seq));
	}

	hv_call_assert_virtual_interrupt(irqfd->irqfd_partn->pt_id,
					 irq->lapic_vector, irq->lapic_apic_id,
					 irq->lapic_control);
	srcu_read_unlock(&partition->pt_irq_srcu, idx);
}

static void mshv_irqfd_resampler_shutdown(struct mshv_irqfd *irqfd)
{
	struct mshv_irqfd_resampler *rp = irqfd->irqfd_resampler;
	struct mshv_partition *pt = rp->rsmplr_partn;

	mutex_lock(&pt->irqfds_resampler_lock);

	hlist_del_rcu(&irqfd->irqfd_resampler_hnode);
	synchronize_srcu(&pt->pt_irq_srcu);

	if (hlist_empty(&rp->rsmplr_irqfd_list)) {
		hlist_del(&rp->rsmplr_hnode);
		mshv_unregister_irq_ack_notifier(pt, &rp->rsmplr_notifier);
		kfree(rp);
	}

	mutex_unlock(&pt->irqfds_resampler_lock);
}

/*
 * Race-free decouple logic (ordering is critical)
 */
static void mshv_irqfd_shutdown(struct work_struct *work)
{
	struct mshv_irqfd *irqfd =
			container_of(work, struct mshv_irqfd, irqfd_shutdown);

	/*
	 * Synchronize with the wait-queue and unhook ourselves to prevent
	 * further events.
	 */
	remove_wait_queue(irqfd->irqfd_wqh, &irqfd->irqfd_wait);

	if (irqfd->irqfd_resampler) {
		mshv_irqfd_resampler_shutdown(irqfd);
		eventfd_ctx_put(irqfd->irqfd_resamplefd);
	}

	/*
	 * It is now safe to release the object's resources
	 */
	eventfd_ctx_put(irqfd->irqfd_eventfd_ctx);
	kfree(irqfd);
}

/* assumes partition->pt_irqfds_lock is held */
static bool mshv_irqfd_is_active(struct mshv_irqfd *irqfd)
{
	return !hlist_unhashed(&irqfd->irqfd_hnode);
}

/*
 * Mark the irqfd as inactive and schedule it for removal
 *
 * assumes partition->pt_irqfds_lock is held
 */
static void mshv_irqfd_deactivate(struct mshv_irqfd *irqfd)
{
	if (!mshv_irqfd_is_active(irqfd))
		return;

	hlist_del(&irqfd->irqfd_hnode);

	queue_work(irqfd_cleanup_wq, &irqfd->irqfd_shutdown);
}

/*
 * Called with wqh->lock held and interrupts disabled
 */
static int mshv_irqfd_wakeup(wait_queue_entry_t *wait, unsigned int mode,
			     int sync, void *key)
{
	struct mshv_irqfd *irqfd = container_of(wait, struct mshv_irqfd,
						irqfd_wait);
	unsigned long flags = (unsigned long)key;
	int idx;
	unsigned int seq;
	struct mshv_partition *pt = irqfd->irqfd_partn;
	int ret = 0;

	if (flags & POLLIN) {
		u64 cnt;

		eventfd_ctx_do_read(irqfd->irqfd_eventfd_ctx, &cnt);
		idx = srcu_read_lock(&pt->pt_irq_srcu);
		do {
			seq = read_seqcount_begin(&irqfd->irqfd_irqe_sc);
		} while (read_seqcount_retry(&irqfd->irqfd_irqe_sc, seq));

		/* An event has been signaled, raise an interrupt */
		ret = mshv_try_assert_irq_fast(irqfd);
		if (ret)
			mshv_assert_irq_slow(irqfd);

		srcu_read_unlock(&pt->pt_irq_srcu, idx);

		ret = 1;
	}

	if (flags & POLLHUP) {
		/* The eventfd is closing, detach from the partition */
		unsigned long flags;

		spin_lock_irqsave(&pt->pt_irqfds_lock, flags);

		/*
		 * We must check if someone deactivated the irqfd before
		 * we could acquire the pt_irqfds_lock since the item is
		 * deactivated from the mshv side before it is unhooked from
		 * the wait-queue.  If it is already deactivated, we can
		 * simply return knowing the other side will cleanup for us.
		 * We cannot race against the irqfd going away since the
		 * other side is required to acquire wqh->lock, which we hold
		 */
		if (mshv_irqfd_is_active(irqfd))
			mshv_irqfd_deactivate(irqfd);

		spin_unlock_irqrestore(&pt->pt_irqfds_lock, flags);
	}

	return ret;
}

/* Must be called under pt_irqfds_lock */
static void mshv_irqfd_update(struct mshv_partition *pt,
			      struct mshv_irqfd *irqfd)
{
	write_seqcount_begin(&irqfd->irqfd_irqe_sc);
	irqfd->irqfd_girq_ent = mshv_ret_girq_entry(pt,
						    irqfd->irqfd_irqnum);
	mshv_copy_girq_info(&irqfd->irqfd_girq_ent, &irqfd->irqfd_lapic_irq);
	write_seqcount_end(&irqfd->irqfd_irqe_sc);
}

void mshv_irqfd_routing_update(struct mshv_partition *pt)
{
	struct mshv_irqfd *irqfd;

	spin_lock_irq(&pt->pt_irqfds_lock);
	hlist_for_each_entry(irqfd, &pt->pt_irqfds_list, irqfd_hnode)
		mshv_irqfd_update(pt, irqfd);
	spin_unlock_irq(&pt->pt_irqfds_lock);
}

static void mshv_irqfd_queue_proc(struct file *file, wait_queue_head_t *wqh,
				  poll_table *polltbl)
{
	struct mshv_irqfd *irqfd =
			container_of(polltbl, struct mshv_irqfd, irqfd_polltbl);

	irqfd->irqfd_wqh = wqh;
	add_wait_queue_priority(wqh, &irqfd->irqfd_wait);
}

static int mshv_irqfd_assign(struct mshv_partition *pt,
			     struct mshv_user_irqfd *args)
{
	struct eventfd_ctx *eventfd = NULL, *resamplefd = NULL;
	struct mshv_irqfd *irqfd, *tmp;
	unsigned int events;
	struct fd f;
	int ret;
	int idx;

	irqfd = kzalloc(sizeof(*irqfd), GFP_KERNEL);
	if (!irqfd)
		return -ENOMEM;

	irqfd->irqfd_partn = pt;
	irqfd->irqfd_irqnum = args->gsi;
	INIT_WORK(&irqfd->irqfd_shutdown, mshv_irqfd_shutdown);
	seqcount_spinlock_init(&irqfd->irqfd_irqe_sc, &pt->pt_irqfds_lock);

	f = fdget(args->fd);
	if (!fd_file(f)) {
		ret = -EBADF;
		goto out;
	}

	eventfd = eventfd_ctx_fileget(fd_file(f));
	if (IS_ERR(eventfd)) {
		ret = PTR_ERR(eventfd);
		goto fail;
	}

	irqfd->irqfd_eventfd_ctx = eventfd;

	if (args->flags & BIT(MSHV_IRQFD_BIT_RESAMPLE)) {
		struct mshv_irqfd_resampler *rp;

		resamplefd = eventfd_ctx_fdget(args->resamplefd);
		if (IS_ERR(resamplefd)) {
			ret = PTR_ERR(resamplefd);
			goto fail;
		}

		irqfd->irqfd_resamplefd = resamplefd;

		mutex_lock(&pt->irqfds_resampler_lock);

		hlist_for_each_entry(rp, &pt->irqfds_resampler_list,
				     rsmplr_hnode) {
			if (rp->rsmplr_notifier.irq_ack_gsi ==
							 irqfd->irqfd_irqnum) {
				irqfd->irqfd_resampler = rp;
				break;
			}
		}

		if (!irqfd->irqfd_resampler) {
			rp = kzalloc(sizeof(*rp), GFP_KERNEL_ACCOUNT);
			if (!rp) {
				ret = -ENOMEM;
				mutex_unlock(&pt->irqfds_resampler_lock);
				goto fail;
			}

			rp->rsmplr_partn = pt;
			INIT_HLIST_HEAD(&rp->rsmplr_irqfd_list);
			rp->rsmplr_notifier.irq_ack_gsi = irqfd->irqfd_irqnum;
			rp->rsmplr_notifier.irq_acked =
						      mshv_irqfd_resampler_ack;

			hlist_add_head(&rp->rsmplr_hnode,
				       &pt->irqfds_resampler_list);
			mshv_register_irq_ack_notifier(pt,
						       &rp->rsmplr_notifier);
			irqfd->irqfd_resampler = rp;
		}

		hlist_add_head_rcu(&irqfd->irqfd_resampler_hnode,
				   &irqfd->irqfd_resampler->rsmplr_irqfd_list);

		mutex_unlock(&pt->irqfds_resampler_lock);
	}

	/*
	 * Install our own custom wake-up handling so we are notified via
	 * a callback whenever someone signals the underlying eventfd
	 */
	init_waitqueue_func_entry(&irqfd->irqfd_wait, mshv_irqfd_wakeup);
	init_poll_funcptr(&irqfd->irqfd_polltbl, mshv_irqfd_queue_proc);

	spin_lock_irq(&pt->pt_irqfds_lock);
	if (args->flags & BIT(MSHV_IRQFD_BIT_RESAMPLE) &&
	    !irqfd->irqfd_lapic_irq.lapic_control.level_triggered) {
		/*
		 * Resample Fd must be for level triggered interrupt
		 * Otherwise return with failure
		 */
		spin_unlock_irq(&pt->pt_irqfds_lock);
		ret = -EINVAL;
		goto fail;
	}
	ret = 0;
	hlist_for_each_entry(tmp, &pt->pt_irqfds_list, irqfd_hnode) {
		if (irqfd->irqfd_eventfd_ctx != tmp->irqfd_eventfd_ctx)
			continue;
		/* This fd is used for another irq already. */
		ret = -EBUSY;
		spin_unlock_irq(&pt->pt_irqfds_lock);
		goto fail;
	}

	idx = srcu_read_lock(&pt->pt_irq_srcu);
	mshv_irqfd_update(pt, irqfd);
	hlist_add_head(&irqfd->irqfd_hnode, &pt->pt_irqfds_list);
	spin_unlock_irq(&pt->pt_irqfds_lock);

	/*
	 * Check if there was an event already pending on the eventfd
	 * before we registered, and trigger it as if we didn't miss it.
	 */
	events = vfs_poll(fd_file(f), &irqfd->irqfd_polltbl);

	if (events & POLLIN)
		mshv_assert_irq_slow(irqfd);

	srcu_read_unlock(&pt->pt_irq_srcu, idx);
	/*
	 * do not drop the file until the irqfd is fully initialized, otherwise
	 * we might race against the POLLHUP
	 */
	fdput(f);

	return 0;

fail:
	if (irqfd->irqfd_resampler)
		mshv_irqfd_resampler_shutdown(irqfd);

	if (resamplefd && !IS_ERR(resamplefd))
		eventfd_ctx_put(resamplefd);

	if (eventfd && !IS_ERR(eventfd))
		eventfd_ctx_put(eventfd);

	fdput(f);

out:
	kfree(irqfd);
	return ret;
}

/*
 * shutdown any irqfd's that match fd+gsi
 */
static int mshv_irqfd_deassign(struct mshv_partition *pt,
			       struct mshv_user_irqfd *args)
{
	struct mshv_irqfd *irqfd;
	struct hlist_node *n;
	struct eventfd_ctx *eventfd;

	eventfd = eventfd_ctx_fdget(args->fd);
	if (IS_ERR(eventfd))
		return PTR_ERR(eventfd);

	hlist_for_each_entry_safe(irqfd, n, &pt->pt_irqfds_list,
				  irqfd_hnode) {
		if (irqfd->irqfd_eventfd_ctx == eventfd &&
		    irqfd->irqfd_irqnum == args->gsi)

			mshv_irqfd_deactivate(irqfd);
	}

	eventfd_ctx_put(eventfd);

	/*
	 * Block until we know all outstanding shutdown jobs have completed
	 * so that we guarantee there will not be any more interrupts on this
	 * gsi once this deassign function returns.
	 */
	flush_workqueue(irqfd_cleanup_wq);

	return 0;
}

int mshv_set_unset_irqfd(struct mshv_partition *pt,
			 struct mshv_user_irqfd *args)
{
	if (args->flags & ~MSHV_IRQFD_FLAGS_MASK)
		return -EINVAL;

	if (args->flags & BIT(MSHV_IRQFD_BIT_DEASSIGN))
		return mshv_irqfd_deassign(pt, args);

	return mshv_irqfd_assign(pt, args);
}

/*
 * This function is called as the mshv VM fd is being released.
 * Shutdown all irqfds that still remain open
 */
static void mshv_irqfd_release(struct mshv_partition *pt)
{
	struct mshv_irqfd *irqfd;
	struct hlist_node *n;

	spin_lock_irq(&pt->pt_irqfds_lock);

	hlist_for_each_entry_safe(irqfd, n, &pt->pt_irqfds_list, irqfd_hnode)
		mshv_irqfd_deactivate(irqfd);

	spin_unlock_irq(&pt->pt_irqfds_lock);

	/*
	 * Block until we know all outstanding shutdown jobs have completed
	 * since we do not take a mshv_partition* reference.
	 */
	flush_workqueue(irqfd_cleanup_wq);
}

int mshv_irqfd_wq_init(void)
{
	irqfd_cleanup_wq = alloc_workqueue("mshv-irqfd-cleanup", 0, 0);
	if (!irqfd_cleanup_wq)
		return -ENOMEM;

	return 0;
}

void mshv_irqfd_wq_cleanup(void)
{
	destroy_workqueue(irqfd_cleanup_wq);
}

/*
 * --------------------------------------------------------------------
 * ioeventfd: translate a MMIO memory write to an eventfd signal.
 *
 * userspace can register a MMIO address with an eventfd for receiving
 * notification when the memory has been touched.
 * --------------------------------------------------------------------
 */

static void ioeventfd_release(struct mshv_ioeventfd *p, u64 partition_id)
{
	if (p->iovntfd_doorbell_id > 0)
		mshv_unregister_doorbell(partition_id, p->iovntfd_doorbell_id);
	eventfd_ctx_put(p->iovntfd_eventfd);
	kfree(p);
}

/* MMIO writes trigger an event if the addr/val match */
static void ioeventfd_mmio_write(int doorbell_id, void *data)
{
	struct mshv_partition *partition = (struct mshv_partition *)data;
	struct mshv_ioeventfd *p;

	rcu_read_lock();
	hlist_for_each_entry_rcu(p, &partition->ioeventfds_list, iovntfd_hnode)
		if (p->iovntfd_doorbell_id == doorbell_id) {
			eventfd_signal(p->iovntfd_eventfd);
			break;
		}

	rcu_read_unlock();
}

static bool ioeventfd_check_collision(struct mshv_partition *pt,
				      struct mshv_ioeventfd *p)
	__must_hold(&pt->mutex)
{
	struct mshv_ioeventfd *_p;

	hlist_for_each_entry(_p, &pt->ioeventfds_list, iovntfd_hnode)
		if (_p->iovntfd_addr == p->iovntfd_addr &&
		    _p->iovntfd_length == p->iovntfd_length &&
		    (_p->iovntfd_wildcard || p->iovntfd_wildcard ||
		     _p->iovntfd_datamatch == p->iovntfd_datamatch))
			return true;

	return false;
}

static int mshv_assign_ioeventfd(struct mshv_partition *pt,
				 struct mshv_user_ioeventfd *args)
	__must_hold(&pt->mutex)
{
	struct mshv_ioeventfd *p;
	struct eventfd_ctx *eventfd;
	u64 doorbell_flags = 0;
	int ret;

	/* This mutex is currently protecting ioeventfd.items list */
	WARN_ON_ONCE(!mutex_is_locked(&pt->pt_mutex));

	if (args->flags & BIT(MSHV_IOEVENTFD_BIT_PIO))
		return -EOPNOTSUPP;

	/* must be natural-word sized */
	switch (args->len) {
	case 0:
		doorbell_flags = HV_DOORBELL_FLAG_TRIGGER_SIZE_ANY;
		break;
	case 1:
		doorbell_flags = HV_DOORBELL_FLAG_TRIGGER_SIZE_BYTE;
		break;
	case 2:
		doorbell_flags = HV_DOORBELL_FLAG_TRIGGER_SIZE_WORD;
		break;
	case 4:
		doorbell_flags = HV_DOORBELL_FLAG_TRIGGER_SIZE_DWORD;
		break;
	case 8:
		doorbell_flags = HV_DOORBELL_FLAG_TRIGGER_SIZE_QWORD;
		break;
	default:
		return -EINVAL;
	}

	/* check for range overflow */
	if (args->addr + args->len < args->addr)
		return -EINVAL;

	/* check for extra flags that we don't understand */
	if (args->flags & ~MSHV_IOEVENTFD_FLAGS_MASK)
		return -EINVAL;

	eventfd = eventfd_ctx_fdget(args->fd);
	if (IS_ERR(eventfd))
		return PTR_ERR(eventfd);

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p) {
		ret = -ENOMEM;
		goto fail;
	}

	p->iovntfd_addr = args->addr;
	p->iovntfd_length  = args->len;
	p->iovntfd_eventfd = eventfd;

	/* The datamatch feature is optional, otherwise this is a wildcard */
	if (args->flags & BIT(MSHV_IOEVENTFD_BIT_DATAMATCH)) {
		p->iovntfd_datamatch = args->datamatch;
	} else {
		p->iovntfd_wildcard = true;
		doorbell_flags |= HV_DOORBELL_FLAG_TRIGGER_ANY_VALUE;
	}

	if (ioeventfd_check_collision(pt, p)) {
		ret = -EEXIST;
		goto unlock_fail;
	}

	ret = mshv_register_doorbell(pt->pt_id, ioeventfd_mmio_write,
				     (void *)pt, p->iovntfd_addr,
				     p->iovntfd_datamatch, doorbell_flags);
	if (ret < 0)
		goto unlock_fail;

	p->iovntfd_doorbell_id = ret;

	hlist_add_head_rcu(&p->iovntfd_hnode, &pt->ioeventfds_list);

	return 0;

unlock_fail:
	kfree(p);

fail:
	eventfd_ctx_put(eventfd);

	return ret;
}

static int mshv_deassign_ioeventfd(struct mshv_partition *pt,
				   struct mshv_user_ioeventfd *args)
	__must_hold(&pt->mutex)
{
	struct mshv_ioeventfd *p;
	struct eventfd_ctx *eventfd;
	struct hlist_node *n;
	int ret = -ENOENT;

	/* This mutex is currently protecting ioeventfd.items list */
	WARN_ON_ONCE(!mutex_is_locked(&pt->pt_mutex));

	eventfd = eventfd_ctx_fdget(args->fd);
	if (IS_ERR(eventfd))
		return PTR_ERR(eventfd);

	hlist_for_each_entry_safe(p, n, &pt->ioeventfds_list, iovntfd_hnode) {
		bool wildcard = !(args->flags & BIT(MSHV_IOEVENTFD_BIT_DATAMATCH));

		if (p->iovntfd_eventfd != eventfd  ||
		    p->iovntfd_addr != args->addr  ||
		    p->iovntfd_length != args->len ||
		    p->iovntfd_wildcard != wildcard)
			continue;

		if (!p->iovntfd_wildcard &&
		    p->iovntfd_datamatch != args->datamatch)
			continue;

		hlist_del_rcu(&p->iovntfd_hnode);
		synchronize_rcu();
		ioeventfd_release(p, pt->pt_id);
		ret = 0;
		break;
	}

	eventfd_ctx_put(eventfd);

	return ret;
}

int mshv_set_unset_ioeventfd(struct mshv_partition *pt,
			     struct mshv_user_ioeventfd *args)
	__must_hold(&pt->mutex)
{
	if ((args->flags & ~MSHV_IOEVENTFD_FLAGS_MASK) ||
	    mshv_field_nonzero(*args, rsvd))
		return -EINVAL;

	/* PIO not yet implemented */
	if (args->flags & BIT(MSHV_IOEVENTFD_BIT_PIO))
		return -EOPNOTSUPP;

	if (args->flags & BIT(MSHV_IOEVENTFD_BIT_DEASSIGN))
		return mshv_deassign_ioeventfd(pt, args);

	return mshv_assign_ioeventfd(pt, args);
}

void mshv_eventfd_init(struct mshv_partition *pt)
{
	spin_lock_init(&pt->pt_irqfds_lock);
	INIT_HLIST_HEAD(&pt->pt_irqfds_list);

	INIT_HLIST_HEAD(&pt->irqfds_resampler_list);
	mutex_init(&pt->irqfds_resampler_lock);

	INIT_HLIST_HEAD(&pt->ioeventfds_list);
}

void mshv_eventfd_release(struct mshv_partition *pt)
{
	struct hlist_head items;
	struct hlist_node *n;
	struct mshv_ioeventfd *p;

	hlist_move_list(&pt->ioeventfds_list, &items);
	synchronize_rcu();

	hlist_for_each_entry_safe(p, n, &items, iovntfd_hnode) {
		hlist_del(&p->iovntfd_hnode);
		ioeventfd_release(p, pt->pt_id);
	}

	mshv_irqfd_release(pt);
}
