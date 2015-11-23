/*
 * core_intr.c - DesignWare HS OTG Controller common interrupt handling
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
 * This file contains the common interrupt handlers
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
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

static const char *dwc2_op_state_str(struct dwc2_hsotg *hsotg)
{
	switch (hsotg->op_state) {
	case OTG_STATE_A_HOST:
		return "a_host";
	case OTG_STATE_A_SUSPEND:
		return "a_suspend";
	case OTG_STATE_A_PERIPHERAL:
		return "a_peripheral";
	case OTG_STATE_B_PERIPHERAL:
		return "b_peripheral";
	case OTG_STATE_B_HOST:
		return "b_host";
	default:
		return "unknown";
	}
}

/**
 * dwc2_handle_usb_port_intr - handles OTG PRTINT interrupts.
 * When the PRTINT interrupt fires, there are certain status bits in the Host
 * Port that needs to get cleared.
 *
 * @hsotg: Programming view of DWC_otg controller
 */
static void dwc2_handle_usb_port_intr(struct dwc2_hsotg *hsotg)
{
	u32 hprt0 = dwc2_readl(hsotg->regs + HPRT0);

	if (hprt0 & HPRT0_ENACHG) {
		hprt0 &= ~HPRT0_ENA;
		dwc2_writel(hprt0, hsotg->regs + HPRT0);
	}

	/* Clear interrupt */
	dwc2_writel(GINTSTS_PRTINT, hsotg->regs + GINTSTS);
}

/**
 * dwc2_handle_mode_mismatch_intr() - Logs a mode mismatch warning message
 *
 * @hsotg: Programming view of DWC_otg controller
 */
static void dwc2_handle_mode_mismatch_intr(struct dwc2_hsotg *hsotg)
{
	dev_warn(hsotg->dev, "Mode Mismatch Interrupt: currently in %s mode\n",
		 dwc2_is_host_mode(hsotg) ? "Host" : "Device");

	/* Clear interrupt */
	dwc2_writel(GINTSTS_MODEMIS, hsotg->regs + GINTSTS);
}

/**
 * dwc2_handle_otg_intr() - Handles the OTG Interrupts. It reads the OTG
 * Interrupt Register (GOTGINT) to determine what interrupt has occurred.
 *
 * @hsotg: Programming view of DWC_otg controller
 */
static void dwc2_handle_otg_intr(struct dwc2_hsotg *hsotg)
{
	u32 gotgint;
	u32 gotgctl;
	u32 gintmsk;

	gotgint = dwc2_readl(hsotg->regs + GOTGINT);
	gotgctl = dwc2_readl(hsotg->regs + GOTGCTL);
	dev_dbg(hsotg->dev, "++OTG Interrupt gotgint=%0x [%s]\n", gotgint,
		dwc2_op_state_str(hsotg));

	if (gotgint & GOTGINT_SES_END_DET) {
		dev_dbg(hsotg->dev,
			" ++OTG Interrupt: Session End Detected++ (%s)\n",
			dwc2_op_state_str(hsotg));
		gotgctl = dwc2_readl(hsotg->regs + GOTGCTL);

		if (dwc2_is_device_mode(hsotg))
			dwc2_hsotg_disconnect(hsotg);

		if (hsotg->op_state == OTG_STATE_B_HOST) {
			hsotg->op_state = OTG_STATE_B_PERIPHERAL;
		} else {
			/*
			 * If not B_HOST and Device HNP still set, HNP did
			 * not succeed!
			 */
			if (gotgctl & GOTGCTL_DEVHNPEN) {
				dev_dbg(hsotg->dev, "Session End Detected\n");
				dev_err(hsotg->dev,
					"Device Not Connected/Responding!\n");
			}

			/*
			 * If Session End Detected the B-Cable has been
			 * disconnected
			 */
			/* Reset to a clean state */
			hsotg->lx_state = DWC2_L0;
		}

		gotgctl = dwc2_readl(hsotg->regs + GOTGCTL);
		gotgctl &= ~GOTGCTL_DEVHNPEN;
		dwc2_writel(gotgctl, hsotg->regs + GOTGCTL);
	}

	if (gotgint & GOTGINT_SES_REQ_SUC_STS_CHNG) {
		dev_dbg(hsotg->dev,
			" ++OTG Interrupt: Session Request Success Status Change++\n");
		gotgctl = dwc2_readl(hsotg->regs + GOTGCTL);
		if (gotgctl & GOTGCTL_SESREQSCS) {
			if (hsotg->core_params->phy_type ==
					DWC2_PHY_TYPE_PARAM_FS
			    && hsotg->core_params->i2c_enable > 0) {
				hsotg->srp_success = 1;
			} else {
				/* Clear Session Request */
				gotgctl = dwc2_readl(hsotg->regs + GOTGCTL);
				gotgctl &= ~GOTGCTL_SESREQ;
				dwc2_writel(gotgctl, hsotg->regs + GOTGCTL);
			}
		}
	}

	if (gotgint & GOTGINT_HST_NEG_SUC_STS_CHNG) {
		/*
		 * Print statements during the HNP interrupt handling
		 * can cause it to fail
		 */
		gotgctl = dwc2_readl(hsotg->regs + GOTGCTL);
		/*
		 * WA for 3.00a- HW is not setting cur_mode, even sometimes
		 * this does not help
		 */
		if (hsotg->hw_params.snpsid >= DWC2_CORE_REV_3_00a)
			udelay(100);
		if (gotgctl & GOTGCTL_HSTNEGSCS) {
			if (dwc2_is_host_mode(hsotg)) {
				hsotg->op_state = OTG_STATE_B_HOST;
				/*
				 * Need to disable SOF interrupt immediately.
				 * When switching from device to host, the PCD
				 * interrupt handler won't handle the interrupt
				 * if host mode is already set. The HCD
				 * interrupt handler won't get called if the
				 * HCD state is HALT. This means that the
				 * interrupt does not get handled and Linux
				 * complains loudly.
				 */
				gintmsk = dwc2_readl(hsotg->regs + GINTMSK);
				gintmsk &= ~GINTSTS_SOF;
				dwc2_writel(gintmsk, hsotg->regs + GINTMSK);

				/*
				 * Call callback function with spin lock
				 * released
				 */
				spin_unlock(&hsotg->lock);

				/* Initialize the Core for Host mode */
				dwc2_hcd_start(hsotg);
				spin_lock(&hsotg->lock);
				hsotg->op_state = OTG_STATE_B_HOST;
			}
		} else {
			gotgctl = dwc2_readl(hsotg->regs + GOTGCTL);
			gotgctl &= ~(GOTGCTL_HNPREQ | GOTGCTL_DEVHNPEN);
			dwc2_writel(gotgctl, hsotg->regs + GOTGCTL);
			dev_dbg(hsotg->dev, "HNP Failed\n");
			dev_err(hsotg->dev,
				"Device Not Connected/Responding\n");
		}
	}

	if (gotgint & GOTGINT_HST_NEG_DET) {
		/*
		 * The disconnect interrupt is set at the same time as
		 * Host Negotiation Detected. During the mode switch all
		 * interrupts are cleared so the disconnect interrupt
		 * handler will not get executed.
		 */
		dev_dbg(hsotg->dev,
			" ++OTG Interrupt: Host Negotiation Detected++ (%s)\n",
			(dwc2_is_host_mode(hsotg) ? "Host" : "Device"));
		if (dwc2_is_device_mode(hsotg)) {
			dev_dbg(hsotg->dev, "a_suspend->a_peripheral (%d)\n",
				hsotg->op_state);
			spin_unlock(&hsotg->lock);
			dwc2_hcd_disconnect(hsotg);
			spin_lock(&hsotg->lock);
			hsotg->op_state = OTG_STATE_A_PERIPHERAL;
		} else {
			/* Need to disable SOF interrupt immediately */
			gintmsk = dwc2_readl(hsotg->regs + GINTMSK);
			gintmsk &= ~GINTSTS_SOF;
			dwc2_writel(gintmsk, hsotg->regs + GINTMSK);
			spin_unlock(&hsotg->lock);
			dwc2_hcd_start(hsotg);
			spin_lock(&hsotg->lock);
			hsotg->op_state = OTG_STATE_A_HOST;
		}
	}

	if (gotgint & GOTGINT_A_DEV_TOUT_CHG)
		dev_dbg(hsotg->dev,
			" ++OTG Interrupt: A-Device Timeout Change++\n");
	if (gotgint & GOTGINT_DBNCE_DONE)
		dev_dbg(hsotg->dev, " ++OTG Interrupt: Debounce Done++\n");

	/* Clear GOTGINT */
	dwc2_writel(gotgint, hsotg->regs + GOTGINT);
}

/**
 * dwc2_handle_conn_id_status_change_intr() - Handles the Connector ID Status
 * Change Interrupt
 *
 * @hsotg: Programming view of DWC_otg controller
 *
 * Reads the OTG Interrupt Register (GOTCTL) to determine whether this is a
 * Device to Host Mode transition or a Host to Device Mode transition. This only
 * occurs when the cable is connected/removed from the PHY connector.
 */
static void dwc2_handle_conn_id_status_change_intr(struct dwc2_hsotg *hsotg)
{
	u32 gintmsk = dwc2_readl(hsotg->regs + GINTMSK);

	/* Need to disable SOF interrupt immediately */
	gintmsk &= ~GINTSTS_SOF;
	dwc2_writel(gintmsk, hsotg->regs + GINTMSK);

	dev_dbg(hsotg->dev, " ++Connector ID Status Change Interrupt++  (%s)\n",
		dwc2_is_host_mode(hsotg) ? "Host" : "Device");

	/*
	 * Need to schedule a work, as there are possible DELAY function calls.
	 * Release lock before scheduling workq as it holds spinlock during
	 * scheduling.
	 */
	if (hsotg->wq_otg) {
		spin_unlock(&hsotg->lock);
		queue_work(hsotg->wq_otg, &hsotg->wf_otg);
		spin_lock(&hsotg->lock);
	}

	/* Clear interrupt */
	dwc2_writel(GINTSTS_CONIDSTSCHNG, hsotg->regs + GINTSTS);
}

/**
 * dwc2_handle_session_req_intr() - This interrupt indicates that a device is
 * initiating the Session Request Protocol to request the host to turn on bus
 * power so a new session can begin
 *
 * @hsotg: Programming view of DWC_otg controller
 *
 * This handler responds by turning on bus power. If the DWC_otg controller is
 * in low power mode, this handler brings the controller out of low power mode
 * before turning on bus power.
 */
static void dwc2_handle_session_req_intr(struct dwc2_hsotg *hsotg)
{
	int ret;

	dev_dbg(hsotg->dev, "Session request interrupt - lx_state=%d\n",
							hsotg->lx_state);

	/* Clear interrupt */
	dwc2_writel(GINTSTS_SESSREQINT, hsotg->regs + GINTSTS);

	if (dwc2_is_device_mode(hsotg)) {
		if (hsotg->lx_state == DWC2_L2) {
			ret = dwc2_exit_hibernation(hsotg, true);
			if (ret && (ret != -ENOTSUPP))
				dev_err(hsotg->dev,
					"exit hibernation failed\n");
		}

		/*
		 * Report disconnect if there is any previous session
		 * established
		 */
		dwc2_hsotg_disconnect(hsotg);
	}
}

/*
 * This interrupt indicates that the DWC_otg controller has detected a
 * resume or remote wakeup sequence. If the DWC_otg controller is in
 * low power mode, the handler must brings the controller out of low
 * power mode. The controller automatically begins resume signaling.
 * The handler schedules a time to stop resume signaling.
 */
static void dwc2_handle_wakeup_detected_intr(struct dwc2_hsotg *hsotg)
{
	int ret;
	dev_dbg(hsotg->dev, "++Resume or Remote Wakeup Detected Interrupt++\n");
	dev_dbg(hsotg->dev, "%s lxstate = %d\n", __func__, hsotg->lx_state);

	if (dwc2_is_device_mode(hsotg)) {
		dev_dbg(hsotg->dev, "DSTS=0x%0x\n",
			dwc2_readl(hsotg->regs + DSTS));
		if (hsotg->lx_state == DWC2_L2) {
			u32 dctl = dwc2_readl(hsotg->regs + DCTL);

			/* Clear Remote Wakeup Signaling */
			dctl &= ~DCTL_RMTWKUPSIG;
			dwc2_writel(dctl, hsotg->regs + DCTL);
			ret = dwc2_exit_hibernation(hsotg, true);
			if (ret && (ret != -ENOTSUPP))
				dev_err(hsotg->dev, "exit hibernation failed\n");

			call_gadget(hsotg, resume);
		}
		/* Change to L0 state */
		hsotg->lx_state = DWC2_L0;
	} else {
		if (hsotg->core_params->hibernation) {
			dwc2_writel(GINTSTS_WKUPINT, hsotg->regs + GINTSTS);
			return;
		}
		if (hsotg->lx_state != DWC2_L1) {
			u32 pcgcctl = dwc2_readl(hsotg->regs + PCGCTL);

			/* Restart the Phy Clock */
			pcgcctl &= ~PCGCTL_STOPPCLK;
			dwc2_writel(pcgcctl, hsotg->regs + PCGCTL);
			mod_timer(&hsotg->wkp_timer,
				  jiffies + msecs_to_jiffies(71));
		} else {
			/* Change to L0 state */
			hsotg->lx_state = DWC2_L0;
		}
	}

	/* Clear interrupt */
	dwc2_writel(GINTSTS_WKUPINT, hsotg->regs + GINTSTS);
}

/*
 * This interrupt indicates that a device has been disconnected from the
 * root port
 */
static void dwc2_handle_disconnect_intr(struct dwc2_hsotg *hsotg)
{
	dev_dbg(hsotg->dev, "++Disconnect Detected Interrupt++ (%s) %s\n",
		dwc2_is_host_mode(hsotg) ? "Host" : "Device",
		dwc2_op_state_str(hsotg));

	if (hsotg->op_state == OTG_STATE_A_HOST)
		dwc2_hcd_disconnect(hsotg);

	dwc2_writel(GINTSTS_DISCONNINT, hsotg->regs + GINTSTS);
}

/*
 * This interrupt indicates that SUSPEND state has been detected on the USB.
 *
 * For HNP the USB Suspend interrupt signals the change from "a_peripheral"
 * to "a_host".
 *
 * When power management is enabled the core will be put in low power mode.
 */
static void dwc2_handle_usb_suspend_intr(struct dwc2_hsotg *hsotg)
{
	u32 dsts;
	int ret;

	dev_dbg(hsotg->dev, "USB SUSPEND\n");

	if (dwc2_is_device_mode(hsotg)) {
		/*
		 * Check the Device status register to determine if the Suspend
		 * state is active
		 */
		dsts = dwc2_readl(hsotg->regs + DSTS);
		dev_dbg(hsotg->dev, "DSTS=0x%0x\n", dsts);
		dev_dbg(hsotg->dev,
			"DSTS.Suspend Status=%d HWCFG4.Power Optimize=%d\n",
			!!(dsts & DSTS_SUSPSTS),
			hsotg->hw_params.power_optimized);
		if ((dsts & DSTS_SUSPSTS) && hsotg->hw_params.power_optimized) {
			/* Ignore suspend request before enumeration */
			if (!dwc2_is_device_connected(hsotg)) {
				dev_dbg(hsotg->dev,
						"ignore suspend request before enumeration\n");
				goto clear_int;
			}

			ret = dwc2_enter_hibernation(hsotg);
			if (ret) {
				if (ret != -ENOTSUPP)
					dev_err(hsotg->dev,
							"enter hibernation failed\n");
				goto skip_power_saving;
			}

			udelay(100);

			/* Ask phy to be suspended */
			if (!IS_ERR_OR_NULL(hsotg->uphy))
				usb_phy_set_suspend(hsotg->uphy, true);
skip_power_saving:
			/*
			 * Change to L2 (suspend) state before releasing
			 * spinlock
			 */
			hsotg->lx_state = DWC2_L2;

			/* Call gadget suspend callback */
			call_gadget(hsotg, suspend);
		}
	} else {
		if (hsotg->op_state == OTG_STATE_A_PERIPHERAL) {
			dev_dbg(hsotg->dev, "a_peripheral->a_host\n");

			/* Change to L2 (suspend) state */
			hsotg->lx_state = DWC2_L2;
			/* Clear the a_peripheral flag, back to a_host */
			spin_unlock(&hsotg->lock);
			dwc2_hcd_start(hsotg);
			spin_lock(&hsotg->lock);
			hsotg->op_state = OTG_STATE_A_HOST;
		}
	}

clear_int:
	/* Clear interrupt */
	dwc2_writel(GINTSTS_USBSUSP, hsotg->regs + GINTSTS);
}

#define GINTMSK_COMMON	(GINTSTS_WKUPINT | GINTSTS_SESSREQINT |		\
			 GINTSTS_CONIDSTSCHNG | GINTSTS_OTGINT |	\
			 GINTSTS_MODEMIS | GINTSTS_DISCONNINT |		\
			 GINTSTS_USBSUSP | GINTSTS_PRTINT)

/*
 * This function returns the Core Interrupt register
 */
static u32 dwc2_read_common_intr(struct dwc2_hsotg *hsotg)
{
	u32 gintsts;
	u32 gintmsk;
	u32 gahbcfg;
	u32 gintmsk_common = GINTMSK_COMMON;

	gintsts = dwc2_readl(hsotg->regs + GINTSTS);
	gintmsk = dwc2_readl(hsotg->regs + GINTMSK);
	gahbcfg = dwc2_readl(hsotg->regs + GAHBCFG);

	/* If any common interrupts set */
	if (gintsts & gintmsk_common)
		dev_dbg(hsotg->dev, "gintsts=%08x  gintmsk=%08x\n",
			gintsts, gintmsk);

	if (gahbcfg & GAHBCFG_GLBL_INTR_EN)
		return gintsts & gintmsk & gintmsk_common;
	else
		return 0;
}

/*
 * Common interrupt handler
 *
 * The common interrupts are those that occur in both Host and Device mode.
 * This handler handles the following interrupts:
 * - Mode Mismatch Interrupt
 * - OTG Interrupt
 * - Connector ID Status Change Interrupt
 * - Disconnect Interrupt
 * - Session Request Interrupt
 * - Resume / Remote Wakeup Detected Interrupt
 * - Suspend Interrupt
 */
irqreturn_t dwc2_handle_common_intr(int irq, void *dev)
{
	struct dwc2_hsotg *hsotg = dev;
	u32 gintsts;
	irqreturn_t retval = IRQ_NONE;

	spin_lock(&hsotg->lock);

	if (!dwc2_is_controller_alive(hsotg)) {
		dev_warn(hsotg->dev, "Controller is dead\n");
		goto out;
	}

	gintsts = dwc2_read_common_intr(hsotg);
	if (gintsts & ~GINTSTS_PRTINT)
		retval = IRQ_HANDLED;

	if (gintsts & GINTSTS_MODEMIS)
		dwc2_handle_mode_mismatch_intr(hsotg);
	if (gintsts & GINTSTS_OTGINT)
		dwc2_handle_otg_intr(hsotg);
	if (gintsts & GINTSTS_CONIDSTSCHNG)
		dwc2_handle_conn_id_status_change_intr(hsotg);
	if (gintsts & GINTSTS_DISCONNINT)
		dwc2_handle_disconnect_intr(hsotg);
	if (gintsts & GINTSTS_SESSREQINT)
		dwc2_handle_session_req_intr(hsotg);
	if (gintsts & GINTSTS_WKUPINT)
		dwc2_handle_wakeup_detected_intr(hsotg);
	if (gintsts & GINTSTS_USBSUSP)
		dwc2_handle_usb_suspend_intr(hsotg);

	if (gintsts & GINTSTS_PRTINT) {
		/*
		 * The port interrupt occurs while in device mode with HPRT0
		 * Port Enable/Disable
		 */
		if (dwc2_is_device_mode(hsotg)) {
			dev_dbg(hsotg->dev,
				" --Port interrupt received in Device mode--\n");
			dwc2_handle_usb_port_intr(hsotg);
			retval = IRQ_HANDLED;
		}
	}

out:
	spin_unlock(&hsotg->lock);
	return retval;
}
