/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell RVU Ethernet driver
 *
 * Copyright (C) 2023 Marvell.
 *
 */
#ifndef OTX2_QOS_H
#define OTX2_QOS_H

#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/rhashtable.h>

#define OTX2_QOS_MAX_LVL		4
#define OTX2_QOS_MAX_PRIO		7
#define OTX2_QOS_MAX_LEAF_NODES                16

enum qos_smq_operations {
	QOS_CFG_SQ,
	QOS_SMQ_FLUSH,
};

u64 otx2_get_txschq_rate_regval(struct otx2_nic *nic, u64 maxrate, u32 burst);

int otx2_setup_tc_htb(struct net_device *ndev, struct tc_htb_qopt_offload *htb);
int otx2_qos_get_qid(struct otx2_nic *pfvf);
void otx2_qos_free_qid(struct otx2_nic *pfvf, int qidx);
int otx2_qos_enable_sq(struct otx2_nic *pfvf, int qidx);
void otx2_qos_disable_sq(struct otx2_nic *pfvf, int qidx);

struct otx2_qos_cfg {
	u16 schq[NIX_TXSCH_LVL_CNT];
	u16 schq_contig[NIX_TXSCH_LVL_CNT];
	int static_node_pos[NIX_TXSCH_LVL_CNT];
	int dwrr_node_pos[NIX_TXSCH_LVL_CNT];
	u16 schq_contig_list[NIX_TXSCH_LVL_CNT][MAX_TXSCHQ_PER_FUNC];
	u16 schq_list[NIX_TXSCH_LVL_CNT][MAX_TXSCHQ_PER_FUNC];
	bool schq_index_used[NIX_TXSCH_LVL_CNT][MAX_TXSCHQ_PER_FUNC];
};

struct otx2_qos {
	DECLARE_HASHTABLE(qos_hlist, order_base_2(OTX2_QOS_MAX_LEAF_NODES));
	struct mutex qos_lock; /* child list lock */
	u16 qid_to_sqmap[OTX2_QOS_MAX_LEAF_NODES];
	struct list_head qos_tree;
	DECLARE_BITMAP(qos_sq_bmap, OTX2_QOS_MAX_LEAF_NODES);
	u16 maj_id;
	u16 defcls;
	u8  link_cfg_lvl; /* LINKX_CFG CSRs mapped to TL3 or TL2's index ? */
};

struct otx2_qos_node {
	struct list_head list; /* list management */
	struct list_head child_list;
	struct list_head child_schq_list;
	struct hlist_node hlist;
	DECLARE_BITMAP(prio_bmap, OTX2_QOS_MAX_PRIO + 1);
	struct otx2_qos_node *parent;	/* parent qos node */
	u64 rate; /* htb params */
	u64 ceil;
	u32 classid;
	u32 prio;
	u32 quantum;
	/* hw txschq */
	u16 schq;
	u16 qid;
	u16 prio_anchor;
	u16 max_static_prio;
	u16 child_dwrr_cnt;
	u16 child_static_cnt;
	u16 child_dwrr_prio;
	u16 txschq_idx;			/* txschq allocation index */
	u8 level;
	bool is_static;
};


#endif
