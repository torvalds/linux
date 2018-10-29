// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTx2 RVU Admin Function driver
 *
 * Copyright (C) 2018 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "rvu.h"
#include "cgx.h"

struct cgx_evq_entry {
	struct list_head evq_node;
	struct cgx_link_event link_event;
};

#define M(_name, _id, _req_type, _rsp_type)				\
static struct _req_type __maybe_unused					\
*otx2_mbox_alloc_msg_ ## _name(struct rvu *rvu, int devid)		\
{									\
	struct _req_type *req;						\
									\
	req = (struct _req_type *)otx2_mbox_alloc_msg_rsp(		\
		&rvu->mbox_up, devid, sizeof(struct _req_type),		\
		sizeof(struct _rsp_type));				\
	if (!req)							\
		return NULL;						\
	req->hdr.sig = OTX2_MBOX_REQ_SIG;				\
	req->hdr.id = _id;						\
	return req;							\
}

MBOX_UP_CGX_MESSAGES
#undef M

/* Returns bitmap of mapped PFs */
static inline u16 cgxlmac_to_pfmap(struct rvu *rvu, u8 cgx_id, u8 lmac_id)
{
	return rvu->cgxlmac2pf_map[CGX_OFFSET(cgx_id) + lmac_id];
}

static inline u8 cgxlmac_id_to_bmap(u8 cgx_id, u8 lmac_id)
{
	return ((cgx_id & 0xF) << 4) | (lmac_id & 0xF);
}

void *rvu_cgx_pdata(u8 cgx_id, struct rvu *rvu)
{
	if (cgx_id >= rvu->cgx_cnt)
		return NULL;

	return rvu->cgx_idmap[cgx_id];
}

static int rvu_map_cgx_lmac_pf(struct rvu *rvu)
{
	struct npc_pkind *pkind = &rvu->hw->pkind;
	int cgx_cnt = rvu->cgx_cnt;
	int cgx, lmac_cnt, lmac;
	int pf = PF_CGXMAP_BASE;
	int size, free_pkind;

	if (!cgx_cnt)
		return 0;

	if (cgx_cnt > 0xF || MAX_LMAC_PER_CGX > 0xF)
		return -EINVAL;

	/* Alloc map table
	 * An additional entry is required since PF id starts from 1 and
	 * hence entry at offset 0 is invalid.
	 */
	size = (cgx_cnt * MAX_LMAC_PER_CGX + 1) * sizeof(u8);
	rvu->pf2cgxlmac_map = devm_kzalloc(rvu->dev, size, GFP_KERNEL);
	if (!rvu->pf2cgxlmac_map)
		return -ENOMEM;

	/* Initialize offset 0 with an invalid cgx and lmac id */
	rvu->pf2cgxlmac_map[0] = 0xFF;

	/* Reverse map table */
	rvu->cgxlmac2pf_map = devm_kzalloc(rvu->dev,
				  cgx_cnt * MAX_LMAC_PER_CGX * sizeof(u16),
				  GFP_KERNEL);
	if (!rvu->cgxlmac2pf_map)
		return -ENOMEM;

	rvu->cgx_mapped_pfs = 0;
	for (cgx = 0; cgx < cgx_cnt; cgx++) {
		lmac_cnt = cgx_get_lmac_cnt(rvu_cgx_pdata(cgx, rvu));
		for (lmac = 0; lmac < lmac_cnt; lmac++, pf++) {
			rvu->pf2cgxlmac_map[pf] = cgxlmac_id_to_bmap(cgx, lmac);
			rvu->cgxlmac2pf_map[CGX_OFFSET(cgx) + lmac] = 1 << pf;
			free_pkind = rvu_alloc_rsrc(&pkind->rsrc);
			pkind->pfchan_map[free_pkind] = ((pf) & 0x3F) << 16;
			rvu->cgx_mapped_pfs++;
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
	if (err)
		goto skip_add;
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
		msg = otx2_mbox_alloc_msg_CGX_LINK_EVENT(rvu, pfid);
		if (!msg)
			continue;
		msg->link_info = *linfo;
		otx2_mbox_msg_send(&rvu->mbox_up, pfid);
		err = otx2_mbox_wait_for_rsp(&rvu->mbox_up, pfid);
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

static void cgx_lmac_event_handler_init(struct rvu *rvu)
{
	struct cgx_event_cb cb;
	int cgx, lmac, err;
	void *cgxd;

	spin_lock_init(&rvu->cgx_evq_lock);
	INIT_LIST_HEAD(&rvu->cgx_evq_head);
	INIT_WORK(&rvu->cgx_evh_work, cgx_evhandler_task);
	rvu->cgx_evh_wq = alloc_workqueue("rvu_evh_wq", 0, 0);
	if (!rvu->cgx_evh_wq) {
		dev_err(rvu->dev, "alloc workqueue failed");
		return;
	}

	cb.notify_link_chg = cgx_lmac_postevent; /* link change call back */
	cb.data = rvu;

	for (cgx = 0; cgx < rvu->cgx_cnt; cgx++) {
		cgxd = rvu_cgx_pdata(cgx, rvu);
		for (lmac = 0; lmac < cgx_get_lmac_cnt(cgxd); lmac++) {
			err = cgx_lmac_evh_register(&cb, cgxd, lmac);
			if (err)
				dev_err(rvu->dev,
					"%d:%d handler register failed\n",
					cgx, lmac);
		}
	}
}

void rvu_cgx_wq_destroy(struct rvu *rvu)
{
	if (rvu->cgx_evh_wq) {
		flush_workqueue(rvu->cgx_evh_wq);
		destroy_workqueue(rvu->cgx_evh_wq);
		rvu->cgx_evh_wq = NULL;
	}
}

int rvu_cgx_probe(struct rvu *rvu)
{
	int i, err;

	/* find available cgx ports */
	rvu->cgx_cnt = cgx_get_cgx_cnt();
	if (!rvu->cgx_cnt) {
		dev_info(rvu->dev, "No CGX devices found!\n");
		return -ENODEV;
	}

	rvu->cgx_idmap = devm_kzalloc(rvu->dev, rvu->cgx_cnt * sizeof(void *),
				      GFP_KERNEL);
	if (!rvu->cgx_idmap)
		return -ENOMEM;

	/* Initialize the cgxdata table */
	for (i = 0; i < rvu->cgx_cnt; i++)
		rvu->cgx_idmap[i] = cgx_get_pdata(i);

	/* Map CGX LMAC interfaces to RVU PFs */
	err = rvu_map_cgx_lmac_pf(rvu);
	if (err)
		return err;

	/* Register for CGX events */
	cgx_lmac_event_handler_init(rvu);
	return 0;
}

int rvu_cgx_config_rxtx(struct rvu *rvu, u16 pcifunc, bool start)
{
	int pf = rvu_get_pf(pcifunc);
	u8 cgx_id, lmac_id;

	/* This msg is expected only from PFs that are mapped to CGX LMACs,
	 * if received from other PF/VF simply ACK, nothing to do.
	 */
	if ((pcifunc & RVU_PFVF_FUNC_MASK) || !is_pf_cgxmapped(rvu, pf))
		return -ENODEV;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);

	cgx_lmac_rx_tx_enable(rvu_cgx_pdata(cgx_id, rvu), lmac_id, start);

	return 0;
}

int rvu_mbox_handler_CGX_START_RXTX(struct rvu *rvu, struct msg_req *req,
				    struct msg_rsp *rsp)
{
	rvu_cgx_config_rxtx(rvu, req->hdr.pcifunc, true);
	return 0;
}

int rvu_mbox_handler_CGX_STOP_RXTX(struct rvu *rvu, struct msg_req *req,
				   struct msg_rsp *rsp)
{
	rvu_cgx_config_rxtx(rvu, req->hdr.pcifunc, false);
	return 0;
}

int rvu_mbox_handler_CGX_STATS(struct rvu *rvu, struct msg_req *req,
			       struct cgx_stats_rsp *rsp)
{
	int pf = rvu_get_pf(req->hdr.pcifunc);
	int stat = 0, err = 0;
	u64 tx_stat, rx_stat;
	u8 cgx_idx, lmac;
	void *cgxd;

	if ((req->hdr.pcifunc & RVU_PFVF_FUNC_MASK) ||
	    !is_pf_cgxmapped(rvu, pf))
		return -ENODEV;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_idx, &lmac);
	cgxd = rvu_cgx_pdata(cgx_idx, rvu);

	/* Rx stats */
	while (stat < CGX_RX_STATS_COUNT) {
		err = cgx_get_rx_stats(cgxd, lmac, stat, &rx_stat);
		if (err)
			return err;
		rsp->rx_stats[stat] = rx_stat;
		stat++;
	}

	/* Tx stats */
	stat = 0;
	while (stat < CGX_TX_STATS_COUNT) {
		err = cgx_get_tx_stats(cgxd, lmac, stat, &tx_stat);
		if (err)
			return err;
		rsp->tx_stats[stat] = tx_stat;
		stat++;
	}
	return 0;
}

int rvu_mbox_handler_CGX_MAC_ADDR_SET(struct rvu *rvu,
				      struct cgx_mac_addr_set_or_get *req,
				      struct cgx_mac_addr_set_or_get *rsp)
{
	int pf = rvu_get_pf(req->hdr.pcifunc);
	u8 cgx_id, lmac_id;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);

	cgx_lmac_addr_set(cgx_id, lmac_id, req->mac_addr);

	return 0;
}

int rvu_mbox_handler_CGX_MAC_ADDR_GET(struct rvu *rvu,
				      struct cgx_mac_addr_set_or_get *req,
				      struct cgx_mac_addr_set_or_get *rsp)
{
	int pf = rvu_get_pf(req->hdr.pcifunc);
	u8 cgx_id, lmac_id;
	int rc = 0, i;
	u64 cfg;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);

	rsp->hdr.rc = rc;
	cfg = cgx_lmac_addr_get(cgx_id, lmac_id);
	/* copy 48 bit mac address to req->mac_addr */
	for (i = 0; i < ETH_ALEN; i++)
		rsp->mac_addr[i] = cfg >> (ETH_ALEN - 1 - i) * 8;
	return 0;
}

int rvu_mbox_handler_CGX_PROMISC_ENABLE(struct rvu *rvu, struct msg_req *req,
					struct msg_rsp *rsp)
{
	u16 pcifunc = req->hdr.pcifunc;
	int pf = rvu_get_pf(pcifunc);
	u8 cgx_id, lmac_id;

	/* This msg is expected only from PFs that are mapped to CGX LMACs,
	 * if received from other PF/VF simply ACK, nothing to do.
	 */
	if ((req->hdr.pcifunc & RVU_PFVF_FUNC_MASK) ||
	    !is_pf_cgxmapped(rvu, pf))
		return -ENODEV;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);

	cgx_lmac_promisc_config(cgx_id, lmac_id, true);
	return 0;
}

int rvu_mbox_handler_CGX_PROMISC_DISABLE(struct rvu *rvu, struct msg_req *req,
					 struct msg_rsp *rsp)
{
	u16 pcifunc = req->hdr.pcifunc;
	int pf = rvu_get_pf(pcifunc);
	u8 cgx_id, lmac_id;

	/* This msg is expected only from PFs that are mapped to CGX LMACs,
	 * if received from other PF/VF simply ACK, nothing to do.
	 */
	if ((req->hdr.pcifunc & RVU_PFVF_FUNC_MASK) ||
	    !is_pf_cgxmapped(rvu, pf))
		return -ENODEV;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);

	cgx_lmac_promisc_config(cgx_id, lmac_id, false);
	return 0;
}

static int rvu_cgx_config_linkevents(struct rvu *rvu, u16 pcifunc, bool en)
{
	int pf = rvu_get_pf(pcifunc);
	u8 cgx_id, lmac_id;

	/* This msg is expected only from PFs that are mapped to CGX LMACs,
	 * if received from other PF/VF simply ACK, nothing to do.
	 */
	if ((pcifunc & RVU_PFVF_FUNC_MASK) || !is_pf_cgxmapped(rvu, pf))
		return -ENODEV;

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

int rvu_mbox_handler_CGX_START_LINKEVENTS(struct rvu *rvu, struct msg_req *req,
					  struct msg_rsp *rsp)
{
	rvu_cgx_config_linkevents(rvu, req->hdr.pcifunc, true);
	return 0;
}

int rvu_mbox_handler_CGX_STOP_LINKEVENTS(struct rvu *rvu, struct msg_req *req,
					 struct msg_rsp *rsp)
{
	rvu_cgx_config_linkevents(rvu, req->hdr.pcifunc, false);
	return 0;
}

int rvu_mbox_handler_CGX_GET_LINKINFO(struct rvu *rvu, struct msg_req *req,
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

static int rvu_cgx_config_intlbk(struct rvu *rvu, u16 pcifunc, bool en)
{
	int pf = rvu_get_pf(pcifunc);
	u8 cgx_id, lmac_id;

	/* This msg is expected only from PFs that are mapped to CGX LMACs,
	 * if received from other PF/VF simply ACK, nothing to do.
	 */
	if ((pcifunc & RVU_PFVF_FUNC_MASK) || !is_pf_cgxmapped(rvu, pf))
		return -ENODEV;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);

	return cgx_lmac_internal_loopback(rvu_cgx_pdata(cgx_id, rvu),
					  lmac_id, en);
}

int rvu_mbox_handler_CGX_INTLBK_ENABLE(struct rvu *rvu, struct msg_req *req,
				       struct msg_rsp *rsp)
{
	rvu_cgx_config_intlbk(rvu, req->hdr.pcifunc, true);
	return 0;
}

int rvu_mbox_handler_CGX_INTLBK_DISABLE(struct rvu *rvu, struct msg_req *req,
					struct msg_rsp *rsp)
{
	rvu_cgx_config_intlbk(rvu, req->hdr.pcifunc, false);
	return 0;
}
