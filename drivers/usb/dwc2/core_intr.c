// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/*
 * core_intr.c - DesignWare HS OTG Controller common interrupt handling
 *
 * Copyright (C) 2004-2013 Synopsys, Inc.
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
	u32 hprt0 = dwc2_readl(hsotg, HPRT0);

	if (hprt0 & HPRT0_ENACHG) {
		hprt0 &= ~HPRT0_ENA;
		dwc2_writel(hsotg, hprt0, HPRT0);
	}
}

/**
 * dwc2_handle_mode_mismatch_intr() - Logs a mode mismatch warning message
 *
 * @hsotg: Programming view of DWC_otg controller
 */
static void dwc2_handle_mode_mismatch_intr(struct dwc2_hsotg *hsotg)
{
	/* Clear interrupt */
	dwc2_writel(hsotg, GINTSTS_MODEMIS, GINTSTS);

	dev_warn(hsotg->dev, "Mode Mismatch Interrupt: currently in %s mode\n",
		 dwc2_is_host_mode(hsotg) ? "Host" : "Device");
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

	gotgint = dwc2_readl(hsotg, GOTGINT);
	gotgctl = dwc2_readl(hsotg, GOTGCTL);
	dev_dbg(hsotg->dev, "++OTG Interrupt gotgint=%0x [%s]\n", gotgint,
		dwc2_op_state_str(hsotg));

	if (gotgint & GOTGINT_SES_END_DET) {
		dev_dbg(hsotg->dev,
			" ++OTG Interrupt: Session End Detected++ (%s)\n",
			dwc2_op_state_str(hsotg));
		gotgctl = dwc2_readl(hsotg, GOTGCTL);

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

		gotgctl = dwc2_readl(hsotg, GOTGCTL);
		gotgctl &= ~GOTGCTL_DEVHNPEN;
		dwc2_writel(hsotg, gotgctl, GOTGCTL);
	}

	if (gotgint & GOTGINT_SES_REQ_SUC_STS_CHNG) {
		dev_dbg(hsotg->dev,
			" ++OTG Interrupt: Session Request Success Status Change++\n");
		gotgctl = dwc2_readl(hsotg, GOTGCTL);
		if (gotgctl & GOTGCTL_SESREQSCS) {
			if (hsotg->params.phy_type == DWC2_PHY_TYPE_PARAM_FS &&
			    hsotg->params.i2c_enable) {
				hsotg->srp_success = 1;
			} else {
				/* Clear Session Request */
				gotgctl = dwc2_readl(hsotg, GOTGCTL);
				gotgctl &= ~GOTGCTL_SESREQ;
				dwc2_writel(hsotg, gotgctl, GOTGCTL);
			}
		}
	}

	if (gotgint & GOTGINT_HST_NEG_SUC_STS_CHNG) {
		/*
		 * Print statements during the HNP interrupt handling
		 * can cause it to fail
		 */
		gotgctl = dwc2_readl(hsotg, GOTGCTL);
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
				gintmsk = dwc2_readl(hsotg, GINTMSK);
				gintmsk &= ~GINTSTS_SOF;
				dwc2_writel(hsotg, gintmsk, GINTMSK);

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
			gotgctl = dwc2_readl(hsotg, GOTGCTL);
			gotgctl &= ~(GOTGCTL_HNPREQ | GOTGCTL_DEVHNPEN);
			dwc2_writel(hsotg, gotgctl, GOTGCTL);
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
			dwc2_hcd_disconnect(hsotg, false);
			spin_lock(&hsotg->lock);
			hsotg->op_state = OTG_STATE_A_PERIPHERAL;
		} else {
			/* Need to disable SOF interrupt immediately */
			gintmsk = dwc2_readl(hsotg, GINTMSK);
			gintmsk &= ~GINTSTS_SOF;
			dwc2_writel(hsotg, gintmsk, GINTMSK);
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
	dwc2_writel(hsotg, gotgint, GOTGINT);
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
	u32 gintmsk;

	/* Clear interrupt */
	dwc2_writel(hsotg, GINTSTS_CONIDSTSCHNG, GINTSTS);

	/* Need to disable SOF interrupt immediately */
	gintmsk = dwc2_readl(hsotg, GINTMSK);
	gintmsk &= ~GINTSTS_SOF;
	dwc2_writel(hsotg, gintmsk, GINTMSK);

	dev_dbg(hsotg->dev, " ++Connector ID Status Change Interrupt++  (%s)\n",
		dwc2_is_host_mode(hsotg) ? "Host" : "Device");

	/*
	 * Need to schedule a work, as there are possible DELAY function calls.
	 */
	if (hsotg->wq_otg)
		queue_work(hsotg->wq_otg, &hsotg->wf_otg);
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
	u32 hprt0;

	/* Clear interrupt */
	dwc2_writel(hsotg, GINTSTS_SESSREQINT, GINTSTS);

	dev_dbg(hsotg->dev, "Session request interrupt - lx_state=%d\n",
		hsotg->lx_state);

	if (dwc2_is_device_mode(hsotg)) {
		if (hsotg->lx_state == DWC2_L2) {
			if (hsotg->in_ppd) {
				ret = dwc2_exit_partial_power_down(hsotg, 0,
								   true);
				if (ret)
					dev_err(hsotg->dev,
						"exit power_down failed\n");
			}

			/* Exit gadget mode clock gating. */
			if (hsotg->params.power_down ==
			    DWC2_POWER_DOWN_PARAM_NONE && hsotg->bus_suspended)
				dwc2_gadget_exit_clock_gating(hsotg, 0);
		}

		/*
		 * Report disconnect if there is any previous session
		 * established
		 */
		dwc2_hsotg_disconnect(hsotg);
	} else {
		/* Turn on the port power bit. */
		hprt0 = dwc2_read_hprt0(hsotg);
		hprt0 |= HPRT0_PWR;
		dwc2_writel(hsotg, hprt0, HPRT0);
		/* Connect hcd after port power is set. */
		dwc2_hcd_connect(hsotg);
	}
}

/**
 * dwc2_wakeup_from_lpm_l1 - Exit the device from LPM L1 state
 *
 * @hsotg: Programming view of DWC_otg controller
 *
 */
static void dwc2_wakeup_from_lpm_l1(struct dwc2_hsotg *hsotg)
{
	u32 glpmcfg;
	u32 i = 0;

	if (hsotg->lx_state != DWC2_L1) {
		dev_err(hsotg->dev, "Core isn't in DWC2_L1 state\n");
		return;
	}

	glpmcfg = dwc2_readl(hsotg, GLPMCFG);
	if (dwc2_is_device_mode(hsotg)) {
		dev_dbg(hsotg->dev, "Exit from L1 state\n");
		glpmcfg &= ~GLPMCFG_ENBLSLPM;
		glpmcfg &= ~GLPMCFG_HIRD_THRES_EN;
		dwc2_writel(hsotg, glpmcfg, GLPMCFG);

		do {
			glpmcfg = dwc2_readl(hsotg, GLPMCFG);

			if (!(glpmcfg & (GLPMCFG_COREL1RES_MASK |
					 GLPMCFG_L1RESUMEOK | GLPMCFG_SLPSTS)))
				break;

			udelay(1);
		} while (++i < 200);

		if (i == 200) {
			dev_err(hsotg->dev, "Failed to exit L1 sleep state in 200us.\n");
			return;
		}
		dwc2_gadget_init_lpm(hsotg);
	} else {
		/* TODO */
		dev_err(hsotg->dev, "Host side LPM is not supported.\n");
		return;
	}

	/* Change to L0 state */
	hsotg->lx_state = DWC2_L0;

	/* Inform gadget to exit from L1 */
	call_gadget(hsotg, resume);
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

	/* Clear interrupt */
	dwc2_writel(hsotg, GINTSTS_WKUPINT, GINTSTS);

	dev_dbg(hsotg->dev, "++Resume or Remote Wakeup Detected Interrupt++\n");
	dev_dbg(hsotg->dev, "%s lxstate = %d\n", __func__, hsotg->lx_state);

	if (hsotg->lx_state == DWC2_L1) {
		dwc2_wakeup_from_lpm_l1(hsotg);
		return;
	}

	if (dwc2_is_device_mode(hsotg)) {
		dev_dbg(hsotg->dev, "DSTS=0x%0x\n",
			dwc2_readl(hsotg, DSTS));
		if (hsotg->lx_state == DWC2_L2) {
			if (hsotg->in_ppd) {
				u32 dctl = dwc2_readl(hsotg, DCTL);
				/* Clear Remote Wakeup Signaling */
				dctl &= ~DCTL_RMTWKUPSIG;
				dwc2_writel(hsotg, dctl, DCTL);
				ret = dwc2_exit_partial_power_down(hsotg, 1,
								   true);
				if (ret)
					dev_err(hsotg->dev,
						"exit partial_power_down failed\n");
				call_gadget(hsotg, resume);
			}

			/* Exit gadget mode clock gating. */
			if (hsotg->params.power_down ==
			    DWC2_POWER_DOWN_PARAM_NONE && hsotg->bus_suspended)
				dwc2_gadget_exit_clock_gating(hsotg, 0);
		} else {
			/* Change to L0 state */
			hsotg->lx_state = DWC2_L0;
		}
	} else {
		if (hsotg->lx_state == DWC2_L2) {
			if (hsotg->in_ppd) {
				ret = dwc2_exit_partial_power_down(hsotg, 1,
								   true);
				if (ret)
					dev_err(hsotg->dev,
						"exit partial_power_down failed\n");
			}

			if (hsotg->params.power_down ==
			    DWC2_POWER_DOWN_PARAM_NONE && hsotg->bus_suspended)
				dwc2_host_exit_clock_gating(hsotg, 1);

			/*
			 * If we've got this quirk then the PHY is stuck upon
			 * wakeup.  Assert reset.  This will propagate out and
			 * eventually we'll re-enumerate the device.  Not great
			 * but the best we can do.  We can't call phy_reset()
			 * at interrupt time but there's no hurry, so we'll
			 * schedule it for later.
			 */
			if (hsotg->reset_phy_on_wake)
				dwc2_host_schedule_phy_reset(hsotg);

			mod_timer(&hsotg->wkp_timer,
				  jiffies + msecs_to_jiffies(71));
		} else {
			/* Change to L0 state */
			hsotg->lx_state = DWC2_L0;
		}
	}
}

/*
 * This interrupt indicates that a device has been disconnected from the
 * root port
 */
static void dwc2_handle_disconnect_intr(struct dwc2_hsotg *hsotg)
{
	dwc2_writel(hsotg, GINTSTS_DISCONNINT, GINTSTS);

	dev_dbg(hsotg->dev, "++Disconnect Detected Interrupt++ (%s) %s\n",
		dwc2_is_host_mode(hsotg) ? "Host" : "Device",
		dwc2_op_state_str(hsotg));

	if (hsotg->op_state == OTG_STATE_A_HOST)
		dwc2_hcd_disconnect(hsotg, false);
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

	/* Clear interrupt */
	dwc2_writel(hsotg, GINTSTS_USBSUSP, GINTSTS);

	dev_dbg(hsotg->dev, "USB SUSPEND\n");

	if (dwc2_is_device_mode(hsotg)) {
		/*
		 * Check the Device status register to determine if the Suspend
		 * state is active
		 */
		dsts = dwc2_readl(hsotg, DSTS);
		dev_dbg(hsotg->dev, "%s: DSTS=0x%0x\n", __func__, dsts);
		dev_dbg(hsotg->dev,
			"DSTS.Suspend Status=%d HWCFG4.Power Optimize=%d HWCFG4.Hibernation=%d\n",
			!!(dsts & DSTS_SUSPSTS),
			hsotg->hw_params.power_optimized,
			hsotg->hw_params.hibernation);

		/* Ignore suspend request before enumeration */
		if (!dwc2_is_device_connected(hsotg)) {
			dev_dbg(hsotg->dev,
				"ignore suspend request before enumeration\n");
			return;
		}
		if (dsts & DSTS_SUSPSTS) {
			switch (hsotg->params.power_down) {
			case DWC2_POWER_DOWN_PARAM_PARTIAL:
				ret = dwc2_enter_partial_power_down(hsotg);
				if (ret)
					dev_err(hsotg->dev,
						"enter partial_power_down failed\n");

				udelay(100);

				/* Ask phy to be suspended */
				if (!IS_ERR_OR_NULL(hsotg->uphy))
					usb_phy_set_suspend(hsotg->uphy, true);
				break;
			case DWC2_POWER_DOWN_PARAM_HIBERNATION:
				ret = dwc2_enter_hibernation(hsotg, 0);
				if (ret)
					dev_err(hsotg->dev,
						"enter hibernation failed\n");
				break;
			case DWC2_POWER_DOWN_PARAM_NONE:
				/*
				 * If neither hibernation nor partial power down are supported,
				 * clock gating is used to save power.
				 */
				if (!hsotg->params.no_clock_gating)
					dwc2_gadget_enter_clock_gating(hsotg);
			}

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
}

/**
 * dwc2_handle_lpm_intr - GINTSTS_LPMTRANRCVD Interrupt handler
 *
 * @hsotg: Programming view of DWC_otg controller
 *
 */
static void dwc2_handle_lpm_intr(struct dwc2_hsotg *hsotg)
{
	u32 glpmcfg;
	u32 pcgcctl;
	u32 hird;
	u32 hird_thres;
	u32 hird_thres_en;
	u32 enslpm;

	/* Clear interrupt */
	dwc2_writel(hsotg, GINTSTS_LPMTRANRCVD, GINTSTS);

	glpmcfg = dwc2_readl(hsotg, GLPMCFG);

	if (!(glpmcfg & GLPMCFG_LPMCAP)) {
		dev_err(hsotg->dev, "Unexpected LPM interrupt\n");
		return;
	}

	hird = (glpmcfg & GLPMCFG_HIRD_MASK) >> GLPMCFG_HIRD_SHIFT;
	hird_thres = (glpmcfg & GLPMCFG_HIRD_THRES_MASK &
			~GLPMCFG_HIRD_THRES_EN) >> GLPMCFG_HIRD_THRES_SHIFT;
	hird_thres_en = glpmcfg & GLPMCFG_HIRD_THRES_EN;
	enslpm = glpmcfg & GLPMCFG_ENBLSLPM;

	if (dwc2_is_device_mode(hsotg)) {
		dev_dbg(hsotg->dev, "HIRD_THRES_EN = %d\n", hird_thres_en);

		if (hird_thres_en && hird >= hird_thres) {
			dev_dbg(hsotg->dev, "L1 with utmi_l1_suspend_n\n");
		} else if (enslpm) {
			dev_dbg(hsotg->dev, "L1 with utmi_sleep_n\n");
		} else {
			dev_dbg(hsotg->dev, "Entering Sleep with L1 Gating\n");

			pcgcctl = dwc2_readl(hsotg, PCGCTL);
			pcgcctl |= PCGCTL_ENBL_SLEEP_GATING;
			dwc2_writel(hsotg, pcgcctl, PCGCTL);
		}
		/**
		 * Examine prt_sleep_sts after TL1TokenTetry period max (10 us)
		 */
		udelay(10);

		glpmcfg = dwc2_readl(hsotg, GLPMCFG);

		if (glpmcfg & GLPMCFG_SLPSTS) {
			/* Save the current state */
			hsotg->lx_state = DWC2_L1;
			dev_dbg(hsotg->dev,
				"Core is in L1 sleep glpmcfg=%08x\n", glpmcfg);

			/* Inform gadget that we are in L1 state */
			call_gadget(hsotg, suspend);
		}
	}
}

#define GINTMSK_COMMON	(GINTSTS_WKUPINT | GINTSTS_SESSREQINT |		\
			 GINTSTS_CONIDSTSCHNG | GINTSTS_OTGINT |	\
			 GINTSTS_MODEMIS | GINTSTS_DISCONNINT |		\
			 GINTSTS_USBSUSP | GINTSTS_PRTINT |		\
			 GINTSTS_LPMTRANRCVD)

/*
 * This function returns the Core Interrupt register
 */
static u32 dwc2_read_common_intr(struct dwc2_hsotg *hsotg)
{
	u32 gintsts;
	u32 gintmsk;
	u32 gahbcfg;
	u32 gintmsk_common = GINTMSK_COMMON;

	gintsts = dwc2_readl(hsotg, GINTSTS);
	gintmsk = dwc2_readl(hsotg, GINTMSK);
	gahbcfg = dwc2_readl(hsotg, GAHBCFG);

	/* If any common interrupts set */
	if (gintsts & gintmsk_common)
		dev_dbg(hsotg->dev, "gintsts=%08x  gintmsk=%08x\n",
			gintsts, gintmsk);

	if (gahbcfg & GAHBCFG_GLBL_INTR_EN)
		return gintsts & gintmsk & gintmsk_common;
	else
		return 0;
}

/**
 * dwc_handle_gpwrdn_disc_det() - Handles the gpwrdn disconnect detect.
 * Exits hibernation without restoring registers.
 *
 * @hsotg: Programming view of DWC_otg controller
 * @gpwrdn: GPWRDN register
 */
static inline void dwc_handle_gpwrdn_disc_det(struct dwc2_hsotg *hsotg,
					      u32 gpwrdn)
{
	u32 gpwrdn_tmp;

	/* Switch-on voltage to the core */
	gpwrdn_tmp = dwc2_readl(hsotg, GPWRDN);
	gpwrdn_tmp &= ~GPWRDN_PWRDNSWTCH;
	dwc2_writel(hsotg, gpwrdn_tmp, GPWRDN);
	udelay(5);

	/* Reset core */
	gpwrdn_tmp = dwc2_readl(hsotg, GPWRDN);
	gpwrdn_tmp &= ~GPWRDN_PWRDNRSTN;
	dwc2_writel(hsotg, gpwrdn_tmp, GPWRDN);
	udelay(5);

	/* Disable Power Down Clamp */
	gpwrdn_tmp = dwc2_readl(hsotg, GPWRDN);
	gpwrdn_tmp &= ~GPWRDN_PWRDNCLMP;
	dwc2_writel(hsotg, gpwrdn_tmp, GPWRDN);
	udelay(5);

	/* Deassert reset core */
	gpwrdn_tmp = dwc2_readl(hsotg, GPWRDN);
	gpwrdn_tmp |= GPWRDN_PWRDNRSTN;
	dwc2_writel(hsotg, gpwrdn_tmp, GPWRDN);
	udelay(5);

	/* Disable PMU interrupt */
	gpwrdn_tmp = dwc2_readl(hsotg, GPWRDN);
	gpwrdn_tmp &= ~GPWRDN_PMUINTSEL;
	dwc2_writel(hsotg, gpwrdn_tmp, GPWRDN);

	/* De-assert Wakeup Logic */
	gpwrdn_tmp = dwc2_readl(hsotg, GPWRDN);
	gpwrdn_tmp &= ~GPWRDN_PMUACTV;
	dwc2_writel(hsotg, gpwrdn_tmp, GPWRDN);

	hsotg->hibernated = 0;
	hsotg->bus_suspended = 0;

	if (gpwrdn & GPWRDN_IDSTS) {
		hsotg->op_state = OTG_STATE_B_PERIPHERAL;
		dwc2_core_init(hsotg, false);
		dwc2_enable_global_interrupts(hsotg);
		dwc2_hsotg_core_init_disconnected(hsotg, false);
		dwc2_hsotg_core_connect(hsotg);
	} else {
		hsotg->op_state = OTG_STATE_A_HOST;

		/* Initialize the Core for Host mode */
		dwc2_core_init(hsotg, false);
		dwc2_enable_global_interrupts(hsotg);
		dwc2_hcd_start(hsotg);
	}
}

/*
 * GPWRDN interrupt handler.
 *
 * The GPWRDN interrupts are those that occur in both Host and
 * Device mode while core is in hibernated state.
 */
static int dwc2_handle_gpwrdn_intr(struct dwc2_hsotg *hsotg)
{
	u32 gpwrdn;
	int linestate;
	int ret = 0;

	gpwrdn = dwc2_readl(hsotg, GPWRDN);
	/* clear all interrupt */
	dwc2_writel(hsotg, gpwrdn, GPWRDN);
	linestate = (gpwrdn & GPWRDN_LINESTATE_MASK) >> GPWRDN_LINESTATE_SHIFT;
	dev_dbg(hsotg->dev,
		"%s: dwc2_handle_gpwrdwn_intr called gpwrdn= %08x\n", __func__,
		gpwrdn);

	if ((gpwrdn & GPWRDN_DISCONN_DET) &&
	    (gpwrdn & GPWRDN_DISCONN_DET_MSK) && !linestate) {
		dev_dbg(hsotg->dev, "%s: GPWRDN_DISCONN_DET\n", __func__);
		/*
		 * Call disconnect detect function to exit from
		 * hibernation
		 */
		dwc_handle_gpwrdn_disc_det(hsotg, gpwrdn);
	} else if ((gpwrdn & GPWRDN_LNSTSCHG) &&
		   (gpwrdn & GPWRDN_LNSTSCHG_MSK) && linestate) {
		dev_dbg(hsotg->dev, "%s: GPWRDN_LNSTSCHG\n", __func__);
		if (hsotg->hw_params.hibernation &&
		    hsotg->hibernated) {
			if (gpwrdn & GPWRDN_IDSTS) {
				ret = dwc2_exit_hibernation(hsotg, 0, 0, 0);
				if (ret)
					dev_err(hsotg->dev,
						"exit hibernation failed.\n");
				call_gadget(hsotg, resume);
			} else {
				ret = dwc2_exit_hibernation(hsotg, 1, 0, 1);
				if (ret)
					dev_err(hsotg->dev,
						"exit hibernation failed.\n");
			}
		}
	} else if ((gpwrdn & GPWRDN_RST_DET) &&
		   (gpwrdn & GPWRDN_RST_DET_MSK)) {
		dev_dbg(hsotg->dev, "%s: GPWRDN_RST_DET\n", __func__);
		if (!linestate) {
			ret = dwc2_exit_hibernation(hsotg, 0, 1, 0);
			if (ret)
				dev_err(hsotg->dev,
					"exit hibernation failed.\n");
		}
	} else if ((gpwrdn & GPWRDN_STS_CHGINT) &&
		   (gpwrdn & GPWRDN_STS_CHGINT_MSK)) {
		dev_dbg(hsotg->dev, "%s: GPWRDN_STS_CHGINT\n", __func__);
		/*
		 * As GPWRDN_STS_CHGINT exit from hibernation flow is
		 * the same as in GPWRDN_DISCONN_DET flow. Call
		 * disconnect detect helper function to exit from
		 * hibernation.
		 */
		dwc_handle_gpwrdn_disc_det(hsotg, gpwrdn);
	}

	return ret;
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

	/* Reading current frame number value in device or host modes. */
	if (dwc2_is_device_mode(hsotg))
		hsotg->frame_number = (dwc2_readl(hsotg, DSTS)
				       & DSTS_SOFFN_MASK) >> DSTS_SOFFN_SHIFT;
	else
		hsotg->frame_number = (dwc2_readl(hsotg, HFNUM)
				       & HFNUM_FRNUM_MASK) >> HFNUM_FRNUM_SHIFT;

	gintsts = dwc2_read_common_intr(hsotg);
	if (gintsts & ~GINTSTS_PRTINT)
		retval = IRQ_HANDLED;

	/* In case of hibernated state gintsts must not work */
	if (hsotg->hibernated) {
		dwc2_handle_gpwrdn_intr(hsotg);
		retval = IRQ_HANDLED;
		goto out;
	}

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
	if (gintsts & GINTSTS_LPMTRANRCVD)
		dwc2_handle_lpm_intr(hsotg);

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
