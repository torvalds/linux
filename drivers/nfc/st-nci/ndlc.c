// SPDX-License-Identifier: GPL-2.0-only
/*
 * Low Level Transport (NDLC) Driver for STMicroelectronics NFC Chip
 *
 * Copyright (C) 2014-2015  STMicroelectronics SAS. All rights reserved.
 */

#include <linux/sched.h>
#include <net/nfc/nci_core.h>

#include "st-nci.h"

#define NDLC_TIMER_T1		100
#define NDLC_TIMER_T1_WAIT	400
#define NDLC_TIMER_T2		1200

#define PCB_TYPE_DATAFRAME		0x80
#define PCB_TYPE_SUPERVISOR		0xc0
#define PCB_TYPE_MASK			PCB_TYPE_SUPERVISOR

#define PCB_SYNC_ACK			0x20
#define PCB_SYNC_NACK			0x10
#define PCB_SYNC_WAIT			0x30
#define PCB_SYNC_NOINFO			0x00
#define PCB_SYNC_MASK			PCB_SYNC_WAIT

#define PCB_DATAFRAME_RETRANSMIT_YES	0x00
#define PCB_DATAFRAME_RETRANSMIT_NO	0x04
#define PCB_DATAFRAME_RETRANSMIT_MASK	PCB_DATAFRAME_RETRANSMIT_NO

#define PCB_SUPERVISOR_RETRANSMIT_YES	0x00
#define PCB_SUPERVISOR_RETRANSMIT_NO	0x02
#define PCB_SUPERVISOR_RETRANSMIT_MASK	PCB_SUPERVISOR_RETRANSMIT_NO

#define PCB_FRAME_CRC_INFO_PRESENT	0x08
#define PCB_FRAME_CRC_INFO_NOTPRESENT	0x00
#define PCB_FRAME_CRC_INFO_MASK		PCB_FRAME_CRC_INFO_PRESENT

#define NDLC_DUMP_SKB(info, skb)                                 \
do {                                                             \
	pr_debug("%s:\n", info);                                 \
	print_hex_dump(KERN_DEBUG, "ndlc: ", DUMP_PREFIX_OFFSET, \
			16, 1, skb->data, skb->len, 0);          \
} while (0)

int ndlc_open(struct llt_ndlc *ndlc)
{
	/* toggle reset pin */
	ndlc->ops->enable(ndlc->phy_id);
	ndlc->powered = 1;
	return 0;
}
EXPORT_SYMBOL(ndlc_open);

void ndlc_close(struct llt_ndlc *ndlc)
{
	struct nci_mode_set_cmd cmd;

	cmd.cmd_type = ST_NCI_SET_NFC_MODE;
	cmd.mode = 0;

	/* toggle reset pin */
	ndlc->ops->enable(ndlc->phy_id);

	nci_prop_cmd(ndlc->ndev, ST_NCI_CORE_PROP,
		     sizeof(struct nci_mode_set_cmd), (__u8 *)&cmd);

	ndlc->powered = 0;
	ndlc->ops->disable(ndlc->phy_id);
}
EXPORT_SYMBOL(ndlc_close);

int ndlc_send(struct llt_ndlc *ndlc, struct sk_buff *skb)
{
	/* add ndlc header */
	u8 pcb = PCB_TYPE_DATAFRAME | PCB_DATAFRAME_RETRANSMIT_NO |
		PCB_FRAME_CRC_INFO_NOTPRESENT;

	*(u8 *)skb_push(skb, 1) = pcb;
	skb_queue_tail(&ndlc->send_q, skb);

	schedule_work(&ndlc->sm_work);

	return 0;
}
EXPORT_SYMBOL(ndlc_send);

static void llt_ndlc_send_queue(struct llt_ndlc *ndlc)
{
	struct sk_buff *skb;
	int r;
	unsigned long time_sent;

	if (ndlc->send_q.qlen)
		pr_debug("sendQlen=%d unackQlen=%d\n",
			 ndlc->send_q.qlen, ndlc->ack_pending_q.qlen);

	while (ndlc->send_q.qlen) {
		skb = skb_dequeue(&ndlc->send_q);
		NDLC_DUMP_SKB("ndlc frame written", skb);
		r = ndlc->ops->write(ndlc->phy_id, skb);
		if (r < 0) {
			ndlc->hard_fault = r;
			break;
		}
		time_sent = jiffies;
		*(unsigned long *)skb->cb = time_sent;

		skb_queue_tail(&ndlc->ack_pending_q, skb);

		/* start timer t1 for ndlc aknowledge */
		ndlc->t1_active = true;
		mod_timer(&ndlc->t1_timer, time_sent +
			msecs_to_jiffies(NDLC_TIMER_T1));
		/* start timer t2 for chip availability */
		ndlc->t2_active = true;
		mod_timer(&ndlc->t2_timer, time_sent +
			msecs_to_jiffies(NDLC_TIMER_T2));
	}
}

static void llt_ndlc_requeue_data_pending(struct llt_ndlc *ndlc)
{
	struct sk_buff *skb;
	u8 pcb;

	while ((skb = skb_dequeue_tail(&ndlc->ack_pending_q))) {
		pcb = skb->data[0];
		switch (pcb & PCB_TYPE_MASK) {
		case PCB_TYPE_SUPERVISOR:
			skb->data[0] = (pcb & ~PCB_SUPERVISOR_RETRANSMIT_MASK) |
				PCB_SUPERVISOR_RETRANSMIT_YES;
			break;
		case PCB_TYPE_DATAFRAME:
			skb->data[0] = (pcb & ~PCB_DATAFRAME_RETRANSMIT_MASK) |
				PCB_DATAFRAME_RETRANSMIT_YES;
			break;
		default:
			pr_err("UNKNOWN Packet Control Byte=%d\n", pcb);
			kfree_skb(skb);
			continue;
		}
		skb_queue_head(&ndlc->send_q, skb);
	}
}

static void llt_ndlc_rcv_queue(struct llt_ndlc *ndlc)
{
	struct sk_buff *skb;
	u8 pcb;
	unsigned long time_sent;

	if (ndlc->rcv_q.qlen)
		pr_debug("rcvQlen=%d\n", ndlc->rcv_q.qlen);

	while ((skb = skb_dequeue(&ndlc->rcv_q)) != NULL) {
		pcb = skb->data[0];
		skb_pull(skb, 1);
		if ((pcb & PCB_TYPE_MASK) == PCB_TYPE_SUPERVISOR) {
			switch (pcb & PCB_SYNC_MASK) {
			case PCB_SYNC_ACK:
				skb = skb_dequeue(&ndlc->ack_pending_q);
				kfree_skb(skb);
				del_timer_sync(&ndlc->t1_timer);
				del_timer_sync(&ndlc->t2_timer);
				ndlc->t2_active = false;
				ndlc->t1_active = false;
				break;
			case PCB_SYNC_NACK:
				llt_ndlc_requeue_data_pending(ndlc);
				llt_ndlc_send_queue(ndlc);
				/* start timer t1 for ndlc aknowledge */
				time_sent = jiffies;
				ndlc->t1_active = true;
				mod_timer(&ndlc->t1_timer, time_sent +
					msecs_to_jiffies(NDLC_TIMER_T1));
				break;
			case PCB_SYNC_WAIT:
				time_sent = jiffies;
				ndlc->t1_active = true;
				mod_timer(&ndlc->t1_timer, time_sent +
					  msecs_to_jiffies(NDLC_TIMER_T1_WAIT));
				break;
			default:
				kfree_skb(skb);
				break;
			}
		} else if ((pcb & PCB_TYPE_MASK) == PCB_TYPE_DATAFRAME) {
			nci_recv_frame(ndlc->ndev, skb);
		} else {
			kfree_skb(skb);
		}
	}
}

static void llt_ndlc_sm_work(struct work_struct *work)
{
	struct llt_ndlc *ndlc = container_of(work, struct llt_ndlc, sm_work);

	llt_ndlc_send_queue(ndlc);
	llt_ndlc_rcv_queue(ndlc);

	if (ndlc->t1_active && timer_pending(&ndlc->t1_timer) == 0) {
		pr_debug
		    ("Handle T1(recv SUPERVISOR) elapsed (T1 now inactive)\n");
		ndlc->t1_active = false;

		llt_ndlc_requeue_data_pending(ndlc);
		llt_ndlc_send_queue(ndlc);
	}

	if (ndlc->t2_active && timer_pending(&ndlc->t2_timer) == 0) {
		pr_debug("Handle T2(recv DATA) elapsed (T2 now inactive)\n");
		ndlc->t2_active = false;
		ndlc->t1_active = false;
		del_timer_sync(&ndlc->t1_timer);
		del_timer_sync(&ndlc->t2_timer);
		ndlc_close(ndlc);
		ndlc->hard_fault = -EREMOTEIO;
	}
}

void ndlc_recv(struct llt_ndlc *ndlc, struct sk_buff *skb)
{
	if (skb == NULL) {
		pr_err("NULL Frame -> link is dead\n");
		ndlc->hard_fault = -EREMOTEIO;
		ndlc_close(ndlc);
	} else {
		NDLC_DUMP_SKB("incoming frame", skb);
		skb_queue_tail(&ndlc->rcv_q, skb);
	}

	schedule_work(&ndlc->sm_work);
}
EXPORT_SYMBOL(ndlc_recv);

static void ndlc_t1_timeout(struct timer_list *t)
{
	struct llt_ndlc *ndlc = from_timer(ndlc, t, t1_timer);

	pr_debug("\n");

	schedule_work(&ndlc->sm_work);
}

static void ndlc_t2_timeout(struct timer_list *t)
{
	struct llt_ndlc *ndlc = from_timer(ndlc, t, t2_timer);

	pr_debug("\n");

	schedule_work(&ndlc->sm_work);
}

int ndlc_probe(void *phy_id, struct nfc_phy_ops *phy_ops, struct device *dev,
	       int phy_headroom, int phy_tailroom, struct llt_ndlc **ndlc_id,
	       struct st_nci_se_status *se_status)
{
	struct llt_ndlc *ndlc;

	ndlc = devm_kzalloc(dev, sizeof(struct llt_ndlc), GFP_KERNEL);
	if (!ndlc)
		return -ENOMEM;

	ndlc->ops = phy_ops;
	ndlc->phy_id = phy_id;
	ndlc->dev = dev;
	ndlc->powered = 0;

	*ndlc_id = ndlc;

	/* initialize timers */
	timer_setup(&ndlc->t1_timer, ndlc_t1_timeout, 0);
	timer_setup(&ndlc->t2_timer, ndlc_t2_timeout, 0);

	skb_queue_head_init(&ndlc->rcv_q);
	skb_queue_head_init(&ndlc->send_q);
	skb_queue_head_init(&ndlc->ack_pending_q);

	INIT_WORK(&ndlc->sm_work, llt_ndlc_sm_work);

	return st_nci_probe(ndlc, phy_headroom, phy_tailroom, se_status);
}
EXPORT_SYMBOL(ndlc_probe);

void ndlc_remove(struct llt_ndlc *ndlc)
{
	/* cancel timers */
	del_timer_sync(&ndlc->t1_timer);
	del_timer_sync(&ndlc->t2_timer);
	ndlc->t2_active = false;
	ndlc->t1_active = false;
	/* cancel work */
	cancel_work_sync(&ndlc->sm_work);

	st_nci_remove(ndlc->ndev);

	skb_queue_purge(&ndlc->rcv_q);
	skb_queue_purge(&ndlc->send_q);
}
EXPORT_SYMBOL(ndlc_remove);
