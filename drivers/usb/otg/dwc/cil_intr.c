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

/*
 * The Core Interface Layer provides basic services for accessing and
 * managing the DWC_otg hardware. These services are used by both the
 * Host Controller Driver and the Peripheral Controller Driver.
 *
 * This file contains the Common Interrupt handlers.
 */
#include <linux/delay.h>

#include "cil.h"

/**
 *  This function will log a debug message
 */
static int dwc_otg_handle_mode_mismatch_intr(struct core_if *core_if)
{
	u32 gintsts = 0;
	ulong global_regs = core_if->core_global_regs;

	pr_warning("Mode Mismatch Interrupt: currently in %s mode\n",
		   dwc_otg_mode(core_if) ? "Host" : "Device");

	/* Clear interrupt */
	gintsts |= DWC_INTSTS_MODE_MISMTC;
	dwc_reg_write(global_regs, DWC_GINTSTS, gintsts);

	return 1;
}

/**
 *  Start the HCD.  Helper function for using the HCD callbacks.
 */
static inline void hcd_start(struct core_if *core_if)
{
	if (core_if->hcd_cb && core_if->hcd_cb->start)
		core_if->hcd_cb->start(core_if->hcd_cb->p);
}

/**
 *  Stop the HCD.  Helper function for using the HCD callbacks.
 */
static inline void hcd_stop(struct core_if *core_if)
{
	if (core_if->hcd_cb && core_if->hcd_cb->stop)
		core_if->hcd_cb->stop(core_if->hcd_cb->p);
}

/**
 *  Disconnect the HCD.  Helper function for using the HCD callbacks.
 */
static inline void hcd_disconnect(struct core_if *core_if)
{
	if (core_if->hcd_cb && core_if->hcd_cb->disconnect)
		core_if->hcd_cb->disconnect(core_if->hcd_cb->p);
}

/**
 *  Inform the HCD the a New Session has begun.  Helper function for using the
 *  HCD callbacks.
 */
static inline void hcd_session_start(struct core_if *core_if)
{
	if (core_if->hcd_cb && core_if->hcd_cb->session_start)
		core_if->hcd_cb->session_start(core_if->hcd_cb->p);
}

/**
 *  Start the PCD.  Helper function for using the PCD callbacks.
 */
static inline void pcd_start(struct core_if *core_if)
{
	if (core_if->pcd_cb && core_if->pcd_cb->start) {
		struct dwc_pcd *pcd;

		pcd = (struct dwc_pcd *)core_if->pcd_cb->p;
		spin_lock(&pcd->lock);
		core_if->pcd_cb->start(core_if->pcd_cb->p);
		spin_unlock(&pcd->lock);
	}
}

/**
 *  Stop the PCD.  Helper function for using the PCD callbacks.
 */
static inline void pcd_stop(struct core_if *core_if)
{
	if (core_if->pcd_cb && core_if->pcd_cb->stop) {
		struct dwc_pcd *pcd;

		pcd = (struct dwc_pcd *)core_if->pcd_cb->p;
		spin_lock(&pcd->lock);
		core_if->pcd_cb->stop(core_if->pcd_cb->p);
		spin_unlock(&pcd->lock);
	}
}

/**
 *  Suspend the PCD.  Helper function for using the PCD callbacks.
 */
static inline void pcd_suspend(struct core_if *core_if)
{
	if (core_if->pcd_cb && core_if->pcd_cb->suspend) {
		struct dwc_pcd *pcd;

		pcd = (struct dwc_pcd *)core_if->pcd_cb->p;
		spin_lock(&pcd->lock);
		core_if->pcd_cb->suspend(core_if->pcd_cb->p);
		spin_unlock(&pcd->lock);
	}
}

/**
 *  Resume the PCD.  Helper function for using the PCD callbacks.
 */
static inline void pcd_resume(struct core_if *core_if)
{
	if (core_if->pcd_cb && core_if->pcd_cb->resume_wakeup) {
		struct dwc_pcd *pcd;

		pcd = (struct dwc_pcd *)core_if->pcd_cb->p;
		spin_lock(&pcd->lock);
		core_if->pcd_cb->resume_wakeup(core_if->pcd_cb->p);
		spin_unlock(&pcd->lock);
	}
}

/**
 * This function handles the OTG Interrupts. It reads the OTG
 * Interrupt Register (GOTGINT) to determine what interrupt has
 * occurred.
 */
static int dwc_otg_handle_otg_intr(struct core_if *core_if)
{
	ulong global_regs = core_if->core_global_regs;
	u32 gotgint;
	u32 gotgctl;
	u32 gintmsk;

	gotgint = dwc_reg_read(global_regs, DWC_GOTGINT);
	if (gotgint & DWC_GINT_SES_ENDDET) {
		gotgctl = dwc_reg_read(global_regs, DWC_GOTGCTL);
		if (core_if->xceiv->state == OTG_STATE_B_HOST) {
			pcd_start(core_if);
			core_if->xceiv->state = OTG_STATE_B_PERIPHERAL;
		} else {
			/*
			 * If not B_HOST and Device HNP still set. HNP did not
			 * succeed
			 */
			if (gotgctl & DWC_GCTL_DEV_HNP_ENA)
				pr_err("Device Not Connected / "
				       "Responding\n");
			/*
			 * If Session End Detected the B-Cable has been
			 * disconnected.  Reset PCD and Gadget driver to a
			 * clean state.
			 */
			pcd_stop(core_if);
		}
		gotgctl = 0;
		gotgctl |= DWC_GCTL_DEV_HNP_ENA;
		dwc_reg_modify(global_regs, DWC_GOTGCTL, gotgctl, 0);
	}
	if (gotgint & DWC_GINT_SES_REQSUC) {
		gotgctl = dwc_reg_read(global_regs, DWC_GOTGCTL);
		if (gotgctl & DWC_GCTL_SES_REQ_SUCCESS) {
			pcd_resume(core_if);

			/* Clear Session Request */
			gotgctl = 0;
			gotgctl |= DWC_GCTL_SES_REQ;
			dwc_reg_modify(global_regs, DWC_GOTGCTL,
				     gotgctl, 0);
		}
	}
	if (gotgint & DWC_GINT_HST_NEGSUC) {
		/*
		 * Print statements during the HNP interrupt handling can cause
		 * it to fail.
		 */
		gotgctl = dwc_reg_read(global_regs, DWC_GOTGCTL);
		if (gotgctl & DWC_GCTL_HOST_NEG_SUCCES) {
			if (dwc_otg_is_host_mode(core_if)) {
				core_if->xceiv->state = OTG_STATE_B_HOST;
				/*
				 * Need to disable SOF interrupt immediately.
				 * When switching from device to host, the PCD
				 * interrupt handler won't handle the
				 * interrupt if host mode is already set. The
				 * HCD interrupt handler won't get called if
				 * the HCD state is HALT. This means that the
				 * interrupt does not get handled and Linux
				 * complains loudly.
				 */
				gintmsk = 0;
				gintmsk |= DWC_INTMSK_STRT_OF_FRM;
				dwc_reg_modify(global_regs, DWC_GINTMSK,
					     gintmsk, 0);
				pcd_stop(core_if);
				/* Initialize the Core for Host mode. */
				hcd_start(core_if);
				core_if->xceiv->state = OTG_STATE_B_HOST;
			}
		} else {
			gotgctl = 0;
			gotgctl |= DWC_GCTL_HNP_REQ;
			gotgctl |= DWC_GCTL_DEV_HNP_ENA;
			dwc_reg_modify(global_regs, DWC_GOTGCTL, gotgctl, 0);

			pr_err("Device Not Connected / Responding\n");
		}
	}
	if (gotgint & DWC_GINT_HST_NEGDET) {
		/*
		 * The disconnect interrupt is set at the same time as
		 * Host Negotiation Detected.  During the mode
		 * switch all interrupts are cleared so the disconnect
		 * interrupt handler will not get executed.
		 */
		if (dwc_otg_is_device_mode(core_if)) {
			hcd_disconnect(core_if);
			pcd_start(core_if);
			core_if->xceiv->state = OTG_STATE_A_PERIPHERAL;
		} else {
			/*
			 * Need to disable SOF interrupt immediately. When
			 * switching from device to host, the PCD interrupt
			 * handler won't handle the interrupt if host mode is
			 * already set. The HCD interrupt handler won't get
			 * called if the HCD state is HALT. This means that
			 * the interrupt does not get handled and Linux
			 * complains loudly.
			 */
			gintmsk = 0;
			gintmsk |= DWC_INTMSK_STRT_OF_FRM;
			dwc_reg_modify(global_regs, DWC_GINTMSK, gintmsk, 0);
			pcd_stop(core_if);
			hcd_start(core_if);
			core_if->xceiv->state = OTG_STATE_A_HOST;
		}
	}
	if (gotgint & DWC_GINT_DEVTOUT)
		pr_info(" ++OTG Interrupt: A-Device Timeout " "Change++\n");
	if (gotgint & DWC_GINT_DEBDONE)
		pr_info(" ++OTG Interrupt: Debounce Done++\n");

	/* Clear GOTGINT */
	dwc_reg_write(global_regs, DWC_GOTGINT, gotgint);
	return 1;
}

/*
 * Wakeup Workqueue implementation
 */
static void port_otg_wqfunc(struct work_struct *work)
{
	struct core_if *core_if = container_of(work, struct core_if,
					       usb_port_otg);
	ulong global_regs = core_if->core_global_regs;
	u32 count = 0;
	u32 gotgctl;

	pr_info("%s\n", __func__);

	gotgctl = dwc_reg_read(global_regs, DWC_GOTGCTL);
	if (gotgctl & DWC_GCTL_CONN_ID_STATUS) {
		/*
		 * B-Device connector (device mode) wait for switch to device
		 * mode.
		 */
		while (!dwc_otg_is_device_mode(core_if) && ++count <= 10000) {
			pr_info("Waiting for Peripheral Mode, "
				"Mode=%s\n", dwc_otg_is_host_mode(core_if) ?
				"Host" : "Peripheral");
			msleep(100);
		}
		BUG_ON(count > 10000);
		core_if->xceiv->state = OTG_STATE_B_PERIPHERAL;
		dwc_otg_core_init(core_if);
		dwc_otg_enable_global_interrupts(core_if);
		pcd_start(core_if);
	} else {
		/*
		 * A-Device connector (host mode) wait for switch to host
		 * mode.
		 */
		while (!dwc_otg_is_host_mode(core_if) && ++count <= 10000) {
			pr_info("Waiting for Host Mode, Mode=%s\n",
				dwc_otg_is_host_mode(core_if) ?
				"Host" : "Peripheral");
			msleep(100);
		}
		BUG_ON(count > 10000);
		core_if->xceiv->state = OTG_STATE_A_HOST;
		dwc_otg_core_init(core_if);
		dwc_otg_enable_global_interrupts(core_if);
		hcd_start(core_if);
	}
}

/**
 * This function handles the Connector ID Status Change Interrupt.  It
 * reads the OTG Interrupt Register (GOTCTL) to determine whether this
 * is a Device to Host Mode transition or a Host Mode to Device
 * Transition.
 *
 * This only occurs when the cable is connected/removed from the PHY
 * connector.
 */
static int dwc_otg_handle_conn_id_status_change_intr(struct core_if *core_if)
{
	u32 gintsts = 0;
	u32 gintmsk = 0;
	ulong global_regs = core_if->core_global_regs;

	/*
	 * Need to disable SOF interrupt immediately. If switching from device
	 * to host, the PCD interrupt handler won't handle the interrupt if
	 * host mode is already set. The HCD interrupt handler won't get
	 * called if the HCD state is HALT. This means that the interrupt does
	 * not get handled and Linux complains loudly.
	 */
	gintmsk |= DWC_INTSTS_STRT_OF_FRM;
	dwc_reg_modify(global_regs, DWC_GINTMSK, gintmsk, 0);

	if(!core_if->wqfunc_setup_done) {
		core_if->wqfunc_setup_done = 1;
		INIT_WORK(&core_if->usb_port_otg, port_otg_wqfunc);
	}
	schedule_work(&core_if->usb_port_otg);

	/* Set flag and clear interrupt */
	gintsts |= DWC_INTSTS_CON_ID_STS_CHG;
	dwc_reg_write(global_regs, DWC_GINTSTS, gintsts);
	return 1;
}

/**
 * This interrupt indicates that a device is initiating the Session
 * Request Protocol to request the host to turn on bus power so a new
 * session can begin. The handler responds by turning on bus power. If
 * the DWC_otg controller is in low power mode, the handler brings the
 * controller out of low power mode before turning on bus power.
 */
static int dwc_otg_handle_session_req_intr(struct core_if *core_if)
{
	u32 gintsts = 0;
	ulong global_regs = core_if->core_global_regs;

	if (!dwc_has_feature(core_if, DWC_HOST_ONLY)) {
		u32 hprt0;

		if (dwc_otg_is_device_mode(core_if)) {
			pr_info("SRP: Device mode\n");
		} else {
			pr_info("SRP: Host mode\n");

			/* Turn on the port power bit. */
			hprt0 = dwc_otg_read_hprt0(core_if);
			hprt0 = DWC_HPRT0_PRT_PWR_RW(hprt0, 1);
			dwc_reg_write(core_if->host_if->hprt0, 0, hprt0);

			/*
			 * Start the Connection timer.
			 * A message can be displayed,
			 * if connect does not occur within 10 seconds.
			 */
			hcd_session_start(core_if);
		}
	}
	/* Clear interrupt */
	gintsts |= DWC_INTSTS_NEW_SES_DET;
	dwc_reg_write(global_regs, DWC_GINTSTS, gintsts);
	return 1;
}

/**
 * This interrupt indicates that the DWC_otg controller has detected a
 * resume or remote wakeup sequence. If the DWC_otg controller is in
 * low power mode, the handler must brings the controller out of low
 * power mode. The controller automatically begins resume
 * signaling. The handler schedules a time to stop resume signaling.
 */
static int dwc_otg_handle_wakeup_detected_intr(struct core_if *core_if)
{
	u32 gintsts = 0;
	struct device_if *dev_if = core_if->dev_if;
	ulong global_regs = core_if->core_global_regs;

	if (dwc_otg_is_device_mode(core_if)) {
		u32 dctl = 0;

		/* Clear the Remote Wakeup Signalling */
		dctl = DEC_DCTL_REMOTE_WAKEUP_SIG(dctl, 1);
		dwc_reg_modify(dev_if->dev_global_regs, DWC_DCTL, dctl, 0);

		if (core_if->pcd_cb && core_if->pcd_cb->resume_wakeup)
			core_if->pcd_cb->resume_wakeup(core_if->pcd_cb->p);
	} else {
		u32 pcgcctl = 0;

		/* Restart the Phy Clock */
		pcgcctl = DWC_PCGCCTL_STOP_CLK_SET(pcgcctl);
		dwc_reg_modify(core_if->pcgcctl, 0, pcgcctl, 0);
		schedule_delayed_work(&core_if->usb_port_wakeup, 10);
	}

	/* Clear interrupt */
	gintsts |= DWC_INTSTS_WKP;
	dwc_reg_write(global_regs, DWC_GINTSTS, gintsts);
	return 1;
}

/**
 * This interrupt indicates that a device has been disconnected from
 * the root port.
 */
static int dwc_otg_handle_disconnect_intr(struct core_if *core_if)
{
	u32 gintsts = 0;
	ulong global_regs = core_if->core_global_regs;

	if (!dwc_has_feature(core_if, DWC_HOST_ONLY)) {
		if (core_if->xceiv->state == OTG_STATE_B_HOST) {
			hcd_disconnect(core_if);
			pcd_start(core_if);
			core_if->xceiv->state = OTG_STATE_B_PERIPHERAL;
		} else if (dwc_otg_is_device_mode(core_if)) {
			u32 gotgctl;

			gotgctl = dwc_reg_read(global_regs, DWC_GOTGCTL);

			/*
			 * If HNP is in process, do nothing.
			 * The OTG "Host Negotiation Detected"
			 * interrupt will do the mode switch.
			 * Otherwise, since we are in device mode,
			 * disconnect and stop the HCD,
			 * then start the PCD.
			 */
			if ((gotgctl) & DWC_GCTL_DEV_HNP_ENA) {
				hcd_disconnect(core_if);
				pcd_start(core_if);
				core_if->xceiv->state = OTG_STATE_B_PERIPHERAL;
			}
		} else if (core_if->xceiv->state == OTG_STATE_A_HOST) {
			/* A-Cable still connected but device disconnected. */
			hcd_disconnect(core_if);
		}
	}
	gintsts |= DWC_INTSTS_SES_DISCON_DET;
	dwc_reg_write(global_regs, DWC_GINTSTS, gintsts);
	return 1;
}

/**
 * This interrupt indicates that SUSPEND state has been detected on
 * the USB.
 *
 * For HNP the USB Suspend interrupt signals the change from
 * "a_peripheral" to "a_host".
 *
 * When power management is enabled the core will be put in low power
 * mode.
 */
static int dwc_otg_handle_usb_suspend_intr(struct core_if *core_if)
{
	u32 dsts = 0;
	u32 gintsts = 0;
	ulong global_regs = core_if->core_global_regs;
	struct device_if *dev_if = core_if->dev_if;

	if (dwc_otg_is_device_mode(core_if)) {
		/*
		 * Check the Device status register to determine if the Suspend
		 * state is active.
		 */
		dsts = dwc_reg_read(dev_if->dev_global_regs, DWC_DSTS);
		/* PCD callback for suspend. */
		pcd_suspend(core_if);
	} else {
		if (core_if->xceiv->state == OTG_STATE_A_PERIPHERAL) {
			/* Clear the a_peripheral flag, back to a_host. */
			pcd_stop(core_if);
			hcd_start(core_if);
			core_if->xceiv->state = OTG_STATE_A_HOST;
		}
	}

	/* Clear interrupt */
	gintsts |= DWC_INTMSK_USB_SUSP;
	dwc_reg_write(global_regs, DWC_GINTSTS, gintsts);
	return 1;
}

/**
 * This function returns the Core Interrupt register.
 *
 * Although the Host Port interrupt (portintr) is documented as host mode
 * only, it appears to occur in device mode when Port Enable / Disable Changed
 * bit in HPRT0 is set. The code in dwc_otg_handle_common_intr checks if in
 * device mode and just clears the interrupt.
 */
static inline u32 dwc_otg_read_common_intr(struct core_if *core_if)
{
	u32 gintsts;
	u32 gintmsk;
	u32 gintmsk_common = 0;
	ulong global_regs = core_if->core_global_regs;

	gintmsk_common |= DWC_INTMSK_WKP;
	gintmsk_common |= DWC_INTMSK_NEW_SES_DET;
	gintmsk_common |= DWC_INTMSK_CON_ID_STS_CHG;
	gintmsk_common |= DWC_INTMSK_OTG;
	gintmsk_common |= DWC_INTMSK_MODE_MISMTC;
	gintmsk_common |= DWC_INTMSK_SES_DISCON_DET;
	gintmsk_common |= DWC_INTMSK_USB_SUSP;
	gintmsk_common |= DWC_INTMSK_HST_PORT;

	gintsts = dwc_reg_read(global_regs, DWC_GINTSTS);
	gintmsk = dwc_reg_read(global_regs, DWC_GINTMSK);

	return (gintsts & gintmsk) & gintmsk_common;
}

/**
 * Common interrupt handler.
 *
 * The common interrupts are those that occur in both Host and Device mode.
 * This handler handles the following interrupts:
 * - Mode Mismatch Interrupt
 * - Disconnect Interrupt
 * - OTG Interrupt
 * - Connector ID Status Change Interrupt
 * - Session Request Interrupt.
 * - Resume / Remote Wakeup Detected Interrupt.
 *
 * - Host Port Interrupt.  Although this interrupt is documented as only
 *   occurring in Host mode, it also occurs in Device mode when Port Enable /
 *   Disable Changed bit in HPRT0 is set. If it is seen here, while in Device
 *   mode, the interrupt is just cleared.
 *
 */
int dwc_otg_handle_common_intr(struct core_if *core_if)
{
	int retval = 0;
	u32 gintsts;
	ulong global_regs = core_if->core_global_regs;

	gintsts = dwc_otg_read_common_intr(core_if);

	if (gintsts & DWC_INTSTS_MODE_MISMTC)
		retval |= dwc_otg_handle_mode_mismatch_intr(core_if);
	if (gintsts & DWC_INTSTS_OTG)
		retval |= dwc_otg_handle_otg_intr(core_if);
	if (gintsts & DWC_INTSTS_CON_ID_STS_CHG)
		retval |= dwc_otg_handle_conn_id_status_change_intr(core_if);
	if (gintsts & DWC_INTSTS_SES_DISCON_DET)
		retval |= dwc_otg_handle_disconnect_intr(core_if);
	if (gintsts & DWC_INTSTS_NEW_SES_DET)
		retval |= dwc_otg_handle_session_req_intr(core_if);
	if (gintsts & DWC_INTSTS_WKP)
		retval |= dwc_otg_handle_wakeup_detected_intr(core_if);
	if (gintsts & DWC_INTMSK_USB_SUSP)
		retval |= dwc_otg_handle_usb_suspend_intr(core_if);

	if ((gintsts & DWC_INTSTS_HST_PORT) &&
			dwc_otg_is_device_mode(core_if)) {
		gintsts = 0;
		gintsts |= DWC_INTSTS_HST_PORT;
		dwc_reg_write(global_regs, DWC_GINTSTS, gintsts);
		retval |= 1;
		pr_info("RECEIVED PORTINT while in Device mode\n");
	}

	return retval;
}
