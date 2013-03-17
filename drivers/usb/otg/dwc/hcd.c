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

/*
 * This file contains the implementation of the HCD. In Linux, the HCD
 * implements the hc_driver API.
 */

#include <asm/unaligned.h>
#include <linux/dma-mapping.h>

#include "hcd.h"

static const char dwc_otg_hcd_name[] = "dwc_otg_hcd";

/**
 * Clears the transfer state for a host channel. This function is normally
 * called after a transfer is done and the host channel is being released. It
 * clears the channel interrupt enables and any unhandled channel interrupt
 * conditions.
 */
void dwc_otg_hc_cleanup(struct core_if *core_if, struct dwc_hc *hc)
{
	ulong regs;

	hc->xfer_started = 0;
	regs = core_if->host_if->hc_regs[hc->hc_num];
	dwc_reg_write(regs, DWC_HCINTMSK, 0);
	dwc_reg_write(regs, DWC_HCINT, 0xFFFFFFFF);
}

/**
 * This function enables the Host mode interrupts.
 */
static void dwc_otg_enable_host_interrupts(struct core_if *core_if)
{
	ulong global_regs = core_if->core_global_regs;
	u32 intr_mask = 0;

	/* Disable all interrupts. */
	dwc_reg_write(global_regs, DWC_GINTMSK, 0);

	/* Clear any pending interrupts. */
	dwc_reg_write(global_regs, DWC_GINTSTS, 0xFFFFFFFF);

	/* Enable the common interrupts */
	dwc_otg_enable_common_interrupts(core_if);

	/*
	 * Enable host mode interrupts without disturbing common
	 * interrupts.
	 */
	intr_mask |= DWC_INTMSK_STRT_OF_FRM;
	intr_mask |= DWC_INTMSK_HST_PORT;
	intr_mask |= DWC_INTMSK_HST_CHAN;
	dwc_reg_modify(global_regs, DWC_GINTMSK, intr_mask, intr_mask);
}

/**
 * This function initializes the DWC_otg controller registers for
 * host mode.
 *
 * This function flushes the Tx and Rx FIFOs and it flushes any entries in the
 * request queues. Host channels are reset to ensure that they are ready for
 * performing transfers.
 */
static void dwc_otg_core_host_init(struct core_if *core_if)
{
	ulong global_regs = core_if->core_global_regs;
	struct dwc_host_if *host_if = core_if->host_if;
	struct core_params *params = core_if->core_params;
	u32 hprt0 = 0;
	u32 nptxfifosize = 0;
	u32 ptxfifosize = 0;
	u32 i;
	u32 hcchar;
	ulong hc_regs;
	int num_channels;
	u32 gotgctl = 0;

	/* Restart the Phy Clock */
	dwc_reg_write(core_if->pcgcctl, 0, 0);

	/* Initialize Host Configuration Register */
	init_fslspclksel(core_if);

	/* Configure data FIFO sizes */
	if (DWC_HWCFG2_DYN_FIFO_RD(core_if->hwcfg2)
	    && params->enable_dynamic_fifo) {
		/* Rx FIFO */
		dwc_reg_write(global_regs, DWC_GRXFSIZ,
			    params->host_rx_fifo_size);

		/* Non-periodic Tx FIFO */
		nptxfifosize = DWC_RX_FIFO_DEPTH_WR(nptxfifosize,
						    params->
						    host_nperio_tx_fifo_size);
		nptxfifosize =
		    DWC_RX_FIFO_START_ADDR_WR(nptxfifosize,
					      params->host_rx_fifo_size);
		dwc_reg_write(global_regs, DWC_GNPTXFSIZ, nptxfifosize);

		/* Periodic Tx FIFO */
		ptxfifosize = DWC_RX_FIFO_DEPTH_WR(ptxfifosize,
						   params->
						   host_perio_tx_fifo_size);
		ptxfifosize =
		    DWC_RX_FIFO_START_ADDR_WR(ptxfifosize,
					      (DWC_RX_FIFO_START_ADDR_RD
					       (nptxfifosize) +
					       DWC_RX_FIFO_DEPTH_RD
					       (nptxfifosize)));
		dwc_reg_write(global_regs, DWC_HPTXFSIZ, ptxfifosize);
	}

	/* Clear Host Set HNP Enable in the OTG Control Register */
	gotgctl |= DWC_GCTL_HOST_HNP_ENA;
	dwc_reg_modify(global_regs, DWC_GOTGCTL, gotgctl, 0);

	/* Make sure the FIFOs are flushed. */
	dwc_otg_flush_tx_fifo(core_if, DWC_GRSTCTL_TXFNUM_ALL);
	dwc_otg_flush_rx_fifo(core_if);

	/* Flush out any leftover queued requests. */
	num_channels = core_if->core_params->host_channels;
	for (i = 0; i < num_channels; i++) {
		hc_regs = core_if->host_if->hc_regs[i];
		hcchar = dwc_reg_read(hc_regs, DWC_HCCHAR);
		hcchar = DWC_HCCHAR_ENA_RW(hcchar, 0);
		hcchar = DWC_HCCHAR_DIS_RW(hcchar, 1);
		hcchar = DWC_HCCHAR_EPDIR_RW(hcchar, 0);
		dwc_reg_write(hc_regs, DWC_HCCHAR, hcchar);
	}

	/* Halt all channels to put them into a known state. */
	for (i = 0; i < num_channels; i++) {
		int count = 0;

		hc_regs = core_if->host_if->hc_regs[i];
		hcchar = dwc_reg_read(hc_regs, DWC_HCCHAR);
		hcchar = DWC_HCCHAR_ENA_RW(hcchar, 1);
		hcchar = DWC_HCCHAR_DIS_RW(hcchar, 1);
		hcchar = DWC_HCCHAR_EPDIR_RW(hcchar, 0);
		dwc_reg_write(hc_regs, DWC_HCCHAR, hcchar);

		do {
			hcchar = dwc_reg_read(hc_regs, DWC_HCCHAR);
			if (++count > 200) {
				pr_err("%s: Unable to clear halt on "
				       "channel %d\n", __func__, i);
				break;
			}
			udelay(100);
		} while (DWC_HCCHAR_ENA_RD(hcchar));
	}

	/* Turn on the vbus power. */
	pr_info("Init: Port Power? op_state=%s\n",
		otg_state_string(core_if->xceiv->state));

	if (core_if->xceiv->state == OTG_STATE_A_HOST) {
		hprt0 = dwc_otg_read_hprt0(core_if);
		pr_info("Init: Power Port (%d)\n", DWC_HPRT0_PRT_PWR_RD(hprt0));
		if (DWC_HPRT0_PRT_PWR_RD(hprt0) == 0) {
			hprt0 = DWC_HPRT0_PRT_PWR_RW(hprt0, 1);
			dwc_reg_write(host_if->hprt0, 0, hprt0);
		}
	}
	dwc_otg_enable_host_interrupts(core_if);
}

/**
 * Initializes dynamic portions of the DWC_otg HCD state.
 */
static void hcd_reinit(struct dwc_hcd *hcd)
{
	struct list_head *item;
	int num_channels;
	u32 i;
	struct dwc_hc *channel;

	hcd->flags.d32 = 0;
	hcd->non_periodic_qh_ptr = &hcd->non_periodic_sched_active;
	hcd->available_host_channels = hcd->core_if->core_params->host_channels;

	/*
	 * Put all channels in the free channel list and clean up channel
	 * states.
	 */
	item = hcd->free_hc_list.next;
	while (item != &hcd->free_hc_list) {
		list_del(item);
		item = hcd->free_hc_list.next;
	}

	num_channels = hcd->core_if->core_params->host_channels;
	for (i = 0; i < num_channels; i++) {
		channel = hcd->hc_ptr_array[i];
		list_add_tail(&channel->hc_list_entry, &hcd->free_hc_list);
		dwc_otg_hc_cleanup(hcd->core_if, channel);
	}

	/* Initialize the DWC core for host mode operation. */
	dwc_otg_core_host_init(hcd->core_if);
}

/* Gets the dwc_hcd from a struct usb_hcd */
static inline struct dwc_hcd *hcd_to_dwc_otg_hcd(struct usb_hcd *hcd)
{
	return (struct dwc_hcd *)hcd->hcd_priv;
}

/**
 * Initializes the DWC_otg controller and its root hub and prepares it for host
 * mode operation. Activates the root port. Returns 0 on success and a negative
 * error code on failure.
*/
static int dwc_otg_hcd_start(struct usb_hcd *hcd)
{
	struct dwc_hcd *dwc_hcd = hcd_to_dwc_otg_hcd(hcd);
	struct usb_bus *bus = hcd_to_bus(hcd);

	hcd->state = HC_STATE_RUNNING;

	/* Inform the HUB driver to resume. */
	if (bus->root_hub)
		usb_hcd_resume_root_hub(hcd);

	hcd_reinit(dwc_hcd);
	return 0;
}

/**
 * Work queue function for starting the HCD when A-Cable is connected.
 * The dwc_otg_hcd_start() must be called in a process context.
 */
static void hcd_start_func(struct work_struct *work)
{
	struct dwc_hcd *priv = container_of(work, struct dwc_hcd, start_work);
	struct usb_hcd *usb_hcd = (struct usb_hcd *)priv->_p;

	if (usb_hcd)
		dwc_otg_hcd_start(usb_hcd);
}

/**
 * HCD Callback function for starting the HCD when A-Cable is
 * connected.
 */
static int dwc_otg_hcd_start_cb(void *_p)
{
	struct dwc_hcd *dwc_hcd = hcd_to_dwc_otg_hcd(_p);
	struct core_if *core_if = dwc_hcd->core_if;
	u32 hprt0;

	if (core_if->xceiv->state == OTG_STATE_B_HOST) {
		/*
		 * Reset the port.  During a HNP mode switch the reset
		 * needs to occur within 1ms and have a duration of at
		 * least 50ms.
		 */
		hprt0 = dwc_otg_read_hprt0(core_if);
		hprt0 = DWC_HPRT0_PRT_RST_RW(hprt0, 1);
		dwc_reg_write(core_if->host_if->hprt0, 0, hprt0);
		((struct usb_hcd *)_p)->self.is_b_host = 1;
	} else {
		((struct usb_hcd *)_p)->self.is_b_host = 0;
	}

	/* Need to start the HCD in a non-interrupt context. */
	dwc_hcd->_p = _p;
	schedule_work(&dwc_hcd->start_work);
	return 1;
}

/**
 * This function disables the Host Mode interrupts.
 */
static void dwc_otg_disable_host_interrupts(struct core_if *core_if)
{
	u32 global_regs = core_if->core_global_regs;
	u32 intr_mask = 0;

	/*
	 * Disable host mode interrupts without disturbing common
	 * interrupts.
	 */
	intr_mask |= DWC_INTMSK_STRT_OF_FRM;
	intr_mask |= DWC_INTMSK_HST_PORT;
	intr_mask |= DWC_INTMSK_HST_CHAN;
	intr_mask |= DWC_INTMSK_P_TXFIFO_EMPTY;
	intr_mask |= DWC_INTMSK_NP_TXFIFO_EMPT;
	dwc_reg_modify(global_regs, DWC_GINTMSK, intr_mask, 0);
}

/**
 * Halts the DWC_otg host mode operations in a clean manner. USB transfers are
 * stopped.
 */
static void dwc_otg_hcd_stop(struct usb_hcd *hcd)
{
	struct dwc_hcd *dwc_hcd = hcd_to_dwc_otg_hcd(hcd);
	u32 hprt0 = 0;

	/* Turn off all host-specific interrupts. */
	spin_lock(&dwc_hcd->lock);
	dwc_otg_disable_host_interrupts(dwc_hcd->core_if);
	spin_unlock(&dwc_hcd->lock);

	/*
	 * The root hub should be disconnected before this function is called.
	 * The disconnect will clear the QTD lists (via ..._hcd_urb_dequeue)
	 * and the QH lists (via ..._hcd_endpoint_disable).
	 */

	/* Turn off the vbus power */
	pr_info("PortPower off\n");
	hprt0 = DWC_HPRT0_PRT_PWR_RW(hprt0, 0);
	dwc_reg_write(dwc_hcd->core_if->host_if->hprt0, 0, hprt0);
}

/**
 * HCD Callback function for stopping the HCD.
 */
static int dwc_otg_hcd_stop_cb(void *_p)
{
	struct usb_hcd *usb_hcd = (struct usb_hcd *)_p;

	dwc_otg_hcd_stop(usb_hcd);
	return 1;
}

static void del_timers(struct dwc_hcd *hcd)
{
	del_timer_sync(&hcd->conn_timer);
}

/**
 * Processes all the URBs in a single list of QHs. Completes them with
 * -ETIMEDOUT and frees the QTD.
 */
static void kill_urbs_in_qh_list(struct dwc_hcd *hcd, struct list_head *qh_list)
{
	struct list_head *qh_item, *q;

	qh_item = qh_list->next;
	list_for_each_safe(qh_item, q, qh_list) {
		struct dwc_qh *qh;
		struct list_head *qtd_item;
		struct dwc_qtd *qtd;

		qh = list_entry(qh_item, struct dwc_qh, qh_list_entry);
		qtd_item = qh->qtd_list.next;
		qtd = list_entry(qtd_item, struct dwc_qtd, qtd_list_entry);
		if (qtd->urb != NULL) {
			spin_lock(&hcd->lock);
			dwc_otg_hcd_complete_urb(hcd, qtd->urb, -ETIMEDOUT);
			dwc_otg_hcd_qtd_remove_and_free(qtd);
			spin_unlock(&hcd->lock);
		}
	}
}

/**
 * Responds with an error status of ETIMEDOUT to all URBs in the non-periodic
 * and periodic schedules. The QTD associated with each URB is removed from
 * the schedule and freed. This function may be called when a disconnect is
 * detected or when the HCD is being stopped.
 */
static void kill_all_urbs(struct dwc_hcd *hcd)
{
	kill_urbs_in_qh_list(hcd, &hcd->non_periodic_sched_deferred);
	kill_urbs_in_qh_list(hcd, &hcd->non_periodic_sched_inactive);
	kill_urbs_in_qh_list(hcd, &hcd->non_periodic_sched_active);
	kill_urbs_in_qh_list(hcd, &hcd->periodic_sched_inactive);
	kill_urbs_in_qh_list(hcd, &hcd->periodic_sched_ready);
	kill_urbs_in_qh_list(hcd, &hcd->periodic_sched_assigned);
	kill_urbs_in_qh_list(hcd, &hcd->periodic_sched_queued);
}

/**
 * HCD Callback function for disconnect of the HCD.
 */
static int dwc_otg_hcd_disconnect_cb(void *_p)
{
	u32 intr;
	struct dwc_hcd *hcd = hcd_to_dwc_otg_hcd(_p);
	struct core_if *core_if = hcd->core_if;
	unsigned long flags = 0;

	/* Set status flags for the hub driver. */
	hcd->flags.b.port_connect_status_change = 1;
	hcd->flags.b.port_connect_status = 0;

	/*
	 * Shutdown any transfers in process by clearing the Tx FIFO Empty
	 * interrupt mask and status bits and disabling subsequent host
	 * channel interrupts.
	 */
	intr = 0;
	intr |= DWC_INTMSK_NP_TXFIFO_EMPT;
	intr |= DWC_INTMSK_P_TXFIFO_EMPTY;
	intr |= DWC_INTMSK_HST_CHAN;
	spin_lock_irqsave(&hcd->lock, flags);
	dwc_reg_modify(gintmsk_reg(hcd), 0, intr, 0);
	dwc_reg_modify(gintsts_reg(hcd), 0, intr, 0);
	spin_unlock_irqrestore(&hcd->lock, flags);

	del_timer(&hcd->conn_timer);

	/*
	 * Turn off the vbus power only if the core has transitioned to device
	 * mode. If still in host mode, need to keep power on to detect a
	 * reconnection.
	 */
	if (dwc_otg_is_device_mode(core_if)) {
		if (core_if->xceiv->state != OTG_STATE_A_SUSPEND) {
			u32 hprt0 = 0;

			pr_info("Disconnect: PortPower off\n");
			hprt0 = DWC_HPRT0_PRT_PWR_RW(hprt0, 0);
			dwc_reg_write(core_if->host_if->hprt0, 0, hprt0);
		}
		dwc_otg_disable_host_interrupts(core_if);
	}

	/* Respond with an error status to all URBs in the schedule. */
	kill_all_urbs(hcd);
	if (dwc_otg_is_host_mode(core_if)) {
		/* Clean up any host channels that were in use. */
		int num_channels;
		u32 i;
		struct dwc_hc *channel;
		ulong regs;
		u32 hcchar;

		num_channels = core_if->core_params->host_channels;
		if (!core_if->dma_enable) {
			/* Flush out any channel requests in slave mode. */
			for (i = 0; i < num_channels; i++) {
				channel = hcd->hc_ptr_array[i];
				if (list_empty(&channel->hc_list_entry)) {
					regs =
					    core_if->host_if->hc_regs[i];
					hcchar = dwc_reg_read(regs, DWC_HCCHAR);

					if (DWC_HCCHAR_ENA_RD(hcchar)) {
						hcchar =
						    DWC_HCCHAR_ENA_RW(hcchar,
								      0);
						hcchar =
						    DWC_HCCHAR_DIS_RW(hcchar,
								      1);
						hcchar =
						    DWC_HCCHAR_EPDIR_RW(hcchar,
									0);
						dwc_reg_write(regs, DWC_HCCHAR,
							    hcchar);
					}
				}
			}
		}

		for (i = 0; i < num_channels; i++) {
			channel = hcd->hc_ptr_array[i];
			if (list_empty(&channel->hc_list_entry)) {
				regs = core_if->host_if->hc_regs[i];
				hcchar = dwc_reg_read(regs, DWC_HCCHAR);

				if (DWC_HCCHAR_ENA_RD(hcchar)) {
					/* Halt the channel. */
					hcchar = DWC_HCCHAR_DIS_RW(hcchar, 1);
					dwc_reg_write(regs, DWC_HCCHAR, hcchar);
				}
				dwc_otg_hc_cleanup(core_if, channel);
				list_add_tail(&channel->hc_list_entry,
					      &hcd->free_hc_list);
			}
		}
	}

	/*
	 * A disconnect will end the session so the B-Device is no
	 * longer a B-host.
	 */
	((struct usb_hcd *)_p)->self.is_b_host = 0;
	return 1;
}

/**
 * Connection timeout function.  An OTG host is required to display a
 * message if the device does not connect within 10 seconds.
 */
static void dwc_otg_hcd_connect_timeout(unsigned long _ptr)
{
	pr_info("Connect Timeout\n");
	pr_err("Device Not Connected/Responding\n");
}

/**
 * Start the connection timer.  An OTG host is required to display a
 * message if the device does not connect within 10 seconds.  The
 * timer is deleted if a port connect interrupt occurs before the
 * timer expires.
 */
static void dwc_otg_hcd_start_connect_timer(struct dwc_hcd *hcd)
{
	init_timer(&hcd->conn_timer);
	hcd->conn_timer.function = dwc_otg_hcd_connect_timeout;
	hcd->conn_timer.data = (unsigned long)0;
	hcd->conn_timer.expires = jiffies + (HZ * 10);
	add_timer(&hcd->conn_timer);
}

/**
 * HCD Callback function for disconnect of the HCD.
 */
static int dwc_otg_hcd_session_start_cb(void *_p)
{
	struct dwc_hcd *hcd = hcd_to_dwc_otg_hcd(_p);

	dwc_otg_hcd_start_connect_timer(hcd);
	return 1;
}

/*
 * Reset Workqueue implementation
 */
static void port_reset_wqfunc(struct work_struct *work)
{
	struct dwc_hcd *hcd = container_of(work, struct dwc_hcd,
					   usb_port_reset);
	struct core_if *core_if = hcd->core_if;
	u32 hprt0 = 0;
	unsigned long flags;

	pr_info("%s\n", __func__);
	spin_lock_irqsave(&hcd->lock, flags);
	hprt0 = dwc_otg_read_hprt0(core_if);
	hprt0 = DWC_HPRT0_PRT_RST_RW(hprt0, 1);
	dwc_reg_write(core_if->host_if->hprt0, 0, hprt0);
	spin_unlock_irqrestore(&hcd->lock, flags);
	msleep(60);
	spin_lock_irqsave(&hcd->lock, flags);
	hprt0 = DWC_HPRT0_PRT_RST_RW(hprt0, 0);
	dwc_reg_write(core_if->host_if->hprt0, 0, hprt0);
	hcd->flags.b.port_reset_change = 1;
	spin_unlock_irqrestore(&hcd->lock, flags);
}

/*
 * Wakeup Workqueue implementation
 */
static void port_wakeup_wqfunc(struct work_struct *work)
{
	struct core_if *core_if = container_of(to_delayed_work(work),
					       struct core_if, usb_port_wakeup);
	u32 hprt0;

	pr_info("%s\n", __func__);
	/* Now wait for 70 ms. */
	hprt0 = dwc_otg_read_hprt0(core_if);
	msleep(70);
	hprt0 = DWC_HPRT0_PRT_RES_RW(hprt0, 0);
	dwc_reg_write(core_if->host_if->hprt0, 0, hprt0);
}

/**
 * Starts processing a USB transfer request specified by a USB Request Block
 * (URB). mem_flags indicates the type of memory allocation to use while
 * processing this URB.
 */
static int dwc_otg_hcd_urb_enqueue(struct usb_hcd *hcd, struct urb *urb,
				   gfp_t _mem_flags)
{
	int retval;
	unsigned long flags;
	struct dwc_hcd *dwc_hcd = hcd_to_dwc_otg_hcd(hcd);
	struct dwc_qtd *qtd;

	if (!dwc_hcd->flags.b.port_connect_status) {
		/* No longer connected. */
		retval = -ENODEV;
		goto err_enq;
	}

	qtd = dwc_otg_hcd_qtd_create(urb, _mem_flags);
	if (!qtd) {
		pr_err("DWC OTG HCD URB Enqueue failed creating " "QTD\n");
		retval = -ENOMEM;
		goto err_enq;
	}

	spin_lock_irqsave(&dwc_hcd->lock, flags);
	retval = usb_hcd_link_urb_to_ep(hcd, urb);
	if (unlikely(retval))
		goto fail;

	retval = dwc_otg_hcd_qtd_add(qtd, dwc_hcd);
	if (retval < 0) {
		pr_err("DWC OTG HCD URB Enqueue failed adding QTD. "
		       "Error status %d\n", retval);
		usb_hcd_unlink_urb_from_ep(hcd, urb);
		goto fail;
	}

fail:
	if (retval)
		dwc_otg_hcd_qtd_free(qtd);

	spin_unlock_irqrestore(&dwc_hcd->lock, flags);
err_enq:

	return retval;
}

/**
 * Attempts to halt a host channel. This function should only be called in
 * Slave mode or to abort a transfer in either Slave mode or DMA mode. Under
 * normal circumstances in DMA mode, the controller halts the channel when the
 * transfer is complete or a condition occurs that requires application
 * intervention.
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
void dwc_otg_hc_halt(struct core_if *core_if, struct dwc_hc *hc,
		     enum dwc_halt_status hlt_sts)
{
	u32 nptxsts;
	u32 hptxsts = 0;
	u32 hcchar;
	ulong hc_regs;
	ulong global_regs = core_if->core_global_regs;
	ulong host_global_regs;

	hc_regs = core_if->host_if->hc_regs[hc->hc_num];
	host_global_regs = core_if->host_if->host_global_regs;

	WARN_ON(hlt_sts == DWC_OTG_HC_XFER_NO_HALT_STATUS);

	if (hlt_sts == DWC_OTG_HC_XFER_URB_DEQUEUE ||
	    hlt_sts == DWC_OTG_HC_XFER_AHB_ERR) {
		/*
		 * Disable all channel interrupts except Ch Halted. The QTD
		 * and QH state associated with this transfer has been cleared
		 * (in the case of URB_DEQUEUE), so the channel needs to be
		 * shut down carefully to prevent crashes.
		 */
		u32 hcintmsk;
		hcintmsk = 0;
		hcintmsk = DWC_HCINTMSK_CHAN_HALTED_RW(hcintmsk, 1);
		dwc_reg_write(hc_regs, DWC_HCINTMSK, hcintmsk);

		/*
		 * Make sure no other interrupts besides halt are currently
		 * pending. Handling another interrupt could cause a crash due
		 * to the QTD and QH state.
		 */
		dwc_reg_write(hc_regs, DWC_HCINT, ~hcintmsk);

		/*
		 * Make sure the halt status is set to URB_DEQUEUE or AHB_ERR
		 * even if the channel was already halted for some other reason.
		 */
		hc->halt_status = hlt_sts;

		/*
		 * If the channel is not enabled, the channel is either already
		 * halted or it hasn't started yet. In DMA mode, the transfer
		 * may halt if it finishes normally or a condition occurs that
		 * requires driver intervention. Don't want to halt the channel
		 * again. In either Slave or DMA mode, it's possible that the
		 * transfer has been assigned to a channel, but not started yet
		 * when an URB is dequeued. Don't want to halt a channel that
		 * hasn't started yet.
		 */
		hcchar = dwc_reg_read(hc_regs, DWC_HCCHAR);
		if (!DWC_HCCHAR_ENA_RD(hcchar))
			return;
	}

	if (hc->halt_pending)
		/*
		 * A halt has already been issued for this channel. This might
		 * happen when a transfer is aborted by a higher level in
		 * the stack.
		 */
		return;

	hcchar = dwc_reg_read(hc_regs, DWC_HCCHAR);
	hcchar = DWC_HCCHAR_ENA_RW(hcchar, 1);
	hcchar = DWC_HCCHAR_DIS_RW(hcchar, 1);
	if (!core_if->dma_enable) {
		/* Check for space in the request queue to issue the halt. */
		if (hc->ep_type == DWC_OTG_EP_TYPE_CONTROL ||
		    hc->ep_type == DWC_OTG_EP_TYPE_BULK) {
			nptxsts = dwc_reg_read(global_regs, DWC_GNPTXSTS);

			if (!DWC_GNPTXSTS_NPTXQSPCAVAIL_RD(nptxsts))
				hcchar = DWC_HCCHAR_ENA_RW(hcchar, 0);
		} else {
			hptxsts =
			    dwc_reg_read(host_global_regs, DWC_HPTXSTS);

			if (!DWC_HPTXSTS_PTXSPC_AVAIL_RD(hptxsts) ||
			    core_if->queuing_high_bandwidth)
				hcchar = DWC_HCCHAR_ENA_RW(hcchar, 0);
		}
	}
	dwc_reg_write(hc_regs, DWC_HCCHAR, hcchar);

	hc->halt_status = hlt_sts;
	if (DWC_HCCHAR_ENA_RD(hcchar)) {
		hc->halt_pending = 1;
		hc->halt_on_queue = 0;
	} else {
		hc->halt_on_queue = 1;
	}
}

/**
 * Aborts/cancels a USB transfer request. Always returns 0 to indicate
 * success.
 */
static int dwc_otg_hcd_urb_dequeue(struct usb_hcd *hcd, struct urb *urb,
				   int status)
{
	unsigned long flags;
	struct dwc_hcd *dwc_hcd;
	struct dwc_qtd *urb_qtd;
	struct dwc_qh *qh;
	int retval;

	urb_qtd = (struct dwc_qtd *)urb->hcpriv;
	if (!urb_qtd)
		return -EINVAL;
	qh = (struct dwc_qh *)urb_qtd->qtd_qh_ptr;
	if (!qh)
		return -EINVAL;

	dwc_hcd = hcd_to_dwc_otg_hcd(hcd);
	spin_lock_irqsave(&dwc_hcd->lock, flags);

	retval = usb_hcd_check_unlink_urb(hcd, urb, status);
	if (retval) {
		spin_unlock_irqrestore(&dwc_hcd->lock, flags);
		return retval;
	}

	if (urb_qtd == qh->qtd_in_process) {
		/* The QTD is in process (it has been assigned to a channel). */
		if (dwc_hcd->flags.b.port_connect_status) {
			/*
			 * If still connected (i.e. in host mode), halt the
			 * channel so it can be used for other transfers. If
			 * no longer connected, the host registers can't be
			 * written to halt the channel since the core is in
			 * device mode.
			 */
			dwc_otg_hc_halt(dwc_hcd->core_if, qh->channel,
					DWC_OTG_HC_XFER_URB_DEQUEUE);
		}
	}

	/*
	 * Free the QTD and clean up the associated QH. Leave the QH in the
	 * schedule if it has any remaining QTDs.
	 */
	dwc_otg_hcd_qtd_remove_and_free(urb_qtd);
	if (qh && urb_qtd == qh->qtd_in_process) {
		dwc_otg_hcd_qh_deactivate(dwc_hcd, qh, 0);
		qh->channel = NULL;
		qh->qtd_in_process = NULL;
	} else if (qh && list_empty(&qh->qtd_list)) {
		dwc_otg_hcd_qh_remove(dwc_hcd, qh);
	}

	urb->hcpriv = NULL;
	usb_hcd_unlink_urb_from_ep(hcd, urb);
	spin_unlock_irqrestore(&dwc_hcd->lock, flags);

	/* Higher layer software sets URB status. */
	usb_hcd_giveback_urb(hcd, urb, status);

	return 0;
}

/* Remove and free a QH */
static inline void dwc_otg_hcd_qh_remove_and_free(struct dwc_hcd *hcd,
						  struct dwc_qh *qh)
{
	dwc_otg_hcd_qh_remove(hcd, qh);
	dwc_otg_hcd_qh_free(qh);
}

static void qh_list_free(struct dwc_hcd *hcd, struct list_head *_qh_list)
{
	struct list_head *item, *tmp;
	struct dwc_qh *qh;

	/* If the list hasn't been initialized yet, return. */
	if (_qh_list->next == NULL)
		return;

	/* Ensure there are no QTDs or URBs left. */
	kill_urbs_in_qh_list(hcd, _qh_list);

	list_for_each_safe(item, tmp, _qh_list) {
		qh = list_entry(item, struct dwc_qh, qh_list_entry);
		dwc_otg_hcd_qh_remove_and_free(hcd, qh);
	}
}

/**
 * Frees resources in the DWC_otg controller related to a given endpoint. Also
 * clears state in the HCD related to the endpoint. Any URBs for the endpoint
 * must already be dequeued.
 */
static void dwc_otg_hcd_endpoint_disable(struct usb_hcd *hcd,
					 struct usb_host_endpoint *ep)
{
	struct dwc_qh *qh;
	struct dwc_hcd *dwc_hcd = hcd_to_dwc_otg_hcd(hcd);
	unsigned long flags;

	spin_lock_irqsave(&dwc_hcd->lock, flags);
	qh = (struct dwc_qh *)ep->hcpriv;
	if (qh) {
		dwc_otg_hcd_qh_remove_and_free(dwc_hcd, qh);
		ep->hcpriv = NULL;
	}
	spin_unlock_irqrestore(&dwc_hcd->lock, flags);
}

/**
 * Creates Status Change bitmap for the root hub and root port. The bitmap is
 * returned in buf. Bit 0 is the status change indicator for the root hub. Bit 1
 * is the status change indicator for the single root port. Returns 1 if either
 * change indicator is 1, otherwise returns 0.
 */
static int dwc_otg_hcd_hub_status_data(struct usb_hcd *_hcd, char *buf)
{
	struct dwc_hcd *hcd = hcd_to_dwc_otg_hcd(_hcd);

	buf[0] = 0;
	buf[0] |= (hcd->flags.b.port_connect_status_change
		   || hcd->flags.b.port_reset_change
		   || hcd->flags.b.port_enable_change
		   || hcd->flags.b.port_suspend_change
		   || hcd->flags.b.port_over_current_change) << 1;

	return (buf[0] != 0);
}

/* Handles the hub class-specific ClearPortFeature request.*/
static int do_clear_port_feature(struct dwc_hcd *hcd, u16 val)
{
	struct core_if *core_if = hcd->core_if;
	u32 hprt0 = 0;
	unsigned long flags;

	spin_lock_irqsave(&hcd->lock, flags);
	switch (val) {
	case USB_PORT_FEAT_ENABLE:
		hprt0 = dwc_otg_read_hprt0(core_if);
		hprt0 = DWC_HPRT0_PRT_ENA_RW(hprt0, 1);
		dwc_reg_write(core_if->host_if->hprt0, 0, hprt0);
		break;
	case USB_PORT_FEAT_SUSPEND:
		hprt0 = dwc_otg_read_hprt0(core_if);
		hprt0 = DWC_HPRT0_PRT_RES_RW(hprt0, 1);
		dwc_reg_write(core_if->host_if->hprt0, 0, hprt0);

		/* Clear Resume bit */
		spin_unlock_irqrestore(&hcd->lock, flags);
		msleep(100);
		spin_lock_irqsave(&hcd->lock, flags);
		hprt0 = DWC_HPRT0_PRT_RES_RW(hprt0, 0);
		dwc_reg_write(core_if->host_if->hprt0, 0, hprt0);
		break;
	case USB_PORT_FEAT_POWER:
		hprt0 = dwc_otg_read_hprt0(core_if);
		hprt0 = DWC_HPRT0_PRT_PWR_RW(hprt0, 0);
		dwc_reg_write(core_if->host_if->hprt0, 0, hprt0);
		break;
	case USB_PORT_FEAT_INDICATOR:
		/* Port inidicator not supported */
		break;
	case USB_PORT_FEAT_C_CONNECTION:
		/* Clears drivers internal connect status change flag */
		hcd->flags.b.port_connect_status_change = 0;
		break;
	case USB_PORT_FEAT_C_RESET:
		/* Clears driver's internal Port Reset Change flag */
		hcd->flags.b.port_reset_change = 0;
		break;
	case USB_PORT_FEAT_C_ENABLE:
		/* Clears driver's internal Port Enable/Disable Change flag  */
		hcd->flags.b.port_enable_change = 0;
		break;
	case USB_PORT_FEAT_C_SUSPEND:
		/*
		 * Clears the driver's internal Port Suspend
		 * Change flag, which is set when resume signaling on
		 * the host port is complete
		 */
		hcd->flags.b.port_suspend_change = 0;
		break;
	case USB_PORT_FEAT_C_OVER_CURRENT:
		hcd->flags.b.port_over_current_change = 0;
		break;
	default:
		pr_err("DWC OTG HCD - ClearPortFeature request %xh "
		       "unknown or unsupported\n", val);
		spin_unlock_irqrestore(&hcd->lock, flags);
		return -EINVAL;
	}
	spin_unlock_irqrestore(&hcd->lock, flags);
	return 0;
}

/* Handles the hub class-specific SetPortFeature request.*/
static int do_set_port_feature(struct usb_hcd *hcd, u16 val, u16 index)
{
	struct core_if *core_if = hcd_to_dwc_otg_hcd(hcd)->core_if;
	u32 hprt0 = 0;
	struct dwc_hcd *dwc_hcd = hcd_to_dwc_otg_hcd(hcd);
	unsigned long flags;
	u32 pcgcctl = 0;

	spin_lock_irqsave(&dwc_hcd->lock, flags);

	switch (val) {
	case USB_PORT_FEAT_SUSPEND:
		if (hcd->self.otg_port == index && hcd->self.b_hnp_enable) {
			u32 gotgctl = 0;
			gotgctl |= DWC_GCTL_HOST_HNP_ENA;
			dwc_reg_modify(core_if->core_global_regs,
				     DWC_GOTGCTL, 0, gotgctl);
			core_if->xceiv->state = OTG_STATE_A_SUSPEND;
		}

		hprt0 = dwc_otg_read_hprt0(core_if);
		hprt0 = DWC_HPRT0_PRT_SUS_RW(hprt0, 1);
		dwc_reg_write(core_if->host_if->hprt0, 0, hprt0);

		/* Suspend the Phy Clock */
		pcgcctl = DWC_PCGCCTL_STOP_CLK_SET(pcgcctl);
		dwc_reg_write(core_if->pcgcctl, 0, pcgcctl);

		/* For HNP the bus must be suspended for at least 200ms. */
		if (hcd->self.b_hnp_enable) {
			spin_unlock_irqrestore(&dwc_hcd->lock, flags);
			msleep(200);
			spin_lock_irqsave(&dwc_hcd->lock, flags);
		}
		break;
	case USB_PORT_FEAT_POWER:
		hprt0 = dwc_otg_read_hprt0(core_if);
		hprt0 = DWC_HPRT0_PRT_PWR_RW(hprt0, 1);
		dwc_reg_write(core_if->host_if->hprt0, 0, hprt0);
		break;
	case USB_PORT_FEAT_RESET:
		hprt0 = dwc_otg_read_hprt0(core_if);

		/*
		 * When B-Host the Port reset bit is set in the Start HCD
		 * Callback function, so that the reset is started within 1ms
		 * of the HNP success interrupt.
		 */
		if (!hcd->self.is_b_host) {
			hprt0 = DWC_HPRT0_PRT_RST_RW(hprt0, 1);
			dwc_reg_write(core_if->host_if->hprt0, 0, hprt0);
		}

		/* Clear reset bit in 10ms (FS/LS) or 50ms (HS) */
		spin_unlock_irqrestore(&dwc_hcd->lock, flags);
		msleep(60);
		spin_lock_irqsave(&dwc_hcd->lock, flags);
		hprt0 = DWC_HPRT0_PRT_RST_RW(hprt0, 0);
		dwc_reg_write(core_if->host_if->hprt0, 0, hprt0);
		break;
	case USB_PORT_FEAT_INDICATOR:
		/* Not supported */
		break;
	default:
		pr_err("DWC OTG HCD - "
		       "SetPortFeature request %xh "
		       "unknown or unsupported\n", val);
		spin_unlock_irqrestore(&dwc_hcd->lock, flags);
		return -EINVAL;
	}
	spin_unlock_irqrestore(&dwc_hcd->lock, flags);
	return 0;
}

/* Handles hub class-specific requests.*/
static int dwc_otg_hcd_hub_control(struct usb_hcd *hcd, u16 req_type, u16 val,
				   u16 index, char *buf, u16 len)
{
	int retval = 0;
	struct dwc_hcd *dwc_hcd = hcd_to_dwc_otg_hcd(hcd);
	struct core_if *core_if = hcd_to_dwc_otg_hcd(hcd)->core_if;
	struct usb_hub_descriptor *desc;
	u32 hprt0 = 0;
	u32 port_status;
	unsigned long flags;

	spin_lock_irqsave(&dwc_hcd->lock, flags);
	switch (req_type) {
	case ClearHubFeature:
		switch (val) {
		case C_HUB_LOCAL_POWER:
		case C_HUB_OVER_CURRENT:
			/* Nothing required here */
			break;
		default:
			retval = -EINVAL;
			pr_err("DWC OTG HCD - ClearHubFeature request"
			       " %xh unknown\n", val);
		}
		break;
	case ClearPortFeature:
		if (!index || index > 1)
			goto error;
		spin_unlock_irqrestore(&dwc_hcd->lock, flags);
		retval = do_clear_port_feature(dwc_hcd, val);
		spin_lock_irqsave(&dwc_hcd->lock, flags);
		break;
	case GetHubDescriptor:
		desc = (struct usb_hub_descriptor *)buf;
		desc->bDescLength = 9;
		desc->bDescriptorType = 0x29;
		desc->bNbrPorts = 1;
		desc->wHubCharacteristics = 0x08;
		desc->bPwrOn2PwrGood = 1;
		desc->bHubContrCurrent = 0;
		break;
	case GetHubStatus:
		memset(buf, 0, 4);
		break;
	case GetPortStatus:
		if (!index || index > 1)
			goto error;

		port_status = 0;
		if (dwc_hcd->flags.b.port_connect_status_change)
			port_status |= (1 << USB_PORT_FEAT_C_CONNECTION);
		if (dwc_hcd->flags.b.port_enable_change)
			port_status |= (1 << USB_PORT_FEAT_C_ENABLE);
		if (dwc_hcd->flags.b.port_suspend_change)
			port_status |= (1 << USB_PORT_FEAT_C_SUSPEND);
		if (dwc_hcd->flags.b.port_reset_change)
			port_status |= (1 << USB_PORT_FEAT_C_RESET);
		if (dwc_hcd->flags.b.port_over_current_change) {
			pr_err("Device Not Supported\n");
			port_status |= (1 << USB_PORT_FEAT_C_OVER_CURRENT);
		}
		if (!dwc_hcd->flags.b.port_connect_status) {
			/*
			 * The port is disconnected, which means the core is
			 * either in device mode or it soon will be. Just
			 * return 0's for the remainder of the port status
			 * since the port register can't be read if the core
			 * is in device mode.
			 */
			*((__le32 *) buf) = cpu_to_le32(port_status);
			break;
		}

		hprt0 = dwc_reg_read(core_if->host_if->hprt0, 0);

		if (DWC_HPRT0_PRT_STS_RD(hprt0))
			port_status |= USB_PORT_STAT_CONNECTION;
		if (DWC_HPRT0_PRT_ENA_RD(hprt0))
			port_status |= USB_PORT_STAT_ENABLE;
		if (DWC_HPRT0_PRT_SUS_RD(hprt0))
			port_status |= USB_PORT_STAT_SUSPEND;
		if (DWC_HPRT0_PRT_OVRCURR_ACT_RD(hprt0))
			port_status |= USB_PORT_STAT_OVERCURRENT;
		if (DWC_HPRT0_PRT_RST_RD(hprt0))
			port_status |= USB_PORT_STAT_RESET;
		if (DWC_HPRT0_PRT_PWR_RD(hprt0))
			port_status |= USB_PORT_STAT_POWER;

		if (DWC_HPRT0_PRT_SPD_RD(hprt0) == DWC_HPRT0_PRTSPD_HIGH_SPEED)
			port_status |= USB_PORT_STAT_HIGH_SPEED;
		else if (DWC_HPRT0_PRT_SPD_RD(hprt0) ==
			 DWC_HPRT0_PRTSPD_LOW_SPEED)
			port_status |= USB_PORT_STAT_LOW_SPEED;

		if (DWC_HPRT0_PRT_TST_CTL_RD(hprt0))
			port_status |= (1 << USB_PORT_FEAT_TEST);

		/* USB_PORT_FEAT_INDICATOR unsupported always 0 */
		*((__le32 *) buf) = cpu_to_le32(port_status);
		break;
	case SetHubFeature:
		/* No HUB features supported */
		break;
	case SetPortFeature:
		if (val != USB_PORT_FEAT_TEST && (!index || index > 1))
			goto error;

		if (!dwc_hcd->flags.b.port_connect_status) {
			/*
			 * The port is disconnected, which means the core is
			 * either in device mode or it soon will be. Just
			 * return without doing anything since the port
			 * register can't be written if the core is in device
			 * mode.
			 */
			break;
		}
		spin_unlock_irqrestore(&dwc_hcd->lock, flags);
		retval = do_set_port_feature(hcd, val, index);
		spin_lock_irqsave(&dwc_hcd->lock, flags);
		break;
	default:
error:
		retval = -EINVAL;
		pr_warning("DWC OTG HCD - Unknown hub control request"
			   " type or invalid req_type: %xh index: %xh "
			   "val: %xh\n", req_type, index, val);
		break;
	}
	spin_unlock_irqrestore(&dwc_hcd->lock, flags);
	return retval;
}

/**
 * Handles host mode interrupts for the DWC_otg controller. Returns IRQ_NONE if
 * there was no interrupt to handle. Returns IRQ_HANDLED if there was a valid
 * interrupt.
 *
 * This function is called by the USB core when an interrupt occurs
 */
static irqreturn_t dwc_otg_hcd_irq(struct usb_hcd *hcd)
{
	struct dwc_hcd *dwc_hcd = hcd_to_dwc_otg_hcd(hcd);

	return IRQ_RETVAL(dwc_otg_hcd_handle_intr(dwc_hcd));
}
static const struct hc_driver dwc_otg_hc_driver = {
	.description = dwc_otg_hcd_name,
	.product_desc = "DWC OTG Controller",
	.hcd_priv_size = sizeof(struct dwc_hcd),
	.irq = dwc_otg_hcd_irq,
	.flags = HCD_MEMORY | HCD_USB2,
	.start = dwc_otg_hcd_start,
	.stop = dwc_otg_hcd_stop,
	.urb_enqueue = dwc_otg_hcd_urb_enqueue,
	.urb_dequeue = dwc_otg_hcd_urb_dequeue,
	.endpoint_disable = dwc_otg_hcd_endpoint_disable,
	.get_frame_number = dwc_otg_hcd_get_frame_number,
	.hub_status_data = dwc_otg_hcd_hub_status_data,
	.hub_control = dwc_otg_hcd_hub_control,
};

/**
 * Frees secondary storage associated with the dwc_hcd structure contained
 * in the struct usb_hcd field.
 */
static void dwc_otg_hcd_free(struct usb_hcd *hcd)
{
	struct dwc_hcd *dwc_hcd = hcd_to_dwc_otg_hcd(hcd);
	u32 i;

	del_timers(dwc_hcd);

	/* Free memory for QH/QTD lists */
	qh_list_free(dwc_hcd, &dwc_hcd->non_periodic_sched_inactive);
	qh_list_free(dwc_hcd, &dwc_hcd->non_periodic_sched_deferred);
	qh_list_free(dwc_hcd, &dwc_hcd->non_periodic_sched_active);
	qh_list_free(dwc_hcd, &dwc_hcd->periodic_sched_inactive);
	qh_list_free(dwc_hcd, &dwc_hcd->periodic_sched_ready);
	qh_list_free(dwc_hcd, &dwc_hcd->periodic_sched_assigned);
	qh_list_free(dwc_hcd, &dwc_hcd->periodic_sched_queued);

	/* Free memory for the host channels. */
	for (i = 0; i < MAX_EPS_CHANNELS; i++) {
		struct dwc_hc *hc = dwc_hcd->hc_ptr_array[i];

		kfree(hc);
	}
	if (dwc_hcd->core_if->dma_enable) {
		if (dwc_hcd->status_buf_dma)
			dma_free_coherent(hcd->self.controller,
					  DWC_OTG_HCD_STATUS_BUF_SIZE,
					  dwc_hcd->status_buf,
					  dwc_hcd->status_buf_dma);
	} else {
		kfree(dwc_hcd->status_buf);
	}

}

/**
 * Initializes the HCD. This function allocates memory for and initializes the
 * static parts of the usb_hcd and dwc_hcd structures. It also registers the
 * USB bus with the core and calls the hc_driver->start() function. It returns
 * a negative error on failure.
 */
int dwc_otg_hcd_init(struct device *_dev,
			       struct dwc_otg_device *dwc_otg_device)
{
	struct usb_hcd *hcd;
	struct dwc_hcd *dwc_hcd;
	struct dwc_otg_device *otg_dev = dev_get_drvdata(_dev);
	struct cil_callbacks *callback = kmalloc(sizeof(callback), GFP_KERNEL);
	int num_channels;
	u32 i;
	struct dwc_hc *channel;
	int retval = 0;

	/*
	 * Allocate memory for the base HCD plus the DWC OTG HCD.
	 * Initialize the base HCD.
	 */
	hcd = usb_create_hcd(&dwc_otg_hc_driver, _dev, dwc_otg_hcd_name);
	if (!hcd) {
		retval = -ENOMEM;
		goto error1;
	}
	dev_set_drvdata(_dev, dwc_otg_device);
	hcd->regs = otg_dev->base;
	hcd->rsrc_start = otg_dev->phys_addr;
	hcd->rsrc_len = otg_dev->base_len;
	hcd->self.otg_port = 1;

	/* Initialize the DWC OTG HCD. */
	dwc_hcd = hcd_to_dwc_otg_hcd(hcd);
	dwc_hcd->core_if = otg_dev->core_if;
	spin_lock_init(&dwc_hcd->lock);
	otg_dev->hcd = dwc_hcd;

	/* Register the HCD CIL Callbacks */
	callback->start = &dwc_otg_hcd_start_cb;
	callback->stop = &dwc_otg_hcd_stop_cb;
	callback->disconnect = &dwc_otg_hcd_disconnect_cb;
	callback->session_start = &dwc_otg_hcd_session_start_cb;
	callback->p = NULL;

	dwc_otg_cil_register_hcd_callbacks(otg_dev->core_if, callback,
					   hcd);

	/* Initialize the non-periodic schedule. */
	INIT_LIST_HEAD(&dwc_hcd->non_periodic_sched_inactive);
	INIT_LIST_HEAD(&dwc_hcd->non_periodic_sched_active);
	INIT_LIST_HEAD(&dwc_hcd->non_periodic_sched_deferred);

	/* Initialize the periodic schedule. */
	INIT_LIST_HEAD(&dwc_hcd->periodic_sched_inactive);
	INIT_LIST_HEAD(&dwc_hcd->periodic_sched_ready);
	INIT_LIST_HEAD(&dwc_hcd->periodic_sched_assigned);
	INIT_LIST_HEAD(&dwc_hcd->periodic_sched_queued);

	/*
	 * Create a host channel descriptor for each host channel implemented
	 * in the controller. Initialize the channel descriptor array.
	 */
	INIT_LIST_HEAD(&dwc_hcd->free_hc_list);
	num_channels = dwc_hcd->core_if->core_params->host_channels;

	for (i = 0; i < num_channels; i++) {
		channel = kzalloc(sizeof(struct dwc_hc), GFP_KERNEL);
		if (!channel) {
			retval = -ENOMEM;
			pr_err("%s: host channel allocation failed\n",
			       __func__);
			goto error2;
		}

		channel->hc_num = i;
		dwc_hcd->hc_ptr_array[i] = channel;
	}

	/* Initialize the Connection timeout timer. */
	init_timer(&dwc_hcd->conn_timer);

	/* Initialize workqueue */
	INIT_WORK(&dwc_hcd->usb_port_reset, port_reset_wqfunc);
	INIT_WORK(&dwc_hcd->start_work, hcd_start_func);
	INIT_WORK(&dwc_hcd->core_if->usb_port_otg, NULL);
	INIT_DELAYED_WORK(&dwc_hcd->core_if->usb_port_wakeup,
			  port_wakeup_wqfunc);

	/* Set device flags indicating whether the HCD supports DMA. */
	if (otg_dev->core_if->dma_enable) {
		static u64 dummy_mask = DMA_BIT_MASK(32);

		pr_info("Using DMA mode\n");
		_dev->dma_mask = (void *)&dummy_mask;
		_dev->coherent_dma_mask = ~0;
	} else {
		pr_info("Using Slave mode\n");
		_dev->dma_mask = (void *)0;
		_dev->coherent_dma_mask = 0;
	}

	init_hcd_usecs(dwc_hcd);
	/*
	 * Finish generic HCD initialization and start the HCD. This function
	 * allocates the DMA buffer pool, registers the USB bus, requests the
	 * IRQ line, and calls dwc_otg_hcd_start method.
	 */
	retval = usb_add_hcd(hcd, otg_dev->irq, IRQF_SHARED);
	if (retval < 0)
		goto error2;
	hcd->rsrc_start = otg_dev->phys_addr;
	hcd->rsrc_len = otg_dev->base_len;

	/*
	 * Allocate space for storing data on status transactions. Normally no
	 * data is sent, but this space acts as a bit bucket. This must be
	 * done after usb_add_hcd since that function allocates the DMA buffer
	 * pool.
	 */
	if (otg_dev->core_if->dma_enable) {
		dwc_hcd->status_buf =
		    dma_alloc_coherent(_dev, DWC_OTG_HCD_STATUS_BUF_SIZE,
				       &dwc_hcd->status_buf_dma,
				       GFP_KERNEL | GFP_DMA);
	} else {
		dwc_hcd->status_buf = kmalloc(DWC_OTG_HCD_STATUS_BUF_SIZE,
					      GFP_KERNEL);
	}
	if (!dwc_hcd->status_buf) {
		retval = -ENOMEM;
		pr_err("%s: status_buf allocation failed\n", __func__);
		goto error3;
	}
	return 0;

error3:
	usb_remove_hcd(hcd);
error2:
	kfree(callback);
	dwc_otg_hcd_free(hcd);
	usb_put_hcd(hcd);
error1:
	return retval;
}

/**
 * Removes the HCD.
 * Frees memory and resources associated with the HCD and deregisters the bus.
 */
void dwc_otg_hcd_remove(struct device *_dev)
{
	struct dwc_otg_device *otg_dev = dev_get_drvdata(_dev);
	struct dwc_hcd *dwc_hcd = otg_dev->hcd;
	struct usb_hcd *hcd = dwc_otg_hcd_to_hcd(dwc_hcd);
	unsigned long flags = 0;

	/* Turn off all interrupts */
	spin_lock_irqsave(&dwc_hcd->lock, flags);
	dwc_reg_write(gintmsk_reg(dwc_hcd), 0, 0);
	spin_unlock_irqrestore(&dwc_hcd->lock, flags);
	dwc_reg_modify(gahbcfg_reg(dwc_hcd), 0, 1, 0);

	cancel_work_sync(&dwc_hcd->start_work);
	cancel_work_sync(&dwc_hcd->usb_port_reset);
	cancel_work_sync(&dwc_hcd->core_if->usb_port_otg);

	usb_remove_hcd(hcd);
	dwc_otg_hcd_free(hcd);
	usb_put_hcd(hcd);
}

/** Returns the current frame number. */
int dwc_otg_hcd_get_frame_number(struct usb_hcd *hcd)
{
	struct dwc_hcd *dwc_hcd = hcd_to_dwc_otg_hcd(hcd);
	u32 hfnum = 0;

	hfnum = dwc_reg_read(dwc_hcd->core_if->host_if->
			   host_global_regs, DWC_HFNUM);

	return DWC_HFNUM_FRNUM_RD(hfnum);
}

/**
 * Prepares a host channel for transferring packets to/from a specific
 * endpoint. The HCCHARn register is set up with the characteristics specified
 * in _hc. Host channel interrupts that may need to be serviced while this
 * transfer is in progress are enabled.
 */
static void dwc_otg_hc_init(struct core_if *core_if, struct dwc_hc *hc)
{
	u32 intr_enable;
	ulong global_regs = core_if->core_global_regs;
	u32 hc_intr_mask = 0;
	u32 gintmsk = 0;
	u32 hcchar;
	u32 hcsplt;
	u8 hc_num = hc->hc_num;
	struct dwc_host_if *host_if = core_if->host_if;
	ulong hc_regs = host_if->hc_regs[hc_num];

	/* Clear old interrupt conditions for this host channel. */
	hc_intr_mask = 0x3FF;
	dwc_reg_write(hc_regs, DWC_HCINT, hc_intr_mask);

	/* Enable channel interrupts required for this transfer. */
	hc_intr_mask = 0;
	hc_intr_mask = DWC_HCINTMSK_CHAN_HALTED_RW(hc_intr_mask, 1);
	if (core_if->dma_enable) {
		hc_intr_mask = DWC_HCINTMSK_AHB_ERR_RW(hc_intr_mask, 1);

		if (hc->error_state && !hc->do_split &&
		    hc->ep_type != DWC_OTG_EP_TYPE_ISOC) {
			hc_intr_mask =
			    DWC_HCINTMSK_ACK_RESP_REC_RW(hc_intr_mask, 1);
			if (hc->ep_is_in) {
				hc_intr_mask =
				    DWC_HCINTMSK_DATA_TOG_ERR_RW(hc_intr_mask,
								 1);
				if (hc->ep_type != DWC_OTG_EP_TYPE_INTR)
					hc_intr_mask =
					    DWC_HCINTMSK_NAK_RESP_REC_RW
					    (hc_intr_mask, 1);
			}
		}
	} else {
		switch (hc->ep_type) {
		case DWC_OTG_EP_TYPE_CONTROL:
		case DWC_OTG_EP_TYPE_BULK:
			hc_intr_mask =
			    DWC_HCINTMSK_TXFER_CMPL_RW(hc_intr_mask, 1);
			hc_intr_mask =
			    DWC_HCINTMSK_STALL_RESP_REC_RW(hc_intr_mask, 1);
			hc_intr_mask =
			    DWC_HCINTMSK_TRANS_ERR_RW(hc_intr_mask, 1);
			hc_intr_mask =
			    DWC_HCINTMSK_DATA_TOG_ERR_RW(hc_intr_mask, 1);

			if (hc->ep_is_in) {
				hc_intr_mask =
				    DWC_HCINTMSK_BBL_ERR_RW(hc_intr_mask, 1);
			} else {
				hc_intr_mask =
				    DWC_HCINTMSK_NAK_RESP_REC_RW(hc_intr_mask,
								 1);
				hc_intr_mask =
				    DWC_HCINTMSK_NYET_RESP_REC_RW(hc_intr_mask,
								  1);
				if (hc->do_ping)
					hc_intr_mask =
					    DWC_HCINTMSK_ACK_RESP_REC_RW
					    (hc_intr_mask, 1);
			}

			if (hc->do_split) {
				hc_intr_mask =
				    DWC_HCINTMSK_NAK_RESP_REC_RW(hc_intr_mask,
								 1);
				if (hc->complete_split)
					hc_intr_mask =
					    DWC_HCINTMSK_NYET_RESP_REC_RW
					    (hc_intr_mask, 1);
				else
					hc_intr_mask =
					    DWC_HCINTMSK_ACK_RESP_REC_RW
					    (hc_intr_mask, 1);
			}

			if (hc->error_state)
				hc_intr_mask =
				    DWC_HCINTMSK_ACK_RESP_REC_RW(hc_intr_mask,
								 1);
			break;
		case DWC_OTG_EP_TYPE_INTR:
			hc_intr_mask =
			    DWC_HCINTMSK_TXFER_CMPL_RW(hc_intr_mask, 1);
			hc_intr_mask =
			    DWC_HCINTMSK_NAK_RESP_REC_RW(hc_intr_mask, 1);
			hc_intr_mask =
			    DWC_HCINTMSK_STALL_RESP_REC_RW(hc_intr_mask, 1);
			hc_intr_mask =
			    DWC_HCINTMSK_TRANS_ERR_RW(hc_intr_mask, 1);
			hc_intr_mask =
			    DWC_HCINTMSK_DATA_TOG_ERR_RW(hc_intr_mask, 1);
			hc_intr_mask =
			    DWC_HCINTMSK_FRAME_OVERN_ERR_RW(hc_intr_mask, 1);

			if (hc->ep_is_in)
				hc_intr_mask =
				    DWC_HCINTMSK_BBL_ERR_RW(hc_intr_mask, 1);
			if (hc->error_state)
				hc_intr_mask =
				    DWC_HCINTMSK_ACK_RESP_REC_RW(hc_intr_mask,
								 1);

			if (hc->do_split) {
				if (hc->complete_split)
					hc_intr_mask =
					    DWC_HCINTMSK_NYET_RESP_REC_RW
					    (hc_intr_mask, 1);
				else
					hc_intr_mask =
					    DWC_HCINTMSK_ACK_RESP_REC_RW
					    (hc_intr_mask, 1);
			}
			break;
		case DWC_OTG_EP_TYPE_ISOC:
			hc_intr_mask =
			    DWC_HCINTMSK_TXFER_CMPL_RW(hc_intr_mask, 1);
			hc_intr_mask =
			    DWC_HCINTMSK_FRAME_OVERN_ERR_RW(hc_intr_mask, 1);
			hc_intr_mask =
			    DWC_HCINTMSK_ACK_RESP_REC_RW(hc_intr_mask, 1);

			if (hc->ep_is_in) {
				hc_intr_mask =
				    DWC_HCINTMSK_TRANS_ERR_RW(hc_intr_mask, 1);
				hc_intr_mask =
				    DWC_HCINTMSK_BBL_ERR_RW(hc_intr_mask, 1);
			}
			break;
		}
	}
	dwc_reg_write(hc_regs, DWC_HCINTMSK, hc_intr_mask);

	/* Enable the top level host channel interrupt. */
	intr_enable = (1 << hc_num);
	dwc_reg_modify(host_if->host_global_regs, DWC_HAINTMSK, 0,
		     intr_enable);

	/* Make sure host channel interrupts are enabled. */
	gintmsk |= DWC_INTMSK_HST_CHAN;
	dwc_reg_modify(global_regs, DWC_GINTMSK, 0, gintmsk);

	/*
	 * Program the HCCHARn register with the endpoint characteristics for
	 * the current transfer.
	 */
	hcchar = 0;
	hcchar = DWC_HCCHAR_DEV_ADDR_RW(hcchar, hc->dev_addr);
	hcchar = DWC_HCCHAR_EP_NUM_RW(hcchar, hc->ep_num);
	hcchar = DWC_HCCHAR_EPDIR_RW(hcchar, hc->ep_is_in);
	hcchar = DWC_HCCHAR_LSP_DEV_RW(hcchar, (hc->speed ==
						DWC_OTG_EP_SPEED_LOW));
	hcchar = DWC_HCCHAR_EPTYPE_RW(hcchar, hc->ep_type);
	hcchar = DWC_HCCHAR_MPS_RW(hcchar, hc->max_packet);
	dwc_reg_write(host_if->hc_regs[hc_num], DWC_HCCHAR, hcchar);

	/* Program the HCSPLIT register for SPLITs */
	hcsplt = 0;
	if (hc->do_split) {
		hcsplt = DWC_HCSPLT_COMP_SPLT_RW(hcsplt, hc->complete_split);
		hcsplt = DWC_HCSPLT_TRANS_POS_RW(hcsplt, hc->xact_pos);
		hcsplt = DWC_HCSPLT_HUB_ADDR_RW(hcsplt, hc->hub_addr);
		hcsplt = DWC_HCSPLT_PRT_ADDR_RW(hcsplt, hc->port_addr);
	}
	dwc_reg_write(host_if->hc_regs[hc_num], DWC_HCSPLT, hcsplt);
}

/**
 * Assigns transactions from a QTD to a free host channel and initializes the
 * host channel to perform the transactions. The host channel is removed from
 * the free list.
 */
static void assign_and_init_hc(struct dwc_hcd *hcd, struct dwc_qh *qh)
{
	struct dwc_hc *hc;
	struct dwc_qtd *qtd;
	struct urb *urb;
	struct usb_iso_packet_descriptor *frame_desc;

	hc = list_entry(hcd->free_hc_list.next, struct dwc_hc, hc_list_entry);

	/* Remove the host channel from the free list. */
	list_del_init(&hc->hc_list_entry);
	qtd = list_entry(qh->qtd_list.next, struct dwc_qtd, qtd_list_entry);
	urb = qtd->urb;
	qh->channel = hc;
	qh->qtd_in_process = qtd;

	/*
	 * Use usb_pipedevice to determine device address. This address is
	 * 0 before the SET_ADDRESS command and the correct address afterward.
	 */
	hc->dev_addr = usb_pipedevice(urb->pipe);
	hc->ep_num = usb_pipeendpoint(urb->pipe);

	if (urb->dev->speed == USB_SPEED_LOW)
		hc->speed = DWC_OTG_EP_SPEED_LOW;
	else if (urb->dev->speed == USB_SPEED_FULL)
		hc->speed = DWC_OTG_EP_SPEED_FULL;
	else
		hc->speed = DWC_OTG_EP_SPEED_HIGH;

	hc->max_packet = dwc_max_packet(qh->maxp);
	hc->xfer_started = 0;
	hc->halt_status = DWC_OTG_HC_XFER_NO_HALT_STATUS;
	hc->error_state = (qtd->error_count > 0);
	hc->halt_on_queue = 0;
	hc->halt_pending = 0;
	hc->requests = 0;

	/*
	 * The following values may be modified in the transfer type section
	 * below. The xfer_len value may be reduced when the transfer is
	 * started to accommodate the max widths of the XferSize and PktCnt
	 * fields in the HCTSIZn register.
	 */
	hc->do_ping = qh->ping_state;
	hc->ep_is_in = (usb_pipein(urb->pipe) != 0);
	hc->data_pid_start = qh->data_toggle;
	hc->multi_count = 1;

	if (hcd->core_if->dma_enable)
		hc->xfer_buff = urb->transfer_dma + (u8 *) urb->actual_length;
	else
		hc->xfer_buff = (u8 *) urb->transfer_buffer +
		    urb->actual_length;

	hc->xfer_len = urb->transfer_buffer_length - urb->actual_length;
	hc->xfer_count = 0;

	/*
	 * Set the split attributes
	 */
	hc->do_split = 0;
	if (qh->do_split) {
		hc->do_split = 1;
		hc->xact_pos = qtd->isoc_split_pos;
		hc->complete_split = qtd->complete_split;
		hc->hub_addr = urb->dev->tt->hub->devnum;
		hc->port_addr = urb->dev->ttport;
	}

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_CONTROL:
		hc->ep_type = DWC_OTG_EP_TYPE_CONTROL;

		switch (qtd->control_phase) {
		case DWC_OTG_CONTROL_SETUP:
			hc->do_ping = 0;
			hc->ep_is_in = 0;
			hc->data_pid_start = DWC_OTG_HC_PID_SETUP;

			if (hcd->core_if->dma_enable)
				hc->xfer_buff = (u8 *) (u32) urb->setup_dma;
			else
				hc->xfer_buff = (u8 *) urb->setup_packet;

			hc->xfer_len = 8;
			break;
		case DWC_OTG_CONTROL_DATA:
			hc->data_pid_start = qtd->data_toggle;
			break;
		case DWC_OTG_CONTROL_STATUS:
			/*
			 * Direction is opposite of data direction or IN if no
			 * data.
			 */
			if (urb->transfer_buffer_length == 0)
				hc->ep_is_in = 1;
			else
				hc->ep_is_in = (usb_pipein(urb->pipe) !=
						USB_DIR_IN);

			if (hc->ep_is_in)
				hc->do_ping = 0;

			hc->data_pid_start = DWC_OTG_HC_PID_DATA1;
			hc->xfer_len = 0;
			if (hcd->core_if->dma_enable)
				hc->xfer_buff =
				    (u8 *) (u32) hcd->status_buf_dma;
			else
				hc->xfer_buff = (u8 *) hcd->status_buf;
			break;
		}
		break;
	case PIPE_BULK:
		hc->ep_type = DWC_OTG_EP_TYPE_BULK;
		break;
	case PIPE_INTERRUPT:
		hc->ep_type = DWC_OTG_EP_TYPE_INTR;
		break;
	case PIPE_ISOCHRONOUS:
		frame_desc = &urb->iso_frame_desc[qtd->isoc_frame_index];
		hc->ep_type = DWC_OTG_EP_TYPE_ISOC;

		if (hcd->core_if->dma_enable)
			hc->xfer_buff = (u8 *) (u32) urb->transfer_dma;
		else
			hc->xfer_buff = (u8 *) urb->transfer_buffer;

		hc->xfer_buff += frame_desc->offset + qtd->isoc_split_offset;
		hc->xfer_len = frame_desc->length - qtd->isoc_split_offset;

		if (hc->xact_pos == DWC_HCSPLIT_XACTPOS_ALL) {
			if (hc->xfer_len <= 188)
				hc->xact_pos = DWC_HCSPLIT_XACTPOS_ALL;
			else
				hc->xact_pos = DWC_HCSPLIT_XACTPOS_BEGIN;
		}
		break;
	}

	if (hc->ep_type == DWC_OTG_EP_TYPE_INTR ||
	    hc->ep_type == DWC_OTG_EP_TYPE_ISOC)
		/*
		 * This value may be modified when the transfer is started to
		 * reflect the actual transfer length.
		 */
		hc->multi_count = dwc_hb_mult(qh->maxp);

	dwc_otg_hc_init(hcd->core_if, hc);
	hc->qh = qh;
}

/**
 * This function selects transactions from the HCD transfer schedule and
 * assigns them to available host channels. It is called from HCD interrupt
 * handler functions.
 */
enum dwc_transaction_type dwc_otg_hcd_select_transactions(struct dwc_hcd *hcd)
{
	struct list_head *qh_ptr;
	struct dwc_qh *qh;
	int num_channels;
	enum dwc_transaction_type ret_val = DWC_OTG_TRANSACTION_NONE;

	/* Process entries in the periodic ready list. */
	num_channels = hcd->core_if->core_params->host_channels;
	qh_ptr = hcd->periodic_sched_ready.next;
	while (qh_ptr != &hcd->periodic_sched_ready &&
	       !list_empty(&hcd->free_hc_list)) {
		/* Leave one channel for non periodic transactions. */
		if (hcd->available_host_channels <= 1)
			break;
		hcd->available_host_channels--;
		qh = list_entry(qh_ptr, struct dwc_qh, qh_list_entry);
		assign_and_init_hc(hcd, qh);
		/*
		 * Move the QH from the periodic ready schedule to the
		 * periodic assigned schedule.
		 */
		qh_ptr = qh_ptr->next;
		list_move(&qh->qh_list_entry, &hcd->periodic_sched_assigned);
		ret_val = DWC_OTG_TRANSACTION_PERIODIC;
	}

	/*
	 * Process entries in the deferred portion of the non-periodic list.
	 * A NAK put them here and, at the right time, they need to be
	 * placed on the sched_inactive list.
	 */
	qh_ptr = hcd->non_periodic_sched_deferred.next;
	while (qh_ptr != &hcd->non_periodic_sched_deferred) {
		u16 frame_number =
		    dwc_otg_hcd_get_frame_number(dwc_otg_hcd_to_hcd(hcd));
		qh = list_entry(qh_ptr, struct dwc_qh, qh_list_entry);
		qh_ptr = qh_ptr->next;

		if (dwc_frame_num_le(qh->sched_frame, frame_number))
			/*
			 * Move the QH from the non periodic deferred schedule
			 * to the non periodic inactive schedule.
			 */
			list_move(&qh->qh_list_entry,
				  &hcd->non_periodic_sched_inactive);
	}

	/*
	 * Process entries in the inactive portion of the non-periodic
	 * schedule. Some free host channels may not be used if they are
	 * reserved for periodic transfers.
	 */
	qh_ptr = hcd->non_periodic_sched_inactive.next;
	num_channels = hcd->core_if->core_params->host_channels;

	while (qh_ptr != &hcd->non_periodic_sched_inactive
	       && !list_empty(&hcd->free_hc_list)) {
		if (hcd->available_host_channels < 1)
			break;
		hcd->available_host_channels--;
		qh = list_entry(qh_ptr, struct dwc_qh, qh_list_entry);
		assign_and_init_hc(hcd, qh);
		/*
		 * Move the QH from the non-periodic inactive schedule to the
		 * non-periodic active schedule.
		 */
		qh_ptr = qh_ptr->next;
		list_move(&qh->qh_list_entry, &hcd->non_periodic_sched_active);
		if (ret_val == DWC_OTG_TRANSACTION_NONE)
			ret_val = DWC_OTG_TRANSACTION_NON_PERIODIC;
		else
			ret_val = DWC_OTG_TRANSACTION_ALL;

	}
	return ret_val;
}

/**
 * Sets the channel property that indicates in which frame a periodic transfer
 * should occur. This is always set to the _next_ frame. This function has no
 * effect on non-periodic transfers.
 */
static inline void hc_set_even_odd_frame(struct core_if *core_if,
					 struct dwc_hc *hc, u32 * hcchar)
{
	if (hc->ep_type == DWC_OTG_EP_TYPE_INTR ||
	    hc->ep_type == DWC_OTG_EP_TYPE_ISOC) {
		u32 hfnum = 0;

		hfnum = dwc_reg_read(core_if->host_if->host_global_regs,
				   DWC_HFNUM);

		/* 1 if _next_ frame is odd, 0 if it's even */
		*hcchar = DWC_HCCHAR_ODD_FRAME_RW(*hcchar,
						  ((DWC_HFNUM_FRNUM_RD(hfnum) &
						    0x1) ? 0 : 1));
	}
}

static void set_initial_xfer_pid(struct dwc_hc *hc)
{
	if (hc->speed == DWC_OTG_EP_SPEED_HIGH) {
		if (hc->ep_is_in) {
			if (hc->multi_count == 1)
				hc->data_pid_start = DWC_OTG_HC_PID_DATA0;
			else if (hc->multi_count == 2)
				hc->data_pid_start = DWC_OTG_HC_PID_DATA1;
			else
				hc->data_pid_start = DWC_OTG_HC_PID_DATA2;
		} else {
			if (hc->multi_count == 1)
				hc->data_pid_start = DWC_OTG_HC_PID_DATA0;
			else
				hc->data_pid_start = DWC_OTG_HC_PID_MDATA;
		}
	} else {
		hc->data_pid_start = DWC_OTG_HC_PID_DATA0;
	}
}

/**
 * Starts a PING transfer. This function should only be called in Slave mode.
 * The Do Ping bit is set in the HCTSIZ register, then the channel is enabled.
 */
static void dwc_otg_hc_do_ping(struct core_if *core_if, struct dwc_hc *hc)
{
	u32 hcchar;
	u32 hctsiz = 0;

	ulong hc_regs = core_if->host_if->hc_regs[hc->hc_num];

	hctsiz = 0;
	hctsiz = DWC_HCTSIZ_DO_PING_PROTO_RW(hctsiz, 1);
	hctsiz = DWC_HCTSIZ_PKT_CNT_RW(hctsiz, 1);
	dwc_reg_write(hc_regs, DWC_HCTSIZ, hctsiz);

	hcchar = dwc_reg_read(hc_regs, DWC_HCCHAR);
	hcchar = DWC_HCCHAR_ENA_RW(hcchar, 1);
	hcchar = DWC_HCCHAR_DIS_RW(hcchar, 0);
	dwc_reg_write(hc_regs, DWC_HCCHAR, hcchar);
}

/**
 * This function writes a packet into the Tx FIFO associated with the Host
 * Channel. For a channel associated with a non-periodic EP, the non-periodic
 * Tx FIFO is written. For a channel associated with a periodic EP, the
 * periodic Tx FIFO is written. This function should only be called in Slave
 * mode.
 *
 * Upon return the xfer_buff and xfer_count fields in hc are incremented by
 * then number of bytes written to the Tx FIFO.
 */
static void dwc_otg_hc_write_packet(struct core_if *core_if, struct dwc_hc *hc)
{
	u32 i;
	u32 remaining_count;
	u32 byte_count;
	u32 dword_count;
	u32 *data_buff = (u32 *) (hc->xfer_buff);
	u32 data_fifo = core_if->data_fifo[hc->hc_num];

	remaining_count = hc->xfer_len - hc->xfer_count;
	if (remaining_count > hc->max_packet)
		byte_count = hc->max_packet;
	else
		byte_count = remaining_count;

	dword_count = (byte_count + 3) / 4;

	if (((unsigned long)data_buff) & 0x3)
		/* xfer_buff is not DWORD aligned. */
		for (i = 0; i < dword_count; i++, data_buff++)
			dwc_write_fifo32(data_fifo,
					     get_unaligned(data_buff));
	else
		/* xfer_buff is DWORD aligned. */
		for (i = 0; i < dword_count; i++, data_buff++)
			dwc_write_fifo32(data_fifo, *data_buff);

	hc->xfer_count += byte_count;
	hc->xfer_buff += byte_count;
}

/**
 * This function does the setup for a data transfer for a host channel and
 * starts the transfer. May be called in either Slave mode or DMA mode. In
 * Slave mode, the caller must ensure that there is sufficient space in the
 * request queue and Tx Data FIFO.
 *
 * For an OUT transfer in Slave mode, it loads a data packet into the
 * appropriate FIFO. If necessary, additional data packets will be loaded in
 * the Host ISR.
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
static void dwc_otg_hc_start_transfer(struct dwc_hcd *hcd,
				      struct dwc_hc *hc)
{
	u32 hcchar;
	u32 hctsiz = 0;
	u16 num_packets;
	struct core_if *core_if = hcd->core_if;
	u32 max_hc_xfer_size = core_if->core_params->max_transfer_size;
	u16 max_hc_pkt_count = core_if->core_params->max_packet_count;
	struct dwc_qtd *qtd;
	ulong hc_regs = core_if->host_if->hc_regs[hc->hc_num];
	hctsiz = 0;

	if (hc->do_ping) {
		if (!core_if->dma_enable) {
			dwc_otg_hc_do_ping(core_if, hc);
			hc->xfer_started = 1;
			return;
		} else {
			hctsiz = DWC_HCTSIZ_DO_PING_PROTO_RW(hctsiz, 1);
		}
	}

	if (hc->do_split) {
		num_packets = 1;

		if (hc->complete_split && !hc->ep_is_in)
			/*
			 * For CSPLIT OUT Transfer, set the size to 0 so the
			 * core doesn't expect any data written to the FIFO
			 */
			hc->xfer_len = 0;
		else if (hc->ep_is_in || (hc->xfer_len > hc->max_packet))
			hc->xfer_len = hc->max_packet;
		else if (!hc->ep_is_in && (hc->xfer_len > 188))
			hc->xfer_len = 188;

		hctsiz = DWC_HCTSIZ_XFER_SIZE_RW(hctsiz, hc->xfer_len);
	} else {
		/*
		 * Ensure that the transfer length and packet count will fit
		 * in the widths allocated for them in the HCTSIZn register.
		 */
		if (hc->ep_type == DWC_OTG_EP_TYPE_INTR ||
		    hc->ep_type == DWC_OTG_EP_TYPE_ISOC) {
			u32 max_len = hc->multi_count * hc->max_packet;

			/*
			 * Make sure the transfer size is no larger than one
			 * (micro)frame's worth of data. (A check was done
			 * when the periodic transfer was accepted to ensure
			 * that a (micro)frame's worth of data can be
			 * programmed into a channel.)
			 */
			if (hc->xfer_len > max_len)
				hc->xfer_len = max_len;
		} else if (hc->xfer_len > max_hc_xfer_size) {
			/*
			 * Make sure that xfer_len is a multiple of max packet
			 * size.
			 */
			hc->xfer_len = max_hc_xfer_size - hc->max_packet + 1;
		}
		if (hc->xfer_len > 0) {
			num_packets = (hc->xfer_len + hc->max_packet - 1) /
			    hc->max_packet;
			if (num_packets > max_hc_pkt_count) {
				num_packets = max_hc_pkt_count;
				hc->xfer_len = num_packets * hc->max_packet;
			}
		} else {
			/* Need 1 packet for transfer length of 0. */
			num_packets = 1;
		}

		if (hc->ep_is_in)
			/*
			 * Always program an integral # of max packets for IN
			 * transfers.
			 */
			hc->xfer_len = num_packets * hc->max_packet;

		if (hc->ep_type == DWC_OTG_EP_TYPE_INTR ||
		    hc->ep_type == DWC_OTG_EP_TYPE_ISOC)
			/*
			 * Make sure that the multi_count field matches the
			 * actual transfer length.
			 */
			hc->multi_count = num_packets;

		/* Set up the initial PID for the transfer. */
		if (hc->ep_type == DWC_OTG_EP_TYPE_ISOC)
			set_initial_xfer_pid(hc);

		hctsiz = DWC_HCTSIZ_XFER_SIZE_RW(hctsiz, hc->xfer_len);
	}

	hc->start_pkt_count = num_packets;
	hctsiz = DWC_HCTSIZ_PKT_CNT_RW(hctsiz, num_packets);
	hctsiz = DWC_HCTSIZ_PKT_PID_RW(hctsiz, hc->data_pid_start);
	dwc_reg_write(hc_regs, DWC_HCTSIZ, hctsiz);

	if (core_if->dma_enable)
		dwc_reg_write(hc_regs, DWC_HCDMA, (u32) hc->xfer_buff);

	/* Start the split */
	if (hc->do_split) {
		u32 hcsplt;

		hcsplt = dwc_reg_read(hc_regs, DWC_HCSPLT);
		hcsplt = DWC_HCSPLT_COMP_SPLT_RW(hcsplt, 1);
		dwc_reg_write(hc_regs, DWC_HCSPLT, hcsplt);
	}

	hcchar = dwc_reg_read(hc_regs, DWC_HCCHAR);
	hcchar = DWC_HCCHAR_MULTI_CNT_RW(hcchar, hc->multi_count);
	hc_set_even_odd_frame(core_if, hc, &hcchar);

	/* Set host channel enable after all other setup is complete. */
	hcchar = DWC_HCCHAR_ENA_RW(hcchar, 1);
	hcchar = DWC_HCCHAR_DIS_RW(hcchar, 0);
	dwc_reg_write(hc_regs, DWC_HCCHAR, hcchar);

	hc->xfer_started = 1;
	hc->requests++;
	if (!core_if->dma_enable && !hc->ep_is_in && hc->xfer_len > 0) {
		if(hc->qh != 0) {
			qtd = list_entry(hc->qh->qtd_list.next,
					struct dwc_qtd, qtd_list_entry);
			usb_hcd_unmap_urb_for_dma(dwc_otg_hcd_to_hcd(hcd),
						qtd->urb);
		}

		/* Load OUT packet into the appropriate Tx FIFO. */
		dwc_otg_hc_write_packet(core_if, hc);
	}
}

/**
 * This function continues a data transfer that was started by previous call
 * to dwc_otg_hc_start_transfer</code>. The caller must ensure there is
 * sufficient space in the request queue and Tx Data FIFO. This function
 * should only be called in Slave mode. In DMA mode, the controller acts
 * autonomously to complete transfers programmed to a host channel.
 *
 * For an OUT transfer, a new data packet is loaded into the appropriate FIFO
 * if there is any data remaining to be queued. For an IN transfer, another
 * data packet is always requested. For the SETUP phase of a control transfer,
 * this function does nothing.
 */
static int dwc_otg_hc_continue_transfer(struct dwc_hcd *hcd,
					struct dwc_hc *hc)
{
	struct core_if *core_if = hcd->core_if;
	struct dwc_qtd *qtd;

	if (hc->do_split) {
		/* SPLITs always queue just once per channel */
		return 0;
	} else if (hc->data_pid_start == DWC_OTG_HC_PID_SETUP) {
		/* SETUPs are queued only once since they can't be NAKed. */
		return 0;
	} else if (hc->ep_is_in) {
		/*
		 * Always queue another request for other IN transfers. If
		 * back-to-back INs are issued and NAKs are received for both,
		 * the driver may still be processing the first NAK when the
		 * second NAK is received. When the interrupt handler clears
		 * the NAK interrupt for the first NAK, the second NAK will
		 * not be seen. So we can't depend on the NAK interrupt
		 * handler to requeue a NAKed request. Instead, IN requests
		 * are issued each time this function is called. When the
		 * transfer completes, the extra requests for the channel will
		 * be flushed.
		 */
		u32 hcchar;
		ulong hc_regs = core_if->host_if->hc_regs[hc->hc_num];

		hcchar = dwc_reg_read(hc_regs, DWC_HCCHAR);
		hc_set_even_odd_frame(core_if, hc, &hcchar);

		hcchar = DWC_HCCHAR_ENA_RW(hcchar, 1);
		hcchar = DWC_HCCHAR_DIS_RW(hcchar, 0);
		dwc_reg_write(hc_regs, DWC_HCCHAR, hcchar);

		hc->requests++;
		return 1;
	} else {
		/* OUT transfers. */
		if (hc->xfer_count < hc->xfer_len) {
			if (hc->ep_type == DWC_OTG_EP_TYPE_INTR ||
			    hc->ep_type == DWC_OTG_EP_TYPE_ISOC) {
				u32 hcchar;
				u32 hc_regs;

				hc_regs =
				    core_if->host_if->hc_regs[hc->hc_num];
				hcchar = dwc_reg_read(hc_regs, DWC_HCCHAR);
				hc_set_even_odd_frame(core_if, hc, &hcchar);
			}

			if(hc->qh != 0) {
				qtd = list_entry(hc->qh->qtd_list.next,
						struct dwc_qtd, qtd_list_entry);
				usb_hcd_unmap_urb_for_dma(dwc_otg_hcd_to_hcd(hcd),
							qtd->urb);
			}
			/* Load OUT packet into the appropriate Tx FIFO. */
			dwc_otg_hc_write_packet(core_if, hc);
			hc->requests++;
			return 1;
		} else {
			return 0;
		}
	}
}

/**
 * This function writes a packet into the Tx FIFO associated with the Host
 * Channel. For a channel associated with a non-periodic EP, the non-periodic
 * Tx FIFO is written. For a channel associated with a periodic EP, the
 * periodic Tx FIFO is written. This function should only be called in Slave
 * mode.
 *
 * Upon return the xfer_buff and xfer_count fields in hc are incremented by
 * then number of bytes written to the Tx FIFO.
 */

/**
 * Attempts to queue a single transaction request for a host channel
 * associated with either a periodic or non-periodic transfer. This function
 * assumes that there is space available in the appropriate request queue. For
 * an OUT transfer or SETUP transaction in Slave mode, it checks whether space
 * is available in the appropriate Tx FIFO.
 */
static int queue_transaction(struct dwc_hcd *hcd, struct dwc_hc *hc,
			     u16 _fifo_dwords_avail)
{
	int retval;

	if (hcd->core_if->dma_enable) {
		if (!hc->xfer_started) {
			dwc_otg_hc_start_transfer(hcd, hc);
			hc->qh->ping_state = 0;
		}
		retval = 0;
	} else if (hc->halt_pending) {
		/* Don't queue a request if the channel has been halted. */
		retval = 0;
	} else if (hc->halt_on_queue) {
		dwc_otg_hc_halt(hcd->core_if, hc, hc->halt_status);
		retval = 0;
	} else if (hc->do_ping) {
		if (!hc->xfer_started)
			dwc_otg_hc_start_transfer(hcd, hc);
		retval = 0;
	} else if (!hc->ep_is_in || hc->data_pid_start ==
			DWC_OTG_HC_PID_SETUP) {
		if ((_fifo_dwords_avail * 4) >= hc->max_packet) {
			if (!hc->xfer_started) {
				dwc_otg_hc_start_transfer(hcd, hc);
				retval = 1;
			} else {
				retval =
				    dwc_otg_hc_continue_transfer(hcd,
								 hc);
			}
		} else {
			retval = -1;
		}
	} else {
		if (!hc->xfer_started) {
			dwc_otg_hc_start_transfer(hcd, hc);
			retval = 1;
		} else {
			retval = dwc_otg_hc_continue_transfer(hcd, hc);
		}
	}
	return retval;
}

/**
 * Processes active non-periodic channels and queues transactions for these
 * channels to the DWC_otg controller. After queueing transactions, the NP Tx
 * FIFO Empty interrupt is enabled if there are more transactions to queue as
 * NP Tx FIFO or request queue space becomes available. Otherwise, the NP Tx
 * FIFO Empty interrupt is disabled.
 */
static void process_non_periodic_channels(struct dwc_hcd *hcd)
{
	u32 tx_status = 0;
	struct list_head *orig_qh_ptr;
	struct dwc_qh *qh;
	int status;
	int no_queue_space = 0;
	int no_fifo_space = 0;
	int more_to_do = 0;
	ulong regs = hcd->core_if->core_global_regs;

	/*
	 * Keep track of the starting point. Skip over the start-of-list
	 * entry.
	 */
	if (hcd->non_periodic_qh_ptr == &hcd->non_periodic_sched_active)
		hcd->non_periodic_qh_ptr = hcd->non_periodic_qh_ptr->next;
	orig_qh_ptr = hcd->non_periodic_qh_ptr;

	/*
	 * Process once through the active list or until no more space is
	 * available in the request queue or the Tx FIFO.
	 */
	do {
		tx_status = dwc_reg_read(regs, DWC_GNPTXSTS);
		if (!hcd->core_if->dma_enable &&
		    DWC_GNPTXSTS_NPTXQSPCAVAIL_RD(tx_status) == 0) {
			no_queue_space = 1;
			break;
		}

		qh = list_entry(hcd->non_periodic_qh_ptr, struct dwc_qh,
				qh_list_entry);
		status = queue_transaction(hcd, qh->channel,
					   DWC_GNPTXSTS_NPTXFSPCAVAIL_RD
					   (tx_status));

		if (status > 0) {
			more_to_do = 1;
		} else if (status < 0) {
			no_fifo_space = 1;
			break;
		}

		/* Advance to next QH, skipping start-of-list entry. */
		hcd->non_periodic_qh_ptr = hcd->non_periodic_qh_ptr->next;
		if (hcd->non_periodic_qh_ptr == &hcd->non_periodic_sched_active)
			hcd->non_periodic_qh_ptr =
			    hcd->non_periodic_qh_ptr->next;
	} while (hcd->non_periodic_qh_ptr != orig_qh_ptr);

	if (!hcd->core_if->dma_enable) {
		u32 intr_mask = 0;

		intr_mask |= DWC_INTMSK_NP_TXFIFO_EMPT;
		if (more_to_do || no_queue_space || no_fifo_space) {
			/*
			 * May need to queue more transactions as the request
			 * queue or Tx FIFO empties. Enable the non-periodic
			 * Tx FIFO empty interrupt. (Always use the half-empty
			 * level to ensure that new requests are loaded as
			 * soon as possible.)
			 */
			dwc_reg_modify(gintmsk_reg(hcd), 0, 0, intr_mask);
		} else {
			/*
			 * Disable the Tx FIFO empty interrupt since there are
			 * no more transactions that need to be queued right
			 * now. This function is called from interrupt
			 * handlers to queue more transactions as transfer
			 * states change.
			 */
			dwc_reg_modify(gintmsk_reg(hcd), 0, intr_mask, 0);
		}
	}
}

/**
 * Processes periodic channels for the next frame and queues transactions for
 * these channels to the DWC_otg controller. After queueing transactions, the
 * Periodic Tx FIFO Empty interrupt is enabled if there are more transactions
 * to queue as Periodic Tx FIFO or request queue space becomes available.
 * Otherwise, the Periodic Tx FIFO Empty interrupt is disabled.
 */
static void process_periodic_channels(struct dwc_hcd *hcd)
{
	u32 tx_status = 0;
	struct list_head *qh_ptr;
	struct dwc_qh *qh;
	int status;
	int no_queue_space = 0;
	int no_fifo_space = 0;
	ulong host_regs;

	host_regs = hcd->core_if->host_if->host_global_regs;

	qh_ptr = hcd->periodic_sched_assigned.next;
	while (qh_ptr != &hcd->periodic_sched_assigned) {
		tx_status = dwc_reg_read(host_regs, DWC_HPTXSTS);
		if (DWC_HPTXSTS_PTXSPC_AVAIL_RD(tx_status) == 0) {
			no_queue_space = 1;
			break;
		}

		qh = list_entry(qh_ptr, struct dwc_qh, qh_list_entry);

		/*
		 * Set a flag if we're queuing high-bandwidth in slave mode.
		 * The flag prevents any halts to get into the request queue in
		 * the middle of multiple high-bandwidth packets getting queued.
		 */
		if (!hcd->core_if->dma_enable && qh->channel->multi_count > 1)
			hcd->core_if->queuing_high_bandwidth = 1;

		status = queue_transaction(hcd, qh->channel,
					   DWC_HPTXSTS_PTXFSPC_AVAIL_RD
					   (tx_status));
		if (status < 0) {
			no_fifo_space = 1;
			break;
		}

		/*
		 * In Slave mode, stay on the current transfer until there is
		 * nothing more to do or the high-bandwidth request count is
		 * reached. In DMA mode, only need to queue one request. The
		 * controller automatically handles multiple packets for
		 * high-bandwidth transfers.
		 */
		if (hcd->core_if->dma_enable || (status == 0 ||
						 qh->channel->requests ==
						 qh->channel->multi_count)) {
			qh_ptr = qh_ptr->next;

			/*
			 * Move the QH from the periodic assigned schedule to
			 * the periodic queued schedule.
			 */
			list_move(&qh->qh_list_entry,
				  &hcd->periodic_sched_queued);

			/* done queuing high bandwidth */
			hcd->core_if->queuing_high_bandwidth = 0;
		}
	}

	if (!hcd->core_if->dma_enable) {
		u32 intr_mask = 0;

		intr_mask |= DWC_INTMSK_NP_TXFIFO_EMPT;

		if (!list_empty(&hcd->periodic_sched_assigned) ||
		    no_queue_space || no_fifo_space)
			/*
			 * May need to queue more transactions as the request
			 * queue or Tx FIFO empties. Enable the periodic Tx
			 * FIFO empty interrupt. (Always use the half-empty
			 * level to ensure that new requests are loaded as
			 * soon as possible.)
			 */
			dwc_reg_modify(gintmsk_reg(hcd), 0, 0, intr_mask);
		else
			/*
			 * Disable the Tx FIFO empty interrupt since there are
			 * no more transactions that need to be queued right
			 * now. This function is called from interrupt
			 * handlers to queue more transactions as transfer
			 * states change.
			 */
			dwc_reg_modify(gintmsk_reg(hcd), 0, intr_mask, 0);
	}
}

/**
 * This function processes the currently active host channels and queues
 * transactions for these channels to the DWC_otg controller. It is called
 * from HCD interrupt handler functions.
 */
void dwc_otg_hcd_queue_transactions(struct dwc_hcd *hcd,
				    enum dwc_transaction_type tr_type)
{
	/* Process host channels associated with periodic transfers. */
	if ((tr_type == DWC_OTG_TRANSACTION_PERIODIC ||
	     tr_type == DWC_OTG_TRANSACTION_ALL) &&
	    !list_empty(&hcd->periodic_sched_assigned))
		process_periodic_channels(hcd);

	/* Process host channels associated with non-periodic transfers. */
	if (tr_type == DWC_OTG_TRANSACTION_NON_PERIODIC ||
	    tr_type == DWC_OTG_TRANSACTION_ALL) {
		if (!list_empty(&hcd->non_periodic_sched_active)) {
			process_non_periodic_channels(hcd);
		} else {
			/*
			 * Ensure NP Tx FIFO empty interrupt is disabled when
			 * there are no non-periodic transfers to process.
			 */
			u32 gintmsk = 0;
			gintmsk |= DWC_INTMSK_NP_TXFIFO_EMPT;
			dwc_reg_modify(gintmsk_reg(hcd), 0, gintmsk, 0);
		}
	}
}

/**
 * Sets the final status of an URB and returns it to the device driver. Any
 * required cleanup of the URB is performed.
 */
void dwc_otg_hcd_complete_urb(struct dwc_hcd *hcd, struct urb *urb, int status)
__releases(hcd->lock) __acquires(hcd->lock)
{
	urb->hcpriv = NULL;
	usb_hcd_unlink_urb_from_ep(dwc_otg_hcd_to_hcd(hcd), urb);

	spin_unlock(&hcd->lock);
	usb_hcd_giveback_urb(dwc_otg_hcd_to_hcd(hcd), urb, status);
	spin_lock(&hcd->lock);
}
