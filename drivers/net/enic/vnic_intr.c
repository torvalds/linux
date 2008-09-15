/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/delay.h>

#include "vnic_dev.h"
#include "vnic_intr.h"

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
		printk(KERN_ERR "Failed to hook INTR[%d].ctrl resource\n",
			index);
		return -EINVAL;
	}

	return 0;
}

void vnic_intr_init(struct vnic_intr *intr, unsigned int coalescing_timer,
	unsigned int coalescing_type, unsigned int mask_on_assertion)
{
	iowrite32(coalescing_timer, &intr->ctrl->coalescing_timer);
	iowrite32(coalescing_type, &intr->ctrl->coalescing_type);
	iowrite32(mask_on_assertion, &intr->ctrl->mask_on_assertion);
	iowrite32(0, &intr->ctrl->int_credits);
}

void vnic_intr_clean(struct vnic_intr *intr)
{
	iowrite32(0, &intr->ctrl->int_credits);
}
