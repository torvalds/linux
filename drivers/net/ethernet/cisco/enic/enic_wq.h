/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright 2025 Cisco Systems, Inc.  All rights reserved.
 */

unsigned int vnic_cq_service(struct vnic_cq *cq, unsigned int work_to_do,
			     int (*q_service)(struct vnic_dev *vdev,
					      struct cq_desc *cq_desc, u8 type,
					      u16 q_number, u16 completed_index,
					      void *opaque), void *opaque);

void enic_free_wq_buf(struct vnic_wq *wq, struct vnic_wq_buf *buf);

int enic_wq_service(struct vnic_dev *vdev, struct cq_desc *cq_desc, u8 type,
		    u16 q_number, u16 completed_index, void *opaque);
