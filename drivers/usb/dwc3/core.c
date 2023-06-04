// SPDX-License-Identifier: GPL-2.0
/*
 * core.c - DesignWare USB3 DRD Controller Core file
 *
 * Copyright (C) 2010-2011 Texas Instruments Incorporated - https://www.ti.com
 *
 * Authors: Felipe Balbi <balbi@ti.com>,
 *	    Sebastian Andrzej Siewior <bigeasy@linutronix.de>
 */

#include <linux/clk.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/acpi.h>
#include <linux/pinctrl/consumer.h>
#include <linux/reset.h>
#include <linux/bitfield.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/of.h>
#include <linux/usb/otg.h>

#include "core.h"
#include "gadget.h"
#include "io.h"

#include "debug.h"

#define DWC3_DEFAULT_AUTOSUSPEND_DELAY	5000 /* ms */

/**
 * dwc3_get_dr_mode - Validates and sets dr_mode
 * @dwc: pointer to our context structure
 */
static int dwc3_get_dr_mode(struct dwc3 *dwc)
{
	enum usb_dr_mode mode;
	struct device *dev = dwc->dev;
	unsigned int hw_mode;

	if (dwc->dr_mode == USB_DR_MODE_UNKNOWN)
		dwc->dr_mode = USB_DR_MODE_OTG;

	mode = dwc->dr_mode;
	hw_mode = DWC3_GHWPARAMS0_MODE(dwc->hwparams.hwparams0);

	switch (hw_mode) {
	case DWC3_GHWPARAMS0_MODE_GADGET:
		if (IS_ENABLED(CONFIG_USB_DWC3_HOST)) {
			dev_err(dev,
				"Controller does not support host mode.\n");
			return -EINVAL;
		}
		mode = USB_DR_MODE_PERIPHERAL;
		break;
	case DWC3_GHWPARAMS0_MODE_HOST:
		if (IS_ENABLED(CONFIG_USB_DWC3_GADGET)) {
			dev_err(dev,
				"Controller does not support device mode.\n");
			return -EINVAL;
		}
		mode = USB_DR_MODE_HOST;
		break;
	default:
		if (IS_ENABLED(CONFIG_USB_DWC3_HOST))
			mode = USB_DR_MODE_HOST;
		else if (IS_ENABLED(CONFIG_USB_DWC3_GADGET))
			mode = USB_DR_MODE_PERIPHERAL;

		/*
		 * DWC_usb31 and DWC_usb3 v3.30a and higher do not support OTG
		 * mode. If the controller supports DRD but the dr_mode is not
		 * specified or set to OTG, then set the mode to peripheral.
		 */
		if (mode == USB_DR_MODE_OTG && !dwc->edev &&
		    (!IS_ENABLED(CONFIG_USB_ROLE_SWITCH) ||
		     !device_property_read_bool(dwc->dev, "usb-role-switch")) &&
		    !DWC3_VER_IS_PRIOR(DWC3, 330A))
			mode = USB_DR_MODE_PERIPHERAL;
	}

	if (mode != dwc->dr_mode) {
		dev_warn(dev,
			 "Configuration mismatch. dr_mode forced to %s\n",
			 mode == USB_DR_MODE_HOST ? "host" : "gadget");

		dwc->dr_mode = mode;
	}

	return 0;
}

void dwc3_set_prtcap(struct dwc3 *dwc, u32 mode)
{
	u32 reg;

	reg = dwc3_readl(dwc->regs, DWC3_GCTL);
	reg &= ~(DWC3_GCTL_PRTCAPDIR(DWC3_GCTL_PRTCAP_OTG));
	reg |= DWC3_GCTL_PRTCAPDIR(mode);
	dwc3_writel(dwc->regs, DWC3_GCTL, reg);

	dwc->current_dr_role = mode;
}

static void __dwc3_set_mode(struct work_struct *work)
{
	struct dwc3 *dwc = work_to_dwc(work);
	unsigned long flags;
	int ret;
	u32 reg;
	u32 desired_dr_role;

	mutex_lock(&dwc->mutex);
	spin_lock_irqsave(&dwc->lock, flags);
	desired_dr_role = dwc->desired_dr_role;
	spin_unlock_irqrestore(&dwc->lock, flags);

	pm_runtime_get_sync(dwc->dev);

	if (dwc->current_dr_role == DWC3_GCTL_PRTCAP_OTG)
		dwc3_otg_update(dwc, 0);

	if (!desired_dr_role)
		goto out;

	if (desired_dr_role == dwc->current_dr_role)
		goto out;

	if (desired_dr_role == DWC3_GCTL_PRTCAP_OTG && dwc->edev)
		goto out;

	switch (dwc->current_dr_role) {
	case DWC3_GCTL_PRTCAP_HOST:
		dwc3_host_exit(dwc);
		break;
	case DWC3_GCTL_PRTCAP_DEVICE:
		dwc3_gadget_exit(dwc);
		dwc3_event_buffers_cleanup(dwc);
		break;
	case DWC3_GCTL_PRTCAP_OTG:
		dwc3_otg_exit(dwc);
		spin_lock_irqsave(&dwc->lock, flags);
		dwc->desired_otg_role = DWC3_OTG_ROLE_IDLE;
		spin_unlock_irqrestore(&dwc->lock, flags);
		dwc3_otg_update(dwc, 1);
		break;
	default:
		break;
	}

	/*
	 * When current_dr_role is not set, there's no role switching.
	 * Only perform GCTL.CoreSoftReset when there's DRD role switching.
	 */
	if (dwc->current_dr_role && ((DWC3_IP_IS(DWC3) ||
			DWC3_VER_IS_PRIOR(DWC31, 190A)) &&
			desired_dr_role != DWC3_GCTL_PRTCAP_OTG)) {
		reg = dwc3_readl(dwc->regs, DWC3_GCTL);
		reg |= DWC3_GCTL_CORESOFTRESET;
		dwc3_writel(dwc->regs, DWC3_GCTL, reg);

		/*
		 * Wait for internal clocks to synchronized. DWC_usb31 and
		 * DWC_usb32 may need at least 50ms (less for DWC_usb3). To
		 * keep it consistent across different IPs, let's wait up to
		 * 100ms before clearing GCTL.CORESOFTRESET.
		 */
		msleep(100);

		reg = dwc3_readl(dwc->regs, DWC3_GCTL);
		reg &= ~DWC3_GCTL_CORESOFTRESET;
		dwc3_writel(dwc->regs, DWC3_GCTL, reg);
	}

	spin_lock_irqsave(&dwc->lock, flags);

	dwc3_set_prtcap(dwc, desired_dr_role);

	spin_unlock_irqrestore(&dwc->lock, flags);

	switch (desired_dr_role) {
	case DWC3_GCTL_PRTCAP_HOST:
		ret = dwc3_host_init(dwc);
		if (ret) {
			dev_err(dwc->dev, "failed to initialize host\n");
		} else {
			if (dwc->usb2_phy)
				otg_set_vbus(dwc->usb2_phy->otg, true);
			phy_set_mode(dwc->usb2_generic_phy, PHY_MODE_USB_HOST);
			phy_set_mode(dwc->usb3_generic_phy, PHY_MODE_USB_HOST);
			if (dwc->dis_split_quirk) {
				reg = dwc3_readl(dwc->regs, DWC3_GUCTL3);
				reg |= DWC3_GUCTL3_SPLITDISABLE;
				dwc3_writel(dwc->regs, DWC3_GUCTL3, reg);
			}
		}
		break;
	case DWC3_GCTL_PRTCAP_DEVICE:
		dwc3_core_soft_reset(dwc);

		dwc3_event_buffers_setup(dwc);

		if (dwc->usb2_phy)
			otg_set_vbus(dwc->usb2_phy->otg, false);
		phy_set_mode(dwc->usb2_generic_phy, PHY_MODE_USB_DEVICE);
		phy_set_mode(dwc->usb3_generic_phy, PHY_MODE_USB_DEVICE);

		ret = dwc3_gadget_init(dwc);
		if (ret)
			dev_err(dwc->dev, "failed to initialize peripheral\n");
		break;
	case DWC3_GCTL_PRTCAP_OTG:
		dwc3_otg_init(dwc);
		dwc3_otg_update(dwc, 0);
		break;
	default:
		break;
	}

out:
	pm_runtime_mark_last_busy(dwc->dev);
	pm_runtime_put_autosuspend(dwc->dev);
	mutex_unlock(&dwc->mutex);
}

void dwc3_set_mode(struct dwc3 *dwc, u32 mode)
{
	unsigned long flags;

	if (dwc->dr_mode != USB_DR_MODE_OTG)
		return;

	spin_lock_irqsave(&dwc->lock, flags);
	dwc->desired_dr_role = mode;
	spin_unlock_irqrestore(&dwc->lock, flags);

	queue_work(system_freezable_wq, &dwc->drd_work);
}

u32 dwc3_core_fifo_space(struct dwc3_ep *dep, u8 type)
{
	struct dwc3		*dwc = dep->dwc;
	u32			reg;

	dwc3_writel(dwc->regs, DWC3_GDBGFIFOSPACE,
			DWC3_GDBGFIFOSPACE_NUM(dep->number) |
			DWC3_GDBGFIFOSPACE_TYPE(type));

	reg = dwc3_readl(dwc->regs, DWC3_GDBGFIFOSPACE);

	return DWC3_GDBGFIFOSPACE_SPACE_AVAILABLE(reg);
}

/**
 * dwc3_core_soft_reset - Issues core soft reset and PHY reset
 * @dwc: pointer to our context structure
 */
int dwc3_core_soft_reset(struct dwc3 *dwc)
{
	u32		reg;
	int		retries = 1000;

	/*
	 * We're resetting only the device side because, if we're in host mode,
	 * XHCI driver will reset the host block. If dwc3 was configured for
	 * host-only mode, then we can return early.
	 */
	if (dwc->current_dr_role == DWC3_GCTL_PRTCAP_HOST)
		return 0;

	reg = dwc3_readl(dwc->regs, DWC3_DCTL);
	reg |= DWC3_DCTL_CSFTRST;
	reg &= ~DWC3_DCTL_RUN_STOP;
	dwc3_gadget_dctl_write_safe(dwc, reg);

	/*
	 * For DWC_usb31 controller 1.90a and later, the DCTL.CSFRST bit
	 * is cleared only after all the clocks are synchronized. This can
	 * take a little more than 50ms. Set the polling rate at 20ms
	 * for 10 times instead.
	 */
	if (DWC3_VER_IS_WITHIN(DWC31, 190A, ANY) || DWC3_IP_IS(DWC32))
		retries = 10;

	do {
		reg = dwc3_readl(dwc->regs, DWC3_DCTL);
		if (!(reg & DWC3_DCTL_CSFTRST))
			goto done;

		if (DWC3_VER_IS_WITHIN(DWC31, 190A, ANY) || DWC3_IP_IS(DWC32))
			msleep(20);
		else
			udelay(1);
	} while (--retries);

	dev_warn(dwc->dev, "DWC3 controller soft reset failed.\n");
	return -ETIMEDOUT;

done:
	/*
	 * For DWC_usb31 controller 1.80a and prior, once DCTL.CSFRST bit
	 * is cleared, we must wait at least 50ms before accessing the PHY
	 * domain (synchronization delay).
	 */
	if (DWC3_VER_IS_WITHIN(DWC31, ANY, 180A))
		msleep(50);

	return 0;
}

/*
 * dwc3_frame_length_adjustment - Adjusts frame length if required
 * @dwc3: Pointer to our controller context structure
 */
static void dwc3_frame_length_adjustment(struct dwc3 *dwc)
{
	u32 reg;
	u32 dft;

	if (DWC3_VER_IS_PRIOR(DWC3, 250A))
		return;

	if (dwc->fladj == 0)
		return;

	reg = dwc3_readl(dwc->regs, DWC3_GFLADJ);
	dft = reg & DWC3_GFLADJ_30MHZ_MASK;
	if (dft != dwc->fladj) {
		reg &= ~DWC3_GFLADJ_30MHZ_MASK;
		reg |= DWC3_GFLADJ_30MHZ_SDBND_SEL | dwc->fladj;
		dwc3_writel(dwc->regs, DWC3_GFLADJ, reg);
	}
}

/**
 * dwc3_ref_clk_period - Reference clock period configuration
 *		Default reference clock period depends on hardware
 *		configuration. For systems with reference clock that differs
 *		from the default, this will set clock period in DWC3_GUCTL
 *		register.
 * @dwc: Pointer to our controller context structure
 */
static void dwc3_ref_clk_period(struct dwc3 *dwc)
{
	unsigned long period;
	unsigned long fladj;
	unsigned long decr;
	unsigned long rate;
	u32 reg;

	if (dwc->ref_clk) {
		rate = clk_get_rate(dwc->ref_clk);
		if (!rate)
			return;
		period = NSEC_PER_SEC / rate;
	} else if (dwc->ref_clk_per) {
		period = dwc->ref_clk_per;
		rate = NSEC_PER_SEC / period;
	} else {
		return;
	}

	reg = dwc3_readl(dwc->regs, DWC3_GUCTL);
	reg &= ~DWC3_GUCTL_REFCLKPER_MASK;
	reg |=  FIELD_PREP(DWC3_GUCTL_REFCLKPER_MASK, period);
	dwc3_writel(dwc->regs, DWC3_GUCTL, reg);

	if (DWC3_VER_IS_PRIOR(DWC3, 250A))
		return;

	/*
	 * The calculation below is
	 *
	 * 125000 * (NSEC_PER_SEC / (rate * period) - 1)
	 *
	 * but rearranged for fixed-point arithmetic. The division must be
	 * 64-bit because 125000 * NSEC_PER_SEC doesn't fit in 32 bits (and
	 * neither does rate * period).
	 *
	 * Note that rate * period ~= NSEC_PER_SECOND, minus the number of
	 * nanoseconds of error caused by the truncation which happened during
	 * the division when calculating rate or period (whichever one was
	 * derived from the other). We first calculate the relative error, then
	 * scale it to units of 8 ppm.
	 */
	fladj = div64_u64(125000ULL * NSEC_PER_SEC, (u64)rate * period);
	fladj -= 125000;

	/*
	 * The documented 240MHz constant is scaled by 2 to get PLS1 as well.
	 */
	decr = 480000000 / rate;

	reg = dwc3_readl(dwc->regs, DWC3_GFLADJ);
	reg &= ~DWC3_GFLADJ_REFCLK_FLADJ_MASK
	    &  ~DWC3_GFLADJ_240MHZDECR
	    &  ~DWC3_GFLADJ_240MHZDECR_PLS1;
	reg |= FIELD_PREP(DWC3_GFLADJ_REFCLK_FLADJ_MASK, fladj)
	    |  FIELD_PREP(DWC3_GFLADJ_240MHZDECR, decr >> 1)
	    |  FIELD_PREP(DWC3_GFLADJ_240MHZDECR_PLS1, decr & 1);

	if (dwc->gfladj_refclk_lpm_sel)
		reg |=  DWC3_GFLADJ_REFCLK_LPM_SEL;

	dwc3_writel(dwc->regs, DWC3_GFLADJ, reg);
}

/**
 * dwc3_free_one_event_buffer - Frees one event buffer
 * @dwc: Pointer to our controller context structure
 * @evt: Pointer to event buffer to be freed
 */
static void dwc3_free_one_event_buffer(struct dwc3 *dwc,
		struct dwc3_event_buffer *evt)
{
	dma_free_coherent(dwc->sysdev, evt->length, evt->buf, evt->dma);
}

/**
 * dwc3_alloc_one_event_buffer - Allocates one event buffer structure
 * @dwc: Pointer to our controller context structure
 * @length: size of the event buffer
 *
 * Returns a pointer to the allocated event buffer structure on success
 * otherwise ERR_PTR(errno).
 */
static struct dwc3_event_buffer *dwc3_alloc_one_event_buffer(struct dwc3 *dwc,
		unsigned int length)
{
	struct dwc3_event_buffer	*evt;

	evt = devm_kzalloc(dwc->dev, sizeof(*evt), GFP_KERNEL);
	if (!evt)
		return ERR_PTR(-ENOMEM);

	evt->dwc	= dwc;
	evt->length	= length;
	evt->cache	= devm_kzalloc(dwc->dev, length, GFP_KERNEL);
	if (!evt->cache)
		return ERR_PTR(-ENOMEM);

	evt->buf	= dma_alloc_coherent(dwc->sysdev, length,
			&evt->dma, GFP_KERNEL);
	if (!evt->buf)
		return ERR_PTR(-ENOMEM);

	return evt;
}

/**
 * dwc3_free_event_buffers - frees all allocated event buffers
 * @dwc: Pointer to our controller context structure
 */
static void dwc3_free_event_buffers(struct dwc3 *dwc)
{
	struct dwc3_event_buffer	*evt;

	evt = dwc->ev_buf;
	if (evt)
		dwc3_free_one_event_buffer(dwc, evt);
}

/**
 * dwc3_alloc_event_buffers - Allocates @num event buffers of size @length
 * @dwc: pointer to our controller context structure
 * @length: size of event buffer
 *
 * Returns 0 on success otherwise negative errno. In the error case, dwc
 * may contain some buffers allocated but not all which were requested.
 */
static int dwc3_alloc_event_buffers(struct dwc3 *dwc, unsigned int length)
{
	struct dwc3_event_buffer *evt;

	evt = dwc3_alloc_one_event_buffer(dwc, length);
	if (IS_ERR(evt)) {
		dev_err(dwc->dev, "can't allocate event buffer\n");
		return PTR_ERR(evt);
	}
	dwc->ev_buf = evt;

	return 0;
}

/**
 * dwc3_event_buffers_setup - setup our allocated event buffers
 * @dwc: pointer to our controller context structure
 *
 * Returns 0 on success otherwise negative errno.
 */
int dwc3_event_buffers_setup(struct dwc3 *dwc)
{
	struct dwc3_event_buffer	*evt;

	evt = dwc->ev_buf;
	evt->lpos = 0;
	dwc3_writel(dwc->regs, DWC3_GEVNTADRLO(0),
			lower_32_bits(evt->dma));
	dwc3_writel(dwc->regs, DWC3_GEVNTADRHI(0),
			upper_32_bits(evt->dma));
	dwc3_writel(dwc->regs, DWC3_GEVNTSIZ(0),
			DWC3_GEVNTSIZ_SIZE(evt->length));
	dwc3_writel(dwc->regs, DWC3_GEVNTCOUNT(0), 0);

	return 0;
}

void dwc3_event_buffers_cleanup(struct dwc3 *dwc)
{
	struct dwc3_event_buffer	*evt;

	evt = dwc->ev_buf;

	evt->lpos = 0;

	dwc3_writel(dwc->regs, DWC3_GEVNTADRLO(0), 0);
	dwc3_writel(dwc->regs, DWC3_GEVNTADRHI(0), 0);
	dwc3_writel(dwc->regs, DWC3_GEVNTSIZ(0), DWC3_GEVNTSIZ_INTMASK
			| DWC3_GEVNTSIZ_SIZE(0));
	dwc3_writel(dwc->regs, DWC3_GEVNTCOUNT(0), 0);
}

static void dwc3_core_num_eps(struct dwc3 *dwc)
{
	struct dwc3_hwparams	*parms = &dwc->hwparams;

	dwc->num_eps = DWC3_NUM_EPS(parms);
}

static void dwc3_cache_hwparams(struct dwc3 *dwc)
{
	struct dwc3_hwparams	*parms = &dwc->hwparams;

	parms->hwparams0 = dwc3_readl(dwc->regs, DWC3_GHWPARAMS0);
	parms->hwparams1 = dwc3_readl(dwc->regs, DWC3_GHWPARAMS1);
	parms->hwparams2 = dwc3_readl(dwc->regs, DWC3_GHWPARAMS2);
	parms->hwparams3 = dwc3_readl(dwc->regs, DWC3_GHWPARAMS3);
	parms->hwparams4 = dwc3_readl(dwc->regs, DWC3_GHWPARAMS4);
	parms->hwparams5 = dwc3_readl(dwc->regs, DWC3_GHWPARAMS5);
	parms->hwparams6 = dwc3_readl(dwc->regs, DWC3_GHWPARAMS6);
	parms->hwparams7 = dwc3_readl(dwc->regs, DWC3_GHWPARAMS7);
	parms->hwparams8 = dwc3_readl(dwc->regs, DWC3_GHWPARAMS8);

	if (DWC3_IP_IS(DWC32))
		parms->hwparams9 = dwc3_readl(dwc->regs, DWC3_GHWPARAMS9);
}

static int dwc3_core_ulpi_init(struct dwc3 *dwc)
{
	int intf;
	int ret = 0;

	intf = DWC3_GHWPARAMS3_HSPHY_IFC(dwc->hwparams.hwparams3);

	if (intf == DWC3_GHWPARAMS3_HSPHY_IFC_ULPI ||
	    (intf == DWC3_GHWPARAMS3_HSPHY_IFC_UTMI_ULPI &&
	     dwc->hsphy_interface &&
	     !strncmp(dwc->hsphy_interface, "ulpi", 4)))
		ret = dwc3_ulpi_init(dwc);

	return ret;
}

/**
 * dwc3_phy_setup - Configure USB PHY Interface of DWC3 Core
 * @dwc: Pointer to our controller context structure
 *
 * Returns 0 on success. The USB PHY interfaces are configured but not
 * initialized. The PHY interfaces and the PHYs get initialized together with
 * the core in dwc3_core_init.
 */
static int dwc3_phy_setup(struct dwc3 *dwc)
{
	unsigned int hw_mode;
	u32 reg;

	hw_mode = DWC3_GHWPARAMS0_MODE(dwc->hwparams.hwparams0);

	reg = dwc3_readl(dwc->regs, DWC3_GUSB3PIPECTL(0));

	/*
	 * Make sure UX_EXIT_PX is cleared as that causes issues with some
	 * PHYs. Also, this bit is not supposed to be used in normal operation.
	 */
	reg &= ~DWC3_GUSB3PIPECTL_UX_EXIT_PX;

	/*
	 * Above 1.94a, it is recommended to set DWC3_GUSB3PIPECTL_SUSPHY
	 * to '0' during coreConsultant configuration. So default value
	 * will be '0' when the core is reset. Application needs to set it
	 * to '1' after the core initialization is completed.
	 */
	if (!DWC3_VER_IS_WITHIN(DWC3, ANY, 194A))
		reg |= DWC3_GUSB3PIPECTL_SUSPHY;

	/*
	 * For DRD controllers, GUSB3PIPECTL.SUSPENDENABLE must be cleared after
	 * power-on reset, and it can be set after core initialization, which is
	 * after device soft-reset during initialization.
	 */
	if (hw_mode == DWC3_GHWPARAMS0_MODE_DRD)
		reg &= ~DWC3_GUSB3PIPECTL_SUSPHY;

	if (dwc->u2ss_inp3_quirk)
		reg |= DWC3_GUSB3PIPECTL_U2SSINP3OK;

	if (dwc->dis_rxdet_inp3_quirk)
		reg |= DWC3_GUSB3PIPECTL_DISRXDETINP3;

	if (dwc->req_p1p2p3_quirk)
		reg |= DWC3_GUSB3PIPECTL_REQP1P2P3;

	if (dwc->del_p1p2p3_quirk)
		reg |= DWC3_GUSB3PIPECTL_DEP1P2P3_EN;

	if (dwc->del_phy_power_chg_quirk)
		reg |= DWC3_GUSB3PIPECTL_DEPOCHANGE;

	if (dwc->lfps_filter_quirk)
		reg |= DWC3_GUSB3PIPECTL_LFPSFILT;

	if (dwc->rx_detect_poll_quirk)
		reg |= DWC3_GUSB3PIPECTL_RX_DETOPOLL;

	if (dwc->tx_de_emphasis_quirk)
		reg |= DWC3_GUSB3PIPECTL_TX_DEEPH(dwc->tx_de_emphasis);

	if (dwc->dis_u3_susphy_quirk)
		reg &= ~DWC3_GUSB3PIPECTL_SUSPHY;

	if (dwc->dis_del_phy_power_chg_quirk)
		reg &= ~DWC3_GUSB3PIPECTL_DEPOCHANGE;

	dwc3_writel(dwc->regs, DWC3_GUSB3PIPECTL(0), reg);

	reg = dwc3_readl(dwc->regs, DWC3_GUSB2PHYCFG(0));

	/* Select the HS PHY interface */
	switch (DWC3_GHWPARAMS3_HSPHY_IFC(dwc->hwparams.hwparams3)) {
	case DWC3_GHWPARAMS3_HSPHY_IFC_UTMI_ULPI:
		if (dwc->hsphy_interface &&
				!strncmp(dwc->hsphy_interface, "utmi", 4)) {
			reg &= ~DWC3_GUSB2PHYCFG_ULPI_UTMI;
			break;
		} else if (dwc->hsphy_interface &&
				!strncmp(dwc->hsphy_interface, "ulpi", 4)) {
			reg |= DWC3_GUSB2PHYCFG_ULPI_UTMI;
			dwc3_writel(dwc->regs, DWC3_GUSB2PHYCFG(0), reg);
		} else {
			/* Relying on default value. */
			if (!(reg & DWC3_GUSB2PHYCFG_ULPI_UTMI))
				break;
		}
		fallthrough;
	case DWC3_GHWPARAMS3_HSPHY_IFC_ULPI:
	default:
		break;
	}

	switch (dwc->hsphy_mode) {
	case USBPHY_INTERFACE_MODE_UTMI:
		reg &= ~(DWC3_GUSB2PHYCFG_PHYIF_MASK |
		       DWC3_GUSB2PHYCFG_USBTRDTIM_MASK);
		reg |= DWC3_GUSB2PHYCFG_PHYIF(UTMI_PHYIF_8_BIT) |
		       DWC3_GUSB2PHYCFG_USBTRDTIM(USBTRDTIM_UTMI_8_BIT);
		break;
	case USBPHY_INTERFACE_MODE_UTMIW:
		reg &= ~(DWC3_GUSB2PHYCFG_PHYIF_MASK |
		       DWC3_GUSB2PHYCFG_USBTRDTIM_MASK);
		reg |= DWC3_GUSB2PHYCFG_PHYIF(UTMI_PHYIF_16_BIT) |
		       DWC3_GUSB2PHYCFG_USBTRDTIM(USBTRDTIM_UTMI_16_BIT);
		break;
	default:
		break;
	}

	/*
	 * Above 1.94a, it is recommended to set DWC3_GUSB2PHYCFG_SUSPHY to
	 * '0' during coreConsultant configuration. So default value will
	 * be '0' when the core is reset. Application needs to set it to
	 * '1' after the core initialization is completed.
	 */
	if (!DWC3_VER_IS_WITHIN(DWC3, ANY, 194A))
		reg |= DWC3_GUSB2PHYCFG_SUSPHY;

	/*
	 * For DRD controllers, GUSB2PHYCFG.SUSPHY must be cleared after
	 * power-on reset, and it can be set after core initialization, which is
	 * after device soft-reset during initialization.
	 */
	if (hw_mode == DWC3_GHWPARAMS0_MODE_DRD)
		reg &= ~DWC3_GUSB2PHYCFG_SUSPHY;

	if (dwc->dis_u2_susphy_quirk)
		reg &= ~DWC3_GUSB2PHYCFG_SUSPHY;

	if (dwc->dis_enblslpm_quirk)
		reg &= ~DWC3_GUSB2PHYCFG_ENBLSLPM;
	else
		reg |= DWC3_GUSB2PHYCFG_ENBLSLPM;

	if (dwc->dis_u2_freeclk_exists_quirk || dwc->gfladj_refclk_lpm_sel)
		reg &= ~DWC3_GUSB2PHYCFG_U2_FREECLK_EXISTS;

	/*
	 * Some ULPI USB PHY does not support internal VBUS supply, to drive
	 * the CPEN pin requires the configuration of the ULPI DRVVBUSEXTERNAL
	 * bit of OTG_CTRL register. Controller configures the USB2 PHY
	 * ULPIEXTVBUSDRV bit[17] of the GUSB2PHYCFG register to drive vBus
	 * with an external supply.
	 */
	if (dwc->ulpi_ext_vbus_drv)
		reg |= DWC3_GUSB2PHYCFG_ULPIEXTVBUSDRV;

	dwc3_writel(dwc->regs, DWC3_GUSB2PHYCFG(0), reg);

	return 0;
}

static int dwc3_phy_init(struct dwc3 *dwc)
{
	int ret;

	usb_phy_init(dwc->usb2_phy);
	usb_phy_init(dwc->usb3_phy);

	ret = phy_init(dwc->usb2_generic_phy);
	if (ret < 0)
		goto err_shutdown_usb3_phy;

	ret = phy_init(dwc->usb3_generic_phy);
	if (ret < 0)
		goto err_exit_usb2_phy;

	return 0;

err_exit_usb2_phy:
	phy_exit(dwc->usb2_generic_phy);
err_shutdown_usb3_phy:
	usb_phy_shutdown(dwc->usb3_phy);
	usb_phy_shutdown(dwc->usb2_phy);

	return ret;
}

static void dwc3_phy_exit(struct dwc3 *dwc)
{
	phy_exit(dwc->usb3_generic_phy);
	phy_exit(dwc->usb2_generic_phy);

	usb_phy_shutdown(dwc->usb3_phy);
	usb_phy_shutdown(dwc->usb2_phy);
}

static int dwc3_phy_power_on(struct dwc3 *dwc)
{
	int ret;

	usb_phy_set_suspend(dwc->usb2_phy, 0);
	usb_phy_set_suspend(dwc->usb3_phy, 0);

	ret = phy_power_on(dwc->usb2_generic_phy);
	if (ret < 0)
		goto err_suspend_usb3_phy;

	ret = phy_power_on(dwc->usb3_generic_phy);
	if (ret < 0)
		goto err_power_off_usb2_phy;

	return 0;

err_power_off_usb2_phy:
	phy_power_off(dwc->usb2_generic_phy);
err_suspend_usb3_phy:
	usb_phy_set_suspend(dwc->usb3_phy, 1);
	usb_phy_set_suspend(dwc->usb2_phy, 1);

	return ret;
}

static void dwc3_phy_power_off(struct dwc3 *dwc)
{
	phy_power_off(dwc->usb3_generic_phy);
	phy_power_off(dwc->usb2_generic_phy);

	usb_phy_set_suspend(dwc->usb3_phy, 1);
	usb_phy_set_suspend(dwc->usb2_phy, 1);
}

static int dwc3_clk_enable(struct dwc3 *dwc)
{
	int ret;

	ret = clk_prepare_enable(dwc->bus_clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(dwc->ref_clk);
	if (ret)
		goto disable_bus_clk;

	ret = clk_prepare_enable(dwc->susp_clk);
	if (ret)
		goto disable_ref_clk;

	return 0;

disable_ref_clk:
	clk_disable_unprepare(dwc->ref_clk);
disable_bus_clk:
	clk_disable_unprepare(dwc->bus_clk);
	return ret;
}

static void dwc3_clk_disable(struct dwc3 *dwc)
{
	clk_disable_unprepare(dwc->susp_clk);
	clk_disable_unprepare(dwc->ref_clk);
	clk_disable_unprepare(dwc->bus_clk);
}

static void dwc3_core_exit(struct dwc3 *dwc)
{
	dwc3_event_buffers_cleanup(dwc);
	dwc3_phy_power_off(dwc);
	dwc3_phy_exit(dwc);
	dwc3_clk_disable(dwc);
	reset_control_assert(dwc->reset);
}

static bool dwc3_core_is_valid(struct dwc3 *dwc)
{
	u32 reg;

	reg = dwc3_readl(dwc->regs, DWC3_GSNPSID);
	dwc->ip = DWC3_GSNPS_ID(reg);

	/* This should read as U3 followed by revision number */
	if (DWC3_IP_IS(DWC3)) {
		dwc->revision = reg;
	} else if (DWC3_IP_IS(DWC31) || DWC3_IP_IS(DWC32)) {
		dwc->revision = dwc3_readl(dwc->regs, DWC3_VER_NUMBER);
		dwc->version_type = dwc3_readl(dwc->regs, DWC3_VER_TYPE);
	} else {
		return false;
	}

	return true;
}

static void dwc3_core_setup_global_control(struct dwc3 *dwc)
{
	u32 reg;

	reg = dwc3_readl(dwc->regs, DWC3_GCTL);
	reg &= ~DWC3_GCTL_SCALEDOWN_MASK;

	switch (DWC3_GHWPARAMS1_EN_PWROPT(dwc->hwparams.hwparams1)) {
	case DWC3_GHWPARAMS1_EN_PWROPT_CLK:
		/**
		 * WORKAROUND: DWC3 revisions between 2.10a and 2.50a have an
		 * issue which would cause xHCI compliance tests to fail.
		 *
		 * Because of that we cannot enable clock gating on such
		 * configurations.
		 *
		 * Refers to:
		 *
		 * STAR#9000588375: Clock Gating, SOF Issues when ref_clk-Based
		 * SOF/ITP Mode Used
		 */
		if ((dwc->dr_mode == USB_DR_MODE_HOST ||
				dwc->dr_mode == USB_DR_MODE_OTG) &&
				DWC3_VER_IS_WITHIN(DWC3, 210A, 250A))
			reg |= DWC3_GCTL_DSBLCLKGTNG | DWC3_GCTL_SOFITPSYNC;
		else
			reg &= ~DWC3_GCTL_DSBLCLKGTNG;
		break;
	case DWC3_GHWPARAMS1_EN_PWROPT_HIB:
		/*
		 * REVISIT Enabling this bit so that host-mode hibernation
		 * will work. Device-mode hibernation is not yet implemented.
		 */
		reg |= DWC3_GCTL_GBLHIBERNATIONEN;
		break;
	default:
		/* nothing */
		break;
	}

	/* check if current dwc3 is on simulation board */
	if (dwc->hwparams.hwparams6 & DWC3_GHWPARAMS6_EN_FPGA) {
		dev_info(dwc->dev, "Running with FPGA optimizations\n");
		dwc->is_fpga = true;
	}

	WARN_ONCE(dwc->disable_scramble_quirk && !dwc->is_fpga,
			"disable_scramble cannot be used on non-FPGA builds\n");

	if (dwc->disable_scramble_quirk && dwc->is_fpga)
		reg |= DWC3_GCTL_DISSCRAMBLE;
	else
		reg &= ~DWC3_GCTL_DISSCRAMBLE;

	if (dwc->u2exit_lfps_quirk)
		reg |= DWC3_GCTL_U2EXIT_LFPS;

	/*
	 * WORKAROUND: DWC3 revisions <1.90a have a bug
	 * where the device can fail to connect at SuperSpeed
	 * and falls back to high-speed mode which causes
	 * the device to enter a Connect/Disconnect loop
	 */
	if (DWC3_VER_IS_PRIOR(DWC3, 190A))
		reg |= DWC3_GCTL_U2RSTECN;

	dwc3_writel(dwc->regs, DWC3_GCTL, reg);
}

static int dwc3_core_get_phy(struct dwc3 *dwc);
static int dwc3_core_ulpi_init(struct dwc3 *dwc);

/* set global incr burst type configuration registers */
static void dwc3_set_incr_burst_type(struct dwc3 *dwc)
{
	struct device *dev = dwc->dev;
	/* incrx_mode : for INCR burst type. */
	bool incrx_mode;
	/* incrx_size : for size of INCRX burst. */
	u32 incrx_size;
	u32 *vals;
	u32 cfg;
	int ntype;
	int ret;
	int i;

	cfg = dwc3_readl(dwc->regs, DWC3_GSBUSCFG0);

	/*
	 * Handle property "snps,incr-burst-type-adjustment".
	 * Get the number of value from this property:
	 * result <= 0, means this property is not supported.
	 * result = 1, means INCRx burst mode supported.
	 * result > 1, means undefined length burst mode supported.
	 */
	ntype = device_property_count_u32(dev, "snps,incr-burst-type-adjustment");
	if (ntype <= 0)
		return;

	vals = kcalloc(ntype, sizeof(u32), GFP_KERNEL);
	if (!vals)
		return;

	/* Get INCR burst type, and parse it */
	ret = device_property_read_u32_array(dev,
			"snps,incr-burst-type-adjustment", vals, ntype);
	if (ret) {
		kfree(vals);
		dev_err(dev, "Error to get property\n");
		return;
	}

	incrx_size = *vals;

	if (ntype > 1) {
		/* INCRX (undefined length) burst mode */
		incrx_mode = INCRX_UNDEF_LENGTH_BURST_MODE;
		for (i = 1; i < ntype; i++) {
			if (vals[i] > incrx_size)
				incrx_size = vals[i];
		}
	} else {
		/* INCRX burst mode */
		incrx_mode = INCRX_BURST_MODE;
	}

	kfree(vals);

	/* Enable Undefined Length INCR Burst and Enable INCRx Burst */
	cfg &= ~DWC3_GSBUSCFG0_INCRBRST_MASK;
	if (incrx_mode)
		cfg |= DWC3_GSBUSCFG0_INCRBRSTENA;
	switch (incrx_size) {
	case 256:
		cfg |= DWC3_GSBUSCFG0_INCR256BRSTENA;
		break;
	case 128:
		cfg |= DWC3_GSBUSCFG0_INCR128BRSTENA;
		break;
	case 64:
		cfg |= DWC3_GSBUSCFG0_INCR64BRSTENA;
		break;
	case 32:
		cfg |= DWC3_GSBUSCFG0_INCR32BRSTENA;
		break;
	case 16:
		cfg |= DWC3_GSBUSCFG0_INCR16BRSTENA;
		break;
	case 8:
		cfg |= DWC3_GSBUSCFG0_INCR8BRSTENA;
		break;
	case 4:
		cfg |= DWC3_GSBUSCFG0_INCR4BRSTENA;
		break;
	case 1:
		break;
	default:
		dev_err(dev, "Invalid property\n");
		break;
	}

	dwc3_writel(dwc->regs, DWC3_GSBUSCFG0, cfg);
}

static void dwc3_set_power_down_clk_scale(struct dwc3 *dwc)
{
	u32 scale;
	u32 reg;

	if (!dwc->susp_clk)
		return;

	/*
	 * The power down scale field specifies how many suspend_clk
	 * periods fit into a 16KHz clock period. When performing
	 * the division, round up the remainder.
	 *
	 * The power down scale value is calculated using the fastest
	 * frequency of the suspend_clk. If it isn't fixed (but within
	 * the accuracy requirement), the driver may not know the max
	 * rate of the suspend_clk, so only update the power down scale
	 * if the default is less than the calculated value from
	 * clk_get_rate() or if the default is questionably high
	 * (3x or more) to be within the requirement.
	 */
	scale = DIV_ROUND_UP(clk_get_rate(dwc->susp_clk), 16000);
	reg = dwc3_readl(dwc->regs, DWC3_GCTL);
	if ((reg & DWC3_GCTL_PWRDNSCALE_MASK) < DWC3_GCTL_PWRDNSCALE(scale) ||
	    (reg & DWC3_GCTL_PWRDNSCALE_MASK) > DWC3_GCTL_PWRDNSCALE(scale*3)) {
		reg &= ~(DWC3_GCTL_PWRDNSCALE_MASK);
		reg |= DWC3_GCTL_PWRDNSCALE(scale);
		dwc3_writel(dwc->regs, DWC3_GCTL, reg);
	}
}

/**
 * dwc3_core_init - Low-level initialization of DWC3 Core
 * @dwc: Pointer to our controller context structure
 *
 * Returns 0 on success otherwise negative errno.
 */
static int dwc3_core_init(struct dwc3 *dwc)
{
	unsigned int		hw_mode;
	u32			reg;
	int			ret;

	hw_mode = DWC3_GHWPARAMS0_MODE(dwc->hwparams.hwparams0);

	/*
	 * Write Linux Version Code to our GUID register so it's easy to figure
	 * out which kernel version a bug was found.
	 */
	dwc3_writel(dwc->regs, DWC3_GUID, LINUX_VERSION_CODE);

	ret = dwc3_phy_setup(dwc);
	if (ret)
		return ret;

	if (!dwc->ulpi_ready) {
		ret = dwc3_core_ulpi_init(dwc);
		if (ret) {
			if (ret == -ETIMEDOUT) {
				dwc3_core_soft_reset(dwc);
				ret = -EPROBE_DEFER;
			}
			return ret;
		}
		dwc->ulpi_ready = true;
	}

	if (!dwc->phys_ready) {
		ret = dwc3_core_get_phy(dwc);
		if (ret)
			goto err_exit_ulpi;
		dwc->phys_ready = true;
	}

	ret = dwc3_phy_init(dwc);
	if (ret)
		goto err_exit_ulpi;

	ret = dwc3_core_soft_reset(dwc);
	if (ret)
		goto err_exit_phy;

	if (hw_mode == DWC3_GHWPARAMS0_MODE_DRD &&
	    !DWC3_VER_IS_WITHIN(DWC3, ANY, 194A)) {
		if (!dwc->dis_u3_susphy_quirk) {
			reg = dwc3_readl(dwc->regs, DWC3_GUSB3PIPECTL(0));
			reg |= DWC3_GUSB3PIPECTL_SUSPHY;
			dwc3_writel(dwc->regs, DWC3_GUSB3PIPECTL(0), reg);
		}

		if (!dwc->dis_u2_susphy_quirk) {
			reg = dwc3_readl(dwc->regs, DWC3_GUSB2PHYCFG(0));
			reg |= DWC3_GUSB2PHYCFG_SUSPHY;
			dwc3_writel(dwc->regs, DWC3_GUSB2PHYCFG(0), reg);
		}
	}

	dwc3_core_setup_global_control(dwc);
	dwc3_core_num_eps(dwc);

	/* Set power down scale of suspend_clk */
	dwc3_set_power_down_clk_scale(dwc);

	/* Adjust Frame Length */
	dwc3_frame_length_adjustment(dwc);

	/* Adjust Reference Clock Period */
	dwc3_ref_clk_period(dwc);

	dwc3_set_incr_burst_type(dwc);

	ret = dwc3_phy_power_on(dwc);
	if (ret)
		goto err_exit_phy;

	ret = dwc3_event_buffers_setup(dwc);
	if (ret) {
		dev_err(dwc->dev, "failed to setup event buffers\n");
		goto err_power_off_phy;
	}

	/*
	 * ENDXFER polling is available on version 3.10a and later of
	 * the DWC_usb3 controller. It is NOT available in the
	 * DWC_usb31 controller.
	 */
	if (DWC3_VER_IS_WITHIN(DWC3, 310A, ANY)) {
		reg = dwc3_readl(dwc->regs, DWC3_GUCTL2);
		reg |= DWC3_GUCTL2_RST_ACTBITLATER;
		dwc3_writel(dwc->regs, DWC3_GUCTL2, reg);
	}

	/*
	 * When configured in HOST mode, after issuing U3/L2 exit controller
	 * fails to send proper CRC checksum in CRC5 feild. Because of this
	 * behaviour Transaction Error is generated, resulting in reset and
	 * re-enumeration of usb device attached. All the termsel, xcvrsel,
	 * opmode becomes 0 during end of resume. Enabling bit 10 of GUCTL1
	 * will correct this problem. This option is to support certain
	 * legacy ULPI PHYs.
	 */
	if (dwc->resume_hs_terminations) {
		reg = dwc3_readl(dwc->regs, DWC3_GUCTL1);
		reg |= DWC3_GUCTL1_RESUME_OPMODE_HS_HOST;
		dwc3_writel(dwc->regs, DWC3_GUCTL1, reg);
	}

	if (!DWC3_VER_IS_PRIOR(DWC3, 250A)) {
		reg = dwc3_readl(dwc->regs, DWC3_GUCTL1);

		/*
		 * Enable hardware control of sending remote wakeup
		 * in HS when the device is in the L1 state.
		 */
		if (!DWC3_VER_IS_PRIOR(DWC3, 290A))
			reg |= DWC3_GUCTL1_DEV_L1_EXIT_BY_HW;

		/*
		 * Decouple USB 2.0 L1 & L2 events which will allow for
		 * gadget driver to only receive U3/L2 suspend & wakeup
		 * events and prevent the more frequent L1 LPM transitions
		 * from interrupting the driver.
		 */
		if (!DWC3_VER_IS_PRIOR(DWC3, 300A))
			reg |= DWC3_GUCTL1_DEV_DECOUPLE_L1L2_EVT;

		if (dwc->dis_tx_ipgap_linecheck_quirk)
			reg |= DWC3_GUCTL1_TX_IPGAP_LINECHECK_DIS;

		if (dwc->parkmode_disable_ss_quirk)
			reg |= DWC3_GUCTL1_PARKMODE_DISABLE_SS;

		if (dwc->parkmode_disable_hs_quirk)
			reg |= DWC3_GUCTL1_PARKMODE_DISABLE_HS;

		if (DWC3_VER_IS_WITHIN(DWC3, 290A, ANY) &&
		    (dwc->maximum_speed == USB_SPEED_HIGH ||
		     dwc->maximum_speed == USB_SPEED_FULL))
			reg |= DWC3_GUCTL1_DEV_FORCE_20_CLK_FOR_30_CLK;

		dwc3_writel(dwc->regs, DWC3_GUCTL1, reg);
	}

	if (dwc->dr_mode == USB_DR_MODE_HOST ||
	    dwc->dr_mode == USB_DR_MODE_OTG) {
		reg = dwc3_readl(dwc->regs, DWC3_GUCTL);

		/*
		 * Enable Auto retry Feature to make the controller operating in
		 * Host mode on seeing transaction errors(CRC errors or internal
		 * overrun scenerios) on IN transfers to reply to the device
		 * with a non-terminating retry ACK (i.e, an ACK transcation
		 * packet with Retry=1 & Nump != 0)
		 */
		reg |= DWC3_GUCTL_HSTINAUTORETRY;

		dwc3_writel(dwc->regs, DWC3_GUCTL, reg);
	}

	/*
	 * Must config both number of packets and max burst settings to enable
	 * RX and/or TX threshold.
	 */
	if (!DWC3_IP_IS(DWC3) && dwc->dr_mode == USB_DR_MODE_HOST) {
		u8 rx_thr_num = dwc->rx_thr_num_pkt_prd;
		u8 rx_maxburst = dwc->rx_max_burst_prd;
		u8 tx_thr_num = dwc->tx_thr_num_pkt_prd;
		u8 tx_maxburst = dwc->tx_max_burst_prd;

		if (rx_thr_num && rx_maxburst) {
			reg = dwc3_readl(dwc->regs, DWC3_GRXTHRCFG);
			reg |= DWC31_RXTHRNUMPKTSEL_PRD;

			reg &= ~DWC31_RXTHRNUMPKT_PRD(~0);
			reg |= DWC31_RXTHRNUMPKT_PRD(rx_thr_num);

			reg &= ~DWC31_MAXRXBURSTSIZE_PRD(~0);
			reg |= DWC31_MAXRXBURSTSIZE_PRD(rx_maxburst);

			dwc3_writel(dwc->regs, DWC3_GRXTHRCFG, reg);
		}

		if (tx_thr_num && tx_maxburst) {
			reg = dwc3_readl(dwc->regs, DWC3_GTXTHRCFG);
			reg |= DWC31_TXTHRNUMPKTSEL_PRD;

			reg &= ~DWC31_TXTHRNUMPKT_PRD(~0);
			reg |= DWC31_TXTHRNUMPKT_PRD(tx_thr_num);

			reg &= ~DWC31_MAXTXBURSTSIZE_PRD(~0);
			reg |= DWC31_MAXTXBURSTSIZE_PRD(tx_maxburst);

			dwc3_writel(dwc->regs, DWC3_GTXTHRCFG, reg);
		}
	}

	return 0;

err_power_off_phy:
	dwc3_phy_power_off(dwc);
err_exit_phy:
	dwc3_phy_exit(dwc);
err_exit_ulpi:
	dwc3_ulpi_exit(dwc);

	return ret;
}

static int dwc3_core_get_phy(struct dwc3 *dwc)
{
	struct device		*dev = dwc->dev;
	struct device_node	*node = dev->of_node;
	int ret;

	if (node) {
		dwc->usb2_phy = devm_usb_get_phy_by_phandle(dev, "usb-phy", 0);
		dwc->usb3_phy = devm_usb_get_phy_by_phandle(dev, "usb-phy", 1);
	} else {
		dwc->usb2_phy = devm_usb_get_phy(dev, USB_PHY_TYPE_USB2);
		dwc->usb3_phy = devm_usb_get_phy(dev, USB_PHY_TYPE_USB3);
	}

	if (IS_ERR(dwc->usb2_phy)) {
		ret = PTR_ERR(dwc->usb2_phy);
		if (ret == -ENXIO || ret == -ENODEV)
			dwc->usb2_phy = NULL;
		else
			return dev_err_probe(dev, ret, "no usb2 phy configured\n");
	}

	if (IS_ERR(dwc->usb3_phy)) {
		ret = PTR_ERR(dwc->usb3_phy);
		if (ret == -ENXIO || ret == -ENODEV)
			dwc->usb3_phy = NULL;
		else
			return dev_err_probe(dev, ret, "no usb3 phy configured\n");
	}

	dwc->usb2_generic_phy = devm_phy_get(dev, "usb2-phy");
	if (IS_ERR(dwc->usb2_generic_phy)) {
		ret = PTR_ERR(dwc->usb2_generic_phy);
		if (ret == -ENOSYS || ret == -ENODEV)
			dwc->usb2_generic_phy = NULL;
		else
			return dev_err_probe(dev, ret, "no usb2 phy configured\n");
	}

	dwc->usb3_generic_phy = devm_phy_get(dev, "usb3-phy");
	if (IS_ERR(dwc->usb3_generic_phy)) {
		ret = PTR_ERR(dwc->usb3_generic_phy);
		if (ret == -ENOSYS || ret == -ENODEV)
			dwc->usb3_generic_phy = NULL;
		else
			return dev_err_probe(dev, ret, "no usb3 phy configured\n");
	}

	return 0;
}

static int dwc3_core_init_mode(struct dwc3 *dwc)
{
	struct device *dev = dwc->dev;
	int ret;

	switch (dwc->dr_mode) {
	case USB_DR_MODE_PERIPHERAL:
		dwc3_set_prtcap(dwc, DWC3_GCTL_PRTCAP_DEVICE);

		if (dwc->usb2_phy)
			otg_set_vbus(dwc->usb2_phy->otg, false);
		phy_set_mode(dwc->usb2_generic_phy, PHY_MODE_USB_DEVICE);
		phy_set_mode(dwc->usb3_generic_phy, PHY_MODE_USB_DEVICE);

		ret = dwc3_gadget_init(dwc);
		if (ret)
			return dev_err_probe(dev, ret, "failed to initialize gadget\n");
		break;
	case USB_DR_MODE_HOST:
		dwc3_set_prtcap(dwc, DWC3_GCTL_PRTCAP_HOST);

		if (dwc->usb2_phy)
			otg_set_vbus(dwc->usb2_phy->otg, true);
		phy_set_mode(dwc->usb2_generic_phy, PHY_MODE_USB_HOST);
		phy_set_mode(dwc->usb3_generic_phy, PHY_MODE_USB_HOST);

		ret = dwc3_host_init(dwc);
		if (ret)
			return dev_err_probe(dev, ret, "failed to initialize host\n");
		break;
	case USB_DR_MODE_OTG:
		INIT_WORK(&dwc->drd_work, __dwc3_set_mode);
		ret = dwc3_drd_init(dwc);
		if (ret)
			return dev_err_probe(dev, ret, "failed to initialize dual-role\n");
		break;
	default:
		dev_err(dev, "Unsupported mode of operation %d\n", dwc->dr_mode);
		return -EINVAL;
	}

	return 0;
}

static void dwc3_core_exit_mode(struct dwc3 *dwc)
{
	switch (dwc->dr_mode) {
	case USB_DR_MODE_PERIPHERAL:
		dwc3_gadget_exit(dwc);
		break;
	case USB_DR_MODE_HOST:
		dwc3_host_exit(dwc);
		break;
	case USB_DR_MODE_OTG:
		dwc3_drd_exit(dwc);
		break;
	default:
		/* do nothing */
		break;
	}

	/* de-assert DRVVBUS for HOST and OTG mode */
	dwc3_set_prtcap(dwc, DWC3_GCTL_PRTCAP_DEVICE);
}

static void dwc3_get_properties(struct dwc3 *dwc)
{
	struct device		*dev = dwc->dev;
	u8			lpm_nyet_threshold;
	u8			tx_de_emphasis;
	u8			hird_threshold;
	u8			rx_thr_num_pkt_prd = 0;
	u8			rx_max_burst_prd = 0;
	u8			tx_thr_num_pkt_prd = 0;
	u8			tx_max_burst_prd = 0;
	u8			tx_fifo_resize_max_num;
	const char		*usb_psy_name;
	int			ret;

	/* default to highest possible threshold */
	lpm_nyet_threshold = 0xf;

	/* default to -3.5dB de-emphasis */
	tx_de_emphasis = 1;

	/*
	 * default to assert utmi_sleep_n and use maximum allowed HIRD
	 * threshold value of 0b1100
	 */
	hird_threshold = 12;

	/*
	 * default to a TXFIFO size large enough to fit 6 max packets.  This
	 * allows for systems with larger bus latencies to have some headroom
	 * for endpoints that have a large bMaxBurst value.
	 */
	tx_fifo_resize_max_num = 6;

	dwc->maximum_speed = usb_get_maximum_speed(dev);
	dwc->max_ssp_rate = usb_get_maximum_ssp_rate(dev);
	dwc->dr_mode = usb_get_dr_mode(dev);
	dwc->hsphy_mode = of_usb_get_phy_mode(dev->of_node);

	dwc->sysdev_is_parent = device_property_read_bool(dev,
				"linux,sysdev_is_parent");
	if (dwc->sysdev_is_parent)
		dwc->sysdev = dwc->dev->parent;
	else
		dwc->sysdev = dwc->dev;

	ret = device_property_read_string(dev, "usb-psy-name", &usb_psy_name);
	if (ret >= 0) {
		dwc->usb_psy = power_supply_get_by_name(usb_psy_name);
		if (!dwc->usb_psy)
			dev_err(dev, "couldn't get usb power supply\n");
	}

	dwc->has_lpm_erratum = device_property_read_bool(dev,
				"snps,has-lpm-erratum");
	device_property_read_u8(dev, "snps,lpm-nyet-threshold",
				&lpm_nyet_threshold);
	dwc->is_utmi_l1_suspend = device_property_read_bool(dev,
				"snps,is-utmi-l1-suspend");
	device_property_read_u8(dev, "snps,hird-threshold",
				&hird_threshold);
	dwc->dis_start_transfer_quirk = device_property_read_bool(dev,
				"snps,dis-start-transfer-quirk");
	dwc->usb3_lpm_capable = device_property_read_bool(dev,
				"snps,usb3_lpm_capable");
	dwc->usb2_lpm_disable = device_property_read_bool(dev,
				"snps,usb2-lpm-disable");
	dwc->usb2_gadget_lpm_disable = device_property_read_bool(dev,
				"snps,usb2-gadget-lpm-disable");
	device_property_read_u8(dev, "snps,rx-thr-num-pkt-prd",
				&rx_thr_num_pkt_prd);
	device_property_read_u8(dev, "snps,rx-max-burst-prd",
				&rx_max_burst_prd);
	device_property_read_u8(dev, "snps,tx-thr-num-pkt-prd",
				&tx_thr_num_pkt_prd);
	device_property_read_u8(dev, "snps,tx-max-burst-prd",
				&tx_max_burst_prd);
	dwc->do_fifo_resize = device_property_read_bool(dev,
							"tx-fifo-resize");
	if (dwc->do_fifo_resize)
		device_property_read_u8(dev, "tx-fifo-max-num",
					&tx_fifo_resize_max_num);

	dwc->disable_scramble_quirk = device_property_read_bool(dev,
				"snps,disable_scramble_quirk");
	dwc->u2exit_lfps_quirk = device_property_read_bool(dev,
				"snps,u2exit_lfps_quirk");
	dwc->u2ss_inp3_quirk = device_property_read_bool(dev,
				"snps,u2ss_inp3_quirk");
	dwc->req_p1p2p3_quirk = device_property_read_bool(dev,
				"snps,req_p1p2p3_quirk");
	dwc->del_p1p2p3_quirk = device_property_read_bool(dev,
				"snps,del_p1p2p3_quirk");
	dwc->del_phy_power_chg_quirk = device_property_read_bool(dev,
				"snps,del_phy_power_chg_quirk");
	dwc->lfps_filter_quirk = device_property_read_bool(dev,
				"snps,lfps_filter_quirk");
	dwc->rx_detect_poll_quirk = device_property_read_bool(dev,
				"snps,rx_detect_poll_quirk");
	dwc->dis_u3_susphy_quirk = device_property_read_bool(dev,
				"snps,dis_u3_susphy_quirk");
	dwc->dis_u2_susphy_quirk = device_property_read_bool(dev,
				"snps,dis_u2_susphy_quirk");
	dwc->dis_enblslpm_quirk = device_property_read_bool(dev,
				"snps,dis_enblslpm_quirk");
	dwc->dis_u1_entry_quirk = device_property_read_bool(dev,
				"snps,dis-u1-entry-quirk");
	dwc->dis_u2_entry_quirk = device_property_read_bool(dev,
				"snps,dis-u2-entry-quirk");
	dwc->dis_rxdet_inp3_quirk = device_property_read_bool(dev,
				"snps,dis_rxdet_inp3_quirk");
	dwc->dis_u2_freeclk_exists_quirk = device_property_read_bool(dev,
				"snps,dis-u2-freeclk-exists-quirk");
	dwc->dis_del_phy_power_chg_quirk = device_property_read_bool(dev,
				"snps,dis-del-phy-power-chg-quirk");
	dwc->dis_tx_ipgap_linecheck_quirk = device_property_read_bool(dev,
				"snps,dis-tx-ipgap-linecheck-quirk");
	dwc->resume_hs_terminations = device_property_read_bool(dev,
				"snps,resume-hs-terminations");
	dwc->ulpi_ext_vbus_drv = device_property_read_bool(dev,
				"snps,ulpi-ext-vbus-drv");
	dwc->parkmode_disable_ss_quirk = device_property_read_bool(dev,
				"snps,parkmode-disable-ss-quirk");
	dwc->parkmode_disable_hs_quirk = device_property_read_bool(dev,
				"snps,parkmode-disable-hs-quirk");
	dwc->gfladj_refclk_lpm_sel = device_property_read_bool(dev,
				"snps,gfladj-refclk-lpm-sel-quirk");

	dwc->tx_de_emphasis_quirk = device_property_read_bool(dev,
				"snps,tx_de_emphasis_quirk");
	device_property_read_u8(dev, "snps,tx_de_emphasis",
				&tx_de_emphasis);
	device_property_read_string(dev, "snps,hsphy_interface",
				    &dwc->hsphy_interface);
	device_property_read_u32(dev, "snps,quirk-frame-length-adjustment",
				 &dwc->fladj);
	device_property_read_u32(dev, "snps,ref-clock-period-ns",
				 &dwc->ref_clk_per);

	dwc->dis_metastability_quirk = device_property_read_bool(dev,
				"snps,dis_metastability_quirk");

	dwc->dis_split_quirk = device_property_read_bool(dev,
				"snps,dis-split-quirk");

	dwc->lpm_nyet_threshold = lpm_nyet_threshold;
	dwc->tx_de_emphasis = tx_de_emphasis;

	dwc->hird_threshold = hird_threshold;

	dwc->rx_thr_num_pkt_prd = rx_thr_num_pkt_prd;
	dwc->rx_max_burst_prd = rx_max_burst_prd;

	dwc->tx_thr_num_pkt_prd = tx_thr_num_pkt_prd;
	dwc->tx_max_burst_prd = tx_max_burst_prd;

	dwc->imod_interval = 0;

	dwc->tx_fifo_resize_max_num = tx_fifo_resize_max_num;
}

/* check whether the core supports IMOD */
bool dwc3_has_imod(struct dwc3 *dwc)
{
	return DWC3_VER_IS_WITHIN(DWC3, 300A, ANY) ||
		DWC3_VER_IS_WITHIN(DWC31, 120A, ANY) ||
		DWC3_IP_IS(DWC32);
}

static void dwc3_check_params(struct dwc3 *dwc)
{
	struct device *dev = dwc->dev;
	unsigned int hwparam_gen =
		DWC3_GHWPARAMS3_SSPHY_IFC(dwc->hwparams.hwparams3);

	/* Check for proper value of imod_interval */
	if (dwc->imod_interval && !dwc3_has_imod(dwc)) {
		dev_warn(dwc->dev, "Interrupt moderation not supported\n");
		dwc->imod_interval = 0;
	}

	/*
	 * Workaround for STAR 9000961433 which affects only version
	 * 3.00a of the DWC_usb3 core. This prevents the controller
	 * interrupt from being masked while handling events. IMOD
	 * allows us to work around this issue. Enable it for the
	 * affected version.
	 */
	if (!dwc->imod_interval &&
	    DWC3_VER_IS(DWC3, 300A))
		dwc->imod_interval = 1;

	/* Check the maximum_speed parameter */
	switch (dwc->maximum_speed) {
	case USB_SPEED_FULL:
	case USB_SPEED_HIGH:
		break;
	case USB_SPEED_SUPER:
		if (hwparam_gen == DWC3_GHWPARAMS3_SSPHY_IFC_DIS)
			dev_warn(dev, "UDC doesn't support Gen 1\n");
		break;
	case USB_SPEED_SUPER_PLUS:
		if ((DWC3_IP_IS(DWC32) &&
		     hwparam_gen == DWC3_GHWPARAMS3_SSPHY_IFC_DIS) ||
		    (!DWC3_IP_IS(DWC32) &&
		     hwparam_gen != DWC3_GHWPARAMS3_SSPHY_IFC_GEN2))
			dev_warn(dev, "UDC doesn't support SSP\n");
		break;
	default:
		dev_err(dev, "invalid maximum_speed parameter %d\n",
			dwc->maximum_speed);
		fallthrough;
	case USB_SPEED_UNKNOWN:
		switch (hwparam_gen) {
		case DWC3_GHWPARAMS3_SSPHY_IFC_GEN2:
			dwc->maximum_speed = USB_SPEED_SUPER_PLUS;
			break;
		case DWC3_GHWPARAMS3_SSPHY_IFC_GEN1:
			if (DWC3_IP_IS(DWC32))
				dwc->maximum_speed = USB_SPEED_SUPER_PLUS;
			else
				dwc->maximum_speed = USB_SPEED_SUPER;
			break;
		case DWC3_GHWPARAMS3_SSPHY_IFC_DIS:
			dwc->maximum_speed = USB_SPEED_HIGH;
			break;
		default:
			dwc->maximum_speed = USB_SPEED_SUPER;
			break;
		}
		break;
	}

	/*
	 * Currently the controller does not have visibility into the HW
	 * parameter to determine the maximum number of lanes the HW supports.
	 * If the number of lanes is not specified in the device property, then
	 * set the default to support dual-lane for DWC_usb32 and single-lane
	 * for DWC_usb31 for super-speed-plus.
	 */
	if (dwc->maximum_speed == USB_SPEED_SUPER_PLUS) {
		switch (dwc->max_ssp_rate) {
		case USB_SSP_GEN_2x1:
			if (hwparam_gen == DWC3_GHWPARAMS3_SSPHY_IFC_GEN1)
				dev_warn(dev, "UDC only supports Gen 1\n");
			break;
		case USB_SSP_GEN_1x2:
		case USB_SSP_GEN_2x2:
			if (DWC3_IP_IS(DWC31))
				dev_warn(dev, "UDC only supports single lane\n");
			break;
		case USB_SSP_GEN_UNKNOWN:
		default:
			switch (hwparam_gen) {
			case DWC3_GHWPARAMS3_SSPHY_IFC_GEN2:
				if (DWC3_IP_IS(DWC32))
					dwc->max_ssp_rate = USB_SSP_GEN_2x2;
				else
					dwc->max_ssp_rate = USB_SSP_GEN_2x1;
				break;
			case DWC3_GHWPARAMS3_SSPHY_IFC_GEN1:
				if (DWC3_IP_IS(DWC32))
					dwc->max_ssp_rate = USB_SSP_GEN_1x2;
				break;
			}
			break;
		}
	}
}

static struct extcon_dev *dwc3_get_extcon(struct dwc3 *dwc)
{
	struct device *dev = dwc->dev;
	struct device_node *np_phy;
	struct extcon_dev *edev = NULL;
	const char *name;

	if (device_property_read_bool(dev, "extcon"))
		return extcon_get_edev_by_phandle(dev, 0);

	/*
	 * Device tree platforms should get extcon via phandle.
	 * On ACPI platforms, we get the name from a device property.
	 * This device property is for kernel internal use only and
	 * is expected to be set by the glue code.
	 */
	if (device_property_read_string(dev, "linux,extcon-name", &name) == 0)
		return extcon_get_extcon_dev(name);

	/*
	 * Check explicitly if "usb-role-switch" is used since
	 * extcon_find_edev_by_node() can not be used to check the absence of
	 * an extcon device. In the absence of an device it will always return
	 * EPROBE_DEFER.
	 */
	if (IS_ENABLED(CONFIG_USB_ROLE_SWITCH) &&
	    device_property_read_bool(dev, "usb-role-switch"))
		return NULL;

	/*
	 * Try to get an extcon device from the USB PHY controller's "port"
	 * node. Check if it has the "port" node first, to avoid printing the
	 * error message from underlying code, as it's a valid case: extcon
	 * device (and "port" node) may be missing in case of "usb-role-switch"
	 * or OTG mode.
	 */
	np_phy = of_parse_phandle(dev->of_node, "phys", 0);
	if (of_graph_is_present(np_phy)) {
		struct device_node *np_conn;

		np_conn = of_graph_get_remote_node(np_phy, -1, -1);
		if (np_conn)
			edev = extcon_find_edev_by_node(np_conn);
		of_node_put(np_conn);
	}
	of_node_put(np_phy);

	return edev;
}

static int dwc3_get_clocks(struct dwc3 *dwc)
{
	struct device *dev = dwc->dev;

	if (!dev->of_node)
		return 0;

	/*
	 * Clocks are optional, but new DT platforms should support all clocks
	 * as required by the DT-binding.
	 * Some devices have different clock names in legacy device trees,
	 * check for them to retain backwards compatibility.
	 */
	dwc->bus_clk = devm_clk_get_optional(dev, "bus_early");
	if (IS_ERR(dwc->bus_clk)) {
		return dev_err_probe(dev, PTR_ERR(dwc->bus_clk),
				"could not get bus clock\n");
	}

	if (dwc->bus_clk == NULL) {
		dwc->bus_clk = devm_clk_get_optional(dev, "bus_clk");
		if (IS_ERR(dwc->bus_clk)) {
			return dev_err_probe(dev, PTR_ERR(dwc->bus_clk),
					"could not get bus clock\n");
		}
	}

	dwc->ref_clk = devm_clk_get_optional(dev, "ref");
	if (IS_ERR(dwc->ref_clk)) {
		return dev_err_probe(dev, PTR_ERR(dwc->ref_clk),
				"could not get ref clock\n");
	}

	if (dwc->ref_clk == NULL) {
		dwc->ref_clk = devm_clk_get_optional(dev, "ref_clk");
		if (IS_ERR(dwc->ref_clk)) {
			return dev_err_probe(dev, PTR_ERR(dwc->ref_clk),
					"could not get ref clock\n");
		}
	}

	dwc->susp_clk = devm_clk_get_optional(dev, "suspend");
	if (IS_ERR(dwc->susp_clk)) {
		return dev_err_probe(dev, PTR_ERR(dwc->susp_clk),
				"could not get suspend clock\n");
	}

	if (dwc->susp_clk == NULL) {
		dwc->susp_clk = devm_clk_get_optional(dev, "suspend_clk");
		if (IS_ERR(dwc->susp_clk)) {
			return dev_err_probe(dev, PTR_ERR(dwc->susp_clk),
					"could not get suspend clock\n");
		}
	}

	return 0;
}

static int dwc3_probe(struct platform_device *pdev)
{
	struct device		*dev = &pdev->dev;
	struct resource		*res, dwc_res;
	void __iomem		*regs;
	struct dwc3		*dwc;
	int			ret;

	dwc = devm_kzalloc(dev, sizeof(*dwc), GFP_KERNEL);
	if (!dwc)
		return -ENOMEM;

	dwc->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "missing memory resource\n");
		return -ENODEV;
	}

	dwc->xhci_resources[0].start = res->start;
	dwc->xhci_resources[0].end = dwc->xhci_resources[0].start +
					DWC3_XHCI_REGS_END;
	dwc->xhci_resources[0].flags = res->flags;
	dwc->xhci_resources[0].name = res->name;

	/*
	 * Request memory region but exclude xHCI regs,
	 * since it will be requested by the xhci-plat driver.
	 */
	dwc_res = *res;
	dwc_res.start += DWC3_GLOBALS_REGS_START;

	regs = devm_ioremap_resource(dev, &dwc_res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	dwc->regs	= regs;
	dwc->regs_size	= resource_size(&dwc_res);

	dwc3_get_properties(dwc);

	dwc->reset = devm_reset_control_array_get_optional_shared(dev);
	if (IS_ERR(dwc->reset)) {
		ret = PTR_ERR(dwc->reset);
		goto err_put_psy;
	}

	ret = dwc3_get_clocks(dwc);
	if (ret)
		goto err_put_psy;

	ret = reset_control_deassert(dwc->reset);
	if (ret)
		goto err_put_psy;

	ret = dwc3_clk_enable(dwc);
	if (ret)
		goto err_assert_reset;

	if (!dwc3_core_is_valid(dwc)) {
		dev_err(dwc->dev, "this is not a DesignWare USB3 DRD Core\n");
		ret = -ENODEV;
		goto err_disable_clks;
	}

	platform_set_drvdata(pdev, dwc);
	dwc3_cache_hwparams(dwc);

	if (!dwc->sysdev_is_parent &&
	    DWC3_GHWPARAMS0_AWIDTH(dwc->hwparams.hwparams0) == 64) {
		ret = dma_set_mask_and_coherent(dwc->sysdev, DMA_BIT_MASK(64));
		if (ret)
			goto err_disable_clks;
	}

	spin_lock_init(&dwc->lock);
	mutex_init(&dwc->mutex);

	pm_runtime_get_noresume(dev);
	pm_runtime_set_active(dev);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_autosuspend_delay(dev, DWC3_DEFAULT_AUTOSUSPEND_DELAY);
	pm_runtime_enable(dev);

	pm_runtime_forbid(dev);

	ret = dwc3_alloc_event_buffers(dwc, DWC3_EVENT_BUFFERS_SIZE);
	if (ret) {
		dev_err(dwc->dev, "failed to allocate event buffers\n");
		ret = -ENOMEM;
		goto err_allow_rpm;
	}

	dwc->edev = dwc3_get_extcon(dwc);
	if (IS_ERR(dwc->edev)) {
		ret = dev_err_probe(dwc->dev, PTR_ERR(dwc->edev), "failed to get extcon\n");
		goto err_free_event_buffers;
	}

	ret = dwc3_get_dr_mode(dwc);
	if (ret)
		goto err_free_event_buffers;

	ret = dwc3_core_init(dwc);
	if (ret) {
		dev_err_probe(dev, ret, "failed to initialize core\n");
		goto err_free_event_buffers;
	}

	dwc3_check_params(dwc);
	dwc3_debugfs_init(dwc);

	ret = dwc3_core_init_mode(dwc);
	if (ret)
		goto err_exit_debugfs;

	pm_runtime_put(dev);

	return 0;

err_exit_debugfs:
	dwc3_debugfs_exit(dwc);
	dwc3_event_buffers_cleanup(dwc);
	dwc3_phy_power_off(dwc);
	dwc3_phy_exit(dwc);
	dwc3_ulpi_exit(dwc);
err_free_event_buffers:
	dwc3_free_event_buffers(dwc);
err_allow_rpm:
	pm_runtime_allow(dev);
	pm_runtime_disable(dev);
	pm_runtime_dont_use_autosuspend(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_put_noidle(dev);
err_disable_clks:
	dwc3_clk_disable(dwc);
err_assert_reset:
	reset_control_assert(dwc->reset);
err_put_psy:
	if (dwc->usb_psy)
		power_supply_put(dwc->usb_psy);

	return ret;
}

static int dwc3_remove(struct platform_device *pdev)
{
	struct dwc3	*dwc = platform_get_drvdata(pdev);

	pm_runtime_get_sync(&pdev->dev);

	dwc3_core_exit_mode(dwc);
	dwc3_debugfs_exit(dwc);

	dwc3_core_exit(dwc);
	dwc3_ulpi_exit(dwc);

	pm_runtime_allow(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_dont_use_autosuspend(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);

	dwc3_free_event_buffers(dwc);

	if (dwc->usb_psy)
		power_supply_put(dwc->usb_psy);

	return 0;
}

#ifdef CONFIG_PM
static int dwc3_core_init_for_resume(struct dwc3 *dwc)
{
	int ret;

	ret = reset_control_deassert(dwc->reset);
	if (ret)
		return ret;

	ret = dwc3_clk_enable(dwc);
	if (ret)
		goto assert_reset;

	ret = dwc3_core_init(dwc);
	if (ret)
		goto disable_clks;

	return 0;

disable_clks:
	dwc3_clk_disable(dwc);
assert_reset:
	reset_control_assert(dwc->reset);

	return ret;
}

static int dwc3_suspend_common(struct dwc3 *dwc, pm_message_t msg)
{
	unsigned long	flags;
	u32 reg;

	switch (dwc->current_dr_role) {
	case DWC3_GCTL_PRTCAP_DEVICE:
		if (pm_runtime_suspended(dwc->dev))
			break;
		dwc3_gadget_suspend(dwc);
		synchronize_irq(dwc->irq_gadget);
		dwc3_core_exit(dwc);
		break;
	case DWC3_GCTL_PRTCAP_HOST:
		if (!PMSG_IS_AUTO(msg) && !device_may_wakeup(dwc->dev)) {
			dwc3_core_exit(dwc);
			break;
		}

		/* Let controller to suspend HSPHY before PHY driver suspends */
		if (dwc->dis_u2_susphy_quirk ||
		    dwc->dis_enblslpm_quirk) {
			reg = dwc3_readl(dwc->regs, DWC3_GUSB2PHYCFG(0));
			reg |=  DWC3_GUSB2PHYCFG_ENBLSLPM |
				DWC3_GUSB2PHYCFG_SUSPHY;
			dwc3_writel(dwc->regs, DWC3_GUSB2PHYCFG(0), reg);

			/* Give some time for USB2 PHY to suspend */
			usleep_range(5000, 6000);
		}

		phy_pm_runtime_put_sync(dwc->usb2_generic_phy);
		phy_pm_runtime_put_sync(dwc->usb3_generic_phy);
		break;
	case DWC3_GCTL_PRTCAP_OTG:
		/* do nothing during runtime_suspend */
		if (PMSG_IS_AUTO(msg))
			break;

		if (dwc->current_otg_role == DWC3_OTG_ROLE_DEVICE) {
			spin_lock_irqsave(&dwc->lock, flags);
			dwc3_gadget_suspend(dwc);
			spin_unlock_irqrestore(&dwc->lock, flags);
			synchronize_irq(dwc->irq_gadget);
		}

		dwc3_otg_exit(dwc);
		dwc3_core_exit(dwc);
		break;
	default:
		/* do nothing */
		break;
	}

	return 0;
}

static int dwc3_resume_common(struct dwc3 *dwc, pm_message_t msg)
{
	unsigned long	flags;
	int		ret;
	u32		reg;

	switch (dwc->current_dr_role) {
	case DWC3_GCTL_PRTCAP_DEVICE:
		ret = dwc3_core_init_for_resume(dwc);
		if (ret)
			return ret;

		dwc3_set_prtcap(dwc, DWC3_GCTL_PRTCAP_DEVICE);
		dwc3_gadget_resume(dwc);
		break;
	case DWC3_GCTL_PRTCAP_HOST:
		if (!PMSG_IS_AUTO(msg) && !device_may_wakeup(dwc->dev)) {
			ret = dwc3_core_init_for_resume(dwc);
			if (ret)
				return ret;
			dwc3_set_prtcap(dwc, DWC3_GCTL_PRTCAP_HOST);
			break;
		}
		/* Restore GUSB2PHYCFG bits that were modified in suspend */
		reg = dwc3_readl(dwc->regs, DWC3_GUSB2PHYCFG(0));
		if (dwc->dis_u2_susphy_quirk)
			reg &= ~DWC3_GUSB2PHYCFG_SUSPHY;

		if (dwc->dis_enblslpm_quirk)
			reg &= ~DWC3_GUSB2PHYCFG_ENBLSLPM;

		dwc3_writel(dwc->regs, DWC3_GUSB2PHYCFG(0), reg);

		phy_pm_runtime_get_sync(dwc->usb2_generic_phy);
		phy_pm_runtime_get_sync(dwc->usb3_generic_phy);
		break;
	case DWC3_GCTL_PRTCAP_OTG:
		/* nothing to do on runtime_resume */
		if (PMSG_IS_AUTO(msg))
			break;

		ret = dwc3_core_init_for_resume(dwc);
		if (ret)
			return ret;

		dwc3_set_prtcap(dwc, dwc->current_dr_role);

		dwc3_otg_init(dwc);
		if (dwc->current_otg_role == DWC3_OTG_ROLE_HOST) {
			dwc3_otg_host_init(dwc);
		} else if (dwc->current_otg_role == DWC3_OTG_ROLE_DEVICE) {
			spin_lock_irqsave(&dwc->lock, flags);
			dwc3_gadget_resume(dwc);
			spin_unlock_irqrestore(&dwc->lock, flags);
		}

		break;
	default:
		/* do nothing */
		break;
	}

	return 0;
}

static int dwc3_runtime_checks(struct dwc3 *dwc)
{
	switch (dwc->current_dr_role) {
	case DWC3_GCTL_PRTCAP_DEVICE:
		if (dwc->connected)
			return -EBUSY;
		break;
	case DWC3_GCTL_PRTCAP_HOST:
	default:
		/* do nothing */
		break;
	}

	return 0;
}

static int dwc3_runtime_suspend(struct device *dev)
{
	struct dwc3     *dwc = dev_get_drvdata(dev);
	int		ret;

	if (dwc3_runtime_checks(dwc))
		return -EBUSY;

	ret = dwc3_suspend_common(dwc, PMSG_AUTO_SUSPEND);
	if (ret)
		return ret;

	return 0;
}

static int dwc3_runtime_resume(struct device *dev)
{
	struct dwc3     *dwc = dev_get_drvdata(dev);
	int		ret;

	ret = dwc3_resume_common(dwc, PMSG_AUTO_RESUME);
	if (ret)
		return ret;

	switch (dwc->current_dr_role) {
	case DWC3_GCTL_PRTCAP_DEVICE:
		dwc3_gadget_process_pending_events(dwc);
		break;
	case DWC3_GCTL_PRTCAP_HOST:
	default:
		/* do nothing */
		break;
	}

	pm_runtime_mark_last_busy(dev);

	return 0;
}

static int dwc3_runtime_idle(struct device *dev)
{
	struct dwc3     *dwc = dev_get_drvdata(dev);

	switch (dwc->current_dr_role) {
	case DWC3_GCTL_PRTCAP_DEVICE:
		if (dwc3_runtime_checks(dwc))
			return -EBUSY;
		break;
	case DWC3_GCTL_PRTCAP_HOST:
	default:
		/* do nothing */
		break;
	}

	pm_runtime_mark_last_busy(dev);
	pm_runtime_autosuspend(dev);

	return 0;
}
#endif /* CONFIG_PM */

#ifdef CONFIG_PM_SLEEP
static int dwc3_suspend(struct device *dev)
{
	struct dwc3	*dwc = dev_get_drvdata(dev);
	int		ret;

	ret = dwc3_suspend_common(dwc, PMSG_SUSPEND);
	if (ret)
		return ret;

	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int dwc3_resume(struct device *dev)
{
	struct dwc3	*dwc = dev_get_drvdata(dev);
	int		ret;

	pinctrl_pm_select_default_state(dev);

	ret = dwc3_resume_common(dwc, PMSG_RESUME);
	if (ret)
		return ret;

	pm_runtime_disable(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return 0;
}

static void dwc3_complete(struct device *dev)
{
	struct dwc3	*dwc = dev_get_drvdata(dev);
	u32		reg;

	if (dwc->current_dr_role == DWC3_GCTL_PRTCAP_HOST &&
			dwc->dis_split_quirk) {
		reg = dwc3_readl(dwc->regs, DWC3_GUCTL3);
		reg |= DWC3_GUCTL3_SPLITDISABLE;
		dwc3_writel(dwc->regs, DWC3_GUCTL3, reg);
	}
}
#else
#define dwc3_complete NULL
#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops dwc3_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dwc3_suspend, dwc3_resume)
	.complete = dwc3_complete,
	SET_RUNTIME_PM_OPS(dwc3_runtime_suspend, dwc3_runtime_resume,
			dwc3_runtime_idle)
};

#ifdef CONFIG_OF
static const struct of_device_id of_dwc3_match[] = {
	{
		.compatible = "snps,dwc3"
	},
	{
		.compatible = "synopsys,dwc3"
	},
	{ },
};
MODULE_DEVICE_TABLE(of, of_dwc3_match);
#endif

#ifdef CONFIG_ACPI

#define ACPI_ID_INTEL_BSW	"808622B7"

static const struct acpi_device_id dwc3_acpi_match[] = {
	{ ACPI_ID_INTEL_BSW, 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, dwc3_acpi_match);
#endif

static struct platform_driver dwc3_driver = {
	.probe		= dwc3_probe,
	.remove		= dwc3_remove,
	.driver		= {
		.name	= "dwc3",
		.of_match_table	= of_match_ptr(of_dwc3_match),
		.acpi_match_table = ACPI_PTR(dwc3_acpi_match),
		.pm	= &dwc3_dev_pm_ops,
	},
};

module_platform_driver(dwc3_driver);

MODULE_ALIAS("platform:dwc3");
MODULE_AUTHOR("Felipe Balbi <balbi@ti.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare USB3 DRD Controller Driver");
