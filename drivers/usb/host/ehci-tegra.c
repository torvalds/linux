/*
 * EHCI-compliant USB host controller driver for NVIDIA Tegra SoCs
 *
 * Copyright (C) 2010 Google, Inc.
 * Copyright (C) 2009 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/usb/otg.h>
#include <mach/usb_phy.h>

#define TEGRA_USB_USBCMD_REG_OFFSET		0x140
#define TEGRA_USB_USBCMD_RESET			(1 << 1)
#define TEGRA_USB_USBMODE_REG_OFFSET		0x1a8
#define TEGRA_USB_USBMODE_HOST			(3 << 0)
#define TEGRA_USB_PHY_WAKEUP_REG_OFFSET		0x408
#define TEGRA_USB_ID_INT_ENABLE			(1 << 0)
#define TEGRA_USB_ID_INT_STATUS			(1 << 1)
#define TEGRA_USB_ID_PIN_STATUS			(1 << 2)
#define TEGRA_USB_ID_PIN_WAKEUP_ENABLE		(1 << 6)

struct tegra_ehci_hcd {
	struct ehci_hcd *ehci;
	struct work_struct work;
	struct tegra_usb_phy *phy;
	struct clk *clk;
	struct otg_transceiver *transceiver;
	int host_reinited;
	int host_resumed;
};

static void tegra_ehci_power_up(struct usb_hcd *hcd)
{
	struct tegra_ehci_hcd *tegra = dev_get_drvdata(hcd->self.controller);

	clk_enable(tegra->clk);
	tegra_usb_phy_power_on(tegra->phy);
	tegra->host_resumed = 1;
}

static void tegra_ehci_power_down(struct usb_hcd *hcd)
{
	struct tegra_ehci_hcd *tegra = dev_get_drvdata(hcd->self.controller);

	tegra_usb_phy_power_off(tegra->phy);
	clk_disable(tegra->clk);
	tegra->host_resumed = 0;
}

static int tegra_ehci_hub_control(
	struct usb_hcd	*hcd,
	u16		typeReq,
	u16		wValue,
	u16		wIndex,
	char		*buf,
	u16		wLength
)
{
	struct tegra_ehci_hcd *tegra = dev_get_drvdata(hcd->self.controller);
	struct ehci_hcd	*ehci = hcd_to_ehci(hcd);
	u32 __iomem	*status_reg;
	u32		temp;
	unsigned long	flags;
	int		retval = 0;

	status_reg = &ehci->regs->port_status[(wIndex & 0xff) - 1];

	/* if hardware is not accessable then don't read the registers */
	if (!test_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags)) {
		if (buf)
			memset(buf, 0, wLength);
		return retval;
	}

	/*
	 * In ehci_hub_control() for USB_PORT_FEAT_ENABLE clears the other bits
	 * that are write on clear, by writing back the register read value, so
	 * USB_PORT_FEAT_ENABLE is handled by masking the set on clear bits
	 */
	if ((typeReq == ClearPortFeature) && (wValue == USB_PORT_FEAT_ENABLE)) {
		spin_lock_irqsave(&ehci->lock, flags);
		temp = ehci_readl(ehci, status_reg);
		ehci_writel(ehci, (temp & ~PORT_RWC_BITS) & ~PORT_PE, status_reg);
		spin_unlock_irqrestore(&ehci->lock, flags);
		return retval;
	}

	/* Handle the hub control events here */
	retval = ehci_hub_control(hcd, typeReq, wValue, wIndex, buf, wLength);

	/*
	 * Power down the USB phy when there is no port connection and all
	 * HUB events are cleared by checking the lower four bits
	 * (PORT_CONNECT | PORT_CSC | PORT_PE | PORT_PEC)
	 */
	if (tegra->transceiver && tegra->transceiver->state == OTG_STATE_A_SUSPEND) {
		temp = ehci_readl(ehci, status_reg);
		if (!(temp & (PORT_CONNECT | PORT_CSC | PORT_PE | PORT_PEC))
			&& tegra->host_reinited) {
			/* indicate hcd flags, that hardware is not accessable now */
			clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
			tegra_ehci_power_down(hcd);
			tegra->host_reinited = 0;
		}
	}
	return retval;
}

static void tegra_ehci_restart(struct usb_hcd *hcd)
{
	unsigned int temp;
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);

	/* Set to Host mode by setting bit 0-1 of USB device mode register */
	temp = readl(hcd->regs + TEGRA_USB_USBMODE_REG_OFFSET);
	writel((temp | TEGRA_USB_USBMODE_HOST),
		(hcd->regs + TEGRA_USB_USBMODE_REG_OFFSET));

	/* reset the ehci controller */
	ehci->controller_resets_phy = 0;
	ehci_reset(ehci);
	ehci->controller_resets_phy = 1;
	/* setup the frame list and Async q heads */
	ehci_writel(ehci, ehci->periodic_dma, &ehci->regs->frame_list);
	ehci_writel(ehci, (u32)ehci->async->qh_dma, &ehci->regs->async_next);
	/* setup the command register and set the controller in RUN mode */
	ehci->command &= ~(CMD_LRESET|CMD_IAAD|CMD_PSE|CMD_ASE|CMD_RESET);
	ehci->command |= CMD_RUN;
	ehci_writel(ehci, ehci->command, &ehci->regs->command);

	down_write(&ehci_cf_port_reset_rwsem);
	hcd->state = HC_STATE_RUNNING;
	ehci_writel(ehci, FLAG_CF, &ehci->regs->configured_flag);
	/* flush posted writes */
	ehci_readl(ehci, &ehci->regs->command);
	up_write(&ehci_cf_port_reset_rwsem);

	/* Turn On Interrupts */
	ehci_writel(ehci, INTR_MASK, &ehci->regs->intr_enable);
}

static int tegra_ehci_reset(struct usb_hcd *hcd)
{
	unsigned long temp;
	int usec = 250*1000; /* see ehci_reset */

	temp = readl(hcd->regs + TEGRA_USB_USBCMD_REG_OFFSET);
	temp |= TEGRA_USB_USBCMD_RESET;
	writel(temp, hcd->regs + TEGRA_USB_USBCMD_REG_OFFSET);

	do {
		temp = readl(hcd->regs + TEGRA_USB_USBCMD_REG_OFFSET);
		if (!(temp & TEGRA_USB_USBCMD_RESET))
			break;
		udelay(1);
		usec--;
	} while (usec);

	if (!usec)
		return -ETIMEDOUT;

	return 0;
}

static void tegra_ehci_shutdown(struct usb_hcd *hcd)
{
	struct tegra_ehci_hcd *tegra = dev_get_drvdata(hcd->self.controller);
	/* ehci_shutdown touches the USB controller registers, make sure
	 * controller has clocks to it */
	if (!tegra->host_resumed)
		tegra_ehci_power_up(hcd);

	/* call ehci shut down */
	ehci_shutdown(hcd);

	/* we are ready to shut down, powerdown the phy */
	tegra_ehci_power_down(hcd);
}

/*
 * Work thread function for handling the USB power sequence.
 *
 * This work thread is created to avoid the pre-emption from the ISR context.
 * USB Power Rail and Vbus are controlled based on the USB cable connection.
 * USB Power rail function and VBUS control function cannot be called from ISR
 * as the PMU uses I2C driver, that waits on semaphore during the I2C transaction
 * this will cause the pre-emption if called in ISR.
 */
static void tegra_ehci_irq_work(struct work_struct *irq_work)
{
	struct tegra_ehci_hcd *tegra = container_of(irq_work, struct tegra_ehci_hcd, work);
	struct usb_hcd *hcd = ehci_to_hcd(tegra->ehci);

	if (tegra->transceiver) {
		if (tegra->transceiver->state == OTG_STATE_A_HOST &&
			    !tegra->host_reinited) {
			tegra->host_reinited = 1;
			tegra_ehci_power_up(hcd);
			tegra_ehci_restart(hcd);
		} else if (tegra->transceiver->state == OTG_STATE_A_SUSPEND &&
			    tegra->host_reinited) {
			tegra_ehci_power_down(hcd);
			tegra->host_reinited = 0;
		}
	}
}

static irqreturn_t tegra_ehci_irq(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	struct tegra_ehci_hcd *tegra = dev_get_drvdata(hcd->self.controller);
	unsigned long status;

	spin_lock(&ehci->lock);

	if (tegra->transceiver) {
		if (tegra->transceiver->state == OTG_STATE_A_HOST) {
			if (!tegra->host_reinited)
				schedule_work(&tegra->work);
		} else if (tegra->transceiver->state == OTG_STATE_A_SUSPEND) {
			if (!tegra->host_reinited) {
				spin_unlock(&ehci->lock);
				return IRQ_HANDLED;
			} else {
				schedule_work(&tegra->work);
			}
		} else {
			spin_unlock(&ehci->lock);
			return IRQ_HANDLED;
		}
	} else {
		/* read otgsc register for ID pin status change */
		status = readl(hcd->regs + TEGRA_USB_PHY_WAKEUP_REG_OFFSET);
		writel(status, (hcd->regs + TEGRA_USB_PHY_WAKEUP_REG_OFFSET));

		/* Check if there is any ID pin interrupt */
		if (status & TEGRA_USB_ID_INT_STATUS)
			schedule_work(&tegra->work);
	}

	spin_unlock(&ehci->lock);
	return ehci_irq(hcd);
}

static int tegra_ehci_setup(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int retval;

	/* EHCI registers start at offset 0x100 */
	ehci->caps = hcd->regs + 0x100;
	ehci->regs = hcd->regs + 0x100 +
		HC_LENGTH(readl(&ehci->caps->hc_capbase));

	dbg_hcs_params(ehci, "reset");
	dbg_hcc_params(ehci, "reset");

	/* cache this readonly data; minimize chip reads */
	ehci->hcs_params = readl(&ehci->caps->hcs_params);

	retval = ehci_halt(ehci);
	if (retval)
		return retval;

	/* data structure init */
	retval = ehci_init(hcd);
	if (retval)
		return retval;

	hcd->has_tt = 1;
	ehci->sbrn = 0x20;

	ehci_reset(ehci);

	/*
	 * Resetting the controller has the side effect of resetting the PHY.
	 * So, never reset the controller after the calling
	 * tegra_ehci_reinit API.
	 */
	ehci->controller_resets_phy = 1;

	ehci_port_power(ehci, 0);
	return retval;
}


static int tegra_ehci_bus_suspend(struct usb_hcd *hcd)
{
	struct tegra_ehci_hcd *tegra = dev_get_drvdata(hcd->self.controller);
	int error_status = 0;

	/* we are not in host mode, return */
	if (tegra->transceiver && tegra->transceiver->state != OTG_STATE_A_HOST)
		return 0;

	if (tegra->host_resumed) {
		error_status = ehci_bus_suspend(hcd);
		if (!error_status)
			tegra_ehci_power_down(hcd);
	}

	return error_status;
}

static int tegra_ehci_bus_resume(struct usb_hcd *hcd)
{
	struct tegra_ehci_hcd *tegra = dev_get_drvdata(hcd->self.controller);

	if (tegra->transceiver && tegra->transceiver->state != OTG_STATE_A_HOST)
		return 0;

	if (!tegra->host_resumed)
		tegra_ehci_power_up(hcd);

	return ehci_bus_resume(hcd);
}

static const struct hc_driver tegra_ehci_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "Tegra EHCI Host Controller",
	.hcd_priv_size		= sizeof(struct ehci_hcd),

	.flags			= HCD_USB2,

	.reset			= tegra_ehci_setup,
	.irq			= tegra_ehci_irq,

	.start			= ehci_run,
	.stop			= ehci_stop,
	.shutdown		= tegra_ehci_shutdown,
	.urb_enqueue		= ehci_urb_enqueue,
	.urb_dequeue		= ehci_urb_dequeue,
	.endpoint_disable	= ehci_endpoint_disable,
	.endpoint_reset		= ehci_endpoint_reset,
	.get_frame_number	= ehci_get_frame,
	.hub_status_data	= ehci_hub_status_data,
	.hub_control		= tegra_ehci_hub_control,
	.clear_tt_buffer_complete = ehci_clear_tt_buffer_complete,
#ifdef CONFIG_PM
	.bus_suspend		= tegra_ehci_bus_suspend,
	.bus_resume		= tegra_ehci_bus_resume,
#endif
	.relinquish_port	= ehci_relinquish_port,
	.port_handed_over	= ehci_port_handed_over,
};

static int tegra_ehci_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct usb_hcd *hcd;
	struct ehci_hcd *ehci;
	struct tegra_ehci_hcd *tegra;
	int err = 0;
	int irq;
	unsigned int temp;
	int instance = pdev->id;

	tegra = kzalloc(sizeof(struct tegra_ehci_hcd), GFP_KERNEL);
	if (!tegra)
		return -ENOMEM;

	hcd = usb_create_hcd(&tegra_ehci_hc_driver, &pdev->dev,
					dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Unable to create HCD\n");
		err = -ENOMEM;
		goto fail_hcd;
	}

	platform_set_drvdata(pdev, tegra);
	INIT_WORK(&tegra->work, tegra_ehci_irq_work);

	tegra->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(tegra->clk)) {
		dev_err(&pdev->dev, "Can't get ehci clock\n");
		err = PTR_ERR(tegra->clk);
		goto fail_clk;
	}

	err = clk_enable(tegra->clk);
	if (err)
		goto fail_clken;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get I/O memory\n");
		err = -ENXIO;
		goto fail_io;
	}
	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->regs = ioremap(res->start, resource_size(res));
	if (!hcd->regs) {
		dev_err(&pdev->dev, "Failed to remap I/O memory\n");
		err = -ENOMEM;
		goto fail_io;
	}

	tegra->phy = tegra_usb_phy_open(instance, hcd->regs);
	if (IS_ERR(tegra->phy)) {
		dev_err(&pdev->dev, "Failed to open USB phy\n");
		err = -ENXIO;
		goto fail_phy;
	}

	tegra_usb_phy_power_on(tegra->phy);

	err = tegra_ehci_reset(hcd);
	if (err) {
		dev_err(&pdev->dev, "Failed to reset controller\n");
		goto fail;
	}

	tegra->host_resumed = 1;

	/* Set to Host mode by setting bit 0-1 of USB device mode register */
	temp = readl(hcd->regs + TEGRA_USB_USBMODE_REG_OFFSET);
	writel((temp | TEGRA_USB_USBMODE_HOST),
			(hcd->regs + TEGRA_USB_USBMODE_REG_OFFSET));
	temp = readl(hcd->regs + TEGRA_USB_USBMODE_REG_OFFSET);

	irq = platform_get_irq(pdev, 0);
	if (!irq) {
		dev_err(&pdev->dev, "Failed to get IRQ\n");
		err = -ENODEV;
		goto fail;
	}

	set_irq_flags(irq, IRQF_VALID);

	ehci = hcd_to_ehci(hcd);
	tegra->ehci = ehci;

#ifdef CONFIG_USB_OTG_UTILS
	if (instance == 0)
		tegra->transceiver = otg_get_transceiver();
#endif

	err = usb_add_hcd(hcd, irq, IRQF_DISABLED | IRQF_SHARED);
	if (err != 0) {
		dev_err(&pdev->dev, "Failed to add USB HCD\n");
		goto fail;
	}

	if (tegra->transceiver) {
		otg_set_host(tegra->transceiver, (struct usb_bus *)hcd);

		/*
		 * Stop the controller and power down the phy, OTG will
		 * start the host driver based on the ID pin
		 * detection
		 */
		ehci_halt(ehci);

		/* reset the host and put the controller in idle mode */
		temp = ehci_readl(ehci, &ehci->regs->command);
		temp |= CMD_RESET;
		ehci_writel(ehci, temp, &ehci->regs->command);

		temp = readl(hcd->regs + TEGRA_USB_USBMODE_REG_OFFSET);
		writel((temp & ~TEGRA_USB_USBMODE_HOST),
			(hcd->regs + TEGRA_USB_USBMODE_REG_OFFSET));

		/* indicate hcd flags, that hardware is not accessable now in host mode*/
		clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

		tegra_ehci_power_down(hcd);
		tegra->host_reinited = 0;
	} else if (instance == 0) {
		/* enable the cable ID interrupt */
		temp = readl(hcd->regs + TEGRA_USB_PHY_WAKEUP_REG_OFFSET);
		temp |= TEGRA_USB_ID_INT_ENABLE;
		temp |= TEGRA_USB_ID_PIN_WAKEUP_ENABLE;
		writel(temp, (hcd->regs + TEGRA_USB_PHY_WAKEUP_REG_OFFSET));

		/* Check if we detect any device connected */
		if (temp & TEGRA_USB_ID_PIN_STATUS)
			tegra_ehci_power_down(hcd);
		else
			tegra_ehci_power_up(hcd);
	}

	return err;

fail:
	tegra_usb_phy_close(tegra->phy);
fail_phy:
	iounmap(hcd->regs);
fail_io:
	clk_disable(tegra->clk);
fail_clken:
	clk_put(tegra->clk);
fail_clk:
	usb_put_hcd(hcd);
fail_hcd:
	kfree(tegra);
	return err;
}

#ifdef CONFIG_PM
static int tegra_ehci_resume(struct platform_device *pdev)
{
	struct tegra_ehci_hcd *tegra = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = ehci_to_hcd(tegra->ehci);

	if (!tegra->host_resumed) {
		tegra_ehci_power_up(hcd);
		tegra_ehci_restart(hcd);
	}

	return 0;
}

static int tegra_ehci_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct tegra_ehci_hcd *tegra = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = ehci_to_hcd(tegra->ehci);

	if (tegra->transceiver) {
		if (tegra->transceiver->state != OTG_STATE_A_HOST) {
			/* we are not in host mode, return */
			return 0;
		} else {
			tegra->host_reinited = 0;
			ehci_halt(tegra->ehci);
			/* indicate hcd flags, that hardware is not accessable now */
			clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
		}
	}

	if (tegra->host_resumed)
		tegra_ehci_power_down(hcd);

	return 0;
}
#endif

static int tegra_ehci_remove(struct platform_device *pdev)
{
	struct tegra_ehci_hcd *tegra = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = ehci_to_hcd(tegra->ehci);

	if (tegra == NULL || hcd == NULL)
		return -EINVAL;

	usb_remove_hcd(hcd);
	tegra_usb_phy_close(tegra->phy);
	iounmap(hcd->regs);

	clk_disable(tegra->clk);
	clk_put(tegra->clk);
	usb_put_hcd(hcd);

	kfree(tegra);
	return 0;
}

static void tegra_ehci_hcd_shutdown(struct platform_device *pdev)
{
	struct tegra_ehci_hcd *tegra = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = ehci_to_hcd(tegra->ehci);

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}

static struct platform_driver tegra_ehci_driver = {
	.probe		= tegra_ehci_probe,
	.remove		= tegra_ehci_remove,
#ifdef CONFIG_PM
	.suspend	= tegra_ehci_suspend,
	.resume		= tegra_ehci_resume,
#endif
	.shutdown	= tegra_ehci_hcd_shutdown,
	.driver		= {
		.name	= "tegra-ehci",
	}
};
