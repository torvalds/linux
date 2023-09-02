// SPDX-License-Identifier: GPL-2.0
/* Marvell RVU Admin Function driver
 *
 * Copyright (C) 2018 Marvell.
 *
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "rvu.h"
#include "cgx.h"
#include "lmac_common.h"
#include "rvu_reg.h"
#include "rvu_trace.h"
#include "rvu_npc_hash.h"

struct cgx_evq_entry {
	struct list_head evq_node;
	struct cgx_link_event link_event;
};

#define M(_name, _id, _fn_name, _req_type, _rsp_type)			\
static struct _req_type __maybe_unused					\
*otx2_mbox_alloc_msg_ ## _fn_name(struct rvu *rvu, int devid)		\
{									\
	struct _req_type *req;						\
									\
	req = (struct _req_type *)otx2_mbox_alloc_msg_rsp(		\
		&rvu->afpf_wq_info.mbox_up, devid, sizeof(struct _req_type), \
		sizeof(struct _rsp_type));				\
	if (!req)							\
		return NULL;						\
	req->hdr.sig = OTX2_MBOX_REQ_SIG;				\
	req->hdr.id = _id;						\
	trace_otx2_msg_alloc(rvu->pdev, _id, sizeof(*req));		\
	return req;							\
}

MBOX_UP_CGX_MESSAGES
#undef M

bool is_mac_feature_supported(struct rvu *rvu, int pf, int feature)
{
	u8 cgx_id, lmac_id;
	void *cgxd;

	if (!is_pf_cgxmapped(rvu, pf))
		return 0;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);
	cgxd = rvu_cgx_pdata(cgx_id, rvu);

	return  (cgx_features_get(cgxd) & feature);
}

/* Returns bitmap of mapped PFs */
static u16 cgxlmac_to_pfmap(struct rvu *rvu, u8 cgx_id, u8 lmac_id)
{
	return rvu->cgxlmac2pf_map[CGX_OFFSET(cgx_id) + lmac_id];
}

int cgxlmac_to_pf(struct rvu *rvu, int cgx_id, int lmac_id)
{
	unsigned long pfmap;

	pfmap = cgxlmac_to_pfmap(rvu, cgx_id, lmac_id);

	/* Assumes only one pf mapped to a cgx lmac port */
	if (!pfmap)
		return -ENODEV;
	else
		return find_first_bit(&pfmap, 16);
}

static u8 cgxlmac_id_to_bmap(u8 cgx_id, u8 lmac_id)
{
	return ((cgx_id & 0xF) << 4) | (lmac_id & 0xF);
}

void *rvu_cgx_pdata(u8 cgx_id, struct rvu *rvu)
{
	if (cgx_id >= rvu->cgx_cnt_max)
		return NULL;

	return rvu->cgx_idmap[cgx_id];
}

/* Return first enabled CGX instance if none are enabled then return NULL */
void *rvu_first_cgx_pdata(struct rvu *rvu)
{
	int first_enabled_cgx = 0;
	void *cgxd = NULL;

	for (; first_enabled_cgx < rvu->cgx_cnt_max; first_enabled_cgx++) {
		cgxd = rvu_cgx_pdata(first_enabled_cgx, rvu);
		if (cgxd)
			break;
	}

	return cgxd;
}

/* Based on P2X connectivity find mapped NIX block for a PF */
static void rvu_map_cgx_nix_block(struct rvu *rvu, int pf,
				  int cgx_id, int lmac_id)
{
	struct rvu_pfvf *pfvf = &rvu->pf[pf];
	u8 p2x;

	p2x = cgx_lmac_get_p2x(cgx_id, lmac_id);
	/* Firmware sets P2X_SELECT as either NIX0 or NIX1 */
	pfvf->nix_blkaddr = BLKADDR_NIX0;
	if (is_rvu_supports_nix1(rvu) && p2x == CMR_P2X_SEL_NIX1)
		pfvf->nix_blkaddr = BLKADDR_NIX1;
}

static int rvu_map_cgx_lmac_pf(struct rvu *rvu)
{
	struct npc_pkind *pkind = &rvu->hw->pkind;
	int cgx_cnt_max = rvu->cgx_cnt_max;
	int pf = PF_CGXMAP_BASE;
	unsigned long lmac_bmap;
	int size, free_pkind;
	int cgx, lmac, iter;
	int numvfs, hwvfs;

	if (!cgx_cnt_max)
		return 0;

	if (cgx_cnt_max > 0xF || MAX_LMAC_PER_CGX > 0xF)
		return -EINVAL;

	/* Alloc map table
	 * An additional entry is required since PF id starts from 1 and
	 * hence entry at offset 0 is invalid.
	 */
	size = (cgx_cnt_max * MAX_LMAC_PER_CGX + 1) * sizeof(u8);
	rvu->pf2cgxlmac_map = devm_kmalloc(rvu->dev, size, GFP_KERNEL);
	if (!rvu->pf2cgxlmac_map)
		return -ENOMEM;

	/* Initialize all entries with an invalid cgx and lmac id */
	memset(rvu->pf2cgxlmac_map, 0xFF, size);

	/* Reverse map table */
	rvu->cgxlmac2pf_map = devm_kzalloc(rvu->dev,
				  cgx_cnt_max * MAX_LMAC_PER_CGX * sizeof(u16),
				  GFP_KERNEL);
	if (!rvu->cgxlmac2pf_map)
		return -ENOMEM;

	rvu->cgx_mapped_pfs = 0;
	for (cgx = 0; cgx < cgx_cnt_max; cgx++) {
		if (!rvu_cgx_pdata(cgx, rvu))
			continue;
		lmac_bmap = cgx_get_lmac_bmap(rvu_cgx_pdata(cgx, rvu));
		for_each_set_bit(iter, &lmac_bmap, MAX_LMAC_PER_CGX) {
			lmac = cgx_get_lmacid(rvu_cgx_pdata(cgx, rvu),
					      iter);
			rvu->pf2cgxlmac_map[pf] = cgxlmac_id_to_bmap(cgx, lmac);
			rvu->cgxlmac2pf_map[CGX_OFFSET(cgx) + lmac] = 1 << pf;
			free_pkind = rvu_alloc_rsrc(&pkind->rsrc);
			pkind->pfchan_map[free_pkind] = ((pf) & 0x3F) << 16;
			rvu_map_cgx_nix_block(rvu, pf, cgx, lmac);
			rvu->cgx_mapped_pfs++;
			rvu_get_pf_numvfs(rvu, pf, &numvfs, &hwvfs);
			rvu->cgx_mapped_vfs += numvfs;
			pf++;
		}
	}
	return 0;
}

static int rvu_cgx_send_link_info(int cgx_id, int lmac_id, struct rvu *rvu)
{
	struct cgx_evq_entry *qentry;
	unsigned long flags;
	int err;

	qentry = kmalloc(sizeof(*qentry), GFP_KERNEL);
	if (!qentry)
		return -ENOMEM;

	/* Lock the event queue before we read the local link status */
	spin_lock_irqsave(&rvu->cgx_evq_lock, flags);
	err = cgx_get_link_info(rvu_cgx_pdata(cgx_id, rvu), lmac_id,
				&qentry->link_event.link_uinfo);
	qentry->link_event.cgx_id = cgx_id;
	qentry->link_event.lmac_id = lmac_id;
	if (err) {
		kfree(qentry);
		goto skip_add;
	}
	list_add_tail(&qentry->evq_node, &rvu->cgx_evq_head);
skip_add:
	spin_unlock_irqrestore(&rvu->cgx_evq_lock, flags);

	/* start worker to process the events */
	queue_work(rvu->cgx_evh_wq, &rvu->cgx_evh_work);

	return 0;
}

/* This is called from interrupt context and is expected to be atomic */
static int cgx_lmac_postevent(struct cgx_link_event *event, void *data)
{
	struct cgx_evq_entry *qentry;
	struct rvu *rvu = data;

	/* post event to the event queue */
	qentry = kmalloc(sizeof(*qentry), GFP_ATOMIC);
	if (!qentry)
		return -ENOMEM;
	qentry->link_event = *event;
	spin_lock(&rvu->cgx_evq_lock);
	list_add_tail(&qentry->evq_node, &rvu->cgx_evq_head);
	spin_unlock(&rvu->cgx_evq_lock);

	/* start worker to process the events */
	queue_work(rvu->cgx_evh_wq, &rvu->cgx_evh_work);

	return 0;
}

static void cgx_notify_pfs(struct cgx_link_event *event, struct rvu *rvu)
{
	struct cgx_link_user_info *linfo;
	struct cgx_link_info_msg *msg;
	unsigned long pfmap;
	int err, pfid;

	linfo = &event->link_uinfo;
	pfmap = cgxlmac_to_pfmap(rvu, event->cgx_id, event->lmac_id);

	do {
		pfid = find_first_bit(&pfmap, 16);
		clear_bit(pfid, &pfmap);

		/* check if notification is enabled */
		if (!test_bit(pfid, &rvu->pf_notify_bmap)) {
			dev_info(rvu->dev, "cgx %d: lmac %d Link status %s\n",
				 event->cgx_id, event->lmac_id,
				 linfo->link_up ? "UP" : "DOWN");
			continue;
		}

		/* Send mbox message to PF */
		msg = otx2_mbox_alloc_msg_cgx_link_event(rvu, pfid);
		if (!msg)
			continue;
		msg->link_info = *linfo;
		otx2_mbox_msg_send(&rvu->afpf_wq_info.mbox_up, pfid);
		err = otx2_mbox_wait_for_rsp(&rvu->afpf_wq_info.mbox_up, pfid);
		if (err)
			dev_warn(rvu->dev, "notification to pf %d failed\n",
				 pfid);
	} while (pfmap);
}

static void cgx_evhandler_task(struct work_struct *work)
{
	struct rvu *rvu = container_of(work, struct rvu, cgx_evh_work);
	struct cgx_evq_entry *qentry;
	struct cgx_link_event *event;
	unsigned long flags;

	do {
		/* Dequeue an event */
		spin_lock_irqsave(&rvu->cgx_evq_lock, flags);
		qentry = list_first_entry_or_null(&rvu->cgx_evq_head,
						  struct cgx_evq_entry,
						  evq_node);
		if (qentry)
			list_del(&qentry->evq_node);
		spin_unlock_irqrestore(&rvu->cgx_evq_lock, flags);
		if (!qentry)
			break; /* nothing more to process */

		event = &qentry->link_event;

		/* process event */
		cgx_notify_pfs(event, rvu);
		kfree(qentry);
	} while (1);
}

static int cgx_lmac_event_handler_init(struct rvu *rvu)
{
	unsigned long lmac_bmap;
	struct cgx_event_cb cb;
	int cgx, lmac, err;
	void *cgxd;

	spin_lock_init(&rvu->cgx_evq_lock);
	INIT_LIST_HEAD(&rvu->cgx_evq_head);
	INIT_WORK(&rvu->cgx_evh_work, cgx_evhandler_task);
	rvu->cgx_evh_wq = alloc_workqueue("rvu_evh_wq", 0, 0);
	if (!rvu->cgx_evh_wq) {
		dev_err(rvu->dev, "alloc workqueue failed");
		return -ENOMEM;
	}

	cb.notify_link_chg = cgx_lmac_postevent; /* link change call back */
	cb.data = rvu;

	for (cgx = 0; cgx <= rvu->cgx_cnt_max; cgx++) {
		cgxd = rvu_cgx_pdata(cgx, rvu);
		if (!cgxd)
			continue;
		lmac_bmap = cgx_get_lmac_bmap(cgxd);
		for_each_set_bit(lmac, &lmac_bmap, MAX_LMAC_PER_CGX) {
			err = cgx_lmac_evh_register(&cb, cgxd, lmac);
			if (err)
				dev_err(rvu->dev,
					"%d:%d handler register failed\n",
					cgx, lmac);
		}
	}

	return 0;
}

static void rvu_cgx_wq_destroy(struct rvu *rvu)
{
	if (rvu->cgx_evh_wq) {
		destroy_workqueue(rvu->cgx_evh_wq);
		rvu->cgx_evh_wq = NULL;
	}
}

int rvu_cgx_init(struct rvu *rvu)
{
	int cgx, err;
	void *cgxd;

	/* CGX port id starts from 0 and are not necessarily contiguous
	 * Hence we allocate resources based on the maximum port id value.
	 */
	rvu->cgx_cnt_max = cgx_get_cgxcnt_max();
	if (!rvu->cgx_cnt_max) {
		dev_info(rvu->dev, "No CGX devices found!\n");
		return -ENODEV;
	}

	rvu->cgx_idmap = devm_kzalloc(rvu->dev, rvu->cgx_cnt_max *
				      sizeof(void *), GFP_KERNEL);
	if (!rvu->cgx_idmap)
		return -ENOMEM;

	/* Initialize the cgxdata table */
	for (cgx = 0; cgx < rvu->cgx_cnt_max; cgx++)
		rvu->cgx_idmap[cgx] = cgx_get_pdata(cgx);

	/* Map CGX LMAC interfaces to RVU PFs */
	err = rvu_map_cgx_lmac_pf(rvu);
	if (err)
		return err;

	/* Register for CGX events */
	err = cgx_lmac_event_handler_init(rvu);
	if (err)
		return err;

	mutex_init(&rvu->cgx_cfg_lock);

	/* Ensure event handler registration is completed, before
	 * we turn on the links
	 */
	mb();

	/* Do link up for all CGX ports */
	for (cgx = 0; cgx <= rvu->cgx_cnt_max; cgx++) {
		cgxd = rvu_cgx_pdata(cgx, rvu);
		if (!cgxd)
			continue;
		err = cgx_lmac_linkup_start(cgxd);
		if (err)
			dev_err(rvu->dev,
				"Link up process failed to start on cgx %d\n",
				cgx);
	}

	return 0;
}

int rvu_cgx_exit(struct rvu *rvu)
{
	unsigned long lmac_bmap;
	int cgx, lmac;
	void *cgxd;

	for (cgx = 0; cgx <= rvu->cgx_cnt_max; cgx++) {
		cgxd = rvu_cgx_pdata(cgx, rvu);
		if (!cgxd)
			continue;
		lmac_bmap = cgx_get_lmac_bmap(cgxd);
		for_each_set_bit(lmac, &lmac_bmap, MAX_LMAC_PER_CGX)
			cgx_lmac_evh_unregister(cgxd, lmac);
	}

	/* Ensure event handler unregister is completed */
	mb();

	rvu_cgx_wq_destroy(rvu);
	return 0;
}

/* Most of the CGX configuration is restricted to the mapped PF only,
 * VF's of mapped PF and other PFs are not allowed. This fn() checks
 * whether a PFFUNC is permitted to do the config or not.
 */
inline bool is_cgx_config_permitted(struct rvu *rvu, u16 pcifunc)
{
	if ((pcifunc & RVU_PFVF_FUNC_MASK) ||
	    !is_pf_cgxmapped(rvu, rvu_get_pf(pcifunc)))
		return false;
	return true;
}

void rvu_cgx_enadis_rx_bp(struct rvu *rvu, int pf, bool enable)
{
	struct mac_ops *mac_ops;
	u8 cgx_id, lmac_id;
	void *cgxd;

	if (!is_pf_cgxmapped(rvu, pf))
		return;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);
	cgxd = rvu_cgx_pdata(cgx_id, rvu);

	mac_ops = get_mac_ops(cgxd);
	/* Set / clear CTL_BCK to control pause frame forwarding to NIX */
	if (enable)
		mac_ops->mac_enadis_rx_pause_fwding(cgxd, lmac_id, true);
	else
		mac_ops->mac_enadis_rx_pause_fwding(cgxd, lmac_id, false);
}

int rvu_cgx_config_rxtx(struct rvu *rvu, u16 pcifunc, bool start)
{
	int pf = rvu_get_pf(pcifunc);
	struct mac_ops *mac_ops;
	u8 cgx_id, lmac_id;
	void *cgxd;

	if (!is_cgx_config_permitted(rvu, pcifunc))
		return LMAC_AF_ERR_PERM_DENIED;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);
	cgxd = rvu_cgx_pdata(cgx_id, rvu);
	mac_ops = get_mac_ops(cgxd);

	return mac_ops->mac_rx_tx_enable(cgxd, lmac_id, start);
}

int rvu_cgx_config_tx(void *cgxd, int lmac_id, bool enable)
{
	struct mac_ops *mac_ops;

	mac_ops = get_mac_ops(cgxd);
	return mac_ops->mac_tx_enable(cgxd, lmac_id, enable);
}

void rvu_cgx_disable_dmac_entries(struct rvu *rvu, u16 pcifunc)
{
	int pf = rvu_get_pf(pcifunc);
	int i = 0, lmac_count = 0;
	u8 max_dmac_filters;
	u8 cgx_id, lmac_id;
	void *cgx_dev;

	if (!is_cgx_config_permitted(rvu, pcifunc))
		return;

	if (rvu_npc_exact_has_match_table(rvu)) {
		rvu_npc_exact_reset(rvu, pcifunc);
		return;
	}

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);
	cgx_dev = cgx_get_pdata(cgx_id);
	lmac_count = cgx_get_lmac_cnt(cgx_dev);
	max_dmac_filters = MAX_DMAC_ENTRIES_PER_CGX / lmac_count;

	for (i = 0; i < max_dmac_filters; i++)
		cgx_lmac_addr_del(cgx_id, lmac_id, i);

	/* As cgx_lmac_addr_del does not clear entry for index 0
	 * so it needs to be done explicitly
	 */
	cgx_lmac_addr_reset(cgx_id, lmac_id);
}

int rvu_mbox_handler_cgx_start_rxtx(struct rvu *rvu, struct msg_req *req,
				    struct msg_rsp *rsp)
{
	rvu_cgx_config_rxtx(rvu, req->hdr.pcifunc, true);
	return 0;
}

int rvu_mbox_handler_cgx_stop_rxtx(struct rvu *rvu, struct msg_req *req,
				   struct msg_rsp *rsp)
{
	rvu_cgx_config_rxtx(rvu, req->hdr.pcifunc, false);
	return 0;
}

static int rvu_lmac_get_stats(struct rvu *rvu, struct msg_req *req,
			      void *rsp)
{
	int pf = rvu_get_pf(req->hdr.pcifunc);
	struct mac_ops *mac_ops;
	int stat = 0, err = 0;
	u64 tx_stat, rx_stat;
	u8 cgx_idx, lmac;
	void *cgxd;

	if (!is_cgx_config_permitted(rvu, req->hdr.pcifunc))
		return LMAC_AF_ERR_PERM_DENIED;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_idx, &lmac);
	cgxd = rvu_cgx_pdata(cgx_idx, rvu);
	mac_ops = get_mac_ops(cgxd);

	/* Rx stats */
	while (stat < mac_ops->rx_stats_cnt) {
		err = mac_ops->mac_get_rx_stats(cgxd, lmac, stat, &rx_stat);
		if (err)
			return err;
		if (mac_ops->rx_stats_cnt == RPM_RX_STATS_COUNT)
			((struct rpm_stats_rsp *)rsp)->rx_stats[stat] = rx_stat;
		else
			((struct cgx_stats_rsp *)rsp)->rx_stats[stat] = rx_stat;
		stat++;
	}

	/* Tx stats */
	stat = 0;
	while (stat < mac_ops->tx_stats_cnt) {
		err = mac_ops->mac_get_tx_stats(cgxd, lmac, stat, &tx_stat);
		if (err)
			return err;
		if (mac_ops->tx_stats_cnt == RPM_TX_STATS_COUNT)
			((struct rpm_stats_rsp *)rsp)->tx_stats[stat] = tx_stat;
		else
			((struct cgx_stats_rsp *)rsp)->tx_stats[stat] = tx_stat;
		stat++;
	}
	return 0;
}

int rvu_mbox_handler_cgx_stats(struct rvu *rvu, struct msg_req *req,
			       struct cgx_stats_rsp *rsp)
{
	return rvu_lmac_get_stats(rvu, req, (void *)rsp);
}

int rvu_mbox_handler_rpm_stats(struct rvu *rvu, struct msg_req *req,
			       struct rpm_stats_rsp *rsp)
{
	return rvu_lmac_get_stats(rvu, req, (void *)rsp);
}

int rvu_mbox_handler_cgx_fec_stats(struct rvu *rvu,
				   struct msg_req *req,
				   struct cgx_fec_stats_rsp *rsp)
{
	int pf = rvu_get_pf(req->hdr.pcifunc);
	u8 cgx_idx, lmac;
	void *cgxd;

	if (!is_cgx_config_permitted(rvu, req->hdr.pcifunc))
		return LMAC_AF_ERR_PERM_DENIED;
	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_idx, &lmac);

	cgxd = rvu_cgx_pdata(cgx_idx, rvu);
	return cgx_get_fec_stats(cgxd, lmac, rsp);
}

int rvu_mbox_handler_cgx_mac_addr_set(struct rvu *rvu,
				      struct cgx_mac_addr_set_or_get *req,
				      struct cgx_mac_addr_set_or_get *rsp)
{
	int pf = rvu_get_pf(req->hdr.pcifunc);
	u8 cgx_id, lmac_id;

	if (!is_cgx_config_permitted(rvu, req->hdr.pcifunc))
		return -EPERM;

	if (rvu_npc_exact_has_match_table(rvu))
		return rvu_npc_exact_mac_addr_set(rvu, req, rsp);

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);

	cgx_lmac_addr_set(cgx_id, lmac_id, req->mac_addr);

	return 0;
}

int rvu_mbox_handler_cgx_mac_addr_add(struct rvu *rvu,
				      struct cgx_mac_addr_add_req *req,
				      struct cgx_mac_addr_add_rsp *rsp)
{
	int pf = rvu_get_pf(req->hdr.pcifunc);
	u8 cgx_id, lmac_id;
	int rc = 0;

	if (!is_cgx_config_permitted(rvu, req->hdr.pcifunc))
		return -EPERM;

	if (rvu_npc_exact_has_match_table(rvu))
		return rvu_npc_exact_mac_addr_add(rvu, req, rsp);

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);
	rc = cgx_lmac_addr_add(cgx_id, lmac_id, req->mac_addr);
	if (rc >= 0) {
		rsp->index = rc;
		return 0;
	}

	return rc;
}

int rvu_mbox_handler_cgx_mac_addr_del(struct rvu *rvu,
				      struct cgx_mac_addr_del_req *req,
				      struct msg_rsp *rsp)
{
	int pf = rvu_get_pf(req->hdr.pcifunc);
	u8 cgx_id, lmac_id;

	if (!is_cgx_config_permitted(rvu, req->hdr.pcifunc))
		return -EPERM;

	if (rvu_npc_exact_has_match_table(rvu))
		return rvu_npc_exact_mac_addr_del(rvu, req, rsp);

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);
	return cgx_lmac_addr_del(cgx_id, lmac_id, req->index);
}

int rvu_mbox_handler_cgx_mac_max_entries_get(struct rvu *rvu,
					     struct msg_req *req,
					     struct cgx_max_dmac_entries_get_rsp
					     *rsp)
{
	int pf = rvu_get_pf(req->hdr.pcifunc);
	u8 cgx_id, lmac_id;

	/* If msg is received from PFs(which are not mapped to CGX LMACs)
	 * or VF then no entries are allocated for DMAC filters at CGX level.
	 * So returning zero.
	 */
	if (!is_cgx_config_permitted(rvu, req->hdr.pcifunc)) {
		rsp->max_dmac_filters = 0;
		return 0;
	}

	if (rvu_npc_exact_has_match_table(rvu)) {
		rsp->max_dmac_filters = rvu_npc_exact_get_max_entries(rvu);
		return 0;
	}

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);
	rsp->max_dmac_filters = cgx_lmac_addr_max_entries_get(cgx_id, lmac_id);
	return 0;
}

int rvu_mbox_handler_cgx_mac_addr_get(struct rvu *rvu,
				      struct cgx_mac_addr_set_or_get *req,
				      struct cgx_mac_addr_set_or_get *rsp)
{
	int pf = rvu_get_pf(req->hdr.pcifunc);
	u8 cgx_id, lmac_id;
	int rc = 0, i;
	u64 cfg;

	if (!is_cgx_config_permitted(rvu, req->hdr.pcifunc))
		return -EPERM;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);

	rsp->hdr.rc = rc;
	cfg = cgx_lmac_addr_get(cgx_id, lmac_id);
	/* copy 48 bit mac address to req->mac_addr */
	for (i = 0; i < ETH_ALEN; i++)
		rsp->mac_addr[i] = cfg >> (ETH_ALEN - 1 - i) * 8;
	return 0;
}

int rvu_mbox_handler_cgx_promisc_enable(struct rvu *rvu, struct msg_req *req,
					struct msg_rsp *rsp)
{
	u16 pcifunc = req->hdr.pcifunc;
	int pf = rvu_get_pf(pcifunc);
	u8 cgx_id, lmac_id;

	if (!is_cgx_config_permitted(rvu, req->hdr.pcifunc))
		return -EPERM;

	/* Disable drop on non hit rule */
	if (rvu_npc_exact_has_match_table(rvu))
		return rvu_npc_exact_promisc_enable(rvu, req->hdr.pcifunc);

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);

	cgx_lmac_promisc_config(cgx_id, lmac_id, true);
	return 0;
}

int rvu_mbox_handler_cgx_promisc_disable(struct rvu *rvu, struct msg_req *req,
					 struct msg_rsp *rsp)
{
	int pf = rvu_get_pf(req->hdr.pcifunc);
	u8 cgx_id, lmac_id;

	if (!is_cgx_config_permitted(rvu, req->hdr.pcifunc))
		return -EPERM;

	/* Disable drop on non hit rule */
	if (rvu_npc_exact_has_match_table(rvu))
		return rvu_npc_exact_promisc_disable(rvu, req->hdr.pcifunc);

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);

	cgx_lmac_promisc_config(cgx_id, lmac_id, false);
	return 0;
}

static int rvu_cgx_ptp_rx_cfg(struct rvu *rvu, u16 pcifunc, bool enable)
{
	struct rvu_pfvf *pfvf = rvu_get_pfvf(rvu, pcifunc);
	int pf = rvu_get_pf(pcifunc);
	struct mac_ops *mac_ops;
	u8 cgx_id, lmac_id;
	void *cgxd;

	if (!is_mac_feature_supported(rvu, pf, RVU_LMAC_FEAT_PTP))
		return 0;

	/* This msg is expected only from PFs that are mapped to CGX LMACs,
	 * if received from other PF/VF simply ACK, nothing to do.
	 */
	if ((pcifunc & RVU_PFVF_FUNC_MASK) ||
	    !is_pf_cgxmapped(rvu, pf))
		return -ENODEV;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);
	cgxd = rvu_cgx_pdata(cgx_id, rvu);

	mac_ops = get_mac_ops(cgxd);
	mac_ops->mac_enadis_ptp_config(cgxd, lmac_id, enable);
	/* If PTP is enabled then inform NPC that packets to be
	 * parsed by this PF will have their data shifted by 8 bytes
	 * and if PTP is disabled then no shift is required
	 */
	if (npc_config_ts_kpuaction(rvu, pf, pcifunc, enable))
		return -EINVAL;
	/* This flag is required to clean up CGX conf if app gets killed */
	pfvf->hw_rx_tstamp_en = enable;

	/* Inform MCS about 8B RX header */
	rvu_mcs_ptp_cfg(rvu, cgx_id, lmac_id, enable);
	return 0;
}

int rvu_mbox_handler_cgx_ptp_rx_enable(struct rvu *rvu, struct msg_req *req,
				       struct msg_rsp *rsp)
{
	if (!is_pf_cgxmapped(rvu, rvu_get_pf(req->hdr.pcifunc)))
		return -EPERM;

	return rvu_cgx_ptp_rx_cfg(rvu, req->hdr.pcifunc, true);
}

int rvu_mbox_handler_cgx_ptp_rx_disable(struct rvu *rvu, struct msg_req *req,
					struct msg_rsp *rsp)
{
	return rvu_cgx_ptp_rx_cfg(rvu, req->hdr.pcifunc, false);
}

static int rvu_cgx_config_linkevents(struct rvu *rvu, u16 pcifunc, bool en)
{
	int pf = rvu_get_pf(pcifunc);
	u8 cgx_id, lmac_id;

	if (!is_cgx_config_permitted(rvu, pcifunc))
		return -EPERM;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);

	if (en) {
		set_bit(pf, &rvu->pf_notify_bmap);
		/* Send the current link status to PF */
		rvu_cgx_send_link_info(cgx_id, lmac_id, rvu);
	} else {
		clear_bit(pf, &rvu->pf_notify_bmap);
	}

	return 0;
}

int rvu_mbox_handler_cgx_start_linkevents(struct rvu *rvu, struct msg_req *req,
					  struct msg_rsp *rsp)
{
	rvu_cgx_config_linkevents(rvu, req->hdr.pcifunc, true);
	return 0;
}

int rvu_mbox_handler_cgx_stop_linkevents(struct rvu *rvu, struct msg_req *req,
					 struct msg_rsp *rsp)
{
	rvu_cgx_config_linkevents(rvu, req->hdr.pcifunc, false);
	return 0;
}

int rvu_mbox_handler_cgx_get_linkinfo(struct rvu *rvu, struct msg_req *req,
				      struct cgx_link_info_msg *rsp)
{
	u8 cgx_id, lmac_id;
	int pf, err;

	pf = rvu_get_pf(req->hdr.pcifunc);

	if (!is_pf_cgxmapped(rvu, pf))
		return -ENODEV;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);

	err = cgx_get_link_info(rvu_cgx_pdata(cgx_id, rvu), lmac_id,
				&rsp->link_info);
	return err;
}

int rvu_mbox_handler_cgx_features_get(struct rvu *rvu,
				      struct msg_req *req,
				      struct cgx_features_info_msg *rsp)
{
	int pf = rvu_get_pf(req->hdr.pcifunc);
	u8 cgx_idx, lmac;
	void *cgxd;

	if (!is_pf_cgxmapped(rvu, pf))
		return 0;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_idx, &lmac);
	cgxd = rvu_cgx_pdata(cgx_idx, rvu);
	rsp->lmac_features = cgx_features_get(cgxd);

	return 0;
}

u32 rvu_cgx_get_fifolen(struct rvu *rvu)
{
	struct mac_ops *mac_ops;
	u32 fifo_len;

	mac_ops = get_mac_ops(rvu_first_cgx_pdata(rvu));
	fifo_len = mac_ops ? mac_ops->fifo_len : 0;

	return fifo_len;
}

u32 rvu_cgx_get_lmac_fifolen(struct rvu *rvu, int cgx, int lmac)
{
	struct mac_ops *mac_ops;
	void *cgxd;

	cgxd = rvu_cgx_pdata(cgx, rvu);
	if (!cgxd)
		return 0;

	mac_ops = get_mac_ops(cgxd);
	if (!mac_ops->lmac_fifo_len)
		return 0;

	return mac_ops->lmac_fifo_len(cgxd, lmac);
}

static int rvu_cgx_config_intlbk(struct rvu *rvu, u16 pcifunc, bool en)
{
	int pf = rvu_get_pf(pcifunc);
	struct mac_ops *mac_ops;
	u8 cgx_id, lmac_id;

	if (!is_cgx_config_permitted(rvu, pcifunc))
		return -EPERM;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);
	mac_ops = get_mac_ops(rvu_cgx_pdata(cgx_id, rvu));

	return mac_ops->mac_lmac_intl_lbk(rvu_cgx_pdata(cgx_id, rvu),
					  lmac_id, en);
}

int rvu_mbox_handler_cgx_intlbk_enable(struct rvu *rvu, struct msg_req *req,
				       struct msg_rsp *rsp)
{
	rvu_cgx_config_intlbk(rvu, req->hdr.pcifunc, true);
	return 0;
}

int rvu_mbox_handler_cgx_intlbk_disable(struct rvu *rvu, struct msg_req *req,
					struct msg_rsp *rsp)
{
	rvu_cgx_config_intlbk(rvu, req->hdr.pcifunc, false);
	return 0;
}

int rvu_cgx_cfg_pause_frm(struct rvu *rvu, u16 pcifunc, u8 tx_pause, u8 rx_pause)
{
	int pf = rvu_get_pf(pcifunc);
	u8 rx_pfc = 0, tx_pfc = 0;
	struct mac_ops *mac_ops;
	u8 cgx_id, lmac_id;
	void *cgxd;

	if (!is_mac_feature_supported(rvu, pf, RVU_LMAC_FEAT_FC))
		return 0;

	/* This msg is expected only from PF/VFs that are mapped to CGX LMACs,
	 * if received from other PF/VF simply ACK, nothing to do.
	 */
	if (!is_pf_cgxmapped(rvu, pf))
		return LMAC_AF_ERR_PF_NOT_MAPPED;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);
	cgxd = rvu_cgx_pdata(cgx_id, rvu);
	mac_ops = get_mac_ops(cgxd);

	mac_ops->mac_get_pfc_frm_cfg(cgxd, lmac_id, &tx_pfc, &rx_pfc);
	if (tx_pfc || rx_pfc) {
		dev_warn(rvu->dev,
			 "Can not configure 802.3X flow control as PFC frames are enabled");
		return LMAC_AF_ERR_8023PAUSE_ENADIS_PERM_DENIED;
	}

	mutex_lock(&rvu->rsrc_lock);
	if (verify_lmac_fc_cfg(cgxd, lmac_id, tx_pause, rx_pause,
			       pcifunc & RVU_PFVF_FUNC_MASK)) {
		mutex_unlock(&rvu->rsrc_lock);
		return LMAC_AF_ERR_PERM_DENIED;
	}
	mutex_unlock(&rvu->rsrc_lock);

	return mac_ops->mac_enadis_pause_frm(cgxd, lmac_id, tx_pause, rx_pause);
}

int rvu_mbox_handler_cgx_cfg_pause_frm(struct rvu *rvu,
				       struct cgx_pause_frm_cfg *req,
				       struct cgx_pause_frm_cfg *rsp)
{
	int pf = rvu_get_pf(req->hdr.pcifunc);
	struct mac_ops *mac_ops;
	u8 cgx_id, lmac_id;
	int err = 0;
	void *cgxd;

	/* This msg is expected only from PF/VFs that are mapped to CGX LMACs,
	 * if received from other PF/VF simply ACK, nothing to do.
	 */
	if (!is_pf_cgxmapped(rvu, pf))
		return -ENODEV;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);
	cgxd = rvu_cgx_pdata(cgx_id, rvu);
	mac_ops = get_mac_ops(cgxd);

	if (req->set)
		err = rvu_cgx_cfg_pause_frm(rvu, req->hdr.pcifunc, req->tx_pause, req->rx_pause);
	else
		mac_ops->mac_get_pause_frm_status(cgxd, lmac_id, &rsp->tx_pause, &rsp->rx_pause);

	return err;
}

int rvu_mbox_handler_cgx_get_phy_fec_stats(struct rvu *rvu, struct msg_req *req,
					   struct msg_rsp *rsp)
{
	int pf = rvu_get_pf(req->hdr.pcifunc);
	u8 cgx_id, lmac_id;

	if (!is_pf_cgxmapped(rvu, pf))
		return LMAC_AF_ERR_PF_NOT_MAPPED;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);
	return cgx_get_phy_fec_stats(rvu_cgx_pdata(cgx_id, rvu), lmac_id);
}

/* Finds cumulative status of NIX rx/tx counters from LF of a PF and those
 * from its VFs as well. ie. NIX rx/tx counters at the CGX port level
 */
int rvu_cgx_nix_cuml_stats(struct rvu *rvu, void *cgxd, int lmac_id,
			   int index, int rxtxflag, u64 *stat)
{
	struct rvu_block *block;
	int blkaddr;
	u16 pcifunc;
	int pf, lf;

	*stat = 0;

	if (!cgxd || !rvu)
		return -EINVAL;

	pf = cgxlmac_to_pf(rvu, cgx_get_cgxid(cgxd), lmac_id);
	if (pf < 0)
		return pf;

	/* Assumes LF of a PF and all of its VF belongs to the same
	 * NIX block
	 */
	pcifunc = pf << RVU_PFVF_PF_SHIFT;
	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, pcifunc);
	if (blkaddr < 0)
		return 0;
	block = &rvu->hw->block[blkaddr];

	for (lf = 0; lf < block->lf.max; lf++) {
		/* Check if a lf is attached to this PF or one of its VFs */
		if (!((block->fn_map[lf] & ~RVU_PFVF_FUNC_MASK) == (pcifunc &
			 ~RVU_PFVF_FUNC_MASK)))
			continue;
		if (rxtxflag == NIX_STATS_RX)
			*stat += rvu_read64(rvu, blkaddr,
					    NIX_AF_LFX_RX_STATX(lf, index));
		else
			*stat += rvu_read64(rvu, blkaddr,
					    NIX_AF_LFX_TX_STATX(lf, index));
	}

	return 0;
}

int rvu_cgx_start_stop_io(struct rvu *rvu, u16 pcifunc, bool start)
{
	struct rvu_pfvf *parent_pf, *pfvf;
	int cgx_users, err = 0;

	if (!is_pf_cgxmapped(rvu, rvu_get_pf(pcifunc)))
		return 0;

	parent_pf = &rvu->pf[rvu_get_pf(pcifunc)];
	pfvf = rvu_get_pfvf(rvu, pcifunc);

	mutex_lock(&rvu->cgx_cfg_lock);

	if (start && pfvf->cgx_in_use)
		goto exit;  /* CGX is already started hence nothing to do */
	if (!start && !pfvf->cgx_in_use)
		goto exit; /* CGX is already stopped hence nothing to do */

	if (start) {
		cgx_users = parent_pf->cgx_users;
		parent_pf->cgx_users++;
	} else {
		parent_pf->cgx_users--;
		cgx_users = parent_pf->cgx_users;
	}

	/* Start CGX when first of all NIXLFs is started.
	 * Stop CGX when last of all NIXLFs is stopped.
	 */
	if (!cgx_users) {
		err = rvu_cgx_config_rxtx(rvu, pcifunc & ~RVU_PFVF_FUNC_MASK,
					  start);
		if (err) {
			dev_err(rvu->dev, "Unable to %s CGX\n",
				start ? "start" : "stop");
			/* Revert the usage count in case of error */
			parent_pf->cgx_users = start ? parent_pf->cgx_users  - 1
					       : parent_pf->cgx_users  + 1;
			goto exit;
		}
	}
	pfvf->cgx_in_use = start;
exit:
	mutex_unlock(&rvu->cgx_cfg_lock);
	return err;
}

int rvu_mbox_handler_cgx_set_fec_param(struct rvu *rvu,
				       struct fec_mode *req,
				       struct fec_mode *rsp)
{
	int pf = rvu_get_pf(req->hdr.pcifunc);
	u8 cgx_id, lmac_id;

	if (!is_pf_cgxmapped(rvu, pf))
		return -EPERM;

	if (req->fec == OTX2_FEC_OFF)
		req->fec = OTX2_FEC_NONE;
	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);
	rsp->fec = cgx_set_fec(req->fec, cgx_id, lmac_id);
	return 0;
}

int rvu_mbox_handler_cgx_get_aux_link_info(struct rvu *rvu, struct msg_req *req,
					   struct cgx_fw_data *rsp)
{
	int pf = rvu_get_pf(req->hdr.pcifunc);
	u8 cgx_id, lmac_id;

	if (!rvu->fwdata)
		return LMAC_AF_ERR_FIRMWARE_DATA_NOT_MAPPED;

	if (!is_pf_cgxmapped(rvu, pf))
		return -EPERM;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);

	memcpy(&rsp->fwdata, &rvu->fwdata->cgx_fw_data[cgx_id][lmac_id],
	       sizeof(struct cgx_lmac_fwdata_s));
	return 0;
}

int rvu_mbox_handler_cgx_set_link_mode(struct rvu *rvu,
				       struct cgx_set_link_mode_req *req,
				       struct cgx_set_link_mode_rsp *rsp)
{
	int pf = rvu_get_pf(req->hdr.pcifunc);
	u8 cgx_idx, lmac;
	void *cgxd;

	if (!is_cgx_config_permitted(rvu, req->hdr.pcifunc))
		return -EPERM;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_idx, &lmac);
	cgxd = rvu_cgx_pdata(cgx_idx, rvu);
	rsp->status = cgx_set_link_mode(cgxd, req->args, cgx_idx, lmac);
	return 0;
}

int rvu_mbox_handler_cgx_mac_addr_reset(struct rvu *rvu, struct cgx_mac_addr_reset_req *req,
					struct msg_rsp *rsp)
{
	int pf = rvu_get_pf(req->hdr.pcifunc);
	u8 cgx_id, lmac_id;

	if (!is_cgx_config_permitted(rvu, req->hdr.pcifunc))
		return LMAC_AF_ERR_PERM_DENIED;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);

	if (rvu_npc_exact_has_match_table(rvu))
		return rvu_npc_exact_mac_addr_reset(rvu, req, rsp);

	return cgx_lmac_addr_reset(cgx_id, lmac_id);
}

int rvu_mbox_handler_cgx_mac_addr_update(struct rvu *rvu,
					 struct cgx_mac_addr_update_req *req,
					 struct cgx_mac_addr_update_rsp *rsp)
{
	int pf = rvu_get_pf(req->hdr.pcifunc);
	u8 cgx_id, lmac_id;

	if (!is_cgx_config_permitted(rvu, req->hdr.pcifunc))
		return LMAC_AF_ERR_PERM_DENIED;

	if (rvu_npc_exact_has_match_table(rvu))
		return rvu_npc_exact_mac_addr_update(rvu, req, rsp);

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);
	return cgx_lmac_addr_update(cgx_id, lmac_id, req->mac_addr, req->index);
}

int rvu_cgx_prio_flow_ctrl_cfg(struct rvu *rvu, u16 pcifunc, u8 tx_pause,
			       u8 rx_pause, u16 pfc_en)
{
	int pf = rvu_get_pf(pcifunc);
	u8 rx_8023 = 0, tx_8023 = 0;
	struct mac_ops *mac_ops;
	u8 cgx_id, lmac_id;
	void *cgxd;

	/* This msg is expected only from PF/VFs that are mapped to CGX LMACs,
	 * if received from other PF/VF simply ACK, nothing to do.
	 */
	if (!is_pf_cgxmapped(rvu, pf))
		return -ENODEV;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);
	cgxd = rvu_cgx_pdata(cgx_id, rvu);
	mac_ops = get_mac_ops(cgxd);

	mac_ops->mac_get_pause_frm_status(cgxd, lmac_id, &tx_8023, &rx_8023);
	if (tx_8023 || rx_8023) {
		dev_warn(rvu->dev,
			 "Can not configure PFC as 802.3X pause frames are enabled");
		return LMAC_AF_ERR_PFC_ENADIS_PERM_DENIED;
	}

	mutex_lock(&rvu->rsrc_lock);
	if (verify_lmac_fc_cfg(cgxd, lmac_id, tx_pause, rx_pause,
			       pcifunc & RVU_PFVF_FUNC_MASK)) {
		mutex_unlock(&rvu->rsrc_lock);
		return LMAC_AF_ERR_PERM_DENIED;
	}
	mutex_unlock(&rvu->rsrc_lock);

	return mac_ops->pfc_config(cgxd, lmac_id, tx_pause, rx_pause, pfc_en);
}

int rvu_mbox_handler_cgx_prio_flow_ctrl_cfg(struct rvu *rvu,
					    struct cgx_pfc_cfg *req,
					    struct cgx_pfc_rsp *rsp)
{
	int pf = rvu_get_pf(req->hdr.pcifunc);
	struct mac_ops *mac_ops;
	u8 cgx_id, lmac_id;
	void *cgxd;
	int err;

	/* This msg is expected only from PF/VFs that are mapped to CGX LMACs,
	 * if received from other PF/VF simply ACK, nothing to do.
	 */
	if (!is_pf_cgxmapped(rvu, pf))
		return -ENODEV;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);
	cgxd = rvu_cgx_pdata(cgx_id, rvu);
	mac_ops = get_mac_ops(cgxd);

	err = rvu_cgx_prio_flow_ctrl_cfg(rvu, req->hdr.pcifunc, req->tx_pause,
					 req->rx_pause, req->pfc_en);

	mac_ops->mac_get_pfc_frm_cfg(cgxd, lmac_id, &rsp->tx_pause, &rsp->rx_pause);
	return err;
}
