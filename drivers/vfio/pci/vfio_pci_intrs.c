/*
 * VFIO PCI interrupt handling
 *
 * Copyright (C) 2012 Red Hat, Inc.  All rights reserved.
 *     Author: Alex Williamson <alex.williamson@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Derived from original vfio:
 * Copyright 2010 Cisco Systems, Inc.  All rights reserved.
 * Author: Tom Lyon, pugs@cisco.com
 */

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/eventfd.h>
#include <linux/pci.h>
#include <linux/file.h>
#include <linux/poll.h>
#include <linux/vfio.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/slab.h>

#include "vfio_pci_private.h"

/*
 * IRQfd - generic
 */
struct virqfd {
	struct vfio_pci_device	*vdev;
	struct eventfd_ctx	*eventfd;
	int			(*handler)(struct vfio_pci_device *, void *);
	void			(*thread)(struct vfio_pci_device *, void *);
	void			*data;
	struct work_struct	inject;
	wait_queue_t		wait;
	poll_table		pt;
	struct work_struct	shutdown;
	struct virqfd		**pvirqfd;
};

static struct workqueue_struct *vfio_irqfd_cleanup_wq;

int __init vfio_pci_virqfd_init(void)
{
	vfio_irqfd_cleanup_wq =
		create_singlethread_workqueue("vfio-irqfd-cleanup");
	if (!vfio_irqfd_cleanup_wq)
		return -ENOMEM;

	return 0;
}

void vfio_pci_virqfd_exit(void)
{
	destroy_workqueue(vfio_irqfd_cleanup_wq);
}

static void virqfd_deactivate(struct virqfd *virqfd)
{
	queue_work(vfio_irqfd_cleanup_wq, &virqfd->shutdown);
}

static int virqfd_wakeup(wait_queue_t *wait, unsigned mode, int sync, void *key)
{
	struct virqfd *virqfd = container_of(wait, struct virqfd, wait);
	unsigned long flags = (unsigned long)key;

	if (flags & POLLIN) {
		/* An event has been signaled, call function */
		if ((!virqfd->handler ||
		     virqfd->handler(virqfd->vdev, virqfd->data)) &&
		    virqfd->thread)
			schedule_work(&virqfd->inject);
	}

	if (flags & POLLHUP) {
		unsigned long flags;
		spin_lock_irqsave(&virqfd->vdev->irqlock, flags);

		/*
		 * The eventfd is closing, if the virqfd has not yet been
		 * queued for release, as determined by testing whether the
		 * vdev pointer to it is still valid, queue it now.  As
		 * with kvm irqfds, we know we won't race against the virqfd
		 * going away because we hold wqh->lock to get here.
		 */
		if (*(virqfd->pvirqfd) == virqfd) {
			*(virqfd->pvirqfd) = NULL;
			virqfd_deactivate(virqfd);
		}

		spin_unlock_irqrestore(&virqfd->vdev->irqlock, flags);
	}

	return 0;
}

static void virqfd_ptable_queue_proc(struct file *file,
				     wait_queue_head_t *wqh, poll_table *pt)
{
	struct virqfd *virqfd = container_of(pt, struct virqfd, pt);
	add_wait_queue(wqh, &virqfd->wait);
}

static void virqfd_shutdown(struct work_struct *work)
{
	struct virqfd *virqfd = container_of(work, struct virqfd, shutdown);
	u64 cnt;

	eventfd_ctx_remove_wait_queue(virqfd->eventfd, &virqfd->wait, &cnt);
	flush_work(&virqfd->inject);
	eventfd_ctx_put(virqfd->eventfd);

	kfree(virqfd);
}

static void virqfd_inject(struct work_struct *work)
{
	struct virqfd *virqfd = container_of(work, struct virqfd, inject);
	if (virqfd->thread)
		virqfd->thread(virqfd->vdev, virqfd->data);
}

static int virqfd_enable(struct vfio_pci_device *vdev,
			 int (*handler)(struct vfio_pci_device *, void *),
			 void (*thread)(struct vfio_pci_device *, void *),
			 void *data, struct virqfd **pvirqfd, int fd)
{
	struct fd irqfd;
	struct eventfd_ctx *ctx;
	struct virqfd *virqfd;
	int ret = 0;
	unsigned int events;

	virqfd = kzalloc(sizeof(*virqfd), GFP_KERNEL);
	if (!virqfd)
		return -ENOMEM;

	virqfd->pvirqfd = pvirqfd;
	virqfd->vdev = vdev;
	virqfd->handler = handler;
	virqfd->thread = thread;
	virqfd->data = data;

	INIT_WORK(&virqfd->shutdown, virqfd_shutdown);
	INIT_WORK(&virqfd->inject, virqfd_inject);

	irqfd = fdget(fd);
	if (!irqfd.file) {
		ret = -EBADF;
		goto err_fd;
	}

	ctx = eventfd_ctx_fileget(irqfd.file);
	if (IS_ERR(ctx)) {
		ret = PTR_ERR(ctx);
		goto err_ctx;
	}

	virqfd->eventfd = ctx;

	/*
	 * virqfds can be released by closing the eventfd or directly
	 * through ioctl.  These are both done through a workqueue, so
	 * we update the pointer to the virqfd under lock to avoid
	 * pushing multiple jobs to release the same virqfd.
	 */
	spin_lock_irq(&vdev->irqlock);

	if (*pvirqfd) {
		spin_unlock_irq(&vdev->irqlock);
		ret = -EBUSY;
		goto err_busy;
	}
	*pvirqfd = virqfd;

	spin_unlock_irq(&vdev->irqlock);

	/*
	 * Install our own custom wake-up handling so we are notified via
	 * a callback whenever someone signals the underlying eventfd.
	 */
	init_waitqueue_func_entry(&virqfd->wait, virqfd_wakeup);
	init_poll_funcptr(&virqfd->pt, virqfd_ptable_queue_proc);

	events = irqfd.file->f_op->poll(irqfd.file, &virqfd->pt);

	/*
	 * Check if there was an event already pending on the eventfd
	 * before we registered and trigger it as if we didn't miss it.
	 */
	if (events & POLLIN) {
		if ((!handler || handler(vdev, data)) && thread)
			schedule_work(&virqfd->inject);
	}

	/*
	 * Do not drop the file until the irqfd is fully initialized,
	 * otherwise we might race against the POLLHUP.
	 */
	fdput(irqfd);

	return 0;
err_busy:
	eventfd_ctx_put(ctx);
err_ctx:
	fdput(irqfd);
err_fd:
	kfree(virqfd);

	return ret;
}

static void virqfd_disable(struct vfio_pci_device *vdev,
			   struct virqfd **pvirqfd)
{
	unsigned long flags;

	spin_lock_irqsave(&vdev->irqlock, flags);

	if (*pvirqfd) {
		virqfd_deactivate(*pvirqfd);
		*pvirqfd = NULL;
	}

	spin_unlock_irqrestore(&vdev->irqlock, flags);

	/*
	 * Block until we know all outstanding shutdown jobs have completed.
	 * Even if we don't queue the job, flush the wq to be sure it's
	 * been released.
	 */
	flush_workqueue(vfio_irqfd_cleanup_wq);
}

/*
 * INTx
 */
static void vfio_send_intx_eventfd(struct vfio_pci_device *vdev, void *unused)
{
	if (likely(is_intx(vdev) && !vdev->virq_disabled))
		eventfd_signal(vdev->ctx[0].trigger, 1);
}

void vfio_pci_intx_mask(struct vfio_pci_device *vdev)
{
	struct pci_dev *pdev = vdev->pdev;
	unsigned long flags;

	spin_lock_irqsave(&vdev->irqlock, flags);

	/*
	 * Masking can come from interrupt, ioctl, or config space
	 * via INTx disable.  The latter means this can get called
	 * even when not using intx delivery.  In this case, just
	 * try to have the physical bit follow the virtual bit.
	 */
	if (unlikely(!is_intx(vdev))) {
		if (vdev->pci_2_3)
			pci_intx(pdev, 0);
	} else if (!vdev->ctx[0].masked) {
		/*
		 * Can't use check_and_mask here because we always want to
		 * mask, not just when something is pending.
		 */
		if (vdev->pci_2_3)
			pci_intx(pdev, 0);
		else
			disable_irq_nosync(pdev->irq);

		vdev->ctx[0].masked = true;
	}

	spin_unlock_irqrestore(&vdev->irqlock, flags);
}

/*
 * If this is triggered by an eventfd, we can't call eventfd_signal
 * or else we'll deadlock on the eventfd wait queue.  Return >0 when
 * a signal is necessary, which can then be handled via a work queue
 * or directly depending on the caller.
 */
static int vfio_pci_intx_unmask_handler(struct vfio_pci_device *vdev,
					void *unused)
{
	struct pci_dev *pdev = vdev->pdev;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&vdev->irqlock, flags);

	/*
	 * Unmasking comes from ioctl or config, so again, have the
	 * physical bit follow the virtual even when not using INTx.
	 */
	if (unlikely(!is_intx(vdev))) {
		if (vdev->pci_2_3)
			pci_intx(pdev, 1);
	} else if (vdev->ctx[0].masked && !vdev->virq_disabled) {
		/*
		 * A pending interrupt here would immediately trigger,
		 * but we can avoid that overhead by just re-sending
		 * the interrupt to the user.
		 */
		if (vdev->pci_2_3) {
			if (!pci_check_and_unmask_intx(pdev))
				ret = 1;
		} else
			enable_irq(pdev->irq);

		vdev->ctx[0].masked = (ret > 0);
	}

	spin_unlock_irqrestore(&vdev->irqlock, flags);

	return ret;
}

void vfio_pci_intx_unmask(struct vfio_pci_device *vdev)
{
	if (vfio_pci_intx_unmask_handler(vdev, NULL) > 0)
		vfio_send_intx_eventfd(vdev, NULL);
}

static irqreturn_t vfio_intx_handler(int irq, void *dev_id)
{
	struct vfio_pci_device *vdev = dev_id;
	unsigned long flags;
	int ret = IRQ_NONE;

	spin_lock_irqsave(&vdev->irqlock, flags);

	if (!vdev->pci_2_3) {
		disable_irq_nosync(vdev->pdev->irq);
		vdev->ctx[0].masked = true;
		ret = IRQ_HANDLED;
	} else if (!vdev->ctx[0].masked &&  /* may be shared */
		   pci_check_and_mask_intx(vdev->pdev)) {
		vdev->ctx[0].masked = true;
		ret = IRQ_HANDLED;
	}

	spin_unlock_irqrestore(&vdev->irqlock, flags);

	if (ret == IRQ_HANDLED)
		vfio_send_intx_eventfd(vdev, NULL);

	return ret;
}

static int vfio_intx_enable(struct vfio_pci_device *vdev)
{
	if (!is_irq_none(vdev))
		return -EINVAL;

	if (!vdev->pdev->irq)
		return -ENODEV;

	vdev->ctx = kzalloc(sizeof(struct vfio_pci_irq_ctx), GFP_KERNEL);
	if (!vdev->ctx)
		return -ENOMEM;

	vdev->num_ctx = 1;

	/*
	 * If the virtual interrupt is masked, restore it.  Devices
	 * supporting DisINTx can be masked at the hardware level
	 * here, non-PCI-2.3 devices will have to wait until the
	 * interrupt is enabled.
	 */
	vdev->ctx[0].masked = vdev->virq_disabled;
	if (vdev->pci_2_3)
		pci_intx(vdev->pdev, !vdev->ctx[0].masked);

	vdev->irq_type = VFIO_PCI_INTX_IRQ_INDEX;

	return 0;
}

static int vfio_intx_set_signal(struct vfio_pci_device *vdev, int fd)
{
	struct pci_dev *pdev = vdev->pdev;
	unsigned long irqflags = IRQF_SHARED;
	struct eventfd_ctx *trigger;
	unsigned long flags;
	int ret;

	if (vdev->ctx[0].trigger) {
		free_irq(pdev->irq, vdev);
		kfree(vdev->ctx[0].name);
		eventfd_ctx_put(vdev->ctx[0].trigger);
		vdev->ctx[0].trigger = NULL;
	}

	if (fd < 0) /* Disable only */
		return 0;

	vdev->ctx[0].name = kasprintf(GFP_KERNEL, "vfio-intx(%s)",
				      pci_name(pdev));
	if (!vdev->ctx[0].name)
		return -ENOMEM;

	trigger = eventfd_ctx_fdget(fd);
	if (IS_ERR(trigger)) {
		kfree(vdev->ctx[0].name);
		return PTR_ERR(trigger);
	}

	vdev->ctx[0].trigger = trigger;

	if (!vdev->pci_2_3)
		irqflags = 0;

	ret = request_irq(pdev->irq, vfio_intx_handler,
			  irqflags, vdev->ctx[0].name, vdev);
	if (ret) {
		vdev->ctx[0].trigger = NULL;
		kfree(vdev->ctx[0].name);
		eventfd_ctx_put(trigger);
		return ret;
	}

	/*
	 * INTx disable will stick across the new irq setup,
	 * disable_irq won't.
	 */
	spin_lock_irqsave(&vdev->irqlock, flags);
	if (!vdev->pci_2_3 && vdev->ctx[0].masked)
		disable_irq_nosync(pdev->irq);
	spin_unlock_irqrestore(&vdev->irqlock, flags);

	return 0;
}

static void vfio_intx_disable(struct vfio_pci_device *vdev)
{
	vfio_intx_set_signal(vdev, -1);
	virqfd_disable(vdev, &vdev->ctx[0].unmask);
	virqfd_disable(vdev, &vdev->ctx[0].mask);
	vdev->irq_type = VFIO_PCI_NUM_IRQS;
	vdev->num_ctx = 0;
	kfree(vdev->ctx);
}

/*
 * MSI/MSI-X
 */
static irqreturn_t vfio_msihandler(int irq, void *arg)
{
	struct eventfd_ctx *trigger = arg;

	eventfd_signal(trigger, 1);
	return IRQ_HANDLED;
}

static int vfio_msi_enable(struct vfio_pci_device *vdev, int nvec, bool msix)
{
	struct pci_dev *pdev = vdev->pdev;
	int ret;

	if (!is_irq_none(vdev))
		return -EINVAL;

	vdev->ctx = kzalloc(nvec * sizeof(struct vfio_pci_irq_ctx), GFP_KERNEL);
	if (!vdev->ctx)
		return -ENOMEM;

	if (msix) {
		int i;

		vdev->msix = kzalloc(nvec * sizeof(struct msix_entry),
				     GFP_KERNEL);
		if (!vdev->msix) {
			kfree(vdev->ctx);
			return -ENOMEM;
		}

		for (i = 0; i < nvec; i++)
			vdev->msix[i].entry = i;

		ret = pci_enable_msix(pdev, vdev->msix, nvec);
		if (ret) {
			kfree(vdev->msix);
			kfree(vdev->ctx);
			return ret;
		}
	} else {
		ret = pci_enable_msi_block(pdev, nvec);
		if (ret) {
			kfree(vdev->ctx);
			return ret;
		}
	}

	vdev->num_ctx = nvec;
	vdev->irq_type = msix ? VFIO_PCI_MSIX_IRQ_INDEX :
				VFIO_PCI_MSI_IRQ_INDEX;

	if (!msix) {
		/*
		 * Compute the virtual hardware field for max msi vectors -
		 * it is the log base 2 of the number of vectors.
		 */
		vdev->msi_qmax = fls(nvec * 2 - 1) - 1;
	}

	return 0;
}

static int vfio_msi_set_vector_signal(struct vfio_pci_device *vdev,
				      int vector, int fd, bool msix)
{
	struct pci_dev *pdev = vdev->pdev;
	int irq = msix ? vdev->msix[vector].vector : pdev->irq + vector;
	char *name = msix ? "vfio-msix" : "vfio-msi";
	struct eventfd_ctx *trigger;
	int ret;

	if (vector >= vdev->num_ctx)
		return -EINVAL;

	if (vdev->ctx[vector].trigger) {
		free_irq(irq, vdev->ctx[vector].trigger);
		kfree(vdev->ctx[vector].name);
		eventfd_ctx_put(vdev->ctx[vector].trigger);
		vdev->ctx[vector].trigger = NULL;
	}

	if (fd < 0)
		return 0;

	vdev->ctx[vector].name = kasprintf(GFP_KERNEL, "%s[%d](%s)",
					   name, vector, pci_name(pdev));
	if (!vdev->ctx[vector].name)
		return -ENOMEM;

	trigger = eventfd_ctx_fdget(fd);
	if (IS_ERR(trigger)) {
		kfree(vdev->ctx[vector].name);
		return PTR_ERR(trigger);
	}

	ret = request_irq(irq, vfio_msihandler, 0,
			  vdev->ctx[vector].name, trigger);
	if (ret) {
		kfree(vdev->ctx[vector].name);
		eventfd_ctx_put(trigger);
		return ret;
	}

	vdev->ctx[vector].trigger = trigger;

	return 0;
}

static int vfio_msi_set_block(struct vfio_pci_device *vdev, unsigned start,
			      unsigned count, int32_t *fds, bool msix)
{
	int i, j, ret = 0;

	if (start + count > vdev->num_ctx)
		return -EINVAL;

	for (i = 0, j = start; i < count && !ret; i++, j++) {
		int fd = fds ? fds[i] : -1;
		ret = vfio_msi_set_vector_signal(vdev, j, fd, msix);
	}

	if (ret) {
		for (--j; j >= start; j--)
			vfio_msi_set_vector_signal(vdev, j, -1, msix);
	}

	return ret;
}

static void vfio_msi_disable(struct vfio_pci_device *vdev, bool msix)
{
	struct pci_dev *pdev = vdev->pdev;
	int i;

	vfio_msi_set_block(vdev, 0, vdev->num_ctx, NULL, msix);

	for (i = 0; i < vdev->num_ctx; i++) {
		virqfd_disable(vdev, &vdev->ctx[i].unmask);
		virqfd_disable(vdev, &vdev->ctx[i].mask);
	}

	if (msix) {
		pci_disable_msix(vdev->pdev);
		kfree(vdev->msix);
	} else
		pci_disable_msi(pdev);

	vdev->irq_type = VFIO_PCI_NUM_IRQS;
	vdev->num_ctx = 0;
	kfree(vdev->ctx);
}

/*
 * IOCTL support
 */
static int vfio_pci_set_intx_unmask(struct vfio_pci_device *vdev,
				    unsigned index, unsigned start,
				    unsigned count, uint32_t flags, void *data)
{
	if (!is_intx(vdev) || start != 0 || count != 1)
		return -EINVAL;

	if (flags & VFIO_IRQ_SET_DATA_NONE) {
		vfio_pci_intx_unmask(vdev);
	} else if (flags & VFIO_IRQ_SET_DATA_BOOL) {
		uint8_t unmask = *(uint8_t *)data;
		if (unmask)
			vfio_pci_intx_unmask(vdev);
	} else if (flags & VFIO_IRQ_SET_DATA_EVENTFD) {
		int32_t fd = *(int32_t *)data;
		if (fd >= 0)
			return virqfd_enable(vdev, vfio_pci_intx_unmask_handler,
					     vfio_send_intx_eventfd, NULL,
					     &vdev->ctx[0].unmask, fd);

		virqfd_disable(vdev, &vdev->ctx[0].unmask);
	}

	return 0;
}

static int vfio_pci_set_intx_mask(struct vfio_pci_device *vdev,
				  unsigned index, unsigned start,
				  unsigned count, uint32_t flags, void *data)
{
	if (!is_intx(vdev) || start != 0 || count != 1)
		return -EINVAL;

	if (flags & VFIO_IRQ_SET_DATA_NONE) {
		vfio_pci_intx_mask(vdev);
	} else if (flags & VFIO_IRQ_SET_DATA_BOOL) {
		uint8_t mask = *(uint8_t *)data;
		if (mask)
			vfio_pci_intx_mask(vdev);
	} else if (flags & VFIO_IRQ_SET_DATA_EVENTFD) {
		return -ENOTTY; /* XXX implement me */
	}

	return 0;
}

static int vfio_pci_set_intx_trigger(struct vfio_pci_device *vdev,
				     unsigned index, unsigned start,
				     unsigned count, uint32_t flags, void *data)
{
	if (is_intx(vdev) && !count && (flags & VFIO_IRQ_SET_DATA_NONE)) {
		vfio_intx_disable(vdev);
		return 0;
	}

	if (!(is_intx(vdev) || is_irq_none(vdev)) || start != 0 || count != 1)
		return -EINVAL;

	if (flags & VFIO_IRQ_SET_DATA_EVENTFD) {
		int32_t fd = *(int32_t *)data;
		int ret;

		if (is_intx(vdev))
			return vfio_intx_set_signal(vdev, fd);

		ret = vfio_intx_enable(vdev);
		if (ret)
			return ret;

		ret = vfio_intx_set_signal(vdev, fd);
		if (ret)
			vfio_intx_disable(vdev);

		return ret;
	}

	if (!is_intx(vdev))
		return -EINVAL;

	if (flags & VFIO_IRQ_SET_DATA_NONE) {
		vfio_send_intx_eventfd(vdev, NULL);
	} else if (flags & VFIO_IRQ_SET_DATA_BOOL) {
		uint8_t trigger = *(uint8_t *)data;
		if (trigger)
			vfio_send_intx_eventfd(vdev, NULL);
	}
	return 0;
}

static int vfio_pci_set_msi_trigger(struct vfio_pci_device *vdev,
				    unsigned index, unsigned start,
				    unsigned count, uint32_t flags, void *data)
{
	int i;
	bool msix = (index == VFIO_PCI_MSIX_IRQ_INDEX) ? true : false;

	if (irq_is(vdev, index) && !count && (flags & VFIO_IRQ_SET_DATA_NONE)) {
		vfio_msi_disable(vdev, msix);
		return 0;
	}

	if (!(irq_is(vdev, index) || is_irq_none(vdev)))
		return -EINVAL;

	if (flags & VFIO_IRQ_SET_DATA_EVENTFD) {
		int32_t *fds = data;
		int ret;

		if (vdev->irq_type == index)
			return vfio_msi_set_block(vdev, start, count,
						  fds, msix);

		ret = vfio_msi_enable(vdev, start + count, msix);
		if (ret)
			return ret;

		ret = vfio_msi_set_block(vdev, start, count, fds, msix);
		if (ret)
			vfio_msi_disable(vdev, msix);

		return ret;
	}

	if (!irq_is(vdev, index) || start + count > vdev->num_ctx)
		return -EINVAL;

	for (i = start; i < start + count; i++) {
		if (!vdev->ctx[i].trigger)
			continue;
		if (flags & VFIO_IRQ_SET_DATA_NONE) {
			eventfd_signal(vdev->ctx[i].trigger, 1);
		} else if (flags & VFIO_IRQ_SET_DATA_BOOL) {
			uint8_t *bools = data;
			if (bools[i - start])
				eventfd_signal(vdev->ctx[i].trigger, 1);
		}
	}
	return 0;
}

static int vfio_pci_set_err_trigger(struct vfio_pci_device *vdev,
				    unsigned index, unsigned start,
				    unsigned count, uint32_t flags, void *data)
{
	int32_t fd = *(int32_t *)data;
	struct pci_dev *pdev = vdev->pdev;

	if ((index != VFIO_PCI_ERR_IRQ_INDEX) ||
	    !(flags & VFIO_IRQ_SET_DATA_TYPE_MASK))
		return -EINVAL;

	/*
	 * device_lock synchronizes setting and checking of
	 * err_trigger. The vfio_pci_aer_err_detected() is also
	 * called with device_lock held.
	 */

	/* DATA_NONE/DATA_BOOL enables loopback testing */

	if (flags & VFIO_IRQ_SET_DATA_NONE) {
		device_lock(&pdev->dev);
		if (vdev->err_trigger)
			eventfd_signal(vdev->err_trigger, 1);
		device_unlock(&pdev->dev);
		return 0;
	} else if (flags & VFIO_IRQ_SET_DATA_BOOL) {
		uint8_t trigger = *(uint8_t *)data;
		device_lock(&pdev->dev);
		if (trigger && vdev->err_trigger)
			eventfd_signal(vdev->err_trigger, 1);
		device_unlock(&pdev->dev);
		return 0;
	}

	/* Handle SET_DATA_EVENTFD */

	if (fd == -1) {
		device_lock(&pdev->dev);
		if (vdev->err_trigger)
			eventfd_ctx_put(vdev->err_trigger);
		vdev->err_trigger = NULL;
		device_unlock(&pdev->dev);
		return 0;
	} else if (fd >= 0) {
		struct eventfd_ctx *efdctx;
		efdctx = eventfd_ctx_fdget(fd);
		if (IS_ERR(efdctx))
			return PTR_ERR(efdctx);
		device_lock(&pdev->dev);
		if (vdev->err_trigger)
			eventfd_ctx_put(vdev->err_trigger);
		vdev->err_trigger = efdctx;
		device_unlock(&pdev->dev);
		return 0;
	} else
		return -EINVAL;
}
int vfio_pci_set_irqs_ioctl(struct vfio_pci_device *vdev, uint32_t flags,
			    unsigned index, unsigned start, unsigned count,
			    void *data)
{
	int (*func)(struct vfio_pci_device *vdev, unsigned index,
		    unsigned start, unsigned count, uint32_t flags,
		    void *data) = NULL;

	switch (index) {
	case VFIO_PCI_INTX_IRQ_INDEX:
		switch (flags & VFIO_IRQ_SET_ACTION_TYPE_MASK) {
		case VFIO_IRQ_SET_ACTION_MASK:
			func = vfio_pci_set_intx_mask;
			break;
		case VFIO_IRQ_SET_ACTION_UNMASK:
			func = vfio_pci_set_intx_unmask;
			break;
		case VFIO_IRQ_SET_ACTION_TRIGGER:
			func = vfio_pci_set_intx_trigger;
			break;
		}
		break;
	case VFIO_PCI_MSI_IRQ_INDEX:
	case VFIO_PCI_MSIX_IRQ_INDEX:
		switch (flags & VFIO_IRQ_SET_ACTION_TYPE_MASK) {
		case VFIO_IRQ_SET_ACTION_MASK:
		case VFIO_IRQ_SET_ACTION_UNMASK:
			/* XXX Need masking support exported */
			break;
		case VFIO_IRQ_SET_ACTION_TRIGGER:
			func = vfio_pci_set_msi_trigger;
			break;
		}
		break;
	case VFIO_PCI_ERR_IRQ_INDEX:
		switch (flags & VFIO_IRQ_SET_ACTION_TYPE_MASK) {
		case VFIO_IRQ_SET_ACTION_TRIGGER:
			if (pci_is_pcie(vdev->pdev))
				func = vfio_pci_set_err_trigger;
			break;
		}
	}

	if (!func)
		return -ENOTTY;

	return func(vdev, index, start, count, flags, data);
}
