// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 */
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/workqueue.h>
#include <scsi/fc/fc_fip.h>
#include <scsi/fc/fc_els.h>
#include <scsi/fc_frame.h>
#include <linux/etherdevice.h>
#include <scsi/scsi_transport_fc.h>
#include "fnic_io.h"
#include "fnic.h"
#include "fnic_fdls.h"
#include "fdls_fc.h"
#include "cq_enet_desc.h"
#include "cq_exch_desc.h"
#include "fip.h"

#define MAX_RESET_WAIT_COUNT    64

struct workqueue_struct *fnic_event_queue;

static uint8_t FCOE_ALL_FCF_MAC[6] = FC_FCOE_FLOGI_MAC;

/*
 * Internal Functions
 * This function will initialize the src_mac address to be
 * used in outgoing frames
 */
static inline void fnic_fdls_set_fcoe_srcmac(struct fnic *fnic,
							 uint8_t *src_mac)
{
	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "Setting src mac: %02x:%02x:%02x:%02x:%02x:%02x",
				 src_mac[0], src_mac[1], src_mac[2], src_mac[3],
				 src_mac[4], src_mac[5]);

	memcpy(fnic->iport.fpma, src_mac, 6);
}

/*
 * This function will initialize the dst_mac address to be
 * used in outgoing frames
 */
static inline  void fnic_fdls_set_fcoe_dstmac(struct fnic *fnic,
							 uint8_t *dst_mac)
{
	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "Setting dst mac: %02x:%02x:%02x:%02x:%02x:%02x",
				 dst_mac[0], dst_mac[1], dst_mac[2], dst_mac[3],
				 dst_mac[4], dst_mac[5]);

	memcpy(fnic->iport.fcfmac, dst_mac, 6);
}

void fnic_get_host_port_state(struct Scsi_Host *shost)
{
	struct fnic *fnic = *((struct fnic **) shost_priv(shost));
	struct fnic_iport_s *iport = &fnic->iport;
	unsigned long flags;

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	if (!fnic->link_status)
		fc_host_port_state(shost) = FC_PORTSTATE_LINKDOWN;
	else if (iport->state == FNIC_IPORT_STATE_READY)
		fc_host_port_state(shost) = FC_PORTSTATE_ONLINE;
	else
		fc_host_port_state(shost) = FC_PORTSTATE_OFFLINE;
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
}

void fnic_fdls_link_status_change(struct fnic *fnic, int linkup)
{
	struct fnic_iport_s *iport = &fnic->iport;

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "link up: %d, usefip: %d", linkup, iport->usefip);

	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);

	if (linkup) {
		if (iport->usefip) {
			iport->state = FNIC_IPORT_STATE_FIP;
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
						 "link up: %d, usefip: %d", linkup, iport->usefip);
			fnic_fcoe_send_vlan_req(fnic);
		} else {
			iport->state = FNIC_IPORT_STATE_FABRIC_DISC;
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
						 "iport->state: %d", iport->state);
			fnic_fdls_disc_start(iport);
		}
	} else {
		iport->state = FNIC_IPORT_STATE_LINK_WAIT;
		if (!is_zero_ether_addr(iport->fpma))
			vnic_dev_del_addr(fnic->vdev, iport->fpma);
		fnic_common_fip_cleanup(fnic);
		fnic_fdls_link_down(iport);

	}
	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
}


/*
 * FPMA can be either taken from ethhdr(dst_mac) or flogi resp
 * or derive from FC_MAP and FCID combination. While it should be
 * same, revisit this if there is any possibility of not-correct.
 */
void fnic_fdls_learn_fcoe_macs(struct fnic_iport_s *iport, void *rx_frame,
							   uint8_t *fcid)
{
	struct fnic *fnic = iport->fnic;
	struct ethhdr *ethhdr = (struct ethhdr *) rx_frame;
	uint8_t fcmac[6] = { 0x0E, 0xFC, 0x00, 0x00, 0x00, 0x00 };

	memcpy(&fcmac[3], fcid, 3);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "learn fcoe: dst_mac: %02x:%02x:%02x:%02x:%02x:%02x",
				 ethhdr->h_dest[0], ethhdr->h_dest[1],
				 ethhdr->h_dest[2], ethhdr->h_dest[3],
				 ethhdr->h_dest[4], ethhdr->h_dest[5]);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "learn fcoe: fc_mac: %02x:%02x:%02x:%02x:%02x:%02x",
				 fcmac[0], fcmac[1], fcmac[2], fcmac[3], fcmac[4],
				 fcmac[5]);

	fnic_fdls_set_fcoe_srcmac(fnic, fcmac);
	fnic_fdls_set_fcoe_dstmac(fnic, ethhdr->h_source);
}

void fnic_fdls_init(struct fnic *fnic, int usefip)
{
	struct fnic_iport_s *iport = &fnic->iport;

	/* Initialize iPort structure */
	iport->state = FNIC_IPORT_STATE_INIT;
	iport->fnic = fnic;
	iport->usefip = usefip;

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "iportsrcmac: %02x:%02x:%02x:%02x:%02x:%02x",
				 iport->hwmac[0], iport->hwmac[1], iport->hwmac[2],
				 iport->hwmac[3], iport->hwmac[4], iport->hwmac[5]);

	INIT_LIST_HEAD(&iport->tport_list);
	INIT_LIST_HEAD(&iport->tport_list_pending_del);

	fnic_fdls_disc_init(iport);
}

void fnic_handle_link(struct work_struct *work)
{
	struct fnic *fnic = container_of(work, struct fnic, link_work);
	int old_link_status;
	u32 old_link_down_cnt;
	int max_count = 0;

	if (vnic_dev_get_intr_mode(fnic->vdev) != VNIC_DEV_INTR_MODE_MSI)
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "Interrupt mode is not MSI\n");

	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);

	if (fnic->stop_rx_link_events) {
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "Stop link rx events\n");
		return;
	}

	/* Do not process if the fnic is already in transitional state */
	if ((fnic->state != FNIC_IN_ETH_MODE)
		&& (fnic->state != FNIC_IN_FC_MODE)) {
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "fnic in transitional state: %d. link up: %d ignored",
			 fnic->state, vnic_dev_link_status(fnic->vdev));
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "Current link status: %d iport state: %d\n",
			 fnic->link_status, fnic->iport.state);
		return;
	}

	old_link_down_cnt = fnic->link_down_cnt;
	old_link_status = fnic->link_status;
	fnic->link_status = vnic_dev_link_status(fnic->vdev);
	fnic->link_down_cnt = vnic_dev_link_down_cnt(fnic->vdev);

	while (fnic->reset_in_progress == IN_PROGRESS) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "fnic reset in progress. Link event needs to wait\n");

		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "waiting for reset completion\n");
		wait_for_completion_timeout(&fnic->reset_completion_wait,
									msecs_to_jiffies(5000));
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "woken up from reset completion wait\n");
		spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);

		max_count++;
		if (max_count >= MAX_RESET_WAIT_COUNT) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "Rstth waited for too long. Skipping handle link event\n");
			spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
			return;
		}
	}
	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "Marking fnic reset in progress\n");
	fnic->reset_in_progress = IN_PROGRESS;

	if ((vnic_dev_get_intr_mode(fnic->vdev) != VNIC_DEV_INTR_MODE_MSI) ||
		(fnic->link_status != old_link_status)) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "old link status: %d link status: %d\n",
					 old_link_status, (int) fnic->link_status);
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "old down count %d down count: %d\n",
					 old_link_down_cnt, (int) fnic->link_down_cnt);
	}

	if (old_link_status == fnic->link_status) {
		if (!fnic->link_status) {
			/* DOWN -> DOWN */
			spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
						 "down->down\n");
		} else {
			if (old_link_down_cnt != fnic->link_down_cnt) {
				/* UP -> DOWN -> UP */
				spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
				FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
							 "up->down. Link down\n");
				fnic_fdls_link_status_change(fnic, 0);

				FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
							 "down->up. Link up\n");
				fnic_fdls_link_status_change(fnic, 1);
			} else {
				/* UP -> UP */
				spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
				FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
							 "up->up\n");
			}
		}
	} else if (fnic->link_status) {
		/* DOWN -> UP */
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "down->up. Link up\n");
		fnic_fdls_link_status_change(fnic, 1);
	} else {
		/* UP -> DOWN */
		spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "up->down. Link down\n");
		fnic_fdls_link_status_change(fnic, 0);
	}

	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);
	fnic->reset_in_progress = NOT_IN_PROGRESS;
	complete(&fnic->reset_completion_wait);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "Marking fnic reset completion\n");
	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
}

void fnic_handle_frame(struct work_struct *work)
{
	struct fnic *fnic = container_of(work, struct fnic, frame_work);
	struct fnic_frame_list *cur_frame, *next;
	int fchdr_offset = 0;

	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);
	list_for_each_entry_safe(cur_frame, next, &fnic->frame_queue, links) {
		if (fnic->stop_rx_link_events) {
			list_del(&cur_frame->links);
			spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
			kfree(cur_frame->fp);
			mempool_free(cur_frame, fnic->frame_elem_pool);
			return;
		}

		/*
		 * If we're in a transitional state, just re-queue and return.
		 * The queue will be serviced when we get to a stable state.
		 */
		if (fnic->state != FNIC_IN_FC_MODE &&
			fnic->state != FNIC_IN_ETH_MODE) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "Cannot process frame in transitional state\n");
			spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
			return;
		}

		list_del(&cur_frame->links);

		/* Frames from FCP_RQ will have ethhdrs stripped off */
		fchdr_offset = (cur_frame->rx_ethhdr_stripped) ?
			0 : FNIC_ETH_FCOE_HDRS_OFFSET;

		fnic_fdls_recv_frame(&fnic->iport, cur_frame->fp,
							 cur_frame->frame_len, fchdr_offset);

		kfree(cur_frame->fp);
		mempool_free(cur_frame, fnic->frame_elem_pool);
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
}

void fnic_handle_fip_frame(struct work_struct *work)
{
	struct fnic_frame_list *cur_frame, *next;
	struct fnic *fnic = container_of(work, struct fnic, fip_frame_work);

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "Processing FIP frame\n");

	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);
	list_for_each_entry_safe(cur_frame, next, &fnic->fip_frame_queue,
							 links) {
		if (fnic->stop_rx_link_events) {
			list_del(&cur_frame->links);
			spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
			kfree(cur_frame->fp);
			kfree(cur_frame);
			return;
		}

		/*
		 * If we're in a transitional state, just re-queue and return.
		 * The queue will be serviced when we get to a stable state.
		 */
		if (fnic->state != FNIC_IN_FC_MODE &&
			fnic->state != FNIC_IN_ETH_MODE) {
			spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
			return;
		}

		list_del(&cur_frame->links);

		if (fdls_fip_recv_frame(fnic, cur_frame->fp)) {
			kfree(cur_frame->fp);
			kfree(cur_frame);
		}
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
}

/**
 * fnic_import_rq_eth_pkt() - handle received FCoE or FIP frame.
 * @fnic:	fnic instance.
 * @fp:		Ethernet Frame.
 */
static inline int fnic_import_rq_eth_pkt(struct fnic *fnic, void *fp)
{
	struct ethhdr *eh;
	struct fnic_frame_list *fip_fr_elem;
	unsigned long flags;

	eh = (struct ethhdr *) fp;
	if ((eh->h_proto == cpu_to_be16(ETH_P_FIP)) && (fnic->iport.usefip)) {
		fip_fr_elem = (struct fnic_frame_list *)
			kzalloc(sizeof(struct fnic_frame_list), GFP_ATOMIC);
		if (!fip_fr_elem)
			return 0;
		fip_fr_elem->fp = fp;
		spin_lock_irqsave(&fnic->fnic_lock, flags);
		list_add_tail(&fip_fr_elem->links, &fnic->fip_frame_queue);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		queue_work(fnic_fip_queue, &fnic->fip_frame_work);
		return 1;				/* let caller know packet was used */
	} else
		return 0;
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
	struct fnic_iport_s *iport = &fnic->iport;
	u8 *ctl = iport->hwmac;
	u8 *data = fnic->data_src_addr;

	if (is_zero_ether_addr(new))
		new = ctl;
	if (ether_addr_equal(data, new))
		return;

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "Update MAC: %u\n", *new);

	if (!is_zero_ether_addr(data) && !ether_addr_equal(data, ctl))
		vnic_dev_del_addr(fnic->vdev, data);

	memcpy(data, new, ETH_ALEN);
	if (!ether_addr_equal(new, ctl))
		vnic_dev_add_addr(fnic->vdev, new);
}

static void fnic_rq_cmpl_frame_recv(struct vnic_rq *rq, struct cq_desc
				    *cq_desc, struct vnic_rq_buf *buf,
				    int skipped __attribute__((unused)),
				    void *opaque)
{
	struct fnic *fnic = vnic_dev_priv(rq->vdev);
	uint8_t *fp;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	unsigned int ethhdr_stripped;
	u8 type, color, eop, sop, ingress_port, vlan_stripped;
	u8 fcoe_fnic_crc_ok = 1, fcoe_enc_error = 0;
	u8 fcs_ok = 1, packet_error = 0;
	u16 q_number, completed_index, vlan;
	u32 rss_hash;
	u16 checksum;
	u8 csum_not_calc, rss_type, ipv4, ipv6, ipv4_fragment;
	u8 tcp_udp_csum_ok, udp, tcp, ipv4_csum_ok;
	u8 fcoe = 0, fcoe_sof, fcoe_eof;
	u16 exchange_id, tmpl;
	u8 sof = 0;
	u8 eof = 0;
	u32 fcp_bytes_written = 0;
	u16 enet_bytes_written = 0;
	u32 bytes_written = 0;
	unsigned long flags;
	struct fnic_frame_list *frame_elem = NULL;
	struct ethhdr *eh;

	dma_unmap_single(&fnic->pdev->dev, buf->dma_addr, buf->len,
					 DMA_FROM_DEVICE);
	fp = (uint8_t *) buf->os_buf;
	buf->os_buf = NULL;

	cq_desc_dec(cq_desc, &type, &color, &q_number, &completed_index);
	if (type == CQ_DESC_TYPE_RQ_FCP) {
		cq_fcp_rq_desc_dec((struct cq_fcp_rq_desc *) cq_desc, &type,
						   &color, &q_number, &completed_index, &eop, &sop,
						   &fcoe_fnic_crc_ok, &exchange_id, &tmpl,
						   &fcp_bytes_written, &sof, &eof, &ingress_port,
						   &packet_error, &fcoe_enc_error, &fcs_ok,
						   &vlan_stripped, &vlan);
		ethhdr_stripped = 1;
		bytes_written = fcp_bytes_written;
	} else if (type == CQ_DESC_TYPE_RQ_ENET) {
		cq_enet_rq_desc_dec((struct cq_enet_rq_desc *) cq_desc, &type,
					&color, &q_number, &completed_index,
					&ingress_port, &fcoe, &eop, &sop, &rss_type,
					&csum_not_calc, &rss_hash, &enet_bytes_written,
					&packet_error, &vlan_stripped, &vlan,
					&checksum, &fcoe_sof, &fcoe_fnic_crc_ok,
					&fcoe_enc_error, &fcoe_eof, &tcp_udp_csum_ok,
					&udp, &tcp, &ipv4_csum_ok, &ipv6, &ipv4,
					&ipv4_fragment, &fcs_ok);

		ethhdr_stripped = 0;
		bytes_written = enet_bytes_written;

		if (!fcs_ok) {
			atomic64_inc(&fnic_stats->misc_stats.frame_errors);
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
						 "fnic 0x%p fcs error.  Dropping packet.\n", fnic);
			goto drop;
		}
		eh = (struct ethhdr *) fp;
		if (eh->h_proto != cpu_to_be16(ETH_P_FCOE)) {

			if (fnic_import_rq_eth_pkt(fnic, fp))
				return;

			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
							 "Dropping h_proto 0x%x",
							 be16_to_cpu(eh->h_proto));
			goto drop;
		}
	} else {
		/* wrong CQ type */
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "fnic rq_cmpl wrong cq type x%x\n", type);
		goto drop;
	}

	if (!fcs_ok || packet_error || !fcoe_fnic_crc_ok || fcoe_enc_error) {
		atomic64_inc(&fnic_stats->misc_stats.frame_errors);
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "fcoe %x fcsok %x pkterr %x ffco %x fee %x\n",
			 fcoe, fcs_ok, packet_error,
			 fcoe_fnic_crc_ok, fcoe_enc_error);
		goto drop;
	}

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	if (fnic->stop_rx_link_events) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "fnic->stop_rx_link_events: %d\n",
					 fnic->stop_rx_link_events);
		goto drop;
	}

	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	frame_elem = mempool_alloc(fnic->frame_elem_pool,
					GFP_ATOMIC | __GFP_ZERO);
	if (!frame_elem) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "Failed to allocate memory for frame elem");
		goto drop;
	}
	frame_elem->fp = fp;
	frame_elem->rx_ethhdr_stripped = ethhdr_stripped;
	frame_elem->frame_len = bytes_written;

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	list_add_tail(&frame_elem->links, &fnic->frame_queue);
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	queue_work(fnic_event_queue, &fnic->frame_work);
	return;

drop:
	kfree(fp);
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
		if (cur_work_done && fnic->stop_rx_link_events != 1) {
			err = vnic_rq_fill(&fnic->rq[i], fnic_alloc_rq_frame);
			if (err)
				shost_printk(KERN_ERR, fnic->host,
					     "fnic_alloc_rq_frame can't alloc"
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
	void *buf;
	u16 len;
	dma_addr_t pa;
	int ret;

	len = FNIC_FRAME_HT_ROOM;
	buf = kmalloc(len, GFP_ATOMIC);
	if (!buf) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "Unable to allocate RQ buffer of size: %d\n", len);
		return -ENOMEM;
	}

	pa = dma_map_single(&fnic->pdev->dev, buf, len, DMA_FROM_DEVICE);
	if (dma_mapping_error(&fnic->pdev->dev, pa)) {
		ret = -ENOMEM;
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "PCI mapping failed with error %d\n", ret);
		goto free_buf;
	}

	fnic_queue_rq_desc(rq, buf, pa, len);
	return 0;
free_buf:
	kfree(buf);
	return ret;
}

void fnic_free_rq_buf(struct vnic_rq *rq, struct vnic_rq_buf *buf)
{
	void *rq_buf = buf->os_buf;
	struct fnic *fnic = vnic_dev_priv(rq->vdev);

	dma_unmap_single(&fnic->pdev->dev, buf->dma_addr, buf->len,
			 DMA_FROM_DEVICE);

	kfree(rq_buf);
	buf->os_buf = NULL;
}

/*
 * Send FC frame.
 */
static int fnic_send_frame(struct fnic *fnic, void *frame, int frame_len)
{
	struct vnic_wq *wq = &fnic->wq[0];
	dma_addr_t pa;
	int ret = 0;
	unsigned long flags;

	pa = dma_map_single(&fnic->pdev->dev, frame, frame_len, DMA_TO_DEVICE);
	if (dma_mapping_error(&fnic->pdev->dev, pa))
		return -ENOMEM;

	if ((fnic_fc_trace_set_data(fnic->fnic_num,
				FNIC_FC_SEND | 0x80, (char *) frame,
				frame_len)) != 0) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "fnic ctlr frame trace error");
	}

	spin_lock_irqsave(&fnic->wq_lock[0], flags);

	if (!vnic_wq_desc_avail(wq)) {
		dma_unmap_single(&fnic->pdev->dev, pa, frame_len, DMA_TO_DEVICE);
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "vnic work queue descriptor is not available");
		ret = -1;
		goto fnic_send_frame_end;
	}

	/* hw inserts cos value */
	fnic_queue_wq_desc(wq, frame, pa, frame_len, FC_EOF_T,
					   0, fnic->vlan_id, 1, 1, 1);

fnic_send_frame_end:
	spin_unlock_irqrestore(&fnic->wq_lock[0], flags);
	return ret;
}

/**
 * fdls_send_fcoe_frame - send a filled-in FC frame, filling in eth and FCoE
 *	info. This interface is used only in the non fast path. (login, fabric
 *	registrations etc.)
 *
 * @fnic:	fnic instance
 * @frame:	frame structure with FC payload filled in
 * @frame_size:	length of the frame to be sent
 * @srcmac:	source mac address
 * @dstmac:	destination mac address
 *
 * Called with the fnic lock held.
 */
static int
fdls_send_fcoe_frame(struct fnic *fnic, void *frame, int frame_size,
					 uint8_t *srcmac, uint8_t *dstmac)
{
	struct ethhdr *pethhdr;
	struct fcoe_hdr *pfcoe_hdr;
	struct fnic_frame_list *frame_elem;
	int len = frame_size;
	int ret;
	struct fc_frame_header *fchdr = (struct fc_frame_header *) (frame +
			FNIC_ETH_FCOE_HDRS_OFFSET);

	pethhdr = (struct ethhdr *) frame;
	pethhdr->h_proto = cpu_to_be16(ETH_P_FCOE);
	memcpy(pethhdr->h_source, srcmac, ETH_ALEN);
	memcpy(pethhdr->h_dest, dstmac, ETH_ALEN);

	pfcoe_hdr = (struct fcoe_hdr *) (frame + sizeof(struct ethhdr));
	pfcoe_hdr->fcoe_sof = FC_SOF_I3;

	/*
	 * Queue frame if in a transitional state.
	 * This occurs while registering the Port_ID / MAC address after FLOGI.
	 */
	if ((fnic->state != FNIC_IN_FC_MODE)
		&& (fnic->state != FNIC_IN_ETH_MODE)) {
		frame_elem = mempool_alloc(fnic->frame_elem_pool,
						GFP_ATOMIC | __GFP_ZERO);
		if (!frame_elem) {
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "Failed to allocate memory for frame elem");
			return -ENOMEM;
		}

		FNIC_FCS_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
			"Queueing FC frame: sid/did/type/oxid = 0x%x/0x%x/0x%x/0x%x\n",
			ntoh24(fchdr->fh_s_id), ntoh24(fchdr->fh_d_id),
			fchdr->fh_type, FNIC_STD_GET_OX_ID(fchdr));
		frame_elem->fp = frame;
		frame_elem->frame_len = len;
		list_add_tail(&frame_elem->links, &fnic->tx_queue);
		return 0;
	}

	fnic_debug_dump_fc_frame(fnic, fchdr, frame_size, "Outgoing");

	ret = fnic_send_frame(fnic, frame, len);
	return ret;
}

void fnic_send_fcoe_frame(struct fnic_iport_s *iport, void *frame,
						 int frame_size)
{
	struct fnic *fnic = iport->fnic;
	uint8_t *dstmac, *srcmac;

	/* If module unload is in-progress, don't send */
	if (fnic->in_remove)
		return;

	if (iport->fabric.flags & FNIC_FDLS_FPMA_LEARNT) {
		srcmac = iport->fpma;
		dstmac = iport->fcfmac;
	} else {
		srcmac = iport->hwmac;
		dstmac = FCOE_ALL_FCF_MAC;
	}

	fdls_send_fcoe_frame(fnic, frame, frame_size, srcmac, dstmac);
}

int
fnic_send_fip_frame(struct fnic_iport_s *iport, void *frame,
					int frame_size)
{
	struct fnic *fnic = iport->fnic;

	if (fnic->in_remove)
		return -1;

	fnic_debug_dump_fip_frame(fnic, frame, frame_size, "Outgoing");
	return fnic_send_frame(fnic, frame, frame_size);
}

/**
 * fnic_flush_tx() - send queued frames.
 * @work: pointer to work element
 *
 * Send frames that were waiting to go out in FC or Ethernet mode.
 * Whenever changing modes we purge queued frames, so these frames should
 * be queued for the stable mode that we're in, either FC or Ethernet.
 *
 * Called without fnic_lock held.
 */
void fnic_flush_tx(struct work_struct *work)
{
	struct fnic *fnic = container_of(work, struct fnic, flush_work);
	struct fc_frame *fp;
	struct fnic_frame_list *cur_frame, *next;

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "Flush queued frames");

	list_for_each_entry_safe(cur_frame, next, &fnic->tx_queue, links) {
		fp = cur_frame->fp;
		list_del(&cur_frame->links);
		fnic_send_frame(fnic, fp, cur_frame->frame_len);
		mempool_free(cur_frame, fnic->frame_elem_pool);
	}
}

int
fnic_fdls_register_portid(struct fnic_iport_s *iport, u32 port_id,
						  void *fp)
{
	struct fnic *fnic = iport->fnic;
	struct ethhdr *ethhdr;
	int ret;

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "Setting port id: 0x%x fp: 0x%p fnic state: %d", port_id,
				 fp, fnic->state);

	if (fp) {
		ethhdr = (struct ethhdr *) fp;
		vnic_dev_add_addr(fnic->vdev, ethhdr->h_dest);
	}

	/* Change state to reflect transition to FC mode */
	if (fnic->state == FNIC_IN_ETH_MODE || fnic->state == FNIC_IN_FC_MODE)
		fnic->state = FNIC_IN_ETH_TRANS_FC_MODE;
	else {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			 "Unexpected fnic state while processing FLOGI response\n");
		return -1;
	}

	/*
	 * Send FLOGI registration to firmware to set up FC mode.
	 * The new address will be set up when registration completes.
	 */
	ret = fnic_flogi_reg_handler(fnic, port_id);
	if (ret < 0) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "FLOGI registration error ret: %d fnic state: %d\n",
					 ret, fnic->state);
		if (fnic->state == FNIC_IN_ETH_TRANS_FC_MODE)
			fnic->state = FNIC_IN_ETH_MODE;

		return -1;
	}
	iport->fabric.flags |= FNIC_FDLS_FPMA_LEARNT;

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "FLOGI registration success\n");
	return 0;
}

void fnic_free_txq(struct list_head *head)
{
	struct fnic_frame_list *cur_frame, *next;

	list_for_each_entry_safe(cur_frame, next, head, links) {
		list_del(&cur_frame->links);
		kfree(cur_frame->fp);
		kfree(cur_frame);
	}
}

static void fnic_wq_complete_frame_send(struct vnic_wq *wq,
					struct cq_desc *cq_desc,
					struct vnic_wq_buf *buf, void *opaque)
{
	struct fnic *fnic = vnic_dev_priv(wq->vdev);

	dma_unmap_single(&fnic->pdev->dev, buf->dma_addr, buf->len,
			 DMA_TO_DEVICE);
	mempool_free(buf->os_buf, fnic->frame_pool);
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
	struct fnic *fnic = vnic_dev_priv(wq->vdev);

	dma_unmap_single(&fnic->pdev->dev, buf->dma_addr, buf->len,
			 DMA_TO_DEVICE);

	kfree(buf->os_buf);
	buf->os_buf = NULL;
}

void
fnic_fdls_add_tport(struct fnic_iport_s *iport, struct fnic_tport_s *tport,
					unsigned long flags)
{
	struct fnic *fnic = iport->fnic;
	struct fc_rport *rport;
	struct fc_rport_identifiers ids;
	struct rport_dd_data_s *rdd_data;

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "Adding rport fcid: 0x%x", tport->fcid);

	ids.node_name = tport->wwnn;
	ids.port_name = tport->wwpn;
	ids.port_id = tport->fcid;
	ids.roles = FC_RPORT_ROLE_FCP_TARGET;

	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	rport = fc_remote_port_add(fnic->host, 0, &ids);
	spin_lock_irqsave(&fnic->fnic_lock, flags);
	if (!rport) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "Failed to add rport for tport: 0x%x", tport->fcid);
		return;
	}

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "Added rport fcid: 0x%x", tport->fcid);

	/* Mimic these assignments in queuecommand to avoid timing issues */
	rport->maxframe_size = FNIC_FC_MAX_PAYLOAD_LEN;
	rport->supported_classes = FC_COS_CLASS3 | FC_RPORT_ROLE_FCP_TARGET;
	rdd_data = rport->dd_data;
	rdd_data->tport = tport;
	rdd_data->iport = iport;
	tport->rport = rport;
	tport->flags |= FNIC_FDLS_SCSI_REGISTERED;
}

void
fnic_fdls_remove_tport(struct fnic_iport_s *iport,
					   struct fnic_tport_s *tport, unsigned long flags)
{
	struct fnic *fnic = iport->fnic;
	struct rport_dd_data_s *rdd_data;

	struct fc_rport *rport;

	if (!tport)
		return;

	fdls_set_tport_state(tport, FDLS_TGT_STATE_OFFLINE);
	rport = tport->rport;

	if (rport) {
		/* tport resource release will be done
		 * after fnic_terminate_rport_io()
		 */
		tport->flags |= FNIC_FDLS_TPORT_DELETED;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);

		/* Interface to scsi_fc_transport  */
		fc_remote_port_delete(rport);

		spin_lock_irqsave(&fnic->fnic_lock, flags);
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		 "Deregistered and freed tport fcid: 0x%x from scsi transport fc",
		 tport->fcid);

		/*
		 * the dd_data is allocated by fc transport
		 * of size dd_fcrport_size
		 */
		rdd_data = rport->dd_data;
		rdd_data->tport = NULL;
		rdd_data->iport = NULL;
		list_del(&tport->links);
		kfree(tport);
	} else {
		fnic_del_tport_timer_sync(fnic, tport);
		list_del(&tport->links);
		kfree(tport);
	}
}

void fnic_delete_fcp_tports(struct fnic *fnic)
{
	struct fnic_tport_s *tport, *next;
	unsigned long flags;

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	list_for_each_entry_safe(tport, next, &fnic->iport.tport_list, links) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "removing fcp rport fcid: 0x%x", tport->fcid);
		fdls_set_tport_state(tport, FDLS_TGT_STATE_OFFLINING);
		fnic_del_tport_timer_sync(fnic, tport);
		fnic_fdls_remove_tport(&fnic->iport, tport, flags);
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
}

/**
 * fnic_tport_event_handler() - Handler for remote port events
 * in the tport_event_queue.
 *
 * @work: Handle to the remote port being dequeued
 */
void fnic_tport_event_handler(struct work_struct *work)
{
	struct fnic *fnic = container_of(work, struct fnic, tport_work);
	struct fnic_tport_event_s *cur_evt, *next;
	unsigned long flags;
	struct fnic_tport_s *tport;

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	list_for_each_entry_safe(cur_evt, next, &fnic->tport_event_list, links) {
		tport = cur_evt->arg1;
		switch (cur_evt->event) {
		case TGT_EV_RPORT_ADD:
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
						 "Add rport event");
			if (tport->state == FDLS_TGT_STATE_READY) {
				fnic_fdls_add_tport(&fnic->iport,
					(struct fnic_tport_s *) cur_evt->arg1, flags);
			} else {
				FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "Target not ready. Add rport event dropped: 0x%x",
					 tport->fcid);
			}
			break;
		case TGT_EV_RPORT_DEL:
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
						 "Remove rport event");
			if (tport->state == FDLS_TGT_STATE_OFFLINING) {
				fnic_fdls_remove_tport(&fnic->iport,
					   (struct fnic_tport_s *) cur_evt->arg1, flags);
			} else {
				FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
							 "remove rport event dropped tport fcid: 0x%x",
							 tport->fcid);
			}
			break;
		case TGT_EV_TPORT_DELETE:
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
						 "Delete tport event");
			fdls_delete_tport(tport->iport, tport);
			break;
		default:
			FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
						 "Unknown tport event");
			break;
		}
		list_del(&cur_evt->links);
		kfree(cur_evt);
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
}

void fnic_flush_tport_event_list(struct fnic *fnic)
{
	struct fnic_tport_event_s *cur_evt, *next;
	unsigned long flags;

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	list_for_each_entry_safe(cur_evt, next, &fnic->tport_event_list, links) {
		list_del(&cur_evt->links);
		kfree(cur_evt);
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
}

void fnic_reset_work_handler(struct work_struct *work)
{
	struct fnic *cur_fnic, *next_fnic;
	unsigned long reset_fnic_list_lock_flags;
	int host_reset_ret_code;

	/*
	 * This is a single thread. It is per fnic module, not per fnic
	 * All the fnics that need to be reset
	 * have been serialized via the reset fnic list.
	 */
	spin_lock_irqsave(&reset_fnic_list_lock, reset_fnic_list_lock_flags);
	list_for_each_entry_safe(cur_fnic, next_fnic, &reset_fnic_list, links) {
		list_del(&cur_fnic->links);
		spin_unlock_irqrestore(&reset_fnic_list_lock,
							   reset_fnic_list_lock_flags);

		dev_err(&cur_fnic->pdev->dev, "fnic: <%d>: issuing a host reset\n",
			   cur_fnic->fnic_num);
		host_reset_ret_code = fnic_host_reset(cur_fnic->host);
		dev_err(&cur_fnic->pdev->dev,
		   "fnic: <%d>: returned from host reset with status: %d\n",
		   cur_fnic->fnic_num, host_reset_ret_code);

		spin_lock_irqsave(&cur_fnic->fnic_lock, cur_fnic->lock_flags);
		cur_fnic->pc_rscn_handling_status =
			PC_RSCN_HANDLING_NOT_IN_PROGRESS;
		spin_unlock_irqrestore(&cur_fnic->fnic_lock, cur_fnic->lock_flags);

		spin_lock_irqsave(&reset_fnic_list_lock,
						  reset_fnic_list_lock_flags);
	}
	spin_unlock_irqrestore(&reset_fnic_list_lock,
						   reset_fnic_list_lock_flags);
}
