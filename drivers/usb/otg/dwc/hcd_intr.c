/*
 * DesignWare HS OTG controller driver
 * Copyright (C) 2006 Synopsys, Inc.
 * Portions Copyright (C) 2010 Applied Micro Circuits Corporation.
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License version 2 for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see http://www.gnu.org/licenses
 * or write to the Free Software Foundation, Inc., 51 Franklin Street,
 * Suite 500, Boston, MA 02110-1335 USA.
 *
 * Based on Synopsys driver version 2.60a
 * Modified by Mark Miesfeld <mmiesfeld@apm.com>
 * Modified by Stefan Roese <sr@denx.de>, DENX Software Engineering
 * Modified by Chuck Meade <chuck@theptrgroup.com>
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING BUT NOT LIMITED TO THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL SYNOPSYS, INC. BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES
 * (INCLUDING BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "hcd.h"

/* This file contains the implementation of the HCD Interrupt handlers.	*/
static const int erratum_usb09_patched;
static const int deferral_on = 1;
static const int nak_deferral_delay = 8;
static const int nyet_deferral_delay = 1;

/**
 * Handles the start-of-frame interrupt in host mode. Non-periodic
 * transactions may be queued to the DWC_otg controller for the current
 * (micro)frame. Periodic transactions may be queued to the controller for the
 * next (micro)frame.
 */
static int dwc_otg_hcd_handle_sof_intr(struct dwc_hcd *hcd)
{
	u32 hfnum = 0;
	struct list_head *qh_entry;
	struct dwc_qh *qh;
	enum dwc_transaction_type tr_type;
	u32 gintsts = 0;

	hfnum =
	    dwc_reg_read(hcd->core_if->host_if->host_global_regs,
		       DWC_HFNUM);

	hcd->frame_number = DWC_HFNUM_FRNUM_RD(hfnum);

	/* Determine whether any periodic QHs should be executed. */
	qh_entry = hcd->periodic_sched_inactive.next;
	while (qh_entry != &hcd->periodic_sched_inactive) {
		qh = list_entry(qh_entry, struct dwc_qh, qh_list_entry);
		qh_entry = qh_entry->next;

		/*
		 * If needed, move QH to the ready list to be executed next
		 * (micro)frame.
		 */
		if (dwc_frame_num_le(qh->sched_frame, hcd->frame_number))
			list_move(&qh->qh_list_entry,
				  &hcd->periodic_sched_ready);
	}

	tr_type = dwc_otg_hcd_select_transactions(hcd);
	if (tr_type != DWC_OTG_TRANSACTION_NONE)
		dwc_otg_hcd_queue_transactions(hcd, tr_type);

	/* Clear interrupt */
	gintsts |= DWC_INTMSK_STRT_OF_FRM;
	dwc_reg_write(gintsts_reg(hcd), 0, gintsts);
	return 1;
}

/**
 * Handles the Rx Status Queue Level Interrupt, which indicates that there is at
 * least one packet in the Rx FIFO.  The packets are moved from the FIFO to
 * memory if the DWC_otg controller is operating in Slave mode.
 */
static int dwc_otg_hcd_handle_rx_status_q_level_intr(struct dwc_hcd *hcd)
{
	u32 grxsts;
	struct dwc_hc *hc;
	struct dwc_qtd *qtd;

	grxsts = dwc_reg_read(hcd->core_if->core_global_regs, DWC_GRXSTSP);
	hc = hcd->hc_ptr_array[grxsts & DWC_HM_RXSTS_CHAN_NUM_RD(grxsts)];

	/* Packet Status */
	switch (DWC_HM_RXSTS_PKT_STS_RD(grxsts)) {
	case DWC_GRXSTS_PKTSTS_IN:
		/* Read the data into the host buffer. */
		if (DWC_HM_RXSTS_BYTE_CNT_RD(grxsts) > 0) {
			if(hc->qh != 0) {
				qtd = list_entry(hc->qh->qtd_list.next,
					struct dwc_qtd, qtd_list_entry);
				usb_hcd_unmap_urb_for_dma(
					dwc_otg_hcd_to_hcd(hcd), qtd->urb);
			}
			dwc_otg_read_packet(hcd->core_if, hc->xfer_buff,
					    DWC_HM_RXSTS_BYTE_CNT_RD(grxsts));
			/* Update the HC fields for the next packet received. */
			hc->xfer_count += DWC_HM_RXSTS_BYTE_CNT_RD(grxsts);
			hc->xfer_buff += DWC_HM_RXSTS_BYTE_CNT_RD(grxsts);
		}
	case DWC_GRXSTS_PKTSTS_IN_XFER_COMP:
	case DWC_GRXSTS_PKTSTS_DATA_TOGGLE_ERR:
	case DWC_GRXSTS_PKTSTS_CH_HALTED:
		/* Handled in interrupt, just ignore data */
		break;
	default:
		pr_err("RX_STS_Q Interrupt: Unknown status %d\n",
		       DWC_HM_RXSTS_PKT_STS_RD(grxsts));
		break;
	}
	return 1;
}

/**
 * This interrupt occurs when the non-periodic Tx FIFO is half-empty. More
 * data packets may be written to the FIFO for OUT transfers. More requests
 * may be written to the non-periodic request queue for IN transfers. This
 * interrupt is enabled only in Slave mode.
 */
static int dwc_otg_hcd_handle_np_tx_fifo_empty_intr(struct dwc_hcd *hcd)
{
	dwc_otg_hcd_queue_transactions(hcd, DWC_OTG_TRANSACTION_NON_PERIODIC);
	return 1;
}

/**
 * This interrupt occurs when the periodic Tx FIFO is half-empty. More data
 * packets may be written to the FIFO for OUT transfers. More requests may be
 * written to the periodic request queue for IN transfers. This interrupt is
 * enabled only in Slave mode.
 */
static int dwc_otg_hcd_handle_perio_tx_fifo_empty_intr(struct dwc_hcd *hcd)
{
	dwc_otg_hcd_queue_transactions(hcd, DWC_OTG_TRANSACTION_PERIODIC);
	return 1;
}

/**
 * Helper function to handle the port enable changed interrupt when the port
 * becomes enabled.  Checks if we need to adjust the PHY clock speed for low
 * power and  adjusts it if needed.
 */
/**
 * There are multiple conditions that can cause a port interrupt. This function
 * determines which interrupt conditions have occurred and handles them
 * appropriately.
 */
static int dwc_otg_hcd_handle_port_intr(struct dwc_hcd *hcd)
{
	int retval = 0;
	u32 hprt0;
	u32 hprt0_modify;

	hprt0 = dwc_reg_read(hcd->core_if->host_if->hprt0, 0);
	hprt0_modify = dwc_reg_read(hcd->core_if->host_if->hprt0, 0);

	/*
	 * Clear appropriate bits in HPRT0 to clear the interrupt bit in
	 * GINTSTS
	 */
	hprt0_modify = DWC_HPRT0_PRT_ENA_RW(hprt0_modify, 0);
	hprt0_modify = DWC_HPRT0_PRT_CONN_DET_RW(hprt0_modify, 0);
	hprt0_modify = DWC_HPRT0_PRT_ENA_DIS_CHG_RW(hprt0_modify, 0);
	hprt0_modify = DWC_HPRT0_PRT_OVRCURR_CHG_RW(hprt0_modify, 0);

	/* Port connect detected interrupt */
	if (DWC_HPRT0_PRT_CONN_DET_RD(hprt0)) {
		/* Set the status flags and clear interrupt */
		hcd->flags.b.port_connect_status_change = 1;
		hcd->flags.b.port_connect_status = 1;
		hprt0_modify = DWC_HPRT0_PRT_CONN_DET_RW(hprt0_modify, 1);
		/* We need to wake up the USB core */
		if(hcd_to_bus(dwc_otg_hcd_to_hcd(hcd)))
			usb_hcd_resume_root_hub(dwc_otg_hcd_to_hcd(hcd));

		/* B-Device has connected, Delete the connection timer. */
		del_timer(&hcd->conn_timer);

		/*
		 * The Hub driver asserts a reset when it sees port connect
		 * status change flag
		 */
		retval |= 1;
	}

	/* Port enable changed interrupt */
	if (DWC_HPRT0_PRT_ENA_DIS_CHG_RD(hprt0)) {
		/* Set the internal flag if the port was disabled */
			hcd->flags.b.port_enable_change = 1;

		/* Clear the interrupt */
		hprt0_modify = DWC_HPRT0_PRT_ENA_DIS_CHG_RW(hprt0_modify, 1);
		retval |= 1;
	}

	/* Overcurrent change interrupt */
	if (DWC_HPRT0_PRT_OVRCURR_CHG_RD(hprt0)) {
		hcd->flags.b.port_over_current_change = 1;
		hprt0_modify = DWC_HPRT0_PRT_OVRCURR_CHG_RW(hprt0_modify, 1);
		retval |= 1;
	}

	/* Clear the port interrupts */
	dwc_reg_write(hcd->core_if->host_if->hprt0, 0, hprt0_modify);
	return retval;
}

/**
 * Gets the actual length of a transfer after the transfer halts. halt_status
 * holds the reason for the halt.
 *
 * For IN transfers where halt_status is DWC_OTG_HC_XFER_COMPLETE, _short_read
 * is set to 1 upon return if less than the requested number of bytes were
 * transferred. Otherwise, _short_read is set to 0 upon return. _short_read may
 * also be NULL on entry, in which case it remains unchanged.
 */
static u32 get_actual_xfer_length(struct dwc_hc *hc, ulong regs,
				  struct dwc_qtd *qtd,
				  enum dwc_halt_status halt_status,
				  int *_short_read)
{
	u32 hctsiz = 0;
	u32 length;

	if (_short_read)
		*_short_read = 0;

	hctsiz = dwc_reg_read(regs, DWC_HCTSIZ);
	if (halt_status == DWC_OTG_HC_XFER_COMPLETE) {
		if (hc->ep_is_in) {
			length = hc->xfer_len - DWC_HCTSIZ_XFER_SIZE_RD(hctsiz);
			if (_short_read)
				*_short_read =
				    (DWC_HCTSIZ_XFER_SIZE_RD(hctsiz) != 0);
		} else if (hc->qh->do_split) {
			length = qtd->ssplit_out_xfer_count;
		} else {
			length = hc->xfer_len;
		}
	} else {
		/*
		 * Must use the hctsiz.pktcnt field to determine how much data
		 * has been transferred. This field reflects the number of
		 * packets that have been transferred via the USB. This is
		 * always an integral number of packets if the transfer was
		 * halted before its normal completion. (Can't use the
		 * hctsiz.xfersize field because that reflects the number of
		 * bytes transferred via the AHB, not the USB).
		 */
		length = (hc->start_pkt_count - DWC_HCTSIZ_PKT_CNT_RD(hctsiz)) *
		    hc->max_packet;
	}
	return length;
}

/**
 * Updates the state of the URB after a Transfer Complete interrupt on the
 * host channel. Updates the actual_length field of the URB based on the
 * number of bytes transferred via the host channel. Sets the URB status
 * if the data transfer is finished.
 */
static int update_urb_state_xfer_comp(struct dwc_hc *hc,
				      ulong regs, struct urb *urb,
				      struct dwc_qtd *qtd, int *status)
{
	int xfer_done = 0;
	int short_read = 0;

	urb->actual_length += get_actual_xfer_length(hc, regs, qtd,
						     DWC_OTG_HC_XFER_COMPLETE,
						     &short_read);

	if (short_read || urb->actual_length == urb->transfer_buffer_length) {
		xfer_done = 1;
		if (short_read && (urb->transfer_flags & URB_SHORT_NOT_OK))
			*status = -EREMOTEIO;
		else
			*status = 0;
	}
	return xfer_done;
}

/*
 * Save the starting data toggle for the next transfer. The data toggle is
 * saved in the QH for non-control transfers and it's saved in the QTD for
 * control transfers.
 */
static void save_data_toggle(struct dwc_hc *hc, ulong regs, struct dwc_qtd *qtd)
{
	u32 hctsiz = 0;
	hctsiz = dwc_reg_read(regs, DWC_HCTSIZ);

	if (hc->ep_type != DWC_OTG_EP_TYPE_CONTROL) {
		struct dwc_qh *qh = hc->qh;

		if (DWC_HCTSIZ_PKT_PID_RD(hctsiz) == DWC_HCTSIZ_DATA0)
			qh->data_toggle = DWC_OTG_HC_PID_DATA0;
		else
			qh->data_toggle = DWC_OTG_HC_PID_DATA1;
	} else {
		if (DWC_HCTSIZ_PKT_PID_RD(hctsiz) == DWC_HCTSIZ_DATA0)
			qtd->data_toggle = DWC_OTG_HC_PID_DATA0;
		else
			qtd->data_toggle = DWC_OTG_HC_PID_DATA1;
	}
}

/**
 * Frees the first QTD in the QH's list if free_qtd is 1. For non-periodic
 * QHs, removes the QH from the active non-periodic schedule. If any QTDs are
 * still linked to the QH, the QH is added to the end of the inactive
 * non-periodic schedule. For periodic QHs, removes the QH from the periodic
 * schedule if no more QTDs are linked to the QH.
 */
static void deactivate_qh(struct dwc_hcd *hcd, struct dwc_qh *qh, int free_qtd)
{
	int continue_split = 0;
	struct dwc_qtd *qtd;

	qtd = list_entry(qh->qtd_list.next, struct dwc_qtd, qtd_list_entry);
	if (qtd->complete_split)
		continue_split = 1;
	else if (qtd->isoc_split_pos == DWC_HCSPLIT_XACTPOS_MID ||
		 qtd->isoc_split_pos == DWC_HCSPLIT_XACTPOS_END)
		continue_split = 1;

	if (free_qtd) {
		dwc_otg_hcd_qtd_remove(qtd);
		continue_split = 0;
	}

	qh->channel = NULL;
	qh->qtd_in_process = NULL;
	dwc_otg_hcd_qh_deactivate(hcd, qh, continue_split);
}

/**
 * Updates the state of an Isochronous URB when the transfer is stopped for
 * any reason. The fields of the current entry in the frame descriptor array
 * are set based on the transfer state and the input status. Completes the
 * Isochronous URB if all the URB frames have been completed.
 */
static enum dwc_halt_status update_isoc_urb_state(struct dwc_hcd *hcd,
						  struct dwc_hc *hc, u32 regs,
						  struct dwc_qtd *qtd,
						  enum dwc_halt_status status)
{
	struct urb *urb = qtd->urb;
	enum dwc_halt_status ret_val = status;
	struct usb_iso_packet_descriptor *frame_desc;
	frame_desc = &urb->iso_frame_desc[qtd->isoc_frame_index];

	switch (status) {
	case DWC_OTG_HC_XFER_COMPLETE:
		frame_desc->status = 0;
		frame_desc->actual_length =
		    get_actual_xfer_length(hc, regs, qtd, status, NULL);
		break;
	case DWC_OTG_HC_XFER_FRAME_OVERRUN:
		urb->error_count++;
		if (hc->ep_is_in)
			frame_desc->status = -ENOSR;
		else
			frame_desc->status = -ECOMM;

		frame_desc->actual_length = 0;
		break;
	case DWC_OTG_HC_XFER_BABBLE_ERR:
		/* Don't need to update actual_length in this case. */
		urb->error_count++;
		frame_desc->status = -EOVERFLOW;
		break;
	case DWC_OTG_HC_XFER_XACT_ERR:
		urb->error_count++;
		frame_desc->status = -EPROTO;
		frame_desc->actual_length =
		    get_actual_xfer_length(hc, regs, qtd, status, NULL);
	default:
		pr_err("%s: Unhandled halt_status (%d)\n", __func__, status);
		BUG();
		break;
	}

	if (++qtd->isoc_frame_index == urb->number_of_packets) {
		/*
		 * urb->status is not used for isoc transfers.
		 * The individual frame_desc statuses are used instead.
		 */
		dwc_otg_hcd_complete_urb(hcd, urb, 0);
		ret_val = DWC_OTG_HC_XFER_URB_COMPLETE;
	} else {
		ret_val = DWC_OTG_HC_XFER_COMPLETE;
	}
	return ret_val;
}

/**
 * Releases a host channel for use by other transfers. Attempts to select and
 * queue more transactions since at least one host channel is available.
 */
static void release_channel(struct dwc_hcd *hcd, struct dwc_hc *hc,
			    struct dwc_qtd *qtd,
			    enum dwc_halt_status halt_status, int *must_free)
{
	enum dwc_transaction_type tr_type;
	int free_qtd;
	int deact = 1;
	struct dwc_qh *qh;
	int retry_delay = 1;

	switch (halt_status) {
	case DWC_OTG_HC_XFER_NYET:
	case DWC_OTG_HC_XFER_NAK:
		if (halt_status == DWC_OTG_HC_XFER_NYET)
			retry_delay = nyet_deferral_delay;
		else
			retry_delay = nak_deferral_delay;
		free_qtd = 0;
		if (deferral_on && hc->do_split) {
			qh = hc->qh;
			if (qh)
				deact = dwc_otg_hcd_qh_deferr(hcd, qh,
							      retry_delay);
		}
		break;
	case DWC_OTG_HC_XFER_URB_COMPLETE:
		free_qtd = 1;
		break;
	case DWC_OTG_HC_XFER_AHB_ERR:
	case DWC_OTG_HC_XFER_STALL:
	case DWC_OTG_HC_XFER_BABBLE_ERR:
		free_qtd = 1;
		break;
	case DWC_OTG_HC_XFER_XACT_ERR:
		if (qtd->error_count >= 3) {
			free_qtd = 1;
			dwc_otg_hcd_complete_urb(hcd, qtd->urb, -EPROTO);
		} else {
			free_qtd = 0;
		}
		break;
	case DWC_OTG_HC_XFER_URB_DEQUEUE:
		/*
		 * The QTD has already been removed and the QH has been
		 * deactivated. Don't want to do anything except release the
		 * host channel and try to queue more transfers.
		 */
		goto cleanup;
	case DWC_OTG_HC_XFER_NO_HALT_STATUS:
		pr_err("%s: No halt_status, channel %d\n", __func__,
		       hc->hc_num);
		free_qtd = 0;
		break;
	default:
		free_qtd = 0;
		break;
	}
	if (free_qtd)
		/* must_free pre-initialized to zero */
		*must_free = 1;
	if (deact)
		deactivate_qh(hcd, hc->qh, free_qtd);

cleanup:
	/*
	 * Release the host channel for use by other transfers. The cleanup
	 * function clears the channel interrupt enables and conditions, so
	 * there's no need to clear the Channel Halted interrupt separately.
	 */
	dwc_otg_hc_cleanup(hcd->core_if, hc);
	list_add_tail(&hc->hc_list_entry, &hcd->free_hc_list);
	hcd->available_host_channels++;
	/* Try to queue more transfers now that there's a free channel. */
	if (!erratum_usb09_patched) {
		tr_type = dwc_otg_hcd_select_transactions(hcd);
		if (tr_type != DWC_OTG_TRANSACTION_NONE)
			dwc_otg_hcd_queue_transactions(hcd, tr_type);
	}
}

/**
 * Halts a host channel. If the channel cannot be halted immediately because
 * the request queue is full, this function ensures that the FIFO empty
 * interrupt for the appropriate queue is enabled so that the halt request can
 * be queued when there is space in the request queue.
 *
 * This function may also be called in DMA mode. In that case, the channel is
 * simply released since the core always halts the channel automatically in
 * DMA mode.
 */
static void halt_channel(struct dwc_hcd *hcd, struct dwc_hc *hc,
			 struct dwc_qtd *qtd, enum dwc_halt_status halt_status,
			 int *must_free)
{
	if (hcd->core_if->dma_enable) {
		release_channel(hcd, hc, qtd, halt_status, must_free);
		return;
	}

	/* Slave mode processing... */
	dwc_otg_hc_halt(hcd->core_if, hc, halt_status);
	if (hc->halt_on_queue) {
		u32 gintmsk = 0;

		if (hc->ep_type == DWC_OTG_EP_TYPE_CONTROL ||
		    hc->ep_type == DWC_OTG_EP_TYPE_BULK) {
			/*
			 * Make sure the Non-periodic Tx FIFO empty interrupt
			 * is enabled so that the non-periodic schedule will
			 * be processed.
			 */
			gintmsk |= DWC_INTMSK_NP_TXFIFO_EMPT;
			dwc_reg_modify(gintmsk_reg(hcd), 0, 0, gintmsk);
		} else {
			/*
			 * Move the QH from the periodic queued schedule to
			 * the periodic assigned schedule. This allows the
			 * halt to be queued when the periodic schedule is
			 * processed.
			 */
			list_move(&hc->qh->qh_list_entry,
				  &hcd->periodic_sched_assigned);

			/*
			 * Make sure the Periodic Tx FIFO Empty interrupt is
			 * enabled so that the periodic schedule will be
			 * processed.
			 */
			gintmsk |= DWC_INTMSK_P_TXFIFO_EMPTY;
			dwc_reg_modify(gintmsk_reg(hcd), 0, 0, gintmsk);
		}
	}
}

/**
 * Performs common cleanup for non-periodic transfers after a Transfer
 * Complete interrupt. This function should be called after any endpoint type
 * specific handling is finished to release the host channel.
 */
static void complete_non_periodic_xfer(struct dwc_hcd *hcd, struct dwc_hc *hc,
				       ulong regs, struct dwc_qtd *qtd,
				       enum dwc_halt_status halt_status,
				       int *must_free)
{
	u32 hcint;

	qtd->error_count = 0;
	hcint = dwc_reg_read(regs, DWC_HCINT);
	if (DWC_HCINT_NYET_RESP_REC_RD(hcint)) {
		u32 hcint_clear = 0;

		hcint_clear = DWC_HCINT_NYET_RESP_REC_RW(hcint_clear, 1);
		/*
		 * Got a NYET on the last transaction of the transfer. This
		 * means that the endpoint should be in the PING state at the
		 * beginning of the next transfer.
		 */
		hc->qh->ping_state = 1;
		dwc_reg_write(regs, DWC_HCINT, hcint_clear);
	}

	/*
	 * Always halt and release the host channel to make it available for
	 * more transfers. There may still be more phases for a control
	 * transfer or more data packets for a bulk transfer at this point,
	 * but the host channel is still halted. A channel will be reassigned
	 * to the transfer when the non-periodic schedule is processed after
	 * the channel is released. This allows transactions to be queued
	 * properly via dwc_otg_hcd_queue_transactions, which also enables the
	 * Tx FIFO Empty interrupt if necessary.
	 *
	 * IN transfers in Slave mode require an explicit disable to
	 * halt the channel. (In DMA mode, this call simply releases
	 * the channel.)
	 *
	 * The channel is automatically disabled by the core for OUT
	 * transfers in Slave mode.
	 */
	if (hc->ep_is_in)
		halt_channel(hcd, hc, qtd, halt_status, must_free);
	else
		release_channel(hcd, hc, qtd, halt_status, must_free);
}

/**
 * Performs common cleanup for periodic transfers after a Transfer Complete
 * interrupt. This function should be called after any endpoint type specific
 * handling is finished to release the host channel.
 */
static void complete_periodic_xfer(struct dwc_hcd *hcd, struct dwc_hc *hc,
				   ulong regs, struct dwc_qtd *qtd,
				   enum dwc_halt_status halt_status,
				   int *must_free)
{
	u32 hctsiz = 0;

	hctsiz = dwc_reg_read(regs, DWC_HCTSIZ);
	qtd->error_count = 0;

	/*
	 * For OUT transfers and 0 packet count, the Core halts the channel,
	 * otherwise, Flush any outstanding requests from the Tx queue.
	 */
	if (!hc->ep_is_in || (DWC_HCTSIZ_PKT_CNT_RD(hctsiz) == 0))
		release_channel(hcd, hc, qtd, halt_status, must_free);
	else
		halt_channel(hcd, hc, qtd, halt_status, must_free);
}

/**
 * Handles a host channel Transfer Complete interrupt. This handler may be
 * called in either DMA mode or Slave mode.
 */
static int handle_hc_xfercomp_intr(struct dwc_hcd *hcd, struct dwc_hc *hc,
				   ulong regs, struct dwc_qtd *qtd,
				   int *must_free)
{
	int urb_xfer_done;
	enum dwc_halt_status halt_status = DWC_OTG_HC_XFER_COMPLETE;
	struct urb *urb = qtd->urb;
	int pipe_type = usb_pipetype(urb->pipe);
	int status = -EINPROGRESS;
	u32 hcintmsk = 0;

	/* Handle xfer complete on CSPLIT. */
	if (hc->qh->do_split)
		qtd->complete_split = 0;

	/* Update the QTD and URB states. */
	switch (pipe_type) {
	case PIPE_CONTROL:
		switch (qtd->control_phase) {
		case DWC_OTG_CONTROL_SETUP:
			if (urb->transfer_buffer_length > 0)
				qtd->control_phase = DWC_OTG_CONTROL_DATA;
			else
				qtd->control_phase = DWC_OTG_CONTROL_STATUS;
			halt_status = DWC_OTG_HC_XFER_COMPLETE;
			break;
		case DWC_OTG_CONTROL_DATA:
			urb_xfer_done = update_urb_state_xfer_comp(hc, regs,
								   urb, qtd,
								   &status);
			if (urb_xfer_done)
				qtd->control_phase = DWC_OTG_CONTROL_STATUS;
			else
				save_data_toggle(hc, regs, qtd);
			halt_status = DWC_OTG_HC_XFER_COMPLETE;
			break;
		case DWC_OTG_CONTROL_STATUS:
			if (status == -EINPROGRESS)
				status = 0;
			dwc_otg_hcd_complete_urb(hcd, urb, status);
			halt_status = DWC_OTG_HC_XFER_URB_COMPLETE;
			break;
		}
		complete_non_periodic_xfer(hcd, hc, regs, qtd,
					   halt_status, must_free);
		break;
	case PIPE_BULK:
		urb_xfer_done = update_urb_state_xfer_comp(hc, regs, urb, qtd,
							   &status);
		if (urb_xfer_done) {
			dwc_otg_hcd_complete_urb(hcd, urb, status);
			halt_status = DWC_OTG_HC_XFER_URB_COMPLETE;
		} else {
			halt_status = DWC_OTG_HC_XFER_COMPLETE;
		}

		save_data_toggle(hc, regs, qtd);
		complete_non_periodic_xfer(hcd, hc, regs, qtd,
					   halt_status, must_free);
		break;
	case PIPE_INTERRUPT:
		update_urb_state_xfer_comp(hc, regs, urb, qtd, &status);
		/*
		 * Interrupt URB is done on the first transfer complete
		 * interrupt.
		 */
		dwc_otg_hcd_complete_urb(hcd, urb, status);
		save_data_toggle(hc, regs, qtd);
		complete_periodic_xfer(hcd, hc, regs, qtd,
				       DWC_OTG_HC_XFER_URB_COMPLETE, must_free);
		break;
	case PIPE_ISOCHRONOUS:
		if (qtd->isoc_split_pos == DWC_HCSPLIT_XACTPOS_ALL) {
			halt_status = update_isoc_urb_state(hcd, hc, regs, qtd,
						    DWC_OTG_HC_XFER_COMPLETE);
		}
		complete_periodic_xfer(hcd, hc, regs, qtd,
				       halt_status, must_free);
		break;
	}

	/* disable xfercompl */
	hcintmsk = DWC_HCINTMSK_TXFER_CMPL_RW(hcintmsk, 1);
	dwc_reg_modify(regs, DWC_HCINTMSK, hcintmsk, 0);

	return 1;
}

/**
 * Handles a host channel STALL interrupt. This handler may be called in
 * either DMA mode or Slave mode.
 */
static int handle_hc_stall_intr(struct dwc_hcd *hcd, struct dwc_hc *hc,
				u32 regs, struct dwc_qtd *qtd, int *must_free)
{
	struct urb *urb = qtd->urb;
	int pipe_type = usb_pipetype(urb->pipe);
	u32 hcintmsk = 0;

	if (pipe_type == PIPE_CONTROL)
		dwc_otg_hcd_complete_urb(hcd, qtd->urb, -EPIPE);

	if (pipe_type == PIPE_BULK || pipe_type == PIPE_INTERRUPT) {
		dwc_otg_hcd_complete_urb(hcd, qtd->urb, -EPIPE);
		/*
		 * USB protocol requires resetting the data toggle for bulk
		 * and interrupt endpoints when a CLEAR_FEATURE(ENDPOINT_HALT)
		 * setup command is issued to the endpoint. Anticipate the
		 * CLEAR_FEATURE command since a STALL has occurred and reset
		 * the data toggle now.
		 */
		hc->qh->data_toggle = 0;
	}

	halt_channel(hcd, hc, qtd, DWC_OTG_HC_XFER_STALL, must_free);
	/* disable stall */
	hcintmsk = DWC_HCINTMSK_STALL_RESP_REC_RW(hcintmsk, 1);
	dwc_reg_modify(regs, DWC_HCINTMSK, hcintmsk, 0);

	return 1;
}

/**
 * Updates the state of the URB when a transfer has been stopped due to an
 * abnormal condition before the transfer completes. Modifies the
 * actual_length field of the URB to reflect the number of bytes that have
 * actually been transferred via the host channel.
 */
static void update_urb_state_xfer_intr(struct dwc_hc *hc,
				       u32 regs, struct urb *urb,
				       struct dwc_qtd *qtd,
				       enum dwc_halt_status sts)
{
	u32 xfr_len = get_actual_xfer_length(hc, regs, qtd, sts, NULL);
	urb->actual_length += xfr_len;
}

/**
 * Handles a host channel NAK interrupt. This handler may be called in either
 * DMA mode or Slave mode.
 */
static int handle_hc_nak_intr(struct dwc_hcd *hcd, struct dwc_hc *hc,
			      u32 regs, struct dwc_qtd *qtd, int *must_free)
{
	u32 hcintmsk = 0;

	/*
	 * Handle NAK for IN/OUT SSPLIT/CSPLIT transfers, bulk, control, and
	 * interrupt.  Re-start the SSPLIT transfer.
	 */
	if (hc->do_split) {
		if (hc->complete_split)
			qtd->error_count = 0;

		qtd->complete_split = 0;
		halt_channel(hcd, hc, qtd, DWC_OTG_HC_XFER_NAK, must_free);
		goto handle_nak_done;
	}
	switch (usb_pipetype(qtd->urb->pipe)) {
	case PIPE_CONTROL:
	case PIPE_BULK:
		if (hcd->core_if->dma_enable && hc->ep_is_in) {
			/*
			 * NAK interrupts are enabled on bulk/control IN
			 * transfers in DMA mode for the sole purpose of
			 * resetting the error count after a transaction error
			 * occurs. The core will continue transferring data.
			 */
			qtd->error_count = 0;
			goto handle_nak_done;
		}

		/*
		 * NAK interrupts normally occur during OUT transfers in DMA
		 * or Slave mode. For IN transfers, more requests will be
		 * queued as request queue space is available.
		 */
		qtd->error_count = 0;
		if (!hc->qh->ping_state) {
			update_urb_state_xfer_intr(hc, regs, qtd->urb, qtd,
						   DWC_OTG_HC_XFER_NAK);

			save_data_toggle(hc, regs, qtd);
			if (qtd->urb->dev->speed == USB_SPEED_HIGH)
				hc->qh->ping_state = 1;
		}

		/*
		 * Halt the channel so the transfer can be re-started from
		 * the appropriate point or the PING protocol will
		 * start/continue.
		 */
		halt_channel(hcd, hc, qtd, DWC_OTG_HC_XFER_NAK, must_free);
		break;
	case PIPE_INTERRUPT:
		qtd->error_count = 0;
		halt_channel(hcd, hc, qtd, DWC_OTG_HC_XFER_NAK, must_free);
		break;
	case PIPE_ISOCHRONOUS:
		/* Should never get called for isochronous transfers. */
		BUG();
		break;
	}

handle_nak_done:
	/* disable nak */
	hcintmsk = DWC_HCINTMSK_NAK_RESP_REC_RW(hcintmsk, 1);
	dwc_reg_modify(regs, DWC_HCINTMSK, hcintmsk, 0);

	return 1;
}

/**
 * Helper function for handle_hc_ack_intr().  Sets the split values for an ACK
 * on SSPLIT for ISOC OUT.
 */
static void set_isoc_out_vals(struct dwc_hc *hc, struct dwc_qtd *qtd)
{
	struct usb_iso_packet_descriptor *frame_desc;

	switch (hc->xact_pos) {
	case DWC_HCSPLIT_XACTPOS_ALL:
		break;
	case DWC_HCSPLIT_XACTPOS_END:
		qtd->isoc_split_pos = DWC_HCSPLIT_XACTPOS_ALL;
		qtd->isoc_split_offset = 0;
		break;
	case DWC_HCSPLIT_XACTPOS_BEGIN:
	case DWC_HCSPLIT_XACTPOS_MID:
		/*
		 * For BEGIN or MID, calculate the length for the next
		 * microframe to determine the correct SSPLIT token, either MID
		 * or END.
		 */
		frame_desc = &qtd->urb->iso_frame_desc[qtd->isoc_frame_index];
		qtd->isoc_split_offset += 188;

		if ((frame_desc->length - qtd->isoc_split_offset) <= 188)
			qtd->isoc_split_pos = DWC_HCSPLIT_XACTPOS_END;
		else
			qtd->isoc_split_pos = DWC_HCSPLIT_XACTPOS_MID;

		break;
	}
}

/**
 * Handles a host channel ACK interrupt. This interrupt is enabled when
 * performing the PING protocol in Slave mode, when errors occur during
 * either Slave mode or DMA mode, and during Start Split transactions.
 */
static int handle_hc_ack_intr(struct dwc_hcd *hcd, struct dwc_hc *hc,
			      u32 regs, struct dwc_qtd *qtd, int *must_free)
{
	u32 hcintmsk = 0;

	if (hc->do_split) {
		/* Handle ACK on SSPLIT. ACK should not occur in CSPLIT. */
		if (!hc->ep_is_in && hc->data_pid_start != DWC_OTG_HC_PID_SETUP)
			qtd->ssplit_out_xfer_count = hc->xfer_len;

		/* Don't need complete for isochronous out transfers. */
		if (!(hc->ep_type == DWC_OTG_EP_TYPE_ISOC && !hc->ep_is_in))
			qtd->complete_split = 1;

		if (hc->ep_type == DWC_OTG_EP_TYPE_ISOC && !hc->ep_is_in)
			set_isoc_out_vals(hc, qtd);
		else
			halt_channel(hcd, hc, qtd, DWC_OTG_HC_XFER_ACK,
				     must_free);
	} else {
		qtd->error_count = 0;
		if (hc->qh->ping_state) {
			hc->qh->ping_state = 0;

			/*
			 * Halt the channel so the transfer can be re-started
			 * from the appropriate point. This only happens in
			 * Slave mode. In DMA mode, the ping_state is cleared
			 * when the transfer is started because the core
			 * automatically executes the PING, then the transfer.
			 */
			halt_channel(hcd, hc, qtd, DWC_OTG_HC_XFER_ACK,
				     must_free);
		}
	}

	/*
	 * If the ACK occurred when _not_ in the PING state, let the channel
	 * continue transferring data after clearing the error count.
	 */
	/* disable ack */
	hcintmsk = DWC_HCINTMSK_ACK_RESP_REC_RW(hcintmsk, 1);
	dwc_reg_modify(regs, DWC_HCINTMSK, hcintmsk, 0);

	return 1;
}

/**
 * Handles a host channel NYET interrupt. This interrupt should only occur on
 * Bulk and Control OUT endpoints and for complete split transactions. If a
 * NYET occurs at the same time as a Transfer Complete interrupt, it is
 * handled in the xfercomp interrupt handler, not here. This handler may be
 * called in either DMA mode or Slave mode.
 */
static int handle_hc_nyet_intr(struct dwc_hcd *hcd, struct dwc_hc *hc,
			       u32 regs, struct dwc_qtd *qtd, int *must_free)
{
	u32 hcintmsk = 0;
	u32 hcint_clear = 0;

	/*
	 * NYET on CSPLIT
	 * re-do the CSPLIT immediately on non-periodic
	 */
	if (hc->do_split && hc->complete_split) {
		if (hc->ep_type == DWC_OTG_EP_TYPE_INTR ||
		    hc->ep_type == DWC_OTG_EP_TYPE_ISOC) {
			int frnum =
			    dwc_otg_hcd_get_frame_number(dwc_otg_hcd_to_hcd
							 (hcd));
			if (dwc_full_frame_num(frnum) !=
			    dwc_full_frame_num(hc->qh->sched_frame)) {
				qtd->complete_split = 0;
				halt_channel(hcd, hc, qtd,
					     DWC_OTG_HC_XFER_XACT_ERR,
					     must_free);
				goto handle_nyet_done;
			}
		}
		halt_channel(hcd, hc, qtd, DWC_OTG_HC_XFER_NYET, must_free);
		goto handle_nyet_done;
	}
	hc->qh->ping_state = 1;
	qtd->error_count = 0;
	update_urb_state_xfer_intr(hc, regs, qtd->urb, qtd,
				   DWC_OTG_HC_XFER_NYET);
	save_data_toggle(hc, regs, qtd);
	/*
	 * Halt the channel and re-start the transfer so the PING
	 * protocol will start.
	 */
	halt_channel(hcd, hc, qtd, DWC_OTG_HC_XFER_NYET, must_free);

handle_nyet_done:
	/* disable nyet */
	hcintmsk = DWC_HCINTMSK_NYET_RESP_REC_RW(hcintmsk, 1);
	dwc_reg_modify(regs, DWC_HCINTMSK, hcintmsk, 0);
	/* clear nyet */
	hcint_clear = DWC_HCINT_NYET_RESP_REC_RW(hcint_clear, 1);
	dwc_reg_write(regs, DWC_HCINT, hcint_clear);
	return 1;
}

/**
 * Handles a host channel babble interrupt. This handler may be called in
 * either DMA mode or Slave mode.
 */
static int handle_hc_babble_intr(struct dwc_hcd *hcd, struct dwc_hc *hc,
				 u32 regs, struct dwc_qtd *qtd, int *must_free)
{
	u32 hcintmsk = 0;

	if (hc->ep_type != DWC_OTG_EP_TYPE_ISOC) {
		dwc_otg_hcd_complete_urb(hcd, qtd->urb, -EOVERFLOW);
		halt_channel(hcd, hc, qtd, DWC_OTG_HC_XFER_BABBLE_ERR,
			     must_free);
	} else {
		enum dwc_halt_status halt_status;
		halt_status = update_isoc_urb_state(hcd, hc, regs, qtd,
						    DWC_OTG_HC_XFER_BABBLE_ERR);
		halt_channel(hcd, hc, qtd, halt_status, must_free);
	}
	/* disable bblerr */
	hcintmsk = DWC_HCINTMSK_BBL_ERR_RW(hcintmsk, 1);
	dwc_reg_modify(regs, DWC_HCINTMSK, hcintmsk, 0);
	return 1;
}

/**
 * Handles a host channel AHB error interrupt. This handler is only called in
 * DMA mode.
 */
static int handle_hc_ahberr_intr(struct dwc_hcd *hcd, struct dwc_hc *hc,
				 u32 regs, struct dwc_qtd *qtd)
{
	u32 hcchar;
	u32 hcsplt;
	u32 hctsiz = 0;
	u32 hcdma;
	struct urb *urb = qtd->urb;
	u32 hcintmsk = 0;

	hcchar = dwc_reg_read(regs, DWC_HCCHAR);
	hcsplt = dwc_reg_read(regs, DWC_HCSPLT);
	hctsiz = dwc_reg_read(regs, DWC_HCTSIZ);
	hcdma = dwc_reg_read(regs, DWC_HCDMA);

	pr_err("AHB ERROR, Channel %d\n", hc->hc_num);
	pr_err("  hcchar 0x%08x, hcsplt 0x%08x\n", hcchar, hcsplt);
	pr_err("  hctsiz 0x%08x, hcdma 0x%08x\n", hctsiz, hcdma);

	pr_err("  Device address: %d\n", usb_pipedevice(urb->pipe));
	pr_err("  Endpoint: %d, %s\n", usb_pipeendpoint(urb->pipe),
	       (usb_pipein(urb->pipe) ? "IN" : "OUT"));

	pr_err("  Endpoint type: %s\n", pipetype_str(urb->pipe));
	pr_err("  Speed: %s\n", dev_speed_str(urb->dev->speed));
	pr_err("  Max packet size: %d\n",
	       usb_maxpacket(urb->dev, urb->pipe, usb_pipeout(urb->pipe)));
	pr_err("  Data buffer length: %d\n", urb->transfer_buffer_length);
	pr_err("  Transfer buffer: %p, Transfer DMA: %p\n",
	       urb->transfer_buffer, (void *)(u32) urb->transfer_dma);
	pr_err("  Setup buffer: %p, Setup DMA: %p\n",
	       urb->setup_packet, (void *)(u32) urb->setup_dma);
	pr_err("  Interval: %d\n", urb->interval);

	dwc_otg_hcd_complete_urb(hcd, urb, -EIO);

	/*
	 * Force a channel halt. Don't call halt_channel because that won't
	 * write to the HCCHARn register in DMA mode to force the halt.
	 */
	dwc_otg_hc_halt(hcd->core_if, hc, DWC_OTG_HC_XFER_AHB_ERR);
	/* disable ahberr */
	hcintmsk = DWC_HCINTMSK_AHB_ERR_RW(hcintmsk, 1);
	dwc_reg_modify(regs, DWC_HCINTMSK, hcintmsk, 0);

	return 1;
}

/**
 * Handles a host channel transaction error interrupt. This handler may be
 * called in either DMA mode or Slave mode.
 */
static int handle_hc_xacterr_intr(struct dwc_hcd *hcd, struct dwc_hc *hc,
				  u32 regs, struct dwc_qtd *qtd, int *must_free)
{
	enum dwc_halt_status status = DWC_OTG_HC_XFER_XACT_ERR;
	u32 hcintmsk = 0;

	switch (usb_pipetype(qtd->urb->pipe)) {
	case PIPE_CONTROL:
	case PIPE_BULK:
		qtd->error_count++;
		if (!hc->qh->ping_state) {
			update_urb_state_xfer_intr(hc, regs, qtd->urb, qtd,
						   status);
			save_data_toggle(hc, regs, qtd);

			if (!hc->ep_is_in && qtd->urb->dev->speed ==
			    USB_SPEED_HIGH)
				hc->qh->ping_state = 1;
		}
		/*
		 * Halt the channel so the transfer can be re-started from
		 * the appropriate point or the PING protocol will start.
		 */
		halt_channel(hcd, hc, qtd, status, must_free);
		break;
	case PIPE_INTERRUPT:
		qtd->error_count++;
		if (hc->do_split && hc->complete_split)
			qtd->complete_split = 0;

		halt_channel(hcd, hc, qtd, status, must_free);
		break;
	case PIPE_ISOCHRONOUS:
		status = update_isoc_urb_state(hcd, hc, regs, qtd, status);
		halt_channel(hcd, hc, qtd, status, must_free);
		break;
	}
	/* Disable xacterr */
	hcintmsk = DWC_HCINTMSK_TRANS_ERR_RW(hcintmsk, 1);
	dwc_reg_modify(regs, DWC_HCINTMSK, hcintmsk, 0);

	return 1;
}

/**
 * Handles a host channel frame overrun interrupt. This handler may be called
 * in either DMA mode or Slave mode.
 */
static int handle_hc_frmovrun_intr(struct dwc_hcd *hcd, struct dwc_hc *hc,
				   u32 regs, struct dwc_qtd *qtd,
				   int *must_free)
{
	enum dwc_halt_status status = DWC_OTG_HC_XFER_FRAME_OVERRUN;
	u32 hcintmsk = 0;

	switch (usb_pipetype(qtd->urb->pipe)) {
	case PIPE_CONTROL:
	case PIPE_BULK:
		break;
	case PIPE_INTERRUPT:
		halt_channel(hcd, hc, qtd, status, must_free);
		break;
	case PIPE_ISOCHRONOUS:
		status = update_isoc_urb_state(hcd, hc, regs, qtd, status);
		halt_channel(hcd, hc, qtd, status, must_free);
		break;
	}
	/* Disable frmovrun */
	hcintmsk = DWC_HCINTMSK_FRAME_OVERN_ERR_RW(hcintmsk, 1);
	dwc_reg_modify(regs, DWC_HCINTMSK, hcintmsk, 0);

	return 1;
}

/**
 * Handles a host channel data toggle error interrupt. This handler may be
 * called in either DMA mode or Slave mode.
 */
static int handle_hc_datatglerr_intr(struct dwc_hcd *hcd, struct dwc_hc *hc,
				     u32 regs, struct dwc_qtd *qtd)
{
	u32 hcintmsk = 0;

	if (hc->ep_is_in)
		qtd->error_count = 0;
	else
		pr_err("Data Toggle Error on OUT transfer, channel "
		       "%d\n", hc->hc_num);

	/* disable datatglerr */
	hcintmsk = DWC_HCINTMSK_DATA_TOG_ERR_RW(hcintmsk, 1);
	dwc_reg_modify(regs, DWC_HCINTMSK, hcintmsk, 0);

	return 1;
}

/**
 * Handles a host Channel Halted interrupt in DMA mode. This handler
 * determines the reason the channel halted and proceeds accordingly.
 */
static void handle_hc_chhltd_intr_dma(struct dwc_hcd *hcd, struct dwc_hc *hc,
				      ulong regs, struct dwc_qtd *qtd,
				      int *must_free)
{
	u32 hcint;
	u32 hcintmsk = 0;

	if (hc->halt_status == DWC_OTG_HC_XFER_URB_DEQUEUE ||
	    hc->halt_status == DWC_OTG_HC_XFER_AHB_ERR) {
		/*
		 * Just release the channel. A dequeue can happen on a
		 * transfer timeout. In the case of an AHB Error, the channel
		 * was forced to halt because there's no way to gracefully
		 * recover.
		 */
		release_channel(hcd, hc, qtd, hc->halt_status, must_free);
		return;
	}

	/* Read the HCINTn register to determine the cause for the halt. */
	hcint = dwc_reg_read(regs, DWC_HCINT);
	hcintmsk = dwc_reg_read(regs, DWC_HCINTMSK);
	if (DWC_HCINT_TXFER_CMPL_RD(hcint)) {
		/*
		 * This is here because of a possible hardware bug.  Spec
		 * says that on SPLIT-ISOC OUT transfers in DMA mode that a HALT
		 * interrupt w/ACK bit set should occur, but I only see the
		 * XFERCOMP bit, even with it masked out.  This is a workaround
		 * for that behavior.  Should fix this when hardware is fixed.
		 */
		if (hc->ep_type == DWC_OTG_EP_TYPE_ISOC && !hc->ep_is_in)
			handle_hc_ack_intr(hcd, hc, regs, qtd, must_free);

		handle_hc_xfercomp_intr(hcd, hc, regs, qtd, must_free);
	} else if (DWC_HCINT_STALL_RESP_REC_RD(hcint)) {
		handle_hc_stall_intr(hcd, hc, regs, qtd, must_free);
	} else if (DWC_HCINT_TRANS_ERR_RD(hcint)) {
		/*
		 * Must handle xacterr before nak or ack. Could get a xacterr
		 * at the same time as either of these on a BULK/CONTROL OUT
		 * that started with a PING. The xacterr takes precedence.
		 */
		handle_hc_xacterr_intr(hcd, hc, regs, qtd, must_free);
	} else if (DWC_HCINT_NYET_RESP_REC_RD(hcint)) {
		/*
		 * Must handle nyet before nak or ack. Could get a nyet at the
		 * same time as either of those on a BULK/CONTROL OUT that
		 * started with a PING. The nyet takes precedence.
		 */
		handle_hc_nyet_intr(hcd, hc, regs, qtd, must_free);
	} else if (DWC_HCINT_BBL_ERR_RD(hcint)) {
		handle_hc_babble_intr(hcd, hc, regs, qtd, must_free);
	} else if (DWC_HCINT_FRAME_OVERN_ERR_RD(hcint)) {
		handle_hc_frmovrun_intr(hcd, hc, regs, qtd, must_free);
	} else if (DWC_HCINT_DATA_TOG_ERR_RD(hcint)) {
		handle_hc_datatglerr_intr(hcd, hc, regs, qtd);
		hc->qh->data_toggle = 0;
		halt_channel(hcd, hc, qtd, hc->halt_status, must_free);
	} else if (DWC_HCINT_NAK_RESP_REC_RD(hcint) &&
		   !DWC_HCINTMSK_NAK_RESP_REC_RD(hcintmsk)) {
		/*
		 * If nak is not masked, it's because a non-split IN transfer
		 * is in an error state. In that case, the nak is handled by
		 * the nak interrupt handler, not here. Handle nak here for
		 * BULK/CONTROL OUT transfers, which halt on a NAK to allow
		 * rewinding the buffer pointer.
		 */
		handle_hc_nak_intr(hcd, hc, regs, qtd, must_free);
	} else if (DWC_HCINT_ACK_RESP_REC_RD(hcint) &&
		   !DWC_HCINTMSK_ACK_RESP_REC_RD(hcintmsk)) {
		/*
		 * If ack is not masked, it's because a non-split IN transfer
		 * is in an error state. In that case, the ack is handled by
		 * the ack interrupt handler, not here. Handle ack here for
		 * split transfers. Start splits halt on ACK.
		 */
		handle_hc_ack_intr(hcd, hc, regs, qtd, must_free);
	} else {
		if (hc->ep_type == DWC_OTG_EP_TYPE_INTR ||
		    hc->ep_type == DWC_OTG_EP_TYPE_ISOC) {
			/*
			 * A periodic transfer halted with no other channel
			 * interrupts set. Assume it was halted by the core
			 * because it could not be completed in its scheduled
			 * (micro)frame.
			 */
			halt_channel(hcd, hc, qtd,
				     DWC_OTG_HC_XFER_PERIODIC_INCOMPLETE,
				     must_free);
		} else {
			pr_err("%s: Channel %d, DMA Mode -- ChHltd "
			       "set, but reason for halting is unknown, "
			       "hcint 0x%08x, intsts 0x%08x\n",
			       __func__, hc->hc_num, hcint,
			       dwc_reg_read(gintsts_reg(hcd), 0));
		}
	}
}

/**
 * Handles a host channel Channel Halted interrupt.
 *
 * In slave mode, this handler is called only when the driver specifically
 * requests a halt. This occurs during handling other host channel interrupts
 * (e.g. nak, xacterr, stall, nyet, etc.).
 *
 * In DMA mode, this is the interrupt that occurs when the core has finished
 * processing a transfer on a channel. Other host channel interrupts (except
 * ahberr) are disabled in DMA mode.
 */
static int handle_hc_chhltd_intr(struct dwc_hcd *hcd, struct dwc_hc *hc,
		ulong regs, struct dwc_qtd *qtd, int *must_free)
{
	if (hcd->core_if->dma_enable)
		handle_hc_chhltd_intr_dma(hcd, hc, regs, qtd, must_free);
	else
		release_channel(hcd, hc, qtd, hc->halt_status, must_free);

	return 1;
}

/* Handles interrupt for a specific Host Channel */
static int dwc_otg_hcd_handle_hc_n_intr(struct dwc_hcd *hcd, u32 num)
{
	int must_free = 0;
	int retval = 0;
	u32 hcint;
	u32 hcintmsk = 0;
	struct dwc_hc *hc;
	ulong hc_regs;
	struct dwc_qtd *qtd;

	hc = hcd->hc_ptr_array[num];
	hc_regs = hcd->core_if->host_if->hc_regs[num];
	qtd = list_entry(hc->qh->qtd_list.next, struct dwc_qtd, qtd_list_entry);

	hcint = dwc_reg_read(hc_regs, DWC_HCINT);
	hcintmsk = dwc_reg_read(hc_regs, DWC_HCINTMSK);

	hcint = hcint & hcintmsk;
	if (!hcd->core_if->dma_enable && DWC_HCINT_CHAN_HALTED_RD(hcint)
	    && hcint != 0x2)
		hcint = DWC_HCINT_CHAN_HALTED_RW(hcint, 0);

	if (DWC_HCINT_TXFER_CMPL_RD(hcint)) {
		retval |= handle_hc_xfercomp_intr(hcd, hc, hc_regs,
						  qtd, &must_free);
		/*
		 * If NYET occurred at same time as Xfer Complete, the NYET is
		 * handled by the Xfer Complete interrupt handler. Don't want
		 * to call the NYET interrupt handler in this case.
		 */
		hcint = DWC_HCINT_NYET_RESP_REC_RW(hcint, 0);
	}

	if (DWC_HCINT_CHAN_HALTED_RD(hcint))
		retval |= handle_hc_chhltd_intr(hcd, hc, hc_regs,
						qtd, &must_free);
	if (DWC_HCINT_AHB_ERR_RD(hcint))
		retval |= handle_hc_ahberr_intr(hcd, hc, hc_regs, qtd);
	if (DWC_HCINT_STALL_RESP_REC_RD(hcint))
		retval |= handle_hc_stall_intr(hcd, hc, hc_regs,
					       qtd, &must_free);
	if (DWC_HCINT_NAK_RESP_REC_RD(hcint))
		retval |= handle_hc_nak_intr(hcd, hc, hc_regs, qtd, &must_free);
	if (DWC_HCINT_ACK_RESP_REC_RD(hcint))
		retval |= handle_hc_ack_intr(hcd, hc, hc_regs, qtd, &must_free);
	if (DWC_HCINT_NYET_RESP_REC_RD(hcint))
		retval |= handle_hc_nyet_intr(hcd, hc, hc_regs,
					      qtd, &must_free);
	if (DWC_HCINT_TRANS_ERR_RD(hcint))
		retval |= handle_hc_xacterr_intr(hcd, hc, hc_regs,
						 qtd, &must_free);
	if (DWC_HCINT_BBL_ERR_RD(hcint))
		retval |= handle_hc_babble_intr(hcd, hc, hc_regs,
						qtd, &must_free);
	if (DWC_HCINT_FRAME_OVERN_ERR_RD(hcint))
		retval |= handle_hc_frmovrun_intr(hcd, hc, hc_regs,
						  qtd, &must_free);
	if (DWC_HCINT_DATA_TOG_ERR_RD(hcint))
		retval |= handle_hc_datatglerr_intr(hcd, hc, hc_regs, qtd);

	if (must_free)
		/* Free the qtd here now that we are done using it. */
		dwc_otg_hcd_qtd_free(qtd);
	return retval;
}

/**
 * This function returns the Host All Channel Interrupt register
 */
static inline u32 dwc_otg_read_host_all_channels_intr(struct core_if
						      *core_if)
{
	return dwc_reg_read(core_if->host_if->host_global_regs, DWC_HAINT);
}

/**
 * This interrupt indicates that one or more host channels has a pending
 * interrupt. There are multiple conditions that can cause each host channel
 * interrupt. This function determines which conditions have occurred for each
 * host channel interrupt and handles them appropriately.
 */
static int dwc_otg_hcd_handle_hc_intr(struct dwc_hcd *hcd)
{
	u32 i;
	int retval = 0;
	u32 haint;

	/*
	 * Clear appropriate bits in HCINTn to clear the interrupt bit in
	 *  GINTSTS
	 */
	haint = dwc_otg_read_host_all_channels_intr(hcd->core_if);
	for (i = 0; i < hcd->core_if->core_params->host_channels; i++)
		if (DWC_HAINT_RD(haint) & (1 << i))
			retval |= dwc_otg_hcd_handle_hc_n_intr(hcd, i);

	return retval;
}

/* This function handles interrupts for the HCD.*/
int dwc_otg_hcd_handle_intr(struct dwc_hcd *hcd)
{
	int ret = 0;
	struct core_if *core_if = hcd->core_if;
	u32 gintsts;

	/* Check if HOST Mode */
	if (dwc_otg_is_host_mode(core_if)) {
		spin_lock(&hcd->lock);
		gintsts = dwc_otg_read_core_intr(core_if);
		if (!gintsts) {
			spin_unlock(&hcd->lock);
			return IRQ_NONE;
		}

		if (gintsts & DWC_INTMSK_STRT_OF_FRM)
			ret |= dwc_otg_hcd_handle_sof_intr(hcd);
		if (gintsts & DWC_INTMSK_RXFIFO_NOT_EMPT)
			ret |= dwc_otg_hcd_handle_rx_status_q_level_intr(hcd);
		if (gintsts & DWC_INTMSK_NP_TXFIFO_EMPT)
			ret |= dwc_otg_hcd_handle_np_tx_fifo_empty_intr(hcd);
		if (gintsts & DWC_INTMSK_HST_PORT)
			ret |= dwc_otg_hcd_handle_port_intr(hcd);
		if (gintsts & DWC_INTMSK_HST_CHAN)
			ret |= dwc_otg_hcd_handle_hc_intr(hcd);
		if (gintsts & DWC_INTMSK_P_TXFIFO_EMPTY)
			ret |= dwc_otg_hcd_handle_perio_tx_fifo_empty_intr(hcd);

		spin_unlock(&hcd->lock);
	}
	return ret;
}
