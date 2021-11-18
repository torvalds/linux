// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/*
 * hcd.c - DesignWare HS OTG Controller host-mode routines
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
 * This file contains the core HCD code, and implements the Linux hc_driver
 * API
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include <linux/usb/hcd.h>
#include <linux/usb/ch11.h>

#include "core.h"
#include "hcd.h"

/*
 * =========================================================================
 *  Host Core Layer Functions
 * =========================================================================
 */

/**
 * dwc2_enable_common_interrupts() - Initializes the commmon interrupts,
 * used in both device and host modes
 *
 * @hsotg: Programming view of the DWC_otg controller
 */
static void dwc2_enable_common_interrupts(struct dwc2_hsotg *hsotg)
{
	u32 intmsk;

	/* Clear any pending OTG Interrupts */
	dwc2_writel(hsotg, 0xffffffff, GOTGINT);

	/* Clear any pending interrupts */
	dwc2_writel(hsotg, 0xffffffff, GINTSTS);

	/* Enable the interrupts in the GINTMSK */
	intmsk = GINTSTS_MODEMIS | GINTSTS_OTGINT;

	if (!hsotg->params.host_dma)
		intmsk |= GINTSTS_RXFLVL;
	if (!hsotg->params.external_id_pin_ctl)
		intmsk |= GINTSTS_CONIDSTSCHNG;

	intmsk |= GINTSTS_WKUPINT | GINTSTS_USBSUSP |
		  GINTSTS_SESSREQINT;

	if (dwc2_is_device_mode(hsotg) && hsotg->params.lpm)
		intmsk |= GINTSTS_LPMTRANRCVD;

	dwc2_writel(hsotg, intmsk, GINTMSK);
}

static int dwc2_gahbcfg_init(struct dwc2_hsotg *hsotg)
{
	u32 ahbcfg = dwc2_readl(hsotg, GAHBCFG);

	switch (hsotg->hw_params.arch) {
	case GHWCFG2_EXT_DMA_ARCH:
		dev_err(hsotg->dev, "External DMA Mode not supported\n");
		return -EINVAL;

	case GHWCFG2_INT_DMA_ARCH:
		dev_dbg(hsotg->dev, "Internal DMA Mode\n");
		if (hsotg->params.ahbcfg != -1) {
			ahbcfg &= GAHBCFG_CTRL_MASK;
			ahbcfg |= hsotg->params.ahbcfg &
				  ~GAHBCFG_CTRL_MASK;
		}
		break;

	case GHWCFG2_SLAVE_ONLY_ARCH:
	default:
		dev_dbg(hsotg->dev, "Slave Only Mode\n");
		break;
	}

	if (hsotg->params.host_dma)
		ahbcfg |= GAHBCFG_DMA_EN;
	else
		hsotg->params.dma_desc_enable = false;

	dwc2_writel(hsotg, ahbcfg, GAHBCFG);

	return 0;
}

static void dwc2_gusbcfg_init(struct dwc2_hsotg *hsotg)
{
	u32 usbcfg;

	usbcfg = dwc2_readl(hsotg, GUSBCFG);
	usbcfg &= ~(GUSBCFG_HNPCAP | GUSBCFG_SRPCAP);

	switch (hsotg->hw_params.op_mode) {
	case GHWCFG2_OP_MODE_HNP_SRP_CAPABLE:
		if (hsotg->params.otg_caps.hnp_support &&
		    hsotg->params.otg_caps.srp_support)
			usbcfg |= GUSBCFG_HNPCAP;
		fallthrough;

	case GHWCFG2_OP_MODE_SRP_ONLY_CAPABLE:
	case GHWCFG2_OP_MODE_SRP_CAPABLE_DEVICE:
	case GHWCFG2_OP_MODE_SRP_CAPABLE_HOST:
		if (hsotg->params.otg_caps.srp_support)
			usbcfg |= GUSBCFG_SRPCAP;
		break;

	case GHWCFG2_OP_MODE_NO_HNP_SRP_CAPABLE:
	case GHWCFG2_OP_MODE_NO_SRP_CAPABLE_DEVICE:
	case GHWCFG2_OP_MODE_NO_SRP_CAPABLE_HOST:
	default:
		break;
	}

	dwc2_writel(hsotg, usbcfg, GUSBCFG);
}

static int dwc2_vbus_supply_init(struct dwc2_hsotg *hsotg)
{
	if (hsotg->vbus_supply)
		return regulator_enable(hsotg->vbus_supply);

	return 0;
}

static int dwc2_vbus_supply_exit(struct dwc2_hsotg *hsotg)
{
	if (hsotg->vbus_supply)
		return regulator_disable(hsotg->vbus_supply);

	return 0;
}

/**
 * dwc2_enable_host_interrupts() - Enables the Host mode interrupts
 *
 * @hsotg: Programming view of DWC_otg controller
 */
static void dwc2_enable_host_interrupts(struct dwc2_hsotg *hsotg)
{
	u32 intmsk;

	dev_dbg(hsotg->dev, "%s()\n", __func__);

	/* Disable all interrupts */
	dwc2_writel(hsotg, 0, GINTMSK);
	dwc2_writel(hsotg, 0, HAINTMSK);

	/* Enable the common interrupts */
	dwc2_enable_common_interrupts(hsotg);

	/* Enable host mode interrupts without disturbing common interrupts */
	intmsk = dwc2_readl(hsotg, GINTMSK);
	intmsk |= GINTSTS_DISCONNINT | GINTSTS_PRTINT | GINTSTS_HCHINT;
	dwc2_writel(hsotg, intmsk, GINTMSK);
}

/**
 * dwc2_disable_host_interrupts() - Disables the Host Mode interrupts
 *
 * @hsotg: Programming view of DWC_otg controller
 */
static void dwc2_disable_host_interrupts(struct dwc2_hsotg *hsotg)
{
	u32 intmsk = dwc2_readl(hsotg, GINTMSK);

	/* Disable host mode interrupts without disturbing common interrupts */
	intmsk &= ~(GINTSTS_SOF | GINTSTS_PRTINT | GINTSTS_HCHINT |
		    GINTSTS_PTXFEMP | GINTSTS_NPTXFEMP | GINTSTS_DISCONNINT);
	dwc2_writel(hsotg, intmsk, GINTMSK);
}

/*
 * dwc2_calculate_dynamic_fifo() - Calculates the default fifo size
 * For system that have a total fifo depth that is smaller than the default
 * RX + TX fifo size.
 *
 * @hsotg: Programming view of DWC_otg controller
 */
static void dwc2_calculate_dynamic_fifo(struct dwc2_hsotg *hsotg)
{
	struct dwc2_core_params *params = &hsotg->params;
	struct dwc2_hw_params *hw = &hsotg->hw_params;
	u32 rxfsiz, nptxfsiz, ptxfsiz, total_fifo_size;

	total_fifo_size = hw->total_fifo_size;
	rxfsiz = params->host_rx_fifo_size;
	nptxfsiz = params->host_nperio_tx_fifo_size;
	ptxfsiz = params->host_perio_tx_fifo_size;

	/*
	 * Will use Method 2 defined in the DWC2 spec: minimum FIFO depth
	 * allocation with support for high bandwidth endpoints. Synopsys
	 * defines MPS(Max Packet size) for a periodic EP=1024, and for
	 * non-periodic as 512.
	 */
	if (total_fifo_size < (rxfsiz + nptxfsiz + ptxfsiz)) {
		/*
		 * For Buffer DMA mode/Scatter Gather DMA mode
		 * 2 * ((Largest Packet size / 4) + 1 + 1) + n
		 * with n = number of host channel.
		 * 2 * ((1024/4) + 2) = 516
		 */
		rxfsiz = 516 + hw->host_channels;

		/*
		 * min non-periodic tx fifo depth
		 * 2 * (largest non-periodic USB packet used / 4)
		 * 2 * (512/4) = 256
		 */
		nptxfsiz = 256;

		/*
		 * min periodic tx fifo depth
		 * (largest packet size*MC)/4
		 * (1024 * 3)/4 = 768
		 */
		ptxfsiz = 768;

		params->host_rx_fifo_size = rxfsiz;
		params->host_nperio_tx_fifo_size = nptxfsiz;
		params->host_perio_tx_fifo_size = ptxfsiz;
	}

	/*
	 * If the summation of RX, NPTX and PTX fifo sizes is still
	 * bigger than the total_fifo_size, then we have a problem.
	 *
	 * We won't be able to allocate as many endpoints. Right now,
	 * we're just printing an error message, but ideally this FIFO
	 * allocation algorithm would be improved in the future.
	 *
	 * FIXME improve this FIFO allocation algorithm.
	 */
	if (unlikely(total_fifo_size < (rxfsiz + nptxfsiz + ptxfsiz)))
		dev_err(hsotg->dev, "invalid fifo sizes\n");
}

static void dwc2_config_fifos(struct dwc2_hsotg *hsotg)
{
	struct dwc2_core_params *params = &hsotg->params;
	u32 nptxfsiz, hptxfsiz, dfifocfg, grxfsiz;

	if (!params->enable_dynamic_fifo)
		return;

	dwc2_calculate_dynamic_fifo(hsotg);

	/* Rx FIFO */
	grxfsiz = dwc2_readl(hsotg, GRXFSIZ);
	dev_dbg(hsotg->dev, "initial grxfsiz=%08x\n", grxfsiz);
	grxfsiz &= ~GRXFSIZ_DEPTH_MASK;
	grxfsiz |= params->host_rx_fifo_size <<
		   GRXFSIZ_DEPTH_SHIFT & GRXFSIZ_DEPTH_MASK;
	dwc2_writel(hsotg, grxfsiz, GRXFSIZ);
	dev_dbg(hsotg->dev, "new grxfsiz=%08x\n",
		dwc2_readl(hsotg, GRXFSIZ));

	/* Non-periodic Tx FIFO */
	dev_dbg(hsotg->dev, "initial gnptxfsiz=%08x\n",
		dwc2_readl(hsotg, GNPTXFSIZ));
	nptxfsiz = params->host_nperio_tx_fifo_size <<
		   FIFOSIZE_DEPTH_SHIFT & FIFOSIZE_DEPTH_MASK;
	nptxfsiz |= params->host_rx_fifo_size <<
		    FIFOSIZE_STARTADDR_SHIFT & FIFOSIZE_STARTADDR_MASK;
	dwc2_writel(hsotg, nptxfsiz, GNPTXFSIZ);
	dev_dbg(hsotg->dev, "new gnptxfsiz=%08x\n",
		dwc2_readl(hsotg, GNPTXFSIZ));

	/* Periodic Tx FIFO */
	dev_dbg(hsotg->dev, "initial hptxfsiz=%08x\n",
		dwc2_readl(hsotg, HPTXFSIZ));
	hptxfsiz = params->host_perio_tx_fifo_size <<
		   FIFOSIZE_DEPTH_SHIFT & FIFOSIZE_DEPTH_MASK;
	hptxfsiz |= (params->host_rx_fifo_size +
		     params->host_nperio_tx_fifo_size) <<
		    FIFOSIZE_STARTADDR_SHIFT & FIFOSIZE_STARTADDR_MASK;
	dwc2_writel(hsotg, hptxfsiz, HPTXFSIZ);
	dev_dbg(hsotg->dev, "new hptxfsiz=%08x\n",
		dwc2_readl(hsotg, HPTXFSIZ));

	if (hsotg->params.en_multiple_tx_fifo &&
	    hsotg->hw_params.snpsid >= DWC2_CORE_REV_2_91a) {
		/*
		 * This feature was implemented in 2.91a version
		 * Global DFIFOCFG calculation for Host mode -
		 * include RxFIFO, NPTXFIFO and HPTXFIFO
		 */
		dfifocfg = dwc2_readl(hsotg, GDFIFOCFG);
		dfifocfg &= ~GDFIFOCFG_EPINFOBASE_MASK;
		dfifocfg |= (params->host_rx_fifo_size +
			     params->host_nperio_tx_fifo_size +
			     params->host_perio_tx_fifo_size) <<
			    GDFIFOCFG_EPINFOBASE_SHIFT &
			    GDFIFOCFG_EPINFOBASE_MASK;
		dwc2_writel(hsotg, dfifocfg, GDFIFOCFG);
	}
}

/**
 * dwc2_calc_frame_interval() - Calculates the correct frame Interval value for
 * the HFIR register according to PHY type and speed
 *
 * @hsotg: Programming view of DWC_otg controller
 *
 * NOTE: The caller can modify the value of the HFIR register only after the
 * Port Enable bit of the Host Port Control and Status register (HPRT.EnaPort)
 * has been set
 */
u32 dwc2_calc_frame_interval(struct dwc2_hsotg *hsotg)
{
	u32 usbcfg;
	u32 hprt0;
	int clock = 60;	/* default value */

	usbcfg = dwc2_readl(hsotg, GUSBCFG);
	hprt0 = dwc2_readl(hsotg, HPRT0);

	if (!(usbcfg & GUSBCFG_PHYSEL) && (usbcfg & GUSBCFG_ULPI_UTMI_SEL) &&
	    !(usbcfg & GUSBCFG_PHYIF16))
		clock = 60;
	if ((usbcfg & GUSBCFG_PHYSEL) && hsotg->hw_params.fs_phy_type ==
	    GHWCFG2_FS_PHY_TYPE_SHARED_ULPI)
		clock = 48;
	if (!(usbcfg & GUSBCFG_PHY_LP_CLK_SEL) && !(usbcfg & GUSBCFG_PHYSEL) &&
	    !(usbcfg & GUSBCFG_ULPI_UTMI_SEL) && (usbcfg & GUSBCFG_PHYIF16))
		clock = 30;
	if (!(usbcfg & GUSBCFG_PHY_LP_CLK_SEL) && !(usbcfg & GUSBCFG_PHYSEL) &&
	    !(usbcfg & GUSBCFG_ULPI_UTMI_SEL) && !(usbcfg & GUSBCFG_PHYIF16))
		clock = 60;
	if ((usbcfg & GUSBCFG_PHY_LP_CLK_SEL) && !(usbcfg & GUSBCFG_PHYSEL) &&
	    !(usbcfg & GUSBCFG_ULPI_UTMI_SEL) && (usbcfg & GUSBCFG_PHYIF16))
		clock = 48;
	if ((usbcfg & GUSBCFG_PHYSEL) && !(usbcfg & GUSBCFG_PHYIF16) &&
	    hsotg->hw_params.fs_phy_type == GHWCFG2_FS_PHY_TYPE_SHARED_UTMI)
		clock = 48;
	if ((usbcfg & GUSBCFG_PHYSEL) &&
	    hsotg->hw_params.fs_phy_type == GHWCFG2_FS_PHY_TYPE_DEDICATED)
		clock = 48;

	if ((hprt0 & HPRT0_SPD_MASK) >> HPRT0_SPD_SHIFT == HPRT0_SPD_HIGH_SPEED)
		/* High speed case */
		return 125 * clock - 1;

	/* FS/LS case */
	return 1000 * clock - 1;
}

/**
 * dwc2_read_packet() - Reads a packet from the Rx FIFO into the destination
 * buffer
 *
 * @hsotg: Programming view of DWC_otg controller
 * @dest:    Destination buffer for the packet
 * @bytes:   Number of bytes to copy to the destination
 */
void dwc2_read_packet(struct dwc2_hsotg *hsotg, u8 *dest, u16 bytes)
{
	u32 *data_buf = (u32 *)dest;
	int word_count = (bytes + 3) / 4;
	int i;

	/*
	 * Todo: Account for the case where dest is not dword aligned. This
	 * requires reading data from the FIFO into a u32 temp buffer, then
	 * moving it into the data buffer.
	 */

	dev_vdbg(hsotg->dev, "%s(%p,%p,%d)\n", __func__, hsotg, dest, bytes);

	for (i = 0; i < word_count; i++, data_buf++)
		*data_buf = dwc2_readl(hsotg, HCFIFO(0));
}

/**
 * dwc2_dump_channel_info() - Prints the state of a host channel
 *
 * @hsotg: Programming view of DWC_otg controller
 * @chan:  Pointer to the channel to dump
 *
 * Must be called with interrupt disabled and spinlock held
 *
 * NOTE: This function will be removed once the peripheral controller code
 * is integrated and the driver is stable
 */
static void dwc2_dump_channel_info(struct dwc2_hsotg *hsotg,
				   struct dwc2_host_chan *chan)
{
#ifdef VERBOSE_DEBUG
	int num_channels = hsotg->params.host_channels;
	struct dwc2_qh *qh;
	u32 hcchar;
	u32 hcsplt;
	u32 hctsiz;
	u32 hc_dma;
	int i;

	if (!chan)
		return;

	hcchar = dwc2_readl(hsotg, HCCHAR(chan->hc_num));
	hcsplt = dwc2_readl(hsotg, HCSPLT(chan->hc_num));
	hctsiz = dwc2_readl(hsotg, HCTSIZ(chan->hc_num));
	hc_dma = dwc2_readl(hsotg, HCDMA(chan->hc_num));

	dev_dbg(hsotg->dev, "  Assigned to channel %p:\n", chan);
	dev_dbg(hsotg->dev, "    hcchar 0x%08x, hcsplt 0x%08x\n",
		hcchar, hcsplt);
	dev_dbg(hsotg->dev, "    hctsiz 0x%08x, hc_dma 0x%08x\n",
		hctsiz, hc_dma);
	dev_dbg(hsotg->dev, "    dev_addr: %d, ep_num: %d, ep_is_in: %d\n",
		chan->dev_addr, chan->ep_num, chan->ep_is_in);
	dev_dbg(hsotg->dev, "    ep_type: %d\n", chan->ep_type);
	dev_dbg(hsotg->dev, "    max_packet: %d\n", chan->max_packet);
	dev_dbg(hsotg->dev, "    data_pid_start: %d\n", chan->data_pid_start);
	dev_dbg(hsotg->dev, "    xfer_started: %d\n", chan->xfer_started);
	dev_dbg(hsotg->dev, "    halt_status: %d\n", chan->halt_status);
	dev_dbg(hsotg->dev, "    xfer_buf: %p\n", chan->xfer_buf);
	dev_dbg(hsotg->dev, "    xfer_dma: %08lx\n",
		(unsigned long)chan->xfer_dma);
	dev_dbg(hsotg->dev, "    xfer_len: %d\n", chan->xfer_len);
	dev_dbg(hsotg->dev, "    qh: %p\n", chan->qh);
	dev_dbg(hsotg->dev, "  NP inactive sched:\n");
	list_for_each_entry(qh, &hsotg->non_periodic_sched_inactive,
			    qh_list_entry)
		dev_dbg(hsotg->dev, "    %p\n", qh);
	dev_dbg(hsotg->dev, "  NP waiting sched:\n");
	list_for_each_entry(qh, &hsotg->non_periodic_sched_waiting,
			    qh_list_entry)
		dev_dbg(hsotg->dev, "    %p\n", qh);
	dev_dbg(hsotg->dev, "  NP active sched:\n");
	list_for_each_entry(qh, &hsotg->non_periodic_sched_active,
			    qh_list_entry)
		dev_dbg(hsotg->dev, "    %p\n", qh);
	dev_dbg(hsotg->dev, "  Channels:\n");
	for (i = 0; i < num_channels; i++) {
		struct dwc2_host_chan *chan = hsotg->hc_ptr_array[i];

		dev_dbg(hsotg->dev, "    %2d: %p\n", i, chan);
	}
#endif /* VERBOSE_DEBUG */
}

static int _dwc2_hcd_start(struct usb_hcd *hcd);

static void dwc2_host_start(struct dwc2_hsotg *hsotg)
{
	struct usb_hcd *hcd = dwc2_hsotg_to_hcd(hsotg);

	hcd->self.is_b_host = dwc2_hcd_is_b_host(hsotg);
	_dwc2_hcd_start(hcd);
}

static void dwc2_host_disconnect(struct dwc2_hsotg *hsotg)
{
	struct usb_hcd *hcd = dwc2_hsotg_to_hcd(hsotg);

	hcd->self.is_b_host = 0;
}

static void dwc2_host_hub_info(struct dwc2_hsotg *hsotg, void *context,
			       int *hub_addr, int *hub_port)
{
	struct urb *urb = context;

	if (urb->dev->tt)
		*hub_addr = urb->dev->tt->hub->devnum;
	else
		*hub_addr = 0;
	*hub_port = urb->dev->ttport;
}

/*
 * =========================================================================
 *  Low Level Host Channel Access Functions
 * =========================================================================
 */

static void dwc2_hc_enable_slave_ints(struct dwc2_hsotg *hsotg,
				      struct dwc2_host_chan *chan)
{
	u32 hcintmsk = HCINTMSK_CHHLTD;

	switch (chan->ep_type) {
	case USB_ENDPOINT_XFER_CONTROL:
	case USB_ENDPOINT_XFER_BULK:
		dev_vdbg(hsotg->dev, "control/bulk\n");
		hcintmsk |= HCINTMSK_XFERCOMPL;
		hcintmsk |= HCINTMSK_STALL;
		hcintmsk |= HCINTMSK_XACTERR;
		hcintmsk |= HCINTMSK_DATATGLERR;
		if (chan->ep_is_in) {
			hcintmsk |= HCINTMSK_BBLERR;
		} else {
			hcintmsk |= HCINTMSK_NAK;
			hcintmsk |= HCINTMSK_NYET;
			if (chan->do_ping)
				hcintmsk |= HCINTMSK_ACK;
		}

		if (chan->do_split) {
			hcintmsk |= HCINTMSK_NAK;
			if (chan->complete_split)
				hcintmsk |= HCINTMSK_NYET;
			else
				hcintmsk |= HCINTMSK_ACK;
		}

		if (chan->error_state)
			hcintmsk |= HCINTMSK_ACK;
		break;

	case USB_ENDPOINT_XFER_INT:
		if (dbg_perio())
			dev_vdbg(hsotg->dev, "intr\n");
		hcintmsk |= HCINTMSK_XFERCOMPL;
		hcintmsk |= HCINTMSK_NAK;
		hcintmsk |= HCINTMSK_STALL;
		hcintmsk |= HCINTMSK_XACTERR;
		hcintmsk |= HCINTMSK_DATATGLERR;
		hcintmsk |= HCINTMSK_FRMOVRUN;

		if (chan->ep_is_in)
			hcintmsk |= HCINTMSK_BBLERR;
		if (chan->error_state)
			hcintmsk |= HCINTMSK_ACK;
		if (chan->do_split) {
			if (chan->complete_split)
				hcintmsk |= HCINTMSK_NYET;
			else
				hcintmsk |= HCINTMSK_ACK;
		}
		break;

	case USB_ENDPOINT_XFER_ISOC:
		if (dbg_perio())
			dev_vdbg(hsotg->dev, "isoc\n");
		hcintmsk |= HCINTMSK_XFERCOMPL;
		hcintmsk |= HCINTMSK_FRMOVRUN;
		hcintmsk |= HCINTMSK_ACK;

		if (chan->ep_is_in) {
			hcintmsk |= HCINTMSK_XACTERR;
			hcintmsk |= HCINTMSK_BBLERR;
		}
		break;
	default:
		dev_err(hsotg->dev, "## Unknown EP type ##\n");
		break;
	}

	dwc2_writel(hsotg, hcintmsk, HCINTMSK(chan->hc_num));
	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "set HCINTMSK to %08x\n", hcintmsk);
}

static void dwc2_hc_enable_dma_ints(struct dwc2_hsotg *hsotg,
				    struct dwc2_host_chan *chan)
{
	u32 hcintmsk = HCINTMSK_CHHLTD;

	/*
	 * For Descriptor DMA mode core halts the channel on AHB error.
	 * Interrupt is not required.
	 */
	if (!hsotg->params.dma_desc_enable) {
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "desc DMA disabled\n");
		hcintmsk |= HCINTMSK_AHBERR;
	} else {
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "desc DMA enabled\n");
		if (chan->ep_type == USB_ENDPOINT_XFER_ISOC)
			hcintmsk |= HCINTMSK_XFERCOMPL;
	}

	if (chan->error_state && !chan->do_split &&
	    chan->ep_type != USB_ENDPOINT_XFER_ISOC) {
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "setting ACK\n");
		hcintmsk |= HCINTMSK_ACK;
		if (chan->ep_is_in) {
			hcintmsk |= HCINTMSK_DATATGLERR;
			if (chan->ep_type != USB_ENDPOINT_XFER_INT)
				hcintmsk |= HCINTMSK_NAK;
		}
	}

	dwc2_writel(hsotg, hcintmsk, HCINTMSK(chan->hc_num));
	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "set HCINTMSK to %08x\n", hcintmsk);
}

static void dwc2_hc_enable_ints(struct dwc2_hsotg *hsotg,
				struct dwc2_host_chan *chan)
{
	u32 intmsk;

	if (hsotg->params.host_dma) {
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "DMA enabled\n");
		dwc2_hc_enable_dma_ints(hsotg, chan);
	} else {
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "DMA disabled\n");
		dwc2_hc_enable_slave_ints(hsotg, chan);
	}

	/* Enable the top level host channel interrupt */
	intmsk = dwc2_readl(hsotg, HAINTMSK);
	intmsk |= 1 << chan->hc_num;
	dwc2_writel(hsotg, intmsk, HAINTMSK);
	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "set HAINTMSK to %08x\n", intmsk);

	/* Make sure host channel interrupts are enabled */
	intmsk = dwc2_readl(hsotg, GINTMSK);
	intmsk |= GINTSTS_HCHINT;
	dwc2_writel(hsotg, intmsk, GINTMSK);
	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "set GINTMSK to %08x\n", intmsk);
}

/**
 * dwc2_hc_init() - Prepares a host channel for transferring packets to/from
 * a specific endpoint
 *
 * @hsotg: Programming view of DWC_otg controller
 * @chan:  Information needed to initialize the host channel
 *
 * The HCCHARn register is set up with the characteristics specified in chan.
 * Host channel interrupts that may need to be serviced while this transfer is
 * in progress are enabled.
 */
static void dwc2_hc_init(struct dwc2_hsotg *hsotg, struct dwc2_host_chan *chan)
{
	u8 hc_num = chan->hc_num;
	u32 hcintmsk;
	u32 hcchar;
	u32 hcsplt = 0;

	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "%s()\n", __func__);

	/* Clear old interrupt conditions for this host channel */
	hcintmsk = 0xffffffff;
	hcintmsk &= ~HCINTMSK_RESERVED14_31;
	dwc2_writel(hsotg, hcintmsk, HCINT(hc_num));

	/* Enable channel interrupts required for this transfer */
	dwc2_hc_enable_ints(hsotg, chan);

	/*
	 * Program the HCCHARn register with the endpoint characteristics for
	 * the current transfer
	 */
	hcchar = chan->dev_addr << HCCHAR_DEVADDR_SHIFT & HCCHAR_DEVADDR_MASK;
	hcchar |= chan->ep_num << HCCHAR_EPNUM_SHIFT & HCCHAR_EPNUM_MASK;
	if (chan->ep_is_in)
		hcchar |= HCCHAR_EPDIR;
	if (chan->speed == USB_SPEED_LOW)
		hcchar |= HCCHAR_LSPDDEV;
	hcchar |= chan->ep_type << HCCHAR_EPTYPE_SHIFT & HCCHAR_EPTYPE_MASK;
	hcchar |= chan->max_packet << HCCHAR_MPS_SHIFT & HCCHAR_MPS_MASK;
	dwc2_writel(hsotg, hcchar, HCCHAR(hc_num));
	if (dbg_hc(chan)) {
		dev_vdbg(hsotg->dev, "set HCCHAR(%d) to %08x\n",
			 hc_num, hcchar);

		dev_vdbg(hsotg->dev, "%s: Channel %d\n",
			 __func__, hc_num);
		dev_vdbg(hsotg->dev, "	 Dev Addr: %d\n",
			 chan->dev_addr);
		dev_vdbg(hsotg->dev, "	 Ep Num: %d\n",
			 chan->ep_num);
		dev_vdbg(hsotg->dev, "	 Is In: %d\n",
			 chan->ep_is_in);
		dev_vdbg(hsotg->dev, "	 Is Low Speed: %d\n",
			 chan->speed == USB_SPEED_LOW);
		dev_vdbg(hsotg->dev, "	 Ep Type: %d\n",
			 chan->ep_type);
		dev_vdbg(hsotg->dev, "	 Max Pkt: %d\n",
			 chan->max_packet);
	}

	/* Program the HCSPLT register for SPLITs */
	if (chan->do_split) {
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev,
				 "Programming HC %d with split --> %s\n",
				 hc_num,
				 chan->complete_split ? "CSPLIT" : "SSPLIT");
		if (chan->complete_split)
			hcsplt |= HCSPLT_COMPSPLT;
		hcsplt |= chan->xact_pos << HCSPLT_XACTPOS_SHIFT &
			  HCSPLT_XACTPOS_MASK;
		hcsplt |= chan->hub_addr << HCSPLT_HUBADDR_SHIFT &
			  HCSPLT_HUBADDR_MASK;
		hcsplt |= chan->hub_port << HCSPLT_PRTADDR_SHIFT &
			  HCSPLT_PRTADDR_MASK;
		if (dbg_hc(chan)) {
			dev_vdbg(hsotg->dev, "	  comp split %d\n",
				 chan->complete_split);
			dev_vdbg(hsotg->dev, "	  xact pos %d\n",
				 chan->xact_pos);
			dev_vdbg(hsotg->dev, "	  hub addr %d\n",
				 chan->hub_addr);
			dev_vdbg(hsotg->dev, "	  hub port %d\n",
				 chan->hub_port);
			dev_vdbg(hsotg->dev, "	  is_in %d\n",
				 chan->ep_is_in);
			dev_vdbg(hsotg->dev, "	  Max Pkt %d\n",
				 chan->max_packet);
			dev_vdbg(hsotg->dev, "	  xferlen %d\n",
				 chan->xfer_len);
		}
	}

	dwc2_writel(hsotg, hcsplt, HCSPLT(hc_num));
}

/**
 * dwc2_hc_halt() - Attempts to halt a host channel
 *
 * @hsotg:       Controller register interface
 * @chan:        Host channel to halt
 * @halt_status: Reason for halting the channel
 *
 * This function should only be called in Slave mode or to abort a transfer in
 * either Slave mode or DMA mode. Under normal circumstances in DMA mode, the
 * controller halts the channel when the transfer is complete or a condition
 * occurs that requires application intervention.
 *
 * In slave mode, checks for a free request queue entry, then sets the Channel
 * Enable and Channel Disable bits of the Host Channel Characteristics
 * register of the specified channel to intiate the halt. If there is no free
 * request queue entry, sets only the Channel Disable bit of the HCCHARn
 * register to flush requests for this channel. In the latter case, sets a
 * flag to indicate that the host channel needs to be halted when a request
 * queue slot is open.
 *
 * In DMA mode, always sets the Channel Enable and Channel Disable bits of the
 * HCCHARn register. The controller ensures there is space in the request
 * queue before submitting the halt request.
 *
 * Some time may elapse before the core flushes any posted requests for this
 * host channel and halts. The Channel Halted interrupt handler completes the
 * deactivation of the host channel.
 */
void dwc2_hc_halt(struct dwc2_hsotg *hsotg, struct dwc2_host_chan *chan,
		  enum dwc2_halt_status halt_status)
{
	u32 nptxsts, hptxsts, hcchar;

	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "%s()\n", __func__);

	/*
	 * In buffer DMA or external DMA mode channel can't be halted
	 * for non-split periodic channels. At the end of the next
	 * uframe/frame (in the worst case), the core generates a channel
	 * halted and disables the channel automatically.
	 */
	if ((hsotg->params.g_dma && !hsotg->params.g_dma_desc) ||
	    hsotg->hw_params.arch == GHWCFG2_EXT_DMA_ARCH) {
		if (!chan->do_split &&
		    (chan->ep_type == USB_ENDPOINT_XFER_ISOC ||
		     chan->ep_type == USB_ENDPOINT_XFER_INT)) {
			dev_err(hsotg->dev, "%s() Channel can't be halted\n",
				__func__);
			return;
		}
	}

	if (halt_status == DWC2_HC_XFER_NO_HALT_STATUS)
		dev_err(hsotg->dev, "!!! halt_status = %d !!!\n", halt_status);

	if (halt_status == DWC2_HC_XFER_URB_DEQUEUE ||
	    halt_status == DWC2_HC_XFER_AHB_ERR) {
		/*
		 * Disable all channel interrupts except Ch Halted. The QTD
		 * and QH state associated with this transfer has been cleared
		 * (in the case of URB_DEQUEUE), so the channel needs to be
		 * shut down carefully to prevent crashes.
		 */
		u32 hcintmsk = HCINTMSK_CHHLTD;

		dev_vdbg(hsotg->dev, "dequeue/error\n");
		dwc2_writel(hsotg, hcintmsk, HCINTMSK(chan->hc_num));

		/*
		 * Make sure no other interrupts besides halt are currently
		 * pending. Handling another interrupt could cause a crash due
		 * to the QTD and QH state.
		 */
		dwc2_writel(hsotg, ~hcintmsk, HCINT(chan->hc_num));

		/*
		 * Make sure the halt status is set to URB_DEQUEUE or AHB_ERR
		 * even if the channel was already halted for some other
		 * reason
		 */
		chan->halt_status = halt_status;

		hcchar = dwc2_readl(hsotg, HCCHAR(chan->hc_num));
		if (!(hcchar & HCCHAR_CHENA)) {
			/*
			 * The channel is either already halted or it hasn't
			 * started yet. In DMA mode, the transfer may halt if
			 * it finishes normally or a condition occurs that
			 * requires driver intervention. Don't want to halt
			 * the channel again. In either Slave or DMA mode,
			 * it's possible that the transfer has been assigned
			 * to a channel, but not started yet when an URB is
			 * dequeued. Don't want to halt a channel that hasn't
			 * started yet.
			 */
			return;
		}
	}
	if (chan->halt_pending) {
		/*
		 * A halt has already been issued for this channel. This might
		 * happen when a transfer is aborted by a higher level in
		 * the stack.
		 */
		dev_vdbg(hsotg->dev,
			 "*** %s: Channel %d, chan->halt_pending already set ***\n",
			 __func__, chan->hc_num);
		return;
	}

	hcchar = dwc2_readl(hsotg, HCCHAR(chan->hc_num));

	/* No need to set the bit in DDMA for disabling the channel */
	/* TODO check it everywhere channel is disabled */
	if (!hsotg->params.dma_desc_enable) {
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "desc DMA disabled\n");
		hcchar |= HCCHAR_CHENA;
	} else {
		if (dbg_hc(chan))
			dev_dbg(hsotg->dev, "desc DMA enabled\n");
	}
	hcchar |= HCCHAR_CHDIS;

	if (!hsotg->params.host_dma) {
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "DMA not enabled\n");
		hcchar |= HCCHAR_CHENA;

		/* Check for space in the request queue to issue the halt */
		if (chan->ep_type == USB_ENDPOINT_XFER_CONTROL ||
		    chan->ep_type == USB_ENDPOINT_XFER_BULK) {
			dev_vdbg(hsotg->dev, "control/bulk\n");
			nptxsts = dwc2_readl(hsotg, GNPTXSTS);
			if ((nptxsts & TXSTS_QSPCAVAIL_MASK) == 0) {
				dev_vdbg(hsotg->dev, "Disabling channel\n");
				hcchar &= ~HCCHAR_CHENA;
			}
		} else {
			if (dbg_perio())
				dev_vdbg(hsotg->dev, "isoc/intr\n");
			hptxsts = dwc2_readl(hsotg, HPTXSTS);
			if ((hptxsts & TXSTS_QSPCAVAIL_MASK) == 0 ||
			    hsotg->queuing_high_bandwidth) {
				if (dbg_perio())
					dev_vdbg(hsotg->dev, "Disabling channel\n");
				hcchar &= ~HCCHAR_CHENA;
			}
		}
	} else {
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "DMA enabled\n");
	}

	dwc2_writel(hsotg, hcchar, HCCHAR(chan->hc_num));
	chan->halt_status = halt_status;

	if (hcchar & HCCHAR_CHENA) {
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "Channel enabled\n");
		chan->halt_pending = 1;
		chan->halt_on_queue = 0;
	} else {
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "Channel disabled\n");
		chan->halt_on_queue = 1;
	}

	if (dbg_hc(chan)) {
		dev_vdbg(hsotg->dev, "%s: Channel %d\n", __func__,
			 chan->hc_num);
		dev_vdbg(hsotg->dev, "	 hcchar: 0x%08x\n",
			 hcchar);
		dev_vdbg(hsotg->dev, "	 halt_pending: %d\n",
			 chan->halt_pending);
		dev_vdbg(hsotg->dev, "	 halt_on_queue: %d\n",
			 chan->halt_on_queue);
		dev_vdbg(hsotg->dev, "	 halt_status: %d\n",
			 chan->halt_status);
	}
}

/**
 * dwc2_hc_cleanup() - Clears the transfer state for a host channel
 *
 * @hsotg: Programming view of DWC_otg controller
 * @chan:  Identifies the host channel to clean up
 *
 * This function is normally called after a transfer is done and the host
 * channel is being released
 */
void dwc2_hc_cleanup(struct dwc2_hsotg *hsotg, struct dwc2_host_chan *chan)
{
	u32 hcintmsk;

	chan->xfer_started = 0;

	list_del_init(&chan->split_order_list_entry);

	/*
	 * Clear channel interrupt enables and any unhandled channel interrupt
	 * conditions
	 */
	dwc2_writel(hsotg, 0, HCINTMSK(chan->hc_num));
	hcintmsk = 0xffffffff;
	hcintmsk &= ~HCINTMSK_RESERVED14_31;
	dwc2_writel(hsotg, hcintmsk, HCINT(chan->hc_num));
}

/**
 * dwc2_hc_set_even_odd_frame() - Sets the channel property that indicates in
 * which frame a periodic transfer should occur
 *
 * @hsotg:  Programming view of DWC_otg controller
 * @chan:   Identifies the host channel to set up and its properties
 * @hcchar: Current value of the HCCHAR register for the specified host channel
 *
 * This function has no effect on non-periodic transfers
 */
static void dwc2_hc_set_even_odd_frame(struct dwc2_hsotg *hsotg,
				       struct dwc2_host_chan *chan, u32 *hcchar)
{
	if (chan->ep_type == USB_ENDPOINT_XFER_INT ||
	    chan->ep_type == USB_ENDPOINT_XFER_ISOC) {
		int host_speed;
		int xfer_ns;
		int xfer_us;
		int bytes_in_fifo;
		u16 fifo_space;
		u16 frame_number;
		u16 wire_frame;

		/*
		 * Try to figure out if we're an even or odd frame. If we set
		 * even and the current frame number is even the the transfer
		 * will happen immediately.  Similar if both are odd. If one is
		 * even and the other is odd then the transfer will happen when
		 * the frame number ticks.
		 *
		 * There's a bit of a balancing act to get this right.
		 * Sometimes we may want to send data in the current frame (AK
		 * right away).  We might want to do this if the frame number
		 * _just_ ticked, but we might also want to do this in order
		 * to continue a split transaction that happened late in a
		 * microframe (so we didn't know to queue the next transfer
		 * until the frame number had ticked).  The problem is that we
		 * need a lot of knowledge to know if there's actually still
		 * time to send things or if it would be better to wait until
		 * the next frame.
		 *
		 * We can look at how much time is left in the current frame
		 * and make a guess about whether we'll have time to transfer.
		 * We'll do that.
		 */

		/* Get speed host is running at */
		host_speed = (chan->speed != USB_SPEED_HIGH &&
			      !chan->do_split) ? chan->speed : USB_SPEED_HIGH;

		/* See how many bytes are in the periodic FIFO right now */
		fifo_space = (dwc2_readl(hsotg, HPTXSTS) &
			      TXSTS_FSPCAVAIL_MASK) >> TXSTS_FSPCAVAIL_SHIFT;
		bytes_in_fifo = sizeof(u32) *
				(hsotg->params.host_perio_tx_fifo_size -
				 fifo_space);

		/*
		 * Roughly estimate bus time for everything in the periodic
		 * queue + our new transfer.  This is "rough" because we're
		 * using a function that makes takes into account IN/OUT
		 * and INT/ISO and we're just slamming in one value for all
		 * transfers.  This should be an over-estimate and that should
		 * be OK, but we can probably tighten it.
		 */
		xfer_ns = usb_calc_bus_time(host_speed, false, false,
					    chan->xfer_len + bytes_in_fifo);
		xfer_us = NS_TO_US(xfer_ns);

		/* See what frame number we'll be at by the time we finish */
		frame_number = dwc2_hcd_get_future_frame_number(hsotg, xfer_us);

		/* This is when we were scheduled to be on the wire */
		wire_frame = dwc2_frame_num_inc(chan->qh->next_active_frame, 1);

		/*
		 * If we'd finish _after_ the frame we're scheduled in then
		 * it's hopeless.  Just schedule right away and hope for the
		 * best.  Note that it _might_ be wise to call back into the
		 * scheduler to pick a better frame, but this is better than
		 * nothing.
		 */
		if (dwc2_frame_num_gt(frame_number, wire_frame)) {
			dwc2_sch_vdbg(hsotg,
				      "QH=%p EO MISS fr=%04x=>%04x (%+d)\n",
				      chan->qh, wire_frame, frame_number,
				      dwc2_frame_num_dec(frame_number,
							 wire_frame));
			wire_frame = frame_number;

			/*
			 * We picked a different frame number; communicate this
			 * back to the scheduler so it doesn't try to schedule
			 * another in the same frame.
			 *
			 * Remember that next_active_frame is 1 before the wire
			 * frame.
			 */
			chan->qh->next_active_frame =
				dwc2_frame_num_dec(frame_number, 1);
		}

		if (wire_frame & 1)
			*hcchar |= HCCHAR_ODDFRM;
		else
			*hcchar &= ~HCCHAR_ODDFRM;
	}
}

static void dwc2_set_pid_isoc(struct dwc2_host_chan *chan)
{
	/* Set up the initial PID for the transfer */
	if (chan->speed == USB_SPEED_HIGH) {
		if (chan->ep_is_in) {
			if (chan->multi_count == 1)
				chan->data_pid_start = DWC2_HC_PID_DATA0;
			else if (chan->multi_count == 2)
				chan->data_pid_start = DWC2_HC_PID_DATA1;
			else
				chan->data_pid_start = DWC2_HC_PID_DATA2;
		} else {
			if (chan->multi_count == 1)
				chan->data_pid_start = DWC2_HC_PID_DATA0;
			else
				chan->data_pid_start = DWC2_HC_PID_MDATA;
		}
	} else {
		chan->data_pid_start = DWC2_HC_PID_DATA0;
	}
}

/**
 * dwc2_hc_write_packet() - Writes a packet into the Tx FIFO associated with
 * the Host Channel
 *
 * @hsotg: Programming view of DWC_otg controller
 * @chan:  Information needed to initialize the host channel
 *
 * This function should only be called in Slave mode. For a channel associated
 * with a non-periodic EP, the non-periodic Tx FIFO is written. For a channel
 * associated with a periodic EP, the periodic Tx FIFO is written.
 *
 * Upon return the xfer_buf and xfer_count fields in chan are incremented by
 * the number of bytes written to the Tx FIFO.
 */
static void dwc2_hc_write_packet(struct dwc2_hsotg *hsotg,
				 struct dwc2_host_chan *chan)
{
	u32 i;
	u32 remaining_count;
	u32 byte_count;
	u32 dword_count;
	u32 *data_buf = (u32 *)chan->xfer_buf;

	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "%s()\n", __func__);

	remaining_count = chan->xfer_len - chan->xfer_count;
	if (remaining_count > chan->max_packet)
		byte_count = chan->max_packet;
	else
		byte_count = remaining_count;

	dword_count = (byte_count + 3) / 4;

	if (((unsigned long)data_buf & 0x3) == 0) {
		/* xfer_buf is DWORD aligned */
		for (i = 0; i < dword_count; i++, data_buf++)
			dwc2_writel(hsotg, *data_buf, HCFIFO(chan->hc_num));
	} else {
		/* xfer_buf is not DWORD aligned */
		for (i = 0; i < dword_count; i++, data_buf++) {
			u32 data = data_buf[0] | data_buf[1] << 8 |
				   data_buf[2] << 16 | data_buf[3] << 24;
			dwc2_writel(hsotg, data, HCFIFO(chan->hc_num));
		}
	}

	chan->xfer_count += byte_count;
	chan->xfer_buf += byte_count;
}

/**
 * dwc2_hc_do_ping() - Starts a PING transfer
 *
 * @hsotg: Programming view of DWC_otg controller
 * @chan:  Information needed to initialize the host channel
 *
 * This function should only be called in Slave mode. The Do Ping bit is set in
 * the HCTSIZ register, then the channel is enabled.
 */
static void dwc2_hc_do_ping(struct dwc2_hsotg *hsotg,
			    struct dwc2_host_chan *chan)
{
	u32 hcchar;
	u32 hctsiz;

	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "%s: Channel %d\n", __func__,
			 chan->hc_num);

	hctsiz = TSIZ_DOPNG;
	hctsiz |= 1 << TSIZ_PKTCNT_SHIFT;
	dwc2_writel(hsotg, hctsiz, HCTSIZ(chan->hc_num));

	hcchar = dwc2_readl(hsotg, HCCHAR(chan->hc_num));
	hcchar |= HCCHAR_CHENA;
	hcchar &= ~HCCHAR_CHDIS;
	dwc2_writel(hsotg, hcchar, HCCHAR(chan->hc_num));
}

/**
 * dwc2_hc_start_transfer() - Does the setup for a data transfer for a host
 * channel and starts the transfer
 *
 * @hsotg: Programming view of DWC_otg controller
 * @chan:  Information needed to initialize the host channel. The xfer_len value
 *         may be reduced to accommodate the max widths of the XferSize and
 *         PktCnt fields in the HCTSIZn register. The multi_count value may be
 *         changed to reflect the final xfer_len value.
 *
 * This function may be called in either Slave mode or DMA mode. In Slave mode,
 * the caller must ensure that there is sufficient space in the request queue
 * and Tx Data FIFO.
 *
 * For an OUT transfer in Slave mode, it loads a data packet into the
 * appropriate FIFO. If necessary, additional data packets are loaded in the
 * Host ISR.
 *
 * For an IN transfer in Slave mode, a data packet is requested. The data
 * packets are unloaded from the Rx FIFO in the Host ISR. If necessary,
 * additional data packets are requested in the Host ISR.
 *
 * For a PING transfer in Slave mode, the Do Ping bit is set in the HCTSIZ
 * register along with a packet count of 1 and the channel is enabled. This
 * causes a single PING transaction to occur. Other fields in HCTSIZ are
 * simply set to 0 since no data transfer occurs in this case.
 *
 * For a PING transfer in DMA mode, the HCTSIZ register is initialized with
 * all the information required to perform the subsequent data transfer. In
 * addition, the Do Ping bit is set in the HCTSIZ register. In this case, the
 * controller performs the entire PING protocol, then starts the data
 * transfer.
 */
static void dwc2_hc_start_transfer(struct dwc2_hsotg *hsotg,
				   struct dwc2_host_chan *chan)
{
	u32 max_hc_xfer_size = hsotg->params.max_transfer_size;
	u16 max_hc_pkt_count = hsotg->params.max_packet_count;
	u32 hcchar;
	u32 hctsiz = 0;
	u16 num_packets;
	u32 ec_mc;

	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "%s()\n", __func__);

	if (chan->do_ping) {
		if (!hsotg->params.host_dma) {
			if (dbg_hc(chan))
				dev_vdbg(hsotg->dev, "ping, no DMA\n");
			dwc2_hc_do_ping(hsotg, chan);
			chan->xfer_started = 1;
			return;
		}

		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "ping, DMA\n");

		hctsiz |= TSIZ_DOPNG;
	}

	if (chan->do_split) {
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "split\n");
		num_packets = 1;

		if (chan->complete_split && !chan->ep_is_in)
			/*
			 * For CSPLIT OUT Transfer, set the size to 0 so the
			 * core doesn't expect any data written to the FIFO
			 */
			chan->xfer_len = 0;
		else if (chan->ep_is_in || chan->xfer_len > chan->max_packet)
			chan->xfer_len = chan->max_packet;
		else if (!chan->ep_is_in && chan->xfer_len > 188)
			chan->xfer_len = 188;

		hctsiz |= chan->xfer_len << TSIZ_XFERSIZE_SHIFT &
			  TSIZ_XFERSIZE_MASK;

		/* For split set ec_mc for immediate retries */
		if (chan->ep_type == USB_ENDPOINT_XFER_INT ||
		    chan->ep_type == USB_ENDPOINT_XFER_ISOC)
			ec_mc = 3;
		else
			ec_mc = 1;
	} else {
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "no split\n");
		/*
		 * Ensure that the transfer length and packet count will fit
		 * in the widths allocated for them in the HCTSIZn register
		 */
		if (chan->ep_type == USB_ENDPOINT_XFER_INT ||
		    chan->ep_type == USB_ENDPOINT_XFER_ISOC) {
			/*
			 * Make sure the transfer size is no larger than one
			 * (micro)frame's worth of data. (A check was done
			 * when the periodic transfer was accepted to ensure
			 * that a (micro)frame's worth of data can be
			 * programmed into a channel.)
			 */
			u32 max_periodic_len =
				chan->multi_count * chan->max_packet;

			if (chan->xfer_len > max_periodic_len)
				chan->xfer_len = max_periodic_len;
		} else if (chan->xfer_len > max_hc_xfer_size) {
			/*
			 * Make sure that xfer_len is a multiple of max packet
			 * size
			 */
			chan->xfer_len =
				max_hc_xfer_size - chan->max_packet + 1;
		}

		if (chan->xfer_len > 0) {
			num_packets = (chan->xfer_len + chan->max_packet - 1) /
					chan->max_packet;
			if (num_packets > max_hc_pkt_count) {
				num_packets = max_hc_pkt_count;
				chan->xfer_len = num_packets * chan->max_packet;
			} else if (chan->ep_is_in) {
				/*
				 * Always program an integral # of max packets
				 * for IN transfers.
				 * Note: This assumes that the input buffer is
				 * aligned and sized accordingly.
				 */
				chan->xfer_len = num_packets * chan->max_packet;
			}
		} else {
			/* Need 1 packet for transfer length of 0 */
			num_packets = 1;
		}

		if (chan->ep_type == USB_ENDPOINT_XFER_INT ||
		    chan->ep_type == USB_ENDPOINT_XFER_ISOC)
			/*
			 * Make sure that the multi_count field matches the
			 * actual transfer length
			 */
			chan->multi_count = num_packets;

		if (chan->ep_type == USB_ENDPOINT_XFER_ISOC)
			dwc2_set_pid_isoc(chan);

		hctsiz |= chan->xfer_len << TSIZ_XFERSIZE_SHIFT &
			  TSIZ_XFERSIZE_MASK;

		/* The ec_mc gets the multi_count for non-split */
		ec_mc = chan->multi_count;
	}

	chan->start_pkt_count = num_packets;
	hctsiz |= num_packets << TSIZ_PKTCNT_SHIFT & TSIZ_PKTCNT_MASK;
	hctsiz |= chan->data_pid_start << TSIZ_SC_MC_PID_SHIFT &
		  TSIZ_SC_MC_PID_MASK;
	dwc2_writel(hsotg, hctsiz, HCTSIZ(chan->hc_num));
	if (dbg_hc(chan)) {
		dev_vdbg(hsotg->dev, "Wrote %08x to HCTSIZ(%d)\n",
			 hctsiz, chan->hc_num);

		dev_vdbg(hsotg->dev, "%s: Channel %d\n", __func__,
			 chan->hc_num);
		dev_vdbg(hsotg->dev, "	 Xfer Size: %d\n",
			 (hctsiz & TSIZ_XFERSIZE_MASK) >>
			 TSIZ_XFERSIZE_SHIFT);
		dev_vdbg(hsotg->dev, "	 Num Pkts: %d\n",
			 (hctsiz & TSIZ_PKTCNT_MASK) >>
			 TSIZ_PKTCNT_SHIFT);
		dev_vdbg(hsotg->dev, "	 Start PID: %d\n",
			 (hctsiz & TSIZ_SC_MC_PID_MASK) >>
			 TSIZ_SC_MC_PID_SHIFT);
	}

	if (hsotg->params.host_dma) {
		dma_addr_t dma_addr;

		if (chan->align_buf) {
			if (dbg_hc(chan))
				dev_vdbg(hsotg->dev, "align_buf\n");
			dma_addr = chan->align_buf;
		} else {
			dma_addr = chan->xfer_dma;
		}
		dwc2_writel(hsotg, (u32)dma_addr, HCDMA(chan->hc_num));

		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "Wrote %08lx to HCDMA(%d)\n",
				 (unsigned long)dma_addr, chan->hc_num);
	}

	/* Start the split */
	if (chan->do_split) {
		u32 hcsplt = dwc2_readl(hsotg, HCSPLT(chan->hc_num));

		hcsplt |= HCSPLT_SPLTENA;
		dwc2_writel(hsotg, hcsplt, HCSPLT(chan->hc_num));
	}

	hcchar = dwc2_readl(hsotg, HCCHAR(chan->hc_num));
	hcchar &= ~HCCHAR_MULTICNT_MASK;
	hcchar |= (ec_mc << HCCHAR_MULTICNT_SHIFT) & HCCHAR_MULTICNT_MASK;
	dwc2_hc_set_even_odd_frame(hsotg, chan, &hcchar);

	if (hcchar & HCCHAR_CHDIS)
		dev_warn(hsotg->dev,
			 "%s: chdis set, channel %d, hcchar 0x%08x\n",
			 __func__, chan->hc_num, hcchar);

	/* Set host channel enable after all other setup is complete */
	hcchar |= HCCHAR_CHENA;
	hcchar &= ~HCCHAR_CHDIS;

	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "	 Multi Cnt: %d\n",
			 (hcchar & HCCHAR_MULTICNT_MASK) >>
			 HCCHAR_MULTICNT_SHIFT);

	dwc2_writel(hsotg, hcchar, HCCHAR(chan->hc_num));
	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "Wrote %08x to HCCHAR(%d)\n", hcchar,
			 chan->hc_num);

	chan->xfer_started = 1;
	chan->requests++;

	if (!hsotg->params.host_dma &&
	    !chan->ep_is_in && chan->xfer_len > 0)
		/* Load OUT packet into the appropriate Tx FIFO */
		dwc2_hc_write_packet(hsotg, chan);
}

/**
 * dwc2_hc_start_transfer_ddma() - Does the setup for a data transfer for a
 * host channel and starts the transfer in Descriptor DMA mode
 *
 * @hsotg: Programming view of DWC_otg controller
 * @chan:  Information needed to initialize the host channel
 *
 * Initializes HCTSIZ register. For a PING transfer the Do Ping bit is set.
 * Sets PID and NTD values. For periodic transfers initializes SCHED_INFO field
 * with micro-frame bitmap.
 *
 * Initializes HCDMA register with descriptor list address and CTD value then
 * starts the transfer via enabling the channel.
 */
void dwc2_hc_start_transfer_ddma(struct dwc2_hsotg *hsotg,
				 struct dwc2_host_chan *chan)
{
	u32 hcchar;
	u32 hctsiz = 0;

	if (chan->do_ping)
		hctsiz |= TSIZ_DOPNG;

	if (chan->ep_type == USB_ENDPOINT_XFER_ISOC)
		dwc2_set_pid_isoc(chan);

	/* Packet Count and Xfer Size are not used in Descriptor DMA mode */
	hctsiz |= chan->data_pid_start << TSIZ_SC_MC_PID_SHIFT &
		  TSIZ_SC_MC_PID_MASK;

	/* 0 - 1 descriptor, 1 - 2 descriptors, etc */
	hctsiz |= (chan->ntd - 1) << TSIZ_NTD_SHIFT & TSIZ_NTD_MASK;

	/* Non-zero only for high-speed interrupt endpoints */
	hctsiz |= chan->schinfo << TSIZ_SCHINFO_SHIFT & TSIZ_SCHINFO_MASK;

	if (dbg_hc(chan)) {
		dev_vdbg(hsotg->dev, "%s: Channel %d\n", __func__,
			 chan->hc_num);
		dev_vdbg(hsotg->dev, "	 Start PID: %d\n",
			 chan->data_pid_start);
		dev_vdbg(hsotg->dev, "	 NTD: %d\n", chan->ntd - 1);
	}

	dwc2_writel(hsotg, hctsiz, HCTSIZ(chan->hc_num));

	dma_sync_single_for_device(hsotg->dev, chan->desc_list_addr,
				   chan->desc_list_sz, DMA_TO_DEVICE);

	dwc2_writel(hsotg, chan->desc_list_addr, HCDMA(chan->hc_num));

	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "Wrote %pad to HCDMA(%d)\n",
			 &chan->desc_list_addr, chan->hc_num);

	hcchar = dwc2_readl(hsotg, HCCHAR(chan->hc_num));
	hcchar &= ~HCCHAR_MULTICNT_MASK;
	hcchar |= chan->multi_count << HCCHAR_MULTICNT_SHIFT &
		  HCCHAR_MULTICNT_MASK;

	if (hcchar & HCCHAR_CHDIS)
		dev_warn(hsotg->dev,
			 "%s: chdis set, channel %d, hcchar 0x%08x\n",
			 __func__, chan->hc_num, hcchar);

	/* Set host channel enable after all other setup is complete */
	hcchar |= HCCHAR_CHENA;
	hcchar &= ~HCCHAR_CHDIS;

	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "	 Multi Cnt: %d\n",
			 (hcchar & HCCHAR_MULTICNT_MASK) >>
			 HCCHAR_MULTICNT_SHIFT);

	dwc2_writel(hsotg, hcchar, HCCHAR(chan->hc_num));
	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "Wrote %08x to HCCHAR(%d)\n", hcchar,
			 chan->hc_num);

	chan->xfer_started = 1;
	chan->requests++;
}

/**
 * dwc2_hc_continue_transfer() - Continues a data transfer that was started by
 * a previous call to dwc2_hc_start_transfer()
 *
 * @hsotg: Programming view of DWC_otg controller
 * @chan:  Information needed to initialize the host channel
 *
 * The caller must ensure there is sufficient space in the request queue and Tx
 * Data FIFO. This function should only be called in Slave mode. In DMA mode,
 * the controller acts autonomously to complete transfers programmed to a host
 * channel.
 *
 * For an OUT transfer, a new data packet is loaded into the appropriate FIFO
 * if there is any data remaining to be queued. For an IN transfer, another
 * data packet is always requested. For the SETUP phase of a control transfer,
 * this function does nothing.
 *
 * Return: 1 if a new request is queued, 0 if no more requests are required
 * for this transfer
 */
static int dwc2_hc_continue_transfer(struct dwc2_hsotg *hsotg,
				     struct dwc2_host_chan *chan)
{
	if (dbg_hc(chan))
		dev_vdbg(hsotg->dev, "%s: Channel %d\n", __func__,
			 chan->hc_num);

	if (chan->do_split)
		/* SPLITs always queue just once per channel */
		return 0;

	if (chan->data_pid_start == DWC2_HC_PID_SETUP)
		/* SETUPs are queued only once since they can't be NAK'd */
		return 0;

	if (chan->ep_is_in) {
		/*
		 * Always queue another request for other IN transfers. If
		 * back-to-back INs are issued and NAKs are received for both,
		 * the driver may still be processing the first NAK when the
		 * second NAK is received. When the interrupt handler clears
		 * the NAK interrupt for the first NAK, the second NAK will
		 * not be seen. So we can't depend on the NAK interrupt
		 * handler to requeue a NAK'd request. Instead, IN requests
		 * are issued each time this function is called. When the
		 * transfer completes, the extra requests for the channel will
		 * be flushed.
		 */
		u32 hcchar = dwc2_readl(hsotg, HCCHAR(chan->hc_num));

		dwc2_hc_set_even_odd_frame(hsotg, chan, &hcchar);
		hcchar |= HCCHAR_CHENA;
		hcchar &= ~HCCHAR_CHDIS;
		if (dbg_hc(chan))
			dev_vdbg(hsotg->dev, "	 IN xfer: hcchar = 0x%08x\n",
				 hcchar);
		dwc2_writel(hsotg, hcchar, HCCHAR(chan->hc_num));
		chan->requests++;
		return 1;
	}

	/* OUT transfers */

	if (chan->xfer_count < chan->xfer_len) {
		if (chan->ep_type == USB_ENDPOINT_XFER_INT ||
		    chan->ep_type == USB_ENDPOINT_XFER_ISOC) {
			u32 hcchar = dwc2_readl(hsotg,
						HCCHAR(chan->hc_num));

			dwc2_hc_set_even_odd_frame(hsotg, chan,
						   &hcchar);
		}

		/* Load OUT packet into the appropriate Tx FIFO */
		dwc2_hc_write_packet(hsotg, chan);
		chan->requests++;
		return 1;
	}

	return 0;
}

/*
 * =========================================================================
 *  HCD
 * =========================================================================
 */

/*
 * Processes all the URBs in a single list of QHs. Completes them with
 * -ETIMEDOUT and frees the QTD.
 *
 * Must be called with interrupt disabled and spinlock held
 */
static void dwc2_kill_urbs_in_qh_list(struct dwc2_hsotg *hsotg,
				      struct list_head *qh_list)
{
	struct dwc2_qh *qh, *qh_tmp;
	struct dwc2_qtd *qtd, *qtd_tmp;

	list_for_each_entry_safe(qh, qh_tmp, qh_list, qh_list_entry) {
		list_for_each_entry_safe(qtd, qtd_tmp, &qh->qtd_list,
					 qtd_list_entry) {
			dwc2_host_complete(hsotg, qtd, -ECONNRESET);
			dwc2_hcd_qtd_unlink_and_free(hsotg, qtd, qh);
		}
	}
}

static void dwc2_qh_list_free(struct dwc2_hsotg *hsotg,
			      struct list_head *qh_list)
{
	struct dwc2_qtd *qtd, *qtd_tmp;
	struct dwc2_qh *qh, *qh_tmp;
	unsigned long flags;

	if (!qh_list->next)
		/* The list hasn't been initialized yet */
		return;

	spin_lock_irqsave(&hsotg->lock, flags);

	/* Ensure there are no QTDs or URBs left */
	dwc2_kill_urbs_in_qh_list(hsotg, qh_list);

	list_for_each_entry_safe(qh, qh_tmp, qh_list, qh_list_entry) {
		dwc2_hcd_qh_unlink(hsotg, qh);

		/* Free each QTD in the QH's QTD list */
		list_for_each_entry_safe(qtd, qtd_tmp, &qh->qtd_list,
					 qtd_list_entry)
			dwc2_hcd_qtd_unlink_and_free(hsotg, qtd, qh);

		if (qh->channel && qh->channel->qh == qh)
			qh->channel->qh = NULL;

		spin_unlock_irqrestore(&hsotg->lock, flags);
		dwc2_hcd_qh_free(hsotg, qh);
		spin_lock_irqsave(&hsotg->lock, flags);
	}

	spin_unlock_irqrestore(&hsotg->lock, flags);
}

/*
 * Responds with an error status of -ETIMEDOUT to all URBs in the non-periodic
 * and periodic schedules. The QTD associated with each URB is removed from
 * the schedule and freed. This function may be called when a disconnect is
 * detected or when the HCD is being stopped.
 *
 * Must be called with interrupt disabled and spinlock held
 */
static void dwc2_kill_all_urbs(struct dwc2_hsotg *hsotg)
{
	dwc2_kill_urbs_in_qh_list(hsotg, &hsotg->non_periodic_sched_inactive);
	dwc2_kill_urbs_in_qh_list(hsotg, &hsotg->non_periodic_sched_waiting);
	dwc2_kill_urbs_in_qh_list(hsotg, &hsotg->non_periodic_sched_active);
	dwc2_kill_urbs_in_qh_list(hsotg, &hsotg->periodic_sched_inactive);
	dwc2_kill_urbs_in_qh_list(hsotg, &hsotg->periodic_sched_ready);
	dwc2_kill_urbs_in_qh_list(hsotg, &hsotg->periodic_sched_assigned);
	dwc2_kill_urbs_in_qh_list(hsotg, &hsotg->periodic_sched_queued);
}

/**
 * dwc2_hcd_start() - Starts the HCD when switching to Host mode
 *
 * @hsotg: Pointer to struct dwc2_hsotg
 */
void dwc2_hcd_start(struct dwc2_hsotg *hsotg)
{
	u32 hprt0;

	if (hsotg->op_state == OTG_STATE_B_HOST) {
		/*
		 * Reset the port. During a HNP mode switch the reset
		 * needs to occur within 1ms and have a duration of at
		 * least 50ms.
		 */
		hprt0 = dwc2_read_hprt0(hsotg);
		hprt0 |= HPRT0_RST;
		dwc2_writel(hsotg, hprt0, HPRT0);
	}

	queue_delayed_work(hsotg->wq_otg, &hsotg->start_work,
			   msecs_to_jiffies(50));
}

/* Must be called with interrupt disabled and spinlock held */
static void dwc2_hcd_cleanup_channels(struct dwc2_hsotg *hsotg)
{
	int num_channels = hsotg->params.host_channels;
	struct dwc2_host_chan *channel;
	u32 hcchar;
	int i;

	if (!hsotg->params.host_dma) {
		/* Flush out any channel requests in slave mode */
		for (i = 0; i < num_channels; i++) {
			channel = hsotg->hc_ptr_array[i];
			if (!list_empty(&channel->hc_list_entry))
				continue;
			hcchar = dwc2_readl(hsotg, HCCHAR(i));
			if (hcchar & HCCHAR_CHENA) {
				hcchar &= ~(HCCHAR_CHENA | HCCHAR_EPDIR);
				hcchar |= HCCHAR_CHDIS;
				dwc2_writel(hsotg, hcchar, HCCHAR(i));
			}
		}
	}

	for (i = 0; i < num_channels; i++) {
		channel = hsotg->hc_ptr_array[i];
		if (!list_empty(&channel->hc_list_entry))
			continue;
		hcchar = dwc2_readl(hsotg, HCCHAR(i));
		if (hcchar & HCCHAR_CHENA) {
			/* Halt the channel */
			hcchar |= HCCHAR_CHDIS;
			dwc2_writel(hsotg, hcchar, HCCHAR(i));
		}

		dwc2_hc_cleanup(hsotg, channel);
		list_add_tail(&channel->hc_list_entry, &hsotg->free_hc_list);
		/*
		 * Added for Descriptor DMA to prevent channel double cleanup in
		 * release_channel_ddma(), which is called from ep_disable when
		 * device disconnects
		 */
		channel->qh = NULL;
	}
	/* All channels have been freed, mark them available */
	if (hsotg->params.uframe_sched) {
		hsotg->available_host_channels =
			hsotg->params.host_channels;
	} else {
		hsotg->non_periodic_channels = 0;
		hsotg->periodic_channels = 0;
	}
}

/**
 * dwc2_hcd_connect() - Handles connect of the HCD
 *
 * @hsotg: Pointer to struct dwc2_hsotg
 *
 * Must be called with interrupt disabled and spinlock held
 */
void dwc2_hcd_connect(struct dwc2_hsotg *hsotg)
{
	if (hsotg->lx_state != DWC2_L0)
		usb_hcd_resume_root_hub(hsotg->priv);

	hsotg->flags.b.port_connect_status_change = 1;
	hsotg->flags.b.port_connect_status = 1;
}

/**
 * dwc2_hcd_disconnect() - Handles disconnect of the HCD
 *
 * @hsotg: Pointer to struct dwc2_hsotg
 * @force: If true, we won't try to reconnect even if we see device connected.
 *
 * Must be called with interrupt disabled and spinlock held
 */
void dwc2_hcd_disconnect(struct dwc2_hsotg *hsotg, bool force)
{
	u32 intr;
	u32 hprt0;

	/* Set status flags for the hub driver */
	hsotg->flags.b.port_connect_status_change = 1;
	hsotg->flags.b.port_connect_status = 0;

	/*
	 * Shutdown any transfers in process by clearing the Tx FIFO Empty
	 * interrupt mask and status bits and disabling subsequent host
	 * channel interrupts.
	 */
	intr = dwc2_readl(hsotg, GINTMSK);
	intr &= ~(GINTSTS_NPTXFEMP | GINTSTS_PTXFEMP | GINTSTS_HCHINT);
	dwc2_writel(hsotg, intr, GINTMSK);
	intr = GINTSTS_NPTXFEMP | GINTSTS_PTXFEMP | GINTSTS_HCHINT;
	dwc2_writel(hsotg, intr, GINTSTS);

	/*
	 * Turn off the vbus power only if the core has transitioned to device
	 * mode. If still in host mode, need to keep power on to detect a
	 * reconnection.
	 */
	if (dwc2_is_device_mode(hsotg)) {
		if (hsotg->op_state != OTG_STATE_A_SUSPEND) {
			dev_dbg(hsotg->dev, "Disconnect: PortPower off\n");
			dwc2_writel(hsotg, 0, HPRT0);
		}

		dwc2_disable_host_interrupts(hsotg);
	}

	/* Respond with an error status to all URBs in the schedule */
	dwc2_kill_all_urbs(hsotg);

	if (dwc2_is_host_mode(hsotg))
		/* Clean up any host channels that were in use */
		dwc2_hcd_cleanup_channels(hsotg);

	dwc2_host_disconnect(hsotg);

	/*
	 * Add an extra check here to see if we're actually connected but
	 * we don't have a detection interrupt pending.  This can happen if:
	 *   1. hardware sees connect
	 *   2. hardware sees disconnect
	 *   3. hardware sees connect
	 *   4. dwc2_port_intr() - clears connect interrupt
	 *   5. dwc2_handle_common_intr() - calls here
	 *
	 * Without the extra check here we will end calling disconnect
	 * and won't get any future interrupts to handle the connect.
	 */
	if (!force) {
		hprt0 = dwc2_readl(hsotg, HPRT0);
		if (!(hprt0 & HPRT0_CONNDET) && (hprt0 & HPRT0_CONNSTS))
			dwc2_hcd_connect(hsotg);
	}
}

/**
 * dwc2_hcd_rem_wakeup() - Handles Remote Wakeup
 *
 * @hsotg: Pointer to struct dwc2_hsotg
 */
static void dwc2_hcd_rem_wakeup(struct dwc2_hsotg *hsotg)
{
	if (hsotg->bus_suspended) {
		hsotg->flags.b.port_suspend_change = 1;
		usb_hcd_resume_root_hub(hsotg->priv);
	}

	if (hsotg->lx_state == DWC2_L1)
		hsotg->flags.b.port_l1_change = 1;
}

/**
 * dwc2_hcd_stop() - Halts the DWC_otg host mode operations in a clean manner
 *
 * @hsotg: Pointer to struct dwc2_hsotg
 *
 * Must be called with interrupt disabled and spinlock held
 */
void dwc2_hcd_stop(struct dwc2_hsotg *hsotg)
{
	dev_dbg(hsotg->dev, "DWC OTG HCD STOP\n");

	/*
	 * The root hub should be disconnected before this function is called.
	 * The disconnect will clear the QTD lists (via ..._hcd_urb_dequeue)
	 * and the QH lists (via ..._hcd_endpoint_disable).
	 */

	/* Turn off all host-specific interrupts */
	dwc2_disable_host_interrupts(hsotg);

	/* Turn off the vbus power */
	dev_dbg(hsotg->dev, "PortPower off\n");
	dwc2_writel(hsotg, 0, HPRT0);
}

/* Caller must hold driver lock */
static int dwc2_hcd_urb_enqueue(struct dwc2_hsotg *hsotg,
				struct dwc2_hcd_urb *urb, struct dwc2_qh *qh,
				struct dwc2_qtd *qtd)
{
	u32 intr_mask;
	int retval;
	int dev_speed;

	if (!hsotg->flags.b.port_connect_status) {
		/* No longer connected */
		dev_err(hsotg->dev, "Not connected\n");
		return -ENODEV;
	}

	dev_speed = dwc2_host_get_speed(hsotg, urb->priv);

	/* Some configurations cannot support LS traffic on a FS root port */
	if ((dev_speed == USB_SPEED_LOW) &&
	    (hsotg->hw_params.fs_phy_type == GHWCFG2_FS_PHY_TYPE_DEDICATED) &&
	    (hsotg->hw_params.hs_phy_type == GHWCFG2_HS_PHY_TYPE_UTMI)) {
		u32 hprt0 = dwc2_readl(hsotg, HPRT0);
		u32 prtspd = (hprt0 & HPRT0_SPD_MASK) >> HPRT0_SPD_SHIFT;

		if (prtspd == HPRT0_SPD_FULL_SPEED)
			return -ENODEV;
	}

	if (!qtd)
		return -EINVAL;

	dwc2_hcd_qtd_init(qtd, urb);
	retval = dwc2_hcd_qtd_add(hsotg, qtd, qh);
	if (retval) {
		dev_err(hsotg->dev,
			"DWC OTG HCD URB Enqueue failed adding QTD. Error status %d\n",
			retval);
		return retval;
	}

	intr_mask = dwc2_readl(hsotg, GINTMSK);
	if (!(intr_mask & GINTSTS_SOF)) {
		enum dwc2_transaction_type tr_type;

		if (qtd->qh->ep_type == USB_ENDPOINT_XFER_BULK &&
		    !(qtd->urb->flags & URB_GIVEBACK_ASAP))
			/*
			 * Do not schedule SG transactions until qtd has
			 * URB_GIVEBACK_ASAP set
			 */
			return 0;

		tr_type = dwc2_hcd_select_transactions(hsotg);
		if (tr_type != DWC2_TRANSACTION_NONE)
			dwc2_hcd_queue_transactions(hsotg, tr_type);
	}

	return 0;
}

/* Must be called with interrupt disabled and spinlock held */
static int dwc2_hcd_urb_dequeue(struct dwc2_hsotg *hsotg,
				struct dwc2_hcd_urb *urb)
{
	struct dwc2_qh *qh;
	struct dwc2_qtd *urb_qtd;

	urb_qtd = urb->qtd;
	if (!urb_qtd) {
		dev_dbg(hsotg->dev, "## Urb QTD is NULL ##\n");
		return -EINVAL;
	}

	qh = urb_qtd->qh;
	if (!qh) {
		dev_dbg(hsotg->dev, "## Urb QTD QH is NULL ##\n");
		return -EINVAL;
	}

	urb->priv = NULL;

	if (urb_qtd->in_process && qh->channel) {
		dwc2_dump_channel_info(hsotg, qh->channel);

		/* The QTD is in process (it has been assigned to a channel) */
		if (hsotg->flags.b.port_connect_status)
			/*
			 * If still connected (i.e. in host mode), halt the
			 * channel so it can be used for other transfers. If
			 * no longer connected, the host registers can't be
			 * written to halt the channel since the core is in
			 * device mode.
			 */
			dwc2_hc_halt(hsotg, qh->channel,
				     DWC2_HC_XFER_URB_DEQUEUE);
	}

	/*
	 * Free the QTD and clean up the associated QH. Leave the QH in the
	 * schedule if it has any remaining QTDs.
	 */
	if (!hsotg->params.dma_desc_enable) {
		u8 in_process = urb_qtd->in_process;

		dwc2_hcd_qtd_unlink_and_free(hsotg, urb_qtd, qh);
		if (in_process) {
			dwc2_hcd_qh_deactivate(hsotg, qh, 0);
			qh->channel = NULL;
		} else if (list_empty(&qh->qtd_list)) {
			dwc2_hcd_qh_unlink(hsotg, qh);
		}
	} else {
		dwc2_hcd_qtd_unlink_and_free(hsotg, urb_qtd, qh);
	}

	return 0;
}

/* Must NOT be called with interrupt disabled or spinlock held */
static int dwc2_hcd_endpoint_disable(struct dwc2_hsotg *hsotg,
				     struct usb_host_endpoint *ep, int retry)
{
	struct dwc2_qtd *qtd, *qtd_tmp;
	struct dwc2_qh *qh;
	unsigned long flags;
	int rc;

	spin_lock_irqsave(&hsotg->lock, flags);

	qh = ep->hcpriv;
	if (!qh) {
		rc = -EINVAL;
		goto err;
	}

	while (!list_empty(&qh->qtd_list) && retry--) {
		if (retry == 0) {
			dev_err(hsotg->dev,
				"## timeout in dwc2_hcd_endpoint_disable() ##\n");
			rc = -EBUSY;
			goto err;
		}

		spin_unlock_irqrestore(&hsotg->lock, flags);
		msleep(20);
		spin_lock_irqsave(&hsotg->lock, flags);
		qh = ep->hcpriv;
		if (!qh) {
			rc = -EINVAL;
			goto err;
		}
	}

	dwc2_hcd_qh_unlink(hsotg, qh);

	/* Free each QTD in the QH's QTD list */
	list_for_each_entry_safe(qtd, qtd_tmp, &qh->qtd_list, qtd_list_entry)
		dwc2_hcd_qtd_unlink_and_free(hsotg, qtd, qh);

	ep->hcpriv = NULL;

	if (qh->channel && qh->channel->qh == qh)
		qh->channel->qh = NULL;

	spin_unlock_irqrestore(&hsotg->lock, flags);

	dwc2_hcd_qh_free(hsotg, qh);

	return 0;

err:
	ep->hcpriv = NULL;
	spin_unlock_irqrestore(&hsotg->lock, flags);

	return rc;
}

/* Must be called with interrupt disabled and spinlock held */
static int dwc2_hcd_endpoint_reset(struct dwc2_hsotg *hsotg,
				   struct usb_host_endpoint *ep)
{
	struct dwc2_qh *qh = ep->hcpriv;

	if (!qh)
		return -EINVAL;

	qh->data_toggle = DWC2_HC_PID_DATA0;

	return 0;
}

/**
 * dwc2_core_init() - Initializes the DWC_otg controller registers and
 * prepares the core for device mode or host mode operation
 *
 * @hsotg:         Programming view of the DWC_otg controller
 * @initial_setup: If true then this is the first init for this instance.
 */
int dwc2_core_init(struct dwc2_hsotg *hsotg, bool initial_setup)
{
	u32 usbcfg, otgctl;
	int retval;

	dev_dbg(hsotg->dev, "%s(%p)\n", __func__, hsotg);

	usbcfg = dwc2_readl(hsotg, GUSBCFG);

	/* Set ULPI External VBUS bit if needed */
	usbcfg &= ~GUSBCFG_ULPI_EXT_VBUS_DRV;
	if (hsotg->params.phy_ulpi_ext_vbus)
		usbcfg |= GUSBCFG_ULPI_EXT_VBUS_DRV;

	/* Set external TS Dline pulsing bit if needed */
	usbcfg &= ~GUSBCFG_TERMSELDLPULSE;
	if (hsotg->params.ts_dline)
		usbcfg |= GUSBCFG_TERMSELDLPULSE;

	dwc2_writel(hsotg, usbcfg, GUSBCFG);

	/*
	 * Reset the Controller
	 *
	 * We only need to reset the controller if this is a re-init.
	 * For the first init we know for sure that earlier code reset us (it
	 * needed to in order to properly detect various parameters).
	 */
	if (!initial_setup) {
		retval = dwc2_core_reset(hsotg, false);
		if (retval) {
			dev_err(hsotg->dev, "%s(): Reset failed, aborting\n",
				__func__);
			return retval;
		}
	}

	/*
	 * This needs to happen in FS mode before any other programming occurs
	 */
	retval = dwc2_phy_init(hsotg, initial_setup);
	if (retval)
		return retval;

	/* Program the GAHBCFG Register */
	retval = dwc2_gahbcfg_init(hsotg);
	if (retval)
		return retval;

	/* Program the GUSBCFG register */
	dwc2_gusbcfg_init(hsotg);

	/* Program the GOTGCTL register */
	otgctl = dwc2_readl(hsotg, GOTGCTL);
	otgctl &= ~GOTGCTL_OTGVER;
	dwc2_writel(hsotg, otgctl, GOTGCTL);

	/* Clear the SRP success bit for FS-I2c */
	hsotg->srp_success = 0;

	/* Enable common interrupts */
	dwc2_enable_common_interrupts(hsotg);

	/*
	 * Do device or host initialization based on mode during PCD and
	 * HCD initialization
	 */
	if (dwc2_is_host_mode(hsotg)) {
		dev_dbg(hsotg->dev, "Host Mode\n");
		hsotg->op_state = OTG_STATE_A_HOST;
	} else {
		dev_dbg(hsotg->dev, "Device Mode\n");
		hsotg->op_state = OTG_STATE_B_PERIPHERAL;
	}

	return 0;
}

/**
 * dwc2_core_host_init() - Initializes the DWC_otg controller registers for
 * Host mode
 *
 * @hsotg: Programming view of DWC_otg controller
 *
 * This function flushes the Tx and Rx FIFOs and flushes any entries in the
 * request queues. Host channels are reset to ensure that they are ready for
 * performing transfers.
 */
static void dwc2_core_host_init(struct dwc2_hsotg *hsotg)
{
	u32 hcfg, hfir, otgctl, usbcfg;

	dev_dbg(hsotg->dev, "%s(%p)\n", __func__, hsotg);

	/* Set HS/FS Timeout Calibration to 7 (max available value).
	 * The number of PHY clocks that the application programs in
	 * this field is added to the high/full speed interpacket timeout
	 * duration in the core to account for any additional delays
	 * introduced by the PHY. This can be required, because the delay
	 * introduced by the PHY in generating the linestate condition
	 * can vary from one PHY to another.
	 */
	usbcfg = dwc2_readl(hsotg, GUSBCFG);
	usbcfg |= GUSBCFG_TOUTCAL(7);
	dwc2_writel(hsotg, usbcfg, GUSBCFG);

	/* Restart the Phy Clock */
	dwc2_writel(hsotg, 0, PCGCTL);

	/* Initialize Host Configuration Register */
	dwc2_init_fs_ls_pclk_sel(hsotg);
	if (hsotg->params.speed == DWC2_SPEED_PARAM_FULL ||
	    hsotg->params.speed == DWC2_SPEED_PARAM_LOW) {
		hcfg = dwc2_readl(hsotg, HCFG);
		hcfg |= HCFG_FSLSSUPP;
		dwc2_writel(hsotg, hcfg, HCFG);
	}

	/*
	 * This bit allows dynamic reloading of the HFIR register during
	 * runtime. This bit needs to be programmed during initial configuration
	 * and its value must not be changed during runtime.
	 */
	if (hsotg->params.reload_ctl) {
		hfir = dwc2_readl(hsotg, HFIR);
		hfir |= HFIR_RLDCTRL;
		dwc2_writel(hsotg, hfir, HFIR);
	}

	if (hsotg->params.dma_desc_enable) {
		u32 op_mode = hsotg->hw_params.op_mode;

		if (hsotg->hw_params.snpsid < DWC2_CORE_REV_2_90a ||
		    !hsotg->hw_params.dma_desc_enable ||
		    op_mode == GHWCFG2_OP_MODE_SRP_CAPABLE_DEVICE ||
		    op_mode == GHWCFG2_OP_MODE_NO_SRP_CAPABLE_DEVICE ||
		    op_mode == GHWCFG2_OP_MODE_UNDEFINED) {
			dev_err(hsotg->dev,
				"Hardware does not support descriptor DMA mode -\n");
			dev_err(hsotg->dev,
				"falling back to buffer DMA mode.\n");
			hsotg->params.dma_desc_enable = false;
		} else {
			hcfg = dwc2_readl(hsotg, HCFG);
			hcfg |= HCFG_DESCDMA;
			dwc2_writel(hsotg, hcfg, HCFG);
		}
	}

	/* Configure data FIFO sizes */
	dwc2_config_fifos(hsotg);

	/* TODO - check this */
	/* Clear Host Set HNP Enable in the OTG Control Register */
	otgctl = dwc2_readl(hsotg, GOTGCTL);
	otgctl &= ~GOTGCTL_HSTSETHNPEN;
	dwc2_writel(hsotg, otgctl, GOTGCTL);

	/* Make sure the FIFOs are flushed */
	dwc2_flush_tx_fifo(hsotg, 0x10 /* all TX FIFOs */);
	dwc2_flush_rx_fifo(hsotg);

	/* Clear Host Set HNP Enable in the OTG Control Register */
	otgctl = dwc2_readl(hsotg, GOTGCTL);
	otgctl &= ~GOTGCTL_HSTSETHNPEN;
	dwc2_writel(hsotg, otgctl, GOTGCTL);

	if (!hsotg->params.dma_desc_enable) {
		int num_channels, i;
		u32 hcchar;

		/* Flush out any leftover queued requests */
		num_channels = hsotg->params.host_channels;
		for (i = 0; i < num_channels; i++) {
			hcchar = dwc2_readl(hsotg, HCCHAR(i));
			if (hcchar & HCCHAR_CHENA) {
				hcchar &= ~HCCHAR_CHENA;
				hcchar |= HCCHAR_CHDIS;
				hcchar &= ~HCCHAR_EPDIR;
				dwc2_writel(hsotg, hcchar, HCCHAR(i));
			}
		}

		/* Halt all channels to put them into a known state */
		for (i = 0; i < num_channels; i++) {
			hcchar = dwc2_readl(hsotg, HCCHAR(i));
			if (hcchar & HCCHAR_CHENA) {
				hcchar |= HCCHAR_CHENA | HCCHAR_CHDIS;
				hcchar &= ~HCCHAR_EPDIR;
				dwc2_writel(hsotg, hcchar, HCCHAR(i));
				dev_dbg(hsotg->dev, "%s: Halt channel %d\n",
					__func__, i);

				if (dwc2_hsotg_wait_bit_clear(hsotg, HCCHAR(i),
							      HCCHAR_CHENA,
							      1000)) {
					dev_warn(hsotg->dev,
						 "Unable to clear enable on channel %d\n",
						 i);
				}
			}
		}
	}

	/* Enable ACG feature in host mode, if supported */
	dwc2_enable_acg(hsotg);

	/* Turn on the vbus power */
	dev_dbg(hsotg->dev, "Init: Port Power? op_state=%d\n", hsotg->op_state);
	if (hsotg->op_state == OTG_STATE_A_HOST) {
		u32 hprt0 = dwc2_read_hprt0(hsotg);

		dev_dbg(hsotg->dev, "Init: Power Port (%d)\n",
			!!(hprt0 & HPRT0_PWR));
		if (!(hprt0 & HPRT0_PWR)) {
			hprt0 |= HPRT0_PWR;
			dwc2_writel(hsotg, hprt0, HPRT0);
		}
	}

	dwc2_enable_host_interrupts(hsotg);
}

/*
 * Initializes dynamic portions of the DWC_otg HCD state
 *
 * Must be called with interrupt disabled and spinlock held
 */
static void dwc2_hcd_reinit(struct dwc2_hsotg *hsotg)
{
	struct dwc2_host_chan *chan, *chan_tmp;
	int num_channels;
	int i;

	hsotg->flags.d32 = 0;
	hsotg->non_periodic_qh_ptr = &hsotg->non_periodic_sched_active;

	if (hsotg->params.uframe_sched) {
		hsotg->available_host_channels =
			hsotg->params.host_channels;
	} else {
		hsotg->non_periodic_channels = 0;
		hsotg->periodic_channels = 0;
	}

	/*
	 * Put all channels in the free channel list and clean up channel
	 * states
	 */
	list_for_each_entry_safe(chan, chan_tmp, &hsotg->free_hc_list,
				 hc_list_entry)
		list_del_init(&chan->hc_list_entry);

	num_channels = hsotg->params.host_channels;
	for (i = 0; i < num_channels; i++) {
		chan = hsotg->hc_ptr_array[i];
		list_add_tail(&chan->hc_list_entry, &hsotg->free_hc_list);
		dwc2_hc_cleanup(hsotg, chan);
	}

	/* Initialize the DWC core for host mode operation */
	dwc2_core_host_init(hsotg);
}

static void dwc2_hc_init_split(struct dwc2_hsotg *hsotg,
			       struct dwc2_host_chan *chan,
			       struct dwc2_qtd *qtd, struct dwc2_hcd_urb *urb)
{
	int hub_addr, hub_port;

	chan->do_split = 1;
	chan->xact_pos = qtd->isoc_split_pos;
	chan->complete_split = qtd->complete_split;
	dwc2_host_hub_info(hsotg, urb->priv, &hub_addr, &hub_port);
	chan->hub_addr = (u8)hub_addr;
	chan->hub_port = (u8)hub_port;
}

static void dwc2_hc_init_xfer(struct dwc2_hsotg *hsotg,
			      struct dwc2_host_chan *chan,
			      struct dwc2_qtd *qtd)
{
	struct dwc2_hcd_urb *urb = qtd->urb;
	struct dwc2_hcd_iso_packet_desc *frame_desc;

	switch (dwc2_hcd_get_pipe_type(&urb->pipe_info)) {
	case USB_ENDPOINT_XFER_CONTROL:
		chan->ep_type = USB_ENDPOINT_XFER_CONTROL;

		switch (qtd->control_phase) {
		case DWC2_CONTROL_SETUP:
			dev_vdbg(hsotg->dev, "  Control setup transaction\n");
			chan->do_ping = 0;
			chan->ep_is_in = 0;
			chan->data_pid_start = DWC2_HC_PID_SETUP;
			if (hsotg->params.host_dma)
				chan->xfer_dma = urb->setup_dma;
			else
				chan->xfer_buf = urb->setup_packet;
			chan->xfer_len = 8;
			break;

		case DWC2_CONTROL_DATA:
			dev_vdbg(hsotg->dev, "  Control data transaction\n");
			chan->data_pid_start = qtd->data_toggle;
			break;

		case DWC2_CONTROL_STATUS:
			/*
			 * Direction is opposite of data direction or IN if no
			 * data
			 */
			dev_vdbg(hsotg->dev, "  Control status transaction\n");
			if (urb->length == 0)
				chan->ep_is_in = 1;
			else
				chan->ep_is_in =
					dwc2_hcd_is_pipe_out(&urb->pipe_info);
			if (chan->ep_is_in)
				chan->do_ping = 0;
			chan->data_pid_start = DWC2_HC_PID_DATA1;
			chan->xfer_len = 0;
			if (hsotg->params.host_dma)
				chan->xfer_dma = hsotg->status_buf_dma;
			else
				chan->xfer_buf = hsotg->status_buf;
			break;
		}
		break;

	case USB_ENDPOINT_XFER_BULK:
		chan->ep_type = USB_ENDPOINT_XFER_BULK;
		break;

	case USB_ENDPOINT_XFER_INT:
		chan->ep_type = USB_ENDPOINT_XFER_INT;
		break;

	case USB_ENDPOINT_XFER_ISOC:
		chan->ep_type = USB_ENDPOINT_XFER_ISOC;
		if (hsotg->params.dma_desc_enable)
			break;

		frame_desc = &urb->iso_descs[qtd->isoc_frame_index];
		frame_desc->status = 0;

		if (hsotg->params.host_dma) {
			chan->xfer_dma = urb->dma;
			chan->xfer_dma += frame_desc->offset +
					qtd->isoc_split_offset;
		} else {
			chan->xfer_buf = urb->buf;
			chan->xfer_buf += frame_desc->offset +
					qtd->isoc_split_offset;
		}

		chan->xfer_len = frame_desc->length - qtd->isoc_split_offset;

		if (chan->xact_pos == DWC2_HCSPLT_XACTPOS_ALL) {
			if (chan->xfer_len <= 188)
				chan->xact_pos = DWC2_HCSPLT_XACTPOS_ALL;
			else
				chan->xact_pos = DWC2_HCSPLT_XACTPOS_BEGIN;
		}
		break;
	}
}

static int dwc2_alloc_split_dma_aligned_buf(struct dwc2_hsotg *hsotg,
					    struct dwc2_qh *qh,
					    struct dwc2_host_chan *chan)
{
	if (!hsotg->unaligned_cache ||
	    chan->max_packet > DWC2_KMEM_UNALIGNED_BUF_SIZE)
		return -ENOMEM;

	if (!qh->dw_align_buf) {
		qh->dw_align_buf = kmem_cache_alloc(hsotg->unaligned_cache,
						    GFP_ATOMIC | GFP_DMA);
		if (!qh->dw_align_buf)
			return -ENOMEM;
	}

	qh->dw_align_buf_dma = dma_map_single(hsotg->dev, qh->dw_align_buf,
					      DWC2_KMEM_UNALIGNED_BUF_SIZE,
					      DMA_FROM_DEVICE);

	if (dma_mapping_error(hsotg->dev, qh->dw_align_buf_dma)) {
		dev_err(hsotg->dev, "can't map align_buf\n");
		chan->align_buf = 0;
		return -EINVAL;
	}

	chan->align_buf = qh->dw_align_buf_dma;
	return 0;
}

#define DWC2_USB_DMA_ALIGN 4

static void dwc2_free_dma_aligned_buffer(struct urb *urb)
{
	void *stored_xfer_buffer;
	size_t length;

	if (!(urb->transfer_flags & URB_ALIGNED_TEMP_BUFFER))
		return;

	/* Restore urb->transfer_buffer from the end of the allocated area */
	memcpy(&stored_xfer_buffer,
	       PTR_ALIGN(urb->transfer_buffer + urb->transfer_buffer_length,
			 dma_get_cache_alignment()),
	       sizeof(urb->transfer_buffer));

	if (usb_urb_dir_in(urb)) {
		if (usb_pipeisoc(urb->pipe))
			length = urb->transfer_buffer_length;
		else
			length = urb->actual_length;

		memcpy(stored_xfer_buffer, urb->transfer_buffer, length);
	}
	kfree(urb->transfer_buffer);
	urb->transfer_buffer = stored_xfer_buffer;

	urb->transfer_flags &= ~URB_ALIGNED_TEMP_BUFFER;
}

static int dwc2_alloc_dma_aligned_buffer(struct urb *urb, gfp_t mem_flags)
{
	void *kmalloc_ptr;
	size_t kmalloc_size;

	if (urb->num_sgs || urb->sg ||
	    urb->transfer_buffer_length == 0 ||
	    !((uintptr_t)urb->transfer_buffer & (DWC2_USB_DMA_ALIGN - 1)))
		return 0;

	/*
	 * Allocate a buffer with enough padding for original transfer_buffer
	 * pointer. This allocation is guaranteed to be aligned properly for
	 * DMA
	 */
	kmalloc_size = urb->transfer_buffer_length +
		(dma_get_cache_alignment() - 1) +
		sizeof(urb->transfer_buffer);

	kmalloc_ptr = kmalloc(kmalloc_size, mem_flags);
	if (!kmalloc_ptr)
		return -ENOMEM;

	/*
	 * Position value of original urb->transfer_buffer pointer to the end
	 * of allocation for later referencing
	 */
	memcpy(PTR_ALIGN(kmalloc_ptr + urb->transfer_buffer_length,
			 dma_get_cache_alignment()),
	       &urb->transfer_buffer, sizeof(urb->transfer_buffer));

	if (usb_urb_dir_out(urb))
		memcpy(kmalloc_ptr, urb->transfer_buffer,
		       urb->transfer_buffer_length);
	urb->transfer_buffer = kmalloc_ptr;

	urb->transfer_flags |= URB_ALIGNED_TEMP_BUFFER;

	return 0;
}

static int dwc2_map_urb_for_dma(struct usb_hcd *hcd, struct urb *urb,
				gfp_t mem_flags)
{
	int ret;

	/* We assume setup_dma is always aligned; warn if not */
	WARN_ON_ONCE(urb->setup_dma &&
		     (urb->setup_dma & (DWC2_USB_DMA_ALIGN - 1)));

	ret = dwc2_alloc_dma_aligned_buffer(urb, mem_flags);
	if (ret)
		return ret;

	ret = usb_hcd_map_urb_for_dma(hcd, urb, mem_flags);
	if (ret)
		dwc2_free_dma_aligned_buffer(urb);

	return ret;
}

static void dwc2_unmap_urb_for_dma(struct usb_hcd *hcd, struct urb *urb)
{
	usb_hcd_unmap_urb_for_dma(hcd, urb);
	dwc2_free_dma_aligned_buffer(urb);
}

/**
 * dwc2_assign_and_init_hc() - Assigns transactions from a QTD to a free host
 * channel and initializes the host channel to perform the transactions. The
 * host channel is removed from the free list.
 *
 * @hsotg: The HCD state structure
 * @qh:    Transactions from the first QTD for this QH are selected and assigned
 *         to a free host channel
 */
static int dwc2_assign_and_init_hc(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh)
{
	struct dwc2_host_chan *chan;
	struct dwc2_hcd_urb *urb;
	struct dwc2_qtd *qtd;

	if (dbg_qh(qh))
		dev_vdbg(hsotg->dev, "%s(%p,%p)\n", __func__, hsotg, qh);

	if (list_empty(&qh->qtd_list)) {
		dev_dbg(hsotg->dev, "No QTDs in QH list\n");
		return -ENOMEM;
	}

	if (list_empty(&hsotg->free_hc_list)) {
		dev_dbg(hsotg->dev, "No free channel to assign\n");
		return -ENOMEM;
	}

	chan = list_first_entry(&hsotg->free_hc_list, struct dwc2_host_chan,
				hc_list_entry);

	/* Remove host channel from free list */
	list_del_init(&chan->hc_list_entry);

	qtd = list_first_entry(&qh->qtd_list, struct dwc2_qtd, qtd_list_entry);
	urb = qtd->urb;
	qh->channel = chan;
	qtd->in_process = 1;

	/*
	 * Use usb_pipedevice to determine device address. This address is
	 * 0 before the SET_ADDRESS command and the correct address afterward.
	 */
	chan->dev_addr = dwc2_hcd_get_dev_addr(&urb->pipe_info);
	chan->ep_num = dwc2_hcd_get_ep_num(&urb->pipe_info);
	chan->speed = qh->dev_speed;
	chan->max_packet = qh->maxp;

	chan->xfer_started = 0;
	chan->halt_status = DWC2_HC_XFER_NO_HALT_STATUS;
	chan->error_state = (qtd->error_count > 0);
	chan->halt_on_queue = 0;
	chan->halt_pending = 0;
	chan->requests = 0;

	/*
	 * The following values may be modified in the transfer type section
	 * below. The xfer_len value may be reduced when the transfer is
	 * started to accommodate the max widths of the XferSize and PktCnt
	 * fields in the HCTSIZn register.
	 */

	chan->ep_is_in = (dwc2_hcd_is_pipe_in(&urb->pipe_info) != 0);
	if (chan->ep_is_in)
		chan->do_ping = 0;
	else
		chan->do_ping = qh->ping_state;

	chan->data_pid_start = qh->data_toggle;
	chan->multi_count = 1;

	if (urb->actual_length > urb->length &&
	    !dwc2_hcd_is_pipe_in(&urb->pipe_info))
		urb->actual_length = urb->length;

	if (hsotg->params.host_dma)
		chan->xfer_dma = urb->dma + urb->actual_length;
	else
		chan->xfer_buf = (u8 *)urb->buf + urb->actual_length;

	chan->xfer_len = urb->length - urb->actual_length;
	chan->xfer_count = 0;

	/* Set the split attributes if required */
	if (qh->do_split)
		dwc2_hc_init_split(hsotg, chan, qtd, urb);
	else
		chan->do_split = 0;

	/* Set the transfer attributes */
	dwc2_hc_init_xfer(hsotg, chan, qtd);

	/* For non-dword aligned buffers */
	if (hsotg->params.host_dma && qh->do_split &&
	    chan->ep_is_in && (chan->xfer_dma & 0x3)) {
		dev_vdbg(hsotg->dev, "Non-aligned buffer\n");
		if (dwc2_alloc_split_dma_aligned_buf(hsotg, qh, chan)) {
			dev_err(hsotg->dev,
				"Failed to allocate memory to handle non-aligned buffer\n");
			/* Add channel back to free list */
			chan->align_buf = 0;
			chan->multi_count = 0;
			list_add_tail(&chan->hc_list_entry,
				      &hsotg->free_hc_list);
			qtd->in_process = 0;
			qh->channel = NULL;
			return -ENOMEM;
		}
	} else {
		/*
		 * We assume that DMA is always aligned in non-split
		 * case or split out case. Warn if not.
		 */
		WARN_ON_ONCE(hsotg->params.host_dma &&
			     (chan->xfer_dma & 0x3));
		chan->align_buf = 0;
	}

	if (chan->ep_type == USB_ENDPOINT_XFER_INT ||
	    chan->ep_type == USB_ENDPOINT_XFER_ISOC)
		/*
		 * This value may be modified when the transfer is started
		 * to reflect the actual transfer length
		 */
		chan->multi_count = qh->maxp_mult;

	if (hsotg->params.dma_desc_enable) {
		chan->desc_list_addr = qh->desc_list_dma;
		chan->desc_list_sz = qh->desc_list_sz;
	}

	dwc2_hc_init(hsotg, chan);
	chan->qh = qh;

	return 0;
}

/**
 * dwc2_hcd_select_transactions() - Selects transactions from the HCD transfer
 * schedule and assigns them to available host channels. Called from the HCD
 * interrupt handler functions.
 *
 * @hsotg: The HCD state structure
 *
 * Return: The types of new transactions that were assigned to host channels
 */
enum dwc2_transaction_type dwc2_hcd_select_transactions(
		struct dwc2_hsotg *hsotg)
{
	enum dwc2_transaction_type ret_val = DWC2_TRANSACTION_NONE;
	struct list_head *qh_ptr;
	struct dwc2_qh *qh;
	int num_channels;

#ifdef DWC2_DEBUG_SOF
	dev_vdbg(hsotg->dev, "  Select Transactions\n");
#endif

	/* Process entries in the periodic ready list */
	qh_ptr = hsotg->periodic_sched_ready.next;
	while (qh_ptr != &hsotg->periodic_sched_ready) {
		if (list_empty(&hsotg->free_hc_list))
			break;
		if (hsotg->params.uframe_sched) {
			if (hsotg->available_host_channels <= 1)
				break;
			hsotg->available_host_channels--;
		}
		qh = list_entry(qh_ptr, struct dwc2_qh, qh_list_entry);
		if (dwc2_assign_and_init_hc(hsotg, qh))
			break;

		/*
		 * Move the QH from the periodic ready schedule to the
		 * periodic assigned schedule
		 */
		qh_ptr = qh_ptr->next;
		list_move_tail(&qh->qh_list_entry,
			       &hsotg->periodic_sched_assigned);
		ret_val = DWC2_TRANSACTION_PERIODIC;
	}

	/*
	 * Process entries in the inactive portion of the non-periodic
	 * schedule. Some free host channels may not be used if they are
	 * reserved for periodic transfers.
	 */
	num_channels = hsotg->params.host_channels;
	qh_ptr = hsotg->non_periodic_sched_inactive.next;
	while (qh_ptr != &hsotg->non_periodic_sched_inactive) {
		if (!hsotg->params.uframe_sched &&
		    hsotg->non_periodic_channels >= num_channels -
						hsotg->periodic_channels)
			break;
		if (list_empty(&hsotg->free_hc_list))
			break;
		qh = list_entry(qh_ptr, struct dwc2_qh, qh_list_entry);
		if (hsotg->params.uframe_sched) {
			if (hsotg->available_host_channels < 1)
				break;
			hsotg->available_host_channels--;
		}

		if (dwc2_assign_and_init_hc(hsotg, qh))
			break;

		/*
		 * Move the QH from the non-periodic inactive schedule to the
		 * non-periodic active schedule
		 */
		qh_ptr = qh_ptr->next;
		list_move_tail(&qh->qh_list_entry,
			       &hsotg->non_periodic_sched_active);

		if (ret_val == DWC2_TRANSACTION_NONE)
			ret_val = DWC2_TRANSACTION_NON_PERIODIC;
		else
			ret_val = DWC2_TRANSACTION_ALL;

		if (!hsotg->params.uframe_sched)
			hsotg->non_periodic_channels++;
	}

	return ret_val;
}

/**
 * dwc2_queue_transaction() - Attempts to queue a single transaction request for
 * a host channel associated with either a periodic or non-periodic transfer
 *
 * @hsotg: The HCD state structure
 * @chan:  Host channel descriptor associated with either a periodic or
 *         non-periodic transfer
 * @fifo_dwords_avail: Number of DWORDs available in the periodic Tx FIFO
 *                     for periodic transfers or the non-periodic Tx FIFO
 *                     for non-periodic transfers
 *
 * Return: 1 if a request is queued and more requests may be needed to
 * complete the transfer, 0 if no more requests are required for this
 * transfer, -1 if there is insufficient space in the Tx FIFO
 *
 * This function assumes that there is space available in the appropriate
 * request queue. For an OUT transfer or SETUP transaction in Slave mode,
 * it checks whether space is available in the appropriate Tx FIFO.
 *
 * Must be called with interrupt disabled and spinlock held
 */
static int dwc2_queue_transaction(struct dwc2_hsotg *hsotg,
				  struct dwc2_host_chan *chan,
				  u16 fifo_dwords_avail)
{
	int retval = 0;

	if (chan->do_split)
		/* Put ourselves on the list to keep order straight */
		list_move_tail(&chan->split_order_list_entry,
			       &hsotg->split_order);

	if (hsotg->params.host_dma && chan->qh) {
		if (hsotg->params.dma_desc_enable) {
			if (!chan->xfer_started ||
			    chan->ep_type == USB_ENDPOINT_XFER_ISOC) {
				dwc2_hcd_start_xfer_ddma(hsotg, chan->qh);
				chan->qh->ping_state = 0;
			}
		} else if (!chan->xfer_started) {
			dwc2_hc_start_transfer(hsotg, chan);
			chan->qh->ping_state = 0;
		}
	} else if (chan->halt_pending) {
		/* Don't queue a request if the channel has been halted */
	} else if (chan->halt_on_queue) {
		dwc2_hc_halt(hsotg, chan, chan->halt_status);
	} else if (chan->do_ping) {
		if (!chan->xfer_started)
			dwc2_hc_start_transfer(hsotg, chan);
	} else if (!chan->ep_is_in ||
		   chan->data_pid_start == DWC2_HC_PID_SETUP) {
		if ((fifo_dwords_avail * 4) >= chan->max_packet) {
			if (!chan->xfer_started) {
				dwc2_hc_start_transfer(hsotg, chan);
				retval = 1;
			} else {
				retval = dwc2_hc_continue_transfer(hsotg, chan);
			}
		} else {
			retval = -1;
		}
	} else {
		if (!chan->xfer_started) {
			dwc2_hc_start_transfer(hsotg, chan);
			retval = 1;
		} else {
			retval = dwc2_hc_continue_transfer(hsotg, chan);
		}
	}

	return retval;
}

/*
 * Processes periodic channels for the next frame and queues transactions for
 * these channels to the DWC_otg controller. After queueing transactions, the
 * Periodic Tx FIFO Empty interrupt is enabled if there are more transactions
 * to queue as Periodic Tx FIFO or request queue space becomes available.
 * Otherwise, the Periodic Tx FIFO Empty interrupt is disabled.
 *
 * Must be called with interrupt disabled and spinlock held
 */
static void dwc2_process_periodic_channels(struct dwc2_hsotg *hsotg)
{
	struct list_head *qh_ptr;
	struct dwc2_qh *qh;
	u32 tx_status;
	u32 fspcavail;
	u32 gintmsk;
	int status;
	bool no_queue_space = false;
	bool no_fifo_space = false;
	u32 qspcavail;

	/* If empty list then just adjust interrupt enables */
	if (list_empty(&hsotg->periodic_sched_assigned))
		goto exit;

	if (dbg_perio())
		dev_vdbg(hsotg->dev, "Queue periodic transactions\n");

	tx_status = dwc2_readl(hsotg, HPTXSTS);
	qspcavail = (tx_status & TXSTS_QSPCAVAIL_MASK) >>
		    TXSTS_QSPCAVAIL_SHIFT;
	fspcavail = (tx_status & TXSTS_FSPCAVAIL_MASK) >>
		    TXSTS_FSPCAVAIL_SHIFT;

	if (dbg_perio()) {
		dev_vdbg(hsotg->dev, "  P Tx Req Queue Space Avail (before queue): %d\n",
			 qspcavail);
		dev_vdbg(hsotg->dev, "  P Tx FIFO Space Avail (before queue): %d\n",
			 fspcavail);
	}

	qh_ptr = hsotg->periodic_sched_assigned.next;
	while (qh_ptr != &hsotg->periodic_sched_assigned) {
		tx_status = dwc2_readl(hsotg, HPTXSTS);
		qspcavail = (tx_status & TXSTS_QSPCAVAIL_MASK) >>
			    TXSTS_QSPCAVAIL_SHIFT;
		if (qspcavail == 0) {
			no_queue_space = true;
			break;
		}

		qh = list_entry(qh_ptr, struct dwc2_qh, qh_list_entry);
		if (!qh->channel) {
			qh_ptr = qh_ptr->next;
			continue;
		}

		/* Make sure EP's TT buffer is clean before queueing qtds */
		if (qh->tt_buffer_dirty) {
			qh_ptr = qh_ptr->next;
			continue;
		}

		/*
		 * Set a flag if we're queuing high-bandwidth in slave mode.
		 * The flag prevents any halts to get into the request queue in
		 * the middle of multiple high-bandwidth packets getting queued.
		 */
		if (!hsotg->params.host_dma &&
		    qh->channel->multi_count > 1)
			hsotg->queuing_high_bandwidth = 1;

		fspcavail = (tx_status & TXSTS_FSPCAVAIL_MASK) >>
			    TXSTS_FSPCAVAIL_SHIFT;
		status = dwc2_queue_transaction(hsotg, qh->channel, fspcavail);
		if (status < 0) {
			no_fifo_space = true;
			break;
		}

		/*
		 * In Slave mode, stay on the current transfer until there is
		 * nothing more to do or the high-bandwidth request count is
		 * reached. In DMA mode, only need to queue one request. The
		 * controller automatically handles multiple packets for
		 * high-bandwidth transfers.
		 */
		if (hsotg->params.host_dma || status == 0 ||
		    qh->channel->requests == qh->channel->multi_count) {
			qh_ptr = qh_ptr->next;
			/*
			 * Move the QH from the periodic assigned schedule to
			 * the periodic queued schedule
			 */
			list_move_tail(&qh->qh_list_entry,
				       &hsotg->periodic_sched_queued);

			/* done queuing high bandwidth */
			hsotg->queuing_high_bandwidth = 0;
		}
	}

exit:
	if (no_queue_space || no_fifo_space ||
	    (!hsotg->params.host_dma &&
	     !list_empty(&hsotg->periodic_sched_assigned))) {
		/*
		 * May need to queue more transactions as the request
		 * queue or Tx FIFO empties. Enable the periodic Tx
		 * FIFO empty interrupt. (Always use the half-empty
		 * level to ensure that new requests are loaded as
		 * soon as possible.)
		 */
		gintmsk = dwc2_readl(hsotg, GINTMSK);
		if (!(gintmsk & GINTSTS_PTXFEMP)) {
			gintmsk |= GINTSTS_PTXFEMP;
			dwc2_writel(hsotg, gintmsk, GINTMSK);
		}
	} else {
		/*
		 * Disable the Tx FIFO empty interrupt since there are
		 * no more transactions that need to be queued right
		 * now. This function is called from interrupt
		 * handlers to queue more transactions as transfer
		 * states change.
		 */
		gintmsk = dwc2_readl(hsotg, GINTMSK);
		if (gintmsk & GINTSTS_PTXFEMP) {
			gintmsk &= ~GINTSTS_PTXFEMP;
			dwc2_writel(hsotg, gintmsk, GINTMSK);
		}
	}
}

/*
 * Processes active non-periodic channels and queues transactions for these
 * channels to the DWC_otg controller. After queueing transactions, the NP Tx
 * FIFO Empty interrupt is enabled if there are more transactions to queue as
 * NP Tx FIFO or request queue space becomes available. Otherwise, the NP Tx
 * FIFO Empty interrupt is disabled.
 *
 * Must be called with interrupt disabled and spinlock held
 */
static void dwc2_process_non_periodic_channels(struct dwc2_hsotg *hsotg)
{
	struct list_head *orig_qh_ptr;
	struct dwc2_qh *qh;
	u32 tx_status;
	u32 qspcavail;
	u32 fspcavail;
	u32 gintmsk;
	int status;
	int no_queue_space = 0;
	int no_fifo_space = 0;
	int more_to_do = 0;

	dev_vdbg(hsotg->dev, "Queue non-periodic transactions\n");

	tx_status = dwc2_readl(hsotg, GNPTXSTS);
	qspcavail = (tx_status & TXSTS_QSPCAVAIL_MASK) >>
		    TXSTS_QSPCAVAIL_SHIFT;
	fspcavail = (tx_status & TXSTS_FSPCAVAIL_MASK) >>
		    TXSTS_FSPCAVAIL_SHIFT;
	dev_vdbg(hsotg->dev, "  NP Tx Req Queue Space Avail (before queue): %d\n",
		 qspcavail);
	dev_vdbg(hsotg->dev, "  NP Tx FIFO Space Avail (before queue): %d\n",
		 fspcavail);

	/*
	 * Keep track of the starting point. Skip over the start-of-list
	 * entry.
	 */
	if (hsotg->non_periodic_qh_ptr == &hsotg->non_periodic_sched_active)
		hsotg->non_periodic_qh_ptr = hsotg->non_periodic_qh_ptr->next;
	orig_qh_ptr = hsotg->non_periodic_qh_ptr;

	/*
	 * Process once through the active list or until no more space is
	 * available in the request queue or the Tx FIFO
	 */
	do {
		tx_status = dwc2_readl(hsotg, GNPTXSTS);
		qspcavail = (tx_status & TXSTS_QSPCAVAIL_MASK) >>
			    TXSTS_QSPCAVAIL_SHIFT;
		if (!hsotg->params.host_dma && qspcavail == 0) {
			no_queue_space = 1;
			break;
		}

		qh = list_entry(hsotg->non_periodic_qh_ptr, struct dwc2_qh,
				qh_list_entry);
		if (!qh->channel)
			goto next;

		/* Make sure EP's TT buffer is clean before queueing qtds */
		if (qh->tt_buffer_dirty)
			goto next;

		fspcavail = (tx_status & TXSTS_FSPCAVAIL_MASK) >>
			    TXSTS_FSPCAVAIL_SHIFT;
		status = dwc2_queue_transaction(hsotg, qh->channel, fspcavail);

		if (status > 0) {
			more_to_do = 1;
		} else if (status < 0) {
			no_fifo_space = 1;
			break;
		}
next:
		/* Advance to next QH, skipping start-of-list entry */
		hsotg->non_periodic_qh_ptr = hsotg->non_periodic_qh_ptr->next;
		if (hsotg->non_periodic_qh_ptr ==
				&hsotg->non_periodic_sched_active)
			hsotg->non_periodic_qh_ptr =
					hsotg->non_periodic_qh_ptr->next;
	} while (hsotg->non_periodic_qh_ptr != orig_qh_ptr);

	if (!hsotg->params.host_dma) {
		tx_status = dwc2_readl(hsotg, GNPTXSTS);
		qspcavail = (tx_status & TXSTS_QSPCAVAIL_MASK) >>
			    TXSTS_QSPCAVAIL_SHIFT;
		fspcavail = (tx_status & TXSTS_FSPCAVAIL_MASK) >>
			    TXSTS_FSPCAVAIL_SHIFT;
		dev_vdbg(hsotg->dev,
			 "  NP Tx Req Queue Space Avail (after queue): %d\n",
			 qspcavail);
		dev_vdbg(hsotg->dev,
			 "  NP Tx FIFO Space Avail (after queue): %d\n",
			 fspcavail);

		if (more_to_do || no_queue_space || no_fifo_space) {
			/*
			 * May need to queue more transactions as the request
			 * queue or Tx FIFO empties. Enable the non-periodic
			 * Tx FIFO empty interrupt. (Always use the half-empty
			 * level to ensure that new requests are loaded as
			 * soon as possible.)
			 */
			gintmsk = dwc2_readl(hsotg, GINTMSK);
			gintmsk |= GINTSTS_NPTXFEMP;
			dwc2_writel(hsotg, gintmsk, GINTMSK);
		} else {
			/*
			 * Disable the Tx FIFO empty interrupt since there are
			 * no more transactions that need to be queued right
			 * now. This function is called from interrupt
			 * handlers to queue more transactions as transfer
			 * states change.
			 */
			gintmsk = dwc2_readl(hsotg, GINTMSK);
			gintmsk &= ~GINTSTS_NPTXFEMP;
			dwc2_writel(hsotg, gintmsk, GINTMSK);
		}
	}
}

/**
 * dwc2_hcd_queue_transactions() - Processes the currently active host channels
 * and queues transactions for these channels to the DWC_otg controller. Called
 * from the HCD interrupt handler functions.
 *
 * @hsotg:   The HCD state structure
 * @tr_type: The type(s) of transactions to queue (non-periodic, periodic,
 *           or both)
 *
 * Must be called with interrupt disabled and spinlock held
 */
void dwc2_hcd_queue_transactions(struct dwc2_hsotg *hsotg,
				 enum dwc2_transaction_type tr_type)
{
#ifdef DWC2_DEBUG_SOF
	dev_vdbg(hsotg->dev, "Queue Transactions\n");
#endif
	/* Process host channels associated with periodic transfers */
	if (tr_type == DWC2_TRANSACTION_PERIODIC ||
	    tr_type == DWC2_TRANSACTION_ALL)
		dwc2_process_periodic_channels(hsotg);

	/* Process host channels associated with non-periodic transfers */
	if (tr_type == DWC2_TRANSACTION_NON_PERIODIC ||
	    tr_type == DWC2_TRANSACTION_ALL) {
		if (!list_empty(&hsotg->non_periodic_sched_active)) {
			dwc2_process_non_periodic_channels(hsotg);
		} else {
			/*
			 * Ensure NP Tx FIFO empty interrupt is disabled when
			 * there are no non-periodic transfers to process
			 */
			u32 gintmsk = dwc2_readl(hsotg, GINTMSK);

			gintmsk &= ~GINTSTS_NPTXFEMP;
			dwc2_writel(hsotg, gintmsk, GINTMSK);
		}
	}
}

static void dwc2_conn_id_status_change(struct work_struct *work)
{
	struct dwc2_hsotg *hsotg = container_of(work, struct dwc2_hsotg,
						wf_otg);
	u32 count = 0;
	u32 gotgctl;
	unsigned long flags;

	dev_dbg(hsotg->dev, "%s()\n", __func__);

	gotgctl = dwc2_readl(hsotg, GOTGCTL);
	dev_dbg(hsotg->dev, "gotgctl=%0x\n", gotgctl);
	dev_dbg(hsotg->dev, "gotgctl.b.conidsts=%d\n",
		!!(gotgctl & GOTGCTL_CONID_B));

	/* B-Device connector (Device Mode) */
	if (gotgctl & GOTGCTL_CONID_B) {
		dwc2_vbus_supply_exit(hsotg);
		/* Wait for switch to device mode */
		dev_dbg(hsotg->dev, "connId B\n");
		if (hsotg->bus_suspended) {
			dev_info(hsotg->dev,
				 "Do port resume before switching to device mode\n");
			dwc2_port_resume(hsotg);
		}
		while (!dwc2_is_device_mode(hsotg)) {
			dev_info(hsotg->dev,
				 "Waiting for Peripheral Mode, Mode=%s\n",
				 dwc2_is_host_mode(hsotg) ? "Host" :
				 "Peripheral");
			msleep(20);
			/*
			 * Sometimes the initial GOTGCTRL read is wrong, so
			 * check it again and jump to host mode if that was
			 * the case.
			 */
			gotgctl = dwc2_readl(hsotg, GOTGCTL);
			if (!(gotgctl & GOTGCTL_CONID_B))
				goto host;
			if (++count > 250)
				break;
		}
		if (count > 250)
			dev_err(hsotg->dev,
				"Connection id status change timed out\n");

		/*
		 * Exit Partial Power Down without restoring registers.
		 * No need to check the return value as registers
		 * are not being restored.
		 */
		if (hsotg->in_ppd && hsotg->lx_state == DWC2_L2)
			dwc2_exit_partial_power_down(hsotg, 0, false);

		hsotg->op_state = OTG_STATE_B_PERIPHERAL;
		dwc2_core_init(hsotg, false);
		dwc2_enable_global_interrupts(hsotg);
		spin_lock_irqsave(&hsotg->lock, flags);
		dwc2_hsotg_core_init_disconnected(hsotg, false);
		spin_unlock_irqrestore(&hsotg->lock, flags);
		/* Enable ACG feature in device mode,if supported */
		dwc2_enable_acg(hsotg);
		dwc2_hsotg_core_connect(hsotg);
	} else {
host:
		/* A-Device connector (Host Mode) */
		dev_dbg(hsotg->dev, "connId A\n");
		while (!dwc2_is_host_mode(hsotg)) {
			dev_info(hsotg->dev, "Waiting for Host Mode, Mode=%s\n",
				 dwc2_is_host_mode(hsotg) ?
				 "Host" : "Peripheral");
			msleep(20);
			if (++count > 250)
				break;
		}
		if (count > 250)
			dev_err(hsotg->dev,
				"Connection id status change timed out\n");

		spin_lock_irqsave(&hsotg->lock, flags);
		dwc2_hsotg_disconnect(hsotg);
		spin_unlock_irqrestore(&hsotg->lock, flags);

		hsotg->op_state = OTG_STATE_A_HOST;
		/* Initialize the Core for Host mode */
		dwc2_core_init(hsotg, false);
		dwc2_enable_global_interrupts(hsotg);
		dwc2_hcd_start(hsotg);
	}
}

static void dwc2_wakeup_detected(struct timer_list *t)
{
	struct dwc2_hsotg *hsotg = from_timer(hsotg, t, wkp_timer);
	u32 hprt0;

	dev_dbg(hsotg->dev, "%s()\n", __func__);

	/*
	 * Clear the Resume after 70ms. (Need 20 ms minimum. Use 70 ms
	 * so that OPT tests pass with all PHYs.)
	 */
	hprt0 = dwc2_read_hprt0(hsotg);
	dev_dbg(hsotg->dev, "Resume: HPRT0=%0x\n", hprt0);
	hprt0 &= ~HPRT0_RES;
	dwc2_writel(hsotg, hprt0, HPRT0);
	dev_dbg(hsotg->dev, "Clear Resume: HPRT0=%0x\n",
		dwc2_readl(hsotg, HPRT0));

	dwc2_hcd_rem_wakeup(hsotg);
	hsotg->bus_suspended = false;

	/* Change to L0 state */
	hsotg->lx_state = DWC2_L0;
}

static int dwc2_host_is_b_hnp_enabled(struct dwc2_hsotg *hsotg)
{
	struct usb_hcd *hcd = dwc2_hsotg_to_hcd(hsotg);

	return hcd->self.b_hnp_enable;
}

/**
 * dwc2_port_suspend() - Put controller in suspend mode for host.
 *
 * @hsotg: Programming view of the DWC_otg controller
 * @windex: The control request wIndex field
 *
 * Return: non-zero if failed to enter suspend mode for host.
 *
 * This function is for entering Host mode suspend.
 * Must NOT be called with interrupt disabled or spinlock held.
 */
int dwc2_port_suspend(struct dwc2_hsotg *hsotg, u16 windex)
{
	unsigned long flags;
	u32 pcgctl;
	u32 gotgctl;
	int ret = 0;

	dev_dbg(hsotg->dev, "%s()\n", __func__);

	spin_lock_irqsave(&hsotg->lock, flags);

	if (windex == hsotg->otg_port && dwc2_host_is_b_hnp_enabled(hsotg)) {
		gotgctl = dwc2_readl(hsotg, GOTGCTL);
		gotgctl |= GOTGCTL_HSTSETHNPEN;
		dwc2_writel(hsotg, gotgctl, GOTGCTL);
		hsotg->op_state = OTG_STATE_A_SUSPEND;
	}

	switch (hsotg->params.power_down) {
	case DWC2_POWER_DOWN_PARAM_PARTIAL:
		ret = dwc2_enter_partial_power_down(hsotg);
		if (ret)
			dev_err(hsotg->dev,
				"enter partial_power_down failed.\n");
		break;
	case DWC2_POWER_DOWN_PARAM_HIBERNATION:
		/*
		 * Perform spin unlock and lock because in
		 * "dwc2_host_enter_hibernation()" function there is a spinlock
		 * logic which prevents servicing of any IRQ during entering
		 * hibernation.
		 */
		spin_unlock_irqrestore(&hsotg->lock, flags);
		ret = dwc2_enter_hibernation(hsotg, 1);
		if (ret)
			dev_err(hsotg->dev, "enter hibernation failed.\n");
		spin_lock_irqsave(&hsotg->lock, flags);
		break;
	case DWC2_POWER_DOWN_PARAM_NONE:
		/*
		 * If not hibernation nor partial power down are supported,
		 * clock gating is used to save power.
		 */
		if (!hsotg->params.no_clock_gating)
			dwc2_host_enter_clock_gating(hsotg);
		break;
	}

	/* For HNP the bus must be suspended for at least 200ms */
	if (dwc2_host_is_b_hnp_enabled(hsotg)) {
		pcgctl = dwc2_readl(hsotg, PCGCTL);
		pcgctl &= ~PCGCTL_STOPPCLK;
		dwc2_writel(hsotg, pcgctl, PCGCTL);

		spin_unlock_irqrestore(&hsotg->lock, flags);

		msleep(200);
	} else {
		spin_unlock_irqrestore(&hsotg->lock, flags);
	}

	return ret;
}

/**
 * dwc2_port_resume() - Exit controller from suspend mode for host.
 *
 * @hsotg: Programming view of the DWC_otg controller
 *
 * Return: non-zero if failed to exit suspend mode for host.
 *
 * This function is for exiting Host mode suspend.
 * Must NOT be called with interrupt disabled or spinlock held.
 */
int dwc2_port_resume(struct dwc2_hsotg *hsotg)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&hsotg->lock, flags);

	switch (hsotg->params.power_down) {
	case DWC2_POWER_DOWN_PARAM_PARTIAL:
		ret = dwc2_exit_partial_power_down(hsotg, 0, true);
		if (ret)
			dev_err(hsotg->dev,
				"exit partial_power_down failed.\n");
		break;
	case DWC2_POWER_DOWN_PARAM_HIBERNATION:
		/* Exit host hibernation. */
		ret = dwc2_exit_hibernation(hsotg, 0, 0, 1);
		if (ret)
			dev_err(hsotg->dev, "exit hibernation failed.\n");
		break;
	case DWC2_POWER_DOWN_PARAM_NONE:
		/*
		 * If not hibernation nor partial power down are supported,
		 * port resume is done using the clock gating programming flow.
		 */
		spin_unlock_irqrestore(&hsotg->lock, flags);
		dwc2_host_exit_clock_gating(hsotg, 0);
		spin_lock_irqsave(&hsotg->lock, flags);
		break;
	}

	spin_unlock_irqrestore(&hsotg->lock, flags);

	return ret;
}

/* Handles hub class-specific requests */
static int dwc2_hcd_hub_control(struct dwc2_hsotg *hsotg, u16 typereq,
				u16 wvalue, u16 windex, char *buf, u16 wlength)
{
	struct usb_hub_descriptor *hub_desc;
	int retval = 0;
	u32 hprt0;
	u32 port_status;
	u32 speed;
	u32 pcgctl;
	u32 pwr;

	switch (typereq) {
	case ClearHubFeature:
		dev_dbg(hsotg->dev, "ClearHubFeature %1xh\n", wvalue);

		switch (wvalue) {
		case C_HUB_LOCAL_POWER:
		case C_HUB_OVER_CURRENT:
			/* Nothing required here */
			break;

		default:
			retval = -EINVAL;
			dev_err(hsotg->dev,
				"ClearHubFeature request %1xh unknown\n",
				wvalue);
		}
		break;

	case ClearPortFeature:
		if (wvalue != USB_PORT_FEAT_L1)
			if (!windex || windex > 1)
				goto error;
		switch (wvalue) {
		case USB_PORT_FEAT_ENABLE:
			dev_dbg(hsotg->dev,
				"ClearPortFeature USB_PORT_FEAT_ENABLE\n");
			hprt0 = dwc2_read_hprt0(hsotg);
			hprt0 |= HPRT0_ENA;
			dwc2_writel(hsotg, hprt0, HPRT0);
			break;

		case USB_PORT_FEAT_SUSPEND:
			dev_dbg(hsotg->dev,
				"ClearPortFeature USB_PORT_FEAT_SUSPEND\n");

			if (hsotg->bus_suspended)
				retval = dwc2_port_resume(hsotg);
			break;

		case USB_PORT_FEAT_POWER:
			dev_dbg(hsotg->dev,
				"ClearPortFeature USB_PORT_FEAT_POWER\n");
			hprt0 = dwc2_read_hprt0(hsotg);
			pwr = hprt0 & HPRT0_PWR;
			hprt0 &= ~HPRT0_PWR;
			dwc2_writel(hsotg, hprt0, HPRT0);
			if (pwr)
				dwc2_vbus_supply_exit(hsotg);
			break;

		case USB_PORT_FEAT_INDICATOR:
			dev_dbg(hsotg->dev,
				"ClearPortFeature USB_PORT_FEAT_INDICATOR\n");
			/* Port indicator not supported */
			break;

		case USB_PORT_FEAT_C_CONNECTION:
			/*
			 * Clears driver's internal Connect Status Change flag
			 */
			dev_dbg(hsotg->dev,
				"ClearPortFeature USB_PORT_FEAT_C_CONNECTION\n");
			hsotg->flags.b.port_connect_status_change = 0;
			break;

		case USB_PORT_FEAT_C_RESET:
			/* Clears driver's internal Port Reset Change flag */
			dev_dbg(hsotg->dev,
				"ClearPortFeature USB_PORT_FEAT_C_RESET\n");
			hsotg->flags.b.port_reset_change = 0;
			break;

		case USB_PORT_FEAT_C_ENABLE:
			/*
			 * Clears the driver's internal Port Enable/Disable
			 * Change flag
			 */
			dev_dbg(hsotg->dev,
				"ClearPortFeature USB_PORT_FEAT_C_ENABLE\n");
			hsotg->flags.b.port_enable_change = 0;
			break;

		case USB_PORT_FEAT_C_SUSPEND:
			/*
			 * Clears the driver's internal Port Suspend Change
			 * flag, which is set when resume signaling on the host
			 * port is complete
			 */
			dev_dbg(hsotg->dev,
				"ClearPortFeature USB_PORT_FEAT_C_SUSPEND\n");
			hsotg->flags.b.port_suspend_change = 0;
			break;

		case USB_PORT_FEAT_C_PORT_L1:
			dev_dbg(hsotg->dev,
				"ClearPortFeature USB_PORT_FEAT_C_PORT_L1\n");
			hsotg->flags.b.port_l1_change = 0;
			break;

		case USB_PORT_FEAT_C_OVER_CURRENT:
			dev_dbg(hsotg->dev,
				"ClearPortFeature USB_PORT_FEAT_C_OVER_CURRENT\n");
			hsotg->flags.b.port_over_current_change = 0;
			break;

		default:
			retval = -EINVAL;
			dev_err(hsotg->dev,
				"ClearPortFeature request %1xh unknown or unsupported\n",
				wvalue);
		}
		break;

	case GetHubDescriptor:
		dev_dbg(hsotg->dev, "GetHubDescriptor\n");
		hub_desc = (struct usb_hub_descriptor *)buf;
		hub_desc->bDescLength = 9;
		hub_desc->bDescriptorType = USB_DT_HUB;
		hub_desc->bNbrPorts = 1;
		hub_desc->wHubCharacteristics =
			cpu_to_le16(HUB_CHAR_COMMON_LPSM |
				    HUB_CHAR_INDV_PORT_OCPM);
		hub_desc->bPwrOn2PwrGood = 1;
		hub_desc->bHubContrCurrent = 0;
		hub_desc->u.hs.DeviceRemovable[0] = 0;
		hub_desc->u.hs.DeviceRemovable[1] = 0xff;
		break;

	case GetHubStatus:
		dev_dbg(hsotg->dev, "GetHubStatus\n");
		memset(buf, 0, 4);
		break;

	case GetPortStatus:
		dev_vdbg(hsotg->dev,
			 "GetPortStatus wIndex=0x%04x flags=0x%08x\n", windex,
			 hsotg->flags.d32);
		if (!windex || windex > 1)
			goto error;

		port_status = 0;
		if (hsotg->flags.b.port_connect_status_change)
			port_status |= USB_PORT_STAT_C_CONNECTION << 16;
		if (hsotg->flags.b.port_enable_change)
			port_status |= USB_PORT_STAT_C_ENABLE << 16;
		if (hsotg->flags.b.port_suspend_change)
			port_status |= USB_PORT_STAT_C_SUSPEND << 16;
		if (hsotg->flags.b.port_l1_change)
			port_status |= USB_PORT_STAT_C_L1 << 16;
		if (hsotg->flags.b.port_reset_change)
			port_status |= USB_PORT_STAT_C_RESET << 16;
		if (hsotg->flags.b.port_over_current_change) {
			dev_warn(hsotg->dev, "Overcurrent change detected\n");
			port_status |= USB_PORT_STAT_C_OVERCURRENT << 16;
		}

		if (!hsotg->flags.b.port_connect_status) {
			/*
			 * The port is disconnected, which means the core is
			 * either in device mode or it soon will be. Just
			 * return 0's for the remainder of the port status
			 * since the port register can't be read if the core
			 * is in device mode.
			 */
			*(__le32 *)buf = cpu_to_le32(port_status);
			break;
		}

		hprt0 = dwc2_readl(hsotg, HPRT0);
		dev_vdbg(hsotg->dev, "  HPRT0: 0x%08x\n", hprt0);

		if (hprt0 & HPRT0_CONNSTS)
			port_status |= USB_PORT_STAT_CONNECTION;
		if (hprt0 & HPRT0_ENA)
			port_status |= USB_PORT_STAT_ENABLE;
		if (hprt0 & HPRT0_SUSP)
			port_status |= USB_PORT_STAT_SUSPEND;
		if (hprt0 & HPRT0_OVRCURRACT)
			port_status |= USB_PORT_STAT_OVERCURRENT;
		if (hprt0 & HPRT0_RST)
			port_status |= USB_PORT_STAT_RESET;
		if (hprt0 & HPRT0_PWR)
			port_status |= USB_PORT_STAT_POWER;

		speed = (hprt0 & HPRT0_SPD_MASK) >> HPRT0_SPD_SHIFT;
		if (speed == HPRT0_SPD_HIGH_SPEED)
			port_status |= USB_PORT_STAT_HIGH_SPEED;
		else if (speed == HPRT0_SPD_LOW_SPEED)
			port_status |= USB_PORT_STAT_LOW_SPEED;

		if (hprt0 & HPRT0_TSTCTL_MASK)
			port_status |= USB_PORT_STAT_TEST;
		/* USB_PORT_FEAT_INDICATOR unsupported always 0 */

		if (hsotg->params.dma_desc_fs_enable) {
			/*
			 * Enable descriptor DMA only if a full speed
			 * device is connected.
			 */
			if (hsotg->new_connection &&
			    ((port_status &
			      (USB_PORT_STAT_CONNECTION |
			       USB_PORT_STAT_HIGH_SPEED |
			       USB_PORT_STAT_LOW_SPEED)) ==
			       USB_PORT_STAT_CONNECTION)) {
				u32 hcfg;

				dev_info(hsotg->dev, "Enabling descriptor DMA mode\n");
				hsotg->params.dma_desc_enable = true;
				hcfg = dwc2_readl(hsotg, HCFG);
				hcfg |= HCFG_DESCDMA;
				dwc2_writel(hsotg, hcfg, HCFG);
				hsotg->new_connection = false;
			}
		}

		dev_vdbg(hsotg->dev, "port_status=%08x\n", port_status);
		*(__le32 *)buf = cpu_to_le32(port_status);
		break;

	case SetHubFeature:
		dev_dbg(hsotg->dev, "SetHubFeature\n");
		/* No HUB features supported */
		break;

	case SetPortFeature:
		dev_dbg(hsotg->dev, "SetPortFeature\n");
		if (wvalue != USB_PORT_FEAT_TEST && (!windex || windex > 1))
			goto error;

		if (!hsotg->flags.b.port_connect_status) {
			/*
			 * The port is disconnected, which means the core is
			 * either in device mode or it soon will be. Just
			 * return without doing anything since the port
			 * register can't be written if the core is in device
			 * mode.
			 */
			break;
		}

		switch (wvalue) {
		case USB_PORT_FEAT_SUSPEND:
			dev_dbg(hsotg->dev,
				"SetPortFeature - USB_PORT_FEAT_SUSPEND\n");
			if (windex != hsotg->otg_port)
				goto error;
			if (!hsotg->bus_suspended)
				retval = dwc2_port_suspend(hsotg, windex);
			break;

		case USB_PORT_FEAT_POWER:
			dev_dbg(hsotg->dev,
				"SetPortFeature - USB_PORT_FEAT_POWER\n");
			hprt0 = dwc2_read_hprt0(hsotg);
			pwr = hprt0 & HPRT0_PWR;
			hprt0 |= HPRT0_PWR;
			dwc2_writel(hsotg, hprt0, HPRT0);
			if (!pwr)
				dwc2_vbus_supply_init(hsotg);
			break;

		case USB_PORT_FEAT_RESET:
			dev_dbg(hsotg->dev,
				"SetPortFeature - USB_PORT_FEAT_RESET\n");

			hprt0 = dwc2_read_hprt0(hsotg);

			if (hsotg->hibernated) {
				retval = dwc2_exit_hibernation(hsotg, 0, 1, 1);
				if (retval)
					dev_err(hsotg->dev,
						"exit hibernation failed\n");
			}

			if (hsotg->in_ppd) {
				retval = dwc2_exit_partial_power_down(hsotg, 1,
								      true);
				if (retval)
					dev_err(hsotg->dev,
						"exit partial_power_down failed\n");
			}

			if (hsotg->params.power_down ==
			    DWC2_POWER_DOWN_PARAM_NONE && hsotg->bus_suspended)
				dwc2_host_exit_clock_gating(hsotg, 0);

			pcgctl = dwc2_readl(hsotg, PCGCTL);
			pcgctl &= ~(PCGCTL_ENBL_SLEEP_GATING | PCGCTL_STOPPCLK);
			dwc2_writel(hsotg, pcgctl, PCGCTL);
			/* ??? Original driver does this */
			dwc2_writel(hsotg, 0, PCGCTL);

			hprt0 = dwc2_read_hprt0(hsotg);
			pwr = hprt0 & HPRT0_PWR;
			/* Clear suspend bit if resetting from suspend state */
			hprt0 &= ~HPRT0_SUSP;

			/*
			 * When B-Host the Port reset bit is set in the Start
			 * HCD Callback function, so that the reset is started
			 * within 1ms of the HNP success interrupt
			 */
			if (!dwc2_hcd_is_b_host(hsotg)) {
				hprt0 |= HPRT0_PWR | HPRT0_RST;
				dev_dbg(hsotg->dev,
					"In host mode, hprt0=%08x\n", hprt0);
				dwc2_writel(hsotg, hprt0, HPRT0);
				if (!pwr)
					dwc2_vbus_supply_init(hsotg);
			}

			/* Clear reset bit in 10ms (FS/LS) or 50ms (HS) */
			msleep(50);
			hprt0 &= ~HPRT0_RST;
			dwc2_writel(hsotg, hprt0, HPRT0);
			hsotg->lx_state = DWC2_L0; /* Now back to On state */
			break;

		case USB_PORT_FEAT_INDICATOR:
			dev_dbg(hsotg->dev,
				"SetPortFeature - USB_PORT_FEAT_INDICATOR\n");
			/* Not supported */
			break;

		case USB_PORT_FEAT_TEST:
			hprt0 = dwc2_read_hprt0(hsotg);
			dev_dbg(hsotg->dev,
				"SetPortFeature - USB_PORT_FEAT_TEST\n");
			hprt0 &= ~HPRT0_TSTCTL_MASK;
			hprt0 |= (windex >> 8) << HPRT0_TSTCTL_SHIFT;
			dwc2_writel(hsotg, hprt0, HPRT0);
			break;

		default:
			retval = -EINVAL;
			dev_err(hsotg->dev,
				"SetPortFeature %1xh unknown or unsupported\n",
				wvalue);
			break;
		}
		break;

	default:
error:
		retval = -EINVAL;
		dev_dbg(hsotg->dev,
			"Unknown hub control request: %1xh wIndex: %1xh wValue: %1xh\n",
			typereq, windex, wvalue);
		break;
	}

	return retval;
}

static int dwc2_hcd_is_status_changed(struct dwc2_hsotg *hsotg, int port)
{
	int retval;

	if (port != 1)
		return -EINVAL;

	retval = (hsotg->flags.b.port_connect_status_change ||
		  hsotg->flags.b.port_reset_change ||
		  hsotg->flags.b.port_enable_change ||
		  hsotg->flags.b.port_suspend_change ||
		  hsotg->flags.b.port_over_current_change);

	if (retval) {
		dev_dbg(hsotg->dev,
			"DWC OTG HCD HUB STATUS DATA: Root port status changed\n");
		dev_dbg(hsotg->dev, "  port_connect_status_change: %d\n",
			hsotg->flags.b.port_connect_status_change);
		dev_dbg(hsotg->dev, "  port_reset_change: %d\n",
			hsotg->flags.b.port_reset_change);
		dev_dbg(hsotg->dev, "  port_enable_change: %d\n",
			hsotg->flags.b.port_enable_change);
		dev_dbg(hsotg->dev, "  port_suspend_change: %d\n",
			hsotg->flags.b.port_suspend_change);
		dev_dbg(hsotg->dev, "  port_over_current_change: %d\n",
			hsotg->flags.b.port_over_current_change);
	}

	return retval;
}

int dwc2_hcd_get_frame_number(struct dwc2_hsotg *hsotg)
{
	u32 hfnum = dwc2_readl(hsotg, HFNUM);

#ifdef DWC2_DEBUG_SOF
	dev_vdbg(hsotg->dev, "DWC OTG HCD GET FRAME NUMBER %d\n",
		 (hfnum & HFNUM_FRNUM_MASK) >> HFNUM_FRNUM_SHIFT);
#endif
	return (hfnum & HFNUM_FRNUM_MASK) >> HFNUM_FRNUM_SHIFT;
}

int dwc2_hcd_get_future_frame_number(struct dwc2_hsotg *hsotg, int us)
{
	u32 hprt = dwc2_readl(hsotg, HPRT0);
	u32 hfir = dwc2_readl(hsotg, HFIR);
	u32 hfnum = dwc2_readl(hsotg, HFNUM);
	unsigned int us_per_frame;
	unsigned int frame_number;
	unsigned int remaining;
	unsigned int interval;
	unsigned int phy_clks;

	/* High speed has 125 us per (micro) frame; others are 1 ms per */
	us_per_frame = (hprt & HPRT0_SPD_MASK) ? 1000 : 125;

	/* Extract fields */
	frame_number = (hfnum & HFNUM_FRNUM_MASK) >> HFNUM_FRNUM_SHIFT;
	remaining = (hfnum & HFNUM_FRREM_MASK) >> HFNUM_FRREM_SHIFT;
	interval = (hfir & HFIR_FRINT_MASK) >> HFIR_FRINT_SHIFT;

	/*
	 * Number of phy clocks since the last tick of the frame number after
	 * "us" has passed.
	 */
	phy_clks = (interval - remaining) +
		   DIV_ROUND_UP(interval * us, us_per_frame);

	return dwc2_frame_num_inc(frame_number, phy_clks / interval);
}

int dwc2_hcd_is_b_host(struct dwc2_hsotg *hsotg)
{
	return hsotg->op_state == OTG_STATE_B_HOST;
}

static struct dwc2_hcd_urb *dwc2_hcd_urb_alloc(struct dwc2_hsotg *hsotg,
					       int iso_desc_count,
					       gfp_t mem_flags)
{
	struct dwc2_hcd_urb *urb;

	urb = kzalloc(struct_size(urb, iso_descs, iso_desc_count), mem_flags);
	if (urb)
		urb->packet_count = iso_desc_count;
	return urb;
}

static void dwc2_hcd_urb_set_pipeinfo(struct dwc2_hsotg *hsotg,
				      struct dwc2_hcd_urb *urb, u8 dev_addr,
				      u8 ep_num, u8 ep_type, u8 ep_dir,
				      u16 maxp, u16 maxp_mult)
{
	if (dbg_perio() ||
	    ep_type == USB_ENDPOINT_XFER_BULK ||
	    ep_type == USB_ENDPOINT_XFER_CONTROL)
		dev_vdbg(hsotg->dev,
			 "addr=%d, ep_num=%d, ep_dir=%1x, ep_type=%1x, maxp=%d (%d mult)\n",
			 dev_addr, ep_num, ep_dir, ep_type, maxp, maxp_mult);
	urb->pipe_info.dev_addr = dev_addr;
	urb->pipe_info.ep_num = ep_num;
	urb->pipe_info.pipe_type = ep_type;
	urb->pipe_info.pipe_dir = ep_dir;
	urb->pipe_info.maxp = maxp;
	urb->pipe_info.maxp_mult = maxp_mult;
}

/*
 * NOTE: This function will be removed once the peripheral controller code
 * is integrated and the driver is stable
 */
void dwc2_hcd_dump_state(struct dwc2_hsotg *hsotg)
{
#ifdef DEBUG
	struct dwc2_host_chan *chan;
	struct dwc2_hcd_urb *urb;
	struct dwc2_qtd *qtd;
	int num_channels;
	u32 np_tx_status;
	u32 p_tx_status;
	int i;

	num_channels = hsotg->params.host_channels;
	dev_dbg(hsotg->dev, "\n");
	dev_dbg(hsotg->dev,
		"************************************************************\n");
	dev_dbg(hsotg->dev, "HCD State:\n");
	dev_dbg(hsotg->dev, "  Num channels: %d\n", num_channels);

	for (i = 0; i < num_channels; i++) {
		chan = hsotg->hc_ptr_array[i];
		dev_dbg(hsotg->dev, "  Channel %d:\n", i);
		dev_dbg(hsotg->dev,
			"    dev_addr: %d, ep_num: %d, ep_is_in: %d\n",
			chan->dev_addr, chan->ep_num, chan->ep_is_in);
		dev_dbg(hsotg->dev, "    speed: %d\n", chan->speed);
		dev_dbg(hsotg->dev, "    ep_type: %d\n", chan->ep_type);
		dev_dbg(hsotg->dev, "    max_packet: %d\n", chan->max_packet);
		dev_dbg(hsotg->dev, "    data_pid_start: %d\n",
			chan->data_pid_start);
		dev_dbg(hsotg->dev, "    multi_count: %d\n", chan->multi_count);
		dev_dbg(hsotg->dev, "    xfer_started: %d\n",
			chan->xfer_started);
		dev_dbg(hsotg->dev, "    xfer_buf: %p\n", chan->xfer_buf);
		dev_dbg(hsotg->dev, "    xfer_dma: %08lx\n",
			(unsigned long)chan->xfer_dma);
		dev_dbg(hsotg->dev, "    xfer_len: %d\n", chan->xfer_len);
		dev_dbg(hsotg->dev, "    xfer_count: %d\n", chan->xfer_count);
		dev_dbg(hsotg->dev, "    halt_on_queue: %d\n",
			chan->halt_on_queue);
		dev_dbg(hsotg->dev, "    halt_pending: %d\n",
			chan->halt_pending);
		dev_dbg(hsotg->dev, "    halt_status: %d\n", chan->halt_status);
		dev_dbg(hsotg->dev, "    do_split: %d\n", chan->do_split);
		dev_dbg(hsotg->dev, "    complete_split: %d\n",
			chan->complete_split);
		dev_dbg(hsotg->dev, "    hub_addr: %d\n", chan->hub_addr);
		dev_dbg(hsotg->dev, "    hub_port: %d\n", chan->hub_port);
		dev_dbg(hsotg->dev, "    xact_pos: %d\n", chan->xact_pos);
		dev_dbg(hsotg->dev, "    requests: %d\n", chan->requests);
		dev_dbg(hsotg->dev, "    qh: %p\n", chan->qh);

		if (chan->xfer_started) {
			u32 hfnum, hcchar, hctsiz, hcint, hcintmsk;

			hfnum = dwc2_readl(hsotg, HFNUM);
			hcchar = dwc2_readl(hsotg, HCCHAR(i));
			hctsiz = dwc2_readl(hsotg, HCTSIZ(i));
			hcint = dwc2_readl(hsotg, HCINT(i));
			hcintmsk = dwc2_readl(hsotg, HCINTMSK(i));
			dev_dbg(hsotg->dev, "    hfnum: 0x%08x\n", hfnum);
			dev_dbg(hsotg->dev, "    hcchar: 0x%08x\n", hcchar);
			dev_dbg(hsotg->dev, "    hctsiz: 0x%08x\n", hctsiz);
			dev_dbg(hsotg->dev, "    hcint: 0x%08x\n", hcint);
			dev_dbg(hsotg->dev, "    hcintmsk: 0x%08x\n", hcintmsk);
		}

		if (!(chan->xfer_started && chan->qh))
			continue;

		list_for_each_entry(qtd, &chan->qh->qtd_list, qtd_list_entry) {
			if (!qtd->in_process)
				break;
			urb = qtd->urb;
			dev_dbg(hsotg->dev, "    URB Info:\n");
			dev_dbg(hsotg->dev, "      qtd: %p, urb: %p\n",
				qtd, urb);
			if (urb) {
				dev_dbg(hsotg->dev,
					"      Dev: %d, EP: %d %s\n",
					dwc2_hcd_get_dev_addr(&urb->pipe_info),
					dwc2_hcd_get_ep_num(&urb->pipe_info),
					dwc2_hcd_is_pipe_in(&urb->pipe_info) ?
					"IN" : "OUT");
				dev_dbg(hsotg->dev,
					"      Max packet size: %d (%d mult)\n",
					dwc2_hcd_get_maxp(&urb->pipe_info),
					dwc2_hcd_get_maxp_mult(&urb->pipe_info));
				dev_dbg(hsotg->dev,
					"      transfer_buffer: %p\n",
					urb->buf);
				dev_dbg(hsotg->dev,
					"      transfer_dma: %08lx\n",
					(unsigned long)urb->dma);
				dev_dbg(hsotg->dev,
					"      transfer_buffer_length: %d\n",
					urb->length);
				dev_dbg(hsotg->dev, "      actual_length: %d\n",
					urb->actual_length);
			}
		}
	}

	dev_dbg(hsotg->dev, "  non_periodic_channels: %d\n",
		hsotg->non_periodic_channels);
	dev_dbg(hsotg->dev, "  periodic_channels: %d\n",
		hsotg->periodic_channels);
	dev_dbg(hsotg->dev, "  periodic_usecs: %d\n", hsotg->periodic_usecs);
	np_tx_status = dwc2_readl(hsotg, GNPTXSTS);
	dev_dbg(hsotg->dev, "  NP Tx Req Queue Space Avail: %d\n",
		(np_tx_status & TXSTS_QSPCAVAIL_MASK) >> TXSTS_QSPCAVAIL_SHIFT);
	dev_dbg(hsotg->dev, "  NP Tx FIFO Space Avail: %d\n",
		(np_tx_status & TXSTS_FSPCAVAIL_MASK) >> TXSTS_FSPCAVAIL_SHIFT);
	p_tx_status = dwc2_readl(hsotg, HPTXSTS);
	dev_dbg(hsotg->dev, "  P Tx Req Queue Space Avail: %d\n",
		(p_tx_status & TXSTS_QSPCAVAIL_MASK) >> TXSTS_QSPCAVAIL_SHIFT);
	dev_dbg(hsotg->dev, "  P Tx FIFO Space Avail: %d\n",
		(p_tx_status & TXSTS_FSPCAVAIL_MASK) >> TXSTS_FSPCAVAIL_SHIFT);
	dwc2_dump_global_registers(hsotg);
	dwc2_dump_host_registers(hsotg);
	dev_dbg(hsotg->dev,
		"************************************************************\n");
	dev_dbg(hsotg->dev, "\n");
#endif
}

struct wrapper_priv_data {
	struct dwc2_hsotg *hsotg;
};

/* Gets the dwc2_hsotg from a usb_hcd */
static struct dwc2_hsotg *dwc2_hcd_to_hsotg(struct usb_hcd *hcd)
{
	struct wrapper_priv_data *p;

	p = (struct wrapper_priv_data *)&hcd->hcd_priv;
	return p->hsotg;
}

/**
 * dwc2_host_get_tt_info() - Get the dwc2_tt associated with context
 *
 * This will get the dwc2_tt structure (and ttport) associated with the given
 * context (which is really just a struct urb pointer).
 *
 * The first time this is called for a given TT we allocate memory for our
 * structure.  When everyone is done and has called dwc2_host_put_tt_info()
 * then the refcount for the structure will go to 0 and we'll free it.
 *
 * @hsotg:     The HCD state structure for the DWC OTG controller.
 * @context:   The priv pointer from a struct dwc2_hcd_urb.
 * @mem_flags: Flags for allocating memory.
 * @ttport:    We'll return this device's port number here.  That's used to
 *             reference into the bitmap if we're on a multi_tt hub.
 *
 * Return: a pointer to a struct dwc2_tt.  Don't forget to call
 *         dwc2_host_put_tt_info()!  Returns NULL upon memory alloc failure.
 */

struct dwc2_tt *dwc2_host_get_tt_info(struct dwc2_hsotg *hsotg, void *context,
				      gfp_t mem_flags, int *ttport)
{
	struct urb *urb = context;
	struct dwc2_tt *dwc_tt = NULL;

	if (urb->dev->tt) {
		*ttport = urb->dev->ttport;

		dwc_tt = urb->dev->tt->hcpriv;
		if (!dwc_tt) {
			size_t bitmap_size;

			/*
			 * For single_tt we need one schedule.  For multi_tt
			 * we need one per port.
			 */
			bitmap_size = DWC2_ELEMENTS_PER_LS_BITMAP *
				      sizeof(dwc_tt->periodic_bitmaps[0]);
			if (urb->dev->tt->multi)
				bitmap_size *= urb->dev->tt->hub->maxchild;

			dwc_tt = kzalloc(sizeof(*dwc_tt) + bitmap_size,
					 mem_flags);
			if (!dwc_tt)
				return NULL;

			dwc_tt->usb_tt = urb->dev->tt;
			dwc_tt->usb_tt->hcpriv = dwc_tt;
		}

		dwc_tt->refcount++;
	}

	return dwc_tt;
}

/**
 * dwc2_host_put_tt_info() - Put the dwc2_tt from dwc2_host_get_tt_info()
 *
 * Frees resources allocated by dwc2_host_get_tt_info() if all current holders
 * of the structure are done.
 *
 * It's OK to call this with NULL.
 *
 * @hsotg:     The HCD state structure for the DWC OTG controller.
 * @dwc_tt:    The pointer returned by dwc2_host_get_tt_info.
 */
void dwc2_host_put_tt_info(struct dwc2_hsotg *hsotg, struct dwc2_tt *dwc_tt)
{
	/* Model kfree and make put of NULL a no-op */
	if (!dwc_tt)
		return;

	WARN_ON(dwc_tt->refcount < 1);

	dwc_tt->refcount--;
	if (!dwc_tt->refcount) {
		dwc_tt->usb_tt->hcpriv = NULL;
		kfree(dwc_tt);
	}
}

int dwc2_host_get_speed(struct dwc2_hsotg *hsotg, void *context)
{
	struct urb *urb = context;

	return urb->dev->speed;
}

static void dwc2_allocate_bus_bandwidth(struct usb_hcd *hcd, u16 bw,
					struct urb *urb)
{
	struct usb_bus *bus = hcd_to_bus(hcd);

	if (urb->interval)
		bus->bandwidth_allocated += bw / urb->interval;
	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS)
		bus->bandwidth_isoc_reqs++;
	else
		bus->bandwidth_int_reqs++;
}

static void dwc2_free_bus_bandwidth(struct usb_hcd *hcd, u16 bw,
				    struct urb *urb)
{
	struct usb_bus *bus = hcd_to_bus(hcd);

	if (urb->interval)
		bus->bandwidth_allocated -= bw / urb->interval;
	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS)
		bus->bandwidth_isoc_reqs--;
	else
		bus->bandwidth_int_reqs--;
}

/*
 * Sets the final status of an URB and returns it to the upper layer. Any
 * required cleanup of the URB is performed.
 *
 * Must be called with interrupt disabled and spinlock held
 */
void dwc2_host_complete(struct dwc2_hsotg *hsotg, struct dwc2_qtd *qtd,
			int status)
{
	struct urb *urb;
	int i;

	if (!qtd) {
		dev_dbg(hsotg->dev, "## %s: qtd is NULL ##\n", __func__);
		return;
	}

	if (!qtd->urb) {
		dev_dbg(hsotg->dev, "## %s: qtd->urb is NULL ##\n", __func__);
		return;
	}

	urb = qtd->urb->priv;
	if (!urb) {
		dev_dbg(hsotg->dev, "## %s: urb->priv is NULL ##\n", __func__);
		return;
	}

	urb->actual_length = dwc2_hcd_urb_get_actual_length(qtd->urb);

	if (dbg_urb(urb))
		dev_vdbg(hsotg->dev,
			 "%s: urb %p device %d ep %d-%s status %d actual %d\n",
			 __func__, urb, usb_pipedevice(urb->pipe),
			 usb_pipeendpoint(urb->pipe),
			 usb_pipein(urb->pipe) ? "IN" : "OUT", status,
			 urb->actual_length);

	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {
		urb->error_count = dwc2_hcd_urb_get_error_count(qtd->urb);
		for (i = 0; i < urb->number_of_packets; ++i) {
			urb->iso_frame_desc[i].actual_length =
				dwc2_hcd_urb_get_iso_desc_actual_length(
						qtd->urb, i);
			urb->iso_frame_desc[i].status =
				dwc2_hcd_urb_get_iso_desc_status(qtd->urb, i);
		}
	}

	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS && dbg_perio()) {
		for (i = 0; i < urb->number_of_packets; i++)
			dev_vdbg(hsotg->dev, " ISO Desc %d status %d\n",
				 i, urb->iso_frame_desc[i].status);
	}

	urb->status = status;
	if (!status) {
		if ((urb->transfer_flags & URB_SHORT_NOT_OK) &&
		    urb->actual_length < urb->transfer_buffer_length)
			urb->status = -EREMOTEIO;
	}

	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS ||
	    usb_pipetype(urb->pipe) == PIPE_INTERRUPT) {
		struct usb_host_endpoint *ep = urb->ep;

		if (ep)
			dwc2_free_bus_bandwidth(dwc2_hsotg_to_hcd(hsotg),
					dwc2_hcd_get_ep_bandwidth(hsotg, ep),
					urb);
	}

	usb_hcd_unlink_urb_from_ep(dwc2_hsotg_to_hcd(hsotg), urb);
	urb->hcpriv = NULL;
	kfree(qtd->urb);
	qtd->urb = NULL;

	usb_hcd_giveback_urb(dwc2_hsotg_to_hcd(hsotg), urb, status);
}

/*
 * Work queue function for starting the HCD when A-Cable is connected
 */
static void dwc2_hcd_start_func(struct work_struct *work)
{
	struct dwc2_hsotg *hsotg = container_of(work, struct dwc2_hsotg,
						start_work.work);

	dev_dbg(hsotg->dev, "%s() %p\n", __func__, hsotg);
	dwc2_host_start(hsotg);
}

/*
 * Reset work queue function
 */
static void dwc2_hcd_reset_func(struct work_struct *work)
{
	struct dwc2_hsotg *hsotg = container_of(work, struct dwc2_hsotg,
						reset_work.work);
	unsigned long flags;
	u32 hprt0;

	dev_dbg(hsotg->dev, "USB RESET function called\n");

	spin_lock_irqsave(&hsotg->lock, flags);

	hprt0 = dwc2_read_hprt0(hsotg);
	hprt0 &= ~HPRT0_RST;
	dwc2_writel(hsotg, hprt0, HPRT0);
	hsotg->flags.b.port_reset_change = 1;

	spin_unlock_irqrestore(&hsotg->lock, flags);
}

static void dwc2_hcd_phy_reset_func(struct work_struct *work)
{
	struct dwc2_hsotg *hsotg = container_of(work, struct dwc2_hsotg,
						phy_reset_work);
	int ret;

	ret = phy_reset(hsotg->phy);
	if (ret)
		dev_warn(hsotg->dev, "PHY reset failed\n");
}

/*
 * =========================================================================
 *  Linux HC Driver Functions
 * =========================================================================
 */

/*
 * Initializes the DWC_otg controller and its root hub and prepares it for host
 * mode operation. Activates the root port. Returns 0 on success and a negative
 * error code on failure.
 */
static int _dwc2_hcd_start(struct usb_hcd *hcd)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);
	struct usb_bus *bus = hcd_to_bus(hcd);
	unsigned long flags;
	u32 hprt0;
	int ret;

	dev_dbg(hsotg->dev, "DWC OTG HCD START\n");

	spin_lock_irqsave(&hsotg->lock, flags);
	hsotg->lx_state = DWC2_L0;
	hcd->state = HC_STATE_RUNNING;
	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

	if (dwc2_is_device_mode(hsotg)) {
		spin_unlock_irqrestore(&hsotg->lock, flags);
		return 0;	/* why 0 ?? */
	}

	dwc2_hcd_reinit(hsotg);

	hprt0 = dwc2_read_hprt0(hsotg);
	/* Has vbus power been turned on in dwc2_core_host_init ? */
	if (hprt0 & HPRT0_PWR) {
		/* Enable external vbus supply before resuming root hub */
		spin_unlock_irqrestore(&hsotg->lock, flags);
		ret = dwc2_vbus_supply_init(hsotg);
		if (ret)
			return ret;
		spin_lock_irqsave(&hsotg->lock, flags);
	}

	/* Initialize and connect root hub if one is not already attached */
	if (bus->root_hub) {
		dev_dbg(hsotg->dev, "DWC OTG HCD Has Root Hub\n");
		/* Inform the HUB driver to resume */
		usb_hcd_resume_root_hub(hcd);
	}

	spin_unlock_irqrestore(&hsotg->lock, flags);

	return 0;
}

/*
 * Halts the DWC_otg host mode operations in a clean manner. USB transfers are
 * stopped.
 */
static void _dwc2_hcd_stop(struct usb_hcd *hcd)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);
	unsigned long flags;
	u32 hprt0;

	/* Turn off all host-specific interrupts */
	dwc2_disable_host_interrupts(hsotg);

	/* Wait for interrupt processing to finish */
	synchronize_irq(hcd->irq);

	spin_lock_irqsave(&hsotg->lock, flags);
	hprt0 = dwc2_read_hprt0(hsotg);
	/* Ensure hcd is disconnected */
	dwc2_hcd_disconnect(hsotg, true);
	dwc2_hcd_stop(hsotg);
	hsotg->lx_state = DWC2_L3;
	hcd->state = HC_STATE_HALT;
	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	spin_unlock_irqrestore(&hsotg->lock, flags);

	/* keep balanced supply init/exit by checking HPRT0_PWR */
	if (hprt0 & HPRT0_PWR)
		dwc2_vbus_supply_exit(hsotg);

	usleep_range(1000, 3000);
}

static int _dwc2_hcd_suspend(struct usb_hcd *hcd)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&hsotg->lock, flags);

	if (dwc2_is_device_mode(hsotg))
		goto unlock;

	if (hsotg->lx_state != DWC2_L0)
		goto unlock;

	if (!HCD_HW_ACCESSIBLE(hcd))
		goto unlock;

	if (hsotg->op_state == OTG_STATE_B_PERIPHERAL)
		goto unlock;

	if (hsotg->bus_suspended)
		goto skip_power_saving;

	if (hsotg->flags.b.port_connect_status == 0)
		goto skip_power_saving;

	switch (hsotg->params.power_down) {
	case DWC2_POWER_DOWN_PARAM_PARTIAL:
		/* Enter partial_power_down */
		ret = dwc2_enter_partial_power_down(hsotg);
		if (ret)
			dev_err(hsotg->dev,
				"enter partial_power_down failed\n");
		/* After entering suspend, hardware is not accessible */
		clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
		break;
	case DWC2_POWER_DOWN_PARAM_HIBERNATION:
		/* Enter hibernation */
		spin_unlock_irqrestore(&hsotg->lock, flags);
		ret = dwc2_enter_hibernation(hsotg, 1);
		if (ret)
			dev_err(hsotg->dev, "enter hibernation failed\n");
		spin_lock_irqsave(&hsotg->lock, flags);

		/* After entering suspend, hardware is not accessible */
		clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
		break;
	case DWC2_POWER_DOWN_PARAM_NONE:
		/*
		 * If not hibernation nor partial power down are supported,
		 * clock gating is used to save power.
		 */
		if (!hsotg->params.no_clock_gating)
			dwc2_host_enter_clock_gating(hsotg);

		/* After entering suspend, hardware is not accessible */
		clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
		break;
	default:
		goto skip_power_saving;
	}

	spin_unlock_irqrestore(&hsotg->lock, flags);
	dwc2_vbus_supply_exit(hsotg);
	spin_lock_irqsave(&hsotg->lock, flags);

	/* Ask phy to be suspended */
	if (!IS_ERR_OR_NULL(hsotg->uphy)) {
		spin_unlock_irqrestore(&hsotg->lock, flags);
		usb_phy_set_suspend(hsotg->uphy, true);
		spin_lock_irqsave(&hsotg->lock, flags);
	}

skip_power_saving:
	hsotg->lx_state = DWC2_L2;
unlock:
	spin_unlock_irqrestore(&hsotg->lock, flags);

	return ret;
}

static int _dwc2_hcd_resume(struct usb_hcd *hcd)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);
	unsigned long flags;
	u32 hprt0;
	int ret = 0;

	spin_lock_irqsave(&hsotg->lock, flags);

	if (dwc2_is_device_mode(hsotg))
		goto unlock;

	if (hsotg->lx_state != DWC2_L2)
		goto unlock;

	hprt0 = dwc2_read_hprt0(hsotg);

	/*
	 * Added port connection status checking which prevents exiting from
	 * Partial Power Down mode from _dwc2_hcd_resume() if not in Partial
	 * Power Down mode.
	 */
	if (hprt0 & HPRT0_CONNSTS) {
		hsotg->lx_state = DWC2_L0;
		goto unlock;
	}

	switch (hsotg->params.power_down) {
	case DWC2_POWER_DOWN_PARAM_PARTIAL:
		ret = dwc2_exit_partial_power_down(hsotg, 0, true);
		if (ret)
			dev_err(hsotg->dev,
				"exit partial_power_down failed\n");
		/*
		 * Set HW accessible bit before powering on the controller
		 * since an interrupt may rise.
		 */
		set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
		break;
	case DWC2_POWER_DOWN_PARAM_HIBERNATION:
		ret = dwc2_exit_hibernation(hsotg, 0, 0, 1);
		if (ret)
			dev_err(hsotg->dev, "exit hibernation failed.\n");

		/*
		 * Set HW accessible bit before powering on the controller
		 * since an interrupt may rise.
		 */
		set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
		break;
	case DWC2_POWER_DOWN_PARAM_NONE:
		/*
		 * If not hibernation nor partial power down are supported,
		 * port resume is done using the clock gating programming flow.
		 */
		spin_unlock_irqrestore(&hsotg->lock, flags);
		dwc2_host_exit_clock_gating(hsotg, 0);

		/*
		 * Initialize the Core for Host mode, as after system resume
		 * the global interrupts are disabled.
		 */
		dwc2_core_init(hsotg, false);
		dwc2_enable_global_interrupts(hsotg);
		dwc2_hcd_reinit(hsotg);
		spin_lock_irqsave(&hsotg->lock, flags);

		/*
		 * Set HW accessible bit before powering on the controller
		 * since an interrupt may rise.
		 */
		set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
		break;
	default:
		hsotg->lx_state = DWC2_L0;
		goto unlock;
	}

	/* Change Root port status, as port status change occurred after resume.*/
	hsotg->flags.b.port_suspend_change = 1;

	/*
	 * Enable power if not already done.
	 * This must not be spinlocked since duration
	 * of this call is unknown.
	 */
	if (!IS_ERR_OR_NULL(hsotg->uphy)) {
		spin_unlock_irqrestore(&hsotg->lock, flags);
		usb_phy_set_suspend(hsotg->uphy, false);
		spin_lock_irqsave(&hsotg->lock, flags);
	}

	/* Enable external vbus supply after resuming the port. */
	spin_unlock_irqrestore(&hsotg->lock, flags);
	dwc2_vbus_supply_init(hsotg);

	/* Wait for controller to correctly update D+/D- level */
	usleep_range(3000, 5000);
	spin_lock_irqsave(&hsotg->lock, flags);

	/*
	 * Clear Port Enable and Port Status changes.
	 * Enable Port Power.
	 */
	dwc2_writel(hsotg, HPRT0_PWR | HPRT0_CONNDET |
			HPRT0_ENACHG, HPRT0);

	/* Wait for controller to detect Port Connect */
	spin_unlock_irqrestore(&hsotg->lock, flags);
	usleep_range(5000, 7000);
	spin_lock_irqsave(&hsotg->lock, flags);
unlock:
	spin_unlock_irqrestore(&hsotg->lock, flags);

	return ret;
}

/* Returns the current frame number */
static int _dwc2_hcd_get_frame_number(struct usb_hcd *hcd)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);

	return dwc2_hcd_get_frame_number(hsotg);
}

static void dwc2_dump_urb_info(struct usb_hcd *hcd, struct urb *urb,
			       char *fn_name)
{
#ifdef VERBOSE_DEBUG
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);
	char *pipetype = NULL;
	char *speed = NULL;

	dev_vdbg(hsotg->dev, "%s, urb %p\n", fn_name, urb);
	dev_vdbg(hsotg->dev, "  Device address: %d\n",
		 usb_pipedevice(urb->pipe));
	dev_vdbg(hsotg->dev, "  Endpoint: %d, %s\n",
		 usb_pipeendpoint(urb->pipe),
		 usb_pipein(urb->pipe) ? "IN" : "OUT");

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_CONTROL:
		pipetype = "CONTROL";
		break;
	case PIPE_BULK:
		pipetype = "BULK";
		break;
	case PIPE_INTERRUPT:
		pipetype = "INTERRUPT";
		break;
	case PIPE_ISOCHRONOUS:
		pipetype = "ISOCHRONOUS";
		break;
	}

	dev_vdbg(hsotg->dev, "  Endpoint type: %s %s (%s)\n", pipetype,
		 usb_urb_dir_in(urb) ? "IN" : "OUT", usb_pipein(urb->pipe) ?
		 "IN" : "OUT");

	switch (urb->dev->speed) {
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

	dev_vdbg(hsotg->dev, "  Speed: %s\n", speed);
	dev_vdbg(hsotg->dev, "  Max packet size: %d (%d mult)\n",
		 usb_endpoint_maxp(&urb->ep->desc),
		 usb_endpoint_maxp_mult(&urb->ep->desc));

	dev_vdbg(hsotg->dev, "  Data buffer length: %d\n",
		 urb->transfer_buffer_length);
	dev_vdbg(hsotg->dev, "  Transfer buffer: %p, Transfer DMA: %08lx\n",
		 urb->transfer_buffer, (unsigned long)urb->transfer_dma);
	dev_vdbg(hsotg->dev, "  Setup buffer: %p, Setup DMA: %08lx\n",
		 urb->setup_packet, (unsigned long)urb->setup_dma);
	dev_vdbg(hsotg->dev, "  Interval: %d\n", urb->interval);

	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {
		int i;

		for (i = 0; i < urb->number_of_packets; i++) {
			dev_vdbg(hsotg->dev, "  ISO Desc %d:\n", i);
			dev_vdbg(hsotg->dev, "    offset: %d, length %d\n",
				 urb->iso_frame_desc[i].offset,
				 urb->iso_frame_desc[i].length);
		}
	}
#endif
}

/*
 * Starts processing a USB transfer request specified by a USB Request Block
 * (URB). mem_flags indicates the type of memory allocation to use while
 * processing this URB.
 */
static int _dwc2_hcd_urb_enqueue(struct usb_hcd *hcd, struct urb *urb,
				 gfp_t mem_flags)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);
	struct usb_host_endpoint *ep = urb->ep;
	struct dwc2_hcd_urb *dwc2_urb;
	int i;
	int retval;
	int alloc_bandwidth = 0;
	u8 ep_type = 0;
	u32 tflags = 0;
	void *buf;
	unsigned long flags;
	struct dwc2_qh *qh;
	bool qh_allocated = false;
	struct dwc2_qtd *qtd;
	struct dwc2_gregs_backup *gr;

	gr = &hsotg->gr_backup;

	if (dbg_urb(urb)) {
		dev_vdbg(hsotg->dev, "DWC OTG HCD URB Enqueue\n");
		dwc2_dump_urb_info(hcd, urb, "urb_enqueue");
	}

	if (hsotg->hibernated) {
		if (gr->gotgctl & GOTGCTL_CURMODE_HOST)
			retval = dwc2_exit_hibernation(hsotg, 0, 0, 1);
		else
			retval = dwc2_exit_hibernation(hsotg, 0, 0, 0);

		if (retval)
			dev_err(hsotg->dev,
				"exit hibernation failed.\n");
	}

	if (hsotg->in_ppd) {
		retval = dwc2_exit_partial_power_down(hsotg, 0, true);
		if (retval)
			dev_err(hsotg->dev,
				"exit partial_power_down failed\n");
	}

	if (hsotg->params.power_down == DWC2_POWER_DOWN_PARAM_NONE &&
	    hsotg->bus_suspended) {
		if (dwc2_is_device_mode(hsotg))
			dwc2_gadget_exit_clock_gating(hsotg, 0);
		else
			dwc2_host_exit_clock_gating(hsotg, 0);
	}

	if (!ep)
		return -EINVAL;

	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS ||
	    usb_pipetype(urb->pipe) == PIPE_INTERRUPT) {
		spin_lock_irqsave(&hsotg->lock, flags);
		if (!dwc2_hcd_is_bandwidth_allocated(hsotg, ep))
			alloc_bandwidth = 1;
		spin_unlock_irqrestore(&hsotg->lock, flags);
	}

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_CONTROL:
		ep_type = USB_ENDPOINT_XFER_CONTROL;
		break;
	case PIPE_ISOCHRONOUS:
		ep_type = USB_ENDPOINT_XFER_ISOC;
		break;
	case PIPE_BULK:
		ep_type = USB_ENDPOINT_XFER_BULK;
		break;
	case PIPE_INTERRUPT:
		ep_type = USB_ENDPOINT_XFER_INT;
		break;
	}

	dwc2_urb = dwc2_hcd_urb_alloc(hsotg, urb->number_of_packets,
				      mem_flags);
	if (!dwc2_urb)
		return -ENOMEM;

	dwc2_hcd_urb_set_pipeinfo(hsotg, dwc2_urb, usb_pipedevice(urb->pipe),
				  usb_pipeendpoint(urb->pipe), ep_type,
				  usb_pipein(urb->pipe),
				  usb_endpoint_maxp(&ep->desc),
				  usb_endpoint_maxp_mult(&ep->desc));

	buf = urb->transfer_buffer;

	if (hcd_uses_dma(hcd)) {
		if (!buf && (urb->transfer_dma & 3)) {
			dev_err(hsotg->dev,
				"%s: unaligned transfer with no transfer_buffer",
				__func__);
			retval = -EINVAL;
			goto fail0;
		}
	}

	if (!(urb->transfer_flags & URB_NO_INTERRUPT))
		tflags |= URB_GIVEBACK_ASAP;
	if (urb->transfer_flags & URB_ZERO_PACKET)
		tflags |= URB_SEND_ZERO_PACKET;

	dwc2_urb->priv = urb;
	dwc2_urb->buf = buf;
	dwc2_urb->dma = urb->transfer_dma;
	dwc2_urb->length = urb->transfer_buffer_length;
	dwc2_urb->setup_packet = urb->setup_packet;
	dwc2_urb->setup_dma = urb->setup_dma;
	dwc2_urb->flags = tflags;
	dwc2_urb->interval = urb->interval;
	dwc2_urb->status = -EINPROGRESS;

	for (i = 0; i < urb->number_of_packets; ++i)
		dwc2_hcd_urb_set_iso_desc_params(dwc2_urb, i,
						 urb->iso_frame_desc[i].offset,
						 urb->iso_frame_desc[i].length);

	urb->hcpriv = dwc2_urb;
	qh = (struct dwc2_qh *)ep->hcpriv;
	/* Create QH for the endpoint if it doesn't exist */
	if (!qh) {
		qh = dwc2_hcd_qh_create(hsotg, dwc2_urb, mem_flags);
		if (!qh) {
			retval = -ENOMEM;
			goto fail0;
		}
		ep->hcpriv = qh;
		qh_allocated = true;
	}

	qtd = kzalloc(sizeof(*qtd), mem_flags);
	if (!qtd) {
		retval = -ENOMEM;
		goto fail1;
	}

	spin_lock_irqsave(&hsotg->lock, flags);
	retval = usb_hcd_link_urb_to_ep(hcd, urb);
	if (retval)
		goto fail2;

	retval = dwc2_hcd_urb_enqueue(hsotg, dwc2_urb, qh, qtd);
	if (retval)
		goto fail3;

	if (alloc_bandwidth) {
		dwc2_allocate_bus_bandwidth(hcd,
				dwc2_hcd_get_ep_bandwidth(hsotg, ep),
				urb);
	}

	spin_unlock_irqrestore(&hsotg->lock, flags);

	return 0;

fail3:
	dwc2_urb->priv = NULL;
	usb_hcd_unlink_urb_from_ep(hcd, urb);
	if (qh_allocated && qh->channel && qh->channel->qh == qh)
		qh->channel->qh = NULL;
fail2:
	spin_unlock_irqrestore(&hsotg->lock, flags);
	urb->hcpriv = NULL;
	kfree(qtd);
fail1:
	if (qh_allocated) {
		struct dwc2_qtd *qtd2, *qtd2_tmp;

		ep->hcpriv = NULL;
		dwc2_hcd_qh_unlink(hsotg, qh);
		/* Free each QTD in the QH's QTD list */
		list_for_each_entry_safe(qtd2, qtd2_tmp, &qh->qtd_list,
					 qtd_list_entry)
			dwc2_hcd_qtd_unlink_and_free(hsotg, qtd2, qh);
		dwc2_hcd_qh_free(hsotg, qh);
	}
fail0:
	kfree(dwc2_urb);

	return retval;
}

/*
 * Aborts/cancels a USB transfer request. Always returns 0 to indicate success.
 */
static int _dwc2_hcd_urb_dequeue(struct usb_hcd *hcd, struct urb *urb,
				 int status)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);
	int rc;
	unsigned long flags;

	dev_dbg(hsotg->dev, "DWC OTG HCD URB Dequeue\n");
	dwc2_dump_urb_info(hcd, urb, "urb_dequeue");

	spin_lock_irqsave(&hsotg->lock, flags);

	rc = usb_hcd_check_unlink_urb(hcd, urb, status);
	if (rc)
		goto out;

	if (!urb->hcpriv) {
		dev_dbg(hsotg->dev, "## urb->hcpriv is NULL ##\n");
		goto out;
	}

	rc = dwc2_hcd_urb_dequeue(hsotg, urb->hcpriv);

	usb_hcd_unlink_urb_from_ep(hcd, urb);

	kfree(urb->hcpriv);
	urb->hcpriv = NULL;

	/* Higher layer software sets URB status */
	spin_unlock(&hsotg->lock);
	usb_hcd_giveback_urb(hcd, urb, status);
	spin_lock(&hsotg->lock);

	dev_dbg(hsotg->dev, "Called usb_hcd_giveback_urb()\n");
	dev_dbg(hsotg->dev, "  urb->status = %d\n", urb->status);
out:
	spin_unlock_irqrestore(&hsotg->lock, flags);

	return rc;
}

/*
 * Frees resources in the DWC_otg controller related to a given endpoint. Also
 * clears state in the HCD related to the endpoint. Any URBs for the endpoint
 * must already be dequeued.
 */
static void _dwc2_hcd_endpoint_disable(struct usb_hcd *hcd,
				       struct usb_host_endpoint *ep)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);

	dev_dbg(hsotg->dev,
		"DWC OTG HCD EP DISABLE: bEndpointAddress=0x%02x, ep->hcpriv=%p\n",
		ep->desc.bEndpointAddress, ep->hcpriv);
	dwc2_hcd_endpoint_disable(hsotg, ep, 250);
}

/*
 * Resets endpoint specific parameter values, in current version used to reset
 * the data toggle (as a WA). This function can be called from usb_clear_halt
 * routine.
 */
static void _dwc2_hcd_endpoint_reset(struct usb_hcd *hcd,
				     struct usb_host_endpoint *ep)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);
	unsigned long flags;

	dev_dbg(hsotg->dev,
		"DWC OTG HCD EP RESET: bEndpointAddress=0x%02x\n",
		ep->desc.bEndpointAddress);

	spin_lock_irqsave(&hsotg->lock, flags);
	dwc2_hcd_endpoint_reset(hsotg, ep);
	spin_unlock_irqrestore(&hsotg->lock, flags);
}

/*
 * Handles host mode interrupts for the DWC_otg controller. Returns IRQ_NONE if
 * there was no interrupt to handle. Returns IRQ_HANDLED if there was a valid
 * interrupt.
 *
 * This function is called by the USB core when an interrupt occurs
 */
static irqreturn_t _dwc2_hcd_irq(struct usb_hcd *hcd)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);

	return dwc2_handle_hcd_intr(hsotg);
}

/*
 * Creates Status Change bitmap for the root hub and root port. The bitmap is
 * returned in buf. Bit 0 is the status change indicator for the root hub. Bit 1
 * is the status change indicator for the single root port. Returns 1 if either
 * change indicator is 1, otherwise returns 0.
 */
static int _dwc2_hcd_hub_status_data(struct usb_hcd *hcd, char *buf)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);

	buf[0] = dwc2_hcd_is_status_changed(hsotg, 1) << 1;
	return buf[0] != 0;
}

/* Handles hub class-specific requests */
static int _dwc2_hcd_hub_control(struct usb_hcd *hcd, u16 typereq, u16 wvalue,
				 u16 windex, char *buf, u16 wlength)
{
	int retval = dwc2_hcd_hub_control(dwc2_hcd_to_hsotg(hcd), typereq,
					  wvalue, windex, buf, wlength);
	return retval;
}

/* Handles hub TT buffer clear completions */
static void _dwc2_hcd_clear_tt_buffer_complete(struct usb_hcd *hcd,
					       struct usb_host_endpoint *ep)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);
	struct dwc2_qh *qh;
	unsigned long flags;

	qh = ep->hcpriv;
	if (!qh)
		return;

	spin_lock_irqsave(&hsotg->lock, flags);
	qh->tt_buffer_dirty = 0;

	if (hsotg->flags.b.port_connect_status)
		dwc2_hcd_queue_transactions(hsotg, DWC2_TRANSACTION_ALL);

	spin_unlock_irqrestore(&hsotg->lock, flags);
}

/*
 * HPRT0_SPD_HIGH_SPEED: high speed
 * HPRT0_SPD_FULL_SPEED: full speed
 */
static void dwc2_change_bus_speed(struct usb_hcd *hcd, int speed)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);

	if (hsotg->params.speed == speed)
		return;

	hsotg->params.speed = speed;
	queue_work(hsotg->wq_otg, &hsotg->wf_otg);
}

static void dwc2_free_dev(struct usb_hcd *hcd, struct usb_device *udev)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);

	if (!hsotg->params.change_speed_quirk)
		return;

	/*
	 * On removal, set speed to default high-speed.
	 */
	if (udev->parent && udev->parent->speed > USB_SPEED_UNKNOWN &&
	    udev->parent->speed < USB_SPEED_HIGH) {
		dev_info(hsotg->dev, "Set speed to default high-speed\n");
		dwc2_change_bus_speed(hcd, HPRT0_SPD_HIGH_SPEED);
	}
}

static int dwc2_reset_device(struct usb_hcd *hcd, struct usb_device *udev)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);

	if (!hsotg->params.change_speed_quirk)
		return 0;

	if (udev->speed == USB_SPEED_HIGH) {
		dev_info(hsotg->dev, "Set speed to high-speed\n");
		dwc2_change_bus_speed(hcd, HPRT0_SPD_HIGH_SPEED);
	} else if ((udev->speed == USB_SPEED_FULL ||
				udev->speed == USB_SPEED_LOW)) {
		/*
		 * Change speed setting to full-speed if there's
		 * a full-speed or low-speed device plugged in.
		 */
		dev_info(hsotg->dev, "Set speed to full-speed\n");
		dwc2_change_bus_speed(hcd, HPRT0_SPD_FULL_SPEED);
	}

	return 0;
}

static struct hc_driver dwc2_hc_driver = {
	.description = "dwc2_hsotg",
	.product_desc = "DWC OTG Controller",
	.hcd_priv_size = sizeof(struct wrapper_priv_data),

	.irq = _dwc2_hcd_irq,
	.flags = HCD_MEMORY | HCD_USB2 | HCD_BH,

	.start = _dwc2_hcd_start,
	.stop = _dwc2_hcd_stop,
	.urb_enqueue = _dwc2_hcd_urb_enqueue,
	.urb_dequeue = _dwc2_hcd_urb_dequeue,
	.endpoint_disable = _dwc2_hcd_endpoint_disable,
	.endpoint_reset = _dwc2_hcd_endpoint_reset,
	.get_frame_number = _dwc2_hcd_get_frame_number,

	.hub_status_data = _dwc2_hcd_hub_status_data,
	.hub_control = _dwc2_hcd_hub_control,
	.clear_tt_buffer_complete = _dwc2_hcd_clear_tt_buffer_complete,

	.bus_suspend = _dwc2_hcd_suspend,
	.bus_resume = _dwc2_hcd_resume,

	.map_urb_for_dma	= dwc2_map_urb_for_dma,
	.unmap_urb_for_dma	= dwc2_unmap_urb_for_dma,
};

/*
 * Frees secondary storage associated with the dwc2_hsotg structure contained
 * in the struct usb_hcd field
 */
static void dwc2_hcd_free(struct dwc2_hsotg *hsotg)
{
	u32 ahbcfg;
	u32 dctl;
	int i;

	dev_dbg(hsotg->dev, "DWC OTG HCD FREE\n");

	/* Free memory for QH/QTD lists */
	dwc2_qh_list_free(hsotg, &hsotg->non_periodic_sched_inactive);
	dwc2_qh_list_free(hsotg, &hsotg->non_periodic_sched_waiting);
	dwc2_qh_list_free(hsotg, &hsotg->non_periodic_sched_active);
	dwc2_qh_list_free(hsotg, &hsotg->periodic_sched_inactive);
	dwc2_qh_list_free(hsotg, &hsotg->periodic_sched_ready);
	dwc2_qh_list_free(hsotg, &hsotg->periodic_sched_assigned);
	dwc2_qh_list_free(hsotg, &hsotg->periodic_sched_queued);

	/* Free memory for the host channels */
	for (i = 0; i < MAX_EPS_CHANNELS; i++) {
		struct dwc2_host_chan *chan = hsotg->hc_ptr_array[i];

		if (chan) {
			dev_dbg(hsotg->dev, "HCD Free channel #%i, chan=%p\n",
				i, chan);
			hsotg->hc_ptr_array[i] = NULL;
			kfree(chan);
		}
	}

	if (hsotg->params.host_dma) {
		if (hsotg->status_buf) {
			dma_free_coherent(hsotg->dev, DWC2_HCD_STATUS_BUF_SIZE,
					  hsotg->status_buf,
					  hsotg->status_buf_dma);
			hsotg->status_buf = NULL;
		}
	} else {
		kfree(hsotg->status_buf);
		hsotg->status_buf = NULL;
	}

	ahbcfg = dwc2_readl(hsotg, GAHBCFG);

	/* Disable all interrupts */
	ahbcfg &= ~GAHBCFG_GLBL_INTR_EN;
	dwc2_writel(hsotg, ahbcfg, GAHBCFG);
	dwc2_writel(hsotg, 0, GINTMSK);

	if (hsotg->hw_params.snpsid >= DWC2_CORE_REV_3_00a) {
		dctl = dwc2_readl(hsotg, DCTL);
		dctl |= DCTL_SFTDISCON;
		dwc2_writel(hsotg, dctl, DCTL);
	}

	if (hsotg->wq_otg) {
		if (!cancel_work_sync(&hsotg->wf_otg))
			flush_workqueue(hsotg->wq_otg);
		destroy_workqueue(hsotg->wq_otg);
	}

	cancel_work_sync(&hsotg->phy_reset_work);

	del_timer(&hsotg->wkp_timer);
}

static void dwc2_hcd_release(struct dwc2_hsotg *hsotg)
{
	/* Turn off all host-specific interrupts */
	dwc2_disable_host_interrupts(hsotg);

	dwc2_hcd_free(hsotg);
}

/*
 * Initializes the HCD. This function allocates memory for and initializes the
 * static parts of the usb_hcd and dwc2_hsotg structures. It also registers the
 * USB bus with the core and calls the hc_driver->start() function. It returns
 * a negative error on failure.
 */
int dwc2_hcd_init(struct dwc2_hsotg *hsotg)
{
	struct platform_device *pdev = to_platform_device(hsotg->dev);
	struct resource *res;
	struct usb_hcd *hcd;
	struct dwc2_host_chan *channel;
	u32 hcfg;
	int i, num_channels;
	int retval;

	if (usb_disabled())
		return -ENODEV;

	dev_dbg(hsotg->dev, "DWC OTG HCD INIT\n");

	retval = -ENOMEM;

	hcfg = dwc2_readl(hsotg, HCFG);
	dev_dbg(hsotg->dev, "hcfg=%08x\n", hcfg);

#ifdef CONFIG_USB_DWC2_TRACK_MISSED_SOFS
	hsotg->frame_num_array = kcalloc(FRAME_NUM_ARRAY_SIZE,
					 sizeof(*hsotg->frame_num_array),
					 GFP_KERNEL);
	if (!hsotg->frame_num_array)
		goto error1;
	hsotg->last_frame_num_array =
		kcalloc(FRAME_NUM_ARRAY_SIZE,
			sizeof(*hsotg->last_frame_num_array), GFP_KERNEL);
	if (!hsotg->last_frame_num_array)
		goto error1;
#endif
	hsotg->last_frame_num = HFNUM_MAX_FRNUM;

	/* Check if the bus driver or platform code has setup a dma_mask */
	if (hsotg->params.host_dma &&
	    !hsotg->dev->dma_mask) {
		dev_warn(hsotg->dev,
			 "dma_mask not set, disabling DMA\n");
		hsotg->params.host_dma = false;
		hsotg->params.dma_desc_enable = false;
	}

	/* Set device flags indicating whether the HCD supports DMA */
	if (hsotg->params.host_dma) {
		if (dma_set_mask(hsotg->dev, DMA_BIT_MASK(32)) < 0)
			dev_warn(hsotg->dev, "can't set DMA mask\n");
		if (dma_set_coherent_mask(hsotg->dev, DMA_BIT_MASK(32)) < 0)
			dev_warn(hsotg->dev, "can't set coherent DMA mask\n");
	}

	if (hsotg->params.change_speed_quirk) {
		dwc2_hc_driver.free_dev = dwc2_free_dev;
		dwc2_hc_driver.reset_device = dwc2_reset_device;
	}

	if (hsotg->params.host_dma)
		dwc2_hc_driver.flags |= HCD_DMA;

	hcd = usb_create_hcd(&dwc2_hc_driver, hsotg->dev, dev_name(hsotg->dev));
	if (!hcd)
		goto error1;

	hcd->has_tt = 1;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		retval = -EINVAL;
		goto error1;
	}
	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	((struct wrapper_priv_data *)&hcd->hcd_priv)->hsotg = hsotg;
	hsotg->priv = hcd;

	/*
	 * Disable the global interrupt until all the interrupt handlers are
	 * installed
	 */
	dwc2_disable_global_interrupts(hsotg);

	/* Initialize the DWC_otg core, and select the Phy type */
	retval = dwc2_core_init(hsotg, true);
	if (retval)
		goto error2;

	/* Create new workqueue and init work */
	retval = -ENOMEM;
	hsotg->wq_otg = alloc_ordered_workqueue("dwc2", 0);
	if (!hsotg->wq_otg) {
		dev_err(hsotg->dev, "Failed to create workqueue\n");
		goto error2;
	}
	INIT_WORK(&hsotg->wf_otg, dwc2_conn_id_status_change);

	timer_setup(&hsotg->wkp_timer, dwc2_wakeup_detected, 0);

	/* Initialize the non-periodic schedule */
	INIT_LIST_HEAD(&hsotg->non_periodic_sched_inactive);
	INIT_LIST_HEAD(&hsotg->non_periodic_sched_waiting);
	INIT_LIST_HEAD(&hsotg->non_periodic_sched_active);

	/* Initialize the periodic schedule */
	INIT_LIST_HEAD(&hsotg->periodic_sched_inactive);
	INIT_LIST_HEAD(&hsotg->periodic_sched_ready);
	INIT_LIST_HEAD(&hsotg->periodic_sched_assigned);
	INIT_LIST_HEAD(&hsotg->periodic_sched_queued);

	INIT_LIST_HEAD(&hsotg->split_order);

	/*
	 * Create a host channel descriptor for each host channel implemented
	 * in the controller. Initialize the channel descriptor array.
	 */
	INIT_LIST_HEAD(&hsotg->free_hc_list);
	num_channels = hsotg->params.host_channels;
	memset(&hsotg->hc_ptr_array[0], 0, sizeof(hsotg->hc_ptr_array));

	for (i = 0; i < num_channels; i++) {
		channel = kzalloc(sizeof(*channel), GFP_KERNEL);
		if (!channel)
			goto error3;
		channel->hc_num = i;
		INIT_LIST_HEAD(&channel->split_order_list_entry);
		hsotg->hc_ptr_array[i] = channel;
	}

	/* Initialize work */
	INIT_DELAYED_WORK(&hsotg->start_work, dwc2_hcd_start_func);
	INIT_DELAYED_WORK(&hsotg->reset_work, dwc2_hcd_reset_func);
	INIT_WORK(&hsotg->phy_reset_work, dwc2_hcd_phy_reset_func);

	/*
	 * Allocate space for storing data on status transactions. Normally no
	 * data is sent, but this space acts as a bit bucket. This must be
	 * done after usb_add_hcd since that function allocates the DMA buffer
	 * pool.
	 */
	if (hsotg->params.host_dma)
		hsotg->status_buf = dma_alloc_coherent(hsotg->dev,
					DWC2_HCD_STATUS_BUF_SIZE,
					&hsotg->status_buf_dma, GFP_KERNEL);
	else
		hsotg->status_buf = kzalloc(DWC2_HCD_STATUS_BUF_SIZE,
					  GFP_KERNEL);

	if (!hsotg->status_buf)
		goto error3;

	/*
	 * Create kmem caches to handle descriptor buffers in descriptor
	 * DMA mode.
	 * Alignment must be set to 512 bytes.
	 */
	if (hsotg->params.dma_desc_enable ||
	    hsotg->params.dma_desc_fs_enable) {
		hsotg->desc_gen_cache = kmem_cache_create("dwc2-gen-desc",
				sizeof(struct dwc2_dma_desc) *
				MAX_DMA_DESC_NUM_GENERIC, 512, SLAB_CACHE_DMA,
				NULL);
		if (!hsotg->desc_gen_cache) {
			dev_err(hsotg->dev,
				"unable to create dwc2 generic desc cache\n");

			/*
			 * Disable descriptor dma mode since it will not be
			 * usable.
			 */
			hsotg->params.dma_desc_enable = false;
			hsotg->params.dma_desc_fs_enable = false;
		}

		hsotg->desc_hsisoc_cache = kmem_cache_create("dwc2-hsisoc-desc",
				sizeof(struct dwc2_dma_desc) *
				MAX_DMA_DESC_NUM_HS_ISOC, 512, 0, NULL);
		if (!hsotg->desc_hsisoc_cache) {
			dev_err(hsotg->dev,
				"unable to create dwc2 hs isoc desc cache\n");

			kmem_cache_destroy(hsotg->desc_gen_cache);

			/*
			 * Disable descriptor dma mode since it will not be
			 * usable.
			 */
			hsotg->params.dma_desc_enable = false;
			hsotg->params.dma_desc_fs_enable = false;
		}
	}

	if (hsotg->params.host_dma) {
		/*
		 * Create kmem caches to handle non-aligned buffer
		 * in Buffer DMA mode.
		 */
		hsotg->unaligned_cache = kmem_cache_create("dwc2-unaligned-dma",
						DWC2_KMEM_UNALIGNED_BUF_SIZE, 4,
						SLAB_CACHE_DMA, NULL);
		if (!hsotg->unaligned_cache)
			dev_err(hsotg->dev,
				"unable to create dwc2 unaligned cache\n");
	}

	hsotg->otg_port = 1;
	hsotg->frame_list = NULL;
	hsotg->frame_list_dma = 0;
	hsotg->periodic_qh_count = 0;

	/* Initiate lx_state to L3 disconnected state */
	hsotg->lx_state = DWC2_L3;

	hcd->self.otg_port = hsotg->otg_port;

	/* Don't support SG list at this point */
	hcd->self.sg_tablesize = 0;

	if (!IS_ERR_OR_NULL(hsotg->uphy))
		otg_set_host(hsotg->uphy->otg, &hcd->self);

	/*
	 * Finish generic HCD initialization and start the HCD. This function
	 * allocates the DMA buffer pool, registers the USB bus, requests the
	 * IRQ line, and calls hcd_start method.
	 */
	retval = usb_add_hcd(hcd, hsotg->irq, IRQF_SHARED);
	if (retval < 0)
		goto error4;

	device_wakeup_enable(hcd->self.controller);

	dwc2_hcd_dump_state(hsotg);

	dwc2_enable_global_interrupts(hsotg);

	return 0;

error4:
	kmem_cache_destroy(hsotg->unaligned_cache);
	kmem_cache_destroy(hsotg->desc_hsisoc_cache);
	kmem_cache_destroy(hsotg->desc_gen_cache);
error3:
	dwc2_hcd_release(hsotg);
error2:
	usb_put_hcd(hcd);
error1:

#ifdef CONFIG_USB_DWC2_TRACK_MISSED_SOFS
	kfree(hsotg->last_frame_num_array);
	kfree(hsotg->frame_num_array);
#endif

	dev_err(hsotg->dev, "%s() FAILED, returning %d\n", __func__, retval);
	return retval;
}

/*
 * Removes the HCD.
 * Frees memory and resources associated with the HCD and deregisters the bus.
 */
void dwc2_hcd_remove(struct dwc2_hsotg *hsotg)
{
	struct usb_hcd *hcd;

	dev_dbg(hsotg->dev, "DWC OTG HCD REMOVE\n");

	hcd = dwc2_hsotg_to_hcd(hsotg);
	dev_dbg(hsotg->dev, "hsotg->hcd = %p\n", hcd);

	if (!hcd) {
		dev_dbg(hsotg->dev, "%s: dwc2_hsotg_to_hcd(hsotg) NULL!\n",
			__func__);
		return;
	}

	if (!IS_ERR_OR_NULL(hsotg->uphy))
		otg_set_host(hsotg->uphy->otg, NULL);

	usb_remove_hcd(hcd);
	hsotg->priv = NULL;

	kmem_cache_destroy(hsotg->unaligned_cache);
	kmem_cache_destroy(hsotg->desc_hsisoc_cache);
	kmem_cache_destroy(hsotg->desc_gen_cache);

	dwc2_hcd_release(hsotg);
	usb_put_hcd(hcd);

#ifdef CONFIG_USB_DWC2_TRACK_MISSED_SOFS
	kfree(hsotg->last_frame_num_array);
	kfree(hsotg->frame_num_array);
#endif
}

/**
 * dwc2_backup_host_registers() - Backup controller host registers.
 * When suspending usb bus, registers needs to be backuped
 * if controller power is disabled once suspended.
 *
 * @hsotg: Programming view of the DWC_otg controller
 */
int dwc2_backup_host_registers(struct dwc2_hsotg *hsotg)
{
	struct dwc2_hregs_backup *hr;
	int i;

	dev_dbg(hsotg->dev, "%s\n", __func__);

	/* Backup Host regs */
	hr = &hsotg->hr_backup;
	hr->hcfg = dwc2_readl(hsotg, HCFG);
	hr->haintmsk = dwc2_readl(hsotg, HAINTMSK);
	for (i = 0; i < hsotg->params.host_channels; ++i)
		hr->hcintmsk[i] = dwc2_readl(hsotg, HCINTMSK(i));

	hr->hprt0 = dwc2_read_hprt0(hsotg);
	hr->hfir = dwc2_readl(hsotg, HFIR);
	hr->hptxfsiz = dwc2_readl(hsotg, HPTXFSIZ);
	hr->valid = true;

	return 0;
}

/**
 * dwc2_restore_host_registers() - Restore controller host registers.
 * When resuming usb bus, device registers needs to be restored
 * if controller power were disabled.
 *
 * @hsotg: Programming view of the DWC_otg controller
 */
int dwc2_restore_host_registers(struct dwc2_hsotg *hsotg)
{
	struct dwc2_hregs_backup *hr;
	int i;

	dev_dbg(hsotg->dev, "%s\n", __func__);

	/* Restore host regs */
	hr = &hsotg->hr_backup;
	if (!hr->valid) {
		dev_err(hsotg->dev, "%s: no host registers to restore\n",
			__func__);
		return -EINVAL;
	}
	hr->valid = false;

	dwc2_writel(hsotg, hr->hcfg, HCFG);
	dwc2_writel(hsotg, hr->haintmsk, HAINTMSK);

	for (i = 0; i < hsotg->params.host_channels; ++i)
		dwc2_writel(hsotg, hr->hcintmsk[i], HCINTMSK(i));

	dwc2_writel(hsotg, hr->hprt0, HPRT0);
	dwc2_writel(hsotg, hr->hfir, HFIR);
	dwc2_writel(hsotg, hr->hptxfsiz, HPTXFSIZ);
	hsotg->frame_number = 0;

	return 0;
}

/**
 * dwc2_host_enter_hibernation() - Put controller in Hibernation.
 *
 * @hsotg: Programming view of the DWC_otg controller
 */
int dwc2_host_enter_hibernation(struct dwc2_hsotg *hsotg)
{
	unsigned long flags;
	int ret = 0;
	u32 hprt0;
	u32 pcgcctl;
	u32 gusbcfg;
	u32 gpwrdn;

	dev_dbg(hsotg->dev, "Preparing host for hibernation\n");
	ret = dwc2_backup_global_registers(hsotg);
	if (ret) {
		dev_err(hsotg->dev, "%s: failed to backup global registers\n",
			__func__);
		return ret;
	}
	ret = dwc2_backup_host_registers(hsotg);
	if (ret) {
		dev_err(hsotg->dev, "%s: failed to backup host registers\n",
			__func__);
		return ret;
	}

	/* Enter USB Suspend Mode */
	hprt0 = dwc2_readl(hsotg, HPRT0);
	hprt0 |= HPRT0_SUSP;
	hprt0 &= ~HPRT0_ENA;
	dwc2_writel(hsotg, hprt0, HPRT0);

	/* Wait for the HPRT0.PrtSusp register field to be set */
	if (dwc2_hsotg_wait_bit_set(hsotg, HPRT0, HPRT0_SUSP, 5000))
		dev_warn(hsotg->dev, "Suspend wasn't generated\n");

	/*
	 * We need to disable interrupts to prevent servicing of any IRQ
	 * during going to hibernation
	 */
	spin_lock_irqsave(&hsotg->lock, flags);
	hsotg->lx_state = DWC2_L2;

	gusbcfg = dwc2_readl(hsotg, GUSBCFG);
	if (gusbcfg & GUSBCFG_ULPI_UTMI_SEL) {
		/* ULPI interface */
		/* Suspend the Phy Clock */
		pcgcctl = dwc2_readl(hsotg, PCGCTL);
		pcgcctl |= PCGCTL_STOPPCLK;
		dwc2_writel(hsotg, pcgcctl, PCGCTL);
		udelay(10);

		gpwrdn = dwc2_readl(hsotg, GPWRDN);
		gpwrdn |= GPWRDN_PMUACTV;
		dwc2_writel(hsotg, gpwrdn, GPWRDN);
		udelay(10);
	} else {
		/* UTMI+ Interface */
		gpwrdn = dwc2_readl(hsotg, GPWRDN);
		gpwrdn |= GPWRDN_PMUACTV;
		dwc2_writel(hsotg, gpwrdn, GPWRDN);
		udelay(10);

		pcgcctl = dwc2_readl(hsotg, PCGCTL);
		pcgcctl |= PCGCTL_STOPPCLK;
		dwc2_writel(hsotg, pcgcctl, PCGCTL);
		udelay(10);
	}

	/* Enable interrupts from wake up logic */
	gpwrdn = dwc2_readl(hsotg, GPWRDN);
	gpwrdn |= GPWRDN_PMUINTSEL;
	dwc2_writel(hsotg, gpwrdn, GPWRDN);
	udelay(10);

	/* Unmask host mode interrupts in GPWRDN */
	gpwrdn = dwc2_readl(hsotg, GPWRDN);
	gpwrdn |= GPWRDN_DISCONN_DET_MSK;
	gpwrdn |= GPWRDN_LNSTSCHG_MSK;
	gpwrdn |= GPWRDN_STS_CHGINT_MSK;
	dwc2_writel(hsotg, gpwrdn, GPWRDN);
	udelay(10);

	/* Enable Power Down Clamp */
	gpwrdn = dwc2_readl(hsotg, GPWRDN);
	gpwrdn |= GPWRDN_PWRDNCLMP;
	dwc2_writel(hsotg, gpwrdn, GPWRDN);
	udelay(10);

	/* Switch off VDD */
	gpwrdn = dwc2_readl(hsotg, GPWRDN);
	gpwrdn |= GPWRDN_PWRDNSWTCH;
	dwc2_writel(hsotg, gpwrdn, GPWRDN);

	hsotg->hibernated = 1;
	hsotg->bus_suspended = 1;
	dev_dbg(hsotg->dev, "Host hibernation completed\n");
	spin_unlock_irqrestore(&hsotg->lock, flags);
	return ret;
}

/*
 * dwc2_host_exit_hibernation()
 *
 * @hsotg: Programming view of the DWC_otg controller
 * @rem_wakeup: indicates whether resume is initiated by Device or Host.
 * @param reset: indicates whether resume is initiated by Reset.
 *
 * Return: non-zero if failed to enter to hibernation.
 *
 * This function is for exiting from Host mode hibernation by
 * Host Initiated Resume/Reset and Device Initiated Remote-Wakeup.
 */
int dwc2_host_exit_hibernation(struct dwc2_hsotg *hsotg, int rem_wakeup,
			       int reset)
{
	u32 gpwrdn;
	u32 hprt0;
	int ret = 0;
	struct dwc2_gregs_backup *gr;
	struct dwc2_hregs_backup *hr;

	gr = &hsotg->gr_backup;
	hr = &hsotg->hr_backup;

	dev_dbg(hsotg->dev,
		"%s: called with rem_wakeup = %d reset = %d\n",
		__func__, rem_wakeup, reset);

	dwc2_hib_restore_common(hsotg, rem_wakeup, 1);
	hsotg->hibernated = 0;

	/*
	 * This step is not described in functional spec but if not wait for
	 * this delay, mismatch interrupts occurred because just after restore
	 * core is in Device mode(gintsts.curmode == 0)
	 */
	mdelay(100);

	/* Clear all pending interupts */
	dwc2_writel(hsotg, 0xffffffff, GINTSTS);

	/* De-assert Restore */
	gpwrdn = dwc2_readl(hsotg, GPWRDN);
	gpwrdn &= ~GPWRDN_RESTORE;
	dwc2_writel(hsotg, gpwrdn, GPWRDN);
	udelay(10);

	/* Restore GUSBCFG, HCFG */
	dwc2_writel(hsotg, gr->gusbcfg, GUSBCFG);
	dwc2_writel(hsotg, hr->hcfg, HCFG);

	/* De-assert Wakeup Logic */
	gpwrdn = dwc2_readl(hsotg, GPWRDN);
	gpwrdn &= ~GPWRDN_PMUACTV;
	dwc2_writel(hsotg, gpwrdn, GPWRDN);
	udelay(10);

	hprt0 = hr->hprt0;
	hprt0 |= HPRT0_PWR;
	hprt0 &= ~HPRT0_ENA;
	hprt0 &= ~HPRT0_SUSP;
	dwc2_writel(hsotg, hprt0, HPRT0);

	hprt0 = hr->hprt0;
	hprt0 |= HPRT0_PWR;
	hprt0 &= ~HPRT0_ENA;
	hprt0 &= ~HPRT0_SUSP;

	if (reset) {
		hprt0 |= HPRT0_RST;
		dwc2_writel(hsotg, hprt0, HPRT0);

		/* Wait for Resume time and then program HPRT again */
		mdelay(60);
		hprt0 &= ~HPRT0_RST;
		dwc2_writel(hsotg, hprt0, HPRT0);
	} else {
		hprt0 |= HPRT0_RES;
		dwc2_writel(hsotg, hprt0, HPRT0);

		/* Wait for Resume time and then program HPRT again */
		mdelay(100);
		hprt0 &= ~HPRT0_RES;
		dwc2_writel(hsotg, hprt0, HPRT0);
	}
	/* Clear all interrupt status */
	hprt0 = dwc2_readl(hsotg, HPRT0);
	hprt0 |= HPRT0_CONNDET;
	hprt0 |= HPRT0_ENACHG;
	hprt0 &= ~HPRT0_ENA;
	dwc2_writel(hsotg, hprt0, HPRT0);

	hprt0 = dwc2_readl(hsotg, HPRT0);

	/* Clear all pending interupts */
	dwc2_writel(hsotg, 0xffffffff, GINTSTS);

	/* Restore global registers */
	ret = dwc2_restore_global_registers(hsotg);
	if (ret) {
		dev_err(hsotg->dev, "%s: failed to restore registers\n",
			__func__);
		return ret;
	}

	/* Restore host registers */
	ret = dwc2_restore_host_registers(hsotg);
	if (ret) {
		dev_err(hsotg->dev, "%s: failed to restore host registers\n",
			__func__);
		return ret;
	}

	if (rem_wakeup) {
		dwc2_hcd_rem_wakeup(hsotg);
		/*
		 * Change "port_connect_status_change" flag to re-enumerate,
		 * because after exit from hibernation port connection status
		 * is not detected.
		 */
		hsotg->flags.b.port_connect_status_change = 1;
	}

	hsotg->hibernated = 0;
	hsotg->bus_suspended = 0;
	hsotg->lx_state = DWC2_L0;
	dev_dbg(hsotg->dev, "Host hibernation restore complete\n");
	return ret;
}

bool dwc2_host_can_poweroff_phy(struct dwc2_hsotg *dwc2)
{
	struct usb_device *root_hub = dwc2_hsotg_to_hcd(dwc2)->self.root_hub;

	/* If the controller isn't allowed to wakeup then we can power off. */
	if (!device_may_wakeup(dwc2->dev))
		return true;

	/*
	 * We don't want to power off the PHY if something under the
	 * root hub has wakeup enabled.
	 */
	if (usb_wakeup_enabled_descendants(root_hub))
		return false;

	/* No reason to keep the PHY powered, so allow poweroff */
	return true;
}

/**
 * dwc2_host_enter_partial_power_down() - Put controller in partial
 * power down.
 *
 * @hsotg: Programming view of the DWC_otg controller
 *
 * Return: non-zero if failed to enter host partial power down.
 *
 * This function is for entering Host mode partial power down.
 */
int dwc2_host_enter_partial_power_down(struct dwc2_hsotg *hsotg)
{
	u32 pcgcctl;
	u32 hprt0;
	int ret = 0;

	dev_dbg(hsotg->dev, "Entering host partial power down started.\n");

	/* Put this port in suspend mode. */
	hprt0 = dwc2_read_hprt0(hsotg);
	hprt0 |= HPRT0_SUSP;
	dwc2_writel(hsotg, hprt0, HPRT0);
	udelay(5);

	/* Wait for the HPRT0.PrtSusp register field to be set */
	if (dwc2_hsotg_wait_bit_set(hsotg, HPRT0, HPRT0_SUSP, 3000))
		dev_warn(hsotg->dev, "Suspend wasn't generated\n");

	/* Backup all registers */
	ret = dwc2_backup_global_registers(hsotg);
	if (ret) {
		dev_err(hsotg->dev, "%s: failed to backup global registers\n",
			__func__);
		return ret;
	}

	ret = dwc2_backup_host_registers(hsotg);
	if (ret) {
		dev_err(hsotg->dev, "%s: failed to backup host registers\n",
			__func__);
		return ret;
	}

	/*
	 * Clear any pending interrupts since dwc2 will not be able to
	 * clear them after entering partial_power_down.
	 */
	dwc2_writel(hsotg, 0xffffffff, GINTSTS);

	/* Put the controller in low power state */
	pcgcctl = dwc2_readl(hsotg, PCGCTL);

	pcgcctl |= PCGCTL_PWRCLMP;
	dwc2_writel(hsotg, pcgcctl, PCGCTL);
	udelay(5);

	pcgcctl |= PCGCTL_RSTPDWNMODULE;
	dwc2_writel(hsotg, pcgcctl, PCGCTL);
	udelay(5);

	pcgcctl |= PCGCTL_STOPPCLK;
	dwc2_writel(hsotg, pcgcctl, PCGCTL);

	/* Set in_ppd flag to 1 as here core enters suspend. */
	hsotg->in_ppd = 1;
	hsotg->lx_state = DWC2_L2;
	hsotg->bus_suspended = true;

	dev_dbg(hsotg->dev, "Entering host partial power down completed.\n");

	return ret;
}

/*
 * dwc2_host_exit_partial_power_down() - Exit controller from host partial
 * power down.
 *
 * @hsotg: Programming view of the DWC_otg controller
 * @rem_wakeup: indicates whether resume is initiated by Reset.
 * @restore: indicates whether need to restore the registers or not.
 *
 * Return: non-zero if failed to exit host partial power down.
 *
 * This function is for exiting from Host mode partial power down.
 */
int dwc2_host_exit_partial_power_down(struct dwc2_hsotg *hsotg,
				      int rem_wakeup, bool restore)
{
	u32 pcgcctl;
	int ret = 0;
	u32 hprt0;

	dev_dbg(hsotg->dev, "Exiting host partial power down started.\n");

	pcgcctl = dwc2_readl(hsotg, PCGCTL);
	pcgcctl &= ~PCGCTL_STOPPCLK;
	dwc2_writel(hsotg, pcgcctl, PCGCTL);
	udelay(5);

	pcgcctl = dwc2_readl(hsotg, PCGCTL);
	pcgcctl &= ~PCGCTL_PWRCLMP;
	dwc2_writel(hsotg, pcgcctl, PCGCTL);
	udelay(5);

	pcgcctl = dwc2_readl(hsotg, PCGCTL);
	pcgcctl &= ~PCGCTL_RSTPDWNMODULE;
	dwc2_writel(hsotg, pcgcctl, PCGCTL);

	udelay(100);
	if (restore) {
		ret = dwc2_restore_global_registers(hsotg);
		if (ret) {
			dev_err(hsotg->dev, "%s: failed to restore registers\n",
				__func__);
			return ret;
		}

		ret = dwc2_restore_host_registers(hsotg);
		if (ret) {
			dev_err(hsotg->dev, "%s: failed to restore host registers\n",
				__func__);
			return ret;
		}
	}

	/* Drive resume signaling and exit suspend mode on the port. */
	hprt0 = dwc2_read_hprt0(hsotg);
	hprt0 |= HPRT0_RES;
	hprt0 &= ~HPRT0_SUSP;
	dwc2_writel(hsotg, hprt0, HPRT0);
	udelay(5);

	if (!rem_wakeup) {
		/* Stop driveing resume signaling on the port. */
		hprt0 = dwc2_read_hprt0(hsotg);
		hprt0 &= ~HPRT0_RES;
		dwc2_writel(hsotg, hprt0, HPRT0);

		hsotg->bus_suspended = false;
	} else {
		/* Turn on the port power bit. */
		hprt0 = dwc2_read_hprt0(hsotg);
		hprt0 |= HPRT0_PWR;
		dwc2_writel(hsotg, hprt0, HPRT0);

		/* Connect hcd. */
		dwc2_hcd_connect(hsotg);

		mod_timer(&hsotg->wkp_timer,
			  jiffies + msecs_to_jiffies(71));
	}

	/* Set lx_state to and in_ppd to 0 as here core exits from suspend. */
	hsotg->in_ppd = 0;
	hsotg->lx_state = DWC2_L0;

	dev_dbg(hsotg->dev, "Exiting host partial power down completed.\n");
	return ret;
}

/**
 * dwc2_host_enter_clock_gating() - Put controller in clock gating.
 *
 * @hsotg: Programming view of the DWC_otg controller
 *
 * This function is for entering Host mode clock gating.
 */
void dwc2_host_enter_clock_gating(struct dwc2_hsotg *hsotg)
{
	u32 hprt0;
	u32 pcgctl;

	dev_dbg(hsotg->dev, "Entering host clock gating.\n");

	/* Put this port in suspend mode. */
	hprt0 = dwc2_read_hprt0(hsotg);
	hprt0 |= HPRT0_SUSP;
	dwc2_writel(hsotg, hprt0, HPRT0);

	/* Set the Phy Clock bit as suspend is received. */
	pcgctl = dwc2_readl(hsotg, PCGCTL);
	pcgctl |= PCGCTL_STOPPCLK;
	dwc2_writel(hsotg, pcgctl, PCGCTL);
	udelay(5);

	/* Set the Gate hclk as suspend is received. */
	pcgctl = dwc2_readl(hsotg, PCGCTL);
	pcgctl |= PCGCTL_GATEHCLK;
	dwc2_writel(hsotg, pcgctl, PCGCTL);
	udelay(5);

	hsotg->bus_suspended = true;
	hsotg->lx_state = DWC2_L2;
}

/**
 * dwc2_host_exit_clock_gating() - Exit controller from clock gating.
 *
 * @hsotg: Programming view of the DWC_otg controller
 * @rem_wakeup: indicates whether resume is initiated by remote wakeup
 *
 * This function is for exiting Host mode clock gating.
 */
void dwc2_host_exit_clock_gating(struct dwc2_hsotg *hsotg, int rem_wakeup)
{
	u32 hprt0;
	u32 pcgctl;

	dev_dbg(hsotg->dev, "Exiting host clock gating.\n");

	/* Clear the Gate hclk. */
	pcgctl = dwc2_readl(hsotg, PCGCTL);
	pcgctl &= ~PCGCTL_GATEHCLK;
	dwc2_writel(hsotg, pcgctl, PCGCTL);
	udelay(5);

	/* Phy Clock bit. */
	pcgctl = dwc2_readl(hsotg, PCGCTL);
	pcgctl &= ~PCGCTL_STOPPCLK;
	dwc2_writel(hsotg, pcgctl, PCGCTL);
	udelay(5);

	/* Drive resume signaling and exit suspend mode on the port. */
	hprt0 = dwc2_read_hprt0(hsotg);
	hprt0 |= HPRT0_RES;
	hprt0 &= ~HPRT0_SUSP;
	dwc2_writel(hsotg, hprt0, HPRT0);
	udelay(5);

	if (!rem_wakeup) {
		/* In case of port resume need to wait for 40 ms */
		msleep(USB_RESUME_TIMEOUT);

		/* Stop driveing resume signaling on the port. */
		hprt0 = dwc2_read_hprt0(hsotg);
		hprt0 &= ~HPRT0_RES;
		dwc2_writel(hsotg, hprt0, HPRT0);

		hsotg->bus_suspended = false;
		hsotg->lx_state = DWC2_L0;
	} else {
		mod_timer(&hsotg->wkp_timer,
			  jiffies + msecs_to_jiffies(71));
	}
}
