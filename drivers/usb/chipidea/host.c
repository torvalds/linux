/*
 * host.c - ChipIdea USB host controller driver
 *
 * Copyright (c) 2012 Intel Corporation
 *
 * Author: Alexander Shishkin
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/usb/chipidea.h>
#include <linux/regulator/consumer.h>
#include <linux/imx_gpc.h>

#include "../host/ehci.h"

#include "ci.h"
#include "bits.h"
#include "host.h"

static struct hc_driver __read_mostly ci_ehci_hc_driver;
static int (*orig_bus_suspend)(struct usb_hcd *hcd);
static int (*orig_bus_resume)(struct usb_hcd *hcd);
static int (*orig_hub_control)(struct usb_hcd *hcd,
				u16 typeReq, u16 wValue, u16 wIndex,
				char *buf, u16 wLength);

struct ehci_ci_priv {
	struct regulator *reg_vbus;
};

/* This function is used to override WKCN, WKDN, and WKOC */
static void ci_ehci_override_wakeup_flag(struct ehci_hcd *ehci,
		u32 __iomem *reg, u32 flags, bool set)
{
	u32 val = ehci_readl(ehci, reg);

	if (set)
		val |= flags;
	else
		val &= ~flags;

	ehci_writel(ehci, val, reg);
}

static int ehci_ci_portpower(struct usb_hcd *hcd, int portnum, bool enable)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	struct ehci_ci_priv *priv = (struct ehci_ci_priv *)ehci->priv;
	struct device *dev = hcd->self.controller;
	struct ci_hdrc *ci = dev_get_drvdata(dev);
	int ret = 0;
	int port = HCS_N_PORTS(ehci->hcs_params);

	if (priv->reg_vbus) {
		if (port > 1) {
			dev_warn(dev,
				"Not support multi-port regulator control\n");
			return 0;
		}
		if (enable)
			ret = regulator_enable(priv->reg_vbus);
		else
			ret = regulator_disable(priv->reg_vbus);
		if (ret) {
			dev_err(dev,
				"Failed to %s vbus regulator, ret=%d\n",
				enable ? "enable" : "disable", ret);
			return ret;
		}
	}

	if (enable && (ci->platdata->phy_mode == USBPHY_INTERFACE_MODE_HSIC)) {
		/*
		 * Marvell 28nm HSIC PHY requires forcing the port to HS mode.
		 * As HSIC is always HS, this should be safe for others.
		 */
		hw_port_test_set(ci, 5);
		hw_port_test_set(ci, 0);
	}
	return 0;
};

static int ehci_ci_reset(struct usb_hcd *hcd)
{
	struct device *dev = hcd->self.controller;
	struct ci_hdrc *ci = dev_get_drvdata(dev);
	int ret;

	ret = ehci_setup(hcd);
	if (ret)
		return ret;

	ci_platform_configure(ci);

	return ret;
}

static const struct ehci_driver_overrides ehci_ci_overrides = {
	.extra_priv_size = sizeof(struct ehci_ci_priv),
	.port_power	 = ehci_ci_portpower,
	.reset		 = ehci_ci_reset,
};

static int ci_imx_ehci_bus_resume(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int port;

	int ret = orig_bus_resume(hcd);

	if (ret)
		return ret;

	port = HCS_N_PORTS(ehci->hcs_params);
	while (port--) {
		u32 __iomem *reg = &ehci->regs->port_status[port];
		u32 portsc = ehci_readl(ehci, reg);
		/*
		 * Notify PHY after resume signal has finished, it is
		 * for global suspend case.
		 */
		if (hcd->usb_phy
			&& test_bit(port, &ehci->bus_suspended)
			&& (portsc & PORT_CONNECT)
			&& (ehci_port_speed(ehci, portsc) ==
				USB_PORT_STAT_HIGH_SPEED))
			/* notify the USB PHY */
			usb_phy_notify_resume(hcd->usb_phy, USB_SPEED_HIGH);
	}

	return 0;
}

#ifdef CONFIG_USB_OTG

static int ci_start_port_reset(struct usb_hcd *hcd, unsigned port)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	u32 __iomem *reg;
	u32 status;

	if (!port)
		return -EINVAL;
	port--;
	/* start port reset before HNP protocol time out */
	reg = &ehci->regs->port_status[port];
	status = ehci_readl(ehci, reg);
	if (!(status & PORT_CONNECT))
		return -ENODEV;

	/* khubd will finish the reset later */
	if (ehci_is_TDI(ehci))
		ehci_writel(ehci, status | (PORT_RESET & ~PORT_RWC_BITS), reg);
	else
		ehci_writel(ehci, status | PORT_RESET, reg);

	return 0;
}

#else

#define ci_start_port_reset    NULL

#endif

/* The below code is based on tegra ehci driver */
static int ci_imx_ehci_hub_control(
	struct usb_hcd	*hcd,
	u16		typeReq,
	u16		wValue,
	u16		wIndex,
	char		*buf,
	u16		wLength
)
{
	struct ehci_hcd	*ehci = hcd_to_ehci(hcd);
	u32 __iomem	*status_reg;
	u32		temp;
	unsigned long	flags;
	int		retval = 0;
	struct device *dev = hcd->self.controller;
	struct ci_hdrc *ci = dev_get_drvdata(dev);

	status_reg = &ehci->regs->port_status[(wIndex & 0xff) - 1];

	spin_lock_irqsave(&ehci->lock, flags);

	if (typeReq == SetPortFeature && wValue == USB_PORT_FEAT_SUSPEND) {
		temp = ehci_readl(ehci, status_reg);
		if ((temp & PORT_PE) == 0 || (temp & PORT_RESET) != 0) {
			retval = -EPIPE;
			goto done;
		}

		temp &= ~(PORT_RWC_BITS | PORT_WKCONN_E);
		temp |= PORT_WKDISC_E | PORT_WKOC_E;
		ehci_writel(ehci, temp | PORT_SUSPEND, status_reg);

		/*
		 * If a transaction is in progress, there may be a delay in
		 * suspending the port. Poll until the port is suspended.
		 */
		if (ehci_handshake(ehci, status_reg, PORT_SUSPEND,
						PORT_SUSPEND, 5000))
			ehci_err(ehci, "timeout waiting for SUSPEND\n");

		if (ci->platdata->flags & CI_HDRC_IMX_IS_HSIC) {
			if (ci->platdata->notify_event)
				ci->platdata->notify_event
					(ci, CI_HDRC_IMX_HSIC_SUSPEND_EVENT);
			ci_ehci_override_wakeup_flag(ehci, status_reg,
				PORT_WKDISC_E | PORT_WKCONN_E, false);
		}

		spin_unlock_irqrestore(&ehci->lock, flags);
		if (ehci_port_speed(ehci, temp) ==
				USB_PORT_STAT_HIGH_SPEED && hcd->usb_phy) {
			/* notify the USB PHY */
			usb_phy_notify_suspend(hcd->usb_phy, USB_SPEED_HIGH);
		}
		spin_lock_irqsave(&ehci->lock, flags);

		set_bit((wIndex & 0xff) - 1, &ehci->suspended_ports);
		goto done;
	}

	/*
	 * After resume has finished, it needs do some post resume
	 * operation for some SoCs.
	 */
	else if (typeReq == ClearPortFeature &&
					wValue == USB_PORT_FEAT_C_SUSPEND) {

		/* Make sure the resume has finished, it should be finished */
		if (ehci_handshake(ehci, status_reg, PORT_RESUME, 0, 25000))
			ehci_err(ehci, "timeout waiting for resume\n");

		temp = ehci_readl(ehci, status_reg);

		if (ehci_port_speed(ehci, temp) ==
				USB_PORT_STAT_HIGH_SPEED && hcd->usb_phy) {
			/* notify the USB PHY */
			usb_phy_notify_resume(hcd->usb_phy, USB_SPEED_HIGH);
		}
	}

	spin_unlock_irqrestore(&ehci->lock, flags);

	/* Handle the hub control events here */
	return orig_hub_control(hcd, typeReq, wValue, wIndex, buf, wLength);
done:
	spin_unlock_irqrestore(&ehci->lock, flags);
	return retval;
}

static irqreturn_t host_irq(struct ci_hdrc *ci)
{
	if (ci->hcd)
		return usb_hcd_irq(ci->irq, ci->hcd);
	else
		return IRQ_NONE;
}

static int host_start(struct ci_hdrc *ci)
{
	struct usb_hcd *hcd;
	struct ehci_hcd *ehci;
	struct ehci_ci_priv *priv;
	int ret;

	if (usb_disabled())
		return -ENODEV;

	hcd = usb_create_hcd(&ci_ehci_hc_driver, ci->dev, dev_name(ci->dev));
	if (!hcd)
		return -ENOMEM;

	dev_set_drvdata(ci->dev, ci);
	hcd->rsrc_start = ci->hw_bank.phys;
	hcd->rsrc_len = ci->hw_bank.size;
	hcd->regs = ci->hw_bank.abs;
	hcd->has_tt = 1;

	hcd->power_budget = ci->platdata->power_budget;
	hcd->tpl_support = ci->platdata->tpl_support;
	if (ci->phy)
		hcd->phy = ci->phy;
	else
		hcd->usb_phy = ci->usb_phy;

	ehci = hcd_to_ehci(hcd);
	ehci->caps = ci->hw_bank.cap;
	ehci->has_hostpc = ci->hw_bank.lpm;
	ehci->has_tdi_phy_lpm = ci->hw_bank.lpm;
	ehci->imx28_write_fix = ci->imx28_write_fix;

	priv = (struct ehci_ci_priv *)ehci->priv;
	priv->reg_vbus = NULL;

	if (ci->platdata->reg_vbus && !ci_otg_is_fsm_mode(ci)) {
		if (ci->platdata->flags & CI_HDRC_TURN_VBUS_EARLY_ON) {
			ret = regulator_enable(ci->platdata->reg_vbus);
			if (ret) {
				dev_err(ci->dev,
				"Failed to enable vbus regulator, ret=%d\n",
									ret);
				goto put_hcd;
			}
		} else {
			priv->reg_vbus = ci->platdata->reg_vbus;
		}
	}

	if (ci_otg_is_fsm_mode(ci)) {
		if (ci->fsm.id && ci->fsm.otg->state <= OTG_STATE_B_HOST)
			hcd->self.is_b_host = 1;
		else
			hcd->self.is_b_host = 0;
	}

	ret = usb_add_hcd(hcd, 0, 0);
	if (ret) {
		goto disable_reg;
	} else {
		struct usb_otg *otg = &ci->otg;

		ci->hcd = hcd;

		if (ci_otg_is_fsm_mode(ci)) {
			hcd->self.otg_fsm = &ci->fsm;
			otg->host = &hcd->self;
			hcd->self.otg_port = 1;
		}
	}

	if (ci->platdata->notify_event &&
		(ci->platdata->flags & CI_HDRC_IMX_IS_HSIC))
		ci->platdata->notify_event
			(ci, CI_HDRC_IMX_HSIC_ACTIVE_EVENT);

	return ret;

disable_reg:
	if (ci->platdata->reg_vbus && !ci_otg_is_fsm_mode(ci) &&
			(ci->platdata->flags & CI_HDRC_TURN_VBUS_EARLY_ON))
		regulator_disable(ci->platdata->reg_vbus);
put_hcd:
	usb_put_hcd(hcd);

	return ret;
}

static void host_stop(struct ci_hdrc *ci)
{
	struct usb_hcd *hcd = ci->hcd;

	if (hcd) {
		usb_remove_hcd(hcd);
		usb_put_hcd(hcd);
		if (ci->platdata->reg_vbus && !ci_otg_is_fsm_mode(ci) &&
			(ci->platdata->flags & CI_HDRC_TURN_VBUS_EARLY_ON))
				regulator_disable(ci->platdata->reg_vbus);
		if (hcd->self.is_b_host)
			hcd->self.is_b_host = 0;
	}
	ci->hcd = NULL;
}

bool ci_hdrc_host_has_device(struct ci_hdrc *ci)
{
	struct usb_device *roothub;
	int i;

	if ((ci->role == CI_ROLE_HOST) && ci->hcd) {
		roothub = ci->hcd->self.root_hub;
		for (i = 0; i < roothub->maxchild; ++i) {
			if (usb_hub_find_child(roothub, (i + 1)))
				return true;
		}
	}
	return false;
}

static void ci_hdrc_host_save_for_power_lost(struct ci_hdrc *ci)
{
	struct ehci_hcd *ehci;

	if (!ci->hcd)
		return;

	ehci = hcd_to_ehci(ci->hcd);
	/* save EHCI registers */
	ci->pm_usbmode = ehci_readl(ehci, &ehci->regs->usbmode);
	ci->pm_command = ehci_readl(ehci, &ehci->regs->command);
	ci->pm_command &= ~CMD_RUN;
	ci->pm_status  = ehci_readl(ehci, &ehci->regs->status);
	ci->pm_intr_enable  = ehci_readl(ehci, &ehci->regs->intr_enable);
	ci->pm_frame_index  = ehci_readl(ehci, &ehci->regs->frame_index);
	ci->pm_segment  = ehci_readl(ehci, &ehci->regs->segment);
	ci->pm_frame_list  = ehci_readl(ehci, &ehci->regs->frame_list);
	ci->pm_async_next  = ehci_readl(ehci, &ehci->regs->async_next);
	ci->pm_configured_flag  =
			ehci_readl(ehci, &ehci->regs->configured_flag);
	ci->pm_portsc = ehci_readl(ehci, &ehci->regs->port_status[0]);
}

static void ci_hdrc_host_restore_from_power_lost(struct ci_hdrc *ci)
{
	struct ehci_hcd *ehci;
	unsigned long   flags;
	u32 tmp;

	if (!ci->hcd)
		return;

	hw_controller_reset(ci);

	ehci = hcd_to_ehci(ci->hcd);
	spin_lock_irqsave(&ehci->lock, flags);
	/* Restore EHCI registers */
	ehci_writel(ehci, ci->pm_usbmode, &ehci->regs->usbmode);
	ehci_writel(ehci, ci->pm_portsc, &ehci->regs->port_status[0]);
	ehci_writel(ehci, ci->pm_command, &ehci->regs->command);
	ehci_writel(ehci, ci->pm_intr_enable, &ehci->regs->intr_enable);
	ehci_writel(ehci, ci->pm_frame_index, &ehci->regs->frame_index);
	ehci_writel(ehci, ci->pm_segment, &ehci->regs->segment);
	ehci_writel(ehci, ci->pm_frame_list, &ehci->regs->frame_list);
	ehci_writel(ehci, ci->pm_async_next, &ehci->regs->async_next);
	ehci_writel(ehci, ci->pm_configured_flag,
					&ehci->regs->configured_flag);
	/* Restore the PHY's connect notifier setting */
	if (ci->pm_portsc & PORTSC_HSP)
		usb_phy_notify_connect(ci->usb_phy, USB_SPEED_HIGH);

	tmp = ehci_readl(ehci, &ehci->regs->command);
	tmp |= CMD_RUN;
	ehci_writel(ehci, tmp, &ehci->regs->command);
	spin_unlock_irqrestore(&ehci->lock, flags);
}

static void ci_hdrc_host_suspend(struct ci_hdrc *ci)
{
	if (ci_hdrc_host_has_device(ci))
		imx_gpc_mf_request_on(ci->irq, 1);

	ci_hdrc_host_save_for_power_lost(ci);
}

static void ci_hdrc_host_resume(struct ci_hdrc *ci, bool power_lost)
{
	imx_gpc_mf_request_on(ci->irq, 0);

	if (power_lost)
		ci_hdrc_host_restore_from_power_lost(ci);
}

void ci_hdrc_host_destroy(struct ci_hdrc *ci)
{
	if (ci->role == CI_ROLE_HOST && ci->hcd) {
		disable_irq_nosync(ci->irq);
		host_stop(ci);
		enable_irq(ci->irq);
	}
}

static int ci_ehci_bus_suspend(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int port;
	u32 tmp;
	struct device *dev = hcd->self.controller;
	struct ci_hdrc *ci = dev_get_drvdata(dev);

	int ret = orig_bus_suspend(hcd);

	if (ret)
		return ret;

	port = HCS_N_PORTS(ehci->hcs_params);
	while (port--) {
		u32 __iomem *reg = &ehci->regs->port_status[port];
		u32 portsc = ehci_readl(ehci, reg);

		if (portsc & PORT_CONNECT) {
			/*
			 * For chipidea, the resume signal will be ended
			 * automatically, so for remote wakeup case, the
			 * usbcmd.rs may not be set before the resume has
			 * ended if other resume paths consumes too much
			 * time (~24ms), in that case, the SOF will not
			 * send out within 3ms after resume ends, then the
			 * high speed device will enter full speed mode.
			 */

			tmp = ehci_readl(ehci, &ehci->regs->command);
			tmp |= CMD_RUN;
			ehci_writel(ehci, tmp, &ehci->regs->command);
			/*
			 * It needs a short delay between set RS bit and PHCD.
			 */
			usleep_range(150, 200);

			/*
			 * If a transaction is in progress, there may be
			 * a delay in suspending the port. Poll until the
			 * port is suspended.
			 */
			if (test_bit(port, &ehci->bus_suspended) &&
					ehci_handshake(ehci, reg, PORT_SUSPEND,
							PORT_SUSPEND, 5000))
				ehci_err(ehci, "timeout waiting for SUSPEND\n");

			if (ci->platdata->flags & CI_HDRC_IMX_IS_HSIC)
				ci_ehci_override_wakeup_flag(ehci, reg,
					PORT_WKDISC_E | PORT_WKCONN_E, false);

			if (hcd->usb_phy && test_bit(port, &ehci->bus_suspended)
				&& (ehci_port_speed(ehci, portsc) ==
					USB_PORT_STAT_HIGH_SPEED))
				/*
				 * notify the USB PHY, it is for global
				 * suspend case.
				 */
				usb_phy_notify_suspend(hcd->usb_phy,
					USB_SPEED_HIGH);
			break;
		}
	}

	return 0;
}

int ci_hdrc_host_init(struct ci_hdrc *ci)
{
	struct ci_role_driver *rdrv;

	if (!hw_read(ci, CAP_DCCPARAMS, DCCPARAMS_HC))
		return -ENXIO;

	rdrv = devm_kzalloc(ci->dev, sizeof(struct ci_role_driver), GFP_KERNEL);
	if (!rdrv)
		return -ENOMEM;

	rdrv->start	= host_start;
	rdrv->stop	= host_stop;
	rdrv->irq	= host_irq;
	rdrv->suspend	= ci_hdrc_host_suspend;
	rdrv->resume	= ci_hdrc_host_resume;
	rdrv->name	= "host";
	ci->roles[CI_ROLE_HOST] = rdrv;

	return 0;
}

void ci_hdrc_host_driver_init(void)
{
	ehci_init_driver(&ci_ehci_hc_driver, &ehci_ci_overrides);
	orig_bus_suspend = ci_ehci_hc_driver.bus_suspend;
	orig_bus_resume = ci_ehci_hc_driver.bus_resume;
	orig_hub_control = ci_ehci_hc_driver.hub_control;

	ci_ehci_hc_driver.bus_suspend = ci_ehci_bus_suspend;
	ci_ehci_hc_driver.bus_resume = ci_imx_ehci_bus_resume;
	ci_ehci_hc_driver.hub_control = ci_imx_ehci_hub_control;
	ci_ehci_hc_driver.start_port_reset = ci_start_port_reset;
}
