/* ==========================================================================
 * $File: //dwh/usb_iip/dev/software/otg/linux/drivers/dwc_otg_pcd_intr.c $
 * $Revision: #119 $
 * $Date: 2012/12/21 $
 * $Change: 2131568 $
 *
 * Synopsys HS OTG Linux Software Driver and documentation (hereinafter,
 * "Software") is an Unsupported proprietary work of Synopsys, Inc. unless
 * otherwise expressly agreed to in writing between Synopsys and you.
 *
 * The Software IS NOT an item of Licensed Software or Licensed Product under
 * any End User Software License Agreement or Agreement for Licensed Product
 * with Synopsys or any supplement thereto. You are permitted to use and
 * redistribute this Software in source and binary forms, with or without
 * modification, provided that redistributions of source code must retain this
 * notice. You may not view, use, disclose, copy or distribute this file or
 * any information contained herein except pursuant to this license grant from
 * Synopsys. If you do not agree with this notice, including the disclaimer
 * below, then you are not authorized to use the Software.
 *
 * THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS" BASIS
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * ========================================================================== */
#ifndef DWC_HOST_ONLY

#include "dwc_otg_pcd.h"
#include "dwc_otg_driver.h"

#ifdef DWC_UTE_CFI
#include "dwc_otg_cfi.h"
#endif

#ifdef DWC_UTE_PER_IO
extern void complete_xiso_ep(dwc_otg_pcd_ep_t *ep);
#endif
/* #define PRINT_CFI_DMA_DESCS */

#define DEBUG_EP0

/**
 * This function updates OTG.
 */
static void dwc_otg_pcd_update_otg(dwc_otg_pcd_t *pcd, const unsigned reset)
{

	if (reset) {
		pcd->b_hnp_enable = 0;
		pcd->a_hnp_support = 0;
		pcd->a_alt_hnp_support = 0;
	}

	if (pcd->fops->hnp_changed) {
		pcd->fops->hnp_changed(pcd);
	}
}

/** @file
 * This file contains the implementation of the PCD Interrupt handlers.
 *
 * The PCD handles the device interrupts.  Many conditions can cause a
 * device interrupt. When an interrupt occurs, the device interrupt
 * service routine determines the cause of the interrupt and
 * dispatches handling to the appropriate function. These interrupt
 * handling functions are described below.
 * All interrupt registers are processed from LSB to MSB.
 */

/**
 * This function prints the ep0 state for debug purposes.
 */
static inline void print_ep0_state(dwc_otg_pcd_t *pcd)
{
#ifdef DEBUG
	char str[40];

	switch (pcd->ep0state) {
	case EP0_DISCONNECT:
		dwc_strcpy(str, "EP0_DISCONNECT");
		break;
	case EP0_IDLE:
		dwc_strcpy(str, "EP0_IDLE");
		break;
	case EP0_IN_DATA_PHASE:
		dwc_strcpy(str, "EP0_IN_DATA_PHASE");
		break;
	case EP0_OUT_DATA_PHASE:
		dwc_strcpy(str, "EP0_OUT_DATA_PHASE");
		break;
	case EP0_IN_STATUS_PHASE:
		dwc_strcpy(str, "EP0_IN_STATUS_PHASE");
		break;
	case EP0_OUT_STATUS_PHASE:
		dwc_strcpy(str, "EP0_OUT_STATUS_PHASE");
		break;
	case EP0_STALL:
		dwc_strcpy(str, "EP0_STALL");
		break;
	default:
		dwc_strcpy(str, "EP0_INVALID");
	}

	DWC_DEBUGPL(DBG_ANY, "%s(%d)\n", str, pcd->ep0state);
#endif
}

/**
 * This function calculate the size of the payload in the memory
 * for out endpoints and prints size for debug purposes(used in
 * 2.93a DevOutNak feature).
 */
static inline void print_memory_payload(dwc_otg_pcd_t *pcd, dwc_ep_t *ep)
{
#ifdef DEBUG
	deptsiz_data_t deptsiz_init = {.d32 = 0 };
	deptsiz_data_t deptsiz_updt = {.d32 = 0 };
	int pack_num;
	unsigned payload;

	deptsiz_init.d32 = pcd->core_if->start_doeptsiz_val[ep->num];
	deptsiz_updt.d32 =
	    DWC_READ_REG32(&pcd->core_if->dev_if->
			   out_ep_regs[ep->num]->doeptsiz);
	/* Payload will be */
	payload = deptsiz_init.b.xfersize - deptsiz_updt.b.xfersize;
	/* Packet count is decremented every time a packet
	 * is written to the RxFIFO not in to the external memory
	 * So, if payload == 0, then it means no packet was sent to ext memory*/
	pack_num =
	    (!payload) ? 0 : (deptsiz_init.b.pktcnt - deptsiz_updt.b.pktcnt);
	DWC_DEBUGPL(DBG_PCDV, "Payload for EP%d-%s\n", ep->num,
		    (ep->is_in ? "IN" : "OUT"));
	DWC_DEBUGPL(DBG_PCDV, "Number of transfered bytes = 0x%08x\n", payload);
	DWC_DEBUGPL(DBG_PCDV, "Number of transfered packets = %d\n", pack_num);
#endif
}

#ifdef DWC_UTE_CFI
static inline void print_desc(struct dwc_otg_dma_desc *ddesc,
			      const uint8_t *epname, int descnum)
{
	CFI_INFO
	    ("%s DMA_DESC(%d) buf=0x%08x bytes=0x%04x; sp=0x%x; l=0x%x; sts=0x%02x; bs=0x%02x\n",
	     epname, descnum, ddesc->buf, ddesc->status.b.bytes,
	     ddesc->status.b.sp, ddesc->status.b.l, ddesc->status.b.sts,
	     ddesc->status.b.bs);
}
#endif

/**
 * This function returns pointer to in ep struct with number ep_num
 */
static inline dwc_otg_pcd_ep_t *get_in_ep(dwc_otg_pcd_t *pcd, uint32_t ep_num)
{
	int i;
	int num_in_eps = GET_CORE_IF(pcd)->dev_if->num_in_eps;
	if (ep_num == 0) {
		return &pcd->ep0;
	} else {
		for (i = 0; i < num_in_eps; ++i) {
			if (pcd->in_ep[i].dwc_ep.num == ep_num)
				return &pcd->in_ep[i];
		}
		return 0;
	}
}

/**
 * This function returns pointer to out ep struct with number ep_num
 */
static inline dwc_otg_pcd_ep_t *get_out_ep(dwc_otg_pcd_t *pcd, uint32_t ep_num)
{
	int i;
	int num_out_eps = GET_CORE_IF(pcd)->dev_if->num_out_eps;
	if (ep_num == 0) {
		return &pcd->ep0;
	} else {
		for (i = 0; i < num_out_eps; ++i) {
			if (pcd->out_ep[i].dwc_ep.num == ep_num)
				return &pcd->out_ep[i];
		}
		return 0;
	}
}

/**
 * This functions gets a pointer to an EP from the wIndex address
 * value of the control request.
 */
dwc_otg_pcd_ep_t *get_ep_by_addr(dwc_otg_pcd_t *pcd, u16 wIndex)
{
	dwc_otg_pcd_ep_t *ep;
	uint32_t ep_num = UE_GET_ADDR(wIndex);

	if (ep_num == 0) {
		ep = &pcd->ep0;
	} else if (UE_GET_DIR(wIndex) == UE_DIR_IN) {	/* in ep */
		ep = &pcd->in_ep[ep_num - 1];
	} else {
		ep = &pcd->out_ep[ep_num - 1];
	}

	return ep;
}

/**
 * This function checks the EP request queue, if the queue is not
 * empty the next request is started.
 */
void start_next_request(dwc_otg_pcd_ep_t *ep)
{
	dwc_otg_pcd_request_t *req = 0;
	uint32_t max_transfer =
	    GET_CORE_IF(ep->pcd)->core_params->max_transfer_size;

#ifdef DWC_UTE_CFI
	struct dwc_otg_pcd *pcd;
	pcd = ep->pcd;
#endif

	if (!DWC_CIRCLEQ_EMPTY(&ep->queue)) {
		req = DWC_CIRCLEQ_FIRST(&ep->queue);

#ifdef DWC_UTE_CFI
		if (ep->dwc_ep.buff_mode != BM_STANDARD) {
			ep->dwc_ep.cfi_req_len = req->length;
			pcd->cfi->ops.build_descriptors(pcd->cfi, pcd, ep, req);
		} else {
#endif
			/* Setup and start the Transfer */
			if (req->dw_align_buf) {
				ep->dwc_ep.dma_addr = req->dw_align_buf_dma;
				ep->dwc_ep.start_xfer_buff = req->dw_align_buf;
				ep->dwc_ep.xfer_buff = req->dw_align_buf;
			} else {
				ep->dwc_ep.dma_addr = req->dma;
				ep->dwc_ep.start_xfer_buff = req->buf;
				ep->dwc_ep.xfer_buff = req->buf;
			}
			ep->dwc_ep.sent_zlp = 0;
			ep->dwc_ep.total_len = req->length;
			ep->dwc_ep.xfer_len = 0;
			ep->dwc_ep.xfer_count = 0;

			ep->dwc_ep.maxxfer = max_transfer;
			if (GET_CORE_IF(ep->pcd)->dma_desc_enable) {
				uint32_t out_max_xfer = DDMA_MAX_TRANSFER_SIZE
				    - (DDMA_MAX_TRANSFER_SIZE % 4);
				if (ep->dwc_ep.is_in) {
					if (ep->dwc_ep.maxxfer >
					    DDMA_MAX_TRANSFER_SIZE) {
						ep->dwc_ep.maxxfer =
						    DDMA_MAX_TRANSFER_SIZE;
					}
				} else {
					if (ep->dwc_ep.maxxfer > out_max_xfer) {
						ep->dwc_ep.maxxfer =
						    out_max_xfer;
					}
				}
			}
			if (ep->dwc_ep.maxxfer < ep->dwc_ep.total_len) {
				ep->dwc_ep.maxxfer -=
				    (ep->dwc_ep.maxxfer % ep->dwc_ep.maxpacket);
			}
			if (req->sent_zlp) {
				if ((ep->dwc_ep.total_len %
				     ep->dwc_ep.maxpacket == 0)
				    && (ep->dwc_ep.total_len != 0)) {
					ep->dwc_ep.sent_zlp = 1;
				}

			}
#ifdef DWC_UTE_CFI
		}
#endif
		dwc_otg_ep_start_transfer(GET_CORE_IF(ep->pcd), &ep->dwc_ep);
	} else if (ep->dwc_ep.type == DWC_OTG_EP_TYPE_ISOC) {
		DWC_PRINTF("There are no more ISOC requests \n");
		ep->dwc_ep.frame_num = 0xFFFFFFFF;
	}
}

/**
 * This function handles the SOF Interrupts. At this time the SOF
 * Interrupt is disabled.
 */
int32_t dwc_otg_pcd_handle_sof_intr(dwc_otg_pcd_t *pcd)
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(pcd);

	gintsts_data_t gintsts;

	DWC_DEBUGPL(DBG_PCD, "SOF\n");

	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.sofintr = 1;
	DWC_WRITE_REG32(&core_if->core_global_regs->gintsts, gintsts.d32);

	return 1;
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
int32_t dwc_otg_pcd_handle_rx_status_q_level_intr(dwc_otg_pcd_t *pcd)
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(pcd);
	dwc_otg_core_global_regs_t *global_regs = core_if->core_global_regs;
	gintmsk_data_t gintmask = {.d32 = 0 };
	device_grxsts_data_t status;
	dwc_otg_pcd_ep_t *ep;
	gintsts_data_t gintsts;
#ifdef DEBUG
	static char *dpid_str[] = { "D0", "D2", "D1", "MDATA" };
#endif

	/* DWC_DEBUGPL(DBG_PCDV, "%s(%p)\n", __func__, _pcd); */
	/* Disable the Rx Status Queue Level interrupt */
	gintmask.b.rxstsqlvl = 1;
	DWC_MODIFY_REG32(&global_regs->gintmsk, gintmask.d32, 0);

	/* Get the Status from the top of the FIFO */
	status.d32 = DWC_READ_REG32(&global_regs->grxstsp);

	DWC_DEBUGPL(DBG_PCD, "EP:%d BCnt:%d DPID:%s "
		    "pktsts:%x Frame:%d(0x%0x)\n",
		    status.b.epnum, status.b.bcnt,
		    dpid_str[status.b.dpid],
		    status.b.pktsts, status.b.fn, status.b.fn);
	/* Get pointer to EP structure */
	ep = get_out_ep(pcd, status.b.epnum);

	switch (status.b.pktsts) {
	case DWC_DSTS_GOUT_NAK:
		DWC_DEBUGPL(DBG_PCDV, "Global OUT NAK\n");
		break;
	case DWC_STS_DATA_UPDT:
		DWC_DEBUGPL(DBG_PCDV, "OUT Data Packet\n");
		if (status.b.bcnt && ep->dwc_ep.xfer_buff) {
			/** @todo NGS Check for buffer overflow? */
			dwc_otg_read_packet(core_if,
					    ep->dwc_ep.xfer_buff,
					    status.b.bcnt);
			ep->dwc_ep.xfer_count += status.b.bcnt;
			ep->dwc_ep.xfer_buff += status.b.bcnt;
		}
		break;
	case DWC_STS_XFER_COMP:
		DWC_DEBUGPL(DBG_PCDV, "OUT Complete\n");
		break;
	case DWC_DSTS_SETUP_COMP:
#ifdef DEBUG_EP0
		DWC_DEBUGPL(DBG_PCDV, "Setup Complete\n");
#endif
		break;
	case DWC_DSTS_SETUP_UPDT:
		dwc_otg_read_setup_packet(core_if, pcd->setup_pkt->d32);
#ifdef DEBUG_EP0
		DWC_DEBUGPL(DBG_PCD,
			    "SETUP PKT: %02x.%02x v%04x i%04x l%04x\n",
			    pcd->setup_pkt->req.bmRequestType,
			    pcd->setup_pkt->req.bRequest,
			    UGETW(pcd->setup_pkt->req.wValue),
			    UGETW(pcd->setup_pkt->req.wIndex),
			    UGETW(pcd->setup_pkt->req.wLength));
#endif
		ep->dwc_ep.xfer_count += status.b.bcnt;
		break;
	default:
		DWC_DEBUGPL(DBG_PCDV, "Invalid Packet Status (0x%0x)\n",
			    status.b.pktsts);
		break;
	}

	/* Enable the Rx Status Queue Level interrupt */
	DWC_MODIFY_REG32(&global_regs->gintmsk, 0, gintmask.d32);
	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.rxstsqlvl = 1;
	DWC_WRITE_REG32(&global_regs->gintsts, gintsts.d32);

	/* DWC_DEBUGPL(DBG_PCDV, "EXIT: %s\n", __func__); */
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
 *
 * @param core_if Programming view of DWC_otg controller.
 *
 */
static inline int get_ep_of_last_in_token(dwc_otg_core_if_t *core_if)
{
	dwc_otg_device_global_regs_t *dev_global_regs =
	    core_if->dev_if->dev_global_regs;
	const uint32_t TOKEN_Q_DEPTH = core_if->hwcfg2.b.dev_token_q_depth;
	/* Number of Token Queue Registers */
	const int DTKNQ_REG_CNT = (TOKEN_Q_DEPTH + 7) / 8;
	dtknq1_data_t dtknqr1;
	uint32_t in_tkn_epnums[4];
	int ndx = 0;
	int i = 0;
	volatile uint32_t *addr = &dev_global_regs->dtknqr1;
	int epnum = 0;

	/* DWC_DEBUGPL(DBG_PCD,"dev_token_q_depth=%d\n",TOKEN_Q_DEPTH); */

	/* Read the DTKNQ Registers */
	for (i = 0; i < DTKNQ_REG_CNT; i++) {
		in_tkn_epnums[i] = DWC_READ_REG32(addr);
		DWC_DEBUGPL(DBG_PCDV, "DTKNQR%d=0x%08x\n", i + 1,
			    in_tkn_epnums[i]);
		if (addr == &dev_global_regs->dvbusdis) {
			addr = &dev_global_regs->dtknqr3_dthrctl;
		} else {
			++addr;
		}

	}

	/* Copy the DTKNQR1 data to the bit field. */
	dtknqr1.d32 = in_tkn_epnums[0];
	/* Get the EP numbers */
	in_tkn_epnums[0] = dtknqr1.b.epnums0_5;
	ndx = dtknqr1.b.intknwptr - 1;

	/* DWC_DEBUGPL(DBG_PCDV,"ndx=%d\n",ndx); */
	if (ndx == -1) {
		/** @todo Find a simpler way to calculate the max
		 * queue position.*/
		int cnt = TOKEN_Q_DEPTH;
		if (TOKEN_Q_DEPTH <= 6) {
			cnt = TOKEN_Q_DEPTH - 1;
		} else if (TOKEN_Q_DEPTH <= 14) {
			cnt = TOKEN_Q_DEPTH - 7;
		} else if (TOKEN_Q_DEPTH <= 22) {
			cnt = TOKEN_Q_DEPTH - 15;
		} else {
			cnt = TOKEN_Q_DEPTH - 23;
		}
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
	/* DWC_DEBUGPL(DBG_PCD,"epnum=%d\n",epnum); */
	return epnum;
}

/**
 * This interrupt occurs when the non-periodic Tx FIFO is half-empty.
 * The active request is checked for the next packet to be loaded into
 * the non-periodic Tx FIFO.
 */
int32_t dwc_otg_pcd_handle_np_tx_fifo_empty_intr(dwc_otg_pcd_t *pcd)
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(pcd);
	dwc_otg_core_global_regs_t *global_regs = core_if->core_global_regs;
	dwc_otg_dev_in_ep_regs_t *ep_regs;
	gnptxsts_data_t txstatus = {.d32 = 0 };
	gintsts_data_t gintsts;

	int epnum = 0;
	dwc_otg_pcd_ep_t *ep = 0;
	uint32_t len = 0;
	int dwords;

	/* Get the epnum from the IN Token Learning Queue. */
	epnum = get_ep_of_last_in_token(core_if);
	ep = get_in_ep(pcd, epnum);

	DWC_DEBUGPL(DBG_PCD, "NP TxFifo Empty: %d \n", epnum);

	ep_regs = core_if->dev_if->in_ep_regs[epnum];

	len = ep->dwc_ep.xfer_len - ep->dwc_ep.xfer_count;
	if (len > ep->dwc_ep.maxpacket) {
		len = ep->dwc_ep.maxpacket;
	}
	dwords = (len + 3) / 4;

	/* While there is space in the queue and space in the FIFO and
	 * More data to tranfer, Write packets to the Tx FIFO */
	txstatus.d32 = DWC_READ_REG32(&global_regs->gnptxsts);
	DWC_DEBUGPL(DBG_PCDV, "b4 GNPTXSTS=0x%08x\n", txstatus.d32);

	while (txstatus.b.nptxqspcavail > 0 &&
	       txstatus.b.nptxfspcavail > dwords &&
	       ep->dwc_ep.xfer_count < ep->dwc_ep.xfer_len) {
		/* Write the FIFO */
		dwc_otg_ep_write_packet(core_if, &ep->dwc_ep, 0);
		len = ep->dwc_ep.xfer_len - ep->dwc_ep.xfer_count;

		if (len > ep->dwc_ep.maxpacket) {
			len = ep->dwc_ep.maxpacket;
		}

		dwords = (len + 3) / 4;
		txstatus.d32 = DWC_READ_REG32(&global_regs->gnptxsts);
		DWC_DEBUGPL(DBG_PCDV, "GNPTXSTS=0x%08x\n", txstatus.d32);
	}

	DWC_DEBUGPL(DBG_PCDV, "GNPTXSTS=0x%08x\n",
		    DWC_READ_REG32(&global_regs->gnptxsts));

	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.nptxfempty = 1;
	DWC_WRITE_REG32(&global_regs->gintsts, gintsts.d32);

	return 1;
}

/**
 * This function is called when dedicated Tx FIFO Empty interrupt occurs.
 * The active request is checked for the next packet to be loaded into
 * apropriate Tx FIFO.
 */
static int32_t write_empty_tx_fifo(dwc_otg_pcd_t *pcd, uint32_t epnum)
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(pcd);
	dwc_otg_dev_if_t *dev_if = core_if->dev_if;
	dwc_otg_dev_in_ep_regs_t *ep_regs;
	dtxfsts_data_t txstatus = {.d32 = 0 };
	dwc_otg_pcd_ep_t *ep = 0;
	uint32_t len = 0;
	int dwords;

	ep = get_in_ep(pcd, epnum);

	DWC_DEBUGPL(DBG_PCD, "Dedicated TxFifo Empty: %d \n", epnum);

	ep_regs = core_if->dev_if->in_ep_regs[epnum];

	len = ep->dwc_ep.xfer_len - ep->dwc_ep.xfer_count;

	if (len > ep->dwc_ep.maxpacket) {
		len = ep->dwc_ep.maxpacket;
	}

	dwords = (len + 3) / 4;

	/* While there is space in the queue and space in the FIFO and
	 * More data to tranfer, Write packets to the Tx FIFO */
	txstatus.d32 = DWC_READ_REG32(&dev_if->in_ep_regs[epnum]->dtxfsts);
	DWC_DEBUGPL(DBG_PCDV, "b4 dtxfsts[%d]=0x%08x\n", epnum, txstatus.d32);

	while (txstatus.b.txfspcavail > dwords &&
	       ep->dwc_ep.xfer_count < ep->dwc_ep.xfer_len &&
	       ep->dwc_ep.xfer_len != 0) {
		/* Write the FIFO */
		dwc_otg_ep_write_packet(core_if, &ep->dwc_ep, 0);

		len = ep->dwc_ep.xfer_len - ep->dwc_ep.xfer_count;
		if (len > ep->dwc_ep.maxpacket) {
			len = ep->dwc_ep.maxpacket;
		}

		dwords = (len + 3) / 4;
		txstatus.d32 =
		    DWC_READ_REG32(&dev_if->in_ep_regs[epnum]->dtxfsts);
		DWC_DEBUGPL(DBG_PCDV, "dtxfsts[%d]=0x%08x\n", epnum,
			    txstatus.d32);
	}

	DWC_DEBUGPL(DBG_PCDV, "b4 dtxfsts[%d]=0x%08x\n", epnum,
		    DWC_READ_REG32(&dev_if->in_ep_regs[epnum]->dtxfsts));

	return 1;
}

/**
 * This function is called when the Device is disconnected. It stops
 * any active requests and informs the Gadget driver of the
 * disconnect.
 */
void dwc_otg_pcd_stop(dwc_otg_pcd_t *pcd)
{
	int i, num_in_eps, num_out_eps;
	dwc_otg_pcd_ep_t *ep;
	gintmsk_data_t intr_mask = {.d32 = 0 };

	DWC_SPINLOCK(pcd->lock);

	num_in_eps = GET_CORE_IF(pcd)->dev_if->num_in_eps;
	num_out_eps = GET_CORE_IF(pcd)->dev_if->num_out_eps;

	DWC_DEBUGPL(DBG_PCDV, "%s() \n", __func__);
	/* don't disconnect drivers more than once */
	if (pcd->ep0state == EP0_DISCONNECT) {
		DWC_DEBUGPL(DBG_ANY, "%s() Already Disconnected\n", __func__);
		DWC_SPINUNLOCK(pcd->lock);
		return;
	}
	pcd->ep0state = EP0_DISCONNECT;

	/* Reset the OTG state. */
	dwc_otg_pcd_update_otg(pcd, 1);

	/* Disable the NP Tx Fifo Empty Interrupt. */
	intr_mask.b.nptxfempty = 1;
	DWC_MODIFY_REG32(&GET_CORE_IF(pcd)->core_global_regs->gintmsk,
			 intr_mask.d32, 0);

	/* Flush the FIFOs */
	/**@todo NGS Flush Periodic FIFOs */
	dwc_otg_flush_tx_fifo(GET_CORE_IF(pcd), 0x10);
	dwc_otg_flush_rx_fifo(GET_CORE_IF(pcd));

	/* prevent new request submissions, kill any outstanding requests  */
	ep = &pcd->ep0;
	dwc_otg_request_nuke(ep);
	/* prevent new request submissions, kill any outstanding requests  */
	for (i = 0; i < num_in_eps; i++) {
		dwc_otg_pcd_ep_t *ep = &pcd->in_ep[i];
		dwc_otg_request_nuke(ep);
	}
	/* prevent new request submissions, kill any outstanding requests  */
	for (i = 0; i < num_out_eps; i++) {
		dwc_otg_pcd_ep_t *ep = &pcd->out_ep[i];
		dwc_otg_request_nuke(ep);
	}

	/* report disconnect; the driver is already quiesced */
	if (pcd->fops->disconnect) {
		DWC_SPINUNLOCK(pcd->lock);
		pcd->fops->disconnect(pcd);
		DWC_SPINLOCK(pcd->lock);
	}
	DWC_SPINUNLOCK(pcd->lock);
}

/**
 * This interrupt indicates that ...
 */
int32_t dwc_otg_pcd_handle_i2c_intr(dwc_otg_pcd_t *pcd)
{
	gintmsk_data_t intr_mask = {.d32 = 0 };
	gintsts_data_t gintsts;

	DWC_PRINTF("INTERRUPT Handler not implemented for %s\n", "i2cintr");
	intr_mask.b.i2cintr = 1;
	DWC_MODIFY_REG32(&GET_CORE_IF(pcd)->core_global_regs->gintmsk,
			 intr_mask.d32, 0);

	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.i2cintr = 1;
	DWC_WRITE_REG32(&GET_CORE_IF(pcd)->core_global_regs->gintsts,
			gintsts.d32);
	return 1;
}

/**
 * This interrupt indicates that ...
 */
int32_t dwc_otg_pcd_handle_early_suspend_intr(dwc_otg_pcd_t *pcd)
{
	gintsts_data_t gintsts;
#if defined(VERBOSE)
	DWC_PRINTF("Early Suspend Detected\n");
#endif

	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.erlysuspend = 1;
	DWC_WRITE_REG32(&GET_CORE_IF(pcd)->core_global_regs->gintsts,
			gintsts.d32);
	return 1;
}

/**
 * This function configures EPO to receive SETUP packets.
 *
 * @todo NGS: Update the comments from the HW FS.
 *
 *	-# Program the following fields in the endpoint specific registers
 *	for Control OUT EP 0, in order to receive a setup packet
 *	- DOEPTSIZ0.Packet Count = 3 (To receive up to 3 back to back
 *	  setup packets)
 *	- DOEPTSIZE0.Transfer Size = 24 Bytes (To receive up to 3 back
 *	  to back setup packets)
 *		- In DMA mode, DOEPDMA0 Register with a memory address to
 *		  store any setup packets received
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param pcd	  Programming view of the PCD.
 */
static inline void ep0_out_start(dwc_otg_core_if_t *core_if,
				 dwc_otg_pcd_t *pcd)
{
	dwc_otg_dev_if_t *dev_if = core_if->dev_if;
	deptsiz0_data_t doeptsize0 = {.d32 = 0 };
	dwc_otg_dev_dma_desc_t *dma_desc;
	depctl_data_t doepctl = {.d32 = 0 };

#ifdef VERBOSE
	DWC_DEBUGPL(DBG_PCDV, "%s() doepctl0=%0x\n", __func__,
		    DWC_READ_REG32(&dev_if->out_ep_regs[0]->doepctl));
#endif
	if (core_if->snpsid >= OTG_CORE_REV_3_00a) {
		doepctl.d32 = DWC_READ_REG32(&dev_if->out_ep_regs[0]->doepctl);
		if (doepctl.b.epena) {
			return;
		}
	}

	doeptsize0.b.supcnt = 1;
	doeptsize0.b.pktcnt = 1;
	doeptsize0.b.xfersize = 8 * 3;

	if (core_if->dma_enable) {
		if (!core_if->dma_desc_enable) {
			/** put here as for Hermes mode deptisz register should not be written */
			DWC_WRITE_REG32(&dev_if->out_ep_regs[0]->doeptsiz,
					doeptsize0.d32);

			/** @todo dma needs to handle multiple setup packets (up to 3) */
			DWC_WRITE_REG32(&dev_if->out_ep_regs[0]->doepdma,
					pcd->setup_pkt_dma_handle);
		} else {
			dev_if->setup_desc_index =
			    (dev_if->setup_desc_index + 1) & 1;
			dma_desc =
			    dev_if->setup_desc_addr[dev_if->setup_desc_index];

			/** DMA Descriptor Setup */
			dma_desc->status.b.bs = BS_HOST_BUSY;
			if (core_if->snpsid >= OTG_CORE_REV_3_00a) {
				dma_desc->status.b.sr = 0;
				dma_desc->status.b.mtrf = 0;
			}
			dma_desc->status.b.l = 1;
			dma_desc->status.b.ioc = 1;
			dma_desc->status.b.bytes = pcd->ep0.dwc_ep.maxpacket;
			dma_desc->buf = pcd->setup_pkt_dma_handle;
			dma_desc->status.b.sts = 0;
			dma_desc->status.b.bs = BS_HOST_READY;

			/** DOEPDMA0 Register write */
			DWC_WRITE_REG32(&dev_if->out_ep_regs[0]->doepdma,
					dev_if->dma_setup_desc_addr
					[dev_if->setup_desc_index]);
		}

	} else {
		/** put here as for Hermes mode deptisz register should not be written */
		DWC_WRITE_REG32(&dev_if->out_ep_regs[0]->doeptsiz,
				doeptsize0.d32);
	}

	/** DOEPCTL0 Register write cnak will be set after setup interrupt */
	doepctl.d32 = 0;
	doepctl.b.epena = 1;
	DWC_MODIFY_REG32(&dev_if->out_ep_regs[0]->doepctl, 0,
			 doepctl.d32);

#ifdef VERBOSE
	DWC_DEBUGPL(DBG_PCDV, "doepctl0=%0x\n",
		    DWC_READ_REG32(&dev_if->out_ep_regs[0]->doepctl));
	DWC_DEBUGPL(DBG_PCDV, "diepctl0=%0x\n",
		    DWC_READ_REG32(&dev_if->in_ep_regs[0]->diepctl));
#endif
}

/**
 * This interrupt occurs when a USB Reset is detected. When the USB
 * Reset Interrupt occurs the device state is set to DEFAULT and the
 * EP0 state is set to IDLE.
 *	-#	Set the NAK bit for all OUT endpoints (DOEPCTLn.SNAK = 1)
 *	-#	Unmask the following interrupt bits
 *		- DAINTMSK.INEP0 = 1 (Control 0 IN endpoint)
 *	- DAINTMSK.OUTEP0 = 1 (Control 0 OUT endpoint)
 *	- DOEPMSK.SETUP = 1
 *	- DOEPMSK.XferCompl = 1
 *	- DIEPMSK.XferCompl = 1
 *	- DIEPMSK.TimeOut = 1
 *	-# Program the following fields in the endpoint specific registers
 *	for Control OUT EP 0, in order to receive a setup packet
 *	- DOEPTSIZ0.Packet Count = 3 (To receive up to 3 back to back
 *	  setup packets)
 *	- DOEPTSIZE0.Transfer Size = 24 Bytes (To receive up to 3 back
 *	  to back setup packets)
 *		- In DMA mode, DOEPDMA0 Register with a memory address to
 *		  store any setup packets received
 * At this point, all the required initialization, except for enabling
 * the control 0 OUT endpoint is done, for receiving SETUP packets.
 */
int32_t dwc_otg_pcd_handle_usb_reset_intr(dwc_otg_pcd_t *pcd)
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(pcd);
	dwc_otg_dev_if_t *dev_if = core_if->dev_if;
	depctl_data_t doepctl = {.d32 = 0 };
	depctl_data_t diepctl = {.d32 = 0 };
	daint_data_t daintmsk = {.d32 = 0 };
	doepmsk_data_t doepmsk = {.d32 = 0 };
	diepmsk_data_t diepmsk = {.d32 = 0 };
	dcfg_data_t dcfg = {.d32 = 0 };
	grstctl_t resetctl = {.d32 = 0 };
	dctl_data_t dctl = {.d32 = 0 };
	int i = 0;
	gintsts_data_t gintsts;
	pcgcctl_data_t power = {.d32 = 0 };

	power.d32 = DWC_READ_REG32(core_if->pcgcctl);
	if (power.b.stoppclk) {
		power.d32 = 0;
		power.b.stoppclk = 1;
		DWC_MODIFY_REG32(core_if->pcgcctl, power.d32, 0);

		power.b.pwrclmp = 1;
		DWC_MODIFY_REG32(core_if->pcgcctl, power.d32, 0);

		power.b.rstpdwnmodule = 1;
		DWC_MODIFY_REG32(core_if->pcgcctl, power.d32, 0);
	}

	core_if->lx_state = DWC_OTG_L0;
	core_if->otg_sts = 0;

	DWC_PRINTF("USB RESET\n");
#ifdef DWC_EN_ISOC
	for (i = 1; i < 16; ++i) {
		dwc_otg_pcd_ep_t *ep;
		dwc_ep_t *dwc_ep;
		ep = get_in_ep(pcd, i);
		if (ep != 0) {
			dwc_ep = &ep->dwc_ep;
			dwc_ep->next_frame = 0xffffffff;
		}
	}
#endif /* DWC_EN_ISOC */

	/* reset the HNP settings */
	dwc_otg_pcd_update_otg(pcd, 1);

	/* Clear the Remote Wakeup Signalling */
	dctl.b.rmtwkupsig = 1;
	DWC_MODIFY_REG32(&core_if->dev_if->dev_global_regs->dctl, dctl.d32, 0);

	/* Set NAK for all OUT EPs */
	doepctl.b.snak = 1;
	for (i = 0; i <= dev_if->num_out_eps; i++) {
		DWC_WRITE_REG32(&dev_if->out_ep_regs[i]->doepctl, doepctl.d32);
	}

	/* Flush the NP Tx FIFO */
	dwc_otg_flush_tx_fifo(core_if, 0x10);
	/* Flush the Learning Queue */
	resetctl.b.intknqflsh = 1;
	DWC_WRITE_REG32(&core_if->core_global_regs->grstctl, resetctl.d32);

	if (!core_if->core_params->en_multiple_tx_fifo && core_if->dma_enable) {
		core_if->start_predict = 0;
		for (i = 0; i <= core_if->dev_if->num_in_eps; ++i) {
			core_if->nextep_seq[i] = 0xff; /*0xff - EP not active */
		}
		core_if->nextep_seq[0] = 0;
		core_if->first_in_nextep_seq = 0;
		diepctl.d32 = DWC_READ_REG32(&dev_if->in_ep_regs[0]->diepctl);
		diepctl.b.nextep = 0;
		DWC_WRITE_REG32(&dev_if->in_ep_regs[0]->diepctl, diepctl.d32);

		/* Update IN Endpoint Mismatch Count by active IN NP EP count + 1 */
		dcfg.d32 = DWC_READ_REG32(&dev_if->dev_global_regs->dcfg);
		dcfg.b.epmscnt = 2;
		DWC_WRITE_REG32(&dev_if->dev_global_regs->dcfg, dcfg.d32);

		DWC_DEBUGPL(DBG_PCDV,
			    "%s first_in_nextep_seq= %2d; nextep_seq[]:\n",
			    __func__, core_if->first_in_nextep_seq);
		for (i = 0; i <= core_if->dev_if->num_in_eps; i++) {
			DWC_DEBUGPL(DBG_PCDV, "%2d\n", core_if->nextep_seq[i]);
		}
	}

	if (core_if->multiproc_int_enable) {
		daintmsk.b.inep0 = 1;
		daintmsk.b.outep0 = 1;
		DWC_WRITE_REG32(&dev_if->dev_global_regs->deachintmsk,
				daintmsk.d32);

		doepmsk.b.setup = 1;
		doepmsk.b.xfercompl = 1;
		doepmsk.b.ahberr = 1;
		doepmsk.b.epdisabled = 1;

		if ((core_if->dma_desc_enable) ||
		    (core_if->dma_enable
		     && core_if->snpsid >= OTG_CORE_REV_3_00a)) {
			doepmsk.b.stsphsercvd = 1;
		}
		if (core_if->dma_desc_enable)
			doepmsk.b.bna = 1;
/*
		doepmsk.b.babble = 1;
		doepmsk.b.nyet = 1;

		if (core_if->dma_enable) {
			doepmsk.b.nak = 1;
		}
*/
		DWC_WRITE_REG32(&dev_if->dev_global_regs->doepeachintmsk[0],
				doepmsk.d32);

		diepmsk.b.xfercompl = 1;
		diepmsk.b.timeout = 1;
		diepmsk.b.epdisabled = 1;
		diepmsk.b.ahberr = 1;
		diepmsk.b.intknepmis = 1;
		if (!core_if->en_multiple_tx_fifo && core_if->dma_enable)
			diepmsk.b.intknepmis = 0;

/*		if (core_if->dma_desc_enable) {
			diepmsk.b.bna = 1;
		}
*/
/*
		if (core_if->dma_enable) {
			diepmsk.b.nak = 1;
		}
*/
		DWC_WRITE_REG32(&dev_if->dev_global_regs->diepeachintmsk[0],
				diepmsk.d32);
	} else {
		daintmsk.b.inep0 = 1;
		daintmsk.b.outep0 = 1;
		DWC_WRITE_REG32(&dev_if->dev_global_regs->daintmsk,
				daintmsk.d32);

		doepmsk.b.setup = 1;
		doepmsk.b.xfercompl = 1;
		doepmsk.b.ahberr = 1;
		doepmsk.b.epdisabled = 1;

		if ((core_if->dma_desc_enable) ||
		    (core_if->dma_enable
		     && core_if->snpsid >= OTG_CORE_REV_3_00a)) {
			doepmsk.b.stsphsercvd = 1;
		}
		if (core_if->dma_desc_enable)
			doepmsk.b.bna = 1;
		DWC_WRITE_REG32(&dev_if->dev_global_regs->doepmsk, doepmsk.d32);

		diepmsk.b.xfercompl = 1;
		diepmsk.b.timeout = 1;
		diepmsk.b.epdisabled = 1;
		diepmsk.b.ahberr = 1;
		if (!core_if->en_multiple_tx_fifo && core_if->dma_enable)
			diepmsk.b.intknepmis = 0;
/*
		if (core_if->dma_desc_enable) {
			diepmsk.b.bna = 1;
		}
*/

		DWC_WRITE_REG32(&dev_if->dev_global_regs->diepmsk, diepmsk.d32);
	}

	/* Reset Device Address */
	dcfg.d32 = DWC_READ_REG32(&dev_if->dev_global_regs->dcfg);
	dcfg.b.devaddr = 0;
	DWC_WRITE_REG32(&dev_if->dev_global_regs->dcfg, dcfg.d32);

	/* setup EP0 to receive SETUP packets */
	if (core_if->snpsid <= OTG_CORE_REV_2_94a)
		ep0_out_start(core_if, pcd);

	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.usbreset = 1;
	DWC_WRITE_REG32(&core_if->core_global_regs->gintsts, gintsts.d32);

	return 1;
}

/**
 * Get the device speed from the device status register and convert it
 * to USB speed constant.
 *
 * @param core_if Programming view of DWC_otg controller.
 */
static int get_device_speed(dwc_otg_core_if_t *core_if)
{
	dsts_data_t dsts;
	int speed = 0;
	dsts.d32 = DWC_READ_REG32(&core_if->dev_if->dev_global_regs->dsts);

	switch (dsts.b.enumspd) {
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
 * Read the device status register and set the device speed in the
 * data structure.
 * Set up EP0 to receive SETUP packets by calling dwc_ep0_activate.
 */
int32_t dwc_otg_pcd_handle_enum_done_intr(dwc_otg_pcd_t *pcd)
{
	dwc_otg_pcd_ep_t *ep0 = &pcd->ep0;
	gintsts_data_t gintsts;
	gusbcfg_data_t gusbcfg;
	dwc_otg_core_global_regs_t *global_regs =
	    GET_CORE_IF(pcd)->core_global_regs;
	uint8_t utmi16b, utmi8b;
	int speed;

	DWC_DEBUGPL(DBG_PCD, "SPEED ENUM\n");

	if (GET_CORE_IF(pcd)->snpsid >= OTG_CORE_REV_2_60a) {
		utmi16b = 5;	/* vahrama old value was 6; */
		utmi8b = 9;
	} else {
		utmi16b = 4;
		utmi8b = 8;
	}
	dwc_otg_ep0_activate(GET_CORE_IF(pcd), &ep0->dwc_ep);
	if (GET_CORE_IF(pcd)->snpsid >= OTG_CORE_REV_3_00a) {
		ep0_out_start(GET_CORE_IF(pcd), pcd);
	}
#ifdef DEBUG_EP0
	print_ep0_state(pcd);
#endif

	if (pcd->ep0state == EP0_DISCONNECT) {
		pcd->ep0state = EP0_IDLE;
	} else if (pcd->ep0state == EP0_STALL) {
		pcd->ep0state = EP0_IDLE;
	}

	pcd->ep0state = EP0_IDLE;

	ep0->stopped = 0;

	speed = get_device_speed(GET_CORE_IF(pcd));
	pcd->fops->connect(pcd, speed);

	/* Set USB turnaround time based on device speed and PHY interface. */
	gusbcfg.d32 = DWC_READ_REG32(&global_regs->gusbcfg);
	if (speed == USB_SPEED_HIGH) {
		if (GET_CORE_IF(pcd)->hwcfg2.b.hs_phy_type ==
		    DWC_HWCFG2_HS_PHY_TYPE_ULPI) {
			/* ULPI interface */
			gusbcfg.b.usbtrdtim = 9;
		}
		if (GET_CORE_IF(pcd)->hwcfg2.b.hs_phy_type ==
		    DWC_HWCFG2_HS_PHY_TYPE_UTMI) {
			/* UTMI+ interface */
			if (GET_CORE_IF(pcd)->hwcfg4.b.utmi_phy_data_width == 0) {
				gusbcfg.b.usbtrdtim = utmi8b;
			} else if (GET_CORE_IF(pcd)->hwcfg4.
				   b.utmi_phy_data_width == 1) {
				gusbcfg.b.usbtrdtim = utmi16b;
			} else if (GET_CORE_IF(pcd)->
				   core_params->phy_utmi_width == 8) {
				gusbcfg.b.usbtrdtim = utmi8b;
			} else {
				gusbcfg.b.usbtrdtim = utmi16b;
			}
		}
		if (GET_CORE_IF(pcd)->hwcfg2.b.hs_phy_type ==
		    DWC_HWCFG2_HS_PHY_TYPE_UTMI_ULPI) {
			/* UTMI+  OR  ULPI interface */
			if (gusbcfg.b.ulpi_utmi_sel == 1) {
				/* ULPI interface */
				gusbcfg.b.usbtrdtim = 9;
			} else {
				/* UTMI+ interface */
				if (GET_CORE_IF(pcd)->
				    core_params->phy_utmi_width == 16) {
					gusbcfg.b.usbtrdtim = utmi16b;
				} else {
					gusbcfg.b.usbtrdtim = utmi8b;
				}
			}
		}
	} else {
		/* Full or low speed */
		gusbcfg.b.usbtrdtim = 9;
	}
	DWC_WRITE_REG32(&global_regs->gusbcfg, gusbcfg.d32);

	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.enumdone = 1;
	DWC_WRITE_REG32(&GET_CORE_IF(pcd)->core_global_regs->gintsts,
			gintsts.d32);
	return 1;
}

/**
 * This interrupt indicates that the ISO OUT Packet was dropped due to
 * Rx FIFO full or Rx Status Queue Full.  If this interrupt occurs
 * read all the data from the Rx FIFO.
 */
int32_t dwc_otg_pcd_handle_isoc_out_packet_dropped_intr(dwc_otg_pcd_t *pcd)
{
	gintmsk_data_t intr_mask = {.d32 = 0 };
	gintsts_data_t gintsts;

	DWC_WARN("INTERRUPT Handler not implemented for %s\n",
		 "ISOC Out Dropped");

	intr_mask.b.isooutdrop = 1;
	DWC_MODIFY_REG32(&GET_CORE_IF(pcd)->core_global_regs->gintmsk,
			 intr_mask.d32, 0);

	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.isooutdrop = 1;
	DWC_WRITE_REG32(&GET_CORE_IF(pcd)->core_global_regs->gintsts,
			gintsts.d32);

	return 1;
}

/**
 * This interrupt indicates the end of the portion of the micro-frame
 * for periodic transactions.  If there is a periodic transaction for
 * the next frame, load the packets into the EP periodic Tx FIFO.
 */
int32_t dwc_otg_pcd_handle_end_periodic_frame_intr(dwc_otg_pcd_t *pcd)
{
	gintmsk_data_t intr_mask = {.d32 = 0 };
	gintsts_data_t gintsts;
	DWC_PRINTF("INTERRUPT Handler not implemented for %s\n", "EOP");

	intr_mask.b.eopframe = 1;
	DWC_MODIFY_REG32(&GET_CORE_IF(pcd)->core_global_regs->gintmsk,
			 intr_mask.d32, 0);

	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.eopframe = 1;
	DWC_WRITE_REG32(&GET_CORE_IF(pcd)->core_global_regs->gintsts,
			gintsts.d32);

	return 1;
}

/**
 * This interrupt indicates that EP of the packet on the top of the
 * non-periodic Tx FIFO does not match EP of the IN Token received.
 *
 * The "Device IN Token Queue" Registers are read to determine the
 * order the IN Tokens have been received. The non-periodic Tx FIFO
 * is flushed, so it can be reloaded in the order seen in the IN Token
 * Queue.
 */
int32_t dwc_otg_pcd_handle_ep_mismatch_intr(dwc_otg_pcd_t *pcd)
{
	gintsts_data_t gintsts;
	dwc_otg_core_if_t *core_if = GET_CORE_IF(pcd);
	dctl_data_t dctl;
	gintmsk_data_t intr_mask = {.d32 = 0 };

	if (!core_if->en_multiple_tx_fifo && core_if->dma_enable) {
		core_if->start_predict = 1;

		DWC_DEBUGPL(DBG_PCDV, "%s(%p)\n", __func__, core_if);

		gintsts.d32 =
		    DWC_READ_REG32(&core_if->core_global_regs->gintsts);
		if (!gintsts.b.ginnakeff) {
			/* Disable EP Mismatch interrupt */
			intr_mask.d32 = 0;
			intr_mask.b.epmismatch = 1;
			DWC_MODIFY_REG32(&core_if->core_global_regs->gintmsk,
					 intr_mask.d32, 0);
			/* Enable the Global IN NAK Effective Interrupt */
			intr_mask.d32 = 0;
			intr_mask.b.ginnakeff = 1;
			DWC_MODIFY_REG32(&core_if->core_global_regs->gintmsk, 0,
					 intr_mask.d32);
			/* Set the global non-periodic IN NAK handshake */
			dctl.d32 =
			    DWC_READ_REG32(&core_if->dev_if->
					   dev_global_regs->dctl);
			dctl.b.sgnpinnak = 1;
			DWC_WRITE_REG32(&core_if->dev_if->dev_global_regs->dctl,
					dctl.d32);
		} else {
			DWC_PRINTF
			    ("gintsts.b.ginnakeff = 1! dctl.b.sgnpinnak not set\n");
		}
		/* Disabling of all EP's will be done in dwc_otg_pcd_handle_in_nak_effective()
		 * handler after Global IN NAK Effective interrupt will be asserted */
	}
	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.epmismatch = 1;
	DWC_WRITE_REG32(&core_if->core_global_regs->gintsts, gintsts.d32);

	return 1;
}

/**
 * This interrupt is valid only in DMA mode. This interrupt indicates that the
 * core has stopped fetching data for IN endpoints due to the unavailability of
 * TxFIFO space or Request Queue space. This interrupt is used by the
 * application for an endpoint mismatch algorithm.
 *
 * @param pcd The PCD
 */
int32_t dwc_otg_pcd_handle_ep_fetsusp_intr(dwc_otg_pcd_t *pcd)
{
	gintsts_data_t gintsts;
	gintmsk_data_t gintmsk_data;
	dctl_data_t dctl;
	dwc_otg_core_if_t *core_if = GET_CORE_IF(pcd);
	DWC_DEBUGPL(DBG_PCDV, "%s(%p)\n", __func__, core_if);

	/* Clear the global non-periodic IN NAK handshake */
	dctl.d32 = 0;
	dctl.b.cgnpinnak = 1;
	DWC_MODIFY_REG32(&core_if->dev_if->dev_global_regs->dctl, dctl.d32,
			 dctl.d32);

	/* Mask GINTSTS.FETSUSP interrupt */
	gintmsk_data.d32 = DWC_READ_REG32(&core_if->core_global_regs->gintmsk);
	gintmsk_data.b.fetsusp = 0;
	DWC_WRITE_REG32(&core_if->core_global_regs->gintmsk, gintmsk_data.d32);

	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.fetsusp = 1;
	DWC_WRITE_REG32(&core_if->core_global_regs->gintsts, gintsts.d32);

	return 1;
}

/**
 * This funcion stalls EP0.
 */
static inline void ep0_do_stall(dwc_otg_pcd_t *pcd, const int err_val)
{
	dwc_otg_pcd_ep_t *ep0 = &pcd->ep0;
	usb_device_request_t *ctrl = &pcd->setup_pkt->req;
	DWC_WARN("req %02x.%02x protocol STALL; err %d\n",
		 ctrl->bmRequestType, ctrl->bRequest, err_val);

	ep0->dwc_ep.is_in = 1;
	dwc_otg_ep_set_stall(GET_CORE_IF(pcd), &ep0->dwc_ep);
	ep0->dwc_ep.is_in = 0;
	dwc_otg_ep_set_stall(GET_CORE_IF(pcd), &ep0->dwc_ep);
	pcd->ep0.stopped = 1;
	pcd->ep0state = EP0_IDLE;
	ep0_out_start(GET_CORE_IF(pcd), pcd);
}

/**
 * This functions delegates the setup command to the gadget driver.
 */
static inline void do_gadget_setup(dwc_otg_pcd_t *pcd,
				   usb_device_request_t *ctrl)
{
	int ret = 0;
	DWC_SPINUNLOCK(pcd->lock);
	ret = pcd->fops->setup(pcd, (uint8_t *) ctrl);
	if (spin_is_locked((spinlock_t *) pcd->lock))
		DWC_WARN("%s warning: pcd->lock locked without unlock\n",
			 __func__);
	DWC_SPINLOCK(pcd->lock);
	if (ret < 0) {
		ep0_do_stall(pcd, ret);
	}

	/** @todo This is a g_file_storage gadget driver specific
	 * workaround: a DELAYED_STATUS result from the fsg_setup
	 * routine will result in the gadget queueing a EP0 IN status
	 * phase for a two-stage control transfer. Exactly the same as
	 * a SET_CONFIGURATION/SET_INTERFACE except that this is a class
	 * specific request.  Need a generic way to know when the gadget
	 * driver will queue the status phase. Can we assume when we
	 * call the gadget driver setup() function that it will always
	 * queue and require the following flag? Need to look into
	 * this.
	 */

	if (ret == 256 + 999) {
		pcd->request_config = 1;
	}
}

#ifdef DWC_UTE_CFI
/**
 * This functions delegates the CFI setup commands to the gadget driver.
 * This function will return a negative value to indicate a failure.
 */
static inline int cfi_gadget_setup(dwc_otg_pcd_t *pcd,
				   struct cfi_usb_ctrlrequest *ctrl_req)
{
	int ret = 0;

	if (pcd->fops && pcd->fops->cfi_setup) {
		DWC_SPINUNLOCK(pcd->lock);
		ret = pcd->fops->cfi_setup(pcd, ctrl_req);
		DWC_SPINLOCK(pcd->lock);
		if (ret < 0) {
			ep0_do_stall(pcd, ret);
			return ret;
		}
	}

	return ret;
}
#endif

/**
 * This function starts the Zero-Length Packet for the IN status phase
 * of a 2 stage control transfer.
 */
static inline void do_setup_in_status_phase(dwc_otg_pcd_t *pcd)
{
	dwc_otg_pcd_ep_t *ep0 = &pcd->ep0;
	if (pcd->ep0state == EP0_STALL) {
		return;
	}

	pcd->ep0state = EP0_IN_STATUS_PHASE;

	/* Prepare for more SETUP Packets */
	DWC_DEBUGPL(DBG_PCD, "EP0 IN ZLP\n");
	if ((GET_CORE_IF(pcd)->snpsid >= OTG_CORE_REV_3_00a)
	    && (pcd->core_if->dma_desc_enable)
	    && (ep0->dwc_ep.xfer_count < ep0->dwc_ep.total_len)) {
		DWC_DEBUGPL(DBG_PCDV,
			    "Data terminated wait next packet in out_desc_addr\n");
		pcd->backup_buf = phys_to_virt(ep0->dwc_ep.dma_addr);
		pcd->data_terminated = 1;
	}
	ep0->dwc_ep.xfer_len = 0;
	ep0->dwc_ep.xfer_count = 0;
	ep0->dwc_ep.is_in = 1;
	ep0->dwc_ep.dma_addr = pcd->setup_pkt_dma_handle;
	dwc_otg_ep0_start_transfer(GET_CORE_IF(pcd), &ep0->dwc_ep);

	/* Prepare for more SETUP Packets */
	/* ep0_out_start(GET_CORE_IF(pcd), pcd); */
}

/**
 * This function starts the Zero-Length Packet for the OUT status phase
 * of a 2 stage control transfer.
 */
static inline void do_setup_out_status_phase(dwc_otg_pcd_t *pcd)
{
	dwc_otg_pcd_ep_t *ep0 = &pcd->ep0;
	if (pcd->ep0state == EP0_STALL) {
		DWC_DEBUGPL(DBG_PCD, "EP0 STALLED\n");
		return;
	}
	pcd->ep0state = EP0_OUT_STATUS_PHASE;

	DWC_DEBUGPL(DBG_PCD, "EP0 OUT ZLP\n");
	ep0->dwc_ep.xfer_len = 0;
	ep0->dwc_ep.xfer_count = 0;
	ep0->dwc_ep.is_in = 0;
	ep0->dwc_ep.dma_addr = pcd->setup_pkt_dma_handle;
	dwc_otg_ep0_start_transfer(GET_CORE_IF(pcd), &ep0->dwc_ep);

	/* Prepare for more SETUP Packets */
	if (GET_CORE_IF(pcd)->dma_enable == 0) {
		ep0_out_start(GET_CORE_IF(pcd), pcd);
	}
}

/**
 * Clear the EP halt (STALL) and if pending requests start the
 * transfer.
 */
static inline void pcd_clear_halt(dwc_otg_pcd_t *pcd, dwc_otg_pcd_ep_t *ep)
{
	if (ep->dwc_ep.stall_clear_flag) {
		/* Start Control Status Phase */
		do_setup_in_status_phase(pcd);
		return;
	}

	dwc_otg_ep_clear_stall(GET_CORE_IF(pcd), &ep->dwc_ep);

	/* Reactive the EP */
	dwc_otg_ep_activate(GET_CORE_IF(pcd), &ep->dwc_ep);
	if (ep->stopped) {
		ep->stopped = 0;
		/* If there is a request in the EP queue start it */

		/** @todo FIXME: this causes an EP mismatch in DMA mode.
		 * epmismatch not yet implemented. */

		/*
		 * Above fixme is solved by implmenting a tasklet to call the
		 * start_next_request(), outside of interrupt context at some
		 * time after the current time, after a clear-halt setup packet.
		 * Still need to implement ep mismatch in the future if a gadget
		 * ever uses more than one endpoint at once
		 */
		ep->queue_sof = 1;
		DWC_TASK_SCHEDULE(pcd->start_xfer_tasklet);
	}
	/* Start Control Status Phase */
	do_setup_in_status_phase(pcd);
}

/**
 * This function is called when the SET_FEATURE TEST_MODE Setup packet
 * is sent from the host.  The Device Control register is written with
 * the Test Mode bits set to the specified Test Mode.  This is done as
 * a tasklet so that the "Status" phase of the control transfer
 * completes before transmitting the TEST packets.
 *
 * @todo This has not been tested since the tasklet struct was put
 * into the PCD struct!
 *
 */
void do_test_mode(void *data)
{
	dctl_data_t dctl;
	dwc_otg_pcd_t *pcd = (dwc_otg_pcd_t *) data;
	dwc_otg_core_if_t *core_if = GET_CORE_IF(pcd);
	int test_mode = pcd->test_mode;

	/* DWC_WARN("%s() has not been tested since being rewritten!\n", __func__); */

	dctl.d32 = DWC_READ_REG32(&core_if->dev_if->dev_global_regs->dctl);
	switch (test_mode) {
	case 1:		/* TEST_J */
		dctl.b.tstctl = 1;
		break;

	case 2:		/* TEST_K */
		dctl.b.tstctl = 2;
		break;

	case 3:		/* TEST_SE0_NAK */
		dctl.b.tstctl = 3;
		break;

	case 4:		/* TEST_PACKET */
		dctl.b.tstctl = 4;
		break;

	case 5:		/* TEST_FORCE_ENABLE */
		dctl.b.tstctl = 5;
		break;
	case 7:
		dwc_otg_set_hnpreq(core_if, 1);
	}
	DWC_PRINTF("test mode = %d\n", test_mode);
	core_if->test_mode = test_mode;
	DWC_WRITE_REG32(&core_if->dev_if->dev_global_regs->dctl, dctl.d32);
}

/**
 * This function process the GET_STATUS Setup Commands.
 */
static inline void do_get_status(dwc_otg_pcd_t *pcd)
{
	usb_device_request_t ctrl = pcd->setup_pkt->req;
	dwc_otg_pcd_ep_t *ep;
	dwc_otg_pcd_ep_t *ep0 = &pcd->ep0;
	uint16_t *status = pcd->status_buf;
	dwc_otg_core_if_t *core_if = GET_CORE_IF(pcd);

#ifdef DEBUG_EP0
	DWC_DEBUGPL(DBG_PCD,
		    "GET_STATUS %02x.%02x v%04x i%04x l%04x\n",
		    ctrl.bmRequestType, ctrl.bRequest,
		    UGETW(ctrl.wValue), UGETW(ctrl.wIndex),
		    UGETW(ctrl.wLength));
#endif

	switch (UT_GET_RECIPIENT(ctrl.bmRequestType)) {
	case UT_DEVICE:
		if (UGETW(ctrl.wIndex) == 0xF000) {	/* OTG Status selector */
			DWC_PRINTF("wIndex - %d\n", UGETW(ctrl.wIndex));
			DWC_PRINTF("OTG VERSION - %d\n", core_if->otg_ver);
			DWC_PRINTF("OTG CAP - %d, %d\n",
				   core_if->core_params->otg_cap,
				   DWC_OTG_CAP_PARAM_HNP_SRP_CAPABLE);
			if (core_if->otg_ver == 1
			    && core_if->core_params->otg_cap ==
			    DWC_OTG_CAP_PARAM_HNP_SRP_CAPABLE) {
				uint8_t *otgsts = (uint8_t *) pcd->status_buf;
				*otgsts = (core_if->otg_sts & 0x1);
				pcd->ep0_pending = 1;
				ep0->dwc_ep.start_xfer_buff =
				    (uint8_t *) otgsts;
				ep0->dwc_ep.xfer_buff = (uint8_t *) otgsts;
				ep0->dwc_ep.dma_addr =
				    pcd->status_buf_dma_handle;
				ep0->dwc_ep.xfer_len = 1;
				ep0->dwc_ep.xfer_count = 0;
				ep0->dwc_ep.total_len = ep0->dwc_ep.xfer_len;
				dwc_otg_ep0_start_transfer(GET_CORE_IF(pcd),
							   &ep0->dwc_ep);
				return;
			} else {
				ep0_do_stall(pcd, -DWC_E_NOT_SUPPORTED);
				return;
			}
			break;
		} else {
			*status = 0x1;	/* Self powered */
			*status |= pcd->remote_wakeup_enable << 1;
			break;
		}
	case UT_INTERFACE:
		*status = 0;
		break;

	case UT_ENDPOINT:
		ep = get_ep_by_addr(pcd, UGETW(ctrl.wIndex));
		if (ep == 0 || UGETW(ctrl.wLength) > 2) {
			ep0_do_stall(pcd, -DWC_E_NOT_SUPPORTED);
			return;
		}
		/** @todo check for EP stall */
		*status = ep->stopped;
		break;
	}
	pcd->ep0_pending = 1;
	ep0->dwc_ep.start_xfer_buff = (uint8_t *) status;
	ep0->dwc_ep.xfer_buff = (uint8_t *) status;
	ep0->dwc_ep.dma_addr = pcd->status_buf_dma_handle;
	ep0->dwc_ep.xfer_len = 2;
	ep0->dwc_ep.xfer_count = 0;
	ep0->dwc_ep.total_len = ep0->dwc_ep.xfer_len;
	dwc_otg_ep0_start_transfer(GET_CORE_IF(pcd), &ep0->dwc_ep);
}

/**
 * This function process the SET_FEATURE Setup Commands.
 */
static inline void do_set_feature(dwc_otg_pcd_t *pcd)
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(pcd);
	dwc_otg_core_global_regs_t *global_regs = core_if->core_global_regs;
	usb_device_request_t ctrl = pcd->setup_pkt->req;
	dwc_otg_pcd_ep_t *ep = 0;
	int32_t otg_cap_param = core_if->core_params->otg_cap;
	gotgctl_data_t gotgctl = {.d32 = 0 };

	DWC_DEBUGPL(DBG_PCD, "SET_FEATURE:%02x.%02x v%04x i%04x l%04x\n",
		    ctrl.bmRequestType, ctrl.bRequest,
		    UGETW(ctrl.wValue), UGETW(ctrl.wIndex),
		    UGETW(ctrl.wLength));
	DWC_DEBUGPL(DBG_PCD, "otg_cap=%d\n", otg_cap_param);

	switch (UT_GET_RECIPIENT(ctrl.bmRequestType)) {
	case UT_DEVICE:
		switch (UGETW(ctrl.wValue)) {
		case UF_DEVICE_REMOTE_WAKEUP:
			pcd->remote_wakeup_enable = 1;
			break;

		case UF_TEST_MODE:
			/* Setup the Test Mode tasklet to do the Test
			 * Packet generation after the SETUP Status
			 * phase has completed. */

			/** @todo This has not been tested since the
			 * tasklet struct was put into the PCD
			 * struct! */
			pcd->test_mode = UGETW(ctrl.wIndex) >> 8;
			DWC_TASK_SCHEDULE(pcd->test_mode_tasklet);
			break;

		case UF_DEVICE_B_HNP_ENABLE:
			DWC_DEBUGPL(DBG_PCDV,
				    "SET_FEATURE: USB_DEVICE_B_HNP_ENABLE\n");

			/* dev may initiate HNP */
			if (otg_cap_param == DWC_OTG_CAP_PARAM_HNP_SRP_CAPABLE) {
				gotgctl.b.devhnpen = 1;
				if (core_if->otg_ver == 1)
					DWC_MODIFY_REG32(&global_regs->gotgctl,
							 0, gotgctl.d32);
				else {
					pcd->b_hnp_enable = 1;
					dwc_otg_pcd_update_otg(pcd, 0);
					DWC_DEBUGPL(DBG_PCD, "Request B HNP\n");
					/**@todo Is the gotgctl.devhnpen cleared
					 * by a USB Reset? */
					gotgctl.b.hnpreq = 1;
					DWC_WRITE_REG32(&global_regs->gotgctl,
							gotgctl.d32);
				}
			} else {
				ep0_do_stall(pcd, -DWC_E_NOT_SUPPORTED);
				return;
			}
			break;

		case UF_DEVICE_A_HNP_SUPPORT:
			/* RH port supports HNP */
			DWC_DEBUGPL(DBG_PCDV,
				    "SET_FEATURE: USB_DEVICE_A_HNP_SUPPORT\n");
			if (otg_cap_param == DWC_OTG_CAP_PARAM_HNP_SRP_CAPABLE) {
				pcd->a_hnp_support = 1;
				dwc_otg_pcd_update_otg(pcd, 0);
			} else {
				ep0_do_stall(pcd, -DWC_E_NOT_SUPPORTED);
				return;
			}
			break;

		case UF_DEVICE_A_ALT_HNP_SUPPORT:
			/* other RH port does */
			DWC_DEBUGPL(DBG_PCDV,
				    "SET_FEATURE: USB_DEVICE_A_ALT_HNP_SUPPORT\n");
			if (otg_cap_param == DWC_OTG_CAP_PARAM_HNP_SRP_CAPABLE) {
				pcd->a_alt_hnp_support = 1;
				dwc_otg_pcd_update_otg(pcd, 0);
			} else {
				ep0_do_stall(pcd, -DWC_E_NOT_SUPPORTED);
				return;
			}
			break;

		default:
			ep0_do_stall(pcd, -DWC_E_NOT_SUPPORTED);
			return;

		}
		do_setup_in_status_phase(pcd);
		break;

	case UT_INTERFACE:
		do_gadget_setup(pcd, &ctrl);
		break;

	case UT_ENDPOINT:
		if (UGETW(ctrl.wValue) == UF_ENDPOINT_HALT) {
			ep = get_ep_by_addr(pcd, UGETW(ctrl.wIndex));
			if (ep == 0) {
				ep0_do_stall(pcd, -DWC_E_NOT_SUPPORTED);
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
static inline void do_clear_feature(dwc_otg_pcd_t *pcd)
{
	usb_device_request_t ctrl = pcd->setup_pkt->req;
	dwc_otg_pcd_ep_t *ep = 0;

	DWC_DEBUGPL(DBG_PCD,
		    "CLEAR_FEATURE:%02x.%02x v%04x i%04x l%04x\n",
		    ctrl.bmRequestType, ctrl.bRequest,
		    UGETW(ctrl.wValue), UGETW(ctrl.wIndex),
		    UGETW(ctrl.wLength));

	switch (UT_GET_RECIPIENT(ctrl.bmRequestType)) {
	case UT_DEVICE:
		switch (UGETW(ctrl.wValue)) {
		case UF_DEVICE_REMOTE_WAKEUP:
			pcd->remote_wakeup_enable = 0;
			break;

		case UF_TEST_MODE:
			/** @todo Add CLEAR_FEATURE for TEST modes. */
			break;

		default:
			ep0_do_stall(pcd, -DWC_E_NOT_SUPPORTED);
			return;
		}
		do_setup_in_status_phase(pcd);
		break;

	case UT_ENDPOINT:
		ep = get_ep_by_addr(pcd, UGETW(ctrl.wIndex));
		if (ep == 0) {
			ep0_do_stall(pcd, -DWC_E_NOT_SUPPORTED);
			return;
		}

		pcd_clear_halt(pcd, ep);

		break;
	}
}

/**
 * This function process the SET_ADDRESS Setup Commands.
 */
static inline void do_set_address(dwc_otg_pcd_t *pcd)
{
	dwc_otg_dev_if_t *dev_if = GET_CORE_IF(pcd)->dev_if;
	usb_device_request_t ctrl = pcd->setup_pkt->req;

	if (ctrl.bmRequestType == UT_DEVICE) {
		dcfg_data_t dcfg = {.d32 = 0 };

#ifdef DEBUG_EP0
		/* DWC_DEBUGPL(DBG_PCDV, "SET_ADDRESS:%d\n", ctrl.wValue); */
#endif
		dcfg.b.devaddr = UGETW(ctrl.wValue);
		DWC_MODIFY_REG32(&dev_if->dev_global_regs->dcfg, 0, dcfg.d32);
		do_setup_in_status_phase(pcd);
	}
}

/**
 *	This function processes SETUP commands. In Linux, the USB Command
 *	processing is done in two places - the first being the PCD and the
 *	second in the Gadget Driver (for example, the File-Backed Storage
 *	Gadget Driver).
 *
 * <table>
 * <tr><td>Command	</td><td>Driver </td><td>Description</td></tr>
 *
 * <tr><td>GET_STATUS </td><td>PCD </td><td>Command is processed as
 * defined in chapter 9 of the USB 2.0 Specification chapter 9
 * </td></tr>
 *
 * <tr><td>CLEAR_FEATURE </td><td>PCD </td><td>The Device and Endpoint
 * requests are the ENDPOINT_HALT feature is procesed, all others the
 * interface requests are ignored.</td></tr>
 *
 * <tr><td>SET_FEATURE </td><td>PCD </td><td>The Device and Endpoint
 * requests are processed by the PCD.  Interface requests are passed
 * to the Gadget Driver.</td></tr>
 *
 * <tr><td>SET_ADDRESS </td><td>PCD </td><td>Program the DCFG reg,
 * with device address received </td></tr>
 *
 * <tr><td>GET_DESCRIPTOR </td><td>Gadget Driver </td><td>Return the
 * requested descriptor</td></tr>
 *
 * <tr><td>SET_DESCRIPTOR </td><td>Gadget Driver </td><td>Optional -
 * not implemented by any of the existing Gadget Drivers.</td></tr>
 *
 * <tr><td>SET_CONFIGURATION </td><td>Gadget Driver </td><td>Disable
 * all EPs and enable EPs for new configuration.</td></tr>
 *
 * <tr><td>GET_CONFIGURATION </td><td>Gadget Driver </td><td>Return
 * the current configuration</td></tr>
 *
 * <tr><td>SET_INTERFACE </td><td>Gadget Driver </td><td>Disable all
 * EPs and enable EPs for new configuration.</td></tr>
 *
 * <tr><td>GET_INTERFACE </td><td>Gadget Driver </td><td>Return the
 * current interface.</td></tr>
 *
 * <tr><td>SYNC_FRAME </td><td>PCD </td><td>Display debug
 * message.</td></tr>
 * </table>
 *
 * When the SETUP Phase Done interrupt occurs, the PCD SETUP commands are
 * processed by pcd_setup. Calling the Function Driver's setup function from
 *pcd_setup processes the gadget SETUP commands.
 */
static inline void pcd_setup(dwc_otg_pcd_t *pcd)
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(pcd);
	dwc_otg_dev_if_t *dev_if = core_if->dev_if;
	usb_device_request_t ctrl = pcd->setup_pkt->req;
	dwc_otg_pcd_ep_t *ep0 = &pcd->ep0;

	deptsiz0_data_t doeptsize0 = {.d32 = 0 };

#ifdef DWC_UTE_CFI
	int retval = 0;
	struct cfi_usb_ctrlrequest cfi_req;
#endif

	doeptsize0.d32 = DWC_READ_REG32(&dev_if->out_ep_regs[0]->doeptsiz);

#ifdef DEBUG_EP0
	DWC_DEBUGPL(DBG_PCD, "SETUP %02x.%02x v%04x i%04x l%04x\n",
		    ctrl.bmRequestType, ctrl.bRequest,
		    UGETW(ctrl.wValue), UGETW(ctrl.wIndex),
		    UGETW(ctrl.wLength));
#endif

	/* Clean up the request queue */
	dwc_otg_request_nuke(ep0);
	ep0->stopped = 0;

	if (ctrl.bmRequestType & UE_DIR_IN) {
		ep0->dwc_ep.is_in = 1;
		pcd->ep0state = EP0_IN_DATA_PHASE;
	} else {
		ep0->dwc_ep.is_in = 0;
		pcd->ep0state = EP0_OUT_DATA_PHASE;
	}

	if (UGETW(ctrl.wLength) == 0) {
		ep0->dwc_ep.is_in = 1;
		pcd->ep0state = EP0_IN_STATUS_PHASE;
	}

	if (UT_GET_TYPE(ctrl.bmRequestType) != UT_STANDARD) {

#ifdef DWC_UTE_CFI
		DWC_MEMCPY(&cfi_req, &ctrl, sizeof(usb_device_request_t));

		/* printk(KERN_ALERT "CFI: req_type=0x%02x; req=0x%02x\n",
		 * ctrl.bRequestType, ctrl.bRequest); */
		if (UT_GET_TYPE(cfi_req.bRequestType) == UT_VENDOR) {
			if (cfi_req.bRequest > 0xB0 && cfi_req.bRequest < 0xBF) {
				retval = cfi_setup(pcd, &cfi_req);
				if (retval < 0) {
					ep0_do_stall(pcd, retval);
					pcd->ep0_pending = 0;
					return;
				}

				/* if need gadget setup then call it and check the retval */
				if (pcd->cfi->need_gadget_att) {
					retval =
					    cfi_gadget_setup(pcd,
							     &pcd->
							     cfi->ctrl_req);
					if (retval < 0) {
						pcd->ep0_pending = 0;
						return;
					}
				}

				if (pcd->cfi->need_status_in_complete) {
					do_setup_in_status_phase(pcd);
				}
				return;
			}
		}
#endif

		/* handle non-standard (class/vendor) requests in the gadget driver */
		do_gadget_setup(pcd, &ctrl);
		return;
	}

	/** @todo NGS: Handle bad setup packet? */

	/* --- Standard Request handling --- */

	switch (ctrl.bRequest) {
	case UR_GET_STATUS:
		do_get_status(pcd);
		break;

	case UR_CLEAR_FEATURE:
		do_clear_feature(pcd);
		break;

	case UR_SET_FEATURE:
		do_set_feature(pcd);
		break;

	case UR_SET_ADDRESS:
		do_set_address(pcd);
		break;

	case UR_SET_INTERFACE:
	case UR_SET_CONFIG:
		/* _pcd->request_config = 1; */      /* Configuration changed */
		do_gadget_setup(pcd, &ctrl);
		break;

	case UR_SYNCH_FRAME:
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
static int32_t ep0_complete_request(dwc_otg_pcd_ep_t *ep)
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(ep->pcd);
	dwc_otg_dev_if_t *dev_if = core_if->dev_if;
	dwc_otg_dev_in_ep_regs_t *in_ep_regs =
	    dev_if->in_ep_regs[ep->dwc_ep.num];
#ifdef DEBUG_EP0
	dwc_otg_dev_out_ep_regs_t *out_ep_regs =
	    dev_if->out_ep_regs[ep->dwc_ep.num];
#endif
	deptsiz0_data_t deptsiz;
	dev_dma_desc_sts_t desc_sts;
	dwc_otg_pcd_request_t *req;
	int is_last = 0;
	dwc_otg_pcd_t *pcd = ep->pcd;

#ifdef DWC_UTE_CFI
	struct cfi_usb_ctrlrequest *ctrlreq;
	int retval = -DWC_E_NOT_SUPPORTED;
#endif

	if (pcd->ep0_pending && DWC_CIRCLEQ_EMPTY(&ep->queue)) {
		if (ep->dwc_ep.is_in) {
#ifdef DEBUG_EP0
			DWC_DEBUGPL(DBG_PCDV, "Do setup OUT status phase\n");
#endif
			do_setup_out_status_phase(pcd);
		} else {
#ifdef DEBUG_EP0
			DWC_DEBUGPL(DBG_PCDV, "Do setup IN status phase\n");
#endif

#ifdef DWC_UTE_CFI
			ctrlreq = &pcd->cfi->ctrl_req;

			if (UT_GET_TYPE(ctrlreq->bRequestType) == UT_VENDOR) {
				if (ctrlreq->bRequest > 0xB0
				    && ctrlreq->bRequest < 0xBF) {

					/* Return if the PCD failed to handle the request */
					retval = pcd->cfi->ops.
						 ctrl_write_complete(pcd->cfi, pcd);
					if (retval < 0) {
						CFI_INFO
						    ("ERROR setting a new value in the PCD(%d)\n",
						     retval);
						ep0_do_stall(pcd, retval);
						pcd->ep0_pending = 0;
						return 0;
					}

					/* If the gadget needs to be notified on the request */
					if (pcd->cfi->need_gadget_att == 1) {
						/* retval = do_gadget_setup(pcd, &pcd->cfi->ctrl_req); */
						retval =
						    cfi_gadget_setup(pcd,
								     &pcd->cfi->
								     ctrl_req);

						/* Return from the function if the gadget failed to process
						 * the request properly - this should never happen !!!
						 */
						if (retval < 0) {
							CFI_INFO
							    ("ERROR setting a new value in the gadget(%d)\n",
							     retval);
							pcd->ep0_pending = 0;
							return 0;
						}
					}

					CFI_INFO("%s: RETVAL=%d\n", __func__,
						 retval);
					/* If we hit here then the PCD and the gadget has properly
					 * handled the request - so send the ZLP IN to the host.
					 */
					/* @todo: MAS - decide whether we need to start the setup
					 * stage based on the need_setup value of the cfi object
					 */
					do_setup_in_status_phase(pcd);
					pcd->ep0_pending = 0;
					return 1;
				}
			}
#endif

			do_setup_in_status_phase(pcd);
		}
		pcd->ep0_pending = 0;
		return 1;
	}

	if (DWC_CIRCLEQ_EMPTY(&ep->queue)) {
		return 0;
	}
	req = DWC_CIRCLEQ_FIRST(&ep->queue);

	if (pcd->ep0state == EP0_OUT_STATUS_PHASE
	    || pcd->ep0state == EP0_IN_STATUS_PHASE) {
		is_last = 1;
	} else if (ep->dwc_ep.is_in) {
		deptsiz.d32 = DWC_READ_REG32(&in_ep_regs->dieptsiz);
		if (core_if->dma_desc_enable != 0)
			desc_sts = dev_if->in_desc_addr->status;
#ifdef DEBUG_EP0
		DWC_DEBUGPL(DBG_PCDV, "%d len=%d  xfersize=%d pktcnt=%d\n",
			    ep->dwc_ep.num, ep->dwc_ep.xfer_len,
			    deptsiz.b.xfersize, deptsiz.b.pktcnt);
#endif

		if (((core_if->dma_desc_enable == 0)
		     && (deptsiz.b.xfersize == 0))
		    || ((core_if->dma_desc_enable != 0)
			&& (desc_sts.b.bytes == 0))) {
			req->actual = ep->dwc_ep.xfer_count;
			/* Is a Zero Len Packet needed? */
			if (req->sent_zlp) {
#ifdef DEBUG_EP0
				DWC_DEBUGPL(DBG_PCD, "Setup Rx ZLP\n");
#endif
				req->sent_zlp = 0;
			}
			do_setup_out_status_phase(pcd);
		}
	} else {
		/* ep0-OUT */
#ifdef DEBUG_EP0
		deptsiz.d32 = DWC_READ_REG32(&out_ep_regs->doeptsiz);
		DWC_DEBUGPL(DBG_PCDV, "%d len=%d xsize=%d pktcnt=%d\n",
			    ep->dwc_ep.num, ep->dwc_ep.xfer_len,
			    deptsiz.b.xfersize, deptsiz.b.pktcnt);
#endif
		req->actual = ep->dwc_ep.xfer_count;

		/* Is a Zero Len Packet needed? */
		if (req->sent_zlp) {
#ifdef DEBUG_EP0
			DWC_DEBUGPL(DBG_PCDV, "Setup Tx ZLP\n");
#endif
			req->sent_zlp = 0;
		}
		/* For older cores do setup in status phase in Slave/BDMA modes,
		 * starting from 3.00 do that only in slave, and for DMA modes
		 * just re-enable ep 0 OUT here*/
		do_setup_in_status_phase(pcd);
	}

	/* Complete the request */
	if (is_last) {
		dwc_otg_request_done(ep, req, 0);
		ep->dwc_ep.start_xfer_buff = 0;
		ep->dwc_ep.xfer_buff = 0;
		ep->dwc_ep.xfer_len = 0;
		return 1;
	}
	return 0;
}

#ifdef DWC_UTE_CFI
/**
 * This function calculates traverses all the CFI DMA descriptors and
 * and accumulates the bytes that are left to be transfered.
 *
 * @return The total bytes left to transfered, or a negative value as failure
 */
static inline int cfi_calc_desc_residue(dwc_otg_pcd_ep_t *ep)
{
	int32_t ret = 0;
	int i;
	struct dwc_otg_dma_desc *ddesc = NULL;
	struct cfi_ep *cfiep;

	/* See if the pcd_ep has its respective cfi_ep mapped */
	cfiep = get_cfi_ep_by_pcd_ep(ep->pcd->cfi, ep);
	if (!cfiep) {
		CFI_INFO("%s: Failed to find ep\n", __func__);
		return -1;
	}

	ddesc = ep->dwc_ep.descs;

	for (i = 0; (i < cfiep->desc_count) && (i < MAX_DMA_DESCS_PER_EP); i++) {

#if defined(PRINT_CFI_DMA_DESCS)
		print_desc(ddesc, ep->ep.name, i);
#endif
		ret += ddesc->status.b.bytes;
		ddesc++;
	}

	if (ret)
		CFI_INFO("!!!!!!!!!! WARNING (%s) - residue=%d\n", __func__,
			 ret);

	return ret;
}
#endif

/**
 * This function completes the request for the EP. If there are
 * additional requests for the EP in the queue they will be started.
 */
static void complete_ep(dwc_otg_pcd_ep_t *ep)
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(ep->pcd);
	dwc_otg_dev_if_t *dev_if = core_if->dev_if;
	dwc_otg_dev_in_ep_regs_t *in_ep_regs =
	    dev_if->in_ep_regs[ep->dwc_ep.num];
	deptsiz_data_t deptsiz;
	dev_dma_desc_sts_t desc_sts;
	dwc_otg_pcd_request_t *req = 0;
	dwc_otg_dev_dma_desc_t *dma_desc;
	uint32_t byte_count = 0;
	int is_last = 0;
	int i;

	DWC_DEBUGPL(DBG_PCDV, "%s() %d-%s\n", __func__, ep->dwc_ep.num,
		    (ep->dwc_ep.is_in ? "IN" : "OUT"));

	/* Get any pending requests */
	if (!DWC_CIRCLEQ_EMPTY(&ep->queue)) {
		req = DWC_CIRCLEQ_FIRST(&ep->queue);
		if (!req) {
			DWC_PRINTF("complete_ep 0x%p, req = NULL!\n", ep);
			return;
		}
	} else {
		DWC_PRINTF("complete_ep 0x%p, ep->queue empty!\n", ep);
		return;
	}

	DWC_DEBUGPL(DBG_PCD, "Requests %d\n", ep->pcd->request_pending);

	if (ep->dwc_ep.is_in) {
		deptsiz.d32 = DWC_READ_REG32(&in_ep_regs->dieptsiz);

		if (core_if->dma_enable) {
			if (core_if->dma_desc_enable == 0) {
				if (deptsiz.b.xfersize == 0
				    && deptsiz.b.pktcnt == 0) {
					byte_count =
					    ep->dwc_ep.xfer_len -
					    ep->dwc_ep.xfer_count;

					ep->dwc_ep.xfer_buff += byte_count;
					ep->dwc_ep.dma_addr += byte_count;
					ep->dwc_ep.xfer_count += byte_count;

					DWC_DEBUGPL(DBG_PCDV,
						    "%d-%s len=%d  xfersize=%d pktcnt=%d\n",
						    ep->dwc_ep.num,
						    (ep->dwc_ep.is_in ? "IN" :
						     "OUT"),
						    ep->dwc_ep.xfer_len,
						    deptsiz.b.xfersize,
						    deptsiz.b.pktcnt);

					if (ep->dwc_ep.xfer_len <
					    ep->dwc_ep.total_len) {
						dwc_otg_ep_start_transfer
						    (core_if, &ep->dwc_ep);
					} else if (ep->dwc_ep.sent_zlp) {
						/*
						 * This fragment of code should initiate 0
						 * length transfer in case if it is queued
						 * a transfer with size divisible to EPs max
						 * packet size and with usb_request zero field
						 * is set, which means that after data is transfered,
						 * it is also should be transfered
						 * a 0 length packet at the end. For Slave and
						 * Buffer DMA modes in this case SW has
						 * to initiate 2 transfers one with transfer size,
						 * and the second with 0 size. For Descriptor
						 * DMA mode SW is able to initiate a transfer,
						 * which will handle all the packets including
						 * the last  0 length.
						 */
						ep->dwc_ep.sent_zlp = 0;
						dwc_otg_ep_start_zl_transfer
						    (core_if, &ep->dwc_ep);
					} else {
						is_last = 1;
					}
				} else {
					if (ep->dwc_ep.type ==
					    DWC_OTG_EP_TYPE_ISOC) {
						req->actual = 0;
						dwc_otg_request_done(ep, req,
								     0);

						ep->dwc_ep.start_xfer_buff = 0;
						ep->dwc_ep.xfer_buff = 0;
						ep->dwc_ep.xfer_len = 0;

						/* If there is a request in the queue start it. */
						start_next_request(ep);
					} else
						DWC_WARN
						    ("Incomplete transfer (%d - %s [siz=%d pkt=%d])\n",
						     ep->dwc_ep.num,
						     (ep->
						      dwc_ep.is_in ? "IN" :
						      "OUT"),
						     deptsiz.b.xfersize,
						     deptsiz.b.pktcnt);
				}
			} else {
				dma_desc = ep->dwc_ep.desc_addr;
				byte_count = 0;
				ep->dwc_ep.sent_zlp = 0;

#ifdef DWC_UTE_CFI
				CFI_INFO("%s: BUFFER_MODE=%d\n", __func__,
					 ep->dwc_ep.buff_mode);
				if (ep->dwc_ep.buff_mode != BM_STANDARD) {
					int residue;

					residue = cfi_calc_desc_residue(ep);
					if (residue < 0)
						return;

					byte_count = residue;
				} else {
#endif
					for (i = 0; i < ep->dwc_ep.desc_cnt;
					     ++i) {
						desc_sts = dma_desc->status;
						byte_count += desc_sts.b.bytes;
						dma_desc++;
					}
#ifdef DWC_UTE_CFI
				}
#endif
				if (byte_count == 0) {
					ep->dwc_ep.xfer_count =
					    ep->dwc_ep.total_len;
					is_last = 1;
				} else {
					DWC_WARN("Incomplete transfer\n");
				}
			}
		} else {
			if (deptsiz.b.xfersize == 0 && deptsiz.b.pktcnt == 0) {
				DWC_DEBUGPL(DBG_PCDV,
					    "%d-%s len=%d  xfersize=%d pktcnt=%d\n",
					    ep->dwc_ep.num,
					    ep->dwc_ep.is_in ? "IN" : "OUT",
					    ep->dwc_ep.xfer_len,
					    deptsiz.b.xfersize,
					    deptsiz.b.pktcnt);

				/*      Check if the whole transfer was completed,
				 *      if no, setup transfer for next portion of data
				 */
				if (ep->dwc_ep.xfer_len < ep->dwc_ep.total_len) {
					dwc_otg_ep_start_transfer(core_if,
								  &ep->dwc_ep);
				} else if (ep->dwc_ep.sent_zlp) {
					/*
					 * This fragment of code should initiate 0
					 * length trasfer in case if it is queued
					 * a trasfer with size divisible to EPs max
					 * packet size and with usb_request zero field
					 * is set, which means that after data is transfered,
					 * it is also should be transfered
					 * a 0 length packet at the end. For Slave and
					 * Buffer DMA modes in this case SW has
					 * to initiate 2 transfers one with transfer size,
					 * and the second with 0 size. For Desriptor
					 * DMA mode SW is able to initiate a transfer,
					 * which will handle all the packets including
					 * the last  0 legth.
					 */
					ep->dwc_ep.sent_zlp = 0;
					dwc_otg_ep_start_zl_transfer(core_if,
								     &ep->dwc_ep);
				} else {
					is_last = 1;
				}
			} else {
				DWC_WARN
				    ("Incomplete transfer (%d-%s [siz=%d pkt=%d])\n",
				     ep->dwc_ep.num,
				     (ep->dwc_ep.is_in ? "IN" : "OUT"),
				     deptsiz.b.xfersize, deptsiz.b.pktcnt);
			}
		}
	} else {
		dwc_otg_dev_out_ep_regs_t *out_ep_regs =
		    dev_if->out_ep_regs[ep->dwc_ep.num];
		desc_sts.d32 = 0;
		if (core_if->dma_enable) {
			if (core_if->dma_desc_enable) {
				dma_desc = ep->dwc_ep.desc_addr;
				byte_count = 0;
				ep->dwc_ep.sent_zlp = 0;

#ifdef DWC_UTE_CFI
				CFI_INFO("%s: BUFFER_MODE=%d\n", __func__,
					 ep->dwc_ep.buff_mode);
				if (ep->dwc_ep.buff_mode != BM_STANDARD) {
					int residue;
					residue = cfi_calc_desc_residue(ep);
					if (residue < 0)
						return;
					byte_count = residue;
				} else {
#endif

					for (i = 0; i < ep->dwc_ep.desc_cnt;
					     ++i) {
						desc_sts = dma_desc->status;
						byte_count += desc_sts.b.bytes;
						dma_desc++;
					}

#ifdef DWC_UTE_CFI
				}
#endif
				/* Checking for interrupt Out transfers with not
				 * dword aligned mps sizes
				 */
				if (ep->dwc_ep.type == DWC_OTG_EP_TYPE_INTR &&
				    (ep->dwc_ep.maxpacket % 4)) {
					ep->dwc_ep.xfer_count =
					    ep->dwc_ep.total_len - byte_count;
					if ((ep->dwc_ep.xfer_len %
					     ep->dwc_ep.maxpacket)
					    && (ep->dwc_ep.xfer_len /
						ep->dwc_ep.maxpacket <
						MAX_DMA_DESC_CNT))
						ep->dwc_ep.xfer_len -=
						    (ep->dwc_ep.desc_cnt -
						     1)*ep->dwc_ep.maxpacket +
						    ep->dwc_ep.xfer_len %
						    ep->dwc_ep.maxpacket;
					else
						ep->dwc_ep.xfer_len -=
						    ep->dwc_ep.desc_cnt *
						    ep->dwc_ep.maxpacket;
					if (ep->dwc_ep.xfer_len > 0) {
						dwc_otg_ep_start_transfer
						    (core_if, &ep->dwc_ep);
					} else {
						is_last = 1;
					}
				} else {
					ep->dwc_ep.xfer_count =
					    ep->dwc_ep.total_len - byte_count +
					    ((4 -
					      (ep->dwc_ep.
					       total_len & 0x3)) & 0x3);
					is_last = 1;
				}
			} else {
				deptsiz.d32 = 0;
				deptsiz.d32 =
				    DWC_READ_REG32(&out_ep_regs->doeptsiz);

				byte_count = (ep->dwc_ep.xfer_len -
					      ep->dwc_ep.xfer_count -
					      deptsiz.b.xfersize);
				ep->dwc_ep.xfer_buff += byte_count;
				ep->dwc_ep.dma_addr += byte_count;
				ep->dwc_ep.xfer_count += byte_count;

				/*      Check if the whole transfer was completed,
				 *      if no, setup transfer for next portion of data
				 */
				if (ep->dwc_ep.xfer_len < ep->dwc_ep.total_len) {
					dwc_otg_ep_start_transfer(core_if,
								  &ep->dwc_ep);
				} else if (ep->dwc_ep.sent_zlp) {
					/*
					 * This fragment of code should initiate 0
					 * length trasfer in case if it is queued
					 * a trasfer with size divisible to EPs max
					 * packet size and with usb_request zero field
					 * is set, which means that after data is transfered,
					 * it is also should be transfered
					 * a 0 length packet at the end. For Slave and
					 * Buffer DMA modes in this case SW has
					 * to initiate 2 transfers one with transfer size,
					 * and the second with 0 size. For Desriptor
					 * DMA mode SW is able to initiate a transfer,
					 * which will handle all the packets including
					 * the last  0 legth.
					 */
					ep->dwc_ep.sent_zlp = 0;
					dwc_otg_ep_start_zl_transfer(core_if,
								     &ep->dwc_ep);
				} else {
					is_last = 1;
				}
			}
		} else {
			/*      Check if the whole transfer was completed,
			 *      if no, setup transfer for next portion of data
			 */
			if (ep->dwc_ep.xfer_len < ep->dwc_ep.total_len) {
				dwc_otg_ep_start_transfer(core_if, &ep->dwc_ep);
			} else if (ep->dwc_ep.sent_zlp) {
				/*
				 * This fragment of code should initiate 0
				 * length transfer in case if it is queued
				 * a transfer with size divisible to EPs max
				 * packet size and with usb_request zero field
				 * is set, which means that after data is transfered,
				 * it is also should be transfered
				 * a 0 length packet at the end. For Slave and
				 * Buffer DMA modes in this case SW has
				 * to initiate 2 transfers one with transfer size,
				 * and the second with 0 size. For Descriptor
				 * DMA mode SW is able to initiate a transfer,
				 * which will handle all the packets including
				 * the last  0 length.
				 */
				ep->dwc_ep.sent_zlp = 0;
				dwc_otg_ep_start_zl_transfer(core_if,
							     &ep->dwc_ep);
			} else {
				is_last = 1;
			}
		}

		DWC_DEBUGPL(DBG_PCDV,
			    "addr %p,	 %d-%s len=%d cnt=%d xsize=%d pktcnt=%d\n",
			    &out_ep_regs->doeptsiz, ep->dwc_ep.num,
			    ep->dwc_ep.is_in ? "IN" : "OUT",
			    ep->dwc_ep.xfer_len, ep->dwc_ep.xfer_count,
			    deptsiz.b.xfersize, deptsiz.b.pktcnt);
	}

	/* Complete the request */
	if (is_last) {
#ifdef DWC_UTE_CFI
		if (ep->dwc_ep.buff_mode != BM_STANDARD) {
			req->actual = ep->dwc_ep.cfi_req_len - byte_count;
		} else {
#endif
			req->actual = ep->dwc_ep.xfer_count;
#ifdef DWC_UTE_CFI
		}
#endif
		if (req->dw_align_buf) {
			if (!ep->dwc_ep.is_in) {
				dwc_memcpy(req->buf, req->dw_align_buf,
					   req->length);
			}
			DWC_DEV_DMA_FREE(req->length, req->dw_align_buf,
					 req->dw_align_buf_dma);
		}

		dwc_otg_request_done(ep, req, 0);

		ep->dwc_ep.start_xfer_buff = 0;
		ep->dwc_ep.xfer_buff = 0;
		ep->dwc_ep.xfer_len = 0;

		/* If there is a request in the queue start it. */
		start_next_request(ep);
	}
}

#ifdef DWC_EN_ISOC

/**
 * This function BNA interrupt for Isochronous EPs
 *
 */
static void dwc_otg_pcd_handle_iso_bna(dwc_otg_pcd_ep_t *ep)
{
	dwc_ep_t *dwc_ep = &ep->dwc_ep;
	volatile uint32_t *addr;
	depctl_data_t depctl = {
	.d32 = 0};
	dwc_otg_pcd_t *pcd = ep->pcd;
	dwc_otg_dev_dma_desc_t *dma_desc;
	int i;

	dma_desc =
	    dwc_ep->iso_desc_addr + dwc_ep->desc_cnt * (dwc_ep->proc_buf_num);

	if (dwc_ep->is_in) {
		dev_dma_desc_sts_t sts = {
		.d32 = 0};
		for (i = 0; i < dwc_ep->desc_cnt; ++i, ++dma_desc) {
			sts.d32 = dma_desc->status.d32;
			sts.b_iso_in.bs = BS_HOST_READY;
			dma_desc->status.d32 = sts.d32;
		}
	} else {
		dev_dma_desc_sts_t sts = {
		.d32 = 0};
		for (i = 0; i < dwc_ep->desc_cnt; ++i, ++dma_desc) {
			sts.d32 = dma_desc->status.d32;
			sts.b_iso_out.bs = BS_HOST_READY;
			dma_desc->status.d32 = sts.d32;
		}
	}

	if (dwc_ep->is_in == 0) {
		addr =
		    &GET_CORE_IF(pcd)->dev_if->out_ep_regs[dwc_ep->
							   num]->doepctl;
	} else {
		addr =
		    &GET_CORE_IF(pcd)->dev_if->in_ep_regs[dwc_ep->num]->diepctl;
	}
	depctl.b.epena = 1;
	DWC_MODIFY_REG32(addr, depctl.d32, depctl.d32);
}

/**
 * This function sets latest iso packet information(non-PTI mode)
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param ep The EP to start the transfer on.
 *
 */
void set_current_pkt_info(dwc_otg_core_if_t *core_if, dwc_ep_t *ep)
{
	deptsiz_data_t deptsiz = {
	.d32 = 0};
	dma_addr_t dma_addr;
	uint32_t offset;

	if (ep->proc_buf_num)
		dma_addr = ep->dma_addr1;
	else
		dma_addr = ep->dma_addr0;

	if (ep->is_in) {
		deptsiz.d32 =
		    DWC_READ_REG32(&core_if->dev_if->
				   in_ep_regs[ep->num]->dieptsiz);
		offset = ep->data_per_frame;
	} else {
		deptsiz.d32 =
		    DWC_READ_REG32(&core_if->dev_if->
				   out_ep_regs[ep->num]->doeptsiz);
		offset =
		    ep->data_per_frame +
		    (0x4 & (0x4 - (ep->data_per_frame & 0x3)));
	}

	if (!deptsiz.b.xfersize) {
		ep->pkt_info[ep->cur_pkt].length = ep->data_per_frame;
		ep->pkt_info[ep->cur_pkt].offset =
		    ep->cur_pkt_dma_addr - dma_addr;
		ep->pkt_info[ep->cur_pkt].status = 0;
	} else {
		ep->pkt_info[ep->cur_pkt].length = ep->data_per_frame;
		ep->pkt_info[ep->cur_pkt].offset =
		    ep->cur_pkt_dma_addr - dma_addr;
		ep->pkt_info[ep->cur_pkt].status = -DWC_E_NO_DATA;
	}
	ep->cur_pkt_addr += offset;
	ep->cur_pkt_dma_addr += offset;
	ep->cur_pkt++;
}

/**
 * This function sets latest iso packet information(DDMA mode)
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param dwc_ep The EP to start the transfer on.
 *
 */
static void set_ddma_iso_pkts_info(dwc_otg_core_if_t *core_if,
				   dwc_ep_t *dwc_ep) {
	dwc_otg_dev_dma_desc_t *dma_desc;
	dev_dma_desc_sts_t sts = {
	.d32 = 0};
	iso_pkt_info_t *iso_packet;
	uint32_t data_per_desc;
	uint32_t offset;
	int i, j;

	iso_packet = dwc_ep->pkt_info;

	/** Reinit closed DMA Descriptors*/
	/** ISO OUT EP */
	if (dwc_ep->is_in == 0) {
		dma_desc =
		    dwc_ep->iso_desc_addr +
		    dwc_ep->desc_cnt*dwc_ep->proc_buf_num;
		offset = 0;

		for (i = 0; i < dwc_ep->desc_cnt - dwc_ep->pkt_per_frm;
		     i += dwc_ep->pkt_per_frm) {
			for (j = 0; j < dwc_ep->pkt_per_frm; ++j) {
				data_per_desc =
				    ((j + 1)*dwc_ep->maxpacket >
				     dwc_ep->
				     data_per_frame) ? dwc_ep->data_per_frame -
				    j*dwc_ep->maxpacket : dwc_ep->maxpacket;
				data_per_desc +=
				    (data_per_desc % 4) ? (4 -
							   data_per_desc %
							   4) : 0;

				sts.d32 = dma_desc->status.d32;

				/* Write status in iso_packet_decsriptor  */
				iso_packet->status =
				    sts.b_iso_out.rxsts +
				    (sts.b_iso_out.bs ^ BS_DMA_DONE);
				if (iso_packet->status) {
					iso_packet->status = -DWC_E_NO_DATA;
				}

				/* Received data length */
				if (!sts.b_iso_out.rxbytes) {
					iso_packet->length =
					    data_per_desc -
					    sts.b_iso_out.rxbytes;
				} else {
					iso_packet->length =
					    data_per_desc -
					    sts.b_iso_out.rxbytes + (4 -
								     dwc_ep->data_per_frame
								     % 4);
				}

				iso_packet->offset = offset;

				offset += data_per_desc;
				dma_desc++;
				iso_packet++;
			}
		}

		for (j = 0; j < dwc_ep->pkt_per_frm - 1; ++j) {
			data_per_desc =
			    ((j + 1)*dwc_ep->maxpacket >
			     dwc_ep->data_per_frame) ? dwc_ep->data_per_frame -
			    j*dwc_ep->maxpacket : dwc_ep->maxpacket;
			data_per_desc +=
			    (data_per_desc % 4) ? (4 - data_per_desc % 4) : 0;

			sts.d32 = dma_desc->status.d32;

			/* Write status in iso_packet_decsriptor  */
			iso_packet->status =
			    sts.b_iso_out.rxsts +
			    (sts.b_iso_out.bs ^ BS_DMA_DONE);
			if (iso_packet->status) {
				iso_packet->status = -DWC_E_NO_DATA;
			}

			/* Received data length */
			iso_packet->length =
			    dwc_ep->data_per_frame - sts.b_iso_out.rxbytes;

			iso_packet->offset = offset;

			offset += data_per_desc;
			iso_packet++;
			dma_desc++;
		}

		sts.d32 = dma_desc->status.d32;

		/* Write status in iso_packet_decsriptor  */
		iso_packet->status =
		    sts.b_iso_out.rxsts + (sts.b_iso_out.bs ^ BS_DMA_DONE);
		if (iso_packet->status) {
			iso_packet->status = -DWC_E_NO_DATA;
		}
		/* Received data length */
		if (!sts.b_iso_out.rxbytes) {
			iso_packet->length =
			    dwc_ep->data_per_frame - sts.b_iso_out.rxbytes;
		} else {
			iso_packet->length =
			    dwc_ep->data_per_frame - sts.b_iso_out.rxbytes +
			    (4 - dwc_ep->data_per_frame % 4);
		}

		iso_packet->offset = offset;
	} else {
/** ISO IN EP */

		dma_desc =
		    dwc_ep->iso_desc_addr +
		    dwc_ep->desc_cnt*dwc_ep->proc_buf_num;

		for (i = 0; i < dwc_ep->desc_cnt - 1; i++) {
			sts.d32 = dma_desc->status.d32;

			/* Write status in iso packet descriptor */
			iso_packet->status =
			    sts.b_iso_in.txsts +
			    (sts.b_iso_in.bs ^ BS_DMA_DONE);
			if (iso_packet->status != 0) {
				iso_packet->status = -DWC_E_NO_DATA;

			}
			/* Bytes has been transfered */
			iso_packet->length =
			    dwc_ep->data_per_frame - sts.b_iso_in.txbytes;

			dma_desc++;
			iso_packet++;
		}

		sts.d32 = dma_desc->status.d32;
		while (sts.b_iso_in.bs == BS_DMA_BUSY) {
			sts.d32 = dma_desc->status.d32;
		}

		/* Write status in iso packet descriptor ??? do be done with ERROR codes */
		iso_packet->status =
		    sts.b_iso_in.txsts + (sts.b_iso_in.bs ^ BS_DMA_DONE);
		if (iso_packet->status != 0) {
			iso_packet->status = -DWC_E_NO_DATA;
		}

		/* Bytes has been transfered */
		iso_packet->length =
		    dwc_ep->data_per_frame - sts.b_iso_in.txbytes;
	}
}

/**
 * This function reinitialize DMA Descriptors for Isochronous transfer
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param dwc_ep The EP to start the transfer on.
 *
 */
static void reinit_ddma_iso_xfer(dwc_otg_core_if_t *core_if, dwc_ep_t *dwc_ep)
{
	int i, j;
	dwc_otg_dev_dma_desc_t *dma_desc;
	dma_addr_t dma_ad;
	volatile uint32_t *addr;
	dev_dma_desc_sts_t sts = {
	.d32 = 0};
	uint32_t data_per_desc;

	if (dwc_ep->is_in == 0) {
		addr = &core_if->dev_if->out_ep_regs[dwc_ep->num]->doepctl;
	} else {
		addr = &core_if->dev_if->in_ep_regs[dwc_ep->num]->diepctl;
	}

	if (dwc_ep->proc_buf_num == 0) {
		/** Buffer 0 descriptors setup */
		dma_ad = dwc_ep->dma_addr0;
	} else {
		/** Buffer 1 descriptors setup */
		dma_ad = dwc_ep->dma_addr1;
	}

	/** Reinit closed DMA Descriptors*/
	/** ISO OUT EP */
	if (dwc_ep->is_in == 0) {
		dma_desc =
		    dwc_ep->iso_desc_addr +
		    dwc_ep->desc_cnt*dwc_ep->proc_buf_num;

		sts.b_iso_out.bs = BS_HOST_READY;
		sts.b_iso_out.rxsts = 0;
		sts.b_iso_out.l = 0;
		sts.b_iso_out.sp = 0;
		sts.b_iso_out.ioc = 0;
		sts.b_iso_out.pid = 0;
		sts.b_iso_out.framenum = 0;

		for (i = 0; i < dwc_ep->desc_cnt - dwc_ep->pkt_per_frm;
		     i += dwc_ep->pkt_per_frm) {
			for (j = 0; j < dwc_ep->pkt_per_frm; ++j) {
				data_per_desc =
				    ((j + 1)*dwc_ep->maxpacket >
				     dwc_ep->
				     data_per_frame) ? dwc_ep->data_per_frame -
				    j*dwc_ep->maxpacket : dwc_ep->maxpacket;
				data_per_desc +=
				    (data_per_desc % 4) ? (4 -
							   data_per_desc %
							   4) : 0;
				sts.b_iso_out.rxbytes = data_per_desc;
				dma_desc->buf = dma_ad;
				dma_desc->status.d32 = sts.d32;

				dma_ad += data_per_desc;
				dma_desc++;
			}
		}

		for (j = 0; j < dwc_ep->pkt_per_frm - 1; ++j) {

			data_per_desc =
			    ((j + 1)*dwc_ep->maxpacket >
			     dwc_ep->data_per_frame) ? dwc_ep->data_per_frame -
			    j*dwc_ep->maxpacket : dwc_ep->maxpacket;
			data_per_desc +=
			    (data_per_desc % 4) ? (4 - data_per_desc % 4) : 0;
			sts.b_iso_out.rxbytes = data_per_desc;

			dma_desc->buf = dma_ad;
			dma_desc->status.d32 = sts.d32;

			dma_desc++;
			dma_ad += data_per_desc;
		}

		sts.b_iso_out.ioc = 1;
		sts.b_iso_out.l = dwc_ep->proc_buf_num;

		data_per_desc =
		    ((j + 1)*dwc_ep->maxpacket >
		     dwc_ep->data_per_frame) ? dwc_ep->data_per_frame -
		    j*dwc_ep->maxpacket : dwc_ep->maxpacket;
		data_per_desc +=
		    (data_per_desc % 4) ? (4 - data_per_desc % 4) : 0;
		sts.b_iso_out.rxbytes = data_per_desc;

		dma_desc->buf = dma_ad;
		dma_desc->status.d32 = sts.d32;
	} else {
/** ISO IN EP */

		dma_desc =
		    dwc_ep->iso_desc_addr +
		    dwc_ep->desc_cnt*dwc_ep->proc_buf_num;

		sts.b_iso_in.bs = BS_HOST_READY;
		sts.b_iso_in.txsts = 0;
		sts.b_iso_in.sp = 0;
		sts.b_iso_in.ioc = 0;
		sts.b_iso_in.pid = dwc_ep->pkt_per_frm;
		sts.b_iso_in.framenum = dwc_ep->next_frame;
		sts.b_iso_in.txbytes = dwc_ep->data_per_frame;
		sts.b_iso_in.l = 0;

		for (i = 0; i < dwc_ep->desc_cnt - 1; i++) {
			dma_desc->buf = dma_ad;
			dma_desc->status.d32 = sts.d32;

			sts.b_iso_in.framenum += dwc_ep->bInterval;
			dma_ad += dwc_ep->data_per_frame;
			dma_desc++;
		}

		sts.b_iso_in.ioc = 1;
		sts.b_iso_in.l = dwc_ep->proc_buf_num;

		dma_desc->buf = dma_ad;
		dma_desc->status.d32 = sts.d32;

		dwc_ep->next_frame =
		    sts.b_iso_in.framenum + dwc_ep->bInterval * 1;
	}
	dwc_ep->proc_buf_num = (dwc_ep->proc_buf_num ^ 1) & 0x1;
}

/**
 * This function is to handle Iso EP transfer complete interrupt
 * in case Iso out packet was dropped
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param dwc_ep The EP for wihich transfer complete was asserted
 *
 */
static uint32_t handle_iso_out_pkt_dropped(dwc_otg_core_if_t *core_if,
					   dwc_ep_t *dwc_ep) {
	uint32_t dma_addr;
	uint32_t drp_pkt;
	uint32_t drp_pkt_cnt;
	deptsiz_data_t deptsiz = {
	.d32 = 0};
	depctl_data_t depctl = {
	.d32 = 0};
	int i;

	deptsiz.d32 =
	    DWC_READ_REG32(&core_if->dev_if->
			   out_ep_regs[dwc_ep->num]->doeptsiz);

	drp_pkt = dwc_ep->pkt_cnt - deptsiz.b.pktcnt;
	drp_pkt_cnt = dwc_ep->pkt_per_frm - (drp_pkt % dwc_ep->pkt_per_frm);

	/* Setting dropped packets status */
	for (i = 0; i < drp_pkt_cnt; ++i) {
		dwc_ep->pkt_info[drp_pkt].status = -DWC_E_NO_DATA;
		drp_pkt++;
		deptsiz.b.pktcnt--;
	}

	if (deptsiz.b.pktcnt > 0) {
		deptsiz.b.xfersize =
		    dwc_ep->xfer_len - (dwc_ep->pkt_cnt -
					deptsiz.b.pktcnt)*dwc_ep->maxpacket;
	} else {
		deptsiz.b.xfersize = 0;
		deptsiz.b.pktcnt = 0;
	}

	DWC_WRITE_REG32(&core_if->dev_if->out_ep_regs[dwc_ep->num]->doeptsiz,
			deptsiz.d32);

	if (deptsiz.b.pktcnt > 0) {
		if (dwc_ep->proc_buf_num) {
			dma_addr =
			    dwc_ep->dma_addr1 + dwc_ep->xfer_len -
			    deptsiz.b.xfersize;
		} else {
			dma_addr =
			    dwc_ep->dma_addr0 + dwc_ep->xfer_len -
			    deptsiz.b.xfersize;;
		}

		DWC_WRITE_REG32(&core_if->dev_if->
				out_ep_regs[dwc_ep->num]->doepdma, dma_addr);

		/** Re-enable endpoint, clear nak  */
		depctl.d32 = 0;
		depctl.b.epena = 1;
		depctl.b.cnak = 1;

		DWC_MODIFY_REG32(&core_if->dev_if->
				 out_ep_regs[dwc_ep->num]->doepctl, depctl.d32,
				 depctl.d32);
		return 0;
	} else {
		return 1;
	}
}

/**
 * This function sets iso packets information(PTI mode)
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param ep The EP to start the transfer on.
 *
 */
static uint32_t set_iso_pkts_info(dwc_otg_core_if_t *core_if, dwc_ep_t *ep)
{
	int i, j;
	dma_addr_t dma_ad;
	iso_pkt_info_t *packet_info = ep->pkt_info;
	uint32_t offset;
	uint32_t frame_data;
	deptsiz_data_t deptsiz;

	if (ep->proc_buf_num == 0) {
		/** Buffer 0 descriptors setup */
		dma_ad = ep->dma_addr0;
	} else {
		/** Buffer 1 descriptors setup */
		dma_ad = ep->dma_addr1;
	}

	if (ep->is_in) {
		deptsiz.d32 =
		    DWC_READ_REG32(&core_if->dev_if->in_ep_regs[ep->num]->
				   dieptsiz);
	} else {
		deptsiz.d32 =
		    DWC_READ_REG32(&core_if->dev_if->out_ep_regs[ep->num]->
				   doeptsiz);
	}

	if (!deptsiz.b.xfersize) {
		offset = 0;
		for (i = 0; i < ep->pkt_cnt; i += ep->pkt_per_frm) {
			frame_data = ep->data_per_frame;
			for (j = 0; j < ep->pkt_per_frm; ++j) {

				/* Packet status - is not set as initially
				 * it is set to 0 and if packet was sent
				 successfully, status field will remain 0*/

				/* Bytes has been transfered */
				packet_info->length =
				    (ep->maxpacket <
				     frame_data) ? ep->maxpacket : frame_data;

				/* Received packet offset */
				packet_info->offset = offset;
				offset += packet_info->length;
				frame_data -= packet_info->length;

				packet_info++;
			}
		}
		return 1;
	} else {
		/* This is a workaround for in case of Transfer Complete with
		 * PktDrpSts interrupts merging - in this case Transfer complete
		 * interrupt for Isoc Out Endpoint is asserted without PktDrpSts
		 * set and with DOEPTSIZ register non zero. Investigations showed,
		 * that this happens when Out packet is dropped, but because of
		 * interrupts merging during first interrupt handling PktDrpSts
		 * bit is cleared and for next merged interrupts it is not reset.
		 * In this case SW hadles the interrupt as if PktDrpSts bit is set.
		 */
		if (ep->is_in) {
			return 1;
		} else {
			return handle_iso_out_pkt_dropped(core_if, ep);
		}
	}
}

/**
 * This function is to handle Iso EP transfer complete interrupt
 *
 * @param pcd The PCD
 * @param ep The EP for which transfer complete was asserted
 *
 */
static void complete_iso_ep(dwc_otg_pcd_t *pcd, dwc_otg_pcd_ep_t *ep)
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(ep->pcd);
	dwc_ep_t *dwc_ep = &ep->dwc_ep;
	uint8_t is_last = 0;

	if (ep->dwc_ep.next_frame == 0xffffffff) {
		DWC_WARN("Next frame is not set!\n");
		return;
	}

	if (core_if->dma_enable) {
		if (core_if->dma_desc_enable) {
			set_ddma_iso_pkts_info(core_if, dwc_ep);
			reinit_ddma_iso_xfer(core_if, dwc_ep);
			is_last = 1;
		} else {
			if (core_if->pti_enh_enable) {
				if (set_iso_pkts_info(core_if, dwc_ep)) {
					dwc_ep->proc_buf_num =
					    (dwc_ep->proc_buf_num ^ 1) & 0x1;
					dwc_otg_iso_ep_start_buf_transfer
					    (core_if, dwc_ep);
					is_last = 1;
				}
			} else {
				set_current_pkt_info(core_if, dwc_ep);
				if (dwc_ep->cur_pkt >= dwc_ep->pkt_cnt) {
					is_last = 1;
					dwc_ep->cur_pkt = 0;
					dwc_ep->proc_buf_num =
					    (dwc_ep->proc_buf_num ^ 1) & 0x1;
					if (dwc_ep->proc_buf_num) {
						dwc_ep->cur_pkt_addr =
						    dwc_ep->xfer_buff1;
						dwc_ep->cur_pkt_dma_addr =
						    dwc_ep->dma_addr1;
					} else {
						dwc_ep->cur_pkt_addr =
						    dwc_ep->xfer_buff0;
						dwc_ep->cur_pkt_dma_addr =
						    dwc_ep->dma_addr0;
					}

				}
				dwc_otg_iso_ep_start_frm_transfer(core_if,
								  dwc_ep);
			}
		}
	} else {
		set_current_pkt_info(core_if, dwc_ep);
		if (dwc_ep->cur_pkt >= dwc_ep->pkt_cnt) {
			is_last = 1;
			dwc_ep->cur_pkt = 0;
			dwc_ep->proc_buf_num = (dwc_ep->proc_buf_num ^ 1) & 0x1;
			if (dwc_ep->proc_buf_num) {
				dwc_ep->cur_pkt_addr = dwc_ep->xfer_buff1;
				dwc_ep->cur_pkt_dma_addr = dwc_ep->dma_addr1;
			} else {
				dwc_ep->cur_pkt_addr = dwc_ep->xfer_buff0;
				dwc_ep->cur_pkt_dma_addr = dwc_ep->dma_addr0;
			}

		}
		dwc_otg_iso_ep_start_frm_transfer(core_if, dwc_ep);
	}
	if (is_last)
		dwc_otg_iso_buffer_done(pcd, ep, ep->iso_req_handle);
}
#endif /* DWC_EN_ISOC */

/**
 * This function handle BNA interrupt for Non Isochronous EPs
 *
 */
static void dwc_otg_pcd_handle_noniso_bna(dwc_otg_pcd_ep_t *ep)
{
	dwc_ep_t *dwc_ep = &ep->dwc_ep;
	volatile uint32_t *addr;
	depctl_data_t depctl = {
	.d32 = 0};
	dwc_otg_pcd_t *pcd = ep->pcd;
	dwc_otg_dev_dma_desc_t *dma_desc;
	dev_dma_desc_sts_t sts = {
	.d32 = 0};
	dwc_otg_core_if_t *core_if = ep->pcd->core_if;
	int i, start;

	if (!dwc_ep->desc_cnt)
		DWC_WARN("Ep%d %s Descriptor count = %d \n", dwc_ep->num,
			 (dwc_ep->is_in ? "IN" : "OUT"), dwc_ep->desc_cnt);

	if (core_if->core_params->cont_on_bna && !dwc_ep->is_in
	    && dwc_ep->type != DWC_OTG_EP_TYPE_CONTROL) {
		uint32_t doepdma;
		dwc_otg_dev_out_ep_regs_t *out_regs =
		    core_if->dev_if->out_ep_regs[dwc_ep->num];
		doepdma = DWC_READ_REG32(&(out_regs->doepdma));
		start =
		    (doepdma -
		     dwc_ep->dma_desc_addr) / sizeof(dwc_otg_dev_dma_desc_t);
		dma_desc = &(dwc_ep->desc_addr[start]);
	} else {
		start = 0;
		dma_desc = dwc_ep->desc_addr;
	}

	for (i = start; i < dwc_ep->desc_cnt; ++i, ++dma_desc) {
		sts.d32 = dma_desc->status.d32;
		sts.b.bs = BS_HOST_READY;
		dma_desc->status.d32 = sts.d32;
	}

	if (dwc_ep->is_in == 0) {
		addr =
		    &GET_CORE_IF(pcd)->dev_if->out_ep_regs[dwc_ep->num]->
		    doepctl;
	} else {
		addr =
		    &GET_CORE_IF(pcd)->dev_if->in_ep_regs[dwc_ep->num]->diepctl;
	}
	depctl.b.epena = 1;
	depctl.b.cnak = 1;
	DWC_MODIFY_REG32(addr, 0, depctl.d32);
}

/**
 * This function handles EP0 Control transfers.
 *
 * The state of the control transfers are tracked in
 * <code>ep0state</code>.
 */
static void handle_ep0(dwc_otg_pcd_t *pcd)
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(pcd);
	dwc_otg_pcd_ep_t *ep0 = &pcd->ep0;
	dev_dma_desc_sts_t desc_sts;
	deptsiz0_data_t deptsiz;
	uint32_t byte_count;

#ifdef DEBUG_EP0
	DWC_DEBUGPL(DBG_PCDV, "%s()\n", __func__);
	print_ep0_state(pcd);
#endif

	switch (pcd->ep0state) {
	case EP0_DISCONNECT:
		break;

	case EP0_IDLE:
		pcd->request_config = 0;

		pcd_setup(pcd);
		break;

	case EP0_IN_DATA_PHASE:
#ifdef DEBUG_EP0
		DWC_DEBUGPL(DBG_PCD, "DATA_IN EP%d-%s: type=%d, mps=%d\n",
			    ep0->dwc_ep.num, (ep0->dwc_ep.is_in ? "IN" : "OUT"),
			    ep0->dwc_ep.type, ep0->dwc_ep.maxpacket);
#endif

		if (core_if->dma_enable != 0) {
			/*
			 * For EP0 we can only program 1 packet at a time so we
			 * need to do the make calculations after each complete.
			 * Call write_packet to make the calculations, as in
			 * slave mode, and use those values to determine if we
			 * can complete.
			 */
			if (core_if->dma_desc_enable == 0) {
				deptsiz.d32 =
				    DWC_READ_REG32(&core_if->
						   dev_if->in_ep_regs[0]->
						   dieptsiz);
				byte_count =
				    ep0->dwc_ep.xfer_len - deptsiz.b.xfersize;
			} else {
				desc_sts =
				    core_if->dev_if->in_desc_addr->status;
				byte_count =
				    ep0->dwc_ep.xfer_len - desc_sts.b.bytes;
			}
			ep0->dwc_ep.xfer_count += byte_count;
			ep0->dwc_ep.xfer_buff += byte_count;
			ep0->dwc_ep.dma_addr += byte_count;
		}
		if (ep0->dwc_ep.xfer_count < ep0->dwc_ep.total_len) {
			dwc_otg_ep0_continue_transfer(GET_CORE_IF(pcd),
						      &ep0->dwc_ep);
			DWC_DEBUGPL(DBG_PCD, "CONTINUE TRANSFER\n");
		} else if (ep0->dwc_ep.sent_zlp) {
			dwc_otg_ep0_continue_transfer(GET_CORE_IF(pcd),
						      &ep0->dwc_ep);
			ep0->dwc_ep.sent_zlp = 0;
			DWC_DEBUGPL(DBG_PCD, "CONTINUE TRANSFER sent zlp\n");
		} else {
			ep0_complete_request(ep0);
			DWC_DEBUGPL(DBG_PCD, "COMPLETE TRANSFER\n");
		}
		break;
	case EP0_OUT_DATA_PHASE:
#ifdef DEBUG_EP0
		DWC_DEBUGPL(DBG_PCD, "DATA_OUT EP%d-%s: type=%d, mps=%d\n",
			    ep0->dwc_ep.num, (ep0->dwc_ep.is_in ? "IN" : "OUT"),
			    ep0->dwc_ep.type, ep0->dwc_ep.maxpacket);
#endif
		if (core_if->dma_enable != 0) {
			if (core_if->dma_desc_enable == 0) {
				deptsiz.d32 =
				    DWC_READ_REG32(&core_if->
						   dev_if->out_ep_regs[0]->
						   doeptsiz);
				byte_count =
				    ep0->dwc_ep.maxpacket - deptsiz.b.xfersize;
			} else {
				desc_sts =
				    core_if->dev_if->out_desc_addr->status;
				byte_count =
				    ep0->dwc_ep.maxpacket - desc_sts.b.bytes;
			}
			ep0->dwc_ep.xfer_count += byte_count;
			ep0->dwc_ep.xfer_buff += byte_count;
			ep0->dwc_ep.dma_addr += byte_count;
		}
		if (ep0->dwc_ep.xfer_count < ep0->dwc_ep.total_len) {
			dwc_otg_ep0_continue_transfer(GET_CORE_IF(pcd),
						      &ep0->dwc_ep);
			DWC_DEBUGPL(DBG_PCD, "CONTINUE TRANSFER\n");
		} else if (ep0->dwc_ep.sent_zlp) {
			dwc_otg_ep0_continue_transfer(GET_CORE_IF(pcd),
						      &ep0->dwc_ep);
			ep0->dwc_ep.sent_zlp = 0;
			DWC_DEBUGPL(DBG_PCD, "CONTINUE TRANSFER sent zlp\n");
		} else {
			ep0_complete_request(ep0);
			DWC_DEBUGPL(DBG_PCD, "COMPLETE TRANSFER\n");
		}
		break;

	case EP0_IN_STATUS_PHASE:
	case EP0_OUT_STATUS_PHASE:
		DWC_DEBUGPL(DBG_PCD, "CASE: EP0_STATUS\n");
		ep0_complete_request(ep0);
		pcd->ep0state = EP0_IDLE;
		ep0->stopped = 1;
		ep0->dwc_ep.is_in = 0;	/* OUT for next SETUP */

		/* Prepare for more SETUP Packets */
		if (core_if->dma_enable) {
			ep0_out_start(core_if, pcd);
		}
		break;

	case EP0_STALL:
		DWC_ERROR("EP0 STALLed, should not get here pcd_setup()\n");
		break;
	}
#ifdef DEBUG_EP0
	print_ep0_state(pcd);
#endif
}

/**
 * Restart transfer
 */
static void restart_transfer(dwc_otg_pcd_t *pcd, const uint32_t epnum)
{
	dwc_otg_core_if_t *core_if;
	dwc_otg_dev_if_t *dev_if;
	deptsiz_data_t dieptsiz = {
	.d32 = 0};
	dwc_otg_pcd_ep_t *ep;

	ep = get_in_ep(pcd, epnum);

#ifdef DWC_EN_ISOC
	if (ep->dwc_ep.type == DWC_OTG_EP_TYPE_ISOC) {
		return;
	}
#endif /* DWC_EN_ISOC  */

	core_if = GET_CORE_IF(pcd);
	dev_if = core_if->dev_if;

	dieptsiz.d32 = DWC_READ_REG32(&dev_if->in_ep_regs[epnum]->dieptsiz);

	DWC_DEBUGPL(DBG_PCD, "xfer_buff=%p xfer_count=%0x xfer_len=%0x"
		    " stopped=%d\n", ep->dwc_ep.xfer_buff,
		    ep->dwc_ep.xfer_count, ep->dwc_ep.xfer_len, ep->stopped);
	/*
	 * If xfersize is 0 and pktcnt in not 0, resend the last packet.
	 */
	if (dieptsiz.b.pktcnt && dieptsiz.b.xfersize == 0 &&
	    ep->dwc_ep.start_xfer_buff != 0) {
		if (ep->dwc_ep.total_len <= ep->dwc_ep.maxpacket) {
			ep->dwc_ep.xfer_count = 0;
			ep->dwc_ep.xfer_buff = ep->dwc_ep.start_xfer_buff;
			ep->dwc_ep.xfer_len = ep->dwc_ep.xfer_count;
		} else {
			ep->dwc_ep.xfer_count -= ep->dwc_ep.maxpacket;
			/* convert packet size to dwords. */
			ep->dwc_ep.xfer_buff -= ep->dwc_ep.maxpacket;
			ep->dwc_ep.xfer_len = ep->dwc_ep.xfer_count;
		}
		ep->stopped = 0;
		DWC_DEBUGPL(DBG_PCD, "xfer_buff=%p xfer_count=%0x "
			    "xfer_len=%0x stopped=%d\n",
			    ep->dwc_ep.xfer_buff,
			    ep->dwc_ep.xfer_count, ep->dwc_ep.xfer_len,
			    ep->stopped);
		if (epnum == 0) {
			dwc_otg_ep0_start_transfer(core_if, &ep->dwc_ep);
		} else {
			dwc_otg_ep_start_transfer(core_if, &ep->dwc_ep);
		}
	}
}

/*
 * This function create new nextep sequnce based on Learn Queue.
 *
 * @param core_if Programming view of DWC_otg controller
 */
void predict_nextep_seq(dwc_otg_core_if_t *core_if)
{
	dwc_otg_device_global_regs_t *dev_global_regs =
	    core_if->dev_if->dev_global_regs;
	const uint32_t TOKEN_Q_DEPTH = core_if->hwcfg2.b.dev_token_q_depth;
	/* Number of Token Queue Registers */
	const int DTKNQ_REG_CNT = (TOKEN_Q_DEPTH + 7) / 8;
	dtknq1_data_t dtknqr1;
	uint32_t in_tkn_epnums[4];
	uint8_t seqnum[MAX_EPS_CHANNELS];
	uint8_t intkn_seq[TOKEN_Q_DEPTH];
	grstctl_t resetctl = {
	.d32 = 0};
	uint8_t temp;
	int ndx = 0;
	int start = 0;
	int end = 0;
	int sort_done = 0;
	int i = 0;
	volatile uint32_t *addr = &dev_global_regs->dtknqr1;

	DWC_DEBUGPL(DBG_PCD, "dev_token_q_depth=%d\n", TOKEN_Q_DEPTH);

	/* Read the DTKNQ Registers */
	for (i = 0; i < DTKNQ_REG_CNT; i++) {
		in_tkn_epnums[i] = DWC_READ_REG32(addr);
		DWC_DEBUGPL(DBG_PCDV, "DTKNQR%d=0x%08x\n", i + 1,
			    in_tkn_epnums[i]);
		if (addr == &dev_global_regs->dvbusdis) {
			addr = &dev_global_regs->dtknqr3_dthrctl;
		} else {
			++addr;
		}

	}

	/* Copy the DTKNQR1 data to the bit field. */
	dtknqr1.d32 = in_tkn_epnums[0];
	if (dtknqr1.b.wrap_bit) {
		ndx = dtknqr1.b.intknwptr;
		end = ndx - 1;
		if (end < 0)
			end = TOKEN_Q_DEPTH - 1;
	} else {
		ndx = 0;
		end = dtknqr1.b.intknwptr - 1;
		if (end < 0)
			end = 0;
	}
	start = ndx;

	/* Fill seqnum[] by initial values: EP number + 31 */
	for (i = 0; i <= core_if->dev_if->num_in_eps; i++) {
		seqnum[i] = i + 31;
	}

	/* Fill intkn_seq[] from in_tkn_epnums[0] */
	for (i = 0; i < 6; i++)
		intkn_seq[i] = (in_tkn_epnums[0] >> ((7 - i) * 4)) & 0xf;

	if (TOKEN_Q_DEPTH > 6) {
		/* Fill intkn_seq[] from in_tkn_epnums[1] */
		for (i = 6; i < 14; i++)
			intkn_seq[i] =
			    (in_tkn_epnums[1] >> ((7 - (i - 6)) * 4)) & 0xf;
	}

	if (TOKEN_Q_DEPTH > 14) {
		/* Fill intkn_seq[] from in_tkn_epnums[1] */
		for (i = 14; i < 22; i++)
			intkn_seq[i] =
			    (in_tkn_epnums[2] >> ((7 - (i - 14)) * 4)) & 0xf;
	}

	if (TOKEN_Q_DEPTH > 22) {
		/* Fill intkn_seq[] from in_tkn_epnums[1] */
		for (i = 22; i < 30; i++)
			intkn_seq[i] =
			    (in_tkn_epnums[3] >> ((7 - (i - 22)) * 4)) & 0xf;
	}

	DWC_DEBUGPL(DBG_PCDV, "%s start=%d end=%d intkn_seq[]:\n", __func__,
		    start, end);
	for (i = 0; i < TOKEN_Q_DEPTH; i++)
		DWC_DEBUGPL(DBG_PCDV, "%d\n", intkn_seq[i]);

	/* Update seqnum based on intkn_seq[] */
	i = 0;
	do {
		seqnum[intkn_seq[ndx]] = i;
		ndx++;
		i++;
		if (ndx == TOKEN_Q_DEPTH)
			ndx = 0;
	} while (i < TOKEN_Q_DEPTH);

	/* Mark non active EP's in seqnum[] by 0xff */
	for (i = 0; i <= core_if->dev_if->num_in_eps; i++) {
		if (core_if->nextep_seq[i] == 0xff)
			seqnum[i] = 0xff;
	}

	/* Sort seqnum[] */
	sort_done = 0;
	while (!sort_done) {
		sort_done = 1;
		for (i = 0; i < core_if->dev_if->num_in_eps; i++) {
			if (seqnum[i] > seqnum[i + 1]) {
				temp = seqnum[i];
				seqnum[i] = seqnum[i + 1];
				seqnum[i + 1] = temp;
				sort_done = 0;
			}
		}
	}

	ndx = start + seqnum[0];
	if (ndx >= TOKEN_Q_DEPTH)
		ndx = ndx % TOKEN_Q_DEPTH;
	core_if->first_in_nextep_seq = intkn_seq[ndx];

	/* Update seqnum[] by EP numbers  */
	for (i = 0; i <= core_if->dev_if->num_in_eps; i++) {
		ndx = start + i;
		if (seqnum[i] < 31) {
			ndx = start + seqnum[i];
			if (ndx >= TOKEN_Q_DEPTH)
				ndx = ndx % TOKEN_Q_DEPTH;
			seqnum[i] = intkn_seq[ndx];
		} else {
			if (seqnum[i] < 0xff) {
				seqnum[i] = seqnum[i] - 31;
			} else {
				break;
			}
		}
	}

	/* Update nextep_seq[] based on seqnum[] */
	for (i = 0; i < core_if->dev_if->num_in_eps; i++) {
		if (seqnum[i] != 0xff) {
			if (seqnum[i + 1] != 0xff) {
				core_if->nextep_seq[seqnum[i]] = seqnum[i + 1];
			} else {
				core_if->nextep_seq[seqnum[i]] =
				    core_if->first_in_nextep_seq;
				break;
			}
		} else {
			break;
		}
	}

	DWC_DEBUGPL(DBG_PCDV, "%s first_in_nextep_seq= %2d; nextep_seq[]:\n",
		    __func__, core_if->first_in_nextep_seq);
	for (i = 0; i <= core_if->dev_if->num_in_eps; i++) {
		DWC_DEBUGPL(DBG_PCDV, "%2d\n", core_if->nextep_seq[i]);
	}

	/* Flush the Learning Queue */
	resetctl.d32 = DWC_READ_REG32(&core_if->core_global_regs->grstctl);
	resetctl.b.intknqflsh = 1;
	DWC_WRITE_REG32(&core_if->core_global_regs->grstctl, resetctl.d32);

}

/**
 * handle the IN EP disable interrupt.
 */
static inline void handle_in_ep_disable_intr(dwc_otg_pcd_t *pcd,
					     const uint32_t epnum)
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(pcd);
	dwc_otg_dev_if_t *dev_if = core_if->dev_if;
	deptsiz_data_t dieptsiz = {
	.d32 = 0};
	dctl_data_t dctl = {
	.d32 = 0};
	dwc_otg_pcd_ep_t *ep;
	dwc_ep_t *dwc_ep;
	gintmsk_data_t gintmsk_data;
	depctl_data_t depctl;
	uint32_t diepdma;
	uint32_t remain_to_transfer = 0;
	uint8_t i;
	uint32_t xfer_size;

	ep = get_in_ep(pcd, epnum);
	dwc_ep = &ep->dwc_ep;

	if (dwc_ep->type == DWC_OTG_EP_TYPE_ISOC) {
		dwc_otg_flush_tx_fifo(core_if, dwc_ep->tx_fifo_num);
		complete_ep(ep);
		return;
	}

	DWC_DEBUGPL(DBG_PCD, "diepctl%d=%0x\n", epnum,
		    DWC_READ_REG32(&dev_if->in_ep_regs[epnum]->diepctl));
	dieptsiz.d32 = DWC_READ_REG32(&dev_if->in_ep_regs[epnum]->dieptsiz);
	depctl.d32 = DWC_READ_REG32(&dev_if->in_ep_regs[epnum]->diepctl);

	DWC_DEBUGPL(DBG_ANY, "pktcnt=%d size=%d\n",
		    dieptsiz.b.pktcnt, dieptsiz.b.xfersize);

	if ((core_if->start_predict == 0) || (depctl.b.eptype & 1)) {
		if (ep->stopped) {
			if (core_if->en_multiple_tx_fifo)
				/* Flush the Tx FIFO */
				dwc_otg_flush_tx_fifo(core_if,
						      dwc_ep->tx_fifo_num);
			/* Clear the Global IN NP NAK */
			dctl.d32 = 0;
			dctl.b.cgnpinnak = 1;
			DWC_MODIFY_REG32(&dev_if->dev_global_regs->dctl,
					 dctl.d32, dctl.d32);
			/* Restart the transaction */
			if (dieptsiz.b.pktcnt != 0 || dieptsiz.b.xfersize != 0) {
				restart_transfer(pcd, epnum);
			}
		} else {
			/* Restart the transaction */
			if (dieptsiz.b.pktcnt != 0 || dieptsiz.b.xfersize != 0) {
				restart_transfer(pcd, epnum);
			}
			DWC_DEBUGPL(DBG_ANY, "STOPPED!!!\n");
		}
		return;
	}

	if (core_if->start_predict > 2) {
		/* NP IN EP */
		core_if->start_predict--;
		return;
	}

	core_if->start_predict--;

	if (core_if->start_predict == 1) {
		/* All NP IN Ep's disabled now */
		predict_nextep_seq(core_if);

		/* Update all active IN EP's NextEP field based of nextep_seq[] */
		for (i = 0; i <= core_if->dev_if->num_in_eps; i++) {
			depctl.d32 =
			    DWC_READ_REG32(&dev_if->in_ep_regs[i]->diepctl);
			if (core_if->nextep_seq[i] != 0xff) {
				/* Active NP IN EP */
				depctl.b.nextep = core_if->nextep_seq[i];
				DWC_WRITE_REG32(&dev_if->in_ep_regs[i]->diepctl,
						depctl.d32);
			}
		}
		/* Flush Shared NP TxFIFO */
		dwc_otg_flush_tx_fifo(core_if, 0);
		/* Rewind buffers */
		if (!core_if->dma_desc_enable) {
			i = core_if->first_in_nextep_seq;
			do {
				ep = get_in_ep(pcd, i);
				dieptsiz.d32 =
				    DWC_READ_REG32(&dev_if->
						   in_ep_regs[i]->dieptsiz);
				xfer_size =
				    ep->dwc_ep.total_len -
				    ep->dwc_ep.xfer_count;
				if (xfer_size > ep->dwc_ep.maxxfer)
					xfer_size = ep->dwc_ep.maxxfer;
				depctl.d32 =
				    DWC_READ_REG32(&dev_if->
						   in_ep_regs[i]->diepctl);
				if (dieptsiz.b.pktcnt != 0) {
					if (xfer_size == 0) {
						remain_to_transfer = 0;
					} else {
						if ((xfer_size %
						     ep->dwc_ep.maxpacket) ==
						    0) {
							remain_to_transfer =
							    dieptsiz.b.pktcnt *
							    ep->
							    dwc_ep.maxpacket;
						} else {
							remain_to_transfer =
							    ((dieptsiz.
							      b.pktcnt -
							      1) *
							     ep->
							     dwc_ep.maxpacket)
							    +
							    (xfer_size %
							     ep->
							     dwc_ep.maxpacket);
						}
					}
					diepdma =
					    DWC_READ_REG32(&dev_if->in_ep_regs
							   [i]->diepdma);
					dieptsiz.b.xfersize =
					    remain_to_transfer;
					DWC_WRITE_REG32(&dev_if->
							in_ep_regs[i]->dieptsiz,
							dieptsiz.d32);
					diepdma =
					    ep->dwc_ep.dma_addr + (xfer_size -
								   remain_to_transfer);
					DWC_WRITE_REG32(&dev_if->
							in_ep_regs[i]->diepdma,
							diepdma);
				}
				i = core_if->nextep_seq[i];
			} while (i != core_if->first_in_nextep_seq);
		} else {	/* dma_desc_enable */
			DWC_PRINTF("%s Learning Queue not supported in DDMA\n",
				   __func__);
		}

		/* Restart transfers in predicted sequences */
		i = core_if->first_in_nextep_seq;
		do {
			dieptsiz.d32 =
			    DWC_READ_REG32(&dev_if->in_ep_regs[i]->dieptsiz);
			depctl.d32 =
			    DWC_READ_REG32(&dev_if->in_ep_regs[i]->diepctl);
			if (dieptsiz.b.pktcnt != 0) {
				depctl.d32 =
				    DWC_READ_REG32(&dev_if->
						   in_ep_regs[i]->diepctl);
				depctl.b.epena = 1;
				depctl.b.cnak = 1;
				DWC_WRITE_REG32(&dev_if->in_ep_regs[i]->diepctl,
						depctl.d32);
			}
			i = core_if->nextep_seq[i];
		} while (i != core_if->first_in_nextep_seq);

		/* Clear the global non-periodic IN NAK handshake */
		dctl.d32 = 0;
		dctl.b.cgnpinnak = 1;
		DWC_MODIFY_REG32(&dev_if->dev_global_regs->dctl, dctl.d32,
				 dctl.d32);

		/* Unmask EP Mismatch interrupt */
		gintmsk_data.d32 = 0;
		gintmsk_data.b.epmismatch = 1;
		DWC_MODIFY_REG32(&core_if->core_global_regs->gintmsk, 0,
				 gintmsk_data.d32);

		core_if->start_predict = 0;

	}
}

/**
 * Handler for the IN EP timeout handshake interrupt.
 */
static inline void handle_in_ep_timeout_intr(dwc_otg_pcd_t *pcd,
					     const uint32_t epnum)
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(pcd);
	dwc_otg_dev_if_t *dev_if = core_if->dev_if;

#ifdef DEBUG
	deptsiz_data_t dieptsiz = {
	.d32 = 0};
	uint32_t num = 0;
#endif
	dctl_data_t dctl = {
	.d32 = 0};
	dwc_otg_pcd_ep_t *ep;

	gintmsk_data_t intr_mask = {
	.d32 = 0};

	ep = get_in_ep(pcd, epnum);

	/* Disable the NP Tx Fifo Empty Interrrupt */
	if (!core_if->dma_enable) {
		intr_mask.b.nptxfempty = 1;
		DWC_MODIFY_REG32(&core_if->core_global_regs->gintmsk,
				 intr_mask.d32, 0);
	}
	/** @todo NGS Check EP type.
	 * Implement for Periodic EPs */
	/*
	 * Non-periodic EP
	 */
	/* Enable the Global IN NAK Effective Interrupt */
	intr_mask.b.ginnakeff = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gintmsk, 0, intr_mask.d32);

	/* Set Global IN NAK */
	dctl.b.sgnpinnak = 1;
	DWC_MODIFY_REG32(&dev_if->dev_global_regs->dctl, dctl.d32, dctl.d32);

	ep->stopped = 1;

#ifdef DEBUG
	dieptsiz.d32 = DWC_READ_REG32(&dev_if->in_ep_regs[num]->dieptsiz);
	DWC_DEBUGPL(DBG_ANY, "pktcnt=%d size=%d\n",
		    dieptsiz.b.pktcnt, dieptsiz.b.xfersize);
#endif

#ifdef DISABLE_PERIODIC_EP
	/*
	 * Set the NAK bit for this EP to
	 * start the disable process.
	 */
	diepctl.d32 = 0;
	diepctl.b.snak = 1;
	DWC_MODIFY_REG32(&dev_if->in_ep_regs[num]->diepctl, diepctl.d32,
			 diepctl.d32);
	ep->disabling = 1;
	ep->stopped = 1;
#endif
}

/**
 * Handler for the IN EP NAK interrupt.
 */
static inline int32_t handle_in_ep_nak_intr(dwc_otg_pcd_t *pcd,
					    const uint32_t epnum)
{
	/** @todo implement ISR */
	dwc_otg_core_if_t *core_if;
	diepmsk_data_t intr_mask = {
	.d32 = 0};

	DWC_PRINTF("INTERRUPT Handler not implemented for %s\n", "IN EP NAK");
	core_if = GET_CORE_IF(pcd);
	intr_mask.b.nak = 1;

	if (core_if->multiproc_int_enable) {
		DWC_MODIFY_REG32(&core_if->dev_if->dev_global_regs->
				 diepeachintmsk[epnum], intr_mask.d32, 0);
	} else {
		DWC_MODIFY_REG32(&core_if->dev_if->dev_global_regs->diepmsk,
				 intr_mask.d32, 0);
	}

	return 1;
}

/**
 * Handler for the OUT EP Babble interrupt.
 */
static inline int32_t handle_out_ep_babble_intr(dwc_otg_pcd_t *pcd,
						const uint32_t epnum)
{
	/** @todo implement ISR */
	dwc_otg_core_if_t *core_if;
	doepmsk_data_t intr_mask = {
	.d32 = 0};

	DWC_PRINTF("INTERRUPT Handler not implemented for %s\n",
		   "OUT EP Babble");
	core_if = GET_CORE_IF(pcd);
	intr_mask.b.babble = 1;

	if (core_if->multiproc_int_enable) {
		DWC_MODIFY_REG32(&core_if->dev_if->dev_global_regs->
				 doepeachintmsk[epnum], intr_mask.d32, 0);
	} else {
		DWC_MODIFY_REG32(&core_if->dev_if->dev_global_regs->doepmsk,
				 intr_mask.d32, 0);
	}

	return 1;
}

/**
 * Handler for the OUT EP NAK interrupt.
 */
static inline int32_t handle_out_ep_nak_intr(dwc_otg_pcd_t *pcd,
					     const uint32_t epnum)
{
	/** @todo implement ISR */
	dwc_otg_core_if_t *core_if;
	doepmsk_data_t intr_mask = {
	.d32 = 0};

	DWC_DEBUGPL(DBG_ANY, "INTERRUPT Handler not implemented for %s\n",
		    "OUT EP NAK");
	core_if = GET_CORE_IF(pcd);
	intr_mask.b.nak = 1;

	if (core_if->multiproc_int_enable) {
		DWC_MODIFY_REG32(&core_if->dev_if->dev_global_regs->
				 doepeachintmsk[epnum], intr_mask.d32, 0);
	} else {
		DWC_MODIFY_REG32(&core_if->dev_if->dev_global_regs->doepmsk,
				 intr_mask.d32, 0);
	}

	return 1;
}

/**
 * Handler for the OUT EP NYET interrupt.
 */
static inline int32_t handle_out_ep_nyet_intr(dwc_otg_pcd_t *pcd,
					      const uint32_t epnum)
{
	/** @todo implement ISR */
	dwc_otg_core_if_t *core_if;
	doepmsk_data_t intr_mask = {
	.d32 = 0};

	DWC_PRINTF("INTERRUPT Handler not implemented for %s\n", "OUT EP NYET");
	core_if = GET_CORE_IF(pcd);
	intr_mask.b.nyet = 1;

	if (core_if->multiproc_int_enable) {
		DWC_MODIFY_REG32(&core_if->dev_if->dev_global_regs->
				 doepeachintmsk[epnum], intr_mask.d32, 0);
	} else {
		DWC_MODIFY_REG32(&core_if->dev_if->dev_global_regs->doepmsk,
				 intr_mask.d32, 0);
	}

	return 1;
}

/**
 * This interrupt indicates that an IN EP has a pending Interrupt.
 * The sequence for handling the IN EP interrupt is shown below:
 * -#	Read the Device All Endpoint Interrupt register
 * -#	Repeat the following for each IN EP interrupt bit set (from
 *		LSB to MSB).
 * -#	Read the Device Endpoint Interrupt (DIEPINTn) register
 * -#	If "Transfer Complete" call the request complete function
 * -#	If "Endpoint Disabled" complete the EP disable procedure.
 * -#	If "AHB Error Interrupt" log error
 * -#	If "Time-out Handshake" log error
 * -#	If "IN Token Received when TxFIFO Empty" write packet to Tx
 *		FIFO.
 * -#	If "IN Token EP Mismatch" (disable, this is handled by EP
 *		Mismatch Interrupt)
 */
static int32_t dwc_otg_pcd_handle_in_ep_intr(dwc_otg_pcd_t *pcd)
{
#define CLEAR_IN_EP_INTR(__core_if, __epnum, __intr) \
do { \
		diepint_data_t diepint = {.d32 = 0}; \
		diepint.b.__intr = 1; \
		DWC_WRITE_REG32(&__core_if->dev_if->in_ep_regs[__epnum]->diepint, \
		diepint.d32); \
} while (0)

	dwc_otg_core_if_t *core_if = GET_CORE_IF(pcd);
	dwc_otg_dev_if_t *dev_if = core_if->dev_if;
	diepint_data_t diepint = {
	.d32 = 0};
	depctl_data_t depctl = {
	.d32 = 0};
	uint32_t ep_intr;
	uint32_t epnum = 0;
	dwc_otg_pcd_ep_t *ep;
	dwc_ep_t *dwc_ep;
	gintmsk_data_t intr_mask = {
	.d32 = 0};
	dctl_data_t dctl = {
	.d32 = 0};

	DWC_DEBUGPL(DBG_PCDV, "%s(%p)\n", __func__, pcd);

	/* Read in the device interrupt bits */
	ep_intr = dwc_otg_read_dev_all_in_ep_intr(core_if);

	/* Service the Device IN interrupts for each endpoint */
	while (ep_intr) {
		if (ep_intr & 0x1) {
			uint32_t empty_msk;
			/* Get EP pointer */
			ep = get_in_ep(pcd, epnum);
			dwc_ep = &ep->dwc_ep;

			depctl.d32 =
			    DWC_READ_REG32(&dev_if->in_ep_regs[epnum]->diepctl);
			empty_msk =
			    DWC_READ_REG32(&dev_if->
					   dev_global_regs->dtknqr4_fifoemptymsk);

			DWC_DEBUGPL(DBG_PCDV,
				    "IN EP INTERRUPT - %d\nepmty_msk - %8x  diepctl - %8x\n",
				    epnum, empty_msk, depctl.d32);

			DWC_DEBUGPL(DBG_PCD,
				    "EP%d-%s: type=%d, mps=%d\n",
				    dwc_ep->num, (dwc_ep->is_in ? "IN" : "OUT"),
				    dwc_ep->type, dwc_ep->maxpacket);

			diepint.d32 =
			    dwc_otg_read_dev_in_ep_intr(core_if, dwc_ep);

			DWC_DEBUGPL(DBG_PCDV,
				    "EP %d Interrupt Register - 0x%x\n", epnum,
				    diepint.d32);
			/* Transfer complete */
			if (diepint.b.xfercompl) {
				/* Disable the NP Tx FIFO Empty
				 * Interrupt */
				if (core_if->en_multiple_tx_fifo == 0) {
					intr_mask.b.nptxfempty = 1;
					DWC_MODIFY_REG32
					    (&core_if->
					     core_global_regs->gintmsk,
					     intr_mask.d32, 0);
				} else {
					/* Disable the Tx FIFO Empty Interrupt for this EP */
					uint32_t fifoemptymsk =
					    0x1 << dwc_ep->num;
					DWC_MODIFY_REG32(&core_if->
							 dev_if->dev_global_regs->dtknqr4_fifoemptymsk,
							 fifoemptymsk, 0);
				}
				/* Clear the bit in DIEPINTn for this interrupt */
				CLEAR_IN_EP_INTR(core_if, epnum, xfercompl);

				/* Complete the transfer */
				if (epnum == 0) {
					handle_ep0(pcd);
				}
#ifdef DWC_EN_ISOC
				else if (dwc_ep->type == DWC_OTG_EP_TYPE_ISOC) {
					if (!ep->stopped)
						complete_iso_ep(pcd, ep);
				}
#endif /* DWC_EN_ISOC */
#ifdef DWC_UTE_PER_IO
				else if (dwc_ep->type == DWC_OTG_EP_TYPE_ISOC) {
					if (!ep->stopped)
						complete_xiso_ep(ep);
				}
#endif /* DWC_UTE_PER_IO */
				else {
					if (dwc_ep->type == DWC_OTG_EP_TYPE_ISOC
					    && dwc_ep->bInterval > 1) {
						dwc_ep->frame_num +=
						    dwc_ep->bInterval;
						if (dwc_ep->frame_num > 0x3FFF) {
							dwc_ep->frm_overrun = 1;
							dwc_ep->frame_num &=
							    0x3FFF;
						} else
							dwc_ep->frm_overrun = 0;
					}
					complete_ep(ep);
					if (diepint.b.nak)
						CLEAR_IN_EP_INTR(core_if, epnum,
								 nak);
				}
			}
			/* Endpoint disable      */
			if (diepint.b.epdisabled) {
				DWC_DEBUGPL(DBG_ANY, "EP%d IN disabled\n",
					    epnum);
				handle_in_ep_disable_intr(pcd, epnum);

				/* Clear the bit in DIEPINTn for this interrupt */
				CLEAR_IN_EP_INTR(core_if, epnum, epdisabled);
			}
			/* AHB Error */
			if (diepint.b.ahberr) {
				DWC_ERROR("EP%d IN AHB Error\n", epnum);
				/* Clear the bit in DIEPINTn for this interrupt */
				DWC_ERROR("EP%d DEPDMA=0x%08x \n",
					  epnum,
					  core_if->dev_if->
					  in_ep_regs[epnum]->diepdma);
				CLEAR_IN_EP_INTR(core_if, epnum, ahberr);
				dctl.d32 =
				    DWC_READ_REG32(&core_if->
						   dev_if->dev_global_regs->
						   dctl);
				dctl.b.sftdiscon = 1;
				DWC_WRITE_REG32(&core_if->
						dev_if->dev_global_regs->dctl,
						dctl.d32);
				dwc_otg_disable_global_interrupts(core_if);
				ep->pcd->vbus_status = 0;
				if (ep->pcd->conn_status) {
					ep->pcd->conn_status = 0;
				}
				DWC_SPINUNLOCK(pcd->lock);
				cil_pcd_stop(core_if);
				DWC_SPINLOCK(pcd->lock);
			}
			/* TimeOUT Handshake (non-ISOC IN EPs) */
			if (diepint.b.timeout) {
				DWC_ERROR("EP%d IN Time-out\n", epnum);
				handle_in_ep_timeout_intr(pcd, epnum);

				CLEAR_IN_EP_INTR(core_if, epnum, timeout);
			}
			/** IN Token received with TxF Empty */
			if (diepint.b.intktxfemp) {
				DWC_DEBUGPL(DBG_ANY,
					    "EP%d IN TKN TxFifo Empty\n",
					    epnum);
				if (!ep->stopped && epnum != 0) {

					diepmsk_data_t diepmsk = {
					.d32 = 0};
					diepmsk.b.intktxfemp = 1;

					if (core_if->multiproc_int_enable) {
						DWC_MODIFY_REG32
						    (&dev_if->
						     dev_global_regs->diepeachintmsk
						     [epnum], diepmsk.d32, 0);
					} else {
						DWC_MODIFY_REG32
						    (&dev_if->
						     dev_global_regs->diepmsk,
						     diepmsk.d32, 0);
					}
				} else if (core_if->dma_desc_enable
					   && epnum == 0
					   && pcd->ep0state ==
					   EP0_OUT_STATUS_PHASE) {
					/* EP0 IN set STALL */
					depctl.d32 =
					    DWC_READ_REG32(&dev_if->in_ep_regs
							   [epnum]->diepctl);

					/* set the disable and stall bits */
					if (depctl.b.epena) {
						depctl.b.epdis = 1;
					}
					depctl.b.stall = 1;
					DWC_WRITE_REG32(&dev_if->in_ep_regs
							[epnum]->diepctl,
							depctl.d32);
				}
				CLEAR_IN_EP_INTR(core_if, epnum, intktxfemp);
			}
			/** IN Token Received with EP mismatch */
			if (diepint.b.intknepmis) {
				DWC_DEBUGPL(DBG_ANY,
					    "EP%d IN TKN EP Mismatch\n", epnum);
				CLEAR_IN_EP_INTR(core_if, epnum, intknepmis);
			}
			/** IN Endpoint NAK Effective */
			if (diepint.b.inepnakeff) {
				DWC_DEBUGPL(DBG_ANY,
					    "EP%d IN EP NAK Effective\n",
					    epnum);
				/* Periodic EP */
				if (ep->disabling) {
					depctl.d32 = 0;
					depctl.b.snak = 1;
					depctl.b.epdis = 1;
					DWC_MODIFY_REG32(&dev_if->in_ep_regs
							 [epnum]->diepctl,
							 depctl.d32,
							 depctl.d32);
				}
				CLEAR_IN_EP_INTR(core_if, epnum, inepnakeff);

			}

			/** IN EP Tx FIFO Empty Intr */
			if (diepint.b.emptyintr) {
				DWC_DEBUGPL(DBG_ANY,
					    "EP%d Tx FIFO Empty Intr \n",
					    epnum);
				write_empty_tx_fifo(pcd, epnum);

				CLEAR_IN_EP_INTR(core_if, epnum, emptyintr);

			}

			/** IN EP BNA Intr */
			if (diepint.b.bna) {
				CLEAR_IN_EP_INTR(core_if, epnum, bna);
				if (core_if->dma_desc_enable) {
#ifdef DWC_EN_ISOC
					if (dwc_ep->type ==
					    DWC_OTG_EP_TYPE_ISOC) {
						/*
						 * This checking is performed to prevent first "false" BNA
						 * handling occuring right after reconnect
						 */
						if (dwc_ep->next_frame !=
						    0xffffffff)
							dwc_otg_pcd_handle_iso_bna
							    (ep);
					} else
#endif /* DWC_EN_ISOC */
					{
						dwc_otg_pcd_handle_noniso_bna
						    (ep);
					}
				}
			}
			/* NAK Interrutp */
			if (diepint.b.nak) {
				DWC_DEBUGPL(DBG_ANY, "EP%d IN NAK Interrupt\n",
					    epnum);
				if (ep->dwc_ep.type == DWC_OTG_EP_TYPE_ISOC) {
					depctl_data_t depctl;
					if (ep->dwc_ep.frame_num == 0xFFFFFFFF) {
						ep->dwc_ep.frame_num =
							dwc_otg_get_frame_number(core_if);
						if (ep->dwc_ep.bInterval > 1) {
							depctl.d32 = 0;
							depctl.d32 =
							    DWC_READ_REG32
							    (&dev_if->in_ep_regs
							     [epnum]->diepctl);
							if (ep->
							    dwc_ep.frame_num &
							    0x1) {
								depctl.
								    b.setd1pid =
								    1;
								depctl.
								    b.setd0pid =
								    0;
							} else {
								depctl.
								    b.setd0pid =
								    1;
								depctl.
								    b.setd1pid =
								    0;
							}
							DWC_WRITE_REG32
							    (&dev_if->in_ep_regs
							     [epnum]->diepctl,
							     depctl.d32);
						}
						start_next_request(ep);
					}

					if (ep->dwc_ep.frame_num !=
					    0xFFFFFFFF) {
						ep->dwc_ep.frame_num +=
							ep->dwc_ep.bInterval;
						if (dwc_ep->frame_num >
						    0x3FFF) {
							dwc_ep->frm_overrun = 1;
							dwc_ep->frame_num &=
								0x3FFF;
						} else {
							dwc_ep->frm_overrun = 0;
						}
					}
				}

				CLEAR_IN_EP_INTR(core_if, epnum, nak);
			}
		}
		epnum++;
		ep_intr >>= 1;
	}

	return 1;
#undef CLEAR_IN_EP_INTR
}

/**
 * This interrupt indicates that an OUT EP has a pending Interrupt.
 * The sequence for handling the OUT EP interrupt is shown below:
 * -#	Read the Device All Endpoint Interrupt register
 * -#	Repeat the following for each OUT EP interrupt bit set (from
 *		LSB to MSB).
 * -#	Read the Device Endpoint Interrupt (DOEPINTn) register
 * -#	If "Transfer Complete" call the request complete function
 * -#	If "Endpoint Disabled" complete the EP disable procedure.
 * -#	If "AHB Error Interrupt" log error
 * -#	If "Setup Phase Done" process Setup Packet (See Standard USB
 *		Command Processing)
 */
static int32_t dwc_otg_pcd_handle_out_ep_intr(dwc_otg_pcd_t *pcd)
{
#define CLEAR_OUT_EP_INTR(__core_if, __epnum, __intr) \
do { \
		doepint_data_t doepint = {.d32 = 0}; \
		doepint.b.__intr = 1; \
		DWC_WRITE_REG32(&__core_if->dev_if->out_ep_regs[__epnum]->doepint, \
		doepint.d32); \
} while (0)

	dwc_otg_core_if_t *core_if = GET_CORE_IF(pcd);
	uint32_t ep_intr;
	doepint_data_t doepint = {
	.d32 = 0};
	uint32_t epnum = 0;
	dwc_otg_pcd_ep_t *ep;
	dwc_ep_t *dwc_ep;
	dctl_data_t dctl = {
	.d32 = 0};
	gintmsk_data_t gintmsk = {
	.d32 = 0};

	DWC_DEBUGPL(DBG_PCDV, "%s()\n", __func__);

	/* Read in the device interrupt bits */
	ep_intr = dwc_otg_read_dev_all_out_ep_intr(core_if);

	while (ep_intr) {
		if (ep_intr & 0x1) {
			/* Get EP pointer */
			ep = get_out_ep(pcd, epnum);
			dwc_ep = &ep->dwc_ep;

#ifdef VERBOSE
			DWC_DEBUGPL(DBG_PCDV,
				    "EP%d-%s: type=%d, mps=%d\n",
				    dwc_ep->num, (dwc_ep->is_in ? "IN" : "OUT"),
				    dwc_ep->type, dwc_ep->maxpacket);
#endif
			doepint.d32 =
			    dwc_otg_read_dev_out_ep_intr(core_if, dwc_ep);
			/* Transfer complete */
			if (doepint.b.xfercompl) {

				if (epnum == 0) {
					/* Clear the bit in DOEPINTn for this interrupt */
					CLEAR_OUT_EP_INTR(core_if, epnum,
							  xfercompl);
					if (core_if->snpsid >=
					    OTG_CORE_REV_3_00a) {
						DWC_DEBUGPL(DBG_PCDV,
							    "in xfer xomplete DOEPINT=%x doepint=%x\n",
							    DWC_READ_REG32
							    (&core_if->
							     dev_if->out_ep_regs
							     [0]->doepint),
							    doepint.d32);
						DWC_DEBUGPL(DBG_PCDV,
							    "DOEPCTL=%x \n",
							    DWC_READ_REG32
							    (&core_if->
							     dev_if->out_ep_regs
							     [0]->doepctl));

						if (core_if->snpsid >=
						    OTG_CORE_REV_3_00a
						    && core_if->dma_enable ==
						    0) {
							doepint_data_t doepint;
							doepint.d32 =
							    DWC_READ_REG32
							    (&core_if->dev_if->
							     out_ep_regs[0]->
							     doepint);
							if (pcd->ep0state ==
							    EP0_IDLE
							    && doepint.b.sr) {
								CLEAR_OUT_EP_INTR
								    (core_if,
								     epnum, sr);
								goto exit_xfercompl;
							}
						}
						/* In case of DDMA  look at SR bit to go to the Data Stage */
						if (core_if->dma_desc_enable) {
							dev_dma_desc_sts_t
							    status = {
							.d32 = 0};
							if (pcd->ep0state ==
							    EP0_IDLE) {
								status.d32 =
								    core_if->dev_if->setup_desc_addr
								    [core_if->
								     dev_if->setup_desc_index]->status.
								    d32;
								if (pcd->data_terminated) {
									pcd->data_terminated
									    = 0;
									status.d32
									    =
									    core_if->dev_if->out_desc_addr->status.d32;
									dwc_memcpy
									    (&pcd->setup_pkt->req,
									     pcd->backup_buf,
									     8);
								}
								if (status.b.sr) {
									if (doepint.b.setup) {
										DWC_DEBUGPL
										    (DBG_PCDV,
										     "DMA DESC EP0_IDLE SR=1 setup=1\n");
										/* Already started data stage, clear setup */
										CLEAR_OUT_EP_INTR
										    (core_if,
										     epnum,
										     setup);
										doepint.b.setup
										    =
										    0;
										handle_ep0
										    (pcd);
										/* Prepare for more setup packets */
										if (pcd->ep0state == EP0_IN_STATUS_PHASE || pcd->ep0state == EP0_IN_DATA_PHASE) {
											ep0_out_start
											    (core_if,
											     pcd);
										}

										goto exit_xfercompl;
									} else {
										/* Prepare for more setup packets */
										DWC_DEBUGPL
										    (DBG_PCDV,
										     "EP0_IDLE SR=1 setup=0 new setup comes\n");
										ep0_out_start
										    (core_if,
										     pcd);
									}
								}
							} else {
								dwc_otg_pcd_request_t
								    *req;
								dev_dma_desc_sts_t
								    status = {
								.d32 = 0};
								diepint_data_t
								    diepint0;
								diepint0.d32 =
								    DWC_READ_REG32
								    (&core_if->dev_if->
								     in_ep_regs
								     [0]->diepint);

								if (pcd->ep0state == EP0_STALL || pcd->ep0state == EP0_DISCONNECT) {
									DWC_ERROR
									    ("EP0 is stalled/disconnected\n");
								}

								/* Clear IN xfercompl if set */
								if (diepint0.
								    b.xfercompl
								    &&
								    (pcd->ep0state
								     ==
								     EP0_IN_STATUS_PHASE
								     ||
								     pcd->ep0state
								     ==
								     EP0_IN_DATA_PHASE)) {
									DWC_WRITE_REG32
									    (&core_if->dev_if->
									     in_ep_regs
									     [0]->diepint,
									     diepint0.d32);
								}

								status.d32 =
								    core_if->dev_if->setup_desc_addr
								    [core_if->
								     dev_if->setup_desc_index]->status.
								    d32;

								if (ep->
								    dwc_ep.xfer_count
								    !=
								    ep->
								    dwc_ep.total_len
								    &&
								    (pcd->ep0state
								     ==
								     EP0_OUT_DATA_PHASE))
									status.d32
									    =
									    core_if->dev_if->out_desc_addr->status.d32;
								if (pcd->ep0state == EP0_OUT_STATUS_PHASE)
									status.d32
									    =
									    core_if->dev_if->
									    out_desc_addr->status.d32;

								if (status.b.sr) {
									if (DWC_CIRCLEQ_EMPTY(&ep->queue)) {
										DWC_DEBUGPL
										    (DBG_PCDV,
										     "Request queue empty!!\n");
									} else {
										DWC_DEBUGPL
										    (DBG_PCDV,
										     "complete req!!\n");
										req = DWC_CIRCLEQ_FIRST(&ep->queue);
										if (ep->dwc_ep.xfer_count != ep->dwc_ep.total_len && pcd->ep0state == EP0_OUT_DATA_PHASE) {
											/* Read arrived setup packet from req->buf */
											dwc_memcpy
											    (&pcd->setup_pkt->req,
											     req->buf
											     +
											     ep->dwc_ep.xfer_count,
											     8);
										}
										req->actual
										    =
										    ep->dwc_ep.xfer_count;
										dwc_otg_request_done
										    (ep,
										     req,
										     -ECONNRESET);
										ep->dwc_ep.start_xfer_buff = 0;
										ep->dwc_ep.xfer_buff = 0;
										ep->dwc_ep.xfer_len = 0;
									}
									pcd->ep0state
									    =
									    EP0_IDLE;
									if (doepint.b.setup) {
										DWC_DEBUGPL
										    (DBG_PCDV,
										     "EP0_IDLE SR=1 setup=1\n");
										/* Data stage started, clear setup */
										CLEAR_OUT_EP_INTR
										    (core_if,
										     epnum,
										     setup);
										doepint.b.setup
										    =
										    0;
										handle_ep0
										    (pcd);
										/* Prepare for setup packets if ep0in was enabled */
										if (pcd->ep0state == EP0_IN_STATUS_PHASE) {
											ep0_out_start
											    (core_if,
											     pcd);
										}

										goto exit_xfercompl;
									} else {
										/* Prepare for more setup packets */
										DWC_DEBUGPL
										    (DBG_PCDV,
										     "EP0_IDLE SR=1 setup=0 new setup comes 2\n");
										ep0_out_start
										    (core_if,
										     pcd);
									}
								}
							}
						}
						if (core_if->snpsid >=
						    OTG_CORE_REV_3_00a
						    && core_if->dma_enable
						    && core_if->dma_desc_enable
						    == 0) {
							doepint_data_t
							    doepint_temp = {
							.d32 = 0};
							deptsiz0_data_t
							    doeptsize0 = {
							.d32 = 0};
							doepint_temp.d32 =
							    DWC_READ_REG32
							    (&core_if->dev_if->
							     out_ep_regs[ep->
									 dwc_ep.
									 num]->doepint);
							doeptsize0.d32 =
							    DWC_READ_REG32
							    (&core_if->dev_if->
							     out_ep_regs[ep->
									 dwc_ep.
									 num]->doeptsiz);
							if (((ep->
							      dwc_ep.xfer_count
							      ==
							      ep->
							      dwc_ep.total_len
							      || doeptsize0.
							      b.xfersize == 64)
							     && pcd->ep0state ==
							     EP0_OUT_DATA_PHASE
							     && doepint.
							     b.stsphsercvd)
							    || (doeptsize0.
								b.xfersize == 24
								&& pcd->ep0state
								==
								EP0_IN_STATUS_PHASE)){
								CLEAR_OUT_EP_INTR
								    (core_if,
								     epnum,
								     xfercompl);
								DWC_DEBUGPL
								    (DBG_PCDV,
								     "WA for xfercompl along with stsphs \n");
								doepint.
								    b.xfercompl
								    = 0;
								ep0_out_start
								    (core_if,
								     pcd);
								goto exit_xfercompl;
							}

							if (pcd->ep0state ==
							    EP0_IDLE) {
								if (doepint_temp.b.sr) {
									CLEAR_OUT_EP_INTR
									    (core_if,
									     epnum,
									     sr);
								}
								/* Delay is needed for core to update setup
								 * packet count from 3 to 2 after receiving
								 * setup packet*/
								dwc_udelay(100);
								doepint.d32 =
								    DWC_READ_REG32
								    (&core_if->dev_if->
								     out_ep_regs
								     [0]->doepint);
								if (doeptsize0.b.supcnt == 3) {
									DWC_DEBUGPL
									    (DBG_ANY,
									     "Rolling over!!!!!!!\n");
									ep->dwc_ep.stp_rollover = 1;
								}
								if (doepint.
								    b.setup) {
retry:
									/* Already started data stage, clear setup */
									CLEAR_OUT_EP_INTR(core_if, epnum, setup);
									doepint.b.setup
									    = 0;
									handle_ep0
									    (pcd);
									ep->dwc_ep.stp_rollover = 0;
									/* Prepare for more setup packets */
									if (pcd->ep0state == EP0_IN_STATUS_PHASE || pcd->ep0state == EP0_IN_DATA_PHASE) {
										depctl_data_t
										    depctl
										    = {
										.d32 = 0};
										depctl.b.cnak
										    =
										    1;
										ep0_out_start
										    (core_if,
										     pcd);
										/* Core not updating setup packet count
										 * in case of PET testing - @TODO vahrama
										 * to check with HW team further */
										if (!core_if->otg_ver) {
											DWC_MODIFY_REG32
											    (&core_if->dev_if->
											     out_ep_regs
											     [0]->doepctl,
											     0,
											     depctl.d32);
										}
									}
									goto exit_xfercompl;
								} else {
									/* Prepare for more setup packets */
									DWC_DEBUGPL
									    (DBG_ANY,
									     "EP0_IDLE SR=1 setup=0 new setup comes\n");
									doepint.d32
									    =
									    DWC_READ_REG32
									    (&core_if->dev_if->
									     out_ep_regs
									     [0]->doepint);
									if (doepint.b.setup)
										goto retry;
									ep0_out_start
									    (core_if,
									     pcd);
								}
							} else {
								dwc_otg_pcd_request_t
								    *req;
								diepint_data_t
								    diepint0 = {
								.d32 = 0};
								doepint_data_t
								    doepint_temp
								    = {
								.d32 = 0};
								depctl_data_t
								    diepctl0;
								diepint0.d32 =
								    DWC_READ_REG32
								    (&core_if->dev_if->
								     in_ep_regs
								     [0]->diepint);
								diepctl0.d32 =
								    DWC_READ_REG32
								    (&core_if->dev_if->
								     in_ep_regs
								     [0]->diepctl);

								if (pcd->ep0state == EP0_IN_DATA_PHASE || pcd->ep0state == EP0_IN_STATUS_PHASE) {
									if (diepint0.b.xfercompl) {
										DWC_WRITE_REG32
										    (&core_if->dev_if->
										     in_ep_regs
										     [0]->diepint,
										     diepint0.d32);
									}
									if (diepctl0.b.epena) {
										diepint_data_t
										    diepint
										    = {
										.d32 = 0};
										diepctl0.b.snak
										    =
										    1;
										DWC_WRITE_REG32
										    (&core_if->dev_if->
										     in_ep_regs
										     [0]->diepctl,
										     diepctl0.d32);
										do {
											dwc_udelay
											    (10);
											diepint.d32
											    =
											    DWC_READ_REG32
											    (&core_if->dev_if->
											     in_ep_regs
											     [0]->diepint);
										} while (!diepint.b.inepnakeff);
										diepint.b.inepnakeff
										    =
										    1;
										DWC_WRITE_REG32
										    (&core_if->dev_if->
										     in_ep_regs
										     [0]->diepint,
										     diepint.d32);
										diepctl0.d32
										    =
										    0;
										diepctl0.b.epdis
										    =
										    1;
										DWC_WRITE_REG32
										    (&core_if->dev_if->in_ep_regs
										     [0]->diepctl,
										     diepctl0.d32);
										do {
											dwc_udelay
											    (10);
											diepint.d32
											    =
											    DWC_READ_REG32
											    (&core_if->dev_if->
											     in_ep_regs
											     [0]->diepint);
										} while (!diepint.b.epdisabled);
										diepint.b.epdisabled
										    =
										    1;
										DWC_WRITE_REG32
										    (&core_if->dev_if->in_ep_regs
										     [0]->diepint,
										     diepint.d32);
									}
								}
								doepint_temp.d32
								    =
								    DWC_READ_REG32
								    (&core_if->dev_if->
								     out_ep_regs
								     [ep->dwc_ep.num]->doepint);
								if (doepint_temp.b.sr) {
									CLEAR_OUT_EP_INTR
									    (core_if,
									     epnum,
									     sr);
									if (DWC_CIRCLEQ_EMPTY(&ep->queue)) {
										DWC_DEBUGPL
										    (DBG_PCDV,
										     "Request queue empty!!\n");
									} else {
										DWC_DEBUGPL
										    (DBG_PCDV,
										     "complete req!!\n");
										req = DWC_CIRCLEQ_FIRST(&ep->queue);
										if (ep->dwc_ep.xfer_count != ep->dwc_ep.total_len && pcd->ep0state == EP0_OUT_DATA_PHASE) {
											/* Read arrived setup packet from req->buf */
											dwc_memcpy
											    (&pcd->setup_pkt->req,
											     req->buf
											     +
											     ep->dwc_ep.xfer_count,
											     8);
										}
										req->actual
										    =
										    ep->dwc_ep.xfer_count;
										dwc_otg_request_done
										    (ep,
										     req,
										     -ECONNRESET);
										ep->dwc_ep.start_xfer_buff = 0;
										ep->dwc_ep.xfer_buff = 0;
										ep->dwc_ep.xfer_len = 0;
									}
									pcd->ep0state
									    =
									    EP0_IDLE;
									if (doepint.b.setup) {
										DWC_DEBUGPL
										    (DBG_PCDV,
										     "EP0_IDLE SR=1 setup=1\n");
										/* Data stage started, clear setup */
										CLEAR_OUT_EP_INTR
										    (core_if,
										     epnum,
										     setup);
										doepint.b.setup
										    =
										    0;
										handle_ep0
										    (pcd);
										/* Prepare for setup packets if ep0in was enabled */
										if (pcd->ep0state == EP0_IN_STATUS_PHASE) {
											depctl_data_t
											    depctl
											    = {
											.d32 = 0};
											depctl.b.cnak
											    =
											    1;
											ep0_out_start
											    (core_if,
											     pcd);
											/* Core not updating setup packet count
											 * in case of PET testing - @TODO vahrama
											 * to check with HW team further */
											if (!core_if->otg_ver) {
												DWC_MODIFY_REG32
												    (&core_if->dev_if->
												     out_ep_regs
												     [0]->doepctl,
												     0,
												     depctl.d32);
											}
										}
										goto exit_xfercompl;
									} else {
										/* Prepare for more setup packets */
										DWC_DEBUGPL
										    (DBG_PCDV,
										     "EP0_IDLE SR=1 setup=0 new setup comes 2\n");
										ep0_out_start
										    (core_if,
										     pcd);
									}
								}
							}
						}
						if (core_if->dma_enable == 0
						    || pcd->ep0state !=
						    EP0_IDLE)
							handle_ep0(pcd);
exit_xfercompl:
						DWC_DEBUGPL(DBG_PCDV,
							    "after DOEPINT=%x doepint=%x\n",
							    dwc_otg_read_dev_out_ep_intr
							    (core_if, dwc_ep),
							    doepint.d32);
					} else {
						if (core_if->dma_desc_enable ==
						    0
						    || pcd->ep0state !=
						    EP0_IDLE)
							handle_ep0(pcd);
					}
#ifdef DWC_EN_ISOC
				} else if (dwc_ep->type == DWC_OTG_EP_TYPE_ISOC) {
					if (doepint.b.pktdrpsts == 0) {
						/* Clear the bit in DOEPINTn for this interrupt */
						CLEAR_OUT_EP_INTR(core_if,
								  epnum,
								  xfercompl);
						complete_iso_ep(pcd, ep);
					} else {

						doepint_data_t doepint = {
						.d32 = 0};
						doepint.b.xfercompl = 1;
						doepint.b.pktdrpsts = 1;
						DWC_WRITE_REG32
						    (&core_if->
						     dev_if->out_ep_regs
						     [epnum]->doepint,
						     doepint.d32);
						if (handle_iso_out_pkt_dropped
						    (core_if, dwc_ep)) {
							complete_iso_ep(pcd,
									ep);
						}
					}
#endif /* DWC_EN_ISOC */
#ifdef DWC_UTE_PER_IO
				} else if (dwc_ep->type == DWC_OTG_EP_TYPE_ISOC) {
					CLEAR_OUT_EP_INTR(core_if, epnum,
							  xfercompl);
					if (!ep->stopped)
						complete_xiso_ep(ep);
#endif /* DWC_UTE_PER_IO */
				} else {
					/* Clear the bit in DOEPINTn for this interrupt */
					CLEAR_OUT_EP_INTR(core_if, epnum,
							  xfercompl);

					if (core_if->core_params->dev_out_nak) {
						DWC_TIMER_CANCEL(pcd->
								 core_if->ep_xfer_timer
								 [epnum]);
						pcd->
						    core_if->ep_xfer_info
						    [epnum].state = 0;
#ifdef DEBUG
						print_memory_payload(pcd,
								     dwc_ep);
#endif
					}
					complete_ep(ep);
				}

			}

			if (doepint.b.stsphsercvd) {
				deptsiz0_data_t deptsiz;
				CLEAR_OUT_EP_INTR(core_if, epnum, stsphsercvd);
				deptsiz.d32 =
				    DWC_READ_REG32(&core_if->dev_if->
						   out_ep_regs[0]->doeptsiz);
				if (core_if->dma_desc_enable) {
					do_setup_in_status_phase(pcd);
				}

			}

			/* Endpoint disable      */
			if (doepint.b.epdisabled) {

				/* Clear the bit in DOEPINTn for this interrupt */
				CLEAR_OUT_EP_INTR(core_if, epnum, epdisabled);
				if (core_if->core_params->dev_out_nak) {
#ifdef DEBUG
					print_memory_payload(pcd, dwc_ep);
#endif
					/* In case of timeout condition */
					if (core_if->
					    ep_xfer_info[epnum].state == 2) {
						dctl.d32 =
						    DWC_READ_REG32
						    (&core_if->dev_if->
						     dev_global_regs->dctl);
						dctl.b.cgoutnak = 1;
						DWC_WRITE_REG32
						    (&core_if->dev_if->dev_global_regs->dctl,
						     dctl.d32);
						/* Unmask goutnakeff interrupt which was masked
						 * during handle nak out interrupt */
						gintmsk.b.goutnakeff = 1;
						DWC_MODIFY_REG32
						    (&core_if->core_global_regs->gintmsk,
						     0, gintmsk.d32);

						complete_ep(ep);
					}
				}
				if (ep->dwc_ep.type == DWC_OTG_EP_TYPE_ISOC) {
					dctl_data_t dctl;
					gintmsk_data_t intr_mask = {
					.d32 = 0};
					dwc_otg_pcd_request_t *req = 0;

					dctl.d32 =
					    DWC_READ_REG32(&core_if->dev_if->
							   dev_global_regs->dctl);
					dctl.b.cgoutnak = 1;
					DWC_WRITE_REG32(&core_if->
							dev_if->dev_global_regs->
							dctl, dctl.d32);

					intr_mask.d32 = 0;
					intr_mask.b.incomplisoout = 1;

					/* Get any pending requests */
					if (!DWC_CIRCLEQ_EMPTY(&ep->queue)) {
						req =
						    DWC_CIRCLEQ_FIRST
						    (&ep->queue);
						if (!req) {
							DWC_PRINTF
							    ("complete_ep 0x%p, req = NULL!\n",
							     ep);
						} else {
							dwc_otg_request_done(ep,
									     req,
									     0);
							start_next_request(ep);
						}
					} else {
						DWC_PRINTF
						    ("complete_ep 0x%p, ep->queue empty!\n",
						     ep);
					}
				}
			}
			/* AHB Error */
			if (doepint.b.ahberr) {
				DWC_ERROR("EP%d OUT AHB Error\n", epnum);
				DWC_ERROR("EP%d DEPDMA=0x%08x \n",
					  epnum,
					  core_if->dev_if->
					  out_ep_regs[epnum]->doepdma);
				CLEAR_OUT_EP_INTR(core_if, epnum, ahberr);
			}
			/* Setup Phase Done (contorl EPs) */
			if (doepint.b.setup) {
#ifdef DEBUG_EP0
				DWC_DEBUGPL(DBG_PCD, "EP%d SETUP Done\n",
					    epnum);
#endif
				CLEAR_OUT_EP_INTR(core_if, epnum, setup);

				handle_ep0(pcd);
			}

			/** OUT EP BNA Intr */
			if (doepint.b.bna) {
				CLEAR_OUT_EP_INTR(core_if, epnum, bna);
				if (core_if->dma_desc_enable) {
#ifdef DWC_EN_ISOC
					if (dwc_ep->type ==
					    DWC_OTG_EP_TYPE_ISOC) {
						/*
						 * This checking is performed to prevent first "false" BNA
						 * handling occuring right after reconnect
						 */
						if (dwc_ep->next_frame !=
						    0xffffffff)
							dwc_otg_pcd_handle_iso_bna
							    (ep);
					} else
#endif /* DWC_EN_ISOC */
					{
						dwc_otg_pcd_handle_noniso_bna
						    (ep);
					}
				}
			}
			/* Babble Interrupt */
			if (doepint.b.babble) {
				DWC_DEBUGPL(DBG_ANY, "EP%d OUT Babble\n",
					    epnum);
				handle_out_ep_babble_intr(pcd, epnum);

				CLEAR_OUT_EP_INTR(core_if, epnum, babble);
			}
			if (doepint.b.outtknepdis) {
				DWC_DEBUGPL(DBG_ANY, "EP%d OUT Token received when EP is \
					disabled\n",
					    epnum);
				if (ep->dwc_ep.type == DWC_OTG_EP_TYPE_ISOC) {
					doepmsk_data_t doepmsk = {
					.d32 = 0};
					ep->dwc_ep.frame_num =
					    core_if->frame_num;
					if (ep->dwc_ep.bInterval > 1) {
						depctl_data_t depctl;
						depctl.d32 =
						    DWC_READ_REG32
						    (&core_if->dev_if->
						     out_ep_regs
						     [epnum]->doepctl);
						if (ep->dwc_ep.frame_num & 0x1) {
							depctl.b.setd1pid = 1;
							depctl.b.setd0pid = 0;
						} else {
							depctl.b.setd0pid = 1;
							depctl.b.setd1pid = 0;
						}
						DWC_WRITE_REG32
						    (&core_if->dev_if->
						     out_ep_regs
						     [epnum]->doepctl,
						     depctl.d32);
					}
					start_next_request(ep);
					doepmsk.b.outtknepdis = 1;
					DWC_MODIFY_REG32(&core_if->
							 dev_if->dev_global_regs->doepmsk,
							 doepmsk.d32, 0);
				}
				CLEAR_OUT_EP_INTR(core_if, epnum, outtknepdis);
			}

			/* NAK Interrutp */
			if (doepint.b.nak) {
				DWC_DEBUGPL(DBG_ANY, "EP%d OUT NAK\n", epnum);
				handle_out_ep_nak_intr(pcd, epnum);

				CLEAR_OUT_EP_INTR(core_if, epnum, nak);
			}
			/* NYET Interrutp */
			if (doepint.b.nyet) {
				DWC_DEBUGPL(DBG_ANY, "EP%d OUT NYET\n", epnum);
				handle_out_ep_nyet_intr(pcd, epnum);

				CLEAR_OUT_EP_INTR(core_if, epnum, nyet);
			}
		}

		epnum++;
		ep_intr >>= 1;
	}

	return 1;

#undef CLEAR_OUT_EP_INTR
}

static int drop_transfer(uint32_t trgt_fr, uint32_t curr_fr,
			 uint8_t frm_overrun)
{
	int retval = 0;
	if (!frm_overrun && curr_fr >= trgt_fr)
		retval = 1;
	else if (frm_overrun
		 && (curr_fr >= trgt_fr && ((curr_fr - trgt_fr) < 0x3FFF / 2)))
		retval = 1;
	return retval;
}

/**
 * Incomplete ISO IN Transfer Interrupt.
 * This interrupt indicates one of the following conditions occurred
 * while transmitting an ISOC transaction.
 * - Corrupted IN Token for ISOC EP.
 * - Packet not complete in FIFO.
 * The follow actions will be taken:
 *	-#	Determine the EP
 *	-#	Set incomplete flag in dwc_ep structure
 *	-#	Disable EP; when "Endpoint Disabled" interrupt is received
 *		Flush FIFO
 */
int32_t dwc_otg_pcd_handle_incomplete_isoc_in_intr(dwc_otg_pcd_t *pcd)
{
	gintsts_data_t gintsts;

#ifdef DWC_EN_ISOC
	dwc_otg_dev_if_t *dev_if;
	deptsiz_data_t deptsiz = {
	.d32 = 0};
	depctl_data_t depctl = {
	.d32 = 0};
	dsts_data_t dsts = {
	.d32 = 0};
	dwc_ep_t *dwc_ep;
	int i;

	dev_if = GET_CORE_IF(pcd)->dev_if;

	for (i = 1; i <= dev_if->num_in_eps; ++i) {
		dwc_ep = &pcd->in_ep[i].dwc_ep;
		if (dwc_ep->active && dwc_ep->type == DWC_OTG_EP_TYPE_ISOC) {
			deptsiz.d32 =
			    DWC_READ_REG32(&dev_if->in_ep_regs[i]->dieptsiz);
			depctl.d32 =
			    DWC_READ_REG32(&dev_if->in_ep_regs[i]->diepctl);

			if (depctl.b.epdis && deptsiz.d32) {
				set_current_pkt_info(GET_CORE_IF(pcd), dwc_ep);
				if (dwc_ep->cur_pkt >= dwc_ep->pkt_cnt) {
					dwc_ep->cur_pkt = 0;
					dwc_ep->proc_buf_num =
					    (dwc_ep->proc_buf_num ^ 1) & 0x1;

					if (dwc_ep->proc_buf_num) {
						dwc_ep->cur_pkt_addr =
						    dwc_ep->xfer_buff1;
						dwc_ep->cur_pkt_dma_addr =
						    dwc_ep->dma_addr1;
					} else {
						dwc_ep->cur_pkt_addr =
						    dwc_ep->xfer_buff0;
						dwc_ep->cur_pkt_dma_addr =
						    dwc_ep->dma_addr0;
					}

				}

				dsts.d32 =
				    DWC_READ_REG32(&GET_CORE_IF(pcd)->dev_if->
						   dev_global_regs->dsts);
				dwc_ep->next_frame = dsts.b.soffn;

				dwc_otg_iso_ep_start_frm_transfer(GET_CORE_IF
								  (pcd),
								  dwc_ep);
			}
		}
	}

#else
	depctl_data_t depctl = {
	.d32 = 0};
	dwc_ep_t *dwc_ep;
	dwc_otg_dev_if_t *dev_if;
	int i;
	dev_if = GET_CORE_IF(pcd)->dev_if;

	DWC_DEBUGPL(DBG_PCD, "Incomplete ISO IN \n");

	for (i = 1; i <= dev_if->num_in_eps; ++i) {
		dwc_ep = &pcd->in_ep[i - 1].dwc_ep;
		depctl.d32 = DWC_READ_REG32(&dev_if->in_ep_regs[i]->diepctl);
		if (depctl.b.epena && dwc_ep->type == DWC_OTG_EP_TYPE_ISOC) {
			if (drop_transfer
			    (dwc_ep->frame_num, GET_CORE_IF(pcd)->frame_num,
			     dwc_ep->frm_overrun)) {
				depctl.d32 =
				    DWC_READ_REG32(&dev_if->
						   in_ep_regs[i]->diepctl);
				depctl.b.snak = 1;
				depctl.b.epdis = 1;
				DWC_MODIFY_REG32(&dev_if->
						 in_ep_regs[i]->diepctl,
						 depctl.d32, depctl.d32);
			}
		}
	}

	/*intr_mask.b.incomplisoin = 1;
	   DWC_MODIFY_REG32(&GET_CORE_IF(pcd)->core_global_regs->gintmsk,
	   intr_mask.d32, 0);    */
#endif /* DWC_EN_ISOC */

	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.incomplisoin = 1;
	DWC_WRITE_REG32(&GET_CORE_IF(pcd)->core_global_regs->gintsts,
			gintsts.d32);

	return 1;
}

/**
 * Incomplete ISO OUT Transfer Interrupt.
 *
 * This interrupt indicates that the core has dropped an ISO OUT
 * packet. The following conditions can be the cause:
 * - FIFO Full, the entire packet would not fit in the FIFO.
 * - CRC Error
 * - Corrupted Token
 * The follow actions will be taken:
 *	-#	Determine the EP
 *	-#	Set incomplete flag in dwc_ep structure
 *	-#	Read any data from the FIFO
 *	-#	Disable EP. When "Endpoint Disabled" interrupt is received
 *		re-enable EP.
 */
int32_t dwc_otg_pcd_handle_incomplete_isoc_out_intr(dwc_otg_pcd_t *pcd)
{

	gintsts_data_t gintsts;

#ifdef DWC_EN_ISOC
	dwc_otg_dev_if_t *dev_if;
	deptsiz_data_t deptsiz = {
	.d32 = 0};
	depctl_data_t depctl = {
	.d32 = 0};
	dsts_data_t dsts = {
	.d32 = 0};
	dwc_ep_t *dwc_ep;
	int i;

	dev_if = GET_CORE_IF(pcd)->dev_if;

	for (i = 1; i <= dev_if->num_out_eps; ++i) {
		dwc_ep = &pcd->in_ep[i].dwc_ep;
		if (pcd->out_ep[i].dwc_ep.active &&
		    pcd->out_ep[i].dwc_ep.type == DWC_OTG_EP_TYPE_ISOC) {
			deptsiz.d32 =
			    DWC_READ_REG32(&dev_if->out_ep_regs[i]->doeptsiz);
			depctl.d32 =
			    DWC_READ_REG32(&dev_if->out_ep_regs[i]->doepctl);

			if (depctl.b.epdis && deptsiz.d32) {
				set_current_pkt_info(GET_CORE_IF(pcd),
						     &pcd->out_ep[i].dwc_ep);
				if (dwc_ep->cur_pkt >= dwc_ep->pkt_cnt) {
					dwc_ep->cur_pkt = 0;
					dwc_ep->proc_buf_num =
					    (dwc_ep->proc_buf_num ^ 1) & 0x1;

					if (dwc_ep->proc_buf_num) {
						dwc_ep->cur_pkt_addr =
						    dwc_ep->xfer_buff1;
						dwc_ep->cur_pkt_dma_addr =
						    dwc_ep->dma_addr1;
					} else {
						dwc_ep->cur_pkt_addr =
						    dwc_ep->xfer_buff0;
						dwc_ep->cur_pkt_dma_addr =
						    dwc_ep->dma_addr0;
					}

				}

				dsts.d32 =
				    DWC_READ_REG32(&GET_CORE_IF(pcd)->dev_if->
						   dev_global_regs->dsts);
				dwc_ep->next_frame = dsts.b.soffn;

				dwc_otg_iso_ep_start_frm_transfer(GET_CORE_IF
								  (pcd),
								  dwc_ep);
			}
		}
	}
#else
	/** @todo implement ISR */
	gintmsk_data_t intr_mask = {
	.d32 = 0};
	dwc_otg_core_if_t *core_if;
	deptsiz_data_t deptsiz = {
	.d32 = 0};
	depctl_data_t depctl = {
	.d32 = 0};
	dctl_data_t dctl = {
	.d32 = 0};
	dwc_ep_t *dwc_ep = NULL;
	int i;
	core_if = GET_CORE_IF(pcd);

	for (i = 0; i < core_if->dev_if->num_out_eps; ++i) {
		dwc_ep = &pcd->out_ep[i].dwc_ep;
		depctl.d32 =
		    DWC_READ_REG32(&core_if->dev_if->
				   out_ep_regs[dwc_ep->num]->doepctl);
		if (depctl.b.epena
		    && depctl.b.dpid == (core_if->frame_num & 0x1)) {
			core_if->dev_if->isoc_ep = dwc_ep;
			deptsiz.d32 =
			    DWC_READ_REG32(&core_if->
					   dev_if->out_ep_regs[dwc_ep->num]->
					   doeptsiz);
			break;
		}
	}
	dctl.d32 = DWC_READ_REG32(&core_if->dev_if->dev_global_regs->dctl);
	gintsts.d32 = DWC_READ_REG32(&core_if->core_global_regs->gintsts);
	intr_mask.d32 = DWC_READ_REG32(&core_if->core_global_regs->gintmsk);

	if (!intr_mask.b.goutnakeff) {
		/* Unmask it */
		intr_mask.b.goutnakeff = 1;
		DWC_WRITE_REG32(&core_if->core_global_regs->gintmsk,
				intr_mask.d32);
	}
	if (!gintsts.b.goutnakeff) {
		dctl.b.sgoutnak = 1;
	}
	DWC_WRITE_REG32(&core_if->dev_if->dev_global_regs->dctl, dctl.d32);

	depctl.d32 =
	    DWC_READ_REG32(&core_if->dev_if->out_ep_regs[dwc_ep->num]->doepctl);
	if (depctl.b.epena) {
		depctl.b.epdis = 1;
		depctl.b.snak = 1;
	}
	DWC_WRITE_REG32(&core_if->dev_if->out_ep_regs[dwc_ep->num]->doepctl,
			depctl.d32);

	intr_mask.d32 = 0;
	intr_mask.b.incomplisoout = 1;

#endif /* DWC_EN_ISOC */

	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.incomplisoout = 1;
	DWC_WRITE_REG32(&GET_CORE_IF(pcd)->core_global_regs->gintsts,
			gintsts.d32);

	return 1;
}

/**
 * This function handles the Global IN NAK Effective interrupt.
 *
 */
int32_t dwc_otg_pcd_handle_in_nak_effective(dwc_otg_pcd_t *pcd)
{
	dwc_otg_dev_if_t *dev_if = GET_CORE_IF(pcd)->dev_if;
	depctl_data_t diepctl = {
	.d32 = 0};
	gintmsk_data_t intr_mask = {
	.d32 = 0};
	gintsts_data_t gintsts;
	dwc_otg_core_if_t *core_if = GET_CORE_IF(pcd);
	int i;

	DWC_DEBUGPL(DBG_PCD, "Global IN NAK Effective\n");

	/* Disable all active IN EPs */
	for (i = 0; i <= dev_if->num_in_eps; i++) {
		diepctl.d32 = DWC_READ_REG32(&dev_if->in_ep_regs[i]->diepctl);
		if (!(diepctl.b.eptype & 1) && diepctl.b.epena) {
			if (core_if->start_predict > 0)
				core_if->start_predict++;
			diepctl.b.epdis = 1;
			diepctl.b.snak = 1;
			DWC_WRITE_REG32(&dev_if->in_ep_regs[i]->diepctl,
					diepctl.d32);
		}
	}

	/* Disable the Global IN NAK Effective Interrupt */
	intr_mask.b.ginnakeff = 1;
	DWC_MODIFY_REG32(&GET_CORE_IF(pcd)->core_global_regs->gintmsk,
			 intr_mask.d32, 0);

	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.ginnakeff = 1;
	DWC_WRITE_REG32(&GET_CORE_IF(pcd)->core_global_regs->gintsts,
			gintsts.d32);

	return 1;
}

/**
 * OUT NAK Effective.
 *
 */
int32_t dwc_otg_pcd_handle_out_nak_effective(dwc_otg_pcd_t *pcd)
{
	dwc_otg_dev_if_t *dev_if = GET_CORE_IF(pcd)->dev_if;
	gintmsk_data_t intr_mask = {
	.d32 = 0};
	gintsts_data_t gintsts;
	depctl_data_t doepctl;
	int i;

	/* Disable the Global OUT NAK Effective Interrupt */
	intr_mask.b.goutnakeff = 1;
	DWC_MODIFY_REG32(&GET_CORE_IF(pcd)->core_global_regs->gintmsk,
			 intr_mask.d32, 0);

	/* If DEV OUT NAK enabled */
	if (pcd->core_if->core_params->dev_out_nak) {
		/* Run over all out endpoints to determine the ep number on
		 * which the timeout has happened
		 */
		for (i = 0; i <= dev_if->num_out_eps; i++) {
			if (pcd->core_if->ep_xfer_info[i].state == 2)
				break;
		}
		if (i > dev_if->num_out_eps) {
			dctl_data_t dctl;
			dctl.d32 =
			    DWC_READ_REG32(&dev_if->dev_global_regs->dctl);
			dctl.b.cgoutnak = 1;
			DWC_WRITE_REG32(&dev_if->dev_global_regs->dctl,
					dctl.d32);
			goto out;
		}

		/* Disable the endpoint */
		doepctl.d32 = DWC_READ_REG32(&dev_if->out_ep_regs[i]->doepctl);
		if (doepctl.b.epena) {
			doepctl.b.epdis = 1;
			doepctl.b.snak = 1;
		}
		DWC_WRITE_REG32(&dev_if->out_ep_regs[i]->doepctl, doepctl.d32);
		return 1;
	}
	/* We come here from Incomplete ISO OUT handler */
	if (dev_if->isoc_ep) {
		dwc_ep_t *dwc_ep = (dwc_ep_t *) dev_if->isoc_ep;
		uint32_t epnum = dwc_ep->num;
		doepint_data_t doepint;
		doepint.d32 =
		    DWC_READ_REG32(&dev_if->out_ep_regs[dwc_ep->num]->doepint);
		dev_if->isoc_ep = NULL;
		doepctl.d32 =
		    DWC_READ_REG32(&dev_if->out_ep_regs[epnum]->doepctl);
		DWC_PRINTF("Before disable DOEPCTL = %08x\n", doepctl.d32);
		if (doepctl.b.epena) {
			doepctl.b.epdis = 1;
			doepctl.b.snak = 1;
		}
		DWC_WRITE_REG32(&dev_if->out_ep_regs[epnum]->doepctl,
				doepctl.d32);
		return 1;
	} else
		DWC_PRINTF("INTERRUPT Handler not implemented for %s\n",
			   "Global OUT NAK Effective\n");

out:
	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.goutnakeff = 1;
	DWC_WRITE_REG32(&GET_CORE_IF(pcd)->core_global_regs->gintsts,
			gintsts.d32);

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
int32_t dwc_otg_pcd_handle_intr(dwc_otg_pcd_t *pcd)
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(pcd);
#ifdef VERBOSE
	dwc_otg_core_global_regs_t *global_regs = core_if->core_global_regs;
#endif
	gintsts_data_t gintr_status;
	int32_t retval = 0;

	if (dwc_otg_check_haps_status(core_if) == -1) {
		DWC_WARN("HAPS is disconnected");
		return retval;
	}

	/* Exit from ISR if core is hibernated */
	if (core_if->hibernation_suspend == 1) {
		return retval;
	}
#ifdef VERBOSE
	DWC_DEBUGPL(DBG_ANY, "%s() gintsts=%08x	 gintmsk=%08x\n",
		    __func__,
		    DWC_READ_REG32(&global_regs->gintsts),
		    DWC_READ_REG32(&global_regs->gintmsk));
#endif

	if (dwc_otg_is_device_mode(core_if)) {
		DWC_SPINLOCK(pcd->lock);
#ifdef VERBOSE
		DWC_DEBUGPL(DBG_PCDV, "%s() gintsts=%08x  gintmsk=%08x\n",
			    __func__,
			    DWC_READ_REG32(&global_regs->gintsts),
			    DWC_READ_REG32(&global_regs->gintmsk));
#endif

		gintr_status.d32 = dwc_otg_read_core_intr(core_if);

		DWC_DEBUGPL(DBG_PCDV, "%s: gintsts&gintmsk=%08x\n",
			    __func__, gintr_status.d32);

		if (gintr_status.b.sofintr) {
			retval |= dwc_otg_pcd_handle_sof_intr(pcd);
		}
		if (gintr_status.b.rxstsqlvl) {
			retval |=
			    dwc_otg_pcd_handle_rx_status_q_level_intr(pcd);
		}
		if (gintr_status.b.nptxfempty) {
			retval |= dwc_otg_pcd_handle_np_tx_fifo_empty_intr(pcd);
		}
		if (gintr_status.b.goutnakeff) {
			retval |= dwc_otg_pcd_handle_out_nak_effective(pcd);
		}
		if (gintr_status.b.i2cintr) {
			retval |= dwc_otg_pcd_handle_i2c_intr(pcd);
		}
		if (gintr_status.b.erlysuspend) {
			retval |= dwc_otg_pcd_handle_early_suspend_intr(pcd);
		}
		if (gintr_status.b.usbreset) {
			retval |= dwc_otg_pcd_handle_usb_reset_intr(pcd);
			pcd->conn_status = -1;
		}
		if (gintr_status.b.enumdone) {
			retval |= dwc_otg_pcd_handle_enum_done_intr(pcd);
		}
		if (gintr_status.b.isooutdrop) {
			retval |=
			    dwc_otg_pcd_handle_isoc_out_packet_dropped_intr
			    (pcd);
		}
		if (gintr_status.b.eopframe) {
			retval |=
			    dwc_otg_pcd_handle_end_periodic_frame_intr(pcd);
		}
		if (gintr_status.b.inepint) {
			if (!core_if->multiproc_int_enable) {
				retval |= dwc_otg_pcd_handle_in_ep_intr(pcd);
			}
		}
		if (gintr_status.b.outepintr) {
			if (!core_if->multiproc_int_enable) {
				retval |= dwc_otg_pcd_handle_out_ep_intr(pcd);
			}
		}
		if (gintr_status.b.epmismatch) {
			retval |= dwc_otg_pcd_handle_ep_mismatch_intr(pcd);
		}
		if (gintr_status.b.fetsusp) {
			retval |= dwc_otg_pcd_handle_ep_fetsusp_intr(pcd);
		}
		if (gintr_status.b.ginnakeff) {
			retval |= dwc_otg_pcd_handle_in_nak_effective(pcd);
		}
		if (gintr_status.b.incomplisoin) {
			retval |=
			    dwc_otg_pcd_handle_incomplete_isoc_in_intr(pcd);
		}
		if (gintr_status.b.incomplisoout) {
			retval |=
			    dwc_otg_pcd_handle_incomplete_isoc_out_intr(pcd);
		}

		/* In MPI mode Device Endpoints interrupts are asserted
		 * without setting outepintr and inepint bits set, so these
		 * Interrupt handlers are called without checking these bit-fields
		 */
		if (core_if->multiproc_int_enable) {
			retval |= dwc_otg_pcd_handle_in_ep_intr(pcd);
			retval |= dwc_otg_pcd_handle_out_ep_intr(pcd);
		}
#ifdef VERBOSE
		DWC_DEBUGPL(DBG_PCDV, "%s() gintsts=%0x\n", __func__,
			    DWC_READ_REG32(&global_regs->gintsts));
#endif
		DWC_SPINUNLOCK(pcd->lock);
	}
	return retval;
}

#endif /* DWC_HOST_ONLY */
