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
static int dwc2_backup_global_registers(struct dwc2_hsotg *hsotg)
{
	struct dwc2_gregs_backup *gr;
	int i;

	/* Backup global regs */
	gr = &hsotg->gr_backup;

	gr->gotgctl = dwc2_readl(hsotg->regs + GOTGCTL);
	gr->gintmsk = dwc2_readl(hsotg->regs + GINTMSK);
	gr->gahbcfg = dwc2_readl(hsotg->regs + GAHBCFG);
	gr->gusbcfg = dwc2_readl(hsotg->regs + GUSBCFG);
	gr->grxfsiz = dwc2_readl(hsotg->regs + GRXFSIZ);
	gr->gnptxfsiz = dwc2_readl(hsotg->regs + GNPTXFSIZ);
	gr->hptxfsiz = dwc2_readl(hsotg->regs + HPTXFSIZ);
	gr->gdfifocfg = dwc2_readl(hsotg->regs + GDFIFOCFG);
	for (i = 0; i < MAX_EPS_CHANNELS; i++)
		gr->dtxfsiz[i] = dwc2_readl(hsotg->regs + DPTXFSIZN(i));

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
static int dwc2_restore_global_registers(struct dwc2_hsotg *hsotg)
{
	struct dwc2_gregs_backup *gr;
	int i;

	dev_dbg(hsotg->dev, "%s\n", __func__);

	/* Restore global regs */
	gr = &hsotg->gr_backup;
	if (!gr->valid) {
		dev_err(hsotg->dev, "%s: no global registers to restore\n",
				__func__);
		return -EINVAL;
	}
	gr->valid = false;

	dwc2_writel(0xffffffff, hsotg->regs + GINTSTS);
	dwc2_writel(gr->gotgctl, hsotg->regs + GOTGCTL);
	dwc2_writel(gr->gintmsk, hsotg->regs + GINTMSK);
	dwc2_writel(gr->gusbcfg, hsotg->regs + GUSBCFG);
	dwc2_writel(gr->gahbcfg, hsotg->regs + GAHBCFG);
	dwc2_writel(gr->grxfsiz, hsotg->regs + GRXFSIZ);
	dwc2_writel(gr->gnptxfsiz, hsotg->regs + GNPTXFSIZ);
	dwc2_writel(gr->hptxfsiz, hsotg->regs + HPTXFSIZ);
	dwc2_writel(gr->gdfifocfg, hsotg->regs + GDFIFOCFG);
	for (i = 0; i < MAX_EPS_CHANNELS; i++)
		dwc2_writel(gr->dtxfsiz[i], hsotg->regs + DPTXFSIZN(i));

	return 0;
}

/**
 * dwc2_exit_hibernation() - Exit controller from Partial Power Down.
 *
 * @hsotg: Programming view of the DWC_otg controller
 * @restore: Controller registers need to be restored
 */
int dwc2_exit_hibernation(struct dwc2_hsotg *hsotg, bool restore)
{
	u32 pcgcctl;
	int ret = 0;

	if (!hsotg->core_params->hibernation)
		return -ENOTSUPP;

	pcgcctl = dwc2_readl(hsotg->regs + PCGCTL);
	pcgcctl &= ~PCGCTL_STOPPCLK;
	dwc2_writel(pcgcctl, hsotg->regs + PCGCTL);

	pcgcctl = dwc2_readl(hsotg->regs + PCGCTL);
	pcgcctl &= ~PCGCTL_PWRCLMP;
	dwc2_writel(pcgcctl, hsotg->regs + PCGCTL);

	pcgcctl = dwc2_readl(hsotg->regs + PCGCTL);
	pcgcctl &= ~PCGCTL_RSTPDWNMODULE;
	dwc2_writel(pcgcctl, hsotg->regs + PCGCTL);

	udelay(100);
	if (restore) {
		ret = dwc2_restore_global_registers(hsotg);
		if (ret) {
			dev_err(hsotg->dev, "%s: failed to restore registers\n",
					__func__);
			return ret;
		}
		if (dwc2_is_host_mode(hsotg)) {
			ret = dwc2_restore_host_registers(hsotg);
			if (ret) {
				dev_err(hsotg->dev, "%s: failed to restore host registers\n",
						__func__);
				return ret;
			}
		} else {
			ret = dwc2_restore_device_registers(hsotg);
			if (ret) {
				dev_err(hsotg->dev, "%s: failed to restore device registers\n",
						__func__);
				return ret;
			}
		}
	}

	return ret;
}

/**
 * dwc2_enter_hibernation() - Put controller in Partial Power Down.
 *
 * @hsotg: Programming view of the DWC_otg controller
 */
int dwc2_enter_hibernation(struct dwc2_hsotg *hsotg)
{
	u32 pcgcctl;
	int ret = 0;

	if (!hsotg->core_params->hibernation)
		return -ENOTSUPP;

	/* Backup all registers */
	ret = dwc2_backup_global_registers(hsotg);
	if (ret) {
		dev_err(hsotg->dev, "%s: failed to backup global registers\n",
				__func__);
		return ret;
	}

	if (dwc2_is_host_mode(hsotg)) {
		ret = dwc2_backup_host_registers(hsotg);
		if (ret) {
			dev_err(hsotg->dev, "%s: failed to backup host registers\n",
					__func__);
			return ret;
		}
	} else {
		ret = dwc2_backup_device_registers(hsotg);
		if (ret) {
			dev_err(hsotg->dev, "%s: failed to backup device registers\n",
					__func__);
			return ret;
		}
	}

	/*
	 * Clear any pending interrupts since dwc2 will not be able to
	 * clear them after entering hibernation.
	 */
	dwc2_writel(0xffffffff, hsotg->regs + GINTSTS);

	/* Put the controller in low power state */
	pcgcctl = dwc2_readl(hsotg->regs + PCGCTL);

	pcgcctl |= PCGCTL_PWRCLMP;
	dwc2_writel(pcgcctl, hsotg->regs + PCGCTL);
	ndelay(20);

	pcgcctl |= PCGCTL_RSTPDWNMODULE;
	dwc2_writel(pcgcctl, hsotg->regs + PCGCTL);
	ndelay(20);

	pcgcctl |= PCGCTL_STOPPCLK;
	dwc2_writel(pcgcctl, hsotg->regs + PCGCTL);

	return ret;
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
 */
static bool dwc2_iddig_filter_enabled(struct dwc2_hsotg *hsotg)
{
	u32 gsnpsid;
	u32 ghwcfg4;

	if (!dwc2_hw_is_otg(hsotg))
		return false;

	/* Check if core configuration includes the IDDIG filter. */
	ghwcfg4 = dwc2_readl(hsotg->regs + GHWCFG4);
	if (!(ghwcfg4 & GHWCFG4_IDDIG_FILT_EN))
		return false;

	/*
	 * Check if the IDDIG debounce filter is bypassed. Available
	 * in core version >= 3.10a.
	 */
	gsnpsid = dwc2_readl(hsotg->regs + GSNPSID);
	if (gsnpsid >= DWC2_CORE_REV_3_10a) {
		u32 gotgctl = dwc2_readl(hsotg->regs + GOTGCTL);

		if (gotgctl & GOTGCTL_DBNCE_FLTR_BYPASS)
			return false;
	}

	return true;
}

/*
 * Do core a soft reset of the core.  Be careful with this because it
 * resets all the internal state machines of the core.
 */
int dwc2_core_reset(struct dwc2_hsotg *hsotg)
{
	u32 greset;
	int count = 0;
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
		u32 gotgctl = dwc2_readl(hsotg->regs + GOTGCTL);
		u32 gusbcfg = dwc2_readl(hsotg->regs + GUSBCFG);

		if (!(gotgctl & GOTGCTL_CONID_B) ||
		    (gusbcfg & GUSBCFG_FORCEHOSTMODE)) {
			wait_for_host_mode = true;
		}
	}

	/* Core Soft Reset */
	greset = dwc2_readl(hsotg->regs + GRSTCTL);
	greset |= GRSTCTL_CSFTRST;
	dwc2_writel(greset, hsotg->regs + GRSTCTL);
	do {
		udelay(1);
		greset = dwc2_readl(hsotg->regs + GRSTCTL);
		if (++count > 50) {
			dev_warn(hsotg->dev,
				 "%s() HANG! Soft Reset GRSTCTL=%0x\n",
				 __func__, greset);
			return -EBUSY;
		}
	} while (greset & GRSTCTL_CSFTRST);

	/* Wait for AHB master IDLE state */
	count = 0;
	do {
		udelay(1);
		greset = dwc2_readl(hsotg->regs + GRSTCTL);
		if (++count > 50) {
			dev_warn(hsotg->dev,
				 "%s() HANG! AHB Idle GRSTCTL=%0x\n",
				 __func__, greset);
			return -EBUSY;
		}
	} while (!(greset & GRSTCTL_AHBIDLE));

	if (wait_for_host_mode)
		dwc2_wait_for_mode(hsotg, true);

	return 0;
}

/*
 * Force the mode of the controller.
 *
 * Forcing the mode is needed for two cases:
 *
 * 1) If the dr_mode is set to either HOST or PERIPHERAL we force the
 * controller to stay in a particular mode regardless of ID pin
 * changes. We do this usually after a core reset.
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
 */
static bool dwc2_force_mode(struct dwc2_hsotg *hsotg, bool host)
{
	u32 gusbcfg;
	u32 set;
	u32 clear;

	dev_dbg(hsotg->dev, "Forcing mode to %s\n", host ? "host" : "device");

	/*
	 * Force mode has no effect if the hardware is not OTG.
	 */
	if (!dwc2_hw_is_otg(hsotg))
		return false;

	/*
	 * If dr_mode is either peripheral or host only, there is no
	 * need to ever force the mode to the opposite mode.
	 */
	if (WARN_ON(host && hsotg->dr_mode == USB_DR_MODE_PERIPHERAL))
		return false;

	if (WARN_ON(!host && hsotg->dr_mode == USB_DR_MODE_HOST))
		return false;

	gusbcfg = dwc2_readl(hsotg->regs + GUSBCFG);

	set = host ? GUSBCFG_FORCEHOSTMODE : GUSBCFG_FORCEDEVMODE;
	clear = host ? GUSBCFG_FORCEDEVMODE : GUSBCFG_FORCEHOSTMODE;

	gusbcfg &= ~clear;
	gusbcfg |= set;
	dwc2_writel(gusbcfg, hsotg->regs + GUSBCFG);

	dwc2_wait_for_mode(hsotg, host);
	return true;
}

/**
 * dwc2_clear_force_mode() - Clears the force mode bits.
 *
 * After clearing the bits, wait up to 100 ms to account for any
 * potential IDDIG filter delay. We can't know if we expect this delay
 * or not because the value of the connector ID status is affected by
 * the force mode. We only need to call this once during probe if
 * dr_mode == OTG.
 */
static void dwc2_clear_force_mode(struct dwc2_hsotg *hsotg)
{
	u32 gusbcfg;

	gusbcfg = dwc2_readl(hsotg->regs + GUSBCFG);
	gusbcfg &= ~GUSBCFG_FORCEHOSTMODE;
	gusbcfg &= ~GUSBCFG_FORCEDEVMODE;
	dwc2_writel(gusbcfg, hsotg->regs + GUSBCFG);

	if (dwc2_iddig_filter_enabled(hsotg))
		usleep_range(100000, 110000);
}

/*
 * Sets or clears force mode based on the dr_mode parameter.
 */
void dwc2_force_dr_mode(struct dwc2_hsotg *hsotg)
{
	bool ret;

	switch (hsotg->dr_mode) {
	case USB_DR_MODE_HOST:
		ret = dwc2_force_mode(hsotg, true);
		/*
		 * NOTE: This is required for some rockchip soc based
		 * platforms on their host-only dwc2.
		 */
		if (!ret)
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
 * Do core a soft reset of the core.  Be careful with this because it
 * resets all the internal state machines of the core.
 *
 * Additionally this will apply force mode as per the hsotg->dr_mode
 * parameter.
 */
int dwc2_core_reset_and_force_dr_mode(struct dwc2_hsotg *hsotg)
{
	int retval;

	retval = dwc2_core_reset(hsotg);
	if (retval)
		return retval;

	dwc2_force_dr_mode(hsotg);
	return 0;
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
		(unsigned long)addr, dwc2_readl(addr));
	addr = hsotg->regs + HFIR;
	dev_dbg(hsotg->dev, "HFIR	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));
	addr = hsotg->regs + HFNUM;
	dev_dbg(hsotg->dev, "HFNUM	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));
	addr = hsotg->regs + HPTXSTS;
	dev_dbg(hsotg->dev, "HPTXSTS	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));
	addr = hsotg->regs + HAINT;
	dev_dbg(hsotg->dev, "HAINT	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));
	addr = hsotg->regs + HAINTMSK;
	dev_dbg(hsotg->dev, "HAINTMSK	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));
	if (hsotg->core_params->dma_desc_enable > 0) {
		addr = hsotg->regs + HFLBADDR;
		dev_dbg(hsotg->dev, "HFLBADDR @0x%08lX : 0x%08X\n",
			(unsigned long)addr, dwc2_readl(addr));
	}

	addr = hsotg->regs + HPRT0;
	dev_dbg(hsotg->dev, "HPRT0	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));

	for (i = 0; i < hsotg->core_params->host_channels; i++) {
		dev_dbg(hsotg->dev, "Host Channel %d Specific Registers\n", i);
		addr = hsotg->regs + HCCHAR(i);
		dev_dbg(hsotg->dev, "HCCHAR	 @0x%08lX : 0x%08X\n",
			(unsigned long)addr, dwc2_readl(addr));
		addr = hsotg->regs + HCSPLT(i);
		dev_dbg(hsotg->dev, "HCSPLT	 @0x%08lX : 0x%08X\n",
			(unsigned long)addr, dwc2_readl(addr));
		addr = hsotg->regs + HCINT(i);
		dev_dbg(hsotg->dev, "HCINT	 @0x%08lX : 0x%08X\n",
			(unsigned long)addr, dwc2_readl(addr));
		addr = hsotg->regs + HCINTMSK(i);
		dev_dbg(hsotg->dev, "HCINTMSK	 @0x%08lX : 0x%08X\n",
			(unsigned long)addr, dwc2_readl(addr));
		addr = hsotg->regs + HCTSIZ(i);
		dev_dbg(hsotg->dev, "HCTSIZ	 @0x%08lX : 0x%08X\n",
			(unsigned long)addr, dwc2_readl(addr));
		addr = hsotg->regs + HCDMA(i);
		dev_dbg(hsotg->dev, "HCDMA	 @0x%08lX : 0x%08X\n",
			(unsigned long)addr, dwc2_readl(addr));
		if (hsotg->core_params->dma_desc_enable > 0) {
			addr = hsotg->regs + HCDMAB(i);
			dev_dbg(hsotg->dev, "HCDMAB	 @0x%08lX : 0x%08X\n",
				(unsigned long)addr, dwc2_readl(addr));
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
		(unsigned long)addr, dwc2_readl(addr));
	addr = hsotg->regs + GOTGINT;
	dev_dbg(hsotg->dev, "GOTGINT	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));
	addr = hsotg->regs + GAHBCFG;
	dev_dbg(hsotg->dev, "GAHBCFG	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));
	addr = hsotg->regs + GUSBCFG;
	dev_dbg(hsotg->dev, "GUSBCFG	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));
	addr = hsotg->regs + GRSTCTL;
	dev_dbg(hsotg->dev, "GRSTCTL	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));
	addr = hsotg->regs + GINTSTS;
	dev_dbg(hsotg->dev, "GINTSTS	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));
	addr = hsotg->regs + GINTMSK;
	dev_dbg(hsotg->dev, "GINTMSK	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));
	addr = hsotg->regs + GRXSTSR;
	dev_dbg(hsotg->dev, "GRXSTSR	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));
	addr = hsotg->regs + GRXFSIZ;
	dev_dbg(hsotg->dev, "GRXFSIZ	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));
	addr = hsotg->regs + GNPTXFSIZ;
	dev_dbg(hsotg->dev, "GNPTXFSIZ	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));
	addr = hsotg->regs + GNPTXSTS;
	dev_dbg(hsotg->dev, "GNPTXSTS	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));
	addr = hsotg->regs + GI2CCTL;
	dev_dbg(hsotg->dev, "GI2CCTL	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));
	addr = hsotg->regs + GPVNDCTL;
	dev_dbg(hsotg->dev, "GPVNDCTL	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));
	addr = hsotg->regs + GGPIO;
	dev_dbg(hsotg->dev, "GGPIO	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));
	addr = hsotg->regs + GUID;
	dev_dbg(hsotg->dev, "GUID	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));
	addr = hsotg->regs + GSNPSID;
	dev_dbg(hsotg->dev, "GSNPSID	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));
	addr = hsotg->regs + GHWCFG1;
	dev_dbg(hsotg->dev, "GHWCFG1	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));
	addr = hsotg->regs + GHWCFG2;
	dev_dbg(hsotg->dev, "GHWCFG2	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));
	addr = hsotg->regs + GHWCFG3;
	dev_dbg(hsotg->dev, "GHWCFG3	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));
	addr = hsotg->regs + GHWCFG4;
	dev_dbg(hsotg->dev, "GHWCFG4	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));
	addr = hsotg->regs + GLPMCFG;
	dev_dbg(hsotg->dev, "GLPMCFG	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));
	addr = hsotg->regs + GPWRDN;
	dev_dbg(hsotg->dev, "GPWRDN	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));
	addr = hsotg->regs + GDFIFOCFG;
	dev_dbg(hsotg->dev, "GDFIFOCFG	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));
	addr = hsotg->regs + HPTXFSIZ;
	dev_dbg(hsotg->dev, "HPTXFSIZ	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));

	addr = hsotg->regs + PCGCTL;
	dev_dbg(hsotg->dev, "PCGCTL	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(addr));
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
	int count = 0;

	dev_vdbg(hsotg->dev, "Flush Tx FIFO %d\n", num);

	greset = GRSTCTL_TXFFLSH;
	greset |= num << GRSTCTL_TXFNUM_SHIFT & GRSTCTL_TXFNUM_MASK;
	dwc2_writel(greset, hsotg->regs + GRSTCTL);

	do {
		greset = dwc2_readl(hsotg->regs + GRSTCTL);
		if (++count > 10000) {
			dev_warn(hsotg->dev,
				 "%s() HANG! GRSTCTL=%0x GNPTXSTS=0x%08x\n",
				 __func__, greset,
				 dwc2_readl(hsotg->regs + GNPTXSTS));
			break;
		}
		udelay(1);
	} while (greset & GRSTCTL_TXFFLSH);

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
	int count = 0;

	dev_vdbg(hsotg->dev, "%s()\n", __func__);

	greset = GRSTCTL_RXFFLSH;
	dwc2_writel(greset, hsotg->regs + GRSTCTL);

	do {
		greset = dwc2_readl(hsotg->regs + GRSTCTL);
		if (++count > 10000) {
			dev_warn(hsotg->dev, "%s() HANG! GRSTCTL=%0x\n",
				 __func__, greset);
			break;
		}
		udelay(1);
	} while (greset & GRSTCTL_RXFFLSH);

	/* Wait for at least 3 PHY Clocks */
	udelay(1);
}

#define DWC2_OUT_OF_BOUNDS(a, b, c)	((a) < (b) || (a) > (c))

/* Parameter access functions */
void dwc2_set_param_otg_cap(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;

	switch (val) {
	case DWC2_CAP_PARAM_HNP_SRP_CAPABLE:
		if (hsotg->hw_params.op_mode != GHWCFG2_OP_MODE_HNP_SRP_CAPABLE)
			valid = 0;
		break;
	case DWC2_CAP_PARAM_SRP_ONLY_CAPABLE:
		switch (hsotg->hw_params.op_mode) {
		case GHWCFG2_OP_MODE_HNP_SRP_CAPABLE:
		case GHWCFG2_OP_MODE_SRP_ONLY_CAPABLE:
		case GHWCFG2_OP_MODE_SRP_CAPABLE_DEVICE:
		case GHWCFG2_OP_MODE_SRP_CAPABLE_HOST:
			break;
		default:
			valid = 0;
			break;
		}
		break;
	case DWC2_CAP_PARAM_NO_HNP_SRP_CAPABLE:
		/* always valid */
		break;
	default:
		valid = 0;
		break;
	}

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for otg_cap parameter. Check HW configuration.\n",
				val);
		switch (hsotg->hw_params.op_mode) {
		case GHWCFG2_OP_MODE_HNP_SRP_CAPABLE:
			val = DWC2_CAP_PARAM_HNP_SRP_CAPABLE;
			break;
		case GHWCFG2_OP_MODE_SRP_ONLY_CAPABLE:
		case GHWCFG2_OP_MODE_SRP_CAPABLE_DEVICE:
		case GHWCFG2_OP_MODE_SRP_CAPABLE_HOST:
			val = DWC2_CAP_PARAM_SRP_ONLY_CAPABLE;
			break;
		default:
			val = DWC2_CAP_PARAM_NO_HNP_SRP_CAPABLE;
			break;
		}
		dev_dbg(hsotg->dev, "Setting otg_cap to %d\n", val);
	}

	hsotg->core_params->otg_cap = val;
}

void dwc2_set_param_dma_enable(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;

	if (val > 0 && hsotg->hw_params.arch == GHWCFG2_SLAVE_ONLY_ARCH)
		valid = 0;
	if (val < 0)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for dma_enable parameter. Check HW configuration.\n",
				val);
		val = hsotg->hw_params.arch != GHWCFG2_SLAVE_ONLY_ARCH;
		dev_dbg(hsotg->dev, "Setting dma_enable to %d\n", val);
	}

	hsotg->core_params->dma_enable = val;
}

void dwc2_set_param_dma_desc_enable(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;

	if (val > 0 && (hsotg->core_params->dma_enable <= 0 ||
			!hsotg->hw_params.dma_desc_enable))
		valid = 0;
	if (val < 0)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for dma_desc_enable parameter. Check HW configuration.\n",
				val);
		val = (hsotg->core_params->dma_enable > 0 &&
			hsotg->hw_params.dma_desc_enable);
		dev_dbg(hsotg->dev, "Setting dma_desc_enable to %d\n", val);
	}

	hsotg->core_params->dma_desc_enable = val;
}

void dwc2_set_param_dma_desc_fs_enable(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;

	if (val > 0 && (hsotg->core_params->dma_enable <= 0 ||
			!hsotg->hw_params.dma_desc_enable))
		valid = 0;
	if (val < 0)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for dma_desc_fs_enable parameter. Check HW configuration.\n",
				val);
		val = (hsotg->core_params->dma_enable > 0 &&
			hsotg->hw_params.dma_desc_enable);
	}

	hsotg->core_params->dma_desc_fs_enable = val;
	dev_dbg(hsotg->dev, "Setting dma_desc_fs_enable to %d\n", val);
}

void dwc2_set_param_host_support_fs_ls_low_power(struct dwc2_hsotg *hsotg,
						 int val)
{
	if (DWC2_OUT_OF_BOUNDS(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev,
				"Wrong value for host_support_fs_low_power\n");
			dev_err(hsotg->dev,
				"host_support_fs_low_power must be 0 or 1\n");
		}
		val = 0;
		dev_dbg(hsotg->dev,
			"Setting host_support_fs_low_power to %d\n", val);
	}

	hsotg->core_params->host_support_fs_ls_low_power = val;
}

void dwc2_set_param_enable_dynamic_fifo(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;

	if (val > 0 && !hsotg->hw_params.enable_dynamic_fifo)
		valid = 0;
	if (val < 0)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for enable_dynamic_fifo parameter. Check HW configuration.\n",
				val);
		val = hsotg->hw_params.enable_dynamic_fifo;
		dev_dbg(hsotg->dev, "Setting enable_dynamic_fifo to %d\n", val);
	}

	hsotg->core_params->enable_dynamic_fifo = val;
}

void dwc2_set_param_host_rx_fifo_size(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;

	if (val < 16 || val > hsotg->hw_params.host_rx_fifo_size)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for host_rx_fifo_size. Check HW configuration.\n",
				val);
		val = hsotg->hw_params.host_rx_fifo_size;
		dev_dbg(hsotg->dev, "Setting host_rx_fifo_size to %d\n", val);
	}

	hsotg->core_params->host_rx_fifo_size = val;
}

void dwc2_set_param_host_nperio_tx_fifo_size(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;

	if (val < 16 || val > hsotg->hw_params.host_nperio_tx_fifo_size)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for host_nperio_tx_fifo_size. Check HW configuration.\n",
				val);
		val = hsotg->hw_params.host_nperio_tx_fifo_size;
		dev_dbg(hsotg->dev, "Setting host_nperio_tx_fifo_size to %d\n",
			val);
	}

	hsotg->core_params->host_nperio_tx_fifo_size = val;
}

void dwc2_set_param_host_perio_tx_fifo_size(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;

	if (val < 16 || val > hsotg->hw_params.host_perio_tx_fifo_size)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for host_perio_tx_fifo_size. Check HW configuration.\n",
				val);
		val = hsotg->hw_params.host_perio_tx_fifo_size;
		dev_dbg(hsotg->dev, "Setting host_perio_tx_fifo_size to %d\n",
			val);
	}

	hsotg->core_params->host_perio_tx_fifo_size = val;
}

void dwc2_set_param_max_transfer_size(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;

	if (val < 2047 || val > hsotg->hw_params.max_transfer_size)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for max_transfer_size. Check HW configuration.\n",
				val);
		val = hsotg->hw_params.max_transfer_size;
		dev_dbg(hsotg->dev, "Setting max_transfer_size to %d\n", val);
	}

	hsotg->core_params->max_transfer_size = val;
}

void dwc2_set_param_max_packet_count(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;

	if (val < 15 || val > hsotg->hw_params.max_packet_count)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for max_packet_count. Check HW configuration.\n",
				val);
		val = hsotg->hw_params.max_packet_count;
		dev_dbg(hsotg->dev, "Setting max_packet_count to %d\n", val);
	}

	hsotg->core_params->max_packet_count = val;
}

void dwc2_set_param_host_channels(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;

	if (val < 1 || val > hsotg->hw_params.host_channels)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for host_channels. Check HW configuration.\n",
				val);
		val = hsotg->hw_params.host_channels;
		dev_dbg(hsotg->dev, "Setting host_channels to %d\n", val);
	}

	hsotg->core_params->host_channels = val;
}

void dwc2_set_param_phy_type(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 0;
	u32 hs_phy_type, fs_phy_type;

	if (DWC2_OUT_OF_BOUNDS(val, DWC2_PHY_TYPE_PARAM_FS,
			       DWC2_PHY_TYPE_PARAM_ULPI)) {
		if (val >= 0) {
			dev_err(hsotg->dev, "Wrong value for phy_type\n");
			dev_err(hsotg->dev, "phy_type must be 0, 1 or 2\n");
		}

		valid = 0;
	}

	hs_phy_type = hsotg->hw_params.hs_phy_type;
	fs_phy_type = hsotg->hw_params.fs_phy_type;
	if (val == DWC2_PHY_TYPE_PARAM_UTMI &&
	    (hs_phy_type == GHWCFG2_HS_PHY_TYPE_UTMI ||
	     hs_phy_type == GHWCFG2_HS_PHY_TYPE_UTMI_ULPI))
		valid = 1;
	else if (val == DWC2_PHY_TYPE_PARAM_ULPI &&
		 (hs_phy_type == GHWCFG2_HS_PHY_TYPE_ULPI ||
		  hs_phy_type == GHWCFG2_HS_PHY_TYPE_UTMI_ULPI))
		valid = 1;
	else if (val == DWC2_PHY_TYPE_PARAM_FS &&
		 fs_phy_type == GHWCFG2_FS_PHY_TYPE_DEDICATED)
		valid = 1;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for phy_type. Check HW configuration.\n",
				val);
		val = DWC2_PHY_TYPE_PARAM_FS;
		if (hs_phy_type != GHWCFG2_HS_PHY_TYPE_NOT_SUPPORTED) {
			if (hs_phy_type == GHWCFG2_HS_PHY_TYPE_UTMI ||
			    hs_phy_type == GHWCFG2_HS_PHY_TYPE_UTMI_ULPI)
				val = DWC2_PHY_TYPE_PARAM_UTMI;
			else
				val = DWC2_PHY_TYPE_PARAM_ULPI;
		}
		dev_dbg(hsotg->dev, "Setting phy_type to %d\n", val);
	}

	hsotg->core_params->phy_type = val;
}

static int dwc2_get_param_phy_type(struct dwc2_hsotg *hsotg)
{
	return hsotg->core_params->phy_type;
}

void dwc2_set_param_speed(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;

	if (DWC2_OUT_OF_BOUNDS(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev, "Wrong value for speed parameter\n");
			dev_err(hsotg->dev, "max_speed parameter must be 0 or 1\n");
		}
		valid = 0;
	}

	if (val == DWC2_SPEED_PARAM_HIGH &&
	    dwc2_get_param_phy_type(hsotg) == DWC2_PHY_TYPE_PARAM_FS)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for speed parameter. Check HW configuration.\n",
				val);
		val = dwc2_get_param_phy_type(hsotg) == DWC2_PHY_TYPE_PARAM_FS ?
				DWC2_SPEED_PARAM_FULL : DWC2_SPEED_PARAM_HIGH;
		dev_dbg(hsotg->dev, "Setting speed to %d\n", val);
	}

	hsotg->core_params->speed = val;
}

void dwc2_set_param_host_ls_low_power_phy_clk(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;

	if (DWC2_OUT_OF_BOUNDS(val, DWC2_HOST_LS_LOW_POWER_PHY_CLK_PARAM_48MHZ,
			       DWC2_HOST_LS_LOW_POWER_PHY_CLK_PARAM_6MHZ)) {
		if (val >= 0) {
			dev_err(hsotg->dev,
				"Wrong value for host_ls_low_power_phy_clk parameter\n");
			dev_err(hsotg->dev,
				"host_ls_low_power_phy_clk must be 0 or 1\n");
		}
		valid = 0;
	}

	if (val == DWC2_HOST_LS_LOW_POWER_PHY_CLK_PARAM_48MHZ &&
	    dwc2_get_param_phy_type(hsotg) == DWC2_PHY_TYPE_PARAM_FS)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for host_ls_low_power_phy_clk. Check HW configuration.\n",
				val);
		val = dwc2_get_param_phy_type(hsotg) == DWC2_PHY_TYPE_PARAM_FS
			? DWC2_HOST_LS_LOW_POWER_PHY_CLK_PARAM_6MHZ
			: DWC2_HOST_LS_LOW_POWER_PHY_CLK_PARAM_48MHZ;
		dev_dbg(hsotg->dev, "Setting host_ls_low_power_phy_clk to %d\n",
			val);
	}

	hsotg->core_params->host_ls_low_power_phy_clk = val;
}

void dwc2_set_param_phy_ulpi_ddr(struct dwc2_hsotg *hsotg, int val)
{
	if (DWC2_OUT_OF_BOUNDS(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev, "Wrong value for phy_ulpi_ddr\n");
			dev_err(hsotg->dev, "phy_upli_ddr must be 0 or 1\n");
		}
		val = 0;
		dev_dbg(hsotg->dev, "Setting phy_upli_ddr to %d\n", val);
	}

	hsotg->core_params->phy_ulpi_ddr = val;
}

void dwc2_set_param_phy_ulpi_ext_vbus(struct dwc2_hsotg *hsotg, int val)
{
	if (DWC2_OUT_OF_BOUNDS(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev,
				"Wrong value for phy_ulpi_ext_vbus\n");
			dev_err(hsotg->dev,
				"phy_ulpi_ext_vbus must be 0 or 1\n");
		}
		val = 0;
		dev_dbg(hsotg->dev, "Setting phy_ulpi_ext_vbus to %d\n", val);
	}

	hsotg->core_params->phy_ulpi_ext_vbus = val;
}

void dwc2_set_param_phy_utmi_width(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 0;

	switch (hsotg->hw_params.utmi_phy_data_width) {
	case GHWCFG4_UTMI_PHY_DATA_WIDTH_8:
		valid = (val == 8);
		break;
	case GHWCFG4_UTMI_PHY_DATA_WIDTH_16:
		valid = (val == 16);
		break;
	case GHWCFG4_UTMI_PHY_DATA_WIDTH_8_OR_16:
		valid = (val == 8 || val == 16);
		break;
	}

	if (!valid) {
		if (val >= 0) {
			dev_err(hsotg->dev,
				"%d invalid for phy_utmi_width. Check HW configuration.\n",
				val);
		}
		val = (hsotg->hw_params.utmi_phy_data_width ==
		       GHWCFG4_UTMI_PHY_DATA_WIDTH_8) ? 8 : 16;
		dev_dbg(hsotg->dev, "Setting phy_utmi_width to %d\n", val);
	}

	hsotg->core_params->phy_utmi_width = val;
}

void dwc2_set_param_ulpi_fs_ls(struct dwc2_hsotg *hsotg, int val)
{
	if (DWC2_OUT_OF_BOUNDS(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev, "Wrong value for ulpi_fs_ls\n");
			dev_err(hsotg->dev, "ulpi_fs_ls must be 0 or 1\n");
		}
		val = 0;
		dev_dbg(hsotg->dev, "Setting ulpi_fs_ls to %d\n", val);
	}

	hsotg->core_params->ulpi_fs_ls = val;
}

void dwc2_set_param_ts_dline(struct dwc2_hsotg *hsotg, int val)
{
	if (DWC2_OUT_OF_BOUNDS(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev, "Wrong value for ts_dline\n");
			dev_err(hsotg->dev, "ts_dline must be 0 or 1\n");
		}
		val = 0;
		dev_dbg(hsotg->dev, "Setting ts_dline to %d\n", val);
	}

	hsotg->core_params->ts_dline = val;
}

void dwc2_set_param_i2c_enable(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;

	if (DWC2_OUT_OF_BOUNDS(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev, "Wrong value for i2c_enable\n");
			dev_err(hsotg->dev, "i2c_enable must be 0 or 1\n");
		}

		valid = 0;
	}

	if (val == 1 && !(hsotg->hw_params.i2c_enable))
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for i2c_enable. Check HW configuration.\n",
				val);
		val = hsotg->hw_params.i2c_enable;
		dev_dbg(hsotg->dev, "Setting i2c_enable to %d\n", val);
	}

	hsotg->core_params->i2c_enable = val;
}

void dwc2_set_param_en_multiple_tx_fifo(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;

	if (DWC2_OUT_OF_BOUNDS(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev,
				"Wrong value for en_multiple_tx_fifo,\n");
			dev_err(hsotg->dev,
				"en_multiple_tx_fifo must be 0 or 1\n");
		}
		valid = 0;
	}

	if (val == 1 && !hsotg->hw_params.en_multiple_tx_fifo)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for parameter en_multiple_tx_fifo. Check HW configuration.\n",
				val);
		val = hsotg->hw_params.en_multiple_tx_fifo;
		dev_dbg(hsotg->dev, "Setting en_multiple_tx_fifo to %d\n", val);
	}

	hsotg->core_params->en_multiple_tx_fifo = val;
}

void dwc2_set_param_reload_ctl(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;

	if (DWC2_OUT_OF_BOUNDS(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev,
				"'%d' invalid for parameter reload_ctl\n", val);
			dev_err(hsotg->dev, "reload_ctl must be 0 or 1\n");
		}
		valid = 0;
	}

	if (val == 1 && hsotg->hw_params.snpsid < DWC2_CORE_REV_2_92a)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for parameter reload_ctl. Check HW configuration.\n",
				val);
		val = hsotg->hw_params.snpsid >= DWC2_CORE_REV_2_92a;
		dev_dbg(hsotg->dev, "Setting reload_ctl to %d\n", val);
	}

	hsotg->core_params->reload_ctl = val;
}

void dwc2_set_param_ahbcfg(struct dwc2_hsotg *hsotg, int val)
{
	if (val != -1)
		hsotg->core_params->ahbcfg = val;
	else
		hsotg->core_params->ahbcfg = GAHBCFG_HBSTLEN_INCR4 <<
						GAHBCFG_HBSTLEN_SHIFT;
}

void dwc2_set_param_otg_ver(struct dwc2_hsotg *hsotg, int val)
{
	if (DWC2_OUT_OF_BOUNDS(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev,
				"'%d' invalid for parameter otg_ver\n", val);
			dev_err(hsotg->dev,
				"otg_ver must be 0 (for OTG 1.3 support) or 1 (for OTG 2.0 support)\n");
		}
		val = 0;
		dev_dbg(hsotg->dev, "Setting otg_ver to %d\n", val);
	}

	hsotg->core_params->otg_ver = val;
}

static void dwc2_set_param_uframe_sched(struct dwc2_hsotg *hsotg, int val)
{
	if (DWC2_OUT_OF_BOUNDS(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev,
				"'%d' invalid for parameter uframe_sched\n",
				val);
			dev_err(hsotg->dev, "uframe_sched must be 0 or 1\n");
		}
		val = 1;
		dev_dbg(hsotg->dev, "Setting uframe_sched to %d\n", val);
	}

	hsotg->core_params->uframe_sched = val;
}

static void dwc2_set_param_external_id_pin_ctl(struct dwc2_hsotg *hsotg,
		int val)
{
	if (DWC2_OUT_OF_BOUNDS(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev,
				"'%d' invalid for parameter external_id_pin_ctl\n",
				val);
			dev_err(hsotg->dev, "external_id_pin_ctl must be 0 or 1\n");
		}
		val = 0;
		dev_dbg(hsotg->dev, "Setting external_id_pin_ctl to %d\n", val);
	}

	hsotg->core_params->external_id_pin_ctl = val;
}

static void dwc2_set_param_hibernation(struct dwc2_hsotg *hsotg,
		int val)
{
	if (DWC2_OUT_OF_BOUNDS(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev,
				"'%d' invalid for parameter hibernation\n",
				val);
			dev_err(hsotg->dev, "hibernation must be 0 or 1\n");
		}
		val = 0;
		dev_dbg(hsotg->dev, "Setting hibernation to %d\n", val);
	}

	hsotg->core_params->hibernation = val;
}

/*
 * This function is called during module intialization to pass module parameters
 * for the DWC_otg core.
 */
void dwc2_set_parameters(struct dwc2_hsotg *hsotg,
			 const struct dwc2_core_params *params)
{
	dev_dbg(hsotg->dev, "%s()\n", __func__);

	dwc2_set_param_otg_cap(hsotg, params->otg_cap);
	dwc2_set_param_dma_enable(hsotg, params->dma_enable);
	dwc2_set_param_dma_desc_enable(hsotg, params->dma_desc_enable);
	dwc2_set_param_dma_desc_fs_enable(hsotg, params->dma_desc_fs_enable);
	dwc2_set_param_host_support_fs_ls_low_power(hsotg,
			params->host_support_fs_ls_low_power);
	dwc2_set_param_enable_dynamic_fifo(hsotg,
			params->enable_dynamic_fifo);
	dwc2_set_param_host_rx_fifo_size(hsotg,
			params->host_rx_fifo_size);
	dwc2_set_param_host_nperio_tx_fifo_size(hsotg,
			params->host_nperio_tx_fifo_size);
	dwc2_set_param_host_perio_tx_fifo_size(hsotg,
			params->host_perio_tx_fifo_size);
	dwc2_set_param_max_transfer_size(hsotg,
			params->max_transfer_size);
	dwc2_set_param_max_packet_count(hsotg,
			params->max_packet_count);
	dwc2_set_param_host_channels(hsotg, params->host_channels);
	dwc2_set_param_phy_type(hsotg, params->phy_type);
	dwc2_set_param_speed(hsotg, params->speed);
	dwc2_set_param_host_ls_low_power_phy_clk(hsotg,
			params->host_ls_low_power_phy_clk);
	dwc2_set_param_phy_ulpi_ddr(hsotg, params->phy_ulpi_ddr);
	dwc2_set_param_phy_ulpi_ext_vbus(hsotg,
			params->phy_ulpi_ext_vbus);
	dwc2_set_param_phy_utmi_width(hsotg, params->phy_utmi_width);
	dwc2_set_param_ulpi_fs_ls(hsotg, params->ulpi_fs_ls);
	dwc2_set_param_ts_dline(hsotg, params->ts_dline);
	dwc2_set_param_i2c_enable(hsotg, params->i2c_enable);
	dwc2_set_param_en_multiple_tx_fifo(hsotg,
			params->en_multiple_tx_fifo);
	dwc2_set_param_reload_ctl(hsotg, params->reload_ctl);
	dwc2_set_param_ahbcfg(hsotg, params->ahbcfg);
	dwc2_set_param_otg_ver(hsotg, params->otg_ver);
	dwc2_set_param_uframe_sched(hsotg, params->uframe_sched);
	dwc2_set_param_external_id_pin_ctl(hsotg, params->external_id_pin_ctl);
	dwc2_set_param_hibernation(hsotg, params->hibernation);
}

/*
 * Forces either host or device mode if the controller is not
 * currently in that mode.
 *
 * Returns true if the mode was forced.
 */
static bool dwc2_force_mode_if_needed(struct dwc2_hsotg *hsotg, bool host)
{
	if (host && dwc2_is_host_mode(hsotg))
		return false;
	else if (!host && dwc2_is_device_mode(hsotg))
		return false;

	return dwc2_force_mode(hsotg, host);
}

/*
 * Gets host hardware parameters. Forces host mode if not currently in
 * host mode. Should be called immediately after a core soft reset in
 * order to get the reset values.
 */
static void dwc2_get_host_hwparams(struct dwc2_hsotg *hsotg)
{
	struct dwc2_hw_params *hw = &hsotg->hw_params;
	u32 gnptxfsiz;
	u32 hptxfsiz;
	bool forced;

	if (hsotg->dr_mode == USB_DR_MODE_PERIPHERAL)
		return;

	forced = dwc2_force_mode_if_needed(hsotg, true);

	gnptxfsiz = dwc2_readl(hsotg->regs + GNPTXFSIZ);
	hptxfsiz = dwc2_readl(hsotg->regs + HPTXFSIZ);
	dev_dbg(hsotg->dev, "gnptxfsiz=%08x\n", gnptxfsiz);
	dev_dbg(hsotg->dev, "hptxfsiz=%08x\n", hptxfsiz);

	if (forced)
		dwc2_clear_force_mode(hsotg);

	hw->host_nperio_tx_fifo_size = (gnptxfsiz & FIFOSIZE_DEPTH_MASK) >>
				       FIFOSIZE_DEPTH_SHIFT;
	hw->host_perio_tx_fifo_size = (hptxfsiz & FIFOSIZE_DEPTH_MASK) >>
				      FIFOSIZE_DEPTH_SHIFT;
}

/*
 * Gets device hardware parameters. Forces device mode if not
 * currently in device mode. Should be called immediately after a core
 * soft reset in order to get the reset values.
 */
static void dwc2_get_dev_hwparams(struct dwc2_hsotg *hsotg)
{
	struct dwc2_hw_params *hw = &hsotg->hw_params;
	bool forced;
	u32 gnptxfsiz;

	if (hsotg->dr_mode == USB_DR_MODE_HOST)
		return;

	forced = dwc2_force_mode_if_needed(hsotg, false);

	gnptxfsiz = dwc2_readl(hsotg->regs + GNPTXFSIZ);
	dev_dbg(hsotg->dev, "gnptxfsiz=%08x\n", gnptxfsiz);

	if (forced)
		dwc2_clear_force_mode(hsotg);

	hw->dev_nperio_tx_fifo_size = (gnptxfsiz & FIFOSIZE_DEPTH_MASK) >>
				       FIFOSIZE_DEPTH_SHIFT;
}

/**
 * During device initialization, read various hardware configuration
 * registers and interpret the contents.
 */
int dwc2_get_hwparams(struct dwc2_hsotg *hsotg)
{
	struct dwc2_hw_params *hw = &hsotg->hw_params;
	unsigned width;
	u32 hwcfg1, hwcfg2, hwcfg3, hwcfg4;
	u32 grxfsiz;

	/*
	 * Attempt to ensure this device is really a DWC_otg Controller.
	 * Read and verify the GSNPSID register contents. The value should be
	 * 0x45f42xxx or 0x45f43xxx, which corresponds to either "OT2" or "OT3",
	 * as in "OTG version 2.xx" or "OTG version 3.xx".
	 */
	hw->snpsid = dwc2_readl(hsotg->regs + GSNPSID);
	if ((hw->snpsid & 0xfffff000) != 0x4f542000 &&
	    (hw->snpsid & 0xfffff000) != 0x4f543000) {
		dev_err(hsotg->dev, "Bad value for GSNPSID: 0x%08x\n",
			hw->snpsid);
		return -ENODEV;
	}

	dev_dbg(hsotg->dev, "Core Release: %1x.%1x%1x%1x (snpsid=%x)\n",
		hw->snpsid >> 12 & 0xf, hw->snpsid >> 8 & 0xf,
		hw->snpsid >> 4 & 0xf, hw->snpsid & 0xf, hw->snpsid);

	hwcfg1 = dwc2_readl(hsotg->regs + GHWCFG1);
	hwcfg2 = dwc2_readl(hsotg->regs + GHWCFG2);
	hwcfg3 = dwc2_readl(hsotg->regs + GHWCFG3);
	hwcfg4 = dwc2_readl(hsotg->regs + GHWCFG4);
	grxfsiz = dwc2_readl(hsotg->regs + GRXFSIZ);

	dev_dbg(hsotg->dev, "hwcfg1=%08x\n", hwcfg1);
	dev_dbg(hsotg->dev, "hwcfg2=%08x\n", hwcfg2);
	dev_dbg(hsotg->dev, "hwcfg3=%08x\n", hwcfg3);
	dev_dbg(hsotg->dev, "hwcfg4=%08x\n", hwcfg4);
	dev_dbg(hsotg->dev, "grxfsiz=%08x\n", grxfsiz);

	/*
	 * Host specific hardware parameters. Reading these parameters
	 * requires the controller to be in host mode. The mode will
	 * be forced, if necessary, to read these values.
	 */
	dwc2_get_host_hwparams(hsotg);
	dwc2_get_dev_hwparams(hsotg);

	/* hwcfg1 */
	hw->dev_ep_dirs = hwcfg1;

	/* hwcfg2 */
	hw->op_mode = (hwcfg2 & GHWCFG2_OP_MODE_MASK) >>
		      GHWCFG2_OP_MODE_SHIFT;
	hw->arch = (hwcfg2 & GHWCFG2_ARCHITECTURE_MASK) >>
		   GHWCFG2_ARCHITECTURE_SHIFT;
	hw->enable_dynamic_fifo = !!(hwcfg2 & GHWCFG2_DYNAMIC_FIFO);
	hw->host_channels = 1 + ((hwcfg2 & GHWCFG2_NUM_HOST_CHAN_MASK) >>
				GHWCFG2_NUM_HOST_CHAN_SHIFT);
	hw->hs_phy_type = (hwcfg2 & GHWCFG2_HS_PHY_TYPE_MASK) >>
			  GHWCFG2_HS_PHY_TYPE_SHIFT;
	hw->fs_phy_type = (hwcfg2 & GHWCFG2_FS_PHY_TYPE_MASK) >>
			  GHWCFG2_FS_PHY_TYPE_SHIFT;
	hw->num_dev_ep = (hwcfg2 & GHWCFG2_NUM_DEV_EP_MASK) >>
			 GHWCFG2_NUM_DEV_EP_SHIFT;
	hw->nperio_tx_q_depth =
		(hwcfg2 & GHWCFG2_NONPERIO_TX_Q_DEPTH_MASK) >>
		GHWCFG2_NONPERIO_TX_Q_DEPTH_SHIFT << 1;
	hw->host_perio_tx_q_depth =
		(hwcfg2 & GHWCFG2_HOST_PERIO_TX_Q_DEPTH_MASK) >>
		GHWCFG2_HOST_PERIO_TX_Q_DEPTH_SHIFT << 1;
	hw->dev_token_q_depth =
		(hwcfg2 & GHWCFG2_DEV_TOKEN_Q_DEPTH_MASK) >>
		GHWCFG2_DEV_TOKEN_Q_DEPTH_SHIFT;

	/* hwcfg3 */
	width = (hwcfg3 & GHWCFG3_XFER_SIZE_CNTR_WIDTH_MASK) >>
		GHWCFG3_XFER_SIZE_CNTR_WIDTH_SHIFT;
	hw->max_transfer_size = (1 << (width + 11)) - 1;
	width = (hwcfg3 & GHWCFG3_PACKET_SIZE_CNTR_WIDTH_MASK) >>
		GHWCFG3_PACKET_SIZE_CNTR_WIDTH_SHIFT;
	hw->max_packet_count = (1 << (width + 4)) - 1;
	hw->i2c_enable = !!(hwcfg3 & GHWCFG3_I2C);
	hw->total_fifo_size = (hwcfg3 & GHWCFG3_DFIFO_DEPTH_MASK) >>
			      GHWCFG3_DFIFO_DEPTH_SHIFT;

	/* hwcfg4 */
	hw->en_multiple_tx_fifo = !!(hwcfg4 & GHWCFG4_DED_FIFO_EN);
	hw->num_dev_perio_in_ep = (hwcfg4 & GHWCFG4_NUM_DEV_PERIO_IN_EP_MASK) >>
				  GHWCFG4_NUM_DEV_PERIO_IN_EP_SHIFT;
	hw->dma_desc_enable = !!(hwcfg4 & GHWCFG4_DESC_DMA);
	hw->power_optimized = !!(hwcfg4 & GHWCFG4_POWER_OPTIMIZ);
	hw->utmi_phy_data_width = (hwcfg4 & GHWCFG4_UTMI_PHY_DATA_WIDTH_MASK) >>
				  GHWCFG4_UTMI_PHY_DATA_WIDTH_SHIFT;

	/* fifo sizes */
	hw->host_rx_fifo_size = (grxfsiz & GRXFSIZ_DEPTH_MASK) >>
				GRXFSIZ_DEPTH_SHIFT;

	dev_dbg(hsotg->dev, "Detected values from hardware:\n");
	dev_dbg(hsotg->dev, "  op_mode=%d\n",
		hw->op_mode);
	dev_dbg(hsotg->dev, "  arch=%d\n",
		hw->arch);
	dev_dbg(hsotg->dev, "  dma_desc_enable=%d\n",
		hw->dma_desc_enable);
	dev_dbg(hsotg->dev, "  power_optimized=%d\n",
		hw->power_optimized);
	dev_dbg(hsotg->dev, "  i2c_enable=%d\n",
		hw->i2c_enable);
	dev_dbg(hsotg->dev, "  hs_phy_type=%d\n",
		hw->hs_phy_type);
	dev_dbg(hsotg->dev, "  fs_phy_type=%d\n",
		hw->fs_phy_type);
	dev_dbg(hsotg->dev, "  utmi_phy_data_width=%d\n",
		hw->utmi_phy_data_width);
	dev_dbg(hsotg->dev, "  num_dev_ep=%d\n",
		hw->num_dev_ep);
	dev_dbg(hsotg->dev, "  num_dev_perio_in_ep=%d\n",
		hw->num_dev_perio_in_ep);
	dev_dbg(hsotg->dev, "  host_channels=%d\n",
		hw->host_channels);
	dev_dbg(hsotg->dev, "  max_transfer_size=%d\n",
		hw->max_transfer_size);
	dev_dbg(hsotg->dev, "  max_packet_count=%d\n",
		hw->max_packet_count);
	dev_dbg(hsotg->dev, "  nperio_tx_q_depth=0x%0x\n",
		hw->nperio_tx_q_depth);
	dev_dbg(hsotg->dev, "  host_perio_tx_q_depth=0x%0x\n",
		hw->host_perio_tx_q_depth);
	dev_dbg(hsotg->dev, "  dev_token_q_depth=0x%0x\n",
		hw->dev_token_q_depth);
	dev_dbg(hsotg->dev, "  enable_dynamic_fifo=%d\n",
		hw->enable_dynamic_fifo);
	dev_dbg(hsotg->dev, "  en_multiple_tx_fifo=%d\n",
		hw->en_multiple_tx_fifo);
	dev_dbg(hsotg->dev, "  total_fifo_size=%d\n",
		hw->total_fifo_size);
	dev_dbg(hsotg->dev, "  host_rx_fifo_size=%d\n",
		hw->host_rx_fifo_size);
	dev_dbg(hsotg->dev, "  host_nperio_tx_fifo_size=%d\n",
		hw->host_nperio_tx_fifo_size);
	dev_dbg(hsotg->dev, "  host_perio_tx_fifo_size=%d\n",
		hw->host_perio_tx_fifo_size);
	dev_dbg(hsotg->dev, "\n");

	return 0;
}

/*
 * Sets all parameters to the given value.
 *
 * Assumes that the dwc2_core_params struct contains only integers.
 */
void dwc2_set_all_params(struct dwc2_core_params *params, int value)
{
	int *p = (int *)params;
	size_t size = sizeof(*params) / sizeof(*p);
	int i;

	for (i = 0; i < size; i++)
		p[i] = value;
}


u16 dwc2_get_otg_version(struct dwc2_hsotg *hsotg)
{
	return hsotg->core_params->otg_ver == 1 ? 0x0200 : 0x0103;
}

bool dwc2_is_controller_alive(struct dwc2_hsotg *hsotg)
{
	if (dwc2_readl(hsotg->regs + GSNPSID) == 0xffffffff)
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
	u32 ahbcfg = dwc2_readl(hsotg->regs + GAHBCFG);

	ahbcfg |= GAHBCFG_GLBL_INTR_EN;
	dwc2_writel(ahbcfg, hsotg->regs + GAHBCFG);
}

/**
 * dwc2_disable_global_interrupts() - Disables the controller's Global
 * Interrupt in the AHB Config register
 *
 * @hsotg: Programming view of DWC_otg controller
 */
void dwc2_disable_global_interrupts(struct dwc2_hsotg *hsotg)
{
	u32 ahbcfg = dwc2_readl(hsotg->regs + GAHBCFG);

	ahbcfg &= ~GAHBCFG_GLBL_INTR_EN;
	dwc2_writel(ahbcfg, hsotg->regs + GAHBCFG);
}

/* Returns the controller's GHWCFG2.OTG_MODE. */
unsigned dwc2_op_mode(struct dwc2_hsotg *hsotg)
{
	u32 ghwcfg2 = dwc2_readl(hsotg->regs + GHWCFG2);

	return (ghwcfg2 & GHWCFG2_OP_MODE_MASK) >>
		GHWCFG2_OP_MODE_SHIFT;
}

/* Returns true if the controller is capable of DRD. */
bool dwc2_hw_is_otg(struct dwc2_hsotg *hsotg)
{
	unsigned op_mode = dwc2_op_mode(hsotg);

	return (op_mode == GHWCFG2_OP_MODE_HNP_SRP_CAPABLE) ||
		(op_mode == GHWCFG2_OP_MODE_SRP_ONLY_CAPABLE) ||
		(op_mode == GHWCFG2_OP_MODE_NO_HNP_SRP_CAPABLE);
}

/* Returns true if the controller is host-only. */
bool dwc2_hw_is_host(struct dwc2_hsotg *hsotg)
{
	unsigned op_mode = dwc2_op_mode(hsotg);

	return (op_mode == GHWCFG2_OP_MODE_SRP_CAPABLE_HOST) ||
		(op_mode == GHWCFG2_OP_MODE_NO_SRP_CAPABLE_HOST);
}

/* Returns true if the controller is device-only. */
bool dwc2_hw_is_device(struct dwc2_hsotg *hsotg)
{
	unsigned op_mode = dwc2_op_mode(hsotg);

	return (op_mode == GHWCFG2_OP_MODE_SRP_CAPABLE_DEVICE) ||
		(op_mode == GHWCFG2_OP_MODE_NO_SRP_CAPABLE_DEVICE);
}

MODULE_DESCRIPTION("DESIGNWARE HS OTG Core");
MODULE_AUTHOR("Synopsys, Inc.");
MODULE_LICENSE("Dual BSD/GPL");
