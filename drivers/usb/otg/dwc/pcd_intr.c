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

#include "driver.h"
#include "pcd.h"

/**
 * This function returns pointer to in ep struct with number num
 */
static struct pcd_ep *get_in_ep(struct dwc_pcd *pcd, u32 num)
{
	if (num == 0) {
		return &pcd->ep0;
	} else {
		u32 i;
		int num_in_eps = GET_CORE_IF(pcd)->dev_if->num_in_eps;

		for (i = 0; i < num_in_eps; ++i) {
			if (pcd->in_ep[i].dwc_ep.num == num)
				return &pcd->in_ep[i];
		}
	}
	return NULL;
}

/**
 * This function returns pointer to out ep struct with number num
 */
static struct pcd_ep *get_out_ep(struct dwc_pcd *pcd, u32 num)
{
	if (num == 0) {
		return &pcd->ep0;
	} else {
		u32 i;
		int num_out_eps = GET_CORE_IF(pcd)->dev_if->num_out_eps;

		for (i = 0; i < num_out_eps; ++i) {
			if (pcd->out_ep[i].dwc_ep.num == num)
				return &pcd->out_ep[i];
		}
	}
	return NULL;
}

/**
 * This functions gets a pointer to an EP from the wIndex address
 * value of the control request.
 */
static struct pcd_ep *get_ep_by_addr(struct dwc_pcd *pcd, u16 index)
{
	struct pcd_ep *ep;

	if (!(index & USB_ENDPOINT_NUMBER_MASK))
		return &pcd->ep0;

	list_for_each_entry(ep, &pcd->gadget.ep_list, ep.ep_list) {
		u8 bEndpointAddress;

		if (!ep->desc)
			continue;

		bEndpointAddress = ep->desc->bEndpointAddress;
		if ((index ^ bEndpointAddress) & USB_DIR_IN)
			continue;

		if ((index & 0x0f) == (bEndpointAddress & 0x0f))
			return ep;
	}
	return NULL;
}

/**
 * This function checks the EP request queue, if the queue is not
 * empty the next request is started.
 */
void dwc_start_next_request(struct pcd_ep *ep)
{
	if (!list_empty(&ep->queue) && mutex_trylock(&ep->dwc_ep.xfer_mutex)) {
		struct pcd_request *req;

		req = list_entry(ep->queue.next, struct pcd_request, queue);

		/* Setup and start the Transfer */
		ep->dwc_ep.start_xfer_buff = req->req.buf;
		ep->dwc_ep.xfer_buff = req->req.buf;
		ep->dwc_ep.xfer_len = req->req.length;
		ep->dwc_ep.xfer_count = 0;
		ep->dwc_ep.dma_addr = req->req.dma;
		ep->dwc_ep.sent_zlp = 0;
		ep->dwc_ep.total_len = ep->dwc_ep.xfer_len;

		/*
		 * Added-sr: 2007-07-26
		 *
		 * When a new transfer will be started, mark this
		 * endpoint as active. This way it will be blocked
		 * for further transfers, until the current transfer
		 * is finished.
		 */
		if (dwc_has_feature(GET_CORE_IF(ep->pcd), DWC_LIMITED_XFER))
			ep->dwc_ep.active = 1;
		dwc_otg_ep_start_transfer(GET_CORE_IF(ep->pcd), &ep->dwc_ep);
	}
}

/**
 * This function handles the SOF Interrupts. At this time the SOF
 * Interrupt is disabled.
 */
static int dwc_otg_pcd_handle_sof_intr(struct dwc_pcd *pcd)
{
	struct core_if *core_if = GET_CORE_IF(pcd);
	u32 gintsts;

	/* Clear interrupt */
	gintsts = 0;
	gintsts |= DWC_INTMSK_STRT_OF_FRM;
	dwc_reg_write((core_if->core_global_regs), DWC_GINTSTS, gintsts);
	return 1;
}

/**
 * This function reads the 8 bytes of the setup packet from the Rx FIFO into the
 * destination buffer.  It is called from the Rx Status Queue Level (RxStsQLvl)
 * interrupt routine when a SETUP packet has been received in Slave mode.
 */
static void dwc_otg_read_setup_packet(struct core_if *core_if, u32 * dest)
{
	dest[0] = dwc_read_fifo32(core_if->data_fifo[0]);
	dest[1] = dwc_read_fifo32(core_if->data_fifo[0]);
}

/**
 * This function handles the Rx Status Queue Level Interrupt, which
 * indicates that there is a least one packet in the Rx FIFO.  The
 * packets are moved from the FIFO to memory, where they will be
 * processed when the Endpoint Interrupt Register indicates Transfer
 * Complete or SETUP Phase Done.
 *
 * Repeat the following until the Rx Status Queue is empty:
 *	 -# Read the Receive Status Pop Register (GRXSTSP) to get Packet
 *		info
 *	 -# If Receive FIFO is empty then skip to step Clear the interrupt
 *		and exit
 *	 -# If SETUP Packet call dwc_otg_read_setup_packet to copy the
 *		SETUP data to the buffer
 *	 -# If OUT Data Packet call dwc_otg_read_packet to copy the data
 *		to the destination buffer
 */
static int dwc_otg_pcd_handle_rx_status_q_level_intr(struct dwc_pcd *pcd)
{
	struct core_if *core_if = GET_CORE_IF(pcd);
	ulong global_regs = core_if->core_global_regs;
	u32 gintmask = 0;
	u32 grxsts;
	struct pcd_ep *ep;
	u32 gintsts;

	/* Disable the Rx Status Queue Level interrupt */
	gintmask |= DWC_INTMSK_RXFIFO_NOT_EMPT;
	dwc_reg_modify(global_regs, DWC_GINTMSK, gintmask, 0);

	/* Get the Status from the top of the FIFO */
	grxsts = dwc_reg_read(global_regs, DWC_GRXSTSP);

	/* Get pointer to EP structure */
	ep = get_out_ep(pcd, DWC_DM_RXSTS_CHAN_NUM_RD(grxsts));

	switch (DWC_DM_RXSTS_PKT_STS_RD(grxsts)) {
	case DWC_DSTS_GOUT_NAK:
		break;
	case DWC_STS_DATA_UPDT:
		if ((grxsts & DWC_DM_RXSTS_BYTE_CNT) && ep->dwc_ep.xfer_buff) {
			dwc_otg_read_packet(core_if, ep->dwc_ep.xfer_buff,
					    DWC_DM_RXSTS_BYTE_CNT_RD(grxsts));
			ep->dwc_ep.xfer_count +=
			    DWC_DM_RXSTS_BYTE_CNT_RD(grxsts);
			ep->dwc_ep.xfer_buff +=
			    DWC_DM_RXSTS_BYTE_CNT_RD(grxsts);
		}
		break;
	case DWC_STS_XFER_COMP:
		break;
	case DWC_DSTS_SETUP_COMP:
		break;
	case DWC_DSTS_SETUP_UPDT:
		dwc_otg_read_setup_packet(core_if, pcd->setup_pkt->d32);
		ep->dwc_ep.xfer_count += DWC_DM_RXSTS_BYTE_CNT_RD(grxsts);
		break;
	default:
		pr_err("RX_STS_Q Interrupt: Unknown status %d\n",
		       DWC_HM_RXSTS_PKT_STS_RD(grxsts));
		break;
	}

	/* Enable the Rx Status Queue Level interrupt */
	dwc_reg_modify(global_regs, DWC_GINTMSK, 0, gintmask);

	/* Clear interrupt */
	gintsts = 0;
	gintsts |= DWC_INTSTS_RXFIFO_NOT_EMPT;
	dwc_reg_write(global_regs, DWC_GINTSTS, gintsts);

	return 1;
}

/**
 * This function examines the Device IN Token Learning Queue to
 * determine the EP number of the last IN token received.  This
 * implementation is for the Mass Storage device where there are only
 * 2 IN EPs (Control-IN and BULK-IN).
 *
 * The EP numbers for the first six IN Tokens are in DTKNQR1 and there
 * are 8 EP Numbers in each of the other possible DTKNQ Registers.
 */
static int get_ep_of_last_in_token(struct core_if *core_if)
{
	ulong regs = core_if->dev_if->dev_global_regs;
	const u32 TOKEN_Q_DEPTH =
	    DWC_HWCFG2_DEV_TKN_Q_DEPTH_RD(core_if->hwcfg2);
	/* Number of Token Queue Registers */
	const int DTKNQ_REG_CNT = (TOKEN_Q_DEPTH + 7) / 8;
	u32 dtknqr1 = 0;
	u32 in_tkn_epnums[4];
	int ndx;
	u32 i;
	u32 addr = regs + DWC_DTKNQR1;
	int epnum = 0;

	/* Read the DTKNQ Registers */
	for (i = 0; i <= DTKNQ_REG_CNT; i++) {
		in_tkn_epnums[i] = dwc_reg_read(addr, 0);

		if (addr == (regs + DWC_DVBUSDIS))
			addr = regs + DWC_DTKNQR3_DTHRCTL;
		else
			++addr;
	}

	/* Copy the DTKNQR1 data to the bit field. */
	dtknqr1 = in_tkn_epnums[0];

	/* Get the EP numbers */
	in_tkn_epnums[0] = DWC_DTKNQR1_EP_TKN_NO_RD(dtknqr1);
	ndx = DWC_DTKNQR1_INT_TKN_Q_WR_PTR_RD(dtknqr1) - 1;

	if (ndx == -1) {
		/*
		 * Calculate the max queue position.
		 */
		int cnt = TOKEN_Q_DEPTH;

		if (TOKEN_Q_DEPTH <= 6)
			cnt = TOKEN_Q_DEPTH - 1;
		else if (TOKEN_Q_DEPTH <= 14)
			cnt = TOKEN_Q_DEPTH - 7;
		else if (TOKEN_Q_DEPTH <= 22)
			cnt = TOKEN_Q_DEPTH - 15;
		else
			cnt = TOKEN_Q_DEPTH - 23;

		epnum = (in_tkn_epnums[DTKNQ_REG_CNT - 1] >> (cnt * 4)) & 0xF;
	} else {
		if (ndx <= 5) {
			epnum = (in_tkn_epnums[0] >> (ndx * 4)) & 0xF;
		} else if (ndx <= 13) {
			ndx -= 6;
			epnum = (in_tkn_epnums[1] >> (ndx * 4)) & 0xF;
		} else if (ndx <= 21) {
			ndx -= 14;
			epnum = (in_tkn_epnums[2] >> (ndx * 4)) & 0xF;
		} else if (ndx <= 29) {
			ndx -= 22;
			epnum = (in_tkn_epnums[3] >> (ndx * 4)) & 0xF;
		}
	}

	return epnum;
}

static inline int count_dwords(struct pcd_ep *ep, u32 len)
{
	if (len > ep->dwc_ep.maxpacket)
		len = ep->dwc_ep.maxpacket;
	return (len + 3) / 4;
}

/**
 * This function writes a packet into the Tx FIFO associated with the EP.  For
 * non-periodic EPs the non-periodic Tx FIFO is written.  For periodic EPs the
 * periodic Tx FIFO associated with the EP is written with all packets for the
 * next micro-frame.
 *
 * The buffer is padded to DWORD on a per packet basis in
 * slave/dma mode if the MPS is not DWORD aligned.  The last packet, if
 * short, is also padded to a multiple of DWORD.
 *
 * ep->xfer_buff always starts DWORD aligned in memory and is a
 * multiple of DWORD in length
 *
 * ep->xfer_len can be any number of bytes
 *
 * ep->xfer_count is a multiple of ep->maxpacket until the last packet
 *
 * FIFO access is DWORD
 */
static void dwc_otg_ep_write_packet(struct core_if *core_if, struct dwc_ep *ep,
				    int dma)
{
	u32 i;
	u32 byte_count;
	u32 dword_count;
	u32 fifo;
	u32 *data_buff = (u32 *) ep->xfer_buff;

	if (ep->xfer_count >= ep->xfer_len)
		return;

	/* Find the byte length of the packet either short packet or MPS */
	if ((ep->xfer_len - ep->xfer_count) < ep->maxpacket)
		byte_count = ep->xfer_len - ep->xfer_count;
	else
		byte_count = ep->maxpacket;

	/*
	 * Find the DWORD length, padded by extra bytes as neccessary if MPS
	 * is not a multiple of DWORD
	 */
	dword_count = (byte_count + 3) / 4;

	fifo = core_if->data_fifo[ep->num];

	if (!dma)
		for (i = 0; i < dword_count; i++, data_buff++)
			dwc_write_fifo32(fifo, *data_buff);

	ep->xfer_count += byte_count;
	ep->xfer_buff += byte_count;
	ep->dma_addr += byte_count;
}

/**
 * This interrupt occurs when the non-periodic Tx FIFO is half-empty.
 * The active request is checked for the next packet to be loaded into
 * the non-periodic Tx FIFO.
 */
static int dwc_otg_pcd_handle_np_tx_fifo_empty_intr(struct dwc_pcd *pcd)
{
	struct core_if *core_if = GET_CORE_IF(pcd);
	ulong global_regs = core_if->core_global_regs;
	u32 txstatus = 0;
	u32 gintsts = 0;
	int epnum;
	struct pcd_ep *ep;
	u32 len;
	int dwords;

	/* Get the epnum from the IN Token Learning Queue. */
	epnum = get_ep_of_last_in_token(core_if);
	ep = get_in_ep(pcd, epnum);

	txstatus = dwc_reg_read(global_regs, DWC_GNPTXSTS);

	/*
	 * While there is space in the queue, space in the FIFO, and data to
	 * tranfer, write packets to the Tx FIFO
	 */
	len = ep->dwc_ep.xfer_len - ep->dwc_ep.xfer_count;
	dwords = count_dwords(ep, len);
	while ((DWC_GNPTXSTS_NPTXQSPCAVAIL_RD(txstatus) > 0 ||
                core_if->core_params->en_multiple_tx_fifo) &&
	       (DWC_GNPTXSTS_NPTXFSPCAVAIL_RD(txstatus) > dwords) &&
	       ep->dwc_ep.xfer_count < ep->dwc_ep.xfer_len) {
		/*
		 * Added-sr: 2007-07-26
		 *
		 * When a new transfer will be started, mark this
		 * endpoint as active. This way it will be blocked
		 * for further transfers, until the current transfer
		 * is finished.
		 */
		if (dwc_has_feature(core_if, DWC_LIMITED_XFER))
			ep->dwc_ep.active = 1;

		dwc_otg_ep_write_packet(core_if, &ep->dwc_ep, 0);
		len = ep->dwc_ep.xfer_len - ep->dwc_ep.xfer_count;
		dwords = count_dwords(ep, len);
		txstatus = dwc_reg_read(global_regs, DWC_GNPTXSTS);
	}

	/* Clear nptxfempty interrupt */
	gintsts |= DWC_INTMSK_RXFIFO_NOT_EMPT;
	dwc_reg_write(global_regs, DWC_GINTSTS, gintsts);

	/* Re-enable tx-fifo empty interrupt, if packets are stil pending */
	if (len)
		dwc_reg_modify(global_regs, DWC_GINTSTS, 0, gintsts);
	return 1;
}

/**
 * This function is called when dedicated Tx FIFO Empty interrupt occurs.
 * The active request is checked for the next packet to be loaded into
 * apropriate Tx FIFO.
 */
static int write_empty_tx_fifo(struct dwc_pcd *pcd, u32 epnum)
{
	struct core_if *core_if = GET_CORE_IF(pcd);
	ulong regs;
	u32 txstatus = 0;
	struct pcd_ep *ep;
	u32 len;
	int dwords;
	u32 diepint = 0;

	ep = get_in_ep(pcd, epnum);
	regs = core_if->dev_if->in_ep_regs[epnum];
	txstatus = dwc_reg_read(regs, DWC_DTXFSTS);

	/*
	 * While there is space in the queue, space in the FIFO and data to
	 * tranfer, write packets to the Tx FIFO
	 */
	len = ep->dwc_ep.xfer_len - ep->dwc_ep.xfer_count;
	dwords = count_dwords(ep, len);
	while (DWC_DTXFSTS_TXFSSPC_AVAI_RD(txstatus) > dwords
	       && ep->dwc_ep.xfer_count < ep->dwc_ep.xfer_len
	       && ep->dwc_ep.xfer_len != 0) {
		dwc_otg_ep_write_packet(core_if, &ep->dwc_ep, 0);
		len = ep->dwc_ep.xfer_len - ep->dwc_ep.xfer_count;
		dwords = count_dwords(ep, len);
		txstatus = dwc_reg_read(regs, DWC_DTXFSTS);
	}
	/* Clear emptyintr */
	diepint = DWC_DIEPINT_TXFIFO_EMPTY_RW(diepint, 1);
	dwc_reg_write(in_ep_int_reg(pcd, epnum), 0, diepint);
	return 1;
}

/**
 * This function is called when the Device is disconnected.  It stops any active
 * requests and informs the Gadget driver of the disconnect.
 */
void dwc_otg_pcd_stop(struct dwc_pcd *pcd)
{
	int i, num_in_eps, num_out_eps;
	struct pcd_ep *ep;
	u32 intr_mask = 0;
	ulong global_regs = GET_CORE_IF(pcd)->core_global_regs;

	num_in_eps = GET_CORE_IF(pcd)->dev_if->num_in_eps;
	num_out_eps = GET_CORE_IF(pcd)->dev_if->num_out_eps;

	/* Don't disconnect drivers more than once */
	if (pcd->ep0state == EP0_DISCONNECT)
		return;
	pcd->ep0state = EP0_DISCONNECT;

	/* Reset the OTG state. */
	dwc_otg_pcd_update_otg(pcd, 1);

	/* Disable the NP Tx Fifo Empty Interrupt. */
	intr_mask |= DWC_INTMSK_NP_TXFIFO_EMPT;
	dwc_reg_modify(global_regs, DWC_GINTMSK, intr_mask, 0);

	/* Flush the FIFOs */
	dwc_otg_flush_tx_fifo(GET_CORE_IF(pcd), 0);
	dwc_otg_flush_rx_fifo(GET_CORE_IF(pcd));

	/* Prevent new request submissions, kill any outstanding requests  */
	ep = &pcd->ep0;
	request_nuke(ep);

	/* Prevent new request submissions, kill any outstanding requests  */
	for (i = 0; i < num_in_eps; i++)
		request_nuke((struct pcd_ep *)&pcd->in_ep[i]);

	/* Prevent new request submissions, kill any outstanding requests  */
	for (i = 0; i < num_out_eps; i++)
		request_nuke((struct pcd_ep *)&pcd->out_ep[i]);

	/* Report disconnect; the driver is already quiesced */
	if (pcd->driver && pcd->driver->disconnect) {
		spin_unlock(&pcd->lock);
		pcd->driver->disconnect(&pcd->gadget);
		spin_lock(&pcd->lock);
	}
}

/**
 * This interrupt indicates that ...
 */
static int dwc_otg_pcd_handle_i2c_intr(struct dwc_pcd *pcd)
{
	u32 intr_mask = 0;
	u32 gintsts;

	pr_info("Interrupt handler not implemented for i2cintr\n");

	/* Turn off and clean the interrupt */
	intr_mask |= DWC_INTMSK_I2C_INTR;
	dwc_reg_modify((GET_CORE_IF(pcd)->core_global_regs), DWC_GINTMSK,
		     intr_mask, 0);

	gintsts = 0;
	gintsts |= DWC_INTSTS_I2C_INTR;
	dwc_reg_write((GET_CORE_IF(pcd)->core_global_regs), DWC_GINTSTS,
		    gintsts);

	return 1;
}

/**
 * This interrupt indicates that ...
 */
static int dwc_otg_pcd_handle_early_suspend_intr(struct dwc_pcd *pcd)
{
	u32 intr_mask = 0;
	u32 gintsts;

	pr_info("Early Suspend Detected\n");

	/* Turn off and clean the interrupt */
	intr_mask |= DWC_INTMSK_EARLY_SUSP;
	dwc_reg_modify((GET_CORE_IF(pcd)->core_global_regs), DWC_GINTMSK,
		     intr_mask, 0);

	gintsts = 0;
	gintsts |= DWC_INTSTS_EARLY_SUSP;
	dwc_reg_write((GET_CORE_IF(pcd)->core_global_regs), DWC_GINTSTS,
		    gintsts);

	return 1;
}

/**
 * This function configures EPO to receive SETUP packets.
 *
 * Program the following fields in the endpoint specific registers for Control
 * OUT EP 0, in order to receive a setup packet:
 *
 * - DOEPTSIZ0.Packet Count = 3 (To receive up to 3 back to back setup packets)
 *
 * - DOEPTSIZE0.Transfer Size = 24 Bytes (To receive up to 3 back to back setup
 *   packets)
 *
 * In DMA mode, DOEPDMA0 Register with a memory address to store any setup
 * packets received
 */
static void ep0_out_start(struct core_if *core_if, struct dwc_pcd *pcd)
{
	struct device_if *dev_if = core_if->dev_if;
	u32 doeptsize0 = 0;

	doeptsize0 = DWC_DEPTSIZ0_SUPCNT_RW(doeptsize0, 3);
	doeptsize0 = DWC_DEPTSIZ0_PKT_CNT_RW(doeptsize0, 1);
	doeptsize0 = DWC_DEPTSIZ0_XFER_SIZ_RW(doeptsize0, 8 * 3);
	dwc_reg_write(dev_if->out_ep_regs[0], DWC_DOEPTSIZ, doeptsize0);

	if (core_if->dma_enable) {
		u32 doepctl = 0;

		dwc_reg_write(dev_if->out_ep_regs[0], DWC_DOEPDMA,
			    pcd->setup_pkt_dma_handle);
		doepctl = DWC_DEPCTL_EPENA_RW(doepctl, 1);
		doepctl = DWC_DEPCTL_ACT_EP_RW(doepctl, 1);
		dwc_reg_write(out_ep_ctl_reg(pcd, 0), 0, doepctl);
	}
}

/**
 * This interrupt occurs when a USB Reset is detected.  When the USB Reset
 * Interrupt occurs the device state is set to DEFAULT and the EP0 state is set
 * to IDLE.
 *
 * Set the NAK bit for all OUT endpoints (DOEPCTLn.SNAK = 1)
 *
 * Unmask the following interrupt bits:
 *  - DAINTMSK.INEP0 = 1 (Control 0 IN endpoint)
 *  - DAINTMSK.OUTEP0 = 1 (Control 0 OUT endpoint)
 *  - DOEPMSK.SETUP = 1
 *  - DOEPMSK.XferCompl = 1
 *  - DIEPMSK.XferCompl = 1
 *  - DIEPMSK.TimeOut = 1
 *
 * Program the following fields in the endpoint specific registers for Control
 * OUT EP 0, in order to receive a setup packet
 *  - DOEPTSIZ0.Packet Count = 3 (To receive up to 3 back to back setup packets)
 *  - DOEPTSIZE0.Transfer Size = 24 Bytes (To receive up to 3 back to back setup
 *    packets)
 *
 *  - In DMA mode, DOEPDMA0 Register with a memory address to store any setup
 *    packets received
 *
 * At this point, all the required initialization, except for enabling
 * the control 0 OUT endpoint is done, for receiving SETUP packets.
 *
 * Note that the bits in the Device IN endpoint mask register (diepmsk) are laid
 * out exactly the same as the Device IN endpoint interrupt register (diepint.)
 * Likewise for Device OUT endpoint mask / interrupt registers (doepmsk /
 * doepint.)
 */
static int dwc_otg_pcd_handle_usb_reset_intr(struct dwc_pcd *pcd)
{
	struct core_if *core_if = GET_CORE_IF(pcd);
	struct device_if *dev_if = core_if->dev_if;
	u32 doepctl = 0;
	u32 daintmsk = 0;
	u32 doepmsk = 0;
	u32 diepmsk = 0;
	u32 dcfg = 0;
	u32 resetctl = 0;
	u32 dctl = 0;
	u32 i;
	u32 gintsts = 0;

	pr_info("USB RESET\n");

	/* reset the HNP settings */
	dwc_otg_pcd_update_otg(pcd, 1);

	/* Clear the Remote Wakeup Signalling */
	dctl = DEC_DCTL_REMOTE_WAKEUP_SIG(dctl, 1);
	dwc_reg_modify(dev_ctl_reg(pcd), 0, dctl, 0);

	/* Set NAK for all OUT EPs */
	doepctl = DWC_DEPCTL_SET_NAK_RW(doepctl, 1);
	for (i = 0; i <= dev_if->num_out_eps; i++)
		dwc_reg_write(out_ep_ctl_reg(pcd, i), 0, doepctl);

	/* Flush the NP Tx FIFO */
	dwc_otg_flush_tx_fifo(core_if, 0);

	/* Flush the Learning Queue */
	resetctl |= DWC_RSTCTL_TKN_QUE_FLUSH;
	dwc_reg_write(core_if->core_global_regs, DWC_GRSTCTL, resetctl);

	daintmsk |= DWC_DAINT_INEP00;
	daintmsk |= DWC_DAINT_OUTEP00;
	dwc_reg_write(dev_if->dev_global_regs, DWC_DAINTMSK, daintmsk);

	doepmsk = DWC_DOEPMSK_SETUP_DONE_RW(doepmsk, 1);
	doepmsk = DWC_DOEPMSK_AHB_ERROR_RW(doepmsk, 1);
	doepmsk = DWC_DOEPMSK_EP_DISA_RW(doepmsk, 1);
	doepmsk = DWC_DOEPMSK_TX_COMPL_RW(doepmsk, 1);
	dwc_reg_write(dev_if->dev_global_regs, DWC_DOEPMSK, doepmsk);

	diepmsk = DWC_DIEPMSK_TX_CMPL_RW(diepmsk, 1);
	diepmsk = DWC_DIEPMSK_TOUT_COND_RW(diepmsk, 1);
	diepmsk = DWC_DIEPMSK_EP_DISA_RW(diepmsk, 1);
	diepmsk = DWC_DIEPMSK_AHB_ERROR_RW(diepmsk, 1);
	diepmsk = DWC_DIEPMSK_IN_TKN_TX_EMPTY_RW(diepmsk, 1);
	dwc_reg_write(dev_if->dev_global_regs, DWC_DIEPMSK, diepmsk);

	/* Reset Device Address */
	dcfg = dwc_reg_read(dev_if->dev_global_regs, DWC_DCFG);
	dcfg = DWC_DCFG_DEV_ADDR_WR(dcfg, 0);
	dwc_reg_write(dev_if->dev_global_regs, DWC_DCFG, dcfg);

	/* setup EP0 to receive SETUP packets */
	ep0_out_start(core_if, pcd);

	/* Clear interrupt */
	gintsts = 0;
	gintsts |= DWC_INTSTS_USB_RST;
	dwc_reg_write((core_if->core_global_regs), DWC_GINTSTS, gintsts);

	return 1;
}

/**
 * Get the device speed from the device status register and convert it
 * to USB speed constant.
 */
static int get_device_speed(struct dwc_pcd *pcd)
{
	u32 dsts = 0;
	enum usb_device_speed speed = USB_SPEED_UNKNOWN;

	dsts = dwc_reg_read(dev_sts_reg(pcd), 0);

	switch (DWC_DSTS_ENUM_SPEED_RD(dsts)) {
	case DWC_DSTS_ENUMSPD_HS_PHY_30MHZ_OR_60MHZ:
		speed = USB_SPEED_HIGH;
		break;
	case DWC_DSTS_ENUMSPD_FS_PHY_30MHZ_OR_60MHZ:
	case DWC_DSTS_ENUMSPD_FS_PHY_48MHZ:
		speed = USB_SPEED_FULL;
		break;
	case DWC_DSTS_ENUMSPD_LS_PHY_6MHZ:
		speed = USB_SPEED_LOW;
		break;
	}
	return speed;
}

/**
 * This function enables EP0 OUT to receive SETUP packets and configures EP0
 * IN for transmitting packets.  It is normally called when the "Enumeration
 * Done" interrupt occurs.
 */
static void dwc_otg_ep0_activate(struct core_if *core_if, struct dwc_ep *ep)
{
	struct device_if *dev_if = core_if->dev_if;
	u32 dsts;
	u32 diepctl = 0;
	u32 doepctl = 0;
	u32 dctl = 0;

	/* Read the Device Status and Endpoint 0 Control registers */
	dsts = dwc_reg_read(dev_if->dev_global_regs, DWC_DSTS);
	diepctl = dwc_reg_read(dev_if->in_ep_regs[0], DWC_DIEPCTL);
	doepctl = dwc_reg_read(dev_if->out_ep_regs[0], DWC_DOEPCTL);

	/* Set the MPS of the IN EP based on the enumeration speed */
	switch (DWC_DSTS_ENUM_SPEED_RD(dsts)) {
	case DWC_DSTS_ENUMSPD_HS_PHY_30MHZ_OR_60MHZ:
	case DWC_DSTS_ENUMSPD_FS_PHY_30MHZ_OR_60MHZ:
	case DWC_DSTS_ENUMSPD_FS_PHY_48MHZ:
		diepctl = DWC_DEPCTL_MPS_RW(diepctl, DWC_DEP0CTL_MPS_64);
		break;
	case DWC_DSTS_ENUMSPD_LS_PHY_6MHZ:
		diepctl = DWC_DEPCTL_MPS_RW(diepctl, DWC_DEP0CTL_MPS_8);
		break;
	}
	dwc_reg_write(dev_if->in_ep_regs[0], DWC_DIEPCTL, diepctl);

	/* Enable OUT EP for receive */
	doepctl = DWC_DEPCTL_EPENA_RW(doepctl, 1);
	dwc_reg_write(dev_if->out_ep_regs[0], DWC_DOEPCTL, doepctl);

	dctl = DWC_DCTL_CLR_CLBL_NP_IN_NAK(dctl, 1);
	dwc_reg_modify(dev_if->dev_global_regs, DWC_DCTL, dctl, dctl);
}

/**
 * Read the device status register and set the device speed in the
 * data structure.
 * Set up EP0 to receive SETUP packets by calling dwc_ep0_activate.
 */
static int dwc_otg_pcd_handle_enum_done_intr(struct dwc_pcd *pcd)
{
	struct pcd_ep *ep0 = &pcd->ep0;
	u32 gintsts;
	u32 gusbcfg;
	struct core_if *core_if = GET_CORE_IF(pcd);
	ulong global_regs = core_if->core_global_regs;
	u32 gsnpsid = global_regs + DWC_GSNPSID;
	u8 utmi16b, utmi8b;

	if (gsnpsid >= (u32) 0x4f54260a) {
		utmi16b = 5;
		utmi8b = 9;
	} else {
		utmi16b = 4;
		utmi8b = 8;
	}
	dwc_otg_ep0_activate(GET_CORE_IF(pcd), &ep0->dwc_ep);

	pcd->ep0state = EP0_IDLE;
	ep0->stopped = 0;
	pcd->gadget.speed = get_device_speed(pcd);

	gusbcfg = dwc_reg_read(global_regs, DWC_GUSBCFG);

	/* Set USB turnaround time based on device speed and PHY interface. */
	if (pcd->gadget.speed == USB_SPEED_HIGH) {
		switch (DWC_HWCFG2_HS_PHY_TYPE_RD(core_if->hwcfg2)) {
		case DWC_HWCFG2_HS_PHY_TYPE_ULPI:
			gusbcfg =
			    (gusbcfg & (~((u32) DWC_USBCFG_TRN_TIME(0xf)))) |
			    DWC_USBCFG_TRN_TIME(9);
			break;
		case DWC_HWCFG2_HS_PHY_TYPE_UTMI:
			if (DWC_HWCFG4_UTMI_PHY_DATA_WIDTH_RD(core_if->hwcfg4)
			    == 0)
				gusbcfg =
				    (gusbcfg &
				     (~((u32) DWC_USBCFG_TRN_TIME(0xf)))) |
				    DWC_USBCFG_TRN_TIME(utmi8b);
			else if (DWC_HWCFG4_UTMI_PHY_DATA_WIDTH_RD
				 (core_if->hwcfg4) == 1)
				gusbcfg =
				    (gusbcfg &
				     (~((u32) DWC_USBCFG_TRN_TIME(0xf)))) |
				    DWC_USBCFG_TRN_TIME(utmi16b);
			else if (core_if->core_params->phy_utmi_width == 8)
				gusbcfg =
				    (gusbcfg &
				     (~((u32) DWC_USBCFG_TRN_TIME(0xf)))) |
				    DWC_USBCFG_TRN_TIME(utmi8b);
			else
				gusbcfg =
				    (gusbcfg &
				     (~((u32) DWC_USBCFG_TRN_TIME(0xf)))) |
				    DWC_USBCFG_TRN_TIME(utmi16b);
			break;
		case DWC_HWCFG2_HS_PHY_TYPE_UTMI_ULPI:
			if (gusbcfg & DWC_USBCFG_ULPI_UTMI_SEL) {
				gusbcfg =
				    (gusbcfg &
				     (~((u32) DWC_USBCFG_TRN_TIME(0xf)))) |
				    DWC_USBCFG_TRN_TIME(9);
			} else {
				if (core_if->core_params->phy_utmi_width == 16)
					gusbcfg =
					    (gusbcfg &
					     (~
					      ((u32) DWC_USBCFG_TRN_TIME(0xf))))
					    | DWC_USBCFG_TRN_TIME(utmi16b);
				else
					gusbcfg =
					    (gusbcfg &
					     (~
					      ((u32) DWC_USBCFG_TRN_TIME(0xf))))
					    | DWC_USBCFG_TRN_TIME(utmi8b);
			}
			break;
		}
	} else {
		/* Full or low speed */
		gusbcfg = (gusbcfg & (~((u32) DWC_USBCFG_TRN_TIME(0xf)))) |
		    DWC_USBCFG_TRN_TIME(9);
	}
	dwc_reg_write(global_regs, DWC_GUSBCFG, gusbcfg);

	/* Clear interrupt */
	gintsts = 0;
	gintsts |= DWC_INTSTS_ENUM_DONE;
	dwc_reg_write((GET_CORE_IF(pcd)->core_global_regs), DWC_GINTSTS,
		    gintsts);

	return 1;
}

/**
 * This interrupt indicates that the ISO OUT Packet was dropped due to
 * Rx FIFO full or Rx Status Queue Full.  If this interrupt occurs
 * read all the data from the Rx FIFO.
 */
static int dwc_otg_pcd_handle_isoc_out_packet_dropped_intr(struct dwc_pcd *pcd)
{
	u32 intr_mask = 0;
	u32 gintsts;

	pr_info("Interrupt Handler not implemented for ISOC Out " "Dropped\n");

	/* Turn off and clear the interrupt */
	intr_mask |= DWC_INTMSK_ISYNC_OUTPKT_DRP;
	dwc_reg_modify((GET_CORE_IF(pcd)->core_global_regs), DWC_GINTMSK,
		     intr_mask, 0);

	gintsts = 0;
	gintsts |= DWC_INTSTS_ISYNC_OUTPKT_DRP;
	dwc_reg_write((GET_CORE_IF(pcd)->core_global_regs), DWC_GINTSTS,
		    gintsts);

	return 1;
}

/**
 * This interrupt indicates the end of the portion of the micro-frame
 * for periodic transactions.  If there is a periodic transaction for
 * the next frame, load the packets into the EP periodic Tx FIFO.
 */
static int dwc_otg_pcd_handle_end_periodic_frame_intr(struct dwc_pcd *pcd)
{
	u32 intr_mask = 0;
	u32 gintsts;

	pr_info("Interrupt handler not implemented for End of "
		"Periodic Portion of Micro-Frame Interrupt");

	/* Turn off and clear the interrupt */
	intr_mask |= DWC_INTMSK_END_OF_PFRM;
	dwc_reg_modify((GET_CORE_IF(pcd)->core_global_regs), DWC_GINTMSK,
		     intr_mask, 0);

	gintsts = 0;
	gintsts |= DWC_INTSTS_END_OF_PFRM;
	dwc_reg_write((GET_CORE_IF(pcd)->core_global_regs), DWC_GINTSTS,
		    gintsts);

	return 1;
}

/**
 * This interrupt indicates that EP of the packet on the top of the
 * non-periodic Tx FIFO does not match EP of the IN Token received.
 *
 * The "Device IN Token Queue" Registers are read to determine the
 * order the IN Tokens have been received.  The non-periodic Tx FIFO is flushed,
 * so it can be reloaded in the order seen in the IN Token Queue.
 */
static int dwc_otg_pcd_handle_ep_mismatch_intr(struct core_if *core_if)
{
	u32 intr_mask = 0;
	u32 gintsts;

	pr_info("Interrupt handler not implemented for End Point "
		"Mismatch\n");

	/* Turn off and clear the interrupt */
	intr_mask |= DWC_INTMSK_ENDP_MIS_MTCH;
	dwc_reg_modify((core_if->core_global_regs), DWC_GINTMSK,
		     intr_mask, 0);

	gintsts = 0;
	gintsts |= DWC_INTSTS_ENDP_MIS_MTCH;
	dwc_reg_write((core_if->core_global_regs), DWC_GINTSTS, gintsts);
	return 1;
}

/**
 * This funcion stalls EP0.
 */
static void ep0_do_stall(struct dwc_pcd *pcd, const int val)
{
	struct pcd_ep *ep0 = &pcd->ep0;
	struct usb_ctrlrequest *ctrl = &pcd->setup_pkt->req;

	pr_warning("req %02x.%02x protocol STALL; err %d\n",
		   ctrl->bRequestType, ctrl->bRequest, val);

	ep0->dwc_ep.is_in = 1;
	dwc_otg_ep_set_stall(pcd->otg_dev->core_if, &ep0->dwc_ep);

	pcd->ep0.stopped = 1;
	pcd->ep0state = EP0_IDLE;
	ep0_out_start(GET_CORE_IF(pcd), pcd);
}

/**
 * This functions delegates the setup command to the gadget driver.
 */
static void do_gadget_setup(struct dwc_pcd *pcd, struct usb_ctrlrequest *ctrl)
{
	if (pcd->driver && pcd->driver->setup) {
		int ret;

		spin_unlock(&pcd->lock);
		ret = pcd->driver->setup(&pcd->gadget, ctrl);
		spin_lock(&pcd->lock);

		if (ret < 0)
			ep0_do_stall(pcd, ret);

		/** This is a g_file_storage gadget driver specific
		 * workaround: a DELAYED_STATUS result from the fsg_setup
		 * routine will result in the gadget queueing a EP0 IN status
		 * phase for a two-stage control transfer.
		 *
		 * Exactly the same as a SET_CONFIGURATION/SET_INTERFACE except
		 * that this is a class specific request.  Need a generic way to
		 * know when the gadget driver will queue the status phase.
		 *
		 * Can we assume when we call the gadget driver setup() function
		 * that it will always queue and require the following flag?
		 * Need to look into this.
		 */
		if (ret == 256 + 999)
			pcd->request_config = 1;
	}
}

/**
 * This function starts the Zero-Length Packet for the IN status phase
 * of a 2 stage control transfer.
 */
static void do_setup_in_status_phase(struct dwc_pcd *pcd)
{
	struct pcd_ep *ep0 = &pcd->ep0;

	if (pcd->ep0state == EP0_STALL)
		return;

	pcd->ep0state = EP0_STATUS;

	ep0->dwc_ep.xfer_len = 0;
	ep0->dwc_ep.xfer_count = 0;
	ep0->dwc_ep.is_in = 1;
	ep0->dwc_ep.dma_addr = pcd->setup_pkt_dma_handle;
	dwc_otg_ep0_start_transfer(GET_CORE_IF(pcd), &ep0->dwc_ep);

	/* Prepare for more SETUP Packets */
	ep0_out_start(GET_CORE_IF(pcd), pcd);
}

/**
 * This function starts the Zero-Length Packet for the OUT status phase
 * of a 2 stage control transfer.
 */
static void do_setup_out_status_phase(struct dwc_pcd *pcd)
{
	struct pcd_ep *ep0 = &pcd->ep0;

	if (pcd->ep0state == EP0_STALL)
		return;
	pcd->ep0state = EP0_STATUS;

	ep0->dwc_ep.xfer_len = 0;
	ep0->dwc_ep.xfer_count = 0;
	ep0->dwc_ep.is_in = 0;
	ep0->dwc_ep.dma_addr = pcd->setup_pkt_dma_handle;
	dwc_otg_ep0_start_transfer(GET_CORE_IF(pcd), &ep0->dwc_ep);

	/* Prepare for more SETUP Packets */
	ep0_out_start(GET_CORE_IF(pcd), pcd);
}

/**
 * Clear the EP halt (STALL) and if pending requests start the
 * transfer.
 */
static void pcd_clear_halt(struct dwc_pcd *pcd, struct pcd_ep *ep)
{
	struct core_if *core_if = GET_CORE_IF(pcd);

	if (!ep->dwc_ep.stall_clear_flag)
		dwc_otg_ep_clear_stall(core_if, &ep->dwc_ep);

	/* Reactive the EP */
	dwc_otg_ep_activate(core_if, &ep->dwc_ep);

	if (ep->stopped) {
		ep->stopped = 0;
		/* If there is a request in the EP queue start it */

		/*
		 * dwc_start_next_request(), outside of interpt contxt at some
		 * time after the current time, after a clear-halt setup packet.
		 * Still need to implement ep mismatch in the future if a gadget
		 * ever uses more than one endpoint at once
		 */
		ep->queue_sof = 1;
		tasklet_schedule(pcd->start_xfer_tasklet);
	}

	/* Start Control Status Phase */
	do_setup_in_status_phase(pcd);
}

/**
 * This function is called when the SET_FEATURE TEST_MODE Setup packet is sent
 * from the host.  The Device Control register is written with the Test Mode
 * bits set to the specified Test Mode.  This is done as a tasklet so that the
 * "Status" phase of the control transfer completes before transmitting the TEST
 * packets.
 *
 */
static void do_test_mode(unsigned long data)
{
	u32 dctl = 0;
	struct dwc_pcd *pcd = (struct dwc_pcd *)data;
	int test_mode = pcd->test_mode;

	dctl = dwc_reg_read(dev_ctl_reg(pcd), 0);
	switch (test_mode) {
	case 1:		/* TEST_J */
		dctl = DWC_DCTL_TST_CTL(dctl, 1);
		break;
	case 2:		/* TEST_K */
		dctl = DWC_DCTL_TST_CTL(dctl, 2);
		break;
	case 3:		/* TEST_SE0_NAK */
		dctl = DWC_DCTL_TST_CTL(dctl, 3);
		break;
	case 4:		/* TEST_PACKET */
		dctl = DWC_DCTL_TST_CTL(dctl, 4);
		break;
	case 5:		/* TEST_FORCE_ENABLE */
		dctl = DWC_DCTL_TST_CTL(dctl, 5);
		break;
	}
	dwc_reg_write(dev_ctl_reg(pcd), 0, dctl);
}

/**
 * This function process the SET_FEATURE Setup Commands.
 */
static void do_set_feature(struct dwc_pcd *pcd)
{
	struct core_if *core_if = GET_CORE_IF(pcd);
	ulong regs = core_if->core_global_regs;
	struct usb_ctrlrequest ctrl = pcd->setup_pkt->req;
	u32 gotgctl = 0;

	switch (ctrl.bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		switch (__le16_to_cpu(ctrl.wValue)) {
		case USB_DEVICE_REMOTE_WAKEUP:
			pcd->remote_wakeup_enable = 1;
			break;
		case USB_DEVICE_TEST_MODE:
			/*
			 * Setup the Test Mode tasklet to do the Test
			 * Packet generation after the SETUP Status
			 * phase has completed.
			 */

			pcd->test_mode_tasklet.next = NULL;
			pcd->test_mode_tasklet.state = 0;
			atomic_set(&pcd->test_mode_tasklet.count, 0);

			pcd->test_mode_tasklet.func = do_test_mode;
			pcd->test_mode_tasklet.data = (unsigned long)pcd;
			pcd->test_mode = __le16_to_cpu(ctrl.wIndex) >> 8;
			tasklet_schedule(&pcd->test_mode_tasklet);

			break;
		case USB_DEVICE_B_HNP_ENABLE:
			/* dev may initiate HNP */
			pcd->b_hnp_enable = 1;
			dwc_otg_pcd_update_otg(pcd, 0);
			/*
			 * gotgctl.devhnpen cleared by a
			 * USB Reset?
			 */
			gotgctl |= DWC_GCTL_DEV_HNP_ENA;
			gotgctl |= DWC_GCTL_HNP_REQ;
			dwc_reg_write(regs, DWC_GOTGCTL, gotgctl);
			break;
		case USB_DEVICE_A_HNP_SUPPORT:
			/* RH port supports HNP */
			pcd->a_hnp_support = 1;
			dwc_otg_pcd_update_otg(pcd, 0);
			break;
		case USB_DEVICE_A_ALT_HNP_SUPPORT:
			/* other RH port does */
			pcd->a_alt_hnp_support = 1;
			dwc_otg_pcd_update_otg(pcd, 0);
			break;
		}
		do_setup_in_status_phase(pcd);
		break;
	case USB_RECIP_INTERFACE:
		do_gadget_setup(pcd, &ctrl);
		break;
	case USB_RECIP_ENDPOINT:
		if (__le16_to_cpu(ctrl.wValue) == USB_ENDPOINT_HALT) {
			struct pcd_ep *ep;

			ep = get_ep_by_addr(pcd, __le16_to_cpu(ctrl.wIndex));

			if (ep == NULL) {
				ep0_do_stall(pcd, -EOPNOTSUPP);
				return;
			}

			ep->stopped = 1;
			dwc_otg_ep_set_stall(core_if, &ep->dwc_ep);
		}
		do_setup_in_status_phase(pcd);
		break;
	}
}

/**
 * This function process the CLEAR_FEATURE Setup Commands.
 */
static void do_clear_feature(struct dwc_pcd *pcd)
{
	struct usb_ctrlrequest ctrl = pcd->setup_pkt->req;
	struct pcd_ep *ep;

	switch (ctrl.bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		switch (__le16_to_cpu(ctrl.wValue)) {
		case USB_DEVICE_REMOTE_WAKEUP:
			pcd->remote_wakeup_enable = 0;
			break;
		case USB_DEVICE_TEST_MODE:
			/* Add CLEAR_FEATURE for TEST modes. */
			break;
		}
		do_setup_in_status_phase(pcd);
		break;
	case USB_RECIP_ENDPOINT:
		ep = get_ep_by_addr(pcd, __le16_to_cpu(ctrl.wIndex));
		if (ep == NULL) {
			ep0_do_stall(pcd, -EOPNOTSUPP);
			return;
		}

		pcd_clear_halt(pcd, ep);
		break;
	}
}

/**
 * This function processes SETUP commands.  In Linux, the USB Command processing
 * is done in two places - the first being the PCD and the second in the Gadget
 * Driver (for example, the File-Backed Storage Gadget Driver).
 *
 * GET_STATUS: Command is processed as defined in chapter 9 of the USB 2.0
 * Specification chapter 9
 *
 * CLEAR_FEATURE: The Device and Endpoint requests are the ENDPOINT_HALT feature
 * is procesed, all others the interface requests are ignored.
 *
 * SET_FEATURE: The Device and Endpoint requests are processed by the PCD.
 * Interface requests are passed to the Gadget Driver.
 *
 * SET_ADDRESS: PCD, Program the DCFG reg, with device address received
 *
 * GET_DESCRIPTOR: Gadget Driver, Return the requested descriptor
 *
 * SET_DESCRIPTOR: Gadget Driver, Optional - not implemented by any of the
 * existing Gadget Drivers.
 *
 * SET_CONFIGURATION: Gadget Driver, Disable all EPs and enable EPs for new
 * configuration.
 *
 * GET_CONFIGURATION: Gadget Driver, Return the current configuration
 *
 * SET_INTERFACE: Gadget Driver, Disable all EPs and enable EPs for new
 * configuration.
 *
 * GET_INTERFACE: Gadget Driver, Return the current interface.
 *
 * SYNC_FRAME:  Display debug message.
 *
 * When the SETUP Phase Done interrupt occurs, the PCD SETUP commands are
 * processed by pcd_setup. Calling the Function Driver's setup function from
 * pcd_setup processes the gadget SETUP commands.
 */
static void pcd_setup(struct dwc_pcd *pcd)
{
	struct core_if *core_if = GET_CORE_IF(pcd);
	struct device_if *dev_if = core_if->dev_if;
	struct usb_ctrlrequest ctrl = pcd->setup_pkt->req;
	struct pcd_ep *ep;
	struct pcd_ep *ep0 = &pcd->ep0;
	u16 *status = pcd->status_buf;
	u32 doeptsize0 = 0;

	doeptsize0 = dwc_reg_read(dev_if->out_ep_regs[0], DWC_DOEPTSIZ);

	/* handle > 1 setup packet , assert error for now */
	if (core_if->dma_enable && (DWC_DEPTSIZ0_SUPCNT_RD(doeptsize0) < 2))
		pr_err("\n\n	 CANNOT handle > 1 setup packet in "
		       "DMA mode\n\n");

	/* Clean up the request queue */
	request_nuke(ep0);
	ep0->stopped = 0;

	if (ctrl.bRequestType & USB_DIR_IN) {
		ep0->dwc_ep.is_in = 1;
		pcd->ep0state = EP0_IN_DATA_PHASE;
	} else {
		ep0->dwc_ep.is_in = 0;
		pcd->ep0state = EP0_OUT_DATA_PHASE;
	}

	if(ctrl.wLength == 0) {
		ep0->dwc_ep.is_in = 1;
		pcd->ep0state = EP0_STATUS;
	}

	if ((ctrl.bRequestType & USB_TYPE_MASK) != USB_TYPE_STANDARD) {
		/*
		 * Handle non-standard (class/vendor) requests in the gadget
		 * driver
		 */
		do_gadget_setup(pcd, &ctrl);
		return;
	}

	switch (ctrl.bRequest) {
	case USB_REQ_GET_STATUS:
		switch (ctrl.bRequestType & USB_RECIP_MASK) {
		case USB_RECIP_DEVICE:
			*status = 0x1;	/* Self powered */
			*status |= pcd->remote_wakeup_enable << 1;
			break;
		case USB_RECIP_INTERFACE:
			*status = 0;
			break;
		case USB_RECIP_ENDPOINT:
			ep = get_ep_by_addr(pcd, __le16_to_cpu(ctrl.wIndex));
			if (ep == NULL || __le16_to_cpu(ctrl.wLength) > 2) {
				ep0_do_stall(pcd, -EOPNOTSUPP);
				return;
			}
			*status = ep->stopped;
			break;
		}

		*status = __cpu_to_le16(*status);

		pcd->ep0_pending = 1;
		ep0->dwc_ep.start_xfer_buff = (u8 *) status;
		ep0->dwc_ep.xfer_buff = (u8 *) status;
		ep0->dwc_ep.dma_addr = pcd->status_buf_dma_handle;
		ep0->dwc_ep.xfer_len = 2;
		ep0->dwc_ep.xfer_count = 0;
		ep0->dwc_ep.total_len = ep0->dwc_ep.xfer_len;
		dwc_otg_ep0_start_transfer(GET_CORE_IF(pcd), &ep0->dwc_ep);
		break;
	case USB_REQ_CLEAR_FEATURE:
		do_clear_feature(pcd);
		break;
	case USB_REQ_SET_FEATURE:
		do_set_feature(pcd);
		break;
	case USB_REQ_SET_ADDRESS:
		if (ctrl.bRequestType == USB_RECIP_DEVICE) {
			u32 dcfg = 0;

			dcfg = DWC_DCFG_DEV_ADDR_WR(dcfg,
						    __le16_to_cpu(ctrl.wValue));
			dwc_reg_modify(dev_if->dev_global_regs, DWC_DCFG,
				     0, dcfg);
			do_setup_in_status_phase(pcd);
			return;
		}
		break;
	case USB_REQ_SET_INTERFACE:
	case USB_REQ_SET_CONFIGURATION:
		pcd->request_config = 1;	/* Configuration changed */
		do_gadget_setup(pcd, &ctrl);
		break;
	case USB_REQ_SYNCH_FRAME:
		do_gadget_setup(pcd, &ctrl);
		break;
	default:
		/* Call the Gadget Driver's setup functions */
		do_gadget_setup(pcd, &ctrl);
		break;
	}
}

/**
 * This function completes the ep0 control transfer.
 */
static int ep0_complete_request(struct pcd_ep *ep)
{
	struct core_if *core_if = GET_CORE_IF(ep->pcd);
	struct device_if *dev_if = core_if->dev_if;
	ulong in_regs = dev_if->in_ep_regs[ep->dwc_ep.num];
	u32 deptsiz = 0;
	struct pcd_request *req;
	int is_last = 0;
	struct dwc_pcd *pcd = ep->pcd;

	if (pcd->ep0_pending && list_empty(&ep->queue)) {
		if (ep->dwc_ep.is_in)
			do_setup_out_status_phase(pcd);
		else
			do_setup_in_status_phase(pcd);

		pcd->ep0_pending = 0;
		pcd->ep0state = EP0_STATUS;
		return 1;
	}

	if (list_empty(&ep->queue))
		return 0;

	req = list_entry(ep->queue.next, struct pcd_request, queue);

	if (core_if->dma_enable && !DWC_DEPTSIZ_XFER_SIZ_RD(deptsiz))
		ep->dwc_ep.xfer_count = ep->dwc_ep.xfer_len;

	if(!core_if->dma_enable) {
		u32 glbl_regs = (u32) core_if->core_global_regs;
		u32 intr_mask = 0;
		intr_mask |= DWC_INTMSK_NP_TXFIFO_EMPT;
		dwc_reg_modify(glbl_regs, DWC_GINTMSK, intr_mask,
			0);
	}

	if (pcd->ep0state == EP0_STATUS) {
		is_last = 1;
	} else if (ep->dwc_ep.is_in) {
		deptsiz = dwc_reg_read(in_regs, DWC_DIEPTSIZ);

		if (DWC_DEPTSIZ0_XFER_SIZ_RD(deptsiz) == 0) {
			req->req.actual = ep->dwc_ep.xfer_count;
			do_setup_out_status_phase(pcd);
		}
	} else {
		/* This is ep0-OUT */
		req->req.actual = ep->dwc_ep.xfer_count;
		do_setup_in_status_phase(pcd);
	}

	/* Complete the request */
	if (is_last) {
		request_done(ep, req, 0);
		ep->dwc_ep.start_xfer_buff = NULL;
		ep->dwc_ep.xfer_buff = NULL;
		ep->dwc_ep.xfer_len = 0;
		return 1;
	}
	return 0;
}

/**
 * This function completes the request for the EP.  If there are additional
 * requests for the EP in the queue they will be started.
 */
static void complete_ep(struct pcd_ep *ep)
{
	struct core_if *core_if = GET_CORE_IF(ep->pcd);
	struct device_if *dev_if = core_if->dev_if;
	ulong in_ep_regs = dev_if->in_ep_regs[ep->dwc_ep.num];
	u32 deptsiz = 0;
	struct pcd_request *req = NULL;
	int is_last = 0;

	/* Get any pending requests */
	if (!list_empty(&ep->queue))
		req = list_entry(ep->queue.next, struct pcd_request, queue);

	if(req == 0) {
		pr_warning("dwc_otg NULL request completed\n");
		mutex_unlock(&ep->dwc_ep.xfer_mutex);
		return;
	}

	if (ep->dwc_ep.is_in) {
		deptsiz = dwc_reg_read(in_ep_regs, DWC_DIEPTSIZ);

		if (core_if->dma_enable && !DWC_DEPTSIZ_XFER_SIZ_RD(deptsiz))
			ep->dwc_ep.xfer_count = ep->dwc_ep.xfer_len;

		if (DWC_DEPTSIZ_XFER_SIZ_RD(deptsiz) == 0 &&
		    DWC_DEPTSIZ_PKT_CNT_RD(deptsiz) == 0 &&
		    ep->dwc_ep.xfer_count == ep->dwc_ep.xfer_len)
			is_last = 1;
		else
			pr_warning("Incomplete transfer (%s-%s "
				   "[siz=%d pkt=%d])\n", ep->ep.name,
				   ep->dwc_ep.is_in ? "IN" : "OUT",
				   DWC_DEPTSIZ_XFER_SIZ_RD(deptsiz),
				   DWC_DEPTSIZ_PKT_CNT_RD(deptsiz));
	} else {
		ulong out_ep_regs = dev_if->out_ep_regs[ep->dwc_ep.num];

		deptsiz = dwc_reg_read(out_ep_regs, DWC_DOEPTSIZ);
		is_last = 1;
	}

	/* Complete the request */
	if (is_last) {
		/*
		 * Added-sr: 2007-07-26
		 *
		 * Since the 405EZ (Ultra) only support 2047 bytes as
		 * max transfer size, we have to split up bigger transfers
		 * into multiple transfers of 1024 bytes sized messages.
		 * I happens often, that transfers of 4096 bytes are
		 * required (zero-gadget, file_storage-gadget).
		 */
		if ((dwc_has_feature(core_if, DWC_LIMITED_XFER)) &&
		    ep->dwc_ep.bytes_pending) {
			ulong in_regs =
			    core_if->dev_if->in_ep_regs[ep->dwc_ep.num];
			u32 intr_mask = 0;

			ep->dwc_ep.xfer_len = ep->dwc_ep.bytes_pending;
			if (ep->dwc_ep.xfer_len > MAX_XFER_LEN) {
				ep->dwc_ep.bytes_pending = ep->dwc_ep.xfer_len -
				    MAX_XFER_LEN;
				ep->dwc_ep.xfer_len = MAX_XFER_LEN;
			} else {
				ep->dwc_ep.bytes_pending = 0;
			}

			/*
			 * Restart the current transfer with the next "chunk"
			 * of data.
			 */
			ep->dwc_ep.xfer_count = 0;

			deptsiz = dwc_reg_read(in_regs, DWC_DIEPTSIZ);
			deptsiz =
			    DWC_DEPTSIZ_XFER_SIZ_RW(deptsiz,
						    ep->dwc_ep.xfer_len);
			deptsiz =
			    DWC_DEPTSIZ_PKT_CNT_RW(deptsiz,
						   ((ep->dwc_ep.xfer_len - 1 +
						     ep->dwc_ep.maxpacket) /
						    ep->dwc_ep.maxpacket));
			dwc_reg_write(in_regs, DWC_DIEPTSIZ, deptsiz);

			intr_mask |= DWC_INTSTS_NP_TXFIFO_EMPT;
			dwc_reg_modify((core_if->core_global_regs),
				     DWC_GINTSTS, intr_mask, 0);
			dwc_reg_modify((core_if->core_global_regs),
				     DWC_GINTMSK, intr_mask, intr_mask);

			/*
			 * Just return here if message was not completely
			 * transferred.
			 */
			return;
		}
		if (core_if->dma_enable)
			req->req.actual = ep->dwc_ep.xfer_len -
			    DWC_DEPTSIZ_XFER_SIZ_RD(deptsiz);
		else
			req->req.actual = ep->dwc_ep.xfer_count;

		request_done(ep, req, 0);
		ep->dwc_ep.start_xfer_buff = NULL;
		ep->dwc_ep.xfer_buff = NULL;
		ep->dwc_ep.xfer_len = 0;
		if(mutex_is_locked(&ep->dwc_ep.xfer_mutex)) {
			mutex_unlock(&ep->dwc_ep.xfer_mutex);
		}

		/* If there is a request in the queue start it. */
		dwc_start_next_request(ep);
	}
}

/**
 * This function continues control IN transfers started by
 * dwc_otg_ep0_start_transfer, when the transfer does not fit in a
 * single packet.  NOTE: The DIEPCTL0/DOEPCTL0 registers only have one
 * bit for the packet count.
 */
static void dwc_otg_ep0_continue_transfer(struct core_if *c_if,
					  struct dwc_ep *ep)
{
	if (ep->is_in) {
		u32 depctl = 0;
		u32 deptsiz = 0;
		struct device_if *d_if = c_if->dev_if;
		ulong in_regs = d_if->in_ep_regs[0];
		u32 tx_status = 0;
		ulong glbl_regs = c_if->core_global_regs;

		tx_status = dwc_reg_read(glbl_regs, DWC_GNPTXSTS);

		depctl = dwc_reg_read(in_regs, DWC_DIEPCTL);
		deptsiz = dwc_reg_read(in_regs, DWC_DIEPTSIZ);

		/*
		 * Program the transfer size and packet count as follows:
		 *   xfersize = N * maxpacket + short_packet
		 *   pktcnt = N + (short_packet exist ? 1 : 0)
		 */
		if (ep->total_len - ep->xfer_count > ep->maxpacket)
			deptsiz = DWC_DEPTSIZ0_XFER_SIZ_RW(deptsiz,
							   ep->maxpacket);
		else
			deptsiz = DWC_DEPTSIZ0_XFER_SIZ_RW(deptsiz,
							   (ep->total_len -
							    ep->xfer_count));

		deptsiz = DWC_DEPTSIZ0_PKT_CNT_RW(deptsiz, 1);
		ep->xfer_len += DWC_DEPTSIZ0_XFER_SIZ_RD(deptsiz);
		dwc_reg_write(in_regs, DWC_DIEPTSIZ, deptsiz);

		/* Write the DMA register */
		if (DWC_HWCFG2_ARCH_RD(c_if->hwcfg2) == DWC_INT_DMA_ARCH)
			dwc_reg_write(in_regs, DWC_DIEPDMA, ep->dma_addr);

		/* EP enable, IN data in FIFO */
		depctl = DWC_DEPCTL_CLR_NAK_RW(depctl, 1);
		depctl = DWC_DEPCTL_EPENA_RW(depctl, 1);
		dwc_reg_write(in_regs, DWC_DIEPCTL, depctl);

		/*
		 * Enable the Non-Periodic Tx FIFO empty interrupt, the
		 * data will be written into the fifo by the ISR.
		 */
		if (!c_if->dma_enable) {
			u32 intr_mask = 0;

			/* First clear it from GINTSTS */
			intr_mask |= DWC_INTMSK_NP_TXFIFO_EMPT;
			dwc_reg_write(glbl_regs, DWC_GINTSTS, intr_mask);

			/* To avoid spurious NPTxFEmp intr */
			dwc_reg_modify(glbl_regs, DWC_GINTMSK, intr_mask,
				     intr_mask);
		}
	}
}

/**
 * This function handles EP0 Control transfers.
 *
 * The state of the control tranfers are tracked in ep0state
 */
static void handle_ep0(struct dwc_pcd *pcd)
{
	struct core_if *core_if = GET_CORE_IF(pcd);
	struct pcd_ep *ep0 = &pcd->ep0;

	switch (pcd->ep0state) {
	case EP0_DISCONNECT:
		break;
	case EP0_IDLE:
		pcd->request_config = 0;
		pcd_setup(pcd);
		break;
	case EP0_IN_DATA_PHASE:
		if (core_if->dma_enable)
			/*
			 * For EP0 we can only program 1 packet at a time so we
			 * need to do the calculations after each complete.
			 * Call write_packet to make the calculations, as in
			 * slave mode, and use those values to determine if we
			 * can complete.
			 */
			dwc_otg_ep_write_packet(core_if, &ep0->dwc_ep, 1);
		else
			dwc_otg_ep_write_packet(core_if, &ep0->dwc_ep, 0);

		if (ep0->dwc_ep.xfer_count < ep0->dwc_ep.total_len)
			dwc_otg_ep0_continue_transfer(core_if, &ep0->dwc_ep);
		else
			ep0_complete_request(ep0);
		break;
	case EP0_OUT_DATA_PHASE:
		ep0_complete_request(ep0);
		break;
	case EP0_STATUS:
		ep0_complete_request(ep0);
		pcd->ep0state = EP0_IDLE;
		ep0->stopped = 1;
		ep0->dwc_ep.is_in = 0;	/* OUT for next SETUP */

		/* Prepare for more SETUP Packets */
		if (core_if->dma_enable) {
			ep0_out_start(core_if, pcd);
		} else {
			int i;
			u32 diepctl = 0;

			diepctl = dwc_reg_read(in_ep_ctl_reg(pcd, 0), 0);
			if (pcd->ep0.queue_sof) {
				pcd->ep0.queue_sof = 0;
				dwc_start_next_request(&pcd->ep0);
			}

			diepctl = dwc_reg_read(in_ep_ctl_reg(pcd, 0), 0);
			if (pcd->ep0.queue_sof) {
				pcd->ep0.queue_sof = 0;
				dwc_start_next_request(&pcd->ep0);
			}

			for (i = 0; i < core_if->dev_if->num_in_eps; i++) {
				diepctl = dwc_reg_read(in_ep_ctl_reg(pcd, i),
							0);

				if (pcd->in_ep[i].queue_sof) {
					pcd->in_ep[i].queue_sof = 0;
					dwc_start_next_request(&pcd->in_ep[i]);
				}
			}
		}
		break;
	case EP0_STALL:
		pr_err("EP0 STALLed, should not get here handle_ep0()\n");
		break;
	}
}

/**
 * Restart transfer
 */
static void restart_transfer(struct dwc_pcd *pcd, const u32 ep_num)
{
	struct core_if *core_if = GET_CORE_IF(pcd);
	struct device_if *dev_if = core_if->dev_if;
	u32 dieptsiz = 0;
	struct pcd_ep *ep;

	dieptsiz = dwc_reg_read(dev_if->in_ep_regs[ep_num], DWC_DIEPTSIZ);
	ep = get_in_ep(pcd, ep_num);

	/*
	 * If pktcnt is not 0, and xfersize is 0, and there is a buffer,
	 * resend the last packet.
	 */
	mutex_lock(&ep->dwc_ep.xfer_mutex);
	if (DWC_DEPTSIZ_PKT_CNT_RD(dieptsiz) &&
	    !DWC_DEPTSIZ_XFER_SIZ_RD(dieptsiz) && ep->dwc_ep.start_xfer_buff) {
		if (ep->dwc_ep.xfer_len <= ep->dwc_ep.maxpacket) {
			ep->dwc_ep.xfer_count = 0;
			ep->dwc_ep.xfer_buff = ep->dwc_ep.start_xfer_buff;
		} else {
			ep->dwc_ep.xfer_count -= ep->dwc_ep.maxpacket;

			/* convert packet size to dwords. */
			ep->dwc_ep.xfer_buff -= ep->dwc_ep.maxpacket;
		}
		ep->stopped = 0;

		if (!ep_num)
			dwc_otg_ep0_start_transfer(core_if, &ep->dwc_ep);
		else
			dwc_otg_ep_start_transfer(core_if, &ep->dwc_ep);
	}
}

/**
 * Handle the IN EP Transfer Complete interrupt.
 *
 * If dedicated fifos are enabled, then the Tx FIFO empty interrupt for the EP
 * is disabled.  Otherwise the NP Tx FIFO empty interrupt is  disabled.
 */
static void handle_in_ep_xfr_complete_intr(struct dwc_pcd *pcd,
					   struct pcd_ep *ep, u32 num)
{
	struct core_if *c_if = GET_CORE_IF(pcd);
	struct device_if *d_if = c_if->dev_if;
	struct dwc_ep *dwc_ep = &ep->dwc_ep;
	u32 diepint = 0;

	if (c_if->en_multiple_tx_fifo) {
		u32 fifoemptymsk = 0x1 << dwc_ep->num;
		dwc_reg_modify(d_if->dev_global_regs,
			     DWC_DTKNQR4FIFOEMPTYMSK, fifoemptymsk, 0);
	} else {
		u32 intr_mask = 0;

		intr_mask |= DWC_INTMSK_NP_TXFIFO_EMPT;
		dwc_reg_modify((c_if->core_global_regs), DWC_GINTMSK,
			     intr_mask, 0);
	}

	/* Clear the interrupt, then complete the transfer */
	diepint = DWC_DIEPINT_TX_CMPL_RW(diepint, 1);
	dwc_reg_write(d_if->in_ep_regs[num], DWC_DIEPINT, diepint);

	if (!num)
		handle_ep0(pcd);
	else
		complete_ep(ep);
}

/**
 * Handle the IN EP disable interrupt.
 */
static void handle_in_ep_disable_intr(struct dwc_pcd *pcd, const u32 ep_num)
{
	struct core_if *core_if = GET_CORE_IF(pcd);
	struct device_if *dev_if = core_if->dev_if;
	u32 dieptsiz = 0;
	u32 dctl = 0;
	struct pcd_ep *ep;
	struct dwc_ep *dwc_ep;
	u32 diepint = 0;

	ep = get_in_ep(pcd, ep_num);
	dwc_ep = &ep->dwc_ep;

	dieptsiz = dwc_reg_read(dev_if->in_ep_regs[ep_num], DWC_DIEPTSIZ);

	if (ep->stopped) {
		/* Flush the Tx FIFO */
		dwc_otg_flush_tx_fifo(core_if, dwc_ep->tx_fifo_num);

		/* Clear the Global IN NP NAK */
		dctl = 0;
		dctl = DWC_DCTL_CLR_CLBL_NP_IN_NAK(dctl, 1);
		dwc_reg_modify(dev_ctl_reg(pcd), 0, dctl, 0);

		if (DWC_DEPTSIZ_PKT_CNT_RD(dieptsiz) ||
		    DWC_DEPTSIZ_XFER_SIZ_RD(dieptsiz))
			restart_transfer(pcd, ep_num);
	} else {
		if (DWC_DEPTSIZ_PKT_CNT_RD(dieptsiz) ||
		    DWC_DEPTSIZ_XFER_SIZ_RD(dieptsiz))
			restart_transfer(pcd, ep_num);
	}
	/* Clear epdisabled */
	diepint = DWC_DIEPINT_EP_DISA_RW(diepint, 1);
	dwc_reg_write(in_ep_int_reg(pcd, ep_num), 0, diepint);

}

/**
 * Handler for the IN EP timeout handshake interrupt.
 */
static void handle_in_ep_timeout_intr(struct dwc_pcd *pcd, const u32 ep_num)
{
	struct core_if *core_if = GET_CORE_IF(pcd);
	struct pcd_ep *ep;
	u32 dctl = 0;
	u32 intr_mask = 0;
	u32 diepint = 0;

	ep = get_in_ep(pcd, ep_num);

	/* Disable the NP Tx Fifo Empty Interrrupt */
	if (!core_if->dma_enable) {
		intr_mask |= DWC_INTMSK_NP_TXFIFO_EMPT;
		dwc_reg_modify((core_if->core_global_regs), DWC_GINTMSK,
			     intr_mask, 0);
	}

	/* Non-periodic EP */
	/* Enable the Global IN NAK Effective Interrupt */
	intr_mask |= DWC_INTMSK_GLBL_IN_NAK;
	dwc_reg_modify((core_if->core_global_regs), DWC_GINTMSK, 0,
		     intr_mask);

	/* Set Global IN NAK */
	dctl = DWC_DCTL_CLR_CLBL_NP_IN_NAK(dctl, 1);
	dwc_reg_modify(dev_ctl_reg(pcd), 0, dctl, dctl);
	ep->stopped = 1;

	/* Clear timeout */
	diepint = DWC_DIEPINT_TOUT_COND_RW(diepint, 1);
	dwc_reg_write(in_ep_int_reg(pcd, ep_num), 0, diepint);
}

/**
 * Handles the IN Token received with TxF Empty interrupt.
 *
 * For the 405EZ, only start the next transfer, when currently no other transfer
 * is active on this endpoint.
 *
 * Note that the bits in the Device IN endpoint mask register are laid out
 * exactly the same as the Device IN endpoint interrupt register.
 */
static void handle_in_ep_tx_fifo_empty_intr(struct dwc_pcd *pcd,
					    struct pcd_ep *ep, u32 num)
{
	u32 diepint = 0;

	if (!ep->stopped && num) {
		u32 diepmsk = 0;

		diepmsk = DWC_DIEPMSK_IN_TKN_TX_EMPTY_RW(diepmsk, 1);
		dwc_reg_modify(dev_diepmsk_reg(pcd), 0, diepmsk, 0);

		if (dwc_has_feature(GET_CORE_IF(pcd), DWC_LIMITED_XFER)) {
			if (!ep->dwc_ep.active)
				dwc_start_next_request(ep);
		} else {
			dwc_start_next_request(ep);
		}
	}
	/* Clear intktxfemp */
	diepint = DWC_DIEPMSK_IN_TKN_TX_EMPTY_RW(diepint, 1);
	dwc_reg_write(in_ep_int_reg(pcd, num), 0, diepint);
}

static void handle_in_ep_nak_effective_intr(struct dwc_pcd *pcd,
					    struct pcd_ep *ep, u32 num)
{
	u32 diepctl = 0;
	u32 diepint = 0;

	/* Periodic EP */
	if (ep->disabling) {
		diepctl = 0;
		diepctl = DWC_DEPCTL_SET_NAK_RW(diepctl, 1);
		diepctl = DWC_DEPCTL_DPID_RW(diepctl, 1);
		dwc_reg_modify(in_ep_ctl_reg(pcd, num), 0, diepctl, diepctl);
	}
	/* Clear inepnakeff */
	diepint = DWC_DIEPINT_IN_EP_NAK_RW(diepint, 1);
	dwc_reg_write(in_ep_int_reg(pcd, num), 0, diepint);

}

/**
 * This function returns the Device IN EP Interrupt register
 */
static inline u32 dwc_otg_read_diep_intr(struct core_if *core_if,
					 struct dwc_ep *ep)
{
	struct device_if *dev_if = core_if->dev_if;
	u32 v, msk, emp;

	msk = dwc_reg_read(dev_if->dev_global_regs, DWC_DIEPMSK);
	emp =
	    dwc_reg_read(dev_if->dev_global_regs, DWC_DTKNQR4FIFOEMPTYMSK);
	msk |= ((emp >> ep->num) & 0x1) << 7;
	v = dwc_reg_read(dev_if->in_ep_regs[ep->num], DWC_DIEPINT) & msk;
	return v;
}

/**
 * This function reads the Device All Endpoints Interrupt register and
 * returns the IN endpoint interrupt bits.
 */
static inline u32 dwc_otg_read_dev_all_in_ep_intr(struct core_if *_if)
{
	u32 v;

	v = dwc_reg_read(_if->dev_if->dev_global_regs, DWC_DAINT) &
	    dwc_reg_read(_if->dev_if->dev_global_regs, DWC_DAINTMSK);
	return v & 0xffff;
}

/**
 * This interrupt indicates that an IN EP has a pending Interrupt.
 * The sequence for handling the IN EP interrupt is shown below:
 *
 * - Read the Device All Endpoint Interrupt register
 * - Repeat the following for each IN EP interrupt bit set (from LSB to MSB).
 *
 * - Read the Device Endpoint Interrupt (DIEPINTn) register
 * - If "Transfer Complete" call the request complete function
 * - If "Endpoint Disabled" complete the EP disable procedure.
 * - If "AHB Error Interrupt" log error
 * - If "Time-out Handshake" log error
 * - If "IN Token Received when TxFIFO Empty" write packet to Tx FIFO.
 * - If "IN Token EP Mismatch" (disable, this is handled by EP Mismatch
 *   Interrupt)
 */
static int dwc_otg_pcd_handle_in_ep_intr(struct dwc_pcd *pcd)
{
	struct core_if *core_if = GET_CORE_IF(pcd);
	u32 diepint = 0;
	u32 ep_intr;
	u32 epnum = 0;
	struct pcd_ep *ep;
	struct dwc_ep *dwc_ep;

	/* Read in the device interrupt bits */
	ep_intr = dwc_otg_read_dev_all_in_ep_intr(core_if);

	/* Service the Device IN interrupts for each endpoint */
	while (ep_intr) {
		if (ep_intr & 0x1) {
			u32 c_diepint;

			/* Get EP pointer */
			ep = get_in_ep(pcd, epnum);
			dwc_ep = &ep->dwc_ep;

			diepint = dwc_otg_read_diep_intr(core_if, dwc_ep);

			/* Transfer complete */
			if (DWC_DIEPINT_TX_CMPL_RD(diepint))
				handle_in_ep_xfr_complete_intr(pcd, ep, epnum);

			/* Endpoint disable */
			if (DWC_DIEPINT_EP_DISA_RD(diepint))
				handle_in_ep_disable_intr(pcd, epnum);

			/* AHB Error */
			if (DWC_DIEPINT_AHB_ERROR_RD(diepint)) {
				/* Clear ahberr */
				c_diepint = 0;
				c_diepint =
				    DWC_DIEPINT_AHB_ERROR_RW(c_diepint, 1);
				dwc_reg_write(in_ep_int_reg(pcd, epnum), 0,
					    c_diepint);
			}

			/* TimeOUT Handshake (non-ISOC IN EPs) */
			if (DWC_DIEPINT_TOUT_COND_RD(diepint))
				handle_in_ep_timeout_intr(pcd, epnum);

			/* IN Token received with TxF Empty */
			if (DWC_DIEPINT_IN_TKN_TX_EMPTY_RD(diepint))
				handle_in_ep_tx_fifo_empty_intr(pcd, ep, epnum);

			/* IN Token Received with EP mismatch */
			if (DWC_DIEPINT_IN_TKN_EP_MISS_RD(diepint)) {
				/* Clear intknepmis */
				c_diepint = 0;
				c_diepint =
				    DWC_DIEPINT_IN_TKN_EP_MISS_RW(c_diepint, 1);
				dwc_reg_write(in_ep_int_reg(pcd, epnum), 0,
					    c_diepint);
			}

			/* IN Endpoint NAK Effective */
			if (DWC_DIEPINT_IN_EP_NAK_RD(diepint))
				handle_in_ep_nak_effective_intr(pcd, ep, epnum);

			/* IN EP Tx FIFO Empty Intr */
			if (DWC_DIEPINT_TXFIFO_EMPTY_RD(diepint))
				write_empty_tx_fifo(pcd, epnum);
		}
		epnum++;
		ep_intr >>= 1;
	}
	return 1;
}

/**
 * This function reads the Device All Endpoints Interrupt register and
 * returns the OUT endpoint interrupt bits.
 */
static inline u32 dwc_otg_read_dev_all_out_ep_intr(struct core_if *_if)
{
	u32 v;

	v = dwc_reg_read(_if->dev_if->dev_global_regs, DWC_DAINT) &
	    dwc_reg_read(_if->dev_if->dev_global_regs, DWC_DAINTMSK);
	return (v & 0xffff0000) >> 16;
}

/**
 * This function returns the Device OUT EP Interrupt register
 */
static inline u32 dwc_otg_read_doep_intr(struct core_if *core_if,
					 struct dwc_ep *ep)
{
	struct device_if *dev_if = core_if->dev_if;
	u32 v;

	v = dwc_reg_read(dev_if->out_ep_regs[ep->num], DWC_DOEPINT) &
	    dwc_reg_read(dev_if->dev_global_regs, DWC_DOEPMSK);
	return v;
}

/**
 * This interrupt indicates that an OUT EP has a pending Interrupt.
 * The sequence for handling the OUT EP interrupt is shown below:
 *
 * - Read the Device All Endpoint Interrupt register.
 * - Repeat the following for each OUT EP interrupt bit set (from LSB to MSB).
 *
 * - Read the Device Endpoint Interrupt (DOEPINTn) register
 * - If "Transfer Complete" call the request complete function
 * - If "Endpoint Disabled" complete the EP disable procedure.
 * - If "AHB Error Interrupt" log error
 * - If "Setup Phase Done" process Setup Packet (See Standard USB Command
 *   Processing)
 */
static int dwc_otg_pcd_handle_out_ep_intr(struct dwc_pcd *pcd)
{
	struct core_if *core_if = GET_CORE_IF(pcd);
	u32 ep_intr;
	u32 doepint = 0;
	u32 epnum = 0;
	struct dwc_ep *dwc_ep;

	/* Read in the device interrupt bits */
	ep_intr = dwc_otg_read_dev_all_out_ep_intr(core_if);
	while (ep_intr) {
		if (ep_intr & 0x1) {
			u32 c_doepint = 0;

			dwc_ep = &((get_out_ep(pcd, epnum))->dwc_ep);
			doepint = dwc_otg_read_doep_intr(core_if, dwc_ep);

			/* Transfer complete */
			if (DWC_DOEPINT_TX_COMPL_RD(doepint)) {
				/* Clear xfercompl */
				c_doepint = 0;
				c_doepint =
				    DWC_DOEPMSK_TX_COMPL_RW(c_doepint, 1);
				dwc_reg_write(out_ep_int_reg(pcd, epnum), 0,
					    c_doepint);
				if (epnum == 0)
					handle_ep0(pcd);
				else
					complete_ep(get_out_ep(pcd, epnum));
			}

			/* Endpoint disable */
			if (DWC_DOEPINT_EP_DISA_RD(doepint)) {
				/* Clear epdisabled */
				c_doepint = 0;
				c_doepint =
				    DWC_DOEPMSK_EP_DISA_RW(c_doepint, 1);
				dwc_reg_write(out_ep_int_reg(pcd, epnum), 0,
					    c_doepint);
			}

			/* AHB Error */
			if (DWC_DOEPINT_AHB_ERROR_RD(doepint)) {
				c_doepint = 0;
				c_doepint =
				    DWC_DOEPMSK_AHB_ERROR_RW(c_doepint, 1);
				dwc_reg_write(out_ep_int_reg(pcd, epnum), 0,
					    c_doepint);
			}

			/* Setup Phase Done (control EPs) */
			if (DWC_DOEPINT_SETUP_DONE_RD(doepint)) {
				c_doepint = 0;
				c_doepint =
				    DWC_DOEPMSK_SETUP_DONE_RW(c_doepint, 1);
				dwc_reg_write(out_ep_int_reg(pcd, epnum), 0,
					    c_doepint);
				handle_ep0(pcd);
			}
		}
		epnum++;
		ep_intr >>= 1;
	}
	return 1;
}

/**
 * Incomplete ISO IN Transfer Interrupt.  This interrupt indicates one of the
 * following conditions occurred while transmitting an ISOC transaction.
 *
 * - Corrupted IN Token for ISOC EP.
 * - Packet not complete in FIFO.
 *
 * The follow actions should be taken:
 * - Determine the EP
 * - Set incomplete flag in dwc_ep structure
 *  - Disable EP.  When "Endpoint Disabled" interrupt is received Flush FIFO
 */
static int dwc_otg_pcd_handle_incomplete_isoc_in_intr(struct dwc_pcd *pcd)
{
	u32 intr_mask = 0;
	u32 gintsts = 0;

	pr_info("Interrupt handler not implemented for IN ISOC "
		"Incomplete\n");

	/* Turn off and clear the interrupt */
	intr_mask |= DWC_INTMSK_INCMP_IN_ATX;
	dwc_reg_modify((GET_CORE_IF(pcd)->core_global_regs), DWC_GINTMSK,
		     intr_mask, 0);

	gintsts |= DWC_INTSTS_INCMP_IN_ATX;
	dwc_reg_write((GET_CORE_IF(pcd)->core_global_regs), DWC_GINTSTS,
		    gintsts);
	return 1;
}

/**
 * Incomplete ISO OUT Transfer Interrupt.  This interrupt indicates that the
 * core has dropped an ISO OUT packet.  The following conditions can be the
 * cause:
 *
 * - FIFO Full, the entire packet would not fit in the FIFO.
 * - CRC Error
 * - Corrupted Token
 *
 * The follow actions should be taken:
 * - Determine the EP
 * - Set incomplete flag in dwc_ep structure
 * - Read any data from the FIFO
 * - Disable EP.  When "Endpoint Disabled" interrupt is received re-enable EP.
 */
static int dwc_otg_pcd_handle_incomplete_isoc_out_intr(struct dwc_pcd *pcd)
{
	u32 intr_mask = 0;
	u32 gintsts = 0;

	pr_info("Interrupt handler not implemented for OUT ISOC "
		"Incomplete\n");

	/* Turn off and clear the interrupt */
	intr_mask |= DWC_INTMSK_INCMP_OUT_PTX;
	dwc_reg_modify((GET_CORE_IF(pcd)->core_global_regs), DWC_GINTMSK,
		     intr_mask, 0);

	gintsts |= DWC_INTSTS_INCMP_OUT_PTX;
	dwc_reg_write((GET_CORE_IF(pcd)->core_global_regs), DWC_GINTSTS,
		    gintsts);
	return 1;
}

/**
 * This function handles the Global IN NAK Effective interrupt.
 */
static int dwc_otg_pcd_handle_in_nak_effective(struct dwc_pcd *pcd)
{
	struct device_if *dev_if = GET_CORE_IF(pcd)->dev_if;
	u32 diepctl = 0;
	u32 diepctl_rd = 0;
	u32 intr_mask = 0;
	u32 gintsts = 0;
	u32 i;

	/* Disable all active IN EPs */
	diepctl = DWC_DEPCTL_DPID_RW(diepctl, 1);
	diepctl = DWC_DEPCTL_SET_NAK_RW(diepctl, 1);
	for (i = 0; i <= dev_if->num_in_eps; i++) {
		diepctl_rd = dwc_reg_read(in_ep_ctl_reg(pcd, i), 0);
		if (DWC_DEPCTL_EPENA_RD(diepctl_rd))
			dwc_reg_write(in_ep_ctl_reg(pcd, i), 0, diepctl);
	}

	/* Disable the Global IN NAK Effective Interrupt */
	intr_mask |= DWC_INTMSK_GLBL_IN_NAK;
	dwc_reg_modify((GET_CORE_IF(pcd)->core_global_regs), DWC_GINTMSK,
		     intr_mask, 0);

	/* Clear interrupt */
	gintsts |= DWC_INTSTS_GLBL_IN_NAK;
	dwc_reg_write((GET_CORE_IF(pcd)->core_global_regs), DWC_GINTSTS,
		    gintsts);
	return 1;
}

/**
 * This function handles the Global OUT NAK Effective interrupt.
 */
static int dwc_otg_pcd_handle_out_nak_effective(struct dwc_pcd *pcd)
{
	u32 intr_mask = 0;
	u32 gintsts = 0;

	pr_info("Interrupt handler not implemented for Global IN "
		"NAK Effective\n");

	/* Turn off and clear the interrupt */
	intr_mask |= DWC_INTMSK_GLBL_OUT_NAK;
	dwc_reg_modify((GET_CORE_IF(pcd)->core_global_regs), DWC_GINTMSK,
		     intr_mask, 0);

	/* Clear goutnakeff */
	gintsts |= DWC_INTSTS_GLBL_OUT_NAK;
	dwc_reg_write((GET_CORE_IF(pcd)->core_global_regs), DWC_GINTSTS,
		    gintsts);
	return 1;
}

/**
 * PCD interrupt handler.
 *
 * The PCD handles the device interrupts.  Many conditions can cause a
 * device interrupt. When an interrupt occurs, the device interrupt
 * service routine determines the cause of the interrupt and
 * dispatches handling to the appropriate function. These interrupt
 * handling functions are described below.
 *
 * All interrupt registers are processed from LSB to MSB.
 *
 */
int dwc_otg_pcd_handle_intr(struct dwc_pcd *pcd)
{
	struct core_if *core_if = GET_CORE_IF(pcd);
	u32 gintr_status;
	int ret = 0;

	if (dwc_otg_is_device_mode(core_if)) {
		spin_lock(&pcd->lock);

		gintr_status = dwc_otg_read_core_intr(core_if);
		if (!gintr_status) {
			spin_unlock(&pcd->lock);
			return 0;
		}

		if (gintr_status & DWC_INTSTS_STRT_OF_FRM)
			ret |= dwc_otg_pcd_handle_sof_intr(pcd);
		if (gintr_status & DWC_INTSTS_RXFIFO_NOT_EMPT)
			ret |= dwc_otg_pcd_handle_rx_status_q_level_intr(pcd);
		if (gintr_status & DWC_INTSTS_NP_TXFIFO_EMPT)
			ret |= dwc_otg_pcd_handle_np_tx_fifo_empty_intr(pcd);
		if (gintr_status & DWC_INTSTS_GLBL_IN_NAK)
			ret |= dwc_otg_pcd_handle_in_nak_effective(pcd);
		if (gintr_status & DWC_INTSTS_GLBL_OUT_NAK)
			ret |= dwc_otg_pcd_handle_out_nak_effective(pcd);
		if (gintr_status & DWC_INTSTS_I2C_INTR)
			ret |= dwc_otg_pcd_handle_i2c_intr(pcd);
		if (gintr_status & DWC_INTSTS_EARLY_SUSP)
			ret |= dwc_otg_pcd_handle_early_suspend_intr(pcd);
		if (gintr_status & DWC_INTSTS_USB_RST)
			ret |= dwc_otg_pcd_handle_usb_reset_intr(pcd);
		if (gintr_status & DWC_INTSTS_ENUM_DONE)
			ret |= dwc_otg_pcd_handle_enum_done_intr(pcd);
		if (gintr_status & DWC_INTSTS_ISYNC_OUTPKT_DRP)
			ret |=
			    dwc_otg_pcd_handle_isoc_out_packet_dropped_intr
			    (pcd);
		if (gintr_status & DWC_INTSTS_END_OF_PFRM)
			ret |= dwc_otg_pcd_handle_end_periodic_frame_intr(pcd);
		if (gintr_status & DWC_INTSTS_ENDP_MIS_MTCH)
			ret |= dwc_otg_pcd_handle_ep_mismatch_intr(core_if);
		if (gintr_status & DWC_INTSTS_IN_ENDP)
			ret |= dwc_otg_pcd_handle_in_ep_intr(pcd);
		if (gintr_status & DWC_INTSTS_OUT_ENDP)
			ret |= dwc_otg_pcd_handle_out_ep_intr(pcd);
		if (gintr_status & DWC_INTSTS_INCMP_IN_ATX)
			ret |= dwc_otg_pcd_handle_incomplete_isoc_in_intr(pcd);
		if (gintr_status & DWC_INTSTS_INCMP_OUT_PTX)
			ret |= dwc_otg_pcd_handle_incomplete_isoc_out_intr(pcd);

		spin_unlock(&pcd->lock);
	}
	return ret;
}
