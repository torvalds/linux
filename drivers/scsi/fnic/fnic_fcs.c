/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/skbuff.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/workqueue.h>
#include <scsi/fc/fc_els.h>
#include <scsi/fc/fc_fcoe.h>
#include <scsi/fc_frame.h>
#include <scsi/libfc.h>
#include "fnic_io.h"
#include "fnic.h"
#include "cq_enet_desc.h"
#include "cq_exch_desc.h"

struct workqueue_struct *fnic_event_queue;

void fnic_handle_link(struct work_struct *work)
{
	struct fnic *fnic = container_of(work, struct fnic, link_work);
	unsigned long flags;
	int old_link_status;
	u32 old_link_down_cnt;

	spin_lock_irqsave(&fnic->fnic_lock, flags);

	if (fnic->stop_rx_link_events) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return;
	}

	old_link_down_cnt = fnic->link_down_cnt;
	old_link_status = fnic->link_status;
	fnic->link_status = vnic_dev_link_status(fnic->vdev);
	fnic->link_down_cnt = vnic_dev_link_down_cnt(fnic->vdev);

	if (old_link_status == fnic->link_status) {
		if (!fnic->link_status)
			/* DOWN -> DOWN */
			spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		else {
			if (old_link_down_cnt != fnic->link_down_cnt) {
				/* UP -> DOWN -> UP */
				fnic->lport->host_stats.link_failure_count++;
				spin_unlock_irqrestore(&fnic->fnic_lock, flags);
				FNIC_FCS_DBG(KERN_DEBUG, fnic->lport->host,
					     "link down\n");
				fc_linkdown(fnic->lport);
				FNIC_FCS_DBG(KERN_DEBUG, fnic->lport->host,
					     "link up\n");
				fc_linkup(fnic->lport);
			} else
				/* UP -> UP */
				spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		}
	} else if (fnic->link_status) {
		/* DOWN -> UP */
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		FNIC_FCS_DBG(KERN_DEBUG, fnic->lport->host, "link up\n");
		fc_linkup(fnic->lport);
	} else {
		/* UP -> DOWN */
		fnic->lport->host_stats.link_failure_count++;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		FNIC_FCS_DBG(KERN_DEBUG, fnic->lport->host, "link down\n");
		fc_linkdown(fnic->lport);
	}

}

/*
 * This function passes incoming fabric frames to libFC
 */
void fnic_handle_frame(struct work_struct *work)
{
	struct fnic *fnic = container_of(work, struct fnic, frame_work);
	struct fc_lport *lp = fnic->lport;
	unsigned long flags;
	struct sk_buff *skb;
	struct fc_frame *fp;

	while ((skb = skb_dequeue(&fnic->frame_queue))) {

		spin_lock_irqsave(&fnic->fnic_lock, flags);
		if (fnic->stop_rx_link_events) {
			spin_unlock_irqrestore(&fnic->fnic_lock, flags);
			dev_kfree_skb(skb);
			return;
		}
		fp = (struct fc_frame *)skb;
		/* if Flogi resp frame, register the address */
		if (fr_flags(fp)) {
			vnic_dev_add_addr(fnic->vdev,
					  fnic->data_src_addr);
			fr_flags(fp) = 0;
		}
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);

		fc_exch_recv(lp, fp);
	}

}

static inline void fnic_import_rq_fc_frame(struct sk_buff *skb,
					   u32 len, u8 sof, u8 eof)
{
	struct fc_frame *fp = (struct fc_frame *)skb;

	skb_trim(skb, len);
	fr_eof(fp) = eof;
	fr_sof(fp) = sof;
}


static inline int fnic_import_rq_eth_pkt(struct sk_buff *skb, u32 len)
{
	struct fc_frame *fp;
	struct ethhdr *eh;
	struct vlan_ethhdr *vh;
	struct fcoe_hdr *fcoe_hdr;
	struct fcoe_crc_eof *ft;
	u32    transport_len = 0;

	eh = (struct ethhdr *)skb->data;
	vh = (struct vlan_ethhdr *)skb->data;
	if (vh->h_vlan_proto == htons(ETH_P_8021Q) &&
	    vh->h_vlan_encapsulated_proto == htons(ETH_P_FCOE)) {
		skb_pull(skb, sizeof(struct vlan_ethhdr));
		transport_len += sizeof(struct vlan_ethhdr);
	} else if (eh->h_proto == htons(ETH_P_FCOE)) {
		transport_len += sizeof(struct ethhdr);
		skb_pull(skb, sizeof(struct ethhdr));
	} else
		return -1;

	fcoe_hdr = (struct fcoe_hdr *)skb->data;
	if (FC_FCOE_DECAPS_VER(fcoe_hdr) != FC_FCOE_VER)
		return -1;

	fp = (struct fc_frame *)skb;
	fc_frame_init(fp);
	fr_sof(fp) = fcoe_hdr->fcoe_sof;
	skb_pull(skb, sizeof(struct fcoe_hdr));
	transport_len += sizeof(struct fcoe_hdr);

	ft = (struct fcoe_crc_eof *)(skb->data + len -
				     transport_len - sizeof(*ft));
	fr_eof(fp) = ft->fcoe_eof;
	skb_trim(skb, len - transport_len - sizeof(*ft));
	return 0;
}

static inline int fnic_handle_flogi_resp(struct fnic *fnic,
					 struct fc_frame *fp)
{
	u8 mac[ETH_ALEN] = FC_FCOE_FLOGI_MAC;
	struct ethhdr *eth_hdr;
	struct fc_frame_header *fh;
	int ret = 0;
	unsigned long flags;
	struct fc_frame *old_flogi_resp = NULL;

	fh = (struct fc_frame_header *)fr_hdr(fp);

	spin_lock_irqsave(&fnic->fnic_lock, flags);

	if (fnic->state == FNIC_IN_ETH_MODE) {

		/*
		 * Check if oxid matches on taking the lock. A new Flogi
		 * issued by libFC might have changed the fnic cached oxid
		 */
		if (fnic->flogi_oxid != ntohs(fh->fh_ox_id)) {
			FNIC_FCS_DBG(KERN_DEBUG, fnic->lport->host,
				     "Flogi response oxid not"
				     " matching cached oxid, dropping frame"
				     "\n");
			ret = -1;
			spin_unlock_irqrestore(&fnic->fnic_lock, flags);
			dev_kfree_skb_irq(fp_skb(fp));
			goto handle_flogi_resp_end;
		}

		/* Drop older cached flogi response frame, cache this frame */
		old_flogi_resp = fnic->flogi_resp;
		fnic->flogi_resp = fp;
		fnic->flogi_oxid = FC_XID_UNKNOWN;

		/*
		 * this frame is part of flogi get the src mac addr from this
		 * frame if the src mac is fcoui based then we mark the
		 * address mode flag to use fcoui base for dst mac addr
		 * otherwise we have to store the fcoe gateway addr
		 */
		eth_hdr = (struct ethhdr *)skb_mac_header(fp_skb(fp));
		memcpy(mac, eth_hdr->h_source, ETH_ALEN);

		if (ntoh24(mac) == FC_FCOE_OUI)
			fnic->fcoui_mode = 1;
		else {
			fnic->fcoui_mode = 0;
			memcpy(fnic->dest_addr, mac, ETH_ALEN);
		}

		/*
		 * Except for Flogi frame, all outbound frames from us have the
		 * Eth Src address as FC_FCOE_OUI"our_sid". Flogi frame uses
		 * the vnic MAC address as the Eth Src address
		 */
		fc_fcoe_set_mac(fnic->data_src_addr, fh->fh_d_id);

		/* We get our s_id from the d_id of the flogi resp frame */
		fnic->s_id = ntoh24(fh->fh_d_id);

		/* Change state to reflect transition from Eth to FC mode */
		fnic->state = FNIC_IN_ETH_TRANS_FC_MODE;

	} else {
		FNIC_FCS_DBG(KERN_DEBUG, fnic->lport->host,
			     "Unexpected fnic state %s while"
			     " processing flogi resp\n",
			     fnic_state_to_str(fnic->state));
		ret = -1;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		dev_kfree_skb_irq(fp_skb(fp));
		goto handle_flogi_resp_end;
	}

	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	/* Drop older cached frame */
	if (old_flogi_resp)
		dev_kfree_skb_irq(fp_skb(old_flogi_resp));

	/*
	 * send flogi reg request to firmware, this will put the fnic in
	 * in FC mode
	 */
	ret = fnic_flogi_reg_handler(fnic);

	if (ret < 0) {
		int free_fp = 1;
		spin_lock_irqsave(&fnic->fnic_lock, flags);
		/*
		 * free the frame is some other thread is not
		 * pointing to it
		 */
		if (fnic->flogi_resp != fp)
			free_fp = 0;
		else
			fnic->flogi_resp = NULL;

		if (fnic->state == FNIC_IN_ETH_TRANS_FC_MODE)
			fnic->state = FNIC_IN_ETH_MODE;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		if (free_fp)
			dev_kfree_skb_irq(fp_skb(fp));
	}

 handle_flogi_resp_end:
	return ret;
}

/* Returns 1 for a response that matches cached flogi oxid */
static inline int is_matching_flogi_resp_frame(struct fnic *fnic,
					       struct fc_frame *fp)
{
	struct fc_frame_header *fh;
	int ret = 0;
	u32 f_ctl;

	fh = fc_frame_header_get(fp);
	f_ctl = ntoh24(fh->fh_f_ctl);

	if (fnic->flogi_oxid == ntohs(fh->fh_ox_id) &&
	    fh->fh_r_ctl == FC_RCTL_ELS_REP &&
	    (f_ctl & (FC_FC_EX_CTX | FC_FC_SEQ_CTX)) == FC_FC_EX_CTX &&
	    fh->fh_type == FC_TYPE_ELS)
		ret = 1;

	return ret;
}

static void fnic_rq_cmpl_frame_recv(struct vnic_rq *rq, struct cq_desc
				    *cq_desc, struct vnic_rq_buf *buf,
				    int skipped __attribute__((unused)),
				    void *opaque)
{
	struct fnic *fnic = vnic_dev_priv(rq->vdev);
	struct sk_buff *skb;
	struct fc_frame *fp;
	unsigned int eth_hdrs_stripped;
	u8 type, color, eop, sop, ingress_port, vlan_stripped;
	u8 fcoe = 0, fcoe_sof, fcoe_eof;
	u8 fcoe_fc_crc_ok = 1, fcoe_enc_error = 0;
	u8 tcp_udp_csum_ok, udp, tcp, ipv4_csum_ok;
	u8 ipv6, ipv4, ipv4_fragment, rss_type, csum_not_calc;
	u8 fcs_ok = 1, packet_error = 0;
	u16 q_number, completed_index, bytes_written = 0, vlan, checksum;
	u32 rss_hash;
	u16 exchange_id, tmpl;
	u8 sof = 0;
	u8 eof = 0;
	u32 fcp_bytes_written = 0;
	unsigned long flags;

	pci_unmap_single(fnic->pdev, buf->dma_addr, buf->len,
			 PCI_DMA_FROMDEVICE);
	skb = buf->os_buf;
	buf->os_buf = NULL;

	cq_desc_dec(cq_desc, &type, &color, &q_number, &completed_index);
	if (type == CQ_DESC_TYPE_RQ_FCP) {
		cq_fcp_rq_desc_dec((struct cq_fcp_rq_desc *)cq_desc,
				   &type, &color, &q_number, &completed_index,
				   &eop, &sop, &fcoe_fc_crc_ok, &exchange_id,
				   &tmpl, &fcp_bytes_written, &sof, &eof,
				   &ingress_port, &packet_error,
				   &fcoe_enc_error, &fcs_ok, &vlan_stripped,
				   &vlan);
		eth_hdrs_stripped = 1;

	} else if (type == CQ_DESC_TYPE_RQ_ENET) {
		cq_enet_rq_desc_dec((struct cq_enet_rq_desc *)cq_desc,
				    &type, &color, &q_number, &completed_index,
				    &ingress_port, &fcoe, &eop, &sop,
				    &rss_type, &csum_not_calc, &rss_hash,
				    &bytes_written, &packet_error,
				    &vlan_stripped, &vlan, &checksum,
				    &fcoe_sof, &fcoe_fc_crc_ok,
				    &fcoe_enc_error, &fcoe_eof,
				    &tcp_udp_csum_ok, &udp, &tcp,
				    &ipv4_csum_ok, &ipv6, &ipv4,
				    &ipv4_fragment, &fcs_ok);
		eth_hdrs_stripped = 0;

	} else {
		/* wrong CQ type*/
		shost_printk(KERN_ERR, fnic->lport->host,
			     "fnic rq_cmpl wrong cq type x%x\n", type);
		goto drop;
	}

	if (!fcs_ok || packet_error || !fcoe_fc_crc_ok || fcoe_enc_error) {
		FNIC_FCS_DBG(KERN_DEBUG, fnic->lport->host,
			     "fnic rq_cmpl fcoe x%x fcsok x%x"
			     " pkterr x%x fcoe_fc_crc_ok x%x, fcoe_enc_err"
			     " x%x\n",
			     fcoe, fcs_ok, packet_error,
			     fcoe_fc_crc_ok, fcoe_enc_error);
		goto drop;
	}

	if (eth_hdrs_stripped)
		fnic_import_rq_fc_frame(skb, fcp_bytes_written, sof, eof);
	else if (fnic_import_rq_eth_pkt(skb, bytes_written))
		goto drop;

	fp = (struct fc_frame *)skb;

	/*
	 * If frame is an ELS response that matches the cached FLOGI OX_ID,
	 * and is accept, issue flogi_reg_request copy wq request to firmware
	 * to register the S_ID and determine whether FC_OUI mode or GW mode.
	 */
	if (is_matching_flogi_resp_frame(fnic, fp)) {
		if (!eth_hdrs_stripped) {
			if (fc_frame_payload_op(fp) == ELS_LS_ACC) {
				fnic_handle_flogi_resp(fnic, fp);
				return;
			}
			/*
			 * Recd. Flogi reject. No point registering
			 * with fw, but forward to libFC
			 */
			goto forward;
		}
		goto drop;
	}
	if (!eth_hdrs_stripped)
		goto drop;

forward:
	spin_lock_irqsave(&fnic->fnic_lock, flags);
	if (fnic->stop_rx_link_events) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		goto drop;
	}
	/* Use fr_flags to indicate whether succ. flogi resp or not */
	fr_flags(fp) = 0;
	fr_dev(fp) = fnic->lport;
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	skb_queue_tail(&fnic->frame_queue, skb);
	queue_work(fnic_event_queue, &fnic->frame_work);

	return;
drop:
	dev_kfree_skb_irq(skb);
}

static int fnic_rq_cmpl_handler_cont(struct vnic_dev *vdev,
				     struct cq_desc *cq_desc, u8 type,
				     u16 q_number, u16 completed_index,
				     void *opaque)
{
	struct fnic *fnic = vnic_dev_priv(vdev);

	vnic_rq_service(&fnic->rq[q_number], cq_desc, completed_index,
			VNIC_RQ_RETURN_DESC, fnic_rq_cmpl_frame_recv,
			NULL);
	return 0;
}

int fnic_rq_cmpl_handler(struct fnic *fnic, int rq_work_to_do)
{
	unsigned int tot_rq_work_done = 0, cur_work_done;
	unsigned int i;
	int err;

	for (i = 0; i < fnic->rq_count; i++) {
		cur_work_done = vnic_cq_service(&fnic->cq[i], rq_work_to_do,
						fnic_rq_cmpl_handler_cont,
						NULL);
		if (cur_work_done) {
			err = vnic_rq_fill(&fnic->rq[i], fnic_alloc_rq_frame);
			if (err)
				shost_printk(KERN_ERR, fnic->lport->host,
					     "fnic_alloc_rq_frame cant alloc"
					     " frame\n");
		}
		tot_rq_work_done += cur_work_done;
	}

	return tot_rq_work_done;
}

/*
 * This function is called once at init time to allocate and fill RQ
 * buffers. Subsequently, it is called in the interrupt context after RQ
 * buffer processing to replenish the buffers in the RQ
 */
int fnic_alloc_rq_frame(struct vnic_rq *rq)
{
	struct fnic *fnic = vnic_dev_priv(rq->vdev);
	struct sk_buff *skb;
	u16 len;
	dma_addr_t pa;

	len = FC_FRAME_HEADROOM + FC_MAX_FRAME + FC_FRAME_TAILROOM;
	skb = dev_alloc_skb(len);
	if (!skb) {
		FNIC_FCS_DBG(KERN_DEBUG, fnic->lport->host,
			     "Unable to allocate RQ sk_buff\n");
		return -ENOMEM;
	}
	skb_reset_mac_header(skb);
	skb_reset_transport_header(skb);
	skb_reset_network_header(skb);
	skb_put(skb, len);
	pa = pci_map_single(fnic->pdev, skb->data, len, PCI_DMA_FROMDEVICE);
	fnic_queue_rq_desc(rq, skb, pa, len);
	return 0;
}

void fnic_free_rq_buf(struct vnic_rq *rq, struct vnic_rq_buf *buf)
{
	struct fc_frame *fp = buf->os_buf;
	struct fnic *fnic = vnic_dev_priv(rq->vdev);

	pci_unmap_single(fnic->pdev, buf->dma_addr, buf->len,
			 PCI_DMA_FROMDEVICE);

	dev_kfree_skb(fp_skb(fp));
	buf->os_buf = NULL;
}

static inline int is_flogi_frame(struct fc_frame_header *fh)
{
	return fh->fh_r_ctl == FC_RCTL_ELS_REQ && *(u8 *)(fh + 1) == ELS_FLOGI;
}

int fnic_send_frame(struct fnic *fnic, struct fc_frame *fp)
{
	struct vnic_wq *wq = &fnic->wq[0];
	struct sk_buff *skb;
	dma_addr_t pa;
	struct ethhdr *eth_hdr;
	struct vlan_ethhdr *vlan_hdr;
	struct fcoe_hdr *fcoe_hdr;
	struct fc_frame_header *fh;
	u32 tot_len, eth_hdr_len;
	int ret = 0;
	unsigned long flags;

	fh = fc_frame_header_get(fp);
	skb = fp_skb(fp);

	if (!fnic->vlan_hw_insert) {
		eth_hdr_len = sizeof(*vlan_hdr) + sizeof(*fcoe_hdr);
		vlan_hdr = (struct vlan_ethhdr *)skb_push(skb, eth_hdr_len);
		eth_hdr = (struct ethhdr *)vlan_hdr;
		vlan_hdr->h_vlan_proto = htons(ETH_P_8021Q);
		vlan_hdr->h_vlan_encapsulated_proto = htons(ETH_P_FCOE);
		vlan_hdr->h_vlan_TCI = htons(fnic->vlan_id);
		fcoe_hdr = (struct fcoe_hdr *)(vlan_hdr + 1);
	} else {
		eth_hdr_len = sizeof(*eth_hdr) + sizeof(*fcoe_hdr);
		eth_hdr = (struct ethhdr *)skb_push(skb, eth_hdr_len);
		eth_hdr->h_proto = htons(ETH_P_FCOE);
		fcoe_hdr = (struct fcoe_hdr *)(eth_hdr + 1);
	}

	if (is_flogi_frame(fh)) {
		fc_fcoe_set_mac(eth_hdr->h_dest, fh->fh_d_id);
		memcpy(eth_hdr->h_source, fnic->mac_addr, ETH_ALEN);
	} else {
		if (fnic->fcoui_mode)
			fc_fcoe_set_mac(eth_hdr->h_dest, fh->fh_d_id);
		else
			memcpy(eth_hdr->h_dest, fnic->dest_addr, ETH_ALEN);
		memcpy(eth_hdr->h_source, fnic->data_src_addr, ETH_ALEN);
	}

	tot_len = skb->len;
	BUG_ON(tot_len % 4);

	memset(fcoe_hdr, 0, sizeof(*fcoe_hdr));
	fcoe_hdr->fcoe_sof = fr_sof(fp);
	if (FC_FCOE_VER)
		FC_FCOE_ENCAPS_VER(fcoe_hdr, FC_FCOE_VER);

	pa = pci_map_single(fnic->pdev, eth_hdr, tot_len, PCI_DMA_TODEVICE);

	spin_lock_irqsave(&fnic->wq_lock[0], flags);

	if (!vnic_wq_desc_avail(wq)) {
		pci_unmap_single(fnic->pdev, pa,
				 tot_len, PCI_DMA_TODEVICE);
		ret = -1;
		goto fnic_send_frame_end;
	}

	fnic_queue_wq_desc(wq, skb, pa, tot_len, fr_eof(fp),
			   fnic->vlan_hw_insert, fnic->vlan_id, 1, 1, 1);
fnic_send_frame_end:
	spin_unlock_irqrestore(&fnic->wq_lock[0], flags);

	if (ret)
		dev_kfree_skb_any(fp_skb(fp));

	return ret;
}

/*
 * fnic_send
 * Routine to send a raw frame
 */
int fnic_send(struct fc_lport *lp, struct fc_frame *fp)
{
	struct fnic *fnic = lport_priv(lp);
	struct fc_frame_header *fh;
	int ret = 0;
	enum fnic_state old_state;
	unsigned long flags;
	struct fc_frame *old_flogi = NULL;
	struct fc_frame *old_flogi_resp = NULL;

	if (fnic->in_remove) {
		dev_kfree_skb(fp_skb(fp));
		ret = -1;
		goto fnic_send_end;
	}

	fh = fc_frame_header_get(fp);
	/* if not an Flogi frame, send it out, this is the common case */
	if (!is_flogi_frame(fh))
		return fnic_send_frame(fnic, fp);

	/* Flogi frame, now enter the state machine */

	spin_lock_irqsave(&fnic->fnic_lock, flags);
again:
	/* Get any old cached frames, free them after dropping lock */
	old_flogi = fnic->flogi;
	fnic->flogi = NULL;
	old_flogi_resp = fnic->flogi_resp;
	fnic->flogi_resp = NULL;

	fnic->flogi_oxid = FC_XID_UNKNOWN;

	old_state = fnic->state;
	switch (old_state) {
	case FNIC_IN_FC_MODE:
	case FNIC_IN_ETH_TRANS_FC_MODE:
	default:
		fnic->state = FNIC_IN_FC_TRANS_ETH_MODE;
		vnic_dev_del_addr(fnic->vdev, fnic->data_src_addr);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);

		if (old_flogi) {
			dev_kfree_skb(fp_skb(old_flogi));
			old_flogi = NULL;
		}
		if (old_flogi_resp) {
			dev_kfree_skb(fp_skb(old_flogi_resp));
			old_flogi_resp = NULL;
		}

		ret = fnic_fw_reset_handler(fnic);

		spin_lock_irqsave(&fnic->fnic_lock, flags);
		if (fnic->state != FNIC_IN_FC_TRANS_ETH_MODE)
			goto again;
		if (ret) {
			fnic->state = old_state;
			spin_unlock_irqrestore(&fnic->fnic_lock, flags);
			dev_kfree_skb(fp_skb(fp));
			goto fnic_send_end;
		}
		old_flogi = fnic->flogi;
		fnic->flogi = fp;
		fnic->flogi_oxid = ntohs(fh->fh_ox_id);
		old_flogi_resp = fnic->flogi_resp;
		fnic->flogi_resp = NULL;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		break;

	case FNIC_IN_FC_TRANS_ETH_MODE:
		/*
		 * A reset is pending with the firmware. Store the flogi
		 * and its oxid. The transition out of this state happens
		 * only when Firmware completes the reset, either with
		 * success or failed. If success, transition to
		 * FNIC_IN_ETH_MODE, if fail, then transition to
		 * FNIC_IN_FC_MODE
		 */
		fnic->flogi = fp;
		fnic->flogi_oxid = ntohs(fh->fh_ox_id);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		break;

	case FNIC_IN_ETH_MODE:
		/*
		 * The fw/hw is already in eth mode. Store the oxid,
		 * and send the flogi frame out. The transition out of this
		 * state happens only we receive flogi response from the
		 * network, and the oxid matches the cached oxid when the
		 * flogi frame was sent out. If they match, then we issue
		 * a flogi_reg request and transition to state
		 * FNIC_IN_ETH_TRANS_FC_MODE
		 */
		fnic->flogi_oxid = ntohs(fh->fh_ox_id);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		ret = fnic_send_frame(fnic, fp);
		break;
	}

fnic_send_end:
	if (old_flogi)
		dev_kfree_skb(fp_skb(old_flogi));
	if (old_flogi_resp)
		dev_kfree_skb(fp_skb(old_flogi_resp));
	return ret;
}

static void fnic_wq_complete_frame_send(struct vnic_wq *wq,
					struct cq_desc *cq_desc,
					struct vnic_wq_buf *buf, void *opaque)
{
	struct sk_buff *skb = buf->os_buf;
	struct fc_frame *fp = (struct fc_frame *)skb;
	struct fnic *fnic = vnic_dev_priv(wq->vdev);

	pci_unmap_single(fnic->pdev, buf->dma_addr,
			 buf->len, PCI_DMA_TODEVICE);
	dev_kfree_skb_irq(fp_skb(fp));
	buf->os_buf = NULL;
}

static int fnic_wq_cmpl_handler_cont(struct vnic_dev *vdev,
				     struct cq_desc *cq_desc, u8 type,
				     u16 q_number, u16 completed_index,
				     void *opaque)
{
	struct fnic *fnic = vnic_dev_priv(vdev);
	unsigned long flags;

	spin_lock_irqsave(&fnic->wq_lock[q_number], flags);
	vnic_wq_service(&fnic->wq[q_number], cq_desc, completed_index,
			fnic_wq_complete_frame_send, NULL);
	spin_unlock_irqrestore(&fnic->wq_lock[q_number], flags);

	return 0;
}

int fnic_wq_cmpl_handler(struct fnic *fnic, int work_to_do)
{
	unsigned int wq_work_done = 0;
	unsigned int i;

	for (i = 0; i < fnic->raw_wq_count; i++) {
		wq_work_done  += vnic_cq_service(&fnic->cq[fnic->rq_count+i],
						 work_to_do,
						 fnic_wq_cmpl_handler_cont,
						 NULL);
	}

	return wq_work_done;
}


void fnic_free_wq_buf(struct vnic_wq *wq, struct vnic_wq_buf *buf)
{
	struct fc_frame *fp = buf->os_buf;
	struct fnic *fnic = vnic_dev_priv(wq->vdev);

	pci_unmap_single(fnic->pdev, buf->dma_addr,
			 buf->len, PCI_DMA_TODEVICE);

	dev_kfree_skb(fp_skb(fp));
	buf->os_buf = NULL;
}
