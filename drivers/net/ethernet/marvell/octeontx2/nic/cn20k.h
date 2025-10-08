/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell RVU Ethernet driver
 *
 * Copyright (C) 2024 Marvell.
 *
 */

#ifndef CN20K_H
#define CN20K_H

#include "otx2_common.h"

void cn20k_init(struct otx2_nic *pfvf);
int cn20k_register_pfvf_mbox_intr(struct otx2_nic *pf, int numvfs);
void cn20k_disable_pfvf_mbox_intr(struct otx2_nic *pf, int numvfs);
void cn20k_enable_pfvf_mbox_intr(struct otx2_nic *pf, int numvfs);
#endif /* CN20K_H */
