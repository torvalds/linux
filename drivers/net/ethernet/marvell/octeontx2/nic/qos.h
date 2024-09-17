/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell RVU Ethernet driver
 *
 * Copyright (C) 2023 Marvell.
 *
 */
#ifndef OTX2_QOS_H
#define OTX2_QOS_H

#define OTX2_QOS_MAX_LEAF_NODES                16

int otx2_qos_enable_sq(struct otx2_nic *pfvf, int qidx, u16 smq);
void otx2_qos_disable_sq(struct otx2_nic *pfvf, int qidx, u16 mdq);

struct otx2_qos {
	       u16 qid_to_sqmap[OTX2_QOS_MAX_LEAF_NODES];
	};

#endif
