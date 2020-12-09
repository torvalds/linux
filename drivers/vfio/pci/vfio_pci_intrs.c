// SPDX-License-Identifier: GPL-2.0-only
/*
 * VFIO PCI interrupt handling
 *
 * Copyright (C) 2012 Red Hat, Inc.  All rights reserved.
 *     Author: Alex Williamson <alex.williamson@redhat.com>
 *
 * Derived from original vfio:
 * Copyright 2010 Cisco Systems, Inc.  All rights reserved.
 * Author: Tom Lyon, pugs@cisco.com
 */

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/eventfd.h>
#include <linux/msi.h>
#include <linux/pci.h>
#include <linux/file.h>
#include <linux/vfio.h>
#include <linux/wait.h>
#include <linux/slab.h>

#include "vfio_pci_private.h"

/*
 * INTx
 */
static void vfio_send_intx_eventfd(void *opaque, void *unused)
{
	struct vfio_pci_device *vdev = opaque;

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
static int vfio_pci_intx_unmask_handler(void *opaque, void *unused)
{
	struct vfio_pci_device *vdev = opaque;
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
	vfio_virqfd_disable(&vdev->ctx[0].unmask);
	vfio_virqfd_disable(&vdev->ctx[0].mask);
	vfio_intx_set_signal(vdev, -1);
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
	unsigned int flag = msix ? PCI_IRQ_MSIX : PCI_IRQ_MSI;
	int ret;
	u16 cmd;

	if (!is_irq_none(vdev))
		return -EINVAL;

	vdev->ctx = kcalloc(nvec, sizeof(struct vfio_pci_irq_ctx), GFP_KERNEL);
	if (!vdev->ctx)
		return -ENOMEM;

	/* return the number of supported vectors if we can't get all: */
	cmd = vfio_pci_memory_lock_and_enable(vdev);
	ret = pci_alloc_irq_vectors(pdev, 1, nvec, flag);
	if (ret < nvec) {
		if (ret > 0)
			pci_free_irq_vectors(pdev);
		vfio_pci_memory_unlock_and_restore(vdev, cmd);
		kfree(vdev->ctx);
		return ret;
	}
	vfio_pci_memory_unlock_and_restore(vdev, cmd);

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
	struct eventfd_ctx *trigger;
	int irq, ret;
	u16 cmd;

	if (vector < 0 || vector >= vdev->num_ctx)
		return -EINVAL;

	irq = pci_irq_vector(pdev, vector);

	if (vdev->ctx[vector].trigger) {
		irq_bypass_unregister_producer(&vdev->ctx[vector].producer);

		cmd = vfio_pci_memory_lock_and_enable(vdev);
		free_irq(irq, vdev->ctx[vector].trigger);
		vfio_pci_memory_unlock_and_restore(vdev, cmd);

		kfree(vdev->ctx[vector].name);
		eventfd_ctx_put(vdev->ctx[vector].trigger);
		vdev->ctx[vector].trigger = NULL;
	}

	if (fd < 0)
		return 0;

	vdev->ctx[vector].name = kasprintf(GFP_KERNEL, "vfio-msi%s[%d](%s)",
					   msix ? "x" : "", vector,
					   pci_name(pdev));
	if (!vdev->ctx[vector].name)
		return -ENOMEM;

	trigger = eventfd_ctx_fdget(fd);
	if (IS_ERR(trigger)) {
		kfree(vdev->ctx[vector].name);
		return PTR_ERR(trigger);
	}

	/*
	 * The MSIx vector table resides in device memory which may be cleared
	 * via backdoor resets. We don't allow direct access to the vector
	 * table so even if a userspace driver attempts to save/restore around
	 * such a reset it would be unsuccessful. To avoid this, restore the
	 * cached value of the message prior to enabling.
	 */
	cmd = vfio_pci_memory_lock_and_enable(vdev);
	if (msix) {
		struct msi_msg msg;

		get_cached_msi_msg(irq, &msg);
		pci_write_msi_msg(irq, &msg);
	}

	ret = request_irq(irq, vfio_msihandler, 0,
			  vdev->ctx[vector].name, trigger);
	vfio_pci_memory_unlock_and_restore(vdev, cmd);
	if (ret) {
		kfree(vdev->ctx[vector].name);
		eventfd_ctx_put(trigger);
		return ret;
	}

	vdev->ctx[vector].producer.token = trigger;
	vdev->ctx[vector].producer.irq = irq;
	ret = irq_bypass_register_producer(&vdev->ctx[vector].producer);
	if (unlikely(ret)) {
		dev_info(&pdev->dev,
		"irq bypass producer (token %p) registration fails: %d\n",
		vdev->ctx[vector].producer.token, ret);

		vdev->ctx[vector].producer.token = NULL;
	}
	vdev->ctx[vector].trigger = trigger;

	return 0;
}

static int vfio_msi_set_block(struct vfio_pci_device *vdev, unsigned start,
			      unsigned count, int32_t *fds, bool msix)
{
	int i, j, ret = 0;

	if (start >= vdev->num_ctx || start + count > vdev->num_ctx)
		return -EINVAL;

	for (i = 0, j = start; i < count && !ret; i++, j++) {
		int fd = fds ? fds[i] : -1;
		ret = vfio_msi_set_vector_signal(vdev, j, fd, msix);
	}

	if (ret) {
		for (--j; j >= (int)start; j--)
			vfio_msi_set_vector_signal(vdev, j, -1, msix);
	}

	return ret;
}

static void vfio_msi_disable(struct vfio_pci_device *vdev, bool msix)
{
	struct pci_dev *pdev = vdev->pdev;
	int i;
	u16 cmd;

	for (i = 0; i < vdev->num_ctx; i++) {
		vfio_virqfd_disable(&vdev->ctx[i].unmask);
		vfio_virqfd_disable(&vdev->ctx[i].mask);
	}

	vfio_msi_set_block(vdev, 0, vdev->num_ctx, NULL, msix);

	cmd = vfio_pci_memory_lock_and_enable(vdev);
	pci_free_irq_vectors(pdev);
	vfio_pci_memory_unlock_and_restore(vdev, cmd);

	/*
	 * Both disable paths above use pci_intx_for_msi() to clear DisINTx
	 * via their shutdown paths.  Restore for NoINTx devices.
	 */
	if (vdev->nointx)
		pci_intx(pdev, 0);

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
			return vfio_virqfd_enable((void *) vdev,
						  vfio_pci_intx_unmask_handler,
						  vfio_send_intx_eventfd, NULL,
						  &vdev->ctx[0].unmask, fd);

		vfio_virqfd_disable(&vdev->ctx[0].unmask);
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

static int vfio_pci_set_ctx_trigger_single(struct eventfd_ctx **ctx,
					   unsigned int count, uint32_t flags,
					   void *data)
{
	/* DATA_NONE/DATA_BOOL enables loopback testing */
	if (flags & VFIO_IRQ_SET_DATA_NONE) {
		if (*ctx) {
			if (count) {
				eventfd_signal(*ctx, 1);
			} else {
				eventfd_ctx_put(*ctx);
				*ctx = NULL;
			}
			return 0;
		}
	} else if (flags & VFIO_IRQ_SET_DATA_BOOL) {
		uint8_t trigger;

		if (!count)
			return -EINVAL;

		trigger = *(uint8_t *)data;
		if (trigger && *ctx)
			eventfd_signal(*ctx, 1);

		return 0;
	} else if (flags & VFIO_IRQ_SET_DATA_EVENTFD) {
		int32_t fd;

		if (!count)
			return -EINVAL;

		fd = *(int32_t *)data;
		if (fd == -1) {
			if (*ctx)
				eventfd_ctx_put(*ctx);
			*ctx = NULL;
		} else if (fd >= 0) {
			struct eventfd_ctx *efdctx;

			efdctx = eventfd_ctx_fdget(fd);
			if (IS_ERR(efdctx))
				return PTR_ERR(efdctx);

			if (*ctx)
				eventfd_ctx_put(*ctx);

			*ctx = efdctx;
		}
		return 0;
	}

	return -EINVAL;
}

static int vfio_pci_set_err_trigger(struct vfio_pci_device *vdev,
				    unsigned index, unsigned start,
				    unsigned count, uint32_t flags, void *data)
{
	if (index != VFIO_PCI_ERR_IRQ_INDEX || start != 0 || count > 1)
		return -EINVAL;

	return vfio_pci_set_ctx_trigger_single(&vdev->err_trigger,
					       count, flags, data);
}

static int vfio_pci_set_req_trigger(struct vfio_pci_device *vdev,
				    unsigned index, unsigned start,
				    unsigned count, uint32_t flags, void *data)
{
	if (index != VFIO_PCI_REQ_IRQ_INDEX || start != 0 || count > 1)
		return -EINVAL;

	return vfio_pci_set_ctx_trigger_single(&vdev->req_trigger,
					       count, flags, data);
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
		break;
	case VFIO_PCI_REQ_IRQ_INDEX:
		switch (flags & VFIO_IRQ_SET_ACTION_TYPE_MASK) {
		case VFIO_IRQ_SET_ACTION_TRIGGER:
			func = vfio_pci_set_req_trigger;
			break;
		}
		break;
	}

	if (!func)
		return -ENOTTY;

	return func(vdev, index, start, count, flags, data);
}
