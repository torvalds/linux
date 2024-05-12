// SPDX-License-Identifier: GPL-2.0-only
/*
 * VFIO platform devices interrupt handling
 *
 * Copyright (C) 2013 - Virtual Open Systems
 * Author: Antonios Motakis <a.motakis@virtualopensystems.com>
 */

#include <linux/eventfd.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/vfio.h>
#include <linux/irq.h>

#include "vfio_platform_private.h"

static void vfio_platform_mask(struct vfio_platform_irq *irq_ctx)
{
	unsigned long flags;

	spin_lock_irqsave(&irq_ctx->lock, flags);

	if (!irq_ctx->masked) {
		disable_irq_nosync(irq_ctx->hwirq);
		irq_ctx->masked = true;
	}

	spin_unlock_irqrestore(&irq_ctx->lock, flags);
}

static int vfio_platform_mask_handler(void *opaque, void *unused)
{
	struct vfio_platform_irq *irq_ctx = opaque;

	vfio_platform_mask(irq_ctx);

	return 0;
}

static int vfio_platform_set_irq_mask(struct vfio_platform_device *vdev,
				      unsigned index, unsigned start,
				      unsigned count, uint32_t flags,
				      void *data)
{
	if (start != 0 || count != 1)
		return -EINVAL;

	if (!(vdev->irqs[index].flags & VFIO_IRQ_INFO_MASKABLE))
		return -EINVAL;

	if (flags & VFIO_IRQ_SET_DATA_EVENTFD) {
		int32_t fd = *(int32_t *)data;

		if (fd >= 0)
			return vfio_virqfd_enable((void *) &vdev->irqs[index],
						  vfio_platform_mask_handler,
						  NULL, NULL,
						  &vdev->irqs[index].mask, fd);

		vfio_virqfd_disable(&vdev->irqs[index].mask);
		return 0;
	}

	if (flags & VFIO_IRQ_SET_DATA_NONE) {
		vfio_platform_mask(&vdev->irqs[index]);

	} else if (flags & VFIO_IRQ_SET_DATA_BOOL) {
		uint8_t mask = *(uint8_t *)data;

		if (mask)
			vfio_platform_mask(&vdev->irqs[index]);
	}

	return 0;
}

static void vfio_platform_unmask(struct vfio_platform_irq *irq_ctx)
{
	unsigned long flags;

	spin_lock_irqsave(&irq_ctx->lock, flags);

	if (irq_ctx->masked) {
		enable_irq(irq_ctx->hwirq);
		irq_ctx->masked = false;
	}

	spin_unlock_irqrestore(&irq_ctx->lock, flags);
}

static int vfio_platform_unmask_handler(void *opaque, void *unused)
{
	struct vfio_platform_irq *irq_ctx = opaque;

	vfio_platform_unmask(irq_ctx);

	return 0;
}

static int vfio_platform_set_irq_unmask(struct vfio_platform_device *vdev,
					unsigned index, unsigned start,
					unsigned count, uint32_t flags,
					void *data)
{
	if (start != 0 || count != 1)
		return -EINVAL;

	if (!(vdev->irqs[index].flags & VFIO_IRQ_INFO_MASKABLE))
		return -EINVAL;

	if (flags & VFIO_IRQ_SET_DATA_EVENTFD) {
		int32_t fd = *(int32_t *)data;

		if (fd >= 0)
			return vfio_virqfd_enable((void *) &vdev->irqs[index],
						  vfio_platform_unmask_handler,
						  NULL, NULL,
						  &vdev->irqs[index].unmask,
						  fd);

		vfio_virqfd_disable(&vdev->irqs[index].unmask);
		return 0;
	}

	if (flags & VFIO_IRQ_SET_DATA_NONE) {
		vfio_platform_unmask(&vdev->irqs[index]);

	} else if (flags & VFIO_IRQ_SET_DATA_BOOL) {
		uint8_t unmask = *(uint8_t *)data;

		if (unmask)
			vfio_platform_unmask(&vdev->irqs[index]);
	}

	return 0;
}

static irqreturn_t vfio_automasked_irq_handler(int irq, void *dev_id)
{
	struct vfio_platform_irq *irq_ctx = dev_id;
	unsigned long flags;
	int ret = IRQ_NONE;

	spin_lock_irqsave(&irq_ctx->lock, flags);

	if (!irq_ctx->masked) {
		ret = IRQ_HANDLED;

		/* automask maskable interrupts */
		disable_irq_nosync(irq_ctx->hwirq);
		irq_ctx->masked = true;
	}

	spin_unlock_irqrestore(&irq_ctx->lock, flags);

	if (ret == IRQ_HANDLED)
		eventfd_signal(irq_ctx->trigger, 1);

	return ret;
}

static irqreturn_t vfio_irq_handler(int irq, void *dev_id)
{
	struct vfio_platform_irq *irq_ctx = dev_id;

	eventfd_signal(irq_ctx->trigger, 1);

	return IRQ_HANDLED;
}

static int vfio_set_trigger(struct vfio_platform_device *vdev, int index,
			    int fd, irq_handler_t handler)
{
	struct vfio_platform_irq *irq = &vdev->irqs[index];
	struct eventfd_ctx *trigger;
	int ret;

	if (irq->trigger) {
		irq_clear_status_flags(irq->hwirq, IRQ_NOAUTOEN);
		free_irq(irq->hwirq, irq);
		kfree(irq->name);
		eventfd_ctx_put(irq->trigger);
		irq->trigger = NULL;
	}

	if (fd < 0) /* Disable only */
		return 0;
	irq->name = kasprintf(GFP_KERNEL_ACCOUNT, "vfio-irq[%d](%s)",
			      irq->hwirq, vdev->name);
	if (!irq->name)
		return -ENOMEM;

	trigger = eventfd_ctx_fdget(fd);
	if (IS_ERR(trigger)) {
		kfree(irq->name);
		return PTR_ERR(trigger);
	}

	irq->trigger = trigger;

	irq_set_status_flags(irq->hwirq, IRQ_NOAUTOEN);
	ret = request_irq(irq->hwirq, handler, 0, irq->name, irq);
	if (ret) {
		kfree(irq->name);
		eventfd_ctx_put(trigger);
		irq->trigger = NULL;
		return ret;
	}

	if (!irq->masked)
		enable_irq(irq->hwirq);

	return 0;
}

static int vfio_platform_set_irq_trigger(struct vfio_platform_device *vdev,
					 unsigned index, unsigned start,
					 unsigned count, uint32_t flags,
					 void *data)
{
	struct vfio_platform_irq *irq = &vdev->irqs[index];
	irq_handler_t handler;

	if (vdev->irqs[index].flags & VFIO_IRQ_INFO_AUTOMASKED)
		handler = vfio_automasked_irq_handler;
	else
		handler = vfio_irq_handler;

	if (!count && (flags & VFIO_IRQ_SET_DATA_NONE))
		return vfio_set_trigger(vdev, index, -1, handler);

	if (start != 0 || count != 1)
		return -EINVAL;

	if (flags & VFIO_IRQ_SET_DATA_EVENTFD) {
		int32_t fd = *(int32_t *)data;

		return vfio_set_trigger(vdev, index, fd, handler);
	}

	if (flags & VFIO_IRQ_SET_DATA_NONE) {
		handler(irq->hwirq, irq);

	} else if (flags & VFIO_IRQ_SET_DATA_BOOL) {
		uint8_t trigger = *(uint8_t *)data;

		if (trigger)
			handler(irq->hwirq, irq);
	}

	return 0;
}

int vfio_platform_set_irqs_ioctl(struct vfio_platform_device *vdev,
				 uint32_t flags, unsigned index, unsigned start,
				 unsigned count, void *data)
{
	int (*func)(struct vfio_platform_device *vdev, unsigned index,
		    unsigned start, unsigned count, uint32_t flags,
		    void *data) = NULL;

	switch (flags & VFIO_IRQ_SET_ACTION_TYPE_MASK) {
	case VFIO_IRQ_SET_ACTION_MASK:
		func = vfio_platform_set_irq_mask;
		break;
	case VFIO_IRQ_SET_ACTION_UNMASK:
		func = vfio_platform_set_irq_unmask;
		break;
	case VFIO_IRQ_SET_ACTION_TRIGGER:
		func = vfio_platform_set_irq_trigger;
		break;
	}

	if (!func)
		return -ENOTTY;

	return func(vdev, index, start, count, flags, data);
}

int vfio_platform_irq_init(struct vfio_platform_device *vdev)
{
	int cnt = 0, i;

	while (vdev->get_irq(vdev, cnt) >= 0)
		cnt++;

	vdev->irqs = kcalloc(cnt, sizeof(struct vfio_platform_irq),
			     GFP_KERNEL_ACCOUNT);
	if (!vdev->irqs)
		return -ENOMEM;

	for (i = 0; i < cnt; i++) {
		int hwirq = vdev->get_irq(vdev, i);

		if (hwirq < 0)
			goto err;

		spin_lock_init(&vdev->irqs[i].lock);

		vdev->irqs[i].flags = VFIO_IRQ_INFO_EVENTFD;

		if (irq_get_trigger_type(hwirq) & IRQ_TYPE_LEVEL_MASK)
			vdev->irqs[i].flags |= VFIO_IRQ_INFO_MASKABLE
						| VFIO_IRQ_INFO_AUTOMASKED;

		vdev->irqs[i].count = 1;
		vdev->irqs[i].hwirq = hwirq;
		vdev->irqs[i].masked = false;
	}

	vdev->num_irqs = cnt;

	return 0;
err:
	kfree(vdev->irqs);
	return -EINVAL;
}

void vfio_platform_irq_cleanup(struct vfio_platform_device *vdev)
{
	int i;

	for (i = 0; i < vdev->num_irqs; i++)
		vfio_set_trigger(vdev, i, -1, NULL);

	vdev->num_irqs = 0;
	kfree(vdev->irqs);
}
