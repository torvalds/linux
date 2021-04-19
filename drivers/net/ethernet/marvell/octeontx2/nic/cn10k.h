/* SPDX-License-Identifier: GPL-2.0
 * Marvell OcteonTx2 RVU Ethernet driver
 *
 * Copyright (C) 2020 Marvell.
 */

#ifndef CN10K_H
#define CN10K_H

#include "otx2_common.h"

void cn10k_refill_pool_ptrs(void *dev, struct otx2_cq_queue *cq);
void cn10k_sqe_flush(void *dev, struct otx2_snd_queue *sq, int size, int qidx);
int cn10k_sq_aq_init(void *dev, u16 qidx, u16 sqb_aura);
int cn10k_pf_lmtst_init(struct otx2_nic *pf);
int cn10k_vf_lmtst_init(struct otx2_nic *vf);
#endif /* CN10K_H */
