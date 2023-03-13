// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/*
 * Copyright 2013-2016 Freescale Semiconductor Inc.
 * Copyright 2019 NXP
 */

#include <linux/vfio.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/eventfd.h>

#include "linux/fsl/mc.h"
#include "vfio_fsl_mc_private.h"

static int vfio_fsl_mc_irqs_allocate(struct vfio_fsl_mc_device *vdev)
{
	struct fsl_mc_device *mc_dev = vdev->mc_dev;
	struct vfio_fsl_mc_irq *mc_irq;
	int irq_count;
	int ret, i;

	/* Device does not support any interrupt */
	if (mc_dev->obj_desc.irq_count == 0)
		return 0;

	/* interrupts were already allocated for this device */
	if (vdev->mc_irqs)
		return 0;

	irq_count = mc_dev->obj_desc.irq_count;

	mc_irq = kcalloc(irq_count, sizeof(*mc_irq), GFP_KERNEL_ACCOUNT);
	if (!mc_irq)
		return -ENOMEM;

	/* Allocate IRQs */
	ret = fsl_mc_allocate_irqs(mc_dev);
	if (ret) {
		kfree(mc_irq);
		return ret;
	}

	for (i = 0; i < irq_count; i++) {
		mc_irq[i].count = 1;
		mc_irq[i].flags = VFIO_IRQ_INFO_EVENTFD;
	}

	vdev->mc_irqs = mc_irq;

	return 0;
}

static irqreturn_t vfio_fsl_mc_irq_handler(int irq_num, void *arg)
{
	struct vfio_fsl_mc_irq *mc_irq = (struct vfio_fsl_mc_irq *)arg;

	eventfd_signal(mc_irq->trigger, 1);
	return IRQ_HANDLED;
}

static int vfio_set_trigger(struct vfio_fsl_mc_device *vdev,
						   int index, int fd)
{
	struct vfio_fsl_mc_irq *irq = &vdev->mc_irqs[index];
	struct eventfd_ctx *trigger;
	int hwirq;
	int ret;

	hwirq = vdev->mc_dev->irqs[index]->virq;
	if (irq->trigger) {
		free_irq(hwirq, irq);
		kfree(irq->name);
		eventfd_ctx_put(irq->trigger);
		irq->trigger = NULL;
	}

	if (fd < 0) /* Disable only */
		return 0;

	irq->name = kasprintf(GFP_KERNEL_ACCOUNT, "vfio-irq[%d](%s)",
			    hwirq, dev_name(&vdev->mc_dev->dev));
	if (!irq->name)
		return -ENOMEM;

	trigger = eventfd_ctx_fdget(fd);
	if (IS_ERR(trigger)) {
		kfree(irq->name);
		return PTR_ERR(trigger);
	}

	irq->trigger = trigger;

	ret = request_irq(hwirq, vfio_fsl_mc_irq_handler, 0,
		  irq->name, irq);
	if (ret) {
		kfree(irq->name);
		eventfd_ctx_put(trigger);
		irq->trigger = NULL;
		return ret;
	}

	return 0;
}

static int vfio_fsl_mc_set_irq_trigger(struct vfio_fsl_mc_device *vdev,
				       unsigned int index, unsigned int start,
				       unsigned int count, u32 flags,
				       void *data)
{
	struct fsl_mc_device *mc_dev = vdev->mc_dev;
	int ret, hwirq;
	struct vfio_fsl_mc_irq *irq;
	struct device *cont_dev = fsl_mc_cont_dev(&mc_dev->dev);
	struct fsl_mc_device *mc_cont = to_fsl_mc_device(cont_dev);

	if (!count && (flags & VFIO_IRQ_SET_DATA_NONE))
		return vfio_set_trigger(vdev, index, -1);

	if (start != 0 || count != 1)
		return -EINVAL;

	mutex_lock(&vdev->vdev.dev_set->lock);
	ret = fsl_mc_populate_irq_pool(mc_cont,
			FSL_MC_IRQ_POOL_MAX_TOTAL_IRQS);
	if (ret)
		goto unlock;

	ret = vfio_fsl_mc_irqs_allocate(vdev);
	if (ret)
		goto unlock;
	mutex_unlock(&vdev->vdev.dev_set->lock);

	if (flags & VFIO_IRQ_SET_DATA_EVENTFD) {
		s32 fd = *(s32 *)data;

		return vfio_set_trigger(vdev, index, fd);
	}

	hwirq = vdev->mc_dev->irqs[index]->virq;

	irq = &vdev->mc_irqs[index];

	if (flags & VFIO_IRQ_SET_DATA_NONE) {
		vfio_fsl_mc_irq_handler(hwirq, irq);

	} else if (flags & VFIO_IRQ_SET_DATA_BOOL) {
		u8 trigger = *(u8 *)data;

		if (trigger)
			vfio_fsl_mc_irq_handler(hwirq, irq);
	}

	return 0;

unlock:
	mutex_unlock(&vdev->vdev.dev_set->lock);
	return ret;

}

int vfio_fsl_mc_set_irqs_ioctl(struct vfio_fsl_mc_device *vdev,
			       u32 flags, unsigned int index,
			       unsigned int start, unsigned int count,
			       void *data)
{
	if (flags & VFIO_IRQ_SET_ACTION_TRIGGER)
		return  vfio_fsl_mc_set_irq_trigger(vdev, index, start,
			  count, flags, data);
	else
		return -EINVAL;
}

/* Free All IRQs for the given MC object */
void vfio_fsl_mc_irqs_cleanup(struct vfio_fsl_mc_device *vdev)
{
	struct fsl_mc_device *mc_dev = vdev->mc_dev;
	int irq_count = mc_dev->obj_desc.irq_count;
	int i;

	/*
	 * Device does not support any interrupt or the interrupts
	 * were not configured
	 */
	if (!vdev->mc_irqs)
		return;

	for (i = 0; i < irq_count; i++)
		vfio_set_trigger(vdev, i, -1);

	fsl_mc_free_irqs(mc_dev);
	kfree(vdev->mc_irqs);
	vdev->mc_irqs = NULL;
}
