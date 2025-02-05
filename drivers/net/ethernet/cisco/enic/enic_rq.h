/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright 2024 Cisco Systems, Inc.  All rights reserved.
 */

int enic_rq_service(struct vnic_dev *vdev, struct cq_desc *cq_desc, u8 type,
		    u16 q_number, u16 completed_index, void *opaque);
void enic_rq_indicate_buf(struct vnic_rq *rq, struct cq_desc *cq_desc,
			  struct vnic_rq_buf *buf, int skipped, void *opaque);
int enic_rq_alloc_buf(struct vnic_rq *rq);
void enic_free_rq_buf(struct vnic_rq *rq, struct vnic_rq_buf *buf);
