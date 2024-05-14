// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2008-2010 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/delay.h>

#include "vnic_dev.h"
#include "vnic_intr.h"
#include "enic.h"

void vnic_intr_free(struct vnic_intr *intr)
{
	intr->ctrl = NULL;
}

int vnic_intr_alloc(struct vnic_dev *vdev, struct vnic_intr *intr,
	unsigned int index)
{
	intr->index = index;
	intr->vdev = vdev;

	intr->ctrl = vnic_dev_get_res(vdev, RES_TYPE_INTR_CTRL, index);
	if (!intr->ctrl) {
		vdev_err(vdev, "Failed to hook INTR[%d].ctrl resource\n",
			 index);
		return -EINVAL;
	}

	return 0;
}

void vnic_intr_init(struct vnic_intr *intr, u32 coalescing_timer,
	unsigned int coalescing_type, unsigned int mask_on_assertion)
{
	vnic_intr_coalescing_timer_set(intr, coalescing_timer);
	iowrite32(coalescing_type, &intr->ctrl->coalescing_type);
	iowrite32(mask_on_assertion, &intr->ctrl->mask_on_assertion);
	iowrite32(0, &intr->ctrl->int_credits);
}

void vnic_intr_coalescing_timer_set(struct vnic_intr *intr,
	u32 coalescing_timer)
{
	iowrite32(vnic_dev_intr_coal_timer_usec_to_hw(intr->vdev,
		coalescing_timer), &intr->ctrl->coalescing_timer);
}

void vnic_intr_clean(struct vnic_intr *intr)
{
	iowrite32(0, &intr->ctrl->int_credits);
}
