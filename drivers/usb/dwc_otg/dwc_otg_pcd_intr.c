/* ==========================================================================
 * $File: //dwh/usb_iip/dev/software/otg_ipmate/linux/drivers/dwc_otg_pcd_intr.c $
 * $Revision: #18 $
 * $Date: 2007/02/07 $
 * $Change: 791271 $
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
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include "dwc_otg_driver.h"
#include "dwc_otg_pcd.h"


#define DEBUG_EP0

/* request functions defined in "dwc_otg_pcd.c" */
extern void request_done( dwc_otg_pcd_ep_t *_ep, dwc_otg_pcd_request_t *_req, 
								int _status);
extern void request_nuke( dwc_otg_pcd_ep_t *_ep ); 
extern void dwc_otg_pcd_update_otg( dwc_otg_pcd_t *_pcd, 
								const unsigned _reset );
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
static inline void print_ep0_state( dwc_otg_pcd_t *_pcd ) 
{
#ifdef DEBUG
	char str[40];
	
	switch (_pcd->ep0state)
	{
	case EP0_DISCONNECT:
		strcpy(str, "EP0_DISCONNECT");
		break;
	case EP0_IDLE:
		strcpy(str, "EP0_IDLE");
		break;
	case EP0_IN_DATA_PHASE:
		strcpy(str, "EP0_IN_DATA_PHASE");
		break;
	case EP0_OUT_DATA_PHASE:
		strcpy(str, "EP0_OUT_DATA_PHASE");
		break;
	case EP0_STATUS:
		strcpy(str,"EP0_STATUS");
		break;
	case EP0_STALL:
		strcpy(str,"EP0_STALL");
		break;
	default:
		strcpy(str,"EP0_INVALID");				 
	}
	
	DWC_DEBUGPL(DBG_ANY, "%s(%d)\n", str, _pcd->ep0state);		  
#endif
}

/**
 * This function returns pointer to in ep struct with number ep_num
 */
static inline dwc_otg_pcd_ep_t* get_in_ep( dwc_otg_pcd_t *_pcd, uint32_t ep_num) 
{
	int i;
	int num_in_eps = GET_CORE_IF(_pcd)->dev_if->num_in_eps;
	if(ep_num == 0)
	{
		return &_pcd->ep0;
	}
	else
	{
		for(i = 0; i < num_in_eps; ++i)
		{
			if(_pcd->in_ep[i].dwc_ep.num == ep_num)
				return &_pcd->in_ep[i];
		}
		return 0;
	}
}
/**
 * This function returns pointer to out ep struct with number ep_num
 */
static inline dwc_otg_pcd_ep_t* get_out_ep( dwc_otg_pcd_t *_pcd, uint32_t ep_num) 
{
	int i;
	int num_out_eps = GET_CORE_IF(_pcd)->dev_if->num_out_eps;
	if(ep_num == 0)
	{
		return &_pcd->ep0;
	}
	else
	{
		for(i = 0; i < num_out_eps; ++i)
		{
			if(_pcd->out_ep[i].dwc_ep.num == ep_num)
				return &_pcd->out_ep[i];
		}
		return 0;
	}
}
/**
 * This functions gets a pointer to an EP from the wIndex address
 * value of the control request.
 */
static dwc_otg_pcd_ep_t *get_ep_by_addr (dwc_otg_pcd_t *_pcd, u16 _wIndex)
{
	dwc_otg_pcd_ep_t	*ep;

	if ((_wIndex & USB_ENDPOINT_NUMBER_MASK) == 0)
		return &_pcd->ep0;
	list_for_each_entry( ep, &_pcd->gadget.ep_list, ep.ep_list) 
	{
		u8	bEndpointAddress;

		if (!ep->desc)
			continue;
		bEndpointAddress = ep->desc->bEndpointAddress;
		if ((_wIndex ^ bEndpointAddress) & USB_DIR_IN)
			continue;
		if ((_wIndex & 0x0f) == (bEndpointAddress & 0x0f))
			return ep;
	}
	return NULL;
}

/**
 * This function checks the EP request queue, if the queue is not
 * empty the next request is started.
 */
void start_next_request( dwc_otg_pcd_ep_t *_ep )
{
	dwc_otg_pcd_request_t *req = 0;
		
	if (!list_empty(&_ep->queue))
	{
		req = list_entry(_ep->queue.next, 
			   dwc_otg_pcd_request_t, queue);
			
		/* Setup and start the Transfer */
		_ep->dwc_ep.start_xfer_buff = req->req.buf;
		_ep->dwc_ep.xfer_buff = req->req.buf;
		_ep->dwc_ep.xfer_len = req->req.length;
		_ep->dwc_ep.xfer_count = 0;
		_ep->dwc_ep.dma_addr = req->req.dma;
		_ep->dwc_ep.sent_zlp = 0;
		_ep->dwc_ep.total_len = _ep->dwc_ep.xfer_len;
	
		dwc_otg_ep_start_transfer( GET_CORE_IF(_ep->pcd), &_ep->dwc_ep );
	}
}

/**
 * This function handles the SOF Interrupts. At this time the SOF
 * Interrupt is disabled.
 */
int32_t dwc_otg_pcd_handle_sof_intr(dwc_otg_pcd_t *_pcd)
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(_pcd);

	gintsts_data_t gintsts;
		
	//DWC_DEBUGPL(DBG_PCD, "SOF\n");

	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.sofintr = 1;
	dwc_write_reg32 (&core_if->core_global_regs->gintsts, gintsts.d32);

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
int32_t dwc_otg_pcd_handle_rx_status_q_level_intr(dwc_otg_pcd_t *_pcd)
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(_pcd);
	dwc_otg_core_global_regs_t *global_regs = core_if->core_global_regs;
	gintmsk_data_t gintmask = {.d32=0};
	device_grxsts_data_t status;
	dwc_otg_pcd_ep_t *ep;
	gintsts_data_t gintsts;
#ifdef DEBUG
	static char *dpid_str[] ={ "D0", "D2", "D1", "MDATA" };
#endif
		
	//DWC_DEBUGPL(DBG_PCDV, "%s(%p)\n", __func__, _pcd);
	/* Disable the Rx Status Queue Level interrupt */
	gintmask.b.rxstsqlvl= 1;
	dwc_modify_reg32( &global_regs->gintmsk, gintmask.d32, 0);

	/* Get the Status from the top of the FIFO */
	status.d32 = dwc_read_reg32( &global_regs->grxstsp );

	DWC_DEBUGPL(DBG_PCD, "EP:%d BCnt:%d DPID:%s " 
					"pktsts:%x Frame:%d(0x%0x)\n", 
					status.b.epnum, status.b.bcnt,
					dpid_str[status.b.dpid], 
					status.b.pktsts, status.b.fn, status.b.fn);
	/* Get pointer to EP structure */
	ep = get_out_ep(_pcd, status.b.epnum);
//	  ep = &_pcd->out_ep[ status.b.epnum - 1];
		
	switch (status.b.pktsts) 
	{
	case DWC_DSTS_GOUT_NAK:
		DWC_DEBUGPL(DBG_PCDV, "Global OUT NAK\n");
		break;
	case DWC_STS_DATA_UPDT:
		DWC_DEBUGPL(DBG_PCDV, "OUT Data Packet\n");
		if (status.b.bcnt && ep->dwc_ep.xfer_buff)
		{
			/** @todo NGS Check for buffer overflow? */
			dwc_otg_read_packet( core_if, 
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
		dwc_otg_read_setup_packet( core_if, _pcd->setup_pkt->d32);
#ifdef DEBUG_EP0
		DWC_DEBUGPL(DBG_PCD, 
					"SETUP PKT: %02x.%02x v%04x i%04x l%04x\n",
					_pcd->setup_pkt->req.bRequestType, 
					_pcd->setup_pkt->req.bRequest,
					_pcd->setup_pkt->req.wValue, 
					_pcd->setup_pkt->req.wIndex, 
					_pcd->setup_pkt->req.wLength);
#endif
		ep->dwc_ep.xfer_count += status.b.bcnt;
		break;
	default:
		DWC_DEBUGPL(DBG_PCDV, "Invalid Packet Status (0x%0x)\n", 
						status.b.pktsts);
		break;
	}

	/* Enable the Rx Status Queue Level interrupt */
	dwc_modify_reg32( &global_regs->gintmsk, 0, gintmask.d32);
	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.rxstsqlvl = 1;
	dwc_write_reg32 (&global_regs->gintsts, gintsts.d32);

	//DWC_DEBUGPL(DBG_PCDV, "EXIT: %s\n", __func__);
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
 * @param _core_if Programming view of DWC_otg controller.
 *
 */
static inline int get_ep_of_last_in_token(dwc_otg_core_if_t *_core_if)
{
	dwc_otg_device_global_regs_t *dev_global_regs = 
			_core_if->dev_if->dev_global_regs;
	const uint32_t TOKEN_Q_DEPTH = _core_if->hwcfg2.b.dev_token_q_depth;
	/* Number of Token Queue Registers */
	const int DTKNQ_REG_CNT = (TOKEN_Q_DEPTH + 7) / 8;
	dtknq1_data_t dtknqr1;
	uint32_t in_tkn_epnums[4];
	int ndx = 0;
	int i = 0;
	volatile uint32_t *addr = &dev_global_regs->dtknqr1;
	int epnum = 0;

	//DWC_DEBUGPL(DBG_PCD,"dev_token_q_depth=%d\n",TOKEN_Q_DEPTH);
	
	
	/* Read the DTKNQ Registers */
	for (i = 0; i < DTKNQ_REG_CNT; i++) 
	{
		in_tkn_epnums[ i ] = dwc_read_reg32(addr);
		DWC_DEBUGPL(DBG_PCDV, "DTKNQR%d=0x%08x\n", i+1, 
					in_tkn_epnums[i]);
		if (addr == &dev_global_regs->dvbusdis) 
		{
			addr = &dev_global_regs->dtknqr3_dthrctl;
		} 
		else 
		{
			++addr;
		}
				
		}
		
		/* Copy the DTKNQR1 data to the bit field. */
		dtknqr1.d32 = in_tkn_epnums[0];
		/* Get the EP numbers */
		in_tkn_epnums[0] = dtknqr1.b.epnums0_5;
		ndx = dtknqr1.b.intknwptr - 1;

		//DWC_DEBUGPL(DBG_PCDV,"ndx=%d\n",ndx);
		if (ndx == -1) 
		{
			/** @todo Find a simpler way to calculate the max
			 * queue position.*/
			int cnt = TOKEN_Q_DEPTH;
			if (TOKEN_Q_DEPTH <= 6) 
			{
				cnt = TOKEN_Q_DEPTH - 1;
			}
			else if (TOKEN_Q_DEPTH <= 14) 
			{
				cnt = TOKEN_Q_DEPTH - 7;
			} 
			else if (TOKEN_Q_DEPTH <= 22) 
			{
				cnt = TOKEN_Q_DEPTH - 15;
			} 
			else 
			{
				cnt = TOKEN_Q_DEPTH - 23;
			}
			epnum = (in_tkn_epnums[ DTKNQ_REG_CNT - 1 ] >> (cnt * 4)) & 0xF;
		} 
		else 
		{
			if (ndx <= 5) 
			{
				epnum = (in_tkn_epnums[0] >> (ndx * 4)) & 0xF;
			} 
			else if (ndx <= 13 ) 
			{
				ndx -= 6;
				epnum = (in_tkn_epnums[1] >> (ndx * 4)) & 0xF;
			} 
			else if (ndx <= 21 ) 
			{
				ndx -= 14;
				epnum = (in_tkn_epnums[2] >> (ndx * 4)) & 0xF;
			} 
			else if (ndx <= 29 ) 
			{
				ndx -= 22;
				epnum = (in_tkn_epnums[3] >> (ndx * 4)) & 0xF;
			}
		}
		//DWC_DEBUGPL(DBG_PCD,"epnum=%d\n",epnum);
		return epnum;
}

/**
 * This interrupt occurs when the non-periodic Tx FIFO is half-empty.
 * The active request is checked for the next packet to be loaded into
 * the non-periodic Tx FIFO.
 */
int32_t dwc_otg_pcd_handle_np_tx_fifo_empty_intr(dwc_otg_pcd_t *_pcd)
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(_pcd);
	dwc_otg_core_global_regs_t *global_regs = 
			core_if->core_global_regs;
	dwc_otg_dev_in_ep_regs_t *ep_regs;
	gnptxsts_data_t txstatus = {.d32 = 0};
	gintsts_data_t gintsts;
	
	int epnum = 0;
	dwc_otg_pcd_ep_t *ep = 0;
	uint32_t len = 0;
	int dwords;

	/* Get the epnum from the IN Token Learning Queue. */
	epnum = get_ep_of_last_in_token(core_if);
	ep = get_in_ep(_pcd, epnum);
/*	
	if(epnum != 0)
		ep = &_pcd->in_ep[epnum-1];
	else
		ep = &_pcd->ep0;
*/
	DWC_DEBUGPL(DBG_PCD, "NP TxFifo Empty: %s(%d) \n", ep->ep.name, epnum );

	ep_regs = core_if->dev_if->in_ep_regs[epnum];
	
	len = ep->dwc_ep.xfer_len - ep->dwc_ep.xfer_count;
	if (len > ep->dwc_ep.maxpacket) 
	{
		len = ep->dwc_ep.maxpacket;
	}
	dwords = (len + 3)/4;
		
		
	/* While there is space in the queue and space in the FIFO and
	* More data to tranfer, Write packets to the Tx FIFO */
	txstatus.d32 = dwc_read_reg32( &global_regs->gnptxsts );
	DWC_DEBUGPL(DBG_PCDV, "b4 GNPTXSTS=0x%08x\n",txstatus.d32);
	
	while  (txstatus.b.nptxqspcavail > 0 &&
				txstatus.b.nptxfspcavail > dwords &&
				ep->dwc_ep.xfer_count < ep->dwc_ep.xfer_len) 
	{
		/* Write the FIFO */
		dwc_otg_ep_write_packet( core_if, &ep->dwc_ep, 0 );
		len = ep->dwc_ep.xfer_len - ep->dwc_ep.xfer_count;
		
		if (len > ep->dwc_ep.maxpacket) 
		{
			len = ep->dwc_ep.maxpacket;
		}
		
		dwords = (len + 3)/4;
		txstatus.d32 = dwc_read_reg32(&global_regs->gnptxsts);
		DWC_DEBUGPL(DBG_PCDV,"GNPTXSTS=0x%08x\n",txstatus.d32);
	}	
				
	DWC_DEBUGPL(DBG_PCDV, "GNPTXSTS=0x%08x\n",
					dwc_read_reg32( &global_regs->gnptxsts));

	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.nptxfempty = 1;
	dwc_write_reg32 (&global_regs->gintsts, gintsts.d32);

	return 1;
}

/**
 * This function is called when dedicated Tx FIFO Empty interrupt occurs.
 * The active request is checked for the next packet to be loaded into
 * apropriate Tx FIFO.
 */
static int32_t write_empty_tx_fifo(dwc_otg_pcd_t *_pcd, uint32_t epnum)
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(_pcd);
	dwc_otg_dev_if_t* dev_if = core_if->dev_if; 
	dwc_otg_dev_in_ep_regs_t *ep_regs;
	dtxfsts_data_t txstatus = {.d32 = 0};
	dwc_otg_pcd_ep_t *ep = 0;
	uint32_t len = 0;
	int dwords;

	ep = get_in_ep(_pcd, epnum);
/*
	if(epnum != 0)
		ep = &_pcd->in_ep[epnum-1];
	else
		ep = &_pcd->ep0;
*/
	DWC_DEBUGPL(DBG_PCD, "Dedicated TxFifo Empty: %s(%d) \n", ep->ep.name, epnum );

	ep_regs = core_if->dev_if->in_ep_regs[epnum];
		
	len = ep->dwc_ep.xfer_len - ep->dwc_ep.xfer_count;

	if (len > ep->dwc_ep.maxpacket) 
	{
		len = ep->dwc_ep.maxpacket;
	}
	
	dwords = (len + 3)/4;
		
	/* While there is space in the queue and space in the FIFO and
	 * More data to tranfer, Write packets to the Tx FIFO */
	txstatus.d32 = dwc_read_reg32( &dev_if->in_ep_regs[epnum]->dtxfsts);
	DWC_DEBUGPL(DBG_PCDV, "b4 dtxfsts[%d]=0x%08x\n",epnum,txstatus.d32);

	while  (txstatus.b.txfspcavail > dwords && 
			ep->dwc_ep.xfer_count < ep->dwc_ep.xfer_len &&
			ep->dwc_ep.xfer_len != 0) 
	{
		/* Write the FIFO */
		dwc_otg_ep_write_packet( core_if, &ep->dwc_ep, 0);

		len = ep->dwc_ep.xfer_len - ep->dwc_ep.xfer_count;
		if (len > ep->dwc_ep.maxpacket) 
		{
			len = ep->dwc_ep.maxpacket;
		}
				
		dwords = (len + 3)/4;
		txstatus.d32 = dwc_read_reg32(&dev_if->in_ep_regs[epnum]->dtxfsts);
		DWC_DEBUGPL(DBG_PCDV,"dtxfsts[%d]=0x%08x\n", epnum, txstatus.d32);
	}
				
	DWC_DEBUGPL(DBG_PCDV, "b4 dtxfsts[%d]=0x%08x\n",epnum,dwc_read_reg32(&dev_if->in_ep_regs[epnum]->dtxfsts));

	return 1;
}

 
/** 
 * This function is called when the Device is disconnected.	 It stops
 * any active requests and informs the Gadget driver of the
 * disconnect.
 */
void dwc_otg_pcd_stop(dwc_otg_pcd_t *_pcd)
{
	int i, num_in_eps, num_out_eps;
	dwc_otg_pcd_ep_t *ep;
	
	gintmsk_data_t intr_mask = {.d32 = 0};
    
    SPIN_LOCK(&_pcd->lock);

	num_in_eps = GET_CORE_IF(_pcd)->dev_if->num_in_eps;
	num_out_eps = GET_CORE_IF(_pcd)->dev_if->num_out_eps;
	
	DWC_DEBUGPL(DBG_PCDV, "%s() \n", __func__ );
	/* don't disconnect drivers more than once */
	if (_pcd->ep0state == EP0_DISCONNECT) 
	{
		SPIN_UNLOCK(&_pcd->lock);
		DWC_DEBUGPL(DBG_ANY, "%s() Already Disconnected\n", __func__ );
		return;		   
	}
	_pcd->ep0state = EP0_DISCONNECT;

	/* Reset the OTG state. */
	dwc_otg_pcd_update_otg( _pcd, 1);
		
	/* Disable the NP Tx Fifo Empty Interrupt. */
	intr_mask.b.nptxfempty = 1;
	dwc_modify_reg32(&GET_CORE_IF(_pcd)->core_global_regs->gintmsk, 
					 intr_mask.d32, 0);

	/* Flush the FIFOs */
	/**@todo NGS Flush Periodic FIFOs */
	dwc_otg_flush_tx_fifo( GET_CORE_IF(_pcd), 0);
	dwc_otg_flush_rx_fifo( GET_CORE_IF(_pcd) );

	/* prevent new request submissions, kill any outstanding requests  */
	ep = &_pcd->ep0;
	request_nuke(ep);
	/* prevent new request submissions, kill any outstanding requests  */
	for (i = 0; i < num_in_eps; i++) 
	{
		dwc_otg_pcd_ep_t *ep = &_pcd->in_ep[i];
		request_nuke(ep);
	}
	/* prevent new request submissions, kill any outstanding requests  */
	for (i = 0; i < num_out_eps; i++) 
	{
		dwc_otg_pcd_ep_t *ep = &_pcd->out_ep[i];
		request_nuke(ep);
	}

	/* report disconnect; the driver is already quiesced */
	if (_pcd->driver && _pcd->driver->disconnect) 
	{
		SPIN_UNLOCK(&_pcd->lock);
		_pcd->driver->disconnect(&_pcd->gadget);
		SPIN_LOCK(&_pcd->lock);
	}
		SPIN_UNLOCK(&_pcd->lock);
}

/**
 * This interrupt indicates that ...
 */
int32_t dwc_otg_pcd_handle_i2c_intr(dwc_otg_pcd_t *_pcd)
{
	gintmsk_data_t intr_mask = { .d32 = 0};
	gintsts_data_t gintsts;

	DWC_PRINT("INTERRUPT Handler not implemented for %s\n", "i2cintr"); 
	intr_mask.b.i2cintr = 1;
	dwc_modify_reg32( &GET_CORE_IF(_pcd)->core_global_regs->gintmsk, 
					  intr_mask.d32, 0 );

	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.i2cintr = 1;
	dwc_write_reg32 (&GET_CORE_IF(_pcd)->core_global_regs->gintsts, 
						 gintsts.d32);
	return 1;
}


/**
 * This interrupt indicates that ...
 */
int32_t dwc_otg_pcd_handle_early_suspend_intr(dwc_otg_pcd_t *_pcd)
{
	gintsts_data_t gintsts;
#if defined(VERBOSE)
	DWC_PRINT("Early Suspend Detected\n");
#endif
	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.erlysuspend = 1;
	dwc_write_reg32( &GET_CORE_IF(_pcd)->core_global_regs->gintsts, 
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
 * @param _core_if Programming view of DWC_otg controller.
 * @param _pcd	  Programming view of the PCD.
 */
static inline void ep0_out_start( dwc_otg_core_if_t *_core_if, dwc_otg_pcd_t *_pcd )
{
	dwc_otg_dev_if_t *dev_if = _core_if->dev_if;
	deptsiz0_data_t doeptsize0 = { .d32 = 0};

#ifdef VERBOSE
	DWC_DEBUGPL(DBG_PCDV,"%s() doepctl0=%0x\n", __func__,
				dwc_read_reg32(&dev_if->out_ep_regs[0]->doepctl));
#endif

	doeptsize0.b.supcnt = 3;	
	doeptsize0.b.pktcnt = 1;
	doeptsize0.b.xfersize = 8*3;
	
	dwc_write_reg32( &dev_if->out_ep_regs[0]->doeptsiz, 
					 doeptsize0.d32 );		 

	if (_core_if->dma_enable) 
	{
		depctl_data_t doepctl = { .d32 = 0 };
		/** @todo dma needs to handle multiple setup packets (up to 3) */
		dwc_write_reg32(&dev_if->out_ep_regs[0]->doepdma, 
		_pcd->setup_pkt_dma_handle);
		// EP enable
		doepctl.d32 = dwc_read_reg32(&dev_if->out_ep_regs[0]->doepctl);
		doepctl.b.epena = 1;

		doepctl.d32 = 0x80008000;
				dwc_write_reg32(&dev_if->out_ep_regs[0]->doepctl,
				doepctl.d32);
	}
#ifdef VERBOSE
		DWC_DEBUGPL(DBG_PCDV,"doepctl0=%0x\n", 
					dwc_read_reg32(&dev_if->out_ep_regs[0]->doepctl));
		DWC_DEBUGPL(DBG_PCDV,"diepctl0=%0x\n", 
					dwc_read_reg32(&dev_if->in_ep_regs[0]->diepctl));
#endif

}

/**
 * This interrupt occurs when a USB Reset is detected.	When the USB
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
int32_t dwc_otg_pcd_handle_usb_reset_intr( dwc_otg_pcd_t * _pcd)
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(_pcd);
	dwc_otg_dev_if_t *dev_if = core_if->dev_if;
	depctl_data_t doepctl = { .d32 = 0};
	daint_data_t daintmsk = { .d32 = 0};
	doepmsk_data_t doepmsk = { .d32 = 0};
	diepmsk_data_t diepmsk = { .d32 = 0};
	dcfg_data_t dcfg = { .d32=0 };
	grstctl_t resetctl = { .d32=0 };
	dctl_data_t dctl = {.d32=0};
	int i = 0;
	gintsts_data_t gintsts;
						
	DWC_PRINT("USB RESET\n");

	/* reset the HNP settings */
	dwc_otg_pcd_update_otg( _pcd, 1);

	/* Clear the Remote Wakeup Signalling */
	dctl.b.rmtwkupsig = 1;
	dwc_modify_reg32( &core_if->dev_if->dev_global_regs->dctl, 
					  dctl.d32, 0 );

	/* Set NAK for all OUT EPs */
	doepctl.b.snak = 1;
	for (i=0; i <= dev_if->num_out_eps; i++) 
	{
		dwc_write_reg32( &dev_if->out_ep_regs[i]->doepctl, 
						 doepctl.d32 );
	}

	/* Flush the NP Tx FIFO , CHANGE 0x0 to 0x10 to flush all fifo. */
	dwc_otg_flush_tx_fifo( core_if, 0x10 );
	/* Flush the Learning Queue */
	resetctl.b.intknqflsh = 1;
	dwc_write_reg32( &core_if->core_global_regs->grstctl, resetctl.d32);

	daintmsk.b.inep0 = 1;
	daintmsk.b.outep0 = 1;
	dwc_write_reg32( &dev_if->dev_global_regs->daintmsk, daintmsk.d32 );

	doepmsk.b.setup = 1;
	doepmsk.b.xfercompl = 1;
	doepmsk.b.ahberr = 1;
	doepmsk.b.epdisabled = 1;
	dwc_write_reg32( &dev_if->dev_global_regs->doepmsk, doepmsk.d32 );

	diepmsk.b.xfercompl = 1;
	diepmsk.b.timeout = 1;
	diepmsk.b.epdisabled = 1;
	diepmsk.b.ahberr = 1;
	diepmsk.b.intknepmis = 1;
	dwc_write_reg32( &dev_if->dev_global_regs->diepmsk, diepmsk.d32 );		 
	/* Reset Device Address */
	dcfg.d32 = dwc_read_reg32( &dev_if->dev_global_regs->dcfg);
	dcfg.b.devaddr = 0;
	dwc_write_reg32( &dev_if->dev_global_regs->dcfg, dcfg.d32);

	/* setup EP0 to receive SETUP packets */
	ep0_out_start( core_if, _pcd );
		
	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.usbreset = 1;
	dwc_write_reg32 (&core_if->core_global_regs->gintsts, gintsts.d32);

   	return 1;
}

/**
 * Get the device speed from the device status register and convert it
 * to USB speed constant.
 *
 * @param _core_if Programming view of DWC_otg controller.
 */
static int get_device_speed( dwc_otg_core_if_t *_core_if )
{
	dsts_data_t dsts;
	enum usb_device_speed speed = USB_SPEED_UNKNOWN;
	dsts.d32 = dwc_read_reg32(&_core_if->dev_if->dev_global_regs->dsts);

	switch (dsts.b.enumspd) 
	{
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
int32_t dwc_otg_pcd_handle_enum_done_intr(dwc_otg_pcd_t *_pcd)
{
	dwc_otg_pcd_ep_t *ep0 = &_pcd->ep0;
	gintsts_data_t gintsts;
	gusbcfg_data_t gusbcfg;
    volatile depctl_data_t depctl = {.d32 = 0};
	dwc_otg_core_global_regs_t *global_regs =
	GET_CORE_IF(_pcd)->core_global_regs;
	uint32_t gsnpsid = global_regs->gsnpsid;
	uint8_t utmi16b, utmi8b;
	DWC_DEBUGPL(DBG_PCD, "SPEED ENUM\n");

	if(gsnpsid >= (uint32_t)0x4f54260a)
	{
		utmi16b = 5;
		utmi8b = 9;
	}
	else
	{
		utmi16b = 4;
		utmi8b = 8;
	}
	dwc_otg_ep0_activate( GET_CORE_IF(_pcd), &ep0->dwc_ep );

#ifdef DEBUG_EP0
	print_ep0_state(_pcd);
#endif
		
	
	_pcd->ep0state = EP0_IDLE;

	ep0->stopped = 0;
		
	_pcd->gadget.speed = get_device_speed(GET_CORE_IF(_pcd));

	/* Set USB turnaround time based on device speed and PHY interface. */
	gusbcfg.d32 = dwc_read_reg32(&global_regs->gusbcfg);
	if (_pcd->gadget.speed == USB_SPEED_HIGH) 
	{
    	depctl.b.mps = 0x200;
		if (GET_CORE_IF(_pcd)->hwcfg2.b.hs_phy_type == DWC_HWCFG2_HS_PHY_TYPE_ULPI) 
		{
			/* ULPI interface */
			gusbcfg.b.usbtrdtim = 9;
		}
		if (GET_CORE_IF(_pcd)->hwcfg2.b.hs_phy_type == DWC_HWCFG2_HS_PHY_TYPE_UTMI) 
		{
/////
			/* UTMI+ interface */
			if (GET_CORE_IF(_pcd)->hwcfg4.b.utmi_phy_data_width == 0) 
			{
				gusbcfg.b.usbtrdtim = utmi8b;
			}
			else if (GET_CORE_IF(_pcd)->hwcfg4.b.utmi_phy_data_width == 1) 
			{
				gusbcfg.b.usbtrdtim = utmi16b;
			}
			else if (GET_CORE_IF(_pcd)->core_params->phy_utmi_width == 8) 
			{
				gusbcfg.b.usbtrdtim = utmi8b;
			} 
			else 
			{
				gusbcfg.b.usbtrdtim = utmi16b;
			}
		}
		if (GET_CORE_IF(_pcd)->hwcfg2.b.hs_phy_type == DWC_HWCFG2_HS_PHY_TYPE_UTMI_ULPI) 
		{
			/* UTMI+  OR  ULPI interface */
			if (gusbcfg.b.ulpi_utmi_sel == 1) 
			{
				/* ULPI interface */
				gusbcfg.b.usbtrdtim = 9;
			} 
			else 
			{
				/* UTMI+ interface */
				if (GET_CORE_IF(_pcd)->core_params->phy_utmi_width == 16) 
				{
					gusbcfg.b.usbtrdtim = utmi16b;
				} 
				else 
				{
					gusbcfg.b.usbtrdtim = utmi8b;
				}
			}
		}
	} 
	else 
	{
    	depctl.b.mps = 0x40;
		/* Full or low speed */
		gusbcfg.b.usbtrdtim = 9;
	}
	dwc_write_reg32(&global_regs->gusbcfg, gusbcfg.d32);

	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.enumdone = 1;
	dwc_write_reg32( &GET_CORE_IF(_pcd)->core_global_regs->gintsts, 
						 gintsts.d32 );
	/* enable ep2out */
	depctl.b.setd0pid = 1;
    dwc_write_reg32( &GET_CORE_IF(_pcd)->dev_if->in_ep_regs[1]->diepctl, depctl.d32);
	depctl.b.snak = 1;
	depctl.b.txfnum = 2;
	depctl.b.eptype = 2;
    depctl.b.usbactep = 1;
    dwc_write_reg32( &GET_CORE_IF(_pcd)->dev_if->out_ep_regs[2]->doepctl, depctl.d32 );
    dwc_write_reg32( &GET_CORE_IF(_pcd)->dev_if->out_ep_regs[4]->doepctl, depctl.d32 );
    dwc_write_reg32( &GET_CORE_IF(_pcd)->dev_if->out_ep_regs[6]->doepctl, depctl.d32 );
    dwc_write_reg32( &GET_CORE_IF(_pcd)->dev_if->out_ep_regs[8]->doepctl, depctl.d32 );
	return 1;
}

/**
 * This interrupt indicates that the ISO OUT Packet was dropped due to
 * Rx FIFO full or Rx Status Queue Full.  If this interrupt occurs
 * read all the data from the Rx FIFO.
 */
int32_t dwc_otg_pcd_handle_isoc_out_packet_dropped_intr(dwc_otg_pcd_t *_pcd )
{
	gintmsk_data_t intr_mask = { .d32 = 0};
	gintsts_data_t gintsts;
	
	DWC_PRINT("INTERRUPT Handler not implemented for %s\n",
			  "ISOC Out Dropped");

	intr_mask.b.isooutdrop = 1;
	dwc_modify_reg32( &GET_CORE_IF(_pcd)->core_global_regs->gintmsk, 
					  intr_mask.d32, 0 );

	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.isooutdrop = 1;
	dwc_write_reg32 (&GET_CORE_IF(_pcd)->core_global_regs->gintsts, 
						 gintsts.d32);

	return 1;
}

/**
 * This interrupt indicates the end of the portion of the micro-frame
 * for periodic transactions.  If there is a periodic transaction for
 * the next frame, load the packets into the EP periodic Tx FIFO.
 */
int32_t dwc_otg_pcd_handle_end_periodic_frame_intr(dwc_otg_pcd_t *_pcd )
{
	gintmsk_data_t intr_mask = { .d32 = 0};
	gintsts_data_t gintsts;
	DWC_PRINT("INTERRUPT Handler not implemented for %s\n",
			  "EOP");

	intr_mask.b.eopframe = 1;
	dwc_modify_reg32( &GET_CORE_IF(_pcd)->core_global_regs->gintmsk, 
					  intr_mask.d32, 0 );

	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.eopframe = 1;
	dwc_write_reg32( &GET_CORE_IF(_pcd)->core_global_regs->gintsts, 
						 gintsts.d32);

	return 1;
}

/**
 * This interrupt indicates that EP of the packet on the top of the
 * non-periodic Tx FIFO does not match EP of the IN Token received.
 *
 * The "Device IN Token Queue" Registers are read to determine the
 * order the IN Tokens have been received.	The non-periodic Tx FIFO
 * is flushed, so it can be reloaded in the order seen in the IN Token
 * Queue.
 */
int32_t dwc_otg_pcd_handle_ep_mismatch_intr(dwc_otg_core_if_t *_core_if)
{
	gintsts_data_t gintsts;
	DWC_DEBUGPL(DBG_PCDV, "%s(%p)\n", __func__, _core_if);

	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.epmismatch = 1;
	dwc_write_reg32 (&_core_if->core_global_regs->gintsts, gintsts.d32);

	return 1;
}

/**
 * This funcion stalls EP0.
 */
static inline void ep0_do_stall( dwc_otg_pcd_t *_pcd, const int err_val ) 
{
	dwc_otg_pcd_ep_t *ep0 = &_pcd->ep0;
	struct usb_ctrlrequest	*ctrl = &_pcd->setup_pkt->req;
	DWC_WARN("req %02x.%02x v%04x i%04x l%04x protocol STALL; err %d\n",
			 ctrl->bRequestType, ctrl->bRequest,ctrl->wValue, ctrl->wIndex, ctrl->wLength, err_val);		

	ep0->dwc_ep.is_in = 1;
	dwc_otg_ep_set_stall( _pcd->otg_dev->core_if, &ep0->dwc_ep );		
	_pcd->ep0.stopped = 1;
	_pcd->ep0state = EP0_IDLE;
	ep0_out_start( GET_CORE_IF(_pcd), _pcd );
}

/**
 * This functions delegates the setup command to the gadget driver.
 */
static inline void do_gadget_setup( dwc_otg_pcd_t *_pcd, 
									struct usb_ctrlrequest * _ctrl)
{
	int ret = 0;
	if (_pcd->driver && _pcd->driver->setup) 
	{
		SPIN_UNLOCK(&_pcd->lock);
		ret = _pcd->driver->setup(&_pcd->gadget, _ctrl);
		if(spin_is_locked(&_pcd->lock))
		    DWC_PRINT("%s warning: pcd->lock locked without unlock\n", __func__);
		SPIN_LOCK(&_pcd->lock);
		if (ret < 0) 
		{
			ep0_do_stall( _pcd, ret );
		}

		/** @todo This is a g_file_storage gadget driver specific
		 * workaround: a DELAYED_STATUS result from the fsg_setup
		 * routine will result in the gadget queueing a EP0 IN status
		 * phase for a two-stage control transfer.	Exactly the same as
		 * a SET_CONFIGURATION/SET_INTERFACE except that this is a class
		 * specific request.  Need a generic way to know when the gadget
		 * driver will queue the status phase.	Can we assume when we
		 * call the gadget driver setup() function that it will always
		 * queue and require the following flag?  Need to look into
		 * this.
		 */

		if (ret == 256 + 999) 
		{
			_pcd->request_config = 1;
		}
	}
}

/**
 * This function starts the Zero-Length Packet for the IN status phase
 * of a 2 stage control transfer. 
 */
static inline void do_setup_in_status_phase( dwc_otg_pcd_t *_pcd)
{
	dwc_otg_pcd_ep_t *ep0 = &_pcd->ep0;
	if (_pcd->ep0state == EP0_STALL)
	{
		return;
	}
			
	_pcd->ep0state = EP0_STATUS;
		
	/* Prepare for more SETUP Packets */
	DWC_DEBUGPL(DBG_PCD, "EP0 IN ZLP\n");
	ep0->dwc_ep.xfer_len = 0;
	ep0->dwc_ep.xfer_count = 0;
	ep0->dwc_ep.is_in = 1;
	ep0->dwc_ep.dma_addr = _pcd->setup_pkt_dma_handle;

	dwc_otg_ep0_start_transfer( GET_CORE_IF(_pcd), &ep0->dwc_ep );

	/* Prepare for more SETUP Packets */
	ep0_out_start( GET_CORE_IF(_pcd), _pcd );

}

/**
 * This function starts the Zero-Length Packet for the OUT status phase
 * of a 2 stage control transfer. 
 */
static inline void do_setup_out_status_phase( dwc_otg_pcd_t *_pcd)
{
	dwc_otg_pcd_ep_t *ep0 = &_pcd->ep0;
	if (_pcd->ep0state == EP0_STALL)
	{
		DWC_DEBUGPL(DBG_PCD, "EP0 STALLED\n");
		return;
	}
	_pcd->ep0state = EP0_STATUS;
		
	/* Prepare for more SETUP Packets */
	//ep0_out_start( GET_CORE_IF(_pcd), _pcd );

	DWC_DEBUGPL(DBG_PCD, "EP0 OUT ZLP\n");
	ep0->dwc_ep.xfer_len = 0;
	ep0->dwc_ep.xfer_count = 0;
	ep0->dwc_ep.is_in = 0;
	//ep0->dwc_ep.dma_addr = 0xffffffff;
	ep0->dwc_ep.dma_addr = _pcd->setup_pkt_dma_handle;
	dwc_otg_ep0_start_transfer( GET_CORE_IF(_pcd), &ep0->dwc_ep );

	/* Prepare for more SETUP Packets */
	ep0_out_start( GET_CORE_IF(_pcd), _pcd );

}

/**
 * Clear the EP halt (STALL) and if pending requests start the
 * transfer.
 */
static inline void pcd_clear_halt( dwc_otg_pcd_t *_pcd, dwc_otg_pcd_ep_t *_ep )
{
	if(_ep->dwc_ep.stall_clear_flag == 0)
		dwc_otg_ep_clear_stall( GET_CORE_IF(_pcd), &_ep->dwc_ep );

	/* Reactive the EP */
	dwc_otg_ep_activate( GET_CORE_IF(_pcd), &_ep->dwc_ep );
	if (_ep->stopped) 
	{
		_ep->stopped = 0;
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
		if (GET_CORE_IF(_pcd)->dma_enable) 
		{
			_ep->queue_sof = 1;
			tasklet_schedule (_pcd->start_xfer_tasklet);
		}
		else 
		{
#if 0
			_ep->queue_sof = 1;
			DWC_ERROR("tasklet schedule\n");
			tasklet_schedule (_pcd->start_xfer_tasklet);
			if (GET_CORE_IF(_pcd)->core_params->opt) 
			{
				start_next_request( _ep );
			}
#endif
		}
	} 
	/* Start Control Status Phase */
	do_setup_in_status_phase( _pcd );
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
static void do_test_mode( unsigned long _data )
{
	dctl_data_t		dctl;
	dwc_otg_pcd_t *pcd = (dwc_otg_pcd_t *)_data;
	dwc_otg_core_if_t *core_if = GET_CORE_IF(pcd);
	int test_mode = pcd->test_mode;
		

//	  DWC_WARN("%s() has not been tested since being rewritten!\n", __func__);		  
						
	dctl.d32 = dwc_read_reg32(&core_if->dev_if->dev_global_regs->dctl);
	switch (test_mode) 
	{
	case 1: // TEST_J
		dctl.b.tstctl = 1;
		break;

	case 2: // TEST_K	
		dctl.b.tstctl = 2;
		break;

	case 3: // TEST_SE0_NAK
		dctl.b.tstctl = 3;
		break;

	case 4: // TEST_PACKET
		dctl.b.tstctl = 4;
		break;

	case 5: // TEST_FORCE_ENABLE
		dctl.b.tstctl = 5;
		break;
	}
	dwc_write_reg32(&core_if->dev_if->dev_global_regs->dctl,
						dctl.d32);
}


/**
 * This function process the SET_FEATURE Setup Commands.
 */
static inline void do_set_feature( dwc_otg_pcd_t *_pcd )
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(_pcd);
	dwc_otg_core_global_regs_t *global_regs = 
			core_if->core_global_regs;
	struct usb_ctrlrequest	ctrl = _pcd->setup_pkt->req;
	dwc_otg_pcd_ep_t	*ep = 0;
	int32_t otg_cap_param = core_if->core_params->otg_cap;
	gotgctl_data_t gotgctl = { .d32 = 0 }; 

	DWC_DEBUGPL(DBG_PCD, "SET_FEATURE:%02x.%02x v%04x i%04x l%04x\n",
					ctrl.bRequestType, ctrl.bRequest,
					ctrl.wValue, ctrl.wIndex, ctrl.wLength);
	DWC_DEBUGPL(DBG_PCD,"otg_cap=%d\n", otg_cap_param);
		

	switch (ctrl.bRequestType & USB_RECIP_MASK) 
	{
	case USB_RECIP_DEVICE:
		switch (ctrl.wValue) 
		{
		case USB_DEVICE_REMOTE_WAKEUP:
			_pcd->remote_wakeup_enable = 1;
			break;

		case USB_DEVICE_TEST_MODE:
			/* Setup the Test Mode tasklet to do the Test
			 * Packet generation after the SETUP Status
			 * phase has completed. */

			/** @todo This has not been tested since the
			 * tasklet struct was put into the PCD
			 * struct! */
			_pcd->test_mode_tasklet.next = 0;
			_pcd->test_mode_tasklet.state = 0;
			atomic_set( &_pcd->test_mode_tasklet.count, 0);
			_pcd->test_mode_tasklet.func = do_test_mode;
			_pcd->test_mode_tasklet.data = (unsigned long)_pcd;
			_pcd->test_mode = ctrl.wIndex >> 8; 
			tasklet_schedule(&_pcd->test_mode_tasklet);
			break;

		case USB_DEVICE_B_HNP_ENABLE:
			DWC_DEBUGPL(DBG_PCDV, "SET_FEATURE: USB_DEVICE_B_HNP_ENABLE\n");
						
			/* dev may initiate HNP */
			if (otg_cap_param == DWC_OTG_CAP_PARAM_HNP_SRP_CAPABLE) 
			{
				_pcd->b_hnp_enable = 1;
				dwc_otg_pcd_update_otg( _pcd, 0 );
				DWC_DEBUGPL(DBG_PCD, "Request B HNP\n");
				/**@todo Is the gotgctl.devhnpen cleared
				 * by a USB Reset? */
				gotgctl.b.devhnpen = 1;
				gotgctl.b.hnpreq = 1;
				dwc_write_reg32( &global_regs->gotgctl, gotgctl.d32 );
			}
			else 
			{
				ep0_do_stall( _pcd, -EOPNOTSUPP);
			}						 
			break;

		case USB_DEVICE_A_HNP_SUPPORT:
			/* RH port supports HNP */
			DWC_DEBUGPL(DBG_PCDV, "SET_FEATURE: USB_DEVICE_A_HNP_SUPPORT\n");
			if (otg_cap_param == DWC_OTG_CAP_PARAM_HNP_SRP_CAPABLE)
			{
				_pcd->a_hnp_support = 1;
				dwc_otg_pcd_update_otg( _pcd, 0 );
			} 
			else 
			{
				ep0_do_stall( _pcd, -EOPNOTSUPP);
			}						 
			break;

		case USB_DEVICE_A_ALT_HNP_SUPPORT:
			/* other RH port does */
			DWC_DEBUGPL(DBG_PCDV, "SET_FEATURE: USB_DEVICE_A_ALT_HNP_SUPPORT\n");
			if (otg_cap_param == DWC_OTG_CAP_PARAM_HNP_SRP_CAPABLE)
			{
				_pcd->a_alt_hnp_support = 1;
				dwc_otg_pcd_update_otg( _pcd, 0 );
			} 
			else 
			{
				ep0_do_stall( _pcd, -EOPNOTSUPP);
			}						 
			break;
		}
		do_setup_in_status_phase( _pcd );
		break;

	case USB_RECIP_INTERFACE:
		do_gadget_setup(_pcd, &ctrl );
		break;

	case USB_RECIP_ENDPOINT:
		if (ctrl.wValue == USB_ENDPOINT_HALT) 
		{
			ep = get_ep_by_addr(_pcd, ctrl.wIndex);
			if (ep == 0) 
			{
				ep0_do_stall(_pcd, -EOPNOTSUPP);
				return;
			} 
			ep->stopped = 1;
			dwc_otg_ep_set_stall( core_if, &ep->dwc_ep );
		}
		do_setup_in_status_phase( _pcd );
		break;
	}
}

/**
 * This function process the CLEAR_FEATURE Setup Commands.
 */
static inline void do_clear_feature( dwc_otg_pcd_t *_pcd )
{
	struct usb_ctrlrequest	ctrl = _pcd->setup_pkt->req;
	dwc_otg_pcd_ep_t	*ep = 0;


	DWC_DEBUGPL(DBG_PCD, 
				"CLEAR_FEATURE:%02x.%02x v%04x i%04x l%04x\n",
				ctrl.bRequestType, ctrl.bRequest,
				ctrl.wValue, ctrl.wIndex, ctrl.wLength);

	switch (ctrl.bRequestType & USB_RECIP_MASK) 
	{
	case USB_RECIP_DEVICE:
		switch (ctrl.wValue) 
		{
		case USB_DEVICE_REMOTE_WAKEUP:
			_pcd->remote_wakeup_enable = 0;
			break;

		case USB_DEVICE_TEST_MODE:
			/** @todo Add CLEAR_FEATURE for TEST modes. */
			break;
		}
		do_setup_in_status_phase( _pcd );
		break;

	case USB_RECIP_ENDPOINT:
		ep = get_ep_by_addr(_pcd, ctrl.wIndex);
		if (ep == 0) 
		{
			ep0_do_stall(_pcd, -EOPNOTSUPP);
			return;
		} 

		pcd_clear_halt(_pcd, ep );

		break;
	}
}

/**
 *	This function processes SETUP commands.	 In Linux, the USB Command
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
 * pcd_setup processes the gadget SETUP commands.
 */
static inline void pcd_setup( dwc_otg_pcd_t *_pcd )
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(_pcd);
	dwc_otg_dev_if_t *dev_if = core_if->dev_if;
	struct usb_ctrlrequest	ctrl = _pcd->setup_pkt->req;
	dwc_otg_pcd_ep_t	*ep;
	dwc_otg_pcd_ep_t	*ep0 = &_pcd->ep0;
	uint16_t				*status = _pcd->status_buf;

	deptsiz0_data_t doeptsize0 = { .d32 = 0};

#ifdef DEBUG_EP0
	DWC_DEBUGPL(DBG_PCD, "SETUP %02x.%02x v%04x i%04x l%04x\n",
					ctrl.bRequestType, ctrl.bRequest,
					ctrl.wValue, ctrl.wIndex, ctrl.wLength);
#endif

	doeptsize0.d32 = dwc_read_reg32( &dev_if->out_ep_regs[0]->doeptsiz );

	/** @todo handle > 1 setup packet , assert error for now */
	if (core_if->dma_enable && (doeptsize0.b.supcnt < 2)) 
	{
		DWC_ERROR ("\n\n-----------	 CANNOT handle > 1 setup packet in DMA mode\n\n");
	}

	/* Clean up the request queue */
	request_nuke( ep0 );
	ep0->stopped = 0;

	if (ctrl.bRequestType & USB_DIR_IN) 
	{		 
		ep0->dwc_ep.is_in = 1;
		_pcd->ep0state = EP0_IN_DATA_PHASE;
	} 
	else 
	{
		ep0->dwc_ep.is_in = 0;
		_pcd->ep0state = EP0_OUT_DATA_PHASE;
	}

	if ((ctrl.bRequestType & USB_TYPE_MASK) != USB_TYPE_STANDARD) 
	{
		/* handle non-standard (class/vendor) requests in the gadget driver */
		do_gadget_setup(_pcd, &ctrl );
		return;
	}

	/** @todo NGS: Handle bad setup packet? */

	switch (ctrl.bRequest) 
	{
		case USB_REQ_GET_STATUS:
#ifdef DEBUG_EP0
		DWC_DEBUGPL(DBG_PCD, 
						"GET_STATUS %02x.%02x v%04x i%04x l%04x\n",
						ctrl.bRequestType, ctrl.bRequest,
						ctrl.wValue, ctrl.wIndex, ctrl.wLength);
#endif

		switch (ctrl.bRequestType & USB_RECIP_MASK) 
		{
		case USB_RECIP_DEVICE:
			*status = 0x1; /* Self powered */
			*status |= _pcd->remote_wakeup_enable << 1; 
			break;

		case USB_RECIP_INTERFACE:
			*status = 0;
			break;						  

		case USB_RECIP_ENDPOINT:
			ep = get_ep_by_addr(_pcd, ctrl.wIndex);
			if ( ep == 0 || ctrl.wLength > 2) 
			{
				ep0_do_stall(_pcd, -EOPNOTSUPP);
				return;
			}
			/** @todo check for EP stall */
			*status = ep->stopped;
			break;
		}
		_pcd->ep0_pending = 1;

		ep0->dwc_ep.start_xfer_buff = (uint8_t *)status;
		ep0->dwc_ep.xfer_buff = (uint8_t *)status;
		ep0->dwc_ep.dma_addr = _pcd->status_buf_dma_handle;
		ep0->dwc_ep.xfer_len = 2;
		ep0->dwc_ep.xfer_count = 0;
		ep0->dwc_ep.total_len = ep0->dwc_ep.xfer_len;
		dwc_otg_ep0_start_transfer( GET_CORE_IF(_pcd), &ep0->dwc_ep );
		break;

	case USB_REQ_CLEAR_FEATURE:
		do_clear_feature( _pcd );
		break;

	case USB_REQ_SET_FEATURE:
		do_set_feature( _pcd );
		break;
				
	case USB_REQ_SET_ADDRESS:
		if (ctrl.bRequestType == USB_RECIP_DEVICE) 
		{
			dcfg_data_t dcfg = {.d32=0};
						
#ifdef DEBUG_EP0
			DWC_DEBUGPL(DBG_PCDV, "SET_ADDRESS:%d\n", ctrl.wValue);
#endif
			dcfg.b.devaddr = ctrl.wValue;
			dwc_modify_reg32( &dev_if->dev_global_regs->dcfg, 
										  0, dcfg.d32);
			do_setup_in_status_phase( _pcd );
			return;
		}
		break;

	case USB_REQ_SET_INTERFACE:
	case USB_REQ_SET_CONFIGURATION:
			_pcd->request_config = 1;	/* Configuration changed */
			do_gadget_setup(_pcd, &ctrl );
			break;
				
	case USB_REQ_SYNCH_FRAME:
			do_gadget_setup(_pcd, &ctrl );
			break;

	default:
			/* Call the Gadget Driver's setup functions */		  
			do_gadget_setup(_pcd, &ctrl );
		break;
	}
}

/**
 * This function completes the ep0 control transfer.
 */
static int32_t ep0_complete_request( dwc_otg_pcd_ep_t *_ep )
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(_ep->pcd);
	dwc_otg_dev_if_t *dev_if = core_if->dev_if;
	dwc_otg_dev_in_ep_regs_t *in_ep_regs = 
	dev_if->in_ep_regs[_ep->dwc_ep.num];
#ifdef DEBUG_EP0
	dwc_otg_dev_out_ep_regs_t *out_ep_regs = 
			dev_if->out_ep_regs[_ep->dwc_ep.num];
#endif
	deptsiz0_data_t deptsiz;
	dwc_otg_pcd_request_t *req;
	int is_last = 0;
	dwc_otg_pcd_t *pcd = _ep->pcd;
		
	DWC_DEBUGPL(DBG_PCDV, "%s() %s\n", __func__, _ep->ep.name);

	if (pcd->ep0_pending && list_empty(&_ep->queue)) 
	{
		if (_ep->dwc_ep.is_in) 
		{
#ifdef DEBUG_EP0
			DWC_DEBUGPL(DBG_PCDV, "Do setup OUT status phase\n");
#endif
			do_setup_out_status_phase(pcd);
		} 
		else 
		{
#ifdef DEBUG_EP0
			DWC_DEBUGPL(DBG_PCDV, "Do setup IN status phase\n");
#endif
			do_setup_in_status_phase(pcd);
		}
		pcd->ep0_pending = 0;
		pcd->ep0state = EP0_STATUS;
		return 1;
	}
	
	if (list_empty(&_ep->queue)) 
	{
		return 0;
	}
	req = list_entry(_ep->queue.next, dwc_otg_pcd_request_t, queue);


	if (pcd->ep0state == EP0_STATUS) 
	{
		is_last = 1;						
	} 
/*
	else if (req->req.zero) 
	{
		DWC_PRINT("%s--------------------------------------------------\n",__func__);
		req->req.actual = _ep->dwc_ep.xfer_count;
		//do_setup_in_status_phase (pcd);
		req->req.zero = 0;
		_ep->dwc_ep.xfer_len = 0;
		_ep->dwc_ep.xfer_count = 0;
		_ep->dwc_ep.sent_zlp = 1;
		dwc_otg_ep0_start_transfer( GET_CORE_IF(pcd), &_ep->dwc_ep );
		return 1;
	}
*/
	else if (_ep->dwc_ep.is_in) 
	{
		deptsiz.d32 = dwc_read_reg32( &in_ep_regs->dieptsiz);
#ifdef DEBUG_EP0
		DWC_DEBUGPL(DBG_PCDV, "%s len=%d  xfersize=%d pktcnt=%d\n", 
						_ep->ep.name, _ep->dwc_ep.xfer_len,
						deptsiz.b.xfersize, deptsiz.b.pktcnt);
#endif				  
		if (deptsiz.b.xfersize == 0) 
		{
			req->req.actual = _ep->dwc_ep.xfer_count;
			/* Is a Zero Len Packet needed? */
			//if (req->req.zero) {
#ifdef DEBUG_EP0
			DWC_DEBUGPL(DBG_PCD, "Setup Rx ZLP\n");
#endif
			do_setup_out_status_phase(pcd);
		}
	} 
	else 
	{
		/* ep0-OUT */
#ifdef DEBUG_EP0
		deptsiz.d32 = dwc_read_reg32( &out_ep_regs->doeptsiz);
		DWC_DEBUGPL(DBG_PCDV, "%s len=%d xsize=%d pktcnt=%d\n",
						_ep->ep.name, _ep->dwc_ep.xfer_len,
						deptsiz.b.xfersize, 
						deptsiz.b.pktcnt);
#endif
		req->req.actual = _ep->dwc_ep.xfer_count;
		
		/* Is a Zero Len Packet needed? */
		//if (req->req.zero) {
#ifdef DEBUG_EP0
		DWC_DEBUGPL(DBG_PCDV, "Setup Tx ZLP\n");
#endif
		do_setup_in_status_phase(pcd);
	}
		
	/* Complete the request */
	if (is_last) 
	{
		request_done(_ep, req, 0);
		_ep->dwc_ep.start_xfer_buff = 0;
		_ep->dwc_ep.xfer_buff = 0;
		_ep->dwc_ep.xfer_len = 0;
		return 1;
	}
	return 0;		
}

/**
 * This function completes the request for the EP.	If there are
 * additional requests for the EP in the queue they will be started.
 */
static void complete_ep( dwc_otg_pcd_ep_t *_ep )
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(_ep->pcd);
	dwc_otg_dev_if_t *dev_if = core_if->dev_if;
	dwc_otg_dev_in_ep_regs_t *in_ep_regs = 
	dev_if->in_ep_regs[_ep->dwc_ep.num];
	deptsiz_data_t deptsiz;
	dwc_otg_pcd_request_t *req = 0;
	int is_last = 0;
	
	DWC_DEBUGPL(DBG_PCDV,"%s() %s-%s\n", __func__, _ep->ep.name,
					(_ep->dwc_ep.is_in?"IN":"OUT"));

	/* Get any pending requests */
	if (!list_empty(&_ep->queue)) 
	{
		req = list_entry(_ep->queue.next, dwc_otg_pcd_request_t, 
								 queue);
	}
	DWC_DEBUGPL(DBG_PCD, "Requests %d\n",_ep->pcd->request_pending);

	if (_ep->dwc_ep.is_in) 
	{
		deptsiz.d32 = dwc_read_reg32( &in_ep_regs->dieptsiz);

		if (core_if->dma_enable) 
		{
			if (deptsiz.b.xfersize == 0) 
				_ep->dwc_ep.xfer_count = _ep->dwc_ep.xfer_len;
		}

		DWC_DEBUGPL(DBG_PCDV, "%s len=%d  xfersize=%d pktcnt=%d\n", 
						_ep->ep.name, _ep->dwc_ep.xfer_len,
						deptsiz.b.xfersize, deptsiz.b.pktcnt);

		if (deptsiz.b.xfersize == 0 && deptsiz.b.pktcnt == 0 &&
				_ep->dwc_ep.xfer_count == _ep->dwc_ep.xfer_len) 
		{
			is_last = 1;
		} 
		else 
		{
			DWC_WARN("Incomplete transfer (%s-%s [siz=%d pkt=%d])\n", 
					 _ep->ep.name, (_ep->dwc_ep.is_in?"IN":"OUT"),
					 deptsiz.b.xfersize, deptsiz.b.pktcnt);
		}
	} 
	else 
	{
		dwc_otg_dev_out_ep_regs_t *out_ep_regs = 
					dev_if->out_ep_regs[_ep->dwc_ep.num];
		deptsiz.d32 = 0;
		deptsiz.d32 = dwc_read_reg32( &out_ep_regs->doeptsiz);

#ifdef DEBUG
			
		DWC_DEBUGPL(DBG_PCDV, "addr %p,	 %s len=%d cnt=%d xsize=%d pktcnt=%d\n",
						&out_ep_regs->doeptsiz, _ep->ep.name, _ep->dwc_ep.xfer_len,
						_ep->dwc_ep.xfer_count,
						deptsiz.b.xfersize, 
						deptsiz.b.pktcnt);
#endif
		is_last = 1;
	}
		
	/* Complete the request */
	if (is_last) 
	{

        /* HSL @RK,20090807 */
        if( req ) {
			if (core_if->dma_enable) 
			{
				req->req.actual = _ep->dwc_ep.xfer_len - deptsiz.b.xfersize;
			}
			else 
			{
				req->req.actual = _ep->dwc_ep.xfer_count;
			}
			request_done(_ep, req, 0);
        } else {
            DWC_PRINT("\n++++++FIND NULL req,ep=%s++++++++++\n" , _ep->ep.name );
            _ep->pcd->request_pending = 0;
        }
		_ep->dwc_ep.start_xfer_buff = 0;
		_ep->dwc_ep.xfer_buff = 0;
		_ep->dwc_ep.xfer_len = 0;

		/* If there is a request in the queue start it.*/
		start_next_request( _ep );
	}
}

/**
 * This function handles EP0 Control transfers.	 
 *
 * The state of the control tranfers are tracked in
 * <code>ep0state</code>.
 */
static void handle_ep0( dwc_otg_pcd_t *_pcd )
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(_pcd);
	dwc_otg_pcd_ep_t *ep0 = &_pcd->ep0;
	deptsiz_data_t deptsiz;

#ifdef DEBUG_EP0
	DWC_DEBUGPL(DBG_PCDV, "%s()\n", __func__);
	print_ep0_state(_pcd);
#endif

	switch (_pcd->ep0state)
	{
	case EP0_DISCONNECT:
		break;
				
	case EP0_IDLE:
		_pcd->request_config = 0;
				
		pcd_setup( _pcd );
		break;

	case EP0_IN_DATA_PHASE:
#ifdef DEBUG_EP0
		DWC_DEBUGPL(DBG_PCD, "DATA_IN EP%d-%s: type=%d, mps=%d\n", 
						ep0->dwc_ep.num, (ep0->dwc_ep.is_in ?"IN":"OUT"),
						ep0->dwc_ep.type, ep0->dwc_ep.maxpacket );
#endif				  
		if (core_if->dma_enable) 
		{
			/* 
			 * For EP0 we can only program 1 packet at a time so we
			 * need to do the make calculations after each complete.
			 * Call write_packet to make the calculations, as in
			 * slave mode, and use those values to determine if we
			 * can complete.
			 */
			dwc_otg_ep_write_packet (core_if, &ep0->dwc_ep, 1);
		}
		if (ep0->dwc_ep.xfer_count < ep0->dwc_ep.total_len) 
		{
			dwc_otg_ep0_continue_transfer ( GET_CORE_IF(_pcd), &ep0->dwc_ep );
			DWC_DEBUGPL(DBG_PCD, "CONTINUE TRANSFER\n"); 
		}
		else 
		{		
			ep0_complete_request( ep0 );
			DWC_DEBUGPL(DBG_PCD, "COMPLETE TRANSFER\n"); 
		}
		break;
	case EP0_OUT_DATA_PHASE:
#ifdef DEBUG_EP0
		DWC_DEBUGPL(DBG_PCD, "DATA_OUT EP%d-%s: type=%d, mps=%d\n", 
						ep0->dwc_ep.num, (ep0->dwc_ep.is_in ?"IN":"OUT"),
						ep0->dwc_ep.type, ep0->dwc_ep.maxpacket );
#endif
        deptsiz.d32 = dwc_read_reg32( &core_if->dev_if->in_ep_regs[0]->dieptsiz);
        ep0->dwc_ep.xfer_count = ep0->dwc_ep.xfer_len - deptsiz.b.xfersize;
		ep0_complete_request( ep0 );
		break;
				

	case EP0_STATUS:
		DWC_DEBUGPL(DBG_PCD, "CASE: EP0_STATUS\n"); 
						ep0_complete_request( ep0 );
						_pcd->ep0state = EP0_IDLE;
						ep0->stopped = 1;
						ep0->dwc_ep.is_in = 0;	/* OUT for next SETUP */

		/* Prepare for more SETUP Packets */
		if (core_if->dma_enable) 
		{
			ep0_out_start( core_if, _pcd );
		}

		if (!GET_CORE_IF(_pcd)->dma_enable) 
		{	
			int i;

			depctl_data_t diepctl;
			diepctl.d32 = dwc_read_reg32( &core_if->dev_if->in_ep_regs[0]->diepctl);
			
			if (_pcd->ep0.queue_sof) 
			{
				_pcd->ep0.queue_sof = 0;
				start_next_request (&_pcd->ep0);
			}
			
			diepctl.d32 = dwc_read_reg32( &core_if->dev_if->in_ep_regs[0]->diepctl);
			
			if (_pcd->ep0.queue_sof) 
			{
				_pcd->ep0.queue_sof = 0;
				start_next_request (&_pcd->ep0);
			}
			for (i=0; i < core_if->dev_if->num_in_eps; i++) 
			{
				diepctl.d32 = dwc_read_reg32( &core_if->dev_if->in_ep_regs[i+1]->diepctl);
				
				if (_pcd->in_ep[i].queue_sof) 
				{
					_pcd->in_ep[i].queue_sof = 0;
					start_next_request (&_pcd->in_ep[i]);
				}
			}
		}
		break;

	case EP0_STALL:
		DWC_ERROR("EP0 STALLed, should not get here pcd_setup()\n");
		break;
	}
#ifdef DEBUG_EP0
	print_ep0_state(_pcd);
#endif
}

/**
 * Restart transfer
 */
static void restart_transfer( dwc_otg_pcd_t *_pcd, const uint32_t _epnum)
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(_pcd);
	dwc_otg_dev_if_t *dev_if = core_if->dev_if;
	deptsiz_data_t dieptsiz = {.d32=0};
	//depctl_data_t diepctl = {.d32=0};
	dwc_otg_pcd_ep_t *ep;

	dieptsiz.d32 = dwc_read_reg32(&dev_if->in_ep_regs[_epnum]->dieptsiz);

	ep = get_in_ep(_pcd, _epnum);
/*
	if(_epnum != 0)
		ep = &_pcd->in_ep[ _epnum - 1];	   
	else
		ep = &_pcd->ep0;
*/	  
	DWC_DEBUGPL(DBG_PCD,"xfer_buff=%p xfer_count=%0x xfer_len=%0x" 
				" stopped=%d\n", ep->dwc_ep.xfer_buff,
				ep->dwc_ep.xfer_count, ep->dwc_ep.xfer_len ,
				ep->stopped);
	/*
	 * If xfersize is 0 and pktcnt in not 0, resend the last packet.
	 */
	if ( dieptsiz.b.pktcnt && dieptsiz.b.xfersize == 0 &&
		 ep->dwc_ep.start_xfer_buff != 0) 
	{
		if ( ep->dwc_ep.xfer_len <= ep->dwc_ep.maxpacket ) 
		{
			ep->dwc_ep.xfer_count = 0;
			ep->dwc_ep.xfer_buff = ep->dwc_ep.start_xfer_buff;
		} 
		else 
		{
			ep->dwc_ep.xfer_count -= ep->dwc_ep.maxpacket;
			/* convert packet size to dwords. */
			ep->dwc_ep.xfer_buff -= ep->dwc_ep.maxpacket;
		}
		ep->stopped = 0;
		DWC_DEBUGPL(DBG_PCD,"xfer_buff=%p xfer_count=%0x "
						"xfer_len=%0x stopped=%d\n", 
						ep->dwc_ep.xfer_buff,
						ep->dwc_ep.xfer_count, ep->dwc_ep.xfer_len ,
						ep->stopped
						);
		if (_epnum == 0) 
		{
			dwc_otg_ep0_start_transfer(core_if, &ep->dwc_ep);
		} 
		else 
		{
			dwc_otg_ep_start_transfer(core_if, &ep->dwc_ep);
		}
	}
}


/**
 * handle the IN EP disable interrupt.
 */
static inline void handle_in_ep_disable_intr(dwc_otg_pcd_t *_pcd, 
											 const uint32_t _epnum)
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(_pcd);
	dwc_otg_dev_if_t *dev_if = core_if->dev_if;
	deptsiz_data_t dieptsiz = {.d32=0};
	dctl_data_t dctl = {.d32=0};
	dwc_otg_pcd_ep_t *ep;
	dwc_ep_t *dwc_ep;

	ep = get_in_ep(_pcd, _epnum);
	dwc_ep = &ep->dwc_ep;
/*
	if(_epnum != 0)
	{	
		ep = &_pcd->in_ep[ _epnum - 1];
		dwc_ep = &_pcd->in_ep[ _epnum - 1].dwc_ep;
	}
	else
	{
		ep = &_pcd->ep0;
		dwc_ep = &_pcd->ep0.dwc_ep;
	}
*/
	DWC_DEBUGPL(DBG_PCD,"diepctl%d=%0x\n", _epnum,
	dwc_read_reg32(&dev_if->in_ep_regs[_epnum]->diepctl));
	dieptsiz.d32 = dwc_read_reg32(&dev_if->in_ep_regs[_epnum]->dieptsiz);

	DWC_DEBUGPL(DBG_ANY, "pktcnt=%d size=%d\n",
				dieptsiz.b.pktcnt, 
				dieptsiz.b.xfersize );

	if (ep->stopped) 
	{
		/* Flush the Tx FIFO */
		/** @todo NGS: This is not the correct FIFO */
                         /* 20091009,HSL@RK,our dwc fifo work at dedicated mode, use 0x10 to flush all , not 0 */
		dwc_otg_flush_tx_fifo( core_if, 0x10 );
                
		/* Clear the Global IN NP NAK */
		dctl.d32 = 0;
		dctl.b.cgnpinnak = 1;
		dwc_modify_reg32(&dev_if->dev_global_regs->dctl, 
						 dctl.d32, 0);
		/* Restart the transaction */
		if (dieptsiz.b.pktcnt != 0 || 
			dieptsiz.b.xfersize != 0) 
		{
			restart_transfer( _pcd, _epnum );
		}
	} 
	else 
	{
		/* Restart the transaction */
		if (dieptsiz.b.pktcnt != 0 || 
			dieptsiz.b.xfersize != 0) 
		{
			restart_transfer( _pcd, _epnum );
		}
		DWC_DEBUGPL(DBG_ANY, "STOPPED!!!\n");
	}								 
}

/**
 * Handler for the IN EP timeout handshake interrupt. 
 */
static inline void handle_in_ep_timeout_intr(dwc_otg_pcd_t *_pcd, 
											 const uint32_t _epnum)
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(_pcd);
	dwc_otg_dev_if_t *dev_if = core_if->dev_if;

#ifdef DEBUG
	deptsiz_data_t dieptsiz = {.d32=0};
	uint32_t epnum = 0;
#endif
	dctl_data_t dctl = {.d32=0};
	dwc_otg_pcd_ep_t *ep;

	gintmsk_data_t intr_mask = {.d32 = 0};

	ep = get_in_ep(_pcd, _epnum);
/*
	if(_epnum != 0)
		ep = &_pcd->in_ep[ _epnum - 1];
	else
		ep = &_pcd->ep0;
*/		

	/* Disable the NP Tx Fifo Empty Interrrupt */
	if (!core_if->dma_enable) 
	{
		intr_mask.b.nptxfempty = 1;
		dwc_modify_reg32( &core_if->core_global_regs->gintmsk, intr_mask.d32, 0);
	}
	/** @todo NGS Check EP type.
	 * Implement for Periodic EPs */
	/* 
	 * Non-periodic EP 
	 */
	/* Enable the Global IN NAK Effective Interrupt */
	intr_mask.d32 = 0;			/* Bug fixed - clear mask b4 reusing */
	intr_mask.b.ginnakeff = 1;
	dwc_modify_reg32( &core_if->core_global_regs->gintmsk, 
					  0, intr_mask.d32);

	/* Set Global IN NAK */
	dctl.b.sgnpinnak = 1;
	dwc_modify_reg32(&dev_if->dev_global_regs->dctl,
					 dctl.d32, dctl.d32);

	ep->stopped = 1;

#ifdef DEBUG
	dieptsiz.d32 = dwc_read_reg32(&dev_if->in_ep_regs[epnum]->dieptsiz);
	DWC_DEBUGPL(DBG_ANY, "pktcnt=%d size=%d\n",
				dieptsiz.b.pktcnt, 
				dieptsiz.b.xfersize );
#endif

#ifdef DISABLE_PERIODIC_EP
	/*
	 * Set the NAK bit for this EP to
	 * start the disable process.
	 */
	diepctl.d32 = 0;
	diepctl.b.snak = 1;
	dwc_modify_reg32(&dev_if->in_ep_regs[epnum]->diepctl, diepctl.d32, diepctl.d32);
	ep->disabling = 1;
	ep->stopped = 1;
#endif
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
static int32_t dwc_otg_pcd_handle_in_ep_intr(dwc_otg_pcd_t *_pcd)
{
#define CLEAR_IN_EP_INTR(__core_if,__epnum,__intr) \
do { \
		diepint_data_t diepint = {.d32=0}; \
		diepint.b.__intr = 1; \
		dwc_write_reg32(&__core_if->dev_if->in_ep_regs[__epnum]->diepint, \
		diepint.d32); \
} while (0)

	dwc_otg_core_if_t *core_if = GET_CORE_IF(_pcd);
	dwc_otg_dev_if_t *dev_if = core_if->dev_if;
	diepint_data_t diepint = {.d32=0};
	depctl_data_t diepctl = {.d32=0};
	uint32_t ep_intr;
	uint32_t epnum = 0;
	dwc_otg_pcd_ep_t *ep;
	dwc_ep_t *dwc_ep;
	uint32_t _empty_msk, _diepctl;
	gintmsk_data_t intr_mask = {.d32 = 0};

	
	
	DWC_DEBUGPL(DBG_PCDV, "%s(%p)\n", __func__, _pcd);

	/* Read in the device interrupt bits */
	ep_intr = dwc_otg_read_dev_all_in_ep_intr( core_if );

	/* Service the Device IN interrupts for each endpoint */
	while( ep_intr ) 
	{
		if (ep_intr&0x1) 
		{

			
			/* Get EP pointer */	   
			ep = get_in_ep(_pcd, epnum);
			if(ep == NULL){
                DWC_PRINT("%s epnum %d epintr %x\n", __func__, epnum, ep_intr);
                break;
			}
			dwc_ep = &ep->dwc_ep;

			_diepctl = dwc_read_reg32(&dev_if->in_ep_regs[epnum]->diepctl);
			_empty_msk = dwc_read_reg32(&dev_if->dev_global_regs->dtknqr4_fifoemptymsk);

			DWC_DEBUGPL(DBG_PCDV, "IN EP INTERRUPT - %d\nepmty_msk - %8x  diepctl - %8x\n",epnum, _empty_msk, _diepctl);

			DWC_DEBUGPL(DBG_PCD, 
						"EP%d-%s: type=%d, mps=%d\n", 
						dwc_ep->num, (dwc_ep->is_in ?"IN":"OUT"),
						dwc_ep->type, dwc_ep->maxpacket );

			diepint.d32 = dwc_otg_read_dev_in_ep_intr( core_if, dwc_ep );
										   
			DWC_DEBUGPL(DBG_PCDV, "EP %d Interrupt Register - 0x%x\n", epnum, diepint.d32);
			/* Transfer complete */
			if ( diepint.b.xfercompl ) 
			{

				DWC_DEBUGPL(DBG_PCD,"EP%d IN Xfer Complete\n", epnum);
				
				/* Disable the NP Tx FIFO Empty
				 * Interrrupt */
				if(core_if->en_multiple_tx_fifo == 0)
				{
					intr_mask.b.nptxfempty = 1;
					dwc_modify_reg32( &core_if->core_global_regs->gintmsk, intr_mask.d32, 0);
				}
				else
				{						
					/* Disable the Tx FIFO Empty Interrupt for this EP */
					uint32_t fifoemptymsk = 0x1 << dwc_ep->num;
					dwc_modify_reg32(&core_if->dev_if->dev_global_regs->dtknqr4_fifoemptymsk,
					fifoemptymsk, 0);
				}
				/* Clear the bit in DIEPINTn for this interrupt */
				CLEAR_IN_EP_INTR(core_if,epnum,xfercompl);

				/* Complete the transfer */
				if (epnum == 0) 
				{
					handle_ep0( _pcd );
				}
				else 
				{
					complete_ep( ep );
				}
			}
			/* Endpoint disable	 */
			if ( diepint.b.epdisabled ) 
			{
				DWC_DEBUGPL(DBG_ANY,"EP%d IN disabled\n", epnum);
				handle_in_ep_disable_intr( _pcd, epnum );

				/* Clear the bit in DIEPINTn for this interrupt */
				CLEAR_IN_EP_INTR(core_if,epnum,epdisabled);
			}
			/* AHB Error */
			if ( diepint.b.ahberr ) 
			{
				DWC_DEBUGPL(DBG_ANY,"EP%d IN AHB Error\n", epnum);
				/* Clear the bit in DIEPINTn for this interrupt */
				CLEAR_IN_EP_INTR(core_if,epnum,ahberr);
			}
			/* TimeOUT Handshake (non-ISOC IN EPs) */
			if ( diepint.b.timeout ) 
			{ 
				DWC_DEBUGPL(DBG_ANY,"EP%d IN Time-out\n", epnum);
				handle_in_ep_timeout_intr( _pcd, epnum );

				CLEAR_IN_EP_INTR(core_if,epnum,timeout);
			}
			/** IN Token received with TxF Empty */
			if (diepint.b.intktxfemp)
			{
				DWC_DEBUGPL(DBG_ANY,"EP%d IN TKN TxFifo Empty\n",
								epnum);
				if (!ep->stopped && epnum != 0) 
				{
					diepmsk_data_t diepmsk = { .d32 = 0};
					diepmsk.b.intktxfemp = 1;
					dwc_modify_reg32( &dev_if->dev_global_regs->diepmsk, diepmsk.d32, 0 );		 
					start_next_request(ep);
				}
				CLEAR_IN_EP_INTR(core_if,epnum,intktxfemp);
			}					
			/** IN Token Received with EP mismatch */
			if (diepint.b.intknepmis)
			{
				DWC_DEBUGPL(DBG_ANY,"EP%d IN TKN EP Mismatch\n", epnum);
				CLEAR_IN_EP_INTR(core_if,epnum,intknepmis);
			}
			/** IN Endpoint NAK Effective */
			if (diepint.b.inepnakeff)
			{
				DWC_DEBUGPL(DBG_ANY,"EP%d IN EP NAK Effective\n", epnum);
				/* Periodic EP */
				if (ep->disabling) 
				{
					diepctl.d32 = 0;
					diepctl.b.snak = 1;
					diepctl.b.epdis = 1;
					dwc_modify_reg32(&dev_if->in_ep_regs[epnum]->diepctl, diepctl.d32, diepctl.d32);
				}
				CLEAR_IN_EP_INTR(core_if,epnum,inepnakeff);

			}
					
			/** IN EP Tx FIFO Empty Intr */
			if (diepint.b.emptyintr)
			{
				DWC_DEBUGPL(DBG_ANY,"EP%d Tx FIFO Empty Intr \n", epnum);
				write_empty_tx_fifo(_pcd, epnum);										

				CLEAR_IN_EP_INTR(core_if,epnum,emptyintr);
						
			}
		}
		epnum++;
		ep_intr >>=1;
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
static int32_t dwc_otg_pcd_handle_out_ep_intr(dwc_otg_pcd_t *_pcd)
{
#define CLEAR_OUT_EP_INTR(__core_if,__epnum,__intr) \
do { \
		doepint_data_t doepint = {.d32=0}; \
		doepint.b.__intr = 1; \
		dwc_write_reg32(&__core_if->dev_if->out_ep_regs[__epnum]->doepint, \
		doepint.d32); \
} while (0)

	dwc_otg_core_if_t *core_if = GET_CORE_IF(_pcd);
	uint32_t ep_intr;
	doepint_data_t doepint = {.d32=0};
	uint32_t epnum = 0;
	dwc_ep_t *dwc_ep;
	
	DWC_DEBUGPL(DBG_PCDV, "%s()\n", __func__);

	/* Read in the device interrupt bits */
	ep_intr = dwc_otg_read_dev_all_out_ep_intr( core_if );
//	dwc_otg_dump_dev_registers(core_if);

	while( ep_intr ) 
	{
		if (ep_intr&0x1) 
		{
			/* Get EP pointer */	   
			dwc_ep = &((get_out_ep(_pcd, epnum))->dwc_ep);
//			  dwc_ep = &_pcd->out_ep[ epnum - 1].dwc_ep;
//#ifdef VERBOSE
#if 1
			DWC_DEBUGPL(DBG_PCDV, 
							"EP%d-%s: type=%d, mps=%d\n", 
							dwc_ep->num, (dwc_ep->is_in ?"IN":"OUT"),
							dwc_ep->type, dwc_ep->maxpacket );
#endif
			doepint.d32 = dwc_otg_read_dev_out_ep_intr(core_if, dwc_ep);
				
			/* Transfer complete */
			if ( doepint.b.xfercompl ) 
			{
				DWC_DEBUGPL(DBG_PCD,"EP%d OUT Xfer Complete\n",
								epnum);

				/* Clear the bit in DOEPINTn for this interrupt */
				CLEAR_OUT_EP_INTR(core_if,epnum,xfercompl);

				if (epnum == 0) 
				{
					handle_ep0( _pcd );
				} 
				else 
				{
					complete_ep( get_out_ep(_pcd, epnum) );
//						complete_ep( &_pcd->out_ep[ epnum - 1] );
				}
			}
			/* Endpoint disable	 */
			if ( doepint.b.epdisabled ) 
			{
				DWC_DEBUGPL(DBG_PCD,"EP%d OUT disabled\n", epnum);
				/* Clear the bit in DOEPINTn for this interrupt */
				CLEAR_OUT_EP_INTR(core_if,epnum,epdisabled);
			}
			/* AHB Error */
			if ( doepint.b.ahberr ) 
			{
				DWC_DEBUGPL(DBG_PCD,"EP%d OUT AHB Error\n", epnum);
				DWC_DEBUGPL(DBG_PCD,"EP DMA REG	 %d \n", core_if->dev_if->out_ep_regs[epnum]->doepdma);													
				CLEAR_OUT_EP_INTR(core_if,epnum,ahberr);
			}
			/* Setup Phase Done (contorl EPs) */
			if ( doepint.b.setup ) 
			{
#ifdef DEBUG_EP0
				DWC_DEBUGPL(DBG_PCD,"EP%d SETUP Done\n", 
							epnum);
#endif
				handle_ep0( _pcd );
				CLEAR_OUT_EP_INTR(core_if,epnum,setup);
			}
		}
		epnum++;
		ep_intr >>=1;
	}

	return 1;

#undef CLEAR_OUT_EP_INTR
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
int32_t dwc_otg_pcd_handle_incomplete_isoc_in_intr(dwc_otg_pcd_t *_pcd)
{
	gintmsk_data_t intr_mask = { .d32 = 0};
	gintsts_data_t gintsts;
	DWC_PRINT("INTERRUPT Handler not implemented for %s\n",
			  "IN ISOC Incomplete");

	intr_mask.b.incomplisoin = 1;
	dwc_modify_reg32( &GET_CORE_IF(_pcd)->core_global_regs->gintmsk, 
					  intr_mask.d32, 0 );

	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.incomplisoin = 1;
	dwc_write_reg32 (&GET_CORE_IF(_pcd)->core_global_regs->gintsts, 
						 gintsts.d32);

	return 1;
}

/**
 * Incomplete ISO OUT Transfer Interrupt.  
 *
 * This interrupt indicates that the core has dropped an ISO OUT
 * packet.	The following conditions can be the cause:
 * - FIFO Full, the entire packet would not fit in the FIFO.
 * - CRC Error
 * - Corrupted Token
 * The follow actions will be taken:
 *	-#	Determine the EP
 *	-#	Set incomplete flag in dwc_ep structure
 *	-#	Read any data from the FIFO
 *	-#	Disable EP.	 when "Endpoint Disabled" interrupt is received
 *		re-enable EP.
 */
int32_t dwc_otg_pcd_handle_incomplete_isoc_out_intr(dwc_otg_pcd_t *_pcd)
{
	/** @todo implement ISR */
	gintmsk_data_t intr_mask = { .d32 = 0};
	gintsts_data_t gintsts;
	DWC_PRINT("INTERRUPT Handler not implemented for %s\n",
			  "OUT ISOC Incomplete");

	intr_mask.b.incomplisoout = 1;
	dwc_modify_reg32( &GET_CORE_IF(_pcd)->core_global_regs->gintmsk, 
					  intr_mask.d32, 0 );

	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.incomplisoout = 1;
	dwc_write_reg32 (&GET_CORE_IF(_pcd)->core_global_regs->gintsts, 
						 gintsts.d32);

	return 1;
}

/**
 * This function handles the Global IN NAK Effective interrupt.
 *	
 */
int32_t dwc_otg_pcd_handle_in_nak_effective( dwc_otg_pcd_t *_pcd )
{
	dwc_otg_dev_if_t *dev_if = GET_CORE_IF(_pcd)->dev_if;
	depctl_data_t diepctl = { .d32 = 0};
	depctl_data_t diepctl_rd = { .d32 = 0};
	gintmsk_data_t intr_mask = { .d32 = 0};
	gintsts_data_t gintsts;
	int i;

	DWC_DEBUGPL(DBG_PCD, "Global IN NAK Effective\n");
	
	/* Disable all active IN EPs */
	diepctl.b.epdis = 1;
	diepctl.b.snak = 1;
	
	for (i=0; i <= dev_if->num_in_eps; i++) 
	{
		diepctl_rd.d32 = dwc_read_reg32(&dev_if->in_ep_regs[i]->diepctl);
		if (diepctl_rd.b.epena) 
		{
			dwc_write_reg32( &dev_if->in_ep_regs[i]->diepctl, 
								diepctl.d32 );	
		}
	}
	/* Disable the Global IN NAK Effective Interrupt */
	intr_mask.b.ginnakeff = 1;
	dwc_modify_reg32( &GET_CORE_IF(_pcd)->core_global_regs->gintmsk, 
					  intr_mask.d32, 0);

	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.ginnakeff = 1;
	dwc_write_reg32( &GET_CORE_IF(_pcd)->core_global_regs->gintsts, 
						 gintsts.d32);

	return 1;
}

/**
 * OUT NAK Effective.
 *
 */
int32_t dwc_otg_pcd_handle_out_nak_effective( dwc_otg_pcd_t *_pcd )
{
	gintmsk_data_t intr_mask = { .d32 = 0};
	gintsts_data_t gintsts;

	DWC_PRINT("INTERRUPT Handler not implemented for %s\n", 
			  "Global IN NAK Effective\n");
	/* Disable the Global IN NAK Effective Interrupt */
	intr_mask.b.goutnakeff = 1;
	dwc_modify_reg32( &GET_CORE_IF(_pcd)->core_global_regs->gintmsk, 
					  intr_mask.d32, 0);

	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.goutnakeff = 1;
	dwc_write_reg32 (&GET_CORE_IF(_pcd)->core_global_regs->gintsts, 
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
int32_t dwc_otg_pcd_handle_intr( dwc_otg_pcd_t *_pcd )
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(_pcd);
#ifdef VERBOSE
	dwc_otg_core_global_regs_t *global_regs = 
			core_if->core_global_regs;
#endif
	gintsts_data_t gintr_status;
	int32_t retval = 0;

#ifdef VERBOSE
	DWC_DEBUGPL(DBG_ANY, "%s() gintsts=%08x	 gintmsk=%08x\n", 
				__func__,
				dwc_read_reg32( &global_regs->gintsts),
				dwc_read_reg32( &global_regs->gintmsk));
#endif

	if (dwc_otg_is_device_mode(core_if)) 
	{
		SPIN_LOCK(&_pcd->lock);
#ifdef VERBOSE
		DWC_DEBUGPL(DBG_PCDV, "%s() gintsts=%08x  gintmsk=%08x\n", 
						__func__,
						dwc_read_reg32( &global_regs->gintsts),
						dwc_read_reg32( &global_regs->gintmsk));				
#endif
				
		gintr_status.d32 = dwc_otg_read_core_intr(core_if);
		if (!gintr_status.d32) 
		{
			SPIN_UNLOCK(&_pcd->lock);
			return 0;
		}
		DWC_DEBUGPL(DBG_PCDV, "%s: gintsts&gintmsk=%08x\n",
					__func__, gintr_status.d32 );
	
		if (gintr_status.b.sofintr) 
		{
			retval |= dwc_otg_pcd_handle_sof_intr( _pcd );
		}
		if (gintr_status.b.rxstsqlvl) 
		{
			retval |= dwc_otg_pcd_handle_rx_status_q_level_intr( _pcd );
		}
		if (gintr_status.b.nptxfempty) 
		{
			retval |= dwc_otg_pcd_handle_np_tx_fifo_empty_intr( _pcd );
		}
		if (gintr_status.b.ginnakeff) 
		{
			retval |= dwc_otg_pcd_handle_in_nak_effective( _pcd );
		}
		if (gintr_status.b.goutnakeff) 
		{
			retval |= dwc_otg_pcd_handle_out_nak_effective( _pcd );
		}
		if (gintr_status.b.i2cintr) 
		{
			retval |= dwc_otg_pcd_handle_i2c_intr( _pcd );
		}
		if (gintr_status.b.erlysuspend) 
		{
			retval |= dwc_otg_pcd_handle_early_suspend_intr( _pcd );
		}
		if (gintr_status.b.usbreset) 
		{
			retval |= dwc_otg_pcd_handle_usb_reset_intr( _pcd );
			_pcd->conn_status = -1;
		}
		if (gintr_status.b.enumdone) 
		{
			retval |= dwc_otg_pcd_handle_enum_done_intr( _pcd );
		}
		if (gintr_status.b.isooutdrop) 
		{
			retval |= dwc_otg_pcd_handle_isoc_out_packet_dropped_intr( _pcd );
		}
		if (gintr_status.b.eopframe) 
		{
			retval |= dwc_otg_pcd_handle_end_periodic_frame_intr( _pcd );
		}
		if (gintr_status.b.epmismatch) 
		{
			retval |= dwc_otg_pcd_handle_ep_mismatch_intr( core_if );
		}
		if (gintr_status.b.inepint) 
		{
			retval |= dwc_otg_pcd_handle_in_ep_intr( _pcd );
		}
		if (gintr_status.b.outepintr) 
		{
			retval |= dwc_otg_pcd_handle_out_ep_intr( _pcd );
		}
		if (gintr_status.b.incomplisoin) 
		{
			retval |= dwc_otg_pcd_handle_incomplete_isoc_in_intr( _pcd );
		}
		if (gintr_status.b.incomplisoout) 
		{
			retval |= dwc_otg_pcd_handle_incomplete_isoc_out_intr( _pcd );
		}
#ifdef VERBOSE
		DWC_DEBUGPL(DBG_PCDV, "%s() gintsts=%0x\n", __func__,
						dwc_read_reg32( &global_regs->gintsts));
#endif
		SPIN_UNLOCK(&_pcd->lock);
	}
	return retval;
}

#endif /* DWC_HOST_ONLY */
