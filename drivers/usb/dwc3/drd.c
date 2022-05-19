// SPDX-License-Identifier: GPL-2.0
/*
 * drd.c - DesignWare USB3 DRD Controller Dual-role support
 *
 * Copyright (C) 2017 Texas Instruments Incorporated - https://www.ti.com
 *
 * Authors: Roger Quadros <rogerq@ti.com>
 */

#include <linux/extcon.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/property.h>

#include "debug.h"
#include "core.h"
#include "gadget.h"

static void dwc3_otg_disable_events(struct dwc3 *dwc, u32 disable_mask)
{
	u32 reg = dwc3_readl(dwc->regs, DWC3_OEVTEN);

	reg &= ~(disable_mask);
	dwc3_writel(dwc->regs, DWC3_OEVTEN, reg);
}

static void dwc3_otg_enable_events(struct dwc3 *dwc, u32 enable_mask)
{
	u32 reg = dwc3_readl(dwc->regs, DWC3_OEVTEN);

	reg |= (enable_mask);
	dwc3_writel(dwc->regs, DWC3_OEVTEN, reg);
}

static void dwc3_otg_clear_events(struct dwc3 *dwc)
{
	u32 reg = dwc3_readl(dwc->regs, DWC3_OEVT);

	dwc3_writel(dwc->regs, DWC3_OEVTEN, reg);
}

#define DWC3_OTG_ALL_EVENTS	(DWC3_OEVTEN_XHCIRUNSTPSETEN | \
		DWC3_OEVTEN_DEVRUNSTPSETEN | DWC3_OEVTEN_HIBENTRYEN | \
		DWC3_OEVTEN_CONIDSTSCHNGEN | DWC3_OEVTEN_HRRCONFNOTIFEN | \
		DWC3_OEVTEN_HRRINITNOTIFEN | DWC3_OEVTEN_ADEVIDLEEN | \
		DWC3_OEVTEN_ADEVBHOSTENDEN | DWC3_OEVTEN_ADEVHOSTEN | \
		DWC3_OEVTEN_ADEVHNPCHNGEN | DWC3_OEVTEN_ADEVSRPDETEN | \
		DWC3_OEVTEN_ADEVSESSENDDETEN | DWC3_OEVTEN_BDEVBHOSTENDEN | \
		DWC3_OEVTEN_BDEVHNPCHNGEN | DWC3_OEVTEN_BDEVSESSVLDDETEN | \
		DWC3_OEVTEN_BDEVVBUSCHNGEN)

static irqreturn_t dwc3_otg_thread_irq(int irq, void *_dwc)
{
	struct dwc3 *dwc = _dwc;

	spin_lock(&dwc->lock);
	if (dwc->otg_restart_host) {
		dwc3_otg_host_init(dwc);
		dwc->otg_restart_host = false;
	}

	spin_unlock(&dwc->lock);

	dwc3_set_mode(dwc, DWC3_GCTL_PRTCAP_OTG);

	return IRQ_HANDLED;
}

static irqreturn_t dwc3_otg_irq(int irq, void *_dwc)
{
	u32 reg;
	struct dwc3 *dwc = _dwc;
	irqreturn_t ret = IRQ_NONE;

	reg = dwc3_readl(dwc->regs, DWC3_OEVT);
	if (reg) {
		/* ignore non OTG events, we can't disable them in OEVTEN */
		if (!(reg & DWC3_OTG_ALL_EVENTS)) {
			dwc3_writel(dwc->regs, DWC3_OEVT, reg);
			return IRQ_NONE;
		}

		if (dwc->current_otg_role == DWC3_OTG_ROLE_HOST &&
		    !(reg & DWC3_OEVT_DEVICEMODE))
			dwc->otg_restart_host = true;
		dwc3_writel(dwc->regs, DWC3_OEVT, reg);
		ret = IRQ_WAKE_THREAD;
	}

	return ret;
}

static void dwc3_otgregs_init(struct dwc3 *dwc)
{
	u32 reg;

	/*
	 * Prevent host/device reset from resetting OTG core.
	 * If we don't do this then xhci_reset (USBCMD.HCRST) will reset
	 * the signal outputs sent to the PHY, the OTG FSM logic of the
	 * core and also the resets to the VBUS filters inside the core.
	 */
	reg = dwc3_readl(dwc->regs, DWC3_OCFG);
	reg |= DWC3_OCFG_SFTRSTMASK;
	dwc3_writel(dwc->regs, DWC3_OCFG, reg);

	/* Disable hibernation for simplicity */
	reg = dwc3_readl(dwc->regs, DWC3_GCTL);
	reg &= ~DWC3_GCTL_GBLHIBERNATIONEN;
	dwc3_writel(dwc->regs, DWC3_GCTL, reg);

	/*
	 * Initialize OTG registers as per
	 * Figure 11-4 OTG Driver Overall Programming Flow
	 */
	/* OCFG.SRPCap = 0, OCFG.HNPCap = 0 */
	reg = dwc3_readl(dwc->regs, DWC3_OCFG);
	reg &= ~(DWC3_OCFG_SRPCAP | DWC3_OCFG_HNPCAP);
	dwc3_writel(dwc->regs, DWC3_OCFG, reg);
	/* OEVT = FFFF */
	dwc3_otg_clear_events(dwc);
	/* OEVTEN = 0 */
	dwc3_otg_disable_events(dwc, DWC3_OTG_ALL_EVENTS);
	/* OEVTEN.ConIDStsChngEn = 1. Instead we enable all events */
	dwc3_otg_enable_events(dwc, DWC3_OTG_ALL_EVENTS);
	/*
	 * OCTL.PeriMode = 1, OCTL.DevSetHNPEn = 0, OCTL.HstSetHNPEn = 0,
	 * OCTL.HNPReq = 0
	 */
	reg = dwc3_readl(dwc->regs, DWC3_OCTL);
	reg |= DWC3_OCTL_PERIMODE;
	reg &= ~(DWC3_OCTL_DEVSETHNPEN | DWC3_OCTL_HSTSETHNPEN |
		 DWC3_OCTL_HNPREQ);
	dwc3_writel(dwc->regs, DWC3_OCTL, reg);
}

static int dwc3_otg_get_irq(struct dwc3 *dwc)
{
	struct platform_device *dwc3_pdev = to_platform_device(dwc->dev);
	int irq;

	irq = platform_get_irq_byname_optional(dwc3_pdev, "otg");
	if (irq > 0)
		goto out;

	if (irq == -EPROBE_DEFER)
		goto out;

	irq = platform_get_irq_byname_optional(dwc3_pdev, "dwc_usb3");
	if (irq > 0)
		goto out;

	if (irq == -EPROBE_DEFER)
		goto out;

	irq = platform_get_irq(dwc3_pdev, 0);
	if (irq > 0)
		goto out;

	if (!irq)
		irq = -EINVAL;

out:
	return irq;
}

void dwc3_otg_init(struct dwc3 *dwc)
{
	u32 reg;

	/*
	 * As per Figure 11-4 OTG Driver Overall Programming Flow,
	 * block "Initialize GCTL for OTG operation".
	 */
	/* GCTL.PrtCapDir=2'b11 */
	dwc3_set_prtcap(dwc, DWC3_GCTL_PRTCAP_OTG);
	/* GUSB2PHYCFG0.SusPHY=0 */
	reg = dwc3_readl(dwc->regs, DWC3_GUSB2PHYCFG(0));
	reg &= ~DWC3_GUSB2PHYCFG_SUSPHY;
	dwc3_writel(dwc->regs, DWC3_GUSB2PHYCFG(0), reg);

	/* Initialize OTG registers */
	dwc3_otgregs_init(dwc);
}

void dwc3_otg_exit(struct dwc3 *dwc)
{
	/* disable all OTG IRQs */
	dwc3_otg_disable_events(dwc, DWC3_OTG_ALL_EVENTS);
	/* clear all events */
	dwc3_otg_clear_events(dwc);
}

/* should be called before Host controller driver is started */
void dwc3_otg_host_init(struct dwc3 *dwc)
{
	u32 reg;

	/* As per Figure 11-10 A-Device Flow Diagram */
	/* OCFG.HNPCap = 0, OCFG.SRPCap = 0. Already 0 */

	/*
	 * OCTL.PeriMode=0, OCTL.TermSelDLPulse = 0,
	 * OCTL.DevSetHNPEn = 0, OCTL.HstSetHNPEn = 0
	 */
	reg = dwc3_readl(dwc->regs, DWC3_OCTL);
	reg &= ~(DWC3_OCTL_PERIMODE | DWC3_OCTL_TERMSELIDPULSE |
			DWC3_OCTL_DEVSETHNPEN | DWC3_OCTL_HSTSETHNPEN);
	dwc3_writel(dwc->regs, DWC3_OCTL, reg);

	/*
	 * OCFG.DisPrtPwrCutoff = 0/1
	 */
	reg = dwc3_readl(dwc->regs, DWC3_OCFG);
	reg &= ~DWC3_OCFG_DISPWRCUTTOFF;
	dwc3_writel(dwc->regs, DWC3_OCFG, reg);

	/*
	 * OCFG.SRPCap = 1, OCFG.HNPCap = GHWPARAMS6.HNP_CAP
	 * We don't want SRP/HNP for simple dual-role so leave
	 * these disabled.
	 */

	/*
	 * OEVTEN.OTGADevHostEvntEn = 1
	 * OEVTEN.OTGADevSessEndDetEvntEn = 1
	 * We don't want HNP/role-swap so leave these disabled.
	 */

	/* GUSB2PHYCFG.ULPIAutoRes = 1/0, GUSB2PHYCFG.SusPHY = 1 */
	if (!dwc->dis_u2_susphy_quirk) {
		reg = dwc3_readl(dwc->regs, DWC3_GUSB2PHYCFG(0));
		reg |= DWC3_GUSB2PHYCFG_SUSPHY;
		dwc3_writel(dwc->regs, DWC3_GUSB2PHYCFG(0), reg);
	}

	/* Set Port Power to enable VBUS: OCTL.PrtPwrCtl = 1 */
	reg = dwc3_readl(dwc->regs, DWC3_OCTL);
	reg |= DWC3_OCTL_PRTPWRCTL;
	dwc3_writel(dwc->regs, DWC3_OCTL, reg);
}

/* should be called after Host controller driver is stopped */
static void dwc3_otg_host_exit(struct dwc3 *dwc)
{
	u32 reg;

	/*
	 * Exit from A-device flow as per
	 * Figure 11-4 OTG Driver Overall Programming Flow
	 */

	/*
	 * OEVTEN.OTGADevBHostEndEvntEn=0, OEVTEN.OTGADevHNPChngEvntEn=0
	 * OEVTEN.OTGADevSessEndDetEvntEn=0,
	 * OEVTEN.OTGADevHostEvntEn = 0
	 * But we don't disable any OTG events
	 */

	/* OCTL.HstSetHNPEn = 0, OCTL.PrtPwrCtl=0 */
	reg = dwc3_readl(dwc->regs, DWC3_OCTL);
	reg &= ~(DWC3_OCTL_HSTSETHNPEN | DWC3_OCTL_PRTPWRCTL);
	dwc3_writel(dwc->regs, DWC3_OCTL, reg);
}

/* should be called before the gadget controller driver is started */
static void dwc3_otg_device_init(struct dwc3 *dwc)
{
	u32 reg;

	/* As per Figure 11-20 B-Device Flow Diagram */

	/*
	 * OCFG.HNPCap = GHWPARAMS6.HNP_CAP, OCFG.SRPCap = 1
	 * but we keep them 0 for simple dual-role operation.
	 */
	reg = dwc3_readl(dwc->regs, DWC3_OCFG);
	/* OCFG.OTGSftRstMsk = 0/1 */
	reg |= DWC3_OCFG_SFTRSTMASK;
	dwc3_writel(dwc->regs, DWC3_OCFG, reg);
	/*
	 * OCTL.PeriMode = 1
	 * OCTL.TermSelDLPulse = 0/1, OCTL.HNPReq = 0
	 * OCTL.DevSetHNPEn = 0, OCTL.HstSetHNPEn = 0
	 */
	reg = dwc3_readl(dwc->regs, DWC3_OCTL);
	reg |= DWC3_OCTL_PERIMODE;
	reg &= ~(DWC3_OCTL_TERMSELIDPULSE | DWC3_OCTL_HNPREQ |
			DWC3_OCTL_DEVSETHNPEN | DWC3_OCTL_HSTSETHNPEN);
	dwc3_writel(dwc->regs, DWC3_OCTL, reg);
	/* OEVTEN.OTGBDevSesVldDetEvntEn = 1 */
	dwc3_otg_enable_events(dwc, DWC3_OEVTEN_BDEVSESSVLDDETEN);
	/* GUSB2PHYCFG.ULPIAutoRes = 0, GUSB2PHYCFG0.SusPHY = 1 */
	if (!dwc->dis_u2_susphy_quirk) {
		reg = dwc3_readl(dwc->regs, DWC3_GUSB2PHYCFG(0));
		reg |= DWC3_GUSB2PHYCFG_SUSPHY;
		dwc3_writel(dwc->regs, DWC3_GUSB2PHYCFG(0), reg);
	}
	/* GCTL.GblHibernationEn = 0. Already 0. */
}

/* should be called after the gadget controller driver is stopped */
static void dwc3_otg_device_exit(struct dwc3 *dwc)
{
	u32 reg;

	/*
	 * Exit from B-device flow as per
	 * Figure 11-4 OTG Driver Overall Programming Flow
	 */

	/*
	 * OEVTEN.OTGBDevHNPChngEvntEn = 0
	 * OEVTEN.OTGBDevVBusChngEvntEn = 0
	 * OEVTEN.OTGBDevBHostEndEvntEn = 0
	 */
	dwc3_otg_disable_events(dwc, DWC3_OEVTEN_BDEVHNPCHNGEN |
				DWC3_OEVTEN_BDEVVBUSCHNGEN |
				DWC3_OEVTEN_BDEVBHOSTENDEN);

	/* OCTL.DevSetHNPEn = 0, OCTL.HNPReq = 0, OCTL.PeriMode=1 */
	reg = dwc3_readl(dwc->regs, DWC3_OCTL);
	reg &= ~(DWC3_OCTL_DEVSETHNPEN | DWC3_OCTL_HNPREQ);
	reg |= DWC3_OCTL_PERIMODE;
	dwc3_writel(dwc->regs, DWC3_OCTL, reg);
}

void dwc3_otg_update(struct dwc3 *dwc, bool ignore_idstatus)
{
	int ret;
	u32 reg;
	int id;
	unsigned long flags;

	if (dwc->dr_mode != USB_DR_MODE_OTG)
		return;

	/* don't do anything if debug user changed role to not OTG */
	if (dwc->current_dr_role != DWC3_GCTL_PRTCAP_OTG)
		return;

	if (!ignore_idstatus) {
		reg = dwc3_readl(dwc->regs, DWC3_OSTS);
		id = !!(reg & DWC3_OSTS_CONIDSTS);

		dwc->desired_otg_role = id ? DWC3_OTG_ROLE_DEVICE :
					DWC3_OTG_ROLE_HOST;
	}

	if (dwc->desired_otg_role == dwc->current_otg_role)
		return;

	switch (dwc->current_otg_role) {
	case DWC3_OTG_ROLE_HOST:
		dwc3_host_exit(dwc);
		spin_lock_irqsave(&dwc->lock, flags);
		dwc3_otg_host_exit(dwc);
		spin_unlock_irqrestore(&dwc->lock, flags);
		break;
	case DWC3_OTG_ROLE_DEVICE:
		dwc3_gadget_exit(dwc);
		spin_lock_irqsave(&dwc->lock, flags);
		dwc3_event_buffers_cleanup(dwc);
		dwc3_otg_device_exit(dwc);
		spin_unlock_irqrestore(&dwc->lock, flags);
		break;
	default:
		break;
	}

	spin_lock_irqsave(&dwc->lock, flags);

	dwc->current_otg_role = dwc->desired_otg_role;

	spin_unlock_irqrestore(&dwc->lock, flags);

	switch (dwc->desired_otg_role) {
	case DWC3_OTG_ROLE_HOST:
		spin_lock_irqsave(&dwc->lock, flags);
		dwc3_otgregs_init(dwc);
		dwc3_otg_host_init(dwc);
		spin_unlock_irqrestore(&dwc->lock, flags);
		ret = dwc3_host_init(dwc);
		if (ret) {
			dev_err(dwc->dev, "failed to initialize host\n");
		} else {
			if (dwc->usb2_phy)
				otg_set_vbus(dwc->usb2_phy->otg, true);
			if (dwc->usb2_generic_phy)
				phy_set_mode(dwc->usb2_generic_phy,
					     PHY_MODE_USB_HOST);
		}
		break;
	case DWC3_OTG_ROLE_DEVICE:
		spin_lock_irqsave(&dwc->lock, flags);
		dwc3_otgregs_init(dwc);
		dwc3_otg_device_init(dwc);
		dwc3_event_buffers_setup(dwc);
		spin_unlock_irqrestore(&dwc->lock, flags);

		if (dwc->usb2_phy)
			otg_set_vbus(dwc->usb2_phy->otg, false);
		if (dwc->usb2_generic_phy)
			phy_set_mode(dwc->usb2_generic_phy,
				     PHY_MODE_USB_DEVICE);
		ret = dwc3_gadget_init(dwc);
		if (ret)
			dev_err(dwc->dev, "failed to initialize peripheral\n");
		break;
	default:
		break;
	}
}

static void dwc3_drd_update(struct dwc3 *dwc)
{
	int id;

	if (dwc->edev) {
		id = extcon_get_state(dwc->edev, EXTCON_USB_HOST);
		if (id < 0)
			id = 0;
		dwc3_set_mode(dwc, id ?
			      DWC3_GCTL_PRTCAP_HOST :
			      DWC3_GCTL_PRTCAP_DEVICE);
	}
}

static int dwc3_drd_notifier(struct notifier_block *nb,
			     unsigned long event, void *ptr)
{
	struct dwc3 *dwc = container_of(nb, struct dwc3, edev_nb);

	dwc3_set_mode(dwc, event ?
		      DWC3_GCTL_PRTCAP_HOST :
		      DWC3_GCTL_PRTCAP_DEVICE);

	return NOTIFY_DONE;
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
	if (device_property_read_string(dev, "linux,extcon-name", &name) == 0) {
		edev = extcon_get_extcon_dev(name);
		if (!edev)
			return ERR_PTR(-EPROBE_DEFER);

		return edev;
	}

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

#if IS_ENABLED(CONFIG_USB_ROLE_SWITCH)
#define ROLE_SWITCH 1
static int dwc3_usb_role_switch_set(struct usb_role_switch *sw,
				    enum usb_role role)
{
	struct dwc3 *dwc = usb_role_switch_get_drvdata(sw);
	u32 mode;

	switch (role) {
	case USB_ROLE_HOST:
		mode = DWC3_GCTL_PRTCAP_HOST;
		break;
	case USB_ROLE_DEVICE:
		mode = DWC3_GCTL_PRTCAP_DEVICE;
		break;
	default:
		if (dwc->role_switch_default_mode == USB_DR_MODE_HOST)
			mode = DWC3_GCTL_PRTCAP_HOST;
		else
			mode = DWC3_GCTL_PRTCAP_DEVICE;
		break;
	}

	dwc3_set_mode(dwc, mode);
	return 0;
}

static enum usb_role dwc3_usb_role_switch_get(struct usb_role_switch *sw)
{
	struct dwc3 *dwc = usb_role_switch_get_drvdata(sw);
	unsigned long flags;
	enum usb_role role;

	spin_lock_irqsave(&dwc->lock, flags);
	switch (dwc->current_dr_role) {
	case DWC3_GCTL_PRTCAP_HOST:
		role = USB_ROLE_HOST;
		break;
	case DWC3_GCTL_PRTCAP_DEVICE:
		role = USB_ROLE_DEVICE;
		break;
	case DWC3_GCTL_PRTCAP_OTG:
		role = dwc->current_otg_role;
		break;
	default:
		if (dwc->role_switch_default_mode == USB_DR_MODE_HOST)
			role = USB_ROLE_HOST;
		else
			role = USB_ROLE_DEVICE;
		break;
	}
	spin_unlock_irqrestore(&dwc->lock, flags);
	return role;
}

static int dwc3_setup_role_switch(struct dwc3 *dwc)
{
	struct usb_role_switch_desc dwc3_role_switch = {NULL};
	u32 mode;

	dwc->role_switch_default_mode = usb_get_role_switch_default_mode(dwc->dev);
	if (dwc->role_switch_default_mode == USB_DR_MODE_HOST) {
		mode = DWC3_GCTL_PRTCAP_HOST;
	} else {
		dwc->role_switch_default_mode = USB_DR_MODE_PERIPHERAL;
		mode = DWC3_GCTL_PRTCAP_DEVICE;
	}

	dwc3_role_switch.fwnode = dev_fwnode(dwc->dev);
	dwc3_role_switch.set = dwc3_usb_role_switch_set;
	dwc3_role_switch.get = dwc3_usb_role_switch_get;
	dwc3_role_switch.driver_data = dwc;
	dwc->role_sw = usb_role_switch_register(dwc->dev, &dwc3_role_switch);
	if (IS_ERR(dwc->role_sw))
		return PTR_ERR(dwc->role_sw);

	dwc3_set_mode(dwc, mode);
	return 0;
}
#else
#define ROLE_SWITCH 0
#define dwc3_setup_role_switch(x) 0
#endif

int dwc3_drd_init(struct dwc3 *dwc)
{
	int ret, irq;

	if (ROLE_SWITCH &&
	    device_property_read_bool(dwc->dev, "usb-role-switch"))
		return dwc3_setup_role_switch(dwc);

	dwc->edev = dwc3_get_extcon(dwc);
	if (IS_ERR(dwc->edev))
		return PTR_ERR(dwc->edev);

	if (dwc->edev) {
		dwc->edev_nb.notifier_call = dwc3_drd_notifier;
		ret = extcon_register_notifier(dwc->edev, EXTCON_USB_HOST,
					       &dwc->edev_nb);
		if (ret < 0) {
			dev_err(dwc->dev, "couldn't register cable notifier\n");
			return ret;
		}

		dwc3_drd_update(dwc);
	} else {
		dwc3_set_prtcap(dwc, DWC3_GCTL_PRTCAP_OTG);

		/* use OTG block to get ID event */
		irq = dwc3_otg_get_irq(dwc);
		if (irq < 0)
			return irq;

		dwc->otg_irq = irq;

		/* disable all OTG IRQs */
		dwc3_otg_disable_events(dwc, DWC3_OTG_ALL_EVENTS);
		/* clear all events */
		dwc3_otg_clear_events(dwc);

		ret = request_threaded_irq(dwc->otg_irq, dwc3_otg_irq,
					   dwc3_otg_thread_irq,
					   IRQF_SHARED, "dwc3-otg", dwc);
		if (ret) {
			dev_err(dwc->dev, "failed to request irq #%d --> %d\n",
				dwc->otg_irq, ret);
			ret = -ENODEV;
			return ret;
		}

		dwc3_otg_init(dwc);
		dwc3_set_mode(dwc, DWC3_GCTL_PRTCAP_OTG);
	}

	return 0;
}

void dwc3_drd_exit(struct dwc3 *dwc)
{
	unsigned long flags;

	if (dwc->role_sw)
		usb_role_switch_unregister(dwc->role_sw);

	if (dwc->edev)
		extcon_unregister_notifier(dwc->edev, EXTCON_USB_HOST,
					   &dwc->edev_nb);

	cancel_work_sync(&dwc->drd_work);

	/* debug user might have changed role, clean based on current role */
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

	if (dwc->otg_irq)
		free_irq(dwc->otg_irq, dwc);
}
