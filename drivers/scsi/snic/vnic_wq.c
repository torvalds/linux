// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2014 Cisco Systems, Inc.  All rights reserved.

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include "vnic_dev.h"
#include "vnic_wq.h"

static inline int vnic_wq_get_ctrl(struct vnic_dev *vdev, struct vnic_wq *wq,
	unsigned int index, enum vnic_res_type res_type)
{
	wq->ctrl = svnic_dev_get_res(vdev, res_type, index);
	if (!wq->ctrl)
		return -EINVAL;

	return 0;
}

static inline int vnic_wq_alloc_ring(struct vnic_dev *vdev, struct vnic_wq *wq,
	unsigned int index, unsigned int desc_count, unsigned int desc_size)
{
	return svnic_dev_alloc_desc_ring(vdev, &wq->ring, desc_count,
					 desc_size);
}

static int vnic_wq_alloc_bufs(struct vnic_wq *wq)
{
	struct vnic_wq_buf *buf;
	unsigned int i, j, count = wq->ring.desc_count;
	unsigned int blks = VNIC_WQ_BUF_BLKS_NEEDED(count);

	for (i = 0; i < blks; i++) {
		wq->bufs[i] = kzalloc(VNIC_WQ_BUF_BLK_SZ, GFP_ATOMIC);
		if (!wq->bufs[i]) {
			pr_err("Failed to alloc wq_bufs\n");

			return -ENOMEM;
		}
	}

	for (i = 0; i < blks; i++) {
		buf = wq->bufs[i];
		for (j = 0; j < VNIC_WQ_BUF_DFLT_BLK_ENTRIES; j++) {
			buf->index = i * VNIC_WQ_BUF_DFLT_BLK_ENTRIES + j;
			buf->desc = (u8 *)wq->ring.descs +
				wq->ring.desc_size * buf->index;
			if (buf->index + 1 == count) {
				buf->next = wq->bufs[0];
				break;
			} else if (j + 1 == VNIC_WQ_BUF_DFLT_BLK_ENTRIES) {
				buf->next = wq->bufs[i + 1];
			} else {
				buf->next = buf + 1;
				buf++;
			}
		}
	}

	wq->to_use = wq->to_clean = wq->bufs[0];

	return 0;
}

void svnic_wq_free(struct vnic_wq *wq)
{
	struct vnic_dev *vdev;
	unsigned int i;

	vdev = wq->vdev;

	svnic_dev_free_desc_ring(vdev, &wq->ring);

	for (i = 0; i < VNIC_WQ_BUF_BLKS_MAX; i++) {
		kfree(wq->bufs[i]);
		wq->bufs[i] = NULL;
	}

	wq->ctrl = NULL;

}

int vnic_wq_devcmd2_alloc(struct vnic_dev *vdev, struct vnic_wq *wq,
	unsigned int desc_count, unsigned int desc_size)
{
	int err;

	wq->index = 0;
	wq->vdev = vdev;

	err = vnic_wq_get_ctrl(vdev, wq, 0, RES_TYPE_DEVCMD2);
	if (err) {
		pr_err("Failed to get devcmd2 resource\n");

		return err;
	}

	svnic_wq_disable(wq);

	err = vnic_wq_alloc_ring(vdev, wq, 0, desc_count, desc_size);
	if (err)
		return err;

	return 0;
}

int svnic_wq_alloc(struct vnic_dev *vdev, struct vnic_wq *wq,
	unsigned int index, unsigned int desc_count, unsigned int desc_size)
{
	int err;

	wq->index = index;
	wq->vdev = vdev;

	err = vnic_wq_get_ctrl(vdev, wq, index, RES_TYPE_WQ);
	if (err) {
		pr_err("Failed to hook WQ[%d] resource\n", index);

		return err;
	}

	svnic_wq_disable(wq);

	err = vnic_wq_alloc_ring(vdev, wq, index, desc_count, desc_size);
	if (err)
		return err;

	err = vnic_wq_alloc_bufs(wq);
	if (err) {
		svnic_wq_free(wq);

		return err;
	}

	return 0;
}

void vnic_wq_init_start(struct vnic_wq *wq, unsigned int cq_index,
	unsigned int fetch_index, unsigned int posted_index,
	unsigned int error_interrupt_enable,
	unsigned int error_interrupt_offset)
{
	u64 paddr;
	unsigned int count = wq->ring.desc_count;

	paddr = (u64)wq->ring.base_addr | VNIC_PADDR_TARGET;
	writeq(paddr, &wq->ctrl->ring_base);
	iowrite32(count, &wq->ctrl->ring_size);
	iowrite32(fetch_index, &wq->ctrl->fetch_index);
	iowrite32(posted_index, &wq->ctrl->posted_index);
	iowrite32(cq_index, &wq->ctrl->cq_index);
	iowrite32(error_interrupt_enable, &wq->ctrl->error_interrupt_enable);
	iowrite32(error_interrupt_offset, &wq->ctrl->error_interrupt_offset);
	iowrite32(0, &wq->ctrl->error_status);

	wq->to_use = wq->to_clean =
		&wq->bufs[fetch_index / VNIC_WQ_BUF_BLK_ENTRIES(count)]
			[fetch_index % VNIC_WQ_BUF_BLK_ENTRIES(count)];
}

void svnic_wq_init(struct vnic_wq *wq, unsigned int cq_index,
	unsigned int error_interrupt_enable,
	unsigned int error_interrupt_offset)
{
	vnic_wq_init_start(wq, cq_index, 0, 0, error_interrupt_enable,
			   error_interrupt_offset);
}

unsigned int svnic_wq_error_status(struct vnic_wq *wq)
{
	return ioread32(&wq->ctrl->error_status);
}

void svnic_wq_enable(struct vnic_wq *wq)
{
	iowrite32(1, &wq->ctrl->enable);
}

int svnic_wq_disable(struct vnic_wq *wq)
{
	unsigned int wait;

	iowrite32(0, &wq->ctrl->enable);

	/* Wait for HW to ACK disable request */
	for (wait = 0; wait < 100; wait++) {
		if (!(ioread32(&wq->ctrl->running)))
			return 0;
		udelay(1);
	}

	pr_err("Failed to disable WQ[%d]\n", wq->index);

	return -ETIMEDOUT;
}

void svnic_wq_clean(struct vnic_wq *wq,
	void (*buf_clean)(struct vnic_wq *wq, struct vnic_wq_buf *buf))
{
	struct vnic_wq_buf *buf;

	BUG_ON(ioread32(&wq->ctrl->enable));

	buf = wq->to_clean;

	while (svnic_wq_desc_used(wq) > 0) {

		(*buf_clean)(wq, buf);

		buf = wq->to_clean = buf->next;
		wq->ring.desc_avail++;
	}

	wq->to_use = wq->to_clean = wq->bufs[0];

	iowrite32(0, &wq->ctrl->fetch_index);
	iowrite32(0, &wq->ctrl->posted_index);
	iowrite32(0, &wq->ctrl->error_status);

	svnic_dev_clear_desc_ring(&wq->ring);
}
