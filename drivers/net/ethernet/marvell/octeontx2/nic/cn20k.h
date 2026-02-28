/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell RVU Ethernet driver
 *
 * Copyright (C) 2024 Marvell.
 *
 */

#ifndef CN20K_H
#define CN20K_H

#include "otx2_common.h"

struct otx2_flow_config;
struct otx2_tc_flow;

void cn20k_init(struct otx2_nic *pfvf);
int cn20k_register_pfvf_mbox_intr(struct otx2_nic *pf, int numvfs);
void cn20k_disable_pfvf_mbox_intr(struct otx2_nic *pf, int numvfs);
void cn20k_enable_pfvf_mbox_intr(struct otx2_nic *pf, int numvfs);
void cn20k_tc_update_mcam_table_del_req(struct otx2_nic *nic,
					struct otx2_flow_config *flow_cfg,
					struct otx2_tc_flow *node);
int cn20k_tc_update_mcam_table_add_req(struct otx2_nic *nic,
				       struct otx2_flow_config *flow_cfg,
				       struct otx2_tc_flow *node);
int cn20k_tc_alloc_entry(struct otx2_nic *nic,
			 struct flow_cls_offload *tc_flow_cmd,
			 struct otx2_tc_flow *new_node,
			 struct npc_install_flow_req *dummy);
int cn20k_tc_free_mcam_entry(struct otx2_nic *nic, u16 entry);
#endif /* CN20K_H */
