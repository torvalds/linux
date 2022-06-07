/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 */
#ifndef _VNIC_WQ_COPY_H_
#define _VNIC_WQ_COPY_H_

#include <linux/pci.h>
#include "vnic_wq.h"
#include "fcpio.h"

#define	VNIC_WQ_COPY_MAX 1

struct vnic_wq_copy {
	unsigned int index;
	struct vnic_dev *vdev;
	struct vnic_wq_ctrl __iomem *ctrl;	/* memory-mapped */
	struct vnic_dev_ring ring;
	unsigned to_use_index;
	unsigned to_clean_index;
};

static inline unsigned int vnic_wq_copy_desc_avail(struct vnic_wq_copy *wq)
{
	return wq->ring.desc_avail;
}

static inline unsigned int vnic_wq_copy_desc_in_use(struct vnic_wq_copy *wq)
{
	return wq->ring.desc_count - 1 - wq->ring.desc_avail;
}

static inline void *vnic_wq_copy_next_desc(struct vnic_wq_copy *wq)
{
	struct fcpio_host_req *desc = wq->ring.descs;
	return &desc[wq->to_use_index];
}

static inline void vnic_wq_copy_post(struct vnic_wq_copy *wq)
{

	((wq->to_use_index + 1) == wq->ring.desc_count) ?
		(wq->to_use_index = 0) : (wq->to_use_index++);
	wq->ring.desc_avail--;

	/* Adding write memory barrier prevents compiler and/or CPU
	 * reordering, thus avoiding descriptor posting before
	 * descriptor is initialized. Otherwise, hardware can read
	 * stale descriptor fields.
	 */
	wmb();

	iowrite32(wq->to_use_index, &wq->ctrl->posted_index);
}

static inline void vnic_wq_copy_desc_process(struct vnic_wq_copy *wq, u16 index)
{
	unsigned int cnt;

	if (wq->to_clean_index <= index)
		cnt = (index - wq->to_clean_index) + 1;
	else
		cnt = wq->ring.desc_count - wq->to_clean_index + index + 1;

	wq->to_clean_index = ((index + 1) % wq->ring.desc_count);
	wq->ring.desc_avail += cnt;

}

static inline void vnic_wq_copy_service(struct vnic_wq_copy *wq,
	u16 completed_index,
	void (*q_service)(struct vnic_wq_copy *wq,
	struct fcpio_host_req *wq_desc))
{
	struct fcpio_host_req *wq_desc = wq->ring.descs;
	unsigned int curr_index;

	while (1) {

		if (q_service)
			(*q_service)(wq, &wq_desc[wq->to_clean_index]);

		wq->ring.desc_avail++;

		curr_index = wq->to_clean_index;

		/* increment the to-clean index so that we start
		 * with an unprocessed index next time we enter the loop
		 */
		((wq->to_clean_index + 1) == wq->ring.desc_count) ?
			(wq->to_clean_index = 0) : (wq->to_clean_index++);

		if (curr_index == completed_index)
			break;

		/* we have cleaned all the entries */
		if ((completed_index == (u16)-1) &&
		    (wq->to_clean_index == wq->to_use_index))
			break;
	}
}

void vnic_wq_copy_enable(struct vnic_wq_copy *wq);
int vnic_wq_copy_disable(struct vnic_wq_copy *wq);
void vnic_wq_copy_free(struct vnic_wq_copy *wq);
int vnic_wq_copy_alloc(struct vnic_dev *vdev, struct vnic_wq_copy *wq,
	unsigned int index, unsigned int desc_count, unsigned int desc_size);
void vnic_wq_copy_init(struct vnic_wq_copy *wq, unsigned int cq_index,
	unsigned int error_interrupt_enable,
	unsigned int error_interrupt_offset);
void vnic_wq_copy_clean(struct vnic_wq_copy *wq,
	void (*q_clean)(struct vnic_wq_copy *wq,
	struct fcpio_host_req *wq_desc));

#endif /* _VNIC_WQ_COPY_H_ */
