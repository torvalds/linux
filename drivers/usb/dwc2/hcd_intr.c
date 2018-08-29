/*
 * hcd_intr.c - DesignWare HS OTG Controller host-mode interrupt handling
 *
 * Copyright (C) 2004-2013 Synopsys, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This file contains the interrupt handlers for Host mode
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include <linux/usb/hcd.h>
#include <linux/usb/ch11.h>

#include "core.h"
#include "hcd.h"

/* This function is for debug only */
static void dwc2_track_missed_sofs(struct dwc2_hsotg *hsotg)
{
#ifdef CONFIG_USB_DWC2_TRACK_MISSED_SOFS
	u16 curr_frame_number = hsotg->frame_number;

	if (hsotg->frame_num_idx < FRAME_NUM_ARRAY_SIZE) {
		if (((hsotg->last_frame_num + 1) & HFNUM_MAX_FRNUM) !=
		    curr_frame_number) {
			hsotg->frame_num_array[hsotg->frame_num_idx] =
					curr_frame_number;
			hsotg->last_frame_num_array[hsotg->frame_num_idx] =
					hsotg->last_frame_num;
			hsotg->frame_num_idx++;
		}
	} else if (!hsotg->dumped_frame_num_array) {
		int i;

		dev_info(hsotg->dev, "Frame     Last Frame\n");
		dev_info(hsotg->dev, "-----     ----------\n");
		for (i = 0; i < FRAME_NUM_ARRAY_SIZE; i++) {
			dev_info(hsotg->dev, "0x%04x    0x%04x\n",
				 hsotg->frame_num_array[i],
				 hsotg->last_frame_num_array[i]);
		}
		hsotg->dumped_frame_num_array = 1;
	}
	hsotg->last_frame_num = curr_frame_number;
#endif
}

static void dwc2_hc_handle_tt_clear(struct dwc2_hsotg *hsotg,
				    struct dwc2_host_chan *chan,
				    struct dwc2_qtd *qtd)
{
	struct urb *usb_urb;

	if (!chan->qh)
		return;

	if (chan->qh->dev_speed == USB_SPEED_HIGH)
		return;

	if (!qtd->urb)
		return;

	usb_urb = qtd->urb->priv;
	if (!usb_urb || !usb_urb->dev || !usb_urb->dev->tt)
		return;

	if (qtd->urb->status != -EPIPE && qtd->urb->status != -EREMOTEIO) {
		chan->qh->tt_buffer_dirty = 1;
		if (usb_hub_clear_tt_buffer(usb_urb))
			/* Clear failed; let's hope things work anyway */
			chan->qh->tt_buffer_dirty = 0;
	}
}

/*
 * Handles the start-of-frame interrupt in host mode. Non-periodic
 * transactions may be queued to the DWC_otg controller for the current
 * (micro)frame. Periodic transactions may be queued to the controller
 * for the next (micro)frame.
 */
static void dwc2_sof_intr(struct dwc2_hsotg *hsotg)
{
	struct list_head *qh_entry;
	struct dwc2_qh *qh;
	enum dwc2_transaction_type tr_type;

#ifdef DEBUG_SOF
	dev_vdbg(hsotg->dev, "--Start of Frame Interrupt--\n");
#endif

	hsotg->frame_number = dwc2_hcd_get_frame_number(hsotg);

	dwc2_track_missed_sofs(hsotg);

	/* Determine whether any periodic QHs should be executed */
	qh_entry = hsotg->periodic_sched_inactive.next;
	while (qh_entry != &hsotg->periodic_sched_inactive) {
		qh = list_entry(qh_entry, struct dwc2_qh, qh_list_entry);
		qh_entry = qh_entry->next;
		if (dwc2_frame_num_le(qh->sched_frame, hsotg->frame_number))
			/*
			 * Move QH to the ready list to be executed next
			 * (micro)frame
			 */
			list_move(&qh->qh_list_entry,
				  &hsotg->periodic_sched_ready);
	}
	tr_type = dwc2_hcd_select_transactions(hsotg);
	if (tr_type != DWC2_TRANSACTION_NONE)
		dwc2_hcd_queue_transactions(hsotg, tr_type);

	/* Clear interrupt */
	dwc2_writel(GINTSTS_SOF, hsotg->regs + GINTSTS);
}

/*
 * Handles the Rx FIFO Level Interrupt, which indicates that there is
 * at least one packet in the Rx FIFO. The packets are moved from the FIFO to
 * memory if the DWC_otg controller is operating in Slave mode.
 */
static void dwc2_rx_fifo_level_intr(struct dwc2_hsotg *hsotg)
{
	u32 grxsts, chnum, bcnt, dpid, pktsts;
	struct dwc2_host_chan *chan;

	if (dbg_perio())
		dev_vdbg(hsotg->dev, "--RxFIFO Level Interrupt--\n");

	grxsts = dwc2_readl(hsotg->regs + GRXSTSP);
	chnum = (grxsts & GRXSTS_HCHNUM_MASK) >> GRXSTS_HCHNUM_SHIFT;
	chan = hsotg->hc_ptr_array[chnum];
	if (!chan) {
		dev_err(hsotg->dev, "Unable to get corresponding channel\n");
		return;
	}

	bcnt = (grxsts & GRXSTS_BYTECNT_MASK) >> GRXSTS_BYTECNT_SHIFT;
	dpid = (grxsts & GRXSTS_DPID_MASK) >> GRXSTS_DPID_SHIFT;
	pktsts = (grxsts & GRXSTS_PKTSTS_MASK) >> GRXSTS_PKTSTS_SHIFT;

	/* Packet Status */
	if (dbg_perio()) {
		dev_vdbg(hsotg->dev, "    Ch num = %d\n", chnum);
		dev_vdbg(hsotg->dev, "    Count = %d\n", bcnt);
		dev_vdbg(hsotg->dev, "    DPID = %d, chan.dpid = %d\n", dpid,
			 chan->data_pid_start);
		dev_vdbg(hsotg->dev, "    PStatus = %d\n", pktsts);
	}

	switch (pktsts) {
	case GRXSTS_PKTSTS_HCHIN:
		/* Read the data into the host buffer */
		if (bcnt > 0) {
			dwc2_read_packet(hsotg, chan->xfer_buf, bcnt);

			/* Update the HC fields for the next packet received */
			chan->xfer_count += bcnt;
			chan->xfer_buf += bcnt;
		}
		break;
	case GRXSTS_PKTSTS_HCHIN_XFER_COMP:
	case GRXSTS_PKTSTS_DATATOGGLEERR:
	case GRXSTS_PKTSTS_HCHHALTED:
		/* Handled in interrupt, just ignore data */
		break;
	default:
		dev_err(hsotg->dev,
			"RxFIFO Level Interrupt: Unknown status %d\n", pktsts);
		break;
	}
}

/*
 * This interrupt occurs when the non-periodic Tx FIFO is half-empty. More
 * data packets may be written to the FIFO for OUT transfers. More requests
 * may be written to the non-periodic request queue for IN transfers. This
 * interrupt is enabled only in Slave mode.
 */
static void dwc2_np_tx_fifo_empty_intr(struct dwc2_hsotg *hsotg)
{
	dev_vdbg(hsotg->dev, "--Non-Periodic TxFIFO Empty Interrupt--\n");
	dwc2_hcd_queue_transactions(hsotg, DWC2_TRANSACTION_NON_PERIODIC);
}

/*
 * This interrupt occurs when the periodic Tx FIFO is half-empty. More data
 * packets may be written to the FIFO for OUT transfers. More requests may be
 * written to the periodic request queue for IN transfers. This interrupt is
 * enabled only in Slave mode.
 */
static void dwc2_perio_tx_fifo_empty_intr(struct dwc2_hsotg *hsotg)
{
	if (dbg_perio())
		dev_vdbg(hsotg->dev, "--Periodic TxFIFO Empty Interrupt--\n");
	dwc2_hcd_queue_transactions(hsotg, DWC2_TRANSACTION_PERIODIC);
}

static void dwc2_hprt0_enable(struct dwc2_hsotg *hsotg, u32 hprt0,
			      u32 *hprt0_modify)
{
	struct dwc2_core_params *params = hsotg->core_params;
	int do_reset = 0;
	u32 usbcfg;
	u32 prtspd;
	u32 hcfg;
	u32 fslspclksel;
	u32 hfir;

	dev_vdbg(hsotg->dev, "%s(%p)\n", __func__, hsotg);

	/* Every time when port enables calculate HFIR.FrInterval */
	hfir = dwc2_readl(hsotg->regs + HFIR);
	hfir &= ~HFIR_FRINT_MASK;
	hfir |= dwc2_calc_frame_interval(hsotg) << HFIR_FRINT_SHIFT &
		HFIR_FRINT_MASK;
	dwc2_writel(hfir, hsotg->regs + HFIR);

	/* Check if we need to adjust the PHY clock speed for low power */
	if (!params->host_support_fs_ls_low_power) {
		/* Port has been enabled, set the reset change flag */
		hsotg->flags.b.port_reset_change = 1;
		return;
	}

	usbcfg = dwc2_readl(hsotg->regs + GUSBCFG);
	prtspd = (hprt0 & HPRT0_SPD_MASK) >> HPRT0_SPD_SHIFT;

	if (prtspd == HPRT0_SPD_LOW_SPEED || prtspd == HPRT0_SPD_FULL_SPEED) {
		/* Low power */
		if (!(usbcfg & GUSBCFG_PHY_LP_CLK_SEL)) {
			/* Set PHY low power clock select for FS/LS devices */
			usbcfg |= GUSBCFG_PHY_LP_CLK_SEL;
			dwc2_writel(usbcfg, hsotg->regs + GUSBCFG);
			do_reset = 1;
		}

		hcfg = dwc2_readl(hsotg->regs + HCFG);
		fslspclksel = (hcfg & HCFG_FSLSPCLKSEL_MASK) >>
			      HCFG_FSLSPCLKSEL_SHIFT;

		if (prtspd == HPRT0_SPD_LOW_SPEED &&
		    params->host_ls_low_power_phy_clk ==
		    DWC2_HOST_LS_LOW_POWER_PHY_CLK_PARAM_6MHZ) {
			/* 6 MHZ */
			dev_vdbg(hsotg->dev,
				 "FS_PHY programming HCFG to 6 MHz\n");
			if (fslspclksel != HCFG_FSLSPCLKSEL_6_MHZ) {
				fslspclksel = HCFG_FSLSPCLKSEL_6_MHZ;
				hcfg &= ~HCFG_FSLSPCLKSEL_MASK;
				hcfg |= fslspclksel << HCFG_FSLSPCLKSEL_SHIFT;
				dwc2_writel(hcfg, hsotg->regs + HCFG);
				do_reset = 1;
			}
		} else {
			/* 48 MHZ */
			dev_vdbg(hsotg->dev,
				 "FS_PHY programming HCFG to 48 MHz\n");
			if (fslspclksel != HCFG_FSLSPCLKSEL_48_MHZ) {
				fslspclksel = HCFG_FSLSPCLKSEL_48_MHZ;
				hcfg &= ~HCFG_FSLSPCLKSEL_MASK;
				hcfg |= fslspclksel << HCFG_FSLSPCLKSEL_SHIFT;
				dwc2_writel(hcfg, hsotg->regs + HCFG);
				do_reset = 1;
			}
		}
	} else {
		/* Not low power */
		if (usbcfg & GUSBCFG_PHY_LP_CLK_SEL) {
			usbcfg &= ~GUSBCFG_PHY_LP_CLK_SEL;
			dwc2_writel(usbcfg, hsotg->regs + GUSBCFG);
			do_reset = 1;
		}
	}

	if (do_reset) {
		*hprt0_modify |= HPRT0_RST;
		queue_delayed_work(hsotg->wq_otg, &hsotg->reset_work,
				   msecs_to_jiffies(60));
	} else {
		/* Port has been enabled, set the reset change flag */
		hsotg->flags.b.port_reset_change = 1;
	}
}

/*
 * There are multiple conditions that can cause a port interrupt. This function
 * determines which interrupt conditions have occurred and handles them
 * appropriately.
 */
static void dwc2_port_intr(struct dwc2_hsotg *hsotg)
{
	u32 hprt0;
	u32 hprt0_modify;

	dev_vdbg(hsotg->dev, "--Port Interrupt--\n");

	hprt0 = dwc2_readl(hsotg->regs + HPRT0);
	hprt0_modify = hprt0;

	/*
	 * Clear appropriate bits in HPRT0 to clear the interrupt bit in
	 * GINTSTS
	 */
	hprt0_modify &= ~(HPRT0_ENA | HPRT0_CONNDET | HPRT0_ENACHG |
			  HPRT0_OVRCURRCHG);

	/*
	 * Port Connect Detected
	 * Set flag and clear if detected
	 */
	if (hprt0 & HPRT0_CONNDET) {
		dev_vdbg(hsotg->dev,
			 "--Port Interrupt HPRT0=0x%08x Port Connect Detected--\n",
			 hprt0);
		if (hsotg->lx_state != DWC2_L0)
			usb_hcd_resume_root_hub(hsotg->priv);

		hsotg->flags.b.port_connect_status_change = 1;
		hsotg->flags.b.port_connect_status = 1;
		hprt0_modify |= HPRT0_CONNDET;

		/*
		 * The Hub driver asserts a reset when it sees port connect
		 * status change flag
		 */
	}

	/*
	 * Port Enable Changed
	 * Clear if detected - Set internal flag if disabled
	 */
	if (hprt0 & HPRT0_ENACHG) {
		dev_vdbg(hsotg->dev,
			 "  --Port Interrupt HPRT0=0x%08x Port Enable Changed (now %d)--\n",
			 hprt0, !!(hprt0 & HPRT0_ENA));
		hprt0_modify |= HPRT0_ENACHG;
		if (hprt0 & HPRT0_ENA)
			dwc2_hprt0_enable(hsotg, hprt0, &hprt0_modify);
		else
			hsotg->flags.b.port_enable_change = 1;
	}

	/* Overcurrent Change Interrupt */
	if (hprt0 & HPRT0_OVRCURRCHG) {
		dev_vdbg(hsotg->dev,
			 "  --Port Interrupt HPRT0=0x%08x Port Overcurrent Changed--\n",
			 hprt0);
		hsotg->flags.b.port_over_current_change = 1;
		hprt0_modify |= HPRT0_OVRCURRCHG;
	}

	/* Clear Port Interrupts */
	dwc2_writel(hprt0_modify, hsotg->regs + HPRT0);
}

/*
 * Gets the actual length of a transfer after the transfer halts. halt_status
 * holds the reason for the halt.
 *
 * For IN transfers where halt_status is DWC2_HC_XFER_COMPLETE, *short_read
 * is set to 1 upon return if less than the requested number of bytes were
 * transferred. short_read may also be NULL on entry, in which case it remains
 * unchanged.
 */
static u32 dwc2_get_actual_xfer_length(struct dwc2_hsotg *hsotg,
				       struct dwc2_host_chan *chan, int chnum,
				       struct dwc2_qtd *qtd,
				       enum dwc2_halt_status halt_status,
				       int *short_read)
{
	u32 hctsiz, count, length;

	hctsiz = dwc2_readl(hsotg->regs + HCTSIZ(chnum));

	if (halt_status == DWC2_HC_XFER_COMPLETE) {
		if (chan->ep_is_in) {
			count = (hctsiz & TSIZ_XFERSIZE_MASK) >>
				TSIZ_XFERSIZE_SHIFT;
			length = chan->xfer_len - count;
			if (short_read != NULL)
				*short_read = (count != 0);
		} else if (chan->qh->do_split) {
			length = qtd->ssplit_out_xfer_count;
		} else {
			length = chan->xfer_len;
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
		count = (hctsiz & TSIZ_PKTCNT_MASK) >> TSIZ_PKTCNT_SHIFT;
		length = (chan->start_pkt_count - count) * chan->max_packet;
	}

	return length;
}

/**
 * dwc2_update_urb_state() - Updates the state of the URB after a Transfer
 * Complete interrupt on the host channel. Updates the actual_length field
 * of the URB based on the number of bytes transferred via the host channel.
 * Sets the URB status if the data transfer is finished.
 *
 * Return: 1 if the data transfer specified by the URB is completely finished,
 * 0 otherwise
 */
static int dwc2_update_urb_state(struct dwc2_hsotg *hsotg,
				 struct dwc2_host_chan *chan, int chnum,
				 struct dwc2_hcd_urb *urb,
				 struct dwc2_qtd *qtd)
{
	u32 hctsiz;
	int xfer_done = 0;
	int short_read = 0;
	int xfer_length = dwc2_get_actual_xfer_length(hsotg, chan, chnum, qtd,
						      DWC2_HC_XFER_COMPLETE,
						      &short_read);

	if (urb->actual_length + xfer_length > urb->length) {
		dev_warn(hsotg->dev, "%s(): trimming xfer length\n", __func__);
		xfer_length = urb->length - urb->actual_length;
	}

	/* Non DWORD-aligned buffer case handling */
	if (chan->align_buf && xfer_length) {
		dev_vdbg(hsotg->dev, "%s(): non-aligned buffer\n", __func__);
		dma_unmap_single(hsotg->dev, chan->qh->dw_align_buf_dma,
				chan->qh->dw_align_buf_size,
				chan->ep_is_in ?
				DMA_FROM_DEVICE : DMA_TO_DEVICE);
		if (chan->ep_is_in)
			memcpy(urb->buf + urb->actual_length,
					chan->qh->dw_align_buf, xfer_length);
	}

	dev_vdbg(hsotg->dev, "urb->actual_length=%d xfer_length=%d\n",
		 urb->actual_length, xfer_length);
	urb->actual_length += xfer_length;

	if (xfer_length && chan->ep_type == USB_ENDPOINT_XFER_BULK &&
	    (urb->flags & URB_SEND_ZERO_PACKET) &&
	    urb->actual_length >= urb->length &&
	    !(urb->length % chan->max_packet)) {
		xfer_done = 0;
	} else if (short_read || urb->actual_length >= urb->length) {
		xfer_done = 1;
		urb->status = 0;
	}

	hctsiz = dwc2_readl(hsotg->regs + HCTSIZ(chnum));
	dev_vdbg(hsotg->dev, "DWC_otg: %s: %s, channel %d\n",
		 __func__, (chan->ep_is_in ? "IN" : "OUT"), chnum);
	dev_vdbg(hsotg->dev, "  chan->xfer_len %d\n", chan->xfer_len);
	dev_vdbg(hsotg->dev, "  hctsiz.xfersize %d\n",
		 (hctsiz & TSIZ_XFERSIZE_MASK) >> TSIZ_XFERSIZE_SHIFT);
	dev_vdbg(hsotg->dev, "  urb->transfer_buffer_length %d\n", urb->length);
	dev_vdbg(hsotg->dev, "  urb->actual_length %d\n", urb->actual_length);
	dev_vdbg(hsotg->dev, "  short_read %d, xfer_done %d\n", short_read,
		 xfer_done);

	return xfer_done;
}

/*
 * Save the starting data toggle for the next transfer. The data toggle is
 * saved in the QH for non-control transfers and it's saved in the QTD for
 * control transfers.
 */
void dwc2_hcd_save_data_toggle(struct dwc2_hsotg *hsotg,
			       struct dwc2_host_chan *chan, int chnum,
			       struct dwc2_qtd *qtd)
{
	u32 hctsiz = dwc2_readl(hsotg->regs + HCTSIZ(chnum));
	u32 pid = (hctsiz & TSIZ_SC_MC_PID_MASK) >> TSIZ_SC_MC_PID_SHIFT;

	if (chan->ep_type != USB_ENDPOINT_XFER_CONTROL) {
		if (pid == TSIZ_SC_MC_PID_DATA0)
			chan->qh->data_toggle = DWC2_HC_PID_DATA0;
		else
			chan->qh->data_toggle = DWC2_HC_PID_DATA1;
	} else {
		if (pid == TSIZ_SC_MC_PID_DATA0)
			qtd->data_toggle = DWC2_HC_PID_DATA0;
		else
			qtd->data_toggle = DWC2_HC_PID_DATA1;
	}
}

/**
 * dwc2_update_isoc_urb_state() - Updates the state of an Isochronous URB when
 * the transfer is stopped for any reason. The fields of the current entry in
 * the frame descriptor array are set based on the transfer state and the input
 * halt_status. Completes the Isochronous URB if all the URB frames have been
 * completed.
 *
 * Return: DWC2_HC_XFER_COMPLETE if there are more frames remaining to be
 * transferred in the URB. Otherwise return DWC2_HC_XFER_URB_COMPLETE.
 */
static enum dwc2_halt_status dwc2_update_isoc_urb_state(
		struct dwc2_hsotg *hsotg, struct dwc2_host_chan *chan,
		int chnum, struct dwc2_qtd *qtd,
		enum dwc2_halt_status halt_status)
{
	struct dwc2_hcd_iso_packet_desc *frame_desc;
	struct dwc2_hcd_urb *urb = qtd->urb;

	if (!urb)
		return DWC2_HC_XFER_NO_HALT_STATUS;

	frame_desc = &urb->iso_descs[qtd->isoc_frame_index];

	switch (halt_status) {
	case DWC2_HC_XFER_COMPLETE:
		frame_desc->status = 0;
		frame_desc->actual_length = dwc2_get_actual_xfer_length(hsotg,
					chan, chnum, qtd, halt_status, NULL);

		/* Non DWORD-aligned buffer case handling */
		if (chan->align_buf && frame_desc->actual_length) {
			dev_vdbg(hsotg->dev, "%s(): non-aligned buffer\n",
				 __func__);
			dma_unmap_single(hsotg->dev, chan->qh->dw_align_buf_dma,
					chan->qh->dw_align_buf_size,
					chan->ep_is_in ?
					DMA_FROM_DEVICE : DMA_TO_DEVICE);
			if (chan->ep_is_in)
				memcpy(urb->buf + frame_desc->offset +
					qtd->isoc_split_offset,
					chan->qh->dw_align_buf,
					frame_desc->actual_length);
		}
		break;
	case DWC2_HC_XFER_FRAME_OVERRUN:
		urb->error_count++;
		if (chan->ep_is_in)
			frame_desc->status = -ENOSR;
		else
			frame_desc->status = -ECOMM;
		frame_desc->actual_length = 0;
		break;
	case DWC2_HC_XFER_BABBLE_ERR:
		urb->error_count++;
		frame_desc->status = -EOVERFLOW;
		/* Don't need to update actual_length in this case */
		break;
	case DWC2_HC_XFER_XACT_ERR:
		urb->error_count++;
		frame_desc->status = -EPROTO;
		frame_desc->actual_length = dwc2_get_actual_xfer_length(hsotg,
					chan, chnum, qtd, halt_status, NULL);

		/* Non DWORD-aligned buffer case handling */
		if (chan->align_buf && frame_desc->actual_length) {
			dev_vdbg(hsotg->dev, "%s(): non-aligned buffer\n",
				 __func__);
			dma_unmap_single(hsotg->dev, chan->qh->dw_align_buf_dma,
					chan->qh->dw_align_buf_size,
					chan->ep_is_in ?
					DMA_FROM_DEVICE : DMA_TO_DEVICE);
			if (chan->ep_is_in)
				memcpy(urb->buf + frame_desc->offset +
					qtd->isoc_split_offset,
					chan->qh->dw_align_buf,
					frame_desc->actual_length);
		}

		/* Skip whole frame */
		if (chan->qh->do_split &&
		    chan->ep_type == USB_ENDPOINT_XFER_ISOC && chan->ep_is_in &&
		    hsotg->core_params->dma_enable > 0) {
			qtd->complete_split = 0;
			qtd->isoc_split_offset = 0;
		}

		break;
	default:
		dev_err(hsotg->dev, "Unhandled halt_status (%d)\n",
			halt_status);
		break;
	}

	if (++qtd->isoc_frame_index == urb->packet_count) {
		/*
		 * urb->status is not used for isoc transfers. The individual
		 * frame_desc statuses are used instead.
		 */
		dwc2_host_complete(hsotg, qtd, 0);
		halt_status = DWC2_HC_XFER_URB_COMPLETE;
	} else {
		halt_status = DWC2_HC_XFER_COMPLETE;
	}

	return halt_status;
}

/*
 * Frees the first QTD in the QH's list if free_qtd is 1. For non-periodic
 * QHs, removes the QH from the active non-periodic schedule. If any QTDs are
 * still linked to the QH, the QH is added to the end of the inactive
 * non-periodic schedule. For periodic QHs, removes the QH from the periodic
 * schedule if no more QTDs are linked to the QH.
 */
static void dwc2_deactivate_qh(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh,
			       int free_qtd)
{
	int continue_split = 0;
	struct dwc2_qtd *qtd;

	if (dbg_qh(qh))
		dev_vdbg(hsotg->dev, "  %s(%p,%p,%d)\n", __func__,
			 hsotg, qh, free_qtd);

	if (list_empty(&qh->qtd_list)) {
		dev_dbg(hsotg->dev, "## QTD list empty ##\n");
		goto no_qtd;
	}

	qtd = list_first_entry(&qh->qtd_list, struct dwc2_qtd, qtd_list_entry);

	if (qtd->complete_split)
		continue_split = 1;
	else if (qtd->isoc_split_pos == DWC2_HCSPLT_XACTPOS_MID ||
		 qtd->isoc_split_pos == DWC2_HCSPLT_XACTPOS_END)
		continue_split = 1;

	if (free_qtd) {
		dwc2_hcd_qtd_unlink_and_free(hsotg, qtd, qh);
		continue_split = 0;
	}

no_qtd:
	if (qh->channel)
		qh->channel->align_buf = 0;
	qh->channel = NULL;
	dwc2_hcd_qh_deactivate(hsotg, qh, continue_split);
}

/**
 * dwc2_release_channel() - Releases a host channel for use by other transfers
 *
 * @hsotg:       The HCD state structure
 * @chan:        The host channel to release
 * @qtd:         The QTD associated with the host channel. This QTD may be
 *               freed if the transfer is complete or an error has occurred.
 * @halt_status: Reason the channel is being released. This status
 *               determines the actions taken by this function.
 *
 * Also attempts to select and queue more transactions since at least one host
 * channel is available.
 */
static void dwc2_release_channel(struct dwc2_hsotg *hsotg,
				 struct dwc2_host_chan *chan,
				 struct dwc2_qtd *qtd,
				 enum dwc2_halt_status halt_status)
{
	enum dwc2_transaction_type tr_type;
	u32 haintmsk;
	int free_qtd = 0;

	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "  %s: channel %d, halt_status %d\n",
			 __func__, chan->hc_num, halt_status);

	switch (halt_status) {
	case DWC2_HC_XFER_URB_COMPLETE:
		free_qtd = 1;
		break;
	case DWC2_HC_XFER_AHB_ERR:
	case DWC2_HC_XFER_STALL:
	case DWC2_HC_XFER_BABBLE_ERR:
		free_qtd = 1;
		break;
	case DWC2_HC_XFER_XACT_ERR:
		if (qtd && qtd->error_count >= 3) {
			dev_vdbg(hsotg->dev,
				 "  Complete URB with transaction error\n");
			free_qtd = 1;
			dwc2_host_complete(hsotg, qtd, -EPROTO);
		}
		break;
	case DWC2_HC_XFER_URB_DEQUEUE:
		/*
		 * The QTD has already been removed and the QH has been
		 * deactivated. Don't want to do anything except release the
		 * host channel and try to queue more transfers.
		 */
		goto cleanup;
	case DWC2_HC_XFER_PERIODIC_INCOMPLETE:
		dev_vdbg(hsotg->dev, "  Complete URB with I/O error\n");
		free_qtd = 1;
		dwc2_host_complete(hsotg, qtd, -EIO);
		break;
	case DWC2_HC_XFER_NO_HALT_STATUS:
	default:
		break;
	}

	dwc2_deactivate_qh(hsotg, chan->qh, free_qtd);

cleanup:
	/*
	 * Release the host channel for use by other transfers. The cleanup
	 * function clears the channel interrupt enables and conditions, so
	 * there's no need to clear the Channel Halted interrupt separately.
	 */
	if (!list_empty(&chan->hc_list_entry))
		list_del(&chan->hc_list_entry);
	dwc2_hc_cleanup(hsotg, chan);
	list_add_tail(&chan->hc_list_entry, &hsotg->free_hc_list);

	if (hsotg->core_params->uframe_sched > 0) {
		hsotg->available_host_channels++;
	} else {
		switch (chan->ep_type) {
		case USB_ENDPOINT_XFER_CONTROL:
		case USB_ENDPOINT_XFER_BULK:
			hsotg->non_periodic_channels--;
			break;
		default:
			/*
			 * Don't release reservations for periodic channels
			 * here. That's done when a periodic transfer is
			 * descheduled (i.e. when the QH is removed from the
			 * periodic schedule).
			 */
			break;
		}
	}

	haintmsk = dwc2_readl(hsotg->regs + HAINTMSK);
	haintmsk &= ~(1 << chan->hc_num);
	dwc2_writel(haintmsk, hsotg->regs + HAINTMSK);

	/* Try to queue more transfers now that there's a free channel */
	tr_type = dwc2_hcd_select_transactions(hsotg);
	if (tr_type != DWC2_TRANSACTION_NONE)
		dwc2_hcd_queue_transactions(hsotg, tr_type);
}

/*
 * Halts a host channel. If the channel cannot be halted immediately because
 * the request queue is full, this function ensures that the FIFO empty
 * interrupt for the appropriate queue is enabled so that the halt request can
 * be queued when there is space in the request queue.
 *
 * This function may also be called in DMA mode. In that case, the channel is
 * simply released since the core always halts the channel automatically in
 * DMA mode.
 */
static void dwc2_halt_channel(struct dwc2_hsotg *hsotg,
			      struct dwc2_host_chan *chan, struct dwc2_qtd *qtd,
			      enum dwc2_halt_status halt_status)
{
	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "%s()\n", __func__);

	if (hsotg->core_params->dma_enable > 0) {
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "DMA enabled\n");
		dwc2_release_channel(hsotg, chan, qtd, halt_status);
		return;
	}

	/* Slave mode processing */
	dwc2_hc_halt(hsotg, chan, halt_status);

	if (chan->halt_on_queue) {
		u32 gintmsk;

		dev_vdbg(hsotg->dev, "Halt on queue\n");
		if (chan->ep_type == USB_ENDPOINT_XFER_CONTROL ||
		    chan->ep_type == USB_ENDPOINT_XFER_BULK) {
			dev_vdbg(hsotg->dev, "control/bulk\n");
			/*
			 * Make sure the Non-periodic Tx FIFO empty interrupt
			 * is enabled so that the non-periodic schedule will
			 * be processed
			 */
			gintmsk = dwc2_readl(hsotg->regs + GINTMSK);
			gintmsk |= GINTSTS_NPTXFEMP;
			dwc2_writel(gintmsk, hsotg->regs + GINTMSK);
		} else {
			dev_vdbg(hsotg->dev, "isoc/intr\n");
			/*
			 * Move the QH from the periodic queued schedule to
			 * the periodic assigned schedule. This allows the
			 * halt to be queued when the periodic schedule is
			 * processed.
			 */
			list_move(&chan->qh->qh_list_entry,
				  &hsotg->periodic_sched_assigned);

			/*
			 * Make sure the Periodic Tx FIFO Empty interrupt is
			 * enabled so that the periodic schedule will be
			 * processed
			 */
			gintmsk = dwc2_readl(hsotg->regs + GINTMSK);
			gintmsk |= GINTSTS_PTXFEMP;
			dwc2_writel(gintmsk, hsotg->regs + GINTMSK);
		}
	}
}

/*
 * Performs common cleanup for non-periodic transfers after a Transfer
 * Complete interrupt. This function should be called after any endpoint type
 * specific handling is finished to release the host channel.
 */
static void dwc2_complete_non_periodic_xfer(struct dwc2_hsotg *hsotg,
					    struct dwc2_host_chan *chan,
					    int chnum, struct dwc2_qtd *qtd,
					    enum dwc2_halt_status halt_status)
{
	dev_vdbg(hsotg->dev, "%s()\n", __func__);

	qtd->error_count = 0;

	if (chan->hcint & HCINTMSK_NYET) {
		/*
		 * Got a NYET on the last transaction of the transfer. This
		 * means that the endpoint should be in the PING state at the
		 * beginning of the next transfer.
		 */
		dev_vdbg(hsotg->dev, "got NYET\n");
		chan->qh->ping_state = 1;
	}

	/*
	 * Always halt and release the host channel to make it available for
	 * more transfers. There may still be more phases for a control
	 * transfer or more data packets for a bulk transfer at this point,
	 * but the host channel is still halted. A channel will be reassigned
	 * to the transfer when the non-periodic schedule is processed after
	 * the channel is released. This allows transactions to be queued
	 * properly via dwc2_hcd_queue_transactions, which also enables the
	 * Tx FIFO Empty interrupt if necessary.
	 */
	if (chan->ep_is_in) {
		/*
		 * IN transfers in Slave mode require an explicit disable to
		 * halt the channel. (In DMA mode, this call simply releases
		 * the channel.)
		 */
		dwc2_halt_channel(hsotg, chan, qtd, halt_status);
	} else {
		/*
		 * The channel is automatically disabled by the core for OUT
		 * transfers in Slave mode
		 */
		dwc2_release_channel(hsotg, chan, qtd, halt_status);
	}
}

/*
 * Performs common cleanup for periodic transfers after a Transfer Complete
 * interrupt. This function should be called after any endpoint type specific
 * handling is finished to release the host channel.
 */
static void dwc2_complete_periodic_xfer(struct dwc2_hsotg *hsotg,
					struct dwc2_host_chan *chan, int chnum,
					struct dwc2_qtd *qtd,
					enum dwc2_halt_status halt_status)
{
	u32 hctsiz = dwc2_readl(hsotg->regs + HCTSIZ(chnum));

	qtd->error_count = 0;

	if (!chan->ep_is_in || (hctsiz & TSIZ_PKTCNT_MASK) == 0)
		/* Core halts channel in these cases */
		dwc2_release_channel(hsotg, chan, qtd, halt_status);
	else
		/* Flush any outstanding requests from the Tx queue */
		dwc2_halt_channel(hsotg, chan, qtd, halt_status);
}

static int dwc2_xfercomp_isoc_split_in(struct dwc2_hsotg *hsotg,
				       struct dwc2_host_chan *chan, int chnum,
				       struct dwc2_qtd *qtd)
{
	struct dwc2_hcd_iso_packet_desc *frame_desc;
	u32 len;

	if (!qtd->urb)
		return 0;

	frame_desc = &qtd->urb->iso_descs[qtd->isoc_frame_index];
	len = dwc2_get_actual_xfer_length(hsotg, chan, chnum, qtd,
					  DWC2_HC_XFER_COMPLETE, NULL);
	if (!len && !qtd->isoc_split_offset) {
		qtd->complete_split = 0;
		return 0;
	}

	frame_desc->actual_length += len;

	if (chan->align_buf) {
		dev_vdbg(hsotg->dev, "%s(): non-aligned buffer\n", __func__);
		dma_unmap_single(hsotg->dev, chan->qh->dw_align_buf_dma,
				chan->qh->dw_align_buf_size, DMA_FROM_DEVICE);
		memcpy(qtd->urb->buf + frame_desc->offset +
		       qtd->isoc_split_offset, chan->qh->dw_align_buf, len);
	}

	qtd->isoc_split_offset += len;

	if (frame_desc->actual_length >= frame_desc->length) {
		frame_desc->status = 0;
		qtd->isoc_frame_index++;
		qtd->complete_split = 0;
		qtd->isoc_split_offset = 0;
	}

	if (qtd->isoc_frame_index == qtd->urb->packet_count) {
		dwc2_host_complete(hsotg, qtd, 0);
		dwc2_release_channel(hsotg, chan, qtd,
				     DWC2_HC_XFER_URB_COMPLETE);
	} else {
		dwc2_release_channel(hsotg, chan, qtd,
				     DWC2_HC_XFER_NO_HALT_STATUS);
	}

	return 1;	/* Indicates that channel released */
}

/*
 * Handles a host channel Transfer Complete interrupt. This handler may be
 * called in either DMA mode or Slave mode.
 */
static void dwc2_hc_xfercomp_intr(struct dwc2_hsotg *hsotg,
				  struct dwc2_host_chan *chan, int chnum,
				  struct dwc2_qtd *qtd)
{
	struct dwc2_hcd_urb *urb = qtd->urb;
	enum dwc2_halt_status halt_status = DWC2_HC_XFER_COMPLETE;
	int pipe_type;
	int urb_xfer_done;

	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev,
			 "--Host Channel %d Interrupt: Transfer Complete--\n",
			 chnum);

	if (!urb)
		goto handle_xfercomp_done;

	pipe_type = dwc2_hcd_get_pipe_type(&urb->pipe_info);

	if (hsotg->core_params->dma_desc_enable > 0) {
		dwc2_hcd_complete_xfer_ddma(hsotg, chan, chnum, halt_status);
		if (pipe_type == USB_ENDPOINT_XFER_ISOC)
			/* Do not disable the interrupt, just clear it */
			return;
		goto handle_xfercomp_done;
	}

	/* Handle xfer complete on CSPLIT */
	if (chan->qh->do_split) {
		if (chan->ep_type == USB_ENDPOINT_XFER_ISOC && chan->ep_is_in &&
		    hsotg->core_params->dma_enable > 0) {
			if (qtd->complete_split &&
			    dwc2_xfercomp_isoc_split_in(hsotg, chan, chnum,
							qtd))
				goto handle_xfercomp_done;
		} else {
			qtd->complete_split = 0;
		}
	}

	/* Update the QTD and URB states */
	switch (pipe_type) {
	case USB_ENDPOINT_XFER_CONTROL:
		switch (qtd->control_phase) {
		case DWC2_CONTROL_SETUP:
			if (urb->length > 0)
				qtd->control_phase = DWC2_CONTROL_DATA;
			else
				qtd->control_phase = DWC2_CONTROL_STATUS;
			dev_vdbg(hsotg->dev,
				 "  Control setup transaction done\n");
			halt_status = DWC2_HC_XFER_COMPLETE;
			break;
		case DWC2_CONTROL_DATA:
			urb_xfer_done = dwc2_update_urb_state(hsotg, chan,
							      chnum, urb, qtd);
			if (urb_xfer_done) {
				qtd->control_phase = DWC2_CONTROL_STATUS;
				dev_vdbg(hsotg->dev,
					 "  Control data transfer done\n");
			} else {
				dwc2_hcd_save_data_toggle(hsotg, chan, chnum,
							  qtd);
			}
			halt_status = DWC2_HC_XFER_COMPLETE;
			break;
		case DWC2_CONTROL_STATUS:
			dev_vdbg(hsotg->dev, "  Control transfer complete\n");
			if (urb->status == -EINPROGRESS)
				urb->status = 0;
			dwc2_host_complete(hsotg, qtd, urb->status);
			halt_status = DWC2_HC_XFER_URB_COMPLETE;
			break;
		}

		dwc2_complete_non_periodic_xfer(hsotg, chan, chnum, qtd,
						halt_status);
		break;
	case USB_ENDPOINT_XFER_BULK:
		dev_vdbg(hsotg->dev, "  Bulk transfer complete\n");
		urb_xfer_done = dwc2_update_urb_state(hsotg, chan, chnum, urb,
						      qtd);
		if (urb_xfer_done) {
			dwc2_host_complete(hsotg, qtd, urb->status);
			halt_status = DWC2_HC_XFER_URB_COMPLETE;
		} else {
			halt_status = DWC2_HC_XFER_COMPLETE;
		}

		dwc2_hcd_save_data_toggle(hsotg, chan, chnum, qtd);
		dwc2_complete_non_periodic_xfer(hsotg, chan, chnum, qtd,
						halt_status);
		break;
	case USB_ENDPOINT_XFER_INT:
		dev_vdbg(hsotg->dev, "  Interrupt transfer complete\n");
		urb_xfer_done = dwc2_update_urb_state(hsotg, chan, chnum, urb,
						      qtd);

		/*
		 * Interrupt URB is done on the first transfer complete
		 * interrupt
		 */
		if (urb_xfer_done) {
			dwc2_host_complete(hsotg, qtd, urb->status);
			halt_status = DWC2_HC_XFER_URB_COMPLETE;
		} else {
			halt_status = DWC2_HC_XFER_COMPLETE;
		}

		dwc2_hcd_save_data_toggle(hsotg, chan, chnum, qtd);
		dwc2_complete_periodic_xfer(hsotg, chan, chnum, qtd,
					    halt_status);
		break;
	case USB_ENDPOINT_XFER_ISOC:
		if (dbg_perio())
			dev_vdbg(hsotg->dev, "  Isochronous transfer complete\n");
		if (qtd->isoc_split_pos == DWC2_HCSPLT_XACTPOS_ALL)
			halt_status = dwc2_update_isoc_urb_state(hsotg, chan,
					chnum, qtd, DWC2_HC_XFER_COMPLETE);
		dwc2_complete_periodic_xfer(hsotg, chan, chnum, qtd,
					    halt_status);
		break;
	}

handle_xfercomp_done:
	disable_hc_int(hsotg, chnum, HCINTMSK_XFERCOMPL);
}

/*
 * Handles a host channel STALL interrupt. This handler may be called in
 * either DMA mode or Slave mode.
 */
static void dwc2_hc_stall_intr(struct dwc2_hsotg *hsotg,
			       struct dwc2_host_chan *chan, int chnum,
			       struct dwc2_qtd *qtd)
{
	struct dwc2_hcd_urb *urb = qtd->urb;
	int pipe_type;

	dev_dbg(hsotg->dev, "--Host Channel %d Interrupt: STALL Received--\n",
		chnum);

	if (hsotg->core_params->dma_desc_enable > 0) {
		dwc2_hcd_complete_xfer_ddma(hsotg, chan, chnum,
					    DWC2_HC_XFER_STALL);
		goto handle_stall_done;
	}

	if (!urb)
		goto handle_stall_halt;

	pipe_type = dwc2_hcd_get_pipe_type(&urb->pipe_info);

	if (pipe_type == USB_ENDPOINT_XFER_CONTROL)
		dwc2_host_complete(hsotg, qtd, -EPIPE);

	if (pipe_type == USB_ENDPOINT_XFER_BULK ||
	    pipe_type == USB_ENDPOINT_XFER_INT) {
		dwc2_host_complete(hsotg, qtd, -EPIPE);
		/*
		 * USB protocol requires resetting the data toggle for bulk
		 * and interrupt endpoints when a CLEAR_FEATURE(ENDPOINT_HALT)
		 * setup command is issued to the endpoint. Anticipate the
		 * CLEAR_FEATURE command since a STALL has occurred and reset
		 * the data toggle now.
		 */
		chan->qh->data_toggle = 0;
	}

handle_stall_halt:
	dwc2_halt_channel(hsotg, chan, qtd, DWC2_HC_XFER_STALL);

handle_stall_done:
	disable_hc_int(hsotg, chnum, HCINTMSK_STALL);
}

/*
 * Updates the state of the URB when a transfer has been stopped due to an
 * abnormal condition before the transfer completes. Modifies the
 * actual_length field of the URB to reflect the number of bytes that have
 * actually been transferred via the host channel.
 */
static void dwc2_update_urb_state_abn(struct dwc2_hsotg *hsotg,
				      struct dwc2_host_chan *chan, int chnum,
				      struct dwc2_hcd_urb *urb,
				      struct dwc2_qtd *qtd,
				      enum dwc2_halt_status halt_status)
{
	u32 xfer_length = dwc2_get_actual_xfer_length(hsotg, chan, chnum,
						      qtd, halt_status, NULL);
	u32 hctsiz;

	if (urb->actual_length + xfer_length > urb->length) {
		dev_warn(hsotg->dev, "%s(): trimming xfer length\n", __func__);
		xfer_length = urb->length - urb->actual_length;
	}

	/* Non DWORD-aligned buffer case handling */
	if (chan->align_buf && xfer_length && chan->ep_is_in) {
		dev_vdbg(hsotg->dev, "%s(): non-aligned buffer\n", __func__);
		dma_unmap_single(hsotg->dev, chan->qh->dw_align_buf_dma,
				chan->qh->dw_align_buf_size,
				chan->ep_is_in ?
				DMA_FROM_DEVICE : DMA_TO_DEVICE);
		if (chan->ep_is_in)
			memcpy(urb->buf + urb->actual_length,
					chan->qh->dw_align_buf,
					xfer_length);
	}

	urb->actual_length += xfer_length;

	hctsiz = dwc2_readl(hsotg->regs + HCTSIZ(chnum));
	dev_vdbg(hsotg->dev, "DWC_otg: %s: %s, channel %d\n",
		 __func__, (chan->ep_is_in ? "IN" : "OUT"), chnum);
	dev_vdbg(hsotg->dev, "  chan->start_pkt_count %d\n",
		 chan->start_pkt_count);
	dev_vdbg(hsotg->dev, "  hctsiz.pktcnt %d\n",
		 (hctsiz & TSIZ_PKTCNT_MASK) >> TSIZ_PKTCNT_SHIFT);
	dev_vdbg(hsotg->dev, "  chan->max_packet %d\n", chan->max_packet);
	dev_vdbg(hsotg->dev, "  bytes_transferred %d\n",
		 xfer_length);
	dev_vdbg(hsotg->dev, "  urb->actual_length %d\n",
		 urb->actual_length);
	dev_vdbg(hsotg->dev, "  urb->transfer_buffer_length %d\n",
		 urb->length);
}

/*
 * Handles a host channel NAK interrupt. This handler may be called in either
 * DMA mode or Slave mode.
 */
static void dwc2_hc_nak_intr(struct dwc2_hsotg *hsotg,
			     struct dwc2_host_chan *chan, int chnum,
			     struct dwc2_qtd *qtd)
{
	if (!qtd) {
		dev_dbg(hsotg->dev, "%s: qtd is NULL\n", __func__);
		return;
	}

	if (!qtd->urb) {
		dev_dbg(hsotg->dev, "%s: qtd->urb is NULL\n", __func__);
		return;
	}

	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "--Host Channel %d Interrupt: NAK Received--\n",
			 chnum);

	/*
	 * Handle NAK for IN/OUT SSPLIT/CSPLIT transfers, bulk, control, and
	 * interrupt. Re-start the SSPLIT transfer.
	 */
	if (chan->do_split) {
		if (chan->complete_split)
			qtd->error_count = 0;
		qtd->complete_split = 0;
		dwc2_halt_channel(hsotg, chan, qtd, DWC2_HC_XFER_NAK);
		goto handle_nak_done;
	}

	switch (dwc2_hcd_get_pipe_type(&qtd->urb->pipe_info)) {
	case USB_ENDPOINT_XFER_CONTROL:
	case USB_ENDPOINT_XFER_BULK:
		if (hsotg->core_params->dma_enable > 0 && chan->ep_is_in) {
			/*
			 * NAK interrupts are enabled on bulk/control IN
			 * transfers in DMA mode for the sole purpose of
			 * resetting the error count after a transaction error
			 * occurs. The core will continue transferring data.
			 */
			qtd->error_count = 0;
			break;
		}

		/*
		 * NAK interrupts normally occur during OUT transfers in DMA
		 * or Slave mode. For IN transfers, more requests will be
		 * queued as request queue space is available.
		 */
		qtd->error_count = 0;

		if (!chan->qh->ping_state) {
			dwc2_update_urb_state_abn(hsotg, chan, chnum, qtd->urb,
						  qtd, DWC2_HC_XFER_NAK);
			dwc2_hcd_save_data_toggle(hsotg, chan, chnum, qtd);

			if (chan->speed == USB_SPEED_HIGH)
				chan->qh->ping_state = 1;
		}

		/*
		 * Halt the channel so the transfer can be re-started from
		 * the appropriate point or the PING protocol will
		 * start/continue
		 */
		dwc2_halt_channel(hsotg, chan, qtd, DWC2_HC_XFER_NAK);
		break;
	case USB_ENDPOINT_XFER_INT:
		qtd->error_count = 0;
		dwc2_halt_channel(hsotg, chan, qtd, DWC2_HC_XFER_NAK);
		break;
	case USB_ENDPOINT_XFER_ISOC:
		/* Should never get called for isochronous transfers */
		dev_err(hsotg->dev, "NACK interrupt for ISOC transfer\n");
		break;
	}

handle_nak_done:
	disable_hc_int(hsotg, chnum, HCINTMSK_NAK);
}

/*
 * Handles a host channel ACK interrupt. This interrupt is enabled when
 * performing the PING protocol in Slave mode, when errors occur during
 * either Slave mode or DMA mode, and during Start Split transactions.
 */
static void dwc2_hc_ack_intr(struct dwc2_hsotg *hsotg,
			     struct dwc2_host_chan *chan, int chnum,
			     struct dwc2_qtd *qtd)
{
	struct dwc2_hcd_iso_packet_desc *frame_desc;

	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "--Host Channel %d Interrupt: ACK Received--\n",
			 chnum);

	if (chan->do_split) {
		/* Handle ACK on SSPLIT. ACK should not occur in CSPLIT. */
		if (!chan->ep_is_in &&
		    chan->data_pid_start != DWC2_HC_PID_SETUP)
			qtd->ssplit_out_xfer_count = chan->xfer_len;

		if (chan->ep_type != USB_ENDPOINT_XFER_ISOC || chan->ep_is_in) {
			qtd->complete_split = 1;
			dwc2_halt_channel(hsotg, chan, qtd, DWC2_HC_XFER_ACK);
		} else {
			/* ISOC OUT */
			switch (chan->xact_pos) {
			case DWC2_HCSPLT_XACTPOS_ALL:
				break;
			case DWC2_HCSPLT_XACTPOS_END:
				qtd->isoc_split_pos = DWC2_HCSPLT_XACTPOS_ALL;
				qtd->isoc_split_offset = 0;
				break;
			case DWC2_HCSPLT_XACTPOS_BEGIN:
			case DWC2_HCSPLT_XACTPOS_MID:
				/*
				 * For BEGIN or MID, calculate the length for
				 * the next microframe to determine the correct
				 * SSPLIT token, either MID or END
				 */
				frame_desc = &qtd->urb->iso_descs[
						qtd->isoc_frame_index];
				qtd->isoc_split_offset += 188;

				if (frame_desc->length - qtd->isoc_split_offset
							<= 188)
					qtd->isoc_split_pos =
							DWC2_HCSPLT_XACTPOS_END;
				else
					qtd->isoc_split_pos =
							DWC2_HCSPLT_XACTPOS_MID;
				break;
			}
		}
	} else {
		qtd->error_count = 0;

		if (chan->qh->ping_state) {
			chan->qh->ping_state = 0;
			/*
			 * Halt the channel so the transfer can be re-started
			 * from the appropriate point. This only happens in
			 * Slave mode. In DMA mode, the ping_state is cleared
			 * when the transfer is started because the core
			 * automatically executes the PING, then the transfer.
			 */
			dwc2_halt_channel(hsotg, chan, qtd, DWC2_HC_XFER_ACK);
		}
	}

	/*
	 * If the ACK occurred when _not_ in the PING state, let the channel
	 * continue transferring data after clearing the error count
	 */
	disable_hc_int(hsotg, chnum, HCINTMSK_ACK);
}

/*
 * Handles a host channel NYET interrupt. This interrupt should only occur on
 * Bulk and Control OUT endpoints and for complete split transactions. If a
 * NYET occurs at the same time as a Transfer Complete interrupt, it is
 * handled in the xfercomp interrupt handler, not here. This handler may be
 * called in either DMA mode or Slave mode.
 */
static void dwc2_hc_nyet_intr(struct dwc2_hsotg *hsotg,
			      struct dwc2_host_chan *chan, int chnum,
			      struct dwc2_qtd *qtd)
{
	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "--Host Channel %d Interrupt: NYET Received--\n",
			 chnum);

	/*
	 * NYET on CSPLIT
	 * re-do the CSPLIT immediately on non-periodic
	 */
	if (chan->do_split && chan->complete_split) {
		if (chan->ep_is_in && chan->ep_type == USB_ENDPOINT_XFER_ISOC &&
		    hsotg->core_params->dma_enable > 0) {
			qtd->complete_split = 0;
			qtd->isoc_split_offset = 0;
			qtd->isoc_frame_index++;
			if (qtd->urb &&
			    qtd->isoc_frame_index == qtd->urb->packet_count) {
				dwc2_host_complete(hsotg, qtd, 0);
				dwc2_release_channel(hsotg, chan, qtd,
						     DWC2_HC_XFER_URB_COMPLETE);
			} else {
				dwc2_release_channel(hsotg, chan, qtd,
						DWC2_HC_XFER_NO_HALT_STATUS);
			}
			goto handle_nyet_done;
		}

		if (chan->ep_type == USB_ENDPOINT_XFER_INT ||
		    chan->ep_type == USB_ENDPOINT_XFER_ISOC) {
			int frnum = dwc2_hcd_get_frame_number(hsotg);

			if (dwc2_full_frame_num(frnum) !=
			    dwc2_full_frame_num(chan->qh->sched_frame)) {
				/*
				 * No longer in the same full speed frame.
				 * Treat this as a transaction error.
				 */
#if 0
				/*
				 * Todo: Fix system performance so this can
				 * be treated as an error. Right now complete
				 * splits cannot be scheduled precisely enough
				 * due to other system activity, so this error
				 * occurs regularly in Slave mode.
				 */
				qtd->error_count++;
#endif
				qtd->complete_split = 0;
				dwc2_halt_channel(hsotg, chan, qtd,
						  DWC2_HC_XFER_XACT_ERR);
				/* Todo: add support for isoc release */
				goto handle_nyet_done;
			}
		}

		dwc2_halt_channel(hsotg, chan, qtd, DWC2_HC_XFER_NYET);
		goto handle_nyet_done;
	}

	chan->qh->ping_state = 1;
	qtd->error_count = 0;

	dwc2_update_urb_state_abn(hsotg, chan, chnum, qtd->urb, qtd,
				  DWC2_HC_XFER_NYET);
	dwc2_hcd_save_data_toggle(hsotg, chan, chnum, qtd);

	/*
	 * Halt the channel and re-start the transfer so the PING protocol
	 * will start
	 */
	dwc2_halt_channel(hsotg, chan, qtd, DWC2_HC_XFER_NYET);

handle_nyet_done:
	disable_hc_int(hsotg, chnum, HCINTMSK_NYET);
}

/*
 * Handles a host channel babble interrupt. This handler may be called in
 * either DMA mode or Slave mode.
 */
static void dwc2_hc_babble_intr(struct dwc2_hsotg *hsotg,
				struct dwc2_host_chan *chan, int chnum,
				struct dwc2_qtd *qtd)
{
	dev_dbg(hsotg->dev, "--Host Channel %d Interrupt: Babble Error--\n",
		chnum);

	dwc2_hc_handle_tt_clear(hsotg, chan, qtd);

	if (hsotg->core_params->dma_desc_enable > 0) {
		dwc2_hcd_complete_xfer_ddma(hsotg, chan, chnum,
					    DWC2_HC_XFER_BABBLE_ERR);
		goto disable_int;
	}

	if (chan->ep_type != USB_ENDPOINT_XFER_ISOC) {
		dwc2_host_complete(hsotg, qtd, -EOVERFLOW);
		dwc2_halt_channel(hsotg, chan, qtd, DWC2_HC_XFER_BABBLE_ERR);
	} else {
		enum dwc2_halt_status halt_status;

		halt_status = dwc2_update_isoc_urb_state(hsotg, chan, chnum,
						qtd, DWC2_HC_XFER_BABBLE_ERR);
		dwc2_halt_channel(hsotg, chan, qtd, halt_status);
	}

disable_int:
	disable_hc_int(hsotg, chnum, HCINTMSK_BBLERR);
}

/*
 * Handles a host channel AHB error interrupt. This handler is only called in
 * DMA mode.
 */
static void dwc2_hc_ahberr_intr(struct dwc2_hsotg *hsotg,
				struct dwc2_host_chan *chan, int chnum,
				struct dwc2_qtd *qtd)
{
	struct dwc2_hcd_urb *urb = qtd->urb;
	char *pipetype, *speed;
	u32 hcchar;
	u32 hcsplt;
	u32 hctsiz;
	u32 hc_dma;

	dev_dbg(hsotg->dev, "--Host Channel %d Interrupt: AHB Error--\n",
		chnum);

	if (!urb)
		goto handle_ahberr_halt;

	dwc2_hc_handle_tt_clear(hsotg, chan, qtd);

	hcchar = dwc2_readl(hsotg->regs + HCCHAR(chnum));
	hcsplt = dwc2_readl(hsotg->regs + HCSPLT(chnum));
	hctsiz = dwc2_readl(hsotg->regs + HCTSIZ(chnum));
	hc_dma = dwc2_readl(hsotg->regs + HCDMA(chnum));

	dev_err(hsotg->dev, "AHB ERROR, Channel %d\n", chnum);
	dev_err(hsotg->dev, "  hcchar 0x%08x, hcsplt 0x%08x\n", hcchar, hcsplt);
	dev_err(hsotg->dev, "  hctsiz 0x%08x, hc_dma 0x%08x\n", hctsiz, hc_dma);
	dev_err(hsotg->dev, "  Device address: %d\n",
		dwc2_hcd_get_dev_addr(&urb->pipe_info));
	dev_err(hsotg->dev, "  Endpoint: %d, %s\n",
		dwc2_hcd_get_ep_num(&urb->pipe_info),
		dwc2_hcd_is_pipe_in(&urb->pipe_info) ? "IN" : "OUT");

	switch (dwc2_hcd_get_pipe_type(&urb->pipe_info)) {
	case USB_ENDPOINT_XFER_CONTROL:
		pipetype = "CONTROL";
		break;
	case USB_ENDPOINT_XFER_BULK:
		pipetype = "BULK";
		break;
	case USB_ENDPOINT_XFER_INT:
		pipetype = "INTERRUPT";
		break;
	case USB_ENDPOINT_XFER_ISOC:
		pipetype = "ISOCHRONOUS";
		break;
	default:
		pipetype = "UNKNOWN";
		break;
	}

	dev_err(hsotg->dev, "  Endpoint type: %s\n", pipetype);

	switch (chan->speed) {
	case USB_SPEED_HIGH:
		speed = "HIGH";
		break;
	case USB_SPEED_FULL:
		speed = "FULL";
		break;
	case USB_SPEED_LOW:
		speed = "LOW";
		break;
	default:
		speed = "UNKNOWN";
		break;
	}

	dev_err(hsotg->dev, "  Speed: %s\n", speed);

	dev_err(hsotg->dev, "  Max packet size: %d\n",
		dwc2_hcd_get_mps(&urb->pipe_info));
	dev_err(hsotg->dev, "  Data buffer length: %d\n", urb->length);
	dev_err(hsotg->dev, "  Transfer buffer: %p, Transfer DMA: %08lx\n",
		urb->buf, (unsigned long)urb->dma);
	dev_err(hsotg->dev, "  Setup buffer: %p, Setup DMA: %08lx\n",
		urb->setup_packet, (unsigned long)urb->setup_dma);
	dev_err(hsotg->dev, "  Interval: %d\n", urb->interval);

	/* Core halts the channel for Descriptor DMA mode */
	if (hsotg->core_params->dma_desc_enable > 0) {
		dwc2_hcd_complete_xfer_ddma(hsotg, chan, chnum,
					    DWC2_HC_XFER_AHB_ERR);
		goto handle_ahberr_done;
	}

	dwc2_host_complete(hsotg, qtd, -EIO);

handle_ahberr_halt:
	/*
	 * Force a channel halt. Don't call dwc2_halt_channel because that won't
	 * write to the HCCHARn register in DMA mode to force the halt.
	 */
	dwc2_hc_halt(hsotg, chan, DWC2_HC_XFER_AHB_ERR);

handle_ahberr_done:
	disable_hc_int(hsotg, chnum, HCINTMSK_AHBERR);
}

/*
 * Handles a host channel transaction error interrupt. This handler may be
 * called in either DMA mode or Slave mode.
 */
static void dwc2_hc_xacterr_intr(struct dwc2_hsotg *hsotg,
				 struct dwc2_host_chan *chan, int chnum,
				 struct dwc2_qtd *qtd)
{
	dev_dbg(hsotg->dev,
		"--Host Channel %d Interrupt: Transaction Error--\n", chnum);

	dwc2_hc_handle_tt_clear(hsotg, chan, qtd);

	if (hsotg->core_params->dma_desc_enable > 0) {
		dwc2_hcd_complete_xfer_ddma(hsotg, chan, chnum,
					    DWC2_HC_XFER_XACT_ERR);
		goto handle_xacterr_done;
	}

	switch (dwc2_hcd_get_pipe_type(&qtd->urb->pipe_info)) {
	case USB_ENDPOINT_XFER_CONTROL:
	case USB_ENDPOINT_XFER_BULK:
		qtd->error_count++;
		if (!chan->qh->ping_state) {

			dwc2_update_urb_state_abn(hsotg, chan, chnum, qtd->urb,
						  qtd, DWC2_HC_XFER_XACT_ERR);
			dwc2_hcd_save_data_toggle(hsotg, chan, chnum, qtd);
			if (!chan->ep_is_in && chan->speed == USB_SPEED_HIGH)
				chan->qh->ping_state = 1;
		}

		/*
		 * Halt the channel so the transfer can be re-started from
		 * the appropriate point or the PING protocol will start
		 */
		dwc2_halt_channel(hsotg, chan, qtd, DWC2_HC_XFER_XACT_ERR);
		break;
	case USB_ENDPOINT_XFER_INT:
		qtd->error_count++;
		if (chan->do_split && chan->complete_split)
			qtd->complete_split = 0;
		dwc2_halt_channel(hsotg, chan, qtd, DWC2_HC_XFER_XACT_ERR);
		break;
	case USB_ENDPOINT_XFER_ISOC:
		{
			enum dwc2_halt_status halt_status;

			halt_status = dwc2_update_isoc_urb_state(hsotg, chan,
					chnum, qtd, DWC2_HC_XFER_XACT_ERR);
			dwc2_halt_channel(hsotg, chan, qtd, halt_status);
		}
		break;
	}

handle_xacterr_done:
	disable_hc_int(hsotg, chnum, HCINTMSK_XACTERR);
}

/*
 * Handles a host channel frame overrun interrupt. This handler may be called
 * in either DMA mode or Slave mode.
 */
static void dwc2_hc_frmovrun_intr(struct dwc2_hsotg *hsotg,
				  struct dwc2_host_chan *chan, int chnum,
				  struct dwc2_qtd *qtd)
{
	enum dwc2_halt_status halt_status;

	if (dbg_hc(chan))
		dev_dbg(hsotg->dev, "--Host Channel %d Interrupt: Frame Overrun--\n",
			chnum);

	dwc2_hc_handle_tt_clear(hsotg, chan, qtd);

	switch (dwc2_hcd_get_pipe_type(&qtd->urb->pipe_info)) {
	case USB_ENDPOINT_XFER_CONTROL:
	case USB_ENDPOINT_XFER_BULK:
		break;
	case USB_ENDPOINT_XFER_INT:
		dwc2_halt_channel(hsotg, chan, qtd, DWC2_HC_XFER_FRAME_OVERRUN);
		break;
	case USB_ENDPOINT_XFER_ISOC:
		halt_status = dwc2_update_isoc_urb_state(hsotg, chan, chnum,
					qtd, DWC2_HC_XFER_FRAME_OVERRUN);
		dwc2_halt_channel(hsotg, chan, qtd, halt_status);
		break;
	}

	disable_hc_int(hsotg, chnum, HCINTMSK_FRMOVRUN);
}

/*
 * Handles a host channel data toggle error interrupt. This handler may be
 * called in either DMA mode or Slave mode.
 */
static void dwc2_hc_datatglerr_intr(struct dwc2_hsotg *hsotg,
				    struct dwc2_host_chan *chan, int chnum,
				    struct dwc2_qtd *qtd)
{
	dev_dbg(hsotg->dev,
		"--Host Channel %d Interrupt: Data Toggle Error--\n", chnum);

	if (chan->ep_is_in)
		qtd->error_count = 0;
	else
		dev_err(hsotg->dev,
			"Data Toggle Error on OUT transfer, channel %d\n",
			chnum);

	dwc2_hc_handle_tt_clear(hsotg, chan, qtd);
	disable_hc_int(hsotg, chnum, HCINTMSK_DATATGLERR);
}

/*
 * For debug only. It checks that a valid halt status is set and that
 * HCCHARn.chdis is clear. If there's a problem, corrective action is
 * taken and a warning is issued.
 *
 * Return: true if halt status is ok, false otherwise
 */
static bool dwc2_halt_status_ok(struct dwc2_hsotg *hsotg,
				struct dwc2_host_chan *chan, int chnum,
				struct dwc2_qtd *qtd)
{
#ifdef DEBUG
	u32 hcchar;
	u32 hctsiz;
	u32 hcintmsk;
	u32 hcsplt;

	if (chan->halt_status == DWC2_HC_XFER_NO_HALT_STATUS) {
		/*
		 * This code is here only as a check. This condition should
		 * never happen. Ignore the halt if it does occur.
		 */
		hcchar = dwc2_readl(hsotg->regs + HCCHAR(chnum));
		hctsiz = dwc2_readl(hsotg->regs + HCTSIZ(chnum));
		hcintmsk = dwc2_readl(hsotg->regs + HCINTMSK(chnum));
		hcsplt = dwc2_readl(hsotg->regs + HCSPLT(chnum));
		dev_dbg(hsotg->dev,
			"%s: chan->halt_status DWC2_HC_XFER_NO_HALT_STATUS,\n",
			 __func__);
		dev_dbg(hsotg->dev,
			"channel %d, hcchar 0x%08x, hctsiz 0x%08x,\n",
			chnum, hcchar, hctsiz);
		dev_dbg(hsotg->dev,
			"hcint 0x%08x, hcintmsk 0x%08x, hcsplt 0x%08x,\n",
			chan->hcint, hcintmsk, hcsplt);
		if (qtd)
			dev_dbg(hsotg->dev, "qtd->complete_split %d\n",
				qtd->complete_split);
		dev_warn(hsotg->dev,
			 "%s: no halt status, channel %d, ignoring interrupt\n",
			 __func__, chnum);
		return false;
	}

	/*
	 * This code is here only as a check. hcchar.chdis should never be set
	 * when the halt interrupt occurs. Halt the channel again if it does
	 * occur.
	 */
	hcchar = dwc2_readl(hsotg->regs + HCCHAR(chnum));
	if (hcchar & HCCHAR_CHDIS) {
		dev_warn(hsotg->dev,
			 "%s: hcchar.chdis set unexpectedly, hcchar 0x%08x, trying to halt again\n",
			 __func__, hcchar);
		chan->halt_pending = 0;
		dwc2_halt_channel(hsotg, chan, qtd, chan->halt_status);
		return false;
	}
#endif

	return true;
}

/*
 * Handles a host Channel Halted interrupt in DMA mode. This handler
 * determines the reason the channel halted and proceeds accordingly.
 */
static void dwc2_hc_chhltd_intr_dma(struct dwc2_hsotg *hsotg,
				    struct dwc2_host_chan *chan, int chnum,
				    struct dwc2_qtd *qtd)
{
	u32 hcintmsk;
	int out_nak_enh = 0;

	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev,
			 "--Host Channel %d Interrupt: DMA Channel Halted--\n",
			 chnum);

	/*
	 * For core with OUT NAK enhancement, the flow for high-speed
	 * CONTROL/BULK OUT is handled a little differently
	 */
	if (hsotg->hw_params.snpsid >= DWC2_CORE_REV_2_71a) {
		if (chan->speed == USB_SPEED_HIGH && !chan->ep_is_in &&
		    (chan->ep_type == USB_ENDPOINT_XFER_CONTROL ||
		     chan->ep_type == USB_ENDPOINT_XFER_BULK)) {
			out_nak_enh = 1;
		}
	}

	if (chan->halt_status == DWC2_HC_XFER_URB_DEQUEUE ||
	    (chan->halt_status == DWC2_HC_XFER_AHB_ERR &&
	     hsotg->core_params->dma_desc_enable <= 0)) {
		if (hsotg->core_params->dma_desc_enable > 0)
			dwc2_hcd_complete_xfer_ddma(hsotg, chan, chnum,
						    chan->halt_status);
		else
			/*
			 * Just release the channel. A dequeue can happen on a
			 * transfer timeout. In the case of an AHB Error, the
			 * channel was forced to halt because there's no way to
			 * gracefully recover.
			 */
			dwc2_release_channel(hsotg, chan, qtd,
					     chan->halt_status);
		return;
	}

	hcintmsk = dwc2_readl(hsotg->regs + HCINTMSK(chnum));

	if (chan->hcint & HCINTMSK_XFERCOMPL) {
		/*
		 * Todo: This is here because of a possible hardware bug. Spec
		 * says that on SPLIT-ISOC OUT transfers in DMA mode that a HALT
		 * interrupt w/ACK bit set should occur, but I only see the
		 * XFERCOMP bit, even with it masked out. This is a workaround
		 * for that behavior. Should fix this when hardware is fixed.
		 */
		if (chan->ep_type == USB_ENDPOINT_XFER_ISOC && !chan->ep_is_in)
			dwc2_hc_ack_intr(hsotg, chan, chnum, qtd);
		dwc2_hc_xfercomp_intr(hsotg, chan, chnum, qtd);
	} else if (chan->hcint & HCINTMSK_STALL) {
		dwc2_hc_stall_intr(hsotg, chan, chnum, qtd);
	} else if ((chan->hcint & HCINTMSK_XACTERR) &&
		   hsotg->core_params->dma_desc_enable <= 0) {
		if (out_nak_enh) {
			if (chan->hcint &
			    (HCINTMSK_NYET | HCINTMSK_NAK | HCINTMSK_ACK)) {
				dev_vdbg(hsotg->dev,
					 "XactErr with NYET/NAK/ACK\n");
				qtd->error_count = 0;
			} else {
				dev_vdbg(hsotg->dev,
					 "XactErr without NYET/NAK/ACK\n");
			}
		}

		/*
		 * Must handle xacterr before nak or ack. Could get a xacterr
		 * at the same time as either of these on a BULK/CONTROL OUT
		 * that started with a PING. The xacterr takes precedence.
		 */
		dwc2_hc_xacterr_intr(hsotg, chan, chnum, qtd);
	} else if ((chan->hcint & HCINTMSK_XCS_XACT) &&
		   hsotg->core_params->dma_desc_enable > 0) {
		dwc2_hc_xacterr_intr(hsotg, chan, chnum, qtd);
	} else if ((chan->hcint & HCINTMSK_AHBERR) &&
		   hsotg->core_params->dma_desc_enable > 0) {
		dwc2_hc_ahberr_intr(hsotg, chan, chnum, qtd);
	} else if (chan->hcint & HCINTMSK_BBLERR) {
		dwc2_hc_babble_intr(hsotg, chan, chnum, qtd);
	} else if (chan->hcint & HCINTMSK_FRMOVRUN) {
		dwc2_hc_frmovrun_intr(hsotg, chan, chnum, qtd);
	} else if (!out_nak_enh) {
		if (chan->hcint & HCINTMSK_NYET) {
			/*
			 * Must handle nyet before nak or ack. Could get a nyet
			 * at the same time as either of those on a BULK/CONTROL
			 * OUT that started with a PING. The nyet takes
			 * precedence.
			 */
			dwc2_hc_nyet_intr(hsotg, chan, chnum, qtd);
		} else if ((chan->hcint & HCINTMSK_NAK) &&
			   !(hcintmsk & HCINTMSK_NAK)) {
			/*
			 * If nak is not masked, it's because a non-split IN
			 * transfer is in an error state. In that case, the nak
			 * is handled by the nak interrupt handler, not here.
			 * Handle nak here for BULK/CONTROL OUT transfers, which
			 * halt on a NAK to allow rewinding the buffer pointer.
			 */
			dwc2_hc_nak_intr(hsotg, chan, chnum, qtd);
		} else if ((chan->hcint & HCINTMSK_ACK) &&
			   !(hcintmsk & HCINTMSK_ACK)) {
			/*
			 * If ack is not masked, it's because a non-split IN
			 * transfer is in an error state. In that case, the ack
			 * is handled by the ack interrupt handler, not here.
			 * Handle ack here for split transfers. Start splits
			 * halt on ACK.
			 */
			dwc2_hc_ack_intr(hsotg, chan, chnum, qtd);
		} else {
			if (chan->ep_type == USB_ENDPOINT_XFER_INT ||
			    chan->ep_type == USB_ENDPOINT_XFER_ISOC) {
				/*
				 * A periodic transfer halted with no other
				 * channel interrupts set. Assume it was halted
				 * by the core because it could not be completed
				 * in its scheduled (micro)frame.
				 */
				dev_dbg(hsotg->dev,
					"%s: Halt channel %d (assume incomplete periodic transfer)\n",
					__func__, chnum);
				dwc2_halt_channel(hsotg, chan, qtd,
					DWC2_HC_XFER_PERIODIC_INCOMPLETE);
			} else {
				dev_err(hsotg->dev,
					"%s: Channel %d - ChHltd set, but reason is unknown\n",
					__func__, chnum);
				dev_err(hsotg->dev,
					"hcint 0x%08x, intsts 0x%08x\n",
					chan->hcint,
					dwc2_readl(hsotg->regs + GINTSTS));
				goto error;
			}
		}
	} else {
		dev_info(hsotg->dev,
			 "NYET/NAK/ACK/other in non-error case, 0x%08x\n",
			 chan->hcint);
error:
		/* Failthrough: use 3-strikes rule */
		qtd->error_count++;
		dwc2_update_urb_state_abn(hsotg, chan, chnum, qtd->urb,
					  qtd, DWC2_HC_XFER_XACT_ERR);
		dwc2_hcd_save_data_toggle(hsotg, chan, chnum, qtd);
		dwc2_halt_channel(hsotg, chan, qtd, DWC2_HC_XFER_XACT_ERR);
	}
}

/*
 * Handles a host channel Channel Halted interrupt
 *
 * In slave mode, this handler is called only when the driver specifically
 * requests a halt. This occurs during handling other host channel interrupts
 * (e.g. nak, xacterr, stall, nyet, etc.).
 *
 * In DMA mode, this is the interrupt that occurs when the core has finished
 * processing a transfer on a channel. Other host channel interrupts (except
 * ahberr) are disabled in DMA mode.
 */
static void dwc2_hc_chhltd_intr(struct dwc2_hsotg *hsotg,
				struct dwc2_host_chan *chan, int chnum,
				struct dwc2_qtd *qtd)
{
	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "--Host Channel %d Interrupt: Channel Halted--\n",
			 chnum);

	if (hsotg->core_params->dma_enable > 0) {
		dwc2_hc_chhltd_intr_dma(hsotg, chan, chnum, qtd);
	} else {
		if (!dwc2_halt_status_ok(hsotg, chan, chnum, qtd))
			return;
		dwc2_release_channel(hsotg, chan, qtd, chan->halt_status);
	}
}

/*
 * Check if the given qtd is still the top of the list (and thus valid).
 *
 * If dwc2_hcd_qtd_unlink_and_free() has been called since we grabbed
 * the qtd from the top of the list, this will return false (otherwise true).
 */
static bool dwc2_check_qtd_still_ok(struct dwc2_qtd *qtd, struct dwc2_qh *qh)
{
	struct dwc2_qtd *cur_head;

	if (qh == NULL)
		return false;

	cur_head = list_first_entry(&qh->qtd_list, struct dwc2_qtd,
				    qtd_list_entry);
	return (cur_head == qtd);
}

/* Handles interrupt for a specific Host Channel */
static void dwc2_hc_n_intr(struct dwc2_hsotg *hsotg, int chnum)
{
	struct dwc2_qtd *qtd;
	struct dwc2_host_chan *chan;
	u32 hcint, hcintmsk;

	chan = hsotg->hc_ptr_array[chnum];

	hcint = dwc2_readl(hsotg->regs + HCINT(chnum));
	hcintmsk = dwc2_readl(hsotg->regs + HCINTMSK(chnum));
	if (!chan) {
		dev_err(hsotg->dev, "## hc_ptr_array for channel is NULL ##\n");
		dwc2_writel(hcint, hsotg->regs + HCINT(chnum));
		return;
	}

	if (dbg_hc(chan)) {
		dev_vdbg(hsotg->dev, "--Host Channel Interrupt--, Channel %d\n",
			 chnum);
		dev_vdbg(hsotg->dev,
			 "  hcint 0x%08x, hcintmsk 0x%08x, hcint&hcintmsk 0x%08x\n",
			 hcint, hcintmsk, hcint & hcintmsk);
	}

	dwc2_writel(hcint, hsotg->regs + HCINT(chnum));
	chan->hcint = hcint;
	hcint &= hcintmsk;

	/*
	 * If the channel was halted due to a dequeue, the qtd list might
	 * be empty or at least the first entry will not be the active qtd.
	 * In this case, take a shortcut and just release the channel.
	 */
	if (chan->halt_status == DWC2_HC_XFER_URB_DEQUEUE) {
		/*
		 * If the channel was halted, this should be the only
		 * interrupt unmasked
		 */
		WARN_ON(hcint != HCINTMSK_CHHLTD);
		if (hsotg->core_params->dma_desc_enable > 0)
			dwc2_hcd_complete_xfer_ddma(hsotg, chan, chnum,
						    chan->halt_status);
		else
			dwc2_release_channel(hsotg, chan, NULL,
					     chan->halt_status);
		return;
	}

	if (list_empty(&chan->qh->qtd_list)) {
		/*
		 * TODO: Will this ever happen with the
		 * DWC2_HC_XFER_URB_DEQUEUE handling above?
		 */
		dev_dbg(hsotg->dev, "## no QTD queued for channel %d ##\n",
			chnum);
		dev_dbg(hsotg->dev,
			"  hcint 0x%08x, hcintmsk 0x%08x, hcint&hcintmsk 0x%08x\n",
			chan->hcint, hcintmsk, hcint);
		chan->halt_status = DWC2_HC_XFER_NO_HALT_STATUS;
		disable_hc_int(hsotg, chnum, HCINTMSK_CHHLTD);
		chan->hcint = 0;
		return;
	}

	qtd = list_first_entry(&chan->qh->qtd_list, struct dwc2_qtd,
			       qtd_list_entry);

	if (hsotg->core_params->dma_enable <= 0) {
		if ((hcint & HCINTMSK_CHHLTD) && hcint != HCINTMSK_CHHLTD)
			hcint &= ~HCINTMSK_CHHLTD;
	}

	if (hcint & HCINTMSK_XFERCOMPL) {
		dwc2_hc_xfercomp_intr(hsotg, chan, chnum, qtd);
		/*
		 * If NYET occurred at same time as Xfer Complete, the NYET is
		 * handled by the Xfer Complete interrupt handler. Don't want
		 * to call the NYET interrupt handler in this case.
		 */
		hcint &= ~HCINTMSK_NYET;
	}

	if (hcint & HCINTMSK_CHHLTD) {
		dwc2_hc_chhltd_intr(hsotg, chan, chnum, qtd);
		if (!dwc2_check_qtd_still_ok(qtd, chan->qh))
			goto exit;
	}
	if (hcint & HCINTMSK_AHBERR) {
		dwc2_hc_ahberr_intr(hsotg, chan, chnum, qtd);
		if (!dwc2_check_qtd_still_ok(qtd, chan->qh))
			goto exit;
	}
	if (hcint & HCINTMSK_STALL) {
		dwc2_hc_stall_intr(hsotg, chan, chnum, qtd);
		if (!dwc2_check_qtd_still_ok(qtd, chan->qh))
			goto exit;
	}
	if (hcint & HCINTMSK_NAK) {
		dwc2_hc_nak_intr(hsotg, chan, chnum, qtd);
		if (!dwc2_check_qtd_still_ok(qtd, chan->qh))
			goto exit;
	}
	if (hcint & HCINTMSK_ACK) {
		dwc2_hc_ack_intr(hsotg, chan, chnum, qtd);
		if (!dwc2_check_qtd_still_ok(qtd, chan->qh))
			goto exit;
	}
	if (hcint & HCINTMSK_NYET) {
		dwc2_hc_nyet_intr(hsotg, chan, chnum, qtd);
		if (!dwc2_check_qtd_still_ok(qtd, chan->qh))
			goto exit;
	}
	if (hcint & HCINTMSK_XACTERR) {
		dwc2_hc_xacterr_intr(hsotg, chan, chnum, qtd);
		if (!dwc2_check_qtd_still_ok(qtd, chan->qh))
			goto exit;
	}
	if (hcint & HCINTMSK_BBLERR) {
		dwc2_hc_babble_intr(hsotg, chan, chnum, qtd);
		if (!dwc2_check_qtd_still_ok(qtd, chan->qh))
			goto exit;
	}
	if (hcint & HCINTMSK_FRMOVRUN) {
		dwc2_hc_frmovrun_intr(hsotg, chan, chnum, qtd);
		if (!dwc2_check_qtd_still_ok(qtd, chan->qh))
			goto exit;
	}
	if (hcint & HCINTMSK_DATATGLERR) {
		dwc2_hc_datatglerr_intr(hsotg, chan, chnum, qtd);
		if (!dwc2_check_qtd_still_ok(qtd, chan->qh))
			goto exit;
	}

exit:
	chan->hcint = 0;
}

/*
 * This interrupt indicates that one or more host channels has a pending
 * interrupt. There are multiple conditions that can cause each host channel
 * interrupt. This function determines which conditions have occurred for each
 * host channel interrupt and handles them appropriately.
 */
static void dwc2_hc_intr(struct dwc2_hsotg *hsotg)
{
	u32 haint;
	int i;

	haint = dwc2_readl(hsotg->regs + HAINT);
	if (dbg_perio()) {
		dev_vdbg(hsotg->dev, "%s()\n", __func__);

		dev_vdbg(hsotg->dev, "HAINT=%08x\n", haint);
	}

	for (i = 0; i < hsotg->core_params->host_channels; i++) {
		if (haint & (1 << i))
			dwc2_hc_n_intr(hsotg, i);
	}
}

/* This function handles interrupts for the HCD */
irqreturn_t dwc2_handle_hcd_intr(struct dwc2_hsotg *hsotg)
{
	u32 gintsts, dbg_gintsts;
	irqreturn_t retval = IRQ_NONE;

	if (!dwc2_is_controller_alive(hsotg)) {
		dev_warn(hsotg->dev, "Controller is dead\n");
		return retval;
	}

	spin_lock(&hsotg->lock);

	/* Check if HOST Mode */
	if (dwc2_is_host_mode(hsotg)) {
		gintsts = dwc2_read_core_intr(hsotg);
		if (!gintsts) {
			spin_unlock(&hsotg->lock);
			return retval;
		}

		retval = IRQ_HANDLED;

		dbg_gintsts = gintsts;
#ifndef DEBUG_SOF
		dbg_gintsts &= ~GINTSTS_SOF;
#endif
		if (!dbg_perio())
			dbg_gintsts &= ~(GINTSTS_HCHINT | GINTSTS_RXFLVL |
					 GINTSTS_PTXFEMP);

		/* Only print if there are any non-suppressed interrupts left */
		if (dbg_gintsts)
			dev_vdbg(hsotg->dev,
				 "DWC OTG HCD Interrupt Detected gintsts&gintmsk=0x%08x\n",
				 gintsts);

		if (gintsts & GINTSTS_SOF)
			dwc2_sof_intr(hsotg);
		if (gintsts & GINTSTS_RXFLVL)
			dwc2_rx_fifo_level_intr(hsotg);
		if (gintsts & GINTSTS_NPTXFEMP)
			dwc2_np_tx_fifo_empty_intr(hsotg);
		if (gintsts & GINTSTS_PRTINT)
			dwc2_port_intr(hsotg);
		if (gintsts & GINTSTS_HCHINT)
			dwc2_hc_intr(hsotg);
		if (gintsts & GINTSTS_PTXFEMP)
			dwc2_perio_tx_fifo_empty_intr(hsotg);

		if (dbg_gintsts) {
			dev_vdbg(hsotg->dev,
				 "DWC OTG HCD Finished Servicing Interrupts\n");
			dev_vdbg(hsotg->dev,
				 "DWC OTG HCD gintsts=0x%08x gintmsk=0x%08x\n",
				 dwc2_readl(hsotg->regs + GINTSTS),
				 dwc2_readl(hsotg->regs + GINTMSK));
		}
	}

	spin_unlock(&hsotg->lock);

	return retval;
}
