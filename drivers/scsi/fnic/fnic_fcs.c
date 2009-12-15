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
#include <scsi/fc/fc_fip.h>
#include <scsi/fc/fc_els.h>
#include <scsi/fc/fc_fcoe.h>
#include <scsi/fc_frame.h>
#include <scsi/libfc.h>
#include "fnic_io.h"
#include "fnic.h"
#include "cq_enet_desc.h"
#include "cq_exch_desc.h"

struct workqueue_struct *fnic_event_queue;

static void fnic_set_eth_mode(struct fnic *);

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
				fcoe_ctlr_link_down(&fnic->ctlr);
				FNIC_FCS_DBG(KERN_DEBUG, fnic->lport->host,
					     "link up\n");
				fcoe_ctlr_link_up(&fnic->ctlr);
			} else
				/* UP -> UP */
				spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		}
	} else if (fnic->link_status) {
		/* DOWN -> UP */
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		FNIC_FCS_DBG(KERN_DEBUG, fnic->lport->host, "link up\n");
		fcoe_ctlr_link_up(&fnic->ctlr);
	} else {
		/* UP -> DOWN */
		fnic->lport->host_stats.link_failure_count++;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		FNIC_FCS_DBG(KERN_DEBUG, fnic->lport->host, "link down\n");
		fcoe_ctlr_link_down(&fnic->ctlr);
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

		/*
		 * If we're in a transitional state, just re-queue and return.
		 * The queue will be serviced when we get to a stable state.
		 */
		if (fnic->state != FNIC_IN_FC_MODE &&
		    fnic->state != FNIC_IN_ETH_MODE) {
			skb_queue_head(&fnic->frame_queue, skb);
			spin_unlock_irqrestore(&fnic->fnic_lock, flags);
			return;
		}
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);

		fc_exch_recv(lp, fp);
	}
}

/**
 * fnic_import_rq_eth_pkt() - handle received FCoE or FIP frame.
 * @fnic:	fnic instance.
 * @skb:	Ethernet Frame.
 */
static inline int fnic_import_rq_eth_pkt(struct fnic *fnic, struct sk_buff *skb)
{
	struct fc_frame *fp;
	struct ethhdr *eh;
	struct fcoe_hdr *fcoe_hdr;
	struct fcoe_crc_eof *ft;

	/*
	 * Undo VLAN encapsulation if present.
	 */
	eh = (struct ethhdr *)skb->data;
	if (eh->h_proto == htons(ETH_P_8021Q)) {
		memmove((u8 *)eh + VLAN_HLEN, eh, ETH_ALEN * 2);
		eh = (struct ethhdr *)skb_pull(skb, VLAN_HLEN);
		skb_reset_mac_header(skb);
	}
	if (eh->h_proto == htons(ETH_P_FIP)) {
		skb_pull(skb, sizeof(*eh));
		fcoe_ctlr_recv(&fnic->ctlr, skb);
		return 1;		/* let caller know packet was used */
	}
	if (eh->h_proto != htons(ETH_P_FCOE))
		goto drop;
	skb_set_network_header(skb, sizeof(*eh));
	skb_pull(skb, sizeof(*eh));

	fcoe_hdr = (struct fcoe_hdr *)skb->data;
	if (FC_FCOE_DECAPS_VER(fcoe_hdr) != FC_FCOE_VER)
		goto drop;

	fp = (struct fc_frame *)skb;
	fc_frame_init(fp);
	fr_sof(fp) = fcoe_hdr->fcoe_sof;
	skb_pull(skb, sizeof(struct fcoe_hdr));
	skb_reset_transport_header(skb);

	ft = (struct fcoe_crc_eof *)(skb->data + skb->len - sizeof(*ft));
	fr_eof(fp) = ft->fcoe_eof;
	skb_trim(skb, skb->len - sizeof(*ft));
	return 0;
drop:
	dev_kfree_skb_irq(skb);
	return -1;
}

/**
 * fnic_update_mac_locked() - set data MAC address and filters.
 * @fnic:	fnic instance.
 * @new:	newly-assigned FCoE MAC address.
 *
 * Called with the fnic lock held.
 */
void fnic_update_mac_locked(struct fnic *fnic, u8 *new)
{
	u8 *ctl = fnic->ctlr.ctl_src_addr;
	u8 *data = fnic->data_src_addr;

	if (is_zero_ether_addr(new))
		new = ctl;
	if (!compare_ether_addr(data, new))
		return;
	FNIC_FCS_DBG(KERN_DEBUG, fnic->lport->host, "update_mac %pM\n", new);
	if (!is_zero_ether_addr(data) && compare_ether_addr(data, ctl))
		vnic_dev_del_addr(fnic->vdev, data);
	memcpy(data, new, ETH_ALEN);
	if (compare_ether_addr(new, ctl))
		vnic_dev_add_addr(fnic->vdev, new);
}

/**
 * fnic_update_mac() - set data MAC address and filters.
 * @lport:	local port.
 * @new:	newly-assigned FCoE MAC address.
 */
void fnic_update_mac(struct fc_lport *lport, u8 *new)
{
	struct fnic *fnic = lport_priv(lport);

	spin_lock_irq(&fnic->fnic_lock);
	fnic_update_mac_locked(fnic, new);
	spin_unlock_irq(&fnic->fnic_lock);
}

/**
 * fnic_set_port_id() - set the port_ID after successful FLOGI.
 * @lport:	local port.
 * @port_id:	assigned FC_ID.
 * @fp:		received frame containing the FLOGI accept or NULL.
 *
 * This is called from libfc when a new FC_ID has been assigned.
 * This causes us to reset the firmware to FC_MODE and setup the new MAC
 * address and FC_ID.
 *
 * It is also called with FC_ID 0 when we're logged off.
 *
 * If the FC_ID is due to point-to-point, fp may be NULL.
 */
void fnic_set_port_id(struct fc_lport *lport, u32 port_id, struct fc_frame *fp)
{
	struct fnic *fnic = lport_priv(lport);
	u8 *mac;
	int ret;

	FNIC_FCS_DBG(KERN_DEBUG, lport->host, "set port_id %x fp %p\n",
		     port_id, fp);

	/*
	 * If we're clearing the FC_ID, change to use the ctl_src_addr.
	 * Set ethernet mode to send FLOGI.
	 */
	if (!port_id) {
		fnic_update_mac(lport, fnic->ctlr.ctl_src_addr);
		fnic_set_eth_mode(fnic);
		return;
	}

	if (fp) {
		mac = fr_cb(fp)->granted_mac;
		if (is_zero_ether_addr(mac)) {
			/* non-FIP - FLOGI already accepted - ignore return */
			fcoe_ctlr_recv_flogi(&fnic->ctlr, lport, fp);
		}
		fnic_update_mac(lport, mac);
	}

	/* Change state to reflect transition to FC mode */
	spin_lock_irq(&fnic->fnic_lock);
	if (fnic->state == FNIC_IN_ETH_MODE || fnic->state == FNIC_IN_FC_MODE)
		fnic->state = FNIC_IN_ETH_TRANS_FC_MODE;
	else {
		FNIC_FCS_DBG(KERN_DEBUG, fnic->lport->host,
			     "Unexpected fnic state %s while"
			     " processing flogi resp\n",
			     fnic_state_to_str(fnic->state));
		spin_unlock_irq(&fnic->fnic_lock);
		return;
	}
	spin_unlock_irq(&fnic->fnic_lock);

	/*
	 * Send FLOGI registration to firmware to set up FC mode.
	 * The new address will be set up when registration completes.
	 */
	ret = fnic_flogi_reg_handler(fnic, port_id);

	if (ret < 0) {
		spin_lock_irq(&fnic->fnic_lock);
		if (fnic->state == FNIC_IN_ETH_TRANS_FC_MODE)
			fnic->state = FNIC_IN_ETH_MODE;
		spin_unlock_irq(&fnic->fnic_lock);
	}
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
	fp = (struct fc_frame *)skb;
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
		skb_trim(skb, fcp_bytes_written);
		fr_sof(fp) = sof;
		fr_eof(fp) = eof;

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
		skb_trim(skb, bytes_written);
		if (!fcs_ok) {
			FNIC_FCS_DBG(KERN_DEBUG, fnic->lport->host,
				     "fcs error.  dropping packet.\n");
			goto drop;
		}
		if (fnic_import_rq_eth_pkt(fnic, skb))
			return;

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

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	if (fnic->stop_rx_link_events) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		goto drop;
	}
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

/**
 * fnic_eth_send() - Send Ethernet frame.
 * @fip:	fcoe_ctlr instance.
 * @skb:	Ethernet Frame, FIP, without VLAN encapsulation.
 */
void fnic_eth_send(struct fcoe_ctlr *fip, struct sk_buff *skb)
{
	struct fnic *fnic = fnic_from_ctlr(fip);
	struct vnic_wq *wq = &fnic->wq[0];
	dma_addr_t pa;
	struct ethhdr *eth_hdr;
	struct vlan_ethhdr *vlan_hdr;
	unsigned long flags;

	if (!fnic->vlan_hw_insert) {
		eth_hdr = (struct ethhdr *)skb_mac_header(skb);
		vlan_hdr = (struct vlan_ethhdr *)skb_push(skb,
				sizeof(*vlan_hdr) - sizeof(*eth_hdr));
		memcpy(vlan_hdr, eth_hdr, 2 * ETH_ALEN);
		vlan_hdr->h_vlan_proto = htons(ETH_P_8021Q);
		vlan_hdr->h_vlan_encapsulated_proto = eth_hdr->h_proto;
		vlan_hdr->h_vlan_TCI = htons(fnic->vlan_id);
	}

	pa = pci_map_single(fnic->pdev, skb->data, skb->len, PCI_DMA_TODEVICE);

	spin_lock_irqsave(&fnic->wq_lock[0], flags);
	if (!vnic_wq_desc_avail(wq)) {
		pci_unmap_single(fnic->pdev, pa, skb->len, PCI_DMA_TODEVICE);
		spin_unlock_irqrestore(&fnic->wq_lock[0], flags);
		kfree_skb(skb);
		return;
	}

	fnic_queue_wq_eth_desc(wq, skb, pa, skb->len,
			       fnic->vlan_hw_insert, fnic->vlan_id, 1);
	spin_unlock_irqrestore(&fnic->wq_lock[0], flags);
}

/*
 * Send FC frame.
 */
static int fnic_send_frame(struct fnic *fnic, struct fc_frame *fp)
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

	if (unlikely(fh->fh_r_ctl == FC_RCTL_ELS_REQ) &&
	    fcoe_ctlr_els_send(&fnic->ctlr, fnic->lport, skb))
		return 0;

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

	if (fnic->ctlr.map_dest)
		fc_fcoe_set_mac(eth_hdr->h_dest, fh->fh_d_id);
	else
		memcpy(eth_hdr->h_dest, fnic->ctlr.dest_addr, ETH_ALEN);
	memcpy(eth_hdr->h_source, fnic->data_src_addr, ETH_ALEN);

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
	unsigned long flags;

	if (fnic->in_remove) {
		dev_kfree_skb(fp_skb(fp));
		return -1;
	}

	/*
	 * Queue frame if in a transitional state.
	 * This occurs while registering the Port_ID / MAC address after FLOGI.
	 */
	spin_lock_irqsave(&fnic->fnic_lock, flags);
	if (fnic->state != FNIC_IN_FC_MODE && fnic->state != FNIC_IN_ETH_MODE) {
		skb_queue_tail(&fnic->tx_queue, fp_skb(fp));
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return 0;
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	return fnic_send_frame(fnic, fp);
}

/**
 * fnic_flush_tx() - send queued frames.
 * @fnic: fnic device
 *
 * Send frames that were waiting to go out in FC or Ethernet mode.
 * Whenever changing modes we purge queued frames, so these frames should
 * be queued for the stable mode that we're in, either FC or Ethernet.
 *
 * Called without fnic_lock held.
 */
void fnic_flush_tx(struct fnic *fnic)
{
	struct sk_buff *skb;
	struct fc_frame *fp;

	while ((skb = skb_dequeue(&fnic->frame_queue))) {
		fp = (struct fc_frame *)skb;
		fnic_send_frame(fnic, fp);
	}
}

/**
 * fnic_set_eth_mode() - put fnic into ethernet mode.
 * @fnic: fnic device
 *
 * Called without fnic lock held.
 */
static void fnic_set_eth_mode(struct fnic *fnic)
{
	unsigned long flags;
	enum fnic_state old_state;
	int ret;

	spin_lock_irqsave(&fnic->fnic_lock, flags);
again:
	old_state = fnic->state;
	switch (old_state) {
	case FNIC_IN_FC_MODE:
	case FNIC_IN_ETH_TRANS_FC_MODE:
	default:
		fnic->state = FNIC_IN_FC_TRANS_ETH_MODE;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);

		ret = fnic_fw_reset_handler(fnic);

		spin_lock_irqsave(&fnic->fnic_lock, flags);
		if (fnic->state != FNIC_IN_FC_TRANS_ETH_MODE)
			goto again;
		if (ret)
			fnic->state = old_state;
		break;

	case FNIC_IN_FC_TRANS_ETH_MODE:
	case FNIC_IN_ETH_MODE:
		break;
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
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
