// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/*
 * Copyright 2013-2016 Freescale Semiconductor Inc.
 * Copyright 2019 NXP
 */

#include <linux/vfio.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/eventfd.h>
#include <linux/msi.h>

#include "linux/fsl/mc.h"
#include "vfio_fsl_mc_private.h"

static int vfio_fsl_mc_set_irq_trigger(struct vfio_fsl_mc_device *vdev,
				       unsigned int index, unsigned int start,
				       unsigned int count, u32 flags,
				       void *data)
{
	return -EINVAL;
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
