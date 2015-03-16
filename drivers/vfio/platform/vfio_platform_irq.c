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

int vfio_platform_irq_init(struct vfio_platform_device *vdev)
{
	int cnt = 0, i;

	while (vdev->get_irq(vdev, cnt) >= 0)
		cnt++;

	vdev->irqs = kcalloc(cnt, sizeof(struct vfio_platform_irq), GFP_KERNEL);
	if (!vdev->irqs)
		return -ENOMEM;

	for (i = 0; i < cnt; i++) {
		vdev->irqs[i].flags = 0;
		vdev->irqs[i].count = 1;
	}

	vdev->num_irqs = cnt;

	return 0;
}

void vfio_platform_irq_cleanup(struct vfio_platform_device *vdev)
{
	vdev->num_irqs = 0;
	kfree(vdev->irqs);
}
