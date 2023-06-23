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
#include <scsi/fc/fc_fcoe.h>
#include <scsi/fc_frame.h>
#include <scsi/libfc.h>
#include "fnic_io.h"
#include "fnic.h"
#include "fnic_fip.h"
#include "cq_enet_desc.h"
#include "cq_exch_desc.h"

static u8 fcoe_all_fcfs[ETH_ALEN] = FIP_ALL_FCF_MACS;
struct workqueue_struct *fnic_fip_queue;
struct workqueue_struct *fnic_event_queue;

static void fnic_set_eth_mode(struct fnic *);
static void fnic_fcoe_send_vlan_req(struct fnic *fnic);
static void fnic_fcoe_start_fcf_disc(struct fnic *fnic);
static void fnic_fcoe_process_vlan_resp(struct fnic *fnic, struct sk_buff *);
static int fnic_fcoe_vlan_check(struct fnic *fnic, u16 flag);
static int fnic_fcoe_handle_fip_frame(struct fnic *fnic, struct sk_buff *skb);

void fnic_handle_link(struct work_struct *work)
{
	struct fnic *fnic = container_of(work, struct fnic, link_work);
	unsigned long flags;
	int old_link_status;
	u32 old_link_down_cnt;
	u64 old_port_speed, new_port_speed;

	spin_lock_irqsave(&fnic->fnic_lock, flags);

	fnic->link_events = 1;      /* less work to just set everytime*/

	if (fnic->stop_rx_link_events) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return;
	}

	old_link_down_cnt = fnic->link_down_cnt;
	old_link_status = fnic->link_status;
	old_port_speed = atomic64_read(
			&fnic->fnic_stats.misc_stats.current_port_speed);

	fnic->link_status = vnic_dev_link_status(fnic->vdev);
	fnic->link_down_cnt = vnic_dev_link_down_cnt(fnic->vdev);

	new_port_speed = vnic_dev_port_speed(fnic->vdev);
	atomic64_set(&fnic->fnic_stats.misc_stats.current_port_speed,
			new_port_speed);
	if (old_port_speed != new_port_speed)
		FNIC_MAIN_DBG(KERN_INFO, fnic->lport->host,
				"Current vnic speed set to :  %llu\n",
				new_port_speed);

	switch (vnic_dev_port_speed(fnic->vdev)) {
	case DCEM_PORTSPEED_10G:
		fc_host_speed(fnic->lport->host)   = FC_PORTSPEED_10GBIT;
		fnic->lport->link_supported_speeds = FC_PORTSPEED_10GBIT;
		break;
	case DCEM_PORTSPEED_20G:
		fc_host_speed(fnic->lport->host)   = FC_PORTSPEED_20GBIT;
		fnic->lport->link_supported_speeds = FC_PORTSPEED_20GBIT;
		break;
	case DCEM_PORTSPEED_25G:
		fc_host_speed(fnic->lport->host)   = FC_PORTSPEED_25GBIT;
		fnic->lport->link_supported_speeds = FC_PORTSPEED_25GBIT;
		break;
	case DCEM_PORTSPEED_40G:
	case DCEM_PORTSPEED_4x10G:
		fc_host_speed(fnic->lport->host)   = FC_PORTSPEED_40GBIT;
		fnic->lport->link_supported_speeds = FC_PORTSPEED_40GBIT;
		break;
	case DCEM_PORTSPEED_100G:
		fc_host_speed(fnic->lport->host)   = FC_PORTSPEED_100GBIT;
		fnic->lport->link_supported_speeds = FC_PORTSPEED_100GBIT;
		break;
	default:
		fc_host_speed(fnic->lport->host)   = FC_PORTSPEED_UNKNOWN;
		fnic->lport->link_supported_speeds = FC_PORTSPEED_UNKNOWN;
		break;
	}

	if (old_link_status == fnic->link_status) {
		if (!fnic->link_status) {
			/* DOWN -> DOWN */
			spin_unlock_irqrestore(&fnic->fnic_lock, flags);
			fnic_fc_trace_set_data(fnic->lport->host->host_no,
				FNIC_FC_LE, "Link Status: DOWN->DOWN",
				strlen("Link Status: DOWN->DOWN"));
		} else {
			if (old_link_down_cnt != fnic->link_down_cnt) {
				/* UP -> DOWN -> UP */
				fnic->lport->host_stats.link_failure_count++;
				spin_unlock_irqrestore(&fnic->fnic_lock, flags);
				fnic_fc_trace_set_data(
					fnic->lport->host->host_no,
					FNIC_FC_LE,
					"Link Status:UP_DOWN_UP",
					strlen("Link_Status:UP_DOWN_UP")
					);
				FNIC_FCS_DBG(KERN_DEBUG, fnic->lport->host,
					     "link down\n");
				fcoe_ctlr_link_down(&fnic->ctlr);
				if (fnic->config.flags & VFCF_FIP_CAPABLE) {
					/* start FCoE VLAN discovery */
					fnic_fc_trace_set_data(
						fnic->lport->host->host_no,
						FNIC_FC_LE,
						"Link Status: UP_DOWN_UP_VLAN",
						strlen(
						"Link Status: UP_DOWN_UP_VLAN")
						);
					fnic_fcoe_send_vlan_req(fnic);
					return;
				}
				FNIC_FCS_DBG(KERN_DEBUG, fnic->lport->host,
					     "link up\n");
				fcoe_ctlr_link_up(&fnic->ctlr);
			} else {
				/* UP -> UP */
				spin_unlock_irqrestore(&fnic->fnic_lock, flags);
				fnic_fc_trace_set_data(
					fnic->lport->host->host_no, FNIC_FC_LE,
					"Link Status: UP_UP",
					strlen("Link Status: UP_UP"));
			}
		}
	} else if (fnic->link_status) {
		/* DOWN -> UP */
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		if (fnic->config.flags & VFCF_FIP_CAPABLE) {
			/* start FCoE VLAN discovery */
				fnic_fc_trace_set_data(
				fnic->lport->host->host_no,
				FNIC_FC_LE, "Link Status: DOWN_UP_VLAN",
				strlen("Link Status: DOWN_UP_VLAN"));
			fnic_fcoe_send_vlan_req(fnic);
			return;
		}
		FNIC_FCS_DBG(KERN_DEBUG, fnic->lport->host, "link up\n");
		fnic_fc_trace_set_data(fnic->lport->host->host_no, FNIC_FC_LE,
			"Link Status: DOWN_UP", strlen("Link Status: DOWN_UP"));
		fcoe_ctlr_link_up(&fnic->ctlr);
	} else {
		/* UP -> DOWN */
		fnic->lport->host_stats.link_failure_count++;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		FNIC_FCS_DBG(KERN_DEBUG, fnic->lport->host, "link down\n");
		fnic_fc_trace_set_data(
			fnic->lport->host->host_no, FNIC_FC_LE,
			"Link Status: UP_DOWN",
			strlen("Link Status: UP_DOWN"));
		if (fnic->config.flags & VFCF_FIP_CAPABLE) {
			FNIC_FCS_DBG(KERN_DEBUG, fnic->lport->host,
				"deleting fip-timer during link-down\n");
			del_timer_sync(&fnic->fip_timer);
		}
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

void fnic_fcoe_evlist_free(struct fnic *fnic)
{
	struct fnic_event *fevt = NULL;
	struct fnic_event *next = NULL;
	unsigned long flags;

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	if (list_empty(&fnic->evlist)) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return;
	}

	list_for_each_entry_safe(fevt, next, &fnic->evlist, list) {
		list_del(&fevt->list);
		kfree(fevt);
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
}

void fnic_handle_event(struct work_struct *work)
{
	struct fnic *fnic = container_of(work, struct fnic, event_work);
	struct fnic_event *fevt = NULL;
	struct fnic_event *next = NULL;
	unsigned long flags;

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	if (list_empty(&fnic->evlist)) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return;
	}

	list_for_each_entry_safe(fevt, next, &fnic->evlist, list) {
		if (fnic->stop_rx_link_events) {
			list_del(&fevt->list);
			kfree(fevt);
			spin_unlock_irqrestore(&fnic->fnic_lock, flags);
			return;
		}
		/*
		 * If we're in a transitional state, just re-queue and return.
		 * The queue will be serviced when we get to a stable state.
		 */
		if (fnic->state != FNIC_IN_FC_MODE &&
		    fnic->state != FNIC_IN_ETH_MODE) {
			spin_unlock_irqrestore(&fnic->fnic_lock, flags);
			return;
		}

		list_del(&fevt->list);
		switch (fevt->event) {
		case FNIC_EVT_START_VLAN_DISC:
			spin_unlock_irqrestore(&fnic->fnic_lock, flags);
			fnic_fcoe_send_vlan_req(fnic);
			spin_lock_irqsave(&fnic->fnic_lock, flags);
			break;
		case FNIC_EVT_START_FCF_DISC:
			FNIC_FCS_DBG(KERN_DEBUG, fnic->lport->host,
				  "Start FCF Discovery\n");
			fnic_fcoe_start_fcf_disc(fnic);
			break;
		default:
			FNIC_FCS_DBG(KERN_DEBUG, fnic->lport->host,
				  "Unknown event 0x%x\n", fevt->event);
			break;
		}
		kfree(fevt);
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);
}

/**
 * is_fnic_fip_flogi_reject() - Check if the Received FIP FLOGI frame is rejected
 * @fip: The FCoE controller that received the frame
 * @skb: The received FIP frame
 *
 * Returns non-zero if the frame is rejected with unsupported cmd with
 * insufficient resource els explanation.
 */
static inline int is_fnic_fip_flogi_reject(struct fcoe_ctlr *fip,
					 struct sk_buff *skb)
{
	struct fc_lport *lport = fip->lp;
	struct fip_header *fiph;
	struct fc_frame_header *fh = NULL;
	struct fip_desc *desc;
	struct fip_encaps *els;
	u16 op;
	u8 els_op;
	u8 sub;

	size_t rlen;
	size_t dlen = 0;

	if (skb_linearize(skb))
		return 0;

	if (skb->len < sizeof(*fiph))
		return 0;

	fiph = (struct fip_header *)skb->data;
	op = ntohs(fiph->fip_op);
	sub = fiph->fip_subcode;

	if (op != FIP_OP_LS)
		return 0;

	if (sub != FIP_SC_REP)
		return 0;

	rlen = ntohs(fiph->fip_dl_len) * 4;
	if (rlen + sizeof(*fiph) > skb->len)
		return 0;

	desc = (struct fip_desc *)(fiph + 1);
	dlen = desc->fip_dlen * FIP_BPW;

	if (desc->fip_dtype == FIP_DT_FLOGI) {

		if (dlen < sizeof(*els) + sizeof(*fh) + 1)
			return 0;

		els = (struct fip_encaps *)desc;
		fh = (struct fc_frame_header *)(els + 1);

		if (!fh)
			return 0;

		/*
		 * ELS command code, reason and explanation should be = Reject,
		 * unsupported command and insufficient resource
		 */
		els_op = *(u8 *)(fh + 1);
		if (els_op == ELS_LS_RJT) {
			shost_printk(KERN_INFO, lport->host,
				  "Flogi Request Rejected by Switch\n");
			return 1;
		}
		shost_printk(KERN_INFO, lport->host,
				"Flogi Request Accepted by Switch\n");
	}
	return 0;
}

static void fnic_fcoe_send_vlan_req(struct fnic *fnic)
{
	struct fcoe_ctlr *fip = &fnic->ctlr;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	struct sk_buff *skb;
	char *eth_fr;
	struct fip_vlan *vlan;
	u64 vlan_tov;

	fnic_fcoe_reset_vlans(fnic);
	fnic->set_vlan(fnic, 0);

	if (printk_ratelimit())
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host,
			  "Sending VLAN request...\n");

	skb = dev_alloc_skb(sizeof(struct fip_vlan));
	if (!skb)
		return;

	eth_fr = (char *)skb->data;
	vlan = (struct fip_vlan *)eth_fr;

	memset(vlan, 0, sizeof(*vlan));
	memcpy(vlan->eth.h_source, fip->ctl_src_addr, ETH_ALEN);
	memcpy(vlan->eth.h_dest, fcoe_all_fcfs, ETH_ALEN);
	vlan->eth.h_proto = htons(ETH_P_FIP);

	vlan->fip.fip_ver = FIP_VER_ENCAPS(FIP_VER);
	vlan->fip.fip_op = htons(FIP_OP_VLAN);
	vlan->fip.fip_subcode = FIP_SC_VL_REQ;
	vlan->fip.fip_dl_len = htons(sizeof(vlan->desc) / FIP_BPW);

	vlan->desc.mac.fd_desc.fip_dtype = FIP_DT_MAC;
	vlan->desc.mac.fd_desc.fip_dlen = sizeof(vlan->desc.mac) / FIP_BPW;
	memcpy(&vlan->desc.mac.fd_mac, fip->ctl_src_addr, ETH_ALEN);

	vlan->desc.wwnn.fd_desc.fip_dtype = FIP_DT_NAME;
	vlan->desc.wwnn.fd_desc.fip_dlen = sizeof(vlan->desc.wwnn) / FIP_BPW;
	put_unaligned_be64(fip->lp->wwnn, &vlan->desc.wwnn.fd_wwn);
	atomic64_inc(&fnic_stats->vlan_stats.vlan_disc_reqs);

	skb_put(skb, sizeof(*vlan));
	skb->protocol = htons(ETH_P_FIP);
	skb_reset_mac_header(skb);
	skb_reset_network_header(skb);
	fip->send(fip, skb);

	/* set a timer so that we can retry if there no response */
	vlan_tov = jiffies + msecs_to_jiffies(FCOE_CTLR_FIPVLAN_TOV);
	mod_timer(&fnic->fip_timer, round_jiffies(vlan_tov));
}

static void fnic_fcoe_process_vlan_resp(struct fnic *fnic, struct sk_buff *skb)
{
	struct fcoe_ctlr *fip = &fnic->ctlr;
	struct fip_header *fiph;
	struct fip_desc *desc;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	u16 vid;
	size_t rlen;
	size_t dlen;
	struct fcoe_vlan *vlan;
	u64 sol_time;
	unsigned long flags;

	FNIC_FCS_DBG(KERN_INFO, fnic->lport->host,
		  "Received VLAN response...\n");

	fiph = (struct fip_header *) skb->data;

	FNIC_FCS_DBG(KERN_INFO, fnic->lport->host,
		  "Received VLAN response... OP 0x%x SUB_OP 0x%x\n",
		  ntohs(fiph->fip_op), fiph->fip_subcode);

	rlen = ntohs(fiph->fip_dl_len) * 4;
	fnic_fcoe_reset_vlans(fnic);
	spin_lock_irqsave(&fnic->vlans_lock, flags);
	desc = (struct fip_desc *)(fiph + 1);
	while (rlen > 0) {
		dlen = desc->fip_dlen * FIP_BPW;
		switch (desc->fip_dtype) {
		case FIP_DT_VLAN:
			vid = ntohs(((struct fip_vlan_desc *)desc)->fd_vlan);
			shost_printk(KERN_INFO, fnic->lport->host,
				  "process_vlan_resp: FIP VLAN %d\n", vid);
			vlan = kzalloc(sizeof(*vlan), GFP_ATOMIC);
			if (!vlan) {
				/* retry from timer */
				spin_unlock_irqrestore(&fnic->vlans_lock,
							flags);
				goto out;
			}
			vlan->vid = vid & 0x0fff;
			vlan->state = FIP_VLAN_AVAIL;
			list_add_tail(&vlan->list, &fnic->vlans);
			break;
		}
		desc = (struct fip_desc *)((char *)desc + dlen);
		rlen -= dlen;
	}

	/* any VLAN descriptors present ? */
	if (list_empty(&fnic->vlans)) {
		/* retry from timer */
		atomic64_inc(&fnic_stats->vlan_stats.resp_withno_vlanID);
		FNIC_FCS_DBG(KERN_INFO, fnic->lport->host,
			  "No VLAN descriptors in FIP VLAN response\n");
		spin_unlock_irqrestore(&fnic->vlans_lock, flags);
		goto out;
	}

	vlan = list_first_entry(&fnic->vlans, struct fcoe_vlan, list);
	fnic->set_vlan(fnic, vlan->vid);
	vlan->state = FIP_VLAN_SENT; /* sent now */
	vlan->sol_count++;
	spin_unlock_irqrestore(&fnic->vlans_lock, flags);

	/* start the solicitation */
	fcoe_ctlr_link_up(fip);

	sol_time = jiffies + msecs_to_jiffies(FCOE_CTLR_START_DELAY);
	mod_timer(&fnic->fip_timer, round_jiffies(sol_time));
out:
	return;
}

static void fnic_fcoe_start_fcf_disc(struct fnic *fnic)
{
	unsigned long flags;
	struct fcoe_vlan *vlan;
	u64 sol_time;

	spin_lock_irqsave(&fnic->vlans_lock, flags);
	vlan = list_first_entry(&fnic->vlans, struct fcoe_vlan, list);
	fnic->set_vlan(fnic, vlan->vid);
	vlan->state = FIP_VLAN_SENT; /* sent now */
	vlan->sol_count = 1;
	spin_unlock_irqrestore(&fnic->vlans_lock, flags);

	/* start the solicitation */
	fcoe_ctlr_link_up(&fnic->ctlr);

	sol_time = jiffies + msecs_to_jiffies(FCOE_CTLR_START_DELAY);
	mod_timer(&fnic->fip_timer, round_jiffies(sol_time));
}

static int fnic_fcoe_vlan_check(struct fnic *fnic, u16 flag)
{
	unsigned long flags;
	struct fcoe_vlan *fvlan;

	spin_lock_irqsave(&fnic->vlans_lock, flags);
	if (list_empty(&fnic->vlans)) {
		spin_unlock_irqrestore(&fnic->vlans_lock, flags);
		return -EINVAL;
	}

	fvlan = list_first_entry(&fnic->vlans, struct fcoe_vlan, list);
	if (fvlan->state == FIP_VLAN_USED) {
		spin_unlock_irqrestore(&fnic->vlans_lock, flags);
		return 0;
	}

	if (fvlan->state == FIP_VLAN_SENT) {
		fvlan->state = FIP_VLAN_USED;
		spin_unlock_irqrestore(&fnic->vlans_lock, flags);
		return 0;
	}
	spin_unlock_irqrestore(&fnic->vlans_lock, flags);
	return -EINVAL;
}

static void fnic_event_enq(struct fnic *fnic, enum fnic_evt ev)
{
	struct fnic_event *fevt;
	unsigned long flags;

	fevt = kmalloc(sizeof(*fevt), GFP_ATOMIC);
	if (!fevt)
		return;

	fevt->fnic = fnic;
	fevt->event = ev;

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	list_add_tail(&fevt->list, &fnic->evlist);
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	schedule_work(&fnic->event_work);
}

static int fnic_fcoe_handle_fip_frame(struct fnic *fnic, struct sk_buff *skb)
{
	struct fip_header *fiph;
	int ret = 1;
	u16 op;
	u8 sub;

	if (!skb || !(skb->data))
		return -1;

	if (skb_linearize(skb))
		goto drop;

	fiph = (struct fip_header *)skb->data;
	op = ntohs(fiph->fip_op);
	sub = fiph->fip_subcode;

	if (FIP_VER_DECAPS(fiph->fip_ver) != FIP_VER)
		goto drop;

	if (ntohs(fiph->fip_dl_len) * FIP_BPW + sizeof(*fiph) > skb->len)
		goto drop;

	if (op == FIP_OP_DISC && sub == FIP_SC_ADV) {
		if (fnic_fcoe_vlan_check(fnic, ntohs(fiph->fip_flags)))
			goto drop;
		/* pass it on to fcoe */
		ret = 1;
	} else if (op == FIP_OP_VLAN && sub == FIP_SC_VL_NOTE) {
		/* set the vlan as used */
		fnic_fcoe_process_vlan_resp(fnic, skb);
		ret = 0;
	} else if (op == FIP_OP_CTRL && sub == FIP_SC_CLR_VLINK) {
		/* received CVL request, restart vlan disc */
		fnic_event_enq(fnic, FNIC_EVT_START_VLAN_DISC);
		/* pass it on to fcoe */
		ret = 1;
	}
drop:
	return ret;
}

void fnic_handle_fip_frame(struct work_struct *work)
{
	struct fnic *fnic = container_of(work, struct fnic, fip_frame_work);
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	unsigned long flags;
	struct sk_buff *skb;
	struct ethhdr *eh;

	while ((skb = skb_dequeue(&fnic->fip_frame_queue))) {
		spin_lock_irqsave(&fnic->fnic_lock, flags);
		if (fnic->stop_rx_link_events) {
			spin_unlock_irqrestore(&fnic->fnic_lock, flags);
			dev_kfree_skb(skb);
			return;
		}
		/*
		 * If we're in a transitional state, just re-queue and return.
		 * The queue will be serviced when we get to a stable state.
		 */
		if (fnic->state != FNIC_IN_FC_MODE &&
		    fnic->state != FNIC_IN_ETH_MODE) {
			skb_queue_head(&fnic->fip_frame_queue, skb);
			spin_unlock_irqrestore(&fnic->fnic_lock, flags);
			return;
		}
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		eh = (struct ethhdr *)skb->data;
		if (eh->h_proto == htons(ETH_P_FIP)) {
			skb_pull(skb, sizeof(*eh));
			if (fnic_fcoe_handle_fip_frame(fnic, skb) <= 0) {
				dev_kfree_skb(skb);
				continue;
			}
			/*
			 * If there's FLOGI rejects - clear all
			 * fcf's & restart from scratch
			 */
			if (is_fnic_fip_flogi_reject(&fnic->ctlr, skb)) {
				atomic64_inc(
					&fnic_stats->vlan_stats.flogi_rejects);
				shost_printk(KERN_INFO, fnic->lport->host,
					  "Trigger a Link down - VLAN Disc\n");
				fcoe_ctlr_link_down(&fnic->ctlr);
				/* start FCoE VLAN discovery */
				fnic_fcoe_send_vlan_req(fnic);
				dev_kfree_skb(skb);
				continue;
			}
			fcoe_ctlr_recv(&fnic->ctlr, skb);
			continue;
		}
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
		eh = skb_pull(skb, VLAN_HLEN);
		skb_reset_mac_header(skb);
	}
	if (eh->h_proto == htons(ETH_P_FIP)) {
		if (!(fnic->config.flags & VFCF_FIP_CAPABLE)) {
			printk(KERN_ERR "Dropped FIP frame, as firmware "
					"uses non-FIP mode, Enable FIP "
					"using UCSM\n");
			goto drop;
		}
		if ((fnic_fc_trace_set_data(fnic->lport->host->host_no,
			FNIC_FC_RECV|0x80, (char *)skb->data, skb->len)) != 0) {
			printk(KERN_ERR "fnic ctlr frame trace error!!!");
		}
		skb_queue_tail(&fnic->fip_frame_queue, skb);
		queue_work(fnic_fip_queue, &fnic->fip_frame_work);
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
	if (ether_addr_equal(data, new))
		return;
	FNIC_FCS_DBG(KERN_DEBUG, fnic->lport->host, "update_mac %pM\n", new);
	if (!is_zero_ether_addr(data) && !ether_addr_equal(data, ctl))
		vnic_dev_del_addr(fnic->vdev, data);
	memcpy(data, new, ETH_ALEN);
	if (!ether_addr_equal(new, ctl))
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
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
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

	dma_unmap_single(&fnic->pdev->dev, buf->dma_addr, buf->len,
			 DMA_FROM_DEVICE);
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
		skb_trim(skb, bytes_written);
		if (!fcs_ok) {
			atomic64_inc(&fnic_stats->misc_stats.frame_errors);
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
		atomic64_inc(&fnic_stats->misc_stats.frame_errors);
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
	if ((fnic_fc_trace_set_data(fnic->lport->host->host_no, FNIC_FC_RECV,
					(char *)skb->data, skb->len)) != 0) {
		printk(KERN_ERR "fnic ctlr frame trace error!!!");
	}

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
	struct sk_buff *skb;
	u16 len;
	dma_addr_t pa;
	int r;

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
	pa = dma_map_single(&fnic->pdev->dev, skb->data, len, DMA_FROM_DEVICE);
	if (dma_mapping_error(&fnic->pdev->dev, pa)) {
		r = -ENOMEM;
		printk(KERN_ERR "PCI mapping failed with error %d\n", r);
		goto free_skb;
	}

	fnic_queue_rq_desc(rq, skb, pa, len);
	return 0;

free_skb:
	kfree_skb(skb);
	return r;
}

void fnic_free_rq_buf(struct vnic_rq *rq, struct vnic_rq_buf *buf)
{
	struct fc_frame *fp = buf->os_buf;
	struct fnic *fnic = vnic_dev_priv(rq->vdev);

	dma_unmap_single(&fnic->pdev->dev, buf->dma_addr, buf->len,
			 DMA_FROM_DEVICE);

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
		vlan_hdr = skb_push(skb, sizeof(*vlan_hdr) - sizeof(*eth_hdr));
		memcpy(vlan_hdr, eth_hdr, 2 * ETH_ALEN);
		vlan_hdr->h_vlan_proto = htons(ETH_P_8021Q);
		vlan_hdr->h_vlan_encapsulated_proto = eth_hdr->h_proto;
		vlan_hdr->h_vlan_TCI = htons(fnic->vlan_id);
		if ((fnic_fc_trace_set_data(fnic->lport->host->host_no,
			FNIC_FC_SEND|0x80, (char *)eth_hdr, skb->len)) != 0) {
			printk(KERN_ERR "fnic ctlr frame trace error!!!");
		}
	} else {
		if ((fnic_fc_trace_set_data(fnic->lport->host->host_no,
			FNIC_FC_SEND|0x80, (char *)skb->data, skb->len)) != 0) {
			printk(KERN_ERR "fnic ctlr frame trace error!!!");
		}
	}

	pa = dma_map_single(&fnic->pdev->dev, skb->data, skb->len,
			DMA_TO_DEVICE);
	if (dma_mapping_error(&fnic->pdev->dev, pa)) {
		printk(KERN_ERR "DMA mapping failed\n");
		goto free_skb;
	}

	spin_lock_irqsave(&fnic->wq_lock[0], flags);
	if (!vnic_wq_desc_avail(wq))
		goto irq_restore;

	fnic_queue_wq_eth_desc(wq, skb, pa, skb->len,
			       0 /* hw inserts cos value */,
			       fnic->vlan_id, 1);
	spin_unlock_irqrestore(&fnic->wq_lock[0], flags);
	return;

irq_restore:
	spin_unlock_irqrestore(&fnic->wq_lock[0], flags);
	dma_unmap_single(&fnic->pdev->dev, pa, skb->len, DMA_TO_DEVICE);
free_skb:
	kfree_skb(skb);
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
		vlan_hdr = skb_push(skb, eth_hdr_len);
		eth_hdr = (struct ethhdr *)vlan_hdr;
		vlan_hdr->h_vlan_proto = htons(ETH_P_8021Q);
		vlan_hdr->h_vlan_encapsulated_proto = htons(ETH_P_FCOE);
		vlan_hdr->h_vlan_TCI = htons(fnic->vlan_id);
		fcoe_hdr = (struct fcoe_hdr *)(vlan_hdr + 1);
	} else {
		eth_hdr_len = sizeof(*eth_hdr) + sizeof(*fcoe_hdr);
		eth_hdr = skb_push(skb, eth_hdr_len);
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

	pa = dma_map_single(&fnic->pdev->dev, eth_hdr, tot_len, DMA_TO_DEVICE);
	if (dma_mapping_error(&fnic->pdev->dev, pa)) {
		ret = -ENOMEM;
		printk(KERN_ERR "DMA map failed with error %d\n", ret);
		goto free_skb_on_err;
	}

	if ((fnic_fc_trace_set_data(fnic->lport->host->host_no, FNIC_FC_SEND,
				(char *)eth_hdr, tot_len)) != 0) {
		printk(KERN_ERR "fnic ctlr frame trace error!!!");
	}

	spin_lock_irqsave(&fnic->wq_lock[0], flags);

	if (!vnic_wq_desc_avail(wq)) {
		dma_unmap_single(&fnic->pdev->dev, pa, tot_len, DMA_TO_DEVICE);
		ret = -1;
		goto irq_restore;
	}

	fnic_queue_wq_desc(wq, skb, pa, tot_len, fr_eof(fp),
			   0 /* hw inserts cos value */,
			   fnic->vlan_id, 1, 1, 1);

irq_restore:
	spin_unlock_irqrestore(&fnic->wq_lock[0], flags);

free_skb_on_err:
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

	while ((skb = skb_dequeue(&fnic->tx_queue))) {
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

	dma_unmap_single(&fnic->pdev->dev, buf->dma_addr, buf->len,
			 DMA_TO_DEVICE);
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

	dma_unmap_single(&fnic->pdev->dev, buf->dma_addr, buf->len,
			 DMA_TO_DEVICE);

	dev_kfree_skb(fp_skb(fp));
	buf->os_buf = NULL;
}

void fnic_fcoe_reset_vlans(struct fnic *fnic)
{
	unsigned long flags;
	struct fcoe_vlan *vlan;
	struct fcoe_vlan *next;

	/*
	 * indicate a link down to fcoe so that all fcf's are free'd
	 * might not be required since we did this before sending vlan
	 * discovery request
	 */
	spin_lock_irqsave(&fnic->vlans_lock, flags);
	if (!list_empty(&fnic->vlans)) {
		list_for_each_entry_safe(vlan, next, &fnic->vlans, list) {
			list_del(&vlan->list);
			kfree(vlan);
		}
	}
	spin_unlock_irqrestore(&fnic->vlans_lock, flags);
}

void fnic_handle_fip_timer(struct fnic *fnic)
{
	unsigned long flags;
	struct fcoe_vlan *vlan;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	u64 sol_time;

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	if (fnic->stop_rx_link_events) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return;
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	if (fnic->ctlr.mode == FIP_MODE_NON_FIP)
		return;

	spin_lock_irqsave(&fnic->vlans_lock, flags);
	if (list_empty(&fnic->vlans)) {
		spin_unlock_irqrestore(&fnic->vlans_lock, flags);
		/* no vlans available, try again */
		if (unlikely(fnic_log_level & FNIC_FCS_LOGGING))
			if (printk_ratelimit())
				shost_printk(KERN_DEBUG, fnic->lport->host,
						"Start VLAN Discovery\n");
		fnic_event_enq(fnic, FNIC_EVT_START_VLAN_DISC);
		return;
	}

	vlan = list_first_entry(&fnic->vlans, struct fcoe_vlan, list);
	FNIC_FCS_DBG(KERN_DEBUG, fnic->lport->host,
		  "fip_timer: vlan %d state %d sol_count %d\n",
		  vlan->vid, vlan->state, vlan->sol_count);
	switch (vlan->state) {
	case FIP_VLAN_USED:
		FNIC_FCS_DBG(KERN_DEBUG, fnic->lport->host,
			  "FIP VLAN is selected for FC transaction\n");
		spin_unlock_irqrestore(&fnic->vlans_lock, flags);
		break;
	case FIP_VLAN_FAILED:
		spin_unlock_irqrestore(&fnic->vlans_lock, flags);
		/* if all vlans are in failed state, restart vlan disc */
		if (unlikely(fnic_log_level & FNIC_FCS_LOGGING))
			if (printk_ratelimit())
				shost_printk(KERN_DEBUG, fnic->lport->host,
					  "Start VLAN Discovery\n");
		fnic_event_enq(fnic, FNIC_EVT_START_VLAN_DISC);
		break;
	case FIP_VLAN_SENT:
		if (vlan->sol_count >= FCOE_CTLR_MAX_SOL) {
			/*
			 * no response on this vlan, remove  from the list.
			 * Try the next vlan
			 */
			FNIC_FCS_DBG(KERN_INFO, fnic->lport->host,
				  "Dequeue this VLAN ID %d from list\n",
				  vlan->vid);
			list_del(&vlan->list);
			kfree(vlan);
			vlan = NULL;
			if (list_empty(&fnic->vlans)) {
				/* we exhausted all vlans, restart vlan disc */
				spin_unlock_irqrestore(&fnic->vlans_lock,
							flags);
				FNIC_FCS_DBG(KERN_INFO, fnic->lport->host,
					  "fip_timer: vlan list empty, "
					  "trigger vlan disc\n");
				fnic_event_enq(fnic, FNIC_EVT_START_VLAN_DISC);
				return;
			}
			/* check the next vlan */
			vlan = list_first_entry(&fnic->vlans, struct fcoe_vlan,
							list);
			fnic->set_vlan(fnic, vlan->vid);
			vlan->state = FIP_VLAN_SENT; /* sent now */
		}
		spin_unlock_irqrestore(&fnic->vlans_lock, flags);
		atomic64_inc(&fnic_stats->vlan_stats.sol_expiry_count);
		vlan->sol_count++;
		sol_time = jiffies + msecs_to_jiffies
					(FCOE_CTLR_START_DELAY);
		mod_timer(&fnic->fip_timer, round_jiffies(sol_time));
		break;
	}
}
