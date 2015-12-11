/* ==========================================================================
 * $File: //dwh/usb_iip/dev/software/otg/linux/drivers/dwc_otg_pcd.c $
 * $Revision: #104 $
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

/** @file
 * This file implements PCD Core. All code in this file is portable and doesn't
 * use any OS specific functions.
 * PCD Core provides Interface, defined in <code><dwc_otg_pcd_if.h></code>
 * header file, which can be used to implement OS specific PCD interface.
 *
 * An important function of the PCD is managing interrupts generated
 * by the DWC_otg controller. The implementation of the DWC_otg device
 * mode interrupt service routines is in dwc_otg_pcd_intr.c.
 *
 * @todo Add Device Mode test modes (Test J mode, Test K mode, etc).
 * @todo Does it work when the request size is greater than DEPTSIZ
 * transfer size
 *
 */

#include "dwc_otg_pcd.h"

#ifdef DWC_UTE_CFI
#include "dwc_otg_cfi.h"

extern int init_cfi(cfiobject_t *cfiobj);
#endif

/**
 * Choose endpoint from ep arrays using usb_ep structure.
 */
static dwc_otg_pcd_ep_t *get_ep_from_handle(dwc_otg_pcd_t *pcd, void *handle)
{
	int i;
	if (pcd->ep0.priv == handle) {
		return &pcd->ep0;
	}
	for (i = 0; i < MAX_EPS_CHANNELS - 1; i++) {
		if (pcd->in_ep[i].priv == handle)
			return &pcd->in_ep[i];
		if (pcd->out_ep[i].priv == handle)
			return &pcd->out_ep[i];
	}

	return NULL;
}

/**
 * This function completes a request.  It call's the request call back.
 */
void dwc_otg_request_done(dwc_otg_pcd_ep_t *ep, dwc_otg_pcd_request_t *req,
			  int32_t status)
{
	unsigned stopped = ep->stopped;

	DWC_DEBUGPL(DBG_PCDV, "%s(ep %p req %p)\n", __func__, ep, req);
	DWC_CIRCLEQ_REMOVE_INIT(&ep->queue, req, queue_entry);

	/* don't modify queue heads during completion callback */
	ep->stopped = 1;
	/* spin_unlock/spin_lock now done in fops->complete() */
	ep->pcd->fops->complete(ep->pcd, ep->priv, req->priv, status,
				req->actual);

	if (ep->pcd->request_pending > 0) {
		--ep->pcd->request_pending;
	}

	ep->stopped = stopped;
	DWC_FREE(req);
}

/**
 * This function terminates all the requsts in the EP request queue.
 */
void dwc_otg_request_nuke(dwc_otg_pcd_ep_t *ep)
{
	dwc_otg_pcd_request_t *req;

	ep->stopped = 1;

	/* called with irqs blocked?? */
	while (!DWC_CIRCLEQ_EMPTY(&ep->queue)) {
		req = DWC_CIRCLEQ_FIRST(&ep->queue);
		dwc_otg_request_done(ep, req, -DWC_E_SHUTDOWN);
	}
}

void dwc_otg_pcd_start(dwc_otg_pcd_t *pcd,
		       const struct dwc_otg_pcd_function_ops *fops)
{
	pcd->fops = fops;
}

/**
 * PCD Callback function for initializing the PCD when switching to
 * device mode.
 *
 * @param p void pointer to the <code>dwc_otg_pcd_t</code>
 */
static int32_t dwc_otg_pcd_start_cb(void *p)
{
	dwc_otg_pcd_t *pcd = (dwc_otg_pcd_t *) p;
	dwc_otg_core_if_t *core_if = GET_CORE_IF(pcd);

	/*
	 * Initialized the Core for Device mode.
	 */
	if (dwc_otg_is_device_mode(core_if)) {
		/* dwc_otg_core_dev_init(core_if); */
		/* Set core_if's lock pointer to the pcd->lock */
		core_if->lock = pcd->lock;
	}
	return 1;
}

/** CFI-specific buffer allocation function for EP */
#ifdef DWC_UTE_CFI
uint8_t *cfiw_ep_alloc_buffer(dwc_otg_pcd_t *pcd, void *pep, dwc_dma_t *addr,
			      size_t buflen, int flags)
{
	dwc_otg_pcd_ep_t *ep;
	ep = get_ep_from_handle(pcd, pep);
	if (!ep) {
		DWC_WARN("bad ep\n");
		return -DWC_E_INVALID;
	}

	return pcd->cfi->ops.ep_alloc_buf(pcd->cfi, pcd, ep, addr, buflen,
					  flags);
}
#else
uint8_t *cfiw_ep_alloc_buffer(dwc_otg_pcd_t *pcd, void *pep, dwc_dma_t *addr,
			      size_t buflen, int flags);
#endif

/**
 * PCD Callback function for notifying the PCD when resuming from
 * suspend.
 *
 * @param p void pointer to the <code>dwc_otg_pcd_t</code>
 */
static int32_t dwc_otg_pcd_resume_cb(void *p)
{
	dwc_otg_pcd_t *pcd = (dwc_otg_pcd_t *) p;

	if (pcd->fops->resume) {
		pcd->fops->resume(pcd);
	}

	/* Stop the SRP timeout timer. */
	if ((GET_CORE_IF(pcd)->core_params->phy_type != DWC_PHY_TYPE_PARAM_FS)
	    || (!GET_CORE_IF(pcd)->core_params->i2c_enable)) {
		if (GET_CORE_IF(pcd)->srp_timer_started) {
			GET_CORE_IF(pcd)->srp_timer_started = 0;
			DWC_TIMER_CANCEL(GET_CORE_IF(pcd)->srp_timer);
		}
	}
	return 1;
}

/**
 * PCD Callback function for notifying the PCD device is suspended.
 *
 * @param p void pointer to the <code>dwc_otg_pcd_t</code>
 */
static int32_t dwc_otg_pcd_suspend_cb(void *p)
{
	dwc_otg_pcd_t *pcd = (dwc_otg_pcd_t *) p;

	if (pcd->fops->suspend)
		pcd->fops->suspend(pcd);

	return 1;
}

/**
 * PCD Callback function for stopping the PCD when switching to Host
 * mode.
 *
 * @param p void pointer to the <code>dwc_otg_pcd_t</code>
 */
static int32_t dwc_otg_pcd_stop_cb(void *p)
{
	dwc_otg_pcd_t *pcd = (dwc_otg_pcd_t *) p;
	extern void dwc_otg_pcd_stop(dwc_otg_pcd_t *_pcd);

	dwc_otg_pcd_stop(pcd);
	return 1;
}

/**
 * PCD Callback structure for handling mode switching.
 */
static dwc_otg_cil_callbacks_t pcd_callbacks = {
	.start = dwc_otg_pcd_start_cb,
	.stop = dwc_otg_pcd_stop_cb,
	.suspend = dwc_otg_pcd_suspend_cb,
	.resume_wakeup = dwc_otg_pcd_resume_cb,
	.p = 0,			/* Set at registration */
};

/**
 * This function allocates a DMA Descriptor chain for the Endpoint
 * buffer to be used for a transfer to/from the specified endpoint.
 */
dwc_otg_dev_dma_desc_t *dwc_otg_ep_alloc_desc_chain(dwc_dma_t *dma_desc_addr,
						    uint32_t count)
{
	return DWC_DEV_DMA_ALLOC_ATOMIC(count * sizeof(dwc_otg_dev_dma_desc_t),
					dma_desc_addr);
}

/**
 * This function frees a DMA Descriptor chain that was allocated by ep_alloc_desc.
 */
void dwc_otg_ep_free_desc_chain(dwc_otg_dev_dma_desc_t *desc_addr,
				uint32_t dma_desc_addr, uint32_t count)
{
	DWC_DEV_DMA_FREE(count * sizeof(dwc_otg_dev_dma_desc_t), desc_addr,
			 dma_desc_addr);
}

#ifdef DWC_EN_ISOC

/**
 * This function initializes a descriptor chain for Isochronous transfer
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param dwc_ep The EP to start the transfer on.
 *
 */
void dwc_otg_iso_ep_start_ddma_transfer(dwc_otg_core_if_t *core_if,
					dwc_ep_t *dwc_ep)
{

	dsts_data_t dsts = {.d32 = 0 };
	depctl_data_t depctl = {.d32 = 0 };
	volatile uint32_t *addr;
	int i, j;
	uint32_t len;

	if (dwc_ep->is_in)
		dwc_ep->desc_cnt = dwc_ep->buf_proc_intrvl / dwc_ep->bInterval;
	else
		dwc_ep->desc_cnt =
		    dwc_ep->buf_proc_intrvl*dwc_ep->pkt_per_frm /
		    dwc_ep->bInterval;

	/** Allocate descriptors for double buffering */
	dwc_ep->iso_desc_addr =
	    dwc_otg_ep_alloc_desc_chain(&dwc_ep->iso_dma_desc_addr,
					dwc_ep->desc_cnt * 2);
	if (dwc_ep->desc_addr) {
		DWC_WARN("%s, can't allocate DMA descriptor chain\n", __func__);
		return;
	}

	dsts.d32 = DWC_READ_REG32(&core_if->dev_if->dev_global_regs->dsts);

	/** ISO OUT EP */
	if (dwc_ep->is_in == 0) {
		dev_dma_desc_sts_t sts = {.d32 = 0 };
		dwc_otg_dev_dma_desc_t *dma_desc = dwc_ep->iso_desc_addr;
		dma_addr_t dma_ad;
		uint32_t data_per_desc;
		dwc_otg_dev_out_ep_regs_t *out_regs =
		    core_if->dev_if->out_ep_regs[dwc_ep->num];
		int offset;

		addr = &core_if->dev_if->out_ep_regs[dwc_ep->num]->doepctl;
		dma_ad = (dma_addr_t) DWC_READ_REG32(&(out_regs->doepdma));

		/** Buffer 0 descriptors setup */
		dma_ad = dwc_ep->dma_addr0;

		sts.b_iso_out.bs = BS_HOST_READY;
		sts.b_iso_out.rxsts = 0;
		sts.b_iso_out.l = 0;
		sts.b_iso_out.sp = 0;
		sts.b_iso_out.ioc = 0;
		sts.b_iso_out.pid = 0;
		sts.b_iso_out.framenum = 0;

		offset = 0;
		for (i = 0; i < dwc_ep->desc_cnt - dwc_ep->pkt_per_frm;
		     i += dwc_ep->pkt_per_frm) {

			for (j = 0; j < dwc_ep->pkt_per_frm; ++j) {
				uint32_t len = (j + 1)*dwc_ep->maxpacket;
				if (len > dwc_ep->data_per_frame)
					data_per_desc =
					    dwc_ep->data_per_frame -
					    j*dwc_ep->maxpacket;
				else
					data_per_desc = dwc_ep->maxpacket;
				len = data_per_desc % 4;
				if (len)
					data_per_desc += 4 - len;

				sts.b_iso_out.rxbytes = data_per_desc;
				dma_desc->buf = dma_ad;
				dma_desc->status.d32 = sts.d32;

				offset += data_per_desc;
				dma_desc++;
				dma_ad += data_per_desc;
			}
		}

		for (j = 0; j < dwc_ep->pkt_per_frm - 1; ++j) {
			uint32_t len = (j + 1)*dwc_ep->maxpacket;
			if (len > dwc_ep->data_per_frame)
				data_per_desc =
				    dwc_ep->data_per_frame -
				    j*dwc_ep->maxpacket;
			else
				data_per_desc = dwc_ep->maxpacket;
			len = data_per_desc % 4;
			if (len)
				data_per_desc += 4 - len;
			sts.b_iso_out.rxbytes = data_per_desc;
			dma_desc->buf = dma_ad;
			dma_desc->status.d32 = sts.d32;

			offset += data_per_desc;
			dma_desc++;
			dma_ad += data_per_desc;
		}

		sts.b_iso_out.ioc = 1;
		len = (j + 1)*dwc_ep->maxpacket;
		if (len > dwc_ep->data_per_frame)
			data_per_desc =
			    dwc_ep->data_per_frame - j*dwc_ep->maxpacket;
		else
			data_per_desc = dwc_ep->maxpacket;
		len = data_per_desc % 4;
		if (len)
			data_per_desc += 4 - len;
		sts.b_iso_out.rxbytes = data_per_desc;

		dma_desc->buf = dma_ad;
		dma_desc->status.d32 = sts.d32;
		dma_desc++;

		/** Buffer 1 descriptors setup */
		sts.b_iso_out.ioc = 0;
		dma_ad = dwc_ep->dma_addr1;

		offset = 0;
		for (i = 0; i < dwc_ep->desc_cnt - dwc_ep->pkt_per_frm;
		     i += dwc_ep->pkt_per_frm) {
			for (j = 0; j < dwc_ep->pkt_per_frm; ++j) {
				uint32_t len = (j + 1)*dwc_ep->maxpacket;
				if (len > dwc_ep->data_per_frame)
					data_per_desc =
					    dwc_ep->data_per_frame -
					    j*dwc_ep->maxpacket;
				else
					data_per_desc = dwc_ep->maxpacket;
				len = data_per_desc % 4;
				if (len)
					data_per_desc += 4 - len;

				data_per_desc =
				    sts.b_iso_out.rxbytes = data_per_desc;
				dma_desc->buf = dma_ad;
				dma_desc->status.d32 = sts.d32;

				offset += data_per_desc;
				dma_desc++;
				dma_ad += data_per_desc;
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

			offset += data_per_desc;
			dma_desc++;
			dma_ad += data_per_desc;
		}

		sts.b_iso_out.ioc = 1;
		sts.b_iso_out.l = 1;
		data_per_desc =
		    ((j + 1)*dwc_ep->maxpacket >
		     dwc_ep->data_per_frame) ? dwc_ep->data_per_frame -
		    j*dwc_ep->maxpacket : dwc_ep->maxpacket;
		data_per_desc +=
		    (data_per_desc % 4) ? (4 - data_per_desc % 4) : 0;
		sts.b_iso_out.rxbytes = data_per_desc;

		dma_desc->buf = dma_ad;
		dma_desc->status.d32 = sts.d32;

		dwc_ep->next_frame = 0;

		/** Write dma_ad into DOEPDMA register */
		DWC_WRITE_REG32(&(out_regs->doepdma),
				(uint32_t) dwc_ep->iso_dma_desc_addr);

	}
	/** ISO IN EP */
	else {
		dev_dma_desc_sts_t sts = {.d32 = 0 };
		dwc_otg_dev_dma_desc_t *dma_desc = dwc_ep->iso_desc_addr;
		dma_addr_t dma_ad;
		dwc_otg_dev_in_ep_regs_t *in_regs =
		    core_if->dev_if->in_ep_regs[dwc_ep->num];
		unsigned int frmnumber;
		fifosize_data_t txfifosize, rxfifosize;

		txfifosize.d32 =
		    DWC_READ_REG32(&core_if->dev_if->
				   in_ep_regs[dwc_ep->num]->dtxfsts);
		rxfifosize.d32 =
		    DWC_READ_REG32(&core_if->core_global_regs->grxfsiz);

		addr = &core_if->dev_if->in_ep_regs[dwc_ep->num]->diepctl;

		dma_ad = dwc_ep->dma_addr0;

		dsts.d32 =
		    DWC_READ_REG32(&core_if->dev_if->dev_global_regs->dsts);

		sts.b_iso_in.bs = BS_HOST_READY;
		sts.b_iso_in.txsts = 0;
		sts.b_iso_in.sp =
		    (dwc_ep->data_per_frame % dwc_ep->maxpacket) ? 1 : 0;
		sts.b_iso_in.ioc = 0;
		sts.b_iso_in.pid = dwc_ep->pkt_per_frm;

		frmnumber = dwc_ep->next_frame;

		sts.b_iso_in.framenum = frmnumber;
		sts.b_iso_in.txbytes = dwc_ep->data_per_frame;
		sts.b_iso_in.l = 0;

		/** Buffer 0 descriptors setup */
		for (i = 0; i < dwc_ep->desc_cnt - 1; i++) {
			dma_desc->buf = dma_ad;
			dma_desc->status.d32 = sts.d32;
			dma_desc++;

			dma_ad += dwc_ep->data_per_frame;
			sts.b_iso_in.framenum += dwc_ep->bInterval;
		}

		sts.b_iso_in.ioc = 1;
		dma_desc->buf = dma_ad;
		dma_desc->status.d32 = sts.d32;
		++dma_desc;

		/** Buffer 1 descriptors setup */
		sts.b_iso_in.ioc = 0;
		dma_ad = dwc_ep->dma_addr1;

		for (i = 0; i < dwc_ep->desc_cnt - dwc_ep->pkt_per_frm;
		     i += dwc_ep->pkt_per_frm) {
			dma_desc->buf = dma_ad;
			dma_desc->status.d32 = sts.d32;
			dma_desc++;

			dma_ad += dwc_ep->data_per_frame;
			sts.b_iso_in.framenum += dwc_ep->bInterval;

			sts.b_iso_in.ioc = 0;
		}
		sts.b_iso_in.ioc = 1;
		sts.b_iso_in.l = 1;

		dma_desc->buf = dma_ad;
		dma_desc->status.d32 = sts.d32;

		dwc_ep->next_frame = sts.b_iso_in.framenum + dwc_ep->bInterval;

		/** Write dma_ad into diepdma register */
		DWC_WRITE_REG32(&(in_regs->diepdma),
				(uint32_t) dwc_ep->iso_dma_desc_addr);
	}
	/** Enable endpoint, clear nak  */
	depctl.d32 = 0;
	depctl.b.epena = 1;
	depctl.b.usbactep = 1;
	depctl.b.cnak = 1;

	DWC_MODIFY_REG32(addr, depctl.d32, depctl.d32);
	depctl.d32 = DWC_READ_REG32(addr);
}

/**
 * This function initializes a descriptor chain for Isochronous transfer
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param ep The EP to start the transfer on.
 *
 */
void dwc_otg_iso_ep_start_buf_transfer(dwc_otg_core_if_t *core_if,
				       dwc_ep_t *ep)
{
	depctl_data_t depctl = {.d32 = 0 };
	volatile uint32_t *addr;

	if (ep->is_in) {
		addr = &core_if->dev_if->in_ep_regs[ep->num]->diepctl;
	} else {
		addr = &core_if->dev_if->out_ep_regs[ep->num]->doepctl;
	}

	if (core_if->dma_enable == 0 || core_if->dma_desc_enable != 0) {
		return;
	} else {
		deptsiz_data_t deptsiz = {.d32 = 0 };

		ep->xfer_len =
		    ep->data_per_frame*ep->buf_proc_intrvl / ep->bInterval;
		ep->pkt_cnt =
		    (ep->xfer_len - 1 + ep->maxpacket) / ep->maxpacket;
		ep->xfer_count = 0;
		ep->xfer_buff =
		    (ep->proc_buf_num) ? ep->xfer_buff1 : ep->xfer_buff0;
		ep->dma_addr =
		    (ep->proc_buf_num) ? ep->dma_addr1 : ep->dma_addr0;

		if (ep->is_in) {
			/* Program the transfer size and packet count
			 *      as follows: xfersize = N * maxpacket +
			 *      short_packet pktcnt = N + (short_packet
			 *      exist ? 1 : 0)
			 */
			deptsiz.b.mc = ep->pkt_per_frm;
			deptsiz.b.xfersize = ep->xfer_len;
			deptsiz.b.pktcnt =
			    (ep->xfer_len - 1 + ep->maxpacket) / ep->maxpacket;
			DWC_WRITE_REG32(&core_if->dev_if->
					in_ep_regs[ep->num]->dieptsiz,
					deptsiz.d32);

			/* Write the DMA register */
			DWC_WRITE_REG32(&
					(core_if->dev_if->
					 in_ep_regs[ep->num]->diepdma),
					(uint32_t) ep->dma_addr);

		} else {
			deptsiz.b.pktcnt =
			    (ep->xfer_len + (ep->maxpacket - 1)) /
			    ep->maxpacket;
			deptsiz.b.xfersize = deptsiz.b.pktcnt*ep->maxpacket;

			DWC_WRITE_REG32(&core_if->dev_if->
					out_ep_regs[ep->num]->doeptsiz,
					deptsiz.d32);

			/* Write the DMA register */
			DWC_WRITE_REG32(&
					(core_if->dev_if->
					 out_ep_regs[ep->num]->doepdma),
					(uint32_t) ep->dma_addr);

		}
		/** Enable endpoint, clear nak  */
		depctl.d32 = 0;
		depctl.b.epena = 1;
		depctl.b.cnak = 1;

		DWC_MODIFY_REG32(addr, depctl.d32, depctl.d32);
	}
}

/**
 * This function does the setup for a data transfer for an EP and
 * starts the transfer. For an IN transfer, the packets will be
 * loaded into the appropriate Tx FIFO in the ISR. For OUT transfers,
 * the packets are unloaded from the Rx FIFO in the ISR.
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param ep The EP to start the transfer on.
 */

static void dwc_otg_iso_ep_start_transfer(dwc_otg_core_if_t *core_if,
					  dwc_ep_t *ep)
{
	if (core_if->dma_enable) {
		if (core_if->dma_desc_enable) {
			if (ep->is_in) {
				ep->desc_cnt = ep->pkt_cnt / ep->pkt_per_frm;
			} else {
				ep->desc_cnt = ep->pkt_cnt;
			}
			dwc_otg_iso_ep_start_ddma_transfer(core_if, ep);
		} else {
			if (core_if->pti_enh_enable) {
				dwc_otg_iso_ep_start_buf_transfer(core_if, ep);
			} else {
				ep->cur_pkt_addr =
				    (ep->proc_buf_num) ? ep->
				    xfer_buff1 : ep->xfer_buff0;
				ep->cur_pkt_dma_addr =
				    (ep->proc_buf_num) ? ep->
				    dma_addr1 : ep->dma_addr0;
				dwc_otg_iso_ep_start_frm_transfer(core_if, ep);
			}
		}
	} else {
		ep->cur_pkt_addr =
		    (ep->proc_buf_num) ? ep->xfer_buff1 : ep->xfer_buff0;
		ep->cur_pkt_dma_addr =
		    (ep->proc_buf_num) ? ep->dma_addr1 : ep->dma_addr0;
		dwc_otg_iso_ep_start_frm_transfer(core_if, ep);
	}
}

/**
 * This function stops transfer for an EP and
 * resets the ep's variables.
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param ep The EP to start the transfer on.
 */

void dwc_otg_iso_ep_stop_transfer(dwc_otg_core_if_t *core_if, dwc_ep_t *ep)
{
	depctl_data_t depctl = {.d32 = 0 };
	volatile uint32_t *addr;

	if (ep->is_in == 1) {
		addr = &core_if->dev_if->in_ep_regs[ep->num]->diepctl;
	} else {
		addr = &core_if->dev_if->out_ep_regs[ep->num]->doepctl;
	}

	/* disable the ep */
	depctl.d32 = DWC_READ_REG32(addr);

	depctl.b.epdis = 1;
	depctl.b.snak = 1;

	DWC_WRITE_REG32(addr, depctl.d32);

	if (core_if->dma_desc_enable &&
	    ep->iso_desc_addr && ep->iso_dma_desc_addr) {
		dwc_otg_ep_free_desc_chain(ep->iso_desc_addr,
					   ep->iso_dma_desc_addr,
					   ep->desc_cnt * 2);
	}

	/* reset varibales */
	ep->dma_addr0 = 0;
	ep->dma_addr1 = 0;
	ep->xfer_buff0 = 0;
	ep->xfer_buff1 = 0;
	ep->data_per_frame = 0;
	ep->data_pattern_frame = 0;
	ep->sync_frame = 0;
	ep->buf_proc_intrvl = 0;
	ep->bInterval = 0;
	ep->proc_buf_num = 0;
	ep->pkt_per_frm = 0;
	ep->pkt_per_frm = 0;
	ep->desc_cnt = 0;
	ep->iso_desc_addr = 0;
	ep->iso_dma_desc_addr = 0;
}

int dwc_otg_pcd_iso_ep_start(dwc_otg_pcd_t *pcd, void *ep_handle,
			     uint8_t *buf0, uint8_t *buf1, dwc_dma_t dma0,
			     dwc_dma_t dma1, int sync_frame, int dp_frame,
			     int data_per_frame, int start_frame,
			     int buf_proc_intrvl, void *req_handle,
			     int atomic_alloc)
{
	dwc_otg_pcd_ep_t *ep;
	dwc_irqflags_t flags = 0;
	dwc_ep_t *dwc_ep;
	int32_t frm_data;
	dsts_data_t dsts;
	dwc_otg_core_if_t *core_if;

	ep = get_ep_from_handle(pcd, ep_handle);

	if (!ep || !ep->desc || ep->dwc_ep.num == 0) {
		DWC_WARN("bad ep\n");
		return -DWC_E_INVALID;
	}

	DWC_SPINLOCK_IRQSAVE(pcd->lock, &flags);
	core_if = GET_CORE_IF(pcd);
	dwc_ep = &ep->dwc_ep;

	if (ep->iso_req_handle) {
		DWC_WARN("ISO request in progress\n");
	}

	dwc_ep->dma_addr0 = dma0;
	dwc_ep->dma_addr1 = dma1;

	dwc_ep->xfer_buff0 = buf0;
	dwc_ep->xfer_buff1 = buf1;

	dwc_ep->data_per_frame = data_per_frame;

	/** @todo - pattern data support is to be implemented in the future */
	dwc_ep->data_pattern_frame = dp_frame;
	dwc_ep->sync_frame = sync_frame;

	dwc_ep->buf_proc_intrvl = buf_proc_intrvl;

	dwc_ep->bInterval = 1 << (ep->desc->bInterval - 1);

	dwc_ep->proc_buf_num = 0;

	dwc_ep->pkt_per_frm = 0;
	frm_data = ep->dwc_ep.data_per_frame;
	while (frm_data > 0) {
		dwc_ep->pkt_per_frm++;
		frm_data -= ep->dwc_ep.maxpacket;
	}

	dsts.d32 = DWC_READ_REG32(&core_if->dev_if->dev_global_regs->dsts);

	if (start_frame == -1) {
		dwc_ep->next_frame = dsts.b.soffn + 1;
		if (dwc_ep->bInterval != 1) {
			dwc_ep->next_frame =
			    dwc_ep->next_frame + (dwc_ep->bInterval - 1 -
						  dwc_ep->next_frame %
						  dwc_ep->bInterval);
		}
	} else {
		dwc_ep->next_frame = start_frame;
	}

	if (!core_if->pti_enh_enable) {
		dwc_ep->pkt_cnt =
		    dwc_ep->buf_proc_intrvl*dwc_ep->pkt_per_frm /
		    dwc_ep->bInterval;
	} else {
		dwc_ep->pkt_cnt =
		    (dwc_ep->data_per_frame *
		     (dwc_ep->buf_proc_intrvl / dwc_ep->bInterval)
		     - 1 + dwc_ep->maxpacket) / dwc_ep->maxpacket;
	}

	if (core_if->dma_desc_enable) {
		dwc_ep->desc_cnt =
		    dwc_ep->buf_proc_intrvl*dwc_ep->pkt_per_frm /
		    dwc_ep->bInterval;
	}

	if (atomic_alloc) {
		dwc_ep->pkt_info =
		    DWC_ALLOC_ATOMIC(sizeof(iso_pkt_info_t)*dwc_ep->pkt_cnt);
	} else {
		dwc_ep->pkt_info =
		    DWC_ALLOC(sizeof(iso_pkt_info_t)*dwc_ep->pkt_cnt);
	}
	if (!dwc_ep->pkt_info) {
		DWC_SPINUNLOCK_IRQRESTORE(pcd->lock, flags);
		return -DWC_E_NO_MEMORY;
	}
	if (core_if->pti_enh_enable) {
		dwc_memset(dwc_ep->pkt_info, 0,
			   sizeof(iso_pkt_info_t)*dwc_ep->pkt_cnt);
	}

	dwc_ep->cur_pkt = 0;
	ep->iso_req_handle = req_handle;

	DWC_SPINUNLOCK_IRQRESTORE(pcd->lock, flags);
	dwc_otg_iso_ep_start_transfer(core_if, dwc_ep);
	return 0;
}

int dwc_otg_pcd_iso_ep_stop(dwc_otg_pcd_t *pcd, void *ep_handle,
			    void *req_handle)
{
	dwc_irqflags_t flags = 0;
	dwc_otg_pcd_ep_t *ep;
	dwc_ep_t *dwc_ep;

	ep = get_ep_from_handle(pcd, ep_handle);
	if (!ep || !ep->desc || ep->dwc_ep.num == 0) {
		DWC_WARN("bad ep\n");
		return -DWC_E_INVALID;
	}
	dwc_ep = &ep->dwc_ep;

	dwc_otg_iso_ep_stop_transfer(GET_CORE_IF(pcd), dwc_ep);

	DWC_FREE(dwc_ep->pkt_info);
	DWC_SPINLOCK_IRQSAVE(pcd->lock, &flags);
	if (ep->iso_req_handle != req_handle) {
		DWC_SPINUNLOCK_IRQRESTORE(pcd->lock, flags);
		return -DWC_E_INVALID;
	}

	DWC_SPINUNLOCK_IRQRESTORE(pcd->lock, flags);

	ep->iso_req_handle = 0;
	return 0;
}

/**
 * This function is used for perodical data exchnage between PCD and gadget drivers.
 * for Isochronous EPs
 *
 *	- Every time a sync period completes this function is called to
 *	  perform data exchange between PCD and gadget
 */
void dwc_otg_iso_buffer_done(dwc_otg_pcd_t *pcd, dwc_otg_pcd_ep_t *ep,
			     void *req_handle)
{
	int i;
	dwc_ep_t *dwc_ep;

	dwc_ep = &ep->dwc_ep;

	DWC_SPINUNLOCK(ep->pcd->lock);
	pcd->fops->isoc_complete(pcd, ep->priv, ep->iso_req_handle,
				 dwc_ep->proc_buf_num ^ 0x1);
	DWC_SPINLOCK(ep->pcd->lock);

	for (i = 0; i < dwc_ep->pkt_cnt; ++i) {
		dwc_ep->pkt_info[i].status = 0;
		dwc_ep->pkt_info[i].offset = 0;
		dwc_ep->pkt_info[i].length = 0;
	}
}

int dwc_otg_pcd_get_iso_packet_count(dwc_otg_pcd_t *pcd, void *ep_handle,
				     void *iso_req_handle)
{
	dwc_otg_pcd_ep_t *ep;
	dwc_ep_t *dwc_ep;

	ep = get_ep_from_handle(pcd, ep_handle);
	if (!ep->desc || ep->dwc_ep.num == 0) {
		DWC_WARN("bad ep\n");
		return -DWC_E_INVALID;
	}
	dwc_ep = &ep->dwc_ep;

	return dwc_ep->pkt_cnt;
}

void dwc_otg_pcd_get_iso_packet_params(dwc_otg_pcd_t *pcd, void *ep_handle,
				       void *iso_req_handle, int packet,
				       int *status, int *actual, int *offset)
{
	dwc_otg_pcd_ep_t *ep;
	dwc_ep_t *dwc_ep;

	ep = get_ep_from_handle(pcd, ep_handle);
	if (!ep)
		DWC_WARN("bad ep\n");

	dwc_ep = &ep->dwc_ep;

	*status = dwc_ep->pkt_info[packet].status;
	*actual = dwc_ep->pkt_info[packet].length;
	*offset = dwc_ep->pkt_info[packet].offset;
}

#endif /* DWC_EN_ISOC */

static void dwc_otg_pcd_init_ep(dwc_otg_pcd_t *pcd, dwc_otg_pcd_ep_t *pcd_ep,
				uint32_t is_in, uint32_t ep_num)
{
	/* Init EP structure */
	pcd_ep->desc = 0;
	pcd_ep->pcd = pcd;
	pcd_ep->stopped = 1;
	pcd_ep->queue_sof = 0;

	/* Init DWC ep structure */
	pcd_ep->dwc_ep.is_in = is_in;
	pcd_ep->dwc_ep.num = ep_num;
	pcd_ep->dwc_ep.active = 0;
	pcd_ep->dwc_ep.tx_fifo_num = 0;
	/* Control until ep is actvated */
	pcd_ep->dwc_ep.type = DWC_OTG_EP_TYPE_CONTROL;
	pcd_ep->dwc_ep.maxpacket = MAX_PACKET_SIZE;
	pcd_ep->dwc_ep.dma_addr = 0;
	pcd_ep->dwc_ep.start_xfer_buff = 0;
	pcd_ep->dwc_ep.xfer_buff = 0;
	pcd_ep->dwc_ep.xfer_len = 0;
	pcd_ep->dwc_ep.xfer_count = 0;
	pcd_ep->dwc_ep.sent_zlp = 0;
	pcd_ep->dwc_ep.total_len = 0;
	pcd_ep->dwc_ep.desc_addr = 0;
	pcd_ep->dwc_ep.dma_desc_addr = 0;
	DWC_CIRCLEQ_INIT(&pcd_ep->queue);
}

/**
 * Initialize ep's
 */
static void dwc_otg_pcd_reinit(dwc_otg_pcd_t *pcd)
{
	int i;
	uint32_t hwcfg1;
	dwc_otg_pcd_ep_t *ep;
	int in_ep_cntr, out_ep_cntr;
	uint32_t num_in_eps = (GET_CORE_IF(pcd))->dev_if->num_in_eps;
	uint32_t num_out_eps = (GET_CORE_IF(pcd))->dev_if->num_out_eps;
	/**
	 * Initialize the EP0 structure.
	 */
	ep = &pcd->ep0;
	dwc_otg_pcd_init_ep(pcd, ep, 0, 0);

	in_ep_cntr = 0;
	hwcfg1 = (GET_CORE_IF(pcd))->hwcfg1.d32 >> 3;
	for (i = 1; in_ep_cntr < num_in_eps; i++) {
		if ((hwcfg1 & 0x1) == 0) {
			dwc_otg_pcd_ep_t *ep = &pcd->in_ep[in_ep_cntr];
			in_ep_cntr++;
			/**
			 * @todo NGS: Add direction to EP, based on contents
			 * of HWCFG1.  Need a copy of HWCFG1 in pcd structure?
			 * sprintf(";r
			 */
			dwc_otg_pcd_init_ep(pcd, ep, 1 /* IN */ , i);

			DWC_CIRCLEQ_INIT(&ep->queue);
		}
		hwcfg1 >>= 2;
	}

	out_ep_cntr = 0;
	hwcfg1 = (GET_CORE_IF(pcd))->hwcfg1.d32 >> 2;
	for (i = 1; out_ep_cntr < num_out_eps; i++) {
		if ((hwcfg1 & 0x1) == 0) {
			dwc_otg_pcd_ep_t *ep = &pcd->out_ep[out_ep_cntr];
			out_ep_cntr++;
			/**
			 * @todo NGS: Add direction to EP, based on contents
			 * of HWCFG1.  Need a copy of HWCFG1 in pcd structure?
			 * sprintf(";r
			 */
			dwc_otg_pcd_init_ep(pcd, ep, 0 /* OUT */ , i);
			DWC_CIRCLEQ_INIT(&ep->queue);
		}
		hwcfg1 >>= 2;
	}

	pcd->ep0state = EP0_DISCONNECT;
	pcd->ep0.dwc_ep.maxpacket = MAX_EP0_SIZE;
	pcd->ep0.dwc_ep.type = DWC_OTG_EP_TYPE_CONTROL;
}

/**
 * This function is called when the SRP timer expires. The SRP should
 * complete within 6 seconds.
 */
static void srp_timeout(void *ptr)
{
	gotgctl_data_t gotgctl;
	dwc_otg_core_if_t *core_if = (dwc_otg_core_if_t *) ptr;
	volatile uint32_t *addr = &core_if->core_global_regs->gotgctl;

	gotgctl.d32 = DWC_READ_REG32(addr);

	core_if->srp_timer_started = 0;

	if (core_if->adp_enable) {
		if (gotgctl.b.bsesvld == 0) {
			gpwrdn_data_t gpwrdn = {.d32 = 0 };
			DWC_PRINTF("SRP Timeout BSESSVLD = 0\n");
			/* Power off the core */
			if (core_if->power_down == 2) {
				gpwrdn.b.pwrdnswtch = 1;
				DWC_MODIFY_REG32(&core_if->core_global_regs->
						 gpwrdn, gpwrdn.d32, 0);
			}

			gpwrdn.d32 = 0;
			gpwrdn.b.pmuintsel = 1;
			gpwrdn.b.pmuactv = 1;
			DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, 0,
					 gpwrdn.d32);
			dwc_otg_adp_probe_start(core_if);
		} else {
			DWC_PRINTF("SRP Timeout BSESSVLD = 1\n");
			core_if->op_state = B_PERIPHERAL;
			dwc_otg_core_init(core_if);
			dwc_otg_enable_global_interrupts(core_if);
			cil_pcd_start(core_if);
		}
	}

	if ((core_if->core_params->phy_type == DWC_PHY_TYPE_PARAM_FS) &&
	    (core_if->core_params->i2c_enable)) {
		DWC_PRINTF("SRP Timeout\n");

		if ((core_if->srp_success) && (gotgctl.b.bsesvld)) {
			if (core_if->pcd_cb && core_if->pcd_cb->resume_wakeup) {
				core_if->pcd_cb->resume_wakeup(core_if->pcd_cb->
							       p);
			}

			/* Clear Session Request */
			gotgctl.d32 = 0;
			gotgctl.b.sesreq = 1;
			DWC_MODIFY_REG32(&core_if->core_global_regs->gotgctl,
					 gotgctl.d32, 0);

			core_if->srp_success = 0;
		} else {
			__DWC_ERROR("Device not connected/responding\n");
			gotgctl.b.sesreq = 0;
			DWC_WRITE_REG32(addr, gotgctl.d32);
		}
	} else if (gotgctl.b.sesreq) {
		DWC_PRINTF("SRP Timeout\n");

		__DWC_ERROR("Device not connected/responding\n");
		gotgctl.b.sesreq = 0;
		DWC_WRITE_REG32(addr, gotgctl.d32);
	} else {
		DWC_PRINTF(" SRP GOTGCTL=%0x\n", gotgctl.d32);
	}
}

/**
 * Tasklet
 *
 */
extern void start_next_request(dwc_otg_pcd_ep_t *ep);

static void start_xfer_tasklet_func(void *data)
{
	dwc_otg_pcd_t *pcd = (dwc_otg_pcd_t *) data;
	dwc_otg_core_if_t *core_if = GET_CORE_IF(pcd);

	int i;
	depctl_data_t diepctl;

	DWC_DEBUGPL(DBG_PCDV, "Start xfer tasklet\n");

	diepctl.d32 = DWC_READ_REG32(&core_if->dev_if->in_ep_regs[0]->diepctl);

	if (pcd->ep0.queue_sof) {
		pcd->ep0.queue_sof = 0;
		start_next_request(&pcd->ep0);
		/* break; */
	}

	for (i = 0; i < core_if->dev_if->num_in_eps; i++) {
		depctl_data_t diepctl;
		diepctl.d32 =
		    DWC_READ_REG32(&core_if->dev_if->in_ep_regs[i]->diepctl);

		if (pcd->in_ep[i].queue_sof) {
			pcd->in_ep[i].queue_sof = 0;
			start_next_request(&pcd->in_ep[i]);
			/* break; */
		}
	}

	return;
}

/**
 * This function initialized the PCD portion of the driver.
 *
 */
dwc_otg_pcd_t *dwc_otg_pcd_init(dwc_otg_core_if_t *core_if)
{
	dwc_otg_pcd_t *pcd = NULL;
	dwc_otg_dev_if_t *dev_if;
	int i;

	/*
	 * Allocate PCD structure
	 */
	pcd = DWC_ALLOC(sizeof(dwc_otg_pcd_t));

	if (pcd == NULL) {
		return NULL;
	}

	pcd->lock = DWC_SPINLOCK_ALLOC();
	if (!pcd->lock) {
		DWC_ERROR("Could not allocate lock for pcd");
		DWC_FREE(pcd);
		return NULL;
	}
	/* Set core_if's lock pointer to hcd->lock */
	core_if->lock = pcd->lock;
	pcd->core_if = core_if;

	dev_if = core_if->dev_if;
	dev_if->isoc_ep = NULL;

	if (core_if->hwcfg4.b.ded_fifo_en) {
		DWC_PRINTF("Dedicated Tx FIFOs mode\n");
	} else {
		DWC_PRINTF("Shared Tx FIFO mode\n");
	}

	/*
	 * Initialized the Core for Device mode here if there is nod ADP support.
	 * Otherwise it will be done later in dwc_otg_adp_start routine.
	 */
	/* if (dwc_otg_is_device_mode(core_if) ) { */
	/*      dwc_otg_core_dev_init(core_if); */
	/*} */

	/*
	 * Register the PCD Callbacks.
	 */
	dwc_otg_cil_register_pcd_callbacks(core_if, &pcd_callbacks, pcd);

	/*
	 * Initialize the DMA buffer for SETUP packets
	 */
	if (GET_CORE_IF(pcd)->dma_enable) {
		pcd->setup_pkt =
		    DWC_DEV_DMA_ALLOC_ATOMIC(sizeof(*pcd->setup_pkt) * 5,
					     &pcd->setup_pkt_dma_handle);
		if (pcd->setup_pkt == NULL) {
			DWC_FREE(pcd);
			return NULL;
		}

		pcd->status_buf =
		    DWC_DEV_DMA_ALLOC_ATOMIC(sizeof(uint16_t),
					     &pcd->status_buf_dma_handle);
		if (pcd->status_buf == NULL) {
			DWC_DEV_DMA_FREE(sizeof(*pcd->setup_pkt) * 5,
					 pcd->setup_pkt,
					 pcd->setup_pkt_dma_handle);
			DWC_FREE(pcd);
			return NULL;
		}

		if (GET_CORE_IF(pcd)->dma_desc_enable) {
			dev_if->setup_desc_addr[0] =
			    dwc_otg_ep_alloc_desc_chain
			    (&dev_if->dma_setup_desc_addr[0], 1);
			dev_if->setup_desc_addr[1] =
			    dwc_otg_ep_alloc_desc_chain
			    (&dev_if->dma_setup_desc_addr[1], 1);
			dev_if->in_desc_addr =
			    dwc_otg_ep_alloc_desc_chain
			    (&dev_if->dma_in_desc_addr, 1);
			dev_if->out_desc_addr =
			    dwc_otg_ep_alloc_desc_chain
			    (&dev_if->dma_out_desc_addr, 1);
			pcd->data_terminated = 0;

			if (dev_if->setup_desc_addr[0] == 0
			    || dev_if->setup_desc_addr[1] == 0
			    || dev_if->in_desc_addr == 0
			    || dev_if->out_desc_addr == 0) {

				if (dev_if->out_desc_addr)
					dwc_otg_ep_free_desc_chain
					    (dev_if->out_desc_addr,
					     dev_if->dma_out_desc_addr, 1);
				if (dev_if->in_desc_addr)
					dwc_otg_ep_free_desc_chain
					    (dev_if->in_desc_addr,
					     dev_if->dma_in_desc_addr, 1);
				if (dev_if->setup_desc_addr[1])
					dwc_otg_ep_free_desc_chain
					    (dev_if->setup_desc_addr[1],
					     dev_if->dma_setup_desc_addr[1], 1);
				if (dev_if->setup_desc_addr[0])
					dwc_otg_ep_free_desc_chain
					    (dev_if->setup_desc_addr[0],
					     dev_if->dma_setup_desc_addr[0], 1);

				DWC_DEV_DMA_FREE(sizeof(*pcd->setup_pkt) * 5,
						 pcd->setup_pkt,
						 pcd->setup_pkt_dma_handle);
				DWC_DEV_DMA_FREE(sizeof(*pcd->status_buf),
						 pcd->status_buf,
						 pcd->status_buf_dma_handle);

				DWC_FREE(pcd);

				return NULL;
			}
		}
	} else {
		pcd->setup_pkt = DWC_ALLOC(sizeof(*pcd->setup_pkt) * 5);
		if (pcd->setup_pkt == NULL) {
			DWC_FREE(pcd);
			return NULL;
		}

		pcd->status_buf = DWC_ALLOC(sizeof(uint16_t));
		if (pcd->status_buf == NULL) {
			DWC_FREE(pcd->setup_pkt);
			DWC_FREE(pcd);
			return NULL;
		}
	}

	dwc_otg_pcd_reinit(pcd);

	/* Allocate the cfi object for the PCD */
#ifdef DWC_UTE_CFI
	pcd->cfi = DWC_ALLOC(sizeof(cfiobject_t));
	if (NULL == pcd->cfi)
		goto fail;
	if (init_cfi(pcd->cfi)) {
		CFI_INFO("%s: Failed to init the CFI object\n", __func__);
		goto fail;
	}
#endif

	/* Initialize tasklets */
	pcd->start_xfer_tasklet = DWC_TASK_ALLOC("xfer_tasklet",
						 start_xfer_tasklet_func, pcd);
	pcd->test_mode_tasklet = DWC_TASK_ALLOC("test_mode_tasklet",
						do_test_mode, pcd);

	/* Initialize SRP timer */
	core_if->srp_timer = DWC_TIMER_ALLOC("SRP TIMER", srp_timeout, core_if);

	if (core_if->core_params->dev_out_nak) {
		/**
		* Initialize xfer timeout timer. Implemented for
		* 2.93a feature "Device DDMA OUT NAK Enhancement"
		*/
		for (i = 0; i < MAX_EPS_CHANNELS; i++) {
			pcd->core_if->ep_xfer_timer[i] =
			    DWC_TIMER_ALLOC("ep timer", ep_xfer_timeout,
					    &pcd->core_if->ep_xfer_info[i]);
		}
	}

	return pcd;
#ifdef DWC_UTE_CFI
fail:
#endif
	if (pcd->setup_pkt)
		DWC_FREE(pcd->setup_pkt);
	if (pcd->status_buf)
		DWC_FREE(pcd->status_buf);
#ifdef DWC_UTE_CFI
	if (pcd->cfi)
		DWC_FREE(pcd->cfi);
#endif
	if (pcd)
		DWC_FREE(pcd);
	return NULL;

}

/**
 * Remove PCD specific data
 */
void dwc_otg_pcd_remove(dwc_otg_pcd_t *pcd)
{
	dwc_otg_dev_if_t *dev_if = GET_CORE_IF(pcd)->dev_if;
	int i;
	if (pcd->core_if->core_params->dev_out_nak) {
		for (i = 0; i < MAX_EPS_CHANNELS; i++) {
			DWC_TIMER_CANCEL(pcd->core_if->ep_xfer_timer[i]);
			pcd->core_if->ep_xfer_info[i].state = 0;
		}
	}

	if (GET_CORE_IF(pcd)->dma_enable) {
		DWC_DEV_DMA_FREE(sizeof(*pcd->setup_pkt) * 5, pcd->setup_pkt,
				 pcd->setup_pkt_dma_handle);
		DWC_DEV_DMA_FREE(sizeof(uint16_t), pcd->status_buf,
				 pcd->status_buf_dma_handle);
		if (GET_CORE_IF(pcd)->dma_desc_enable) {
			dwc_otg_ep_free_desc_chain(dev_if->setup_desc_addr[0],
						   dev_if->dma_setup_desc_addr
						   [0], 1);
			dwc_otg_ep_free_desc_chain(dev_if->setup_desc_addr[1],
						   dev_if->dma_setup_desc_addr
						   [1], 1);
			dwc_otg_ep_free_desc_chain(dev_if->in_desc_addr,
						   dev_if->dma_in_desc_addr, 1);
			dwc_otg_ep_free_desc_chain(dev_if->out_desc_addr,
						   dev_if->dma_out_desc_addr,
						   1);
		}
	} else {
		DWC_FREE(pcd->setup_pkt);
		DWC_FREE(pcd->status_buf);
	}
	DWC_SPINLOCK_FREE(pcd->lock);
	/* Set core_if's lock pointer to NULL */
	pcd->core_if->lock = NULL;

	DWC_TASK_FREE(pcd->start_xfer_tasklet);
	DWC_TASK_FREE(pcd->test_mode_tasklet);
	if (pcd->core_if->core_params->dev_out_nak) {
		for (i = 0; i < MAX_EPS_CHANNELS; i++) {
			if (pcd->core_if->ep_xfer_timer[i]) {
				DWC_TIMER_FREE(pcd->core_if->ep_xfer_timer[i]);
			}
		}
	}

/* Release the CFI object's dynamic memory */
#ifdef DWC_UTE_CFI
	if (pcd->cfi->ops.release) {
		pcd->cfi->ops.release(pcd->cfi);
	}
#endif

	DWC_FREE(pcd);
}

/**
 * Returns whether registered pcd is dual speed or not
 */
uint32_t dwc_otg_pcd_is_dualspeed(dwc_otg_pcd_t *pcd)
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(pcd);

	if ((core_if->core_params->speed == DWC_SPEED_PARAM_FULL) ||
	    ((core_if->hwcfg2.b.hs_phy_type == 2) &&
	     (core_if->hwcfg2.b.fs_phy_type == 1) &&
	     (core_if->core_params->ulpi_fs_ls))) {
		return 0;
	}

	return 1;
}

/**
 * Returns whether registered pcd is OTG capable or not
 */
uint32_t dwc_otg_pcd_is_otg(dwc_otg_pcd_t *pcd)
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(pcd);
	gusbcfg_data_t usbcfg = {.d32 = 0 };
	uint32_t retval = 0;

	usbcfg.d32 = DWC_READ_REG32(&core_if->core_global_regs->gusbcfg);
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
	if (!usbcfg.b.srpcap || !usbcfg.b.hnpcap)
		return 0;
	else
		return 1;
# else
	if (!usbcfg.b.srpcap)
		return 0;
	else
		retval |= 1;

	if (usbcfg.b.hnpcap)
		retval |= 2;

	if (core_if->adp_enable)
		retval |= 4;
#endif

	return retval;
}

/**
 * This function assigns periodic Tx FIFO to an periodic EP
 * in shared Tx FIFO mode
 */
static uint32_t assign_tx_fifo(dwc_otg_core_if_t *core_if)
{
	uint32_t TxMsk = 1;
	int i;

	for (i = 0; i < core_if->hwcfg4.b.num_in_eps; ++i) {
		if ((TxMsk & core_if->tx_msk) == 0) {
			core_if->tx_msk |= TxMsk;
			return i + 1;
		}
		TxMsk <<= 1;
	}
	return 0;
}

/**
 * This function assigns periodic Tx FIFO to an periodic EP
 * in shared Tx FIFO mode
 */
static uint32_t assign_perio_tx_fifo(dwc_otg_core_if_t *core_if)
{
	uint32_t PerTxMsk = 1;
	int i;
	for (i = 0; i < core_if->hwcfg4.b.num_dev_perio_in_ep; ++i) {
		if ((PerTxMsk & core_if->p_tx_msk) == 0) {
			core_if->p_tx_msk |= PerTxMsk;
			return i + 1;
		}
		PerTxMsk <<= 1;
	}
	return 0;
}

/**
 * This function releases periodic Tx FIFO
 * in shared Tx FIFO mode
 */
static void release_perio_tx_fifo(dwc_otg_core_if_t *core_if,
				  uint32_t fifo_num)
{
	core_if->p_tx_msk =
	    (core_if->p_tx_msk & (1 << (fifo_num - 1))) ^ core_if->p_tx_msk;
}

/**
 * This function releases periodic Tx FIFO
 * in shared Tx FIFO mode
 */
static void release_tx_fifo(dwc_otg_core_if_t *core_if, uint32_t fifo_num)
{
	core_if->tx_msk =
	    (core_if->tx_msk & (1 << (fifo_num - 1))) ^ core_if->tx_msk;
}

/**
 * This function is being called from gadget
 * to enable PCD endpoint.
 */
int dwc_otg_pcd_ep_enable(dwc_otg_pcd_t *pcd,
			  const uint8_t *ep_desc, void *usb_ep)
{
	int num, dir;
	dwc_otg_pcd_ep_t *ep = NULL;
	const usb_endpoint_descriptor_t *desc;
	dwc_irqflags_t flags;
	/* fifosize_data_t dptxfsiz = {.d32 = 0 }; */
	/* gdfifocfg_data_t gdfifocfg = {.d32 = 0 }; */
	/* gdfifocfg_data_t gdfifocfgbase = {.d32 = 0 }; */
	int retval = 0;
	int i, epcount;

	desc = (const usb_endpoint_descriptor_t *)ep_desc;

	if (!desc) {
		pcd->ep0.priv = usb_ep;
		ep = &pcd->ep0;
		retval = -DWC_E_INVALID;
		goto out;
	}

	num = UE_GET_ADDR(desc->bEndpointAddress);
	dir = UE_GET_DIR(desc->bEndpointAddress);

	if (!desc->wMaxPacketSize) {
		DWC_WARN("bad maxpacketsize\n");
		retval = -DWC_E_INVALID;
		goto out;
	}

	if (dir == UE_DIR_IN) {
		epcount = pcd->core_if->dev_if->num_in_eps;
		for (i = 0; i < epcount; i++) {
			if (num == pcd->in_ep[i].dwc_ep.num) {
				ep = &pcd->in_ep[i];
				break;
			}
		}
	} else {
		epcount = pcd->core_if->dev_if->num_out_eps;
		for (i = 0; i < epcount; i++) {
			if (num == pcd->out_ep[i].dwc_ep.num) {
				ep = &pcd->out_ep[i];
				break;
			}
		}
	}

	if (!ep) {
		DWC_WARN("bad address\n");
		retval = -DWC_E_INVALID;
		goto out;
	}

	DWC_SPINLOCK_IRQSAVE(pcd->lock, &flags);

	ep->desc = desc;
	ep->priv = usb_ep;

	/*
	 * Activate the EP
	 */
	ep->stopped = 0;

	ep->dwc_ep.is_in = (dir == UE_DIR_IN);
	ep->dwc_ep.maxpacket = UGETW(desc->wMaxPacketSize);

	ep->dwc_ep.type = desc->bmAttributes & UE_XFERTYPE;

	if (ep->dwc_ep.is_in) {
		if (!GET_CORE_IF(pcd)->en_multiple_tx_fifo) {
			ep->dwc_ep.tx_fifo_num = 0;

			if (ep->dwc_ep.type == UE_ISOCHRONOUS) {
				/*
				 * if ISOC EP then assign a Periodic Tx FIFO.
				 */
				ep->dwc_ep.tx_fifo_num =
				    assign_perio_tx_fifo(GET_CORE_IF(pcd));
			}
		} else {
			/*
			 * if Dedicated FIFOs mode is on then assign a Tx FIFO.
			 */
			ep->dwc_ep.tx_fifo_num =
			    assign_tx_fifo(GET_CORE_IF(pcd));
		}

		/* Calculating EP info controller base address */
#if 0
		if (ep->dwc_ep.tx_fifo_num
		    && GET_CORE_IF(pcd)->en_multiple_tx_fifo) {
			gdfifocfg.d32 =
			    DWC_READ_REG32(&GET_CORE_IF(pcd)->core_global_regs->
					   gdfifocfg);
			gdfifocfgbase.d32 = gdfifocfg.d32 >> 16;
			dptxfsiz.d32 =
			    (DWC_READ_REG32
			     (&GET_CORE_IF(pcd)->
			      core_global_regs->dtxfsiz[ep->dwc_ep.tx_fifo_num -
							1]) >> 16);
			gdfifocfg.b.epinfobase =
			    gdfifocfgbase.d32 + dptxfsiz.d32;
			if (GET_CORE_IF(pcd)->snpsid <= OTG_CORE_REV_2_94a) {
				DWC_WRITE_REG32(&GET_CORE_IF
						(pcd)->core_global_regs->
						gdfifocfg, gdfifocfg.d32);
			}
		}
#endif
	}
	/* Set initial data PID. */
	if (ep->dwc_ep.type == UE_BULK) {
		ep->dwc_ep.data_pid_start = 0;
	}

	/* Alloc DMA Descriptors */
	if (GET_CORE_IF(pcd)->dma_desc_enable) {
#ifndef DWC_UTE_PER_IO
		if (ep->dwc_ep.type != UE_ISOCHRONOUS) {
#endif
			ep->dwc_ep.desc_addr =
			    dwc_otg_ep_alloc_desc_chain(&ep->dwc_ep.
							dma_desc_addr,
							MAX_DMA_DESC_CNT);
			if (!ep->dwc_ep.desc_addr) {
				DWC_WARN("%s, can't allocate DMA descriptor\n",
					 __func__);
				retval = -DWC_E_SHUTDOWN;
				DWC_SPINUNLOCK_IRQRESTORE(pcd->lock, flags);
				goto out;
			}
#ifndef DWC_UTE_PER_IO
		}
#endif
	}

	DWC_DEBUGPL(DBG_PCD, "Activate %s: type=%d, mps=%d desc=%p\n",
		    (ep->dwc_ep.is_in ? "IN" : "OUT"),
		    ep->dwc_ep.type, ep->dwc_ep.maxpacket, ep->desc);
#ifdef DWC_UTE_PER_IO
	ep->dwc_ep.xiso_bInterval = 1 << (ep->desc->bInterval - 1);
#endif
	if (ep->dwc_ep.type == DWC_OTG_EP_TYPE_ISOC) {
		ep->dwc_ep.bInterval = 1 << (ep->desc->bInterval - 1);
		ep->dwc_ep.frame_num = 0xFFFFFFFF;
	}

	dwc_otg_ep_activate(GET_CORE_IF(pcd), &ep->dwc_ep);

#ifdef DWC_UTE_CFI
	if (pcd->cfi->ops.ep_enable) {
		pcd->cfi->ops.ep_enable(pcd->cfi, pcd, ep);
	}
#endif

	DWC_SPINUNLOCK_IRQRESTORE(pcd->lock, flags);

out:
	return retval;
}

/**
 * This function is being called from gadget
 * to disable PCD endpoint.
 */
int dwc_otg_pcd_ep_disable(dwc_otg_pcd_t *pcd, void *ep_handle)
{
	dwc_otg_pcd_ep_t *ep;
	dwc_irqflags_t flags;
	dwc_otg_dev_dma_desc_t *desc_addr;
	dwc_dma_t dma_desc_addr;
	gdfifocfg_data_t gdfifocfgbase = {.d32 = 0 };
	gdfifocfg_data_t gdfifocfg = {.d32 = 0 };
	/* fifosize_data_t dptxfsiz = {.d32 = 0 }; */

	ep = get_ep_from_handle(pcd, ep_handle);

	if (!ep || !ep->desc) {
		DWC_DEBUGPL(DBG_PCD, "bad ep address\n");
		return -DWC_E_INVALID;
	}

	DWC_SPINLOCK_IRQSAVE(pcd->lock, &flags);

	dwc_otg_request_nuke(ep);

	dwc_otg_ep_deactivate(GET_CORE_IF(pcd), &ep->dwc_ep);
	if (pcd->core_if->core_params->dev_out_nak) {
		DWC_TIMER_CANCEL(pcd->core_if->ep_xfer_timer[ep->dwc_ep.num]);
		pcd->core_if->ep_xfer_info[ep->dwc_ep.num].state = 0;
	}
	ep->desc = NULL;
	ep->stopped = 1;

	gdfifocfg.d32 =
	    DWC_READ_REG32(&GET_CORE_IF(pcd)->core_global_regs->gdfifocfg);
	gdfifocfgbase.d32 = gdfifocfg.d32 >> 16;

	if (ep->dwc_ep.is_in) {
		if (GET_CORE_IF(pcd)->en_multiple_tx_fifo) {
			/* Flush the Tx FIFO */
			dwc_otg_flush_tx_fifo(GET_CORE_IF(pcd),
					      ep->dwc_ep.tx_fifo_num);
		}
		release_perio_tx_fifo(GET_CORE_IF(pcd), ep->dwc_ep.tx_fifo_num);
		release_tx_fifo(GET_CORE_IF(pcd), ep->dwc_ep.tx_fifo_num);
#if 0
		if (GET_CORE_IF(pcd)->en_multiple_tx_fifo) {
			/* Decreasing EPinfo Base Addr */
			dptxfsiz.d32 =
			    (DWC_READ_REG32
			     (&GET_CORE_IF(pcd)->core_global_regs->
			      dtxfsiz[ep->dwc_ep.tx_fifo_num - 1]) >> 16);
			gdfifocfg.b.epinfobase =
			    gdfifocfgbase.d32 - dptxfsiz.d32;
			if (GET_CORE_IF(pcd)->snpsid <= OTG_CORE_REV_2_94a) {
				DWC_WRITE_REG32(&GET_CORE_IF(pcd)->
						core_global_regs->gdfifocfg,
						gdfifocfg.d32);
			}
		}
#endif
	}

	/* Free DMA Descriptors */
	if (GET_CORE_IF(pcd)->dma_desc_enable) {
		if (ep->dwc_ep.type != UE_ISOCHRONOUS) {
			desc_addr = ep->dwc_ep.desc_addr;
			dma_desc_addr = ep->dwc_ep.dma_desc_addr;

			/* Cannot call dma_free_coherent() with IRQs disabled */
			DWC_SPINUNLOCK_IRQRESTORE(pcd->lock, flags);
			dwc_otg_ep_free_desc_chain(desc_addr, dma_desc_addr,
						   MAX_DMA_DESC_CNT);

			goto out_unlocked;
		}
	}
	DWC_SPINUNLOCK_IRQRESTORE(pcd->lock, flags);

out_unlocked:
	DWC_DEBUGPL(DBG_PCD, "%d %s disabled\n", ep->dwc_ep.num,
		    ep->dwc_ep.is_in ? "IN" : "OUT");
	return 0;

}

static int dwc_otg_wait_bit_set(volatile uint32_t *reg,
				uint32_t bit, uint32_t timeout)
{
	int i;

	for (i = 0; i < timeout; i++) {
		if (DWC_READ_REG32(reg) & bit)
			return 0;

		dwc_udelay(1);
	}

	return -ETIMEDOUT;
}

static void dwc_otg_pcd_ep_stop_transfer(dwc_otg_core_if_t
					 *core_if, dwc_ep_t *ep)
{
	depctl_data_t depctl = {.d32 = 0 };

	/* Read DEPCTLn register */
	if (ep->is_in == 1)
		depctl.d32 = DWC_READ_REG32(&core_if->dev_if->
					     in_ep_regs[ep->num]->
					     diepctl);
	else
		depctl.d32 = DWC_READ_REG32(&core_if->dev_if->
					    out_ep_regs[ep->num]->
					    doepctl);

	if (ep->is_in == 1) {
		diepint_data_t diepint = {.d32 = 0 };

		depctl.b.snak = 1;
		DWC_WRITE_REG32(&core_if->dev_if->
				in_ep_regs[ep->num]->diepctl,
				depctl.d32);

		diepint.b.inepnakeff = 1;
		/* Wait for Nak effect */
		if (dwc_otg_wait_bit_set(&core_if->dev_if->
					 in_ep_regs[ep->num]
					 ->diepint,
					 diepint.d32,
					 100)) {
			DWC_WARN("%s: timeout diepctl.snak\n",
				 __func__);
		} else {
			DWC_WRITE_REG32(&core_if->dev_if->
					in_ep_regs[ep->num]->
					diepint, diepint.d32);
		}

		depctl.b.epdis = 1;
		DWC_WRITE_REG32(&core_if->dev_if->
				in_ep_regs[ep->num]->diepctl,
				depctl.d32);

		diepint.d32 = 0;
		diepint.b.epdisabled = 1;
		if (dwc_otg_wait_bit_set(&core_if->dev_if->
					 in_ep_regs[ep->num]
					 ->diepint,
					 diepint.d32,
					 100)) {
			DWC_WARN("%s: timeout diepctl.epdis\n",
				 __func__);
		} else {
			DWC_WRITE_REG32(&core_if->dev_if->
					in_ep_regs[ep->num]->
					diepint, diepint.d32);
		}
	} else {
		dctl_data_t dctl = {.d32 = 0 };
		gintmsk_data_t gintsts = {.d32 = 0 };
		doepint_data_t doepint = {.d32 = 0 };

		dctl.b.sgoutnak = 1;
		DWC_MODIFY_REG32(&core_if->dev_if->
				 dev_global_regs->dctl, 0, dctl.d32);

		/* Wait for global nak to take effect */
		gintsts.d32 = 0;
		gintsts.b.goutnakeff = 1;
		if (dwc_otg_wait_bit_set(&core_if->core_global_regs->
					 gintsts, gintsts.d32,
					 100)) {
			DWC_WARN("%s: timeout dctl.sgoutnak\n",
				 __func__);
		} else {
			DWC_WRITE_REG32(&core_if->core_global_regs
					->gintsts, gintsts.d32);
		}

		depctl.b.epdis = 1;
		depctl.b.snak = 1;
		DWC_WRITE_REG32(&core_if->dev_if->out_ep_regs[ep->num]->
				doepctl, depctl.d32);

		doepint.b.epdisabled = 1;
		if (dwc_otg_wait_bit_set(&core_if->dev_if
					 ->out_ep_regs[ep->num]
					 ->doepint, doepint.d32,
					 100)) {
			DWC_WARN("%s: timeout doepctl.epdis\n",
				 __func__);
		} else {
			DWC_WRITE_REG32(&core_if->dev_if->
					out_ep_regs[ep->num]->
					doepint, doepint.d32);
		}

		dctl.d32 = 0;
		dctl.b.cgoutnak = 1;
		DWC_MODIFY_REG32(&core_if->dev_if->dev_global_regs->
				 dctl, 0, dctl.d32);
	}
}

/******************************************************************************/
#ifdef DWC_UTE_PER_IO

/**
 * Free the request and its extended parts
 *
 */
void dwc_pcd_xiso_ereq_free(dwc_otg_pcd_ep_t *ep, dwc_otg_pcd_request_t *req)
{
	DWC_FREE(req->ext_req.per_io_frame_descs);
	DWC_FREE(req);
}

/**
 * Start the next request in the endpoint's queue.
 *
 */
int dwc_otg_pcd_xiso_start_next_request(dwc_otg_pcd_t *pcd,
					dwc_otg_pcd_ep_t *ep)
{
	int i;
	dwc_otg_pcd_request_t *req = NULL;
	dwc_ep_t *dwcep = NULL;
	struct dwc_iso_xreq_port *ereq = NULL;
	struct dwc_iso_pkt_desc_port *ddesc_iso;
	uint16_t nat;
	depctl_data_t diepctl;

	dwcep = &ep->dwc_ep;

	if (dwcep->xiso_active_xfers > 0) {
#if 0
		/* Disable this to decrease s/w overhead
		 * that is crucial for Isoc transfers */
		DWC_WARN("There are currently active transfers for EP%d \
			(active=%d; queued=%d)", dwcep->num,
			dwcep->xiso_active_xfers, dwcep->xiso_queued_xfers);
#endif
		return 0;
	}

	nat = UGETW(ep->desc->wMaxPacketSize);
	nat = (nat >> 11) & 0x03;

	if (!DWC_CIRCLEQ_EMPTY(&ep->queue)) {
		req = DWC_CIRCLEQ_FIRST(&ep->queue);
		ereq = &req->ext_req;
		ep->stopped = 0;

		/* Get the frame number */
		dwcep->xiso_frame_num =
		    dwc_otg_get_frame_number(GET_CORE_IF(pcd));
		DWC_DEBUG("FRM_NUM=%d", dwcep->xiso_frame_num);

		ddesc_iso = ereq->per_io_frame_descs;

		if (dwcep->is_in) {
			/* Setup DMA Descriptor chain for IN Isoc request */
			for (i = 0; i < ereq->pio_pkt_count; i++) {
				/* if ((i % (nat + 1)) == 0) */
				if (i > 0)
					dwcep->xiso_frame_num =
					    (dwcep->xiso_bInterval +
					     dwcep->xiso_frame_num) & 0x3FFF;
				dwcep->desc_addr[i].buf =
				    req->dma + ddesc_iso[i].offset;
				dwcep->desc_addr[i].status.b_iso_in.txbytes =
				    ddesc_iso[i].length;
				dwcep->desc_addr[i].status.b_iso_in.framenum =
				    dwcep->xiso_frame_num;
				dwcep->desc_addr[i].status.b_iso_in.bs =
				    BS_HOST_READY;
				dwcep->desc_addr[i].status.b_iso_in.txsts = 0;
				dwcep->desc_addr[i].status.b_iso_in.sp =
				    (ddesc_iso[i].length %
				     dwcep->maxpacket) ? 1 : 0;
				dwcep->desc_addr[i].status.b_iso_in.ioc = 0;
				dwcep->desc_addr[i].status.b_iso_in.pid =
				    nat + 1;
				dwcep->desc_addr[i].status.b_iso_in.l = 0;

				/* Process the last descriptor */
				if (i == ereq->pio_pkt_count - 1) {
					dwcep->desc_addr[i].status.b_iso_in.
					    ioc = 1;
					dwcep->desc_addr[i].status.b_iso_in.l =
					    1;
				}
			}

			/* Setup and start the transfer for this endpoint */
			dwcep->xiso_active_xfers++;
			DWC_WRITE_REG32(&GET_CORE_IF(pcd)->
					dev_if->in_ep_regs[dwcep->num]->diepdma,
					dwcep->dma_desc_addr);
			diepctl.d32 = 0;
			diepctl.b.epena = 1;
			diepctl.b.cnak = 1;
			DWC_MODIFY_REG32(&GET_CORE_IF(pcd)->
					 dev_if->in_ep_regs[dwcep->num]->
					 diepctl, 0, diepctl.d32);
		} else {
			/* Setup DMA Descriptor chain for OUT Isoc request */
			for (i = 0; i < ereq->pio_pkt_count; i++) {
				/* if ((i % (nat + 1)) == 0) */
				dwcep->xiso_frame_num = (dwcep->xiso_bInterval +
							 dwcep->
							 xiso_frame_num) &
				    0x3FFF;
				dwcep->desc_addr[i].buf =
				    req->dma + ddesc_iso[i].offset;
				dwcep->desc_addr[i].status.b_iso_out.rxbytes =
				    ddesc_iso[i].length;
				dwcep->desc_addr[i].status.b_iso_out.framenum =
				    dwcep->xiso_frame_num;
				dwcep->desc_addr[i].status.b_iso_out.bs =
				    BS_HOST_READY;
				dwcep->desc_addr[i].status.b_iso_out.rxsts = 0;
				dwcep->desc_addr[i].status.b_iso_out.sp =
				    (ddesc_iso[i].length %
				     dwcep->maxpacket) ? 1 : 0;
				dwcep->desc_addr[i].status.b_iso_out.ioc = 0;
				dwcep->desc_addr[i].status.b_iso_out.pid =
				    nat + 1;
				dwcep->desc_addr[i].status.b_iso_out.l = 0;

				/* Process the last descriptor */
				if (i == ereq->pio_pkt_count - 1) {
					dwcep->desc_addr[i].status.b_iso_out.
					    ioc = 1;
					dwcep->desc_addr[i].status.b_iso_out.l =
					    1;
				}
			}

			/* Setup and start the transfer for this endpoint */
			dwcep->xiso_active_xfers++;
			DWC_WRITE_REG32(&GET_CORE_IF(pcd)->dev_if->
					out_ep_regs[dwcep->num]->doepdma,
					dwcep->dma_desc_addr);
			diepctl.d32 = 0;
			diepctl.b.epena = 1;
			diepctl.b.cnak = 1;
			DWC_MODIFY_REG32(&GET_CORE_IF(pcd)->dev_if->
					 out_ep_regs[dwcep->num]->doepctl, 0,
					 diepctl.d32);
		}

	} else {
		ep->stopped = 1;
	}

	return 0;
}

/**
 *	- Remove the request from the queue
 */
void complete_xiso_ep(dwc_otg_pcd_ep_t *ep)
{
	dwc_otg_pcd_request_t *req = NULL;
	struct dwc_iso_xreq_port *ereq = NULL;
	struct dwc_iso_pkt_desc_port *ddesc_iso = NULL;
	dwc_ep_t *dwcep = NULL;
	int i;

	/* DWC_DEBUG(); */
	dwcep = &ep->dwc_ep;

	/* Get the first pending request from the queue */
	if (!DWC_CIRCLEQ_EMPTY(&ep->queue)) {
		req = DWC_CIRCLEQ_FIRST(&ep->queue);
		if (!req) {
			DWC_PRINTF("complete_ep 0x%p, req = NULL!\n", ep);
			return;
		}
		dwcep->xiso_active_xfers--;
		dwcep->xiso_queued_xfers--;
		/* Remove this request from the queue */
		DWC_CIRCLEQ_REMOVE_INIT(&ep->queue, req, queue_entry);
	} else {
		DWC_PRINTF("complete_ep 0x%p, ep->queue empty!\n", ep);
		return;
	}

	ep->stopped = 1;
	ereq = &req->ext_req;
	ddesc_iso = ereq->per_io_frame_descs;

	if (dwcep->xiso_active_xfers < 0) {
		DWC_WARN("EP#%d (xiso_active_xfers=%d)", dwcep->num,
			 dwcep->xiso_active_xfers);
	}

	/* Fill the Isoc descs of portable extended req from dma descriptors */
	for (i = 0; i < ereq->pio_pkt_count; i++) {
		if (dwcep->is_in) {	/* IN endpoints */
			ddesc_iso[i].actual_length = ddesc_iso[i].length -
			    dwcep->desc_addr[i].status.b_iso_in.txbytes;
			ddesc_iso[i].status =
			    dwcep->desc_addr[i].status.b_iso_in.txsts;
		} else {	/* OUT endpoints */
			ddesc_iso[i].actual_length = ddesc_iso[i].length -
			    dwcep->desc_addr[i].status.b_iso_out.rxbytes;
			ddesc_iso[i].status =
			    dwcep->desc_addr[i].status.b_iso_out.rxsts;
		}
	}

	DWC_SPINUNLOCK(ep->pcd->lock);

	/* Call the completion function in the non-portable logic */
	ep->pcd->fops->xisoc_complete(ep->pcd, ep->priv, req->priv, 0,
				      &req->ext_req);

	DWC_SPINLOCK(ep->pcd->lock);

	/* Free the request - specific freeing needed for extended request object */
	dwc_pcd_xiso_ereq_free(ep, req);

	/* Start the next request */
	dwc_otg_pcd_xiso_start_next_request(ep->pcd, ep);

	return;
}

/**
 * Create and initialize the Isoc pkt descriptors of the extended request.
 *
 */
static int dwc_otg_pcd_xiso_create_pkt_descs(dwc_otg_pcd_request_t *req,
					     void *ereq_nonport,
					     int atomic_alloc)
{
	struct dwc_iso_xreq_port *ereq = NULL;
	struct dwc_iso_xreq_port *req_mapped = NULL;
	struct dwc_iso_pkt_desc_port *ipds = NULL;	/* To be created in this function */
	uint32_t pkt_count;
	int i;

	ereq = &req->ext_req;
	req_mapped = (struct dwc_iso_xreq_port *)ereq_nonport;
	pkt_count = req_mapped->pio_pkt_count;

	/* Create the isoc descs */
	if (atomic_alloc) {
		ipds = DWC_ALLOC_ATOMIC(sizeof(*ipds) * pkt_count);
	} else {
		ipds = DWC_ALLOC(sizeof(*ipds) * pkt_count);
	}

	if (!ipds) {
		DWC_ERROR("Failed to allocate isoc descriptors");
		return -DWC_E_NO_MEMORY;
	}

	/* Initialize the extended request fields */
	ereq->per_io_frame_descs = ipds;
	ereq->error_count = 0;
	ereq->pio_alloc_pkt_count = pkt_count;
	ereq->pio_pkt_count = pkt_count;
	ereq->tr_sub_flags = req_mapped->tr_sub_flags;

	/* Init the Isoc descriptors */
	for (i = 0; i < pkt_count; i++) {
		ipds[i].length = req_mapped->per_io_frame_descs[i].length;
		ipds[i].offset = req_mapped->per_io_frame_descs[i].offset;
		ipds[i].status = req_mapped->per_io_frame_descs[i].status;	/* 0 */
		ipds[i].actual_length =
		    req_mapped->per_io_frame_descs[i].actual_length;
	}

	return 0;
}

static void prn_ext_request(struct dwc_iso_xreq_port *ereq)
{
	struct dwc_iso_pkt_desc_port *xfd = NULL;
	int i;

	DWC_DEBUG("per_io_frame_descs=%p", ereq->per_io_frame_descs);
	DWC_DEBUG("tr_sub_flags=%d", ereq->tr_sub_flags);
	DWC_DEBUG("error_count=%d", ereq->error_count);
	DWC_DEBUG("pio_alloc_pkt_count=%d", ereq->pio_alloc_pkt_count);
	DWC_DEBUG("pio_pkt_count=%d", ereq->pio_pkt_count);
	DWC_DEBUG("res=%d", ereq->res);

	for (i = 0; i < ereq->pio_pkt_count; i++) {
		xfd = &ereq->per_io_frame_descs[0];
		DWC_DEBUG("FD #%d", i);

		DWC_DEBUG("xfd->actual_length=%d", xfd->actual_length);
		DWC_DEBUG("xfd->length=%d", xfd->length);
		DWC_DEBUG("xfd->offset=%d", xfd->offset);
		DWC_DEBUG("xfd->status=%d", xfd->status);
	}
}

/**
 *
 */
int dwc_otg_pcd_xiso_ep_queue(dwc_otg_pcd_t *pcd, void *ep_handle,
			      uint8_t *buf, dwc_dma_t dma_buf, uint32_t buflen,
			      int zero, void *req_handle, int atomic_alloc,
			      void *ereq_nonport)
{
	dwc_otg_pcd_request_t *req = NULL;
	dwc_otg_pcd_ep_t *ep;
	dwc_irqflags_t flags;
	int res;

	ep = get_ep_from_handle(pcd, ep_handle);
	if (!ep) {
		DWC_WARN("bad ep\n");
		return -DWC_E_INVALID;
	}

	/* We support this extension only for DDMA mode */
	if (ep->dwc_ep.type == DWC_OTG_EP_TYPE_ISOC)
		if (!GET_CORE_IF(pcd)->dma_desc_enable)
			return -DWC_E_INVALID;

	/* Create a dwc_otg_pcd_request_t object */
	if (atomic_alloc) {
		req = DWC_ALLOC_ATOMIC(sizeof(*req));
	} else {
		req = DWC_ALLOC(sizeof(*req));
	}

	if (!req) {
		return -DWC_E_NO_MEMORY;
	}

	/* Create the Isoc descs for this request which shall be the exact match
	 * of the structure sent to us from the non-portable logic */
	res =
	    dwc_otg_pcd_xiso_create_pkt_descs(req, ereq_nonport, atomic_alloc);
	if (res) {
		DWC_WARN("Failed to init the Isoc descriptors");
		DWC_FREE(req);
		return res;
	}

	DWC_SPINLOCK_IRQSAVE(pcd->lock, &flags);

	DWC_CIRCLEQ_INIT_ENTRY(req, queue_entry);
	req->buf = buf;
	req->dma = dma_buf;
	req->length = buflen;
	req->sent_zlp = zero;
	req->priv = req_handle;

	/* DWC_SPINLOCK_IRQSAVE(pcd->lock, &flags); */
	ep->dwc_ep.dma_addr = dma_buf;
	ep->dwc_ep.start_xfer_buff = buf;
	ep->dwc_ep.xfer_buff = buf;
	ep->dwc_ep.xfer_len = 0;
	ep->dwc_ep.xfer_count = 0;
	ep->dwc_ep.sent_zlp = 0;
	ep->dwc_ep.total_len = buflen;

	/* Add this request to the tail */
	DWC_CIRCLEQ_INSERT_TAIL(&ep->queue, req, queue_entry);
	ep->dwc_ep.xiso_queued_xfers++;

	/* DWC_DEBUG("CP_0"); */
	/* DWC_DEBUG("req->ext_req.tr_sub_flags=%d", req->ext_req.tr_sub_flags); */
	/* prn_ext_request((struct dwc_iso_xreq_port *) ereq_nonport); */
	/* prn_ext_request(&req->ext_req); */

	/* DWC_SPINUNLOCK_IRQRESTORE(pcd->lock, flags); */

	/* If the req->status == ASAP  then check if there is any active transfer
	 * for this endpoint. If no active transfers, then get the first entry
	 * from the queue and start that transfer
	 */
	if (req->ext_req.tr_sub_flags == DWC_EREQ_TF_ASAP) {
		res = dwc_otg_pcd_xiso_start_next_request(pcd, ep);
		if (res) {
			DWC_WARN("Failed to start the next Isoc transfer");
			DWC_SPINUNLOCK_IRQRESTORE(pcd->lock, flags);
			DWC_FREE(req);
			return res;
		}
	}

	DWC_SPINUNLOCK_IRQRESTORE(pcd->lock, flags);
	return 0;
}

#endif
/* END ifdef DWC_UTE_PER_IO ***************************************************/
int dwc_otg_pcd_ep_queue(dwc_otg_pcd_t *pcd, void *ep_handle,
			 uint8_t *buf, dwc_dma_t dma_buf, uint32_t buflen,
			 int zero, void *req_handle, int atomic_alloc)
{
	dwc_irqflags_t flags;
	dwc_otg_pcd_request_t *req;
	dwc_otg_pcd_ep_t *ep;
	uint32_t max_transfer;

	ep = get_ep_from_handle(pcd, ep_handle);
	if (!ep || (!ep->desc && ep->dwc_ep.num != 0)) {
		DWC_WARN("bad ep\n");
		return -DWC_E_INVALID;
	}

	if (atomic_alloc) {
		req = DWC_ALLOC_ATOMIC(sizeof(*req));
	} else {
		req = DWC_ALLOC(sizeof(*req));
	}

	if (!req) {
		return -DWC_E_NO_MEMORY;
	}
	DWC_CIRCLEQ_INIT_ENTRY(req, queue_entry);
	if (!GET_CORE_IF(pcd)->core_params->opt) {
		if (ep->dwc_ep.num != 0) {
			DWC_ERROR("queue req %p, len %d buf %p\n",
				  req_handle, buflen, buf);
		}
	}

	req->buf = buf;
	req->dma = dma_buf;
	req->length = buflen;
	req->sent_zlp = zero;
	req->priv = req_handle;
	req->dw_align_buf = NULL;
	if ((dma_buf & 0x3) && GET_CORE_IF(pcd)->dma_enable
	    && !GET_CORE_IF(pcd)->dma_desc_enable)
		req->dw_align_buf = DWC_DEV_DMA_ALLOC_ATOMIC(buflen,
							     &req->
							     dw_align_buf_dma);
	DWC_SPINLOCK_IRQSAVE(pcd->lock, &flags);

	/*
	 * After adding request to the queue for IN ISOC wait for In Token Received
	 * when TX FIFO is empty interrupt and for OUT ISOC wait for OUT Token
	 * Received when EP is disabled interrupt to obtain starting microframe
	 * (odd/even) start transfer
	 */
	if (ep->dwc_ep.type == DWC_OTG_EP_TYPE_ISOC) {
		if (req != 0) {
			depctl_data_t depctl = {.d32 =
				    DWC_READ_REG32(&pcd->core_if->
						   dev_if->in_ep_regs[ep->
								      dwc_ep.
								      num]->diepctl)
			};
			++pcd->request_pending;

			DWC_CIRCLEQ_INSERT_TAIL(&ep->queue, req, queue_entry);
			if (ep->dwc_ep.is_in) {
				depctl.b.cnak = 1;
				DWC_WRITE_REG32(&pcd->core_if->
						dev_if->in_ep_regs[ep->dwc_ep.
								   num]->diepctl,
						depctl.d32);
			}

			DWC_SPINUNLOCK_IRQRESTORE(pcd->lock, flags);
		}
		return 0;
	}

	/*
	 * For EP0 IN without premature status, zlp is required?
	 */
	if (ep->dwc_ep.num == 0 && ep->dwc_ep.is_in) {
		DWC_DEBUGPL(DBG_PCDV, "%d-OUT ZLP\n", ep->dwc_ep.num);
		/* _req->zero = 1; */
	}

	/* Start the transfer */
	if (DWC_CIRCLEQ_EMPTY(&ep->queue) && !ep->stopped) {
		/* EP0 Transfer? */
		if (ep->dwc_ep.num == 0) {
			switch (pcd->ep0state) {
			case EP0_IN_DATA_PHASE:
				DWC_DEBUGPL(DBG_PCD,
					    "%s ep0: EP0_IN_DATA_PHASE\n",
					    __func__);
				break;

			case EP0_OUT_DATA_PHASE:
				DWC_DEBUGPL(DBG_PCD,
					    "%s ep0: EP0_OUT_DATA_PHASE\n",
					    __func__);
				if (pcd->request_config) {
					/* Complete STATUS PHASE */
					ep->dwc_ep.is_in = 1;
					pcd->ep0state = EP0_IN_STATUS_PHASE;
				}
				break;

			case EP0_IN_STATUS_PHASE:
				DWC_DEBUGPL(DBG_PCD,
					    "%s ep0: EP0_IN_STATUS_PHASE\n",
					    __func__);
				break;

			default:
				DWC_DEBUGPL(DBG_ANY, "ep0: odd state %d\n",
					    pcd->ep0state);
				DWC_SPINUNLOCK_IRQRESTORE(pcd->lock, flags);
				return -DWC_E_SHUTDOWN;
			}

			ep->dwc_ep.dma_addr = dma_buf;
			ep->dwc_ep.start_xfer_buff = buf;
			ep->dwc_ep.xfer_buff = buf;
			ep->dwc_ep.xfer_len = buflen;
			ep->dwc_ep.xfer_count = 0;
			ep->dwc_ep.sent_zlp = 0;
			ep->dwc_ep.total_len = ep->dwc_ep.xfer_len;

			if (zero) {
				if ((ep->dwc_ep.xfer_len %
				     ep->dwc_ep.maxpacket == 0)
				    && (ep->dwc_ep.xfer_len != 0)) {
					ep->dwc_ep.sent_zlp = 1;
				}

			}

			dwc_otg_ep0_start_transfer(GET_CORE_IF(pcd),
						   &ep->dwc_ep);
		}		/* non-ep0 endpoints */
		else {
#ifdef DWC_UTE_CFI
			if (ep->dwc_ep.buff_mode != BM_STANDARD) {
				/* store the request length */
				ep->dwc_ep.cfi_req_len = buflen;
				pcd->cfi->ops.build_descriptors(pcd->cfi, pcd,
								ep, req);
			} else {
#endif
				max_transfer =
				    GET_CORE_IF(ep->pcd)->
				    core_params->max_transfer_size;

				/* Setup and start the Transfer */
				if (req->dw_align_buf) {
					if (ep->dwc_ep.is_in)
						dwc_memcpy(req->dw_align_buf,
							   buf, buflen);
					ep->dwc_ep.dma_addr =
					    req->dw_align_buf_dma;
					ep->dwc_ep.start_xfer_buff =
					    req->dw_align_buf;
					ep->dwc_ep.xfer_buff =
					    req->dw_align_buf;
				} else {
					ep->dwc_ep.dma_addr = dma_buf;
					ep->dwc_ep.start_xfer_buff = buf;
					ep->dwc_ep.xfer_buff = buf;
				}
				ep->dwc_ep.xfer_len = 0;
				ep->dwc_ep.xfer_count = 0;
				ep->dwc_ep.sent_zlp = 0;
				ep->dwc_ep.total_len = buflen;

				ep->dwc_ep.maxxfer = max_transfer;
				if (GET_CORE_IF(pcd)->dma_desc_enable) {
					uint32_t out_max_xfer =
					    DDMA_MAX_TRANSFER_SIZE -
					    (DDMA_MAX_TRANSFER_SIZE % 4);
					if (ep->dwc_ep.is_in) {
						if (ep->dwc_ep.maxxfer >
						    DDMA_MAX_TRANSFER_SIZE) {
							ep->dwc_ep.maxxfer =
							    DDMA_MAX_TRANSFER_SIZE;
						}
					} else {
						if (ep->dwc_ep.maxxfer >
						    out_max_xfer) {
							ep->dwc_ep.maxxfer =
							    out_max_xfer;
						}
					}
				}
				if (ep->dwc_ep.maxxfer < ep->dwc_ep.total_len) {
					ep->dwc_ep.maxxfer -=
					    (ep->dwc_ep.maxxfer %
					     ep->dwc_ep.maxpacket);
				}

				if (zero) {
					if ((ep->dwc_ep.total_len %
					     ep->dwc_ep.maxpacket == 0)
					    && (ep->dwc_ep.total_len != 0)) {
						ep->dwc_ep.sent_zlp = 1;
					}
				}
#ifdef DWC_UTE_CFI
			}
#endif
			dwc_otg_ep_start_transfer(GET_CORE_IF(pcd),
						  &ep->dwc_ep);
		}
	}

	if (req != 0) {
		++pcd->request_pending;
		DWC_CIRCLEQ_INSERT_TAIL(&ep->queue, req, queue_entry);
		if (ep->dwc_ep.is_in && ep->stopped
		    && !(GET_CORE_IF(pcd)->dma_enable)) {
			/** @todo NGS Create a function for this. */
			diepmsk_data_t diepmsk = {.d32 = 0 };
			diepmsk.b.intktxfemp = 1;
			if (GET_CORE_IF(pcd)->multiproc_int_enable) {
				DWC_MODIFY_REG32(&GET_CORE_IF(pcd)->dev_if->
						 dev_global_regs->
						 diepeachintmsk[ep->dwc_ep.num],
						 0, diepmsk.d32);
			} else {
				DWC_MODIFY_REG32(&GET_CORE_IF(pcd)->dev_if->
						 dev_global_regs->diepmsk, 0,
						 diepmsk.d32);
			}

		}
	}
	DWC_SPINUNLOCK_IRQRESTORE(pcd->lock, flags);

	return 0;
}

int dwc_otg_pcd_ep_dequeue(dwc_otg_pcd_t *pcd, void *ep_handle,
			   void *req_handle)
{
	dwc_irqflags_t flags;
	dwc_otg_pcd_request_t *req;
	dwc_otg_pcd_ep_t *ep;

	ep = get_ep_from_handle(pcd, ep_handle);
	if (!ep || (!ep->desc && ep->dwc_ep.num != 0)) {
		DWC_WARN("bad argument\n");
		return -DWC_E_INVALID;
	}

	DWC_SPINLOCK_IRQSAVE(pcd->lock, &flags);

	/* make sure it's actually queued on this endpoint */
	DWC_CIRCLEQ_FOREACH(req, &ep->queue, queue_entry) {
		if (req->priv == (void *)req_handle) {
			break;
		}
	}

	if (req->priv != (void *)req_handle) {
		DWC_SPINUNLOCK_IRQRESTORE(pcd->lock, flags);
		return -DWC_E_INVALID;
	}

	if (!DWC_CIRCLEQ_EMPTY_ENTRY(req, queue_entry)) {
		dwc_otg_pcd_ep_stop_transfer(GET_CORE_IF(pcd),
					     &ep->dwc_ep);
		/* Flush the Tx FIFO */
		if (ep->dwc_ep.is_in) {
			dwc_otg_flush_tx_fifo(GET_CORE_IF(pcd),
					      ep->dwc_ep.tx_fifo_num);
			release_perio_tx_fifo(GET_CORE_IF(pcd),
					      ep->dwc_ep.tx_fifo_num);
			release_tx_fifo(GET_CORE_IF(pcd),
					ep->dwc_ep.tx_fifo_num);
		}

		dwc_otg_request_done(ep, req, -DWC_E_RESTART);
	} else {
		req = NULL;
	}

	DWC_SPINUNLOCK_IRQRESTORE(pcd->lock, flags);

	return req ? 0 : -DWC_E_SHUTDOWN;

}

int dwc_otg_pcd_ep_halt(dwc_otg_pcd_t *pcd, void *ep_handle, int value)
{
	dwc_otg_pcd_ep_t *ep;
	dwc_irqflags_t flags;
	int retval = 0;

	ep = get_ep_from_handle(pcd, ep_handle);

	if (!ep || (!ep->desc && ep != &pcd->ep0) ||
	    (ep->desc && (ep->desc->bmAttributes == UE_ISOCHRONOUS))) {
		DWC_WARN("%s, bad ep\n", __func__);
		return -DWC_E_INVALID;
	}

	DWC_SPINLOCK_IRQSAVE(pcd->lock, &flags);
	if (!DWC_CIRCLEQ_EMPTY(&ep->queue)) {
		DWC_WARN("%d %s XFer In process\n", ep->dwc_ep.num,
			 ep->dwc_ep.is_in ? "IN" : "OUT");
		retval = -DWC_E_AGAIN;
	} else if (value == 0) {
		ep->dwc_ep.stall_clear_flag = 0;
		dwc_otg_ep_clear_stall(GET_CORE_IF(pcd), &ep->dwc_ep);
	} else if (value == 1) {
stall:
		if (ep->dwc_ep.is_in == 1 && GET_CORE_IF(pcd)->dma_desc_enable) {
			dtxfsts_data_t txstatus;
			fifosize_data_t txfifosize;

			txfifosize.d32 =
			    DWC_READ_REG32(&GET_CORE_IF(pcd)->core_global_regs->
					   dtxfsiz[ep->dwc_ep.tx_fifo_num]);
			txstatus.d32 =
			    DWC_READ_REG32(&GET_CORE_IF(pcd)->dev_if->
					   in_ep_regs[ep->dwc_ep.num]->dtxfsts);

			if (txstatus.b.txfspcavail < txfifosize.b.depth) {
				DWC_WARN("%s() Data In Tx Fifo\n", __func__);
				retval = -DWC_E_AGAIN;
			} else {
				if (ep->dwc_ep.num == 0) {
					pcd->ep0state = EP0_STALL;
				}

				ep->stopped = 1;
				dwc_otg_ep_set_stall(GET_CORE_IF(pcd),
						     &ep->dwc_ep);
			}
		} else {
			if (ep->dwc_ep.num == 0) {
				pcd->ep0state = EP0_STALL;
			}

			ep->stopped = 1;
			dwc_otg_ep_set_stall(GET_CORE_IF(pcd), &ep->dwc_ep);
		}
	} else if (value == 2) {
		ep->dwc_ep.stall_clear_flag = 0;
	} else if (value == 3) {
		ep->dwc_ep.stall_clear_flag = 1;
		goto stall;
	}

	DWC_SPINUNLOCK_IRQRESTORE(pcd->lock, flags);

	return retval;
}

/**
 * This function initiates remote wakeup of the host from suspend state.
 */
void dwc_otg_pcd_rem_wkup_from_suspend(dwc_otg_pcd_t *pcd, int set)
{
	dctl_data_t dctl = { 0 };
	dwc_otg_core_if_t *core_if = GET_CORE_IF(pcd);
	dsts_data_t dsts;

	dsts.d32 = DWC_READ_REG32(&core_if->dev_if->dev_global_regs->dsts);
	if (!dsts.b.suspsts) {
		DWC_WARN("Remote wakeup while is not in suspend state\n");
	}
	/* Check if DEVICE_REMOTE_WAKEUP feature enabled */
	if (pcd->remote_wakeup_enable) {
		if (set) {

			if (core_if->adp_enable) {
				gpwrdn_data_t gpwrdn;

				dwc_otg_adp_probe_stop(core_if);

				/* Mask SRP detected interrupt from Power Down Logic */
				gpwrdn.d32 = 0;
				gpwrdn.b.srp_det_msk = 1;
				DWC_MODIFY_REG32(&core_if->core_global_regs->
						 gpwrdn, gpwrdn.d32, 0);

				/* Disable Power Down Logic */
				gpwrdn.d32 = 0;
				gpwrdn.b.pmuactv = 1;
				DWC_MODIFY_REG32(&core_if->core_global_regs->
						 gpwrdn, gpwrdn.d32, 0);

				/*
				 * Initialize the Core for Device mode.
				 */
				core_if->op_state = B_PERIPHERAL;
				dwc_otg_core_init(core_if);
				dwc_otg_enable_global_interrupts(core_if);
				cil_pcd_start(core_if);

				dwc_otg_initiate_srp(core_if);
			}

			dctl.b.rmtwkupsig = 1;
			DWC_MODIFY_REG32(&core_if->dev_if->
					 dev_global_regs->dctl, 0, dctl.d32);
			DWC_DEBUGPL(DBG_PCD, "Set Remote Wakeup\n");

			dwc_mdelay(2);
			DWC_MODIFY_REG32(&core_if->dev_if->
					 dev_global_regs->dctl, dctl.d32, 0);
			DWC_DEBUGPL(DBG_PCD, "Clear Remote Wakeup\n");
		}
	} else {
		DWC_DEBUGPL(DBG_PCD, "Remote Wakeup is disabled\n");
	}
}

#ifdef CONFIG_USB_DWC_OTG_LPM
/**
 * This function initiates remote wakeup of the host from L1 sleep state.
 */
void dwc_otg_pcd_rem_wkup_from_sleep(dwc_otg_pcd_t *pcd, int set)
{
	glpmcfg_data_t lpmcfg;
	pcgcctl_data_t pcgcctl = {.d32 = 0 };

	dwc_otg_core_if_t *core_if = GET_CORE_IF(pcd);

	lpmcfg.d32 = DWC_READ_REG32(&core_if->core_global_regs->glpmcfg);

	/* Check if we are in L1 state */
	if (!lpmcfg.b.prt_sleep_sts) {
		DWC_DEBUGPL(DBG_PCD, "Device is not in sleep state\n");
		return;
	}

	/* Check if host allows remote wakeup */
	if (!lpmcfg.b.rem_wkup_en) {
		DWC_DEBUGPL(DBG_PCD, "Host does not allow remote wakeup\n");
		return;
	}

	/* Check if Resume OK */
	if (!lpmcfg.b.sleep_state_resumeok) {
		DWC_DEBUGPL(DBG_PCD, "Sleep state resume is not OK\n");
		return;
	}

	lpmcfg.d32 = DWC_READ_REG32(&core_if->core_global_regs->glpmcfg);
	lpmcfg.b.en_utmi_sleep = 0;
	lpmcfg.b.hird_thres &= (~(1 << 4));

	/* Clear Enbl_L1Gating bit. */
	pcgcctl.b.enbl_sleep_gating = 1;
	DWC_MODIFY_REG32(core_if->pcgcctl, pcgcctl.d32, 0);

	DWC_WRITE_REG32(&core_if->core_global_regs->glpmcfg, lpmcfg.d32);

	if (set) {
		dctl_data_t dctl = {.d32 = 0 };
		dctl.b.rmtwkupsig = 1;
		/* Set RmtWkUpSig bit to start remote wakup signaling.
		 * Hardware will automatically clear this bit.
		 */
		DWC_MODIFY_REG32(&core_if->dev_if->dev_global_regs->dctl,
				 0, dctl.d32);
		DWC_DEBUGPL(DBG_PCD, "Set Remote Wakeup\n");
	}

}
#endif

/**
 * Performs remote wakeup.
 */
void dwc_otg_pcd_remote_wakeup(dwc_otg_pcd_t *pcd, int set)
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(pcd);
	dwc_irqflags_t flags;
	if (dwc_otg_is_device_mode(core_if)) {
		DWC_SPINLOCK_IRQSAVE(pcd->lock, &flags);
#ifdef CONFIG_USB_DWC_OTG_LPM
		if (core_if->lx_state == DWC_OTG_L1) {
			dwc_otg_pcd_rem_wkup_from_sleep(pcd, set);
		} else {
#endif
			dwc_otg_pcd_rem_wkup_from_suspend(pcd, set);
#ifdef CONFIG_USB_DWC_OTG_LPM
		}
#endif
		DWC_SPINUNLOCK_IRQRESTORE(pcd->lock, flags);
	}
	return;
}

void dwc_otg_pcd_disconnect_us(dwc_otg_pcd_t *pcd, int no_of_usecs)
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(pcd);
	dctl_data_t dctl = { 0 };

	if (dwc_otg_is_device_mode(core_if)) {
		dctl.b.sftdiscon = 1;
		DWC_PRINTF("Soft disconnect for %d useconds\n", no_of_usecs);
		DWC_MODIFY_REG32(&core_if->dev_if->dev_global_regs->dctl, 0,
				 dctl.d32);
		dwc_udelay(no_of_usecs);
		DWC_MODIFY_REG32(&core_if->dev_if->dev_global_regs->dctl,
				 dctl.d32, 0);

	} else {
		DWC_PRINTF("NOT SUPPORTED IN HOST MODE\n");
	}
	return;

}

int dwc_otg_pcd_wakeup(dwc_otg_pcd_t *pcd)
{
	dsts_data_t dsts;
	gotgctl_data_t gotgctl;

	/*
	 * This function starts the Protocol if no session is in progress. If
	 * a session is already in progress, but the device is suspended,
	 * remote wakeup signaling is started.
	 */

	/* Check if valid session */
	gotgctl.d32 =
	    DWC_READ_REG32(&(GET_CORE_IF(pcd)->core_global_regs->gotgctl));
	if (gotgctl.b.bsesvld) {
		/* Check if suspend state */
		dsts.d32 =
		    DWC_READ_REG32(&
				   (GET_CORE_IF(pcd)->dev_if->dev_global_regs->
				    dsts));
		if (dsts.b.suspsts) {
			dwc_otg_pcd_remote_wakeup(pcd, 1);
		}
	} else {
		dwc_otg_pcd_initiate_srp(pcd);
	}

	return 0;

}

/**
 * Implement Soft-Connect and Soft-Disconnect function
 */

void dwc_otg_pcd_pullup_enable(dwc_otg_pcd_t *pcd)
{
	if (pcd)
	DWC_MODIFY_REG32(&(GET_CORE_IF(pcd)->dev_if->dev_global_regs->dctl), 2,
			 0);
}

void dwc_otg_pcd_pullup_disable(dwc_otg_pcd_t *pcd)
{
	if (pcd)
	DWC_MODIFY_REG32(&(GET_CORE_IF(pcd)->dev_if->dev_global_regs->dctl), 0,
			 2);
}

void dwc_pcd_reset(dwc_otg_pcd_t *pcd)
{
	dwc_otg_core_if_t *core_if = GET_CORE_IF(pcd);
	dwc_otg_disable_global_interrupts(core_if);
	dwc_otg_core_init(core_if);
	dwc_otg_pcd_reinit(pcd);
	dwc_otg_core_dev_init(core_if);
	dwc_otg_enable_global_interrupts(core_if);
}

/**
 * Start the SRP timer to detect when the SRP does not complete within
 * 6 seconds.
 *
 * @param pcd the pcd structure.
 */
void dwc_otg_pcd_initiate_srp(dwc_otg_pcd_t *pcd)
{
	dwc_irqflags_t flags;
	DWC_SPINLOCK_IRQSAVE(pcd->lock, &flags);
	dwc_otg_initiate_srp(GET_CORE_IF(pcd));
	DWC_SPINUNLOCK_IRQRESTORE(pcd->lock, flags);
}

int dwc_otg_pcd_get_frame_number(dwc_otg_pcd_t *pcd)
{
	return dwc_otg_get_frame_number(GET_CORE_IF(pcd));
}

int dwc_otg_pcd_is_lpm_enabled(dwc_otg_pcd_t *pcd)
{
	return GET_CORE_IF(pcd)->core_params->lpm_enable;
}

int dwc_otg_pcd_is_besl_enabled(dwc_otg_pcd_t *pcd)
{
	return GET_CORE_IF(pcd)->core_params->besl_enable;
}

int dwc_otg_pcd_get_param_baseline_besl(dwc_otg_pcd_t *pcd)
{
	return GET_CORE_IF(pcd)->core_params->baseline_besl;
}

int dwc_otg_pcd_get_param_deep_besl(dwc_otg_pcd_t *pcd)
{
	return GET_CORE_IF(pcd)->core_params->deep_besl;
}

uint32_t get_b_hnp_enable(dwc_otg_pcd_t *pcd)
{
	return pcd->b_hnp_enable;
}

uint32_t get_a_hnp_support(dwc_otg_pcd_t *pcd)
{
	return pcd->a_hnp_support;
}

uint32_t get_a_alt_hnp_support(dwc_otg_pcd_t *pcd)
{
	return pcd->a_alt_hnp_support;
}

int dwc_otg_pcd_get_rmwkup_enable(dwc_otg_pcd_t *pcd)
{
	return pcd->remote_wakeup_enable;
}

#endif /* DWC_HOST_ONLY */
