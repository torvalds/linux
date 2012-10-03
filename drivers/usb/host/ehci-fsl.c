/*
 * Copyright 2005-2009 MontaVista Software, Inc.
 * Copyright 2008,2012      Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Ported to 834x by Randy Vinson <rvinson@mvista.com> using code provided
 * by Hunter Wu.
 * Power Management support by Dave Liu <daveliu@freescale.com>,
 * Jerry Huang <Chang-Ming.Huang@freescale.com> and
 * Anton Vorontsov <avorontsov@ru.mvista.com>.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/fsl_devices.h>

#include "ehci-fsl.h"

/* configure so an HC device and id are always provided */
/* always called with process context; sleeping is OK */

/**
 * usb_hcd_fsl_probe - initialize FSL-based HCDs
 * @drvier: Driver to be used for this HCD
 * @pdev: USB Host Controller being probed
 * Context: !in_interrupt()
 *
 * Allocates basic resources for this USB host controller.
 *
 */
static int usb_hcd_fsl_probe(const struct hc_driver *driver,
			     struct platform_device *pdev)
{
	struct fsl_usb2_platform_data *pdata;
	struct usb_hcd *hcd;
	struct resource *res;
	int irq;
	int retval;

	pr_debug("initializing FSL-SOC USB Controller\n");

	/* Need platform data for setup */
	pdata = (struct fsl_usb2_platform_data *)pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev,
			"No platform data for %s.\n", dev_name(&pdev->dev));
		return -ENODEV;
	}

	/*
	 * This is a host mode driver, verify that we're supposed to be
	 * in host mode.
	 */
	if (!((pdata->operating_mode == FSL_USB2_DR_HOST) ||
	      (pdata->operating_mode == FSL_USB2_MPH_HOST) ||
	      (pdata->operating_mode == FSL_USB2_DR_OTG))) {
		dev_err(&pdev->dev,
			"Non Host Mode configured for %s. Wrong driver linked.\n",
			dev_name(&pdev->dev));
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev,
			"Found HC with no IRQ. Check %s setup!\n",
			dev_name(&pdev->dev));
		return -ENODEV;
	}
	irq = res->start;

	hcd = usb_create_hcd(driver, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		retval = -ENOMEM;
		goto err1;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev,
			"Found HC with no register addr. Check %s setup!\n",
			dev_name(&pdev->dev));
		retval = -ENODEV;
		goto err2;
	}
	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len,
				driver->description)) {
		dev_dbg(&pdev->dev, "controller already in use\n");
		retval = -EBUSY;
		goto err2;
	}
	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);

	if (hcd->regs == NULL) {
		dev_dbg(&pdev->dev, "error mapping memory\n");
		retval = -EFAULT;
		goto err3;
	}

	pdata->regs = hcd->regs;

	if (pdata->power_budget)
		hcd->power_budget = pdata->power_budget;

	/*
	 * do platform specific init: check the clock, grab/config pins, etc.
	 */
	if (pdata->init && pdata->init(pdev)) {
		retval = -ENODEV;
		goto err4;
	}

	/* Enable USB controller, 83xx or 8536 */
	if (pdata->have_sysif_regs)
		setbits32(hcd->regs + FSL_SOC_USB_CTRL, 0x4);

	/* Don't need to set host mode here. It will be done by tdi_reset() */

	retval = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (retval != 0)
		goto err4;

#ifdef CONFIG_USB_OTG
	if (pdata->operating_mode == FSL_USB2_DR_OTG) {
		struct ehci_hcd *ehci = hcd_to_ehci(hcd);

		hcd->phy = usb_get_phy(USB_PHY_TYPE_USB2);
		dev_dbg(&pdev->dev, "hcd=0x%p  ehci=0x%p, phy=0x%p\n",
			hcd, ehci, hcd->phy);

		if (!IS_ERR_OR_NULL(hcd->phy)) {
			retval = otg_set_host(hcd->phy->otg,
					      &ehci_to_hcd(ehci)->self);
			if (retval) {
				usb_put_phy(hcd->phy);
				goto err4;
			}
		} else {
			dev_err(&pdev->dev, "can't find phy\n");
			retval = -ENODEV;
			goto err4;
		}
	}
#endif
	return retval;

      err4:
	iounmap(hcd->regs);
      err3:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
      err2:
	usb_put_hcd(hcd);
      err1:
	dev_err(&pdev->dev, "init %s fail, %d\n", dev_name(&pdev->dev), retval);
	if (pdata->exit)
		pdata->exit(pdev);
	return retval;
}

/* may be called without controller electrically present */
/* may be called with controller, bus, and devices active */

/**
 * usb_hcd_fsl_remove - shutdown processing for FSL-based HCDs
 * @dev: USB Host Controller being removed
 * Context: !in_interrupt()
 *
 * Reverses the effect of usb_hcd_fsl_probe().
 *
 */
static void usb_hcd_fsl_remove(struct usb_hcd *hcd,
			       struct platform_device *pdev)
{
	struct fsl_usb2_platform_data *pdata = pdev->dev.platform_data;

	if (!IS_ERR_OR_NULL(hcd->phy)) {
		otg_set_host(hcd->phy->otg, NULL);
		usb_put_phy(hcd->phy);
	}

	usb_remove_hcd(hcd);

	/*
	 * do platform specific un-initialization:
	 * release iomux pins, disable clock, etc.
	 */
	if (pdata->exit)
		pdata->exit(pdev);
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);
}

static int ehci_fsl_setup_phy(struct usb_hcd *hcd,
			       enum fsl_usb2_phy_modes phy_mode,
			       unsigned int port_offset)
{
	u32 portsc;
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	void __iomem *non_ehci = hcd->regs;
	struct device *dev = hcd->self.controller;
	struct fsl_usb2_platform_data *pdata = dev->platform_data;

	if (pdata->controller_ver < 0) {
		dev_warn(hcd->self.controller, "Could not get controller version\n");
		return;
	}

	portsc = ehci_readl(ehci, &ehci->regs->port_status[port_offset]);
	portsc &= ~(PORT_PTS_MSK | PORT_PTS_PTW);

	switch (phy_mode) {
	case FSL_USB2_PHY_ULPI:
		if (pdata->controller_ver) {
			/* controller version 1.6 or above */
			setbits32(non_ehci + FSL_SOC_USB_CTRL,
					ULPI_PHY_CLK_SEL);
			/*
			 * Due to controller issue of PHY_CLK_VALID in ULPI
			 * mode, we set USB_CTRL_USB_EN before checking
			 * PHY_CLK_VALID, otherwise PHY_CLK_VALID doesn't work.
			 */
			clrsetbits_be32(non_ehci + FSL_SOC_USB_CTRL,
					UTMI_PHY_EN, USB_CTRL_USB_EN);
		}
		portsc |= PORT_PTS_ULPI;
		break;
	case FSL_USB2_PHY_SERIAL:
		portsc |= PORT_PTS_SERIAL;
		break;
	case FSL_USB2_PHY_UTMI_WIDE:
		portsc |= PORT_PTS_PTW;
		/* fall through */
	case FSL_USB2_PHY_UTMI:
		if (pdata->controller_ver) {
			/* controller version 1.6 or above */
			setbits32(non_ehci + FSL_SOC_USB_CTRL, UTMI_PHY_EN);
			mdelay(FSL_UTMI_PHY_DLY);  /* Delay for UTMI PHY CLK to
						become stable - 10ms*/
		}
		/* enable UTMI PHY */
		if (pdata->have_sysif_regs)
			setbits32(non_ehci + FSL_SOC_USB_CTRL,
				  CTRL_UTMI_PHY_EN);
		portsc |= PORT_PTS_UTMI;
		break;
	case FSL_USB2_PHY_NONE:
		break;
	}

	if (pdata->controller_ver && (phy_mode == FSL_USB2_PHY_ULPI)) {
		/* check PHY_CLK_VALID to get phy clk valid */
		if (!spin_event_timeout(in_be32(non_ehci + FSL_SOC_USB_CTRL) &
				PHY_CLK_VALID, FSL_USB_PHY_CLK_TIMEOUT, 0)) {
			printk(KERN_WARNING "fsl-ehci: USB PHY clock invalid\n");
			return -EINVAL;
		}
	}

	ehci_writel(ehci, portsc, &ehci->regs->port_status[port_offset]);

	if (phy_mode != FSL_USB2_PHY_ULPI)
		setbits32(non_ehci + FSL_SOC_USB_CTRL, USB_CTRL_USB_EN);

	return 0;
}

static int ehci_fsl_usb_setup(struct ehci_hcd *ehci)
{
	struct usb_hcd *hcd = ehci_to_hcd(ehci);
	struct fsl_usb2_platform_data *pdata;
	void __iomem *non_ehci = hcd->regs;

	pdata = hcd->self.controller->platform_data;

	if (pdata->have_sysif_regs) {
		/*
		* Turn on cache snooping hardware, since some PowerPC platforms
		* wholly rely on hardware to deal with cache coherent
		*/

		/* Setup Snooping for all the 4GB space */
		/* SNOOP1 starts from 0x0, size 2G */
		out_be32(non_ehci + FSL_SOC_USB_SNOOP1, 0x0 | SNOOP_SIZE_2GB);
		/* SNOOP2 starts from 0x80000000, size 2G */
		out_be32(non_ehci + FSL_SOC_USB_SNOOP2, 0x80000000 | SNOOP_SIZE_2GB);
	}

	if ((pdata->operating_mode == FSL_USB2_DR_HOST) ||
			(pdata->operating_mode == FSL_USB2_DR_OTG))
		if (ehci_fsl_setup_phy(hcd, pdata->phy_mode, 0))
			return -EINVAL;

	if (pdata->operating_mode == FSL_USB2_MPH_HOST) {
		unsigned int chip, rev, svr;

		svr = mfspr(SPRN_SVR);
		chip = svr >> 16;
		rev = (svr >> 4) & 0xf;

		/* Deal with USB Erratum #14 on MPC834x Rev 1.0 & 1.1 chips */
		if ((rev == 1) && (chip >= 0x8050) && (chip <= 0x8055))
			ehci->has_fsl_port_bug = 1;

		if (pdata->port_enables & FSL_USB2_PORT0_ENABLED)
			if (ehci_fsl_setup_phy(hcd, pdata->phy_mode, 0))
				return -EINVAL;

		if (pdata->port_enables & FSL_USB2_PORT1_ENABLED)
			if (ehci_fsl_setup_phy(hcd, pdata->phy_mode, 1))
				return -EINVAL;
	}

	if (pdata->have_sysif_regs) {
#ifdef CONFIG_FSL_SOC_BOOKE
		out_be32(non_ehci + FSL_SOC_USB_PRICTRL, 0x00000008);
		out_be32(non_ehci + FSL_SOC_USB_AGECNTTHRSH, 0x00000080);
#else
		out_be32(non_ehci + FSL_SOC_USB_PRICTRL, 0x0000000c);
		out_be32(non_ehci + FSL_SOC_USB_AGECNTTHRSH, 0x00000040);
#endif
		out_be32(non_ehci + FSL_SOC_USB_SICTRL, 0x00000001);
	}

	return 0;
}

/* called after powerup, by probe or system-pm "wakeup" */
static int ehci_fsl_reinit(struct ehci_hcd *ehci)
{
	if (ehci_fsl_usb_setup(ehci))
		return -EINVAL;
	ehci_port_power(ehci, 0);

	return 0;
}

/* called during probe() after chip reset completes */
static int ehci_fsl_setup(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int retval;
	struct fsl_usb2_platform_data *pdata;
	struct device *dev;

	dev = hcd->self.controller;
	pdata = hcd->self.controller->platform_data;
	ehci->big_endian_desc = pdata->big_endian_desc;
	ehci->big_endian_mmio = pdata->big_endian_mmio;

	/* EHCI registers start at offset 0x100 */
	ehci->caps = hcd->regs + 0x100;

	hcd->has_tt = 1;

	retval = ehci_setup(hcd);
	if (retval)
		return retval;

	if (of_device_is_compatible(dev->parent->of_node,
				    "fsl,mpc5121-usb2-dr")) {
		/*
		 * set SBUSCFG:AHBBRST so that control msgs don't
		 * fail when doing heavy PATA writes.
		 */
		ehci_writel(ehci, SBUSCFG_INCR8,
			    hcd->regs + FSL_SOC_USB_SBUSCFG);
	}

	retval = ehci_fsl_reinit(ehci);
	return retval;
}

struct ehci_fsl {
	struct ehci_hcd	ehci;

#ifdef CONFIG_PM
	/* Saved USB PHY settings, need to restore after deep sleep. */
	u32 usb_ctrl;
#endif
};

#ifdef CONFIG_PM

#ifdef CONFIG_PPC_MPC512x
static int ehci_fsl_mpc512x_drv_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	struct fsl_usb2_platform_data *pdata = dev->platform_data;
	u32 tmp;

#ifdef DEBUG
	u32 mode = ehci_readl(ehci, hcd->regs + FSL_SOC_USB_USBMODE);
	mode &= USBMODE_CM_MASK;
	tmp = ehci_readl(ehci, hcd->regs + 0x140);	/* usbcmd */

	dev_dbg(dev, "suspend=%d already_suspended=%d "
		"mode=%d  usbcmd %08x\n", pdata->suspended,
		pdata->already_suspended, mode, tmp);
#endif

	/*
	 * If the controller is already suspended, then this must be a
	 * PM suspend.  Remember this fact, so that we will leave the
	 * controller suspended at PM resume time.
	 */
	if (pdata->suspended) {
		dev_dbg(dev, "already suspended, leaving early\n");
		pdata->already_suspended = 1;
		return 0;
	}

	dev_dbg(dev, "suspending...\n");

	ehci->rh_state = EHCI_RH_SUSPENDED;
	dev->power.power_state = PMSG_SUSPEND;

	/* ignore non-host interrupts */
	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

	/* stop the controller */
	tmp = ehci_readl(ehci, &ehci->regs->command);
	tmp &= ~CMD_RUN;
	ehci_writel(ehci, tmp, &ehci->regs->command);

	/* save EHCI registers */
	pdata->pm_command = ehci_readl(ehci, &ehci->regs->command);
	pdata->pm_command &= ~CMD_RUN;
	pdata->pm_status  = ehci_readl(ehci, &ehci->regs->status);
	pdata->pm_intr_enable  = ehci_readl(ehci, &ehci->regs->intr_enable);
	pdata->pm_frame_index  = ehci_readl(ehci, &ehci->regs->frame_index);
	pdata->pm_segment  = ehci_readl(ehci, &ehci->regs->segment);
	pdata->pm_frame_list  = ehci_readl(ehci, &ehci->regs->frame_list);
	pdata->pm_async_next  = ehci_readl(ehci, &ehci->regs->async_next);
	pdata->pm_configured_flag  =
		ehci_readl(ehci, &ehci->regs->configured_flag);
	pdata->pm_portsc = ehci_readl(ehci, &ehci->regs->port_status[0]);
	pdata->pm_usbgenctrl = ehci_readl(ehci,
					  hcd->regs + FSL_SOC_USB_USBGENCTRL);

	/* clear the W1C bits */
	pdata->pm_portsc &= cpu_to_hc32(ehci, ~PORT_RWC_BITS);

	pdata->suspended = 1;

	/* clear PP to cut power to the port */
	tmp = ehci_readl(ehci, &ehci->regs->port_status[0]);
	tmp &= ~PORT_POWER;
	ehci_writel(ehci, tmp, &ehci->regs->port_status[0]);

	return 0;
}

static int ehci_fsl_mpc512x_drv_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	struct fsl_usb2_platform_data *pdata = dev->platform_data;
	u32 tmp;

	dev_dbg(dev, "suspend=%d already_suspended=%d\n",
		pdata->suspended, pdata->already_suspended);

	/*
	 * If the controller was already suspended at suspend time,
	 * then don't resume it now.
	 */
	if (pdata->already_suspended) {
		dev_dbg(dev, "already suspended, leaving early\n");
		pdata->already_suspended = 0;
		return 0;
	}

	if (!pdata->suspended) {
		dev_dbg(dev, "not suspended, leaving early\n");
		return 0;
	}

	pdata->suspended = 0;

	dev_dbg(dev, "resuming...\n");

	/* set host mode */
	tmp = USBMODE_CM_HOST | (pdata->es ? USBMODE_ES : 0);
	ehci_writel(ehci, tmp, hcd->regs + FSL_SOC_USB_USBMODE);

	ehci_writel(ehci, pdata->pm_usbgenctrl,
		    hcd->regs + FSL_SOC_USB_USBGENCTRL);
	ehci_writel(ehci, ISIPHYCTRL_PXE | ISIPHYCTRL_PHYE,
		    hcd->regs + FSL_SOC_USB_ISIPHYCTRL);

	ehci_writel(ehci, SBUSCFG_INCR8, hcd->regs + FSL_SOC_USB_SBUSCFG);

	/* restore EHCI registers */
	ehci_writel(ehci, pdata->pm_command, &ehci->regs->command);
	ehci_writel(ehci, pdata->pm_intr_enable, &ehci->regs->intr_enable);
	ehci_writel(ehci, pdata->pm_frame_index, &ehci->regs->frame_index);
	ehci_writel(ehci, pdata->pm_segment, &ehci->regs->segment);
	ehci_writel(ehci, pdata->pm_frame_list, &ehci->regs->frame_list);
	ehci_writel(ehci, pdata->pm_async_next, &ehci->regs->async_next);
	ehci_writel(ehci, pdata->pm_configured_flag,
		    &ehci->regs->configured_flag);
	ehci_writel(ehci, pdata->pm_portsc, &ehci->regs->port_status[0]);

	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	ehci->rh_state = EHCI_RH_RUNNING;
	dev->power.power_state = PMSG_ON;

	tmp = ehci_readl(ehci, &ehci->regs->command);
	tmp |= CMD_RUN;
	ehci_writel(ehci, tmp, &ehci->regs->command);

	usb_hcd_resume_root_hub(hcd);

	return 0;
}
#else
static inline int ehci_fsl_mpc512x_drv_suspend(struct device *dev)
{
	return 0;
}

static inline int ehci_fsl_mpc512x_drv_resume(struct device *dev)
{
	return 0;
}
#endif /* CONFIG_PPC_MPC512x */

static struct ehci_fsl *hcd_to_ehci_fsl(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);

	return container_of(ehci, struct ehci_fsl, ehci);
}

static int ehci_fsl_drv_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct ehci_fsl *ehci_fsl = hcd_to_ehci_fsl(hcd);
	void __iomem *non_ehci = hcd->regs;

	if (of_device_is_compatible(dev->parent->of_node,
				    "fsl,mpc5121-usb2-dr")) {
		return ehci_fsl_mpc512x_drv_suspend(dev);
	}

	ehci_prepare_ports_for_controller_suspend(hcd_to_ehci(hcd),
			device_may_wakeup(dev));
	if (!fsl_deep_sleep())
		return 0;

	ehci_fsl->usb_ctrl = in_be32(non_ehci + FSL_SOC_USB_CTRL);
	return 0;
}

static int ehci_fsl_drv_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct ehci_fsl *ehci_fsl = hcd_to_ehci_fsl(hcd);
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	void __iomem *non_ehci = hcd->regs;

	if (of_device_is_compatible(dev->parent->of_node,
				    "fsl,mpc5121-usb2-dr")) {
		return ehci_fsl_mpc512x_drv_resume(dev);
	}

	ehci_prepare_ports_for_controller_resume(ehci);
	if (!fsl_deep_sleep())
		return 0;

	usb_root_hub_lost_power(hcd->self.root_hub);

	/* Restore USB PHY settings and enable the controller. */
	out_be32(non_ehci + FSL_SOC_USB_CTRL, ehci_fsl->usb_ctrl);

	ehci_reset(ehci);
	ehci_fsl_reinit(ehci);

	return 0;
}

static int ehci_fsl_drv_restore(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);

	usb_root_hub_lost_power(hcd->self.root_hub);
	return 0;
}

static struct dev_pm_ops ehci_fsl_pm_ops = {
	.suspend = ehci_fsl_drv_suspend,
	.resume = ehci_fsl_drv_resume,
	.restore = ehci_fsl_drv_restore,
};

#define EHCI_FSL_PM_OPS		(&ehci_fsl_pm_ops)
#else
#define EHCI_FSL_PM_OPS		NULL
#endif /* CONFIG_PM */

#ifdef CONFIG_USB_OTG
static int ehci_start_port_reset(struct usb_hcd *hcd, unsigned port)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	u32 status;

	if (!port)
		return -EINVAL;

	port--;

	/* start port reset before HNP protocol time out */
	status = readl(&ehci->regs->port_status[port]);
	if (!(status & PORT_CONNECT))
		return -ENODEV;

	/* khubd will finish the reset later */
	if (ehci_is_TDI(ehci)) {
		writel(PORT_RESET |
		       (status & ~(PORT_CSC | PORT_PEC | PORT_OCC)),
		       &ehci->regs->port_status[port]);
	} else {
		writel(PORT_RESET, &ehci->regs->port_status[port]);
	}

	return 0;
}
#else
#define ehci_start_port_reset	NULL
#endif /* CONFIG_USB_OTG */


static const struct hc_driver ehci_fsl_hc_driver = {
	.description = hcd_name,
	.product_desc = "Freescale On-Chip EHCI Host Controller",
	.hcd_priv_size = sizeof(struct ehci_fsl),

	/*
	 * generic hardware linkage
	 */
	.irq = ehci_irq,
	.flags = HCD_USB2 | HCD_MEMORY,

	/*
	 * basic lifecycle operations
	 */
	.reset = ehci_fsl_setup,
	.start = ehci_run,
	.stop = ehci_stop,
	.shutdown = ehci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue = ehci_urb_enqueue,
	.urb_dequeue = ehci_urb_dequeue,
	.endpoint_disable = ehci_endpoint_disable,
	.endpoint_reset = ehci_endpoint_reset,

	/*
	 * scheduling support
	 */
	.get_frame_number = ehci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data = ehci_hub_status_data,
	.hub_control = ehci_hub_control,
	.bus_suspend = ehci_bus_suspend,
	.bus_resume = ehci_bus_resume,
	.start_port_reset = ehci_start_port_reset,
	.relinquish_port = ehci_relinquish_port,
	.port_handed_over = ehci_port_handed_over,

	.clear_tt_buffer_complete = ehci_clear_tt_buffer_complete,
};

static int ehci_fsl_drv_probe(struct platform_device *pdev)
{
	if (usb_disabled())
		return -ENODEV;

	/* FIXME we only want one one probe() not two */
	return usb_hcd_fsl_probe(&ehci_fsl_hc_driver, pdev);
}

static int ehci_fsl_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	/* FIXME we only want one one remove() not two */
	usb_hcd_fsl_remove(hcd, pdev);
	return 0;
}

MODULE_ALIAS("platform:fsl-ehci");

static struct platform_driver ehci_fsl_driver = {
	.probe = ehci_fsl_drv_probe,
	.remove = ehci_fsl_drv_remove,
	.shutdown = usb_hcd_platform_shutdown,
	.driver = {
		.name = "fsl-ehci",
		.pm = EHCI_FSL_PM_OPS,
	},
};
