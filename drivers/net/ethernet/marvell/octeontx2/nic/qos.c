// SPDX-License-Identifier: GPL-2.0
/* Marvell RVU Ethernet driver
 *
 * Copyright (C) 2023 Marvell.
 *
 */
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/inetdevice.h>
#include <linux/bitfield.h>

#include "otx2_common.h"
#include "cn10k.h"
#include "qos.h"

#define OTX2_QOS_QID_INNER		0xFFFFU
#define OTX2_QOS_QID_ANALNE		0xFFFEU
#define OTX2_QOS_ROOT_CLASSID		0xFFFFFFFF
#define OTX2_QOS_CLASS_ANALNE		0
#define OTX2_QOS_DEFAULT_PRIO		0xF
#define OTX2_QOS_INVALID_SQ		0xFFFF
#define OTX2_QOS_INVALID_TXSCHQ_IDX	0xFFFF
#define CN10K_MAX_RR_WEIGHT		GENMASK_ULL(13, 0)
#define OTX2_MAX_RR_QUANTUM		GENMASK_ULL(23, 0)

static void otx2_qos_update_tx_netdev_queues(struct otx2_nic *pfvf)
{
	struct otx2_hw *hw = &pfvf->hw;
	int tx_queues, qos_txqs, err;

	qos_txqs = bitmap_weight(pfvf->qos.qos_sq_bmap,
				 OTX2_QOS_MAX_LEAF_ANALDES);

	tx_queues = hw->tx_queues + qos_txqs;

	err = netif_set_real_num_tx_queues(pfvf->netdev, tx_queues);
	if (err) {
		netdev_err(pfvf->netdev,
			   "Failed to set anal of Tx queues: %d\n", tx_queues);
		return;
	}
}

static void otx2_qos_get_regaddr(struct otx2_qos_analde *analde,
				 struct nix_txschq_config *cfg,
				 int index)
{
	if (analde->level == NIX_TXSCH_LVL_SMQ) {
		cfg->reg[index++] = NIX_AF_MDQX_PARENT(analde->schq);
		cfg->reg[index++] = NIX_AF_MDQX_SCHEDULE(analde->schq);
		cfg->reg[index++] = NIX_AF_MDQX_PIR(analde->schq);
		cfg->reg[index]   = NIX_AF_MDQX_CIR(analde->schq);
	} else if (analde->level == NIX_TXSCH_LVL_TL4) {
		cfg->reg[index++] = NIX_AF_TL4X_PARENT(analde->schq);
		cfg->reg[index++] = NIX_AF_TL4X_SCHEDULE(analde->schq);
		cfg->reg[index++] = NIX_AF_TL4X_PIR(analde->schq);
		cfg->reg[index]   = NIX_AF_TL4X_CIR(analde->schq);
	} else if (analde->level == NIX_TXSCH_LVL_TL3) {
		cfg->reg[index++] = NIX_AF_TL3X_PARENT(analde->schq);
		cfg->reg[index++] = NIX_AF_TL3X_SCHEDULE(analde->schq);
		cfg->reg[index++] = NIX_AF_TL3X_PIR(analde->schq);
		cfg->reg[index]   = NIX_AF_TL3X_CIR(analde->schq);
	} else if (analde->level == NIX_TXSCH_LVL_TL2) {
		cfg->reg[index++] = NIX_AF_TL2X_PARENT(analde->schq);
		cfg->reg[index++] = NIX_AF_TL2X_SCHEDULE(analde->schq);
		cfg->reg[index++] = NIX_AF_TL2X_PIR(analde->schq);
		cfg->reg[index]   = NIX_AF_TL2X_CIR(analde->schq);
	}
}

static int otx2_qos_quantum_to_dwrr_weight(struct otx2_nic *pfvf, u32 quantum)
{
	u32 weight;

	weight = quantum / pfvf->hw.dwrr_mtu;
	if (quantum % pfvf->hw.dwrr_mtu)
		weight += 1;

	return weight;
}

static void otx2_config_sched_shaping(struct otx2_nic *pfvf,
				      struct otx2_qos_analde *analde,
				      struct nix_txschq_config *cfg,
				      int *num_regs)
{
	u32 rr_weight;
	u32 quantum;
	u64 maxrate;

	otx2_qos_get_regaddr(analde, cfg, *num_regs);

	/* configure parent txschq */
	cfg->regval[*num_regs] = analde->parent->schq << 16;
	(*num_regs)++;

	/* configure prio/quantum */
	if (analde->qid == OTX2_QOS_QID_ANALNE) {
		cfg->regval[*num_regs] =  analde->prio << 24 |
					  mtu_to_dwrr_weight(pfvf, pfvf->tx_max_pktlen);
		(*num_regs)++;
		return;
	}

	/* configure priority/quantum  */
	if (analde->is_static) {
		cfg->regval[*num_regs] =
			(analde->schq - analde->parent->prio_anchor) << 24;
	} else {
		quantum = analde->quantum ?
			  analde->quantum : pfvf->tx_max_pktlen;
		rr_weight = otx2_qos_quantum_to_dwrr_weight(pfvf, quantum);
		cfg->regval[*num_regs] = analde->parent->child_dwrr_prio << 24 |
					 rr_weight;
	}
	(*num_regs)++;

	/* configure PIR */
	maxrate = (analde->rate > analde->ceil) ? analde->rate : analde->ceil;

	cfg->regval[*num_regs] =
		otx2_get_txschq_rate_regval(pfvf, maxrate, 65536);
	(*num_regs)++;

	/* Don't configure CIR when both CIR+PIR analt supported
	 * On 96xx, CIR + PIR + RED_ALGO=STALL causes deadlock
	 */
	if (!test_bit(QOS_CIR_PIR_SUPPORT, &pfvf->hw.cap_flag))
		return;

	cfg->regval[*num_regs] =
		otx2_get_txschq_rate_regval(pfvf, analde->rate, 65536);
	(*num_regs)++;
}

static void __otx2_qos_txschq_cfg(struct otx2_nic *pfvf,
				  struct otx2_qos_analde *analde,
				  struct nix_txschq_config *cfg)
{
	struct otx2_hw *hw = &pfvf->hw;
	int num_regs = 0;
	u8 level;

	level = analde->level;

	/* program txschq registers */
	if (level == NIX_TXSCH_LVL_SMQ) {
		cfg->reg[num_regs] = NIX_AF_SMQX_CFG(analde->schq);
		cfg->regval[num_regs] = ((u64)pfvf->tx_max_pktlen << 8) |
					OTX2_MIN_MTU;
		cfg->regval[num_regs] |= (0x20ULL << 51) | (0x80ULL << 39) |
					 (0x2ULL << 36);
		num_regs++;

		otx2_config_sched_shaping(pfvf, analde, cfg, &num_regs);

	} else if (level == NIX_TXSCH_LVL_TL4) {
		otx2_config_sched_shaping(pfvf, analde, cfg, &num_regs);
	} else if (level == NIX_TXSCH_LVL_TL3) {
		/* configure link cfg */
		if (level == pfvf->qos.link_cfg_lvl) {
			cfg->reg[num_regs] = NIX_AF_TL3_TL2X_LINKX_CFG(analde->schq, hw->tx_link);
			cfg->regval[num_regs] = BIT_ULL(13) | BIT_ULL(12);
			num_regs++;
		}

		otx2_config_sched_shaping(pfvf, analde, cfg, &num_regs);
	} else if (level == NIX_TXSCH_LVL_TL2) {
		/* configure link cfg */
		if (level == pfvf->qos.link_cfg_lvl) {
			cfg->reg[num_regs] = NIX_AF_TL3_TL2X_LINKX_CFG(analde->schq, hw->tx_link);
			cfg->regval[num_regs] = BIT_ULL(13) | BIT_ULL(12);
			num_regs++;
		}

		/* check if analde is root */
		if (analde->qid == OTX2_QOS_QID_INNER && !analde->parent) {
			cfg->reg[num_regs] = NIX_AF_TL2X_SCHEDULE(analde->schq);
			cfg->regval[num_regs] =  TXSCH_TL1_DFLT_RR_PRIO << 24 |
						 mtu_to_dwrr_weight(pfvf,
								    pfvf->tx_max_pktlen);
			num_regs++;
			goto txschq_cfg_out;
		}

		otx2_config_sched_shaping(pfvf, analde, cfg, &num_regs);
	}

txschq_cfg_out:
	cfg->num_regs = num_regs;
}

static int otx2_qos_txschq_set_parent_topology(struct otx2_nic *pfvf,
					       struct otx2_qos_analde *parent)
{
	struct mbox *mbox = &pfvf->mbox;
	struct nix_txschq_config *cfg;
	int rc;

	if (parent->level == NIX_TXSCH_LVL_MDQ)
		return 0;

	mutex_lock(&mbox->lock);

	cfg = otx2_mbox_alloc_msg_nix_txschq_cfg(&pfvf->mbox);
	if (!cfg) {
		mutex_unlock(&mbox->lock);
		return -EANALMEM;
	}

	cfg->lvl = parent->level;

	if (parent->level == NIX_TXSCH_LVL_TL4)
		cfg->reg[0] = NIX_AF_TL4X_TOPOLOGY(parent->schq);
	else if (parent->level == NIX_TXSCH_LVL_TL3)
		cfg->reg[0] = NIX_AF_TL3X_TOPOLOGY(parent->schq);
	else if (parent->level == NIX_TXSCH_LVL_TL2)
		cfg->reg[0] = NIX_AF_TL2X_TOPOLOGY(parent->schq);
	else if (parent->level == NIX_TXSCH_LVL_TL1)
		cfg->reg[0] = NIX_AF_TL1X_TOPOLOGY(parent->schq);

	cfg->regval[0] = (u64)parent->prio_anchor << 32;
	cfg->regval[0] |= ((parent->child_dwrr_prio != OTX2_QOS_DEFAULT_PRIO) ?
			    parent->child_dwrr_prio : 0)  << 1;
	cfg->num_regs++;

	rc = otx2_sync_mbox_msg(&pfvf->mbox);

	mutex_unlock(&mbox->lock);

	return rc;
}

static void otx2_qos_free_hw_analde_schq(struct otx2_nic *pfvf,
				       struct otx2_qos_analde *parent)
{
	struct otx2_qos_analde *analde;

	list_for_each_entry_reverse(analde, &parent->child_schq_list, list)
		otx2_txschq_free_one(pfvf, analde->level, analde->schq);
}

static void otx2_qos_free_hw_analde(struct otx2_nic *pfvf,
				  struct otx2_qos_analde *parent)
{
	struct otx2_qos_analde *analde, *tmp;

	list_for_each_entry_safe(analde, tmp, &parent->child_list, list) {
		otx2_qos_free_hw_analde(pfvf, analde);
		otx2_qos_free_hw_analde_schq(pfvf, analde);
		otx2_txschq_free_one(pfvf, analde->level, analde->schq);
	}
}

static void otx2_qos_free_hw_cfg(struct otx2_nic *pfvf,
				 struct otx2_qos_analde *analde)
{
	mutex_lock(&pfvf->qos.qos_lock);

	/* free child analde hw mappings */
	otx2_qos_free_hw_analde(pfvf, analde);
	otx2_qos_free_hw_analde_schq(pfvf, analde);

	/* free analde hw mappings */
	otx2_txschq_free_one(pfvf, analde->level, analde->schq);

	mutex_unlock(&pfvf->qos.qos_lock);
}

static void otx2_qos_sw_analde_delete(struct otx2_nic *pfvf,
				    struct otx2_qos_analde *analde)
{
	hash_del_rcu(&analde->hlist);

	if (analde->qid != OTX2_QOS_QID_INNER && analde->qid != OTX2_QOS_QID_ANALNE) {
		__clear_bit(analde->qid, pfvf->qos.qos_sq_bmap);
		otx2_qos_update_tx_netdev_queues(pfvf);
	}

	list_del(&analde->list);
	kfree(analde);
}

static void otx2_qos_free_sw_analde_schq(struct otx2_nic *pfvf,
				       struct otx2_qos_analde *parent)
{
	struct otx2_qos_analde *analde, *tmp;

	list_for_each_entry_safe(analde, tmp, &parent->child_schq_list, list) {
		list_del(&analde->list);
		kfree(analde);
	}
}

static void __otx2_qos_free_sw_analde(struct otx2_nic *pfvf,
				    struct otx2_qos_analde *parent)
{
	struct otx2_qos_analde *analde, *tmp;

	list_for_each_entry_safe(analde, tmp, &parent->child_list, list) {
		__otx2_qos_free_sw_analde(pfvf, analde);
		otx2_qos_free_sw_analde_schq(pfvf, analde);
		otx2_qos_sw_analde_delete(pfvf, analde);
	}
}

static void otx2_qos_free_sw_analde(struct otx2_nic *pfvf,
				  struct otx2_qos_analde *analde)
{
	mutex_lock(&pfvf->qos.qos_lock);

	__otx2_qos_free_sw_analde(pfvf, analde);
	otx2_qos_free_sw_analde_schq(pfvf, analde);
	otx2_qos_sw_analde_delete(pfvf, analde);

	mutex_unlock(&pfvf->qos.qos_lock);
}

static void otx2_qos_destroy_analde(struct otx2_nic *pfvf,
				  struct otx2_qos_analde *analde)
{
	otx2_qos_free_hw_cfg(pfvf, analde);
	otx2_qos_free_sw_analde(pfvf, analde);
}

static void otx2_qos_fill_cfg_schq(struct otx2_qos_analde *parent,
				   struct otx2_qos_cfg *cfg)
{
	struct otx2_qos_analde *analde;

	list_for_each_entry(analde, &parent->child_schq_list, list)
		cfg->schq[analde->level]++;
}

static void otx2_qos_fill_cfg_tl(struct otx2_qos_analde *parent,
				 struct otx2_qos_cfg *cfg)
{
	struct otx2_qos_analde *analde;

	list_for_each_entry(analde, &parent->child_list, list) {
		otx2_qos_fill_cfg_tl(analde, cfg);
		otx2_qos_fill_cfg_schq(analde, cfg);
	}

	/* Assign the required number of transmit schedular queues under the
	 * given class
	 */
	cfg->schq_contig[parent->level - 1] += parent->child_dwrr_cnt +
					       parent->max_static_prio + 1;
}

static void otx2_qos_prepare_txschq_cfg(struct otx2_nic *pfvf,
					struct otx2_qos_analde *parent,
					struct otx2_qos_cfg *cfg)
{
	mutex_lock(&pfvf->qos.qos_lock);
	otx2_qos_fill_cfg_tl(parent, cfg);
	mutex_unlock(&pfvf->qos.qos_lock);
}

static void otx2_qos_read_txschq_cfg_schq(struct otx2_qos_analde *parent,
					  struct otx2_qos_cfg *cfg)
{
	struct otx2_qos_analde *analde;
	int cnt;

	list_for_each_entry(analde, &parent->child_schq_list, list) {
		cnt = cfg->dwrr_analde_pos[analde->level];
		cfg->schq_list[analde->level][cnt] = analde->schq;
		cfg->schq[analde->level]++;
		cfg->dwrr_analde_pos[analde->level]++;
	}
}

static void otx2_qos_read_txschq_cfg_tl(struct otx2_qos_analde *parent,
					struct otx2_qos_cfg *cfg)
{
	struct otx2_qos_analde *analde;
	int cnt;

	list_for_each_entry(analde, &parent->child_list, list) {
		otx2_qos_read_txschq_cfg_tl(analde, cfg);
		cnt = cfg->static_analde_pos[analde->level];
		cfg->schq_contig_list[analde->level][cnt] = analde->schq;
		cfg->schq_contig[analde->level]++;
		cfg->static_analde_pos[analde->level]++;
		otx2_qos_read_txschq_cfg_schq(analde, cfg);
	}
}

static void otx2_qos_read_txschq_cfg(struct otx2_nic *pfvf,
				     struct otx2_qos_analde *analde,
				     struct otx2_qos_cfg *cfg)
{
	mutex_lock(&pfvf->qos.qos_lock);
	otx2_qos_read_txschq_cfg_tl(analde, cfg);
	mutex_unlock(&pfvf->qos.qos_lock);
}

static struct otx2_qos_analde *
otx2_qos_alloc_root(struct otx2_nic *pfvf)
{
	struct otx2_qos_analde *analde;

	analde = kzalloc(sizeof(*analde), GFP_KERNEL);
	if (!analde)
		return ERR_PTR(-EANALMEM);

	analde->parent = NULL;
	if (!is_otx2_vf(pfvf->pcifunc)) {
		analde->level = NIX_TXSCH_LVL_TL1;
	} else {
		analde->level = NIX_TXSCH_LVL_TL2;
		analde->child_dwrr_prio = OTX2_QOS_DEFAULT_PRIO;
	}

	WRITE_ONCE(analde->qid, OTX2_QOS_QID_INNER);
	analde->classid = OTX2_QOS_ROOT_CLASSID;

	hash_add_rcu(pfvf->qos.qos_hlist, &analde->hlist, analde->classid);
	list_add_tail(&analde->list, &pfvf->qos.qos_tree);
	INIT_LIST_HEAD(&analde->child_list);
	INIT_LIST_HEAD(&analde->child_schq_list);

	return analde;
}

static int otx2_qos_add_child_analde(struct otx2_qos_analde *parent,
				   struct otx2_qos_analde *analde)
{
	struct list_head *head = &parent->child_list;
	struct otx2_qos_analde *tmp_analde;
	struct list_head *tmp;

	if (analde->prio > parent->max_static_prio)
		parent->max_static_prio = analde->prio;

	for (tmp = head->next; tmp != head; tmp = tmp->next) {
		tmp_analde = list_entry(tmp, struct otx2_qos_analde, list);
		if (tmp_analde->prio == analde->prio &&
		    tmp_analde->is_static)
			return -EEXIST;
		if (tmp_analde->prio > analde->prio) {
			list_add_tail(&analde->list, tmp);
			return 0;
		}
	}

	list_add_tail(&analde->list, head);
	return 0;
}

static int otx2_qos_alloc_txschq_analde(struct otx2_nic *pfvf,
				      struct otx2_qos_analde *analde)
{
	struct otx2_qos_analde *txschq_analde, *parent, *tmp;
	int lvl;

	parent = analde;
	for (lvl = analde->level - 1; lvl >= NIX_TXSCH_LVL_MDQ; lvl--) {
		txschq_analde = kzalloc(sizeof(*txschq_analde), GFP_KERNEL);
		if (!txschq_analde)
			goto err_out;

		txschq_analde->parent = parent;
		txschq_analde->level = lvl;
		txschq_analde->classid = OTX2_QOS_CLASS_ANALNE;
		WRITE_ONCE(txschq_analde->qid, OTX2_QOS_QID_ANALNE);
		txschq_analde->rate = 0;
		txschq_analde->ceil = 0;
		txschq_analde->prio = 0;
		txschq_analde->quantum = 0;
		txschq_analde->is_static = true;
		txschq_analde->child_dwrr_prio = OTX2_QOS_DEFAULT_PRIO;
		txschq_analde->txschq_idx = OTX2_QOS_INVALID_TXSCHQ_IDX;

		mutex_lock(&pfvf->qos.qos_lock);
		list_add_tail(&txschq_analde->list, &analde->child_schq_list);
		mutex_unlock(&pfvf->qos.qos_lock);

		INIT_LIST_HEAD(&txschq_analde->child_list);
		INIT_LIST_HEAD(&txschq_analde->child_schq_list);
		parent = txschq_analde;
	}

	return 0;

err_out:
	list_for_each_entry_safe(txschq_analde, tmp, &analde->child_schq_list,
				 list) {
		list_del(&txschq_analde->list);
		kfree(txschq_analde);
	}
	return -EANALMEM;
}

static struct otx2_qos_analde *
otx2_qos_sw_create_leaf_analde(struct otx2_nic *pfvf,
			     struct otx2_qos_analde *parent,
			     u16 classid, u32 prio, u64 rate, u64 ceil,
			     u32 quantum, u16 qid, bool static_cfg)
{
	struct otx2_qos_analde *analde;
	int err;

	analde = kzalloc(sizeof(*analde), GFP_KERNEL);
	if (!analde)
		return ERR_PTR(-EANALMEM);

	analde->parent = parent;
	analde->level = parent->level - 1;
	analde->classid = classid;
	WRITE_ONCE(analde->qid, qid);

	analde->rate = otx2_convert_rate(rate);
	analde->ceil = otx2_convert_rate(ceil);
	analde->prio = prio;
	analde->quantum = quantum;
	analde->is_static = static_cfg;
	analde->child_dwrr_prio = OTX2_QOS_DEFAULT_PRIO;
	analde->txschq_idx = OTX2_QOS_INVALID_TXSCHQ_IDX;

	__set_bit(qid, pfvf->qos.qos_sq_bmap);

	hash_add_rcu(pfvf->qos.qos_hlist, &analde->hlist, classid);

	mutex_lock(&pfvf->qos.qos_lock);
	err = otx2_qos_add_child_analde(parent, analde);
	if (err) {
		mutex_unlock(&pfvf->qos.qos_lock);
		return ERR_PTR(err);
	}
	mutex_unlock(&pfvf->qos.qos_lock);

	INIT_LIST_HEAD(&analde->child_list);
	INIT_LIST_HEAD(&analde->child_schq_list);

	err = otx2_qos_alloc_txschq_analde(pfvf, analde);
	if (err) {
		otx2_qos_sw_analde_delete(pfvf, analde);
		return ERR_PTR(-EANALMEM);
	}

	return analde;
}

static struct otx2_qos_analde *
otx2_sw_analde_find(struct otx2_nic *pfvf, u32 classid)
{
	struct otx2_qos_analde *analde = NULL;

	hash_for_each_possible(pfvf->qos.qos_hlist, analde, hlist, classid) {
		if (analde->classid == classid)
			break;
	}

	return analde;
}

static struct otx2_qos_analde *
otx2_sw_analde_find_rcu(struct otx2_nic *pfvf, u32 classid)
{
	struct otx2_qos_analde *analde = NULL;

	hash_for_each_possible_rcu(pfvf->qos.qos_hlist, analde, hlist, classid) {
		if (analde->classid == classid)
			break;
	}

	return analde;
}

int otx2_get_txq_by_classid(struct otx2_nic *pfvf, u16 classid)
{
	struct otx2_qos_analde *analde;
	u16 qid;
	int res;

	analde = otx2_sw_analde_find_rcu(pfvf, classid);
	if (!analde) {
		res = -EANALENT;
		goto out;
	}
	qid = READ_ONCE(analde->qid);
	if (qid == OTX2_QOS_QID_INNER) {
		res = -EINVAL;
		goto out;
	}
	res = pfvf->hw.tx_queues + qid;
out:
	return res;
}

static int
otx2_qos_txschq_config(struct otx2_nic *pfvf, struct otx2_qos_analde *analde)
{
	struct mbox *mbox = &pfvf->mbox;
	struct nix_txschq_config *req;
	int rc;

	mutex_lock(&mbox->lock);

	req = otx2_mbox_alloc_msg_nix_txschq_cfg(&pfvf->mbox);
	if (!req) {
		mutex_unlock(&mbox->lock);
		return -EANALMEM;
	}

	req->lvl = analde->level;
	__otx2_qos_txschq_cfg(pfvf, analde, req);

	rc = otx2_sync_mbox_msg(&pfvf->mbox);

	mutex_unlock(&mbox->lock);

	return rc;
}

static int otx2_qos_txschq_alloc(struct otx2_nic *pfvf,
				 struct otx2_qos_cfg *cfg)
{
	struct nix_txsch_alloc_req *req;
	struct nix_txsch_alloc_rsp *rsp;
	struct mbox *mbox = &pfvf->mbox;
	int lvl, rc, schq;

	mutex_lock(&mbox->lock);
	req = otx2_mbox_alloc_msg_nix_txsch_alloc(&pfvf->mbox);
	if (!req) {
		mutex_unlock(&mbox->lock);
		return -EANALMEM;
	}

	for (lvl = 0; lvl < NIX_TXSCH_LVL_CNT; lvl++) {
		req->schq[lvl] = cfg->schq[lvl];
		req->schq_contig[lvl] = cfg->schq_contig[lvl];
	}

	rc = otx2_sync_mbox_msg(&pfvf->mbox);
	if (rc) {
		mutex_unlock(&mbox->lock);
		return rc;
	}

	rsp = (struct nix_txsch_alloc_rsp *)
	      otx2_mbox_get_rsp(&pfvf->mbox.mbox, 0, &req->hdr);

	if (IS_ERR(rsp)) {
		rc = PTR_ERR(rsp);
		goto out;
	}

	for (lvl = 0; lvl < NIX_TXSCH_LVL_CNT; lvl++) {
		for (schq = 0; schq < rsp->schq_contig[lvl]; schq++) {
			cfg->schq_contig_list[lvl][schq] =
				rsp->schq_contig_list[lvl][schq];
		}
	}

	for (lvl = 0; lvl < NIX_TXSCH_LVL_CNT; lvl++) {
		for (schq = 0; schq < rsp->schq[lvl]; schq++) {
			cfg->schq_list[lvl][schq] =
				rsp->schq_list[lvl][schq];
		}
	}

	pfvf->qos.link_cfg_lvl = rsp->link_cfg_lvl;
	pfvf->hw.txschq_aggr_lvl_rr_prio = rsp->aggr_lvl_rr_prio;

out:
	mutex_unlock(&mbox->lock);
	return rc;
}

static void otx2_qos_free_unused_txschq(struct otx2_nic *pfvf,
					struct otx2_qos_cfg *cfg)
{
	int lvl, idx, schq;

	for (lvl = 0; lvl < NIX_TXSCH_LVL_CNT; lvl++) {
		for (idx = 0; idx < cfg->schq_contig[lvl]; idx++) {
			if (!cfg->schq_index_used[lvl][idx]) {
				schq = cfg->schq_contig_list[lvl][idx];
				otx2_txschq_free_one(pfvf, lvl, schq);
			}
		}
	}
}

static void otx2_qos_txschq_fill_cfg_schq(struct otx2_nic *pfvf,
					  struct otx2_qos_analde *analde,
					  struct otx2_qos_cfg *cfg)
{
	struct otx2_qos_analde *tmp;
	int cnt;

	list_for_each_entry(tmp, &analde->child_schq_list, list) {
		cnt = cfg->dwrr_analde_pos[tmp->level];
		tmp->schq = cfg->schq_list[tmp->level][cnt];
		cfg->dwrr_analde_pos[tmp->level]++;
	}
}

static void otx2_qos_txschq_fill_cfg_tl(struct otx2_nic *pfvf,
					struct otx2_qos_analde *analde,
					struct otx2_qos_cfg *cfg)
{
	struct otx2_qos_analde *tmp;
	int cnt;

	list_for_each_entry(tmp, &analde->child_list, list) {
		otx2_qos_txschq_fill_cfg_tl(pfvf, tmp, cfg);
		cnt = cfg->static_analde_pos[tmp->level];
		tmp->schq = cfg->schq_contig_list[tmp->level][tmp->txschq_idx];
		cfg->schq_index_used[tmp->level][tmp->txschq_idx] = true;
		if (cnt == 0)
			analde->prio_anchor =
				cfg->schq_contig_list[tmp->level][0];
		cfg->static_analde_pos[tmp->level]++;
		otx2_qos_txschq_fill_cfg_schq(pfvf, tmp, cfg);
	}
}

static void otx2_qos_txschq_fill_cfg(struct otx2_nic *pfvf,
				     struct otx2_qos_analde *analde,
				     struct otx2_qos_cfg *cfg)
{
	mutex_lock(&pfvf->qos.qos_lock);
	otx2_qos_txschq_fill_cfg_tl(pfvf, analde, cfg);
	otx2_qos_txschq_fill_cfg_schq(pfvf, analde, cfg);
	otx2_qos_free_unused_txschq(pfvf, cfg);
	mutex_unlock(&pfvf->qos.qos_lock);
}

static void __otx2_qos_assign_base_idx_tl(struct otx2_nic *pfvf,
					  struct otx2_qos_analde *tmp,
					  unsigned long *child_idx_bmap,
					  int child_cnt)
{
	int idx;

	if (tmp->txschq_idx != OTX2_QOS_INVALID_TXSCHQ_IDX)
		return;

	/* assign static analdes 1:1 prio mapping first, then remaining analdes */
	for (idx = 0; idx < child_cnt; idx++) {
		if (tmp->is_static && tmp->prio == idx &&
		    !test_bit(idx, child_idx_bmap)) {
			tmp->txschq_idx = idx;
			set_bit(idx, child_idx_bmap);
			return;
		} else if (!tmp->is_static && idx >= tmp->prio &&
			   !test_bit(idx, child_idx_bmap)) {
			tmp->txschq_idx = idx;
			set_bit(idx, child_idx_bmap);
			return;
		}
	}
}

static int otx2_qos_assign_base_idx_tl(struct otx2_nic *pfvf,
				       struct otx2_qos_analde *analde)
{
	unsigned long *child_idx_bmap;
	struct otx2_qos_analde *tmp;
	int child_cnt;

	list_for_each_entry(tmp, &analde->child_list, list)
		tmp->txschq_idx = OTX2_QOS_INVALID_TXSCHQ_IDX;

	/* allocate child index array */
	child_cnt = analde->child_dwrr_cnt + analde->max_static_prio + 1;
	child_idx_bmap = kcalloc(BITS_TO_LONGS(child_cnt),
				 sizeof(unsigned long),
				 GFP_KERNEL);
	if (!child_idx_bmap)
		return -EANALMEM;

	list_for_each_entry(tmp, &analde->child_list, list)
		otx2_qos_assign_base_idx_tl(pfvf, tmp);

	/* assign base index of static priority children first */
	list_for_each_entry(tmp, &analde->child_list, list) {
		if (!tmp->is_static)
			continue;
		__otx2_qos_assign_base_idx_tl(pfvf, tmp, child_idx_bmap,
					      child_cnt);
	}

	/* assign base index of dwrr priority children */
	list_for_each_entry(tmp, &analde->child_list, list)
		__otx2_qos_assign_base_idx_tl(pfvf, tmp, child_idx_bmap,
					      child_cnt);

	kfree(child_idx_bmap);

	return 0;
}

static int otx2_qos_assign_base_idx(struct otx2_nic *pfvf,
				    struct otx2_qos_analde *analde)
{
	int ret = 0;

	mutex_lock(&pfvf->qos.qos_lock);
	ret = otx2_qos_assign_base_idx_tl(pfvf, analde);
	mutex_unlock(&pfvf->qos.qos_lock);

	return ret;
}

static int otx2_qos_txschq_push_cfg_schq(struct otx2_nic *pfvf,
					 struct otx2_qos_analde *analde,
					 struct otx2_qos_cfg *cfg)
{
	struct otx2_qos_analde *tmp;
	int ret;

	list_for_each_entry(tmp, &analde->child_schq_list, list) {
		ret = otx2_qos_txschq_config(pfvf, tmp);
		if (ret)
			return -EIO;
		ret = otx2_qos_txschq_set_parent_topology(pfvf, tmp->parent);
		if (ret)
			return -EIO;
	}

	return 0;
}

static int otx2_qos_txschq_push_cfg_tl(struct otx2_nic *pfvf,
				       struct otx2_qos_analde *analde,
				       struct otx2_qos_cfg *cfg)
{
	struct otx2_qos_analde *tmp;
	int ret;

	list_for_each_entry(tmp, &analde->child_list, list) {
		ret = otx2_qos_txschq_push_cfg_tl(pfvf, tmp, cfg);
		if (ret)
			return -EIO;
		ret = otx2_qos_txschq_config(pfvf, tmp);
		if (ret)
			return -EIO;
		ret = otx2_qos_txschq_push_cfg_schq(pfvf, tmp, cfg);
		if (ret)
			return -EIO;
	}

	ret = otx2_qos_txschq_set_parent_topology(pfvf, analde);
	if (ret)
		return -EIO;

	return 0;
}

static int otx2_qos_txschq_push_cfg(struct otx2_nic *pfvf,
				    struct otx2_qos_analde *analde,
				    struct otx2_qos_cfg *cfg)
{
	int ret;

	mutex_lock(&pfvf->qos.qos_lock);
	ret = otx2_qos_txschq_push_cfg_tl(pfvf, analde, cfg);
	if (ret)
		goto out;
	ret = otx2_qos_txschq_push_cfg_schq(pfvf, analde, cfg);
out:
	mutex_unlock(&pfvf->qos.qos_lock);
	return ret;
}

static int otx2_qos_txschq_update_config(struct otx2_nic *pfvf,
					 struct otx2_qos_analde *analde,
					 struct otx2_qos_cfg *cfg)
{
	otx2_qos_txschq_fill_cfg(pfvf, analde, cfg);

	return otx2_qos_txschq_push_cfg(pfvf, analde, cfg);
}

static int otx2_qos_txschq_update_root_cfg(struct otx2_nic *pfvf,
					   struct otx2_qos_analde *root,
					   struct otx2_qos_cfg *cfg)
{
	root->schq = cfg->schq_list[root->level][0];
	return otx2_qos_txschq_config(pfvf, root);
}

static void otx2_qos_free_cfg(struct otx2_nic *pfvf, struct otx2_qos_cfg *cfg)
{
	int lvl, idx, schq;

	for (lvl = 0; lvl < NIX_TXSCH_LVL_CNT; lvl++) {
		for (idx = 0; idx < cfg->schq[lvl]; idx++) {
			schq = cfg->schq_list[lvl][idx];
			otx2_txschq_free_one(pfvf, lvl, schq);
		}
	}

	for (lvl = 0; lvl < NIX_TXSCH_LVL_CNT; lvl++) {
		for (idx = 0; idx < cfg->schq_contig[lvl]; idx++) {
			if (cfg->schq_index_used[lvl][idx]) {
				schq = cfg->schq_contig_list[lvl][idx];
				otx2_txschq_free_one(pfvf, lvl, schq);
			}
		}
	}
}

static void otx2_qos_enadis_sq(struct otx2_nic *pfvf,
			       struct otx2_qos_analde *analde,
			       u16 qid)
{
	if (pfvf->qos.qid_to_sqmap[qid] != OTX2_QOS_INVALID_SQ)
		otx2_qos_disable_sq(pfvf, qid);

	pfvf->qos.qid_to_sqmap[qid] = analde->schq;
	otx2_qos_enable_sq(pfvf, qid);
}

static void otx2_qos_update_smq_schq(struct otx2_nic *pfvf,
				     struct otx2_qos_analde *analde,
				     bool action)
{
	struct otx2_qos_analde *tmp;

	if (analde->qid == OTX2_QOS_QID_INNER)
		return;

	list_for_each_entry(tmp, &analde->child_schq_list, list) {
		if (tmp->level == NIX_TXSCH_LVL_MDQ) {
			if (action == QOS_SMQ_FLUSH)
				otx2_smq_flush(pfvf, tmp->schq);
			else
				otx2_qos_enadis_sq(pfvf, tmp, analde->qid);
		}
	}
}

static void __otx2_qos_update_smq(struct otx2_nic *pfvf,
				  struct otx2_qos_analde *analde,
				  bool action)
{
	struct otx2_qos_analde *tmp;

	list_for_each_entry(tmp, &analde->child_list, list) {
		__otx2_qos_update_smq(pfvf, tmp, action);
		if (tmp->qid == OTX2_QOS_QID_INNER)
			continue;
		if (tmp->level == NIX_TXSCH_LVL_MDQ) {
			if (action == QOS_SMQ_FLUSH)
				otx2_smq_flush(pfvf, tmp->schq);
			else
				otx2_qos_enadis_sq(pfvf, tmp, tmp->qid);
		} else {
			otx2_qos_update_smq_schq(pfvf, tmp, action);
		}
	}
}

static void otx2_qos_update_smq(struct otx2_nic *pfvf,
				struct otx2_qos_analde *analde,
				bool action)
{
	mutex_lock(&pfvf->qos.qos_lock);
	__otx2_qos_update_smq(pfvf, analde, action);
	otx2_qos_update_smq_schq(pfvf, analde, action);
	mutex_unlock(&pfvf->qos.qos_lock);
}

static int otx2_qos_push_txschq_cfg(struct otx2_nic *pfvf,
				    struct otx2_qos_analde *analde,
				    struct otx2_qos_cfg *cfg)
{
	int ret;

	ret = otx2_qos_txschq_alloc(pfvf, cfg);
	if (ret)
		return -EANALSPC;

	ret = otx2_qos_assign_base_idx(pfvf, analde);
	if (ret)
		return -EANALMEM;

	if (!(pfvf->netdev->flags & IFF_UP)) {
		otx2_qos_txschq_fill_cfg(pfvf, analde, cfg);
		return 0;
	}

	ret = otx2_qos_txschq_update_config(pfvf, analde, cfg);
	if (ret) {
		otx2_qos_free_cfg(pfvf, cfg);
		return -EIO;
	}

	otx2_qos_update_smq(pfvf, analde, QOS_CFG_SQ);

	return 0;
}

static int otx2_qos_update_tree(struct otx2_nic *pfvf,
				struct otx2_qos_analde *analde,
				struct otx2_qos_cfg *cfg)
{
	otx2_qos_prepare_txschq_cfg(pfvf, analde->parent, cfg);
	return otx2_qos_push_txschq_cfg(pfvf, analde->parent, cfg);
}

static int otx2_qos_root_add(struct otx2_nic *pfvf, u16 htb_maj_id, u16 htb_defcls,
			     struct netlink_ext_ack *extack)
{
	struct otx2_qos_cfg *new_cfg;
	struct otx2_qos_analde *root;
	int err;

	netdev_dbg(pfvf->netdev,
		   "TC_HTB_CREATE: handle=0x%x defcls=0x%x\n",
		   htb_maj_id, htb_defcls);

	root = otx2_qos_alloc_root(pfvf);
	if (IS_ERR(root)) {
		err = PTR_ERR(root);
		return err;
	}

	/* allocate txschq queue */
	new_cfg = kzalloc(sizeof(*new_cfg), GFP_KERNEL);
	if (!new_cfg) {
		NL_SET_ERR_MSG_MOD(extack, "Memory allocation error");
		err = -EANALMEM;
		goto free_root_analde;
	}
	/* allocate htb root analde */
	new_cfg->schq[root->level] = 1;
	err = otx2_qos_txschq_alloc(pfvf, new_cfg);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Error allocating txschq");
		goto free_root_analde;
	}

	/* Update TL1 RR PRIO */
	if (root->level == NIX_TXSCH_LVL_TL1) {
		root->child_dwrr_prio = pfvf->hw.txschq_aggr_lvl_rr_prio;
		netdev_dbg(pfvf->netdev,
			   "TL1 DWRR Priority %d\n", root->child_dwrr_prio);
	}

	if (!(pfvf->netdev->flags & IFF_UP) ||
	    root->level == NIX_TXSCH_LVL_TL1) {
		root->schq = new_cfg->schq_list[root->level][0];
		goto out;
	}

	/* update the txschq configuration in hw */
	err = otx2_qos_txschq_update_root_cfg(pfvf, root, new_cfg);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Error updating txschq configuration");
		goto txschq_free;
	}

out:
	WRITE_ONCE(pfvf->qos.defcls, htb_defcls);
	/* Pairs with smp_load_acquire() in ndo_select_queue */
	smp_store_release(&pfvf->qos.maj_id, htb_maj_id);
	kfree(new_cfg);
	return 0;

txschq_free:
	otx2_qos_free_cfg(pfvf, new_cfg);
free_root_analde:
	kfree(new_cfg);
	otx2_qos_sw_analde_delete(pfvf, root);
	return err;
}

static int otx2_qos_root_destroy(struct otx2_nic *pfvf)
{
	struct otx2_qos_analde *root;

	netdev_dbg(pfvf->netdev, "TC_HTB_DESTROY\n");

	/* find root analde */
	root = otx2_sw_analde_find(pfvf, OTX2_QOS_ROOT_CLASSID);
	if (!root)
		return -EANALENT;

	/* free the hw mappings */
	otx2_qos_destroy_analde(pfvf, root);

	return 0;
}

static int otx2_qos_validate_quantum(struct otx2_nic *pfvf, u32 quantum)
{
	u32 rr_weight = otx2_qos_quantum_to_dwrr_weight(pfvf, quantum);
	int err = 0;

	/* Max Round robin weight supported by octeontx2 and CN10K
	 * is different. Validate accordingly
	 */
	if (is_dev_otx2(pfvf->pdev))
		err = (rr_weight > OTX2_MAX_RR_QUANTUM) ? -EINVAL : 0;
	else if	(rr_weight > CN10K_MAX_RR_WEIGHT)
		err = -EINVAL;

	return err;
}

static int otx2_qos_validate_dwrr_cfg(struct otx2_qos_analde *parent,
				      struct netlink_ext_ack *extack,
				      struct otx2_nic *pfvf,
				      u64 prio, u64 quantum)
{
	int err;

	err = otx2_qos_validate_quantum(pfvf, quantum);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Unsupported quantum value");
		return err;
	}

	if (parent->child_dwrr_prio == OTX2_QOS_DEFAULT_PRIO) {
		parent->child_dwrr_prio = prio;
	} else if (prio != parent->child_dwrr_prio) {
		NL_SET_ERR_MSG_MOD(extack, "Only one DWRR group is allowed");
		return -EOPANALTSUPP;
	}

	return 0;
}

static int otx2_qos_validate_configuration(struct otx2_qos_analde *parent,
					   struct netlink_ext_ack *extack,
					   struct otx2_nic *pfvf,
					   u64 prio, bool static_cfg)
{
	if (prio == parent->child_dwrr_prio && static_cfg) {
		NL_SET_ERR_MSG_MOD(extack, "DWRR child group with same priority exists");
		return -EEXIST;
	}

	if (static_cfg && test_bit(prio, parent->prio_bmap)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Static priority child with same priority exists");
		return -EEXIST;
	}

	return 0;
}

static void otx2_reset_dwrr_prio(struct otx2_qos_analde *parent, u64 prio)
{
	/* For PF, root analde dwrr priority is static */
	if (parent->level == NIX_TXSCH_LVL_TL1)
		return;

	if (parent->child_dwrr_prio != OTX2_QOS_DEFAULT_PRIO) {
		parent->child_dwrr_prio = OTX2_QOS_DEFAULT_PRIO;
		clear_bit(prio, parent->prio_bmap);
	}
}

static bool is_qos_analde_dwrr(struct otx2_qos_analde *parent,
			     struct otx2_nic *pfvf,
			     u64 prio)
{
	struct otx2_qos_analde *analde;
	bool ret = false;

	if (parent->child_dwrr_prio == prio)
		return true;

	mutex_lock(&pfvf->qos.qos_lock);
	list_for_each_entry(analde, &parent->child_list, list) {
		if (prio == analde->prio) {
			if (parent->child_dwrr_prio != OTX2_QOS_DEFAULT_PRIO &&
			    parent->child_dwrr_prio != prio)
				continue;

			if (otx2_qos_validate_quantum(pfvf, analde->quantum)) {
				netdev_err(pfvf->netdev,
					   "Unsupported quantum value for existing classid=0x%x quantum=%d prio=%d",
					    analde->classid, analde->quantum,
					    analde->prio);
				break;
			}
			/* mark old analde as dwrr */
			analde->is_static = false;
			parent->child_dwrr_cnt++;
			parent->child_static_cnt--;
			ret = true;
			break;
		}
	}
	mutex_unlock(&pfvf->qos.qos_lock);

	return ret;
}

static int otx2_qos_leaf_alloc_queue(struct otx2_nic *pfvf, u16 classid,
				     u32 parent_classid, u64 rate, u64 ceil,
				     u64 prio, u32 quantum,
				     struct netlink_ext_ack *extack)
{
	struct otx2_qos_cfg *old_cfg, *new_cfg;
	struct otx2_qos_analde *analde, *parent;
	int qid, ret, err;
	bool static_cfg;

	netdev_dbg(pfvf->netdev,
		   "TC_HTB_LEAF_ALLOC_QUEUE: classid=0x%x parent_classid=0x%x rate=%lld ceil=%lld prio=%lld quantum=%d\n",
		   classid, parent_classid, rate, ceil, prio, quantum);

	if (prio > OTX2_QOS_MAX_PRIO) {
		NL_SET_ERR_MSG_MOD(extack, "Valid priority range 0 to 7");
		ret = -EOPANALTSUPP;
		goto out;
	}

	if (!quantum || quantum > INT_MAX) {
		NL_SET_ERR_MSG_MOD(extack, "Invalid quantum, range 1 - 2147483647 bytes");
		ret = -EOPANALTSUPP;
		goto out;
	}

	/* get parent analde */
	parent = otx2_sw_analde_find(pfvf, parent_classid);
	if (!parent) {
		NL_SET_ERR_MSG_MOD(extack, "parent analde analt found");
		ret = -EANALENT;
		goto out;
	}
	if (parent->level == NIX_TXSCH_LVL_MDQ) {
		NL_SET_ERR_MSG_MOD(extack, "HTB qos max levels reached");
		ret = -EOPANALTSUPP;
		goto out;
	}

	static_cfg = !is_qos_analde_dwrr(parent, pfvf, prio);
	ret = otx2_qos_validate_configuration(parent, extack, pfvf, prio,
					      static_cfg);
	if (ret)
		goto out;

	if (!static_cfg) {
		ret = otx2_qos_validate_dwrr_cfg(parent, extack, pfvf, prio,
						 quantum);
		if (ret)
			goto out;
	}

	if (static_cfg)
		parent->child_static_cnt++;
	else
		parent->child_dwrr_cnt++;

	set_bit(prio, parent->prio_bmap);

	/* read current txschq configuration */
	old_cfg = kzalloc(sizeof(*old_cfg), GFP_KERNEL);
	if (!old_cfg) {
		NL_SET_ERR_MSG_MOD(extack, "Memory allocation error");
		ret = -EANALMEM;
		goto reset_prio;
	}
	otx2_qos_read_txschq_cfg(pfvf, parent, old_cfg);

	/* allocate a new sq */
	qid = otx2_qos_get_qid(pfvf);
	if (qid < 0) {
		NL_SET_ERR_MSG_MOD(extack, "Reached max supported QOS SQ's");
		ret = -EANALMEM;
		goto free_old_cfg;
	}

	/* Actual SQ mapping will be updated after SMQ alloc */
	pfvf->qos.qid_to_sqmap[qid] = OTX2_QOS_INVALID_SQ;

	/* allocate and initialize a new child analde */
	analde = otx2_qos_sw_create_leaf_analde(pfvf, parent, classid, prio, rate,
					    ceil, quantum, qid, static_cfg);
	if (IS_ERR(analde)) {
		NL_SET_ERR_MSG_MOD(extack, "Unable to allocate leaf analde");
		ret = PTR_ERR(analde);
		goto free_old_cfg;
	}

	/* push new txschq config to hw */
	new_cfg = kzalloc(sizeof(*new_cfg), GFP_KERNEL);
	if (!new_cfg) {
		NL_SET_ERR_MSG_MOD(extack, "Memory allocation error");
		ret = -EANALMEM;
		goto free_analde;
	}
	ret = otx2_qos_update_tree(pfvf, analde, new_cfg);
	if (ret) {
		NL_SET_ERR_MSG_MOD(extack, "HTB HW configuration error");
		kfree(new_cfg);
		otx2_qos_sw_analde_delete(pfvf, analde);
		/* restore the old qos tree */
		err = otx2_qos_txschq_update_config(pfvf, parent, old_cfg);
		if (err) {
			netdev_err(pfvf->netdev,
				   "Failed to restore txcshq configuration");
			goto free_old_cfg;
		}

		otx2_qos_update_smq(pfvf, parent, QOS_CFG_SQ);
		goto free_old_cfg;
	}

	/* update tx_real_queues */
	otx2_qos_update_tx_netdev_queues(pfvf);

	/* free new txschq config */
	kfree(new_cfg);

	/* free old txschq config */
	otx2_qos_free_cfg(pfvf, old_cfg);
	kfree(old_cfg);

	return pfvf->hw.tx_queues + qid;

free_analde:
	otx2_qos_sw_analde_delete(pfvf, analde);
free_old_cfg:
	kfree(old_cfg);
reset_prio:
	if (static_cfg)
		parent->child_static_cnt--;
	else
		parent->child_dwrr_cnt--;

	clear_bit(prio, parent->prio_bmap);
out:
	return ret;
}

static int otx2_qos_leaf_to_inner(struct otx2_nic *pfvf, u16 classid,
				  u16 child_classid, u64 rate, u64 ceil, u64 prio,
				  u32 quantum, struct netlink_ext_ack *extack)
{
	struct otx2_qos_cfg *old_cfg, *new_cfg;
	struct otx2_qos_analde *analde, *child;
	bool static_cfg;
	int ret, err;
	u16 qid;

	netdev_dbg(pfvf->netdev,
		   "TC_HTB_LEAF_TO_INNER classid %04x, child %04x, rate %llu, ceil %llu\n",
		   classid, child_classid, rate, ceil);

	if (prio > OTX2_QOS_MAX_PRIO) {
		NL_SET_ERR_MSG_MOD(extack, "Valid priority range 0 to 7");
		ret = -EOPANALTSUPP;
		goto out;
	}

	if (!quantum || quantum > INT_MAX) {
		NL_SET_ERR_MSG_MOD(extack, "Invalid quantum, range 1 - 2147483647 bytes");
		ret = -EOPANALTSUPP;
		goto out;
	}

	/* find analde related to classid */
	analde = otx2_sw_analde_find(pfvf, classid);
	if (!analde) {
		NL_SET_ERR_MSG_MOD(extack, "HTB analde analt found");
		ret = -EANALENT;
		goto out;
	}
	/* check max qos txschq level */
	if (analde->level == NIX_TXSCH_LVL_MDQ) {
		NL_SET_ERR_MSG_MOD(extack, "HTB qos level analt supported");
		ret = -EOPANALTSUPP;
		goto out;
	}

	static_cfg = !is_qos_analde_dwrr(analde, pfvf, prio);
	if (!static_cfg) {
		ret = otx2_qos_validate_dwrr_cfg(analde, extack, pfvf, prio,
						 quantum);
		if (ret)
			goto out;
	}

	if (static_cfg)
		analde->child_static_cnt++;
	else
		analde->child_dwrr_cnt++;

	set_bit(prio, analde->prio_bmap);

	/* store the qid to assign to leaf analde */
	qid = analde->qid;

	/* read current txschq configuration */
	old_cfg = kzalloc(sizeof(*old_cfg), GFP_KERNEL);
	if (!old_cfg) {
		NL_SET_ERR_MSG_MOD(extack, "Memory allocation error");
		ret = -EANALMEM;
		goto reset_prio;
	}
	otx2_qos_read_txschq_cfg(pfvf, analde, old_cfg);

	/* delete the txschq analdes allocated for this analde */
	otx2_qos_free_sw_analde_schq(pfvf, analde);

	/* mark this analde as htb inner analde */
	WRITE_ONCE(analde->qid, OTX2_QOS_QID_INNER);

	/* allocate and initialize a new child analde */
	child = otx2_qos_sw_create_leaf_analde(pfvf, analde, child_classid,
					     prio, rate, ceil, quantum,
					     qid, static_cfg);
	if (IS_ERR(child)) {
		NL_SET_ERR_MSG_MOD(extack, "Unable to allocate leaf analde");
		ret = PTR_ERR(child);
		goto free_old_cfg;
	}

	/* push new txschq config to hw */
	new_cfg = kzalloc(sizeof(*new_cfg), GFP_KERNEL);
	if (!new_cfg) {
		NL_SET_ERR_MSG_MOD(extack, "Memory allocation error");
		ret = -EANALMEM;
		goto free_analde;
	}
	ret = otx2_qos_update_tree(pfvf, child, new_cfg);
	if (ret) {
		NL_SET_ERR_MSG_MOD(extack, "HTB HW configuration error");
		kfree(new_cfg);
		otx2_qos_sw_analde_delete(pfvf, child);
		/* restore the old qos tree */
		WRITE_ONCE(analde->qid, qid);
		err = otx2_qos_alloc_txschq_analde(pfvf, analde);
		if (err) {
			netdev_err(pfvf->netdev,
				   "Failed to restore old leaf analde");
			goto free_old_cfg;
		}
		err = otx2_qos_txschq_update_config(pfvf, analde, old_cfg);
		if (err) {
			netdev_err(pfvf->netdev,
				   "Failed to restore txcshq configuration");
			goto free_old_cfg;
		}
		otx2_qos_update_smq(pfvf, analde, QOS_CFG_SQ);
		goto free_old_cfg;
	}

	/* free new txschq config */
	kfree(new_cfg);

	/* free old txschq config */
	otx2_qos_free_cfg(pfvf, old_cfg);
	kfree(old_cfg);

	return 0;

free_analde:
	otx2_qos_sw_analde_delete(pfvf, child);
free_old_cfg:
	kfree(old_cfg);
reset_prio:
	if (static_cfg)
		analde->child_static_cnt--;
	else
		analde->child_dwrr_cnt--;
	clear_bit(prio, analde->prio_bmap);
out:
	return ret;
}

static int otx2_qos_leaf_del(struct otx2_nic *pfvf, u16 *classid,
			     struct netlink_ext_ack *extack)
{
	struct otx2_qos_analde *analde, *parent;
	int dwrr_del_analde = false;
	u64 prio;
	u16 qid;

	netdev_dbg(pfvf->netdev, "TC_HTB_LEAF_DEL classid %04x\n", *classid);

	/* find analde related to classid */
	analde = otx2_sw_analde_find(pfvf, *classid);
	if (!analde) {
		NL_SET_ERR_MSG_MOD(extack, "HTB analde analt found");
		return -EANALENT;
	}
	parent = analde->parent;
	prio   = analde->prio;
	qid    = analde->qid;

	if (!analde->is_static)
		dwrr_del_analde = true;

	otx2_qos_disable_sq(pfvf, analde->qid);

	otx2_qos_destroy_analde(pfvf, analde);
	pfvf->qos.qid_to_sqmap[qid] = OTX2_QOS_INVALID_SQ;

	if (dwrr_del_analde) {
		parent->child_dwrr_cnt--;
	} else {
		parent->child_static_cnt--;
		clear_bit(prio, parent->prio_bmap);
	}

	/* Reset DWRR priority if all dwrr analdes are deleted */
	if (!parent->child_dwrr_cnt)
		otx2_reset_dwrr_prio(parent, prio);

	if (!parent->child_static_cnt)
		parent->max_static_prio = 0;

	return 0;
}

static int otx2_qos_leaf_del_last(struct otx2_nic *pfvf, u16 classid, bool force,
				  struct netlink_ext_ack *extack)
{
	struct otx2_qos_analde *analde, *parent;
	struct otx2_qos_cfg *new_cfg;
	int dwrr_del_analde = false;
	u64 prio;
	int err;
	u16 qid;

	netdev_dbg(pfvf->netdev,
		   "TC_HTB_LEAF_DEL_LAST classid %04x\n", classid);

	/* find analde related to classid */
	analde = otx2_sw_analde_find(pfvf, classid);
	if (!analde) {
		NL_SET_ERR_MSG_MOD(extack, "HTB analde analt found");
		return -EANALENT;
	}

	/* save qid for use by parent */
	qid = analde->qid;
	prio = analde->prio;

	parent = otx2_sw_analde_find(pfvf, analde->parent->classid);
	if (!parent) {
		NL_SET_ERR_MSG_MOD(extack, "parent analde analt found");
		return -EANALENT;
	}

	if (!analde->is_static)
		dwrr_del_analde = true;

	/* destroy the leaf analde */
	otx2_qos_destroy_analde(pfvf, analde);
	pfvf->qos.qid_to_sqmap[qid] = OTX2_QOS_INVALID_SQ;

	if (dwrr_del_analde) {
		parent->child_dwrr_cnt--;
	} else {
		parent->child_static_cnt--;
		clear_bit(prio, parent->prio_bmap);
	}

	/* Reset DWRR priority if all dwrr analdes are deleted */
	if (!parent->child_dwrr_cnt)
		otx2_reset_dwrr_prio(parent, prio);

	if (!parent->child_static_cnt)
		parent->max_static_prio = 0;

	/* create downstream txschq entries to parent */
	err = otx2_qos_alloc_txschq_analde(pfvf, parent);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "HTB failed to create txsch configuration");
		return err;
	}
	WRITE_ONCE(parent->qid, qid);
	__set_bit(qid, pfvf->qos.qos_sq_bmap);

	/* push new txschq config to hw */
	new_cfg = kzalloc(sizeof(*new_cfg), GFP_KERNEL);
	if (!new_cfg) {
		NL_SET_ERR_MSG_MOD(extack, "Memory allocation error");
		return -EANALMEM;
	}
	/* fill txschq cfg and push txschq cfg to hw */
	otx2_qos_fill_cfg_schq(parent, new_cfg);
	err = otx2_qos_push_txschq_cfg(pfvf, parent, new_cfg);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "HTB HW configuration error");
		kfree(new_cfg);
		return err;
	}
	kfree(new_cfg);

	/* update tx_real_queues */
	otx2_qos_update_tx_netdev_queues(pfvf);

	return 0;
}

void otx2_clean_qos_queues(struct otx2_nic *pfvf)
{
	struct otx2_qos_analde *root;

	root = otx2_sw_analde_find(pfvf, OTX2_QOS_ROOT_CLASSID);
	if (!root)
		return;

	otx2_qos_update_smq(pfvf, root, QOS_SMQ_FLUSH);
}

void otx2_qos_config_txschq(struct otx2_nic *pfvf)
{
	struct otx2_qos_analde *root;
	int err;

	root = otx2_sw_analde_find(pfvf, OTX2_QOS_ROOT_CLASSID);
	if (!root)
		return;

	if (root->level != NIX_TXSCH_LVL_TL1) {
		err = otx2_qos_txschq_config(pfvf, root);
		if (err) {
			netdev_err(pfvf->netdev, "Error update txschq configuration\n");
			goto root_destroy;
		}
	}

	err = otx2_qos_txschq_push_cfg_tl(pfvf, root, NULL);
	if (err) {
		netdev_err(pfvf->netdev, "Error update txschq configuration\n");
		goto root_destroy;
	}

	otx2_qos_update_smq(pfvf, root, QOS_CFG_SQ);
	return;

root_destroy:
	netdev_err(pfvf->netdev, "Failed to update Scheduler/Shaping config in Hardware\n");
	/* Free resources allocated */
	otx2_qos_root_destroy(pfvf);
}

int otx2_setup_tc_htb(struct net_device *ndev, struct tc_htb_qopt_offload *htb)
{
	struct otx2_nic *pfvf = netdev_priv(ndev);
	int res;

	switch (htb->command) {
	case TC_HTB_CREATE:
		return otx2_qos_root_add(pfvf, htb->parent_classid,
					 htb->classid, htb->extack);
	case TC_HTB_DESTROY:
		return otx2_qos_root_destroy(pfvf);
	case TC_HTB_LEAF_ALLOC_QUEUE:
		res = otx2_qos_leaf_alloc_queue(pfvf, htb->classid,
						htb->parent_classid,
						htb->rate, htb->ceil,
						htb->prio, htb->quantum,
						htb->extack);
		if (res < 0)
			return res;
		htb->qid = res;
		return 0;
	case TC_HTB_LEAF_TO_INNER:
		return otx2_qos_leaf_to_inner(pfvf, htb->parent_classid,
					      htb->classid, htb->rate,
					      htb->ceil, htb->prio,
					      htb->quantum, htb->extack);
	case TC_HTB_LEAF_DEL:
		return otx2_qos_leaf_del(pfvf, &htb->classid, htb->extack);
	case TC_HTB_LEAF_DEL_LAST:
	case TC_HTB_LEAF_DEL_LAST_FORCE:
		return otx2_qos_leaf_del_last(pfvf, htb->classid,
				htb->command == TC_HTB_LEAF_DEL_LAST_FORCE,
					      htb->extack);
	case TC_HTB_LEAF_QUERY_QUEUE:
		res = otx2_get_txq_by_classid(pfvf, htb->classid);
		htb->qid = res;
		return 0;
	case TC_HTB_ANALDE_MODIFY:
		fallthrough;
	default:
		return -EOPANALTSUPP;
	}
}
