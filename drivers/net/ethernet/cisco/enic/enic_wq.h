/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright 2025 Cisco Systems, Inc.  All rights reserved.
 */

void enic_free_wq_buf(struct vnic_wq *wq, struct vnic_wq_buf *buf);
unsigned int enic_wq_cq_service(struct enic *enic, unsigned int cq_index,
				unsigned int work_to_do);
