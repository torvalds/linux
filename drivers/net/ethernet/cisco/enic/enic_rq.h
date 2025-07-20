/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright 2024 Cisco Systems, Inc.  All rights reserved.
 */

unsigned int enic_rq_cq_service(struct enic *enic, unsigned int cq_index,
				unsigned int work_to_do);
int enic_rq_alloc_buf(struct vnic_rq *rq);
void enic_free_rq_buf(struct vnic_rq *rq, struct vnic_rq_buf *buf);
