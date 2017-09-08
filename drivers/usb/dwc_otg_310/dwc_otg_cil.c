/* ==========================================================================
 * $File: //dwh/usb_iip/dev/software/otg/linux/drivers/dwc_otg_cil.c $
 * $Revision: #198 $
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
 * The CIL manages the memory map for the core so that the HCD and PCD
 * don't have to do this separately. It also handles basic tasks like
 * reading/writing the registers and data FIFOs in the controller.
 * Some of the data access functions provide encapsulation of several
 * operations required to perform a task, such as writing multiple
 * registers to start a transfer. Finally, the CIL performs basic
 * services that are not specific to either the host or device modes
 * of operation. These services include management of the OTG Host
 * Negotiation Protocol (HNP) and Session Request Protocol (SRP). A
 * Diagnostic API is also provided to allow testing of the controller
 * hardware.
 *
 * The Core Interface Layer has the following requirements:
 * - Provides basic controller operations.
 * - Minimal use of OS services.
 * - The OS services used will be abstracted by using inline functions
 *	 or macros.
 *
 */

#include "common_port/dwc_os.h"
#include "dwc_otg_regs.h"
#include "dwc_otg_cil.h"
#include "dwc_otg_driver.h"
#include "usbdev_rk.h"
#include "dwc_otg_hcd.h"

static int dwc_otg_setup_params(dwc_otg_core_if_t *core_if);

/**
 * This function is called to initialize the DWC_otg CSR data
 * structures. The register addresses in the device and host
 * structures are initialized from the base address supplied by the
 * caller. The calling function must make the OS calls to get the
 * base address of the DWC_otg controller registers. The core_params
 * argument holds the parameters that specify how the core should be
 * configured.
 *
 * @param reg_base_addr Base address of DWC_otg core registers
 *
 */
dwc_otg_core_if_t *dwc_otg_cil_init(const uint32_t *reg_base_addr)
{
	dwc_otg_core_if_t *core_if = 0;
	dwc_otg_dev_if_t *dev_if = 0;
	dwc_otg_host_if_t *host_if = 0;
	uint8_t *reg_base = (uint8_t *) reg_base_addr;
	int i = 0;

	DWC_DEBUGPL(DBG_CILV, "%s(%p)\n", __func__, reg_base_addr);

	core_if = DWC_ALLOC(sizeof(dwc_otg_core_if_t));

	if (core_if == NULL) {
		DWC_DEBUGPL(DBG_CIL,
			    "Allocation of dwc_otg_core_if_t failed\n");
		return 0;
	}
	core_if->core_global_regs = (dwc_otg_core_global_regs_t *) reg_base;

	/*
	 * Allocate the Device Mode structures.
	 */
	dev_if = DWC_ALLOC(sizeof(dwc_otg_dev_if_t));

	if (dev_if == NULL) {
		DWC_DEBUGPL(DBG_CIL, "Allocation of dwc_otg_dev_if_t failed\n");
		DWC_FREE(core_if);
		return 0;
	}

	dev_if->dev_global_regs =
	    (dwc_otg_device_global_regs_t *) (reg_base +
					      DWC_DEV_GLOBAL_REG_OFFSET);

	for (i = 0; i < MAX_EPS_CHANNELS; i++) {
		dev_if->in_ep_regs[i] = (dwc_otg_dev_in_ep_regs_t *)
		    (reg_base + DWC_DEV_IN_EP_REG_OFFSET +
		     (i * DWC_EP_REG_OFFSET));

		dev_if->out_ep_regs[i] = (dwc_otg_dev_out_ep_regs_t *)
		    (reg_base + DWC_DEV_OUT_EP_REG_OFFSET +
		     (i * DWC_EP_REG_OFFSET));
		DWC_DEBUGPL(DBG_CILV, "in_ep_regs[%d]->diepctl=%p\n",
			    i, &dev_if->in_ep_regs[i]->diepctl);
		DWC_DEBUGPL(DBG_CILV, "out_ep_regs[%d]->doepctl=%p\n",
			    i, &dev_if->out_ep_regs[i]->doepctl);
	}

	dev_if->speed = 0;	/* unknown */

	core_if->dev_if = dev_if;

	/*
	 * Allocate the Host Mode structures.
	 */
	host_if = DWC_ALLOC(sizeof(dwc_otg_host_if_t));

	if (host_if == NULL) {
		DWC_DEBUGPL(DBG_CIL,
			    "Allocation of dwc_otg_host_if_t failed\n");
		DWC_FREE(dev_if);
		DWC_FREE(core_if);
		return 0;
	}

	host_if->host_global_regs = (dwc_otg_host_global_regs_t *)
	    (reg_base + DWC_OTG_HOST_GLOBAL_REG_OFFSET);

	host_if->hprt0 =
	    (uint32_t *) (reg_base + DWC_OTG_HOST_PORT_REGS_OFFSET);

	for (i = 0; i < MAX_EPS_CHANNELS; i++) {
		host_if->hc_regs[i] = (dwc_otg_hc_regs_t *)
		    (reg_base + DWC_OTG_HOST_CHAN_REGS_OFFSET +
		     (i * DWC_OTG_CHAN_REGS_OFFSET));
		DWC_DEBUGPL(DBG_CILV, "hc_reg[%d]->hcchar=%p\n",
			    i, &host_if->hc_regs[i]->hcchar);
	}

	host_if->num_host_channels = MAX_EPS_CHANNELS;
	core_if->host_if = host_if;

	for (i = 0; i < MAX_EPS_CHANNELS; i++) {
		core_if->data_fifo[i] =
		    (uint32_t *) (reg_base + DWC_OTG_DATA_FIFO_OFFSET +
				  (i * DWC_OTG_DATA_FIFO_SIZE));
		DWC_DEBUGPL(DBG_CILV, "data_fifo[%d]=0x%08lx\n",
			    i, (unsigned long)core_if->data_fifo[i]);
	}

	core_if->pcgcctl = (uint32_t *) (reg_base + DWC_OTG_PCGCCTL_OFFSET);

	/* Initiate lx_state to L3 disconnected state */
	core_if->lx_state = DWC_OTG_L3;
	/*
	 * Store the contents of the hardware configuration registers here for
	 * easy access later.
	 */
	core_if->hwcfg1.d32 =
	    DWC_READ_REG32(&core_if->core_global_regs->ghwcfg1);
	core_if->hwcfg2.d32 =
	    DWC_READ_REG32(&core_if->core_global_regs->ghwcfg2);
	core_if->hwcfg3.d32 =
	    DWC_READ_REG32(&core_if->core_global_regs->ghwcfg3);
	core_if->hwcfg4.d32 =
	    DWC_READ_REG32(&core_if->core_global_regs->ghwcfg4);

	/* do not get HPTXFSIZ here, it's unused.
	 * set global_regs->hptxfsiz in dwc_otg_core_host_init.
	 * for 3.10a version, host20 FIFO can't be configed,
	 * because host20 hwcfg2.b.dynamic_fifo = 0.
	 */
#if 0
	/* Force host mode to get HPTXFSIZ exact power on value */
	{
		gusbcfg_data_t gusbcfg = {.d32 = 0 };
		gusbcfg.d32 =
		    DWC_READ_REG32(&core_if->core_global_regs->gusbcfg);
		gusbcfg.b.force_host_mode = 1;
		DWC_WRITE_REG32(&core_if->core_global_regs->gusbcfg,
				gusbcfg.d32);
		dwc_mdelay(100);
		core_if->hptxfsiz.d32 =
		    DWC_READ_REG32(&core_if->core_global_regs->hptxfsiz);
		gusbcfg.d32 =
		    DWC_READ_REG32(&core_if->core_global_regs->gusbcfg);
		gusbcfg.b.force_host_mode = 0;
		DWC_WRITE_REG32(&core_if->core_global_regs->gusbcfg,
				gusbcfg.d32);
		dwc_mdelay(100);
	}
#endif

	DWC_DEBUGPL(DBG_CILV, "hwcfg1=%08x\n", core_if->hwcfg1.d32);
	DWC_DEBUGPL(DBG_CILV, "hwcfg2=%08x\n", core_if->hwcfg2.d32);
	DWC_DEBUGPL(DBG_CILV, "hwcfg3=%08x\n", core_if->hwcfg3.d32);
	DWC_DEBUGPL(DBG_CILV, "hwcfg4=%08x\n", core_if->hwcfg4.d32);

	core_if->hcfg.d32 =
	    DWC_READ_REG32(&core_if->host_if->host_global_regs->hcfg);
	core_if->dcfg.d32 =
	    DWC_READ_REG32(&core_if->dev_if->dev_global_regs->dcfg);

	DWC_DEBUGPL(DBG_CILV, "hcfg=%08x\n", core_if->hcfg.d32);
	DWC_DEBUGPL(DBG_CILV, "dcfg=%08x\n", core_if->dcfg.d32);

	DWC_DEBUGPL(DBG_CILV, "op_mode=%0x\n", core_if->hwcfg2.b.op_mode);
	DWC_DEBUGPL(DBG_CILV, "arch=%0x\n", core_if->hwcfg2.b.architecture);
	DWC_DEBUGPL(DBG_CILV, "num_dev_ep=%d\n", core_if->hwcfg2.b.num_dev_ep);
	DWC_DEBUGPL(DBG_CILV, "num_host_chan=%d\n",
		    core_if->hwcfg2.b.num_host_chan);
	DWC_DEBUGPL(DBG_CILV, "nonperio_tx_q_depth=0x%0x\n",
		    core_if->hwcfg2.b.nonperio_tx_q_depth);
	DWC_DEBUGPL(DBG_CILV, "host_perio_tx_q_depth=0x%0x\n",
		    core_if->hwcfg2.b.host_perio_tx_q_depth);
	DWC_DEBUGPL(DBG_CILV, "dev_token_q_depth=0x%0x\n",
		    core_if->hwcfg2.b.dev_token_q_depth);

	DWC_DEBUGPL(DBG_CILV, "Total FIFO SZ=%d\n",
		    core_if->hwcfg3.b.dfifo_depth);
	DWC_DEBUGPL(DBG_CILV, "xfer_size_cntr_width=%0x\n",
		    core_if->hwcfg3.b.xfer_size_cntr_width);

	/*
	 * Set the SRP sucess bit for FS-I2c
	 */
	core_if->srp_success = 0;
	core_if->srp_timer_started = 0;

	/*
	 * Create new workqueue and init works
	 */
	core_if->wq_otg = DWC_WORKQ_ALLOC("dwc_otg");
	if (core_if->wq_otg == 0) {
		DWC_WARN("DWC_WORKQ_ALLOC failed\n");
		DWC_FREE(host_if);
		DWC_FREE(dev_if);
		DWC_FREE(core_if);
		return 0;
	}

	core_if->snpsid = DWC_READ_REG32(&core_if->core_global_regs->gsnpsid);
	DWC_PRINTF("%p\n", &core_if->core_global_regs->gsnpsid);
	DWC_PRINTF("Core Release: %x.%x%x%x\n",
		   (core_if->snpsid >> 12 & 0xF),
		   (core_if->snpsid >> 8 & 0xF),
		   (core_if->snpsid >> 4 & 0xF), (core_if->snpsid & 0xF));

	core_if->wkp_tasklet =
	    DWC_TASK_ALLOC("wkp_tasklet", w_wakeup_detected, core_if);

	if (dwc_otg_setup_params(core_if))
		DWC_WARN("Error while setting core params\n");

	core_if->hibernation_suspend = 0;
	if (core_if->otg_ver)
		core_if->test_mode = 0;

	/** ADP initialization */
	dwc_otg_adp_init(core_if);

	return core_if;
}

/**
 * This function frees the structures allocated by dwc_otg_cil_init().
 *
 * @param core_if The core interface pointer returned from
 * dwc_otg_cil_init().
 *
 */
void dwc_otg_cil_remove(dwc_otg_core_if_t *core_if)
{
	dctl_data_t dctl = {.d32 = 0 };
	/* Disable all interrupts */
	DWC_MODIFY_REG32(&core_if->core_global_regs->gahbcfg, 1, 0);
	DWC_WRITE_REG32(&core_if->core_global_regs->gintmsk, 0);

	dctl.b.sftdiscon = 1;
	if (core_if->snpsid >= OTG_CORE_REV_3_00a) {
		DWC_MODIFY_REG32(&core_if->dev_if->dev_global_regs->dctl, 0,
				 dctl.d32);
	}

	if (core_if->wq_otg) {
		DWC_WORKQ_WAIT_WORK_DONE(core_if->wq_otg, 500);
		DWC_WORKQ_FREE(core_if->wq_otg);
	}
	if (core_if->dev_if)
		DWC_FREE(core_if->dev_if);
	if (core_if->host_if)
		DWC_FREE(core_if->host_if);

	/** Remove ADP Stuff  */
	dwc_otg_adp_remove(core_if);
	if (core_if->core_params)
		DWC_FREE(core_if->core_params);
	if (core_if->wkp_tasklet)
		DWC_TASK_FREE(core_if->wkp_tasklet);
	if (core_if->srp_timer)
		DWC_TIMER_FREE(core_if->srp_timer);

	DWC_FREE(core_if);
}

/**
 * This function enables the controller's Global Interrupt in the AHB Config
 * register.
 *
 * @param core_if Programming view of DWC_otg controller.
 */
void dwc_otg_enable_global_interrupts(dwc_otg_core_if_t *core_if)
{
	gahbcfg_data_t ahbcfg = {.d32 = 0 };
	ahbcfg.b.glblintrmsk = 1;	/* Enable interrupts */
	DWC_MODIFY_REG32(&core_if->core_global_regs->gahbcfg, 0, ahbcfg.d32);
}

/**
 * This function disables the controller's Global Interrupt in the AHB Config
 * register.
 *
 * @param core_if Programming view of DWC_otg controller.
 */
void dwc_otg_disable_global_interrupts(dwc_otg_core_if_t *core_if)
{
	gahbcfg_data_t ahbcfg = {.d32 = 0 };
	ahbcfg.b.glblintrmsk = 1;	/* Disable interrupts */
	DWC_MODIFY_REG32(&core_if->core_global_regs->gahbcfg, ahbcfg.d32, 0);
}

/**
 * This function initializes the commmon interrupts, used in both
 * device and host modes.
 *
 * @param core_if Programming view of the DWC_otg controller
 *
 */
static void dwc_otg_enable_common_interrupts(dwc_otg_core_if_t *core_if)
{
	dwc_otg_core_global_regs_t *global_regs = core_if->core_global_regs;
	gintmsk_data_t intr_mask = {.d32 = 0 };

	/* Clear any pending OTG Interrupts */
	DWC_WRITE_REG32(&global_regs->gotgint, 0xFFFFFFFF);

	/* Clear any pending interrupts */
	DWC_WRITE_REG32(&global_regs->gintsts, 0xFFFFFFFF);

	/*
	 * Enable the interrupts in the GINTMSK.
	 */
	intr_mask.b.modemismatch = 1;
	intr_mask.b.otgintr = 1;

	if (!core_if->dma_enable) {
		intr_mask.b.rxstsqlvl = 1;
	}

	intr_mask.b.conidstschng = 1;
	intr_mask.b.wkupintr = 1;
	intr_mask.b.disconnect = 0;
	intr_mask.b.usbsuspend = 1;
	/* intr_mask.b.sessreqintr = 1; */
#ifdef CONFIG_USB_DWC_OTG_LPM
	if (core_if->core_params->lpm_enable)
		intr_mask.b.lpmtranrcvd = 1;
#endif
	DWC_WRITE_REG32(&global_regs->gintmsk, intr_mask.d32);
}

/*
 * The restore operation is modified to support Synopsys Emulated Powerdown and
 * Hibernation. This function is for exiting from Device mode hibernation by
 * Host Initiated Resume/Reset and Device Initiated Remote-Wakeup.
 * @param core_if Programming view of DWC_otg controller.
 * @param rem_wakeup - indicates whether resume is initiated by Device or Host.
 * @param reset - indicates whether resume is initiated by Reset.
 */
int dwc_otg_device_hibernation_restore(dwc_otg_core_if_t *core_if,
				       int rem_wakeup, int reset)
{
	gpwrdn_data_t gpwrdn = {.d32 = 0 };
	pcgcctl_data_t pcgcctl = {.d32 = 0 };
	dctl_data_t dctl = {.d32 = 0 };

	int timeout = 2000;

	if (!core_if->hibernation_suspend) {
		DWC_PRINTF("Already exited from Hibernation\n");
		return 1;
	}

	DWC_DEBUGPL(DBG_PCD, "%s called\n", __func__);
	/* Switch-on voltage to the core */
	gpwrdn.b.pwrdnswtch = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32, 0);
	dwc_udelay(10);

	/* Reset core */
	gpwrdn.d32 = 0;
	gpwrdn.b.pwrdnrstn = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32, 0);
	dwc_udelay(10);

	/* Assert Restore signal */
	gpwrdn.d32 = 0;
	gpwrdn.b.restore = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, 0, gpwrdn.d32);
	dwc_udelay(10);

	/* Disable power clamps */
	gpwrdn.d32 = 0;
	gpwrdn.b.pwrdnclmp = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32, 0);

	if (rem_wakeup)
		dwc_udelay(70);

	/* Deassert Reset core */
	gpwrdn.d32 = 0;
	gpwrdn.b.pwrdnrstn = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, 0, gpwrdn.d32);
	dwc_udelay(10);

	/* Disable PMU interrupt */
	gpwrdn.d32 = 0;
	gpwrdn.b.pmuintsel = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32, 0);

	/* Mask interrupts from gpwrdn */
	gpwrdn.d32 = 0;
	gpwrdn.b.connect_det_msk = 1;
	gpwrdn.b.srp_det_msk = 1;
	gpwrdn.b.disconn_det_msk = 1;
	gpwrdn.b.rst_det_msk = 1;
	gpwrdn.b.lnstchng_msk = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32, 0);

	/* Indicates that we are going out from hibernation */
	core_if->hibernation_suspend = 0;

	/*
	 * Set Restore Essential Regs bit in PCGCCTL register, restore_mode = 1
	 * indicates restore from remote_wakeup
	 */
	restore_essential_regs(core_if, rem_wakeup, 0);

	/*
	 * Wait a little for seeing new value of variable hibernation_suspend if
	 * Restore done interrupt received before polling
	 */
	dwc_udelay(10);

	if (core_if->hibernation_suspend == 0) {
		/*
		 * Wait For Restore_done Interrupt. This mechanism of polling the
		 * interrupt is introduced to avoid any possible race conditions
		 */
		do {
			gintsts_data_t gintsts;
			gintsts.d32 =
			    DWC_READ_REG32(&core_if->core_global_regs->gintsts);
			if (gintsts.b.restoredone) {
				gintsts.d32 = 0;
				gintsts.b.restoredone = 1;
				DWC_WRITE_REG32(&core_if->
						core_global_regs->gintsts,
						gintsts.d32);
				DWC_PRINTF("Restore Done Interrupt seen\n");
				break;
			}
			dwc_udelay(10);
		} while (--timeout);
		if (!timeout) {
			DWC_PRINTF
			    ("Restore Done interrupt wasn't generated here\n");
		}
	}
	/* Clear all pending interupts */
	DWC_WRITE_REG32(&core_if->core_global_regs->gintsts, 0xFFFFFFFF);

	/* De-assert Restore */
	gpwrdn.d32 = 0;
	gpwrdn.b.restore = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32, 0);
	dwc_udelay(10);

	if (!rem_wakeup) {
		pcgcctl.d32 = 0;
		pcgcctl.b.rstpdwnmodule = 1;
		DWC_MODIFY_REG32(core_if->pcgcctl, pcgcctl.d32, 0);
	}

	/* Restore GUSBCFG and DCFG */
	DWC_WRITE_REG32(&core_if->core_global_regs->gusbcfg,
			core_if->gr_backup->gusbcfg_local);
	DWC_WRITE_REG32(&core_if->dev_if->dev_global_regs->dcfg,
			core_if->dr_backup->dcfg);

	/* De-assert Wakeup Logic */
	gpwrdn.d32 = 0;
	gpwrdn.b.pmuactv = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32, 0);
	dwc_udelay(10);

	if (!rem_wakeup) {
		/* Set Device programming done bit */
		dctl.b.pwronprgdone = 1;
		DWC_MODIFY_REG32(&core_if->dev_if->dev_global_regs->dctl, 0,
				 dctl.d32);
	} else {
		/* Start Remote Wakeup Signaling */
		dctl.d32 = core_if->dr_backup->dctl;
		dctl.b.rmtwkupsig = 1;
		DWC_WRITE_REG32(&core_if->dev_if->dev_global_regs->dctl,
				dctl.d32);
	}

	dwc_mdelay(2);
	/* Clear all pending interupts */
	DWC_WRITE_REG32(&core_if->core_global_regs->gintsts, 0xFFFFFFFF);

	/* Restore global registers */
	dwc_otg_restore_global_regs(core_if);
	/* Restore device global registers */
	dwc_otg_restore_dev_regs(core_if, rem_wakeup);

	if (rem_wakeup) {
		dwc_mdelay(7);
		dctl.d32 = 0;
		dctl.b.rmtwkupsig = 1;
		DWC_MODIFY_REG32(&core_if->dev_if->dev_global_regs->dctl,
				 dctl.d32, 0);
	}

	core_if->hibernation_suspend = 0;
	/* The core will be in ON STATE */
	core_if->lx_state = DWC_OTG_L0;
	DWC_PRINTF("Hibernation recovery completes here\n");

	return 1;
}

/*
 * The restore operation is modified to support Synopsys Emulated Powerdown and
 * Hibernation. This function is for exiting from Host mode hibernation by
 * Host Initiated Resume/Reset and Device Initiated Remote-Wakeup.
 * @param core_if Programming view of DWC_otg controller.
 * @param rem_wakeup - indicates whether resume is initiated by Device or Host.
 * @param reset - indicates whether resume is initiated by Reset.
 */
int dwc_otg_host_hibernation_restore(dwc_otg_core_if_t *core_if,
				     int rem_wakeup, int reset)
{
	gpwrdn_data_t gpwrdn = {.d32 = 0 };
	hprt0_data_t hprt0 = {.d32 = 0 };

	int timeout = 2000;

	DWC_DEBUGPL(DBG_HCD, "%s called\n", __func__);
	/* Switch-on voltage to the core */
	gpwrdn.b.pwrdnswtch = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32, 0);
	dwc_udelay(10);

	/* Reset core */
	gpwrdn.d32 = 0;
	gpwrdn.b.pwrdnrstn = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32, 0);
	dwc_udelay(10);

	/* Assert Restore signal */
	gpwrdn.d32 = 0;
	gpwrdn.b.restore = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, 0, gpwrdn.d32);
	dwc_udelay(10);

	/* Disable power clamps */
	gpwrdn.d32 = 0;
	gpwrdn.b.pwrdnclmp = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32, 0);

	if (!rem_wakeup)
		dwc_udelay(50);

	/* Deassert Reset core */
	gpwrdn.d32 = 0;
	gpwrdn.b.pwrdnrstn = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, 0, gpwrdn.d32);
	dwc_udelay(10);

	/* Disable PMU interrupt */
	gpwrdn.d32 = 0;
	gpwrdn.b.pmuintsel = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32, 0);

	gpwrdn.d32 = 0;
	gpwrdn.b.connect_det_msk = 1;
	gpwrdn.b.srp_det_msk = 1;
	gpwrdn.b.disconn_det_msk = 1;
	gpwrdn.b.rst_det_msk = 1;
	gpwrdn.b.lnstchng_msk = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32, 0);

	/* Indicates that we are going out from hibernation */
	core_if->hibernation_suspend = 0;

	/* Set Restore Essential Regs bit in PCGCCTL register */
	restore_essential_regs(core_if, rem_wakeup, 1);

	/* Wait a little for seeing new value of variable hibernation_suspend if
	 * Restore done interrupt received before polling */
	dwc_udelay(10);

	if (core_if->hibernation_suspend == 0) {
		/* Wait For Restore_done Interrupt. This mechanism of polling the
		 * interrupt is introduced to avoid any possible race conditions
		 */
		do {
			gintsts_data_t gintsts;
			gintsts.d32 =
			    DWC_READ_REG32(&core_if->core_global_regs->gintsts);
			if (gintsts.b.restoredone) {
				gintsts.d32 = 0;
				gintsts.b.restoredone = 1;
				DWC_WRITE_REG32(&core_if->core_global_regs->
						gintsts, gintsts.d32);
				DWC_DEBUGPL(DBG_HCD,
					    "Restore Done Interrupt seen\n");
				break;
			}
			dwc_udelay(10);
		} while (--timeout);
		if (!timeout)
			DWC_WARN("Restore Done interrupt wasn't generated\n");
	}

	/* Set the flag's value to 0 again after
	 * receiving restore done interrupt */
	core_if->hibernation_suspend = 0;

	/* This step is not described in functional spec but if not wait for this
	 * delay, mismatch interrupts occurred because just after restore core is
	 * in Device mode(gintsts.curmode == 0) */
	dwc_mdelay(100);

	/* Clear all pending interrupts */
	DWC_WRITE_REG32(&core_if->core_global_regs->gintsts, 0xFFFFFFFF);

	/* De-assert Restore */
	gpwrdn.d32 = 0;
	gpwrdn.b.restore = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32, 0);
	dwc_udelay(10);

	/* Restore GUSBCFG and HCFG */
	DWC_WRITE_REG32(&core_if->core_global_regs->gusbcfg,
			core_if->gr_backup->gusbcfg_local);
	DWC_WRITE_REG32(&core_if->host_if->host_global_regs->hcfg,
			core_if->hr_backup->hcfg_local);

	/* De-assert Wakeup Logic */
	gpwrdn.d32 = 0;
	gpwrdn.b.pmuactv = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gpwrdn, gpwrdn.d32, 0);
	dwc_udelay(10);

	/* Start the Resume operation by programming HPRT0 */
	hprt0.d32 = core_if->hr_backup->hprt0_local;
	hprt0.b.prtpwr = 1;
	hprt0.b.prtena = 0;
	hprt0.b.prtsusp = 0;
	DWC_WRITE_REG32(core_if->host_if->hprt0, hprt0.d32);

	DWC_PRINTF("Resume Starts Now\n");
	if (!reset) {
		/* Indicates it is Resume Operation */
		hprt0.d32 = core_if->hr_backup->hprt0_local;
		hprt0.b.prtres = 1;
		hprt0.b.prtpwr = 1;
		hprt0.b.prtena = 0;
		hprt0.b.prtsusp = 0;
		DWC_WRITE_REG32(core_if->host_if->hprt0, hprt0.d32);

		if (!rem_wakeup)
			hprt0.b.prtres = 0;
		/* Wait for Resume time and then program HPRT again */
		dwc_mdelay(100);
		DWC_WRITE_REG32(core_if->host_if->hprt0, hprt0.d32);

	} else {
		/* Indicates it is Reset Operation */
		hprt0.d32 = core_if->hr_backup->hprt0_local;
		hprt0.b.prtrst = 1;
		hprt0.b.prtpwr = 1;
		hprt0.b.prtena = 0;
		hprt0.b.prtsusp = 0;
		DWC_WRITE_REG32(core_if->host_if->hprt0, hprt0.d32);
		/* Wait for Reset time and then program HPRT again */
		dwc_mdelay(60);
		hprt0.b.prtrst = 0;
		DWC_WRITE_REG32(core_if->host_if->hprt0, hprt0.d32);
	}
	/* Clear all interrupt status */
	hprt0.d32 = dwc_otg_read_hprt0(core_if);
	hprt0.b.prtconndet = 1;
	hprt0.b.prtenchng = 1;
	DWC_WRITE_REG32(core_if->host_if->hprt0, hprt0.d32);

	/* Clear all pending interupts */
	DWC_WRITE_REG32(&core_if->core_global_regs->gintsts, 0xFFFFFFFF);

	/* Restore global registers */
	dwc_otg_restore_global_regs(core_if);
	/* Restore host global registers */
	dwc_otg_restore_host_regs(core_if, reset);

	/* The core will be in ON STATE */
	core_if->lx_state = DWC_OTG_L0;
	DWC_PRINTF("Hibernation recovery is complete here\n");
	return 0;
}

/** Saves some register values into system memory. */
int dwc_otg_save_global_regs(dwc_otg_core_if_t *core_if)
{
	struct dwc_otg_global_regs_backup *gr;
	int i;

	gr = core_if->gr_backup;
	if (!gr) {
		gr = DWC_ALLOC(sizeof(*gr));
		if (!gr)
			return -DWC_E_NO_MEMORY;
		core_if->gr_backup = gr;
	}

	gr->gotgctl_local = DWC_READ_REG32(&core_if->core_global_regs->gotgctl);
	gr->gintmsk_local = DWC_READ_REG32(&core_if->core_global_regs->gintmsk);
	gr->gahbcfg_local = DWC_READ_REG32(&core_if->core_global_regs->gahbcfg);
	gr->gusbcfg_local = DWC_READ_REG32(&core_if->core_global_regs->gusbcfg);
	gr->grxfsiz_local = DWC_READ_REG32(&core_if->core_global_regs->grxfsiz);
	gr->gnptxfsiz_local =
	    DWC_READ_REG32(&core_if->core_global_regs->gnptxfsiz);
	gr->hptxfsiz_local =
	    DWC_READ_REG32(&core_if->core_global_regs->hptxfsiz);
#ifdef CONFIG_USB_DWC_OTG_LPM
	gr->glpmcfg_local = DWC_READ_REG32(&core_if->core_global_regs->glpmcfg);
#endif
	gr->gi2cctl_local = DWC_READ_REG32(&core_if->core_global_regs->gi2cctl);
	gr->pcgcctl_local = DWC_READ_REG32(core_if->pcgcctl);
	gr->gdfifocfg_local =
	    DWC_READ_REG32(&core_if->core_global_regs->gdfifocfg);
	for (i = 0; i < MAX_EPS_CHANNELS; i++) {
		gr->dtxfsiz_local[i] =
		    DWC_READ_REG32(&(core_if->core_global_regs->dtxfsiz[i]));
	}

	DWC_DEBUGPL(DBG_ANY, "===========Backing Global registers==========\n");
	DWC_DEBUGPL(DBG_ANY, "Backed up gotgctl   = %08x\n", gr->gotgctl_local);
	DWC_DEBUGPL(DBG_ANY, "Backed up gintmsk   = %08x\n", gr->gintmsk_local);
	DWC_DEBUGPL(DBG_ANY, "Backed up gahbcfg   = %08x\n", gr->gahbcfg_local);
	DWC_DEBUGPL(DBG_ANY, "Backed up gusbcfg   = %08x\n", gr->gusbcfg_local);
	DWC_DEBUGPL(DBG_ANY, "Backed up grxfsiz   = %08x\n", gr->grxfsiz_local);
	DWC_DEBUGPL(DBG_ANY, "Backed up gnptxfsiz = %08x\n",
		    gr->gnptxfsiz_local);
	DWC_DEBUGPL(DBG_ANY, "Backed up hptxfsiz  = %08x\n",
		    gr->hptxfsiz_local);
#ifdef CONFIG_USB_DWC_OTG_LPM
	DWC_DEBUGPL(DBG_ANY, "Backed up glpmcfg   = %08x\n", gr->glpmcfg_local);
#endif
	DWC_DEBUGPL(DBG_ANY, "Backed up gi2cctl   = %08x\n", gr->gi2cctl_local);
	DWC_DEBUGPL(DBG_ANY, "Backed up pcgcctl   = %08x\n", gr->pcgcctl_local);
	DWC_DEBUGPL(DBG_ANY, "Backed up gdfifocfg   = %08x\n",
		    gr->gdfifocfg_local);

	return 0;
}

/** Saves GINTMSK register before setting the msk bits. */
int dwc_otg_save_gintmsk_reg(dwc_otg_core_if_t *core_if)
{
	struct dwc_otg_global_regs_backup *gr;

	gr = core_if->gr_backup;
	if (!gr) {
		gr = DWC_ALLOC(sizeof(*gr));
		if (!gr)
			return -DWC_E_NO_MEMORY;
		core_if->gr_backup = gr;
	}

	gr->gintmsk_local = DWC_READ_REG32(&core_if->core_global_regs->gintmsk);

	DWC_DEBUGPL(DBG_ANY,
		    "=============Backing GINTMSK registers============\n");
	DWC_DEBUGPL(DBG_ANY, "Backed up gintmsk   = %08x\n", gr->gintmsk_local);

	return 0;
}

int dwc_otg_save_dev_regs(dwc_otg_core_if_t *core_if)
{
	struct dwc_otg_dev_regs_backup *dr;
	int i;

	dr = core_if->dr_backup;
	if (!dr) {
		dr = DWC_ALLOC(sizeof(*dr));
		if (!dr)
			return -DWC_E_NO_MEMORY;
		core_if->dr_backup = dr;
	}

	dr->dcfg = DWC_READ_REG32(&core_if->dev_if->dev_global_regs->dcfg);
	dr->dctl = DWC_READ_REG32(&core_if->dev_if->dev_global_regs->dctl);
	dr->daintmsk =
	    DWC_READ_REG32(&core_if->dev_if->dev_global_regs->daintmsk);
	dr->diepmsk =
	    DWC_READ_REG32(&core_if->dev_if->dev_global_regs->diepmsk);
	dr->doepmsk =
	    DWC_READ_REG32(&core_if->dev_if->dev_global_regs->doepmsk);

	for (i = 0; i < core_if->dev_if->num_in_eps; ++i) {
		dr->diepctl[i] =
		    DWC_READ_REG32(&core_if->dev_if->in_ep_regs[i]->diepctl);
		dr->dieptsiz[i] =
		    DWC_READ_REG32(&core_if->dev_if->in_ep_regs[i]->dieptsiz);
		dr->diepdma[i] =
		    DWC_READ_REG32(&core_if->dev_if->in_ep_regs[i]->diepdma);
	}

	DWC_DEBUGPL(DBG_ANY,
		    "=============Backing Host registers==============\n");
	DWC_DEBUGPL(DBG_ANY, "Backed up dcfg            = %08x\n", dr->dcfg);
	DWC_DEBUGPL(DBG_ANY, "Backed up dctl        = %08x\n", dr->dctl);
	DWC_DEBUGPL(DBG_ANY, "Backed up daintmsk            = %08x\n",
		    dr->daintmsk);
	DWC_DEBUGPL(DBG_ANY, "Backed up diepmsk        = %08x\n", dr->diepmsk);
	DWC_DEBUGPL(DBG_ANY, "Backed up doepmsk        = %08x\n", dr->doepmsk);
	for (i = 0; i < core_if->dev_if->num_in_eps; ++i) {
		DWC_DEBUGPL(DBG_ANY, "Backed up diepctl[%d]        = %08x\n", i,
			    dr->diepctl[i]);
		DWC_DEBUGPL(DBG_ANY, "Backed up dieptsiz[%d]        = %08x\n",
			    i, dr->dieptsiz[i]);
		DWC_DEBUGPL(DBG_ANY, "Backed up diepdma[%d]        = %08x\n", i,
			    dr->diepdma[i]);
	}

	return 0;
}

int dwc_otg_save_host_regs(dwc_otg_core_if_t *core_if)
{
	struct dwc_otg_host_regs_backup *hr;
	int i;

	hr = core_if->hr_backup;
	if (!hr) {
		hr = DWC_ALLOC(sizeof(*hr));
		if (!hr)
			return -DWC_E_NO_MEMORY;
		core_if->hr_backup = hr;
	}

	hr->hcfg_local =
	    DWC_READ_REG32(&core_if->host_if->host_global_regs->hcfg);
	hr->haintmsk_local =
	    DWC_READ_REG32(&core_if->host_if->host_global_regs->haintmsk);
	for (i = 0; i < dwc_otg_get_param_host_channels(core_if); ++i) {
		hr->hcintmsk_local[i] =
		    DWC_READ_REG32(&core_if->host_if->hc_regs[i]->hcintmsk);
	}
	hr->hprt0_local = DWC_READ_REG32(core_if->host_if->hprt0);
	hr->hfir_local =
	    DWC_READ_REG32(&core_if->host_if->host_global_regs->hfir);

	DWC_DEBUGPL(DBG_ANY,
		    "=============Backing Host registers===============\n");
	DWC_DEBUGPL(DBG_ANY, "Backed up hcfg		= %08x\n",
		    hr->hcfg_local);
	DWC_DEBUGPL(DBG_ANY, "Backed up haintmsk = %08x\n", hr->haintmsk_local);
	for (i = 0; i < dwc_otg_get_param_host_channels(core_if); ++i) {
		DWC_DEBUGPL(DBG_ANY, "Backed up hcintmsk[%02d]=%08x\n", i,
			    hr->hcintmsk_local[i]);
	}
	DWC_DEBUGPL(DBG_ANY, "Backed up hprt0           = %08x\n",
		    hr->hprt0_local);
	DWC_DEBUGPL(DBG_ANY, "Backed up hfir           = %08x\n",
		    hr->hfir_local);

	return 0;
}

int dwc_otg_restore_global_regs(dwc_otg_core_if_t *core_if)
{
	struct dwc_otg_global_regs_backup *gr;
	int i;

	gr = core_if->gr_backup;
	if (!gr)
		return -DWC_E_INVALID;

	DWC_WRITE_REG32(&core_if->core_global_regs->gotgctl, gr->gotgctl_local);
	DWC_WRITE_REG32(&core_if->core_global_regs->gintmsk, gr->gintmsk_local);
	DWC_WRITE_REG32(&core_if->core_global_regs->gusbcfg, gr->gusbcfg_local);
	DWC_WRITE_REG32(&core_if->core_global_regs->gahbcfg, gr->gahbcfg_local);
	DWC_WRITE_REG32(&core_if->core_global_regs->grxfsiz, gr->grxfsiz_local);
	DWC_WRITE_REG32(&core_if->core_global_regs->gnptxfsiz,
			gr->gnptxfsiz_local);
	DWC_WRITE_REG32(&core_if->core_global_regs->hptxfsiz,
			gr->hptxfsiz_local);
	DWC_WRITE_REG32(&core_if->core_global_regs->gdfifocfg,
			gr->gdfifocfg_local);
	for (i = 0; i < MAX_EPS_CHANNELS; i++) {
		DWC_WRITE_REG32(&core_if->core_global_regs->dtxfsiz[i],
				gr->dtxfsiz_local[i]);
	}

	DWC_WRITE_REG32(&core_if->core_global_regs->gintsts, 0xFFFFFFFF);
	DWC_WRITE_REG32(core_if->host_if->hprt0, 0x0000100A);
	DWC_WRITE_REG32(&core_if->core_global_regs->gahbcfg,
			(gr->gahbcfg_local));
	return 0;
}

int dwc_otg_restore_dev_regs(dwc_otg_core_if_t *core_if, int rem_wakeup)
{
	struct dwc_otg_dev_regs_backup *dr;
	int i;

	dr = core_if->dr_backup;

	if (!dr) {
		return -DWC_E_INVALID;
	}

	if (!rem_wakeup) {
		DWC_WRITE_REG32(&core_if->dev_if->dev_global_regs->dctl,
				dr->dctl);
	}

	DWC_WRITE_REG32(&core_if->dev_if->dev_global_regs->daintmsk,
			dr->daintmsk);
	DWC_WRITE_REG32(&core_if->dev_if->dev_global_regs->diepmsk,
			dr->diepmsk);
	DWC_WRITE_REG32(&core_if->dev_if->dev_global_regs->doepmsk,
			dr->doepmsk);

	for (i = 0; i < core_if->dev_if->num_in_eps; ++i) {
		DWC_WRITE_REG32(&core_if->dev_if->in_ep_regs[i]->dieptsiz,
				dr->dieptsiz[i]);
		DWC_WRITE_REG32(&core_if->dev_if->in_ep_regs[i]->diepdma,
				dr->diepdma[i]);
		DWC_WRITE_REG32(&core_if->dev_if->in_ep_regs[i]->diepctl,
				dr->diepctl[i]);
	}

	return 0;
}

int dwc_otg_restore_host_regs(dwc_otg_core_if_t *core_if, int reset)
{
	struct dwc_otg_host_regs_backup *hr;
	int i;
	hr = core_if->hr_backup;

	if (!hr)
		return -DWC_E_INVALID;

	DWC_WRITE_REG32(&core_if->host_if->host_global_regs->hcfg,
			hr->hcfg_local);
	/* if (!reset)
	 * {
	 *	DWC_WRITE_REG32(&core_if->host_if->host_global_regs->hfir,
	 *			hr->hfir_local);
	 * }
	 */

	DWC_WRITE_REG32(&core_if->host_if->host_global_regs->haintmsk,
			hr->haintmsk_local);
	for (i = 0; i < dwc_otg_get_param_host_channels(core_if); ++i) {
		DWC_WRITE_REG32(&core_if->host_if->hc_regs[i]->hcintmsk,
				hr->hcintmsk_local[i]);
	}

	return 0;
}

int restore_lpm_i2c_regs(dwc_otg_core_if_t *core_if)
{
	struct dwc_otg_global_regs_backup *gr;

	gr = core_if->gr_backup;

	/* Restore values for LPM and I2C */
#ifdef CONFIG_USB_DWC_OTG_LPM
	DWC_WRITE_REG32(&core_if->core_global_regs->glpmcfg, gr->glpmcfg_local);
#endif
	DWC_WRITE_REG32(&core_if->core_global_regs->gi2cctl, gr->gi2cctl_local);

	return 0;
}

int restore_essential_regs(dwc_otg_core_if_t *core_if, int rmode, int is_host)
{
	struct dwc_otg_global_regs_backup *gr;
	pcgcctl_data_t pcgcctl = {.d32 = 0 };
	gahbcfg_data_t gahbcfg = {.d32 = 0 };
	gusbcfg_data_t gusbcfg = {.d32 = 0 };
	gintmsk_data_t gintmsk = {.d32 = 0 };

	/* Restore LPM and I2C registers */
	restore_lpm_i2c_regs(core_if);

	/* Set PCGCCTL to 0 */
	DWC_WRITE_REG32(core_if->pcgcctl, 0x00000000);

	gr = core_if->gr_backup;
	/* Load restore values for [31:14] bits */
	DWC_WRITE_REG32(core_if->pcgcctl,
			((gr->pcgcctl_local & 0xffffc000) | 0x00020000));

	/* Umnask global Interrupt in GAHBCFG and restore it */
	gahbcfg.d32 = gr->gahbcfg_local;
	gahbcfg.b.glblintrmsk = 1;
	DWC_WRITE_REG32(&core_if->core_global_regs->gahbcfg, gahbcfg.d32);

	/* Clear all pending interupts */
	DWC_WRITE_REG32(&core_if->core_global_regs->gintsts, 0xFFFFFFFF);

	/* Unmask restore done interrupt */
	gintmsk.b.restoredone = 1;
	DWC_WRITE_REG32(&core_if->core_global_regs->gintmsk, gintmsk.d32);

	/* Restore GUSBCFG and HCFG/DCFG */
	gusbcfg.d32 = core_if->gr_backup->gusbcfg_local;
	DWC_WRITE_REG32(&core_if->core_global_regs->gusbcfg, gusbcfg.d32);

	if (is_host) {
		hcfg_data_t hcfg = {.d32 = 0 };
		hcfg.d32 = core_if->hr_backup->hcfg_local;
		DWC_WRITE_REG32(&core_if->host_if->host_global_regs->hcfg,
				hcfg.d32);

		/* Load restore values for [31:14] bits */
		pcgcctl.d32 = gr->pcgcctl_local & 0xffffc000;
		pcgcctl.d32 = gr->pcgcctl_local | 0x00020000;

		if (rmode)
			pcgcctl.b.restoremode = 1;
		DWC_WRITE_REG32(core_if->pcgcctl, pcgcctl.d32);
		dwc_udelay(10);

		/* Load restore values for [31:14] bits
		 * and set EssRegRestored bit */
		pcgcctl.d32 = gr->pcgcctl_local | 0xffffc000;
		pcgcctl.d32 = gr->pcgcctl_local & 0xffffc000;
		pcgcctl.b.ess_reg_restored = 1;
		if (rmode)
			pcgcctl.b.restoremode = 1;
		DWC_WRITE_REG32(core_if->pcgcctl, pcgcctl.d32);
	} else {
		dcfg_data_t dcfg = {.d32 = 0 };
		dcfg.d32 = core_if->dr_backup->dcfg;
		DWC_WRITE_REG32(&core_if->dev_if->dev_global_regs->dcfg,
				dcfg.d32);

		/* Load restore values for [31:14] bits */
		pcgcctl.d32 = gr->pcgcctl_local & 0xffffc000;
		pcgcctl.d32 = gr->pcgcctl_local | 0x00020000;
		if (!rmode)
			pcgcctl.d32 |= 0x208;
		DWC_WRITE_REG32(core_if->pcgcctl, pcgcctl.d32);
		dwc_udelay(10);

		/* Load restore values for [31:14] bits */
		pcgcctl.d32 = gr->pcgcctl_local & 0xffffc000;
		pcgcctl.d32 = gr->pcgcctl_local | 0x00020000;
		pcgcctl.b.ess_reg_restored = 1;
		if (!rmode)
			pcgcctl.d32 |= 0x208;
		DWC_WRITE_REG32(core_if->pcgcctl, pcgcctl.d32);
	}

	return 0;
}

/**
 * Initializes the FSLSPClkSel field of the HCFG register depending on the PHY
 * type.
 */
static void init_fslspclksel(dwc_otg_core_if_t *core_if)
{
	uint32_t val;
	hcfg_data_t hcfg;

	if (((core_if->hwcfg2.b.hs_phy_type == 2) &&
	     (core_if->hwcfg2.b.fs_phy_type == 1) &&
	     (core_if->core_params->ulpi_fs_ls)) ||
	    (core_if->core_params->phy_type == DWC_PHY_TYPE_PARAM_FS)) {
		/* Full speed PHY */
		val = DWC_HCFG_48_MHZ;
	} else {
		/* High speed PHY running at full speed or high speed */
		val = DWC_HCFG_30_60_MHZ;
	}

	DWC_DEBUGPL(DBG_CIL, "Initializing HCFG.FSLSPClkSel to 0x%1x\n", val);
	hcfg.d32 = DWC_READ_REG32(&core_if->host_if->host_global_regs->hcfg);
	hcfg.b.fslspclksel = val;
	DWC_WRITE_REG32(&core_if->host_if->host_global_regs->hcfg, hcfg.d32);
}

/**
 * Initializes the DevSpd field of the DCFG register depending on the PHY type
 * and the enumeration speed of the device.
 */
static void init_devspd(dwc_otg_core_if_t *core_if)
{
	uint32_t val;
	dcfg_data_t dcfg;

	if (((core_if->hwcfg2.b.hs_phy_type == 2) &&
	     (core_if->hwcfg2.b.fs_phy_type == 1) &&
	     (core_if->core_params->ulpi_fs_ls)) ||
	    (core_if->core_params->phy_type == DWC_PHY_TYPE_PARAM_FS)) {
		/* Full speed PHY */
		val = 0x3;
	} else if (core_if->core_params->speed == DWC_SPEED_PARAM_FULL) {
		/* High speed PHY running at full speed */
		val = 0x1;
	} else {
		/* High speed PHY running at high speed */
		val = 0x0;
	}

	DWC_DEBUGPL(DBG_CIL, "Initializing DCFG.DevSpd to 0x%1x\n", val);

	dcfg.d32 = DWC_READ_REG32(&core_if->dev_if->dev_global_regs->dcfg);
	dcfg.b.devspd = val;
	DWC_WRITE_REG32(&core_if->dev_if->dev_global_regs->dcfg, dcfg.d32);
}

/**
 * This function calculates the number of IN EPS
 * using GHWCFG1 and GHWCFG2 registers values
 *
 * @param core_if Programming view of the DWC_otg controller
 */
static uint32_t calc_num_in_eps(dwc_otg_core_if_t *core_if)
{
	uint32_t num_in_eps = 0;
	uint32_t num_eps = core_if->hwcfg2.b.num_dev_ep;
	uint32_t hwcfg1 = core_if->hwcfg1.d32 >> 3;
	uint32_t num_tx_fifos = core_if->hwcfg4.b.num_in_eps;
	int i;

	for (i = 0; i < num_eps; ++i) {
		if (!(hwcfg1 & 0x1))
			num_in_eps++;

		hwcfg1 >>= 2;
	}

	if (core_if->hwcfg4.b.ded_fifo_en) {
		num_in_eps =
		    (num_in_eps > num_tx_fifos) ? num_tx_fifos : num_in_eps;
	}

	return num_in_eps;
}

/**
 * This function calculates the number of OUT EPS
 * using GHWCFG1 and GHWCFG2 registers values
 *
 * @param core_if Programming view of the DWC_otg controller
 */
static uint32_t calc_num_out_eps(dwc_otg_core_if_t *core_if)
{
	uint32_t num_out_eps = 0;
	uint32_t num_eps = core_if->hwcfg2.b.num_dev_ep;
	uint32_t hwcfg1 = core_if->hwcfg1.d32 >> 2;
	int i;

	for (i = 0; i < num_eps; ++i) {
		if (!(hwcfg1 & 0x1))
			num_out_eps++;

		hwcfg1 >>= 2;
	}
	return num_out_eps;
}

void dwc_otg_core_init(dwc_otg_core_if_t *core_if)
{
	int i = 0;
	dwc_otg_core_global_regs_t *global_regs = core_if->core_global_regs;
	dwc_otg_dev_if_t *dev_if = core_if->dev_if;
	gahbcfg_data_t ahbcfg = {.d32 = 0 };
	gusbcfg_data_t usbcfg = {.d32 = 0 };
	gi2cctl_data_t i2cctl = {.d32 = 0 };

	DWC_DEBUGPL(DBG_CILV, "dwc_otg_core_init(%p)\n", core_if);
	/* Common Initialization */
	usbcfg.d32 = DWC_READ_REG32(&global_regs->gusbcfg);

	/* Program the ULPI External VBUS bit if needed */
	usbcfg.b.ulpi_ext_vbus_drv =
	    (core_if->core_params->phy_ulpi_ext_vbus ==
	     DWC_PHY_ULPI_EXTERNAL_VBUS) ? 1 : 0;

	/* Set external TS Dline pulsing */
	usbcfg.b.term_sel_dl_pulse =
	    (core_if->core_params->ts_dline == 1) ? 1 : 0;
	DWC_WRITE_REG32(&global_regs->gusbcfg, usbcfg.d32);

	/* Reset the Controller */
	dwc_otg_core_reset(core_if);

	core_if->adp_enable = core_if->core_params->adp_supp_enable;
	core_if->power_down = core_if->core_params->power_down;

	/* Initialize parameters from Hardware configuration registers. */
	dev_if->num_in_eps = calc_num_in_eps(core_if);
	dev_if->num_out_eps = calc_num_out_eps(core_if);

	DWC_DEBUGPL(DBG_CIL, "num_dev_perio_in_ep=%d\n",
		    core_if->hwcfg4.b.num_dev_perio_in_ep);

	for (i = 0; i < core_if->hwcfg4.b.num_dev_perio_in_ep; i++) {
		dev_if->perio_tx_fifo_size[i] =
		    DWC_READ_REG32(&global_regs->dtxfsiz[i]) >> 16;
		DWC_DEBUGPL(DBG_CIL, "Periodic Tx FIFO SZ #%d=0x%0x\n",
			    i, dev_if->perio_tx_fifo_size[i]);
	}

	for (i = 0; i < core_if->hwcfg4.b.num_in_eps; i++) {
		dev_if->tx_fifo_size[i] =
		    DWC_READ_REG32(&global_regs->dtxfsiz[i]) >> 16;
		DWC_DEBUGPL(DBG_CIL, "Tx FIFO SZ #%d=0x%0x\n",
			    i, dev_if->tx_fifo_size[i]);
	}

	core_if->total_fifo_size = core_if->hwcfg3.b.dfifo_depth;
	core_if->rx_fifo_size = DWC_READ_REG32(&global_regs->grxfsiz);
	core_if->nperio_tx_fifo_size =
	    DWC_READ_REG32(&global_regs->gnptxfsiz) >> 16;

	DWC_DEBUGPL(DBG_CIL, "Total FIFO SZ=%d\n", core_if->total_fifo_size);
	DWC_DEBUGPL(DBG_CIL, "Rx FIFO SZ=%d\n", core_if->rx_fifo_size);
	DWC_DEBUGPL(DBG_CIL, "NP Tx FIFO SZ=%d\n",
		    core_if->nperio_tx_fifo_size);

	/* This programming sequence needs to happen in FS mode before any other
	 * programming occurs */
	if ((core_if->core_params->speed == DWC_SPEED_PARAM_FULL) &&
	    (core_if->core_params->phy_type == DWC_PHY_TYPE_PARAM_FS)) {
		/* If FS mode with FS PHY */

		/* core_init() is now called on every switch so only call the
		 * following for the first time through. */
		if (!core_if->phy_init_done) {
			core_if->phy_init_done = 1;
			DWC_DEBUGPL(DBG_CIL, "FS_PHY detected\n");
			usbcfg.d32 = DWC_READ_REG32(&global_regs->gusbcfg);
			usbcfg.b.physel = 1;
			DWC_WRITE_REG32(&global_regs->gusbcfg, usbcfg.d32);

			/* Reset after a PHY select */
			dwc_otg_core_reset(core_if);
		}

		/* Program DCFG.DevSpd or HCFG.FSLSPclkSel to 48Mhz in FS. Also
		 * do this on HNP Dev/Host mode switches (done in dev_init and
		 * host_init). */
		if (dwc_otg_is_host_mode(core_if))
			init_fslspclksel(core_if);
		else
			init_devspd(core_if);

		if (core_if->core_params->i2c_enable) {
			DWC_DEBUGPL(DBG_CIL, "FS_PHY Enabling I2c\n");
			/* Program GUSBCFG.OtgUtmifsSel to I2C */
			usbcfg.d32 = DWC_READ_REG32(&global_regs->gusbcfg);
			usbcfg.b.otgutmifssel = 1;
			DWC_WRITE_REG32(&global_regs->gusbcfg, usbcfg.d32);

			/* Program GI2CCTL.I2CEn */
			i2cctl.d32 = DWC_READ_REG32(&global_regs->gi2cctl);
			i2cctl.b.i2cdevaddr = 1;
			i2cctl.b.i2cen = 0;
			DWC_WRITE_REG32(&global_regs->gi2cctl, i2cctl.d32);
			i2cctl.b.i2cen = 1;
			DWC_WRITE_REG32(&global_regs->gi2cctl, i2cctl.d32);
		}

	} /* endif speed == DWC_SPEED_PARAM_FULL */
	else {
		/* High speed PHY. */
		if (!core_if->phy_init_done) {
			core_if->phy_init_done = 1;
			/* HS PHY parameters.  These parameters are preserved
			 * during soft reset so only program the first time.  Do
			 * a soft reset immediately after setting phyif.  */

			if (core_if->core_params->phy_type == 2) {
				/* ULPI interface */
				usbcfg.b.ulpi_utmi_sel = 1;
				usbcfg.b.phyif = 0;
				usbcfg.b.ddrsel =
				    core_if->core_params->phy_ulpi_ddr;
			} else if (core_if->core_params->phy_type == 1) {
				/* UTMI+ interface */
				usbcfg.b.ulpi_utmi_sel = 0;
				if (core_if->core_params->phy_utmi_width == 16)
					usbcfg.b.phyif = 1;
				else
					usbcfg.b.phyif = 0;
			} else {
				DWC_ERROR("FS PHY TYPE\n");
			}
			DWC_WRITE_REG32(&global_regs->gusbcfg, usbcfg.d32);
			/* Reset after setting the PHY parameters */
			dwc_otg_core_reset(core_if);
		}
	}

	if ((core_if->hwcfg2.b.hs_phy_type == 2) &&
	    (core_if->hwcfg2.b.fs_phy_type == 1) &&
	    (core_if->core_params->ulpi_fs_ls)) {
		DWC_DEBUGPL(DBG_CIL, "Setting ULPI FSLS\n");
		usbcfg.d32 = DWC_READ_REG32(&global_regs->gusbcfg);
		usbcfg.b.ulpi_fsls = 1;
		usbcfg.b.ulpi_clk_sus_m = 1;
		DWC_WRITE_REG32(&global_regs->gusbcfg, usbcfg.d32);
	} else {
		usbcfg.d32 = DWC_READ_REG32(&global_regs->gusbcfg);
		usbcfg.b.ulpi_fsls = 0;
		usbcfg.b.ulpi_clk_sus_m = 0;
		DWC_WRITE_REG32(&global_regs->gusbcfg, usbcfg.d32);
	}

	/* Program the GAHBCFG Register. */
	switch (core_if->hwcfg2.b.architecture) {

	case DWC_SLAVE_ONLY_ARCH:
		DWC_DEBUGPL(DBG_CIL, "Slave Only Mode\n");
		ahbcfg.b.nptxfemplvl_txfemplvl =
		    DWC_GAHBCFG_TXFEMPTYLVL_HALFEMPTY;
		ahbcfg.b.ptxfemplvl = DWC_GAHBCFG_TXFEMPTYLVL_HALFEMPTY;
		core_if->dma_enable = 0;
		core_if->dma_desc_enable = 0;
		break;

	case DWC_EXT_DMA_ARCH:
		DWC_DEBUGPL(DBG_CIL, "External DMA Mode\n");
		{
			uint8_t brst_sz = core_if->core_params->dma_burst_size;
			ahbcfg.b.hburstlen = 0;
			while (brst_sz > 1) {
				ahbcfg.b.hburstlen++;
				brst_sz >>= 1;
			}
		}
		core_if->dma_enable = (core_if->core_params->dma_enable != 0);
		core_if->dma_desc_enable =
		    (core_if->core_params->dma_desc_enable != 0);
		break;

	case DWC_INT_DMA_ARCH:
		DWC_DEBUGPL(DBG_CIL, "Internal DMA Mode\n");
		/* Old value was DWC_GAHBCFG_INT_DMA_BURST_INCR - done for
		   Host mode ISOC in issue fix - vahrama */
		ahbcfg.b.hburstlen = DWC_GAHBCFG_INT_DMA_BURST_INCR8;
		core_if->dma_enable = (core_if->core_params->dma_enable != 0);
		core_if->dma_desc_enable =
		    (core_if->core_params->dma_desc_enable != 0);
		break;

	}
	if (core_if->dma_enable) {
		if (core_if->dma_desc_enable)
			DWC_PRINTF("Using Descriptor DMA mode\n");
		else
			DWC_PRINTF("Using Buffer DMA mode\n");
	} else {
		DWC_PRINTF("Using Slave mode\n");
		core_if->dma_desc_enable = 0;
	}

	if (core_if->core_params->ahb_single)
		ahbcfg.b.ahbsingle = 1;

	ahbcfg.b.dmaenable = core_if->dma_enable;
	DWC_WRITE_REG32(&global_regs->gahbcfg, ahbcfg.d32);

	core_if->en_multiple_tx_fifo = core_if->hwcfg4.b.ded_fifo_en;

	core_if->pti_enh_enable = core_if->core_params->pti_enable != 0;
	core_if->multiproc_int_enable = core_if->core_params->mpi_enable;
	DWC_PRINTF("Periodic Transfer Interrupt Enhancement - %s\n",
		   ((core_if->pti_enh_enable) ? "enabled" : "disabled"));
	DWC_PRINTF("Multiprocessor Interrupt Enhancement - %s\n",
		   ((core_if->multiproc_int_enable) ? "enabled" : "disabled"));

	/*
	 * Program the GUSBCFG register.
	 */
	usbcfg.d32 = DWC_READ_REG32(&global_regs->gusbcfg);

	switch (core_if->hwcfg2.b.op_mode) {
	case DWC_MODE_HNP_SRP_CAPABLE:
		usbcfg.b.hnpcap = (core_if->core_params->otg_cap ==
				   DWC_OTG_CAP_PARAM_HNP_SRP_CAPABLE);
		usbcfg.b.srpcap = (core_if->core_params->otg_cap !=
				   DWC_OTG_CAP_PARAM_NO_HNP_SRP_CAPABLE);
		break;

	case DWC_MODE_SRP_ONLY_CAPABLE:
		usbcfg.b.hnpcap = 0;
		usbcfg.b.srpcap = (core_if->core_params->otg_cap !=
				   DWC_OTG_CAP_PARAM_NO_HNP_SRP_CAPABLE);
		break;

	case DWC_MODE_NO_HNP_SRP_CAPABLE:
		usbcfg.b.hnpcap = 0;
		usbcfg.b.srpcap = 0;
		break;

	case DWC_MODE_SRP_CAPABLE_DEVICE:
		usbcfg.b.hnpcap = 0;
		usbcfg.b.srpcap = (core_if->core_params->otg_cap !=
				   DWC_OTG_CAP_PARAM_NO_HNP_SRP_CAPABLE);
		break;

	case DWC_MODE_NO_SRP_CAPABLE_DEVICE:
		usbcfg.b.hnpcap = 0;
		usbcfg.b.srpcap = 0;
		break;

	case DWC_MODE_SRP_CAPABLE_HOST:
		usbcfg.b.hnpcap = 0;
		usbcfg.b.srpcap = (core_if->core_params->otg_cap !=
				   DWC_OTG_CAP_PARAM_NO_HNP_SRP_CAPABLE);
		break;

	case DWC_MODE_NO_SRP_CAPABLE_HOST:
		usbcfg.b.hnpcap = 0;
		usbcfg.b.srpcap = 0;
		break;
	}

	DWC_WRITE_REG32(&global_regs->gusbcfg, usbcfg.d32);

#ifdef CONFIG_USB_DWC_OTG_LPM
	if (core_if->core_params->lpm_enable) {
		glpmcfg_data_t lpmcfg = {.d32 = 0 };

		/* To enable LPM support set lpm_cap_en bit */
		lpmcfg.b.lpm_cap_en = 1;

		/* Make AppL1Res ACK */
		lpmcfg.b.appl_resp = 1;

		/* Retry 3 times */
		lpmcfg.b.retry_count = 3;

		DWC_MODIFY_REG32(&core_if->core_global_regs->glpmcfg,
				 0, lpmcfg.d32);

	}
#endif
	if (core_if->core_params->ic_usb_cap) {
		gusbcfg_data_t gusbcfg = {.d32 = 0 };
		gusbcfg.b.ic_usb_cap = 1;
		DWC_MODIFY_REG32(&core_if->core_global_regs->gusbcfg,
				 0, gusbcfg.d32);
	}
	{
		gotgctl_data_t gotgctl = {.d32 = 0 };
		gotgctl.b.otgver = core_if->core_params->otg_ver;
		DWC_MODIFY_REG32(&core_if->core_global_regs->gotgctl, 0,
				 gotgctl.d32);
		/* Set OTG version supported */
		core_if->otg_ver = core_if->core_params->otg_ver;
		DWC_PRINTF("OTG VER PARAM: %d, OTG VER FLAG: %d\n",
			   core_if->core_params->otg_ver, core_if->otg_ver);
	}

	/* Enable common interrupts */
	dwc_otg_enable_common_interrupts(core_if);

	/* Do device or host intialization based on mode during PCD
	 * and HCD initialization  */
	if (dwc_otg_is_host_mode(core_if)) {
		DWC_PRINTF("^^^^^^^^^^^^^^^^^^Host Mode\n");
		core_if->op_state = A_HOST;
	} else {
		DWC_PRINTF("^^^^^^^^^^^^^^^^^Device Mode\n");
		core_if->op_state = B_PERIPHERAL;
#ifdef DWC_DEVICE_ONLY
		dwc_otg_core_dev_init(core_if);
#endif
	}
}

/**
 * This function initializes the DWC_otg controller registers and
 * prepares the core for device mode or host mode operation.
 *
 * @param core_if Programming view of the DWC_otg controller
 *
 */
void dwc_otg_core_init_no_reset(dwc_otg_core_if_t *core_if)
{
	int i = 0;
	dwc_otg_core_global_regs_t *global_regs = core_if->core_global_regs;
	dwc_otg_dev_if_t *dev_if = core_if->dev_if;
	gahbcfg_data_t ahbcfg = {.d32 = 0 };
	gusbcfg_data_t usbcfg = {.d32 = 0 };
	gi2cctl_data_t i2cctl = {.d32 = 0 };

	DWC_DEBUGPL(DBG_CILV, "dwc_otg_core_init(%p)\n", core_if);
	/* Common Initialization */
	usbcfg.d32 = DWC_READ_REG32(&global_regs->gusbcfg);

	/* Program the ULPI External VBUS bit if needed */
	usbcfg.b.ulpi_ext_vbus_drv =
	    (core_if->core_params->phy_ulpi_ext_vbus ==
	     DWC_PHY_ULPI_EXTERNAL_VBUS) ? 1 : 0;

	/* Set external TS Dline pulsing */
	usbcfg.b.term_sel_dl_pulse =
	    (core_if->core_params->ts_dline == 1) ? 1 : 0;
	DWC_WRITE_REG32(&global_regs->gusbcfg, usbcfg.d32);

	/* Reset the Controller */
	/* dwc_otg_core_reset(core_if); */

	core_if->adp_enable = core_if->core_params->adp_supp_enable;
	core_if->power_down = core_if->core_params->power_down;

	/* Initialize parameters from Hardware configuration registers. */
	dev_if->num_in_eps = calc_num_in_eps(core_if);
	dev_if->num_out_eps = calc_num_out_eps(core_if);

	DWC_DEBUGPL(DBG_CIL, "num_dev_perio_in_ep=%d\n",
		    core_if->hwcfg4.b.num_dev_perio_in_ep);

	for (i = 0; i < core_if->hwcfg4.b.num_dev_perio_in_ep; i++) {
		dev_if->perio_tx_fifo_size[i] =
		    DWC_READ_REG32(&global_regs->dtxfsiz[i]) >> 16;
		DWC_DEBUGPL(DBG_CIL, "Periodic Tx FIFO SZ #%d=0x%0x\n",
			    i, dev_if->perio_tx_fifo_size[i]);
	}

	for (i = 0; i < core_if->hwcfg4.b.num_in_eps; i++) {
		dev_if->tx_fifo_size[i] =
		    DWC_READ_REG32(&global_regs->dtxfsiz[i]) >> 16;
		DWC_DEBUGPL(DBG_CIL, "Tx FIFO SZ #%d=0x%0x\n",
			    i, dev_if->tx_fifo_size[i]);
	}

	core_if->total_fifo_size = core_if->hwcfg3.b.dfifo_depth;
	core_if->rx_fifo_size = DWC_READ_REG32(&global_regs->grxfsiz);
	core_if->nperio_tx_fifo_size =
	    DWC_READ_REG32(&global_regs->gnptxfsiz) >> 16;

	DWC_DEBUGPL(DBG_CIL, "Total FIFO SZ=%d\n", core_if->total_fifo_size);
	DWC_DEBUGPL(DBG_CIL, "Rx FIFO SZ=%d\n", core_if->rx_fifo_size);
	DWC_DEBUGPL(DBG_CIL, "NP Tx FIFO SZ=%d\n",
		    core_if->nperio_tx_fifo_size);

	/* This programming sequence needs to happen in FS mode before any other
	 * programming occurs */
	if ((core_if->core_params->speed == DWC_SPEED_PARAM_FULL) &&
	    (core_if->core_params->phy_type == DWC_PHY_TYPE_PARAM_FS)) {
		/* If FS mode with FS PHY */

		/* core_init() is now called on every switch so only call the
		 * following for the first time through. */
		if (!core_if->phy_init_done) {
			core_if->phy_init_done = 1;
			DWC_DEBUGPL(DBG_CIL, "FS_PHY detected\n");
			usbcfg.d32 = DWC_READ_REG32(&global_regs->gusbcfg);
			usbcfg.b.physel = 1;
			DWC_WRITE_REG32(&global_regs->gusbcfg, usbcfg.d32);

			/* Reset after a PHY select */
			/* dwc_otg_core_reset(core_if); */
		}

		/* Program DCFG.DevSpd or HCFG.FSLSPclkSel to 48Mhz in FS. Also
		 * do this on HNP Dev/Host mode switches (done in dev_init and
		 * host_init). */
		if (dwc_otg_is_host_mode(core_if))
			init_fslspclksel(core_if);
		else
			init_devspd(core_if);

		if (core_if->core_params->i2c_enable) {
			DWC_DEBUGPL(DBG_CIL, "FS_PHY Enabling I2c\n");
			/* Program GUSBCFG.OtgUtmifsSel to I2C */
			usbcfg.d32 = DWC_READ_REG32(&global_regs->gusbcfg);
			usbcfg.b.otgutmifssel = 1;
			DWC_WRITE_REG32(&global_regs->gusbcfg, usbcfg.d32);

			/* Program GI2CCTL.I2CEn */
			i2cctl.d32 = DWC_READ_REG32(&global_regs->gi2cctl);
			i2cctl.b.i2cdevaddr = 1;
			i2cctl.b.i2cen = 0;
			DWC_WRITE_REG32(&global_regs->gi2cctl, i2cctl.d32);
			i2cctl.b.i2cen = 1;
			DWC_WRITE_REG32(&global_regs->gi2cctl, i2cctl.d32);
		}

	} /* endif speed == DWC_SPEED_PARAM_FULL */
	else {
		/* High speed PHY. */
		if (!core_if->phy_init_done) {
			core_if->phy_init_done = 1;
			/* HS PHY parameters.  These parameters are preserved
			 * during soft reset so only program the first time.  Do
			 * a soft reset immediately after setting phyif.  */

			if (core_if->core_params->phy_type == 2) {
				/* ULPI interface */
				usbcfg.b.ulpi_utmi_sel = 1;
				usbcfg.b.phyif = 0;
				usbcfg.b.ddrsel =
				    core_if->core_params->phy_ulpi_ddr;
			} else if (core_if->core_params->phy_type == 1) {
				/* UTMI+ interface */
				usbcfg.b.ulpi_utmi_sel = 0;
				if (core_if->core_params->phy_utmi_width == 16)
					usbcfg.b.phyif = 1;
				else
					usbcfg.b.phyif = 0;
			} else {
				DWC_ERROR("FS PHY TYPE\n");
			}
			DWC_WRITE_REG32(&global_regs->gusbcfg, usbcfg.d32);
			/* Reset after setting the PHY parameters */
			/* dwc_otg_core_reset(core_if); */
		}
	}

	if ((core_if->hwcfg2.b.hs_phy_type == 2) &&
	    (core_if->hwcfg2.b.fs_phy_type == 1) &&
	    (core_if->core_params->ulpi_fs_ls)) {
		DWC_DEBUGPL(DBG_CIL, "Setting ULPI FSLS\n");
		usbcfg.d32 = DWC_READ_REG32(&global_regs->gusbcfg);
		usbcfg.b.ulpi_fsls = 1;
		usbcfg.b.ulpi_clk_sus_m = 1;
		DWC_WRITE_REG32(&global_regs->gusbcfg, usbcfg.d32);
	} else {
		usbcfg.d32 = DWC_READ_REG32(&global_regs->gusbcfg);
		usbcfg.b.ulpi_fsls = 0;
		usbcfg.b.ulpi_clk_sus_m = 0;
		DWC_WRITE_REG32(&global_regs->gusbcfg, usbcfg.d32);
	}

	/* Program the GAHBCFG Register. */
	switch (core_if->hwcfg2.b.architecture) {

	case DWC_SLAVE_ONLY_ARCH:
		DWC_DEBUGPL(DBG_CIL, "Slave Only Mode\n");
		ahbcfg.b.nptxfemplvl_txfemplvl =
		    DWC_GAHBCFG_TXFEMPTYLVL_HALFEMPTY;
		ahbcfg.b.ptxfemplvl = DWC_GAHBCFG_TXFEMPTYLVL_HALFEMPTY;
		core_if->dma_enable = 0;
		core_if->dma_desc_enable = 0;
		break;

	case DWC_EXT_DMA_ARCH:
		DWC_DEBUGPL(DBG_CIL, "External DMA Mode\n");
		{
			uint8_t brst_sz = core_if->core_params->dma_burst_size;
			ahbcfg.b.hburstlen = 0;
			while (brst_sz > 1) {
				ahbcfg.b.hburstlen++;
				brst_sz >>= 1;
			}
		}
		core_if->dma_enable = (core_if->core_params->dma_enable != 0);
		core_if->dma_desc_enable =
		    (core_if->core_params->dma_desc_enable != 0);
		break;

	case DWC_INT_DMA_ARCH:
		DWC_DEBUGPL(DBG_CIL, "Internal DMA Mode\n");
		/* Old value was DWC_GAHBCFG_INT_DMA_BURST_INCR - done for
		   Host mode ISOC in issue fix - vahrama */
		ahbcfg.b.hburstlen = DWC_GAHBCFG_INT_DMA_BURST_INCR8;
		core_if->dma_enable = (core_if->core_params->dma_enable != 0);
		core_if->dma_desc_enable =
		    (core_if->core_params->dma_desc_enable != 0);
		break;

	}
	if (core_if->dma_enable) {
		if (core_if->dma_desc_enable)
			DWC_PRINTF("Using Descriptor DMA mode\n");
		else
			DWC_PRINTF("Using Buffer DMA mode\n");
	} else {
		DWC_PRINTF("Using Slave mode\n");
		core_if->dma_desc_enable = 0;
	}

	if (core_if->core_params->ahb_single)
		ahbcfg.b.ahbsingle = 1;

	ahbcfg.b.dmaenable = core_if->dma_enable;
	DWC_WRITE_REG32(&global_regs->gahbcfg, ahbcfg.d32);

	core_if->en_multiple_tx_fifo = core_if->hwcfg4.b.ded_fifo_en;

	core_if->pti_enh_enable = core_if->core_params->pti_enable != 0;
	core_if->multiproc_int_enable = core_if->core_params->mpi_enable;
	DWC_PRINTF("Periodic Transfer Interrupt Enhancement - %s\n",
		   ((core_if->pti_enh_enable) ? "enabled" : "disabled"));
	DWC_PRINTF("Multiprocessor Interrupt Enhancement - %s\n",
		   ((core_if->multiproc_int_enable) ? "enabled" : "disabled"));

	/*
	 * Program the GUSBCFG register.
	 */
	usbcfg.d32 = DWC_READ_REG32(&global_regs->gusbcfg);

	switch (core_if->hwcfg2.b.op_mode) {
	case DWC_MODE_HNP_SRP_CAPABLE:
		usbcfg.b.hnpcap = (core_if->core_params->otg_cap ==
				   DWC_OTG_CAP_PARAM_HNP_SRP_CAPABLE);
		usbcfg.b.srpcap = (core_if->core_params->otg_cap !=
				   DWC_OTG_CAP_PARAM_NO_HNP_SRP_CAPABLE);
		break;

	case DWC_MODE_SRP_ONLY_CAPABLE:
		usbcfg.b.hnpcap = 0;
		usbcfg.b.srpcap = (core_if->core_params->otg_cap !=
				   DWC_OTG_CAP_PARAM_NO_HNP_SRP_CAPABLE);
		break;

	case DWC_MODE_NO_HNP_SRP_CAPABLE:
		usbcfg.b.hnpcap = 0;
		usbcfg.b.srpcap = 0;
		break;

	case DWC_MODE_SRP_CAPABLE_DEVICE:
		usbcfg.b.hnpcap = 0;
		usbcfg.b.srpcap = (core_if->core_params->otg_cap !=
				   DWC_OTG_CAP_PARAM_NO_HNP_SRP_CAPABLE);
		break;

	case DWC_MODE_NO_SRP_CAPABLE_DEVICE:
		usbcfg.b.hnpcap = 0;
		usbcfg.b.srpcap = 0;
		break;

	case DWC_MODE_SRP_CAPABLE_HOST:
		usbcfg.b.hnpcap = 0;
		usbcfg.b.srpcap = (core_if->core_params->otg_cap !=
				   DWC_OTG_CAP_PARAM_NO_HNP_SRP_CAPABLE);
		break;

	case DWC_MODE_NO_SRP_CAPABLE_HOST:
		usbcfg.b.hnpcap = 0;
		usbcfg.b.srpcap = 0;
		break;
	}

	DWC_WRITE_REG32(&global_regs->gusbcfg, usbcfg.d32);

#ifdef CONFIG_USB_DWC_OTG_LPM
	if (core_if->core_params->lpm_enable) {
		glpmcfg_data_t lpmcfg = {.d32 = 0 };

		/* To enable LPM support set lpm_cap_en bit */
		lpmcfg.b.lpm_cap_en = 1;

		/* Make AppL1Res ACK */
		lpmcfg.b.appl_resp = 1;

		/* Retry 3 times */
		lpmcfg.b.retry_count = 3;

		DWC_MODIFY_REG32(&core_if->core_global_regs->glpmcfg,
				 0, lpmcfg.d32);

	}
#endif
	if (core_if->core_params->ic_usb_cap) {
		gusbcfg_data_t gusbcfg = {.d32 = 0 };
		gusbcfg.b.ic_usb_cap = 1;
		DWC_MODIFY_REG32(&core_if->core_global_regs->gusbcfg,
				 0, gusbcfg.d32);
	}
	{
		gotgctl_data_t gotgctl = {.d32 = 0 };
		gotgctl.b.otgver = core_if->core_params->otg_ver;
		DWC_MODIFY_REG32(&core_if->core_global_regs->gotgctl, 0,
				 gotgctl.d32);
		/* Set OTG version supported */
		core_if->otg_ver = core_if->core_params->otg_ver;
		DWC_PRINTF("OTG VER PARAM: %d, OTG VER FLAG: %d\n",
			   core_if->core_params->otg_ver, core_if->otg_ver);
	}

	/* Enable common interrupts */
	dwc_otg_enable_common_interrupts(core_if);

	/* Do device or host intialization based on mode during PCD
	 * and HCD initialization  */
	if (dwc_otg_is_host_mode(core_if)) {
		DWC_PRINTF("^^^^^^^^^^^^^^^^^^Host Mode\n");
		core_if->op_state = A_HOST;
	} else {
		DWC_PRINTF("^^^^^^^^^^^^^^^^^Device Mode\n");
		core_if->op_state = B_PERIPHERAL;
#ifdef DWC_DEVICE_ONLY
		dwc_otg_core_dev_init(core_if);
#endif
	}
}

/**
 * This function enables the Device mode interrupts.
 *
 * @param core_if Programming view of DWC_otg controller
 */
void dwc_otg_enable_device_interrupts(dwc_otg_core_if_t *core_if)
{
	gintmsk_data_t intr_mask = {.d32 = 0 };
	dwc_otg_core_global_regs_t *global_regs = core_if->core_global_regs;

	DWC_DEBUGPL(DBG_CIL, "%s()\n", __func__);

	/* Disable all interrupts. */
	DWC_WRITE_REG32(&global_regs->gintmsk, 0);

	/* Clear any pending interrupts */
	DWC_WRITE_REG32(&global_regs->gintsts, 0xFFFFFFFF);

	/* Enable the common interrupts */
	dwc_otg_enable_common_interrupts(core_if);

	/* Enable interrupts */
	intr_mask.b.usbreset = 1;
	intr_mask.b.enumdone = 1;
	/* Disable Disconnect interrupt in Device mode */
	intr_mask.b.disconnect = 0;

	if (!core_if->multiproc_int_enable) {
		intr_mask.b.inepintr = 1;
		intr_mask.b.outepintr = 1;
	}

	intr_mask.b.erlysuspend = 1;

	if (core_if->en_multiple_tx_fifo == 0)
		intr_mask.b.epmismatch = 1;

	/* intr_mask.b.incomplisoout = 1; */
	intr_mask.b.incomplisoin = 1;

/* Enable the ignore frame number for ISOC xfers - MAS */
/* Disable to support high bandwith ISOC transfers - manukz */
#if 0
#ifdef DWC_UTE_PER_IO
	if (core_if->dma_enable) {
		if (core_if->dma_desc_enable) {
			dctl_data_t dctl1 = {.d32 = 0 };
			dctl1.b.ifrmnum = 1;
			DWC_MODIFY_REG32(&core_if->dev_if->
					 dev_global_regs->dctl, 0, dctl1.d32);
			DWC_DEBUG("----Enabled Ignore frame number (0x%08x)",
				  DWC_READ_REG32(&core_if->
						 dev_if->dev_global_regs->
						 dctl));
		}
	}
#endif
#endif
#ifdef DWC_EN_ISOC
	if (core_if->dma_enable) {
		if (core_if->dma_desc_enable == 0) {
			if (core_if->pti_enh_enable) {
				dctl_data_t dctl = {.d32 = 0 };
				dctl.b.ifrmnum = 1;
				DWC_MODIFY_REG32(&core_if->dev_if->
						 dev_global_regs->dctl, 0,
						 dctl.d32);
			} else {
				intr_mask.b.incomplisoin = 1;
				intr_mask.b.incomplisoout = 1;
			}
		}
	} else {
		intr_mask.b.incomplisoin = 1;
		intr_mask.b.incomplisoout = 1;
	}
#endif /* DWC_EN_ISOC */

	/** @todo NGS: Should this be a module parameter? */
#ifdef USE_PERIODIC_EP
	intr_mask.b.isooutdrop = 1;
	intr_mask.b.eopframe = 1;
	intr_mask.b.incomplisoin = 1;
	intr_mask.b.incomplisoout = 1;
#endif

	DWC_MODIFY_REG32(&global_regs->gintmsk, intr_mask.d32, intr_mask.d32);

	DWC_DEBUGPL(DBG_CIL, "%s() gintmsk=%0x\n", __func__,
		    DWC_READ_REG32(&global_regs->gintmsk));
}

/**
 * This function initializes the DWC_otg controller registers for
 * device mode.
 *
 * @param core_if Programming view of DWC_otg controller
 *
 */
void dwc_otg_core_dev_init(dwc_otg_core_if_t *core_if)
{
	int i;
	dwc_otg_core_global_regs_t *global_regs = core_if->core_global_regs;
	dwc_otg_dev_if_t *dev_if = core_if->dev_if;
	dwc_otg_core_params_t *params = core_if->core_params;
	dcfg_data_t dcfg = {.d32 = 0 };
	depctl_data_t diepctl = {.d32 = 0 };
	grstctl_t resetctl = {.d32 = 0 };
	uint32_t rx_fifo_size;
	fifosize_data_t nptxfifosize;
	fifosize_data_t txfifosize;
	dthrctl_data_t dthrctl;
	fifosize_data_t ptxfifosize;
	/* uint16_t rxfsiz, nptxfsiz; */
	/* gdfifocfg_data_t gdfifocfg = {.d32 = 0 }; */
	/* hwcfg3_data_t hwcfg3 = {.d32 = 0 }; */
	gotgctl_data_t gotgctl = {.d32 = 0 };
	gahbcfg_data_t gahbcfg = {.d32 = 0 };

	/* Restart the Phy Clock */
	pcgcctl_data_t pcgcctl = {.d32 = 0 };
	/* Restart the Phy Clock */
	pcgcctl.b.stoppclk = 1;
	DWC_MODIFY_REG32(core_if->pcgcctl, pcgcctl.d32, 0);
	dwc_udelay(10);

	gahbcfg.b.hburstlen = DWC_GAHBCFG_INT_DMA_BURST_INCR8;
	DWC_MODIFY_REG32(&global_regs->gahbcfg, 0, gahbcfg.d32);

	/* Device configuration register */
	init_devspd(core_if);
	dcfg.d32 = DWC_READ_REG32(&dev_if->dev_global_regs->dcfg);
	dcfg.b.descdma = (core_if->dma_desc_enable) ? 1 : 0;
	dcfg.b.perfrint = DWC_DCFG_FRAME_INTERVAL_80;
	/* Enable Device OUT NAK in case of DDMA mode */
	if (core_if->core_params->dev_out_nak)
		dcfg.b.endevoutnak = 1;

	if (core_if->core_params->cont_on_bna) {
		dctl_data_t dctl = {.d32 = 0 };
		dctl.b.encontonbna = 1;
		DWC_MODIFY_REG32(&dev_if->dev_global_regs->dctl, 0, dctl.d32);
	}
	/** should be done before every reset */
	if (core_if->otg_ver) {
		core_if->otg_sts = 0;
		gotgctl.b.devhnpen = 1;
		DWC_MODIFY_REG32(&core_if->core_global_regs->gotgctl,
				 gotgctl.d32, 0);
	}

	DWC_WRITE_REG32(&dev_if->dev_global_regs->dcfg, dcfg.d32);

	/* Configure data FIFO sizes */

	if (core_if->hwcfg2.b.dynamic_fifo && params->enable_dynamic_fifo) {
#ifdef DWC_UTE_CFI
		core_if->pwron_rxfsiz = DWC_READ_REG32(&global_regs->grxfsiz);
		core_if->init_rxfsiz = params->dev_rx_fifo_size;
#endif
		DWC_DEBUGPL(DBG_CIL, "initial grxfsiz=%08x\n",
			    DWC_READ_REG32(&global_regs->grxfsiz));

		/** Set Periodic Tx FIFO Mask all bits 0 */
		core_if->p_tx_msk = 0;

		/** Set Tx FIFO Mask all bits 0 */
		core_if->tx_msk = 0;
		/* core_if->en_multiple_tx_fifo equals
		 * core_if->hwcfg4.b.ded_fifo_en,
		 * and ded_fifo_en is 1 in default*/
		if (core_if->en_multiple_tx_fifo == 0) {
			/* Non-periodic Tx FIFO */
			DWC_DEBUGPL(DBG_CIL, "initial gnptxfsiz=%08x\n",
				    DWC_READ_REG32(&global_regs->gnptxfsiz));

			nptxfifosize.b.depth = params->dev_nperio_tx_fifo_size;
			nptxfifosize.b.startaddr = params->dev_rx_fifo_size;

			DWC_WRITE_REG32(&global_regs->gnptxfsiz,
					nptxfifosize.d32);

			DWC_DEBUGPL(DBG_CIL, "new gnptxfsiz=%08x\n",
				    DWC_READ_REG32(&global_regs->gnptxfsiz));

			/**@todo NGS: Fix Periodic FIFO Sizing! */
			/*
			 * Periodic Tx FIFOs These FIFOs are numbered from 1 to 15.
			 * Indexes of the FIFO size module parameters in the
			 * dev_perio_tx_fifo_size array and the FIFO size registers in
			 * the dptxfsiz array run from 0 to 14.
			 */
			/** @todo Finish debug of this */
			ptxfifosize.b.startaddr =
			    nptxfifosize.b.startaddr + nptxfifosize.b.depth;
			for (i = 0; i < core_if->hwcfg4.b.num_dev_perio_in_ep;
			     i++) {
				ptxfifosize.b.depth =
				    params->dev_perio_tx_fifo_size[i];
				DWC_DEBUGPL(DBG_CIL,
					    "initial dtxfsiz[%d]=%08x\n", i,
					    DWC_READ_REG32(&global_regs->
							   dtxfsiz[i]));
				DWC_WRITE_REG32(&global_regs->dtxfsiz[i],
						ptxfifosize.d32);
				DWC_DEBUGPL(DBG_CIL, "new dtxfsiz[%d]=%08x\n",
					    i,
					    DWC_READ_REG32(&global_regs->
							   dtxfsiz[i]));
				ptxfifosize.b.startaddr += ptxfifosize.b.depth;
			}
		} else {
			/*
			 * Tx FIFOs These FIFOs are numbered from 1 to 15.
			 * Indexes of the FIFO size module parameters in the
			 * dev_tx_fifo_size array and the FIFO size registers in
			 * the dtxfsiz array run from 0 to 14.
			 */

			/* Non-periodic Tx FIFO */
			DWC_DEBUGPL(DBG_CIL, "initial gnptxfsiz=%08x\n",
				    DWC_READ_REG32(&global_regs->gnptxfsiz));

#ifdef DWC_UTE_CFI
			core_if->pwron_gnptxfsiz =
			    (DWC_READ_REG32(&global_regs->gnptxfsiz) >> 16);
			core_if->init_gnptxfsiz =
			    params->dev_nperio_tx_fifo_size;
#endif
			rx_fifo_size = params->dev_rx_fifo_size;
			DWC_WRITE_REG32(&global_regs->grxfsiz, rx_fifo_size);
			DWC_DEBUGPL(DBG_CIL, "new grxfsiz=%08x\n",
				    DWC_READ_REG32(&global_regs->grxfsiz));

			nptxfifosize.b.depth = params->dev_nperio_tx_fifo_size;
			nptxfifosize.b.startaddr = params->dev_rx_fifo_size;
			DWC_WRITE_REG32(&global_regs->gnptxfsiz,
					nptxfifosize.d32);

			DWC_DEBUGPL(DBG_CIL, "new gnptxfsiz=%08x\n",
				    DWC_READ_REG32(&global_regs->gnptxfsiz));

			txfifosize.b.startaddr =
			    nptxfifosize.b.startaddr + nptxfifosize.b.depth;

			for (i = 0; i < core_if->hwcfg4.b.num_in_eps; i++) {

				txfifosize.b.depth =
				    params->dev_tx_fifo_size[i];

				DWC_DEBUGPL(DBG_CIL,
					    "initial dtxfsiz[%d]=%08x\n",
					    i,
					    DWC_READ_REG32(&global_regs->dtxfsiz
							   [i]));

#ifdef DWC_UTE_CFI
				core_if->pwron_txfsiz[i] =
				    (DWC_READ_REG32
				     (&global_regs->dtxfsiz[i]) >> 16);
				core_if->init_txfsiz[i] =
				    params->dev_tx_fifo_size[i];
#endif
				DWC_WRITE_REG32(&global_regs->dtxfsiz[i],
						txfifosize.d32);

				DWC_DEBUGPL(DBG_CIL,
					    "new dtxfsiz[%d]=%08x\n",
					    i,
					    DWC_READ_REG32(&global_regs->dtxfsiz
							   [i]));

				txfifosize.b.startaddr += txfifosize.b.depth;
			}
#if 0
			/* Calculating DFIFOCFG for Device mode to include RxFIFO and NPTXFIFO
			 * Before 3.00a EpInfoBase was being configured in ep enable/disable
			 * routine as well. Starting from 3.00a it will be set to the end of
			 * allocated FIFO space here due to ep 0 OUT always keeping enabled
			 */
			gdfifocfg.d32 = DWC_READ_REG32(&global_regs->gdfifocfg);
			hwcfg3.d32 = DWC_READ_REG32(&global_regs->ghwcfg3);
			gdfifocfg.b.gdfifocfg =
			    (DWC_READ_REG32(&global_regs->ghwcfg3) >> 16);
			DWC_WRITE_REG32(&global_regs->gdfifocfg, gdfifocfg.d32);
			if (core_if->snpsid <= OTG_CORE_REV_2_94a) {
				rxfsiz =
				    (DWC_READ_REG32(&global_regs->grxfsiz) &
				     0x0000ffff);
				nptxfsiz =
				    (DWC_READ_REG32(&global_regs->gnptxfsiz) >>
				     16);
				gdfifocfg.b.epinfobase = rxfsiz + nptxfsiz;
			} else {
				gdfifocfg.b.epinfobase = txfifosize.b.startaddr;
			}
			/* DWC_WRITE_REG32(&global_regs->gdfifocfg, gdfifocfg.d32); */
#endif
		}
	}

	/* Flush the FIFOs */
	dwc_otg_flush_tx_fifo(core_if, 0x10);	/* all Tx FIFOs */
	dwc_otg_flush_rx_fifo(core_if);

	/* Flush the Learning Queue. */
	resetctl.b.intknqflsh = 1;
	DWC_WRITE_REG32(&core_if->core_global_regs->grstctl, resetctl.d32);

	if (!core_if->core_params->en_multiple_tx_fifo && core_if->dma_enable) {
		core_if->start_predict = 0;
		for (i = 0; i <= core_if->dev_if->num_in_eps; ++i)
			core_if->nextep_seq[i] = 0xff;	/* 0xff - EP not active */
		core_if->nextep_seq[0] = 0;
		core_if->first_in_nextep_seq = 0;
		diepctl.d32 = DWC_READ_REG32(&dev_if->in_ep_regs[0]->diepctl);
		diepctl.b.nextep = 0;
		DWC_WRITE_REG32(&dev_if->in_ep_regs[0]->diepctl, diepctl.d32);

		/* Update IN Endpoint Mismatch Count
		 * by active IN NP EP count + 1 */
		dcfg.d32 = DWC_READ_REG32(&dev_if->dev_global_regs->dcfg);
		dcfg.b.epmscnt = 2;
		DWC_WRITE_REG32(&dev_if->dev_global_regs->dcfg, dcfg.d32);

		DWC_DEBUGPL(DBG_CILV,
			    "%s first_in_nextep_seq= %2d; nextep_seq[]:\n",
			    __func__, core_if->first_in_nextep_seq);
		for (i = 0; i <= core_if->dev_if->num_in_eps; i++)
			DWC_DEBUGPL(DBG_CILV, "%2d ", core_if->nextep_seq[i]);
		DWC_DEBUGPL(DBG_CILV, "\n");
	}

	/* Clear all pending Device Interrupts
	 * @todo - if the condition needed to be checked
	 * or in any case all pending interrutps should be cleared?
	 */
	if (core_if->multiproc_int_enable) {
		for (i = 0; i < core_if->dev_if->num_in_eps; ++i) {
			DWC_WRITE_REG32(&dev_if->
					dev_global_regs->diepeachintmsk[i], 0);
		}

		for (i = 0; i < core_if->dev_if->num_out_eps; ++i) {
			DWC_WRITE_REG32(&dev_if->
					dev_global_regs->doepeachintmsk[i], 0);
		}

		DWC_WRITE_REG32(&dev_if->dev_global_regs->deachint, 0xFFFFFFFF);
		DWC_WRITE_REG32(&dev_if->dev_global_regs->deachintmsk, 0);
	} else {
		DWC_WRITE_REG32(&dev_if->dev_global_regs->diepmsk, 0);
		DWC_WRITE_REG32(&dev_if->dev_global_regs->doepmsk, 0);
		DWC_WRITE_REG32(&dev_if->dev_global_regs->daint, 0xFFFFFFFF);
		DWC_WRITE_REG32(&dev_if->dev_global_regs->daintmsk, 0);
	}

	for (i = 0; i <= dev_if->num_in_eps; i++) {
		depctl_data_t depctl;
		depctl.d32 = DWC_READ_REG32(&dev_if->in_ep_regs[i]->diepctl);
		if (depctl.b.epena) {
			depctl.d32 = 0;
			depctl.b.epdis = 1;
			depctl.b.snak = 1;
		} else {
			depctl.d32 = 0;
		}

		DWC_WRITE_REG32(&dev_if->in_ep_regs[i]->diepctl, depctl.d32);

		DWC_WRITE_REG32(&dev_if->in_ep_regs[i]->dieptsiz, 0);
		DWC_WRITE_REG32(&dev_if->in_ep_regs[i]->diepdma, 0);
		DWC_WRITE_REG32(&dev_if->in_ep_regs[i]->diepint, 0xFF);
	}

	for (i = 1; i <= dev_if->num_out_eps; i++) {
		depctl_data_t depctl;
		depctl.d32 = DWC_READ_REG32(&dev_if->out_ep_regs[i]->doepctl);
		if (depctl.b.epena) {
			int j = 0;
			dctl_data_t dctl = {.d32 = 0 };
			gintmsk_data_t gintsts = {.d32 = 0 };
			doepint_data_t doepint = {.d32 = 0 };
			dctl.b.sgoutnak = 1;
			DWC_MODIFY_REG32(&core_if->dev_if->dev_global_regs->
					 dctl, 0, dctl.d32);
			do {
				j++;
				dwc_udelay(10);
				gintsts.d32 =
				    DWC_READ_REG32(&core_if->core_global_regs->
						   gintsts);
				if (j == 100000) {
					DWC_ERROR
					    ("SNAK as not set during 10s\n");
					break;
				}
			} while (!gintsts.b.goutnakeff);
			gintsts.d32 = 0;
			gintsts.b.goutnakeff = 1;
			DWC_WRITE_REG32(&core_if->core_global_regs->gintsts,
					gintsts.d32);

			depctl.d32 = 0;
			depctl.b.epdis = 1;
			depctl.b.snak = 1;
			j = 0;
			DWC_WRITE_REG32(&core_if->dev_if->out_ep_regs[i]->
					doepctl, depctl.d32);
			do {
				dwc_udelay(10);
				doepint.d32 =
				    DWC_READ_REG32(&core_if->
						   dev_if->out_ep_regs[i]->
						   doepint);
				if (j++ >= 10000) {
					DWC_ERROR
					    ("EPDIS was not set during 1s\n");
					break;
				}
			} while (!doepint.b.epdisabled);

			doepint.b.epdisabled = 1;
			DWC_WRITE_REG32(&core_if->dev_if->out_ep_regs[i]->
					doepint, doepint.d32);

			dctl.d32 = 0;
			dctl.b.cgoutnak = 1;
			DWC_MODIFY_REG32(&core_if->dev_if->dev_global_regs->
					 dctl, 0, dctl.d32);
		} else {
			depctl.d32 = 0;
		}

		DWC_WRITE_REG32(&dev_if->out_ep_regs[i]->doepctl, depctl.d32);
		DWC_WRITE_REG32(&dev_if->out_ep_regs[i]->doeptsiz, 0);
		DWC_WRITE_REG32(&dev_if->out_ep_regs[i]->doepdma, 0);
		DWC_WRITE_REG32(&dev_if->out_ep_regs[i]->doepint, 0xFF);
	}

	if (core_if->en_multiple_tx_fifo && core_if->dma_enable) {
		dev_if->non_iso_tx_thr_en = params->thr_ctl & 0x1;
		dev_if->iso_tx_thr_en = (params->thr_ctl >> 1) & 0x1;
		dev_if->rx_thr_en = (params->thr_ctl >> 2) & 0x1;

		dev_if->rx_thr_length = params->rx_thr_length;
		dev_if->tx_thr_length = params->tx_thr_length;

		dev_if->setup_desc_index = 0;

		dthrctl.d32 = 0;
		dthrctl.b.non_iso_thr_en = dev_if->non_iso_tx_thr_en;
		dthrctl.b.iso_thr_en = dev_if->iso_tx_thr_en;
		dthrctl.b.tx_thr_len = dev_if->tx_thr_length;
		dthrctl.b.rx_thr_en = dev_if->rx_thr_en;
		dthrctl.b.rx_thr_len = dev_if->rx_thr_length;
		dthrctl.b.ahb_thr_ratio = params->ahb_thr_ratio;

		DWC_WRITE_REG32(&dev_if->dev_global_regs->dtknqr3_dthrctl,
				dthrctl.d32);

		DWC_DEBUGPL(DBG_CIL,
			    "Non ISO Tx Thr - %d\nISO Tx Thr - %d\nRx Thr - %d\nTx Thr Len - %d\nRx Thr Len - %d\n",
			    dthrctl.b.non_iso_thr_en, dthrctl.b.iso_thr_en,
			    dthrctl.b.rx_thr_en, dthrctl.b.tx_thr_len,
			    dthrctl.b.rx_thr_len);

	}

	dwc_otg_enable_device_interrupts(core_if);

	{
		diepmsk_data_t msk = {.d32 = 0 };
		msk.b.txfifoundrn = 1;
		if (core_if->multiproc_int_enable) {
			DWC_MODIFY_REG32(&dev_if->
					 dev_global_regs->diepeachintmsk[0],
					 msk.d32, msk.d32);
		} else {
			DWC_MODIFY_REG32(&dev_if->dev_global_regs->diepmsk,
					 msk.d32, msk.d32);
		}
	}

	if (core_if->multiproc_int_enable) {
		/* Set NAK on Babble */
		dctl_data_t dctl = {.d32 = 0 };
		dctl.b.nakonbble = 1;
		DWC_MODIFY_REG32(&dev_if->dev_global_regs->dctl, 0, dctl.d32);
	}
}

/**
 * This function enables the Host mode interrupts.
 *
 * @param core_if Programming view of DWC_otg controller
 */
void dwc_otg_enable_host_interrupts(dwc_otg_core_if_t *core_if)
{
	dwc_otg_core_global_regs_t *global_regs = core_if->core_global_regs;
	gintmsk_data_t intr_mask = {.d32 = 0 };

	DWC_DEBUGPL(DBG_CIL, "%s()\n", __func__);

	/* Disable all interrupts. */
	DWC_WRITE_REG32(&global_regs->gintmsk, 0);

	/* Clear any pending interrupts. */
	DWC_WRITE_REG32(&global_regs->gintsts, 0xFFFFFFFF);

	/* Enable the common interrupts */
	dwc_otg_enable_common_interrupts(core_if);

	/*
	 * Enable host mode interrupts without disturbing common
	 * interrupts.
	 */

	intr_mask.b.disconnect = 1;
	intr_mask.b.portintr = 1;
	intr_mask.b.hcintr = 1;

	DWC_MODIFY_REG32(&global_regs->gintmsk, intr_mask.d32, intr_mask.d32);
}

/**
 * This function disables the Host Mode interrupts.
 *
 * @param core_if Programming view of DWC_otg controller
 */
void dwc_otg_disable_host_interrupts(dwc_otg_core_if_t *core_if)
{
	dwc_otg_core_global_regs_t *global_regs = core_if->core_global_regs;
	gintmsk_data_t intr_mask = {.d32 = 0 };

	DWC_DEBUGPL(DBG_CILV, "%s()\n", __func__);

	/*
	 * Disable host mode interrupts without disturbing common
	 * interrupts.
	 */
	intr_mask.b.sofintr = 1;
	intr_mask.b.portintr = 1;
	intr_mask.b.hcintr = 1;
	intr_mask.b.ptxfempty = 1;
	intr_mask.b.nptxfempty = 1;

	DWC_MODIFY_REG32(&global_regs->gintmsk, intr_mask.d32, 0);
	/* Clear pending interrupts */
	DWC_WRITE_REG32(&global_regs->gintsts, intr_mask.d32);
}

/**
 * This function initializes the DWC_otg controller registers for
 * host mode.
 *
 * This function flushes the Tx and Rx FIFOs and it flushes any entries in the
 * request queues. Host channels are reset to ensure that they are ready for
 * performing transfers.
 *
 * @param core_if Programming view of DWC_otg controller
 *
 */
void dwc_otg_core_host_init(dwc_otg_core_if_t *core_if)
{
	dwc_otg_core_global_regs_t *global_regs = core_if->core_global_regs;
	dwc_otg_host_if_t *host_if = core_if->host_if;
	dwc_otg_core_params_t *params = core_if->core_params;
	hprt0_data_t hprt0 = {.d32 = 0 };
	fifosize_data_t nptxfifosize;
	fifosize_data_t ptxfifosize;
	/* uint16_t rxfsiz, nptxfsiz, hptxfsiz; */
	/* gdfifocfg_data_t gdfifocfg = {.d32 = 0 }; */
	int i;
	hcchar_data_t hcchar;
	hcfg_data_t hcfg;
	hfir_data_t hfir;
	dwc_otg_hc_regs_t *hc_regs;
	int num_channels;
	gotgctl_data_t gotgctl = {.d32 = 0 };
	pcgcctl_data_t pcgcctl = {.d32 = 0 };
	struct dwc_otg_platform_data *pldata;
	pldata = core_if->otg_dev->pldata;

	DWC_DEBUGPL(DBG_CILV, "%s(%p)\n", __func__, core_if);

	/* Restart the Phy Clock */
	pcgcctl.b.stoppclk = 1;
	DWC_MODIFY_REG32(core_if->pcgcctl, pcgcctl.d32, 0);
	dwc_udelay(10);

	if ((core_if->otg_ver == 1) && (core_if->op_state == A_HOST)) {
		DWC_PRINTF("Init: Port Power? op_state=%d\n",
			   core_if->op_state);
		hprt0.d32 = dwc_otg_read_hprt0(core_if);
		DWC_PRINTF("Init: Power Port (%d)\n", hprt0.b.prtpwr);
		if (hprt0.b.prtpwr == 0) {
			hprt0.b.prtpwr = 1;
			DWC_WRITE_REG32(host_if->hprt0, hprt0.d32);
		}
	}

	/* Initialize Host Configuration Register */
	init_fslspclksel(core_if);
	if (core_if->core_params->speed == DWC_SPEED_PARAM_FULL) {
		hcfg.d32 = DWC_READ_REG32(&host_if->host_global_regs->hcfg);
		hcfg.b.fslssupp = 1;
		DWC_WRITE_REG32(&host_if->host_global_regs->hcfg, hcfg.d32);

	}

	/* This bit allows dynamic reloading of the HFIR register
	 * during runtime. This bit needs to be programmed during
	 * initial configuration and its value must not be changed
	 * during runtime.*/
	if (core_if->core_params->reload_ctl == 1) {
		hfir.d32 = DWC_READ_REG32(&host_if->host_global_regs->hfir);
		hfir.b.hfirrldctrl = 1;
		DWC_WRITE_REG32(&host_if->host_global_regs->hfir, hfir.d32);
	}

	if (core_if->core_params->dma_desc_enable) {
		uint8_t op_mode = core_if->hwcfg2.b.op_mode;
		if (!
		    (core_if->hwcfg4.b.desc_dma
		     && (core_if->snpsid >= OTG_CORE_REV_2_90a)
		     && ((op_mode == DWC_HWCFG2_OP_MODE_HNP_SRP_CAPABLE_OTG)
			 || (op_mode == DWC_HWCFG2_OP_MODE_SRP_ONLY_CAPABLE_OTG)
			 || (op_mode ==
			     DWC_HWCFG2_OP_MODE_NO_HNP_SRP_CAPABLE_OTG)
			 || (op_mode == DWC_HWCFG2_OP_MODE_SRP_CAPABLE_HOST)
			 || (op_mode ==
			     DWC_HWCFG2_OP_MODE_NO_SRP_CAPABLE_HOST)))) {

			DWC_ERROR("Host can't operate in Descriptor DMA mode.\n"
				  "Either core version is below 2.90a or "
				  "GHWCFG2, GHWCFG4 registers' values do not allow Descriptor DMA in host mode.\n"
				  "To run the driver in Buffer DMA host mode set dma_desc_enable "
				  "module parameter to 0.\n");
			return;
		}
		hcfg.d32 = DWC_READ_REG32(&host_if->host_global_regs->hcfg);
		hcfg.b.descdma = 1;
		DWC_WRITE_REG32(&host_if->host_global_regs->hcfg, hcfg.d32);
	}

	/* Configure data FIFO sizes */
	if (core_if->hwcfg2.b.dynamic_fifo && params->enable_dynamic_fifo) {
		DWC_DEBUGPL(DBG_CIL, "Total FIFO Size=%d\n",
			    core_if->total_fifo_size);
		DWC_DEBUGPL(DBG_CIL, "Rx FIFO Size=%d\n",
			    params->host_rx_fifo_size);
		DWC_DEBUGPL(DBG_CIL, "NP Tx FIFO Size=%d\n",
			    params->host_nperio_tx_fifo_size);
		DWC_DEBUGPL(DBG_CIL, "P Tx FIFO Size=%d\n",
			    params->host_perio_tx_fifo_size);

		/* Rx FIFO */
		DWC_DEBUGPL(DBG_CIL, "initial grxfsiz=%08x\n",
			    DWC_READ_REG32(&global_regs->grxfsiz));
		/* params->host_rx_fifo_size  */
		DWC_WRITE_REG32(&global_regs->grxfsiz, 0x0200);
		DWC_DEBUGPL(DBG_CIL, "new grxfsiz=%08x\n",
			    DWC_READ_REG32(&global_regs->grxfsiz));

		/* Non-periodic Tx FIFO */
		DWC_DEBUGPL(DBG_CIL, "initial gnptxfsiz=%08x\n",
			    DWC_READ_REG32(&global_regs->gnptxfsiz));
		/* params->host_nperio_tx_fifo_size */
		nptxfifosize.b.depth = 0x0080;
		/* params->host_rx_fifo_size */
		nptxfifosize.b.startaddr = 0x0200;
		DWC_WRITE_REG32(&global_regs->gnptxfsiz, nptxfifosize.d32);
		DWC_DEBUGPL(DBG_CIL, "new gnptxfsiz=%08x\n",
			    DWC_READ_REG32(&global_regs->gnptxfsiz));

		/* Periodic Tx FIFO */
		DWC_DEBUGPL(DBG_CIL, "initial hptxfsiz=%08x\n",
			    DWC_READ_REG32(&global_regs->hptxfsiz));
		/* params->host_perio_tx_fifo_size */
		ptxfifosize.b.depth = 0x0100;
		/* nptxfifosize.b.startaddr + nptxfifosize.b.depth */
		ptxfifosize.b.startaddr = 0x0280;
		DWC_WRITE_REG32(&global_regs->hptxfsiz, ptxfifosize.d32);
		DWC_DEBUGPL(DBG_CIL, "new hptxfsiz=%08x\n",
			    DWC_READ_REG32(&global_regs->hptxfsiz));
#if 0
		/* core_if->en_multiple_tx_fifo equals core_if->hwcfg4.b.ded_fifo_en,
		 * and ded_fifo_en is 1 in default
		 */
		if (core_if->en_multiple_tx_fifo) {
			/* Global DFIFOCFG calculation for Host mode
			 * - include RxFIFO, NPTXFIFO and HPTXFIFO */
			gdfifocfg.d32 = DWC_READ_REG32(&global_regs->gdfifocfg);
			rxfsiz =
			    (DWC_READ_REG32(&global_regs->grxfsiz) &
			     0x0000ffff);
			nptxfsiz =
			    (DWC_READ_REG32(&global_regs->gnptxfsiz) >> 16);
			hptxfsiz =
			    (DWC_READ_REG32(&global_regs->hptxfsiz) >> 16);
			gdfifocfg.b.epinfobase = rxfsiz + nptxfsiz + hptxfsiz;
			DWC_WRITE_REG32(&global_regs->gdfifocfg, gdfifocfg.d32);
		}
#endif
	}

	/* TODO - check this */
	/* Clear Host Set HNP Enable in the OTG Control Register */
	gotgctl.b.hstsethnpen = 1;
	DWC_MODIFY_REG32(&global_regs->gotgctl, gotgctl.d32, 0);
	/* Make sure the FIFOs are flushed
	 * all TX FIFOs */
	dwc_otg_flush_tx_fifo(core_if, 0x10);
	dwc_otg_flush_rx_fifo(core_if);

	/* Clear Host Set HNP Enable in the OTG Control Register */
	gotgctl.b.hstsethnpen = 1;
	DWC_MODIFY_REG32(&global_regs->gotgctl, gotgctl.d32, 0);

	if (!core_if->core_params->dma_desc_enable) {
		/* Flush out any leftover queued requests. */
		num_channels = core_if->core_params->host_channels;

		for (i = 0; i < num_channels; i++) {
			hc_regs = core_if->host_if->hc_regs[i];
			hcchar.d32 = DWC_READ_REG32(&hc_regs->hcchar);
			hcchar.b.chen = 0;
			hcchar.b.chdis = 1;
			hcchar.b.epdir = 0;
			DWC_WRITE_REG32(&hc_regs->hcchar, hcchar.d32);
		}

		/* Halt all channels to put them into a known state. */
		for (i = 0; i < num_channels; i++) {
			int count = 0;
			hc_regs = core_if->host_if->hc_regs[i];
			hcchar.d32 = DWC_READ_REG32(&hc_regs->hcchar);
			hcchar.b.chen = 1;
			hcchar.b.chdis = 1;
			hcchar.b.epdir = 0;
			DWC_WRITE_REG32(&hc_regs->hcchar, hcchar.d32);
			DWC_DEBUGPL(DBG_HCDV, "%s: Halt channel %d\n", __func__,
				    i);
			do {
				hcchar.d32 = DWC_READ_REG32(&hc_regs->hcchar);
				if (++count > 1000) {
					DWC_ERROR
					    ("%s: Unable to clear halt on channel %d\n",
					     __func__, i);
					break;
				}
				dwc_udelay(1);
			} while (hcchar.b.chen);
		}
	}

	/* Turn on the vbus power. */
	if ((core_if->otg_ver == 0) && (core_if->op_state == A_HOST)) {
		hprt0.d32 = dwc_otg_read_hprt0(core_if);
		DWC_PRINTF("Init: Power Port (%d)\n", hprt0.b.prtpwr);
		if (hprt0.b.prtpwr == 0) {
			hprt0.b.prtpwr = 1;
			DWC_WRITE_REG32(host_if->hprt0, hprt0.d32);
		}
		if (pldata->power_enable)
			pldata->power_enable(1);
	}

	dwc_otg_enable_host_interrupts(core_if);
}

/**
 * Prepares a host channel for transferring packets to/from a specific
 * endpoint. The HCCHARn register is set up with the characteristics specified
 * in _hc. Host channel interrupts that may need to be serviced while this
 * transfer is in progress are enabled.
 *
 * @param core_if Programming view of DWC_otg controller
 * @param hc Information needed to initialize the host channel
 */
void dwc_otg_hc_init(dwc_otg_core_if_t *core_if, dwc_hc_t *hc)
{
	uint32_t intr_enable;
	hcintmsk_data_t hc_intr_mask;
	gintmsk_data_t gintmsk = {.d32 = 0 };
	hcchar_data_t hcchar;
	hcsplt_data_t hcsplt;

	uint8_t hc_num = hc->hc_num;
	dwc_otg_host_if_t *host_if = core_if->host_if;
	dwc_otg_hc_regs_t *hc_regs = host_if->hc_regs[hc_num];

	/* Clear old interrupt conditions for this host channel. */
	hc_intr_mask.d32 = 0xFFFFFFFF;
	hc_intr_mask.b.reserved14_31 = 0;
	DWC_WRITE_REG32(&hc_regs->hcint, hc_intr_mask.d32);

	/* Enable channel interrupts required for this transfer. */
	hc_intr_mask.d32 = 0;
	hc_intr_mask.b.chhltd = 1;
	if (core_if->dma_enable) {
		/* For Descriptor DMA mode core halts the channel
		 * on AHB error. Interrupt is not required */
		if (!core_if->dma_desc_enable)
			hc_intr_mask.b.ahberr = 1;
		else {
			if (hc->ep_type == DWC_OTG_EP_TYPE_ISOC)
				hc_intr_mask.b.xfercompl = 1;
		}

		if (hc->error_state && !hc->do_split &&
		    hc->ep_type != DWC_OTG_EP_TYPE_ISOC) {
			hc_intr_mask.b.ack = 1;
			if (hc->ep_is_in) {
				hc_intr_mask.b.datatglerr = 1;
				if (hc->ep_type != DWC_OTG_EP_TYPE_INTR)
					hc_intr_mask.b.nak = 1;
			}
		}
	} else {
		switch (hc->ep_type) {
		case DWC_OTG_EP_TYPE_CONTROL:
		case DWC_OTG_EP_TYPE_BULK:
			hc_intr_mask.b.xfercompl = 1;
			hc_intr_mask.b.stall = 1;
			hc_intr_mask.b.xacterr = 1;
			hc_intr_mask.b.datatglerr = 1;
			if (hc->ep_is_in) {
				hc_intr_mask.b.bblerr = 1;
			} else {
				hc_intr_mask.b.nak = 1;
				hc_intr_mask.b.nyet = 1;
				if (hc->do_ping)
					hc_intr_mask.b.ack = 1;
			}

			if (hc->do_split) {
				hc_intr_mask.b.nak = 1;
				if (hc->complete_split)
					hc_intr_mask.b.nyet = 1;
				else
					hc_intr_mask.b.ack = 1;
			}

			if (hc->error_state)
				hc_intr_mask.b.ack = 1;
			break;
		case DWC_OTG_EP_TYPE_INTR:
			hc_intr_mask.b.xfercompl = 1;
			hc_intr_mask.b.nak = 1;
			hc_intr_mask.b.stall = 1;
			hc_intr_mask.b.xacterr = 1;
			hc_intr_mask.b.datatglerr = 1;
			hc_intr_mask.b.frmovrun = 1;

			if (hc->ep_is_in)
				hc_intr_mask.b.bblerr = 1;
			if (hc->error_state)
				hc_intr_mask.b.ack = 1;
			if (hc->do_split) {
				if (hc->complete_split)
					hc_intr_mask.b.nyet = 1;
				else
					hc_intr_mask.b.ack = 1;
			}
			break;
		case DWC_OTG_EP_TYPE_ISOC:
			hc_intr_mask.b.xfercompl = 1;
			hc_intr_mask.b.frmovrun = 1;
			hc_intr_mask.b.ack = 1;

			if (hc->ep_is_in) {
				hc_intr_mask.b.xacterr = 1;
				hc_intr_mask.b.bblerr = 1;
			}
			break;
		}
	}
	DWC_WRITE_REG32(&hc_regs->hcintmsk, hc_intr_mask.d32);

	/* Enable the top level host channel interrupt. */
	intr_enable = (1 << hc_num);
	DWC_MODIFY_REG32(&host_if->host_global_regs->haintmsk, 0, intr_enable);

	/* Make sure host channel interrupts are enabled. */
	gintmsk.b.hcintr = 1;
	DWC_MODIFY_REG32(&core_if->core_global_regs->gintmsk, 0, gintmsk.d32);

	/*
	 * Program the HCCHARn register with the endpoint characteristics for
	 * the current transfer.
	 */
	hcchar.d32 = 0;
	hcchar.b.devaddr = hc->dev_addr;
	hcchar.b.epnum = hc->ep_num;
	hcchar.b.epdir = hc->ep_is_in;
	hcchar.b.lspddev = (hc->speed == DWC_OTG_EP_SPEED_LOW);
	hcchar.b.eptype = hc->ep_type;
	hcchar.b.mps = hc->max_packet;

	DWC_WRITE_REG32(&host_if->hc_regs[hc_num]->hcchar, hcchar.d32);

	DWC_DEBUGPL(DBG_HCDV, "%s: Channel %d\n", __func__, hc->hc_num);
	DWC_DEBUGPL(DBG_HCDV, "	 Dev Addr: %d\n", hcchar.b.devaddr);
	DWC_DEBUGPL(DBG_HCDV, "	 Ep Num: %d\n", hcchar.b.epnum);
	DWC_DEBUGPL(DBG_HCDV, "	 Is In: %d\n", hcchar.b.epdir);
	DWC_DEBUGPL(DBG_HCDV, "	 Is Low Speed: %d\n", hcchar.b.lspddev);
	DWC_DEBUGPL(DBG_HCDV, "	 Ep Type: %d\n", hcchar.b.eptype);
	DWC_DEBUGPL(DBG_HCDV, "	 Max Pkt: %d\n", hcchar.b.mps);
	DWC_DEBUGPL(DBG_HCDV, "	 Multi Cnt: %d\n", hcchar.b.multicnt);

	/*
	 * Program the HCSPLIT register for SPLITs
	 */
	hcsplt.d32 = 0;
	if (hc->do_split) {
		DWC_DEBUGPL(DBG_HCDV, "Programming HC %d with split --> %s\n",
			    hc->hc_num,
			    hc->complete_split ? "CSPLIT" : "SSPLIT");
		hcsplt.b.compsplt = hc->complete_split;
		hcsplt.b.xactpos = hc->xact_pos;
		hcsplt.b.hubaddr = hc->hub_addr;
		hcsplt.b.prtaddr = hc->port_addr;
		DWC_DEBUGPL(DBG_HCDV, "	  comp split %d\n", hc->complete_split);
		DWC_DEBUGPL(DBG_HCDV, "	  xact pos %d\n", hc->xact_pos);
		DWC_DEBUGPL(DBG_HCDV, "	  hub addr %d\n", hc->hub_addr);
		DWC_DEBUGPL(DBG_HCDV, "	  port addr %d\n", hc->port_addr);
		DWC_DEBUGPL(DBG_HCDV, "	  is_in %d\n", hc->ep_is_in);
		DWC_DEBUGPL(DBG_HCDV, "	  Max Pkt: %d\n", hcchar.b.mps);
		DWC_DEBUGPL(DBG_HCDV, "	  xferlen: %d\n", hc->xfer_len);
	}
	DWC_WRITE_REG32(&host_if->hc_regs[hc_num]->hcsplt, hcsplt.d32);

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
 *
 * @param core_if Controller register interface.
 * @param hc Host channel to halt.
 * @param halt_status Reason for halting the channel.
 */
void dwc_otg_hc_halt(dwc_otg_core_if_t *core_if,
		     dwc_hc_t *hc, dwc_otg_halt_status_e halt_status)
{
	gnptxsts_data_t nptxsts;
	hptxsts_data_t hptxsts;
	hcchar_data_t hcchar;
	dwc_otg_hc_regs_t *hc_regs;
	dwc_otg_core_global_regs_t *global_regs;
	dwc_otg_host_global_regs_t *host_global_regs;

	hc_regs = core_if->host_if->hc_regs[hc->hc_num];
	global_regs = core_if->core_global_regs;
	host_global_regs = core_if->host_if->host_global_regs;

	DWC_ASSERT(!(halt_status == DWC_OTG_HC_XFER_NO_HALT_STATUS),
		   "halt_status = %d\n", halt_status);

	if (halt_status == DWC_OTG_HC_XFER_URB_DEQUEUE ||
	    halt_status == DWC_OTG_HC_XFER_AHB_ERR) {
		/*
		 * Disable all channel interrupts except Ch Halted. The QTD
		 * and QH state associated with this transfer has been cleared
		 * (in the case of URB_DEQUEUE), so the channel needs to be
		 * shut down carefully to prevent crashes.
		 */
		hcintmsk_data_t hcintmsk;
		hcintmsk.d32 = 0;
		hcintmsk.b.chhltd = 1;
		DWC_WRITE_REG32(&hc_regs->hcintmsk, hcintmsk.d32);

		/*
		 * Make sure no other interrupts besides halt are currently
		 * pending. Handling another interrupt could cause a crash due
		 * to the QTD and QH state.
		 */
		DWC_WRITE_REG32(&hc_regs->hcint, ~hcintmsk.d32);

		/*
		 * Make sure the halt status is set to URB_DEQUEUE or AHB_ERR
		 * even if the channel was already halted for some other
		 * reason.
		 */
		hc->halt_status = halt_status;

		hcchar.d32 = DWC_READ_REG32(&hc_regs->hcchar);
		if ((hcchar.b.chen == 0) ||
		    (!hc->do_split && !core_if->hc_halt_quirk &&
		    ((hc->ep_type == DWC_OTG_EP_TYPE_ISOC) ||
		    (hc->ep_type == DWC_OTG_EP_TYPE_INTR)))) {
			/*
			 * hcchar.b.chen is 0 means that:
			 * The channel is either already halted or it hasn't
			 * started yet. In DMA mode, the transfer may halt if
			 * it finishes normally or a condition occurs that
			 * requires driver intervention. Don't want to halt
			 * the channel again. In either Slave or DMA mode,
			 * it's possible that the transfer has been assigned
			 * to a channel, but not started yet when an URB is
			 * dequeued. Don't want to halt a channel that hasn't
			 * started yet.
			 * If channel is used for non-split periodic transfer
			 * according to DWC Programming Guide:
			 * '3.5 Halting a Channel': Channel disable must not
			 * be programmed for non-split periodic channels. At
			 * the end of the next uframe/frame (in the worst
			 * case), the core generates a channel halted and
			 * disables the channel automatically.
			 */
			DWC_PRINTF("%s: hcchar.b.chen %d, ep_type %d\n",
				   __func__, hcchar.b.chen, hc->ep_type);
			return;
		}
	}
	if (hc->halt_pending) {
		/*
		 * A halt has already been issued for this channel. This might
		 * happen when a transfer is aborted by a higher level in
		 * the stack.
		 */
#ifdef DEBUG
		DWC_PRINTF
		    ("*** %s: Channel %d, _hc->halt_pending already set ***\n",
		     __func__, hc->hc_num);

#endif
		return;
	}

	hcchar.d32 = DWC_READ_REG32(&hc_regs->hcchar);

	/* No need to set the bit in DDMA for disabling the channel
	 * TODO check it everywhere channel is disabled */
	if (!core_if->core_params->dma_desc_enable)
		hcchar.b.chen = 1;
	hcchar.b.chdis = 1;

	if (!core_if->dma_enable) {
		/* Check for space in the request queue to issue the halt. */
		if (hc->ep_type == DWC_OTG_EP_TYPE_CONTROL ||
		    hc->ep_type == DWC_OTG_EP_TYPE_BULK) {
			nptxsts.d32 = DWC_READ_REG32(&global_regs->gnptxsts);
			if (nptxsts.b.nptxqspcavail == 0)
				hcchar.b.chen = 0;
		} else {
			hptxsts.d32 =
			    DWC_READ_REG32(&host_global_regs->hptxsts);
			if ((hptxsts.b.ptxqspcavail == 0)
			    || (core_if->queuing_high_bandwidth)) {
				hcchar.b.chen = 0;
			}
		}
	}
	DWC_WRITE_REG32(&hc_regs->hcchar, hcchar.d32);

	hc->halt_status = halt_status;

	if (hcchar.b.chen) {
		hc->halt_pending = 1;
		hc->halt_on_queue = 0;
	} else {
		hc->halt_on_queue = 1;
	}

	DWC_DEBUGPL(DBG_HCDV, "%s: Channel %d\n", __func__, hc->hc_num);
	DWC_DEBUGPL(DBG_HCDV, "	 hcchar: 0x%08x\n", hcchar.d32);
	DWC_DEBUGPL(DBG_HCDV, "	 halt_pending: %d\n", hc->halt_pending);
	DWC_DEBUGPL(DBG_HCDV, "	 halt_on_queue: %d\n", hc->halt_on_queue);
	DWC_DEBUGPL(DBG_HCDV, "	 halt_status: %d\n", hc->halt_status);

	return;
}

/**
 * Clears the transfer state for a host channel. This function is normally
 * called after a transfer is done and the host channel is being released.
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param hc Identifies the host channel to clean up.
 */
void dwc_otg_hc_cleanup(dwc_otg_core_if_t *core_if, dwc_hc_t *hc)
{
	dwc_otg_hc_regs_t *hc_regs;

	hc->xfer_started = 0;

	/*
	 * Clear channel interrupt enables and any unhandled channel interrupt
	 * conditions.
	 */
	hc_regs = core_if->host_if->hc_regs[hc->hc_num];
	DWC_WRITE_REG32(&hc_regs->hcintmsk, 0);
	DWC_WRITE_REG32(&hc_regs->hcint, 0xFFFFFFFF);
#ifdef DEBUG
	DWC_TIMER_CANCEL(core_if->hc_xfer_timer[hc->hc_num]);
#endif
}

/**
 * Sets the channel property that indicates in which frame a periodic transfer
 * should occur. This is always set to the _next_ frame. This function has no
 * effect on non-periodic transfers.
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param hc Identifies the host channel to set up and its properties.
 * @param hcchar Current value of the HCCHAR register for the specified host
 * channel.
 */
static inline void hc_set_even_odd_frame(dwc_otg_core_if_t *core_if,
					 dwc_hc_t *hc, hcchar_data_t *hcchar)
{
	if (hc->ep_type == DWC_OTG_EP_TYPE_INTR ||
	    hc->ep_type == DWC_OTG_EP_TYPE_ISOC) {
		hfnum_data_t hfnum;
		hfnum.d32 =
		    DWC_READ_REG32(&core_if->host_if->host_global_regs->hfnum);

		/* 1 if _next_ frame is odd, 0 if it's even */
		hcchar->b.oddfrm = (hfnum.b.frnum & 0x1) ? 0 : 1;
#ifdef DEBUG
		if (hc->ep_type == DWC_OTG_EP_TYPE_INTR && hc->do_split
		    && !hc->complete_split) {
			switch (hfnum.b.frnum & 0x7) {
			case 7:
				core_if->hfnum_7_samples++;
				core_if->hfnum_7_frrem_accum += hfnum.b.frrem;
				break;
			case 0:
				core_if->hfnum_0_samples++;
				core_if->hfnum_0_frrem_accum += hfnum.b.frrem;
				break;
			default:
				core_if->hfnum_other_samples++;
				core_if->hfnum_other_frrem_accum +=
				    hfnum.b.frrem;
				break;
			}
		}
#endif
	}
}

#ifdef DEBUG
void hc_xfer_timeout(void *ptr)
{
	hc_xfer_info_t *xfer_info = NULL;
	int hc_num = 0;

	if (ptr)
		xfer_info = (hc_xfer_info_t *) ptr;

	if (!xfer_info->hc) {
		DWC_ERROR("xfer_info->hc = %p\n", xfer_info->hc);
		return;
	}

	hc_num = xfer_info->hc->hc_num;
	DWC_WARN("%s: timeout on channel %d\n", __func__, hc_num);
	DWC_WARN("	start_hcchar_val 0x%08x\n",
		 xfer_info->core_if->start_hcchar_val[hc_num]);
}
#endif

void ep_xfer_timeout(void *ptr)
{
	ep_xfer_info_t *xfer_info = NULL;
	int ep_num = 0;
	dctl_data_t dctl = {.d32 = 0 };
	gintsts_data_t gintsts = {.d32 = 0 };
	gintmsk_data_t gintmsk = {.d32 = 0 };

	if (ptr)
		xfer_info = (ep_xfer_info_t *) ptr;

	if (!xfer_info->ep) {
		DWC_ERROR("xfer_info->ep = %p\n", xfer_info->ep);
		return;
	}

	ep_num = xfer_info->ep->num;
	DWC_WARN("%s: timeout on endpoit %d\n", __func__, ep_num);
	/* Put the sate to 2 as it was time outed */
	xfer_info->state = 2;

	dctl.d32 =
	    DWC_READ_REG32(&xfer_info->core_if->dev_if->dev_global_regs->dctl);
	gintsts.d32 =
	    DWC_READ_REG32(&xfer_info->core_if->core_global_regs->gintsts);
	gintmsk.d32 =
	    DWC_READ_REG32(&xfer_info->core_if->core_global_regs->gintmsk);

	if (!gintmsk.b.goutnakeff) {
		/* Unmask it */
		gintmsk.b.goutnakeff = 1;
		DWC_WRITE_REG32(&xfer_info->core_if->core_global_regs->gintmsk,
				gintmsk.d32);

	}

	if (!gintsts.b.goutnakeff)
		dctl.b.sgoutnak = 1;

	DWC_WRITE_REG32(&xfer_info->core_if->dev_if->dev_global_regs->dctl,
			dctl.d32);

}

void set_pid_isoc(dwc_hc_t *hc)
{
	/* Set up the initial PID for the transfer. */
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
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param hc Information needed to initialize the host channel. The xfer_len
 * value may be reduced to accommodate the max widths of the XferSize and
 * PktCnt fields in the HCTSIZn register. The multi_count value may be changed
 * to reflect the final xfer_len value.
 */
void dwc_otg_hc_start_transfer(dwc_otg_core_if_t *core_if, dwc_hc_t *hc)
{
	hcchar_data_t hcchar;
	hctsiz_data_t hctsiz;
	uint16_t num_packets;
	uint32_t max_hc_xfer_size = core_if->core_params->max_transfer_size;
	uint16_t max_hc_pkt_count = core_if->core_params->max_packet_count;
	dwc_otg_hc_regs_t *hc_regs = core_if->host_if->hc_regs[hc->hc_num];

	hctsiz.d32 = 0;

	if (hc->do_ping) {
		if (!core_if->dma_enable) {
			dwc_otg_hc_do_ping(core_if, hc);
			hc->xfer_started = 1;
			return;
		} else {
			hctsiz.b.dopng = 1;
		}
	}

	if (hc->do_split) {
		num_packets = 1;

		if (hc->complete_split && !hc->ep_is_in) {
			/* For CSPLIT OUT Transfer, set the size to 0 so the
			 * core doesn't expect any data written to the FIFO */
			hc->xfer_len = 0;
		} else if (hc->ep_is_in || (hc->xfer_len > hc->max_packet)) {
			hc->xfer_len = hc->max_packet;
		} else if (!hc->ep_is_in && (hc->xfer_len > 188)) {
			hc->xfer_len = 188;
		}

		hctsiz.b.xfersize = hc->xfer_len;
	} else {
		/*
		 * Ensure that the transfer length and packet count will fit
		 * in the widths allocated for them in the HCTSIZn register.
		 */
		if (hc->ep_type == DWC_OTG_EP_TYPE_INTR ||
		    hc->ep_type == DWC_OTG_EP_TYPE_ISOC) {
			/*
			 * Make sure the transfer size is no larger than one
			 * (micro)frame's worth of data. (A check was done
			 * when the periodic transfer was accepted to ensure
			 * that a (micro)frame's worth of data can be
			 * programmed into a channel.)
			 */
			uint32_t max_periodic_len =
			    hc->multi_count * hc->max_packet;
			if (hc->xfer_len > max_periodic_len)
				hc->xfer_len = max_periodic_len;
		} else if (hc->xfer_len > max_hc_xfer_size) {
			/* Make sure that xfer_len is a
			 * multiple of max packet size. */
			hc->xfer_len = max_hc_xfer_size - hc->max_packet + 1;
		}

		if (hc->xfer_len > 0) {
			num_packets =
			    (hc->xfer_len + hc->max_packet -
			     1) / hc->max_packet;
			if (num_packets > max_hc_pkt_count) {
				num_packets = max_hc_pkt_count;
				hc->xfer_len = num_packets * hc->max_packet;
			}
		} else {
			/* Need 1 packet for transfer length of 0. */
			num_packets = 1;
		}

		if (hc->ep_is_in) {
			/* Always program an integral # of max packets for IN transfers. */
			hc->xfer_len = num_packets * hc->max_packet;
		}

		if (hc->ep_type == DWC_OTG_EP_TYPE_INTR ||
		    hc->ep_type == DWC_OTG_EP_TYPE_ISOC) {
			/*
			 * Make sure that the multi_count field matches the
			 * actual transfer length.
			 */
			hc->multi_count = num_packets;
		}

		if (hc->ep_type == DWC_OTG_EP_TYPE_ISOC)
			set_pid_isoc(hc);

		hctsiz.b.xfersize = hc->xfer_len;
	}

	hc->start_pkt_count = num_packets;
	hctsiz.b.pktcnt = num_packets;
	hctsiz.b.pid = hc->data_pid_start;
	DWC_WRITE_REG32(&hc_regs->hctsiz, hctsiz.d32);

	DWC_DEBUGPL(DBG_HCDV, "%s: Channel %d\n", __func__, hc->hc_num);
	DWC_DEBUGPL(DBG_HCDV, "	 Xfer Size: %d\n", hctsiz.b.xfersize);
	DWC_DEBUGPL(DBG_HCDV, "	 Num Pkts: %d\n", hctsiz.b.pktcnt);
	DWC_DEBUGPL(DBG_HCDV, "	 Start PID: %d\n", hctsiz.b.pid);

	if (core_if->dma_enable) {
		dwc_dma_t dma_addr;
		if (hc->align_buff)
			dma_addr = hc->align_buff;
		else
			dma_addr = ((unsigned long)hc->xfer_buff & 0xffffffff);
		DWC_WRITE_REG32(&hc_regs->hcdma, dma_addr);
	}

	/* Start the split */
	if (hc->do_split) {
		hcsplt_data_t hcsplt;
		hcsplt.d32 = DWC_READ_REG32(&hc_regs->hcsplt);
		hcsplt.b.spltena = 1;
		DWC_WRITE_REG32(&hc_regs->hcsplt, hcsplt.d32);
	}

	hcchar.d32 = DWC_READ_REG32(&hc_regs->hcchar);
	hcchar.b.multicnt = hc->multi_count;
	hc_set_even_odd_frame(core_if, hc, &hcchar);
#ifdef DEBUG
	core_if->start_hcchar_val[hc->hc_num] = hcchar.d32;
	if (hcchar.b.chdis) {
		DWC_WARN("%s: chdis set, channel %d, hcchar 0x%08x\n",
			 __func__, hc->hc_num, hcchar.d32);
	}
#endif

	/* Set host channel enable after all other setup is complete. */
	hcchar.b.chen = 1;
	hcchar.b.chdis = 0;
	DWC_WRITE_REG32(&hc_regs->hcchar, hcchar.d32);

	hc->xfer_started = 1;
	hc->requests++;

	if (!core_if->dma_enable && !hc->ep_is_in && hc->xfer_len > 0) {
		/* Load OUT packet into the appropriate Tx FIFO. */
		dwc_otg_hc_write_packet(core_if, hc);
	}
#ifdef DEBUG
	if (hc->ep_type != DWC_OTG_EP_TYPE_INTR) {
		core_if->hc_xfer_info[hc->hc_num].core_if = core_if;
		core_if->hc_xfer_info[hc->hc_num].hc = hc;

		/* Start a timer for this transfer. */
		DWC_TIMER_SCHEDULE(core_if->hc_xfer_timer[hc->hc_num], 10000);
	}
#endif
}

/**
 * This function does the setup for a data transfer for a host channel
 * and starts the transfer in Descriptor DMA mode.
 *
 * Initializes HCTSIZ register. For a PING transfer the Do Ping bit is set.
 * Sets PID and NTD values. For periodic transfers
 * initializes SCHED_INFO field with micro-frame bitmap.
 *
 * Initializes HCDMA register with descriptor list address and CTD value
 * then starts the transfer via enabling the channel.
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param hc Information needed to initialize the host channel.
 */
void dwc_otg_hc_start_transfer_ddma(dwc_otg_core_if_t *core_if, dwc_hc_t *hc)
{
	dwc_otg_hc_regs_t *hc_regs = core_if->host_if->hc_regs[hc->hc_num];
	hcchar_data_t hcchar;
	hctsiz_data_t hctsiz;
	hcdma_data_t hcdma;

	hctsiz.d32 = 0;

	if (hc->do_ping)
		hctsiz.b_ddma.dopng = 1;

	if (hc->ep_type == DWC_OTG_EP_TYPE_ISOC)
		set_pid_isoc(hc);

	/* Packet Count and Xfer Size are not used in Descriptor DMA mode */
	hctsiz.b_ddma.pid = hc->data_pid_start;
	/* 0 - 1 descriptor, 1 - 2 descriptors, etc. */
	hctsiz.b_ddma.ntd = hc->ntd - 1;
	/* Non-zero only for high-speed interrupt endpoints */
	hctsiz.b_ddma.schinfo = hc->schinfo;

	DWC_DEBUGPL(DBG_HCDV, "%s: Channel %d\n", __func__, hc->hc_num);
	DWC_DEBUGPL(DBG_HCDV, "	 Start PID: %d\n", hctsiz.b.pid);
	DWC_DEBUGPL(DBG_HCDV, "	 NTD: %d\n", hctsiz.b_ddma.ntd);

	DWC_WRITE_REG32(&hc_regs->hctsiz, hctsiz.d32);

	hcdma.d32 = 0;
	hcdma.b.dma_addr = ((uint32_t) hc->desc_list_addr) >> 11;

	/* Always start from first descriptor. */
	hcdma.b.ctd = 0;
	DWC_WRITE_REG32(&hc_regs->hcdma, hcdma.d32);

	hcchar.d32 = DWC_READ_REG32(&hc_regs->hcchar);
	hcchar.b.multicnt = hc->multi_count;

#ifdef DEBUG
	core_if->start_hcchar_val[hc->hc_num] = hcchar.d32;
	if (hcchar.b.chdis) {
		DWC_WARN("%s: chdis set, channel %d, hcchar 0x%08x\n",
			 __func__, hc->hc_num, hcchar.d32);
	}
#endif

	/* Set host channel enable after all other setup is complete. */
	hcchar.b.chen = 1;
	hcchar.b.chdis = 0;

	DWC_WRITE_REG32(&hc_regs->hcchar, hcchar.d32);

	hc->xfer_started = 1;
	hc->requests++;

#ifdef DEBUG
	if ((hc->ep_type != DWC_OTG_EP_TYPE_INTR)
	    && (hc->ep_type != DWC_OTG_EP_TYPE_ISOC)) {
		core_if->hc_xfer_info[hc->hc_num].core_if = core_if;
		core_if->hc_xfer_info[hc->hc_num].hc = hc;
		/* Start a timer for this transfer. */
		DWC_TIMER_SCHEDULE(core_if->hc_xfer_timer[hc->hc_num], 10000);
	}
#endif

}

/**
 * This function continues a data transfer that was started by previous call
 * to <code>dwc_otg_hc_start_transfer</code>. The caller must ensure there is
 * sufficient space in the request queue and Tx Data FIFO. This function
 * should only be called in Slave mode. In DMA mode, the controller acts
 * autonomously to complete transfers programmed to a host channel.
 *
 * For an OUT transfer, a new data packet is loaded into the appropriate FIFO
 * if there is any data remaining to be queued. For an IN transfer, another
 * data packet is always requested. For the SETUP phase of a control transfer,
 * this function does nothing.
 *
 * @return 1 if a new request is queued, 0 if no more requests are required
 * for this transfer.
 */
int dwc_otg_hc_continue_transfer(dwc_otg_core_if_t *core_if, dwc_hc_t *hc)
{
	DWC_DEBUGPL(DBG_HCDV, "%s: Channel %d\n", __func__, hc->hc_num);

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
		hcchar_data_t hcchar;
		dwc_otg_hc_regs_t *hc_regs =
		    core_if->host_if->hc_regs[hc->hc_num];

		hcchar.d32 = DWC_READ_REG32(&hc_regs->hcchar);
		hc_set_even_odd_frame(core_if, hc, &hcchar);
		hcchar.b.chen = 1;
		hcchar.b.chdis = 0;
		DWC_DEBUGPL(DBG_HCDV, "	 IN xfer: hcchar = 0x%08x\n",
			    hcchar.d32);
		DWC_WRITE_REG32(&hc_regs->hcchar, hcchar.d32);
		hc->requests++;
		return 1;
	} else {
		/* OUT transfers. */
		if (hc->xfer_count < hc->xfer_len) {
			if (hc->ep_type == DWC_OTG_EP_TYPE_INTR ||
			    hc->ep_type == DWC_OTG_EP_TYPE_ISOC) {
				hcchar_data_t hcchar;
				dwc_otg_hc_regs_t *hc_regs;
				hc_regs = core_if->host_if->hc_regs[hc->hc_num];
				hcchar.d32 = DWC_READ_REG32(&hc_regs->hcchar);
				hc_set_even_odd_frame(core_if, hc, &hcchar);
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
 * Starts a PING transfer. This function should only be called in Slave mode.
 * The Do Ping bit is set in the HCTSIZ register, then the channel is enabled.
 */
void dwc_otg_hc_do_ping(dwc_otg_core_if_t *core_if, dwc_hc_t *hc)
{
	hcchar_data_t hcchar;
	hctsiz_data_t hctsiz;
	dwc_otg_hc_regs_t *hc_regs = core_if->host_if->hc_regs[hc->hc_num];

	DWC_DEBUGPL(DBG_HCDV, "%s: Channel %d\n", __func__, hc->hc_num);

	hctsiz.d32 = 0;
	hctsiz.b.dopng = 1;
	hctsiz.b.pktcnt = 1;
	DWC_WRITE_REG32(&hc_regs->hctsiz, hctsiz.d32);

	hcchar.d32 = DWC_READ_REG32(&hc_regs->hcchar);
	hcchar.b.chen = 1;
	hcchar.b.chdis = 0;
	DWC_WRITE_REG32(&hc_regs->hcchar, hcchar.d32);
}

/*
 * This function writes a packet into the Tx FIFO associated with the Host
 * Channel. For a channel associated with a non-periodic EP, the non-periodic
 * Tx FIFO is written. For a channel associated with a periodic EP, the
 * periodic Tx FIFO is written. This function should only be called in Slave
 * mode.
 *
 * Upon return the xfer_buff and xfer_count fields in _hc are incremented by
 * then number of bytes written to the Tx FIFO.
 */
void dwc_otg_hc_write_packet(dwc_otg_core_if_t *core_if, dwc_hc_t *hc)
{
	uint32_t i;
	uint32_t remaining_count;
	uint32_t byte_count;
	uint32_t dword_count;

	uint32_t *data_buff = (uint32_t *) (hc->xfer_buff);
	uint32_t *data_fifo = core_if->data_fifo[hc->hc_num];

	remaining_count = hc->xfer_len - hc->xfer_count;
	if (remaining_count > hc->max_packet)
		byte_count = hc->max_packet;
	else
		byte_count = remaining_count;

	dword_count = (byte_count + 3) / 4;

	if ((((unsigned long)data_buff) & 0x3) == 0) {
		/* xfer_buff is DWORD aligned. */
		for (i = 0; i < dword_count; i++, data_buff++)
			DWC_WRITE_REG32(data_fifo, *data_buff);
	} else {
		/* xfer_buff is not DWORD aligned. */
		for (i = 0; i < dword_count; i++, data_buff++) {
			uint32_t data;
			data =
			    (data_buff[0] | data_buff[1] << 8 | data_buff[2] <<
			     16 | data_buff[3] << 24);
			DWC_WRITE_REG32(data_fifo, data);
		}
	}

	hc->xfer_count += byte_count;
	hc->xfer_buff += byte_count;
}

/**
 * Gets the current USB frame number. This is the frame number from the last
 * SOF packet.
 */
uint32_t dwc_otg_get_frame_number(dwc_otg_core_if_t *core_if)
{
	dsts_data_t dsts;
	dsts.d32 = DWC_READ_REG32(&core_if->dev_if->dev_global_regs->dsts);

	/* read current frame/microframe number from DSTS register */
	return dsts.b.soffn;
}

/**
 * Calculates and gets the frame Interval value of HFIR register according PHY
 * type and speed.The application can modify a value of HFIR register only after
 * the Port Enable bit of the Host Port Control and Status register
 * (HPRT.PrtEnaPort) has been set.
*/

uint32_t calc_frame_interval(dwc_otg_core_if_t *core_if)
{
	gusbcfg_data_t usbcfg;
	hwcfg2_data_t hwcfg2;
	hprt0_data_t hprt0;
	int clock = 60;		/* default value */
	usbcfg.d32 = DWC_READ_REG32(&core_if->core_global_regs->gusbcfg);
	hwcfg2.d32 = DWC_READ_REG32(&core_if->core_global_regs->ghwcfg2);
	hprt0.d32 = DWC_READ_REG32(core_if->host_if->hprt0);
	if (!usbcfg.b.physel && usbcfg.b.ulpi_utmi_sel && !usbcfg.b.phyif)
		clock = 60;
	if (usbcfg.b.physel && hwcfg2.b.fs_phy_type == 3)
		clock = 48;
	if (!usbcfg.b.phylpwrclksel && !usbcfg.b.physel &&
	    !usbcfg.b.ulpi_utmi_sel && usbcfg.b.phyif)
		clock = 30;
	if (!usbcfg.b.phylpwrclksel && !usbcfg.b.physel &&
	    !usbcfg.b.ulpi_utmi_sel && !usbcfg.b.phyif)
		clock = 60;
	if (usbcfg.b.phylpwrclksel && !usbcfg.b.physel &&
	    !usbcfg.b.ulpi_utmi_sel && usbcfg.b.phyif)
		clock = 48;
	if (usbcfg.b.physel && !usbcfg.b.phyif && hwcfg2.b.fs_phy_type == 2)
		clock = 48;
	if (usbcfg.b.physel && hwcfg2.b.fs_phy_type == 1)
		clock = 48;
	if (hprt0.b.prtspd == 0)
		/* High speed case */
		return 125 * clock;
	else
		/* FS/LS case */
		return 1000 * clock;
}

/**
 * This function reads a setup packet from the Rx FIFO into the destination
 * buffer. This function is called from the Rx Status Queue Level (RxStsQLvl)
 * Interrupt routine when a SETUP packet has been received in Slave mode.
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param dest Destination buffer for packet data.
 */
void dwc_otg_read_setup_packet(dwc_otg_core_if_t *core_if, uint32_t *dest)
{
	device_grxsts_data_t status;
	/* Get the 8 bytes of a setup transaction data */

	/* Pop 2 DWORDS off the receive data FIFO into memory */
	dest[0] = DWC_READ_REG32(core_if->data_fifo[0]);
	dest[1] = DWC_READ_REG32(core_if->data_fifo[0]);
	if (core_if->snpsid >= OTG_CORE_REV_3_00a) {
		status.d32 =
		    DWC_READ_REG32(&core_if->core_global_regs->grxstsp);
		DWC_DEBUGPL(DBG_ANY,
			    "EP:%d BCnt:%d " "pktsts:%x Frame:%d(0x%0x)\n",
			    status.b.epnum, status.b.bcnt, status.b.pktsts,
			    status.b.fn, status.b.fn);
	}
}

/**
 * This function enables EP0 OUT to receive SETUP packets and configures EP0
 * IN for transmitting packets. It is normally called when the
 * "Enumeration Done" interrupt occurs.
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param ep The EP0 data.
 */
void dwc_otg_ep0_activate(dwc_otg_core_if_t *core_if, dwc_ep_t *ep)
{
	dwc_otg_dev_if_t *dev_if = core_if->dev_if;
	dsts_data_t dsts;
	depctl_data_t diepctl;
	depctl_data_t doepctl;
	dctl_data_t dctl = {.d32 = 0 };

	ep->stp_rollover = 0;
	/* Read the Device Status and Endpoint 0 Control registers */
	dsts.d32 = DWC_READ_REG32(&dev_if->dev_global_regs->dsts);
	diepctl.d32 = DWC_READ_REG32(&dev_if->in_ep_regs[0]->diepctl);
	doepctl.d32 = DWC_READ_REG32(&dev_if->out_ep_regs[0]->doepctl);

	/* Set the MPS of the IN EP based on the enumeration speed */
	switch (dsts.b.enumspd) {
	case DWC_DSTS_ENUMSPD_HS_PHY_30MHZ_OR_60MHZ:
	case DWC_DSTS_ENUMSPD_FS_PHY_30MHZ_OR_60MHZ:
	case DWC_DSTS_ENUMSPD_FS_PHY_48MHZ:
		diepctl.b.mps = DWC_DEP0CTL_MPS_64;
		break;
	case DWC_DSTS_ENUMSPD_LS_PHY_6MHZ:
		diepctl.b.mps = DWC_DEP0CTL_MPS_8;
		break;
	}

	DWC_WRITE_REG32(&dev_if->in_ep_regs[0]->diepctl, diepctl.d32);

	/* Enable OUT EP for receive */
	if (core_if->snpsid <= OTG_CORE_REV_2_94a) {
		doepctl.b.epena = 1;
		DWC_WRITE_REG32(&dev_if->out_ep_regs[0]->doepctl, doepctl.d32);
	}
#ifdef VERBOSE
	DWC_DEBUGPL(DBG_PCDV, "doepctl0=%0x\n",
		    DWC_READ_REG32(&dev_if->out_ep_regs[0]->doepctl));
	DWC_DEBUGPL(DBG_PCDV, "diepctl0=%0x\n",
		    DWC_READ_REG32(&dev_if->in_ep_regs[0]->diepctl));
#endif
	dctl.b.cgnpinnak = 1;

	DWC_MODIFY_REG32(&dev_if->dev_global_regs->dctl, dctl.d32, dctl.d32);
	DWC_DEBUGPL(DBG_PCDV, "dctl=%0x\n",
		    DWC_READ_REG32(&dev_if->dev_global_regs->dctl));

}

/**
 * This function activates an EP.  The Device EP control register for
 * the EP is configured as defined in the ep structure. Note: This
 * function is not used for EP0.
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param ep The EP to activate.
 */
void dwc_otg_ep_activate(dwc_otg_core_if_t *core_if, dwc_ep_t *ep)
{
	dwc_otg_dev_if_t *dev_if = core_if->dev_if;
	depctl_data_t depctl;
	volatile uint32_t *addr;
	daint_data_t daintmsk = {.d32 = 0 };
	dcfg_data_t dcfg;
	uint8_t i;

	DWC_DEBUGPL(DBG_PCDV, "%s() EP%d-%s\n", __func__, ep->num,
		    (ep->is_in ? "IN" : "OUT"));

#ifdef DWC_UTE_PER_IO
	ep->xiso_frame_num = 0xFFFFFFFF;
	ep->xiso_active_xfers = 0;
	ep->xiso_queued_xfers = 0;
#endif
	/* Read DEPCTLn register */
	if (ep->is_in == 1) {
		addr = &dev_if->in_ep_regs[ep->num]->diepctl;
		daintmsk.ep.in = 1 << ep->num;
	} else {
		addr = &dev_if->out_ep_regs[ep->num]->doepctl;
		daintmsk.ep.out = 1 << ep->num;
	}

	/* If the EP is already active don't change the EP Control
	 * register. */
	depctl.d32 = DWC_READ_REG32(addr);
	if (!depctl.b.usbactep) {
		depctl.b.mps = ep->maxpacket;
		depctl.b.eptype = ep->type;
		depctl.b.txfnum = ep->tx_fifo_num;

		if (ep->type == DWC_OTG_EP_TYPE_ISOC)
			depctl.b.setd0pid = 1;
		else
			depctl.b.setd0pid = 1;

		depctl.b.usbactep = 1;

		/* Update nextep_seq array and EPMSCNT in DCFG */
		if (!(depctl.b.eptype & 1) && (ep->is_in == 1)) {/*NP IN EP*/
			for (i = 0; i <= core_if->dev_if->num_in_eps; i++) {
				if (core_if->nextep_seq[i] ==
				    core_if->first_in_nextep_seq)
					break;
			}
			core_if->nextep_seq[i] = ep->num;
			core_if->nextep_seq[ep->num] =
			    core_if->first_in_nextep_seq;
			depctl.b.nextep = core_if->nextep_seq[ep->num];
			dcfg.d32 =
			    DWC_READ_REG32(&dev_if->dev_global_regs->dcfg);
			dcfg.b.epmscnt++;
			DWC_WRITE_REG32(&dev_if->dev_global_regs->dcfg,
					dcfg.d32);

			DWC_DEBUGPL(DBG_PCDV,
				    "%s first_in_nextep_seq= %2d; nextep_seq[]:\n",
				    __func__, core_if->first_in_nextep_seq);
			for (i = 0; i <= core_if->dev_if->num_in_eps; i++) {
				DWC_DEBUGPL(DBG_PCDV, "%2d\n",
					    core_if->nextep_seq[i]);
			}

		}

		DWC_WRITE_REG32(addr, depctl.d32);
		DWC_DEBUGPL(DBG_PCDV, "DEPCTL=%08x\n", DWC_READ_REG32(addr));
	}

	/* Enable the Interrupt for this EP */
	if (core_if->multiproc_int_enable) {
		if (ep->is_in == 1) {
			diepmsk_data_t diepmsk = {.d32 = 0 };
			diepmsk.b.xfercompl = 1;
			diepmsk.b.timeout = 1;
			diepmsk.b.epdisabled = 1;
			diepmsk.b.ahberr = 1;
			diepmsk.b.intknepmis = 1;
			if (!core_if->en_multiple_tx_fifo
			    && core_if->dma_enable)
				diepmsk.b.intknepmis = 0;
			diepmsk.b.txfifoundrn = 1;
			if (ep->type == DWC_OTG_EP_TYPE_ISOC)
				diepmsk.b.nak = 1;

			/*
			if (core_if->dma_desc_enable) {
				diepmsk.b.bna = 1;
			}

			if (core_if->dma_enable) {
				doepmsk.b.nak = 1;
			}
			*/
			DWC_WRITE_REG32(&dev_if->
					dev_global_regs->diepeachintmsk[ep->
									num],
					diepmsk.d32);

		} else {
			doepmsk_data_t doepmsk = {.d32 = 0 };
			doepmsk.b.xfercompl = 1;
			doepmsk.b.ahberr = 1;
			doepmsk.b.epdisabled = 1;
			if (ep->type == DWC_OTG_EP_TYPE_ISOC)
				doepmsk.b.outtknepdis = 1;

			/*
			if (core_if->dma_desc_enable) {
				doepmsk.b.bna = 1;
			}
			doepmsk.b.babble = 1;
			doepmsk.b.nyet = 1;
			doepmsk.b.nak = 1;
			*/

			DWC_WRITE_REG32(&dev_if->
					dev_global_regs->doepeachintmsk[ep->
									num],
					doepmsk.d32);
		}
		DWC_MODIFY_REG32(&dev_if->dev_global_regs->deachintmsk,
				 0, daintmsk.d32);
	} else {
		if (ep->type == DWC_OTG_EP_TYPE_ISOC) {
			if (ep->is_in) {
				diepmsk_data_t diepmsk = {.d32 = 0 };
				diepmsk.b.nak = 1;
				DWC_MODIFY_REG32(&dev_if->dev_global_regs->
						 diepmsk, 0, diepmsk.d32);
			} else {
				doepmsk_data_t doepmsk = {.d32 = 0 };
				doepmsk.b.outtknepdis = 1;
				DWC_MODIFY_REG32(&dev_if->dev_global_regs->
						 doepmsk, 0, doepmsk.d32);
			}
		}
		DWC_MODIFY_REG32(&dev_if->dev_global_regs->daintmsk,
				 0, daintmsk.d32);
	}

	DWC_DEBUGPL(DBG_PCDV, "DAINTMSK=%0x\n",
		    DWC_READ_REG32(&dev_if->dev_global_regs->daintmsk));

	ep->stall_clear_flag = 0;

	return;
}

/**
 * This function deactivates an EP. This is done by clearing the USB Active
 * EP bit in the Device EP control register. Note: This function is not used
 * for EP0. EP0 cannot be deactivated.
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param ep The EP to deactivate.
 */
void dwc_otg_ep_deactivate(dwc_otg_core_if_t *core_if, dwc_ep_t *ep)
{
	depctl_data_t depctl = {.d32 = 0 };
	volatile uint32_t *addr;
	daint_data_t daintmsk = {.d32 = 0 };
	dcfg_data_t dcfg;
	uint8_t i = 0;

#ifdef DWC_UTE_PER_IO
	ep->xiso_frame_num = 0xFFFFFFFF;
	ep->xiso_active_xfers = 0;
	ep->xiso_queued_xfers = 0;
#endif

	/* Read DEPCTLn register */
	if (ep->is_in == 1) {
		addr = &core_if->dev_if->in_ep_regs[ep->num]->diepctl;
		daintmsk.ep.in = 1 << ep->num;
	} else {
		addr = &core_if->dev_if->out_ep_regs[ep->num]->doepctl;
		daintmsk.ep.out = 1 << ep->num;
	}

	depctl.d32 = DWC_READ_REG32(addr);

	depctl.b.usbactep = 0;

	/* Update nextep_seq array and EPMSCNT in DCFG
	 * NP EP IN */
	if (!(depctl.b.eptype & 1) && ep->is_in == 1) {
		for (i = 0; i <= core_if->dev_if->num_in_eps; i++) {
			if (core_if->nextep_seq[i] == ep->num)
				break;
		}
		core_if->nextep_seq[i] = core_if->nextep_seq[ep->num];
		if (core_if->first_in_nextep_seq == ep->num)
			core_if->first_in_nextep_seq = i;
		core_if->nextep_seq[ep->num] = 0xff;
		depctl.b.nextep = 0;
		dcfg.d32 =
		    DWC_READ_REG32(&core_if->dev_if->dev_global_regs->dcfg);
		dcfg.b.epmscnt--;
		DWC_WRITE_REG32(&core_if->dev_if->dev_global_regs->dcfg,
				dcfg.d32);

		DWC_DEBUGPL(DBG_PCDV,
			    "%s first_in_nextep_seq= %2d; nextep_seq[]:\n",
			    __func__, core_if->first_in_nextep_seq);
		for (i = 0; i <= core_if->dev_if->num_in_eps; i++)
			DWC_DEBUGPL(DBG_PCDV, "%2d\n", core_if->nextep_seq[i]);
	}

	if (ep->is_in == 1)
		depctl.b.txfnum = 0;

	if (core_if->dma_desc_enable)
		depctl.b.epdis = 1;

	DWC_WRITE_REG32(addr, depctl.d32);
	depctl.d32 = DWC_READ_REG32(addr);
	if (core_if->dma_enable && ep->type == DWC_OTG_EP_TYPE_ISOC
	    && depctl.b.epena) {
		depctl_data_t depctl = {.d32 = 0 };
		if (ep->is_in) {
			diepint_data_t diepint = {.d32 = 0 };

			depctl.b.snak = 1;
			DWC_WRITE_REG32(&core_if->dev_if->
					in_ep_regs[ep->num]->diepctl,
					depctl.d32);
			do {
				dwc_udelay(10);
				diepint.d32 =
				    DWC_READ_REG32(&core_if->dev_if->
						   in_ep_regs[ep->
							      num]->diepint);
			} while (!diepint.b.inepnakeff);
			diepint.b.inepnakeff = 1;
			DWC_WRITE_REG32(&core_if->dev_if->
					in_ep_regs[ep->num]->diepint,
					diepint.d32);
			depctl.d32 = 0;
			depctl.b.epdis = 1;
			DWC_WRITE_REG32(&core_if->dev_if->
					in_ep_regs[ep->num]->diepctl,
					depctl.d32);
			do {
				dwc_udelay(10);
				diepint.d32 =
				    DWC_READ_REG32(&core_if->dev_if->
						   in_ep_regs[ep->
							      num]->diepint);
			} while (!diepint.b.epdisabled);
			diepint.b.epdisabled = 1;
			DWC_WRITE_REG32(&core_if->dev_if->
					in_ep_regs[ep->num]->diepint,
					diepint.d32);
		} else {
			dctl_data_t dctl = {.d32 = 0 };
			gintmsk_data_t gintsts = {.d32 = 0 };
			doepint_data_t doepint = {.d32 = 0 };
			dctl.b.sgoutnak = 1;
			DWC_MODIFY_REG32(&core_if->dev_if->
					 dev_global_regs->dctl, 0, dctl.d32);
			do {
				dwc_udelay(10);
				gintsts.d32 =
				    DWC_READ_REG32(&core_if->core_global_regs->
						   gintsts);
			} while (!gintsts.b.goutnakeff);
			gintsts.d32 = 0;
			gintsts.b.goutnakeff = 1;
			DWC_WRITE_REG32(&core_if->core_global_regs->gintsts,
					gintsts.d32);

			depctl.d32 = 0;
			depctl.b.epdis = 1;
			depctl.b.snak = 1;
			DWC_WRITE_REG32(&core_if->dev_if->out_ep_regs[ep->num]->
					doepctl, depctl.d32);
			do {
				dwc_udelay(10);
				doepint.d32 =
				    DWC_READ_REG32(&core_if->
						   dev_if->out_ep_regs[ep->
								       num]->
						   doepint);
			} while (!doepint.b.epdisabled);

			doepint.b.epdisabled = 1;
			DWC_WRITE_REG32(&core_if->dev_if->out_ep_regs[ep->num]->
					doepint, doepint.d32);

			dctl.d32 = 0;
			dctl.b.cgoutnak = 1;
			DWC_MODIFY_REG32(&core_if->dev_if->dev_global_regs->
					 dctl, 0, dctl.d32);
		}
	}

	/* Disable the Interrupt for this EP */
	if (core_if->multiproc_int_enable) {
		DWC_MODIFY_REG32(&core_if->dev_if->dev_global_regs->deachintmsk,
				 daintmsk.d32, 0);

		if (ep->is_in == 1) {
			DWC_WRITE_REG32(&core_if->dev_if->
					dev_global_regs->diepeachintmsk[ep->
									num],
					0);
		} else {
			DWC_WRITE_REG32(&core_if->dev_if->
					dev_global_regs->doepeachintmsk[ep->
									num],
					0);
		}
	} else {
		DWC_MODIFY_REG32(&core_if->dev_if->dev_global_regs->daintmsk,
				 daintmsk.d32, 0);
	}

}

/**
 * This function initializes dma descriptor chain.
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param ep The EP to start the transfer on.
 */
static void init_dma_desc_chain(dwc_otg_core_if_t *core_if, dwc_ep_t *ep)
{
	dwc_otg_dev_dma_desc_t *dma_desc;
	uint32_t offset;
	uint32_t xfer_est;
	int i;
	unsigned maxxfer_local, total_len;

	if (!ep->is_in && ep->type == DWC_OTG_EP_TYPE_INTR &&
	    (ep->maxpacket % 4)) {
		maxxfer_local = ep->maxpacket;
		total_len = ep->xfer_len;
	} else {
		maxxfer_local = ep->maxxfer;
		total_len = ep->total_len;
	}

	ep->desc_cnt = (total_len / maxxfer_local) +
	    ((total_len % maxxfer_local) ? 1 : 0);

	if (!ep->desc_cnt)
		ep->desc_cnt = 1;

	if (ep->desc_cnt > MAX_DMA_DESC_CNT)
		ep->desc_cnt = MAX_DMA_DESC_CNT;

	dma_desc = ep->desc_addr;
	if (maxxfer_local == ep->maxpacket) {
		if ((total_len % maxxfer_local) &&
		    (total_len / maxxfer_local < MAX_DMA_DESC_CNT)) {
			xfer_est = (ep->desc_cnt - 1) * maxxfer_local +
			    (total_len % maxxfer_local);
		} else
			xfer_est = ep->desc_cnt * maxxfer_local;
	} else
		xfer_est = total_len;
	offset = 0;
	for (i = 0; i < ep->desc_cnt; ++i) {
		/** DMA Descriptor Setup */
		if (xfer_est > maxxfer_local) {
			dma_desc->status.b.bs = BS_HOST_BUSY;
			dma_desc->status.b.l = 0;
			dma_desc->status.b.ioc = 0;
			dma_desc->status.b.sp = 0;
			dma_desc->status.b.bytes = maxxfer_local;
			dma_desc->buf = ep->dma_addr + offset;
			dma_desc->status.b.sts = 0;
			dma_desc->status.b.bs = BS_HOST_READY;

			xfer_est -= maxxfer_local;
			offset += maxxfer_local;
		} else {
			dma_desc->status.b.bs = BS_HOST_BUSY;
			dma_desc->status.b.l = 1;
			dma_desc->status.b.ioc = 1;
			if (ep->is_in) {
				dma_desc->status.b.sp =
				    (xfer_est %
				     ep->
				     maxpacket) ? 1 : ((ep->sent_zlp) ? 1 : 0);
				dma_desc->status.b.bytes = xfer_est;
			} else {
				if (maxxfer_local == ep->maxpacket)
					dma_desc->status.b.bytes = xfer_est;
				else
					dma_desc->status.b.bytes =
					    xfer_est +
					    ((4 - (xfer_est & 0x3)) & 0x3);
			}

			dma_desc->buf = ep->dma_addr + offset;
			dma_desc->status.b.sts = 0;
			dma_desc->status.b.bs = BS_HOST_READY;
		}
		dma_desc++;
	}
}

/**
 * This function is called when to write ISOC data into appropriate dedicated
 * periodic FIFO.
 */
static int32_t write_isoc_tx_fifo(dwc_otg_core_if_t *core_if,
				  dwc_ep_t *dwc_ep)
{
	dwc_otg_dev_if_t *dev_if = core_if->dev_if;
	dwc_otg_dev_in_ep_regs_t *ep_regs;
	dtxfsts_data_t txstatus = {.d32 = 0 };
	uint32_t len = 0;
	int epnum = dwc_ep->num;
	int dwords;

	DWC_DEBUGPL(DBG_PCD, "Dedicated TxFifo Empty: %d \n", epnum);

	ep_regs = core_if->dev_if->in_ep_regs[epnum];

	len = dwc_ep->xfer_len - dwc_ep->xfer_count;

	if (len > dwc_ep->maxpacket)
		len = dwc_ep->maxpacket;

	dwords = (len + 3) / 4;

	/* While there is space in the queue and space in the FIFO and
	 * More data to tranfer, Write packets to the Tx FIFO */
	txstatus.d32 = DWC_READ_REG32(&dev_if->in_ep_regs[epnum]->dtxfsts);
	DWC_DEBUGPL(DBG_PCDV, "b4 dtxfsts[%d]=0x%08x\n", epnum, txstatus.d32);

	while (txstatus.b.txfspcavail > dwords &&
	       dwc_ep->xfer_count < dwc_ep->xfer_len && dwc_ep->xfer_len != 0) {
		/* Write the FIFO */
		dwc_otg_ep_write_packet(core_if, dwc_ep, 0);

		len = dwc_ep->xfer_len - dwc_ep->xfer_count;
		if (len > dwc_ep->maxpacket)
			len = dwc_ep->maxpacket;

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
 * This function does the setup for a data transfer for an EP and
 * starts the transfer. For an IN transfer, the packets will be
 * loaded into the appropriate Tx FIFO in the ISR. For OUT transfers,
 * the packets are unloaded from the Rx FIFO in the ISR.  the ISR.
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param ep The EP to start the transfer on.
 */

void dwc_otg_ep_start_transfer(dwc_otg_core_if_t *core_if, dwc_ep_t *ep)
{
	depctl_data_t depctl;
	deptsiz_data_t deptsiz;
	gintmsk_data_t intr_mask = {.d32 = 0 };

	DWC_DEBUGPL((DBG_PCDV | DBG_CILV), "%s()\n", __func__);
	DWC_DEBUGPL(DBG_PCD, "ep%d-%s xfer_len=%d xfer_cnt=%d "
		    "xfer_buff=%p start_xfer_buff=%p, total_len = %d\n",
		    ep->num, (ep->is_in ? "IN" : "OUT"), ep->xfer_len,
		    ep->xfer_count, ep->xfer_buff, ep->start_xfer_buff,
		    ep->total_len);
	/* IN endpoint */
	if (ep->is_in == 1) {
		dwc_otg_dev_in_ep_regs_t *in_regs =
		    core_if->dev_if->in_ep_regs[ep->num];

		gnptxsts_data_t gtxstatus;

		gtxstatus.d32 =
		    DWC_READ_REG32(&core_if->core_global_regs->gnptxsts);

		if (core_if->en_multiple_tx_fifo == 0
		    && gtxstatus.b.nptxqspcavail == 0 && !core_if->dma_enable) {
#ifdef DEBUG
			DWC_PRINTF("TX Queue Full (0x%0x)\n", gtxstatus.d32);
#endif
			return;
		}

		depctl.d32 = DWC_READ_REG32(&(in_regs->diepctl));
		deptsiz.d32 = DWC_READ_REG32(&(in_regs->dieptsiz));

		if (ep->maxpacket > ep->maxxfer / MAX_PKT_CNT)
			ep->xfer_len +=
			    (ep->maxxfer <
			     (ep->total_len -
			      ep->xfer_len)) ? ep->maxxfer : (ep->total_len -
							      ep->xfer_len);
		else
			ep->xfer_len +=
			    (MAX_PKT_CNT * ep->maxpacket <
			     (ep->total_len -
			      ep->xfer_len)) ? MAX_PKT_CNT *
			    ep->maxpacket : (ep->total_len - ep->xfer_len);

		/* Zero Length Packet? */
		if ((ep->xfer_len - ep->xfer_count) == 0) {
			deptsiz.b.xfersize = 0;
			deptsiz.b.pktcnt = 1;
		} else {
			/* Program the transfer size and packet count
			 *      as follows: xfersize = N * maxpacket +
			 *      short_packet pktcnt = N + (short_packet
			 *	exist ? 1 : 0)
			 */
			deptsiz.b.xfersize = ep->xfer_len - ep->xfer_count;
			deptsiz.b.pktcnt =
			    (ep->xfer_len - ep->xfer_count - 1 +
			     ep->maxpacket) / ep->maxpacket;
			if (deptsiz.b.pktcnt > MAX_PKT_CNT) {
				deptsiz.b.pktcnt = MAX_PKT_CNT;
				deptsiz.b.xfersize =
				    deptsiz.b.pktcnt * ep->maxpacket;
			}
			if (ep->type == DWC_OTG_EP_TYPE_ISOC)
				deptsiz.b.mc = deptsiz.b.pktcnt;
		}

		/* Write the DMA register */
		if (core_if->dma_enable) {
			if (core_if->dma_desc_enable == 0) {
				if (ep->type != DWC_OTG_EP_TYPE_ISOC)
					deptsiz.b.mc = 1;
				DWC_WRITE_REG32(&in_regs->dieptsiz,
						deptsiz.d32);
				DWC_WRITE_REG32(&(in_regs->diepdma),
						(uint32_t) ep->dma_addr);
			} else {
#ifdef DWC_UTE_CFI
				/* The descriptor chain should be
				 * already initialized by now */
				if (ep->buff_mode != BM_STANDARD) {
					DWC_WRITE_REG32(&in_regs->diepdma,
							ep->descs_dma_addr);
				} else {
#endif
					init_dma_desc_chain(core_if, ep);
				/** DIEPDMAn Register write */
					DWC_WRITE_REG32(&in_regs->diepdma,
							ep->dma_desc_addr);
#ifdef DWC_UTE_CFI
				}
#endif
			}
		} else {
			DWC_WRITE_REG32(&in_regs->dieptsiz, deptsiz.d32);
			if (ep->type != DWC_OTG_EP_TYPE_ISOC) {
				/**
				 * Enable the Non-Periodic Tx FIFO empty interrupt,
				 * or the Tx FIFO epmty interrupt in dedicated Tx FIFO mode,
				 * the data will be written into the fifo by the ISR.
				 */
				if (core_if->en_multiple_tx_fifo == 0) {
					intr_mask.b.nptxfempty = 1;
					DWC_MODIFY_REG32
					    (&core_if->core_global_regs->
					     gintmsk, intr_mask.d32,
					     intr_mask.d32);
				} else {
					/* Enable the Tx FIFO Empty Interrupt for this EP */
					if (ep->xfer_len > 0) {
						uint32_t fifoemptymsk = 0;
						fifoemptymsk = 1 << ep->num;
						DWC_MODIFY_REG32
						    (&core_if->dev_if->
						     dev_global_regs->
						     dtknqr4_fifoemptymsk, 0,
						     fifoemptymsk);

					}
				}
			} else {
				write_isoc_tx_fifo(core_if, ep);
			}
		}
		if (!core_if->core_params->en_multiple_tx_fifo
		    && core_if->dma_enable)
			depctl.b.nextep = core_if->nextep_seq[ep->num];

		if (ep->type == DWC_OTG_EP_TYPE_ISOC) {
			dsts_data_t dsts = {.d32 = 0 };
			if (ep->bInterval == 1) {
				dsts.d32 =
				    DWC_READ_REG32(&core_if->
						   dev_if->dev_global_regs->
						   dsts);
				ep->frame_num = dsts.b.soffn + ep->bInterval;
				if (ep->frame_num > 0x3FFF) {
					ep->frm_overrun = 1;
					ep->frame_num &= 0x3FFF;
				} else
					ep->frm_overrun = 0;
				if (ep->frame_num & 0x1)
					depctl.b.setd1pid = 1;
				else
					depctl.b.setd0pid = 1;
			}
		}
		/* EP enable, IN data in FIFO */
		depctl.b.cnak = 1;
		depctl.b.epena = 1;
		DWC_WRITE_REG32(&in_regs->diepctl, depctl.d32);

	} else {
		/* OUT endpoint */
		dwc_otg_dev_out_ep_regs_t *out_regs =
		    core_if->dev_if->out_ep_regs[ep->num];

		depctl.d32 = DWC_READ_REG32(&(out_regs->doepctl));
		deptsiz.d32 = DWC_READ_REG32(&(out_regs->doeptsiz));

		if (!core_if->dma_desc_enable) {
			if (ep->maxpacket > ep->maxxfer / MAX_PKT_CNT)
				ep->xfer_len +=
				    (ep->maxxfer <
				     (ep->total_len -
				      ep->xfer_len)) ? ep->maxxfer : (ep->
								      total_len
								      -
								      ep->
								      xfer_len);
			else
				ep->xfer_len +=
				    (MAX_PKT_CNT * ep->maxpacket <
				     (ep->total_len -
				      ep->xfer_len)) ? MAX_PKT_CNT *
				    ep->maxpacket : (ep->total_len -
						     ep->xfer_len);
		}

		/* Program the transfer size and packet count as follows:
		 *
		 *	pktcnt = N
		 *	xfersize = N * maxpacket
		 */
		if ((ep->xfer_len - ep->xfer_count) == 0) {
			/* Zero Length Packet */
			deptsiz.b.xfersize = ep->maxpacket;
			deptsiz.b.pktcnt = 1;
		} else {
			deptsiz.b.pktcnt =
			    (ep->xfer_len - ep->xfer_count +
			     (ep->maxpacket - 1)) / ep->maxpacket;
			if (deptsiz.b.pktcnt > MAX_PKT_CNT)
				deptsiz.b.pktcnt = MAX_PKT_CNT;
			if (!core_if->dma_desc_enable) {
				ep->xfer_len =
				    deptsiz.b.pktcnt * ep->maxpacket +
				    ep->xfer_count;
			}
			deptsiz.b.xfersize = ep->xfer_len - ep->xfer_count;
		}

		DWC_DEBUGPL(DBG_PCDV, "ep%d xfersize=%d pktcnt=%d\n",
			    ep->num, deptsiz.b.xfersize, deptsiz.b.pktcnt);

		if (core_if->dma_enable) {
			if (!core_if->dma_desc_enable) {
				DWC_WRITE_REG32(&out_regs->doeptsiz,
						deptsiz.d32);

				DWC_WRITE_REG32(&(out_regs->doepdma),
						(uint32_t) ep->dma_addr);
			} else {
#ifdef DWC_UTE_CFI
				/* The descriptor chain should be
				 * already initialized by now */
				if (ep->buff_mode != BM_STANDARD) {
					DWC_WRITE_REG32(&out_regs->doepdma,
							ep->descs_dma_addr);
				} else {
#endif
					/* This is used for
					 * interrupt out transfers*/
					if (!ep->xfer_len)
						ep->xfer_len = ep->total_len;
					init_dma_desc_chain(core_if, ep);

					if (core_if->core_params->dev_out_nak) {
						if (ep->type ==
						    DWC_OTG_EP_TYPE_BULK) {
							deptsiz.b.pktcnt =
							    (ep->total_len +
							     (ep->maxpacket -
							      1)) /
							    ep->maxpacket;
							deptsiz.b.xfersize =
							    ep->total_len;
							/* Remember initial value of doeptsiz */
							core_if->
							    start_doeptsiz_val
							    [ep->num] =
							    deptsiz.d32;
							DWC_WRITE_REG32
							    (&out_regs->
							     doeptsiz,
							     deptsiz.d32);
						}
					}
				/** DOEPDMAn Register write */
					DWC_WRITE_REG32(&out_regs->doepdma,
							ep->dma_desc_addr);
#ifdef DWC_UTE_CFI
				}
#endif
			}
		} else {
			DWC_WRITE_REG32(&out_regs->doeptsiz, deptsiz.d32);
		}

		if (ep->type == DWC_OTG_EP_TYPE_ISOC) {
			dsts_data_t dsts = {.d32 = 0 };
			if (ep->bInterval == 1) {
				dsts.d32 =
				    DWC_READ_REG32(&core_if->
						   dev_if->dev_global_regs->
						   dsts);
				ep->frame_num = dsts.b.soffn + ep->bInterval;
				if (ep->frame_num > 0x3FFF) {
					ep->frm_overrun = 1;
					ep->frame_num &= 0x3FFF;
				} else
					ep->frm_overrun = 0;

				if (ep->frame_num & 0x1)
					depctl.b.setd1pid = 1;
				else
					depctl.b.setd0pid = 1;
			}
		}

		/* EP enable */
		depctl.b.cnak = 1;
		depctl.b.epena = 1;

		DWC_WRITE_REG32(&out_regs->doepctl, depctl.d32);

		DWC_DEBUGPL(DBG_PCD, "DOEPCTL=%08x DOEPTSIZ=%08x\n",
			    DWC_READ_REG32(&out_regs->doepctl),
			    DWC_READ_REG32(&out_regs->doeptsiz));
		DWC_DEBUGPL(DBG_PCD, "DAINTMSK=%08x GINTMSK=%08x\n",
			    DWC_READ_REG32(&core_if->dev_if->
					   dev_global_regs->daintmsk),
			    DWC_READ_REG32(&core_if->
					   core_global_regs->gintmsk));

		/* Timer is scheduling only for out bulk transfers for
		 * "Device DDMA OUT NAK Enhancement" feature to inform user
		 * about received data payload in case of timeout
		 */
		if (core_if->core_params->dev_out_nak) {
			if (ep->type == DWC_OTG_EP_TYPE_BULK) {
				core_if->ep_xfer_info[ep->num].core_if =
				    core_if;
				core_if->ep_xfer_info[ep->num].ep = ep;
				core_if->ep_xfer_info[ep->num].state = 1;

				/* Start a timer for this transfer. */
				DWC_TIMER_SCHEDULE(core_if->
						   ep_xfer_timer[ep->num],
						   10000);
			}
		}
	}
}

/**
 * This function setup a zero length transfer in Buffer DMA and
 * Slave modes for usb requests with zero field set
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param ep The EP to start the transfer on.
 *
 */
void dwc_otg_ep_start_zl_transfer(dwc_otg_core_if_t *core_if, dwc_ep_t *ep)
{

	depctl_data_t depctl;
	deptsiz_data_t deptsiz;
	gintmsk_data_t intr_mask = {.d32 = 0 };

	DWC_DEBUGPL((DBG_PCDV | DBG_CILV), "%s()\n", __func__);
	DWC_PRINTF("zero length transfer is called\n");

	/* IN endpoint */
	if (ep->is_in == 1) {
		dwc_otg_dev_in_ep_regs_t *in_regs =
		    core_if->dev_if->in_ep_regs[ep->num];

		depctl.d32 = DWC_READ_REG32(&(in_regs->diepctl));
		deptsiz.d32 = DWC_READ_REG32(&(in_regs->dieptsiz));

		deptsiz.b.xfersize = 0;
		deptsiz.b.pktcnt = 1;

		/* Write the DMA register */
		if (core_if->dma_enable) {
			if (core_if->dma_desc_enable == 0) {
				deptsiz.b.mc = 1;
				DWC_WRITE_REG32(&in_regs->dieptsiz,
						deptsiz.d32);
				DWC_WRITE_REG32(&(in_regs->diepdma),
						(uint32_t) ep->dma_addr);
			}
		} else {
			DWC_WRITE_REG32(&in_regs->dieptsiz, deptsiz.d32);
			/**
			 * Enable the Non-Periodic Tx FIFO empty interrupt,
			 * or the Tx FIFO epmty interrupt in dedicated Tx FIFO mode,
			 * the data will be written into the fifo by the ISR.
			 */
			if (core_if->en_multiple_tx_fifo == 0) {
				intr_mask.b.nptxfempty = 1;
				DWC_MODIFY_REG32(&core_if->core_global_regs->
						 gintmsk, intr_mask.d32,
						 intr_mask.d32);
			} else {
				/* Enable the Tx FIFO Empty Interrupt for this EP */
				if (ep->xfer_len > 0) {
					uint32_t fifoemptymsk = 0;
					fifoemptymsk = 1 << ep->num;
					DWC_MODIFY_REG32(&core_if->dev_if->
							 dev_global_regs->
							 dtknqr4_fifoemptymsk,
							 0, fifoemptymsk);
				}
			}
		}

		if (!core_if->core_params->en_multiple_tx_fifo
		    && core_if->dma_enable)
			depctl.b.nextep = core_if->nextep_seq[ep->num];
		/* EP enable, IN data in FIFO */
		depctl.b.cnak = 1;
		depctl.b.epena = 1;
		DWC_WRITE_REG32(&in_regs->diepctl, depctl.d32);

	} else {
		/* OUT endpoint */
		dwc_otg_dev_out_ep_regs_t *out_regs =
		    core_if->dev_if->out_ep_regs[ep->num];

		depctl.d32 = DWC_READ_REG32(&(out_regs->doepctl));
		deptsiz.d32 = DWC_READ_REG32(&(out_regs->doeptsiz));

		/* Zero Length Packet */
		deptsiz.b.xfersize = ep->maxpacket;
		deptsiz.b.pktcnt = 1;

		if (core_if->dma_enable) {
			if (!core_if->dma_desc_enable) {
				DWC_WRITE_REG32(&out_regs->doeptsiz,
						deptsiz.d32);

				DWC_WRITE_REG32(&(out_regs->doepdma),
						(uint32_t) ep->dma_addr);
			}
		} else {
			DWC_WRITE_REG32(&out_regs->doeptsiz, deptsiz.d32);
		}

		/* EP enable */
		depctl.b.cnak = 1;
		depctl.b.epena = 1;

		DWC_WRITE_REG32(&out_regs->doepctl, depctl.d32);

	}
}

/**
 * This function does the setup for a data transfer for EP0 and starts
 * the transfer.  For an IN transfer, the packets will be loaded into
 * the appropriate Tx FIFO in the ISR. For OUT transfers, the packets are
 * unloaded from the Rx FIFO in the ISR.
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param ep The EP0 data.
 */
void dwc_otg_ep0_start_transfer(dwc_otg_core_if_t *core_if, dwc_ep_t *ep)
{
	depctl_data_t depctl;
	deptsiz0_data_t deptsiz;
	gintmsk_data_t intr_mask = {.d32 = 0 };
	dwc_otg_dev_dma_desc_t *dma_desc;

	DWC_DEBUGPL(DBG_PCD, "ep%d-%s xfer_len=%d xfer_cnt=%d "
		    "xfer_buff=%p start_xfer_buff=%p \n",
		    ep->num, (ep->is_in ? "IN" : "OUT"), ep->xfer_len,
		    ep->xfer_count, ep->xfer_buff, ep->start_xfer_buff);

	ep->total_len = ep->xfer_len;

	/* IN endpoint */
	if (ep->is_in == 1) {
		dwc_otg_dev_in_ep_regs_t *in_regs =
		    core_if->dev_if->in_ep_regs[0];

		gnptxsts_data_t gtxstatus;

		if (core_if->snpsid >= OTG_CORE_REV_3_00a) {
			depctl.d32 = DWC_READ_REG32(&in_regs->diepctl);
			if (depctl.b.epena)
				return;
		}

		gtxstatus.d32 =
		    DWC_READ_REG32(&core_if->core_global_regs->gnptxsts);

		/* If dedicated FIFO every time flush fifo before enable ep */
		if (core_if->en_multiple_tx_fifo
		    && core_if->snpsid >= OTG_CORE_REV_3_00a)
			dwc_otg_flush_tx_fifo(core_if, ep->tx_fifo_num);

		if (core_if->en_multiple_tx_fifo == 0
		    && gtxstatus.b.nptxqspcavail == 0 && !core_if->dma_enable) {
#ifdef DEBUG
			deptsiz.d32 = DWC_READ_REG32(&in_regs->dieptsiz);
			DWC_DEBUGPL(DBG_PCD, "DIEPCTL0=%0x\n",
				    DWC_READ_REG32(&in_regs->diepctl));
			DWC_DEBUGPL(DBG_PCD, "DIEPTSIZ0=%0x (sz=%d, pcnt=%d)\n",
				    deptsiz.d32,
				    deptsiz.b.xfersize, deptsiz.b.pktcnt);
			DWC_PRINTF("TX Queue or FIFO Full (0x%0x)\n",
				   gtxstatus.d32);
#endif
			return;
		}

		depctl.d32 = DWC_READ_REG32(&in_regs->diepctl);
		deptsiz.d32 = DWC_READ_REG32(&in_regs->dieptsiz);

		/* Zero Length Packet? */
		if (ep->xfer_len == 0) {
			deptsiz.b.xfersize = 0;
			deptsiz.b.pktcnt = 1;
		} else {
			/* Program the transfer size and packet count
			 *      as follows: xfersize = N * maxpacket +
			 *      short_packet pktcnt = N + (short_packet
			 *	exist ? 1 : 0)
			 */
			if (ep->xfer_len > ep->maxpacket) {
				ep->xfer_len = ep->maxpacket;
				deptsiz.b.xfersize = ep->maxpacket;
			} else {
				deptsiz.b.xfersize = ep->xfer_len;
			}
			deptsiz.b.pktcnt = 1;

		}
		DWC_DEBUGPL(DBG_PCDV,
			    "IN len=%d  xfersize=%d pktcnt=%d [%08x]\n",
			    ep->xfer_len, deptsiz.b.xfersize, deptsiz.b.pktcnt,
			    deptsiz.d32);

		/* Write the DMA register */
		if (core_if->dma_enable) {
			if (core_if->dma_desc_enable == 0) {
				DWC_WRITE_REG32(&in_regs->dieptsiz,
						deptsiz.d32);

				DWC_WRITE_REG32(&(in_regs->diepdma),
						(uint32_t) ep->dma_addr);
			} else {
				dma_desc = core_if->dev_if->in_desc_addr;

				/** DMA Descriptor Setup */
				dma_desc->status.b.bs = BS_HOST_BUSY;
				dma_desc->status.b.l = 1;
				dma_desc->status.b.ioc = 1;
				dma_desc->status.b.sp =
				    (ep->xfer_len == ep->maxpacket) ? 0 : 1;
				dma_desc->status.b.bytes = ep->xfer_len;
				dma_desc->buf = ep->dma_addr;
				dma_desc->status.b.sts = 0;
				dma_desc->status.b.bs = BS_HOST_READY;

				/** DIEPDMA0 Register write */
				DWC_WRITE_REG32(&in_regs->diepdma,
						core_if->dev_if->
						dma_in_desc_addr);
			}
		} else {
			DWC_WRITE_REG32(&in_regs->dieptsiz, deptsiz.d32);
		}

		if (!core_if->core_params->en_multiple_tx_fifo
		    && core_if->dma_enable)
			depctl.b.nextep = core_if->nextep_seq[ep->num];
		/* EP enable, IN data in FIFO */
		depctl.b.cnak = 1;
		depctl.b.epena = 1;
		DWC_WRITE_REG32(&in_regs->diepctl, depctl.d32);

		/**
		 * Enable the Non-Periodic Tx FIFO empty interrupt, the
		 * data will be written into the fifo by the ISR.
		 */
		if (!core_if->dma_enable) {
			if (core_if->en_multiple_tx_fifo == 0) {
				intr_mask.b.nptxfempty = 1;
				DWC_MODIFY_REG32(&core_if->core_global_regs->
						 gintmsk, intr_mask.d32,
						 intr_mask.d32);
			} else {
				/* Enable the Tx FIFO Empty Interrupt for this EP */
				if (ep->xfer_len > 0) {
					uint32_t fifoemptymsk = 0;
					fifoemptymsk |= 1 << ep->num;
					DWC_MODIFY_REG32(&core_if->dev_if->
							 dev_global_regs->
							 dtknqr4_fifoemptymsk,
							 0, fifoemptymsk);
				}
			}
		}
	} else {
		/* OUT endpoint */
		dwc_otg_dev_out_ep_regs_t *out_regs =
		    core_if->dev_if->out_ep_regs[0];

		depctl.d32 = DWC_READ_REG32(&out_regs->doepctl);
		deptsiz.d32 = DWC_READ_REG32(&out_regs->doeptsiz);

		/* Program the transfer size and packet count as follows:
		 *      xfersize = N * (maxpacket + 4 - (maxpacket % 4))
		 *      pktcnt = N                                                                                      */
		/* Zero Length Packet */
		deptsiz.b.xfersize = ep->maxpacket;
		deptsiz.b.pktcnt = 1;
		if (core_if->snpsid >= OTG_CORE_REV_3_00a)
			deptsiz.b.supcnt = 1;

		DWC_DEBUGPL(DBG_PCDV, "len=%d  xfersize=%d pktcnt=%d\n",
			    ep->xfer_len, deptsiz.b.xfersize, deptsiz.b.pktcnt);

		if (core_if->dma_enable) {
			if (!core_if->dma_desc_enable) {
				DWC_WRITE_REG32(&out_regs->doeptsiz,
						deptsiz.d32);

				DWC_WRITE_REG32(&(out_regs->doepdma),
						(uint32_t) ep->dma_addr);
			} else {
				dma_desc = core_if->dev_if->out_desc_addr;

				/** DMA Descriptor Setup */
				dma_desc->status.b.bs = BS_HOST_BUSY;
				if (core_if->snpsid >= OTG_CORE_REV_3_00a) {
					dma_desc->status.b.mtrf = 0;
					dma_desc->status.b.sr = 0;
				}
				dma_desc->status.b.l = 1;
				dma_desc->status.b.ioc = 1;
				dma_desc->status.b.bytes = ep->maxpacket;
				dma_desc->buf = ep->dma_addr;
				dma_desc->status.b.sts = 0;
				dma_desc->status.b.bs = BS_HOST_READY;

				/** DOEPDMA0 Register write */
				DWC_WRITE_REG32(&out_regs->doepdma,
						core_if->
						dev_if->dma_out_desc_addr);
			}
		} else {
			DWC_WRITE_REG32(&out_regs->doeptsiz, deptsiz.d32);
		}

		/* EP enable */
		depctl.b.cnak = 1;
		depctl.b.epena = 1;
		DWC_WRITE_REG32(&(out_regs->doepctl), depctl.d32);
	}
}

/**
 * This function continues control IN transfers started by
 * dwc_otg_ep0_start_transfer, when the transfer does not fit in a
 * single packet.  NOTE: The DIEPCTL0/DOEPCTL0 registers only have one
 * bit for the packet count.
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param ep The EP0 data.
 */
void dwc_otg_ep0_continue_transfer(dwc_otg_core_if_t *core_if, dwc_ep_t *ep)
{
	depctl_data_t depctl;
	deptsiz0_data_t deptsiz;
	gintmsk_data_t intr_mask = {.d32 = 0 };
	dwc_otg_dev_dma_desc_t *dma_desc;

	if (ep->is_in == 1) {
		dwc_otg_dev_in_ep_regs_t *in_regs =
		    core_if->dev_if->in_ep_regs[0];
		gnptxsts_data_t tx_status = {.d32 = 0 };

		tx_status.d32 =
		    DWC_READ_REG32(&core_if->core_global_regs->gnptxsts);
		/** @todo Should there be check for room in the Tx
		 * Status Queue.  If not remove the code above this comment. */

		depctl.d32 = DWC_READ_REG32(&in_regs->diepctl);
		deptsiz.d32 = DWC_READ_REG32(&in_regs->dieptsiz);

		/* Program the transfer size and packet count
		 * 	as follows: xfersize = N * maxpacket +
		 * 	short_packet pktcnt = N + (short_packet
		 * 	exist ? 1 : 0)
		 */

		if (core_if->dma_desc_enable == 0) {
			deptsiz.b.xfersize =
			    (ep->total_len - ep->xfer_count) >
			    ep->maxpacket ? ep->maxpacket : (ep->total_len -
							     ep->xfer_count);
			deptsiz.b.pktcnt = 1;
			if (core_if->dma_enable == 0)
				ep->xfer_len += deptsiz.b.xfersize;
			else
				ep->xfer_len = deptsiz.b.xfersize;
			DWC_WRITE_REG32(&in_regs->dieptsiz, deptsiz.d32);
		} else {
			ep->xfer_len =
			    (ep->total_len - ep->xfer_count) >
			    ep->maxpacket ? ep->maxpacket : (ep->total_len -
							     ep->xfer_count);

			dma_desc = core_if->dev_if->in_desc_addr;

			/** DMA Descriptor Setup */
			dma_desc->status.b.bs = BS_HOST_BUSY;
			dma_desc->status.b.l = 1;
			dma_desc->status.b.ioc = 1;
			dma_desc->status.b.sp =
			    (ep->xfer_len == ep->maxpacket) ? 0 : 1;
			dma_desc->status.b.bytes = ep->xfer_len;
			dma_desc->buf = ep->dma_addr;
			dma_desc->status.b.sts = 0;
			dma_desc->status.b.bs = BS_HOST_READY;

			/** DIEPDMA0 Register write */
			DWC_WRITE_REG32(&in_regs->diepdma,
					core_if->dev_if->dma_in_desc_addr);
		}

		DWC_DEBUGPL(DBG_PCDV,
			    "IN len=%d  xfersize=%d pktcnt=%d [%08x]\n",
			    ep->xfer_len, deptsiz.b.xfersize, deptsiz.b.pktcnt,
			    deptsiz.d32);

		/* Write the DMA register */
		if (core_if->hwcfg2.b.architecture == DWC_INT_DMA_ARCH) {
			if (core_if->dma_desc_enable == 0)
				DWC_WRITE_REG32(&(in_regs->diepdma),
						(uint32_t) ep->dma_addr);
		}
		if (!core_if->core_params->en_multiple_tx_fifo
		    && core_if->dma_enable)
			depctl.b.nextep = core_if->nextep_seq[ep->num];
		/* EP enable, IN data in FIFO */
		depctl.b.cnak = 1;
		depctl.b.epena = 1;
		DWC_WRITE_REG32(&in_regs->diepctl, depctl.d32);

		/**
		 * Enable the Non-Periodic Tx FIFO empty interrupt, the
		 * data will be written into the fifo by the ISR.
		 */
		if (!core_if->dma_enable) {
			if (core_if->en_multiple_tx_fifo == 0) {
				/* First clear it from GINTSTS */
				intr_mask.b.nptxfempty = 1;
				DWC_MODIFY_REG32(&core_if->core_global_regs->
						 gintmsk, intr_mask.d32,
						 intr_mask.d32);

			} else {
				/* Enable the Tx FIFO Empty Interrupt for this EP */
				if (ep->xfer_len > 0) {
					uint32_t fifoemptymsk = 0;
					fifoemptymsk |= 1 << ep->num;
					DWC_MODIFY_REG32(&core_if->dev_if->
							 dev_global_regs->
							 dtknqr4_fifoemptymsk,
							 0, fifoemptymsk);
				}
			}
		}
	} else {
		dwc_otg_dev_out_ep_regs_t *out_regs =
		    core_if->dev_if->out_ep_regs[0];

		depctl.d32 = DWC_READ_REG32(&out_regs->doepctl);
		deptsiz.d32 = DWC_READ_REG32(&out_regs->doeptsiz);

		/* Program the transfer size and packet count
		 * 	as follows: xfersize = N * maxpacket +
		 * 	short_packet pktcnt = N + (short_packet
		 * 	exist ? 1 : 0)
		 */
		deptsiz.b.xfersize = ep->maxpacket;
		deptsiz.b.pktcnt = 1;

		if (core_if->dma_desc_enable == 0) {
			DWC_WRITE_REG32(&out_regs->doeptsiz, deptsiz.d32);
		} else {
			dma_desc = core_if->dev_if->out_desc_addr;

			/** DMA Descriptor Setup */
			dma_desc->status.b.bs = BS_HOST_BUSY;
			dma_desc->status.b.l = 1;
			dma_desc->status.b.ioc = 1;
			dma_desc->status.b.bytes = ep->maxpacket;
			dma_desc->buf = ep->dma_addr;
			dma_desc->status.b.sts = 0;
			dma_desc->status.b.bs = BS_HOST_READY;

			/** DOEPDMA0 Register write */
			DWC_WRITE_REG32(&out_regs->doepdma,
					core_if->dev_if->dma_out_desc_addr);
		}

		DWC_DEBUGPL(DBG_PCDV,
			    "IN len=%d  xfersize=%d pktcnt=%d [%08x]\n",
			    ep->xfer_len, deptsiz.b.xfersize, deptsiz.b.pktcnt,
			    deptsiz.d32);

		/* Write the DMA register */
		if (core_if->hwcfg2.b.architecture == DWC_INT_DMA_ARCH) {
			if (core_if->dma_desc_enable == 0)
				DWC_WRITE_REG32(&(out_regs->doepdma),
						(uint32_t) ep->dma_addr);

		}

		/* EP enable, IN data in FIFO */
		depctl.b.cnak = 1;
		depctl.b.epena = 1;
		DWC_WRITE_REG32(&out_regs->doepctl, depctl.d32);

	}
}

#ifdef DEBUG
void dump_msg(const u8 *buf, unsigned int length)
{
	unsigned int start, num, i;
	char line[52], *p;

	if (length >= 512)
		return;
	start = 0;
	while (length > 0) {
		num = length < 16u ? length : 16u;
		p = line;
		for (i = 0; i < num; ++i) {
			if (i == 8)
				*p++ = ' ';
			DWC_SPRINTF(p, " %02x", buf[i]);
			p += 3;
		}
		*p = 0;
		DWC_PRINTF("%6x: %s\n", start, line);
		buf += num;
		start += num;
		length -= num;
	}
}
#else
static inline void dump_msg(const u8 *buf, unsigned int length)
{
}
#endif

/**
 * This function writes a packet into the Tx FIFO associated with the
 * EP. For non-periodic EPs the non-periodic Tx FIFO is written.  For
 * periodic EPs the periodic Tx FIFO associated with the EP is written
 * with all packets for the next micro-frame.
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param ep The EP to write packet for.
 * @param dma Indicates if DMA is being used.
 */
void dwc_otg_ep_write_packet(dwc_otg_core_if_t *core_if, dwc_ep_t *ep,
			     int dma)
{
	/**
	 * The buffer is padded to DWORD on a per packet basis in
	 * slave/dma mode if the MPS is not DWORD aligned. The last
	 * packet, if short, is also padded to a multiple of DWORD.
	 *
	 * ep->xfer_buff always starts DWORD aligned in memory and is a
	 * multiple of DWORD in length
	 *
	 * ep->xfer_len can be any number of bytes
	 *
	 * ep->xfer_count is a multiple of ep->maxpacket until the last
	 *	packet
	 *
	 * FIFO access is DWORD */

	uint32_t i;
	uint32_t byte_count;
	uint32_t dword_count;
	uint32_t *fifo;
	uint32_t *data_buff = (uint32_t *) ep->xfer_buff;

	DWC_DEBUGPL((DBG_PCDV | DBG_CILV), "%s(%p,%p)\n", __func__, core_if,
		    ep);
	if (ep->xfer_count >= ep->xfer_len) {
		DWC_WARN("%s() No data for EP%d!!!\n", __func__, ep->num);
		return;
	}

	/* Find the byte length of the packet either short packet or MPS */
	if ((ep->xfer_len - ep->xfer_count) < ep->maxpacket)
		byte_count = ep->xfer_len - ep->xfer_count;
	else
		byte_count = ep->maxpacket;

	/* Find the DWORD length, padded by extra bytes as neccessary if MPS
	 * is not a multiple of DWORD */
	dword_count = (byte_count + 3) / 4;

#ifdef VERBOSE
	dump_msg(ep->xfer_buff, byte_count);
#endif

	/**@todo NGS Where are the Periodic Tx FIFO addresses
	 * intialized?	What should this be? */

	fifo = core_if->data_fifo[ep->num];

	DWC_DEBUGPL((DBG_PCDV | DBG_CILV), "fifo=%p buff=%p *p=%08x bc=%d\n",
		    fifo, data_buff, *data_buff, byte_count);

	if (!dma) {
		for (i = 0; i < dword_count; i++, data_buff++)
			DWC_WRITE_REG32(fifo, *data_buff);
	}

	ep->xfer_count += byte_count;
	ep->xfer_buff += byte_count;
	ep->dma_addr += byte_count;
}

/**
 * Set the EP STALL.
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param ep The EP to set the stall on.
 */
void dwc_otg_ep_set_stall(dwc_otg_core_if_t *core_if, dwc_ep_t *ep)
{
	depctl_data_t depctl;
	volatile uint32_t *depctl_addr;

	DWC_DEBUGPL(DBG_PCD, "%s ep%d-%s\n", __func__, ep->num,
		    (ep->is_in ? "IN" : "OUT"));

	if (ep->is_in == 1) {
		depctl_addr = &(core_if->dev_if->in_ep_regs[ep->num]->diepctl);
		depctl.d32 = DWC_READ_REG32(depctl_addr);

		/* set the disable and stall bits */
		if (depctl.b.epena)
			depctl.b.epdis = 1;
		depctl.b.stall = 1;
		DWC_WRITE_REG32(depctl_addr, depctl.d32);
	} else {
		depctl_addr = &(core_if->dev_if->out_ep_regs[ep->num]->doepctl);
		depctl.d32 = DWC_READ_REG32(depctl_addr);

		/* set the stall bit */
		depctl.b.stall = 1;
		DWC_WRITE_REG32(depctl_addr, depctl.d32);
	}

	DWC_DEBUGPL(DBG_PCD, "DEPCTL=%0x\n", DWC_READ_REG32(depctl_addr));

	return;
}

/**
 * Clear the EP STALL.
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param ep The EP to clear stall from.
 */
void dwc_otg_ep_clear_stall(dwc_otg_core_if_t *core_if, dwc_ep_t *ep)
{
	depctl_data_t depctl;
	volatile uint32_t *depctl_addr;

	DWC_DEBUGPL(DBG_PCD, "%s ep%d-%s\n", __func__, ep->num,
		    (ep->is_in ? "IN" : "OUT"));

	if (ep->is_in == 1)
		depctl_addr = &(core_if->dev_if->in_ep_regs[ep->num]->diepctl);
	else
		depctl_addr = &(core_if->dev_if->out_ep_regs[ep->num]->doepctl);

	depctl.d32 = DWC_READ_REG32(depctl_addr);

	/* clear the stall bits */
	depctl.b.stall = 0;

	/*
	 * USB Spec 9.4.5: For endpoints using data toggle, regardless
	 * of whether an endpoint has the Halt feature set, a
	 * ClearFeature(ENDPOINT_HALT) request always results in the
	 * data toggle being reinitialized to DATA0.
	 */
	if (ep->type == DWC_OTG_EP_TYPE_INTR ||
	    ep->type == DWC_OTG_EP_TYPE_BULK) {
		depctl.b.setd0pid = 1;	/* DATA0 */
	}

	DWC_WRITE_REG32(depctl_addr, depctl.d32);
	DWC_DEBUGPL(DBG_PCD, "DEPCTL=%0x\n", DWC_READ_REG32(depctl_addr));
	return;
}

/**
 * This function reads a packet from the Rx FIFO into the destination
 * buffer. To read SETUP data use dwc_otg_read_setup_packet.
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param dest	  Destination buffer for the packet.
 * @param bytes  Number of bytes to copy to the destination.
 */
void dwc_otg_read_packet(dwc_otg_core_if_t *core_if,
			 uint8_t *dest, uint16_t bytes)
{
	int i;
	int word_count = (bytes + 3) / 4;

	volatile uint32_t *fifo = core_if->data_fifo[0];
	uint32_t *data_buff = (uint32_t *) dest;

	/**
	 * @todo Account for the case where _dest is not dword aligned. This
	 * requires reading data from the FIFO into a uint32_t temp buffer,
	 * then moving it into the data buffer.
	 */

	DWC_DEBUGPL((DBG_PCDV | DBG_CILV), "%s(%p,%p,%d)\n", __func__,
		    core_if, dest, bytes);

	for (i = 0; i < word_count; i++, data_buff++)
		*data_buff = DWC_READ_REG32(fifo);

	return;
}

/**
 * This functions reads the device registers and prints them
 *
 * @param core_if Programming view of DWC_otg controller.
 */
void dwc_otg_dump_dev_registers(dwc_otg_core_if_t *core_if)
{
	int i;
	volatile uint32_t *addr;
	uint32_t hwcfg1;

	hwcfg1 = ~core_if->core_global_regs->ghwcfg1;

	DWC_PRINTF("Device Global Registers\n");
	addr = &core_if->dev_if->dev_global_regs->dcfg;
	DWC_PRINTF("DCFG		 @0x%08lX : 0x%08X\n",
		   (unsigned long)addr, DWC_READ_REG32(addr));
	addr = &core_if->dev_if->dev_global_regs->dctl;
	DWC_PRINTF("DCTL		 @0x%08lX : 0x%08X\n",
		   (unsigned long)addr, DWC_READ_REG32(addr));
	addr = &core_if->dev_if->dev_global_regs->dsts;
	DWC_PRINTF("DSTS		 @0x%08lX : 0x%08X\n",
		   (unsigned long)addr, DWC_READ_REG32(addr));
	addr = &core_if->dev_if->dev_global_regs->diepmsk;
	DWC_PRINTF("DIEPMSK	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	addr = &core_if->dev_if->dev_global_regs->doepmsk;
	DWC_PRINTF("DOEPMSK	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	addr = &core_if->dev_if->dev_global_regs->daint;
	DWC_PRINTF("DAINT	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	addr = &core_if->dev_if->dev_global_regs->daintmsk;
	DWC_PRINTF("DAINTMSK	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	addr = &core_if->dev_if->dev_global_regs->dtknqr1;
	DWC_PRINTF("DTKNQR1	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	if (core_if->hwcfg2.b.dev_token_q_depth > 6) {
		addr = &core_if->dev_if->dev_global_regs->dtknqr2;
		DWC_PRINTF("DTKNQR2	 @0x%08lX : 0x%08X\n",
			   (unsigned long)addr, DWC_READ_REG32(addr));
	}

	addr = &core_if->dev_if->dev_global_regs->dvbusdis;
	DWC_PRINTF("DVBUSID	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));

	addr = &core_if->dev_if->dev_global_regs->dvbuspulse;
	DWC_PRINTF("DVBUSPULSE	@0x%08lX : 0x%08X\n",
		   (unsigned long)addr, DWC_READ_REG32(addr));

	addr = &core_if->dev_if->dev_global_regs->dtknqr3_dthrctl;
	DWC_PRINTF("DTKNQR3_DTHRCTL	 @0x%08lX : 0x%08X\n",
		   (unsigned long)addr, DWC_READ_REG32(addr));

	if (core_if->hwcfg2.b.dev_token_q_depth > 22) {
		addr = &core_if->dev_if->dev_global_regs->dtknqr4_fifoemptymsk;
		DWC_PRINTF("DTKNQR4	 @0x%08lX : 0x%08X\n",
			   (unsigned long)addr, DWC_READ_REG32(addr));
	}

	addr = &core_if->dev_if->dev_global_regs->dtknqr4_fifoemptymsk;
	DWC_PRINTF("FIFOEMPMSK	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));

	if (core_if->hwcfg2.b.multi_proc_int) {

		addr = &core_if->dev_if->dev_global_regs->deachint;
		DWC_PRINTF("DEACHINT	 @0x%08lX : 0x%08X\n",
			   (unsigned long)addr, DWC_READ_REG32(addr));
		addr = &core_if->dev_if->dev_global_regs->deachintmsk;
		DWC_PRINTF("DEACHINTMSK	 @0x%08lX : 0x%08X\n",
			   (unsigned long)addr, DWC_READ_REG32(addr));

		for (i = 0; i <= core_if->dev_if->num_in_eps; i++) {
			addr =
			    &core_if->dev_if->dev_global_regs->
			    diepeachintmsk[i];
			DWC_PRINTF("DIEPEACHINTMSK[%d]	 @0x%08lX : 0x%08X\n",
				   i, (unsigned long)addr,
				   DWC_READ_REG32(addr));
		}

		for (i = 0; i <= core_if->dev_if->num_out_eps; i++) {
			addr =
			    &core_if->dev_if->dev_global_regs->
			    doepeachintmsk[i];
			DWC_PRINTF("DOEPEACHINTMSK[%d]	 @0x%08lX : 0x%08X\n",
				   i, (unsigned long)addr,
				   DWC_READ_REG32(addr));
		}
	}

	for (i = 0; i <= core_if->core_params->dev_endpoints; i++) {
		if (hwcfg1 & (2 << (i << 1))) {
			DWC_PRINTF("Device IN EP %d Registers\n", i);
			addr = &core_if->dev_if->in_ep_regs[i]->diepctl;
			DWC_PRINTF("DIEPCTL	 @0x%08lX : 0x%08X\n",
				   (unsigned long)addr, DWC_READ_REG32(addr));
			addr = &core_if->dev_if->in_ep_regs[i]->diepint;
			DWC_PRINTF("DIEPINT	 @0x%08lX : 0x%08X\n",
				   (unsigned long)addr, DWC_READ_REG32(addr));
			addr = &core_if->dev_if->in_ep_regs[i]->dieptsiz;
			DWC_PRINTF("DIETSIZ	 @0x%08lX : 0x%08X\n",
				   (unsigned long)addr, DWC_READ_REG32(addr));
			addr = &core_if->dev_if->in_ep_regs[i]->diepdma;
			DWC_PRINTF("DIEPDMA	 @0x%08lX : 0x%08X\n",
				   (unsigned long)addr, DWC_READ_REG32(addr));
			addr = &core_if->dev_if->in_ep_regs[i]->dtxfsts;
			DWC_PRINTF("DTXFSTS	 @0x%08lX : 0x%08X\n",
				   (unsigned long)addr, DWC_READ_REG32(addr));
			addr = &core_if->dev_if->in_ep_regs[i]->diepdmab;
			DWC_PRINTF("DIEPDMAB	 @0x%08lX : 0x%08X\n",
				   (unsigned long)addr,
				   0 /*DWC_READ_REG32(addr) */);
		}
	}

	for (i = 0; i <= core_if->core_params->dev_endpoints; i++) {
		if (hwcfg1 & (1 << (i << 1))) {
			DWC_PRINTF("Device OUT EP %d Registers\n", i);
			addr = &core_if->dev_if->out_ep_regs[i]->doepctl;
			DWC_PRINTF("DOEPCTL	 @0x%08lX : 0x%08X\n",
				   (unsigned long)addr, DWC_READ_REG32(addr));
			addr = &core_if->dev_if->out_ep_regs[i]->doepint;
			DWC_PRINTF("DOEPINT	 @0x%08lX : 0x%08X\n",
				   (unsigned long)addr, DWC_READ_REG32(addr));
			addr = &core_if->dev_if->out_ep_regs[i]->doeptsiz;
			DWC_PRINTF("DOETSIZ	 @0x%08lX : 0x%08X\n",
				   (unsigned long)addr, DWC_READ_REG32(addr));
			addr = &core_if->dev_if->out_ep_regs[i]->doepdma;
			DWC_PRINTF("DOEPDMA	 @0x%08lX : 0x%08X\n",
				   (unsigned long)addr, DWC_READ_REG32(addr));
			/* Don't access this register in SLAVE mode */
			if (core_if->dma_enable) {
				addr =
				    &core_if->dev_if->out_ep_regs[i]->doepdmab;
				DWC_PRINTF("DOEPDMAB	 @0x%08lX : 0x%08X\n",
					   (unsigned long)addr,
					   DWC_READ_REG32(addr));
			}
		}

	}
}

/**
 * This functions reads the SPRAM and prints its content
 *
 * @param core_if Programming view of DWC_otg controller.
 */
void dwc_otg_dump_spram(dwc_otg_core_if_t *core_if)
{
	volatile uint8_t *addr, *start_addr, *end_addr;

	DWC_PRINTF("SPRAM Data:\n");
	start_addr = (void *)core_if->core_global_regs;
	DWC_PRINTF("Base Address: 0x%8lX\n", (unsigned long)start_addr);
	start_addr += 0x00028000;
	end_addr = (void *)core_if->core_global_regs;
	end_addr += 0x000280e0;

	for (addr = start_addr; addr < end_addr; addr += 16) {
		DWC_PRINTF
		    ("0x%8lX:\t%2X %2X %2X %2X %2X %2X %2X %2X %2X %2X %2X %2X %2X %2X %2X %2X\n",
		     (unsigned long)addr, addr[0], addr[1], addr[2], addr[3],
		     addr[4], addr[5], addr[6], addr[7], addr[8], addr[9],
		     addr[10], addr[11], addr[12], addr[13], addr[14], addr[15]
		    );
	}

	return;
}

/**
 * This function reads the host registers and prints them
 *
 * @param core_if Programming view of DWC_otg controller.
 */
void dwc_otg_dump_host_registers(dwc_otg_core_if_t *core_if)
{
	int i;
	volatile uint32_t *addr;

	DWC_PRINTF("Host Global Registers\n");
	addr = &core_if->host_if->host_global_regs->hcfg;
	DWC_PRINTF("HCFG		 @0x%08lX : 0x%08X\n",
		   (unsigned long)addr, DWC_READ_REG32(addr));
	addr = &core_if->host_if->host_global_regs->hfir;
	DWC_PRINTF("HFIR		 @0x%08lX : 0x%08X\n",
		   (unsigned long)addr, DWC_READ_REG32(addr));
	addr = &core_if->host_if->host_global_regs->hfnum;
	DWC_PRINTF("HFNUM	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	addr = &core_if->host_if->host_global_regs->hptxsts;
	DWC_PRINTF("HPTXSTS	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	addr = &core_if->host_if->host_global_regs->haint;
	DWC_PRINTF("HAINT	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	addr = &core_if->host_if->host_global_regs->haintmsk;
	DWC_PRINTF("HAINTMSK	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	if (core_if->dma_desc_enable) {
		addr = &core_if->host_if->host_global_regs->hflbaddr;
		DWC_PRINTF("HFLBADDR	 @0x%08lX : 0x%08X\n",
			   (unsigned long)addr, DWC_READ_REG32(addr));
	}

	addr = core_if->host_if->hprt0;
	DWC_PRINTF("HPRT0	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));

	for (i = 0; i < core_if->core_params->host_channels; i++) {
		DWC_PRINTF("Host Channel %d Specific Registers\n", i);
		addr = &core_if->host_if->hc_regs[i]->hcchar;
		DWC_PRINTF("HCCHAR	 @0x%08lX : 0x%08X\n",
			   (unsigned long)addr, DWC_READ_REG32(addr));
		addr = &core_if->host_if->hc_regs[i]->hcsplt;
		DWC_PRINTF("HCSPLT	 @0x%08lX : 0x%08X\n",
			   (unsigned long)addr, DWC_READ_REG32(addr));
		addr = &core_if->host_if->hc_regs[i]->hcint;
		DWC_PRINTF("HCINT	 @0x%08lX : 0x%08X\n",
			   (unsigned long)addr, DWC_READ_REG32(addr));
		addr = &core_if->host_if->hc_regs[i]->hcintmsk;
		DWC_PRINTF("HCINTMSK	 @0x%08lX : 0x%08X\n",
			   (unsigned long)addr, DWC_READ_REG32(addr));
		addr = &core_if->host_if->hc_regs[i]->hctsiz;
		DWC_PRINTF("HCTSIZ	 @0x%08lX : 0x%08X\n",
			   (unsigned long)addr, DWC_READ_REG32(addr));
		addr = &core_if->host_if->hc_regs[i]->hcdma;
		DWC_PRINTF("HCDMA	 @0x%08lX : 0x%08X\n",
			   (unsigned long)addr, DWC_READ_REG32(addr));
		if (core_if->dma_desc_enable) {
			addr = &core_if->host_if->hc_regs[i]->hcdmab;
			DWC_PRINTF("HCDMAB	 @0x%08lX : 0x%08X\n",
				   (unsigned long)addr, DWC_READ_REG32(addr));
		}

	}
	return;
}

/**
 * This function reads the core global registers and prints them
 *
 * @param core_if Programming view of DWC_otg controller.
 */
void dwc_otg_dump_global_registers(dwc_otg_core_if_t *core_if)
{
	int i, ep_num;
	volatile uint32_t *addr;
	char *txfsiz;

	DWC_PRINTF("Core Global Registers\n");
	addr = &core_if->core_global_regs->gotgctl;
	DWC_PRINTF("GOTGCTL	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	addr = &core_if->core_global_regs->gotgint;
	DWC_PRINTF("GOTGINT	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	addr = &core_if->core_global_regs->gahbcfg;
	DWC_PRINTF("GAHBCFG	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	addr = &core_if->core_global_regs->gusbcfg;
	DWC_PRINTF("GUSBCFG	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	addr = &core_if->core_global_regs->grstctl;
	DWC_PRINTF("GRSTCTL	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	addr = &core_if->core_global_regs->gintsts;
	DWC_PRINTF("GINTSTS	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	addr = &core_if->core_global_regs->gintmsk;
	DWC_PRINTF("GINTMSK	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	addr = &core_if->core_global_regs->grxstsr;
	DWC_PRINTF("GRXSTSR	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	addr = &core_if->core_global_regs->grxfsiz;
	DWC_PRINTF("GRXFSIZ	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	addr = &core_if->core_global_regs->gnptxfsiz;
	DWC_PRINTF("GNPTXFSIZ @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	addr = &core_if->core_global_regs->gnptxsts;
	DWC_PRINTF("GNPTXSTS	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	addr = &core_if->core_global_regs->gi2cctl;
	DWC_PRINTF("GI2CCTL	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	addr = &core_if->core_global_regs->gpvndctl;
	DWC_PRINTF("GPVNDCTL	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	addr = &core_if->core_global_regs->ggpio;
	DWC_PRINTF("GGPIO	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	addr = &core_if->core_global_regs->guid;
	DWC_PRINTF("GUID		 @0x%08lX : 0x%08X\n",
		   (unsigned long)addr, DWC_READ_REG32(addr));
	addr = &core_if->core_global_regs->gsnpsid;
	DWC_PRINTF("GSNPSID	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	addr = &core_if->core_global_regs->ghwcfg1;
	DWC_PRINTF("GHWCFG1	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	addr = &core_if->core_global_regs->ghwcfg2;
	DWC_PRINTF("GHWCFG2	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	addr = &core_if->core_global_regs->ghwcfg3;
	DWC_PRINTF("GHWCFG3	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	addr = &core_if->core_global_regs->ghwcfg4;
	DWC_PRINTF("GHWCFG4	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	addr = &core_if->core_global_regs->glpmcfg;
	DWC_PRINTF("GLPMCFG	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	addr = &core_if->core_global_regs->gpwrdn;
	DWC_PRINTF("GPWRDN	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	addr = &core_if->core_global_regs->gdfifocfg;
	DWC_PRINTF("GDFIFOCFG	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
	addr = &core_if->core_global_regs->hptxfsiz;
	DWC_PRINTF("HPTXFSIZ	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));

	if (core_if->en_multiple_tx_fifo == 0) {
		ep_num = core_if->hwcfg4.b.num_dev_perio_in_ep;
		txfsiz = "DPTXFSIZ";
	} else {
		ep_num = core_if->hwcfg4.b.num_in_eps;
		txfsiz = "DIENPTXF";
	}
	for (i = 0; i < ep_num; i++) {
		addr = &core_if->core_global_regs->dtxfsiz[i];
		DWC_PRINTF("%s[%d] @0x%08lX : 0x%08X\n", txfsiz, i + 1,
			   (unsigned long)addr, DWC_READ_REG32(addr));
	}
	addr = core_if->pcgcctl;
	DWC_PRINTF("PCGCCTL	 @0x%08lX : 0x%08X\n", (unsigned long)addr,
		   DWC_READ_REG32(addr));
}

/**
 * Flush a Tx FIFO.
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param num Tx FIFO to flush.
 */
void dwc_otg_flush_tx_fifo(dwc_otg_core_if_t *core_if, const int num)
{
	dwc_otg_core_global_regs_t *global_regs = core_if->core_global_regs;
	volatile grstctl_t greset = {.d32 = 0 };
	int count = 0;

	DWC_DEBUGPL((DBG_CIL | DBG_PCDV), "Flush Tx FIFO %d\n", num);

	greset.b.txfflsh = 1;
	greset.b.txfnum = num;
	DWC_WRITE_REG32(&global_regs->grstctl, greset.d32);

	do {
		greset.d32 = DWC_READ_REG32(&global_regs->grstctl);
		if (++count > 10000) {
			DWC_WARN("%s() HANG! GRSTCTL=%0x GNPTXSTS=0x%08x\n",
				 __func__, greset.d32,
				 DWC_READ_REG32(&global_regs->gnptxsts));
			break;
		}
		dwc_udelay(1);
	} while (greset.b.txfflsh == 1);

	/* Wait for 3 PHY Clocks */
	dwc_udelay(1);
}

/**
 * Flush Rx FIFO.
 *
 * @param core_if Programming view of DWC_otg controller.
 */
void dwc_otg_flush_rx_fifo(dwc_otg_core_if_t *core_if)
{
	dwc_otg_core_global_regs_t *global_regs = core_if->core_global_regs;
	volatile grstctl_t greset = {.d32 = 0 };
	int count = 0;

	DWC_DEBUGPL((DBG_CIL | DBG_PCDV), "%s\n", __func__);
	/*
	 *
	 */
	greset.b.rxfflsh = 1;
	DWC_WRITE_REG32(&global_regs->grstctl, greset.d32);

	do {
		greset.d32 = DWC_READ_REG32(&global_regs->grstctl);
		if (++count > 10000) {
			DWC_WARN("%s() HANG! GRSTCTL=%0x\n", __func__,
				 greset.d32);
			break;
		}
		dwc_udelay(1);
	} while (greset.b.rxfflsh == 1);

	/* Wait for 3 PHY Clocks */
	dwc_udelay(1);
}

/**
 * Do core a soft reset of the core.  Be careful with this because it
 * resets all the internal state machines of the core.
 */
void dwc_otg_core_reset(dwc_otg_core_if_t *core_if)
{
	dwc_otg_core_global_regs_t *global_regs = core_if->core_global_regs;
	volatile grstctl_t greset = {.d32 = 0 };
	volatile gusbcfg_data_t usbcfg = {.d32 = 0 };
	int count = 0;

	DWC_DEBUGPL(DBG_CILV, "%s\n", __func__);
	/* Wait for AHB master IDLE state. */
	do {
		dwc_udelay(10);
		greset.d32 = DWC_READ_REG32(&global_regs->grstctl);
		if (++count > 100000) {
			DWC_WARN("%s() HANG! AHB Idle GRSTCTL=%0x\n", __func__,
				 greset.d32);
			return;
		}
	} while (greset.b.ahbidle == 0);

	/* Core Soft Reset */
	count = 0;
	greset.b.csftrst = 1;
	DWC_WRITE_REG32(&global_regs->grstctl, greset.d32);

	do {
		greset.d32 = DWC_READ_REG32(&global_regs->grstctl);
		if (++count > 10000) {
			DWC_WARN("%s() HANG! Soft Reset GRSTCTL=%0x\n",
				 __func__, greset.d32);
			break;
		}
		dwc_udelay(1);
	} while (greset.b.csftrst == 1);

	usbcfg.d32 = DWC_READ_REG32(&global_regs->gusbcfg);
	if (core_if->usb_mode == USB_MODE_FORCE_HOST) {
		usbcfg.b.force_host_mode = 1;
		usbcfg.b.force_dev_mode = 0;
	} else if (core_if->usb_mode == USB_MODE_FORCE_DEVICE) {
		usbcfg.b.force_host_mode = 0;
		usbcfg.b.force_dev_mode = 1;
	} else {
		usbcfg.b.force_host_mode = 0;
		usbcfg.b.force_dev_mode = 0;
	}
	DWC_WRITE_REG32(&global_regs->gusbcfg, usbcfg.d32);

	/* Wait for 3 PHY Clocks */
	dwc_mdelay(100);

}

uint8_t dwc_otg_is_device_mode(dwc_otg_core_if_t *_core_if)
{
	return (dwc_otg_mode(_core_if) != DWC_HOST_MODE);
}

uint8_t dwc_otg_is_host_mode(dwc_otg_core_if_t *_core_if)
{
	return (dwc_otg_mode(_core_if) == DWC_HOST_MODE);
}

/**
 * Register HCD callbacks. The callbacks are used to start and stop
 * the HCD for interrupt processing.
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param cb the HCD callback structure.
 * @param p pointer to be passed to callback function (usb_hcd*).
 */
void dwc_otg_cil_register_hcd_callbacks(dwc_otg_core_if_t *core_if,
					dwc_otg_cil_callbacks_t *cb, void *p)
{
	core_if->hcd_cb = cb;
	/* cb->p = p; */
	core_if->hcd_cb_p = p;
}

/**
 * Register PCD callbacks. The callbacks are used to start and stop
 * the PCD for interrupt processing.
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param cb the PCD callback structure.
 * @param p pointer to be passed to callback function (pcd*).
 */
void dwc_otg_cil_register_pcd_callbacks(dwc_otg_core_if_t *core_if,
					dwc_otg_cil_callbacks_t *cb, void *p)
{
	core_if->pcd_cb = cb;
	cb->p = p;
}

#ifdef DWC_EN_ISOC

/**
 * This function writes isoc data per 1 (micro)frame into tx fifo
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param ep The EP to start the transfer on.
 *
 */
void write_isoc_frame_data(dwc_otg_core_if_t *core_if, dwc_ep_t *ep)
{
	dwc_otg_dev_in_ep_regs_t *ep_regs;
	dtxfsts_data_t txstatus = {.d32 = 0 };
	uint32_t len = 0;
	uint32_t dwords;

	ep->xfer_len = ep->data_per_frame;
	ep->xfer_count = 0;

	ep_regs = core_if->dev_if->in_ep_regs[ep->num];

	len = ep->xfer_len - ep->xfer_count;

	if (len > ep->maxpacket) {
		len = ep->maxpacket;
	}

	dwords = (len + 3) / 4;

	/* While there is space in the queue and space in the FIFO and
	 * More data to tranfer, Write packets to the Tx FIFO */
	txstatus.d32 =
	    DWC_READ_REG32(&core_if->dev_if->in_ep_regs[ep->num]->dtxfsts);
	DWC_DEBUGPL(DBG_PCDV, "b4 dtxfsts[%d]=0x%08x\n", ep->num, txstatus.d32);

	while (txstatus.b.txfspcavail > dwords &&
	       ep->xfer_count < ep->xfer_len && ep->xfer_len != 0) {
		/* Write the FIFO */
		dwc_otg_ep_write_packet(core_if, ep, 0);

		len = ep->xfer_len - ep->xfer_count;
		if (len > ep->maxpacket) {
			len = ep->maxpacket;
		}

		dwords = (len + 3) / 4;
		txstatus.d32 =
		    DWC_READ_REG32(&core_if->dev_if->
				   in_ep_regs[ep->num]->dtxfsts);
		DWC_DEBUGPL(DBG_PCDV, "dtxfsts[%d]=0x%08x\n", ep->num,
			    txstatus.d32);
	}
}

/**
 * This function initializes a descriptor chain for Isochronous transfer
 *
 * @param core_if Programming view of DWC_otg controller.
 * @param ep The EP to start the transfer on.
 *
 */
void dwc_otg_iso_ep_start_frm_transfer(dwc_otg_core_if_t *core_if,
				       dwc_ep_t *ep)
{
	deptsiz_data_t deptsiz = {.d32 = 0 };
	depctl_data_t depctl = {.d32 = 0 };
	dsts_data_t dsts = {.d32 = 0 };
	volatile uint32_t *addr;

	if (ep->is_in)
		addr = &core_if->dev_if->in_ep_regs[ep->num]->diepctl;
	else
		addr = &core_if->dev_if->out_ep_regs[ep->num]->doepctl;

	ep->xfer_len = ep->data_per_frame;
	ep->xfer_count = 0;
	ep->xfer_buff = ep->cur_pkt_addr;
	ep->dma_addr = ep->cur_pkt_dma_addr;

	if (ep->is_in) {
		/* Program the transfer size and packet count
		 *	as follows: xfersize = N * maxpacket +
		 *	short_packet pktcnt = N + (short_packet
		 *	exist ? 1 : 0)
		 */
		deptsiz.b.xfersize = ep->xfer_len;
		deptsiz.b.pktcnt =
		    (ep->xfer_len - 1 + ep->maxpacket) / ep->maxpacket;
		deptsiz.b.mc = deptsiz.b.pktcnt;
		DWC_WRITE_REG32(&core_if->dev_if->in_ep_regs[ep->num]->dieptsiz,
				deptsiz.d32);

		/* Write the DMA register */
		if (core_if->dma_enable) {
			DWC_WRITE_REG32(&
					(core_if->dev_if->
					 in_ep_regs[ep->num]->diepdma),
					(uint32_t) ep->dma_addr);
		}
	} else {
		deptsiz.b.pktcnt =
		    (ep->xfer_len + (ep->maxpacket - 1)) / ep->maxpacket;
		deptsiz.b.xfersize = deptsiz.b.pktcnt * ep->maxpacket;

		DWC_WRITE_REG32(&core_if->dev_if->out_ep_regs[ep->num]->
				doeptsiz, deptsiz.d32);

		if (core_if->dma_enable) {
			DWC_WRITE_REG32(&
					(core_if->dev_if->out_ep_regs[ep->num]->
					 doepdma), (uint32_t) ep->dma_addr);
		}
	}

	/** Enable endpoint, clear nak  */

	depctl.d32 = 0;
	if (ep->bInterval == 1) {
		dsts.d32 =
		    DWC_READ_REG32(&core_if->dev_if->dev_global_regs->dsts);
		ep->next_frame = dsts.b.soffn + ep->bInterval;

		if (ep->next_frame & 0x1)
			depctl.b.setd1pid = 1;
		else
			depctl.b.setd0pid = 1;
	} else {
		ep->next_frame += ep->bInterval;

		if (ep->next_frame & 0x1)
			depctl.b.setd1pid = 1;
		else
			depctl.b.setd0pid = 1;
	}
	depctl.b.epena = 1;
	depctl.b.cnak = 1;

	DWC_MODIFY_REG32(addr, 0, depctl.d32);
	depctl.d32 = DWC_READ_REG32(addr);

	if (ep->is_in && core_if->dma_enable == 0) {
		write_isoc_frame_data(core_if, ep);
	}

}
#endif /* DWC_EN_ISOC */

static void dwc_otg_set_uninitialized(int32_t *p, int size)
{
	int i;
	for (i = 0; i < size; i++) {
		p[i] = -1;
	}
}

static int dwc_otg_param_initialized(int32_t val)
{
	return val != -1;
}

static int dwc_otg_setup_params(dwc_otg_core_if_t *core_if)
{
	gintsts_data_t gintsts;
	gintsts.d32 = DWC_READ_REG32(&core_if->core_global_regs->gintsts);

	core_if->core_params = DWC_ALLOC(sizeof(*core_if->core_params));
	if (!core_if->core_params)
		return -DWC_E_NO_MEMORY;
	dwc_otg_set_uninitialized((int32_t *) core_if->core_params,
				  sizeof(*core_if->core_params) /
				  sizeof(int32_t));
	DWC_PRINTF("Setting default values for core params\n");
	dwc_otg_set_param_otg_cap(core_if, dwc_param_otg_cap_default);
	dwc_otg_set_param_dma_enable(core_if, dwc_param_dma_enable_default);
	dwc_otg_set_param_dma_desc_enable(core_if,
					  dwc_param_dma_desc_enable_default);
	dwc_otg_set_param_opt(core_if, dwc_param_opt_default);
	dwc_otg_set_param_dma_burst_size(core_if,
					 dwc_param_dma_burst_size_default);
	dwc_otg_set_param_host_support_fs_ls_low_power(core_if,
						       dwc_param_host_support_fs_ls_low_power_default);
	dwc_otg_set_param_enable_dynamic_fifo(core_if,
					      dwc_param_enable_dynamic_fifo_default);
	dwc_otg_set_param_data_fifo_size(core_if,
					 dwc_param_data_fifo_size_default);
	dwc_otg_set_param_dev_rx_fifo_size(core_if,
					   dwc_param_dev_rx_fifo_size_default);
	dwc_otg_set_param_dev_nperio_tx_fifo_size(core_if,
						  dwc_param_dev_nperio_tx_fifo_size_default);
	dwc_otg_set_param_host_rx_fifo_size(core_if,
					    dwc_param_host_rx_fifo_size_default);
	dwc_otg_set_param_host_nperio_tx_fifo_size(core_if,
						   dwc_param_host_nperio_tx_fifo_size_default);
	dwc_otg_set_param_host_perio_tx_fifo_size(core_if,
						  dwc_param_host_perio_tx_fifo_size_default);
	dwc_otg_set_param_max_transfer_size(core_if,
					    dwc_param_max_transfer_size_default);
	dwc_otg_set_param_max_packet_count(core_if,
					   dwc_param_max_packet_count_default);
	dwc_otg_set_param_host_channels(core_if,
					dwc_param_host_channels_default);
	dwc_otg_set_param_dev_endpoints(core_if,
					dwc_param_dev_endpoints_default);
	dwc_otg_set_param_phy_type(core_if, dwc_param_phy_type_default);
	dwc_otg_set_param_speed(core_if, dwc_param_speed_default);
	dwc_otg_set_param_host_ls_low_power_phy_clk(core_if,
						    dwc_param_host_ls_low_power_phy_clk_default);
	dwc_otg_set_param_phy_ulpi_ddr(core_if, dwc_param_phy_ulpi_ddr_default);
	dwc_otg_set_param_phy_ulpi_ext_vbus(core_if,
					    dwc_param_phy_ulpi_ext_vbus_default);
	dwc_otg_set_param_phy_utmi_width(core_if,
					 dwc_param_phy_utmi_width_default);
	dwc_otg_set_param_ts_dline(core_if, dwc_param_ts_dline_default);
	dwc_otg_set_param_i2c_enable(core_if, dwc_param_i2c_enable_default);
	dwc_otg_set_param_ulpi_fs_ls(core_if, dwc_param_ulpi_fs_ls_default);
	dwc_otg_set_param_en_multiple_tx_fifo(core_if,
					      dwc_param_en_multiple_tx_fifo_default);

	/* do not set dev_perio_tx_fifo_size and dev_tx_fifo_size here
	 * set validate parameter values in "set_parameters" later.
	 */
#if 0
	if (gintsts.b.curmode) {
		/* Force device mode to get power-on values of device FIFOs */
		gusbcfg_data_t gusbcfg = {.d32 = 0 };
		gusbcfg.d32 =
		    DWC_READ_REG32(&core_if->core_global_regs->gusbcfg);
		gusbcfg.b.force_dev_mode = 1;
		DWC_WRITE_REG32(&core_if->core_global_regs->gusbcfg,
				gusbcfg.d32);
		dwc_mdelay(100);
		for (i = 0; i < 15; i++) {
			dwc_otg_set_param_dev_perio_tx_fifo_size(core_if,
								 dwc_param_dev_perio_tx_fifo_size_default,
								 i);
		}
		for (i = 0; i < 15; i++) {
			dwc_otg_set_param_dev_tx_fifo_size(core_if,
							   dwc_param_dev_tx_fifo_size_default,
							   i);
		}
		gusbcfg.d32 =
		    DWC_READ_REG32(&core_if->core_global_regs->gusbcfg);
		gusbcfg.b.force_dev_mode = 0;
		DWC_WRITE_REG32(&core_if->core_global_regs->gusbcfg,
				gusbcfg.d32);
		dwc_mdelay(100);
	} else {
		for (i = 0; i < 15; i++) {
			dwc_otg_set_param_dev_perio_tx_fifo_size(core_if,
								 dwc_param_dev_perio_tx_fifo_size_default,
								 i);
		}
		for (i = 0; i < 15; i++) {
			dwc_otg_set_param_dev_tx_fifo_size(core_if,
							   dwc_param_dev_tx_fifo_size_default,
							   i);
		}
	}
#endif
	dwc_otg_set_param_thr_ctl(core_if, dwc_param_thr_ctl_default);
	dwc_otg_set_param_mpi_enable(core_if, dwc_param_mpi_enable_default);
	dwc_otg_set_param_pti_enable(core_if, dwc_param_pti_enable_default);
	dwc_otg_set_param_lpm_enable(core_if, dwc_param_lpm_enable_default);

	dwc_otg_set_param_besl_enable(core_if, dwc_param_besl_enable_default);
	dwc_otg_set_param_baseline_besl(core_if,
					dwc_param_baseline_besl_default);
	dwc_otg_set_param_deep_besl(core_if, dwc_param_deep_besl_default);

	dwc_otg_set_param_ic_usb_cap(core_if, dwc_param_ic_usb_cap_default);
	dwc_otg_set_param_tx_thr_length(core_if,
					dwc_param_tx_thr_length_default);
	dwc_otg_set_param_rx_thr_length(core_if,
					dwc_param_rx_thr_length_default);
	dwc_otg_set_param_ahb_thr_ratio(core_if,
					dwc_param_ahb_thr_ratio_default);
	dwc_otg_set_param_power_down(core_if, dwc_param_power_down_default);
	dwc_otg_set_param_reload_ctl(core_if, dwc_param_reload_ctl_default);
	dwc_otg_set_param_dev_out_nak(core_if, dwc_param_dev_out_nak_default);
	dwc_otg_set_param_cont_on_bna(core_if, dwc_param_cont_on_bna_default);
	dwc_otg_set_param_ahb_single(core_if, dwc_param_ahb_single_default);
	dwc_otg_set_param_otg_ver(core_if, dwc_param_otg_ver_default);
	dwc_otg_set_param_adp_enable(core_if, dwc_param_adp_enable_default);
	return 0;
}

uint8_t dwc_otg_is_dma_enable(dwc_otg_core_if_t *core_if)
{
	return core_if->dma_enable;
}

/* Checks if the parameter is outside of its valid range of values */
#define DWC_OTG_PARAM_TEST(_param_, _low_, _high_) \
		(((_param_) < (_low_)) || \
		((_param_) > (_high_)))

/* Parameter access functions */
int dwc_otg_set_param_otg_cap(dwc_otg_core_if_t *core_if, int32_t val)
{
	int valid;
	int retval = 0;
	if (DWC_OTG_PARAM_TEST(val, 0, 2)) {
		DWC_WARN("Wrong value for otg_cap parameter\n");
		DWC_WARN("otg_cap parameter must be 0,1 or 2\n");
		retval = -DWC_E_INVALID;
		goto out;
	}

	valid = 1;
	switch (val) {
	case DWC_OTG_CAP_PARAM_HNP_SRP_CAPABLE:
		if (core_if->hwcfg2.b.op_mode !=
		    DWC_HWCFG2_OP_MODE_HNP_SRP_CAPABLE_OTG)
			valid = 0;
		break;
	case DWC_OTG_CAP_PARAM_SRP_ONLY_CAPABLE:
		if ((core_if->hwcfg2.b.op_mode !=
		     DWC_HWCFG2_OP_MODE_HNP_SRP_CAPABLE_OTG)
		    && (core_if->hwcfg2.b.op_mode !=
			DWC_HWCFG2_OP_MODE_SRP_ONLY_CAPABLE_OTG)
		    && (core_if->hwcfg2.b.op_mode !=
			DWC_HWCFG2_OP_MODE_SRP_CAPABLE_DEVICE)
		    && (core_if->hwcfg2.b.op_mode !=
			DWC_HWCFG2_OP_MODE_SRP_CAPABLE_HOST)) {
			valid = 0;
		}
		break;
	case DWC_OTG_CAP_PARAM_NO_HNP_SRP_CAPABLE:
		/* always valid */
		break;
	}
	if (!valid) {
		if (dwc_otg_param_initialized(core_if->core_params->otg_cap)) {
			DWC_ERROR
			    ("%d invalid for otg_cap paremter. Check HW configuration.\n",
			     val);
		}
		val =
		    (((core_if->hwcfg2.b.op_mode ==
		       DWC_HWCFG2_OP_MODE_HNP_SRP_CAPABLE_OTG)
		      || (core_if->hwcfg2.b.op_mode ==
			  DWC_HWCFG2_OP_MODE_SRP_ONLY_CAPABLE_OTG)
		      || (core_if->hwcfg2.b.op_mode ==
			  DWC_HWCFG2_OP_MODE_SRP_CAPABLE_DEVICE)
		      || (core_if->hwcfg2.b.op_mode ==
			  DWC_HWCFG2_OP_MODE_SRP_CAPABLE_HOST)) ?
		     DWC_OTG_CAP_PARAM_SRP_ONLY_CAPABLE :
		     DWC_OTG_CAP_PARAM_NO_HNP_SRP_CAPABLE);
		retval = -DWC_E_INVALID;
	}

	core_if->core_params->otg_cap = val;
out:
	return retval;
}

int32_t dwc_otg_get_param_otg_cap(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->otg_cap;
}

int dwc_otg_set_param_opt(dwc_otg_core_if_t *core_if, int32_t val)
{
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("Wrong value for opt parameter\n");
		return -DWC_E_INVALID;
	}
	core_if->core_params->opt = val;
	return 0;
}

int32_t dwc_otg_get_param_opt(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->opt;
}

int dwc_otg_set_param_dma_enable(dwc_otg_core_if_t *core_if, int32_t val)
{
	int retval = 0;
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("Wrong value for dma enable\n");
		return -DWC_E_INVALID;
	}

	if ((val == 1) && (core_if->hwcfg2.b.architecture == 0)) {
		if (dwc_otg_param_initialized(core_if->core_params->dma_enable)) {
			DWC_ERROR
			    ("%d invalid for dma_enable paremter. Check HW configuration.\n",
			     val);
		}
		val = 0;
		retval = -DWC_E_INVALID;
	}

	core_if->core_params->dma_enable = val;
	if (val == 0) {
		dwc_otg_set_param_dma_desc_enable(core_if, 0);
	}
	return retval;
}

int32_t dwc_otg_get_param_dma_enable(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->dma_enable;
}

int dwc_otg_set_param_dma_desc_enable(dwc_otg_core_if_t *core_if, int32_t val)
{
	int retval = 0;
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("Wrong value for dma_enable\n");
		DWC_WARN("dma_desc_enable must be 0 or 1\n");
		return -DWC_E_INVALID;
	}

	if ((val == 1)
	    && ((dwc_otg_get_param_dma_enable(core_if) == 0)
		|| (core_if->hwcfg4.b.desc_dma == 0))) {
		if (dwc_otg_param_initialized
		    (core_if->core_params->dma_desc_enable)) {
			DWC_ERROR
			    ("%d invalid for dma_desc_enable paremter. Check HW configuration.\n",
			     val);
		}
		val = 0;
		retval = -DWC_E_INVALID;
	}
	core_if->core_params->dma_desc_enable = val;
	return retval;
}

int32_t dwc_otg_get_param_dma_desc_enable(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->dma_desc_enable;
}

int dwc_otg_set_param_host_support_fs_ls_low_power(dwc_otg_core_if_t *core_if,
						   int32_t val)
{
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("Wrong value for host_support_fs_low_power\n");
		DWC_WARN("host_support_fs_low_power must be 0 or 1\n");
		return -DWC_E_INVALID;
	}
	core_if->core_params->host_support_fs_ls_low_power = val;
	return 0;
}

int32_t dwc_otg_get_param_host_support_fs_ls_low_power(dwc_otg_core_if_t
						       *core_if)
{
	return core_if->core_params->host_support_fs_ls_low_power;
}

int dwc_otg_set_param_enable_dynamic_fifo(dwc_otg_core_if_t *core_if,
					  int32_t val)
{
	int retval = 0;
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("Wrong value for enable_dynamic_fifo\n");
		DWC_WARN("enable_dynamic_fifo must be 0 or 1\n");
		return -DWC_E_INVALID;
	}

	if ((val == 1) && (core_if->hwcfg2.b.dynamic_fifo == 0)) {
		if (dwc_otg_param_initialized
		    (core_if->core_params->enable_dynamic_fifo)) {
			DWC_ERROR
			    ("%d invalid for enable_dynamic_fifo paremter. Check HW configuration.\n",
			     val);
		}
		val = 0;
		retval = -DWC_E_INVALID;
	}
	core_if->core_params->enable_dynamic_fifo = val;
	return retval;
}

int32_t dwc_otg_get_param_enable_dynamic_fifo(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->enable_dynamic_fifo;
}

int dwc_otg_set_param_data_fifo_size(dwc_otg_core_if_t *core_if, int32_t val)
{
	int retval = 0;
	if (DWC_OTG_PARAM_TEST(val, 32, 32768)) {
		DWC_WARN("Wrong value for data_fifo_size\n");
		DWC_WARN("data_fifo_size must be 32-32768\n");
		return -DWC_E_INVALID;
	}

	if (val > core_if->hwcfg3.b.dfifo_depth) {
		if (dwc_otg_param_initialized
		    (core_if->core_params->data_fifo_size)) {
			DWC_ERROR
			    ("%d invalid for data_fifo_size parameter. Check HW configuration.\n",
			     val);
		}
		val = core_if->hwcfg3.b.dfifo_depth;
		retval = -DWC_E_INVALID;
	}

	core_if->core_params->data_fifo_size = val;
	return retval;
}

int32_t dwc_otg_get_param_data_fifo_size(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->data_fifo_size;
}

int dwc_otg_set_param_dev_rx_fifo_size(dwc_otg_core_if_t *core_if, int32_t val)
{
	int retval = 0;
	if (DWC_OTG_PARAM_TEST(val, 16, 32768)) {
		DWC_WARN("Wrong value for dev_rx_fifo_size\n");
		DWC_WARN("dev_rx_fifo_size must be 16-32768\n");
		return -DWC_E_INVALID;
	}

	if (val > DWC_READ_REG32(&core_if->core_global_regs->grxfsiz)) {
		if (dwc_otg_param_initialized
		    (core_if->core_params->dev_rx_fifo_size)) {
			DWC_WARN("%d invalid for dev_rx_fifo_size parameter\n",
				 val);
		}
		val = DWC_READ_REG32(&core_if->core_global_regs->grxfsiz);
		retval = -DWC_E_INVALID;
	}

	core_if->core_params->dev_rx_fifo_size = val;
	return retval;
}

int32_t dwc_otg_get_param_dev_rx_fifo_size(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->dev_rx_fifo_size;
}

int dwc_otg_set_param_dev_nperio_tx_fifo_size(dwc_otg_core_if_t *core_if,
					      int32_t val)
{
	int retval = 0;

	if (DWC_OTG_PARAM_TEST(val, 16, 32768)) {
		DWC_WARN("Wrong value for dev_nperio_tx_fifo\n");
		DWC_WARN("dev_nperio_tx_fifo must be 16-32768\n");
		return -DWC_E_INVALID;
	}

	if (val > (DWC_READ_REG32(&core_if->core_global_regs->gnptxfsiz) >> 16)) {
		if (dwc_otg_param_initialized
		    (core_if->core_params->dev_nperio_tx_fifo_size)) {
			DWC_ERROR
			    ("%d invalid for dev_nperio_tx_fifo_size. Check HW configuration.\n",
			     val);
		}
		val =
		    (DWC_READ_REG32(&core_if->core_global_regs->gnptxfsiz) >>
		     16);
		retval = -DWC_E_INVALID;
	}

	core_if->core_params->dev_nperio_tx_fifo_size = val;
	return retval;
}

int32_t dwc_otg_get_param_dev_nperio_tx_fifo_size(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->dev_nperio_tx_fifo_size;
}

int dwc_otg_set_param_host_rx_fifo_size(dwc_otg_core_if_t *core_if,
					int32_t val)
{
	int retval = 0;

	if (DWC_OTG_PARAM_TEST(val, 16, 32768)) {
		DWC_WARN("Wrong value for host_rx_fifo_size\n");
		DWC_WARN("host_rx_fifo_size must be 16-32768\n");
		return -DWC_E_INVALID;
	}

	if (val > DWC_READ_REG32(&core_if->core_global_regs->grxfsiz)) {
		if (dwc_otg_param_initialized
		    (core_if->core_params->host_rx_fifo_size)) {
			DWC_ERROR
			    ("%d invalid for host_rx_fifo_size. Check HW configuration.\n",
			     val);
		}
		val = DWC_READ_REG32(&core_if->core_global_regs->grxfsiz);
		retval = -DWC_E_INVALID;
	}

	core_if->core_params->host_rx_fifo_size = val;
	return retval;

}

int32_t dwc_otg_get_param_host_rx_fifo_size(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->host_rx_fifo_size;
}

int dwc_otg_set_param_host_nperio_tx_fifo_size(dwc_otg_core_if_t *core_if,
					       int32_t val)
{
	int retval = 0;

	if (DWC_OTG_PARAM_TEST(val, 16, 32768)) {
		DWC_WARN("Wrong value for host_nperio_tx_fifo_size\n");
		DWC_WARN("host_nperio_tx_fifo_size must be 16-32768\n");
		return -DWC_E_INVALID;
	}

	if (val > (DWC_READ_REG32(&core_if->core_global_regs->gnptxfsiz) >> 16)) {
		if (dwc_otg_param_initialized
		    (core_if->core_params->host_nperio_tx_fifo_size)) {
			DWC_ERROR
			    ("%d invalid for host_nperio_tx_fifo_size. Check HW configuration.\n",
			     val);
		}
		val =
		    (DWC_READ_REG32(&core_if->core_global_regs->gnptxfsiz) >>
		     16);
		retval = -DWC_E_INVALID;
	}

	core_if->core_params->host_nperio_tx_fifo_size = val;
	return retval;
}

int32_t dwc_otg_get_param_host_nperio_tx_fifo_size(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->host_nperio_tx_fifo_size;
}

int dwc_otg_set_param_host_perio_tx_fifo_size(dwc_otg_core_if_t *core_if,
					      int32_t val)
{
	int retval = 0;
	if (DWC_OTG_PARAM_TEST(val, 16, 32768)) {
		DWC_WARN("Wrong value for host_perio_tx_fifo_size\n");
		DWC_WARN("host_perio_tx_fifo_size must be 16-32768\n");
		return -DWC_E_INVALID;
	}

	if (val > ((core_if->hptxfsiz.d32) >> 16)) {
		if (dwc_otg_param_initialized
		    (core_if->core_params->host_perio_tx_fifo_size)) {
			DWC_ERROR
			    ("%d invalid for host_perio_tx_fifo_size. Check HW configuration.\n",
			     val);
		}
		val = (core_if->hptxfsiz.d32) >> 16;
		retval = -DWC_E_INVALID;
	}

	core_if->core_params->host_perio_tx_fifo_size = val;
	return retval;
}

int32_t dwc_otg_get_param_host_perio_tx_fifo_size(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->host_perio_tx_fifo_size;
}

int dwc_otg_set_param_max_transfer_size(dwc_otg_core_if_t *core_if,
					int32_t val)
{
	int retval = 0;

	if (DWC_OTG_PARAM_TEST(val, 2047, 524288)) {
		DWC_WARN("Wrong value for max_transfer_size\n");
		DWC_WARN("max_transfer_size must be 2047-524288\n");
		return -DWC_E_INVALID;
	}

	if (val >= (1 << (core_if->hwcfg3.b.xfer_size_cntr_width + 11))) {
		if (dwc_otg_param_initialized
		    (core_if->core_params->max_transfer_size)) {
			DWC_ERROR
			    ("%d invalid for max_transfer_size. Check HW configuration.\n",
			     val);
		}
		val =
		    ((1 << (core_if->hwcfg3.b.packet_size_cntr_width + 11)) -
		     1);
		retval = -DWC_E_INVALID;
	}

	core_if->core_params->max_transfer_size = val;
	return retval;
}

int32_t dwc_otg_get_param_max_transfer_size(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->max_transfer_size;
}

int dwc_otg_set_param_max_packet_count(dwc_otg_core_if_t *core_if, int32_t val)
{
	int retval = 0;

	if (DWC_OTG_PARAM_TEST(val, 15, 511)) {
		DWC_WARN("Wrong value for max_packet_count\n");
		DWC_WARN("max_packet_count must be 15-511\n");
		return -DWC_E_INVALID;
	}

	if (val > (1 << (core_if->hwcfg3.b.packet_size_cntr_width + 4))) {
		if (dwc_otg_param_initialized
		    (core_if->core_params->max_packet_count)) {
			DWC_ERROR
			    ("%d invalid for max_packet_count. Check HW configuration.\n",
			     val);
		}
		val =
		    ((1 << (core_if->hwcfg3.b.packet_size_cntr_width + 4)) - 1);
		retval = -DWC_E_INVALID;
	}

	core_if->core_params->max_packet_count = val;
	return retval;
}

int32_t dwc_otg_get_param_max_packet_count(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->max_packet_count;
}

int dwc_otg_set_param_host_channels(dwc_otg_core_if_t *core_if, int32_t val)
{
	int retval = 0;

	if (DWC_OTG_PARAM_TEST(val, 1, 16)) {
		DWC_WARN("Wrong value for host_channels\n");
		DWC_WARN("host_channels must be 1-16\n");
		return -DWC_E_INVALID;
	}

	if (val > (core_if->hwcfg2.b.num_host_chan + 1)) {
		if (dwc_otg_param_initialized
		    (core_if->core_params->host_channels)) {
			DWC_ERROR
			    ("%d invalid for host_channels. Check HW configurations.\n",
			     val);
		}
		val = (core_if->hwcfg2.b.num_host_chan + 1);
		retval = -DWC_E_INVALID;
	}

	core_if->core_params->host_channels = val;
	return retval;
}

int32_t dwc_otg_get_param_host_channels(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->host_channels;
}

int dwc_otg_set_param_dev_endpoints(dwc_otg_core_if_t *core_if, int32_t val)
{
	int retval = 0;

	if (DWC_OTG_PARAM_TEST(val, 1, 15)) {
		DWC_WARN("Wrong value for dev_endpoints\n");
		DWC_WARN("dev_endpoints must be 1-15\n");
		return -DWC_E_INVALID;
	}

	if (val > (core_if->hwcfg2.b.num_dev_ep)) {
		if (dwc_otg_param_initialized
		    (core_if->core_params->dev_endpoints)) {
			DWC_ERROR
			    ("%d invalid for dev_endpoints. Check HW configurations.\n",
			     val);
		}
		val = core_if->hwcfg2.b.num_dev_ep;
		retval = -DWC_E_INVALID;
	}

	core_if->core_params->dev_endpoints = val;
	return retval;
}

int32_t dwc_otg_get_param_dev_endpoints(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->dev_endpoints;
}

int dwc_otg_set_param_phy_type(dwc_otg_core_if_t *core_if, int32_t val)
{
	int retval = 0;
	int valid = 0;

	if (DWC_OTG_PARAM_TEST(val, 0, 2)) {
		DWC_WARN("Wrong value for phy_type\n");
		DWC_WARN("phy_type must be 0,1 or 2\n");
		return -DWC_E_INVALID;
	}
#ifndef NO_FS_PHY_HW_CHECKS
	if ((val == DWC_PHY_TYPE_PARAM_UTMI) &&
	    ((core_if->hwcfg2.b.hs_phy_type == 1) ||
	     (core_if->hwcfg2.b.hs_phy_type == 3))) {
		valid = 1;
	} else if ((val == DWC_PHY_TYPE_PARAM_ULPI) &&
		   ((core_if->hwcfg2.b.hs_phy_type == 2) ||
		    (core_if->hwcfg2.b.hs_phy_type == 3))) {
		valid = 1;
	} else if ((val == DWC_PHY_TYPE_PARAM_FS) &&
		   (core_if->hwcfg2.b.fs_phy_type == 1)) {
		valid = 1;
	}
	if (!valid) {
		if (dwc_otg_param_initialized(core_if->core_params->phy_type)) {
			DWC_ERROR
			    ("%d invalid for phy_type. Check HW configurations.\n",
			     val);
		}
		if (core_if->hwcfg2.b.hs_phy_type) {
			if ((core_if->hwcfg2.b.hs_phy_type == 3) ||
			    (core_if->hwcfg2.b.hs_phy_type == 1)) {
				val = DWC_PHY_TYPE_PARAM_UTMI;
			} else {
				val = DWC_PHY_TYPE_PARAM_ULPI;
			}
		}
		retval = -DWC_E_INVALID;
	}
#endif
	core_if->core_params->phy_type = val;
	return retval;
}

int32_t dwc_otg_get_param_phy_type(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->phy_type;
}

int dwc_otg_set_param_speed(dwc_otg_core_if_t *core_if, int32_t val)
{
	int retval = 0;
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("Wrong value for speed parameter\n");
		DWC_WARN("max_speed parameter must be 0 or 1\n");
		return -DWC_E_INVALID;
	}
	if ((val == 0)
	    && dwc_otg_get_param_phy_type(core_if) == DWC_PHY_TYPE_PARAM_FS) {
		if (dwc_otg_param_initialized(core_if->core_params->speed)) {
			DWC_ERROR
			    ("%d invalid for speed paremter. Check HW configuration.\n",
			     val);
		}
		val =
		    (dwc_otg_get_param_phy_type(core_if) ==
		     DWC_PHY_TYPE_PARAM_FS ? 1 : 0);
		retval = -DWC_E_INVALID;
	}
	core_if->core_params->speed = val;
	return retval;
}

int32_t dwc_otg_get_param_speed(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->speed;
}

int dwc_otg_set_param_host_ls_low_power_phy_clk(dwc_otg_core_if_t *core_if,
						int32_t val)
{
	int retval = 0;

	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN
		    ("Wrong value for host_ls_low_power_phy_clk parameter\n");
		DWC_WARN("host_ls_low_power_phy_clk must be 0 or 1\n");
		return -DWC_E_INVALID;
	}

	if ((val == DWC_HOST_LS_LOW_POWER_PHY_CLK_PARAM_48MHZ)
	    && (dwc_otg_get_param_phy_type(core_if) == DWC_PHY_TYPE_PARAM_FS)) {
		if (dwc_otg_param_initialized
		    (core_if->core_params->host_ls_low_power_phy_clk)) {
			DWC_ERROR
			    ("%d invalid for host_ls_low_power_phy_clk. Check HW configuration.\n",
			     val);
		}
		val =
		    (dwc_otg_get_param_phy_type(core_if) ==
		     DWC_PHY_TYPE_PARAM_FS) ?
		    DWC_HOST_LS_LOW_POWER_PHY_CLK_PARAM_6MHZ :
		    DWC_HOST_LS_LOW_POWER_PHY_CLK_PARAM_48MHZ;
		retval = -DWC_E_INVALID;
	}

	core_if->core_params->host_ls_low_power_phy_clk = val;
	return retval;
}

int32_t dwc_otg_get_param_host_ls_low_power_phy_clk(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->host_ls_low_power_phy_clk;
}

int dwc_otg_set_param_phy_ulpi_ddr(dwc_otg_core_if_t *core_if, int32_t val)
{
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("Wrong value for phy_ulpi_ddr\n");
		DWC_WARN("phy_upli_ddr must be 0 or 1\n");
		return -DWC_E_INVALID;
	}

	core_if->core_params->phy_ulpi_ddr = val;
	return 0;
}

int32_t dwc_otg_get_param_phy_ulpi_ddr(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->phy_ulpi_ddr;
}

int dwc_otg_set_param_phy_ulpi_ext_vbus(dwc_otg_core_if_t *core_if,
					int32_t val)
{
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("Wrong valaue for phy_ulpi_ext_vbus\n");
		DWC_WARN("phy_ulpi_ext_vbus must be 0 or 1\n");
		return -DWC_E_INVALID;
	}

	core_if->core_params->phy_ulpi_ext_vbus = val;
	return 0;
}

int32_t dwc_otg_get_param_phy_ulpi_ext_vbus(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->phy_ulpi_ext_vbus;
}

int dwc_otg_set_param_phy_utmi_width(dwc_otg_core_if_t *core_if, int32_t val)
{
	if (DWC_OTG_PARAM_TEST(val, 8, 8) && DWC_OTG_PARAM_TEST(val, 16, 16)) {
		DWC_WARN("Wrong valaue for phy_utmi_width\n");
		DWC_WARN("phy_utmi_width must be 8 or 16\n");
		return -DWC_E_INVALID;
	}

	core_if->core_params->phy_utmi_width = val;
	return 0;
}

int32_t dwc_otg_get_param_phy_utmi_width(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->phy_utmi_width;
}

int dwc_otg_set_param_ulpi_fs_ls(dwc_otg_core_if_t *core_if, int32_t val)
{
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("Wrong valaue for ulpi_fs_ls\n");
		DWC_WARN("ulpi_fs_ls must be 0 or 1\n");
		return -DWC_E_INVALID;
	}

	core_if->core_params->ulpi_fs_ls = val;
	return 0;
}

int32_t dwc_otg_get_param_ulpi_fs_ls(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->ulpi_fs_ls;
}

int dwc_otg_set_param_ts_dline(dwc_otg_core_if_t *core_if, int32_t val)
{
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("Wrong valaue for ts_dline\n");
		DWC_WARN("ts_dline must be 0 or 1\n");
		return -DWC_E_INVALID;
	}

	core_if->core_params->ts_dline = val;
	return 0;
}

int32_t dwc_otg_get_param_ts_dline(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->ts_dline;
}

int dwc_otg_set_param_i2c_enable(dwc_otg_core_if_t *core_if, int32_t val)
{
	int retval = 0;
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("Wrong valaue for i2c_enable\n");
		DWC_WARN("i2c_enable must be 0 or 1\n");
		return -DWC_E_INVALID;
	}
#ifndef NO_FS_PHY_HW_CHECK
	if (val == 1 && core_if->hwcfg3.b.i2c == 0) {
		if (dwc_otg_param_initialized(core_if->core_params->i2c_enable)) {
			DWC_ERROR
			    ("%d invalid for i2c_enable. Check HW configuration.\n",
			     val);
		}
		val = 0;
		retval = -DWC_E_INVALID;
	}
#endif

	core_if->core_params->i2c_enable = val;
	return retval;
}

int32_t dwc_otg_get_param_i2c_enable(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->i2c_enable;
}

int dwc_otg_set_param_dev_perio_tx_fifo_size(dwc_otg_core_if_t *core_if,
					     int32_t val, int fifo_num)
{
	int retval = 0;
	gintsts_data_t gintsts;
	gintsts.d32 = DWC_READ_REG32(&core_if->core_global_regs->gintsts);

	if (core_if->hwcfg4.b.ded_fifo_en == 0) {
		if (DWC_OTG_PARAM_TEST(val, 4, 768)) {
			DWC_WARN("Wrong value for dev_perio_tx_fifo_size\n");
			DWC_WARN("dev_perio_tx_fifo_size must be 4-768\n");
			return -DWC_E_INVALID;
		}

		if (val >
		    (DWC_READ_REG32
		     (&core_if->core_global_regs->dtxfsiz[fifo_num]) >> 16)) {
			printk("%d   ",
			       DWC_READ_REG32(&core_if->core_global_regs->
					      dtxfsiz[fifo_num]) >> 16);
			printk("val = %d fifo_num = %d\n", val, fifo_num);
			DWC_WARN("Value is larger then power-on FIFO size\n");
			if (dwc_otg_param_initialized
			    (core_if->core_params->
			     dev_perio_tx_fifo_size[fifo_num])) {
				DWC_ERROR
				    ("`%d' invalid for parameter `dev_perio_fifo_size_%d'. Check HW configuration.\n",
				     val, fifo_num);
			}
			val =
			    (DWC_READ_REG32
			     (&core_if->core_global_regs->
			      dtxfsiz[fifo_num]) >> 16);
			retval = -DWC_E_INVALID;
		}

		core_if->core_params->dev_perio_tx_fifo_size[fifo_num] = val;
	}
	return retval;
}

int32_t dwc_otg_get_param_dev_perio_tx_fifo_size(dwc_otg_core_if_t *core_if,
						 int fifo_num)
{
	return core_if->core_params->dev_perio_tx_fifo_size[fifo_num];
}

int dwc_otg_set_param_en_multiple_tx_fifo(dwc_otg_core_if_t *core_if,
					  int32_t val)
{
	int retval = 0;
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("Wrong valaue for en_multiple_tx_fifo,\n");
		DWC_WARN("en_multiple_tx_fifo must be 0 or 1\n");
		return -DWC_E_INVALID;
	}

	if (val == 1 && core_if->hwcfg4.b.ded_fifo_en == 0) {
		if (dwc_otg_param_initialized
		    (core_if->core_params->en_multiple_tx_fifo)) {
			DWC_ERROR
			    ("%d invalid for parameter en_multiple_tx_fifo. Check HW configuration.\n",
			     val);
		}
		val = 0;
		retval = -DWC_E_INVALID;
	}

	core_if->core_params->en_multiple_tx_fifo = val;
	return retval;
}

int32_t dwc_otg_get_param_en_multiple_tx_fifo(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->en_multiple_tx_fifo;
}

int dwc_otg_set_param_dev_tx_fifo_size(dwc_otg_core_if_t *core_if, int32_t val,
				       int fifo_num)
{
	int retval = 0;
	fifosize_data_t txfifosize;
	txfifosize.d32 =
	    DWC_READ_REG32(&core_if->core_global_regs->dtxfsiz[fifo_num]);

	if (DWC_OTG_PARAM_TEST(val, 16, 32768)) {
		DWC_WARN("Wrong value for dev_tx_fifo_size\n");
		DWC_WARN("dev_tx_fifo_size must be 16-32768\n");
		return -DWC_E_INVALID;
	}

	core_if->core_params->dev_tx_fifo_size[fifo_num] = val;
	return retval;
}

int32_t dwc_otg_get_param_dev_tx_fifo_size(dwc_otg_core_if_t *core_if,
					   int fifo_num)
{
	return core_if->core_params->dev_tx_fifo_size[fifo_num];
}

int dwc_otg_set_param_thr_ctl(dwc_otg_core_if_t *core_if, int32_t val)
{
	int retval = 0;

	if (DWC_OTG_PARAM_TEST(val, 0, 7)) {
		DWC_WARN("Wrong value for thr_ctl\n");
		DWC_WARN("thr_ctl must be 0-7\n");
		return -DWC_E_INVALID;
	}

	if ((val != 0) &&
	    (!dwc_otg_get_param_dma_enable(core_if) ||
	     !core_if->hwcfg4.b.ded_fifo_en)) {
		if (dwc_otg_param_initialized(core_if->core_params->thr_ctl)) {
			DWC_ERROR
			    ("%d invalid for parameter thr_ctl. Check HW configuration.\n",
			     val);
		}
		val = 0;
		retval = -DWC_E_INVALID;
	}

	core_if->core_params->thr_ctl = val;
	return retval;
}

int32_t dwc_otg_get_param_thr_ctl(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->thr_ctl;
}

int dwc_otg_set_param_lpm_enable(dwc_otg_core_if_t *core_if, int32_t val)
{
	int retval = 0;

	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("Wrong value for lpm_enable\n");
		DWC_WARN("lpm_enable must be 0 or 1\n");
		return -DWC_E_INVALID;
	}

	if (val && !core_if->hwcfg3.b.otg_lpm_en) {
		if (dwc_otg_param_initialized(core_if->core_params->lpm_enable)) {
			DWC_ERROR
			    ("%d invalid for parameter lpm_enable. Check HW configuration.\n",
			     val);
		}
		val = 0;
		retval = -DWC_E_INVALID;
	}

	core_if->core_params->lpm_enable = val;
	return retval;
}

int32_t dwc_otg_get_param_lpm_enable(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->lpm_enable;
}

int dwc_otg_set_param_besl_enable(dwc_otg_core_if_t *core_if, int32_t val)
{
	int retval = 0;

	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("Wrong value for besl_enable\n");
		DWC_WARN("besl_enable must be 0 or 1\n");
		return -DWC_E_INVALID;
	}

	core_if->core_params->besl_enable = val;

	if (val) {
		retval += dwc_otg_set_param_lpm_enable(core_if, val);
	}

	return retval;
}

int32_t dwc_otg_get_param_besl_enable(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->besl_enable;
}

int dwc_otg_set_param_baseline_besl(dwc_otg_core_if_t *core_if, int32_t val)
{
	int retval = 0;

	if (DWC_OTG_PARAM_TEST(val, 0, 15)) {
		DWC_WARN("Wrong value for baseline_besl\n");
		DWC_WARN("baseline_besl must be 0-15\n");
		return -DWC_E_INVALID;
	}

	core_if->core_params->baseline_besl = val;
	return retval;
}

int32_t dwc_otg_get_param_baseline_besl(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->baseline_besl;
}

int dwc_otg_set_param_deep_besl(dwc_otg_core_if_t *core_if, int32_t val)
{
	int retval = 0;

	if (DWC_OTG_PARAM_TEST(val, 0, 15)) {
		DWC_WARN("Wrong value for deep_besl\n");
		DWC_WARN("deep_besl must be 0-15\n");
		return -DWC_E_INVALID;
	}

	core_if->core_params->deep_besl = val;
	return retval;
}

int32_t dwc_otg_get_param_deep_besl(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->deep_besl;
}

int dwc_otg_set_param_tx_thr_length(dwc_otg_core_if_t *core_if, int32_t val)
{
	if (DWC_OTG_PARAM_TEST(val, 8, 128)) {
		DWC_WARN("Wrong valaue for tx_thr_length\n");
		DWC_WARN("tx_thr_length must be 8 - 128\n");
		return -DWC_E_INVALID;
	}

	core_if->core_params->tx_thr_length = val;
	return 0;
}

int32_t dwc_otg_get_param_tx_thr_length(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->tx_thr_length;
}

int dwc_otg_set_param_rx_thr_length(dwc_otg_core_if_t *core_if, int32_t val)
{
	if (DWC_OTG_PARAM_TEST(val, 8, 128)) {
		DWC_WARN("Wrong valaue for rx_thr_length\n");
		DWC_WARN("rx_thr_length must be 8 - 128\n");
		return -DWC_E_INVALID;
	}

	core_if->core_params->rx_thr_length = val;
	return 0;
}

int32_t dwc_otg_get_param_rx_thr_length(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->rx_thr_length;
}

int dwc_otg_set_param_dma_burst_size(dwc_otg_core_if_t *core_if, int32_t val)
{
	if (DWC_OTG_PARAM_TEST(val, 1, 1) &&
	    DWC_OTG_PARAM_TEST(val, 4, 4) &&
	    DWC_OTG_PARAM_TEST(val, 8, 8) &&
	    DWC_OTG_PARAM_TEST(val, 16, 16) &&
	    DWC_OTG_PARAM_TEST(val, 32, 32) &&
	    DWC_OTG_PARAM_TEST(val, 64, 64) &&
	    DWC_OTG_PARAM_TEST(val, 128, 128) &&
	    DWC_OTG_PARAM_TEST(val, 256, 256)) {
		DWC_WARN("`%d' invalid for parameter `dma_burst_size'\n", val);
		return -DWC_E_INVALID;
	}
	core_if->core_params->dma_burst_size = val;
	return 0;
}

int32_t dwc_otg_get_param_dma_burst_size(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->dma_burst_size;
}

int dwc_otg_set_param_pti_enable(dwc_otg_core_if_t *core_if, int32_t val)
{
	int retval = 0;
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("`%d' invalid for parameter `pti_enable'\n", val);
		return -DWC_E_INVALID;
	}
	if (val && (core_if->snpsid < OTG_CORE_REV_2_72a)) {
		if (dwc_otg_param_initialized(core_if->core_params->pti_enable)) {
			DWC_ERROR
			    ("%d invalid for parameter pti_enable. Check HW configuration.\n",
			     val);
		}
		retval = -DWC_E_INVALID;
		val = 0;
	}
	core_if->core_params->pti_enable = val;
	return retval;
}

int32_t dwc_otg_get_param_pti_enable(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->pti_enable;
}

int dwc_otg_set_param_mpi_enable(dwc_otg_core_if_t *core_if, int32_t val)
{
	int retval = 0;
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("`%d' invalid for parameter `mpi_enable'\n", val);
		return -DWC_E_INVALID;
	}
	if (val && (core_if->hwcfg2.b.multi_proc_int == 0)) {
		if (dwc_otg_param_initialized(core_if->core_params->mpi_enable)) {
			DWC_ERROR
			    ("%d invalid for parameter mpi_enable. Check HW configuration.\n",
			     val);
		}
		retval = -DWC_E_INVALID;
		val = 0;
	}
	core_if->core_params->mpi_enable = val;
	return retval;
}

int32_t dwc_otg_get_param_mpi_enable(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->mpi_enable;
}

int dwc_otg_set_param_adp_enable(dwc_otg_core_if_t *core_if, int32_t val)
{
	int retval = 0;
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("`%d' invalid for parameter `adp_enable'\n", val);
		return -DWC_E_INVALID;
	}
	if (val && (core_if->hwcfg3.b.adp_supp == 0)) {
		if (dwc_otg_param_initialized
		    (core_if->core_params->adp_supp_enable)) {
			DWC_ERROR
			    ("%d invalid for parameter adp_enable. Check HW configuration.\n",
			     val);
		}
		retval = -DWC_E_INVALID;
		val = 0;
	}
	core_if->core_params->adp_supp_enable = val;
	/* Set OTG version 2.0 in case of enabling ADP */
	if (val)
		dwc_otg_set_param_otg_ver(core_if, 1);

	return retval;
}

int32_t dwc_otg_get_param_adp_enable(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->adp_supp_enable;
}

int dwc_otg_set_param_ic_usb_cap(dwc_otg_core_if_t *core_if, int32_t val)
{
	int retval = 0;
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("`%d' invalid for parameter `ic_usb_cap'\n", val);
		DWC_WARN("ic_usb_cap must be 0 or 1\n");
		return -DWC_E_INVALID;
	}

	if (val && (core_if->hwcfg2.b.otg_enable_ic_usb == 0)) {
		if (dwc_otg_param_initialized(core_if->core_params->ic_usb_cap)) {
			DWC_ERROR
			    ("%d invalid for parameter ic_usb_cap. Check HW configuration.\n",
			     val);
		}
		retval = -DWC_E_INVALID;
		val = 0;
	}
	core_if->core_params->ic_usb_cap = val;
	return retval;
}

int32_t dwc_otg_get_param_ic_usb_cap(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->ic_usb_cap;
}

int dwc_otg_set_param_ahb_thr_ratio(dwc_otg_core_if_t *core_if, int32_t val)
{
	int retval = 0;
	int valid = 1;

	if (DWC_OTG_PARAM_TEST(val, 0, 3)) {
		DWC_WARN("`%d' invalid for parameter `ahb_thr_ratio'\n", val);
		DWC_WARN("ahb_thr_ratio must be 0 - 3\n");
		return -DWC_E_INVALID;
	}

	if (val
	    && (core_if->snpsid < OTG_CORE_REV_2_81a
		|| !dwc_otg_get_param_thr_ctl(core_if))) {
		valid = 0;
	} else if (val
		   && ((dwc_otg_get_param_tx_thr_length(core_if) / (1 << val)) <
		       4)) {
		valid = 0;
	}
	if (valid == 0) {
		if (dwc_otg_param_initialized
		    (core_if->core_params->ahb_thr_ratio)) {
			DWC_ERROR
			    ("%d invalid for parameter ahb_thr_ratio. Check HW configuration.\n",
			     val);
		}
		retval = -DWC_E_INVALID;
		val = 0;
	}

	core_if->core_params->ahb_thr_ratio = val;
	return retval;
}

int32_t dwc_otg_get_param_ahb_thr_ratio(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->ahb_thr_ratio;
}

int dwc_otg_set_param_power_down(dwc_otg_core_if_t *core_if, int32_t val)
{
	int retval = 0;
	int valid = 1;
	hwcfg4_data_t hwcfg4 = {.d32 = 0 };
	hwcfg4.d32 = DWC_READ_REG32(&core_if->core_global_regs->ghwcfg4);

	if (DWC_OTG_PARAM_TEST(val, 0, 3)) {
		DWC_WARN("`%d' invalid for parameter `power_down'\n", val);
		DWC_WARN("power_down must be 0 - 2\n");
		return -DWC_E_INVALID;
	}

	if ((val == 2) && (core_if->snpsid < OTG_CORE_REV_2_91a)) {
		valid = 0;
	}
	if ((val == 3)
	    && ((core_if->snpsid < OTG_CORE_REV_3_00a)
		|| (hwcfg4.b.xhiber == 0))) {
		valid = 0;
	}
	if (valid == 0) {
		if (dwc_otg_param_initialized(core_if->core_params->power_down)) {
			DWC_ERROR
			    ("%d invalid for parameter power_down. Check HW configuration.\n",
			     val);
		}
		retval = -DWC_E_INVALID;
		val = 0;
	}
	core_if->core_params->power_down = val;
	return retval;
}

int32_t dwc_otg_get_param_power_down(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->power_down;
}

int dwc_otg_set_param_reload_ctl(dwc_otg_core_if_t *core_if, int32_t val)
{
	int retval = 0;
	int valid = 1;

	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("`%d' invalid for parameter `reload_ctl'\n", val);
		DWC_WARN("reload_ctl must be 0 or 1\n");
		return -DWC_E_INVALID;
	}

	if ((val == 1) && (core_if->snpsid < OTG_CORE_REV_2_92a)) {
		valid = 0;
	}
	if (valid == 0) {
		if (dwc_otg_param_initialized(core_if->core_params->reload_ctl)) {
			DWC_ERROR("%d invalid for parameter reload_ctl."
				  "Check HW configuration.\n", val);
		}
		retval = -DWC_E_INVALID;
		val = 0;
	}
	core_if->core_params->reload_ctl = val;
	return retval;
}

int32_t dwc_otg_get_param_reload_ctl(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->reload_ctl;
}

int dwc_otg_set_param_dev_out_nak(dwc_otg_core_if_t *core_if, int32_t val)
{
	int retval = 0;
	int valid = 1;

	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("`%d' invalid for parameter `dev_out_nak'\n", val);
		DWC_WARN("dev_out_nak must be 0 or 1\n");
		return -DWC_E_INVALID;
	}

	if ((val == 1) && ((core_if->snpsid < OTG_CORE_REV_2_93a) ||
			   !(core_if->core_params->dma_desc_enable))) {
		valid = 0;
	}
	if (valid == 0) {
		if (dwc_otg_param_initialized
		    (core_if->core_params->dev_out_nak)) {
			DWC_ERROR("%d invalid for parameter dev_out_nak."
				  "Check HW configuration.\n", val);
		}
		retval = -DWC_E_INVALID;
		val = 0;
	}
	core_if->core_params->dev_out_nak = val;
	return retval;
}

int32_t dwc_otg_get_param_dev_out_nak(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->dev_out_nak;
}

int dwc_otg_set_param_cont_on_bna(dwc_otg_core_if_t *core_if, int32_t val)
{
	int retval = 0;
	int valid = 1;

	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("`%d' invalid for parameter `cont_on_bna'\n", val);
		DWC_WARN("cont_on_bna must be 0 or 1\n");
		return -DWC_E_INVALID;
	}

	if ((val == 1) && ((core_if->snpsid < OTG_CORE_REV_2_94a) ||
			   !(core_if->core_params->dma_desc_enable))) {
		valid = 0;
	}
	if (valid == 0) {
		if (dwc_otg_param_initialized
		    (core_if->core_params->cont_on_bna)) {
			DWC_ERROR("%d invalid for parameter cont_on_bna."
				  "Check HW configuration.\n", val);
		}
		retval = -DWC_E_INVALID;
		val = 0;
	}
	core_if->core_params->cont_on_bna = val;
	return retval;
}

int32_t dwc_otg_get_param_cont_on_bna(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->cont_on_bna;
}

int dwc_otg_set_param_ahb_single(dwc_otg_core_if_t *core_if, int32_t val)
{
	int retval = 0;
	int valid = 1;

	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("`%d' invalid for parameter `ahb_single'\n", val);
		DWC_WARN("ahb_single must be 0 or 1\n");
		return -DWC_E_INVALID;
	}

	if ((val == 1) && (core_if->snpsid < OTG_CORE_REV_2_94a)) {
		valid = 0;
	}
	if (valid == 0) {
		if (dwc_otg_param_initialized(core_if->core_params->ahb_single)) {
			DWC_ERROR("%d invalid for parameter ahb_single."
				  "Check HW configuration.\n", val);
		}
		retval = -DWC_E_INVALID;
		val = 0;
	}
	core_if->core_params->ahb_single = val;
	return retval;
}

int32_t dwc_otg_get_param_ahb_single(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->ahb_single;
}

int dwc_otg_set_param_otg_ver(dwc_otg_core_if_t *core_if, int32_t val)
{
	int retval = 0;

	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("`%d' invalid for parameter `otg_ver'\n", val);
		DWC_WARN
		    ("otg_ver must be 0(for OTG 1.3 support) or 1(for OTG 2.0 support)\n");
		return -DWC_E_INVALID;
	}

	core_if->core_params->otg_ver = val;
	return retval;
}

int32_t dwc_otg_get_param_otg_ver(dwc_otg_core_if_t *core_if)
{
	return core_if->core_params->otg_ver;
}

uint32_t dwc_otg_get_hnpstatus(dwc_otg_core_if_t *core_if)
{
	gotgctl_data_t otgctl;
	otgctl.d32 = DWC_READ_REG32(&core_if->core_global_regs->gotgctl);
	return otgctl.b.hstnegscs;
}

uint32_t dwc_otg_get_srpstatus(dwc_otg_core_if_t *core_if)
{
	gotgctl_data_t otgctl;
	otgctl.d32 = DWC_READ_REG32(&core_if->core_global_regs->gotgctl);
	return otgctl.b.sesreqscs;
}

void dwc_otg_set_hnpreq(dwc_otg_core_if_t *core_if, uint32_t val)
{
	if (core_if->otg_ver == 0) {
		gotgctl_data_t otgctl;
		otgctl.d32 =
		    DWC_READ_REG32(&core_if->core_global_regs->gotgctl);
		otgctl.b.hnpreq = val;
		DWC_WRITE_REG32(&core_if->core_global_regs->gotgctl,
				otgctl.d32);
	} else {
		core_if->otg_sts = val;
	}
}

uint32_t dwc_otg_get_gsnpsid(dwc_otg_core_if_t *core_if)
{
	return core_if->snpsid;
}

uint32_t dwc_otg_get_mode(dwc_otg_core_if_t *core_if)
{
	gintsts_data_t gintsts;
	gintsts.d32 = DWC_READ_REG32(&core_if->core_global_regs->gintsts);
	return gintsts.b.curmode;
}

uint32_t dwc_otg_get_hnpcapable(dwc_otg_core_if_t *core_if)
{
	gusbcfg_data_t usbcfg;
	usbcfg.d32 = DWC_READ_REG32(&core_if->core_global_regs->gusbcfg);
	return usbcfg.b.hnpcap;
}

void dwc_otg_set_hnpcapable(dwc_otg_core_if_t *core_if, uint32_t val)
{
	gusbcfg_data_t usbcfg;
	usbcfg.d32 = DWC_READ_REG32(&core_if->core_global_regs->gusbcfg);
	usbcfg.b.hnpcap = val;
	DWC_WRITE_REG32(&core_if->core_global_regs->gusbcfg, usbcfg.d32);
}

uint32_t dwc_otg_get_srpcapable(dwc_otg_core_if_t *core_if)
{
	gusbcfg_data_t usbcfg;
	usbcfg.d32 = DWC_READ_REG32(&core_if->core_global_regs->gusbcfg);
	return usbcfg.b.srpcap;
}

void dwc_otg_set_srpcapable(dwc_otg_core_if_t *core_if, uint32_t val)
{
	gusbcfg_data_t usbcfg;
	usbcfg.d32 = DWC_READ_REG32(&core_if->core_global_regs->gusbcfg);
	usbcfg.b.srpcap = val;
	DWC_WRITE_REG32(&core_if->core_global_regs->gusbcfg, usbcfg.d32);
}

uint32_t dwc_otg_get_devspeed(dwc_otg_core_if_t *core_if)
{
	dcfg_data_t dcfg;
	dcfg.d32 = DWC_READ_REG32(&core_if->dev_if->dev_global_regs->dcfg);
	return dcfg.b.devspd;
}

void dwc_otg_set_devspeed(dwc_otg_core_if_t *core_if, uint32_t val)
{
	dcfg_data_t dcfg;
	dcfg.d32 = DWC_READ_REG32(&core_if->dev_if->dev_global_regs->dcfg);
	dcfg.b.devspd = val;
	DWC_WRITE_REG32(&core_if->dev_if->dev_global_regs->dcfg, dcfg.d32);
}

uint32_t dwc_otg_get_busconnected(dwc_otg_core_if_t *core_if)
{
	hprt0_data_t hprt0;
	hprt0.d32 = DWC_READ_REG32(core_if->host_if->hprt0);
	return hprt0.b.prtconnsts;
}

uint32_t dwc_otg_get_enumspeed(dwc_otg_core_if_t *core_if)
{
	dsts_data_t dsts;
	dsts.d32 = DWC_READ_REG32(&core_if->dev_if->dev_global_regs->dsts);
	return dsts.b.enumspd;
}

uint32_t dwc_otg_get_prtpower(dwc_otg_core_if_t *core_if)
{
	hprt0_data_t hprt0;
	hprt0.d32 = DWC_READ_REG32(core_if->host_if->hprt0);
	return hprt0.b.prtpwr;

}

uint32_t dwc_otg_get_core_state(dwc_otg_core_if_t *core_if)
{
	return core_if->hibernation_suspend;
}

void dwc_otg_set_prtpower(dwc_otg_core_if_t *core_if, uint32_t val)
{
	hprt0_data_t hprt0;
	hprt0.d32 = dwc_otg_read_hprt0(core_if);
	hprt0.b.prtpwr = val;
	DWC_WRITE_REG32(core_if->host_if->hprt0, hprt0.d32);
}

uint32_t dwc_otg_get_prtsuspend(dwc_otg_core_if_t *core_if)
{
	hprt0_data_t hprt0;
	hprt0.d32 = DWC_READ_REG32(core_if->host_if->hprt0);
	return hprt0.b.prtsusp;

}

void dwc_otg_set_prtsuspend(dwc_otg_core_if_t *core_if, uint32_t val)
{
	hprt0_data_t hprt0;
	hprt0.d32 = dwc_otg_read_hprt0(core_if);
	hprt0.b.prtsusp = val;
	DWC_WRITE_REG32(core_if->host_if->hprt0, hprt0.d32);
}

uint32_t dwc_otg_get_fr_interval(dwc_otg_core_if_t *core_if)
{
	hfir_data_t hfir;
	hfir.d32 = DWC_READ_REG32(&core_if->host_if->host_global_regs->hfir);
	return hfir.b.frint;

}

void dwc_otg_set_fr_interval(dwc_otg_core_if_t *core_if, uint32_t val)
{
	hfir_data_t hfir;
	uint32_t fram_int;
	fram_int = calc_frame_interval(core_if);
	hfir.d32 = DWC_READ_REG32(&core_if->host_if->host_global_regs->hfir);
	if (!core_if->core_params->reload_ctl) {
		DWC_WARN("\nCannot reload HFIR register.HFIR.HFIRRldCtrl bit is"
			 "not set to 1.\nShould load driver with reload_ctl=1"
			 " module parameter\n");
		return;
	}
	switch (fram_int) {
	case 3750:
		if ((val < 3350) || (val > 4150)) {
			DWC_WARN("HFIR interval for HS core and 30 MHz"
				 "clock freq should be from 3350 to 4150\n");
			return;
		}
		break;
	case 30000:
		if ((val < 26820) || (val > 33180)) {
			DWC_WARN("HFIR interval for FS/LS core and 30 MHz"
				 "clock freq should be from 26820 to 33180\n");
			return;
		}
		break;
	case 6000:
		if ((val < 5360) || (val > 6640)) {
			DWC_WARN("HFIR interval for HS core and 48 MHz"
				 "clock freq should be from 5360 to 6640\n");
			return;
		}
		break;
	case 48000:
		if ((val < 42912) || (val > 53088)) {
			DWC_WARN("HFIR interval for FS/LS core and 48 MHz"
				 "clock freq should be from 42912 to 53088\n");
			return;
		}
		break;
	case 7500:
		if ((val < 6700) || (val > 8300)) {
			DWC_WARN("HFIR interval for HS core and 60 MHz"
				 "clock freq should be from 6700 to 8300\n");
			return;
		}
		break;
	case 60000:
		if ((val < 53640) || (val > 65536)) {
			DWC_WARN("HFIR interval for FS/LS core and 60 MHz"
				 "clock freq should be from 53640 to 65536\n");
			return;
		}
		break;
	default:
		DWC_WARN("Unknown frame interval\n");
		return;
		break;

	}
	hfir.b.frint = val;
	DWC_WRITE_REG32(&core_if->host_if->host_global_regs->hfir, hfir.d32);
}

uint32_t dwc_otg_get_mode_ch_tim(dwc_otg_core_if_t *core_if)
{
	hcfg_data_t hcfg;
	hcfg.d32 = DWC_READ_REG32(&core_if->host_if->host_global_regs->hcfg);
	return hcfg.b.modechtimen;

}

void dwc_otg_set_mode_ch_tim(dwc_otg_core_if_t *core_if, uint32_t val)
{
	hcfg_data_t hcfg;
	hcfg.d32 = DWC_READ_REG32(&core_if->host_if->host_global_regs->hcfg);
	hcfg.b.modechtimen = val;
	DWC_WRITE_REG32(&core_if->host_if->host_global_regs->hcfg, hcfg.d32);
}

void dwc_otg_set_prtresume(dwc_otg_core_if_t *core_if, uint32_t val)
{
	hprt0_data_t hprt0;
	hprt0.d32 = dwc_otg_read_hprt0(core_if);
	hprt0.b.prtres = val;
	DWC_WRITE_REG32(core_if->host_if->hprt0, hprt0.d32);
}

uint32_t dwc_otg_get_remotewakesig(dwc_otg_core_if_t *core_if)
{
	dctl_data_t dctl;
	dctl.d32 = DWC_READ_REG32(&core_if->dev_if->dev_global_regs->dctl);
	return dctl.b.rmtwkupsig;
}

uint32_t dwc_otg_get_beslreject(dwc_otg_core_if_t *core_if)
{
	dctl_data_t dctl;
	dctl.d32 = DWC_READ_REG32(&core_if->dev_if->dev_global_regs->dctl);
	return dctl.b.besl_reject;
}

void dwc_otg_set_beslreject(dwc_otg_core_if_t *core_if, uint32_t val)
{
	dctl_data_t dctl;
	dctl.d32 = DWC_READ_REG32(&core_if->dev_if->dev_global_regs->dctl);
	dctl.b.besl_reject = val;
	DWC_WRITE_REG32(&core_if->dev_if->dev_global_regs->dctl, dctl.d32);
}

uint32_t dwc_otg_get_hirdthresh(dwc_otg_core_if_t *core_if)
{
	glpmcfg_data_t lpmcfg;
	lpmcfg.d32 = DWC_READ_REG32(&core_if->core_global_regs->glpmcfg);
	return lpmcfg.b.hird_thres;
}

void dwc_otg_set_hirdthresh(dwc_otg_core_if_t *core_if, uint32_t val)
{
	glpmcfg_data_t lpmcfg;

	if (DWC_OTG_PARAM_TEST(val, 0, 15)) {
		DWC_WARN("Wrong valaue for hird_thres\n");
		DWC_WARN("hird_thres must be 0-f\n");
		return;
	}

	lpmcfg.d32 = DWC_READ_REG32(&core_if->core_global_regs->glpmcfg);
	lpmcfg.b.hird_thres |= val;
	DWC_WRITE_REG32(&core_if->core_global_regs->glpmcfg, lpmcfg.d32);
}

uint32_t dwc_otg_get_lpm_portsleepstatus(dwc_otg_core_if_t *core_if)
{
	glpmcfg_data_t lpmcfg;
	lpmcfg.d32 = DWC_READ_REG32(&core_if->core_global_regs->glpmcfg);

	DWC_ASSERT(!
		   ((core_if->lx_state == DWC_OTG_L1) ^ lpmcfg.b.prt_sleep_sts),
		   "lx_state = %d, lmpcfg.prt_sleep_sts = %d\n",
		   core_if->lx_state, lpmcfg.b.prt_sleep_sts);

	return lpmcfg.b.prt_sleep_sts;
}

uint32_t dwc_otg_get_lpm_remotewakeenabled(dwc_otg_core_if_t *core_if)
{
	glpmcfg_data_t lpmcfg;
	lpmcfg.d32 = DWC_READ_REG32(&core_if->core_global_regs->glpmcfg);
	return lpmcfg.b.rem_wkup_en;
}

uint32_t dwc_otg_get_lpmresponse(dwc_otg_core_if_t *core_if)
{
	glpmcfg_data_t lpmcfg;
	lpmcfg.d32 = DWC_READ_REG32(&core_if->core_global_regs->glpmcfg);
	return lpmcfg.b.appl_resp;
}

void dwc_otg_set_lpmresponse(dwc_otg_core_if_t *core_if, uint32_t val)
{
	glpmcfg_data_t lpmcfg;
	lpmcfg.d32 = DWC_READ_REG32(&core_if->core_global_regs->glpmcfg);
	lpmcfg.b.appl_resp = val;
	DWC_WRITE_REG32(&core_if->core_global_regs->glpmcfg, lpmcfg.d32);
}

uint32_t dwc_otg_get_hsic_connect(dwc_otg_core_if_t *core_if)
{
	glpmcfg_data_t lpmcfg;
	lpmcfg.d32 = DWC_READ_REG32(&core_if->core_global_regs->glpmcfg);
	return lpmcfg.b.hsic_connect;
}

void dwc_otg_set_hsic_connect(dwc_otg_core_if_t *core_if, uint32_t val)
{
	glpmcfg_data_t lpmcfg;
	lpmcfg.d32 = DWC_READ_REG32(&core_if->core_global_regs->glpmcfg);
	lpmcfg.b.hsic_connect = val;
	DWC_WRITE_REG32(&core_if->core_global_regs->glpmcfg, lpmcfg.d32);
}

uint32_t dwc_otg_get_inv_sel_hsic(dwc_otg_core_if_t *core_if)
{
	glpmcfg_data_t lpmcfg;
	lpmcfg.d32 = DWC_READ_REG32(&core_if->core_global_regs->glpmcfg);
	return lpmcfg.b.inv_sel_hsic;

}

void dwc_otg_set_inv_sel_hsic(dwc_otg_core_if_t *core_if, uint32_t val)
{
	glpmcfg_data_t lpmcfg;
	lpmcfg.d32 = DWC_READ_REG32(&core_if->core_global_regs->glpmcfg);
	lpmcfg.b.inv_sel_hsic = val;
	DWC_WRITE_REG32(&core_if->core_global_regs->glpmcfg, lpmcfg.d32);
}

uint32_t dwc_otg_get_gotgctl(dwc_otg_core_if_t *core_if)
{
	return DWC_READ_REG32(&core_if->core_global_regs->gotgctl);
}

void dwc_otg_set_gotgctl(dwc_otg_core_if_t *core_if, uint32_t val)
{
	DWC_WRITE_REG32(&core_if->core_global_regs->gotgctl, val);
}

uint32_t dwc_otg_get_gusbcfg(dwc_otg_core_if_t *core_if)
{
	return DWC_READ_REG32(&core_if->core_global_regs->gusbcfg);
}

void dwc_otg_set_gusbcfg(dwc_otg_core_if_t *core_if, uint32_t val)
{
	DWC_WRITE_REG32(&core_if->core_global_regs->gusbcfg, val);
}

uint32_t dwc_otg_get_grxfsiz(dwc_otg_core_if_t *core_if)
{
	return DWC_READ_REG32(&core_if->core_global_regs->grxfsiz);
}

void dwc_otg_set_grxfsiz(dwc_otg_core_if_t *core_if, uint32_t val)
{
	DWC_WRITE_REG32(&core_if->core_global_regs->grxfsiz, val);
}

uint32_t dwc_otg_get_gnptxfsiz(dwc_otg_core_if_t *core_if)
{
	return DWC_READ_REG32(&core_if->core_global_regs->gnptxfsiz);
}

void dwc_otg_set_gnptxfsiz(dwc_otg_core_if_t *core_if, uint32_t val)
{
	DWC_WRITE_REG32(&core_if->core_global_regs->gnptxfsiz, val);
}

uint32_t dwc_otg_get_gpvndctl(dwc_otg_core_if_t *core_if)
{
	return DWC_READ_REG32(&core_if->core_global_regs->gpvndctl);
}

void dwc_otg_set_gpvndctl(dwc_otg_core_if_t *core_if, uint32_t val)
{
	DWC_WRITE_REG32(&core_if->core_global_regs->gpvndctl, val);
}

uint32_t dwc_otg_get_ggpio(dwc_otg_core_if_t *core_if)
{
	return DWC_READ_REG32(&core_if->core_global_regs->ggpio);
}

void dwc_otg_set_ggpio(dwc_otg_core_if_t *core_if, uint32_t val)
{
	DWC_WRITE_REG32(&core_if->core_global_regs->ggpio, val);
}

uint32_t dwc_otg_get_hprt0(dwc_otg_core_if_t *core_if)
{
	return DWC_READ_REG32(core_if->host_if->hprt0);

}

void dwc_otg_set_hprt0(dwc_otg_core_if_t *core_if, uint32_t val)
{
	DWC_WRITE_REG32(core_if->host_if->hprt0, val);
}

uint32_t dwc_otg_get_guid(dwc_otg_core_if_t *core_if)
{
	return DWC_READ_REG32(&core_if->core_global_regs->guid);
}

void dwc_otg_set_guid(dwc_otg_core_if_t *core_if, uint32_t val)
{
	DWC_WRITE_REG32(&core_if->core_global_regs->guid, val);
}

uint32_t dwc_otg_get_hptxfsiz(dwc_otg_core_if_t *core_if)
{
	return DWC_READ_REG32(&core_if->core_global_regs->hptxfsiz);
}

uint16_t dwc_otg_get_otg_version(dwc_otg_core_if_t *core_if)
{
	return ((core_if->otg_ver ==
		 1) ? (uint16_t) 0x0200 : (uint16_t) 0x0103);
}

/**
 * Start the SRP timer to detect when the SRP does not complete within
 * 6 seconds.
 *
 * @param core_if the pointer to core_if strucure.
 */
void dwc_otg_pcd_start_srp_timer(dwc_otg_core_if_t *core_if)
{
	core_if->srp_timer_started = 1;
	DWC_TIMER_SCHEDULE(core_if->srp_timer, 6000 /* 6 secs */);
}

void dwc_otg_initiate_srp(void *p)
{
	dwc_otg_core_if_t *core_if = p;
	uint32_t *addr = (uint32_t *)&(core_if->core_global_regs->gotgctl);
	gotgctl_data_t mem;
	gotgctl_data_t val;

	val.d32 = DWC_READ_REG32(addr);
	if (val.b.sesreq) {
		DWC_ERROR("Session Request Already active!\n");
		return;
	}

	DWC_INFO("Session Request Initated\n");
	mem.d32 = DWC_READ_REG32(addr);
	mem.b.sesreq = 1;
	DWC_WRITE_REG32(addr, mem.d32);

	/* Start the SRP timer */
	dwc_otg_pcd_start_srp_timer(core_if);
	return;
}

int dwc_otg_check_haps_status(dwc_otg_core_if_t *core_if)
{
	int retval = 0;

	if (DWC_READ_REG32(&core_if->core_global_regs->gsnpsid) == 0xffffffff) {
		return -1;
	} else {
		return retval;
	}

}

void dwc_otg_set_force_mode(dwc_otg_core_if_t *core_if, int mode)
{
	gusbcfg_data_t usbcfg = {.d32 = 0 };

	usbcfg.d32 = DWC_READ_REG32(&core_if->core_global_regs->gusbcfg);
	switch (mode) {
	case USB_MODE_FORCE_HOST:
		usbcfg.b.force_host_mode = 1;
		usbcfg.b.force_dev_mode = 0;
		break;
	case USB_MODE_FORCE_DEVICE:
		usbcfg.b.force_host_mode = 0;
		usbcfg.b.force_dev_mode = 1;
		break;
	case USB_MODE_NORMAL:
		usbcfg.b.force_host_mode = 0;
		usbcfg.b.force_dev_mode = 0;
		break;
	}
	DWC_WRITE_REG32(&core_if->core_global_regs->gusbcfg, usbcfg.d32);
}
