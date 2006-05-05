/*
 * Universal Host Controller Interface driver for USB.
 *
 * Maintainer: Alan Stern <stern@rowland.harvard.edu>
 *
 * (C) Copyright 1999 Linus Torvalds
 * (C) Copyright 1999-2002 Johannes Erdfelt, johannes@erdfelt.com
 * (C) Copyright 1999 Randy Dunlap
 * (C) Copyright 1999 Georg Acher, acher@in.tum.de
 * (C) Copyright 1999 Deti Fliegl, deti@fliegl.de
 * (C) Copyright 1999 Thomas Sailer, sailer@ife.ee.ethz.ch
 * (C) Copyright 1999 Roman Weissgaerber, weissg@vienna.at
 * (C) Copyright 2000 Yggdrasil Computing, Inc. (port of new PCI interface
 *               support from usb-ohci.c by Adam Richter, adam@yggdrasil.com).
 * (C) Copyright 1999 Gregory P. Smith (from usb-ohci.c)
 * (C) Copyright 2004-2005 Alan Stern, stern@rowland.harvard.edu
 */

static void uhci_free_pending_tds(struct uhci_hcd *uhci);

/*
 * Technically, updating td->status here is a race, but it's not really a
 * problem. The worst that can happen is that we set the IOC bit again
 * generating a spurious interrupt. We could fix this by creating another
 * QH and leaving the IOC bit always set, but then we would have to play
 * games with the FSBR code to make sure we get the correct order in all
 * the cases. I don't think it's worth the effort
 */
static void uhci_set_next_interrupt(struct uhci_hcd *uhci)
{
	if (uhci->is_stopped)
		mod_timer(&uhci_to_hcd(uhci)->rh_timer, jiffies);
	uhci->term_td->status |= cpu_to_le32(TD_CTRL_IOC); 
}

static inline void uhci_clear_next_interrupt(struct uhci_hcd *uhci)
{
	uhci->term_td->status &= ~cpu_to_le32(TD_CTRL_IOC);
}

static struct uhci_td *uhci_alloc_td(struct uhci_hcd *uhci)
{
	dma_addr_t dma_handle;
	struct uhci_td *td;

	td = dma_pool_alloc(uhci->td_pool, GFP_ATOMIC, &dma_handle);
	if (!td)
		return NULL;

	td->dma_handle = dma_handle;
	td->frame = -1;

	INIT_LIST_HEAD(&td->list);
	INIT_LIST_HEAD(&td->remove_list);
	INIT_LIST_HEAD(&td->fl_list);

	return td;
}

static void uhci_free_td(struct uhci_hcd *uhci, struct uhci_td *td)
{
	if (!list_empty(&td->list))
		dev_warn(uhci_dev(uhci), "td %p still in list!\n", td);
	if (!list_empty(&td->remove_list))
		dev_warn(uhci_dev(uhci), "td %p still in remove_list!\n", td);
	if (!list_empty(&td->fl_list))
		dev_warn(uhci_dev(uhci), "td %p still in fl_list!\n", td);

	dma_pool_free(uhci->td_pool, td, td->dma_handle);
}

static inline void uhci_fill_td(struct uhci_td *td, u32 status,
		u32 token, u32 buffer)
{
	td->status = cpu_to_le32(status);
	td->token = cpu_to_le32(token);
	td->buffer = cpu_to_le32(buffer);
}

/*
 * We insert Isochronous URBs directly into the frame list at the beginning
 */
static inline void uhci_insert_td_in_frame_list(struct uhci_hcd *uhci,
		struct uhci_td *td, unsigned framenum)
{
	framenum &= (UHCI_NUMFRAMES - 1);

	td->frame = framenum;

	/* Is there a TD already mapped there? */
	if (uhci->frame_cpu[framenum]) {
		struct uhci_td *ftd, *ltd;

		ftd = uhci->frame_cpu[framenum];
		ltd = list_entry(ftd->fl_list.prev, struct uhci_td, fl_list);

		list_add_tail(&td->fl_list, &ftd->fl_list);

		td->link = ltd->link;
		wmb();
		ltd->link = cpu_to_le32(td->dma_handle);
	} else {
		td->link = uhci->frame[framenum];
		wmb();
		uhci->frame[framenum] = cpu_to_le32(td->dma_handle);
		uhci->frame_cpu[framenum] = td;
	}
}

static inline void uhci_remove_td_from_frame_list(struct uhci_hcd *uhci,
		struct uhci_td *td)
{
	/* If it's not inserted, don't remove it */
	if (td->frame == -1) {
		WARN_ON(!list_empty(&td->fl_list));
		return;
	}

	if (uhci->frame_cpu[td->frame] == td) {
		if (list_empty(&td->fl_list)) {
			uhci->frame[td->frame] = td->link;
			uhci->frame_cpu[td->frame] = NULL;
		} else {
			struct uhci_td *ntd;

			ntd = list_entry(td->fl_list.next, struct uhci_td, fl_list);
			uhci->frame[td->frame] = cpu_to_le32(ntd->dma_handle);
			uhci->frame_cpu[td->frame] = ntd;
		}
	} else {
		struct uhci_td *ptd;

		ptd = list_entry(td->fl_list.prev, struct uhci_td, fl_list);
		ptd->link = td->link;
	}

	list_del_init(&td->fl_list);
	td->frame = -1;
}

/*
 * Remove all the TDs for an Isochronous URB from the frame list
 */
static void uhci_unlink_isochronous_tds(struct uhci_hcd *uhci, struct urb *urb)
{
	struct urb_priv *urbp = (struct urb_priv *) urb->hcpriv;
	struct uhci_td *td;

	list_for_each_entry(td, &urbp->td_list, list)
		uhci_remove_td_from_frame_list(uhci, td);
	wmb();
}

static struct uhci_qh *uhci_alloc_qh(struct uhci_hcd *uhci,
		struct usb_device *udev, struct usb_host_endpoint *hep)
{
	dma_addr_t dma_handle;
	struct uhci_qh *qh;

	qh = dma_pool_alloc(uhci->qh_pool, GFP_ATOMIC, &dma_handle);
	if (!qh)
		return NULL;

	qh->dma_handle = dma_handle;

	qh->element = UHCI_PTR_TERM;
	qh->link = UHCI_PTR_TERM;

	INIT_LIST_HEAD(&qh->queue);
	INIT_LIST_HEAD(&qh->node);

	if (udev) {		/* Normal QH */
		qh->dummy_td = uhci_alloc_td(uhci);
		if (!qh->dummy_td) {
			dma_pool_free(uhci->qh_pool, qh, dma_handle);
			return NULL;
		}
		qh->state = QH_STATE_IDLE;
		qh->hep = hep;
		qh->udev = udev;
		hep->hcpriv = qh;
		qh->type = hep->desc.bmAttributes & USB_ENDPOINT_XFERTYPE_MASK;

	} else {		/* Skeleton QH */
		qh->state = QH_STATE_ACTIVE;
		qh->udev = NULL;
		qh->type = -1;
	}
	return qh;
}

static void uhci_free_qh(struct uhci_hcd *uhci, struct uhci_qh *qh)
{
	WARN_ON(qh->state != QH_STATE_IDLE && qh->udev);
	if (!list_empty(&qh->queue))
		dev_warn(uhci_dev(uhci), "qh %p list not empty!\n", qh);

	list_del(&qh->node);
	if (qh->udev) {
		qh->hep->hcpriv = NULL;
		uhci_free_td(uhci, qh->dummy_td);
	}
	dma_pool_free(uhci->qh_pool, qh, qh->dma_handle);
}

/*
 * When the currently executing URB is dequeued, save its current toggle value
 */
static void uhci_save_toggle(struct uhci_qh *qh, struct urb *urb)
{
	struct urb_priv *urbp = (struct urb_priv *) urb->hcpriv;
	struct uhci_td *td;

	/* If the QH element pointer is UHCI_PTR_TERM then then currently
	 * executing URB has already been unlinked, so this one isn't it. */
	if (qh_element(qh) == UHCI_PTR_TERM ||
				qh->queue.next != &urbp->node)
		return;
	qh->element = UHCI_PTR_TERM;

	/* Only bulk and interrupt pipes have to worry about toggles */
	if (!(qh->type == USB_ENDPOINT_XFER_BULK ||
			qh->type == USB_ENDPOINT_XFER_INT))
		return;

	/* Find the first active TD; that's the device's toggle state */
	list_for_each_entry(td, &urbp->td_list, list) {
		if (td_status(td) & TD_CTRL_ACTIVE) {
			qh->needs_fixup = 1;
			qh->initial_toggle = uhci_toggle(td_token(td));
			return;
		}
	}

	WARN_ON(1);
}

/*
 * Fix up the data toggles for URBs in a queue, when one of them
 * terminates early (short transfer, error, or dequeued).
 */
static void uhci_fixup_toggles(struct uhci_qh *qh, int skip_first)
{
	struct urb_priv *urbp = NULL;
	struct uhci_td *td;
	unsigned int toggle = qh->initial_toggle;
	unsigned int pipe;

	/* Fixups for a short transfer start with the second URB in the
	 * queue (the short URB is the first). */
	if (skip_first)
		urbp = list_entry(qh->queue.next, struct urb_priv, node);

	/* When starting with the first URB, if the QH element pointer is
	 * still valid then we know the URB's toggles are okay. */
	else if (qh_element(qh) != UHCI_PTR_TERM)
		toggle = 2;

	/* Fix up the toggle for the URBs in the queue.  Normally this
	 * loop won't run more than once: When an error or short transfer
	 * occurs, the queue usually gets emptied. */
	urbp = list_prepare_entry(urbp, &qh->queue, node);
	list_for_each_entry_continue(urbp, &qh->queue, node) {

		/* If the first TD has the right toggle value, we don't
		 * need to change any toggles in this URB */
		td = list_entry(urbp->td_list.next, struct uhci_td, list);
		if (toggle > 1 || uhci_toggle(td_token(td)) == toggle) {
			td = list_entry(urbp->td_list.next, struct uhci_td,
					list);
			toggle = uhci_toggle(td_token(td)) ^ 1;

		/* Otherwise all the toggles in the URB have to be switched */
		} else {
			list_for_each_entry(td, &urbp->td_list, list) {
				td->token ^= __constant_cpu_to_le32(
							TD_TOKEN_TOGGLE);
				toggle ^= 1;
			}
		}
	}

	wmb();
	pipe = list_entry(qh->queue.next, struct urb_priv, node)->urb->pipe;
	usb_settoggle(qh->udev, usb_pipeendpoint(pipe),
			usb_pipeout(pipe), toggle);
	qh->needs_fixup = 0;
}

/*
 * Put a QH on the schedule in both hardware and software
 */
static void uhci_activate_qh(struct uhci_hcd *uhci, struct uhci_qh *qh)
{
	struct uhci_qh *pqh;

	WARN_ON(list_empty(&qh->queue));

	/* Set the element pointer if it isn't set already.
	 * This isn't needed for Isochronous queues, but it doesn't hurt. */
	if (qh_element(qh) == UHCI_PTR_TERM) {
		struct urb_priv *urbp = list_entry(qh->queue.next,
				struct urb_priv, node);
		struct uhci_td *td = list_entry(urbp->td_list.next,
				struct uhci_td, list);

		qh->element = cpu_to_le32(td->dma_handle);
	}

	if (qh->state == QH_STATE_ACTIVE)
		return;
	qh->state = QH_STATE_ACTIVE;

	/* Move the QH from its old list to the end of the appropriate
	 * skeleton's list */
	if (qh == uhci->next_qh)
		uhci->next_qh = list_entry(qh->node.next, struct uhci_qh,
				node);
	list_move_tail(&qh->node, &qh->skel->node);

	/* Link it into the schedule */
	pqh = list_entry(qh->node.prev, struct uhci_qh, node);
	qh->link = pqh->link;
	wmb();
	pqh->link = UHCI_PTR_QH | cpu_to_le32(qh->dma_handle);
}

/*
 * Take a QH off the hardware schedule
 */
static void uhci_unlink_qh(struct uhci_hcd *uhci, struct uhci_qh *qh)
{
	struct uhci_qh *pqh;

	if (qh->state == QH_STATE_UNLINKING)
		return;
	WARN_ON(qh->state != QH_STATE_ACTIVE || !qh->udev);
	qh->state = QH_STATE_UNLINKING;

	/* Unlink the QH from the schedule and record when we did it */
	pqh = list_entry(qh->node.prev, struct uhci_qh, node);
	pqh->link = qh->link;
	mb();

	uhci_get_current_frame_number(uhci);
	qh->unlink_frame = uhci->frame_number;

	/* Force an interrupt so we know when the QH is fully unlinked */
	if (list_empty(&uhci->skel_unlink_qh->node))
		uhci_set_next_interrupt(uhci);

	/* Move the QH from its old list to the end of the unlinking list */
	if (qh == uhci->next_qh)
		uhci->next_qh = list_entry(qh->node.next, struct uhci_qh,
				node);
	list_move_tail(&qh->node, &uhci->skel_unlink_qh->node);
}

/*
 * When we and the controller are through with a QH, it becomes IDLE.
 * This happens when a QH has been off the schedule (on the unlinking
 * list) for more than one frame, or when an error occurs while adding
 * the first URB onto a new QH.
 */
static void uhci_make_qh_idle(struct uhci_hcd *uhci, struct uhci_qh *qh)
{
	WARN_ON(qh->state == QH_STATE_ACTIVE);

	if (qh == uhci->next_qh)
		uhci->next_qh = list_entry(qh->node.next, struct uhci_qh,
				node);
	list_move(&qh->node, &uhci->idle_qh_list);
	qh->state = QH_STATE_IDLE;

	/* If anyone is waiting for a QH to become idle, wake them up */
	if (uhci->num_waiting)
		wake_up_all(&uhci->waitqh);
}

static inline struct urb_priv *uhci_alloc_urb_priv(struct uhci_hcd *uhci,
		struct urb *urb)
{
	struct urb_priv *urbp;

	urbp = kmem_cache_alloc(uhci_up_cachep, SLAB_ATOMIC);
	if (!urbp)
		return NULL;

	memset((void *)urbp, 0, sizeof(*urbp));

	urbp->urb = urb;
	urb->hcpriv = urbp;
	
	INIT_LIST_HEAD(&urbp->node);
	INIT_LIST_HEAD(&urbp->td_list);

	return urbp;
}

static void uhci_add_td_to_urb(struct urb *urb, struct uhci_td *td)
{
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;

	list_add_tail(&td->list, &urbp->td_list);
}

static void uhci_remove_td_from_urb(struct uhci_td *td)
{
	if (list_empty(&td->list))
		return;

	list_del_init(&td->list);
}

static void uhci_free_urb_priv(struct uhci_hcd *uhci,
		struct urb_priv *urbp)
{
	struct uhci_td *td, *tmp;

	if (!list_empty(&urbp->node))
		dev_warn(uhci_dev(uhci), "urb %p still on QH's list!\n",
				urbp->urb);

	uhci_get_current_frame_number(uhci);
	if (uhci->frame_number + uhci->is_stopped != uhci->td_remove_age) {
		uhci_free_pending_tds(uhci);
		uhci->td_remove_age = uhci->frame_number;
	}

	/* Check to see if the remove list is empty. Set the IOC bit */
	/* to force an interrupt so we can remove the TDs. */
	if (list_empty(&uhci->td_remove_list))
		uhci_set_next_interrupt(uhci);

	list_for_each_entry_safe(td, tmp, &urbp->td_list, list) {
		uhci_remove_td_from_urb(td);
		list_add(&td->remove_list, &uhci->td_remove_list);
	}

	urbp->urb->hcpriv = NULL;
	kmem_cache_free(uhci_up_cachep, urbp);
}

static void uhci_inc_fsbr(struct uhci_hcd *uhci, struct urb *urb)
{
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;

	if ((!(urb->transfer_flags & URB_NO_FSBR)) && !urbp->fsbr) {
		urbp->fsbr = 1;
		if (!uhci->fsbr++ && !uhci->fsbrtimeout)
			uhci->skel_term_qh->link = cpu_to_le32(uhci->skel_fs_control_qh->dma_handle) | UHCI_PTR_QH;
	}
}

static void uhci_dec_fsbr(struct uhci_hcd *uhci, struct urb *urb)
{
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;

	if ((!(urb->transfer_flags & URB_NO_FSBR)) && urbp->fsbr) {
		urbp->fsbr = 0;
		if (!--uhci->fsbr)
			uhci->fsbrtimeout = jiffies + FSBR_DELAY;
	}
}

/*
 * Map status to standard result codes
 *
 * <status> is (td_status(td) & 0xF60000), a.k.a.
 * uhci_status_bits(td_status(td)).
 * Note: <status> does not include the TD_CTRL_NAK bit.
 * <dir_out> is True for output TDs and False for input TDs.
 */
static int uhci_map_status(int status, int dir_out)
{
	if (!status)
		return 0;
	if (status & TD_CTRL_BITSTUFF)			/* Bitstuff error */
		return -EPROTO;
	if (status & TD_CTRL_CRCTIMEO) {		/* CRC/Timeout */
		if (dir_out)
			return -EPROTO;
		else
			return -EILSEQ;
	}
	if (status & TD_CTRL_BABBLE)			/* Babble */
		return -EOVERFLOW;
	if (status & TD_CTRL_DBUFERR)			/* Buffer error */
		return -ENOSR;
	if (status & TD_CTRL_STALLED)			/* Stalled */
		return -EPIPE;
	WARN_ON(status & TD_CTRL_ACTIVE);		/* Active */
	return 0;
}

/*
 * Control transfers
 */
static int uhci_submit_control(struct uhci_hcd *uhci, struct urb *urb,
		struct uhci_qh *qh)
{
	struct uhci_td *td;
	unsigned long destination, status;
	int maxsze = le16_to_cpu(qh->hep->desc.wMaxPacketSize);
	int len = urb->transfer_buffer_length;
	dma_addr_t data = urb->transfer_dma;
	__le32 *plink;

	/* The "pipe" thing contains the destination in bits 8--18 */
	destination = (urb->pipe & PIPE_DEVEP_MASK) | USB_PID_SETUP;

	/* 3 errors, dummy TD remains inactive */
	status = uhci_maxerr(3);
	if (urb->dev->speed == USB_SPEED_LOW)
		status |= TD_CTRL_LS;

	/*
	 * Build the TD for the control request setup packet
	 */
	td = qh->dummy_td;
	uhci_add_td_to_urb(urb, td);
	uhci_fill_td(td, status, destination | uhci_explen(8),
			urb->setup_dma);
	plink = &td->link;
	status |= TD_CTRL_ACTIVE;

	/*
	 * If direction is "send", change the packet ID from SETUP (0x2D)
	 * to OUT (0xE1).  Else change it from SETUP to IN (0x69) and
	 * set Short Packet Detect (SPD) for all data packets.
	 */
	if (usb_pipeout(urb->pipe))
		destination ^= (USB_PID_SETUP ^ USB_PID_OUT);
	else {
		destination ^= (USB_PID_SETUP ^ USB_PID_IN);
		status |= TD_CTRL_SPD;
	}

	/*
	 * Build the DATA TDs
	 */
	while (len > 0) {
		int pktsze = min(len, maxsze);

		td = uhci_alloc_td(uhci);
		if (!td)
			goto nomem;
		*plink = cpu_to_le32(td->dma_handle);

		/* Alternate Data0/1 (start with Data1) */
		destination ^= TD_TOKEN_TOGGLE;
	
		uhci_add_td_to_urb(urb, td);
		uhci_fill_td(td, status, destination | uhci_explen(pktsze),
				data);
		plink = &td->link;

		data += pktsze;
		len -= pktsze;
	}

	/*
	 * Build the final TD for control status 
	 */
	td = uhci_alloc_td(uhci);
	if (!td)
		goto nomem;
	*plink = cpu_to_le32(td->dma_handle);

	/*
	 * It's IN if the pipe is an output pipe or we're not expecting
	 * data back.
	 */
	destination &= ~TD_TOKEN_PID_MASK;
	if (usb_pipeout(urb->pipe) || !urb->transfer_buffer_length)
		destination |= USB_PID_IN;
	else
		destination |= USB_PID_OUT;

	destination |= TD_TOKEN_TOGGLE;		/* End in Data1 */

	status &= ~TD_CTRL_SPD;

	uhci_add_td_to_urb(urb, td);
	uhci_fill_td(td, status | TD_CTRL_IOC,
			destination | uhci_explen(0), 0);
	plink = &td->link;

	/*
	 * Build the new dummy TD and activate the old one
	 */
	td = uhci_alloc_td(uhci);
	if (!td)
		goto nomem;
	*plink = cpu_to_le32(td->dma_handle);

	uhci_fill_td(td, 0, USB_PID_OUT | uhci_explen(0), 0);
	wmb();
	qh->dummy_td->status |= __constant_cpu_to_le32(TD_CTRL_ACTIVE);
	qh->dummy_td = td;

	/* Low-speed transfers get a different queue, and won't hog the bus.
	 * Also, some devices enumerate better without FSBR; the easiest way
	 * to do that is to put URBs on the low-speed queue while the device
	 * isn't in the CONFIGURED state. */
	if (urb->dev->speed == USB_SPEED_LOW ||
			urb->dev->state != USB_STATE_CONFIGURED)
		qh->skel = uhci->skel_ls_control_qh;
	else {
		qh->skel = uhci->skel_fs_control_qh;
		uhci_inc_fsbr(uhci, urb);
	}
	return 0;

nomem:
	/* Remove the dummy TD from the td_list so it doesn't get freed */
	uhci_remove_td_from_urb(qh->dummy_td);
	return -ENOMEM;
}

/*
 * If control-IN transfer was short, the status packet wasn't sent.
 * This routine changes the element pointer in the QH to point at the
 * status TD.  It's safe to do this even while the QH is live, because
 * the hardware only updates the element pointer following a successful
 * transfer.  The inactive TD for the short packet won't cause an update,
 * so the pointer won't get overwritten.  The next time the controller
 * sees this QH, it will send the status packet.
 */
static int usb_control_retrigger_status(struct uhci_hcd *uhci, struct urb *urb)
{
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;
	struct uhci_td *td;

	urbp->short_transfer = 1;

	td = list_entry(urbp->td_list.prev, struct uhci_td, list);
	urbp->qh->element = cpu_to_le32(td->dma_handle);

	return -EINPROGRESS;
}


static int uhci_result_control(struct uhci_hcd *uhci, struct urb *urb)
{
	struct list_head *tmp, *head;
	struct urb_priv *urbp = urb->hcpriv;
	struct uhci_td *td;
	unsigned int status;
	int ret = 0;

	head = &urbp->td_list;
	if (urbp->short_transfer) {
		tmp = head->prev;
		goto status_stage;
	}

	urb->actual_length = 0;

	tmp = head->next;
	td = list_entry(tmp, struct uhci_td, list);

	/* The first TD is the SETUP stage, check the status, but skip */
	/*  the count */
	status = uhci_status_bits(td_status(td));
	if (status & TD_CTRL_ACTIVE)
		return -EINPROGRESS;

	if (status)
		goto td_error;

	/* The rest of the TDs (but the last) are data */
	tmp = tmp->next;
	while (tmp != head && tmp->next != head) {
		unsigned int ctrlstat;

		td = list_entry(tmp, struct uhci_td, list);
		tmp = tmp->next;

		ctrlstat = td_status(td);
		status = uhci_status_bits(ctrlstat);
		if (status & TD_CTRL_ACTIVE)
			return -EINPROGRESS;

		urb->actual_length += uhci_actual_length(ctrlstat);

		if (status)
			goto td_error;

		/* Check to see if we received a short packet */
		if (uhci_actual_length(ctrlstat) <
				uhci_expected_length(td_token(td))) {
			if (urb->transfer_flags & URB_SHORT_NOT_OK) {
				ret = -EREMOTEIO;
				goto err;
			}

			return usb_control_retrigger_status(uhci, urb);
		}
	}

status_stage:
	td = list_entry(tmp, struct uhci_td, list);

	/* Control status stage */
	status = td_status(td);

#ifdef I_HAVE_BUGGY_APC_BACKUPS
	/* APC BackUPS Pro kludge */
	/* It tries to send all of the descriptor instead of the amount */
	/*  we requested */
	if (status & TD_CTRL_IOC &&	/* IOC is masked out by uhci_status_bits */
	    status & TD_CTRL_ACTIVE &&
	    status & TD_CTRL_NAK)
		return 0;
#endif

	status = uhci_status_bits(status);
	if (status & TD_CTRL_ACTIVE)
		return -EINPROGRESS;

	if (status)
		goto td_error;

	return 0;

td_error:
	ret = uhci_map_status(status, uhci_packetout(td_token(td)));

err:
	if ((debug == 1 && ret != -EPIPE) || debug > 1) {
		/* Some debugging code */
		dev_dbg(uhci_dev(uhci), "%s: failed with status %x\n",
				__FUNCTION__, status);

		if (errbuf) {
			/* Print the chain for debugging purposes */
			uhci_show_qh(urbp->qh, errbuf, ERRBUF_LEN, 0);
			lprintk(errbuf);
		}
	}

	/* Note that the queue has stopped */
	urbp->qh->element = UHCI_PTR_TERM;
	urbp->qh->is_stopped = 1;
	return ret;
}

/*
 * Common submit for bulk and interrupt
 */
static int uhci_submit_common(struct uhci_hcd *uhci, struct urb *urb,
		struct uhci_qh *qh)
{
	struct uhci_td *td;
	unsigned long destination, status;
	int maxsze = le16_to_cpu(qh->hep->desc.wMaxPacketSize);
	int len = urb->transfer_buffer_length;
	dma_addr_t data = urb->transfer_dma;
	__le32 *plink;
	unsigned int toggle;

	if (len < 0)
		return -EINVAL;

	/* The "pipe" thing contains the destination in bits 8--18 */
	destination = (urb->pipe & PIPE_DEVEP_MASK) | usb_packetid(urb->pipe);
	toggle = usb_gettoggle(urb->dev, usb_pipeendpoint(urb->pipe),
			 usb_pipeout(urb->pipe));

	/* 3 errors, dummy TD remains inactive */
	status = uhci_maxerr(3);
	if (urb->dev->speed == USB_SPEED_LOW)
		status |= TD_CTRL_LS;
	if (usb_pipein(urb->pipe))
		status |= TD_CTRL_SPD;

	/*
	 * Build the DATA TDs
	 */
	plink = NULL;
	td = qh->dummy_td;
	do {	/* Allow zero length packets */
		int pktsze = maxsze;

		if (len <= pktsze) {		/* The last packet */
			pktsze = len;
			if (!(urb->transfer_flags & URB_SHORT_NOT_OK))
				status &= ~TD_CTRL_SPD;
		}

		if (plink) {
			td = uhci_alloc_td(uhci);
			if (!td)
				goto nomem;
			*plink = cpu_to_le32(td->dma_handle);
		}
		uhci_add_td_to_urb(urb, td);
		uhci_fill_td(td, status,
				destination | uhci_explen(pktsze) |
					(toggle << TD_TOKEN_TOGGLE_SHIFT),
				data);
		plink = &td->link;
		status |= TD_CTRL_ACTIVE;

		data += pktsze;
		len -= maxsze;
		toggle ^= 1;
	} while (len > 0);

	/*
	 * URB_ZERO_PACKET means adding a 0-length packet, if direction
	 * is OUT and the transfer_length was an exact multiple of maxsze,
	 * hence (len = transfer_length - N * maxsze) == 0
	 * however, if transfer_length == 0, the zero packet was already
	 * prepared above.
	 */
	if ((urb->transfer_flags & URB_ZERO_PACKET) &&
			usb_pipeout(urb->pipe) && len == 0 &&
			urb->transfer_buffer_length > 0) {
		td = uhci_alloc_td(uhci);
		if (!td)
			goto nomem;
		*plink = cpu_to_le32(td->dma_handle);

		uhci_add_td_to_urb(urb, td);
		uhci_fill_td(td, status,
				destination | uhci_explen(0) |
					(toggle << TD_TOKEN_TOGGLE_SHIFT),
				data);
		plink = &td->link;

		toggle ^= 1;
	}

	/* Set the interrupt-on-completion flag on the last packet.
	 * A more-or-less typical 4 KB URB (= size of one memory page)
	 * will require about 3 ms to transfer; that's a little on the
	 * fast side but not enough to justify delaying an interrupt
	 * more than 2 or 3 URBs, so we will ignore the URB_NO_INTERRUPT
	 * flag setting. */
	td->status |= __constant_cpu_to_le32(TD_CTRL_IOC);

	/*
	 * Build the new dummy TD and activate the old one
	 */
	td = uhci_alloc_td(uhci);
	if (!td)
		goto nomem;
	*plink = cpu_to_le32(td->dma_handle);

	uhci_fill_td(td, 0, USB_PID_OUT | uhci_explen(0), 0);
	wmb();
	qh->dummy_td->status |= __constant_cpu_to_le32(TD_CTRL_ACTIVE);
	qh->dummy_td = td;

	usb_settoggle(urb->dev, usb_pipeendpoint(urb->pipe),
			usb_pipeout(urb->pipe), toggle);
	return 0;

nomem:
	/* Remove the dummy TD from the td_list so it doesn't get freed */
	uhci_remove_td_from_urb(qh->dummy_td);
	return -ENOMEM;
}

/*
 * Common result for bulk and interrupt
 */
static int uhci_result_common(struct uhci_hcd *uhci, struct urb *urb)
{
	struct urb_priv *urbp = urb->hcpriv;
	struct uhci_td *td;
	unsigned int status = 0;
	int ret = 0;

	urb->actual_length = 0;

	list_for_each_entry(td, &urbp->td_list, list) {
		unsigned int ctrlstat = td_status(td);

		status = uhci_status_bits(ctrlstat);
		if (status & TD_CTRL_ACTIVE)
			return -EINPROGRESS;

		urb->actual_length += uhci_actual_length(ctrlstat);

		if (status)
			goto td_error;

		if (uhci_actual_length(ctrlstat) <
				uhci_expected_length(td_token(td))) {
			if (urb->transfer_flags & URB_SHORT_NOT_OK) {
				ret = -EREMOTEIO;
				goto err;
			}

			/*
			 * This URB stopped short of its end.  We have to
			 * fix up the toggles of the following URBs on the
			 * queue and restart the queue.
			 *
			 * Do this only the first time we encounter the
			 * short URB.
			 */
			if (!urbp->short_transfer) {
				urbp->short_transfer = 1;
				urbp->qh->initial_toggle =
						uhci_toggle(td_token(td)) ^ 1;
				uhci_fixup_toggles(urbp->qh, 1);

				td = list_entry(urbp->td_list.prev,
						struct uhci_td, list);
				urbp->qh->element = td->link;
			}
			break;
		}
	}

	return 0;

td_error:
	ret = uhci_map_status(status, uhci_packetout(td_token(td)));

	if ((debug == 1 && ret != -EPIPE) || debug > 1) {
		/* Some debugging code */
		dev_dbg(uhci_dev(uhci), "%s: failed with status %x\n",
				__FUNCTION__, status);

		if (debug > 1 && errbuf) {
			/* Print the chain for debugging purposes */
			uhci_show_qh(urbp->qh, errbuf, ERRBUF_LEN, 0);
			lprintk(errbuf);
		}
	}
err:

	/* Note that the queue has stopped and save the next toggle value */
	urbp->qh->element = UHCI_PTR_TERM;
	urbp->qh->is_stopped = 1;
	urbp->qh->needs_fixup = 1;
	urbp->qh->initial_toggle = uhci_toggle(td_token(td)) ^
			(ret == -EREMOTEIO);
	return ret;
}

static inline int uhci_submit_bulk(struct uhci_hcd *uhci, struct urb *urb,
		struct uhci_qh *qh)
{
	int ret;

	/* Can't have low-speed bulk transfers */
	if (urb->dev->speed == USB_SPEED_LOW)
		return -EINVAL;

	qh->skel = uhci->skel_bulk_qh;
	ret = uhci_submit_common(uhci, urb, qh);
	if (ret == 0)
		uhci_inc_fsbr(uhci, urb);
	return ret;
}

static inline int uhci_submit_interrupt(struct uhci_hcd *uhci, struct urb *urb,
		struct uhci_qh *qh)
{
	/* USB 1.1 interrupt transfers only involve one packet per interval.
	 * Drivers can submit URBs of any length, but longer ones will need
	 * multiple intervals to complete.
	 */
	qh->skel = uhci->skelqh[__interval_to_skel(urb->interval)];
	return uhci_submit_common(uhci, urb, qh);
}

/*
 * Isochronous transfers
 */
static int uhci_submit_isochronous(struct uhci_hcd *uhci, struct urb *urb,
		struct uhci_qh *qh)
{
	struct uhci_td *td = NULL;	/* Since urb->number_of_packets > 0 */
	int i, frame;
	unsigned long destination, status;
	struct urb_priv *urbp = (struct urb_priv *) urb->hcpriv;

	if (urb->number_of_packets > 900)	/* 900? Why? */
		return -EFBIG;

	status = TD_CTRL_ACTIVE | TD_CTRL_IOS;
	destination = (urb->pipe & PIPE_DEVEP_MASK) | usb_packetid(urb->pipe);

	/* Figure out the starting frame number */
	if (urb->transfer_flags & URB_ISO_ASAP) {
		if (list_empty(&qh->queue)) {
			uhci_get_current_frame_number(uhci);
			urb->start_frame = (uhci->frame_number + 10);

		} else {		/* Go right after the last one */
			struct urb *last_urb;

			last_urb = list_entry(qh->queue.prev,
					struct urb_priv, node)->urb;
			urb->start_frame = (last_urb->start_frame +
					last_urb->number_of_packets *
					last_urb->interval);
		}
	} else {
		/* FIXME: Sanity check */
	}
	urb->start_frame &= (UHCI_NUMFRAMES - 1);

	for (i = 0; i < urb->number_of_packets; i++) {
		td = uhci_alloc_td(uhci);
		if (!td)
			return -ENOMEM;

		uhci_add_td_to_urb(urb, td);
		uhci_fill_td(td, status, destination |
				uhci_explen(urb->iso_frame_desc[i].length),
				urb->transfer_dma +
					urb->iso_frame_desc[i].offset);
	}

	/* Set the interrupt-on-completion flag on the last packet. */
	td->status |= __constant_cpu_to_le32(TD_CTRL_IOC);

	qh->skel = uhci->skel_iso_qh;

	/* Add the TDs to the frame list */
	frame = urb->start_frame;
	list_for_each_entry(td, &urbp->td_list, list) {
		uhci_insert_td_in_frame_list(uhci, td, frame);
		frame += urb->interval;
	}

	return 0;
}

static int uhci_result_isochronous(struct uhci_hcd *uhci, struct urb *urb)
{
	struct uhci_td *td;
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;
	int status;
	int i, ret = 0;

	urb->actual_length = urb->error_count = 0;

	i = 0;
	list_for_each_entry(td, &urbp->td_list, list) {
		int actlength;
		unsigned int ctrlstat = td_status(td);

		if (ctrlstat & TD_CTRL_ACTIVE)
			return -EINPROGRESS;

		actlength = uhci_actual_length(ctrlstat);
		urb->iso_frame_desc[i].actual_length = actlength;
		urb->actual_length += actlength;

		status = uhci_map_status(uhci_status_bits(ctrlstat),
				usb_pipeout(urb->pipe));
		urb->iso_frame_desc[i].status = status;
		if (status) {
			urb->error_count++;
			ret = status;
		}

		i++;
	}

	return ret;
}

static int uhci_urb_enqueue(struct usb_hcd *hcd,
		struct usb_host_endpoint *hep,
		struct urb *urb, gfp_t mem_flags)
{
	int ret;
	struct uhci_hcd *uhci = hcd_to_uhci(hcd);
	unsigned long flags;
	struct urb_priv *urbp;
	struct uhci_qh *qh;
	int bustime;

	spin_lock_irqsave(&uhci->lock, flags);

	ret = urb->status;
	if (ret != -EINPROGRESS)		/* URB already unlinked! */
		goto done;

	ret = -ENOMEM;
	urbp = uhci_alloc_urb_priv(uhci, urb);
	if (!urbp)
		goto done;

	if (hep->hcpriv)
		qh = (struct uhci_qh *) hep->hcpriv;
	else {
		qh = uhci_alloc_qh(uhci, urb->dev, hep);
		if (!qh)
			goto err_no_qh;
	}
	urbp->qh = qh;

	switch (qh->type) {
	case USB_ENDPOINT_XFER_CONTROL:
		ret = uhci_submit_control(uhci, urb, qh);
		break;
	case USB_ENDPOINT_XFER_BULK:
		ret = uhci_submit_bulk(uhci, urb, qh);
		break;
	case USB_ENDPOINT_XFER_INT:
		if (list_empty(&qh->queue)) {
			bustime = usb_check_bandwidth(urb->dev, urb);
			if (bustime < 0)
				ret = bustime;
			else {
				ret = uhci_submit_interrupt(uhci, urb, qh);
				if (ret == 0)
					usb_claim_bandwidth(urb->dev, urb, bustime, 0);
			}
		} else {	/* inherit from parent */
			struct urb_priv *eurbp;

			eurbp = list_entry(qh->queue.prev, struct urb_priv,
					node);
			urb->bandwidth = eurbp->urb->bandwidth;
			ret = uhci_submit_interrupt(uhci, urb, qh);
		}
		break;
	case USB_ENDPOINT_XFER_ISOC:
		bustime = usb_check_bandwidth(urb->dev, urb);
		if (bustime < 0) {
			ret = bustime;
			break;
		}

		ret = uhci_submit_isochronous(uhci, urb, qh);
		if (ret == 0)
			usb_claim_bandwidth(urb->dev, urb, bustime, 1);
		break;
	}
	if (ret != 0)
		goto err_submit_failed;

	/* Add this URB to the QH */
	urbp->qh = qh;
	list_add_tail(&urbp->node, &qh->queue);

	/* If the new URB is the first and only one on this QH then either
	 * the QH is new and idle or else it's unlinked and waiting to
	 * become idle, so we can activate it right away. */
	if (qh->queue.next == &urbp->node)
		uhci_activate_qh(uhci, qh);
	goto done;

err_submit_failed:
	if (qh->state == QH_STATE_IDLE)
		uhci_make_qh_idle(uhci, qh);	/* Reclaim unused QH */

err_no_qh:
	uhci_free_urb_priv(uhci, urbp);

done:
	spin_unlock_irqrestore(&uhci->lock, flags);
	return ret;
}

static int uhci_urb_dequeue(struct usb_hcd *hcd, struct urb *urb)
{
	struct uhci_hcd *uhci = hcd_to_uhci(hcd);
	unsigned long flags;
	struct urb_priv *urbp;

	spin_lock_irqsave(&uhci->lock, flags);
	urbp = urb->hcpriv;
	if (!urbp)			/* URB was never linked! */
		goto done;

	/* Remove Isochronous TDs from the frame list ASAP */
	if (urbp->qh->type == USB_ENDPOINT_XFER_ISOC)
		uhci_unlink_isochronous_tds(uhci, urb);
	uhci_unlink_qh(uhci, urbp->qh);

done:
	spin_unlock_irqrestore(&uhci->lock, flags);
	return 0;
}

/*
 * Finish unlinking an URB and give it back
 */
static void uhci_giveback_urb(struct uhci_hcd *uhci, struct uhci_qh *qh,
		struct urb *urb, struct pt_regs *regs)
__releases(uhci->lock)
__acquires(uhci->lock)
{
	struct urb_priv *urbp = (struct urb_priv *) urb->hcpriv;

	/* Isochronous TDs get unlinked directly from the frame list */
	if (qh->type == USB_ENDPOINT_XFER_ISOC)
		uhci_unlink_isochronous_tds(uhci, urb);

	/* If the URB isn't first on its queue, adjust the link pointer
	 * of the last TD in the previous URB. */
	else if (qh->queue.next != &urbp->node) {
		struct urb_priv *purbp;
		struct uhci_td *ptd, *ltd;

		purbp = list_entry(urbp->node.prev, struct urb_priv, node);
		ptd = list_entry(purbp->td_list.prev, struct uhci_td,
				list);
		ltd = list_entry(urbp->td_list.prev, struct uhci_td,
				list);
		ptd->link = ltd->link;
	}

	/* Take the URB off the QH's queue.  If the queue is now empty,
	 * this is a perfect time for a toggle fixup. */
	list_del_init(&urbp->node);
	if (list_empty(&qh->queue) && qh->needs_fixup) {
		usb_settoggle(urb->dev, usb_pipeendpoint(urb->pipe),
				usb_pipeout(urb->pipe), qh->initial_toggle);
		qh->needs_fixup = 0;
	}

	uhci_dec_fsbr(uhci, urb);	/* Safe since it checks */
	uhci_free_urb_priv(uhci, urbp);

	switch (qh->type) {
	case USB_ENDPOINT_XFER_ISOC:
		/* Release bandwidth for Interrupt or Isoc. transfers */
		if (urb->bandwidth)
			usb_release_bandwidth(urb->dev, urb, 1);
		break;
	case USB_ENDPOINT_XFER_INT:
		/* Release bandwidth for Interrupt or Isoc. transfers */
		/* Make sure we don't release if we have a queued URB */
		if (list_empty(&qh->queue) && urb->bandwidth)
			usb_release_bandwidth(urb->dev, urb, 0);
		else
			/* bandwidth was passed on to queued URB, */
			/* so don't let usb_unlink_urb() release it */
			urb->bandwidth = 0;
		break;
	}

	spin_unlock(&uhci->lock);
	usb_hcd_giveback_urb(uhci_to_hcd(uhci), urb, regs);
	spin_lock(&uhci->lock);

	/* If the queue is now empty, we can unlink the QH and give up its
	 * reserved bandwidth. */
	if (list_empty(&qh->queue)) {
		uhci_unlink_qh(uhci, qh);

		/* Bandwidth stuff not yet implemented */
	}
}

/*
 * Scan the URBs in a QH's queue
 */
#define QH_FINISHED_UNLINKING(qh)			\
		(qh->state == QH_STATE_UNLINKING &&	\
		uhci->frame_number + uhci->is_stopped != qh->unlink_frame)

static void uhci_scan_qh(struct uhci_hcd *uhci, struct uhci_qh *qh,
		struct pt_regs *regs)
{
	struct urb_priv *urbp;
	struct urb *urb;
	int status;

	while (!list_empty(&qh->queue)) {
		urbp = list_entry(qh->queue.next, struct urb_priv, node);
		urb = urbp->urb;

		switch (qh->type) {
		case USB_ENDPOINT_XFER_CONTROL:
			status = uhci_result_control(uhci, urb);
			break;
		case USB_ENDPOINT_XFER_ISOC:
			status = uhci_result_isochronous(uhci, urb);
			break;
		default:	/* USB_ENDPOINT_XFER_BULK or _INT */
			status = uhci_result_common(uhci, urb);
			break;
		}
		if (status == -EINPROGRESS)
			break;

		spin_lock(&urb->lock);
		if (urb->status == -EINPROGRESS)	/* Not dequeued */
			urb->status = status;
		else
			status = -ECONNRESET;
		spin_unlock(&urb->lock);

		/* Dequeued but completed URBs can't be given back unless
		 * the QH is stopped or has finished unlinking. */
		if (status == -ECONNRESET &&
				!(qh->is_stopped || QH_FINISHED_UNLINKING(qh)))
			return;

		uhci_giveback_urb(uhci, qh, urb, regs);
		if (qh->is_stopped)
			break;
	}

	/* If the QH is neither stopped nor finished unlinking (normal case),
	 * our work here is done. */
 restart:
	if (!(qh->is_stopped || QH_FINISHED_UNLINKING(qh)))
		return;

	/* Otherwise give back each of the dequeued URBs */
	list_for_each_entry(urbp, &qh->queue, node) {
		urb = urbp->urb;
		if (urb->status != -EINPROGRESS) {
			uhci_save_toggle(qh, urb);
			uhci_giveback_urb(uhci, qh, urb, regs);
			goto restart;
		}
	}
	qh->is_stopped = 0;

	/* There are no more dequeued URBs.  If there are still URBs on the
	 * queue, the QH can now be re-activated. */
	if (!list_empty(&qh->queue)) {
		if (qh->needs_fixup)
			uhci_fixup_toggles(qh, 0);
		uhci_activate_qh(uhci, qh);
	}

	/* The queue is empty.  The QH can become idle if it is fully
	 * unlinked. */
	else if (QH_FINISHED_UNLINKING(qh))
		uhci_make_qh_idle(uhci, qh);
}

static void uhci_free_pending_tds(struct uhci_hcd *uhci)
{
	struct uhci_td *td, *tmp;

	list_for_each_entry_safe(td, tmp, &uhci->td_remove_list, remove_list) {
		list_del_init(&td->remove_list);

		uhci_free_td(uhci, td);
	}
}

/*
 * Process events in the schedule, but only in one thread at a time
 */
static void uhci_scan_schedule(struct uhci_hcd *uhci, struct pt_regs *regs)
{
	int i;
	struct uhci_qh *qh;

	/* Don't allow re-entrant calls */
	if (uhci->scan_in_progress) {
		uhci->need_rescan = 1;
		return;
	}
	uhci->scan_in_progress = 1;
 rescan:
	uhci->need_rescan = 0;

	uhci_clear_next_interrupt(uhci);
	uhci_get_current_frame_number(uhci);

	if (uhci->frame_number + uhci->is_stopped != uhci->td_remove_age)
		uhci_free_pending_tds(uhci);

	/* Go through all the QH queues and process the URBs in each one */
	for (i = 0; i < UHCI_NUM_SKELQH - 1; ++i) {
		uhci->next_qh = list_entry(uhci->skelqh[i]->node.next,
				struct uhci_qh, node);
		while ((qh = uhci->next_qh) != uhci->skelqh[i]) {
			uhci->next_qh = list_entry(qh->node.next,
					struct uhci_qh, node);
			uhci_scan_qh(uhci, qh, regs);
		}
	}

	if (uhci->need_rescan)
		goto rescan;
	uhci->scan_in_progress = 0;

	/* If the controller is stopped, we can finish these off right now */
	if (uhci->is_stopped)
		uhci_free_pending_tds(uhci);

	if (list_empty(&uhci->td_remove_list) &&
			list_empty(&uhci->skel_unlink_qh->node))
		uhci_clear_next_interrupt(uhci);
	else
		uhci_set_next_interrupt(uhci);
}

static void check_fsbr(struct uhci_hcd *uhci)
{
	/* For now, don't scan URBs for FSBR timeouts.
	 * Add it back in later... */

	/* Really disable FSBR */
	if (!uhci->fsbr && uhci->fsbrtimeout && time_after_eq(jiffies, uhci->fsbrtimeout)) {
		uhci->fsbrtimeout = 0;
		uhci->skel_term_qh->link = UHCI_PTR_TERM;
	}
}
