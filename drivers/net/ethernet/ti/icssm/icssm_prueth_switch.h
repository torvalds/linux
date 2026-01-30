/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2020-2021 Texas Instruments Incorporated - https://www.ti.com
 */

#ifndef __NET_TI_PRUETH_SWITCH_H
#define __NET_TI_PRUETH_SWITCH_H

#include <net/switchdev.h>

#include "icssm_prueth.h"
#include "icssm_prueth_fdb_tbl.h"
#include "icssm_switchdev.h"

void icssm_prueth_sw_set_stp_state(struct prueth *prueth,
				   enum prueth_port port, u8 state);
u8 icssm_prueth_sw_get_stp_state(struct prueth *prueth,
				 enum prueth_port port);

void icssm_prueth_sw_fdb_tbl_init(struct prueth *prueth);
int icssm_prueth_sw_init_fdb_table(struct prueth *prueth);
void icssm_prueth_sw_free_fdb_table(struct prueth *prueth);
int icssm_prueth_sw_do_purge_fdb(struct prueth_emac *emac);
void icssm_prueth_sw_fdb_add(struct prueth_emac *emac,
			     struct switchdev_notifier_fdb_info *fdb);
void icssm_prueth_sw_fdb_del(struct prueth_emac *emac,
			     struct switchdev_notifier_fdb_info *fdb);
int icssm_prueth_sw_learn_fdb(struct prueth_emac *emac, u8 *src_mac);
int icssm_prueth_sw_purge_fdb(struct prueth_emac *emac);
#endif /* __NET_TI_PRUETH_SWITCH_H */
