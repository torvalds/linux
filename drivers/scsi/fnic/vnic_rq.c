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
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include "vnic_dev.h"
#include "vnic_rq.h"

static int vnic_rq_alloc_bufs(struct vnic_rq *rq)
{
	struct vnic_rq_buf *buf;
	unsigned int i, j, count = rq->ring.desc_count;
	unsigned int blks = VNIC_RQ_BUF_BLKS_NEEDED(count);

	for (i = 0; i < blks; i++) {
		rq->bufs[i] = kzalloc(VNIC_RQ_BUF_BLK_SZ, GFP_ATOMIC);
		if (!rq->bufs[i]) {
			printk(KERN_ERR "Failed to alloc rq_bufs\n");
			return -ENOMEM;
		}
	}

	for (i = 0; i < blks; i++) {
		buf = rq->bufs[i];
		for (j = 0; j < VNIC_RQ_BUF_BLK_ENTRIES; j++) {
			buf->index = i * VNIC_RQ_BUF_BLK_ENTRIES + j;
			buf->desc = (u8 *)rq->ring.descs +
				rq->ring.desc_size * buf->index;
			if (buf->index + 1 == count) {
				buf->next = rq->bufs[0];
				break;
			} else if (j + 1 == VNIC_RQ_BUF_BLK_ENTRIES) {
				buf->next = rq->bufs[i + 1];
			} else {
				buf->next = buf + 1;
				buf++;
			}
		}
	}

	rq->to_use = rq->to_clean = rq->bufs[0];
	rq->buf_index = 0;

	return 0;
}

void vnic_rq_free(struct vnic_rq *rq)
{
	struct vnic_dev *vdev;
	unsigned int i;

	vdev = rq->vdev;

	vnic_dev_free_desc_ring(vdev, &rq->ring);

	for (i = 0; i < VNIC_RQ_BUF_BLKS_MAX; i++) {
		kfree(rq->bufs[i]);
		rq->bufs[i] = NULL;
	}

	rq->ctrl = NULL;
}

int vnic_rq_alloc(struct vnic_dev *vdev, struct vnic_rq *rq, unsigned int index,
	unsigned int desc_count, unsigned int desc_size)
{
	int err;

	rq->index = index;
	rq->vdev = vdev;

	rq->ctrl = vnic_dev_get_res(vdev, RES_TYPE_RQ, index);
	if (!rq->ctrl) {
		printk(KERN_ERR "Failed to hook RQ[%d] resource\n", index);
		return -EINVAL;
	}

	vnic_rq_disable(rq);

	err = vnic_dev_alloc_desc_ring(vdev, &rq->ring, desc_count, desc_size);
	if (err)
		return err;

	err = vnic_rq_alloc_bufs(rq);
	if (err) {
		vnic_rq_free(rq);
		return err;
	}

	return 0;
}

void vnic_rq_init(struct vnic_rq *rq, unsigned int cq_index,
	unsigned int error_interrupt_enable,
	unsigned int error_interrupt_offset)
{
	u64 paddr;
	u32 fetch_index;

	paddr = (u64)rq->ring.base_addr | VNIC_PADDR_TARGET;
	writeq(paddr, &rq->ctrl->ring_base);
	iowrite32(rq->ring.desc_count, &rq->ctrl->ring_size);
	iowrite32(cq_index, &rq->ctrl->cq_index);
	iowrite32(error_interrupt_enable, &rq->ctrl->error_interrupt_enable);
	iowrite32(error_interrupt_offset, &rq->ctrl->error_interrupt_offset);
	iowrite32(0, &rq->ctrl->dropped_packet_count);
	iowrite32(0, &rq->ctrl->error_status);

	/* Use current fetch_index as the ring starting point */
	fetch_index = ioread32(&rq->ctrl->fetch_index);
	rq->to_use = rq->to_clean =
		&rq->bufs[fetch_index / VNIC_RQ_BUF_BLK_ENTRIES]
			[fetch_index % VNIC_RQ_BUF_BLK_ENTRIES];
	iowrite32(fetch_index, &rq->ctrl->posted_index);

	rq->buf_index = 0;
}

unsigned int vnic_rq_error_status(struct vnic_rq *rq)
{
	return ioread32(&rq->ctrl->error_status);
}

void vnic_rq_enable(struct vnic_rq *rq)
{
	iowrite32(1, &rq->ctrl->enable);
}

int vnic_rq_disable(struct vnic_rq *rq)
{
	unsigned int wait;

	iowrite32(0, &rq->ctrl->enable);

	/* Wait for HW to ACK disable request */
	for (wait = 0; wait < 100; wait++) {
		if (!(ioread32(&rq->ctrl->running)))
			return 0;
		udelay(1);
	}

	printk(KERN_ERR "Failed to disable RQ[%d]\n", rq->index);

	return -ETIMEDOUT;
}

void vnic_rq_clean(struct vnic_rq *rq,
	void (*buf_clean)(struct vnic_rq *rq, struct vnic_rq_buf *buf))
{
	struct vnic_rq_buf *buf;
	u32 fetch_index;

	WARN_ON(ioread32(&rq->ctrl->enable));

	buf = rq->to_clean;

	while (vnic_rq_desc_used(rq) > 0) {

		(*buf_clean)(rq, buf);

		buf = rq->to_clean = buf->next;
		rq->ring.desc_avail++;
	}

	/* Use current fetch_index as the ring starting point */
	fetch_index = ioread32(&rq->ctrl->fetch_index);
	rq->to_use = rq->to_clean =
		&rq->bufs[fetch_index / VNIC_RQ_BUF_BLK_ENTRIES]
			[fetch_index % VNIC_RQ_BUF_BLK_ENTRIES];
	iowrite32(fetch_index, &rq->ctrl->posted_index);

	rq->buf_index = 0;

	vnic_dev_clear_desc_ring(&rq->ring);
}

