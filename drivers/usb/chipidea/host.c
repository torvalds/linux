// SPDX-License-Identifier: GPL-2.0
/*
 * host.c - ChipIdea USB host controller driver
 *
 * Copyright (c) 2012 Intel Corporation
 *
 * Author: Alexander Shishkin
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/usb/chipidea.h>
#include <linux/regulator/consumer.h>
#include <linux/pinctrl/consumer.h>

#include "../host/ehci.h"

#include "ci.h"
#include "bits.h"
#include "host.h"

static struct hc_driver __read_mostly ci_ehci_hc_driver;
static int (*orig_bus_suspend)(struct usb_hcd *hcd);

struct ehci_ci_priv {
	struct regulator *reg_vbus;
	bool enabled;
};

struct ci_hdrc_dma_aligned_buffer {
	void *original_buffer;
	u8 data[];
};

static int ehci_ci_portpower(struct usb_hcd *hcd, int portnum, bool enable)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	struct ehci_ci_priv *priv = (struct ehci_ci_priv *)ehci->priv;
	struct device *dev = hcd->self.controller;
	struct ci_hdrc *ci = dev_get_drvdata(dev);
	int ret = 0;
	int port = HCS_N_PORTS(ehci->hcs_params);

	if (priv->reg_vbus && enable != priv->enabled) {
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
		priv->enabled = enable;
	}

	if (ci->platdata->flags & CI_HDRC_PHY_VBUS_CONTROL) {
		if (enable)
			usb_phy_vbus_on(ci->usb_phy);
		else
			usb_phy_vbus_off(ci->usb_phy);
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
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int ret;

	ret = ehci_setup(hcd);
	if (ret)
		return ret;

	ehci->need_io_watchdog = 0;

	if (ci->platdata->notify_event) {
		ret = ci->platdata->notify_event(ci,
				CI_HDRC_CONTROLLER_RESET_EVENT);
		if (ret)
			return ret;
	}

	ci_platform_configure(ci);

	return ret;
}

static const struct ehci_driver_overrides ehci_ci_overrides = {
	.extra_priv_size = sizeof(struct ehci_ci_priv),
	.port_power	 = ehci_ci_portpower,
	.reset		 = ehci_ci_reset,
};

static irqreturn_t host_irq(struct ci_hdrc *ci)
{
	return usb_hcd_irq(ci->irq, ci->hcd);
}

static int host_start(struct ci_hdrc *ci)
{
	struct usb_hcd *hcd;
	struct ehci_hcd *ehci;
	struct ehci_ci_priv *priv;
	int ret;

	if (usb_disabled())
		return -ENODEV;

	hcd = __usb_create_hcd(&ci_ehci_hc_driver, ci->dev->parent,
			       ci->dev, dev_name(ci->dev), NULL);
	if (!hcd)
		return -ENOMEM;

	dev_set_drvdata(ci->dev, ci);
	hcd->rsrc_start = ci->hw_bank.phys;
	hcd->rsrc_len = ci->hw_bank.size;
	hcd->regs = ci->hw_bank.abs;
	hcd->has_tt = 1;

	hcd->power_budget = ci->platdata->power_budget;
	hcd->tpl_support = ci->platdata->tpl_support;
	if (ci->phy || ci->usb_phy) {
		hcd->skip_phy_initialization = 1;
		if (ci->usb_phy)
			hcd->usb_phy = ci->usb_phy;
	}

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

	if (ci->platdata->pins_host)
		pinctrl_select_state(ci->platdata->pctl,
				     ci->platdata->pins_host);

	ci->hcd = hcd;

	ret = usb_add_hcd(hcd, 0, 0);
	if (ret) {
		ci->hcd = NULL;
		goto disable_reg;
	} else {
		struct usb_otg *otg = &ci->otg;

		if (ci_otg_is_fsm_mode(ci)) {
			otg->host = &hcd->self;
			hcd->self.otg_port = 1;
		}

		if (ci->platdata->notify_event &&
			(ci->platdata->flags & CI_HDRC_IMX_IS_HSIC))
			ci->platdata->notify_event
				(ci, CI_HDRC_IMX_HSIC_ACTIVE_EVENT);
	}

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
		if (ci->platdata->notify_event)
			ci->platdata->notify_event(ci,
				CI_HDRC_CONTROLLER_STOPPED_EVENT);
		usb_remove_hcd(hcd);
		ci->role = CI_ROLE_END;
		synchronize_irq(ci->irq);
		usb_put_hcd(hcd);
		if (ci->platdata->reg_vbus && !ci_otg_is_fsm_mode(ci) &&
			(ci->platdata->flags & CI_HDRC_TURN_VBUS_EARLY_ON))
				regulator_disable(ci->platdata->reg_vbus);
	}
	ci->hcd = NULL;
	ci->otg.host = NULL;

	if (ci->platdata->pins_host && ci->platdata->pins_default)
		pinctrl_select_state(ci->platdata->pctl,
				     ci->platdata->pins_default);
}


void ci_hdrc_host_destroy(struct ci_hdrc *ci)
{
	if (ci->role == CI_ROLE_HOST && ci->hcd)
		host_stop(ci);
}

/* The below code is based on tegra ehci driver */
static int ci_ehci_hub_control(
	struct usb_hcd	*hcd,
	u16		typeReq,
	u16		wValue,
	u16		wIndex,
	char		*buf,
	u16		wLength
)
{
	struct ehci_hcd	*ehci = hcd_to_ehci(hcd);
	unsigned int	ports = HCS_N_PORTS(ehci->hcs_params);
	u32 __iomem	*status_reg;
	u32		temp, port_index;
	unsigned long	flags;
	int		retval = 0;
	bool		done = false;
	struct device *dev = hcd->self.controller;
	struct ci_hdrc *ci = dev_get_drvdata(dev);

	port_index = wIndex & 0xff;
	port_index -= (port_index > 0);
	status_reg = &ehci->regs->port_status[port_index];

	spin_lock_irqsave(&ehci->lock, flags);

	if (ci->platdata->hub_control) {
		retval = ci->platdata->hub_control(ci, typeReq, wValue, wIndex,
						   buf, wLength, &done, &flags);
		if (done)
			goto done;
	}

	if (typeReq == SetPortFeature && wValue == USB_PORT_FEAT_SUSPEND) {
		if (!wIndex || wIndex > ports) {
			retval = -EPIPE;
			goto done;
		}

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
				ci->platdata->notify_event(ci,
					CI_HDRC_IMX_HSIC_SUSPEND_EVENT);

			temp = ehci_readl(ehci, status_reg);
			temp &= ~(PORT_WKDISC_E | PORT_WKCONN_E);
			ehci_writel(ehci, temp, status_reg);
		}

		set_bit(port_index, &ehci->suspended_ports);
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
	}

	spin_unlock_irqrestore(&ehci->lock, flags);

	/* Handle the hub control events here */
	return ehci_hub_control(hcd, typeReq, wValue, wIndex, buf, wLength);
done:
	spin_unlock_irqrestore(&ehci->lock, flags);
	return retval;
}
static int ci_ehci_bus_suspend(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	struct device *dev = hcd->self.controller;
	struct ci_hdrc *ci = dev_get_drvdata(dev);
	int port;
	u32 tmp;

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
			 * Need to clear WKCN and WKOC for imx HSIC,
			 * otherwise, there will be wakeup event.
			 */
			if (ci->platdata->flags & CI_HDRC_IMX_IS_HSIC) {
				tmp = ehci_readl(ehci, reg);
				tmp &= ~(PORT_WKDISC_E | PORT_WKCONN_E);
				ehci_writel(ehci, tmp, reg);
			}

			break;
		}
	}

	return 0;
}

static void ci_hdrc_free_dma_aligned_buffer(struct urb *urb, bool copy_back)
{
	struct ci_hdrc_dma_aligned_buffer *temp;

	if (!(urb->transfer_flags & URB_ALIGNED_TEMP_BUFFER))
		return;
	urb->transfer_flags &= ~URB_ALIGNED_TEMP_BUFFER;

	temp = container_of(urb->transfer_buffer,
			    struct ci_hdrc_dma_aligned_buffer, data);
	urb->transfer_buffer = temp->original_buffer;

	if (copy_back && usb_urb_dir_in(urb)) {
		size_t length;

		if (usb_pipeisoc(urb->pipe))
			length = urb->transfer_buffer_length;
		else
			length = urb->actual_length;

		memcpy(temp->original_buffer, temp->data, length);
	}

	kfree(temp);
}

static int ci_hdrc_alloc_dma_aligned_buffer(struct urb *urb, gfp_t mem_flags)
{
	struct ci_hdrc_dma_aligned_buffer *temp;

	if (urb->num_sgs || urb->sg || urb->transfer_buffer_length == 0)
		return 0;
	if (IS_ALIGNED((uintptr_t)urb->transfer_buffer, 4)
	    && IS_ALIGNED(urb->transfer_buffer_length, 4))
		return 0;

	temp = kmalloc(sizeof(*temp) + ALIGN(urb->transfer_buffer_length, 4), mem_flags);
	if (!temp)
		return -ENOMEM;

	if (usb_urb_dir_out(urb))
		memcpy(temp->data, urb->transfer_buffer,
		       urb->transfer_buffer_length);

	temp->original_buffer = urb->transfer_buffer;
	urb->transfer_buffer = temp->data;
	urb->transfer_flags |= URB_ALIGNED_TEMP_BUFFER;

	return 0;
}

static int ci_hdrc_map_urb_for_dma(struct usb_hcd *hcd, struct urb *urb,
				   gfp_t mem_flags)
{
	int ret;

	ret = ci_hdrc_alloc_dma_aligned_buffer(urb, mem_flags);
	if (ret)
		return ret;

	ret = usb_hcd_map_urb_for_dma(hcd, urb, mem_flags);
	if (ret)
		ci_hdrc_free_dma_aligned_buffer(urb, false);

	return ret;
}

static void ci_hdrc_unmap_urb_for_dma(struct usb_hcd *hcd, struct urb *urb)
{
	usb_hcd_unmap_urb_for_dma(hcd, urb);
	ci_hdrc_free_dma_aligned_buffer(urb, true);
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
	rdrv->name	= "host";
	ci->roles[CI_ROLE_HOST] = rdrv;

	if (ci->platdata->flags & CI_HDRC_REQUIRES_ALIGNED_DMA) {
		ci_ehci_hc_driver.map_urb_for_dma = ci_hdrc_map_urb_for_dma;
		ci_ehci_hc_driver.unmap_urb_for_dma = ci_hdrc_unmap_urb_for_dma;
	}

	return 0;
}

void ci_hdrc_host_driver_init(void)
{
	ehci_init_driver(&ci_ehci_hc_driver, &ehci_ci_overrides);
	orig_bus_suspend = ci_ehci_hc_driver.bus_suspend;
	ci_ehci_hc_driver.bus_suspend = ci_ehci_bus_suspend;
	ci_ehci_hc_driver.hub_control = ci_ehci_hub_control;
}
