/*
 *  QLogic FCoE Offload Driver
 *  Copyright (c) 2016-2018 Cavium Inc.
 *
 *  This software is available under the terms of the GNU General Public License
 *  (GPL) Version 2, available from the file COPYING in the main directory of
 *  this source tree.
 */
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include "qedf.h"

extern const struct qed_fcoe_ops *qed_ops;
/*
 * FIP VLAN functions that will eventually move to libfcoe.
 */

void qedf_fcoe_send_vlan_req(struct qedf_ctx *qedf)
{
	struct sk_buff *skb;
	char *eth_fr;
	int fr_len;
	struct fip_vlan *vlan;
#define MY_FIP_ALL_FCF_MACS        ((__u8[6]) { 1, 0x10, 0x18, 1, 0, 2 })
	static u8 my_fcoe_all_fcfs[ETH_ALEN] = MY_FIP_ALL_FCF_MACS;
	unsigned long flags = 0;

	skb = dev_alloc_skb(sizeof(struct fip_vlan));
	if (!skb)
		return;

	fr_len = sizeof(*vlan);
	eth_fr = (char *)skb->data;
	vlan = (struct fip_vlan *)eth_fr;

	memset(vlan, 0, sizeof(*vlan));
	ether_addr_copy(vlan->eth.h_source, qedf->mac);
	ether_addr_copy(vlan->eth.h_dest, my_fcoe_all_fcfs);
	vlan->eth.h_proto = htons(ETH_P_FIP);

	vlan->fip.fip_ver = FIP_VER_ENCAPS(FIP_VER);
	vlan->fip.fip_op = htons(FIP_OP_VLAN);
	vlan->fip.fip_subcode = FIP_SC_VL_REQ;
	vlan->fip.fip_dl_len = htons(sizeof(vlan->desc) / FIP_BPW);

	vlan->desc.mac.fd_desc.fip_dtype = FIP_DT_MAC;
	vlan->desc.mac.fd_desc.fip_dlen = sizeof(vlan->desc.mac) / FIP_BPW;
	ether_addr_copy(vlan->desc.mac.fd_mac, qedf->mac);

	vlan->desc.wwnn.fd_desc.fip_dtype = FIP_DT_NAME;
	vlan->desc.wwnn.fd_desc.fip_dlen = sizeof(vlan->desc.wwnn) / FIP_BPW;
	put_unaligned_be64(qedf->lport->wwnn, &vlan->desc.wwnn.fd_wwn);

	skb_put(skb, sizeof(*vlan));
	skb->protocol = htons(ETH_P_FIP);
	skb_reset_mac_header(skb);
	skb_reset_network_header(skb);

	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_DISC, "Sending FIP VLAN "
		   "request.");

	if (atomic_read(&qedf->link_state) != QEDF_LINK_UP) {
		QEDF_WARN(&(qedf->dbg_ctx), "Cannot send vlan request "
		    "because link is not up.\n");

		kfree_skb(skb);
		return;
	}

	set_bit(QED_LL2_XMIT_FLAGS_FIP_DISCOVERY, &flags);
	qed_ops->ll2->start_xmit(qedf->cdev, skb, flags);
}

static void qedf_fcoe_process_vlan_resp(struct qedf_ctx *qedf,
	struct sk_buff *skb)
{
	struct fip_header *fiph;
	struct fip_desc *desc;
	u16 vid = 0;
	ssize_t rlen;
	size_t dlen;

	fiph = (struct fip_header *)(((void *)skb->data) + 2 * ETH_ALEN + 2);

	rlen = ntohs(fiph->fip_dl_len) * 4;
	desc = (struct fip_desc *)(fiph + 1);
	while (rlen > 0) {
		dlen = desc->fip_dlen * FIP_BPW;
		switch (desc->fip_dtype) {
		case FIP_DT_VLAN:
			vid = ntohs(((struct fip_vlan_desc *)desc)->fd_vlan);
			break;
		}
		desc = (struct fip_desc *)((char *)desc + dlen);
		rlen -= dlen;
	}

	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_DISC, "VLAN response, "
		   "vid=0x%x.\n", vid);

	if (vid > 0 && qedf->vlan_id != vid) {
		qedf_set_vlan_id(qedf, vid);

		/* Inform waiter that it's ok to call fcoe_ctlr_link up() */
		if (!completion_done(&qedf->fipvlan_compl))
			complete(&qedf->fipvlan_compl);
	}
}

void qedf_fip_send(struct fcoe_ctlr *fip, struct sk_buff *skb)
{
	struct qedf_ctx *qedf = container_of(fip, struct qedf_ctx, ctlr);
	struct ethhdr *eth_hdr;
	struct fip_header *fiph;
	u16 op, vlan_tci = 0;
	u8 sub;

	if (!test_bit(QEDF_LL2_STARTED, &qedf->flags)) {
		QEDF_WARN(&(qedf->dbg_ctx), "LL2 not started\n");
		kfree_skb(skb);
		return;
	}

	fiph = (struct fip_header *) ((void *)skb->data + 2 * ETH_ALEN + 2);
	eth_hdr = (struct ethhdr *)skb_mac_header(skb);
	op = ntohs(fiph->fip_op);
	sub = fiph->fip_subcode;

	/*
	 * Add VLAN tag to non-offload FIP frame based on current stored VLAN
	 * for FIP/FCoE traffic.
	 */
	__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), qedf->vlan_id);

	/* Get VLAN ID from skb for printing purposes */
	__vlan_hwaccel_get_tag(skb, &vlan_tci);

	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_LL2, "FIP frame send: "
	    "dest=%pM op=%x sub=%x vlan=%04x.", eth_hdr->h_dest, op, sub,
	    vlan_tci);
	if (qedf_dump_frames)
		print_hex_dump(KERN_WARNING, "fip ", DUMP_PREFIX_OFFSET, 16, 1,
		    skb->data, skb->len, false);

	qed_ops->ll2->start_xmit(qedf->cdev, skb, 0);
}

/* Process incoming FIP frames. */
void qedf_fip_recv(struct qedf_ctx *qedf, struct sk_buff *skb)
{
	struct ethhdr *eth_hdr;
	struct fip_header *fiph;
	struct fip_desc *desc;
	struct fip_mac_desc *mp;
	struct fip_wwn_desc *wp;
	struct fip_vn_desc *vp;
	size_t rlen, dlen;
	u16 op;
	u8 sub;
	bool do_reset = false;

	eth_hdr = (struct ethhdr *)skb_mac_header(skb);
	fiph = (struct fip_header *) ((void *)skb->data + 2 * ETH_ALEN + 2);
	op = ntohs(fiph->fip_op);
	sub = fiph->fip_subcode;

	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_LL2, "FIP frame received: "
	    "skb=%p fiph=%p source=%pM op=%x sub=%x", skb, fiph,
	    eth_hdr->h_source, op, sub);
	if (qedf_dump_frames)
		print_hex_dump(KERN_WARNING, "fip ", DUMP_PREFIX_OFFSET, 16, 1,
		    skb->data, skb->len, false);

	/* Handle FIP VLAN resp in the driver */
	if (op == FIP_OP_VLAN && sub == FIP_SC_VL_NOTE) {
		qedf_fcoe_process_vlan_resp(qedf, skb);
		kfree_skb(skb);
	} else if (op == FIP_OP_CTRL && sub == FIP_SC_CLR_VLINK) {
		QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_DISC, "Clear virtual "
			   "link received.\n");

		/* Check that an FCF has been selected by fcoe */
		if (qedf->ctlr.sel_fcf == NULL) {
			QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_DISC,
			    "Dropping CVL since FCF has not been selected "
			    "yet.");
			kfree_skb(skb);
			return;
		}

		/*
		 * We need to loop through the CVL descriptors to determine
		 * if we want to reset the fcoe link
		 */
		rlen = ntohs(fiph->fip_dl_len) * FIP_BPW;
		desc = (struct fip_desc *)(fiph + 1);
		while (rlen >= sizeof(*desc)) {
			dlen = desc->fip_dlen * FIP_BPW;
			switch (desc->fip_dtype) {
			case FIP_DT_MAC:
				mp = (struct fip_mac_desc *)desc;
				QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_LL2,
				    "fd_mac=%pM\n", mp->fd_mac);
				if (ether_addr_equal(mp->fd_mac,
				    qedf->ctlr.sel_fcf->fcf_mac))
					do_reset = true;
				break;
			case FIP_DT_NAME:
				wp = (struct fip_wwn_desc *)desc;
				QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_LL2,
				    "fc_wwpn=%016llx.\n",
				    get_unaligned_be64(&wp->fd_wwn));
				break;
			case FIP_DT_VN_ID:
				vp = (struct fip_vn_desc *)desc;
				QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_LL2,
				    "fd_fc_id=%x.\n", ntoh24(vp->fd_fc_id));
				if (ntoh24(vp->fd_fc_id) ==
				    qedf->lport->port_id)
					do_reset = true;
				break;
			default:
				/* Ignore anything else */
				break;
			}
			desc = (struct fip_desc *)((char *)desc + dlen);
			rlen -= dlen;
		}

		QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_LL2,
		    "do_reset=%d.\n", do_reset);
		if (do_reset) {
			fcoe_ctlr_link_down(&qedf->ctlr);
			qedf_wait_for_upload(qedf);
			fcoe_ctlr_link_up(&qedf->ctlr);
		}
		kfree_skb(skb);
	} else {
		/* Everything else is handled by libfcoe */
		__skb_pull(skb, ETH_HLEN);
		fcoe_ctlr_recv(&qedf->ctlr, skb);
	}
}

u8 *qedf_get_src_mac(struct fc_lport *lport)
{
	struct qedf_ctx *qedf = lport_priv(lport);

	return qedf->data_src_addr;
}
