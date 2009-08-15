/*
 * Freescale QUICC Engine USB Host Controller Driver
 *
 * Copyright (c) Freescale Semicondutor, Inc. 2006.
 *               Shlomi Gridish <gridish@freescale.com>
 *               Jerry Huang <Chang-Ming.Huang@freescale.com>
 * Copyright (c) Logic Product Development, Inc. 2007
 *               Peter Barada <peterb@logicpd.com>
 * Copyright (c) MontaVista Software, Inc. 2008.
 *               Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/usb.h>
#include <asm/qe.h>
#include <asm/fsl_gtm.h>
#include "../core/hcd.h"
#include "fhci.h"

static void recycle_frame(struct fhci_usb *usb, struct packet *pkt)
{
	pkt->data = NULL;
	pkt->len = 0;
	pkt->status = USB_TD_OK;
	pkt->info = 0;
	pkt->priv_data = NULL;

	cq_put(usb->ep0->empty_frame_Q, pkt);
}

/* confirm submitted packet */
void fhci_transaction_confirm(struct fhci_usb *usb, struct packet *pkt)
{
	struct td *td;
	struct packet *td_pkt;
	struct ed *ed;
	u32 trans_len;
	bool td_done = false;

	td = fhci_remove_td_from_frame(usb->actual_frame);
	td_pkt = td->pkt;
	trans_len = pkt->len;
	td->status = pkt->status;
	if (td->type == FHCI_TA_IN && td_pkt->info & PKT_DUMMY_PACKET) {
		if ((td->data + td->actual_len) && trans_len)
			memcpy(td->data + td->actual_len, pkt->data,
			       trans_len);
		cq_put(usb->ep0->dummy_packets_Q, pkt->data);
	}

	recycle_frame(usb, pkt);

	ed = td->ed;
	if (ed->mode == FHCI_TF_ISO) {
		if (ed->td_list.next->next != &ed->td_list) {
			struct td *td_next =
			    list_entry(ed->td_list.next->next, struct td,
				       node);

			td_next->start_frame = usb->actual_frame->frame_num;
		}
		td->actual_len = trans_len;
		td_done = true;
	} else if ((td->status & USB_TD_ERROR) &&
			!(td->status & USB_TD_TX_ER_NAK)) {
		/*
		 * There was an error on the transaction (but not NAK).
		 * If it is fatal error (data underrun, stall, bad pid or 3
		 * errors exceeded), mark this TD as done.
		 */
		if ((td->status & USB_TD_RX_DATA_UNDERUN) ||
				(td->status & USB_TD_TX_ER_STALL) ||
				(td->status & USB_TD_RX_ER_PID) ||
				(++td->error_cnt >= 3)) {
			ed->state = FHCI_ED_HALTED;
			td_done = true;

			if (td->status & USB_TD_RX_DATA_UNDERUN) {
				fhci_dbg(usb->fhci, "td err fu\n");
				td->toggle = !td->toggle;
				td->actual_len += trans_len;
			} else {
				fhci_dbg(usb->fhci, "td err f!u\n");
			}
		} else {
			fhci_dbg(usb->fhci, "td err !f\n");
			/* it is not a fatal error -retry this transaction */
			td->nak_cnt = 0;
			td->error_cnt++;
			td->status = USB_TD_OK;
		}
	} else if (td->status & USB_TD_TX_ER_NAK) {
		/* there was a NAK response */
		fhci_vdbg(usb->fhci, "td nack\n");
		td->nak_cnt++;
		td->error_cnt = 0;
		td->status = USB_TD_OK;
	} else {
		/* there was no error on transaction */
		td->error_cnt = 0;
		td->nak_cnt = 0;
		td->toggle = !td->toggle;
		td->actual_len += trans_len;

		if (td->len == td->actual_len)
			td_done = true;
	}

	if (td_done)
		fhci_move_td_from_ed_to_done_list(usb, ed);
}

/*
 * Flush all transmitted packets from BDs
 * This routine is called when disabling the USB port to flush all
 * transmissions that are allready scheduled in the BDs
 */
void fhci_flush_all_transmissions(struct fhci_usb *usb)
{
	u8 mode;
	struct td *td;

	mode = in_8(&usb->fhci->regs->usb_mod);
	clrbits8(&usb->fhci->regs->usb_mod, USB_MODE_EN);

	fhci_flush_bds(usb);

	while ((td = fhci_peek_td_from_frame(usb->actual_frame)) != NULL) {
		struct packet *pkt = td->pkt;

		pkt->status = USB_TD_TX_ER_TIMEOUT;
		fhci_transaction_confirm(usb, pkt);
	}

	usb->actual_frame->frame_status = FRAME_END_TRANSMISSION;

	/* reset the event register */
	out_be16(&usb->fhci->regs->usb_event, 0xffff);
	/* enable the USB controller */
	out_8(&usb->fhci->regs->usb_mod, mode | USB_MODE_EN);
}

/*
 * This function forms the packet and transmit the packet. This function
 * will handle all endpoint type:ISO,interrupt,control and bulk
 */
static int add_packet(struct fhci_usb *usb, struct ed *ed, struct td *td)
{
	u32 fw_transaction_time, len = 0;
	struct packet *pkt;
	u8 *data = NULL;

	/* calcalate data address,len and toggle and then add the transaction */
	if (td->toggle == USB_TD_TOGGLE_CARRY)
		td->toggle = ed->toggle_carry;

	switch (ed->mode) {
	case FHCI_TF_ISO:
		len = td->len;
		if (td->type != FHCI_TA_IN)
			data = td->data;
		break;
	case FHCI_TF_CTRL:
	case FHCI_TF_BULK:
		len = min(td->len - td->actual_len, ed->max_pkt_size);
		if (!((td->type == FHCI_TA_IN) &&
		      ((len + td->actual_len) == td->len)))
			data = td->data + td->actual_len;
		break;
	case FHCI_TF_INTR:
		len = min(td->len, ed->max_pkt_size);
		if (!((td->type == FHCI_TA_IN) &&
		      ((td->len + CRC_SIZE) >= ed->max_pkt_size)))
			data = td->data;
		break;
	default:
		break;
	}

	if (usb->port_status == FHCI_PORT_FULL)
		fw_transaction_time = (((len + PROTOCOL_OVERHEAD) * 11) >> 4);
	else
		fw_transaction_time = ((len + PROTOCOL_OVERHEAD) * 6);

	/* check if there's enough space in this frame to submit this TD */
	if (usb->actual_frame->total_bytes + len + PROTOCOL_OVERHEAD >=
			usb->max_bytes_per_frame) {
		fhci_vdbg(usb->fhci, "not enough space in this frame: "
			  "%d %d %d\n", usb->actual_frame->total_bytes, len,
			  usb->max_bytes_per_frame);
		return -1;
	}

	/* check if there's enough time in this frame to submit this TD */
	if (usb->actual_frame->frame_status != FRAME_IS_PREPARED &&
	    (usb->actual_frame->frame_status & FRAME_END_TRANSMISSION ||
	     (fw_transaction_time + usb->sw_transaction_time >=
	      1000 - fhci_get_sof_timer_count(usb)))) {
		fhci_dbg(usb->fhci, "not enough time in this frame\n");
		return -1;
	}

	/* update frame object fields before transmitting */
	pkt = cq_get(usb->ep0->empty_frame_Q);
	if (!pkt) {
		fhci_dbg(usb->fhci, "there is no empty frame\n");
		return -1;
	}
	td->pkt = pkt;

	pkt->info = 0;
	if (data == NULL) {
		data = cq_get(usb->ep0->dummy_packets_Q);
		BUG_ON(!data);
		pkt->info = PKT_DUMMY_PACKET;
	}
	pkt->data = data;
	pkt->len = len;
	pkt->status = USB_TD_OK;
	/* update TD status field before transmitting */
	td->status = USB_TD_INPROGRESS;
	/* update actual frame time object with the actual transmission */
	usb->actual_frame->total_bytes += (len + PROTOCOL_OVERHEAD);
	fhci_add_td_to_frame(usb->actual_frame, td);

	if (usb->port_status != FHCI_PORT_FULL &&
			usb->port_status != FHCI_PORT_LOW) {
		pkt->status = USB_TD_TX_ER_TIMEOUT;
		pkt->len = 0;
		fhci_transaction_confirm(usb, pkt);
	} else if (fhci_host_transaction(usb, pkt, td->type, ed->dev_addr,
			ed->ep_addr, ed->mode, ed->speed, td->toggle)) {
		/* remove TD from actual frame */
		list_del_init(&td->frame_lh);
		td->status = USB_TD_OK;
		if (pkt->info & PKT_DUMMY_PACKET)
			cq_put(usb->ep0->dummy_packets_Q, pkt->data);
		recycle_frame(usb, pkt);
		usb->actual_frame->total_bytes -= (len + PROTOCOL_OVERHEAD);
		fhci_err(usb->fhci, "host transaction failed\n");
		return -1;
	}

	return len;
}

static void move_head_to_tail(struct list_head *list)
{
	struct list_head *node = list->next;

	if (!list_empty(list)) {
		list_del(node);
		list_add_tail(node, list);
	}
}

/*
 * This function goes through the endpoint list and schedules the
 * transactions within this list
 */
static int scan_ed_list(struct fhci_usb *usb,
			struct list_head *list, enum fhci_tf_mode list_type)
{
	static const int frame_part[4] = {
		[FHCI_TF_CTRL] = MAX_BYTES_PER_FRAME,
		[FHCI_TF_ISO] = (MAX_BYTES_PER_FRAME *
				 MAX_PERIODIC_FRAME_USAGE) / 100,
		[FHCI_TF_BULK] = MAX_BYTES_PER_FRAME,
		[FHCI_TF_INTR] = (MAX_BYTES_PER_FRAME *
				  MAX_PERIODIC_FRAME_USAGE) / 100
	};
	struct ed *ed;
	struct td *td;
	int ans = 1;
	u32 save_transaction_time = usb->sw_transaction_time;

	list_for_each_entry(ed, list, node) {
		td = ed->td_head;

		if (!td || (td && td->status == USB_TD_INPROGRESS))
			continue;

		if (ed->state != FHCI_ED_OPER) {
			if (ed->state == FHCI_ED_URB_DEL) {
				td->status = USB_TD_OK;
				fhci_move_td_from_ed_to_done_list(usb, ed);
				ed->state = FHCI_ED_SKIP;
			}
			continue;
		}

		/*
		 * if it isn't interrupt pipe or it is not iso pipe and the
		 * interval time passed
		 */
		if ((list_type == FHCI_TF_INTR || list_type == FHCI_TF_ISO) &&
				(((usb->actual_frame->frame_num -
				   td->start_frame) & 0x7ff) < td->interval))
			continue;

		if (add_packet(usb, ed, td) < 0)
			continue;

		/* update time stamps in the TD */
		td->start_frame = usb->actual_frame->frame_num;
		usb->sw_transaction_time += save_transaction_time;

		if (usb->actual_frame->total_bytes >=
					usb->max_bytes_per_frame) {
			usb->actual_frame->frame_status =
				FRAME_DATA_END_TRANSMISSION;
			fhci_push_dummy_bd(usb->ep0);
			ans = 0;
			break;
		}

		if (usb->actual_frame->total_bytes >= frame_part[list_type])
			break;
	}

	/* be fair to each ED(move list head around) */
	move_head_to_tail(list);
	usb->sw_transaction_time = save_transaction_time;

	return ans;
}

static u32 rotate_frames(struct fhci_usb *usb)
{
	struct fhci_hcd *fhci = usb->fhci;

	if (!list_empty(&usb->actual_frame->tds_list)) {
		if ((((in_be16(&fhci->pram->frame_num) & 0x07ff) -
		      usb->actual_frame->frame_num) & 0x7ff) > 5)
			fhci_flush_actual_frame(usb);
		else
			return -EINVAL;
	}

	usb->actual_frame->frame_status = FRAME_IS_PREPARED;
	usb->actual_frame->frame_num = in_be16(&fhci->pram->frame_num) & 0x7ff;
	usb->actual_frame->total_bytes = 0;

	return 0;
}

/*
 * This function schedule the USB transaction and will process the
 * endpoint in the following order: iso, interrupt, control and bulk.
 */
void fhci_schedule_transactions(struct fhci_usb *usb)
{
	int left = 1;

	if (usb->actual_frame->frame_status & FRAME_END_TRANSMISSION)
		if (rotate_frames(usb) != 0)
			return;

	if (usb->actual_frame->frame_status & FRAME_END_TRANSMISSION)
		return;

	if (usb->actual_frame->total_bytes == 0) {
		/*
		 * schedule the next available ISO transfer
		 *or next stage of the ISO transfer
		 */
		scan_ed_list(usb, &usb->hc_list->iso_list, FHCI_TF_ISO);

		/*
		 * schedule the next available interrupt transfer or
		 * the next stage of the interrupt transfer
		 */
		scan_ed_list(usb, &usb->hc_list->intr_list, FHCI_TF_INTR);

		/*
		 * schedule the next available control transfer
		 * or the next stage of the control transfer
		 */
		left = scan_ed_list(usb, &usb->hc_list->ctrl_list,
				    FHCI_TF_CTRL);
	}

	/*
	 * schedule the next available bulk transfer or the next stage of the
	 * bulk transfer
	 */
	if (left > 0)
		scan_ed_list(usb, &usb->hc_list->bulk_list, FHCI_TF_BULK);
}

/* Handles SOF interrupt */
static void sof_interrupt(struct fhci_hcd *fhci)
{
	struct fhci_usb *usb = fhci->usb_lld;

	if ((usb->port_status == FHCI_PORT_DISABLED) &&
	    (usb->vroot_hub->port.wPortStatus & USB_PORT_STAT_CONNECTION) &&
	    !(usb->vroot_hub->port.wPortChange & USB_PORT_STAT_C_CONNECTION)) {
		if (usb->vroot_hub->port.wPortStatus & USB_PORT_STAT_LOW_SPEED)
			usb->port_status = FHCI_PORT_LOW;
		else
			usb->port_status = FHCI_PORT_FULL;
		/* Disable IDLE */
		usb->saved_msk &= ~USB_E_IDLE_MASK;
		out_be16(&usb->fhci->regs->usb_mask, usb->saved_msk);
	}

	gtm_set_exact_timer16(fhci->timer, usb->max_frame_usage, false);

	fhci_host_transmit_actual_frame(usb);
	usb->actual_frame->frame_status = FRAME_IS_TRANSMITTED;

	fhci_schedule_transactions(usb);
}

/* Handles device disconnected interrupt on port */
void fhci_device_disconnected_interrupt(struct fhci_hcd *fhci)
{
	struct fhci_usb *usb = fhci->usb_lld;

	fhci_dbg(fhci, "-> %s\n", __func__);

	fhci_usb_disable_interrupt(usb);
	clrbits8(&usb->fhci->regs->usb_mod, USB_MODE_LSS);
	usb->port_status = FHCI_PORT_DISABLED;

	fhci_stop_sof_timer(fhci);

	/* Enable IDLE since we want to know if something comes along */
	usb->saved_msk |= USB_E_IDLE_MASK;
	out_be16(&usb->fhci->regs->usb_mask, usb->saved_msk);

	usb->vroot_hub->port.wPortStatus &= ~USB_PORT_STAT_CONNECTION;
	usb->vroot_hub->port.wPortChange |= USB_PORT_STAT_C_CONNECTION;
	usb->max_bytes_per_frame = 0;
	fhci_usb_enable_interrupt(usb);

	fhci_dbg(fhci, "<- %s\n", __func__);
}

/* detect a new device connected on the USB port */
void fhci_device_connected_interrupt(struct fhci_hcd *fhci)
{

	struct fhci_usb *usb = fhci->usb_lld;
	int state;
	int ret;

	fhci_dbg(fhci, "-> %s\n", __func__);

	fhci_usb_disable_interrupt(usb);
	state = fhci_ioports_check_bus_state(fhci);

	/* low-speed device was connected to the USB port */
	if (state == 1) {
		ret = qe_usb_clock_set(fhci->lowspeed_clk, USB_CLOCK >> 3);
		if (ret) {
			fhci_warn(fhci, "Low-Speed device is not supported, "
				  "try use BRGx\n");
			goto out;
		}

		usb->port_status = FHCI_PORT_LOW;
		setbits8(&usb->fhci->regs->usb_mod, USB_MODE_LSS);
		usb->vroot_hub->port.wPortStatus |=
		    (USB_PORT_STAT_LOW_SPEED |
		     USB_PORT_STAT_CONNECTION);
		usb->vroot_hub->port.wPortChange |=
		    USB_PORT_STAT_C_CONNECTION;
		usb->max_bytes_per_frame =
		    (MAX_BYTES_PER_FRAME >> 3) - 7;
		fhci_port_enable(usb);
	} else if (state == 2) {
		ret = qe_usb_clock_set(fhci->fullspeed_clk, USB_CLOCK);
		if (ret) {
			fhci_warn(fhci, "Full-Speed device is not supported, "
				  "try use CLKx\n");
			goto out;
		}

		usb->port_status = FHCI_PORT_FULL;
		clrbits8(&usb->fhci->regs->usb_mod, USB_MODE_LSS);
		usb->vroot_hub->port.wPortStatus &=
		    ~USB_PORT_STAT_LOW_SPEED;
		usb->vroot_hub->port.wPortStatus |=
		    USB_PORT_STAT_CONNECTION;
		usb->vroot_hub->port.wPortChange |=
		    USB_PORT_STAT_C_CONNECTION;
		usb->max_bytes_per_frame = (MAX_BYTES_PER_FRAME - 15);
		fhci_port_enable(usb);
	}
out:
	fhci_usb_enable_interrupt(usb);
	fhci_dbg(fhci, "<- %s\n", __func__);
}

irqreturn_t fhci_frame_limit_timer_irq(int irq, void *_hcd)
{
	struct usb_hcd *hcd = _hcd;
	struct fhci_hcd *fhci = hcd_to_fhci(hcd);
	struct fhci_usb *usb = fhci->usb_lld;

	spin_lock(&fhci->lock);

	gtm_set_exact_timer16(fhci->timer, 1000, false);

	if (usb->actual_frame->frame_status == FRAME_IS_TRANSMITTED) {
		usb->actual_frame->frame_status = FRAME_TIMER_END_TRANSMISSION;
		fhci_push_dummy_bd(usb->ep0);
	}

	fhci_schedule_transactions(usb);

	spin_unlock(&fhci->lock);

	return IRQ_HANDLED;
}

/* Cancel transmission on the USB endpoint */
static void abort_transmission(struct fhci_usb *usb)
{
	fhci_dbg(usb->fhci, "-> %s\n", __func__);
	/* issue stop Tx command */
	qe_issue_cmd(QE_USB_STOP_TX, QE_CR_SUBBLOCK_USB, EP_ZERO, 0);
	/* flush Tx FIFOs */
	out_8(&usb->fhci->regs->usb_comm, USB_CMD_FLUSH_FIFO | EP_ZERO);
	udelay(1000);
	/* reset Tx BDs */
	fhci_flush_bds(usb);
	/* issue restart Tx command */
	qe_issue_cmd(QE_USB_RESTART_TX, QE_CR_SUBBLOCK_USB, EP_ZERO, 0);
	fhci_dbg(usb->fhci, "<- %s\n", __func__);
}

irqreturn_t fhci_irq(struct usb_hcd *hcd)
{
	struct fhci_hcd *fhci = hcd_to_fhci(hcd);
	struct fhci_usb *usb;
	u16 usb_er = 0;
	unsigned long flags;

	spin_lock_irqsave(&fhci->lock, flags);

	usb = fhci->usb_lld;

	usb_er |= in_be16(&usb->fhci->regs->usb_event) &
		  in_be16(&usb->fhci->regs->usb_mask);

	/* clear event bits for next time */
	out_be16(&usb->fhci->regs->usb_event, usb_er);

	fhci_dbg_isr(fhci, usb_er);

	if (usb_er & USB_E_RESET_MASK) {
		if ((usb->port_status == FHCI_PORT_FULL) ||
				(usb->port_status == FHCI_PORT_LOW)) {
			fhci_device_disconnected_interrupt(fhci);
			usb_er &= ~USB_E_IDLE_MASK;
		} else if (usb->port_status == FHCI_PORT_WAITING) {
			usb->port_status = FHCI_PORT_DISCONNECTING;

			/* Turn on IDLE since we want to disconnect */
			usb->saved_msk |= USB_E_IDLE_MASK;
			out_be16(&usb->fhci->regs->usb_event,
				 usb->saved_msk);
		} else if (usb->port_status == FHCI_PORT_DISABLED) {
			if (fhci_ioports_check_bus_state(fhci) == 1)
				fhci_device_connected_interrupt(fhci);
		}
		usb_er &= ~USB_E_RESET_MASK;
	}

	if (usb_er & USB_E_MSF_MASK) {
		abort_transmission(fhci->usb_lld);
		usb_er &= ~USB_E_MSF_MASK;
	}

	if (usb_er & (USB_E_SOF_MASK | USB_E_SFT_MASK)) {
		sof_interrupt(fhci);
		usb_er &= ~(USB_E_SOF_MASK | USB_E_SFT_MASK);
	}

	if (usb_er & USB_E_TXB_MASK) {
		fhci_tx_conf_interrupt(fhci->usb_lld);
		usb_er &= ~USB_E_TXB_MASK;
	}

	if (usb_er & USB_E_TXE1_MASK) {
		fhci_tx_conf_interrupt(fhci->usb_lld);
		usb_er &= ~USB_E_TXE1_MASK;
	}

	if (usb_er & USB_E_IDLE_MASK) {
		if (usb->port_status == FHCI_PORT_DISABLED) {
			usb_er &= ~USB_E_RESET_MASK;
			fhci_device_connected_interrupt(fhci);
		} else if (usb->port_status ==
				FHCI_PORT_DISCONNECTING) {
			/* XXX usb->port_status = FHCI_PORT_WAITING; */
			/* Disable IDLE */
			usb->saved_msk &= ~USB_E_IDLE_MASK;
			out_be16(&usb->fhci->regs->usb_mask,
				 usb->saved_msk);
		} else {
			fhci_dbg_isr(fhci, -1);
		}

		usb_er &= ~USB_E_IDLE_MASK;
	}

	spin_unlock_irqrestore(&fhci->lock, flags);

	return IRQ_HANDLED;
}


/*
 * Process normal completions(error or sucess) and clean the schedule.
 *
 * This is the main path for handing urbs back to drivers. The only other patth
 * is process_del_list(),which unlinks URBs by scanning EDs,instead of scanning
 * the (re-reversed) done list as this does.
 */
static void process_done_list(unsigned long data)
{
	struct urb *urb;
	struct ed *ed;
	struct td *td;
	struct urb_priv *urb_priv;
	struct fhci_hcd *fhci = (struct fhci_hcd *)data;

	disable_irq(fhci->timer->irq);
	disable_irq(fhci_to_hcd(fhci)->irq);
	spin_lock(&fhci->lock);

	td = fhci_remove_td_from_done_list(fhci->hc_list);
	while (td != NULL) {
		urb = td->urb;
		urb_priv = urb->hcpriv;
		ed = td->ed;

		/* update URB's length and status from TD */
		fhci_done_td(urb, td);
		urb_priv->tds_cnt++;

		/*
		 * if all this urb's TDs are done, call complete()
		 * Interrupt transfers are the onley special case:
		 * they are reissued,until "deleted" by usb_unlink_urb
		 * (real work done in a SOF intr, by process_del_list)
		 */
		if (urb_priv->tds_cnt == urb_priv->num_of_tds) {
			fhci_urb_complete_free(fhci, urb);
		} else if (urb_priv->state == URB_DEL &&
				ed->state == FHCI_ED_SKIP) {
			fhci_del_ed_list(fhci, ed);
			ed->state = FHCI_ED_OPER;
		} else if (ed->state == FHCI_ED_HALTED) {
			urb_priv->state = URB_DEL;
			ed->state = FHCI_ED_URB_DEL;
			fhci_del_ed_list(fhci, ed);
			ed->state = FHCI_ED_OPER;
		}

		td = fhci_remove_td_from_done_list(fhci->hc_list);
	}

	spin_unlock(&fhci->lock);
	enable_irq(fhci->timer->irq);
	enable_irq(fhci_to_hcd(fhci)->irq);
}

DECLARE_TASKLET(fhci_tasklet, process_done_list, 0);

/* transfer complted callback */
u32 fhci_transfer_confirm_callback(struct fhci_hcd *fhci)
{
	if (!fhci->process_done_task->state)
		tasklet_schedule(fhci->process_done_task);
	return 0;
}

/*
 * adds urb to the endpoint descriptor list
 * arguments:
 * fhci		data structure for the Low level host controller
 * ep		USB Host endpoint data structure
 * urb		USB request block data structure
 */
void fhci_queue_urb(struct fhci_hcd *fhci, struct urb *urb)
{
	struct ed *ed = urb->ep->hcpriv;
	struct urb_priv *urb_priv = urb->hcpriv;
	u32 data_len = urb->transfer_buffer_length;
	int urb_state = 0;
	int toggle = 0;
	struct td *td;
	u8 *data;
	u16 cnt = 0;

	if (ed == NULL) {
		ed = fhci_get_empty_ed(fhci);
		ed->dev_addr = usb_pipedevice(urb->pipe);
		ed->ep_addr = usb_pipeendpoint(urb->pipe);
		switch (usb_pipetype(urb->pipe)) {
		case PIPE_CONTROL:
			ed->mode = FHCI_TF_CTRL;
			break;
		case PIPE_BULK:
			ed->mode = FHCI_TF_BULK;
			break;
		case PIPE_INTERRUPT:
			ed->mode = FHCI_TF_INTR;
			break;
		case PIPE_ISOCHRONOUS:
			ed->mode = FHCI_TF_ISO;
			break;
		default:
			break;
		}
		ed->speed = (urb->dev->speed == USB_SPEED_LOW) ?
			FHCI_LOW_SPEED : FHCI_FULL_SPEED;
		ed->max_pkt_size = usb_maxpacket(urb->dev,
			urb->pipe, usb_pipeout(urb->pipe));
		urb->ep->hcpriv = ed;
		fhci_dbg(fhci, "new ep speed=%d max_pkt_size=%d\n",
			 ed->speed, ed->max_pkt_size);
	}

	/* for ISO transfer calculate start frame index */
	if (ed->mode == FHCI_TF_ISO && urb->transfer_flags & URB_ISO_ASAP)
		urb->start_frame = ed->td_head ? ed->last_iso + 1 :
						 get_frame_num(fhci);

	/*
	 * OHCI handles the DATA toggle itself,we just use the USB
	 * toggle bits
	 */
	if (usb_gettoggle(urb->dev, usb_pipeendpoint(urb->pipe),
			  usb_pipeout(urb->pipe)))
		toggle = USB_TD_TOGGLE_CARRY;
	else {
		toggle = USB_TD_TOGGLE_DATA0;
		usb_settoggle(urb->dev, usb_pipeendpoint(urb->pipe),
			      usb_pipeout(urb->pipe), 1);
	}

	urb_priv->tds_cnt = 0;
	urb_priv->ed = ed;
	if (data_len > 0)
		data = urb->transfer_buffer;
	else
		data = NULL;

	switch (ed->mode) {
	case FHCI_TF_BULK:
		if (urb->transfer_flags & URB_ZERO_PACKET &&
				urb->transfer_buffer_length > 0 &&
				((urb->transfer_buffer_length %
				usb_maxpacket(urb->dev, urb->pipe,
				usb_pipeout(urb->pipe))) == 0))
			urb_state = US_BULK0;
		while (data_len > 4096) {
			td = fhci_td_fill(fhci, urb, urb_priv, ed, cnt,
				usb_pipeout(urb->pipe) ? FHCI_TA_OUT :
							 FHCI_TA_IN,
				cnt ? USB_TD_TOGGLE_CARRY :
				      toggle,
				data, 4096, 0, 0, true);
			data += 4096;
			data_len -= 4096;
			cnt++;
		}

		td = fhci_td_fill(fhci, urb, urb_priv, ed, cnt,
			usb_pipeout(urb->pipe) ? FHCI_TA_OUT : FHCI_TA_IN,
			cnt ? USB_TD_TOGGLE_CARRY : toggle,
			data, data_len, 0, 0, true);
		cnt++;

		if (urb->transfer_flags & URB_ZERO_PACKET &&
				cnt < urb_priv->num_of_tds) {
			td = fhci_td_fill(fhci, urb, urb_priv, ed, cnt,
				usb_pipeout(urb->pipe) ? FHCI_TA_OUT :
							 FHCI_TA_IN,
				USB_TD_TOGGLE_CARRY, NULL, 0, 0, 0, true);
			cnt++;
		}
		break;
	case FHCI_TF_INTR:
		urb->start_frame = get_frame_num(fhci) + 1;
		td = fhci_td_fill(fhci, urb, urb_priv, ed, cnt++,
			usb_pipeout(urb->pipe) ? FHCI_TA_OUT : FHCI_TA_IN,
			USB_TD_TOGGLE_DATA0, data, data_len,
			urb->interval, urb->start_frame, true);
		break;
	case FHCI_TF_CTRL:
		ed->dev_addr = usb_pipedevice(urb->pipe);
		ed->max_pkt_size = usb_maxpacket(urb->dev, urb->pipe,
			usb_pipeout(urb->pipe));
		td = fhci_td_fill(fhci, urb, urb_priv, ed, cnt++, FHCI_TA_SETUP,
			USB_TD_TOGGLE_DATA0, urb->setup_packet, 8, 0, 0, true);

		if (data_len > 0) {
			td = fhci_td_fill(fhci, urb, urb_priv, ed, cnt++,
				usb_pipeout(urb->pipe) ? FHCI_TA_OUT :
							 FHCI_TA_IN,
				USB_TD_TOGGLE_DATA1, data, data_len, 0, 0,
				true);
		}
		td = fhci_td_fill(fhci, urb, urb_priv, ed, cnt++,
			usb_pipeout(urb->pipe) ? FHCI_TA_IN : FHCI_TA_OUT,
			USB_TD_TOGGLE_DATA1, data, 0, 0, 0, true);
		urb_state = US_CTRL_SETUP;
		break;
	case FHCI_TF_ISO:
		for (cnt = 0; cnt < urb->number_of_packets; cnt++) {
			u16 frame = urb->start_frame;

			/*
			 * FIXME scheduling should handle frame counter
			 * roll-around ... exotic case (and OHCI has
			 * a 2^16 iso range, vs other HCs max of 2^10)
			 */
			frame += cnt * urb->interval;
			frame &= 0x07ff;
			td = fhci_td_fill(fhci, urb, urb_priv, ed, cnt,
				usb_pipeout(urb->pipe) ? FHCI_TA_OUT :
							 FHCI_TA_IN,
				USB_TD_TOGGLE_DATA0,
				data + urb->iso_frame_desc[cnt].offset,
				urb->iso_frame_desc[cnt].length,
				urb->interval, frame, true);
		}
		break;
	default:
		break;
	}

	/*
	 * set the state of URB
	 * control pipe:3 states -- setup,data,status
	 * interrupt and bulk pipe:1 state -- data
	 */
	urb->pipe &= ~0x1f;
	urb->pipe |= urb_state & 0x1f;

	urb_priv->state = URB_INPROGRESS;

	if (!ed->td_head) {
		ed->state = FHCI_ED_OPER;
		switch (ed->mode) {
		case FHCI_TF_CTRL:
			list_add(&ed->node, &fhci->hc_list->ctrl_list);
			break;
		case FHCI_TF_BULK:
			list_add(&ed->node, &fhci->hc_list->bulk_list);
			break;
		case FHCI_TF_INTR:
			list_add(&ed->node, &fhci->hc_list->intr_list);
			break;
		case FHCI_TF_ISO:
			list_add(&ed->node, &fhci->hc_list->iso_list);
			break;
		default:
			break;
		}
	}

	fhci_add_tds_to_ed(ed, urb_priv->tds, urb_priv->num_of_tds);
	fhci->active_urbs++;
}
