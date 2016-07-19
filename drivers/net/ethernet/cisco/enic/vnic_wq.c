/*
 * Copyright 2008-2010 Cisco Systems, Inc.  All rights reserved.
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
#include <linux/slab.h>

#include "vnic_dev.h"
#include "vnic_wq.h"
#include "enic.h"

static int vnic_wq_alloc_bufs(struct vnic_wq *wq)
{
	struct vnic_wq_buf *buf;
	unsigned int i, j, count = wq->ring.desc_count;
	unsigned int blks = VNIC_WQ_BUF_BLKS_NEEDED(count);

	for (i = 0; i < blks; i++) {
		wq->bufs[i] = kzalloc(VNIC_WQ_BUF_BLK_SZ(count), GFP_ATOMIC);
		if (!wq->bufs[i])
			return -ENOMEM;
	}

	for (i = 0; i < blks; i++) {
		buf = wq->bufs[i];
		for (j = 0; j < VNIC_WQ_BUF_BLK_ENTRIES(count); j++) {
			buf->index = i * VNIC_WQ_BUF_BLK_ENTRIES(count) + j;
			buf->desc = (u8 *)wq->ring.descs +
				wq->ring.desc_size * buf->index;
			if (buf->index + 1 == count) {
				buf->next = wq->bufs[0];
				buf->next->prev = buf;
				break;
			} else if (j + 1 == VNIC_WQ_BUF_BLK_ENTRIES(count)) {
				buf->next = wq->bufs[i + 1];
				buf->next->prev = buf;
			} else {
				buf->next = buf + 1;
				buf->next->prev = buf;
				buf++;
			}
		}
	}

	wq->to_use = wq->to_clean = wq->bufs[0];

	return 0;
}

void vnic_wq_free(struct vnic_wq *wq)
{
	struct vnic_dev *vdev;
	unsigned int i;

	vdev = wq->vdev;

	vnic_dev_free_desc_ring(vdev, &wq->ring);

	for (i = 0; i < VNIC_WQ_BUF_BLKS_MAX; i++) {
		if (wq->bufs[i]) {
			kfree(wq->bufs[i]);
			wq->bufs[i] = NULL;
		}
	}

	wq->ctrl = NULL;
}

int vnic_wq_alloc(struct vnic_dev *vdev, struct vnic_wq *wq, unsigned int index,
	unsigned int desc_count, unsigned int desc_size)
{
	int err;

	wq->index = index;
	wq->vdev = vdev;

	wq->ctrl = vnic_dev_get_res(vdev, RES_TYPE_WQ, index);
	if (!wq->ctrl) {
		vdev_err(vdev, "Failed to hook WQ[%d] resource\n", index);
		return -EINVAL;
	}

	vnic_wq_disable(wq);

	err = vnic_dev_alloc_desc_ring(vdev, &wq->ring, desc_count, desc_size);
	if (err)
		return err;

	err = vnic_wq_alloc_bufs(wq);
	if (err) {
		vnic_wq_free(wq);
		return err;
	}

	return 0;
}

int enic_wq_devcmd2_alloc(struct vnic_dev *vdev, struct vnic_wq *wq,
			  unsigned int desc_count, unsigned int desc_size)
{
	int err;

	wq->index = 0;
	wq->vdev = vdev;

	wq->ctrl = vnic_dev_get_res(vdev, RES_TYPE_DEVCMD2, 0);
	if (!wq->ctrl)
		return -EINVAL;
	vnic_wq_disable(wq);
	err = vnic_dev_alloc_desc_ring(vdev, &wq->ring, desc_count, desc_size);

	return err;
}

void enic_wq_init_start(struct vnic_wq *wq, unsigned int cq_index,
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

void vnic_wq_init(struct vnic_wq *wq, unsigned int cq_index,
	unsigned int error_interrupt_enable,
	unsigned int error_interrupt_offset)
{
	enic_wq_init_start(wq, cq_index, 0, 0,
		error_interrupt_enable,
		error_interrupt_offset);
}

unsigned int vnic_wq_error_status(struct vnic_wq *wq)
{
	return ioread32(&wq->ctrl->error_status);
}

void vnic_wq_enable(struct vnic_wq *wq)
{
	iowrite32(1, &wq->ctrl->enable);
}

int vnic_wq_disable(struct vnic_wq *wq)
{
	unsigned int wait;
	struct vnic_dev *vdev = wq->vdev;

	iowrite32(0, &wq->ctrl->enable);

	/* Wait for HW to ACK disable request */
	for (wait = 0; wait < 1000; wait++) {
		if (!(ioread32(&wq->ctrl->running)))
			return 0;
		udelay(10);
	}

	vdev_neterr(vdev, "Failed to disable WQ[%d]\n", wq->index);

	return -ETIMEDOUT;
}

void vnic_wq_clean(struct vnic_wq *wq,
	void (*buf_clean)(struct vnic_wq *wq, struct vnic_wq_buf *buf))
{
	struct vnic_wq_buf *buf;

	buf = wq->to_clean;

	while (vnic_wq_desc_used(wq) > 0) {

		(*buf_clean)(wq, buf);

		buf = wq->to_clean = buf->next;
		wq->ring.desc_avail++;
	}

	wq->to_use = wq->to_clean = wq->bufs[0];

	iowrite32(0, &wq->ctrl->fetch_index);
	iowrite32(0, &wq->ctrl->posted_index);
	iowrite32(0, &wq->ctrl->error_status);

	vnic_dev_clear_desc_ring(&wq->ring);
}
