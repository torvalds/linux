/*
 * VFIO platform devices interrupt handling
 *
 * Copyright (C) 2013 - Virtual Open Systems
 * Author: Antonios Motakis <a.motakis@virtualopensystems.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/eventfd.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/vfio.h>
#include <linux/irq.h>

#include "vfio_platform_private.h"

static int vfio_platform_set_irq_mask(struct vfio_platform_device *vdev,
				      unsigned index, unsigned start,
				      unsigned count, uint32_t flags,
				      void *data)
{
	return -EINVAL;
}

static int vfio_platform_set_irq_unmask(struct vfio_platform_device *vdev,
					unsigned index, unsigned start,
					unsigned count, uint32_t flags,
					void *data)
{
	return -EINVAL;
}

static int vfio_platform_set_irq_trigger(struct vfio_platform_device *vdev,
					 unsigned index, unsigned start,
					 unsigned count, uint32_t flags,
					 void *data)
{
	return -EINVAL;
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

	vdev->irqs = kcalloc(cnt, sizeof(struct vfio_platform_irq), GFP_KERNEL);
	if (!vdev->irqs)
		return -ENOMEM;

	for (i = 0; i < cnt; i++) {
		int hwirq = vdev->get_irq(vdev, i);

		if (hwirq < 0)
			goto err;

		vdev->irqs[i].flags = 0;
		vdev->irqs[i].count = 1;
		vdev->irqs[i].hwirq = hwirq;
	}

	vdev->num_irqs = cnt;

	return 0;
err:
	kfree(vdev->irqs);
	return -EINVAL;
}

void vfio_platform_irq_cleanup(struct vfio_platform_device *vdev)
{
	vdev->num_irqs = 0;
	kfree(vdev->irqs);
}
