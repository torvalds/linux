// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/*
 * core.c - DesignWare HS OTG Controller common routines
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
 * The Core code provides basic services for accessing and managing the
 * DWC_otg hardware. These services are used by both the Host Controller
 * Driver and the Peripheral Controller Driver.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include <linux/usb/hcd.h>
#include <linux/usb/ch11.h>

#include "core.h"
#include "hcd.h"

/**
 * dwc2_backup_global_registers() - Backup global controller registers.
 * When suspending usb bus, registers needs to be backuped
 * if controller power is disabled once suspended.
 *
 * @hsotg: Programming view of the DWC_otg controller
 */
int dwc2_backup_global_registers(struct dwc2_hsotg *hsotg)
{
	struct dwc2_gregs_backup *gr;

	dev_dbg(hsotg->dev, "%s\n", __func__);

	/* Backup global regs */
	gr = &hsotg->gr_backup;

	gr->gotgctl = dwc2_readl(hsotg, GOTGCTL);
	gr->gintmsk = dwc2_readl(hsotg, GINTMSK);
	gr->gahbcfg = dwc2_readl(hsotg, GAHBCFG);
	gr->gusbcfg = dwc2_readl(hsotg, GUSBCFG);
	gr->grxfsiz = dwc2_readl(hsotg, GRXFSIZ);
	gr->gnptxfsiz = dwc2_readl(hsotg, GNPTXFSIZ);
	gr->gdfifocfg = dwc2_readl(hsotg, GDFIFOCFG);
	gr->pcgcctl1 = dwc2_readl(hsotg, PCGCCTL1);
	gr->glpmcfg = dwc2_readl(hsotg, GLPMCFG);
	gr->gi2cctl = dwc2_readl(hsotg, GI2CCTL);
	gr->pcgcctl = dwc2_readl(hsotg, PCGCTL);

	gr->valid = true;
	return 0;
}

/**
 * dwc2_restore_global_registers() - Restore controller global registers.
 * When resuming usb bus, device registers needs to be restored
 * if controller power were disabled.
 *
 * @hsotg: Programming view of the DWC_otg controller
 */
int dwc2_restore_global_registers(struct dwc2_hsotg *hsotg)
{
	struct dwc2_gregs_backup *gr;

	dev_dbg(hsotg->dev, "%s\n", __func__);

	/* Restore global regs */
	gr = &hsotg->gr_backup;
	if (!gr->valid) {
		dev_err(hsotg->dev, "%s: no global registers to restore\n",
			__func__);
		return -EINVAL;
	}
	gr->valid = false;

	dwc2_writel(hsotg, 0xffffffff, GINTSTS);
	dwc2_writel(hsotg, gr->gotgctl, GOTGCTL);
	dwc2_writel(hsotg, gr->gintmsk, GINTMSK);
	dwc2_writel(hsotg, gr->gusbcfg, GUSBCFG);
	dwc2_writel(hsotg, gr->gahbcfg, GAHBCFG);
	dwc2_writel(hsotg, gr->grxfsiz, GRXFSIZ);
	dwc2_writel(hsotg, gr->gnptxfsiz, GNPTXFSIZ);
	dwc2_writel(hsotg, gr->gdfifocfg, GDFIFOCFG);
	dwc2_writel(hsotg, gr->pcgcctl1, PCGCCTL1);
	dwc2_writel(hsotg, gr->glpmcfg, GLPMCFG);
	dwc2_writel(hsotg, gr->pcgcctl, PCGCTL);
	dwc2_writel(hsotg, gr->gi2cctl, GI2CCTL);

	return 0;
}

/**
 * dwc2_exit_partial_power_down() - Exit controller from Partial Power Down.
 *
 * @hsotg: Programming view of the DWC_otg controller
 * @rem_wakeup: indicates whether resume is initiated by Reset.
 * @restore: Controller registers need to be restored
 */
int dwc2_exit_partial_power_down(struct dwc2_hsotg *hsotg, int rem_wakeup,
				 bool restore)
{
	struct dwc2_gregs_backup *gr;

	gr = &hsotg->gr_backup;

	/*
	 * Restore host or device regisers with the same mode core enterted
	 * to partial power down by checking "GOTGCTL_CURMODE_HOST" backup
	 * value of the "gotgctl" register.
	 */
	if (gr->gotgctl & GOTGCTL_CURMODE_HOST)
		return dwc2_host_exit_partial_power_down(hsotg, rem_wakeup,
							 restore);
	else
		return dwc2_gadget_exit_partial_power_down(hsotg, restore);
}

/**
 * dwc2_enter_partial_power_down() - Put controller in Partial Power Down.
 *
 * @hsotg: Programming view of the DWC_otg controller
 */
int dwc2_enter_partial_power_down(struct dwc2_hsotg *hsotg)
{
	if (dwc2_is_host_mode(hsotg))
		return dwc2_host_enter_partial_power_down(hsotg);
	else
		return dwc2_gadget_enter_partial_power_down(hsotg);
}

/**
 * dwc2_restore_essential_regs() - Restore essiential regs of core.
 *
 * @hsotg: Programming view of the DWC_otg controller
 * @rmode: Restore mode, enabled in case of remote-wakeup.
 * @is_host: Host or device mode.
 */
static void dwc2_restore_essential_regs(struct dwc2_hsotg *hsotg, int rmode,
					int is_host)
{
	u32 pcgcctl;
	struct dwc2_gregs_backup *gr;
	struct dwc2_dregs_backup *dr;
	struct dwc2_hregs_backup *hr;

	gr = &hsotg->gr_backup;
	dr = &hsotg->dr_backup;
	hr = &hsotg->hr_backup;

	dev_dbg(hsotg->dev, "%s: restoring essential regs\n", __func__);

	/* Load restore values for [31:14] bits */
	pcgcctl = (gr->pcgcctl & 0xffffc000);
	/* If High Speed */
	if (is_host) {
		if (!(pcgcctl & PCGCTL_P2HD_PRT_SPD_MASK))
			pcgcctl |= BIT(17);
	} else {
		if (!(pcgcctl & PCGCTL_P2HD_DEV_ENUM_SPD_MASK))
			pcgcctl |= BIT(17);
	}
	dwc2_writel(hsotg, pcgcctl, PCGCTL);

	/* Umnask global Interrupt in GAHBCFG and restore it */
	dwc2_writel(hsotg, gr->gahbcfg | GAHBCFG_GLBL_INTR_EN, GAHBCFG);

	/* Clear all pending interupts */
	dwc2_writel(hsotg, 0xffffffff, GINTSTS);

	/* Unmask restore done interrupt */
	dwc2_writel(hsotg, GINTSTS_RESTOREDONE, GINTMSK);

	/* Restore GUSBCFG and HCFG/DCFG */
	dwc2_writel(hsotg, gr->gusbcfg, GUSBCFG);

	if (is_host) {
		dwc2_writel(hsotg, hr->hcfg, HCFG);
		if (rmode)
			pcgcctl |= PCGCTL_RESTOREMODE;
		dwc2_writel(hsotg, pcgcctl, PCGCTL);
		udelay(10);

		pcgcctl |= PCGCTL_ESS_REG_RESTORED;
		dwc2_writel(hsotg, pcgcctl, PCGCTL);
		udelay(10);
	} else {
		dwc2_writel(hsotg, dr->dcfg, DCFG);
		if (!rmode)
			pcgcctl |= PCGCTL_RESTOREMODE | PCGCTL_RSTPDWNMODULE;
		dwc2_writel(hsotg, pcgcctl, PCGCTL);
		udelay(10);

		pcgcctl |= PCGCTL_ESS_REG_RESTORED;
		dwc2_writel(hsotg, pcgcctl, PCGCTL);
		udelay(10);
	}
}

/**
 * dwc2_hib_restore_common() - Common part of restore routine.
 *
 * @hsotg: Programming view of the DWC_otg controller
 * @rem_wakeup: Remote-wakeup, enabled in case of remote-wakeup.
 * @is_host: Host or device mode.
 */
void dwc2_hib_restore_common(struct dwc2_hsotg *hsotg, int rem_wakeup,
			     int is_host)
{
	u32 gpwrdn;

	/* Switch-on voltage to the core */
	gpwrdn = dwc2_readl(hsotg, GPWRDN);
	gpwrdn &= ~GPWRDN_PWRDNSWTCH;
	dwc2_writel(hsotg, gpwrdn, GPWRDN);
	udelay(10);

	/* Reset core */
	gpwrdn = dwc2_readl(hsotg, GPWRDN);
	gpwrdn &= ~GPWRDN_PWRDNRSTN;
	dwc2_writel(hsotg, gpwrdn, GPWRDN);
	udelay(10);

	/* Enable restore from PMU */
	gpwrdn = dwc2_readl(hsotg, GPWRDN);
	gpwrdn |= GPWRDN_RESTORE;
	dwc2_writel(hsotg, gpwrdn, GPWRDN);
	udelay(10);

	/* Disable Power Down Clamp */
	gpwrdn = dwc2_readl(hsotg, GPWRDN);
	gpwrdn &= ~GPWRDN_PWRDNCLMP;
	dwc2_writel(hsotg, gpwrdn, GPWRDN);
	udelay(50);

	if (!is_host && rem_wakeup)
		udelay(70);

	/* Deassert reset core */
	gpwrdn = dwc2_readl(hsotg, GPWRDN);
	gpwrdn |= GPWRDN_PWRDNRSTN;
	dwc2_writel(hsotg, gpwrdn, GPWRDN);
	udelay(10);

	/* Disable PMU interrupt */
	gpwrdn = dwc2_readl(hsotg, GPWRDN);
	gpwrdn &= ~GPWRDN_PMUINTSEL;
	dwc2_writel(hsotg, gpwrdn, GPWRDN);
	udelay(10);

	/* Set Restore Essential Regs bit in PCGCCTL register */
	dwc2_restore_essential_regs(hsotg, rem_wakeup, is_host);

	/*
	 * Wait For Restore_done Interrupt. This mechanism of polling the
	 * interrupt is introduced to avoid any possible race conditions
	 */
	if (dwc2_hsotg_wait_bit_set(hsotg, GINTSTS, GINTSTS_RESTOREDONE,
				    20000)) {
		dev_dbg(hsotg->dev,
			"%s: Restore Done wan't generated here\n",
			__func__);
	} else {
		dev_dbg(hsotg->dev, "restore done  generated here\n");

		/*
		 * To avoid restore done interrupt storm after restore is
		 * generated clear GINTSTS_RESTOREDONE bit.
		 */
		dwc2_writel(hsotg, GINTSTS_RESTOREDONE, GINTSTS);
	}
}

/**
 * dwc2_wait_for_mode() - Waits for the controller mode.
 * @hsotg:	Programming view of the DWC_otg controller.
 * @host_mode:	If true, waits for host mode, otherwise device mode.
 */
static void dwc2_wait_for_mode(struct dwc2_hsotg *hsotg,
			       bool host_mode)
{
	ktime_t start;
	ktime_t end;
	unsigned int timeout = 110;

	dev_vdbg(hsotg->dev, "Waiting for %s mode\n",
		 host_mode ? "host" : "device");

	start = ktime_get();

	while (1) {
		s64 ms;

		if (dwc2_is_host_mode(hsotg) == host_mode) {
			dev_vdbg(hsotg->dev, "%s mode set\n",
				 host_mode ? "Host" : "Device");
			break;
		}

		end = ktime_get();
		ms = ktime_to_ms(ktime_sub(end, start));

		if (ms >= (s64)timeout) {
			dev_warn(hsotg->dev, "%s: Couldn't set %s mode\n",
				 __func__, host_mode ? "host" : "device");
			break;
		}

		usleep_range(1000, 2000);
	}
}

/**
 * dwc2_iddig_filter_enabled() - Returns true if the IDDIG debounce
 * filter is enabled.
 *
 * @hsotg: Programming view of DWC_otg controller
 */
static bool dwc2_iddig_filter_enabled(struct dwc2_hsotg *hsotg)
{
	u32 gsnpsid;
	u32 ghwcfg4;

	if (!dwc2_hw_is_otg(hsotg))
		return false;

	/* Check if core configuration includes the IDDIG filter. */
	ghwcfg4 = dwc2_readl(hsotg, GHWCFG4);
	if (!(ghwcfg4 & GHWCFG4_IDDIG_FILT_EN))
		return false;

	/*
	 * Check if the IDDIG debounce filter is bypassed. Available
	 * in core version >= 3.10a.
	 */
	gsnpsid = dwc2_readl(hsotg, GSNPSID);
	if (gsnpsid >= DWC2_CORE_REV_3_10a) {
		u32 gotgctl = dwc2_readl(hsotg, GOTGCTL);

		if (gotgctl & GOTGCTL_DBNCE_FLTR_BYPASS)
			return false;
	}

	return true;
}

/*
 * dwc2_enter_hibernation() - Common function to enter hibernation.
 *
 * @hsotg: Programming view of the DWC_otg controller
 * @is_host: True if core is in host mode.
 *
 * Return: 0 if successful, negative error code otherwise
 */
int dwc2_enter_hibernation(struct dwc2_hsotg *hsotg, int is_host)
{
	if (is_host)
		return dwc2_host_enter_hibernation(hsotg);
	else
		return dwc2_gadget_enter_hibernation(hsotg);
}

/*
 * dwc2_exit_hibernation() - Common function to exit from hibernation.
 *
 * @hsotg: Programming view of the DWC_otg controller
 * @rem_wakeup: Remote-wakeup, enabled in case of remote-wakeup.
 * @reset: Enabled in case of restore with reset.
 * @is_host: True if core is in host mode.
 *
 * Return: 0 if successful, negative error code otherwise
 */
int dwc2_exit_hibernation(struct dwc2_hsotg *hsotg, int rem_wakeup,
			  int reset, int is_host)
{
	if (is_host)
		return dwc2_host_exit_hibernation(hsotg, rem_wakeup, reset);
	else
		return dwc2_gadget_exit_hibernation(hsotg, rem_wakeup, reset);
}

/*
 * Do core a soft reset of the core.  Be careful with this because it
 * resets all the internal state machines of the core.
 */
int dwc2_core_reset(struct dwc2_hsotg *hsotg, bool skip_wait)
{
	u32 greset;
	bool wait_for_host_mode = false;

	dev_vdbg(hsotg->dev, "%s()\n", __func__);

	/*
	 * If the current mode is host, either due to the force mode
	 * bit being set (which persists after core reset) or the
	 * connector id pin, a core soft reset will temporarily reset
	 * the mode to device. A delay from the IDDIG debounce filter
	 * will occur before going back to host mode.
	 *
	 * Determine whether we will go back into host mode after a
	 * reset and account for this delay after the reset.
	 */
	if (dwc2_iddig_filter_enabled(hsotg)) {
		u32 gotgctl = dwc2_readl(hsotg, GOTGCTL);
		u32 gusbcfg = dwc2_readl(hsotg, GUSBCFG);

		if (!(gotgctl & GOTGCTL_CONID_B) ||
		    (gusbcfg & GUSBCFG_FORCEHOSTMODE)) {
			wait_for_host_mode = true;
		}
	}

	/* Core Soft Reset */
	greset = dwc2_readl(hsotg, GRSTCTL);
	greset |= GRSTCTL_CSFTRST;
	dwc2_writel(hsotg, greset, GRSTCTL);

	if ((hsotg->hw_params.snpsid & DWC2_CORE_REV_MASK) <
		(DWC2_CORE_REV_4_20a & DWC2_CORE_REV_MASK)) {
		if (dwc2_hsotg_wait_bit_clear(hsotg, GRSTCTL,
					      GRSTCTL_CSFTRST, 10000)) {
			dev_warn(hsotg->dev, "%s: HANG! Soft Reset timeout GRSTCTL_CSFTRST\n",
				 __func__);
			return -EBUSY;
		}
	} else {
		if (dwc2_hsotg_wait_bit_set(hsotg, GRSTCTL,
					    GRSTCTL_CSFTRST_DONE, 10000)) {
			dev_warn(hsotg->dev, "%s: HANG! Soft Reset timeout GRSTCTL_CSFTRST_DONE\n",
				 __func__);
			return -EBUSY;
		}
		greset = dwc2_readl(hsotg, GRSTCTL);
		greset &= ~GRSTCTL_CSFTRST;
		greset |= GRSTCTL_CSFTRST_DONE;
		dwc2_writel(hsotg, greset, GRSTCTL);
	}

	/*
	 * Switching from device mode to host mode by disconnecting
	 * device cable core enters and exits form hibernation.
	 * However, the fifo map remains not cleared. It results
	 * to a WARNING (WARNING: CPU: 5 PID: 0 at drivers/usb/dwc2/
	 * gadget.c:307 dwc2_hsotg_init_fifo+0x12/0x152 [dwc2])
	 * if in host mode we disconnect the micro a to b host
	 * cable. Because core reset occurs.
	 * To avoid the WARNING, fifo_map should be cleared
	 * in dwc2_core_reset() function by taking into account configs.
	 * fifo_map must be cleared only if driver is configured in
	 * "CONFIG_USB_DWC2_PERIPHERAL" or "CONFIG_USB_DWC2_DUAL_ROLE"
	 * mode.
	 */
	dwc2_clear_fifo_map(hsotg);

	/* Wait for AHB master IDLE state */
	if (dwc2_hsotg_wait_bit_set(hsotg, GRSTCTL, GRSTCTL_AHBIDLE, 10000)) {
		dev_warn(hsotg->dev, "%s: HANG! AHB Idle timeout GRSTCTL GRSTCTL_AHBIDLE\n",
			 __func__);
		return -EBUSY;
	}

	if (wait_for_host_mode && !skip_wait)
		dwc2_wait_for_mode(hsotg, true);

	return 0;
}

/**
 * dwc2_force_mode() - Force the mode of the controller.
 *
 * Forcing the mode is needed for two cases:
 *
 * 1) If the dr_mode is set to either HOST or PERIPHERAL we force the
 * controller to stay in a particular mode regardless of ID pin
 * changes. We do this once during probe.
 *
 * 2) During probe we want to read reset values of the hw
 * configuration registers that are only available in either host or
 * device mode. We may need to force the mode if the current mode does
 * not allow us to access the register in the mode that we want.
 *
 * In either case it only makes sense to force the mode if the
 * controller hardware is OTG capable.
 *
 * Checks are done in this function to determine whether doing a force
 * would be valid or not.
 *
 * If a force is done, it requires a IDDIG debounce filter delay if
 * the filter is configured and enabled. We poll the current mode of
 * the controller to account for this delay.
 *
 * @hsotg: Programming view of DWC_otg controller
 * @host: Host mode flag
 */
void dwc2_force_mode(struct dwc2_hsotg *hsotg, bool host)
{
	u32 gusbcfg;
	u32 set;
	u32 clear;

	dev_dbg(hsotg->dev, "Forcing mode to %s\n", host ? "host" : "device");

	/*
	 * Force mode has no effect if the hardware is not OTG.
	 */
	if (!dwc2_hw_is_otg(hsotg))
		return;

	/*
	 * If dr_mode is either peripheral or host only, there is no
	 * need to ever force the mode to the opposite mode.
	 */
	if (WARN_ON(host && hsotg->dr_mode == USB_DR_MODE_PERIPHERAL))
		return;

	if (WARN_ON(!host && hsotg->dr_mode == USB_DR_MODE_HOST))
		return;

	gusbcfg = dwc2_readl(hsotg, GUSBCFG);

	set = host ? GUSBCFG_FORCEHOSTMODE : GUSBCFG_FORCEDEVMODE;
	clear = host ? GUSBCFG_FORCEDEVMODE : GUSBCFG_FORCEHOSTMODE;

	gusbcfg &= ~clear;
	gusbcfg |= set;
	dwc2_writel(hsotg, gusbcfg, GUSBCFG);

	dwc2_wait_for_mode(hsotg, host);
	return;
}

/**
 * dwc2_clear_force_mode() - Clears the force mode bits.
 *
 * After clearing the bits, wait up to 100 ms to account for any
 * potential IDDIG filter delay. We can't know if we expect this delay
 * or not because the value of the connector ID status is affected by
 * the force mode. We only need to call this once during probe if
 * dr_mode == OTG.
 *
 * @hsotg: Programming view of DWC_otg controller
 */
static void dwc2_clear_force_mode(struct dwc2_hsotg *hsotg)
{
	u32 gusbcfg;

	if (!dwc2_hw_is_otg(hsotg))
		return;

	dev_dbg(hsotg->dev, "Clearing force mode bits\n");

	gusbcfg = dwc2_readl(hsotg, GUSBCFG);
	gusbcfg &= ~GUSBCFG_FORCEHOSTMODE;
	gusbcfg &= ~GUSBCFG_FORCEDEVMODE;
	dwc2_writel(hsotg, gusbcfg, GUSBCFG);

	if (dwc2_iddig_filter_enabled(hsotg))
		msleep(100);
}

/*
 * Sets or clears force mode based on the dr_mode parameter.
 */
void dwc2_force_dr_mode(struct dwc2_hsotg *hsotg)
{
	switch (hsotg->dr_mode) {
	case USB_DR_MODE_HOST:
		/*
		 * NOTE: This is required for some rockchip soc based
		 * platforms on their host-only dwc2.
		 */
		if (!dwc2_hw_is_otg(hsotg))
			msleep(50);

		break;
	case USB_DR_MODE_PERIPHERAL:
		dwc2_force_mode(hsotg, false);
		break;
	case USB_DR_MODE_OTG:
		dwc2_clear_force_mode(hsotg);
		break;
	default:
		dev_warn(hsotg->dev, "%s() Invalid dr_mode=%d\n",
			 __func__, hsotg->dr_mode);
		break;
	}
}

/*
 * dwc2_enable_acg - enable active clock gating feature
 */
void dwc2_enable_acg(struct dwc2_hsotg *hsotg)
{
	if (hsotg->params.acg_enable) {
		u32 pcgcctl1 = dwc2_readl(hsotg, PCGCCTL1);

		dev_dbg(hsotg->dev, "Enabling Active Clock Gating\n");
		pcgcctl1 |= PCGCCTL1_GATEEN;
		dwc2_writel(hsotg, pcgcctl1, PCGCCTL1);
	}
}

/**
 * dwc2_dump_host_registers() - Prints the host registers
 *
 * @hsotg: Programming view of DWC_otg controller
 *
 * NOTE: This function will be removed once the peripheral controller code
 * is integrated and the driver is stable
 */
void dwc2_dump_host_registers(struct dwc2_hsotg *hsotg)
{
#ifdef DEBUG
	u32 __iomem *addr;
	int i;

	dev_dbg(hsotg->dev, "Host Global Registers\n");
	addr = hsotg->regs + HCFG;
	dev_dbg(hsotg->dev, "HCFG	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, HCFG));
	addr = hsotg->regs + HFIR;
	dev_dbg(hsotg->dev, "HFIR	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, HFIR));
	addr = hsotg->regs + HFNUM;
	dev_dbg(hsotg->dev, "HFNUM	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, HFNUM));
	addr = hsotg->regs + HPTXSTS;
	dev_dbg(hsotg->dev, "HPTXSTS	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, HPTXSTS));
	addr = hsotg->regs + HAINT;
	dev_dbg(hsotg->dev, "HAINT	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, HAINT));
	addr = hsotg->regs + HAINTMSK;
	dev_dbg(hsotg->dev, "HAINTMSK	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, HAINTMSK));
	if (hsotg->params.dma_desc_enable) {
		addr = hsotg->regs + HFLBADDR;
		dev_dbg(hsotg->dev, "HFLBADDR @0x%08lX : 0x%08X\n",
			(unsigned long)addr, dwc2_readl(hsotg, HFLBADDR));
	}

	addr = hsotg->regs + HPRT0;
	dev_dbg(hsotg->dev, "HPRT0	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, HPRT0));

	for (i = 0; i < hsotg->params.host_channels; i++) {
		dev_dbg(hsotg->dev, "Host Channel %d Specific Registers\n", i);
		addr = hsotg->regs + HCCHAR(i);
		dev_dbg(hsotg->dev, "HCCHAR	 @0x%08lX : 0x%08X\n",
			(unsigned long)addr, dwc2_readl(hsotg, HCCHAR(i)));
		addr = hsotg->regs + HCSPLT(i);
		dev_dbg(hsotg->dev, "HCSPLT	 @0x%08lX : 0x%08X\n",
			(unsigned long)addr, dwc2_readl(hsotg, HCSPLT(i)));
		addr = hsotg->regs + HCINT(i);
		dev_dbg(hsotg->dev, "HCINT	 @0x%08lX : 0x%08X\n",
			(unsigned long)addr, dwc2_readl(hsotg, HCINT(i)));
		addr = hsotg->regs + HCINTMSK(i);
		dev_dbg(hsotg->dev, "HCINTMSK	 @0x%08lX : 0x%08X\n",
			(unsigned long)addr, dwc2_readl(hsotg, HCINTMSK(i)));
		addr = hsotg->regs + HCTSIZ(i);
		dev_dbg(hsotg->dev, "HCTSIZ	 @0x%08lX : 0x%08X\n",
			(unsigned long)addr, dwc2_readl(hsotg, HCTSIZ(i)));
		addr = hsotg->regs + HCDMA(i);
		dev_dbg(hsotg->dev, "HCDMA	 @0x%08lX : 0x%08X\n",
			(unsigned long)addr, dwc2_readl(hsotg, HCDMA(i)));
		if (hsotg->params.dma_desc_enable) {
			addr = hsotg->regs + HCDMAB(i);
			dev_dbg(hsotg->dev, "HCDMAB	 @0x%08lX : 0x%08X\n",
				(unsigned long)addr, dwc2_readl(hsotg,
								HCDMAB(i)));
		}
	}
#endif
}

/**
 * dwc2_dump_global_registers() - Prints the core global registers
 *
 * @hsotg: Programming view of DWC_otg controller
 *
 * NOTE: This function will be removed once the peripheral controller code
 * is integrated and the driver is stable
 */
void dwc2_dump_global_registers(struct dwc2_hsotg *hsotg)
{
#ifdef DEBUG
	u32 __iomem *addr;

	dev_dbg(hsotg->dev, "Core Global Registers\n");
	addr = hsotg->regs + GOTGCTL;
	dev_dbg(hsotg->dev, "GOTGCTL	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GOTGCTL));
	addr = hsotg->regs + GOTGINT;
	dev_dbg(hsotg->dev, "GOTGINT	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GOTGINT));
	addr = hsotg->regs + GAHBCFG;
	dev_dbg(hsotg->dev, "GAHBCFG	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GAHBCFG));
	addr = hsotg->regs + GUSBCFG;
	dev_dbg(hsotg->dev, "GUSBCFG	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GUSBCFG));
	addr = hsotg->regs + GRSTCTL;
	dev_dbg(hsotg->dev, "GRSTCTL	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GRSTCTL));
	addr = hsotg->regs + GINTSTS;
	dev_dbg(hsotg->dev, "GINTSTS	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GINTSTS));
	addr = hsotg->regs + GINTMSK;
	dev_dbg(hsotg->dev, "GINTMSK	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GINTMSK));
	addr = hsotg->regs + GRXSTSR;
	dev_dbg(hsotg->dev, "GRXSTSR	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GRXSTSR));
	addr = hsotg->regs + GRXFSIZ;
	dev_dbg(hsotg->dev, "GRXFSIZ	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GRXFSIZ));
	addr = hsotg->regs + GNPTXFSIZ;
	dev_dbg(hsotg->dev, "GNPTXFSIZ	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GNPTXFSIZ));
	addr = hsotg->regs + GNPTXSTS;
	dev_dbg(hsotg->dev, "GNPTXSTS	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GNPTXSTS));
	addr = hsotg->regs + GI2CCTL;
	dev_dbg(hsotg->dev, "GI2CCTL	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GI2CCTL));
	addr = hsotg->regs + GPVNDCTL;
	dev_dbg(hsotg->dev, "GPVNDCTL	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GPVNDCTL));
	addr = hsotg->regs + GGPIO;
	dev_dbg(hsotg->dev, "GGPIO	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GGPIO));
	addr = hsotg->regs + GUID;
	dev_dbg(hsotg->dev, "GUID	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GUID));
	addr = hsotg->regs + GSNPSID;
	dev_dbg(hsotg->dev, "GSNPSID	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GSNPSID));
	addr = hsotg->regs + GHWCFG1;
	dev_dbg(hsotg->dev, "GHWCFG1	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GHWCFG1));
	addr = hsotg->regs + GHWCFG2;
	dev_dbg(hsotg->dev, "GHWCFG2	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GHWCFG2));
	addr = hsotg->regs + GHWCFG3;
	dev_dbg(hsotg->dev, "GHWCFG3	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GHWCFG3));
	addr = hsotg->regs + GHWCFG4;
	dev_dbg(hsotg->dev, "GHWCFG4	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GHWCFG4));
	addr = hsotg->regs + GLPMCFG;
	dev_dbg(hsotg->dev, "GLPMCFG	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GLPMCFG));
	addr = hsotg->regs + GPWRDN;
	dev_dbg(hsotg->dev, "GPWRDN	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GPWRDN));
	addr = hsotg->regs + GDFIFOCFG;
	dev_dbg(hsotg->dev, "GDFIFOCFG	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GDFIFOCFG));
	addr = hsotg->regs + HPTXFSIZ;
	dev_dbg(hsotg->dev, "HPTXFSIZ	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, HPTXFSIZ));

	addr = hsotg->regs + PCGCTL;
	dev_dbg(hsotg->dev, "PCGCTL	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, PCGCTL));
#endif
}

/**
 * dwc2_flush_tx_fifo() - Flushes a Tx FIFO
 *
 * @hsotg: Programming view of DWC_otg controller
 * @num:   Tx FIFO to flush
 */
void dwc2_flush_tx_fifo(struct dwc2_hsotg *hsotg, const int num)
{
	u32 greset;

	dev_vdbg(hsotg->dev, "Flush Tx FIFO %d\n", num);

	/* Wait for AHB master IDLE state */
	if (dwc2_hsotg_wait_bit_set(hsotg, GRSTCTL, GRSTCTL_AHBIDLE, 10000))
		dev_warn(hsotg->dev, "%s:  HANG! AHB Idle GRSCTL\n",
			 __func__);

	greset = GRSTCTL_TXFFLSH;
	greset |= num << GRSTCTL_TXFNUM_SHIFT & GRSTCTL_TXFNUM_MASK;
	dwc2_writel(hsotg, greset, GRSTCTL);

	if (dwc2_hsotg_wait_bit_clear(hsotg, GRSTCTL, GRSTCTL_TXFFLSH, 10000))
		dev_warn(hsotg->dev, "%s:  HANG! timeout GRSTCTL GRSTCTL_TXFFLSH\n",
			 __func__);

	/* Wait for at least 3 PHY Clocks */
	udelay(1);
}

/**
 * dwc2_flush_rx_fifo() - Flushes the Rx FIFO
 *
 * @hsotg: Programming view of DWC_otg controller
 */
void dwc2_flush_rx_fifo(struct dwc2_hsotg *hsotg)
{
	u32 greset;

	dev_vdbg(hsotg->dev, "%s()\n", __func__);

	/* Wait for AHB master IDLE state */
	if (dwc2_hsotg_wait_bit_set(hsotg, GRSTCTL, GRSTCTL_AHBIDLE, 10000))
		dev_warn(hsotg->dev, "%s:  HANG! AHB Idle GRSCTL\n",
			 __func__);

	greset = GRSTCTL_RXFFLSH;
	dwc2_writel(hsotg, greset, GRSTCTL);

	/* Wait for RxFIFO flush done */
	if (dwc2_hsotg_wait_bit_clear(hsotg, GRSTCTL, GRSTCTL_RXFFLSH, 10000))
		dev_warn(hsotg->dev, "%s: HANG! timeout GRSTCTL GRSTCTL_RXFFLSH\n",
			 __func__);

	/* Wait for at least 3 PHY Clocks */
	udelay(1);
}

bool dwc2_is_controller_alive(struct dwc2_hsotg *hsotg)
{
	if (dwc2_readl(hsotg, GSNPSID) == 0xffffffff)
		return false;
	else
		return true;
}

/**
 * dwc2_enable_global_interrupts() - Enables the controller's Global
 * Interrupt in the AHB Config register
 *
 * @hsotg: Programming view of DWC_otg controller
 */
void dwc2_enable_global_interrupts(struct dwc2_hsotg *hsotg)
{
	u32 ahbcfg = dwc2_readl(hsotg, GAHBCFG);

	ahbcfg |= GAHBCFG_GLBL_INTR_EN;
	dwc2_writel(hsotg, ahbcfg, GAHBCFG);
}

/**
 * dwc2_disable_global_interrupts() - Disables the controller's Global
 * Interrupt in the AHB Config register
 *
 * @hsotg: Programming view of DWC_otg controller
 */
void dwc2_disable_global_interrupts(struct dwc2_hsotg *hsotg)
{
	u32 ahbcfg = dwc2_readl(hsotg, GAHBCFG);

	ahbcfg &= ~GAHBCFG_GLBL_INTR_EN;
	dwc2_writel(hsotg, ahbcfg, GAHBCFG);
}

/* Returns the controller's GHWCFG2.OTG_MODE. */
unsigned int dwc2_op_mode(struct dwc2_hsotg *hsotg)
{
	u32 ghwcfg2 = dwc2_readl(hsotg, GHWCFG2);

	return (ghwcfg2 & GHWCFG2_OP_MODE_MASK) >>
		GHWCFG2_OP_MODE_SHIFT;
}

/* Returns true if the controller is capable of DRD. */
bool dwc2_hw_is_otg(struct dwc2_hsotg *hsotg)
{
	unsigned int op_mode = dwc2_op_mode(hsotg);

	return (op_mode == GHWCFG2_OP_MODE_HNP_SRP_CAPABLE) ||
		(op_mode == GHWCFG2_OP_MODE_SRP_ONLY_CAPABLE) ||
		(op_mode == GHWCFG2_OP_MODE_NO_HNP_SRP_CAPABLE);
}

/* Returns true if the controller is host-only. */
bool dwc2_hw_is_host(struct dwc2_hsotg *hsotg)
{
	unsigned int op_mode = dwc2_op_mode(hsotg);

	return (op_mode == GHWCFG2_OP_MODE_SRP_CAPABLE_HOST) ||
		(op_mode == GHWCFG2_OP_MODE_NO_SRP_CAPABLE_HOST);
}

/* Returns true if the controller is device-only. */
bool dwc2_hw_is_device(struct dwc2_hsotg *hsotg)
{
	unsigned int op_mode = dwc2_op_mode(hsotg);

	return (op_mode == GHWCFG2_OP_MODE_SRP_CAPABLE_DEVICE) ||
		(op_mode == GHWCFG2_OP_MODE_NO_SRP_CAPABLE_DEVICE);
}

/**
 * dwc2_hsotg_wait_bit_set - Waits for bit to be set.
 * @hsotg: Programming view of DWC_otg controller.
 * @offset: Register's offset where bit/bits must be set.
 * @mask: Mask of the bit/bits which must be set.
 * @timeout: Timeout to wait.
 *
 * Return: 0 if bit/bits are set or -ETIMEDOUT in case of timeout.
 */
int dwc2_hsotg_wait_bit_set(struct dwc2_hsotg *hsotg, u32 offset, u32 mask,
			    u32 timeout)
{
	u32 i;

	for (i = 0; i < timeout; i++) {
		if (dwc2_readl(hsotg, offset) & mask)
			return 0;
		udelay(1);
	}

	return -ETIMEDOUT;
}

/**
 * dwc2_hsotg_wait_bit_clear - Waits for bit to be clear.
 * @hsotg: Programming view of DWC_otg controller.
 * @offset: Register's offset where bit/bits must be set.
 * @mask: Mask of the bit/bits which must be set.
 * @timeout: Timeout to wait.
 *
 * Return: 0 if bit/bits are set or -ETIMEDOUT in case of timeout.
 */
int dwc2_hsotg_wait_bit_clear(struct dwc2_hsotg *hsotg, u32 offset, u32 mask,
			      u32 timeout)
{
	u32 i;

	for (i = 0; i < timeout; i++) {
		if (!(dwc2_readl(hsotg, offset) & mask))
			return 0;
		udelay(1);
	}

	return -ETIMEDOUT;
}

/*
 * Initializes the FSLSPClkSel field of the HCFG register depending on the
 * PHY type
 */
void dwc2_init_fs_ls_pclk_sel(struct dwc2_hsotg *hsotg)
{
	u32 hcfg, val;

	if ((hsotg->hw_params.hs_phy_type == GHWCFG2_HS_PHY_TYPE_ULPI &&
	     hsotg->hw_params.fs_phy_type == GHWCFG2_FS_PHY_TYPE_DEDICATED &&
	     hsotg->params.ulpi_fs_ls) ||
	    hsotg->params.phy_type == DWC2_PHY_TYPE_PARAM_FS) {
		/* Full speed PHY */
		val = HCFG_FSLSPCLKSEL_48_MHZ;
	} else {
		/* High speed PHY running at full speed or high speed */
		val = HCFG_FSLSPCLKSEL_30_60_MHZ;
	}

	dev_dbg(hsotg->dev, "Initializing HCFG.FSLSPClkSel to %08x\n", val);
	hcfg = dwc2_readl(hsotg, HCFG);
	hcfg &= ~HCFG_FSLSPCLKSEL_MASK;
	hcfg |= val << HCFG_FSLSPCLKSEL_SHIFT;
	dwc2_writel(hsotg, hcfg, HCFG);
}

static int dwc2_fs_phy_init(struct dwc2_hsotg *hsotg, bool select_phy)
{
	u32 usbcfg, ggpio, i2cctl;
	int retval = 0;

	/*
	 * core_init() is now called on every switch so only call the
	 * following for the first time through
	 */
	if (select_phy) {
		dev_dbg(hsotg->dev, "FS PHY selected\n");

		usbcfg = dwc2_readl(hsotg, GUSBCFG);
		if (!(usbcfg & GUSBCFG_PHYSEL)) {
			usbcfg |= GUSBCFG_PHYSEL;
			dwc2_writel(hsotg, usbcfg, GUSBCFG);

			/* Reset after a PHY select */
			retval = dwc2_core_reset(hsotg, false);

			if (retval) {
				dev_err(hsotg->dev,
					"%s: Reset failed, aborting", __func__);
				return retval;
			}
		}

		if (hsotg->params.activate_stm_fs_transceiver) {
			ggpio = dwc2_readl(hsotg, GGPIO);
			if (!(ggpio & GGPIO_STM32_OTG_GCCFG_PWRDWN)) {
				dev_dbg(hsotg->dev, "Activating transceiver\n");
				/*
				 * STM32F4x9 uses the GGPIO register as general
				 * core configuration register.
				 */
				ggpio |= GGPIO_STM32_OTG_GCCFG_PWRDWN;
				dwc2_writel(hsotg, ggpio, GGPIO);
			}
		}
	}

	/*
	 * Program DCFG.DevSpd or HCFG.FSLSPclkSel to 48Mhz in FS. Also
	 * do this on HNP Dev/Host mode switches (done in dev_init and
	 * host_init).
	 */
	if (dwc2_is_host_mode(hsotg))
		dwc2_init_fs_ls_pclk_sel(hsotg);

	if (hsotg->params.i2c_enable) {
		dev_dbg(hsotg->dev, "FS PHY enabling I2C\n");

		/* Program GUSBCFG.OtgUtmiFsSel to I2C */
		usbcfg = dwc2_readl(hsotg, GUSBCFG);
		usbcfg |= GUSBCFG_OTG_UTMI_FS_SEL;
		dwc2_writel(hsotg, usbcfg, GUSBCFG);

		/* Program GI2CCTL.I2CEn */
		i2cctl = dwc2_readl(hsotg, GI2CCTL);
		i2cctl &= ~GI2CCTL_I2CDEVADDR_MASK;
		i2cctl |= 1 << GI2CCTL_I2CDEVADDR_SHIFT;
		i2cctl &= ~GI2CCTL_I2CEN;
		dwc2_writel(hsotg, i2cctl, GI2CCTL);
		i2cctl |= GI2CCTL_I2CEN;
		dwc2_writel(hsotg, i2cctl, GI2CCTL);
	}

	return retval;
}

static int dwc2_hs_phy_init(struct dwc2_hsotg *hsotg, bool select_phy)
{
	u32 usbcfg, usbcfg_old;
	int retval = 0;

	if (!select_phy)
		return 0;

	usbcfg = dwc2_readl(hsotg, GUSBCFG);
	usbcfg_old = usbcfg;

	/*
	 * HS PHY parameters. These parameters are preserved during soft reset
	 * so only program the first time. Do a soft reset immediately after
	 * setting phyif.
	 */
	switch (hsotg->params.phy_type) {
	case DWC2_PHY_TYPE_PARAM_ULPI:
		/* ULPI interface */
		dev_dbg(hsotg->dev, "HS ULPI PHY selected\n");
		usbcfg |= GUSBCFG_ULPI_UTMI_SEL;
		usbcfg &= ~(GUSBCFG_PHYIF16 | GUSBCFG_DDRSEL);
		if (hsotg->params.phy_ulpi_ddr)
			usbcfg |= GUSBCFG_DDRSEL;

		/* Set external VBUS indicator as needed. */
		if (hsotg->params.oc_disable)
			usbcfg |= (GUSBCFG_ULPI_INT_VBUS_IND |
				   GUSBCFG_INDICATORPASSTHROUGH);
		break;
	case DWC2_PHY_TYPE_PARAM_UTMI:
		/* UTMI+ interface */
		dev_dbg(hsotg->dev, "HS UTMI+ PHY selected\n");
		usbcfg &= ~(GUSBCFG_ULPI_UTMI_SEL | GUSBCFG_PHYIF16);
		if (hsotg->params.phy_utmi_width == 16)
			usbcfg |= GUSBCFG_PHYIF16;

		/* Set turnaround time */
		if (dwc2_is_device_mode(hsotg)) {
			usbcfg &= ~GUSBCFG_USBTRDTIM_MASK;
			if (hsotg->params.phy_utmi_width == 16)
				usbcfg |= 5 << GUSBCFG_USBTRDTIM_SHIFT;
			else
				usbcfg |= 9 << GUSBCFG_USBTRDTIM_SHIFT;
		}
		break;
	default:
		dev_err(hsotg->dev, "FS PHY selected at HS!\n");
		break;
	}

	if (usbcfg != usbcfg_old) {
		dwc2_writel(hsotg, usbcfg, GUSBCFG);

		/* Reset after setting the PHY parameters */
		retval = dwc2_core_reset(hsotg, false);
		if (retval) {
			dev_err(hsotg->dev,
				"%s: Reset failed, aborting", __func__);
			return retval;
		}
	}

	return retval;
}

int dwc2_phy_init(struct dwc2_hsotg *hsotg, bool select_phy)
{
	u32 usbcfg;
	int retval = 0;

	if ((hsotg->params.speed == DWC2_SPEED_PARAM_FULL ||
	     hsotg->params.speed == DWC2_SPEED_PARAM_LOW) &&
	    hsotg->params.phy_type == DWC2_PHY_TYPE_PARAM_FS) {
		/* If FS/LS mode with FS/LS PHY */
		retval = dwc2_fs_phy_init(hsotg, select_phy);
		if (retval)
			return retval;
	} else {
		/* High speed PHY */
		retval = dwc2_hs_phy_init(hsotg, select_phy);
		if (retval)
			return retval;
	}

	if (hsotg->hw_params.hs_phy_type == GHWCFG2_HS_PHY_TYPE_ULPI &&
	    hsotg->hw_params.fs_phy_type == GHWCFG2_FS_PHY_TYPE_DEDICATED &&
	    hsotg->params.ulpi_fs_ls) {
		dev_dbg(hsotg->dev, "Setting ULPI FSLS\n");
		usbcfg = dwc2_readl(hsotg, GUSBCFG);
		usbcfg |= GUSBCFG_ULPI_FS_LS;
		usbcfg |= GUSBCFG_ULPI_CLK_SUSP_M;
		dwc2_writel(hsotg, usbcfg, GUSBCFG);
	} else {
		usbcfg = dwc2_readl(hsotg, GUSBCFG);
		usbcfg &= ~GUSBCFG_ULPI_FS_LS;
		usbcfg &= ~GUSBCFG_ULPI_CLK_SUSP_M;
		dwc2_writel(hsotg, usbcfg, GUSBCFG);
	}

	return retval;
}

MODULE_DESCRIPTION("DESIGNWARE HS OTG Core");
MODULE_AUTHOR("Synopsys, Inc.");
MODULE_LICENSE("Dual BSD/GPL");
