/* ==========================================================================
 * $File: //dwh/usb_iip/dev/software/otg/linux/drivers/dwc_otg_cil_intr.c $
 * $Revision: #35 $
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

/** @file
 *
 * The Core Interface Layer provides basic services for accessing and
 * managing the DWC_otg hardware. These services are used by both the
 * Host Controller Driver and the Peripheral Controller Driver.
 *
 * This file contains the Common Interrupt handlers.
 */
#include "common_port/dwc_os.h"
#include "dwc_otg_regs.h"
#include "dwc_otg_cil.h"
#include "dwc_otg_driver.h"
#include "dwc_otg_pcd.h"
#include "dwc_otg_hcd.h"
#include "usbdev_rk.h"

#ifdef DEBUG
inline const char *op_state_str(dwc_otg_core_if_t *core_if)
{
	return (core_if->op_state == A_HOST ? "a_host" :
		(core_if->op_state == A_SUSPEND ? "a_suspend" :
		 (core_if->op_state == A_PERIPHERAL ? "a_peripheral" :
		  (core_if->op_state == B_PERIPHERAL ? "b_peripheral" :
		   (core_if->op_state == B_HOST ? "b_host" : "unknown")))));
}
#endif

/** This function will log a debug message
 *
 * @param core_if Programming view of DWC_otg controller.
 */
int32_t dwc_otg_handle_mode_mismatch_intr(dwc_otg_core_if_t *core_if)
{
	gintsts_data_t gintsts;
	DWC_WARN("Mode Mismatch Interrupt: currently in %s mode\n",
		 dwc_otg_mode(core_if) ? "Host" : "Device");

	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.modemismatch = 1;
	DWC_WRITE_REG32(&core_if->core_global_regs->gintsts, gintsts.d32);
	return 1;
}

/**
 * This function handles the OTG Interrupts. It reads the OTG
 * Interrupt Register (GOTGINT) to determine what interrupt has
 * occurred.
 *
 * @param core_if Programming view of DWC_otg controller.
 */
int32_t dwc_otg_handle_otg_intr(dwc_otg_core_if_t *core_if)
{
	dwc_otg_core_global_regs_t *global_regs = core_if->core_global_regs;
	gotgint_data_t gotgint;
	gotgctl_data_t gotgctl;
	gintmsk_data_t gintmsk;
	gpwrdn_data_t gpwrdn;
	dctl_data_t dctl = {.d32 = 0 };

	gotgint.d32 = DWC_READ_REG32(&global_regs->gotgint);
	gotgctl.d32 = DWC_READ_REG32(&global_regs->gotgctl);
	DWC_DEBUGPL(DBG_CIL, "++OTG Interrupt gotgint=%0x [%s]\n", gotgint.d32,
		    op_state_str(core_if));

	if (gotgint.b.sesenddet) {
		DWC_DEBUGPL(DBG_ANY, " ++OTG Interrupt: "
			    "Session End Detected++ (%s)\n",
			    op_state_str(core_if));

		/* do soft disconnect */
		dctl.d32 =
		    DWC_READ_REG32(&core_if->dev_if->dev_global_regs->dctl);
		dctl.b.sftdiscon = 1;
		DWC_WRITE_REG32(&core_if->dev_if->dev_global_regs->dctl,
				dctl.d32);
		dwc_otg_disable_global_interrupts(core_if);
		core_if->otg_dev->pcd->vbus_status = USB_BC_TYPE_DISCNT;

		DWC_PRINTF("********session end ,soft disconnect***********\n");

		gotgctl.d32 = DWC_READ_REG32(&global_regs->gotgctl);

		if (core_if->op_state == B_HOST) {
			cil_pcd_start(core_if);
			core_if->op_state = B_PERIPHERAL;
		} else {
			/* If not B_HOST and Device HNP still set. HNP
			 * Did not succeed!*/
			if (gotgctl.b.devhnpen) {
				DWC_DEBUGPL(DBG_ANY, "Session End Detected\n");
				__DWC_ERROR
				    ("Device Not Connected/Responding!\n");
			}

			/* If Session End Detected the B-Cable has
			 * been disconnected. */
			/* Reset PCD and Gadget driver to a
			 * clean state. */
			core_if->lx_state = DWC_OTG_L0;
			DWC_SPINUNLOCK(core_if->lock);
			cil_pcd_stop(core_if);
			DWC_SPINLOCK(core_if->lock);

			if (core_if->otg_ver) {
				/** PET testing*/
				gotgctl.d32 = 0;
				gotgctl.b.devhnpen = 1;
				DWC_MODIFY_REG32(&global_regs->gotgctl,
						 gotgctl.d32, 0);
				if (core_if->test_mode == 6) {
					/* manukz: old value was 50 */
					DWC_WORKQ_SCHEDULE_DELAYED(core_if->wq_otg,
								   dwc_otg_initiate_srp,
								   core_if, 3000,
								   "initate SRP");
					core_if->test_mode = 0;
				} else if (core_if->adp_enable) {
					if (core_if->power_down == 2) {
						gpwrdn.d32 = 0;
						gpwrdn.b.pwrdnswtch = 1;
						DWC_MODIFY_REG32
						    (&core_if->core_global_regs->gpwrdn,
						     gpwrdn.d32, 0);
					}

					gpwrdn.d32 = 0;
					gpwrdn.b.pmuintsel = 1;
					gpwrdn.b.pmuactv = 1;
					DWC_MODIFY_REG32(&core_if->
							 core_global_regs->
							 gpwrdn, 0, gpwrdn.d32);
					dwc_otg_adp_sense_start(core_if);
				}
			}
		}
		if (core_if->otg_ver == 0) {
			gotgctl.d32 = 0;
			gotgctl.b.devhnpen = 1;
			DWC_MODIFY_REG32(&global_regs->gotgctl, gotgctl.d32, 0);
		}
	}
	if (gotgint.b.sesreqsucstschng) {
		DWC_DEBUGPL(DBG_ANY, " ++OTG Interrupt: "
			    "Session Reqeust Success Status Change++\n");
		gotgctl.d32 = DWC_READ_REG32(&global_regs->gotgctl);
		if (gotgctl.b.sesreqscs) {

			if ((core_if->core_params->phy_type ==
			     DWC_PHY_TYPE_PARAM_FS)
			    && (core_if->core_params->i2c_enable)) {
				core_if->srp_success = 1;
			} else {
				DWC_SPINUNLOCK(core_if->lock);
				cil_pcd_resume(core_if);
				DWC_SPINLOCK(core_if->lock);
				/* Clear Session Request */
				gotgctl.d32 = 0;
				gotgctl.b.sesreq = 1;
				DWC_MODIFY_REG32(&global_regs->gotgctl,
						 gotgctl.d32, 0);
			}
		}
	}
	if (gotgint.b.hstnegsucstschng) {
		/* Print statements during the HNP interrupt handling
		 * can cause it to fail.*/
		gotgctl.d32 = DWC_READ_REG32(&global_regs->gotgctl);
		/* WA for 3.00a- HW is not setting cur_mode, even sometimes
		 * this does not help*/
		if (core_if->snpsid >= OTG_CORE_REV_3_00a)
			dwc_udelay(100);
		if (gotgctl.b.hstnegscs) {
			if (dwc_otg_is_host_mode(core_if)) {
				core_if->op_state = B_HOST;
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
				gintmsk.d32 = 0;
				gintmsk.b.sofintr = 1;
				/* gintmsk.b.usbsuspend = 1; */
				DWC_MODIFY_REG32(&global_regs->gintmsk,
						 gintmsk.d32, 0);
				/* Call callback function with spin lock released */
				DWC_SPINUNLOCK(core_if->lock);
				cil_pcd_stop(core_if);
				/*
				 * Initialize the Core for Host mode.
				 */
				cil_hcd_start(core_if);
				DWC_SPINLOCK(core_if->lock);
			}
		} else {
			gotgctl.d32 = 0;
			gotgctl.b.hnpreq = 1;
			gotgctl.b.devhnpen = 1;
			DWC_MODIFY_REG32(&global_regs->gotgctl, gotgctl.d32, 0);
			DWC_DEBUGPL(DBG_ANY, "HNP Failed\n");
			__DWC_ERROR("Device Not Connected/Responding\n");
		}
	}
	if (gotgint.b.hstnegdet) {
		/* The disconnect interrupt is set at the same time as
		 * Host Negotiation Detected.  During the mode
		 * switch all interrupts are cleared so the disconnect
		 * interrupt handler will not get executed.
		 */
		DWC_DEBUGPL(DBG_ANY, " ++OTG Interrupt: "
			    "Host Negotiation Detected++ (%s)\n",
			    (dwc_otg_is_host_mode(core_if) ? "Host" :
			     "Device"));
		if (dwc_otg_is_device_mode(core_if)) {
			DWC_DEBUGPL(DBG_ANY, "a_suspend->a_peripheral (%d)\n",
				    core_if->op_state);
			DWC_SPINUNLOCK(core_if->lock);
			cil_hcd_disconnect(core_if);
			cil_pcd_start(core_if);
			DWC_SPINLOCK(core_if->lock);
			core_if->op_state = A_PERIPHERAL;
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
			gintmsk.d32 = 0;
			gintmsk.b.sofintr = 1;
			DWC_MODIFY_REG32(&global_regs->gintmsk, gintmsk.d32, 0);
			DWC_SPINUNLOCK(core_if->lock);
			cil_pcd_stop(core_if);
			cil_hcd_start(core_if);
			DWC_SPINLOCK(core_if->lock);
			core_if->op_state = A_HOST;
		}
	}
	if (gotgint.b.adevtoutchng) {
		DWC_DEBUGPL(DBG_ANY, " ++OTG Interrupt: "
			    "A-Device Timeout Change++\n");
	}
	if (gotgint.b.debdone) {
		DWC_DEBUGPL(DBG_ANY, " ++OTG Interrupt: " "Debounce Done++\n");
		/* Need to power off VBUS after 10s if OTG2 non-hnp capable host */
		if (core_if->otg_ver == 1)
			cil_hcd_session_start(core_if);
	}

	/* Clear GOTGINT */
	DWC_WRITE_REG32(&core_if->core_global_regs->gotgint, gotgint.d32);

	return 1;
}

void w_conn_id_status_change(void *p)
{
	dwc_otg_core_if_t *core_if = p;
	uint32_t count = 0;
	gotgctl_data_t gotgctl = {.d32 = 0 };
	dwc_otg_pcd_t *pcd = core_if->otg_dev->pcd;

	gotgctl.d32 = DWC_READ_REG32(&core_if->core_global_regs->gotgctl);
	DWC_DEBUGPL(DBG_CIL, "gotgctl=%0x\n", gotgctl.d32);
	DWC_DEBUGPL(DBG_CIL, "gotgctl.b.conidsts=%d\n", gotgctl.b.conidsts);

	/* B-Device connector (Device Mode) */
	if (gotgctl.b.conidsts) {
		gotgctl_data_t gotgctl_local;

		/* Wait for switch to device mode. */
		while (!dwc_otg_is_device_mode(core_if)) {
			gotgctl_local.d32 =
			    DWC_READ_REG32(&core_if->core_global_regs->gotgctl);
			DWC_DEBUGPL(DBG_ANY,
				    "Waiting for Peripheral Mode, Mode=%s count = %d gotgctl=%08x\n",
				    (dwc_otg_is_host_mode(core_if) ? "Host" :
				     "Peripheral"), count, gotgctl_local.d32);
			dwc_mdelay(1);	/* vahrama previous value was 100 */
			if (!gotgctl_local.b.conidsts)
				goto host;
			if (++count > 10000)
				break;
		}
		DWC_ASSERT(++count < 10000,
			   "Connection id status change timed out");
		core_if->op_state = B_PERIPHERAL;
		cil_hcd_stop(core_if);;
		/* pcd->phy_suspend = 1; */
		pcd->vbus_status = 0;
		dwc_otg_pcd_start_check_vbus_work(pcd);
		if (core_if->otg_ver == 0)
			dwc_otg_core_init(core_if);
		dwc_otg_enable_global_interrupts(core_if);
		cil_pcd_start(core_if);
	} else {
host:
		/* A-Device connector (Host Mode) */
		while (!dwc_otg_is_host_mode(core_if)) {
			DWC_DEBUGPL(DBG_ANY, "Waiting for Host Mode, Mode=%s\n",
				    (dwc_otg_is_host_mode(core_if) ? "Host" :
				     "Peripheral"));
			dwc_mdelay(1);	/* vahrama previously was 100 */
			if (++count > 10000)
				break;
		}
		DWC_ASSERT(++count < 10000,
			   "Connection id status change timed out");
		core_if->op_state = A_HOST;

		cancel_delayed_work(&pcd->check_vbus_work);

		/*
		 * Initialize the Core for Host mode.
		 */
		if (core_if->otg_ver)
			/* To power off the bus in 10s from the beginning
			 * of test while denounce has not come yet */
			cil_hcd_session_start(core_if);
		else
			dwc_otg_core_init(core_if);
		dwc_otg_enable_global_interrupts(core_if);
		cil_hcd_start(core_if);
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
 *
 * @param core_if Programming view of DWC_otg controller.
 */
int32_t dwc_otg_handle_conn_id_status_change_intr(dwc_otg_core_if_t *core_if)
{

	/*
	 * Need to disable SOF interrupt immediately. If switching from device
	 * to host, the PCD interrupt handler won't handle the interrupt if
	 * host mode is already set. The HCD interrupt handler won't get
	 * called if the HCD state is HALT. This means that the interrupt does
	 * not get handled and Linux complains loudly.
	 */
	gintmsk_data_t gintmsk = {.d32 = 0 };
	gintsts_data_t gintsts = {.d32 = 0 };

	dwc_otg_disable_host_interrupts(core_if);
	if (core_if->usb_mode != USB_MODE_NORMAL)
		goto out;

	gintmsk.b.sofintr = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gintmsk, gintmsk.d32, 0);
	DWC_DEBUGPL(DBG_CIL,
		    " ++Connector ID Status Change Interrupt++  (%s)\n",
		    (dwc_otg_is_host_mode(core_if) ? "Host" : "Device"));

	DWC_SPINUNLOCK(core_if->lock);

	/*
	 * Need to schedule a work, as there are possible DELAY function calls
	 * Release lock before scheduling workq as it holds spinlock during scheduling
	 */

	/*DWC_WORKQ_SCHEDULE(core_if->wq_otg, w_conn_id_status_change,
			   core_if, "connection id status change");*/
	DWC_SPINLOCK(core_if->lock);
out:
	/* Set flag and clear interrupt */
	gintsts.b.conidstschng = 1;
	DWC_WRITE_REG32(&core_if->core_global_regs->gintsts, gintsts.d32);

	return 1;
}

/**
 * This interrupt indicates that a device is initiating the Session
 * Request Protocol to request the host to turn on bus power so a new
 * session can begin. The handler responds by turning on bus power. If
 * the DWC_otg controller is in low power mode, the handler brings the
 * controller out of low power mode before turning on bus power.
 *
 * @param core_if Programming view of DWC_otg controller.
 */
int32_t dwc_otg_handle_session_req_intr(dwc_otg_core_if_t *core_if)
{
	gintsts_data_t gintsts;

#ifndef DWC_HOST_ONLY
	DWC_DEBUGPL(DBG_ANY, "++Session Request Interrupt++\n");
	if (dwc_otg_is_device_mode(core_if)) {
		gotgctl_data_t gotgctl = {.d32 = 0 };
		DWC_PRINTF("SRP: Device mode\n");
		gotgctl.d32 =
		    DWC_READ_REG32(&core_if->core_global_regs->gotgctl);
		if (gotgctl.b.sesreqscs)
			DWC_PRINTF("SRP Success\n");
		else
			DWC_PRINTF("SRP Fail\n");
		if (core_if->otg_ver) {
			gotgctl.d32 = 0;
			gotgctl.b.devhnpen = 1;
			DWC_MODIFY_REG32(&core_if->core_global_regs->gotgctl,
					 gotgctl.d32, 0);
		}
	} else {
		hprt0_data_t hprt0;
		DWC_PRINTF("SRP: Host mode\n");

		/* Turn on the port power bit. */
		hprt0.d32 = dwc_otg_read_hprt0(core_if);
		hprt0.b.prtpwr = 1;
		DWC_WRITE_REG32(core_if->host_if->hprt0, hprt0.d32);

		/* Start the Connection timer. So a message can be displayed
		 * if connect does not occur within 10 seconds. */
		cil_hcd_session_start(core_if);
	}
#endif

	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.sessreqintr = 1;
	DWC_WRITE_REG32(&core_if->core_global_regs->gintsts, gintsts.d32);

	return 1;
}

void w_wakeup_detected(void *data)
{
	dwc_otg_core_if_t *core_if = (dwc_otg_core_if_t *) data;
	/*
	 * Clear the Resume after 70ms. (Need 20 ms minimum. Use 70 ms
	 * so that OPT tests pass with all PHYs).
	 */
	hprt0_data_t hprt0 = {.d32 = 0 };
#if 0
	pcgcctl_data_t pcgcctl = {.d32 = 0 };
	/* Restart the Phy Clock */
	pcgcctl.b.stoppclk = 1;
	DWC_MODIFY_REG32(core_if->pcgcctl, pcgcctl.d32, 0);
	dwc_udelay(10);
#endif
	hprt0.d32 = dwc_otg_read_hprt0(core_if);
	DWC_DEBUGPL(DBG_ANY, "Resume: HPRT0=%0x\n", hprt0.d32);
	dwc_mdelay(70);
	hprt0.b.prtres = 0;	/* Resume */
	DWC_WRITE_REG32(core_if->host_if->hprt0, hprt0.d32);
	DWC_DEBUGPL(DBG_ANY, "Clear Resume: HPRT0=%0x\n",
		    DWC_READ_REG32(core_if->host_if->hprt0));

	cil_hcd_resume(core_if);

	/** Change to L0 state*/
	core_if->lx_state = DWC_OTG_L0;
}

/**
 * This interrupt indicates that the DWC_otg controller has detected a
 * resume or remote wakeup sequence. If the DWC_otg controller is in
 * low power mode, the handler must brings the controller out of low
 * power mode. The controller automatically begins resume
 * signaling. The handler schedules a time to stop resume signaling.
 */
int32_t dwc_otg_handle_wakeup_detected_intr(dwc_otg_core_if_t *core_if)
{
	gintsts_data_t gintsts;

	DWC_DEBUGPL(DBG_ANY,
		    "++Resume and Remote Wakeup Detected Interrupt++\n");

	DWC_PRINTF("%s lxstate = %d\n", __func__, core_if->lx_state);

	if (dwc_otg_is_device_mode(core_if)) {
		dctl_data_t dctl = {.d32 = 0 };
		DWC_DEBUGPL(DBG_PCD, "DSTS=0x%0x\n",
			    DWC_READ_REG32(&core_if->dev_if->
					   dev_global_regs->dsts));
		if (core_if->lx_state == DWC_OTG_L2) {
#ifdef PARTIAL_POWER_DOWN
			if (core_if->hwcfg4.b.power_optimiz) {
				pcgcctl_data_t power = {.d32 = 0 };

				power.d32 = DWC_READ_REG32(core_if->pcgcctl);
				DWC_DEBUGPL(DBG_CIL, "PCGCCTL=%0x\n",
					    power.d32);

				power.b.stoppclk = 0;
				DWC_WRITE_REG32(core_if->pcgcctl, power.d32);

				power.b.pwrclmp = 0;
				DWC_WRITE_REG32(core_if->pcgcctl, power.d32);

				power.b.rstpdwnmodule = 0;
				DWC_WRITE_REG32(core_if->pcgcctl, power.d32);
			}
#endif
			/* Clear the Remote Wakeup Signaling */
			dctl.b.rmtwkupsig = 1;
			DWC_MODIFY_REG32(&core_if->dev_if->
					 dev_global_regs->dctl, dctl.d32, 0);

			DWC_SPINUNLOCK(core_if->lock);
			if (core_if->pcd_cb && core_if->pcd_cb->resume_wakeup) {
				core_if->pcd_cb->resume_wakeup(core_if->pcd_cb->
							       p);
			}
			DWC_SPINLOCK(core_if->lock);
		} else {
			glpmcfg_data_t lpmcfg;
			pcgcctl_data_t pcgcctl = {.d32 = 0 };

			lpmcfg.d32 =
			    DWC_READ_REG32(&core_if->core_global_regs->glpmcfg);
			lpmcfg.b.hird_thres &= (~(1 << 4));
			lpmcfg.b.en_utmi_sleep = 0;

			/* Clear Enbl_L1Gating bit. */
			pcgcctl.b.enbl_sleep_gating = 1;
			DWC_MODIFY_REG32(core_if->pcgcctl, pcgcctl.d32, 0);

			DWC_WRITE_REG32(&core_if->core_global_regs->glpmcfg,
					lpmcfg.d32);
		}
		/** Change to L0 state*/
		core_if->lx_state = DWC_OTG_L0;
	} else {
		if (core_if->lx_state != DWC_OTG_L1) {
			pcgcctl_data_t pcgcctl = {.d32 = 0 };

			/* Restart the Phy Clock */
			pcgcctl.b.stoppclk = 1;
			DWC_MODIFY_REG32(core_if->pcgcctl, pcgcctl.d32, 0);
			udelay(10);

			DWC_TASK_SCHEDULE(core_if->wkp_tasklet);
		} else {
			/** Change to L0 state*/
			core_if->lx_state = DWC_OTG_L0;
		}
	}

	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.wkupintr = 1;
	DWC_WRITE_REG32(&core_if->core_global_regs->gintsts, gintsts.d32);

	return 1;
}

/**
 * This interrupt indicates that the Wakeup Logic has detected a
 * Device disconnect.
 */
static int32_t dwc_otg_handle_pwrdn_disconnect_intr(dwc_otg_core_if_t *core_if)
{
	gpwrdn_data_t gpwrdn = {.d32 = 0 };
	gpwrdn_data_t gpwrdn_temp = {.d32 = 0 };
	gpwrdn_temp.d32 = DWC_READ_REG32(&core_if->core_global_regs->gpwrdn);

	DWC_PRINTF("%s called\n", __FUNCTION__);

	if (!core_if->hibernation_suspend) {
		DWC_PRINTF("Already exited from Hibernation\n");
		return 1;
	}

	/* Switch on the voltage to the core */
	gpwrdn.b.pwrdnswtch = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32, 0);
	dwc_udelay(10);

	/* Reset the core */
	gpwrdn.d32 = 0;
	gpwrdn.b.pwrdnrstn = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32, 0);
	dwc_udelay(10);

	/* Disable power clamps */
	gpwrdn.d32 = 0;
	gpwrdn.b.pwrdnclmp = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32, 0);

	/* Remove reset the core signal */
	gpwrdn.d32 = 0;
	gpwrdn.b.pwrdnrstn = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, 0, gpwrdn.d32);
	dwc_udelay(10);

	/* Disable PMU interrupt */
	gpwrdn.d32 = 0;
	gpwrdn.b.pmuintsel = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32, 0);

	core_if->hibernation_suspend = 0;

	/* Disable PMU */
	gpwrdn.d32 = 0;
	gpwrdn.b.pmuactv = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32, 0);
	dwc_udelay(10);

	if (gpwrdn_temp.b.idsts) {
		core_if->op_state = B_PERIPHERAL;
		dwc_otg_core_init(core_if);
		dwc_otg_enable_global_interrupts(core_if);
		cil_pcd_start(core_if);
	} else {
		core_if->op_state = A_HOST;
		dwc_otg_core_init(core_if);
		dwc_otg_enable_global_interrupts(core_if);
		cil_hcd_start(core_if);
	}

	return 1;
}

/**
 * This interrupt indicates that the Wakeup Logic has detected a
 * remote wakeup sequence.
 */
static int32_t dwc_otg_handle_pwrdn_wakeup_detected_intr(dwc_otg_core_if_t *
							 core_if)
{
	gpwrdn_data_t gpwrdn = {.d32 = 0 };
	DWC_DEBUGPL(DBG_ANY,
		    "++Powerdown Remote Wakeup Detected Interrupt++\n");

	if (!core_if->hibernation_suspend) {
		DWC_PRINTF("Already exited from Hibernation\n");
		return 1;
	}

	gpwrdn.d32 = DWC_READ_REG32(&core_if->core_global_regs->gpwrdn);
	if (gpwrdn.b.idsts) {	/* Device Mode */
		if ((core_if->power_down == 2)
		    && (core_if->hibernation_suspend == 1)) {
			dwc_otg_device_hibernation_restore(core_if, 0, 0);
		}
	} else {
		if ((core_if->power_down == 2)
		    && (core_if->hibernation_suspend == 1)) {
			dwc_otg_host_hibernation_restore(core_if, 1, 0);
		}
	}
	return 1;
}

static int32_t dwc_otg_handle_pwrdn_idsts_change(dwc_otg_device_t *otg_dev)
{
	gpwrdn_data_t gpwrdn = {.d32 = 0 };
	gpwrdn_data_t gpwrdn_temp = {.d32 = 0 };
	dwc_otg_core_if_t *core_if = otg_dev->core_if;

	DWC_DEBUGPL(DBG_ANY, "%s called\n", __FUNCTION__);
	gpwrdn_temp.d32 = DWC_READ_REG32(&core_if->core_global_regs->gpwrdn);
	if (core_if->power_down == 2) {
		if (!core_if->hibernation_suspend) {
			DWC_PRINTF("Already exited from Hibernation\n");
			return 1;
		}
		DWC_DEBUGPL(DBG_ANY,
			    "Exit from hibernation on ID sts change\n");
		/* Switch on the voltage to the core */
		gpwrdn.b.pwrdnswtch = 1;
		DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32,
				 0);
		dwc_udelay(10);

		/* Reset the core */
		gpwrdn.d32 = 0;
		gpwrdn.b.pwrdnrstn = 1;
		DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32,
				 0);
		dwc_udelay(10);

		/* Disable power clamps */
		gpwrdn.d32 = 0;
		gpwrdn.b.pwrdnclmp = 1;
		DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32,
				 0);

		/* Remove reset the core signal */
		gpwrdn.d32 = 0;
		gpwrdn.b.pwrdnrstn = 1;
		DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, 0,
				 gpwrdn.d32);
		dwc_udelay(10);

		/* Disable PMU interrupt */
		gpwrdn.d32 = 0;
		gpwrdn.b.pmuintsel = 1;
		DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32,
				 0);

		/*Indicates that we are exiting from hibernation */
		core_if->hibernation_suspend = 0;

		/* Disable PMU */
		gpwrdn.d32 = 0;
		gpwrdn.b.pmuactv = 1;
		DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32,
				 0);
		dwc_udelay(10);

		gpwrdn.d32 = core_if->gr_backup->gpwrdn_local;
		if (gpwrdn.b.dis_vbus == 1) {
			gpwrdn.d32 = 0;
			gpwrdn.b.dis_vbus = 1;
			DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn,
					 gpwrdn.d32, 0);
		}

		if (gpwrdn_temp.b.idsts) {
			core_if->op_state = B_PERIPHERAL;
			dwc_otg_core_init(core_if);
			dwc_otg_enable_global_interrupts(core_if);
			cil_pcd_start(core_if);
		} else {
			core_if->op_state = A_HOST;
			dwc_otg_core_init(core_if);
			dwc_otg_enable_global_interrupts(core_if);
			cil_hcd_start(core_if);
		}
	}

	if (core_if->adp_enable) {
		uint8_t is_host = 0;
		DWC_SPINUNLOCK(core_if->lock);
		/* Change the core_if's lock to hcd/pcd lock depend on mode? */
#ifndef DWC_HOST_ONLY
		if (gpwrdn_temp.b.idsts)
			core_if->lock = otg_dev->pcd->lock;
#endif
#ifndef DWC_DEVICE_ONLY
		if (!gpwrdn_temp.b.idsts) {
			core_if->lock = otg_dev->hcd->lock;
			is_host = 1;
		}
#endif
		DWC_PRINTF("RESTART ADP\n");
		if (core_if->adp.probe_enabled)
			dwc_otg_adp_probe_stop(core_if);
		if (core_if->adp.sense_enabled)
			dwc_otg_adp_sense_stop(core_if);
		if (core_if->adp.sense_timer_started)
			DWC_TIMER_CANCEL(core_if->adp.sense_timer);
		if (core_if->adp.vbuson_timer_started)
			DWC_TIMER_CANCEL(core_if->adp.vbuson_timer);
		core_if->adp.probe_timer_values[0] = -1;
		core_if->adp.probe_timer_values[1] = -1;
		core_if->adp.sense_timer_started = 0;
		core_if->adp.vbuson_timer_started = 0;
		core_if->adp.probe_counter = 0;
		core_if->adp.gpwrdn = 0;

		/* Disable PMU and restart ADP */
		gpwrdn_temp.d32 = 0;
		gpwrdn_temp.b.pmuactv = 1;
		gpwrdn_temp.b.pmuintsel = 1;
		DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32,
				 0);
		DWC_PRINTF("Check point 1\n");
		dwc_mdelay(110);
		dwc_otg_adp_start(core_if, is_host);
		DWC_SPINLOCK(core_if->lock);
	}

	return 1;
}

static int32_t dwc_otg_handle_pwrdn_session_change(dwc_otg_core_if_t *core_if)
{
	gpwrdn_data_t gpwrdn = {.d32 = 0 };
	int32_t otg_cap_param = core_if->core_params->otg_cap;
	DWC_DEBUGPL(DBG_ANY, "%s called\n", __FUNCTION__);

	gpwrdn.d32 = DWC_READ_REG32(&core_if->core_global_regs->gpwrdn);
	if (core_if->power_down == 2) {
		if (!core_if->hibernation_suspend) {
			DWC_PRINTF("Already exited from Hibernation\n");
			return 1;
		}

		if ((otg_cap_param != DWC_OTG_CAP_PARAM_HNP_SRP_CAPABLE ||
		     otg_cap_param != DWC_OTG_CAP_PARAM_SRP_ONLY_CAPABLE) &&
		    gpwrdn.b.bsessvld == 0) {
			/* Save gpwrdn register for further usage if stschng interrupt */
			core_if->gr_backup->gpwrdn_local =
			    DWC_READ_REG32(&core_if->core_global_regs->gpwrdn);
			/*Exit from ISR and wait for stschng interrupt with bsessvld = 1 */
			return 1;
		}

		/* Switch on the voltage to the core */
		gpwrdn.d32 = 0;
		gpwrdn.b.pwrdnswtch = 1;
		DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32,
				 0);
		dwc_udelay(10);

		/* Reset the core */
		gpwrdn.d32 = 0;
		gpwrdn.b.pwrdnrstn = 1;
		DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32,
				 0);
		dwc_udelay(10);

		/* Disable power clamps */
		gpwrdn.d32 = 0;
		gpwrdn.b.pwrdnclmp = 1;
		DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32,
				 0);

		/* Remove reset the core signal */
		gpwrdn.d32 = 0;
		gpwrdn.b.pwrdnrstn = 1;
		DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, 0,
				 gpwrdn.d32);
		dwc_udelay(10);

		/* Disable PMU interrupt */
		gpwrdn.d32 = 0;
		gpwrdn.b.pmuintsel = 1;
		DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32,
				 0);
		dwc_udelay(10);

		/*Indicates that we are exiting from hibernation */
		core_if->hibernation_suspend = 0;

		/* Disable PMU */
		gpwrdn.d32 = 0;
		gpwrdn.b.pmuactv = 1;
		DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32,
				 0);
		dwc_udelay(10);

		core_if->op_state = B_PERIPHERAL;
		dwc_otg_core_init(core_if);
		dwc_otg_enable_global_interrupts(core_if);
		cil_pcd_start(core_if);

		if (otg_cap_param == DWC_OTG_CAP_PARAM_HNP_SRP_CAPABLE ||
		    otg_cap_param == DWC_OTG_CAP_PARAM_SRP_ONLY_CAPABLE) {
			/*
			 * Initiate SRP after initial ADP probe.
			 */
			dwc_otg_initiate_srp(core_if);
		}
	} else if (core_if->adp_enable) {
		dwc_otg_adp_probe_stop(core_if);
		if (DWC_WORKQ_PENDING(core_if->wq_otg))
			core_if->stop_adpprb = 1;
		/* Disable Power Down Logic */
		gpwrdn.d32 = 0;
		gpwrdn.b.pmuintsel = 1;
		gpwrdn.b.pmuactv = 1;
		DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32,
				 0);

		/*
		 * Initialize the Core for Device mode.
		 */
		core_if->op_state = B_PERIPHERAL;
		cil_pcd_start(core_if);
		dwc_otg_enable_global_interrupts(core_if);
	}

	return 1;
}

/**
 * This interrupt indicates that the Wakeup Logic has detected a
 * status change either on IDDIG or BSessVld.
 */
static uint32_t dwc_otg_handle_pwrdn_stschng_intr(dwc_otg_device_t *otg_dev)
{
	int retval;
	gpwrdn_data_t gpwrdn = {.d32 = 0 };
	gpwrdn_data_t gpwrdn_temp = {.d32 = 0 };
	dwc_otg_core_if_t *core_if = otg_dev->core_if;

	DWC_PRINTF("%s called\n", __FUNCTION__);

	if (core_if->power_down == 2) {
		if (core_if->hibernation_suspend <= 0) {
			DWC_PRINTF("Already exited from Hibernation\n");
			return 1;
		} else
			gpwrdn_temp.d32 = core_if->gr_backup->gpwrdn_local;

	} else {
		gpwrdn_temp.d32 = core_if->adp.gpwrdn;
	}

	gpwrdn.d32 = DWC_READ_REG32(&core_if->core_global_regs->gpwrdn);

	if (gpwrdn.b.idsts ^ gpwrdn_temp.b.idsts) {
		retval = dwc_otg_handle_pwrdn_idsts_change(otg_dev);
	} else if (gpwrdn.b.bsessvld ^ gpwrdn_temp.b.bsessvld) {
		retval = dwc_otg_handle_pwrdn_session_change(core_if);
	}

	return retval;
}

/**
 * This interrupt indicates that the Wakeup Logic has detected a
 * SRP.
 */
static int32_t dwc_otg_handle_pwrdn_srp_intr(dwc_otg_core_if_t *core_if)
{
	gpwrdn_data_t gpwrdn = {.d32 = 0 };

	DWC_PRINTF("%s called\n", __FUNCTION__);

	if (!core_if->hibernation_suspend) {
		DWC_PRINTF("Already exited from Hibernation\n");
		return 1;
	}
#ifdef DWC_DEV_SRPCAP
	if (core_if->pwron_timer_started) {
		core_if->pwron_timer_started = 0;
		DWC_TIMER_CANCEL(core_if->pwron_timer);
	}
#endif

	/* Switch on the voltage to the core */
	gpwrdn.b.pwrdnswtch = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32, 0);
	dwc_udelay(10);

	/* Reset the core */
	gpwrdn.d32 = 0;
	gpwrdn.b.pwrdnrstn = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32, 0);
	dwc_udelay(10);

	/* Disable power clamps */
	gpwrdn.d32 = 0;
	gpwrdn.b.pwrdnclmp = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32, 0);

	/* Remove reset the core signal */
	gpwrdn.d32 = 0;
	gpwrdn.b.pwrdnrstn = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, 0, gpwrdn.d32);
	dwc_udelay(10);

	/* Disable PMU interrupt */
	gpwrdn.d32 = 0;
	gpwrdn.b.pmuintsel = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32, 0);

	/* Indicates that we are exiting from hibernation */
	core_if->hibernation_suspend = 0;

	/* Disable PMU */
	gpwrdn.d32 = 0;
	gpwrdn.b.pmuactv = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32, 0);
	dwc_udelay(10);

	/* Programm Disable VBUS to 0 */
	gpwrdn.d32 = 0;
	gpwrdn.b.dis_vbus = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32, 0);

	/*Initialize the core as Host */
	core_if->op_state = A_HOST;
	dwc_otg_core_init(core_if);
	dwc_otg_enable_global_interrupts(core_if);
	cil_hcd_start(core_if);

	return 1;
}

/** This interrupt indicates that restore command after Hibernation
 * was completed by the core. */
int32_t dwc_otg_handle_restore_done_intr(dwc_otg_core_if_t *core_if)
{
	pcgcctl_data_t pcgcctl;
	DWC_DEBUGPL(DBG_ANY, "++Restore Done Interrupt++\n");

	/* TODO De-assert restore signal. 8.a */
	pcgcctl.d32 = DWC_READ_REG32(core_if->pcgcctl);
	if (pcgcctl.b.restoremode == 1) {
		gintmsk_data_t gintmsk = {.d32 = 0 };
		/*
		 * If restore mode is Remote Wakeup,
		 * unmask Remote Wakeup interrupt.
		 */
		gintmsk.b.wkupintr = 1;
		DWC_MODIFY_REG32(&core_if->core_global_regs->gintmsk,
				 0, gintmsk.d32);
	}

	return 1;
}

/**
 * This interrupt indicates that a device has been disconnected from
 * the root port.
 */
int32_t dwc_otg_handle_disconnect_intr(dwc_otg_core_if_t *core_if)
{
	gintsts_data_t gintsts;

	DWC_DEBUGPL(DBG_ANY, "++Disconnect Detected Interrupt++ (%s) %s\n",
		    (dwc_otg_is_host_mode(core_if) ? "Host" : "Device"),
		    op_state_str(core_if));

/** @todo Consolidate this if statement. */
#ifndef DWC_HOST_ONLY
	if (core_if->op_state == B_HOST) {
		/* If in device mode Disconnect and stop the HCD, then
		 * start the PCD. */
		DWC_SPINUNLOCK(core_if->lock);
		cil_hcd_disconnect(core_if);
		cil_pcd_start(core_if);
		DWC_SPINLOCK(core_if->lock);
		core_if->op_state = B_PERIPHERAL;
	} else if (dwc_otg_is_device_mode(core_if)) {
		gotgctl_data_t gotgctl = {.d32 = 0 };
		gotgctl.d32 =
		    DWC_READ_REG32(&core_if->core_global_regs->gotgctl);
		if (gotgctl.b.hstsethnpen == 1) {
			/* Do nothing, if HNP in process the OTG
			 * interrupt "Host Negotiation Detected"
			 * interrupt will do the mode switch.
			 */
		} else if (gotgctl.b.devhnpen == 0) {
			/* If in device mode Disconnect and stop the HCD, then
			 * start the PCD. */
			DWC_SPINUNLOCK(core_if->lock);
			cil_hcd_disconnect(core_if);
			cil_pcd_start(core_if);
			DWC_SPINLOCK(core_if->lock);
			core_if->op_state = B_PERIPHERAL;
		} else {
			DWC_DEBUGPL(DBG_ANY, "!a_peripheral && !devhnpen\n");
		}
	} else {
		if (core_if->op_state == A_HOST) {
			/* A-Cable still connected but device disconnected. */
			cil_hcd_disconnect(core_if);
			if (core_if->adp_enable) {
				gpwrdn_data_t gpwrdn = {.d32 = 0 };
				cil_hcd_stop(core_if);
				/* Enable Power Down Logic */
				gpwrdn.b.pmuintsel = 1;
				gpwrdn.b.pmuactv = 1;
				DWC_MODIFY_REG32(&core_if->
						 core_global_regs->gpwrdn, 0,
						 gpwrdn.d32);
				dwc_otg_adp_probe_start(core_if);

				/* Power off the core */
				if (core_if->power_down == 2) {
					gpwrdn.d32 = 0;
					gpwrdn.b.pwrdnswtch = 1;
					DWC_MODIFY_REG32
					    (&core_if->core_global_regs->gpwrdn,
					     gpwrdn.d32, 0);
				}
			}
		}
	}
#endif
	/* Change to L3(OFF) state */
	core_if->lx_state = DWC_OTG_L3;

	gintsts.d32 = 0;
	gintsts.b.disconnect = 1;
	DWC_WRITE_REG32(&core_if->core_global_regs->gintsts, gintsts.d32);
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
int32_t dwc_otg_handle_usb_suspend_intr(dwc_otg_core_if_t *core_if)
{
	dsts_data_t dsts;
	gintsts_data_t gintsts;
	dcfg_data_t dcfg;

	DWC_DEBUGPL(DBG_ANY, "USB SUSPEND\n");

	if ((core_if->otg_ver == 1) && (core_if->op_state == A_PERIPHERAL))
		dwc_mdelay(200);	/* vahrama - WA - see BU's mail */

	if (dwc_otg_is_device_mode(core_if)) {
		/* Check the Device status register to determine if the Suspend
		 * state is active. */
		dsts.d32 =
		    DWC_READ_REG32(&core_if->dev_if->dev_global_regs->dsts);
		DWC_DEBUGPL(DBG_PCD, "DSTS=0x%0x\n", dsts.d32);
		DWC_DEBUGPL(DBG_PCD, "DSTS.Suspend Status=%d "
			    "HWCFG4.power Optimize=%d\n",
			    dsts.b.suspsts, core_if->hwcfg4.b.power_optimiz);

#ifdef PARTIAL_POWER_DOWN
/** @todo Add a module parameter for power management. */

		if (dsts.b.suspsts && core_if->hwcfg4.b.power_optimiz) {
			pcgcctl_data_t power = {.d32 = 0 };
			DWC_DEBUGPL(DBG_CIL, "suspend\n");

			power.b.pwrclmp = 1;
			DWC_WRITE_REG32(core_if->pcgcctl, power.d32);

			power.b.rstpdwnmodule = 1;
			DWC_MODIFY_REG32(core_if->pcgcctl, 0, power.d32);

			power.b.stoppclk = 1;
			DWC_MODIFY_REG32(core_if->pcgcctl, 0, power.d32);

		} else {
			DWC_DEBUGPL(DBG_ANY, "disconnect?\n");
		}
#endif
		/* PCD callback for suspend */
		DWC_SPINUNLOCK(core_if->lock);
		cil_pcd_suspend(core_if);
		DWC_SPINLOCK(core_if->lock);

		if (core_if->power_down == 2) {
			dcfg.d32 =
			    DWC_READ_REG32(&core_if->dev_if->dev_global_regs->
					   dcfg);
			DWC_DEBUGPL(DBG_ANY, "lx_state = %08x\n",
				    core_if->lx_state);
			DWC_DEBUGPL(DBG_ANY, " device address = %08d\n",
				    dcfg.b.devaddr);

			if (core_if->lx_state != DWC_OTG_L3 && dcfg.b.devaddr) {
				pcgcctl_data_t pcgcctl = {.d32 = 0 };
				gpwrdn_data_t gpwrdn = {.d32 = 0 };
				gusbcfg_data_t gusbcfg = {.d32 = 0 };

				/* Change to L2(suspend) state */
				core_if->lx_state = DWC_OTG_L2;

				/* Clear interrupt in gintsts */
				gintsts.d32 = 0;
				gintsts.b.usbsuspend = 1;
				DWC_WRITE_REG32(&core_if->
						core_global_regs->gintsts,
						gintsts.d32);
				DWC_PRINTF("Start of hibernation completed\n");
				dwc_otg_save_global_regs(core_if);
				dwc_otg_save_dev_regs(core_if);

				gusbcfg.d32 =
				    DWC_READ_REG32(&core_if->
						   core_global_regs->gusbcfg);
				if (gusbcfg.b.ulpi_utmi_sel == 1) {
					/* ULPI interface */
					/* Suspend the Phy Clock */
					pcgcctl.d32 = 0;
					pcgcctl.b.stoppclk = 1;
					DWC_MODIFY_REG32(core_if->pcgcctl, 0,
							 pcgcctl.d32);
					dwc_udelay(10);
					gpwrdn.b.pmuactv = 1;
					DWC_MODIFY_REG32
					    (&core_if->core_global_regs->gpwrdn,
					     0, gpwrdn.d32);
				} else {
					/* UTMI+ Interface */
					gpwrdn.b.pmuactv = 1;
					DWC_MODIFY_REG32
					    (&core_if->core_global_regs->gpwrdn,
					     0, gpwrdn.d32);
					dwc_udelay(10);
					pcgcctl.b.stoppclk = 1;
					DWC_MODIFY_REG32(core_if->pcgcctl, 0,
							 pcgcctl.d32);
					dwc_udelay(10);
				}

				/* Set flag to indicate that we are in hibernation */
				core_if->hibernation_suspend = 1;
				/* Enable interrupts from wake up logic */
				gpwrdn.d32 = 0;
				gpwrdn.b.pmuintsel = 1;
				DWC_MODIFY_REG32(&core_if->
						 core_global_regs->gpwrdn, 0,
						 gpwrdn.d32);
				dwc_udelay(10);

				/* Unmask device mode interrupts in GPWRDN */
				gpwrdn.d32 = 0;
				gpwrdn.b.rst_det_msk = 1;
				gpwrdn.b.lnstchng_msk = 1;
				gpwrdn.b.sts_chngint_msk = 1;
				DWC_MODIFY_REG32(&core_if->
						 core_global_regs->gpwrdn, 0,
						 gpwrdn.d32);
				dwc_udelay(10);

				/* Enable Power Down Clamp */
				gpwrdn.d32 = 0;
				gpwrdn.b.pwrdnclmp = 1;
				DWC_MODIFY_REG32(&core_if->
						 core_global_regs->gpwrdn, 0,
						 gpwrdn.d32);
				dwc_udelay(10);

				/* Switch off VDD */
				gpwrdn.d32 = 0;
				gpwrdn.b.pwrdnswtch = 1;
				DWC_MODIFY_REG32(&core_if->
						 core_global_regs->gpwrdn, 0,
						 gpwrdn.d32);

				/* Save gpwrdn register for further usage if stschng interrupt */
				core_if->gr_backup->gpwrdn_local =
				    DWC_READ_REG32(&core_if->core_global_regs->
						   gpwrdn);
				DWC_PRINTF("Hibernation completed\n");

				return 1;
			}
		} else if (core_if->power_down == 3) {
			pcgcctl_data_t pcgcctl = {.d32 = 0 };
			dcfg.d32 =
			    DWC_READ_REG32(&core_if->dev_if->dev_global_regs->
					   dcfg);
			DWC_DEBUGPL(DBG_ANY, "lx_state = %08x\n",
				    core_if->lx_state);
			DWC_DEBUGPL(DBG_ANY, " device address = %08d\n",
				    dcfg.b.devaddr);

			if (core_if->lx_state != DWC_OTG_L3 && dcfg.b.devaddr) {
				DWC_DEBUGPL(DBG_ANY,
					    "Start entering to extended hibernation\n");
				core_if->xhib = 1;

				/* Clear interrupt in gintsts */
				gintsts.d32 = 0;
				gintsts.b.usbsuspend = 1;
				DWC_WRITE_REG32(&core_if->
						core_global_regs->gintsts,
						gintsts.d32);

				dwc_otg_save_global_regs(core_if);
				dwc_otg_save_dev_regs(core_if);

				/* Wait for 10 PHY clocks */
				dwc_udelay(10);

				/* Program GPIO register while entering to xHib */
				DWC_WRITE_REG32(&core_if->core_global_regs->
						ggpio, 0x1);

				pcgcctl.b.enbl_extnd_hiber = 1;
				DWC_MODIFY_REG32(core_if->pcgcctl, 0,
						 pcgcctl.d32);
				DWC_MODIFY_REG32(core_if->pcgcctl, 0,
						 pcgcctl.d32);

				pcgcctl.d32 = 0;
				pcgcctl.b.extnd_hiber_pwrclmp = 1;
				DWC_MODIFY_REG32(core_if->pcgcctl, 0,
						 pcgcctl.d32);

				pcgcctl.d32 = 0;
				pcgcctl.b.extnd_hiber_switch = 1;
				core_if->gr_backup->xhib_gpwrdn =
				    DWC_READ_REG32(&core_if->core_global_regs->
						   gpwrdn);
				core_if->gr_backup->xhib_pcgcctl =
				    DWC_READ_REG32(core_if->pcgcctl) | pcgcctl.
				    d32;
				DWC_MODIFY_REG32(core_if->pcgcctl, 0,
						 pcgcctl.d32);

				DWC_DEBUGPL(DBG_ANY,
					    "Finished entering to extended hibernation\n");

				return 1;
			}
		}
		if ((core_if->otg_ver == 1)
		    && (core_if->core_params->otg_cap ==
			DWC_OTG_CAP_PARAM_HNP_SRP_CAPABLE)) {
			gotgctl_data_t gotgctl = {.d32 = 0 };
			gotgctl.d32 =
			    DWC_READ_REG32(&core_if->core_global_regs->gotgctl);
			if (gotgctl.b.devhnpen && core_if->otg_ver == 1) {
				gotgctl_data_t gotgctl = {.d32 = 0 };
				dwc_mdelay(5);
				/**@todo Is the gotgctl.devhnpen cleared
				 * by a USB Reset? */
				gotgctl.b.devhnpen = 1;
				gotgctl.b.hnpreq = 1;
				DWC_WRITE_REG32(&core_if->core_global_regs->
						gotgctl, gotgctl.d32);
			}
		}
	} else {
		if (core_if->op_state == A_PERIPHERAL) {
			DWC_DEBUGPL(DBG_ANY, "a_peripheral->a_host\n");
			/* Clear the a_peripheral flag, back to a_host. */
			DWC_SPINUNLOCK(core_if->lock);
			cil_pcd_stop(core_if);
			cil_hcd_start(core_if);
			DWC_SPINLOCK(core_if->lock);
			core_if->op_state = A_HOST;
		}
	}

	/* Change to L2(suspend) state */
	core_if->lx_state = DWC_OTG_L2;

	/* Clear interrupt */
	gintsts.d32 = 0;
	gintsts.b.usbsuspend = 1;
	DWC_WRITE_REG32(&core_if->core_global_regs->gintsts, gintsts.d32);

	return 1;
}

static int32_t dwc_otg_handle_xhib_exit_intr(dwc_otg_core_if_t *core_if)
{
	gpwrdn_data_t gpwrdn = {.d32 = 0 };
	pcgcctl_data_t pcgcctl = {.d32 = 0 };
	gahbcfg_data_t gahbcfg = {.d32 = 0 };

	dwc_udelay(10);

	/* Program GPIO register while entering to xHib */
	DWC_WRITE_REG32(&core_if->core_global_regs->ggpio, 0x0);

	pcgcctl.d32 = core_if->gr_backup->xhib_pcgcctl;
	pcgcctl.b.extnd_hiber_pwrclmp = 0;
	DWC_WRITE_REG32(core_if->pcgcctl, pcgcctl.d32);
	dwc_udelay(10);

	gpwrdn.d32 = core_if->gr_backup->xhib_gpwrdn;
	gpwrdn.b.restore = 1;
	DWC_WRITE_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32);
	dwc_udelay(10);

	restore_lpm_i2c_regs(core_if);

	pcgcctl.d32 = core_if->gr_backup->pcgcctl_local & (0x3FFFF << 14);
	pcgcctl.b.max_xcvrselect = 1;
	pcgcctl.b.ess_reg_restored = 0;
	pcgcctl.b.extnd_hiber_switch = 0;
	pcgcctl.b.extnd_hiber_pwrclmp = 0;
	pcgcctl.b.enbl_extnd_hiber = 1;
	DWC_WRITE_REG32(core_if->pcgcctl, pcgcctl.d32);

	gahbcfg.d32 = core_if->gr_backup->gahbcfg_local;
	gahbcfg.b.glblintrmsk = 1;
	DWC_WRITE_REG32(&core_if->core_global_regs->gahbcfg, gahbcfg.d32);

	DWC_WRITE_REG32(&core_if->core_global_regs->gintsts, 0xFFFFFFFF);
	DWC_WRITE_REG32(&core_if->core_global_regs->gintmsk, 0x1 << 16);

	DWC_WRITE_REG32(&core_if->core_global_regs->gusbcfg,
			core_if->gr_backup->gusbcfg_local);
	DWC_WRITE_REG32(&core_if->dev_if->dev_global_regs->dcfg,
			core_if->dr_backup->dcfg);

	pcgcctl.d32 = 0;
	pcgcctl.d32 = core_if->gr_backup->pcgcctl_local & (0x3FFFF << 14);
	pcgcctl.b.max_xcvrselect = 1;
	pcgcctl.d32 |= 0x608;
	DWC_WRITE_REG32(core_if->pcgcctl, pcgcctl.d32);
	dwc_udelay(10);

	pcgcctl.d32 = 0;
	pcgcctl.d32 = core_if->gr_backup->pcgcctl_local & (0x3FFFF << 14);
	pcgcctl.b.max_xcvrselect = 1;
	pcgcctl.b.ess_reg_restored = 1;
	pcgcctl.b.enbl_extnd_hiber = 1;
	pcgcctl.b.rstpdwnmodule = 1;
	pcgcctl.b.restoremode = 1;
	DWC_WRITE_REG32(core_if->pcgcctl, pcgcctl.d32);

	DWC_DEBUGPL(DBG_ANY, "%s called\n", __FUNCTION__);

	return 1;
}

#ifdef CONFIG_USB_DWC_OTG_LPM
/**
 * This function hadles LPM transaction received interrupt.
 */
static int32_t dwc_otg_handle_lpm_intr(dwc_otg_core_if_t *core_if)
{
	glpmcfg_data_t lpmcfg;
	gintsts_data_t gintsts;

	if (!core_if->core_params->lpm_enable) {
		DWC_PRINTF("Unexpected LPM interrupt\n");
	}

	lpmcfg.d32 = DWC_READ_REG32(&core_if->core_global_regs->glpmcfg);
	DWC_PRINTF("LPM config register = 0x%08x\n", lpmcfg.d32);

	if (dwc_otg_is_host_mode(core_if)) {
		cil_hcd_sleep(core_if);
	} else {

		pcgcctl_data_t pcgcctl = {.d32 = 0 };

		lpmcfg.b.hird_thres |= (1 << 4);
		lpmcfg.b.en_utmi_sleep = 1;

		pcgcctl.b.enbl_sleep_gating = 1;
		DWC_MODIFY_REG32(core_if->pcgcctl, 0, pcgcctl.d32);

		if (dwc_otg_get_param_besl_enable(core_if)) {
			lpmcfg.b.en_besl = 1;
		}

		DWC_WRITE_REG32(&core_if->core_global_regs->glpmcfg,
				lpmcfg.d32);
	}

	/* Examine prt_sleep_sts after TL1TokenTetry period max (10 us) */
	dwc_udelay(10);
	lpmcfg.d32 = DWC_READ_REG32(&core_if->core_global_regs->glpmcfg);
	if (lpmcfg.b.prt_sleep_sts) {
		/* Save the current state */
		core_if->lx_state = DWC_OTG_L1;
	}

	/* Clear interrupt  */
	gintsts.d32 = 0;
	gintsts.b.lpmtranrcvd = 1;
	DWC_WRITE_REG32(&core_if->core_global_regs->gintsts, gintsts.d32);
	return 1;
}
#endif /* CONFIG_USB_DWC_OTG_LPM */

/**
 * This function returns the Core Interrupt register.
 */
static inline uint32_t dwc_otg_read_common_intr(dwc_otg_core_if_t *core_if)
{
	uint32_t retval;

	gahbcfg_data_t gahbcfg = {.d32 = 0 };
	gintsts_data_t gintsts;
	gintmsk_data_t gintmsk;
	gintmsk_data_t gintmsk_common = {.d32 = 0 };
	gintmsk_common.b.wkupintr = 1;
	gintmsk_common.b.sessreqintr = 1;
	gintmsk_common.b.conidstschng = 1;
	gintmsk_common.b.otgintr = 1;
	gintmsk_common.b.modemismatch = 1;
	gintmsk_common.b.disconnect = 1;
	gintmsk_common.b.usbsuspend = 1;
#ifdef CONFIG_USB_DWC_OTG_LPM
	gintmsk_common.b.lpmtranrcvd = 1;
#endif
	gintmsk_common.b.restoredone = 1;
	/** @todo: The port interrupt occurs while in device
	 * mode. Added code to CIL to clear the interrupt for now!
	 */
	gintmsk_common.b.portintr = 1;

	gintsts.d32 = DWC_READ_REG32(&core_if->core_global_regs->gintsts);
	gintmsk.d32 = DWC_READ_REG32(&core_if->core_global_regs->gintmsk);
	gahbcfg.d32 = DWC_READ_REG32(&core_if->core_global_regs->gahbcfg);

#ifdef DEBUG
	/* if any common interrupts set */
	if (gintsts.d32 & gintmsk_common.d32) {
		DWC_DEBUGPL(DBG_ANY, "gintsts=%08x  gintmsk=%08x\n",
			    gintsts.d32, gintmsk.d32);
	}
#endif
	if (gahbcfg.b.glblintrmsk) {
		retval = (gintsts.d32 & gintmsk.d32) & gintmsk_common.d32;
		return retval;
	} else {
		return 0;
	}
}

/* MACRO for clearing interupt bits in GPWRDN register */
#define CLEAR_GPWRDN_INTR(__core_if, __intr) \
do { \
		gpwrdn_data_t gpwrdn = {.d32 = 0}; \
		gpwrdn.b.__intr = 1; \
		DWC_MODIFY_REG32(&__core_if->core_global_regs->gpwrdn, \
		0, gpwrdn.d32); \
} while (0)

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
 * - LPM Transaction Received Interrupt
 * - ADP Transaction Received Interrupt
 *
 */
int32_t dwc_otg_handle_common_intr(void *dev)
{
	int retval = 0;
	gintsts_data_t gintsts;
	gpwrdn_data_t gpwrdn = {.d32 = 0 };
	dwc_otg_device_t *otg_dev = dev;
	dwc_otg_core_if_t *core_if = otg_dev->core_if;
	gpwrdn.d32 = DWC_READ_REG32(&core_if->core_global_regs->gpwrdn);

	if (dwc_otg_check_haps_status(core_if) == -1) {
		DWC_WARN("HAPS is disconnected");
		return retval;
	}

	if (dwc_otg_is_device_mode(core_if))
		core_if->frame_num = dwc_otg_get_frame_number(core_if);

	if (core_if->lock)
		DWC_SPINLOCK(core_if->lock);

	if (core_if->power_down == 3 && core_if->xhib == 1) {
		DWC_DEBUGPL(DBG_ANY, "Exiting from xHIB state\n");
		retval |= dwc_otg_handle_xhib_exit_intr(core_if);
		core_if->xhib = 2;
		if (core_if->lock)
			DWC_SPINUNLOCK(core_if->lock);

		return retval;
	}

	if (core_if->hibernation_suspend <= 0) {
		gintsts.d32 = dwc_otg_read_common_intr(core_if);

		if (gintsts.b.modemismatch) {
			retval |= dwc_otg_handle_mode_mismatch_intr(core_if);
		}
		if (gintsts.b.otgintr) {
			retval |= dwc_otg_handle_otg_intr(core_if);
		}
		if (gintsts.b.conidstschng) {
			retval |=
			    dwc_otg_handle_conn_id_status_change_intr(core_if);
		}
		if (gintsts.b.disconnect) {
			retval |= dwc_otg_handle_disconnect_intr(core_if);
		}
		if (gintsts.b.sessreqintr) {
			retval |= dwc_otg_handle_session_req_intr(core_if);
		}
		if (gintsts.b.wkupintr) {
			retval |= dwc_otg_handle_wakeup_detected_intr(core_if);
		}
		if (gintsts.b.usbsuspend) {
			retval |= dwc_otg_handle_usb_suspend_intr(core_if);
		}
#ifdef CONFIG_USB_DWC_OTG_LPM
		if (gintsts.b.lpmtranrcvd) {
			retval |= dwc_otg_handle_lpm_intr(core_if);
		}
#endif
		if (gintsts.b.restoredone) {
			gintsts.d32 = 0;
			if (core_if->power_down == 2)
				core_if->hibernation_suspend = -1;
			else if (core_if->power_down == 3 && core_if->xhib == 2) {
				gpwrdn_data_t gpwrdn = {.d32 = 0 };
				pcgcctl_data_t pcgcctl = {.d32 = 0 };
				dctl_data_t dctl = {.d32 = 0 };

				DWC_WRITE_REG32(&core_if->
						core_global_regs->gintsts,
						0xFFFFFFFF);

				DWC_DEBUGPL(DBG_ANY,
					    "RESTORE DONE generated\n");

				gpwrdn.b.restore = 1;
				DWC_MODIFY_REG32(&core_if->core_global_regs->
						 gpwrdn, gpwrdn.d32, 0);
				dwc_udelay(10);

				pcgcctl.b.rstpdwnmodule = 1;
				DWC_MODIFY_REG32(core_if->pcgcctl, pcgcctl.d32,
						 0);

				DWC_WRITE_REG32(&core_if->core_global_regs->
						gusbcfg,
						core_if->gr_backup->
						gusbcfg_local);
				DWC_WRITE_REG32(&core_if->dev_if->
						dev_global_regs->dcfg,
						core_if->dr_backup->dcfg);
				DWC_WRITE_REG32(&core_if->dev_if->
						dev_global_regs->dctl,
						core_if->dr_backup->dctl);
				dwc_udelay(50);

				dctl.b.pwronprgdone = 1;
				DWC_MODIFY_REG32(&core_if->dev_if->
						 dev_global_regs->dctl, 0,
						 dctl.d32);
				dwc_udelay(10);

				dwc_otg_restore_global_regs(core_if);
				dwc_otg_restore_dev_regs(core_if, 0);

				dctl.d32 = 0;
				dctl.b.pwronprgdone = 1;
				DWC_MODIFY_REG32(&core_if->dev_if->
						 dev_global_regs->dctl,
						 dctl.d32, 0);
				dwc_udelay(10);

				pcgcctl.d32 = 0;
				pcgcctl.b.enbl_extnd_hiber = 1;
				DWC_MODIFY_REG32(core_if->pcgcctl, pcgcctl.d32,
						 0);

				/* The core will be in ON STATE */
				core_if->lx_state = DWC_OTG_L0;
				core_if->xhib = 0;

				DWC_SPINUNLOCK(core_if->lock);
				if (core_if->pcd_cb
				    && core_if->pcd_cb->resume_wakeup) {
					core_if->pcd_cb->resume_wakeup(core_if->
								       pcd_cb->
								       p);
				}
				DWC_SPINLOCK(core_if->lock);

			}

			gintsts.b.restoredone = 1;
			DWC_WRITE_REG32(&core_if->core_global_regs->gintsts,
					gintsts.d32);
			DWC_PRINTF(" --Restore done interrupt received-- \n");
			retval |= 1;
		}
		if (gintsts.b.portintr && dwc_otg_is_device_mode(core_if)) {
			/* The port interrupt occurs while in device mode with HPRT0
			 * Port Enable/Disable.
			 */
			gintsts.d32 = 0;
			gintsts.b.portintr = 1;
			DWC_WRITE_REG32(&core_if->core_global_regs->gintsts,
					gintsts.d32);
			retval |= 1;

		}
	} else {
		DWC_DEBUGPL(DBG_ANY, "gpwrdn=%08x\n", gpwrdn.d32);

		if (gpwrdn.b.disconn_det && gpwrdn.b.disconn_det_msk) {
			CLEAR_GPWRDN_INTR(core_if, disconn_det);
			if (gpwrdn.b.linestate == 0) {
				dwc_otg_handle_pwrdn_disconnect_intr(core_if);
			} else {
				DWC_PRINTF
				    ("Disconnect detected while linestate is not 0\n");
			}

			retval |= 1;
		}
		if (gpwrdn.b.lnstschng && gpwrdn.b.lnstchng_msk) {
			CLEAR_GPWRDN_INTR(core_if, lnstschng);
			/* remote wakeup from hibernation */
			if (gpwrdn.b.linestate == 2 || gpwrdn.b.linestate == 1) {
				dwc_otg_handle_pwrdn_wakeup_detected_intr
				    (core_if);
			} else {
				DWC_PRINTF("gpwrdn.linestate = %d\n",
					   gpwrdn.b.linestate);
			}
			retval |= 1;
		}
		if (gpwrdn.b.rst_det && gpwrdn.b.rst_det_msk) {
			CLEAR_GPWRDN_INTR(core_if, rst_det);
			if (gpwrdn.b.linestate == 0) {
				DWC_PRINTF("Reset detected\n");
				retval |=
				    dwc_otg_device_hibernation_restore(core_if,
								       0, 1);
			}
		}
		if (gpwrdn.b.srp_det && gpwrdn.b.srp_det_msk) {
			CLEAR_GPWRDN_INTR(core_if, srp_det);
			dwc_otg_handle_pwrdn_srp_intr(core_if);
			retval |= 1;
		}
	}
	/* Handle ADP interrupt here */
	if (gpwrdn.b.adp_int) {
		DWC_PRINTF("ADP interrupt\n");
		CLEAR_GPWRDN_INTR(core_if, adp_int);
		dwc_otg_adp_handle_intr(core_if);
		retval |= 1;
	}
	if (gpwrdn.b.sts_chngint && gpwrdn.b.sts_chngint_msk) {
		DWC_PRINTF("STS CHNG interrupt asserted\n");
		CLEAR_GPWRDN_INTR(core_if, sts_chngint);
		dwc_otg_handle_pwrdn_stschng_intr(otg_dev);

		retval |= 1;
	}
	if (core_if->lock)
		DWC_SPINUNLOCK(core_if->lock);

	return retval;
}
