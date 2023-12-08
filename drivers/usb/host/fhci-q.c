// SPDX-License-Identifier: GPL-2.0+
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
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include "fhci.h"

/* maps the hardware error code to the USB error code */
static int status_to_error(u32 status)
{
	if (status == USB_TD_OK)
		return 0;
	else if (status & USB_TD_RX_ER_CRC)
		return -EILSEQ;
	else if (status & USB_TD_RX_ER_NONOCT)
		return -EPROTO;
	else if (status & USB_TD_RX_ER_OVERUN)
		return -ECOMM;
	else if (status & USB_TD_RX_ER_BITSTUFF)
		return -EPROTO;
	else if (status & USB_TD_RX_ER_PID)
		return -EILSEQ;
	else if (status & (USB_TD_TX_ER_NAK | USB_TD_TX_ER_TIMEOUT))
		return -ETIMEDOUT;
	else if (status & USB_TD_TX_ER_STALL)
		return -EPIPE;
	else if (status & USB_TD_TX_ER_UNDERUN)
		return -ENOSR;
	else if (status & USB_TD_RX_DATA_UNDERUN)
		return -EREMOTEIO;
	else if (status & USB_TD_RX_DATA_OVERUN)
		return -EOVERFLOW;
	else
		return -EINVAL;
}

void fhci_add_td_to_frame(struct fhci_time_frame *frame, struct td *td)
{
	list_add_tail(&td->frame_lh, &frame->tds_list);
}

void fhci_add_tds_to_ed(struct ed *ed, struct td **td_list, int number)
{
	int i;

	for (i = 0; i < number; i++) {
		struct td *td = td_list[i];
		list_add_tail(&td->node, &ed->td_list);
	}
	if (ed->td_head == NULL)
		ed->td_head = td_list[0];
}

static struct td *peek_td_from_ed(struct ed *ed)
{
	struct td *td;

	if (!list_empty(&ed->td_list))
		td = list_entry(ed->td_list.next, struct td, node);
	else
		td = NULL;

	return td;
}

struct td *fhci_remove_td_from_frame(struct fhci_time_frame *frame)
{
	struct td *td;

	if (!list_empty(&frame->tds_list)) {
		td = list_entry(frame->tds_list.next, struct td, frame_lh);
		list_del_init(frame->tds_list.next);
	} else
		td = NULL;

	return td;
}

struct td *fhci_peek_td_from_frame(struct fhci_time_frame *frame)
{
	struct td *td;

	if (!list_empty(&frame->tds_list))
		td = list_entry(frame->tds_list.next, struct td, frame_lh);
	else
		td = NULL;

	return td;
}

struct td *fhci_remove_td_from_ed(struct ed *ed)
{
	struct td *td;

	if (!list_empty(&ed->td_list)) {
		td = list_entry(ed->td_list.next, struct td, node);
		list_del_init(ed->td_list.next);

		/* if this TD was the ED's head, find next TD */
		if (!list_empty(&ed->td_list))
			ed->td_head = list_entry(ed->td_list.next, struct td,
						 node);
		else
			ed->td_head = NULL;
	} else
		td = NULL;

	return td;
}

struct td *fhci_remove_td_from_done_list(struct fhci_controller_list *p_list)
{
	struct td *td;

	if (!list_empty(&p_list->done_list)) {
		td = list_entry(p_list->done_list.next, struct td, node);
		list_del_init(p_list->done_list.next);
	} else
		td = NULL;

	return td;
}

void fhci_move_td_from_ed_to_done_list(struct fhci_usb *usb, struct ed *ed)
{
	struct td *td;

	td = ed->td_head;
	list_del_init(&td->node);

	/* If this TD was the ED's head,find next TD */
	if (!list_empty(&ed->td_list))
		ed->td_head = list_entry(ed->td_list.next, struct td, node);
	else {
		ed->td_head = NULL;
		ed->state = FHCI_ED_SKIP;
	}
	ed->toggle_carry = td->toggle;
	list_add_tail(&td->node, &usb->hc_list->done_list);
	if (td->ioc)
		usb->transfer_confirm(usb->fhci);
}

/* free done FHCI URB resource such as ED and TD */
static void free_urb_priv(struct fhci_hcd *fhci, struct urb *urb)
{
	int i;
	struct urb_priv *urb_priv = urb->hcpriv;
	struct ed *ed = urb_priv->ed;

	for (i = 0; i < urb_priv->num_of_tds; i++) {
		list_del_init(&urb_priv->tds[i]->node);
		fhci_recycle_empty_td(fhci, urb_priv->tds[i]);
	}

	/* if this TD was the ED's head,find the next TD */
	if (!list_empty(&ed->td_list))
		ed->td_head = list_entry(ed->td_list.next, struct td, node);
	else
		ed->td_head = NULL;

	kfree(urb_priv->tds);
	kfree(urb_priv);
	urb->hcpriv = NULL;

	/* if this TD was the ED's head,find next TD */
	if (ed->td_head == NULL)
		list_del_init(&ed->node);
	fhci->active_urbs--;
}

/* this routine called to complete and free done URB */
void fhci_urb_complete_free(struct fhci_hcd *fhci, struct urb *urb)
{
	free_urb_priv(fhci, urb);

	if (urb->status == -EINPROGRESS) {
		if (urb->actual_length != urb->transfer_buffer_length &&
				urb->transfer_flags & URB_SHORT_NOT_OK)
			urb->status = -EREMOTEIO;
		else
			urb->status = 0;
	}

	usb_hcd_unlink_urb_from_ep(fhci_to_hcd(fhci), urb);

	spin_unlock(&fhci->lock);

	usb_hcd_giveback_urb(fhci_to_hcd(fhci), urb, urb->status);

	spin_lock(&fhci->lock);
}

/*
 * caculate transfer length/stats and update the urb
 * Precondition: irqsafe(only for urb-?status locking)
 */
void fhci_done_td(struct urb *urb, struct td *td)
{
	struct ed *ed = td->ed;
	u32 cc = td->status;

	/* ISO...drivers see per-TD length/status */
	if (ed->mode == FHCI_TF_ISO) {
		u32 len;
		if (!(urb->transfer_flags & URB_SHORT_NOT_OK &&
				cc == USB_TD_RX_DATA_UNDERUN))
			cc = USB_TD_OK;

		if (usb_pipeout(urb->pipe))
			len = urb->iso_frame_desc[td->iso_index].length;
		else
			len = td->actual_len;

		urb->actual_length += len;
		urb->iso_frame_desc[td->iso_index].actual_length = len;
		urb->iso_frame_desc[td->iso_index].status =
			status_to_error(cc);
	}

	/* BULK,INT,CONTROL... drivers see aggregate length/status,
	 * except that "setup" bytes aren't counted and "short" transfers
	 * might not be reported as errors.
	 */
	else {
		if (td->error_cnt >= 3)
			urb->error_count = 3;

		/* control endpoint only have soft stalls */

		/* update packet status if needed(short may be ok) */
		if (!(urb->transfer_flags & URB_SHORT_NOT_OK) &&
				cc == USB_TD_RX_DATA_UNDERUN) {
			ed->state = FHCI_ED_OPER;
			cc = USB_TD_OK;
		}
		if (cc != USB_TD_OK) {
			if (urb->status == -EINPROGRESS)
				urb->status = status_to_error(cc);
		}

		/* count all non-empty packets except control SETUP packet */
		if (td->type != FHCI_TA_SETUP || td->iso_index != 0)
			urb->actual_length += td->actual_len;
	}
}

/* there are some pedning request to unlink */
void fhci_del_ed_list(struct fhci_hcd *fhci, struct ed *ed)
{
	struct td *td = peek_td_from_ed(ed);
	struct urb *urb = td->urb;
	struct urb_priv *urb_priv = urb->hcpriv;

	if (urb_priv->state == URB_DEL) {
		td = fhci_remove_td_from_ed(ed);
		/* HC may have partly processed this TD */
		if (td->status != USB_TD_INPROGRESS)
			fhci_done_td(urb, td);

		/* URB is done;clean up */
		if (++(urb_priv->tds_cnt) == urb_priv->num_of_tds)
			fhci_urb_complete_free(fhci, urb);
	}
}
