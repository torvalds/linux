/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 */
#ifndef _VNIC_CQ_COPY_H_
#define _VNIC_CQ_COPY_H_

#include "fcpio.h"

static inline unsigned int vnic_cq_copy_service(
	struct vnic_cq *cq,
	int (*q_service)(struct vnic_dev *vdev,
			 unsigned int index,
			 struct fcpio_fw_req *desc),
	unsigned int work_to_do)

{
	struct fcpio_fw_req *desc;
	unsigned int work_done = 0;
	u8 color;

	desc = (struct fcpio_fw_req *)((u8 *)cq->ring.descs +
		cq->ring.desc_size * cq->to_clean);
	fcpio_color_dec(desc, &color);

	while (color != cq->last_color) {

		if ((*q_service)(cq->vdev, cq->index, desc))
			break;

		cq->to_clean++;
		if (cq->to_clean == cq->ring.desc_count) {
			cq->to_clean = 0;
			cq->last_color = cq->last_color ? 0 : 1;
		}

		desc = (struct fcpio_fw_req *)((u8 *)cq->ring.descs +
			cq->ring.desc_size * cq->to_clean);
		fcpio_color_dec(desc, &color);

		work_done++;
		if (work_done >= work_to_do)
			break;
	}

	return work_done;
}

#endif /* _VNIC_CQ_COPY_H_ */
